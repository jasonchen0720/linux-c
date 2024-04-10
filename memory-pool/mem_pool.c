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
	struct rb_node node;
	struct list_head list;
	struct mem_slot  *slot;
	struct mem_chunk *recycle;
	int  recycnt;	/* recycle count */
	int  index; 
	char start[0];
} __attribute__((aligned(ALIGN)));
#define lock_acquire(x)	do { if ((x)->lock) pthread_mutex_lock((x)->lock); } while (0)
#define lock_release(x) do { if ((x)->lock) pthread_mutex_unlock((x)->lock); } while (0)
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
	DBG("stock size:%lu", stock->slot->stock_size);
	if ((unsigned long)addr >= (unsigned long)stock + stock->slot->stock_size)
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
	return stock->index == stock->slot->chunk_count && stock->recycle == NULL;
}
static struct mem_stock *stock_alloc(struct mem_slot *slot, struct rb_tree *onrbt)
{
	assert(list_empty(&slot->free_head));
	
	struct mem_stock *stock = NULL;

	int err = posix_memalign((void **)&stock, getpagesize(), slot->stock_size);
	if (err) {
		LOG("mem stock --> calloc err:%d", err);
		return NULL;
	}
	INIT_LIST_HEAD(&stock->list);
	stock->slot 	= slot;
	stock->recycnt 	= 0;
	stock->recycle	= NULL;
	
	stock->index = 0;

	list_add(&stock->list, &slot->free_head);
	rb_insert(&stock->node, onrbt);
	LOG("stock@%p allocated, total stocks: %lu", stock, rb_count(onrbt));
	return stock;
}
static struct mem_slot * stock_slot_init(struct mem_slot *slot, struct rb_tree *onrbt, size_t chunk_count, size_t chunk_size)
{
	INIT_LIST_HEAD(&slot->full_head);
	INIT_LIST_HEAD(&slot->free_head);
	
	size_t align = (ALIGN - 1) & chunk_size;
	if (align) {
		chunk_size += (ALIGN - align);
	}

	if (chunk_size < sizeof(struct mem_chunk))
		chunk_size = sizeof(struct mem_chunk);
	
	slot->chunk_count 	= chunk_count;
	slot->chunk_size 	= chunk_size;
	slot->stock_size	= slot->chunk_count * slot->chunk_size + sizeof(struct mem_stock);
	LOG("slot@%p, chunk_size: %lu, chunk_count: %lu.", slot, chunk_size, chunk_count);
	return stock_alloc(slot, onrbt) ? slot : NULL;
}

static void *chunk_alloc(struct rb_tree *onrbt, struct mem_slot *slot)
{
	struct mem_chunk *chunk = NULL;
	if (list_empty(&slot->free_head) && !stock_alloc(slot, onrbt)) {
		LOG("mem stock --> calloc failed.");
		return NULL;
	}
	struct mem_stock *stock = list_first_entry(&slot->free_head, struct mem_stock, list);
	LOG("stock->recycle: %p, stock->index: %d, stock->start: %p.", stock->recycle, stock->index, stock->start);
	if (stock->recycle) {
		chunk = stock->recycle;
		stock->recycle = chunk->next;
		stock->recycnt--;
	} else if (stock->index < slot->chunk_count){
		chunk = (struct mem_chunk *)(stock->start + stock->index * slot->chunk_size);
		stock->index++;
	} else {
		assert(0);
	}
	
	if (stock_exhausted(stock)) {
		list_del(&stock->list);
		list_add(&stock->list, &slot->full_head);
	}
	
	LOG("chunk allocated: %p", chunk);	
	return (void *)chunk;
}

