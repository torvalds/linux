// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026 Google LLC
 * Author: Chao Yu <chaseyu@google.com>
 */
#include <linux/fs.h>
#include <linux/f2fs_fs.h>
#include <linux/radix-tree.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/pagemap.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/delay.h>
#include "f2fs.h"
#include "cache.h"
#include "node.h"
#include <trace/events/f2fs.h>
#include "segment.h"

static void f2fs_free_cache(struct f2fs_cached_block *entry);

void f2fs_cache_wait_writeback_cond(struct f2fs_cached_block *entry,
					enum page_type type)
{
	/* in case the entry was truncated or on-going shrink */
	if (!entry->cache)
		return;

	if (!f2fs_cache_test_writeback(entry))
		return;

	/* submit cached bio */
	f2fs_submit_merged_write_cache(entry->cache->sbi, entry, 0, type);

	wait_on_bit_io(&entry->state, F2FS_BLOCK_WRITEBACK,
					TASK_UNINTERRUPTIBLE);
}

void f2fs_cache_wait_writeback(struct f2fs_cached_block *entry)
{
	/* in case the entry was truncated or on-going shrink */
	if (!entry->cache)
		return;

	f2fs_cache_wait_writeback_cond(entry,
			IS_META_CACHE(entry->cache) ? META : NODE);
}

void f2fs_cache_update_tag(struct f2fs_cached_block *entry,
		unsigned int clear_from, unsigned int set_to)
{
	struct f2fs_cached_block_list *cache = entry->cache;
	unsigned long flags;

	spin_lock_irqsave(&cache->tree_lock, flags);
	if (clear_from != F2FS_CACHE_TAG_NONE)
		radix_tree_tag_clear(&cache->root, entry->index, clear_from);
	if (set_to != F2FS_CACHE_TAG_NONE)
		radix_tree_tag_set(&cache->root, entry->index, set_to);
	spin_unlock_irqrestore(&cache->tree_lock, flags);
}

bool f2fs_mark_cache_dirty(struct f2fs_cached_block *entry)
{
	struct f2fs_cached_block_list *cache = entry->cache;

	f2fs_cache_set_uptodate(entry);

#ifdef CONFIG_F2FS_CHECK_FS
	if (f2fs_is_node_cache(entry) && IS_INODE(cache->sbi, entry))
		f2fs_inode_chksum_set(cache->sbi, entry);
#endif

	if (!f2fs_cache_test_and_set_dirty(entry)) {
		enum count_type type = IS_META_CACHE(cache) ?
				F2FS_DIRTY_META : F2FS_DIRTY_NODES;

		trace_f2fs_cache_set_dirty(entry,
				IS_META_CACHE(cache) ? META : NODE);
		f2fs_cache_update_tag(entry, F2FS_CACHE_TAG_NONE,
						F2FS_CACHE_TAG_DIRTY);
		inc_cache_count(cache->sbi, type);
		f2fs_wake_up_cache_wb(cache->sbi);
		return true;
	}

	return false;
}

void f2fs_drop_cache_dirty(struct f2fs_cached_block *entry)
{

	struct f2fs_cached_block_list *cache = entry->cache;
	enum count_type type = IS_META_CACHE(cache) ?
				F2FS_DIRTY_META : F2FS_DIRTY_NODES;

	f2fs_cache_clear_uptodate(entry);

	if (!f2fs_cache_test_and_clear_dirty(entry))
		return;

	f2fs_cache_update_tag(entry, F2FS_CACHE_TAG_DIRTY,
					F2FS_CACHE_TAG_NONE);
	dec_cache_count(cache->sbi, type);
}

void f2fs_start_cache_writeback(struct f2fs_cached_block *entry)
{
	f2fs_cache_set_writeback(entry);
	f2fs_cache_update_tag(entry, F2FS_CACHE_TAG_DIRTY,
				F2FS_CACHE_TAG_WRITEBACK);
}

/*
 * The temporary references taken around clear_and_wake_up_bit() are counted
 * in units of F2FS_CACHE_PIN_BIAS, like GUP_PIN_COUNTING_BIAS for folios, so
 * that they can be told apart from the regular references.
 */
#define F2FS_CACHE_PIN_BIAS	(1 << 16)

static void f2fs_cache_pin(struct f2fs_cached_block *entry)
{
	atomic_add(F2FS_CACHE_PIN_BIAS, &entry->refcount);
}

