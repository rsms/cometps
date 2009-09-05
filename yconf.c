#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h> // strcasecmp
#include <assert.h>

#include "yconf.h"

#define YCONF_PATH_SEP "/"


static void _parse_node(yconf_t *config, yaml_node_t *node) {
	printf("ENTER %s\n", __FUNCTION__);
	
	yaml_node_pair_t *pair = NULL;
	
	switch (node->type) {
		case YAML_SCALAR_NODE:
			printf("SCALAR %s => '%s'\n", node->tag, node->data.scalar.value);
			//if (!yaml_document_add_scalar(document_to, node->tag,
			//			node->data.scalar.value, node->data.scalar.length,
			//			node->data.scalar.style)) goto error;
			break;
		case YAML_SEQUENCE_NODE:
			printf("LIST %s => ...\n", node->tag);
			break;
		case YAML_MAPPING_NODE:
			printf("MAP %s => ...\n", node->tag);
			
			//yaml_char_t *anchor = NULL;
			//yaml_event_t event;
			//yaml_mark_t mark  = { 0, 0, 0 };
			//int implicit = (strcmp((char *)node->tag, YAML_DEFAULT_MAPPING_TAG) == 0);
			//MAPPING_START_EVENT_INIT(event, anchor, node->tag, implicit, node->data.mapping.style, mark, mark);
			
			for (pair = node->data.mapping.pairs.start; pair < node->data.mapping.pairs.top; pair++) {
				yaml_node_t *prev_key_node = config->currkey;
				config->currkey = config->document.nodes.start + pair->key - 1;
				yaml_node_t *value_node = config->document.nodes.start + pair->value - 1;
				
				if (config->currkey->type == YAML_SCALAR_NODE)
					printf("%s => ", config->currkey->data.scalar.value);
				else
					printf("? => ");
				
				_parse_node(config, value_node);
				config->currkey = prev_key_node;
			}
			break;
		default:
			assert(0 && "unknown node type");
			break;
	}
}


typedef int strncasecmp_func_t(const char *, const char *, size_t);


yaml_node_t *yconf_find_node_in_list(yconf_t *config, yaml_node_t *list, int index) {
	if (index < (list->data.sequence.items.top - list->data.sequence.items.start)) {
		//printf("access list[%d]\n", index);
		return config->document.nodes.start + (*(list->data.sequence.items.start + index)) - 1;
	}
	return NULL;
}


yaml_node_t *yconf_find_node_in_map(yconf_t *config, yaml_node_t *map, const char *keyname, bool case_sensitive) {
	yaml_node_pair_t *pair;
	size_t keyname_len = strlen(keyname);
	
	strncasecmp_func_t *_strncmp = case_sensitive ? strncmp : strncasecmp;
	
	for (pair = map->data.mapping.pairs.start; pair < map->data.mapping.pairs.top; pair++) {
		yaml_node_t *key_node = config->document.nodes.start + pair->key - 1;
		yaml_node_t *value_node = config->document.nodes.start + pair->value - 1;
		
		if (key_node->type == YAML_SCALAR_NODE && 
			_strncmp((const char *)key_node->data.scalar.value, keyname, keyname_len) == 0)
		{
			//printf("found node %s\n", key_node->data.scalar.value);
			return value_node;
		}
		//else if (key_node->type == YAML_SCALAR_NODE)
		//	printf("non-matching node %s\n", key_node->data.scalar.value);
	}
	
	return NULL;
}


yaml_node_t *yconf_find_node_in_coll(yconf_t *config, yaml_node_t *coll, const char *keyname, bool case_sensitive) {
	if (coll->type == YAML_MAPPING_NODE)
		return yconf_find_node_in_map(config, coll, keyname, case_sensitive);
	if (coll->type == YAML_SEQUENCE_NODE)
		return yconf_find_node_in_list(config, coll, atoi(keyname));
	
	//if (coll->type == YAML_SCALAR_NODE)
	//	printf("not coll but scalar: %s\n", coll->data.scalar.value);
	//else
	//	printf("coll->type = %d\n", coll->type);
	
	return NULL;
}


yaml_node_t *yconf_find_node2(yconf_t *config, yaml_node_t *node, const char *path, bool case_sensitive) {
	char *tokstate = NULL, *tok, pch[256];
	memcpy(pch, path, 255);
	pch[255] = 0;
	
	for (tok = strtok_r(pch, YCONF_PATH_SEP, &tokstate); tok; tok = strtok_r(NULL, YCONF_PATH_SEP, &tokstate)) {
		//printf("find token '%s'\n", tok);
		if (!(node = yconf_find_node_in_coll(config, node, tok, case_sensitive)))
			break;
	}
	
	return node;
}


