#ifndef _YAML_CONFIG_H_
#define _YAML_CONFIG_H_

#include <yaml.h>
#include <stdbool.h>

typedef struct {
	yaml_parser_t parser;
	yaml_document_t document;
	const char *filename;
	yaml_node_t *currkey;
} yconf_t;

// the following foreach macros requires c99 compilation

#define yconf_list_foreach(config, list, item)\
	yaml_node_item_t *__yconf_it ## __LINE__; \
	for (__yconf_it ## __LINE__ = (list)->data.sequence.items.start; \
	     (__yconf_it ## __LINE__ < (list)->data.sequence.items.top) && \
	     (item = ((config)->document.nodes.start + (*__yconf_it ## __LINE__) - 1));\
	    __yconf_it ## __LINE__++)

#define yconf_map_foreach(config, map, knode, vnode)\
	yaml_node_pair_t *__yconf_pair ## __LINE__;\
	for (__yconf_pair ## __LINE__ = (map)->data.mapping.pairs.start;\
		__yconf_pair ## __LINE__ < (map)->data.mapping.pairs.top &&\
		(knode = (config)->document.nodes.start + __yconf_pair ## __LINE__->key - 1) &&\
		(vnode = (config)->document.nodes.start + __yconf_pair ## __LINE__->value - 1);\
		__yconf_pair ## __LINE__++)

int yconf_load(yconf_t *config, const char *filename);
int yconf_reload(yconf_t *config);
void yconf_delete(yconf_t *config);

yaml_node_t *yconf_find_node_in_map (yconf_t *config, yaml_node_t *map, const char *keyname, bool case_sensitive);
yaml_node_t *yconf_find_node_in_list(yconf_t *config, yaml_node_t *list, int index);
yaml_node_t *yconf_find_node_in_coll(yconf_t *config, yaml_node_t *coll, const char *keyname, bool case_sensitive);

yaml_node_t *yconf_find_node(yconf_t *config, const char *path, bool case_sensitive);
yaml_node_t *yconf_find_node2(yconf_t *config, yaml_node_t *node, const char *path, bool case_sensitive);

const char *yconf_get_str  (yconf_t *config, const char *path, const char *fallback);
long long   yconf_get_int  (yconf_t *config, const char *path, long long fallback);
double      yconf_get_float(yconf_t *config, const char *path, double fallback);
bool        yconf_get_bool (yconf_t *config, const char *path, bool fallback);

const char *yconf_get_str2  (yconf_t *config, yaml_node_t *node, const char *path, const char *fallback);
long long   yconf_get_int2  (yconf_t *config, yaml_node_t *node, const char *path, long long fallback);
double      yconf_get_float2(yconf_t *config, yaml_node_t *node, const char *path, double fallback);
bool        yconf_get_bool2 (yconf_t *config, yaml_node_t *node, const char *path, bool fallback);


#endif