static void f2fs_cache_unpin(struct f2fs_cached_block *entry)
{
	int ref = atomic_sub_return(F2FS_CACHE_PIN_BIAS, &entry->refcount);

	WARN_ON_ONCE(ref < 0);
	if (!ref)
		f2fs_free_cache(entry);
}

void f2fs_end_cache_writeback(struct f2fs_cached_block *entry)
{
	/*
	 * should call f2fs_cache_update_tag() before clearing writeback bit,
	 * in case f2fs_truncate_cache() set entry->cache to NULL.
	 */
	f2fs_cache_update_tag(entry, F2FS_CACHE_TAG_WRITEBACK,
						F2FS_CACHE_TAG_NONE);
	/*
	 * Writeback does not hold an entry reference of its own, relying
	 * on truncation to wait for the clearing of F2FS_BLOCK_WRITEBACK.
	 * But here we must make sure that the entry is not freed and
	 * reused before clear_and_wake_up_bit().
	 */
	f2fs_cache_pin(entry);
	clear_and_wake_up_bit(F2FS_BLOCK_WRITEBACK, &entry->state);
	f2fs_cache_unpin(entry);
}

static int f2fs_cache_refcount(struct f2fs_cached_block *entry)
{
	return atomic_read(&entry->refcount);
}

/* the number of regular references, excluding the temporary ones */
static int f2fs_cache_users(struct f2fs_cached_block *entry)
{
	return f2fs_cache_refcount(entry) & (F2FS_CACHE_PIN_BIAS - 1);
}

static void f2fs_do_free_cache(struct f2fs_cached_block *entry)
{
	kfree(entry->data);
	kfree(entry);
}

static void f2fs_free_cache(struct f2fs_cached_block *entry)
{
	WARN_ON_ONCE(!list_empty(&entry->list));
	WARN_ON_ONCE(f2fs_cache_refcount(entry));
	f2fs_do_free_cache(entry);
}

void f2fs_cache_get(struct f2fs_cached_block *entry)
{
	atomic_inc(&entry->refcount);
}

static bool f2fs_cache_put(struct f2fs_cached_block *entry)
{
	WARN_ON_ONCE(!f2fs_cache_refcount(entry));
	if (atomic_dec_and_test(&entry->refcount)) {
		f2fs_free_cache(entry);
		return true;
	}
	return false;
}

static struct f2fs_cached_block *f2fs_create_cache(
		struct f2fs_cached_block_list *cache,
		unsigned long index, bool nofail)
{
	struct f2fs_sb_info *sbi = cache->sbi;
	struct f2fs_cached_block *entry;
	unsigned int flags = GFP_NOFS;

	if (index == ULONG_MAX)
		return ERR_PTR(-ERANGE);

	if (nofail) {
		flags |= __GFP_NOFAIL;
		entry = kzalloc_obj(*entry, flags);
		entry->data = kmalloc(sbi->blocksize, flags);
	} else {
		entry = f2fs_kzalloc(sbi, sizeof(*entry), flags);
		if (!entry)
			return ERR_PTR(-ENOMEM);

		entry->data = f2fs_kmalloc(sbi, sbi->blocksize, flags);
		if (!entry->data) {
			kfree(entry);
			return ERR_PTR(-ENOMEM);
		}
	}
	entry->index = index;

	atomic_set(&entry->refcount, 0);
	if (!IS_COMPRESS_CACHE(cache))
		entry->next_entry = NULL;
	else
		entry->ino = 0;
	INIT_LIST_HEAD(&entry->list);

	entry->cache = cache;

	return entry;
}

static struct f2fs_cached_block *f2fs_insert_cache(
			struct f2fs_cached_block_list *cache,
			unsigned long index,
			struct f2fs_cached_block *new)
{
	struct f2fs_cached_block *e;
	int ret;
	unsigned long flags;

	ret = radix_tree_preload(GFP_NOFS | __GFP_NOFAIL);
	f2fs_bug_on(cache->sbi, ret);

	spin_lock(&cache->list_lock);
	spin_lock_irqsave(&cache->tree_lock, flags);
	e = radix_tree_lookup(&cache->root, index);
	if (!e) {
		e = new;
		f2fs_bug_on(cache->sbi, f2fs_cache_refcount(e));

		ret = radix_tree_insert(&cache->root, index, e);
		f2fs_bug_on(cache->sbi, ret);

		/* radix tree referenced cache entry */
		f2fs_cache_get(e);
		f2fs_bug_on(cache->sbi, !list_empty(&e->list));
		list_add_tail(&e->list, &cache->lru_list);
		cache->num_entries++;
	}
	f2fs_cache_get(e);
	spin_unlock_irqrestore(&cache->tree_lock, flags);
	spin_unlock(&cache->list_lock);
	radix_tree_preload_end();

