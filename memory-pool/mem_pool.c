#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

#include "mem_pool.h"

#define __GENERIC_DBG 1
#include "generic_log.h"

#define LOG_TAG "mem-pool"
#define LOG(format,...) LOGI(format"\n", ##__VA_ARGS__)
#define DBG(format,...) LOGD(format"\n", ##__VA_ARGS__)

#if defined(__i386__)
#define ALIGN	4
#elif defined(__x86_64__)
#define ALIGN	8
#else
#define ALIGN	4
#endif

struct mem_chunk
{
	struct mem_chunk *next;
};
struct mem_stock
{
	struct rb_node    node;
	struct list_head  list;
	struct mem_cache *cache;

	long     		  recycnt;	/* recycle count */
	struct mem_chunk *recycle;
	
	long 	index; 
	char 	start[0];
} __attribute__((aligned(ALIGN)));

static int stock_compare(const struct rb_node *n1, const struct rb_node *n2)
{
	if (n1 > n2)
		return 1;
	else if (n1 < n2)
		return -1;
	else
		return 0;
}
static int stock_search(const void *addr, const struct rb_node *n)
{
	const struct mem_stock *stock = rb_entry(n, struct mem_stock, node);

	DBG("addr:%p", addr);
	DBG("stock:%p", stock);
	DBG("stock start:%p", stock->start);
	DBG("stock size:%lu", stock->cache->stock_size);
	if ((unsigned long)addr >= (unsigned long)stock + stock->cache->stock_size)
		return 1;
	else if ((unsigned long)addr < (unsigned long)stock->start)
		return -1;
	else
		return 0;
}
static void stock_print(const struct rb_node *n)
{
	if (n) 
		printf(rb_is_red(n) ? "\033[31m%lx" : "\033[0m%lx", (unsigned long)n);
	else
		printf("\033[0mnil");
	
	printf("\033[0m\n");
}

static inline int stock_exhausted(struct mem_stock *stock)
{
	return stock->index == stock->cache->chunk_count && stock->recycle == NULL;
}
static struct mem_stock *stock_alloc(struct mem_cache *cache)
{
	assert(list_empty(&cache->free_head));
	
	struct mem_stock *stock = NULL;

	int err = posix_memalign((void **)&stock, getpagesize(), cache->stock_size);
	if (err) {
		LOG("mem stock --> calloc err:%d", err);
		return NULL;
	}
	INIT_LIST_HEAD(&stock->list);
	stock->cache 	= cache;
	stock->recycnt 	= 0;
	stock->recycle	= NULL;
	
	stock->index = 0;

	list_add(&stock->list, &cache->free_head);
	rb_insert(&stock->node, cache->stock_tree);
	LOG("stock@%p allocated, total stocks: %lu", stock, rb_count(cache->stock_tree));
	return stock;
}
static struct mem_cache * cache_init(struct mem_cache *cache, struct rb_tree *stock_tree, size_t chunk_count, size_t chunk_size)
{
	INIT_LIST_HEAD(&cache->full_head);
	INIT_LIST_HEAD(&cache->free_head);
	
	size_t align = (ALIGN - 1) & chunk_size;
	if (align) {
		chunk_size += (ALIGN - align);
	}

	if (chunk_size < sizeof(struct mem_chunk))
		chunk_size = sizeof(struct mem_chunk);
	
	cache->chunk_count 	= chunk_count;
	cache->chunk_size 	= chunk_size;
	cache->stock_size	= cache->chunk_count * cache->chunk_size + sizeof(struct mem_stock);
	cache->stock_tree	= stock_tree;
	
	return stock_alloc(cache) ? cache : NULL;
}

static void chunk_free(struct mem_stock *stock, void *p)
{
	struct mem_chunk *chunk = (struct mem_chunk *)p;

	LOG("stock@%p, recycle count: %lu, index: %lu, stock->start: %p", stock, stock->recycnt, stock->index, stock->start);
	if (stock_exhausted(stock)) {
		list_del(&stock->list);
		list_add(&stock->list, &stock->cache->free_head);
	}
	stock->recycnt++;
	chunk->next = stock->recycle;
	stock->recycle = chunk;

	LOG("chunk released: %p", p);
	if (stock->recycnt == stock->index) {
		struct list_head *head = &stock->cache->free_head;
		/* More than two memory stock */
		if (head->next->next != head ) {
			list_del(&stock->list);
			rb_remove(&stock->node, stock->cache->stock_tree);
			LOG("stock@%p, need released as too many unused stocks existed.", stock);
			free(stock);
			return;
		} 
	}
	LOG("stock@%p, keep in free list", stock);
}

size_t mem_chunk_count(int pages, size_t chunk_size)
{
	return (getpagesize() * pages - sizeof(struct mem_stock)) / chunk_size;
}
struct mem_cache * mem_cache_create(size_t chunk_count, size_t chunk_size)
{
	struct mem_cache *cache = malloc(sizeof(struct mem_cache));
	if (cache == NULL) { 
		LOG("mem pool --> calloc failed");
		return NULL;
	}
	
