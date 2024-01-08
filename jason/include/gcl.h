#ifndef __GCL_H__
#define __GCL_H__

struct gcl_item
{
	char *key;
	char *dval;
	char *value;
};

/* 
 * used for gcl initialization
 * @c : elements must be ordered by key with ASC
 * @n : the number of elements
 * @path: the location of configuration file
 */
int gcl_init(struct gcl_item *c, int n, const char *path);

int gcl_set(struct gcl_item *c, int n, const char *key, const char *value);

const char * gcl_get(struct gcl_item *c, int n, const char *key);

/* you must destroy gcl by caliing gcl_free() after all your work with gcl */
void gcl_free(struct gcl_item *c, int n);

int gcl_reset(struct gcl_item *c, int n, const char *key);

int gcl_sync(struct gcl_item *c, int n, const char *path);

#endif
