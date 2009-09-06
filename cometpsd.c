/*
cometpsd - Simple comet pub/sub server.

(MIT license)

Copyright (c) 2009 Rasmus Andersson <rasmus@notion.se>
 
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
 
The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.
 
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
#include <sys/types.h>

#include <sys/queue.h>
#include <sys/tree.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <err.h>

#include <event.h>
#include <evhttp.h>

#include "yconf.h"

#define CPS_LOG_ERR 0
#define CPS_LOG_WARN 1
#define CPS_LOG_INFO 2
#define CPS_LOG_DEBUG 3

#define MAX_CLIENT_BUFSIZ	(1000 * 1000)

#define cps_warn(fmt, ...) \
	warn("%s:%d (%s) " fmt, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)

#define cps_log(CL, L, fmt, ...)\
	do { if ((CL) >= (L))\
		fprintf(stderr,\
		"%s " fmt "\n", (L>CPS_LOG_INFO ?"D":(L>CPS_LOG_WARN ?"I":(L>CPS_LOG_ERR ?"W":"E"))), ##__VA_ARGS__);\
	} while(0)

#define cps_server_log(server, L, fmt, ...)\
	cps_log((server)->log_level, L, "[%s - -] " fmt, (server)->name, ##__VA_ARGS__)

#define cps_channel_log(ch, L, fmt, ...)\
	cps_log((ch)->log_level, L, "[%s \"%s\" -] " fmt,\
		(ch)->server ? (ch)->server->name : "-", (ch)->name, ##__VA_ARGS__)

#define cps_sub_log(sub, L, fmt, ...)\
	cps_log((sub)->channel->log_level, L, "[%s \"%s\" %s:%d] " fmt,\
		(sub)->channel->server ? (sub)->channel->server->name : "-", (sub)->channel->name,\
		(sub)->req ? (sub)->req->remote_host : "?", (sub)->req ? (sub)->req->remote_port : 0,\
		##__VA_ARGS__)

#define cps_server_log_err(server, fmt, ...)   cps_server_log(server, CPS_LOG_ERR, fmt, ##__VA_ARGS__)
#define cps_server_log_warn(server, fmt, ...)  cps_server_log(server, CPS_LOG_WARN, fmt, ##__VA_ARGS__)
#define cps_server_log_info(server, fmt, ...)  cps_server_log(server, CPS_LOG_INFO, fmt, ##__VA_ARGS__)
#define cps_server_log_debug(server, fmt, ...) cps_server_log(server, CPS_LOG_DEBUG, fmt, ##__VA_ARGS__)

#define cps_channel_log_err(ch, fmt, ...)   cps_channel_log(ch, CPS_LOG_ERR, fmt, ##__VA_ARGS__)
#define cps_channel_log_warn(ch, fmt, ...)  cps_channel_log(ch, CPS_LOG_WARN, fmt, ##__VA_ARGS__)
#define cps_channel_log_info(ch, fmt, ...)  cps_channel_log(ch, CPS_LOG_INFO, fmt, ##__VA_ARGS__)
#define cps_channel_log_debug(ch, fmt, ...) cps_channel_log(ch, CPS_LOG_DEBUG, fmt, ##__VA_ARGS__)

#define cps_sub_log_err(sub, fmt, ...)   cps_sub_log(sub, CPS_LOG_ERR, fmt, ##__VA_ARGS__)
#define cps_sub_log_warn(sub, fmt, ...)  cps_sub_log(sub, CPS_LOG_WARN, fmt, ##__VA_ARGS__)
#define cps_sub_log_info(sub, fmt, ...)  cps_sub_log(sub, CPS_LOG_INFO, fmt, ##__VA_ARGS__)
#define cps_sub_log_debug(sub, fmt, ...) cps_sub_log(sub, CPS_LOG_DEBUG, fmt, ##__VA_ARGS__)

// fwd decl
struct cps_channel;
struct cps_server;

struct cps_sub {
	struct evhttp_request	*req;
	struct event		      ev;
	struct cps_channel    *channel;
	TAILQ_ENTRY(cps_sub)	next;
};
TAILQ_HEAD(cps_subs, cps_sub);

struct cps_channel {
	char *name;
	char *uri;
	char *pubkey;
	int log_level;
	struct cps_server *server;
	struct cps_subs subs;
	TAILQ_ENTRY(cps_channel) next;
};
TAILQ_HEAD(cps_channels, cps_channel);

struct cps_server {
	struct evhttp *http;
	struct cps_channels channels;
	char *name;
	char *channels_uri;
	int log_level;
	TAILQ_ENTRY(cps_server) next;
};
TAILQ_HEAD(cps_servers, cps_server);


typedef struct cps_channel cps_channel_t;
typedef struct cps_server cps_server_t;
typedef struct cps_sub cps_sub_t;

struct cps_servers g_servers;
int g_verbosity = 1;

#include "_4ksp.h" // const char g_4k_sp[4096]

static const char *_evhttp_peername(struct evhttp_connection *evcon) {
	static char buf[128];
	char *address;
	u_short port;
	evhttp_connection_get_peer(evcon, &address, &port);
	snprintf(buf, sizeof(buf), "%s:%d", address, port);
	return (buf);
}


static void _evbuffer_align4k(struct evbuffer *msg) {
	// 4k padding (using SP) needed for MSIE and Safari
	size_t datlen = 4096 - EVBUFFER_LENGTH(msg); //% 4096);
	if (datlen > 0)
		evbuffer_add(msg, g_4k_sp, datlen);
}


static void cps_sub_closed(struct evhttp_connection *evcon, void *_sub) {
	cps_sub_t *sub = (cps_sub_t *)_sub;
	cps_sub_log_info(sub, "unsubscribed");
	TAILQ_REMOVE(&sub->channel->subs, sub, next);
	free(sub);
}


static cps_sub_t *cps_sub_open(cps_channel_t *ch, struct evhttp_request *req) {
	cps_sub_t *sub;
	if (!(sub = calloc(1, sizeof(cps_sub_t))))
		return NULL;
	sub->req = req;
	sub->channel = ch;
	evhttp_connection_set_closecb(req->evcon, cps_sub_closed, sub);
	TAILQ_INSERT_TAIL(&ch->subs, sub, next);
	cps_sub_log_info(sub, "subscribed");
	return sub;
}


static void cps_sub_pub(struct cps_sub *sub, const char *sender, void *buf, size_t len) {
	struct evbuffer *msg;
	int n;
	
	n = EVBUFFER_LENGTH(sub->req->output_buffer);
	if (n >= MAX_CLIENT_BUFSIZ) {
		cps_sub_log_warn(sub, "bufsize >= maxbufsize -- dropping message (%llu) from %s",
			(unsigned long long)len, sender);
	}
	else {
		msg = evbuffer_new();
		evbuffer_add(msg, buf, len);
		cps_sub_log_debug(sub, "sending message(%llu)", (unsigned long long)len);
		_evbuffer_align4k(msg); ///< 4k alignment (using SP) needed for MSIE and Safari
		evhttp_send_reply_chunk(sub->req, msg);
		evbuffer_free(msg);
	}
}


static void cps_channel_pub(cps_channel_t *ch, const char *sender, void *buf, size_t len) {
	struct cps_sub *sub;
	int subcount = 0;
	
	cps_channel_log_info(ch, "publishing %llu bytes", (unsigned long long)len);
	
	TAILQ_FOREACH(sub, &ch->subs, next) {
		subcount++;
		cps_sub_pub(sub, sender, buf, len);
	}
	
	cps_channel_log_debug(ch, "published %llu bytes to %d subscribers",
		(unsigned long long)len, subcount);
}


void cps_channel_request_handler(struct evhttp_request *req, void *_channel) {
	cps_channel_t *ch = (cps_channel_t *)_channel;
	//cps_server_t *server = (cps_server_t *)_server;
	switch (req->type) {
	case EVHTTP_REQ_GET: {
		cps_channel_log_debug(ch, "GET %s from %s:%d", req->uri, req->remote_host, req->remote_port);
		cps_sub_open(ch, req);
		
		struct evbuffer *buf = evbuffer_new();
		evhttp_add_header(req->output_headers, "Content-Type", "text/html; charset=utf-8");
		evbuffer_add_printf(buf, "<!DOCTYPE html><html><head></head><body>\n");
		_evbuffer_align4k(buf);
		evhttp_send_reply_start(req, HTTP_OK, "OK");
		evhttp_send_reply_chunk(req, buf);
		evbuffer_free(buf);
		break;
	}
	case EVHTTP_REQ_POST: {
		cps_channel_log_debug(ch, "POST %s from %s:%d", req->uri, req->remote_host, req->remote_port);
		// publish-key
		if (ch->pubkey && strlen(ch->pubkey)) {
			const char *key = evhttp_find_header(req->input_headers, "X-CPS-Publish-Key");
			if (key == NULL) {
				cps_channel_log_warn(ch, "bad pubkey (missing) from %s:%d", req->remote_host, req->remote_port);
				evhttp_send_reply(req, 400, "Bad Request", NULL);
				return;
			}
			else if (strcmp(key, ch->pubkey) != 0) {
				cps_channel_log_warn(ch, "bad pubkey (mismatch) from %s:%d", req->remote_host, req->remote_port);
				evhttp_send_reply(req, 401, "Unauthorized", NULL);
				return;
			}
		}
		// publish
		cps_channel_pub(ch, _evhttp_peername(req->evcon), 
			EVBUFFER_DATA(req->input_buffer), EVBUFFER_LENGTH(req->input_buffer));
		// empty OK reply
		evhttp_send_reply(req, HTTP_NOCONTENT, "OK", NULL);
		break;
	}
	default:
		cps_channel_log_warn(ch, "bad request method from %s:%d", req->remote_host, req->remote_port);
		evhttp_send_reply(req, 405, "Method Not Allowed", NULL);
		break;
	}
}

void cps_server_request_handler(struct evhttp_request *req, void *_server) {
	cps_server_t *server = (cps_server_t *)_server;
	cps_server_log_debug(server, "unhandled request (404) for \"%s\" from %s:%d",
		req->uri, req->remote_host, req->remote_port);
	evhttp_send_reply(req, 404, "Not Found", NULL);
}


cps_server_t *cps_server_start(const char *address, int port, int log_level) {
	cps_server_t *server = calloc(1, sizeof(cps_server_t));
	
	server->log_level = log_level;
	server->http = evhttp_new(NULL);
	
	if (server->http == NULL) {
		cps_warn("failed to allocate http server");
		free(server);
		return NULL;
	}
	
	if (evhttp_bind_socket(server->http, address, port) == -1) {
		cps_warn("failed to bind http server to %s:%d", address, port);
		free(server->http);
		free(server);
		return NULL;
	}
	
	char *name = calloc(256, 1);
	snprintf(name, 255, "%s:%d", address, port);
	server->name = name;
	
	TAILQ_INIT(&server->channels);
	
	evhttp_set_gencb(server->http, cps_server_request_handler, server);
	
	cps_server_log_info(server, "server listening");
	
	return server;
}


void cps_sub_delete(cps_sub_t *sub) {
}


void cps_channel_delete(cps_channel_t *ch) {
	cps_sub_t *sub;
	TAILQ_FOREACH(sub, &ch->subs, next) {
		cps_sub_delete(sub);
		// hm... is this safe while traversing the tailq?
		//TAILQ_REMOVE(&sub->channel->subs, sub, next);
		free(sub);
	}
	evhttp_del_cb(ch->server->http, ch->uri);
	free(ch->name);
	free(ch->uri);
	if (ch->pubkey)
		free(ch->pubkey);
}


void cps_server_delete(cps_server_t *server) {
	cps_channel_t *ch;
	TAILQ_FOREACH(ch, &server->channels, next) {
		cps_channel_delete(ch);
		// hm... is this safe while traversing the tailq?
		//TAILQ_REMOVE(&sub->channel->subs, sub, next);
		free(ch);
	}
	free(server->name);
	free(server->channels_uri);
	evhttp_free(server->http);
}


cps_channel_t *cps_channel_find(cps_server_t *server, const char *name) {
	cps_channel_t *ch;
	TAILQ_FOREACH(ch, &server->channels, next) {
		if (ch->name[0] == name[0] && strcmp(ch->name, name) == 0)
			return ch;
	}
	return NULL;
}


cps_channel_t *cps_channel_open(cps_server_t *server, const char *name, 
	int client_limit, const char *pubkey, int log_level)
{
	cps_channel_t *channel;
	
	if (cps_channel_find(server, name ? name : "")) {
		cps_server_log_warn(server, "duplicate channels '%s' -- skipping channel", name);
		return NULL;
	}
	
	channel = calloc(1, sizeof(cps_channel_t));
	
	channel->name = name ? strdup(name) : "";
	channel->server = server;
	channel->log_level = log_level;
	
	if (pubkey && strlen(pubkey))
		channel->pubkey = strdup(pubkey);
	
	TAILQ_INIT(&channel->subs);
	
	char *uri = calloc(256, 1);
	snprintf(uri, 255, "%s%s", server->channels_uri ? server->channels_uri : "/channel/", name);
	channel->uri = uri;
	
	evhttp_set_cb(server->http, channel->uri, cps_channel_request_handler, channel);
	
	TAILQ_INSERT_TAIL(&server->channels, channel, next);
	
	cps_channel_log_info(channel, "channel opened at %s%s%s", channel->uri,
		channel->pubkey ? ", publish_key: " : "", channel->pubkey ? channel->pubkey : "");
	
	return channel;
}


void cps_channel_close(cps_channel_t *channel) { // todo ?
}

// ------------------------------------------------------------------------------------------
// signal handlers

static void _sigpipe_cb(int sig, short what, void *arg) {
}

static void _reload_cb(int sig, short what, void *config) {
	if (config)
		yconf_reload((yconf_t *)config);
	// todo: update state, delete/create/update servers and channels which was removed/added/modified.
	cps_log(yconf_get_int(config, "logging/log_level", CPS_LOG_INFO), CPS_LOG_INFO, "config reloaded");
}

// ------------------------------------------------------------------------------------------
// main

void usage(const char *progname, bool full) {
	fprintf(stderr,
	"%s: [options]\n%s"
	"  -a <addr>    Address to bind on (defaults to 127.0.0.1).\n"
	"  -p <port>    Port number to listen on (defaults to 8080).\n"
	"  -c <channel> Channel name (defaults to \"default\").\n"
	"  -k <secret>  Only allow publishing of requests with this key in\n"
	"                the header field \"X-CPS-Publish-Key: <secret>\".\n"
	"  -f <file>    Read configuration from YAML file.\n"
	"  -v           Verbose (multiple times for more logging).\n"
	"  -s           Silent (multiple times for less logging).\n"
	,progname,(full ? "HTTP slow-response type comet server based on libevent.\n\noptions:\n" : ""));
	if (full) {
		fprintf(stderr,
	"\nConfiguration file:\n"
	"  The configuration file is a YAML file which can configure multiple server.\n\n"
	
	"  Example file:\n\n"
	
	"  servers:\n"
	"    - address: \"0.0.0.0\"\n"
	"      port: 8080\n"
	"      channels:\n"
	"        test:\n"
	"          publish_key: xyz\n"
	"        test2:\n"
	"          max_clients: 3\n"
	"    \n"
	"    - port: 1234\n"
	"      address: \"localhost\"\n"
	"      log_level: 2\n"
	"      channels: {a: {publish_key: xyz}, b: {}}\n"
	"\n"
	"By Rasmus Andersson <http://hunch.se/>, open source licensed under MIT.\n"
		);
	}
	else {
		fprintf(stderr, "\nsee %s -h for more details.\n", progname);
	}
}


int main(int argc, char **argv) {
	extern char		     *optarg;
	extern int		     optind;
	struct event	     pipe_ev, usr1_ev;
	int						     c, log_level = CPS_LOG_INFO;
	bool               configured_servers;
	yconf_t	           config;
	struct cps_servers servers;
	cps_server_t       *server;
	
	short			 http_port = 8080;
	char			 *http_addr = "127.0.0.1";
	const char *config_file = NULL;
	const char *pubkey = NULL;
	const char *channel_name = "default";

	while ((c = getopt(argc, argv, "hvsp:l:k:f:c:")) != -1) switch(c) {
		case 'v':
			log_level++;
			break;
		case 's':
			log_level--;
			break;
		case 'p':
			http_port = atoi(optarg);
			if (http_port == 0) {
				usage(argv[0], false);
				exit(1);
			}
			break;
		case 'l':
			http_addr = optarg;
			break;
		case 'k':
			pubkey = optarg;
			break;
		case 'f':
			config_file = optarg;
			break;
		case 'c':
			channel_name = optarg;
			break;
		case 'h':
			usage(argv[0], true);
			exit(1);
		default:
			usage(argv[0], false);
			exit(1);
	}
	argc -= optind;
	argv += optind;
	
	/* init libevent */
	event_init();
	
	// load configuration file
	if (config_file) {
		yconf_load(&config, config_file);
		log_level += 1 - (int)yconf_get_int(&config, "logging/log_level", (long long)log_level);
		//printf("config: servers/0/address => %s\n",
		//	yconf_get_str(&config, "servers/0/address", "?"));
		//printf("config: servers/1/channels/test2/max_clients => %lld\n",
		//	yconf_get_int(&config, "servers/1/channels/test2/max_clients", -1LL));
		signal_set(&usr1_ev, SIGUSR1, _reload_cb, (void *)&config);
		signal_add(&usr1_ev, NULL);
	}

	/* remove soft resource limits */
	struct rlimit rlim = { RLIM_INFINITY, RLIM_INFINITY };
	setrlimit(RLIMIT_NOFILE, &rlim);
	
	/* ignore SIGPIPE (broken connections) */
	signal_set(&pipe_ev, SIGPIPE, _sigpipe_cb, NULL);
	signal_add(&pipe_ev, NULL);
	
	TAILQ_INIT(&servers);
	
	#define yconf_map_foreach(config, map, knode, vnode)\
		for (yaml_node_pair_t *pair = (map)->data.mapping.pairs.start;\
			pair < (map)->data.mapping.pairs.top &&\
			(knode = (config)->document.nodes.start + pair->key - 1) &&\
			(vnode = (config)->document.nodes.start + pair->value - 1);\
			pair++)
	
	// start server(s) from config
	configured_servers = false;
	if (config_file) {
		yaml_node_t *srvs, *srv;
		// servers
		if ((srvs = yconf_find_node(&config, "servers", true)) && srvs->type == YAML_SEQUENCE_NODE) {
			yconf_list_foreach(&config, srvs, srv) {
				server = cps_server_start(
					yconf_get_str2(&config, srv, "address", http_addr),
					(int)yconf_get_int2(&config, srv, "port", http_port),
					(int)yconf_get_int2(&config, srv, "log_level", log_level)
				);
				
				// channels
				yaml_node_t *chnls, *chname, *chnl;
				if ((chnls = yconf_find_node2(&config, srv, "channels", true)) && chnls->type == YAML_MAPPING_NODE) {
					yconf_map_foreach(&config, chnls, chname, chnl) {
						cps_channel_open(server,
							(const char *)chname->data.scalar.value,
							(int)yconf_get_int2(&config, chnl, "max_clients", 0),
							yconf_get_str2(&config, chnl, "publish_key", NULL),
							(int)yconf_get_int2(&config, chnl, "log_level", log_level)
						);
					}
				}
				
				configured_servers = true;
			}
		}
	}
	
	// start server from args if no servers was configured in config
	if (!configured_servers) {
		server = cps_server_start(http_addr, http_port, log_level);
		TAILQ_INSERT_TAIL(&servers, server, next);
		cps_channel_open(server, channel_name, 0, pubkey, log_level);
	}
	
	event_dispatch();
	yconf_delete(&config);
	exit(0);
}