	struct rb_tree *tree = malloc(sizeof(struct rb_tree));
	if (tree == NULL) {
		free(cache);
		return NULL;
	}

	rb_tree_init(tree, stock_compare, stock_search, stock_print);
	
	if (cache_init(cache, tree, chunk_count, chunk_size) == NULL) {
		free(cache);
		free(tree);
		return NULL;
	}
	LOG("mem cache calloc @%p, chunk_size: %lu, chunk_count: %lu.", 
		cache, cache->chunk_size, cache->chunk_count);
	return cache;
}
void mem_cache_destroy(struct mem_cache *cache)
{
	struct mem_stock *stock, *tmp;

	list_for_each_entry_safe(stock, tmp, &cache->full_head, list) {
		free(stock);
	}

	list_for_each_entry_safe(stock, tmp, &cache->free_head, list) {
		free(stock);
	}

	LOG("mem_cache@%p destroyed.", cache);

	free(cache->stock_tree);
	free(cache);
}

void *mem_cache_alloc(struct mem_cache *cache)
{
	struct mem_chunk *chunk = NULL;
	if (list_empty(&cache->free_head) && !stock_alloc(cache)) {
		LOG("mem stock --> calloc failed.");
		return NULL;
	}
	struct mem_stock *stock = list_first_entry(&cache->free_head, struct mem_stock, list);
	LOG("stock->recycle: %p, stock->index: %lu, stock->start: %p.", stock->recycle, stock->index, stock->start);
	if (stock->recycle) {
		chunk = stock->recycle;
		stock->recycle = chunk->next;
		stock->recycnt--;
	} else if (stock->index < cache->chunk_count){
		chunk = (struct mem_chunk *)(stock->start + stock->index * cache->chunk_size);
		stock->index++;
	} else {
		assert(0);
	}
	
	if (stock_exhausted(stock)) {
		list_del(&stock->list);
		list_add(&stock->list, &cache->full_head);

	}
	LOG("chunk allocated: %p", chunk);	
	return (void *)chunk;
}

void mem_cache_free(struct mem_cache *cache, void *p)
{
	LOG("cache free: %p", p);
	struct rb_node *node = rb_search(p, cache->stock_tree);
	assert(node != NULL);
	struct mem_stock *stock = rb_entry(node, struct mem_stock, node);
	chunk_free(stock, p);
}

struct mem_pool *mem_pool_create(size_t start_size, size_t diff_size, size_t slot_count, size_t chunk_count)
{
	struct mem_pool *pool = malloc(sizeof(struct mem_pool));
	if (pool == NULL) { 
		LOG("mem pool --> calloc failed");
		return NULL;
	}

	struct mem_cache *cache = calloc(slot_count, sizeof(struct mem_cache));
	if (cache == NULL) {
		free(pool);
		LOG("mem caches --> calloc failed");
		return NULL;
	}

	rb_tree_init(&pool->stock_tree, stock_compare, stock_search, stock_print);
	
	int i;
	for (i = 0; i < slot_count; i++) {
		if (cache_init(&cache[i], &pool->stock_tree, chunk_count, start_size + i * diff_size) == NULL) {
			mem_pool_destroy(pool);
			return NULL;
		}
	}
	pool->start_size = start_size;
	pool->diff_size  = diff_size;
	pool->slot_count = slot_count;
	pool->slot_cache = cache;
	return pool;
}
void mem_pool_destroy(struct mem_pool *pool)
{
	int i;

	for (i = 0; i < pool->slot_count; i++) {
		struct mem_stock *stock, *tmp;

		list_for_each_entry_safe(stock, tmp, &pool->slot_cache[i].full_head, list) {
			free(stock);
		}

		list_for_each_entry_safe(stock, tmp, &pool->slot_cache[i].free_head, list) {
			free(stock);
		}

		LOG("mem_cache[%d]@%p destroyed.", i, pool->slot_cache + i);
	}

	free(pool->slot_cache);
	free(pool);
}

void *mem_pool_alloc(struct mem_pool *pool, size_t size)
{
	int slot = size / pool->diff_size;

	if (slot > pool->slot_count) {
		LOG("pool alloc error: size is too large");
		return NULL;
	}
	return mem_cache_alloc(pool->slot_cache + slot);
}
void mem_pool_free(struct mem_pool *pool, void *p)
{
	LOG("pool free: %p", p);
	struct rb_node *node = rb_search(p, &pool->stock_tree);
	assert(node != NULL);
	struct mem_stock *stock = rb_entry(node, struct mem_stock, node);
	assert(stock->cache->stock_tree == &pool->stock_tree);
	chunk_free(stock, p);
}
