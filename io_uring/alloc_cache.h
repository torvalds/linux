#ifndef IOU_ALLOC_CACHE_H
#define IOU_ALLOC_CACHE_H

/*
 * Don't allow the cache to grow beyond this size.
 */
#define IO_ALLOC_CACHE_MAX	128

static inline bool io_alloc_cache_put(struct io_alloc_cache *cache,
				      void *entry)
{
	if (cache->nr_cached < cache->max_cached) {
		if (!kasan_mempool_poison_object(entry))
			return false;
		cache->entries[cache->nr_cached++] = entry;
		return true;
	}
	return false;
}

static inline void *io_alloc_cache_get(struct io_alloc_cache *cache)
{
	if (cache->nr_cached) {
		void *entry = cache->entries[--cache->nr_cached];

		kasan_mempool_unpoison_object(entry, cache->elem_size);
		return entry;
	}

	return NULL;
}

/* returns false if the cache was initialized properly */
static inline bool io_alloc_cache_init(struct io_alloc_cache *cache,
				       unsigned max_nr, size_t size)
{
	cache->entries = kvmalloc_array(max_nr, sizeof(void *), GFP_KERNEL);
	if (cache->entries) {
		cache->nr_cached = 0;
		cache->max_cached = max_nr;
		cache->elem_size = size;
		return false;
	}
	return true;
}

static inline void io_alloc_cache_free(struct io_alloc_cache *cache,
				       void (*free)(const void *))
{
	void *entry;

	if (!cache->entries)
		return;

	while ((entry = io_alloc_cache_get(cache)) != NULL)
		free(entry);

	kvfree(cache->entries);
	cache->entries = NULL;
}
#endif
