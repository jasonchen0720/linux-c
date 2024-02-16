#ifndef __MEM_POOL_H__
#define __MEM_POOL_H__
#include "list.h"
#include "rb_tree.h"

struct mem_cache
{
	size_t chunk_size;
	size_t chunk_count;
	size_t stock_size;
	struct list_head full_head;
	struct list_head free_head;
	struct rb_tree 	*stock_tree;
};

struct mem_pool
{
	size_t			  start_size;
	size_t			  diff_size;
	size_t 			  slot_count;
	struct mem_cache *slot_cache;
	struct rb_tree 	  stock_tree;
};
size_t mem_chunk_count(int pages, size_t chunk_size);

struct mem_cache *mem_cache_create(size_t chunk_count, size_t chunk_size);
void  mem_cache_destroy(struct mem_cache *cache);
void *mem_cache_alloc(struct mem_cache *cache);
void  mem_cache_free(struct mem_cache *cache, void *p);

struct mem_pool *mem_pool_create(size_t start_size, size_t diff_size, size_t slot_count, size_t chunk_count);
void mem_pool_destroy(struct mem_pool *pool);
void *mem_pool_alloc(struct mem_pool *pool, size_t size);
void mem_pool_free(struct mem_pool *pool, void *p);

#endif