yaml_node_t *yconf_find_node(yconf_t *config, const char *path, bool case_sensitive) {
	yaml_node_t *node = NULL;
	if (!(node = yaml_document_get_root_node(&config->document)))
		return NULL;
	return yconf_find_node2(config, node, path, case_sensitive);
}


static const char *_yconf_get_strfb = "";

const char *yconf_get_str2(yconf_t *config, yaml_node_t *node, const char *path, const char *fallback) {
	node = yconf_find_node2(config, node, path, 1);
	if (node && node->type == YAML_SCALAR_NODE)
		return (const char *)node->data.scalar.value;
	return fallback;
}

long long yconf_get_int2(yconf_t *config, yaml_node_t *node, const char *path, long long fallback) {
	if ((path = yconf_get_str2(config, node, path, _yconf_get_strfb)) == _yconf_get_strfb)
		return fallback;
	return atoll(path);
}

double yconf_get_float2(yconf_t *config, yaml_node_t *node, const char *path, double fallback) {
	if ((path = yconf_get_str2(config, node, path, _yconf_get_strfb)) == _yconf_get_strfb)
		return fallback;
	return atof(path);
}

bool yconf_get_bool2(yconf_t *config, yaml_node_t *node, const char *path, bool fallback) {
	if ((path = yconf_get_str2(config, node, path, _yconf_get_strfb)) == _yconf_get_strfb)
		return fallback;
	size_t len = strlen(path);
	if (len < 1)
		return false;
	if (path[0] == 'Y' || path[0] == 'y' || path[0] == 'T' || path[0] == 't') // y[es]|t[rue]
		return true;
	if (path[0] == 'N' || path[0] == 'n' || path[0] == 'F' || path[0] == 'f') // n[o]|f[alse]
		return false;
	if (path[0] == 'O' || path[0] == 'o') // o(n), o(f[f])
		return (path[0] == 'N' || path[0] == 'n') ? true : false;
	return atoi(path) ? true : false;
}

#define _ROOT_OR_RET_FALLBACK\
  yaml_node_t *node = NULL;\
	if (!(node = yaml_document_get_root_node(&config->document)))\
		return fallback

const char *yconf_get_str(yconf_t *config, const char *path, const char *fallback) {
  _ROOT_OR_RET_FALLBACK;
  return yconf_get_str2(config, node, path, fallback);
}

long long yconf_get_int(yconf_t *config, const char *path, long long fallback) {
  _ROOT_OR_RET_FALLBACK;
  return yconf_get_int2(config, node, path, fallback);
}

double yconf_get_float(yconf_t *config, const char *path, double fallback) {
  _ROOT_OR_RET_FALLBACK;
  return yconf_get_float2(config, node, path, fallback);
}

bool yconf_get_bool(yconf_t *config, const char *path, bool fallback) {
  _ROOT_OR_RET_FALLBACK;
  return yconf_get_bool2(config, node, path, fallback);
}


void yconf_delete(yconf_t *config) {
	//if (config->document.nodes.start)
	yaml_document_delete(&config->document);
	yaml_parser_delete(&config->parser);
}


int yconf_reload(yconf_t *config) {
	if (config->document.nodes.start)
		yaml_document_delete(&config->document);
	if (!yaml_parser_load(&config->parser, &config->document))
		return false;
	return true;
}


int yconf_load(yconf_t *config, const char *filename) {
	FILE *file = NULL;
	int status = 0;
	
	// clear config
	memset(config, 0, sizeof(config));
	
	if (!yaml_parser_initialize(&config->parser)) {
		fprintf(stderr, "yaml_parser_initialize error\n");
		return -1;
	}
	
	config->filename = filename;
	
	if (strcmp(config->filename, "-") == 0)
		file = stdin;
	else
		assert((file = fopen(config->filename, "rb")));
	
	yaml_parser_set_input_file(&config->parser, file);
	
	if (!yaml_parser_load(&config->parser, &config->document)) {
		fprintf(stderr, "yaml_parser_load error\n");
		status = -2;
	}
	
	if (file != stdin)
		assert(!fclose(file));
	
	return status;
}