static void chunk_free(struct rb_tree *onrbt, void *p)
{
	LOG("chunk free: %p", p);
	struct rb_node *node = rb_search(p, onrbt);
	assert(node != NULL);
	struct mem_stock *stock = rb_entry(node, struct mem_stock, node);
	struct mem_chunk *chunk = (struct mem_chunk *)p;

	LOG("stock@%p, recycle count: %d, index: %d, stock->start: %p", stock, stock->recycnt, stock->index, stock->start);
	if (stock_exhausted(stock)) {
		list_del(&stock->list);
		list_add(&stock->list, &stock->slot->free_head);
	}
	stock->recycnt++;
	chunk->next = stock->recycle;
	stock->recycle = chunk;

	LOG("chunk released: %p", p);
	if (stock->recycnt == stock->index) {
		struct list_head *head = &stock->slot->free_head;
		/* More than two memory stock */
		if (head->next->next != head ) {
			list_del(&stock->list);
			rb_remove(&stock->node, onrbt);
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
struct mem_cache * mem_cache_create(size_t chunk_count, size_t chunk_size, int threadsafe)
{
	struct mem_cache *cache = malloc(sizeof(struct mem_cache));
	if (cache == NULL) { 
		LOG("mem pool --> calloc failed");
		return NULL;
	}
	
	rb_tree_init(&cache->onrbt, stock_compare, stock_search, stock_print);
	
	if (stock_slot_init(&cache->slot, &cache->onrbt, chunk_count, chunk_size) == NULL) {
		goto out;
	}

	if (threadsafe) {
		if ((cache->lock = malloc(sizeof(*cache->lock))) == NULL) {
			goto out;
		}
		pthread_mutex_init(cache->lock, NULL);
	} else
		cache->lock = NULL;
	
	LOG("mem cache calloc @%p, chunk_size: %lu, chunk_count: %lu.", cache, chunk_size, chunk_count);
	return cache;
out:
	free(cache);
	return NULL;
}
void mem_cache_destroy(struct mem_cache *cache)
{
	struct mem_stock *stock, *tmp;

	list_for_each_entry_safe(stock, tmp, &cache->slot.full_head, list) {
		free(stock);
	}

	list_for_each_entry_safe(stock, tmp, &cache->slot.free_head, list) {
		free(stock);
	}

	LOG("mem_cache@%p destroyed.", cache);
	if (cache->lock) {
		pthread_mutex_destroy(cache->lock);
		free(cache->lock);
	}
	free(cache);
}

void *mem_cache_alloc(struct mem_cache *cache)
{
	lock_acquire(cache);
	struct mem_chunk *chunk = chunk_alloc(&cache->onrbt, &cache->slot);
	lock_release(cache);
	return (void *)chunk;
}

void mem_cache_free(struct mem_cache *cache, void *p)
{
	lock_acquire(cache);
	chunk_free(&cache->onrbt, p);
	lock_release(cache);
}

struct mem_pool *mem_pool_create(size_t start_size, size_t diff_size, size_t slot_count, size_t chunk_count, int threadsafe)
{
	struct mem_pool *pool = malloc(sizeof(struct mem_pool));
	if (pool == NULL) { 
		LOG("mem pool --> calloc failed");
		return NULL;
	}

	struct mem_slot *slots = calloc(slot_count, sizeof(struct mem_cache));
	if (slots == NULL) {
		LOG("mem caches --> calloc failed");
		goto out;
	}

	pool->start_size = start_size;
	pool->diff_size  = diff_size;
	pool->slot_count = slot_count;
	pool->slots = slots;
	rb_tree_init(&pool->onrbt, stock_compare, stock_search, stock_print);

	if (threadsafe) {
		if ((pool->lock = malloc(sizeof(*pool->lock))) == NULL) {
			goto out;
		}
		pthread_mutex_init(pool->lock, NULL);
	} else
		pool->lock = NULL;
	
	int i;
	for (i = 0; i < slot_count; i++) {
		if (stock_slot_init(&slots[i], &pool->onrbt, chunk_count, start_size + i * diff_size) == NULL) {
			mem_pool_destroy(pool);
			return NULL;
		}
	}
	return pool;
out:
	free(pool);
	return NULL;
}
void mem_pool_destroy(struct mem_pool *pool)
{
	int i;

	for (i = 0; i < pool->slot_count; i++) {
		struct mem_stock *stock, *tmp;

		list_for_each_entry_safe(stock, tmp, &pool->slots[i].full_head, list) {
			free(stock);
		}

		list_for_each_entry_safe(stock, tmp, &pool->slots[i].free_head, list) {
			free(stock);
		}

		LOG("mem_cache[%d]@%p destroyed.", i, pool->slots + i);
	}
	if (pool->lock) {
		pthread_mutex_destroy(pool->lock);
		free(pool->lock);
	}
	free(pool->slots);
	free(pool);
}

void *mem_pool_alloc(struct mem_pool *pool, size_t size)
{
	int index = size / pool->diff_size;
	if (index > pool->slot_count) {
		LOG("pool alloc error: size is too large");
		return NULL;
	}
	lock_acquire(pool);
	struct mem_chunk *chunk = chunk_alloc(&pool->onrbt, &pool->slots[index]);
	lock_release(pool);
	return chunk;
}
void mem_pool_free(struct mem_pool *pool, void *p)
{
	lock_acquire(pool);
	chunk_free(&pool->onrbt, p);
	lock_release(pool);
}
