/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2026 Google LLC
 * Author: Chao Yu <chaseyu@google.com>
 */
#ifndef _LINUX_F2FS_CACHE_H
#define _LINUX_F2FS_CACHE_H

#include <linux/pagemap.h>
#include <linux/mm.h>
#include <linux/list.h>
#include <linux/radix-tree.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/types.h>

struct f2fs_rwsem;
struct f2fs_io_info;
enum page_type;

/* Represents a single cached block (meta, node or compress) */
struct f2fs_cached_block {
	struct list_head list;		/* LRU list head */
	struct f2fs_cached_block_list *cache;	/* parent cache list */
	union {
		struct f2fs_cached_block *next_entry;/* chain for merged BIO */
		nid_t ino;		/* inode number for compress cache */
	};
	unsigned long index;		/* key in radix tree, (meta/compress: pba, node: nid) */
	unsigned long state;		/* cache entry state (e.g., Dirty, UpToDate) */
	void *data;			/* blocksize-aligned memory (4KB or 16KB) */
	atomic_t refcount;		/* reference count */
};

struct f2fs_sb_info;

enum f2fs_cache_type {
	F2FS_META_CACHE,
	F2FS_NODE_CACHE,
	F2FS_COMPRESS_CACHE,
	NR_CACHE_TYPES,
};

/* Main cache control structure (per sb_info) */
struct f2fs_cached_block_list {
	struct f2fs_sb_info *sbi;	/* Pointer to f2fs_sb_info */
	struct radix_tree_root root;	/* Radix tree for cache lookup */
	spinlock_t tree_lock;		/* Lock for radix tree */
	struct list_head lru_list;	/* Single global LRU list */
	spinlock_t list_lock;		/* Lock for LRU list */
	enum f2fs_cache_type type;	/* Cache type (Node, Meta, Compress) */
	unsigned long num_entries;	/* Current number of entries */
};

#define IS_META_CACHE(cache) ((cache)->type == F2FS_META_CACHE)
#define IS_NODE_CACHE(cache) ((cache)->type == F2FS_NODE_CACHE)
#define IS_COMPRESS_CACHE(cache) ((cache)->type == F2FS_COMPRESS_CACHE)

/* Flags for f2fs_cached_block state */
enum f2fs_cached_state {
	F2FS_BLOCK_LOCKED,		/* cache entry is locked */
	F2FS_BLOCK_UPTODATE,		/* cache data is valid */
	F2FS_BLOCK_DIRTY,		/* cache data is dirty, need to writeback the data */
	F2FS_BLOCK_WRITEBACK,		/* cache data is writeback state */
	F2FS_BLOCK_REFERENCED,		/* cache was accessed recently, shrinker will skip it for once */
	F2FS_BLOCK_INLINE_DATA,		/* indicate inline data */
};

enum {
	__F2FS_CACHE_CREATE,		/* create the cache if there is no cache entry */
	__F2FS_CACHE_LOCK,		/* get and lock the cache entry */
	__F2FS_CACHE_NOFAIL,		/* do not allow failure */
	__F2FS_CACHE_ACCESS,		/* give a chance to add referenced tag */
};

enum f2fs_cache_request_flag {
	F2FS_CACHE_CREATE	= 1 << __F2FS_CACHE_CREATE,
	F2FS_CACHE_LOCK		= 1 << __F2FS_CACHE_LOCK,
	F2FS_CACHE_NOFAIL	= 1 << __F2FS_CACHE_NOFAIL,
	F2FS_CACHE_ACCESS	= 1 << __F2FS_CACHE_ACCESS,
};

#define F2FS_CACHE_LOCK_CREATE	(F2FS_CACHE_LOCK | F2FS_CACHE_CREATE)

#define F2FS_ONSTACK_CACHES		(32)

#define METADATA_CACHE_SHRINK_NUMBER	(128)