	if (new != e) {
		f2fs_bug_on(cache->sbi, f2fs_cache_refcount(new));
		f2fs_do_free_cache(new);
	}

	return e;
}

struct f2fs_cached_block *f2fs_find_cache(
			struct f2fs_cached_block_list *cache,
			unsigned long index,
			enum f2fs_cache_request_flag rflag)
{
	struct f2fs_cached_block *entry;
	unsigned long flags;
	bool access = rflag & F2FS_CACHE_ACCESS;

	spin_lock_irqsave(&cache->tree_lock, flags);
	entry = radix_tree_lookup(&cache->root, index);
	if (entry) {
		f2fs_bug_on(cache->sbi, !f2fs_cache_refcount(entry));
		f2fs_cache_get(entry);
		if (access && !f2fs_cache_test_referenced(entry))
			f2fs_cache_set_referenced(entry);
	} else {
		entry = ERR_PTR(-ENOENT);
	}
	spin_unlock_irqrestore(&cache->tree_lock, flags);

	return entry;
}

struct f2fs_cached_block *f2fs_grab_cache(
			struct f2fs_cached_block_list *cache,
			unsigned long index, int flags)

{
	struct f2fs_cached_block *entry, *new;
	bool create = flags & F2FS_CACHE_CREATE;
	bool nofail = flags & F2FS_CACHE_NOFAIL;
	bool lock = flags & F2FS_CACHE_LOCK;

repeat:
	entry = f2fs_find_cache(cache, index, F2FS_CACHE_ACCESS);
	if (!IS_ERR(entry))
		goto found;

	if (!create)
		return ERR_PTR(-ENOENT);

	new = f2fs_create_cache(cache, index, nofail);
	if (IS_ERR(new))
		return new;

	entry = f2fs_insert_cache(cache, index, new);
found:
	if (lock) {
		f2fs_lock_cache(entry);
		/* has been truncated */
		if (entry->cache != cache) {
			f2fs_put_cache(entry, true);
			goto repeat;
		}
	}
	return entry;
}

bool f2fs_trylock_cache(struct f2fs_cached_block *entry)
{
	return !test_and_set_bit(F2FS_BLOCK_LOCKED, &entry->state);
}

void f2fs_lock_cache(struct f2fs_cached_block *entry)
{
	wait_on_bit_lock(&entry->state, F2FS_BLOCK_LOCKED,
					TASK_UNINTERRUPTIBLE);
}

void f2fs_unlock_cache(struct f2fs_cached_block *entry)
{
	/*
	 * In asynchronous read I/O completion (e.g. from f2fs_ra_node_cache()),
	 * the I/O completion does not hold a reference of its own. Once
	 * clear_and_wake_up_bit() clears F2FS_BLOCK_LOCKED, a concurrent waiter
	 * in f2fs_lock_cache() (e.g. from f2fs_truncate_cache()) can wake up,
	 * truncate the entry, and drop the final reference before wake_up_bit()
	 * finishes.
	 * Pin the entry here to make sure it is not freed before wake_up_bit()
	 * completes.
	 */
	f2fs_cache_pin(entry);
	clear_and_wake_up_bit(F2FS_BLOCK_LOCKED, &entry->state);
	f2fs_cache_unpin(entry);
}

bool f2fs_put_cache(struct f2fs_cached_block *entry, bool unlock)
{
	if (IS_ERR_OR_NULL(entry))
		return false;
	if (unlock)
		f2fs_unlock_cache(entry);
	return f2fs_cache_put(entry);
}

