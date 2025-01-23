#ifndef IOU_ALLOC_CACHE_H
#define IOU_ALLOC_CACHE_H

/*
 * Don't allow the cache to grow beyond this size.
 */
#define IO_ALLOC_CACHE_MAX	128

#if defined(CONFIG_KASAN)
static inline void io_alloc_cache_kasan(struct iovec **iov, int *nr)
{
	kfree(*iov);
	*iov = NULL;
	*nr = 0;
}
#else
static inline void io_alloc_cache_kasan(struct iovec **iov, int *nr)
{
}
#endif

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

		/*
		 * If KASAN is enabled, always clear the initial bytes that
		 * must be zeroed post alloc, in case any of them overlap
		 * with KASAN storage.
		 */
#if defined(CONFIG_KASAN)
		kasan_mempool_unpoison_object(entry, cache->elem_size);
		if (cache->init_clear)
			memset(entry, 0, cache->init_clear);
#endif
		return entry;
	}

	return NULL;
}

static inline void *io_cache_alloc(struct io_alloc_cache *cache, gfp_t gfp)
{
	void *obj;

	obj = io_alloc_cache_get(cache);
	if (obj)
		return obj;

	obj = kmalloc(cache->elem_size, gfp);
	if (obj && cache->init_clear)
		memset(obj, 0, cache->init_clear);
	return obj;
}

/* returns false if the cache was initialized properly */
static inline bool io_alloc_cache_init(struct io_alloc_cache *cache,
				       unsigned max_nr, unsigned int size,
				       unsigned int init_bytes)
{
	cache->entries = kvmalloc_array(max_nr, sizeof(void *), GFP_KERNEL);
	if (cache->entries) {
		cache->nr_cached = 0;
		cache->max_cached = max_nr;
		cache->elem_size = size;
		cache->init_clear = init_bytes;
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
