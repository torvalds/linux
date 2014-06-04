/*
  File: linux/mbcache.h

  (C) 2001 by Andreas Gruenbacher, <a.gruenbacher@computer.org>
*/
struct mb_cache_entry {
	struct list_head		e_lru_list;
	struct mb_cache			*e_cache;
	unsigned short			e_used;
	unsigned short			e_queued;
	atomic_t			e_refcnt;
	struct block_device		*e_bdev;
	sector_t			e_block;
	struct hlist_bl_node		e_block_list;
	struct {
		struct hlist_bl_node	o_list;
		unsigned int		o_key;
	} e_index;
	struct hlist_bl_head		*e_block_hash_p;
	struct hlist_bl_head		*e_index_hash_p;
};

struct mb_cache {
	struct list_head		c_cache_list;
	const char			*c_name;
	atomic_t			c_entry_count;
	int				c_max_entries;
	int				c_bucket_bits;
	struct kmem_cache		*c_entry_cache;
	struct hlist_bl_head		*c_block_hash;
	struct hlist_bl_head		*c_index_hash;
};

/* Functions on caches */

struct mb_cache *mb_cache_create(const char *, int);
void mb_cache_shrink(struct block_device *);
void mb_cache_destroy(struct mb_cache *);

/* Functions on cache entries */

struct mb_cache_entry *mb_cache_entry_alloc(struct mb_cache *, gfp_t);
int mb_cache_entry_insert(struct mb_cache_entry *, struct block_device *,
			  sector_t, unsigned int);
void mb_cache_entry_release(struct mb_cache_entry *);
void mb_cache_entry_free(struct mb_cache_entry *);
struct mb_cache_entry *mb_cache_entry_get(struct mb_cache *,
					  struct block_device *,
					  sector_t);
struct mb_cache_entry *mb_cache_entry_find_first(struct mb_cache *cache,
						 struct block_device *, 
						 unsigned int);
struct mb_cache_entry *mb_cache_entry_find_next(struct mb_cache_entry *,
						struct block_device *, 
						unsigned int);