unsigned int f2fs_cache_gang_lookup(struct f2fs_cached_block_list *cache,
				struct f2fs_cached_block **entries,
				pgoff_t *index, unsigned long end)
{
	unsigned long flags;
	unsigned int max_nr = min((unsigned long)F2FS_ONSTACK_CACHES, end - *index);
	int nr, i;

	if (*index >= end || *index == ULONG_MAX)
		return 0;

	spin_lock_irqsave(&cache->tree_lock, flags);
	nr = radix_tree_gang_lookup(&cache->root, (void **)entries,
				*index, max_nr);
	if (!nr)
		goto out_unlock;

	for (i = 0; i < nr; i++) {
		struct f2fs_cached_block *entry = entries[i];

		if (entry->index >= end) {
			nr = i;
			break;
		}
		f2fs_cache_get(entry);
	}
	if (nr)
		*index = entries[nr - 1]->index + 1;
out_unlock:
	spin_unlock_irqrestore(&cache->tree_lock, flags);
	return nr;
}

unsigned int f2fs_cache_gang_lookup_tag(struct f2fs_cached_block_list *cache,
				struct f2fs_cached_block **entries,
				pgoff_t *index, unsigned int max_nr,
				int tag)
{
	unsigned long flags;
	int nr, i;

	if (*index == ULONG_MAX)
		return 0;

	spin_lock_irqsave(&cache->tree_lock, flags);
	nr = radix_tree_gang_lookup_tag(&cache->root, (void **)entries,
				*index, max_nr, tag);
	if (!nr)
		goto out;

	for (i = 0; i < nr; i++)
		f2fs_cache_get(entries[i]);
	*index = entries[nr - 1]->index + 1;
out:
	spin_unlock_irqrestore(&cache->tree_lock, flags);
	return nr;
}

void f2fs_cache_gang_release(struct f2fs_cached_block **entries,
				unsigned int nr_entries)
{
	int i;

	for (i = 0; i < nr_entries; i++)
		f2fs_put_cache(entries[i], false);
}

void f2fs_cache_wait_on_all_writeback(struct f2fs_cached_block_list *cache)
{
	unsigned long index = 0;
	struct f2fs_cached_block *entries[F2FS_ONSTACK_CACHES];
	int nr, i;

next:
	nr = f2fs_cache_gang_lookup_tag(cache, entries, &index,
			F2FS_ONSTACK_CACHES, F2FS_CACHE_TAG_WRITEBACK);
	if (!nr)
		return;

	for (i = 0; i < nr; i++)
		f2fs_cache_wait_writeback(entries[i]);
	f2fs_cache_gang_release(entries, nr);

	cond_resched();
	goto next;
}

static void f2fs_do_truncate_cache(struct f2fs_cached_block *entry,
						bool drop_dirty)
{
	struct f2fs_cached_block_list *cache = entry->cache;
	unsigned long flags;

	if (drop_dirty)
		goto drop_it;

	if (f2fs_cache_test_dirty(entry) ||
	    f2fs_cache_test_writeback(entry))
		return;

drop_it:
	f2fs_cache_wait_writeback(entry);
	f2fs_drop_cache_dirty(entry);

	spin_lock(&cache->list_lock);
	spin_lock_irqsave(&cache->tree_lock, flags);

	f2fs_bug_on(cache->sbi, !entry->cache);
	if (!radix_tree_delete(&cache->root, entry->index))
		f2fs_bug_on(cache->sbi, !entry->cache);

	entry->cache = NULL;
	cache->num_entries--;

	atomic_dec(&entry->refcount);
	f2fs_bug_on(cache->sbi, !f2fs_cache_refcount(entry));

	f2fs_bug_on(cache->sbi, list_empty(&entry->list));
	list_del_init(&entry->list);

	spin_unlock_irqrestore(&cache->tree_lock, flags);
	spin_unlock(&cache->list_lock);
}

void f2fs_truncate_locked_cache(struct f2fs_cached_block *entry,
				bool drop_dirty)
{
	if (!entry->cache)
		return;
	f2fs_do_truncate_cache(entry, drop_dirty);
}

void f2fs_truncate_cache(struct f2fs_cached_block *entry, bool drop_dirty)
{
	f2fs_lock_cache(entry);
	f2fs_truncate_locked_cache(entry, drop_dirty);
	f2fs_unlock_cache(entry);
}

static void f2fs_drop_cache(struct f2fs_cached_block_list *cache,
			block_t blkaddr, bool drop_dirty)
{
	struct f2fs_cached_block *entry;

	entry = f2fs_find_cache(cache, blkaddr, 0);
	if (IS_ERR(entry))
		return;

	f2fs_truncate_cache(entry, drop_dirty);
	f2fs_put_cache(entry, false);
}

