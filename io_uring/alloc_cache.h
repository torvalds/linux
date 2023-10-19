#ifndef IOU_ALLOC_CACHE_H
#define IOU_ALLOC_CACHE_H

/*
 * Don't allow the cache to grow beyond this size.
 */
#define IO_ALLOC_CACHE_MAX	512

struct io_cache_entry {
	struct hlist_node	node;
};

static inline bool io_alloc_cache_put(struct io_alloc_cache *cache,
				      struct io_cache_entry *entry)
{
	if (cache->nr_cached < IO_ALLOC_CACHE_MAX) {
		cache->nr_cached++;
		hlist_add_head(&entry->node, &cache->list);
		return true;
	}
	return false;
}

static inline struct io_cache_entry *io_alloc_cache_get(struct io_alloc_cache *cache)
{
	if (!hlist_empty(&cache->list)) {
		struct hlist_node *node = cache->list.first;

		hlist_del(node);
		cache->nr_cached--;
		return container_of(node, struct io_cache_entry, node);
	}

	return NULL;
}

static inline void io_alloc_cache_init(struct io_alloc_cache *cache)
{
	INIT_HLIST_HEAD(&cache->list);
	cache->nr_cached = 0;
}

static inline void io_alloc_cache_free(struct io_alloc_cache *cache,
					void (*free)(struct io_cache_entry *))
{
	while (!hlist_empty(&cache->list)) {
		struct hlist_node *node = cache->list.first;

		hlist_del(node);
		free(container_of(node, struct io_cache_entry, node));
	}
	cache->nr_cached = 0;
}
#endif
