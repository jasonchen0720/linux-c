#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "list.h"
//#include "logger.h"

#define ALIGN	8


struct memory_pool
{

	struct list_head full_head;
	struct list_head current;
	
	//struct memory_pool *prev;
	//struct memory_pool *next;
	
	//struct list_head full;
	//struct list_head free;
	
	unsigned long chunk_size;
	unsigned long chunk_count;
	unsigned long cache_size;
	//unsigned long cache_min;
	//unsigned long cache_max;
};
struct memory_cache
{
	struct list_head list;
		
	struct memory_pool 	*pool;

	unsigned long 		nfree;
	struct memory_chunk *free;
	
	unsigned long 		nalloc;
	char 				alloc[];

	//struct list_head 	list;
} __attribute__((aligned(ALIGN)));

struct memory_chunk
{
	struct memory_cache *cache;
	struct memory_chunk *next;
}__attribute__((aligned(ALIGN)));

static inline int memory_cache_full(struct memory_cache *cache)
{
	//printf("nalloc: %lu, chunk_count: %lu, free: %p\n", cache->nalloc, cache->pool->chunk_count, cache->free);
	return cache->nalloc == cache->pool->chunk_count && cache->free == NULL;
}
static struct memory_cache *memory_cache_alloc(struct memory_pool *pool)
{
	assert(list_empty(&pool->current));
	
	struct memory_cache *cache = NULL;

	int err = posix_memalign((void **)&cache, getpagesize(), 
						sizeof(struct memory_cache) + pool->cache_size);
	if (err) {
		printf("memory cache --> calloc err:%d\n", err);
		return NULL;
	}
	INIT_LIST_HEAD(&cache->list);
	cache->pool = pool;
	cache->nfree = 0;
	cache->free  = NULL;
	
	cache->nalloc = 0;
	//cache->alloc  = (struct memory_chunk *)(cache + sizeof(struct memory_cache));
	printf("memory cache --> calloc done:%p\n", cache);

	list_add(&cache->list, &pool->current);
	
	return cache;
}
struct memory_pool *memory_pool_create(int chunk_count, size_t chunk_size)
{
	struct memory_pool *pool = calloc(1, sizeof(struct memory_pool));
	if (pool == NULL) { 
		printf("memory pool --> calloc failed\n");
		return NULL;
	}

	size_t align = (ALIGN - 1) & chunk_size;
	if (align) {
		chunk_size += (ALIGN - align);
	}
		
	pool->chunk_count 	= chunk_count;
	pool->chunk_size 	= chunk_size + sizeof(struct memory_cache *);
	pool->cache_size	= pool->chunk_count * pool->chunk_size;

	INIT_LIST_HEAD(&pool->full_head);
	INIT_LIST_HEAD(&pool->current);
	
	if (!memory_cache_alloc(pool)) {
		free(pool);
		pool = NULL;
	}

	printf("memory pool calloc @%p, chunk_size: %lu, chunk_count: %lu.\n", 
		pool, pool->chunk_size, pool->chunk_count);
	return pool;
}


void *memory_pool_alloc(struct memory_pool *pool)
{
	struct memory_chunk *chunk = NULL;
	//printf("1 pool->current: %p.\n", pool->current);
	if (list_empty(&pool->current) /*&& !memory_cache_alloc(pool)*/) {
		printf("memory cache --> calloc failed.\n");
		return NULL;
	}

	//printf("idle: %p: nalloc: %lu.\n", pool->current->free, pool->current->nalloc);
	struct memory_cache *cache = list_first_entry(&pool->current, struct memory_cache, list);
	if (cache->free) {
		chunk = cache->free;
		cache->free = chunk->next;
		cache->nfree--;
	} else if (cache->nalloc < pool->chunk_count){
		chunk = (struct memory_chunk *)(cache->alloc + cache->nalloc * pool->chunk_size);

		printf("offset: %lx\n", cache->nalloc * pool->chunk_size);
		printf("cache->alloc: %p, chunk: %p\n", cache->alloc, chunk);
		cache->nalloc++;
		printf("cache->free: %p: nalloc: %lu.\n", cache->free, cache->nalloc);
	} else {
		assert(0);
	}
	chunk->cache = cache;
	
	if (memory_cache_full(cache)) {
		list_del(&cache->list);
		list_add(&cache->list, &pool->full_head);

	}

	printf("chunk: %p, chunk offset: %p\n", chunk, &chunk->next);
	
	return (void *)&chunk->next;
}


void *memory_pool_free(struct memory_pool *pool, void *p)
{
	struct memory_chunk *chunk = container_of(p, struct memory_chunk, next);

	assert(chunk->cache);

	printf("nfree: %lu, nalloc: %lu\n", chunk->cache->nfree, chunk->cache->nalloc);
	if (memory_cache_full(chunk->cache))
		list_del(&chunk->cache->list);

	chunk->cache->nfree++;
	chunk->next = chunk->cache->free;
	chunk->cache->free = chunk;
	
	if (list_empty(&pool->current)) {
		list_add(&chunk->cache->list, &pool->current);
		return;
	}

	if (chunk->cache->nfree == chunk->cache->nalloc) {
		struct list_head *head = &pool->current;
		/* More than two cache */
		if (head->next->next != head ) {
			list_del(&chunk->cache->list);
			free(chunk->cache);
		}
	}
}

struct person
{
	int age;
	int sex;
	char name[16];
};
int main(int argc, char **argv)

{
	printf("memory_cache size:%lu.\n", sizeof(struct memory_cache));
	printf("memory_chunk size:%lu.\n", sizeof(struct memory_chunk));
	printf("person size:%lu.\n", sizeof(struct person));
	int c = atoi(argv[1]);
	
	struct memory_pool *mp = memory_pool_create(c, sizeof(struct person));

	if (mp) {
		int i;
		struct person *p,  *t = NULL;
		for (i = 0; i < c + 3; i++) {
			p = memory_pool_alloc(mp);
			printf("p:%p\n", p);

			if (p)
				memory_pool_free(mp, p);
			
		}
	}
	
}