#define F2FS_CACHE_FLAG_TEST_FUNC(name, flagname)			\
static inline bool f2fs_cache_test_##name(				\
			const struct f2fs_cached_block *entry)		\
{									\
	return test_bit(F2FS_BLOCK_##flagname, &entry->state);		\
}									\

#define F2FS_CACHE_FLAG_SET_FUNC(name, flagname)			\
static inline void f2fs_cache_set_##name(				\
			struct f2fs_cached_block *entry)		\
{									\
	set_bit(F2FS_BLOCK_##flagname, &entry->state);			\
}									\

#define F2FS_CACHE_FLAG_CLEAR_FUNC(name, flagname)			\
static inline void f2fs_cache_clear_##name(				\
			struct f2fs_cached_block *entry)		\
{									\
	clear_bit(F2FS_BLOCK_##flagname, &entry->state);		\
}									\

#define F2FS_CACHE_FLAG_TEST_AND_SET_FUNC(name, flagname)		\
static inline bool f2fs_cache_test_and_set_##name(			\
			struct f2fs_cached_block *entry)		\
{									\
	return test_and_set_bit(F2FS_BLOCK_##flagname, &entry->state);	\
}									\

#define F2FS_CACHE_FLAG_TEST_AND_CLEAR_FUNC(name, flagname)		\
static inline bool f2fs_cache_test_and_clear_##name(			\
			struct f2fs_cached_block *entry)		\
{									\
	return test_and_clear_bit(F2FS_BLOCK_##flagname, &entry->state);\
}									\

F2FS_CACHE_FLAG_TEST_FUNC(locked, LOCKED);
F2FS_CACHE_FLAG_SET_FUNC(locked, LOCKED);
F2FS_CACHE_FLAG_CLEAR_FUNC(locked, LOCKED);

F2FS_CACHE_FLAG_TEST_FUNC(uptodate, UPTODATE);
F2FS_CACHE_FLAG_SET_FUNC(uptodate, UPTODATE);
F2FS_CACHE_FLAG_CLEAR_FUNC(uptodate, UPTODATE);

F2FS_CACHE_FLAG_TEST_FUNC(dirty, DIRTY);
F2FS_CACHE_FLAG_SET_FUNC(dirty, DIRTY);
F2FS_CACHE_FLAG_CLEAR_FUNC(dirty, DIRTY);
F2FS_CACHE_FLAG_TEST_AND_SET_FUNC(dirty, DIRTY);
F2FS_CACHE_FLAG_TEST_AND_CLEAR_FUNC(dirty, DIRTY);

F2FS_CACHE_FLAG_TEST_FUNC(writeback, WRITEBACK);
F2FS_CACHE_FLAG_SET_FUNC(writeback, WRITEBACK);
F2FS_CACHE_FLAG_CLEAR_FUNC(writeback, WRITEBACK);

F2FS_CACHE_FLAG_TEST_FUNC(inline, INLINE_DATA);
F2FS_CACHE_FLAG_SET_FUNC(inline, INLINE_DATA);
F2FS_CACHE_FLAG_CLEAR_FUNC(inline, INLINE_DATA);

F2FS_CACHE_FLAG_TEST_FUNC(referenced, REFERENCED);
F2FS_CACHE_FLAG_SET_FUNC(referenced, REFERENCED);
F2FS_CACHE_FLAG_TEST_AND_CLEAR_FUNC(referenced, REFERENCED);

static inline void *cache_address(const struct f2fs_cached_block *entry)
{
	return entry->data;
}

#define CACHED_NODE(entry)	((struct f2fs_node *)(cache_address(entry)))

static inline struct folio *cache_folio(const struct f2fs_cached_block *entry)
{
	return virt_to_folio(entry->data);
}

int f2fs_init_cache(struct f2fs_sb_info *sbi,
				struct f2fs_cached_block_list *cache,
				enum f2fs_cache_type type);
void f2fs_destroy_cache(struct f2fs_cached_block_list *cache);
void f2fs_cache_get(struct f2fs_cached_block *entry);
struct f2fs_cached_block *f2fs_find_cache(
			struct f2fs_cached_block_list *cache,
			unsigned long index,
			enum f2fs_cache_request_flag rflag);
#define F2FS_CACHE_TAG_NONE		0
#define F2FS_CACHE_TAG_DIRTY		1
#define F2FS_CACHE_TAG_WRITEBACK	2

bool f2fs_trylock_cache(struct f2fs_cached_block *entry);
void f2fs_lock_cache(struct f2fs_cached_block *entry);
void f2fs_unlock_cache(struct f2fs_cached_block *entry);
bool f2fs_put_cache(struct f2fs_cached_block *entry, bool unlock);
bool f2fs_mark_cache_dirty(struct f2fs_cached_block *entry);
void f2fs_drop_cache_dirty(struct f2fs_cached_block *entry);
void f2fs_start_cache_writeback(struct f2fs_cached_block *entry);
void f2fs_end_cache_writeback(struct f2fs_cached_block *entry);
unsigned int f2fs_cache_gang_lookup(struct f2fs_cached_block_list *cache,
				struct f2fs_cached_block **entries,
				pgoff_t *index, unsigned long end);
unsigned int f2fs_cache_gang_lookup_tag(struct f2fs_cached_block_list *cache,
		struct f2fs_cached_block **results, pgoff_t *first_index,
		unsigned int max_items, int tag);
void f2fs_cache_gang_release(struct f2fs_cached_block **entries,
				unsigned int nr_entries);
void f2fs_cache_wait_on_all_writeback(struct f2fs_cached_block_list *cache);
void f2fs_cache_wait_writeback_cond(struct f2fs_cached_block *entry,
					enum page_type type);
void f2fs_cache_wait_writeback(struct f2fs_cached_block *entry);
void f2fs_cache_update_tag(struct f2fs_cached_block *entry,
		unsigned int clear_from, unsigned int set_to);
struct f2fs_cached_block *f2fs_grab_cache(struct f2fs_cached_block_list *cache,
				unsigned long index, int flags);
void f2fs_truncate_locked_cache(struct f2fs_cached_block *entry,
				bool drop_dirty);
void f2fs_truncate_cache(struct f2fs_cached_block *entry, bool drop_dirty);
void f2fs_drop_cache_range(struct f2fs_cached_block_list *cache,
		unsigned long start, unsigned long len, bool drop_dirty);

#define META_CACHE(sbi)		(&(sbi)->meta_blocks)
#define NODE_CACHE(sbi)		(&(sbi)->node_blocks)
#define COMPRESS_CACHE(sbi)	(&(sbi)->compress_blocks)

#define f2fs_find_meta_cache(sbi, blkaddr)		\
	f2fs_find_cache(META_CACHE(sbi), blkaddr, 0)
#define f2fs_invalidate_meta_caches(sbi, start, len)	\
	f2fs_drop_cache_range(META_CACHE(sbi), start, len, false)
#define f2fs_truncate_meta_caches(sbi, start, len)	\
	f2fs_drop_cache_range(META_CACHE(sbi), start, len, true)

#define f2fs_grab_node_cache(sbi, blkaddr)		\
	f2fs_grab_cache(NODE_CACHE(sbi), blkaddr,	\
	F2FS_CACHE_LOCK_CREATE)
#define f2fs_find_node_cache(sbi, blkaddr)		\
	f2fs_find_cache(NODE_CACHE(sbi), blkaddr, F2FS_CACHE_ACCESS)
#define f2fs_invalidate_node_cache(sbi, blkaddr)	\
	f2fs_drop_cache_range(NODE_CACHE(sbi), blkaddr, 1, false)
#define f2fs_truncate_node_caches(sbi, start, len)	\
	f2fs_drop_cache_range(NODE_CACHE(sbi), start, len, true)

unsigned long f2fs_shrink_cache(struct f2fs_sb_info *sbi,
				unsigned long nr_to_scan);
unsigned long f2fs_shrink_meta_cache(struct f2fs_sb_info *sbi,
				unsigned long nr_to_scan);
unsigned long f2fs_shrink_node_cache(struct f2fs_sb_info *sbi,
				unsigned long nr_to_scan);

#define DEF_DIRTY_CACHE_TIMEOUT 5000
#define MIN_DIRTY_CACHE_TIMEOUT 100
#define MAX_DIRTY_CACHE_TIMEOUT 30000

#define DEF_CACHE_WB_DIRTY_THRESH	(8192)

struct f2fs_cache_kthread {
	struct task_struct *cache_wb_task;
	wait_queue_head_t cache_wb_wq;
	unsigned int cache_wb_interval;
	unsigned int cache_wb_dirty_threshold;
	unsigned int cache_wb_total_threshold;
	bool cache_wb_throttled;	/* last round made no progress */
};

int f2fs_start_cache_wb_thread(struct f2fs_sb_info *sbi);
void f2fs_stop_cache_wb_thread(struct f2fs_sb_info *sbi);
void f2fs_wake_up_cache_wb(struct f2fs_sb_info *sbi);

#endif /* _LINUX_F2FS_CACHE_H */