void f2fs_drop_cache_range(struct f2fs_cached_block_list *cache,
		unsigned long start, unsigned long len, bool drop_dirty)
{
	unsigned long index = start;
	unsigned long end = (ULONG_MAX - start < len) ?
				ULONG_MAX : (start + len);
	struct f2fs_cached_block *entries[F2FS_ONSTACK_CACHES];
	int nr, i;

	if (len == 1)
		return f2fs_drop_cache(cache, index, drop_dirty);

next:
	nr = f2fs_cache_gang_lookup(cache, entries, &index, end);
	if (!nr)
		return;

	for (i = 0; i < nr; i++)
		f2fs_truncate_cache(entries[i], drop_dirty);
	f2fs_cache_gang_release(entries, nr);

	if (index < end) {
		cond_resched();
		goto next;
	}
}

int f2fs_init_cache(struct f2fs_sb_info *sbi,
		struct f2fs_cached_block_list *cache,
		enum f2fs_cache_type type)
{
	cache->sbi = sbi;
	cache->type = type;
	INIT_RADIX_TREE(&cache->root, GFP_ATOMIC);
	spin_lock_init(&cache->tree_lock);
	spin_lock_init(&cache->list_lock);
	INIT_LIST_HEAD(&cache->lru_list);
	cache->num_entries = 0;

	return 0;
}

void f2fs_destroy_cache(struct f2fs_cached_block_list *cache)
{
	struct list_head *head = &cache->lru_list;
	struct f2fs_cached_block *entry;
	unsigned long flags;

	f2fs_cache_wait_on_all_writeback(cache);
next:
	spin_lock(&cache->list_lock);
	if (list_empty(head)) {
		spin_unlock(&cache->list_lock);
		return;
	}
	entry = list_first_entry(head, struct f2fs_cached_block, list);

	spin_lock_irqsave(&cache->tree_lock, flags);
	radix_tree_delete(&cache->root, entry->index);
	cache->num_entries--;
	list_del_init(&entry->list);
	spin_unlock_irqrestore(&cache->tree_lock, flags);

	spin_unlock(&cache->list_lock);

	/* wait on read cache IO */
	f2fs_lock_cache(entry);
	/* wait on write cache IO */
	f2fs_cache_wait_writeback(entry);
	f2fs_bug_on(cache->sbi, f2fs_cache_test_dirty(entry));
	f2fs_bug_on(cache->sbi, f2fs_cache_test_writeback(entry));
	f2fs_bug_on(cache->sbi, !list_empty(&entry->list));
	/* the I/O completion may not have unpinned the entry yet */
	f2fs_bug_on(cache->sbi, f2fs_cache_users(entry) != 1);
	f2fs_put_cache(entry, true);
	goto next;
}

static unsigned long f2fs_do_shrink_cache(struct f2fs_cached_block_list *cache,
						unsigned long nr_to_scan)
{
	struct f2fs_cached_block *entry, *next;
	LIST_HEAD(dispose_list);
	LIST_HEAD(keep_list);
	unsigned long freed = 0;
	unsigned long scanned = 0;

	/* Phase 1: Isolate candidate entries from LRU list into dispose_list */
	spin_lock(&cache->list_lock);
	list_for_each_entry_safe(entry, next, &cache->lru_list, list) {
		if (scanned >= cache->num_entries)
			break;
		if (scanned++ >= nr_to_scan)
			break;

		/* If accessed, give it a second chance to rotate to tail */
		if (f2fs_cache_test_and_clear_referenced(entry)) {
			list_move_tail(&entry->list, &cache->lru_list);
			continue;
		}

		if (f2fs_cache_test_dirty(entry) ||
		    f2fs_cache_test_writeback(entry) ||
		    f2fs_cache_test_locked(entry))
			continue;

		if (f2fs_cache_refcount(entry) != 1)
			continue;

		list_move_tail(&entry->list, &dispose_list);
	}
	spin_unlock(&cache->list_lock);

	/* Phase 2: Process isolated candidates one by one */
	while (1) {
		spin_lock(&cache->list_lock);
		entry = list_first_entry_or_null(&dispose_list,
						struct f2fs_cached_block, list);
		if (!entry) {
			spin_unlock(&cache->list_lock);
			break;
		}
		f2fs_cache_get(entry);
		list_move_tail(&entry->list, &keep_list);
		spin_unlock(&cache->list_lock);

		if (!f2fs_trylock_cache(entry)) {
			f2fs_put_cache(entry, false);
			continue;
		}

		/* the entry has been truncated */
		if (!entry->cache) {
			f2fs_put_cache(entry, true);
			continue;
		}
		/*
		 * at least there are shrinker, radix tree and another user
		 * has referenced the entry.
		 */
		if (f2fs_cache_refcount(entry) >= 3) {
			f2fs_put_cache(entry, true);
			continue;
		}

		f2fs_do_truncate_cache(entry, false);

		if (f2fs_put_cache(entry, true))
			freed++;
	}

	/* Phase 3: Splice un-reclaimed entries back onto cache->lru_list */
	if (!list_empty(&keep_list)) {
		spin_lock(&cache->list_lock);
		list_splice_tail(&keep_list, &cache->lru_list);
		spin_unlock(&cache->list_lock);
	}

	return freed;
}

