#ifndef IOU_ALLOC_CACHE_H
#define IOU_ALLOC_CACHE_H

/*
 * Don't allow the cache to grow beyond this size.
 */
#define IO_ALLOC_CACHE_MAX	512

struct io_cache_entry {
	struct io_wq_work_node node;
};

static inline bool io_alloc_cache_put(struct io_alloc_cache *cache,
				      struct io_cache_entry *entry)
{
	if (cache->nr_cached < cache->max_cached) {
		cache->nr_cached++;
		wq_stack_add_head(&entry->node, &cache->list);
		kasan_mempool_poison_object(entry);
		return true;
	}
	return false;
}

static inline bool io_alloc_cache_empty(struct io_alloc_cache *cache)
{
	return !cache->list.next;
}

static inline struct io_cache_entry *io_alloc_cache_get(struct io_alloc_cache *cache)
{
	if (cache->list.next) {
		struct io_cache_entry *entry;

		entry = container_of(cache->list.next, struct io_cache_entry, node);
		kasan_mempool_unpoison_object(entry, cache->elem_size);
		cache->list.next = cache->list.next->next;
		cache->nr_cached--;
		return entry;
	}

	return NULL;
}

static inline void io_alloc_cache_init(struct io_alloc_cache *cache,
				       unsigned max_nr, size_t size)
{
	cache->list.next = NULL;
	cache->nr_cached = 0;
	cache->max_cached = max_nr;
	cache->elem_size = size;
}

static inline void io_alloc_cache_free(struct io_alloc_cache *cache,
					void (*free)(struct io_cache_entry *))
{
	while (1) {
		struct io_cache_entry *entry = io_alloc_cache_get(cache);

		if (!entry)
			break;
		free(entry);
	}
	cache->nr_cached = 0;
}
#endif
