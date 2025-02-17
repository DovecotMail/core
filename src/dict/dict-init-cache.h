#ifndef DICT_INIT_CACHE_H
#define DICT_INIT_CACHE_H

int dict_init_cache_get(struct event *event, const char *dict_name,
			struct dict **dict_r, const char **error_r);
void dict_init_cache_unref(struct dict **dict);

void dict_init_cache_wait_all(void);
void dict_init_cache_destroy_all(void);

#endif