unsigned long f2fs_shrink_cache(struct f2fs_sb_info *sbi,
					unsigned long nr_to_scan)
{
	unsigned long freed;

	freed = f2fs_do_shrink_cache(META_CACHE(sbi), nr_to_scan);
	if (freed >= nr_to_scan)
		return freed;

	freed += f2fs_do_shrink_cache(NODE_CACHE(sbi), nr_to_scan - freed);
	if (freed >= nr_to_scan)
		return freed;

	freed += f2fs_do_shrink_cache(COMPRESS_CACHE(sbi), nr_to_scan - freed);
	return freed;
}

unsigned long f2fs_shrink_meta_cache(struct f2fs_sb_info *sbi,
					unsigned long nr_to_scan)
{
	return f2fs_do_shrink_cache(META_CACHE(sbi), nr_to_scan);
}

unsigned long f2fs_shrink_node_cache(struct f2fs_sb_info *sbi,
					unsigned long nr_to_scan)
{
	return f2fs_do_shrink_cache(NODE_CACHE(sbi), nr_to_scan);
}

static inline unsigned long f2fs_total_cached_entries(struct f2fs_sb_info *sbi)
{
	unsigned long total = READ_ONCE(META_CACHE(sbi)->num_entries) +
			      READ_ONCE(NODE_CACHE(sbi)->num_entries);
#ifdef CONFIG_F2FS_FS_COMPRESSION
	if (test_opt(sbi, COMPRESS_CACHE))
		total += READ_ONCE(COMPRESS_CACHE(sbi)->num_entries);
#endif
	return total;
}

static inline s64 f2fs_nr_dirty_caches(struct f2fs_sb_info *sbi)
{
	return get_nr_caches(sbi, F2FS_DIRTY_META) +
		get_nr_caches(sbi, F2FS_DIRTY_NODES);
}

/*
 * f2fs_write_{meta,node}_caches() skip writeback until there are enough
 * dirty caches to gather, so don't wake up f2fs_writeback for nothing.
 */
static inline bool f2fs_cache_wb_collectable(struct f2fs_sb_info *sbi,
						s64 nr_dirty, int type)
{
	return nr_dirty && nr_dirty >= nr_caches_to_skip(sbi, type);
}

static bool f2fs_cache_wb_exceeded(struct f2fs_sb_info *sbi)
{
	struct f2fs_cache_kthread *cache_thread = &sbi->cache_thread;
	unsigned int total_threshold, dirty_threshold;
	s64 nr_meta, nr_node;

	total_threshold = READ_ONCE(cache_thread->cache_wb_total_threshold);
	dirty_threshold = READ_ONCE(cache_thread->cache_wb_dirty_threshold);

	/*
	 * Both thresholds are disabled, rely on periodic writeback only,
	 * unless checkpoint is disabled, as nothing else will flush dirty
	 * caches then.
	 */
	if (!total_threshold && !dirty_threshold &&
	    !is_sbi_flag_set(sbi, SBI_CP_DISABLED))
		return false;

	if (total_threshold &&
	    f2fs_total_cached_entries(sbi) < total_threshold)
		return false;

	nr_meta = get_nr_caches(sbi, F2FS_DIRTY_META);
	nr_node = get_nr_caches(sbi, F2FS_DIRTY_NODES);

	if (dirty_threshold && nr_meta + nr_node < dirty_threshold)
		return false;

	return f2fs_cache_wb_collectable(sbi, nr_meta, META) ||
		f2fs_cache_wb_collectable(sbi, nr_node, NODE);
}

static bool f2fs_should_wake_up_cache_wb(struct f2fs_sb_info *sbi)
{
	/* last round made no progress, wait for periodic writeback */
	if (READ_ONCE(sbi->cache_thread.cache_wb_throttled))
		return false;

	return f2fs_cache_wb_exceeded(sbi);
}

static int f2fs_cache_writeback_kthread(void *data)
{
	struct f2fs_sb_info *sbi = data;
	struct f2fs_cache_kthread *cache_thread = &sbi->cache_thread;
	wait_queue_head_t *wq = &cache_thread->cache_wb_wq;

	set_freezable();

	while (!kthread_should_stop()) {
		unsigned int interval = READ_ONCE(cache_thread->cache_wb_interval);
		long nr_meta = 0, nr_node = 0;
		s64 nr_dirty;

		wait_event_freezable_timeout(*wq,
				kthread_should_stop() ||
				f2fs_should_wake_up_cache_wb(sbi),
				msecs_to_jiffies(interval));

		if (kthread_should_stop())
			break;

		/*
		 * If thresholds are exceeded, throttle threshold based wakeup
		 * until a round makes progress, otherwise we may busy-loop if
		 * this round can not write anything, e.g. on cp_error, freezing,
		 * frozen fs, or lock contention.
		 */
		WRITE_ONCE(cache_thread->cache_wb_throttled,
				f2fs_cache_wb_exceeded(sbi));

		if (f2fs_readonly(sbi->sb))
			continue;

		if (f2fs_cp_error(sbi))
			continue;

		if (unlikely(freezing(current)))
			continue;

		if (!sb_start_write_trylock(sbi->sb))
			continue;

		nr_dirty = f2fs_nr_dirty_caches(sbi);

		f2fs_write_meta_caches(sbi, &nr_meta);
		f2fs_write_node_caches(sbi, &nr_node);

		sb_end_write(sbi->sb);

		/*
		 * Don't rely on the delta of dirty caches only, as caches may
		 * be dirtied concurrently; also count in caches cleaned by
		 * others, e.g. checkpoint triggered by f2fs_balance_fs_bg().
		 */
		if (nr_meta || nr_node || f2fs_nr_dirty_caches(sbi) < nr_dirty)
			WRITE_ONCE(cache_thread->cache_wb_throttled, false);

		cond_resched();
	}
	return 0;
}

void f2fs_wake_up_cache_wb(struct f2fs_sb_info *sbi)
{
	struct f2fs_cache_kthread *cache_thread = &sbi->cache_thread;

	/* pairs with smp_store_release() in f2fs_start_cache_wb_thread() */
	if (!smp_load_acquire(&cache_thread->cache_wb_task))
		return;

	if (!f2fs_should_wake_up_cache_wb(sbi))
		return;

	if (wq_has_sleeper(&cache_thread->cache_wb_wq))
		wake_up(&cache_thread->cache_wb_wq);
}

int f2fs_start_cache_wb_thread(struct f2fs_sb_info *sbi)
{
	struct f2fs_cache_kthread *cache_thread = &sbi->cache_thread;
	struct task_struct *task;
	dev_t dev = sbi->sb->s_dev;
	char name[36];

	if (cache_thread->cache_wb_task)
		return 0;

	init_waitqueue_head(&cache_thread->cache_wb_wq);
	cache_thread->cache_wb_interval = DEF_DIRTY_CACHE_TIMEOUT;
	cache_thread->cache_wb_dirty_threshold = DEF_CACHE_WB_DIRTY_THRESH;
	cache_thread->cache_wb_total_threshold = 0;
	cache_thread->cache_wb_throttled = false;
	snprintf(name, sizeof(name), "f2fs_writeback-%u:%u",
			MAJOR(dev), MINOR(dev));

	task = kthread_run(f2fs_cache_writeback_kthread, sbi, "%s", name);
	if (IS_ERR(task))
		return PTR_ERR(task);

	/* pairs with smp_load_acquire() in f2fs_wake_up_cache_wb() */
	smp_store_release(&cache_thread->cache_wb_task, task);
	return 0;
}

void f2fs_stop_cache_wb_thread(struct f2fs_sb_info *sbi)
{
	struct f2fs_cache_kthread *cache_thread = &sbi->cache_thread;

	if (!cache_thread->cache_wb_task)
		return;

	kthread_stop(cache_thread->cache_wb_task);
	WRITE_ONCE(cache_thread->cache_wb_task, NULL);
}
