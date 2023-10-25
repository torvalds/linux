/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PAGEMAP_H
#define _LINUX_PAGEMAP_H

/*
 * Copyright 1995 Linus Torvalds
 */
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/highmem.h>
#include <linux/compiler.h>
#include <linux/uaccess.h>
#include <linux/gfp.h>
#include <linux/bitops.h>
#include <linux/hardirq.h> /* for in_interrupt() */
#include <linux/hugetlb_inline.h>

struct folio_batch;

unsigned long invalidate_mapping_pages(struct address_space *mapping,
					pgoff_t start, pgoff_t end);

static inline void invalidate_remote_inode(struct inode *inode)
{
	if (S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	    S_ISLNK(inode->i_mode))
		invalidate_mapping_pages(inode->i_mapping, 0, -1);
}
int invalidate_inode_pages2(struct address_space *mapping);
int invalidate_inode_pages2_range(struct address_space *mapping,
		pgoff_t start, pgoff_t end);
int write_inode_now(struct inode *, int sync);
int filemap_fdatawrite(struct address_space *);
int filemap_flush(struct address_space *);
int filemap_fdatawait_keep_errors(struct address_space *mapping);
int filemap_fdatawait_range(struct address_space *, loff_t lstart, loff_t lend);
int filemap_fdatawait_range_keep_errors(struct address_space *mapping,
		loff_t start_byte, loff_t end_byte);

static inline int filemap_fdatawait(struct address_space *mapping)
{
	return filemap_fdatawait_range(mapping, 0, LLONG_MAX);
}

bool filemap_range_has_page(struct address_space *, loff_t lstart, loff_t lend);
int filemap_write_and_wait_range(struct address_space *mapping,
		loff_t lstart, loff_t lend);
int __filemap_fdatawrite_range(struct address_space *mapping,
		loff_t start, loff_t end, int sync_mode);
int filemap_fdatawrite_range(struct address_space *mapping,
		loff_t start, loff_t end);
int filemap_check_errors(struct address_space *mapping);
void __filemap_set_wb_err(struct address_space *mapping, int err);
int filemap_fdatawrite_wbc(struct address_space *mapping,
			   struct writeback_control *wbc);

static inline int filemap_write_and_wait(struct address_space *mapping)
{
	return filemap_write_and_wait_range(mapping, 0, LLONG_MAX);
}

/**
 * filemap_set_wb_err - set a writeback error on an address_space
 * @mapping: mapping in which to set writeback error
 * @err: error to be set in mapping
 *
 * When writeback fails in some way, we must record that error so that
 * userspace can be informed when fsync and the like are called.  We endeavor
 * to report errors on any file that was open at the time of the error.  Some
 * internal callers also need to know when writeback errors have occurred.
 *
 * When a writeback error occurs, most filesystems will want to call
 * filemap_set_wb_err to record the error in the mapping so that it will be
 * automatically reported whenever fsync is called on the file.
 */
static inline void filemap_set_wb_err(struct address_space *mapping, int err)
{
	/* Fastpath for common case of no error */
	if (unlikely(err))
		__filemap_set_wb_err(mapping, err);
}

/**
 * filemap_check_wb_err - has an error occurred since the mark was sampled?
 * @mapping: mapping to check for writeback errors
 * @since: previously-sampled errseq_t
 *
 * Grab the errseq_t value from the mapping, and see if it has changed "since"
 * the given value was sampled.
 *
 * If it has then report the latest error set, otherwise return 0.
 */
static inline int filemap_check_wb_err(struct address_space *mapping,
					errseq_t since)
{
	return errseq_check(&mapping->wb_err, since);
}

/**
 * filemap_sample_wb_err - sample the current errseq_t to test for later errors
 * @mapping: mapping to be sampled
 *
 * Writeback errors are always reported relative to a particular sample point
 * in the past. This function provides those sample points.
 */
static inline errseq_t filemap_sample_wb_err(struct address_space *mapping)
{
	return errseq_sample(&mapping->wb_err);
}

/**
 * file_sample_sb_err - sample the current errseq_t to test for later errors
 * @file: file pointer to be sampled
 *
 * Grab the most current superblock-level errseq_t value for the given
 * struct file.
 */
static inline errseq_t file_sample_sb_err(struct file *file)
{
	return errseq_sample(&file->f_path.dentry->d_sb->s_wb_err);
}

/*
 * Flush file data before changing attributes.  Caller must hold any locks
 * required to prevent further writes to this file until we're done setting
 * flags.
 */
static inline int inode_drain_writes(struct inode *inode)
{
	inode_dio_wait(inode);
	return filemap_write_and_wait(inode->i_mapping);
}

static inline bool mapping_empty(struct address_space *mapping)
{
	return xa_empty(&mapping->i_pages);
}

/*
 * mapping_shrinkable - test if page cache state allows inode reclaim
 * @mapping: the page cache mapping
 *
 * This checks the mapping's cache state for the pupose of inode
 * reclaim and LRU management.
 *
 * The caller is expected to hold the i_lock, but is not required to
 * hold the i_pages lock, which usually protects cache state. That's
 * because the i_lock and the list_lru lock that protect the inode and
 * its LRU state don't nest inside the irq-safe i_pages lock.
 *
 * Cache deletions are performed under the i_lock, which ensures that
 * when an inode goes empty, it will reliably get queued on the LRU.
 *
 * Cache additions do not acquire the i_lock and may race with this
 * check, in which case we'll report the inode as shrinkable when it
 * has cache pages. This is okay: the shrinker also checks the
 * refcount and the referenced bit, which will be elevated or set in
 * the process of adding new cache pages to an inode.
 */
static inline bool mapping_shrinkable(struct address_space *mapping)
{
	void *head;

	/*
	 * On highmem systems, there could be lowmem pressure from the
	 * inodes before there is highmem pressure from the page
	 * cache. Make inodes shrinkable regardless of cache state.
	 */
	if (IS_ENABLED(CONFIG_HIGHMEM))
		return true;

	/* Cache completely empty? Shrink away. */
	head = rcu_access_pointer(mapping->i_pages.xa_head);
	if (!head)
		return true;

	/*
	 * The xarray stores single offset-0 entries directly in the
	 * head pointer, which allows non-resident page cache entries
	 * to escape the shadow shrinker's list of xarray nodes. The
	 * inode shrinker needs to pick them up under memory pressure.
	 */
	if (!xa_is_node(head) && xa_is_value(head))
		return true;

	return false;
}

/*
 * Bits in mapping->flags.
 */
enum mapping_flags {
	AS_EIO		= 0,	/* IO error on async write */
	AS_ENOSPC	= 1,	/* ENOSPC on async write */
	AS_MM_ALL_LOCKS	= 2,	/* under mm_take_all_locks() */
	AS_UNEVICTABLE	= 3,	/* e.g., ramdisk, SHM_LOCK */
	AS_EXITING	= 4, 	/* final truncate in progress */
	/* writeback related tags are not used */
	AS_NO_WRITEBACK_TAGS = 5,
	AS_LARGE_FOLIO_SUPPORT = 6,
	AS_RELEASE_ALWAYS,	/* Call ->release_folio(), even if no private data */
	AS_STABLE_WRITES,	/* must wait for writeback before modifying
				   folio contents */
};

/**
 * mapping_set_error - record a writeback error in the address_space
 * @mapping: the mapping in which an error should be set
 * @error: the error to set in the mapping
 *
 * When writeback fails in some way, we must record that error so that
 * userspace can be informed when fsync and the like are called.  We endeavor
 * to report errors on any file that was open at the time of the error.  Some
 * internal callers also need to know when writeback errors have occurred.
 *
 * When a writeback error occurs, most filesystems will want to call
 * mapping_set_error to record the error in the mapping so that it can be
 * reported when the application calls fsync(2).
 */
static inline void mapping_set_error(struct address_space *mapping, int error)
{
	if (likely(!error))
		return;

	/* Record in wb_err for checkers using errseq_t based tracking */
	__filemap_set_wb_err(mapping, error);

	/* Record it in superblock */
	if (mapping->host)
		errseq_set(&mapping->host->i_sb->s_wb_err, error);

	/* Record it in flags for now, for legacy callers */
	if (error == -ENOSPC)
		set_bit(AS_ENOSPC, &mapping->flags);
	else
		set_bit(AS_EIO, &mapping->flags);
}

static inline void mapping_set_unevictable(struct address_space *mapping)
{
	set_bit(AS_UNEVICTABLE, &mapping->flags);
}

static inline void mapping_clear_unevictable(struct address_space *mapping)
{
	clear_bit(AS_UNEVICTABLE, &mapping->flags);
}

static inline bool mapping_unevictable(struct address_space *mapping)
{
	return mapping && test_bit(AS_UNEVICTABLE, &mapping->flags);
}

static inline void mapping_set_exiting(struct address_space *mapping)
{
	set_bit(AS_EXITING, &mapping->flags);
}

static inline int mapping_exiting(struct address_space *mapping)
{
	return test_bit(AS_EXITING, &mapping->flags);
}

static inline void mapping_set_no_writeback_tags(struct address_space *mapping)
{
	set_bit(AS_NO_WRITEBACK_TAGS, &mapping->flags);
}

static inline int mapping_use_writeback_tags(struct address_space *mapping)
{
	return !test_bit(AS_NO_WRITEBACK_TAGS, &mapping->flags);
}

static inline bool mapping_release_always(const struct address_space *mapping)
{
	return test_bit(AS_RELEASE_ALWAYS, &mapping->flags);
}

static inline void mapping_set_release_always(struct address_space *mapping)
{
	set_bit(AS_RELEASE_ALWAYS, &mapping->flags);
}

static inline void mapping_clear_release_always(struct address_space *mapping)
{
	clear_bit(AS_RELEASE_ALWAYS, &mapping->flags);
}

static inline bool mapping_stable_writes(const struct address_space *mapping)
{
	return test_bit(AS_STABLE_WRITES, &mapping->flags);
}

static inline void mapping_set_stable_writes(struct address_space *mapping)
{
	set_bit(AS_STABLE_WRITES, &mapping->flags);
}

static inline void mapping_clear_stable_writes(struct address_space *mapping)
{
	clear_bit(AS_STABLE_WRITES, &mapping->flags);
}

static inline gfp_t mapping_gfp_mask(struct address_space * mapping)
{
	return mapping->gfp_mask;
}

/* Restricts the given gfp_mask to what the mapping allows. */
static inline gfp_t mapping_gfp_constraint(struct address_space *mapping,
		gfp_t gfp_mask)
{
	return mapping_gfp_mask(mapping) & gfp_mask;
}

/*
 * This is non-atomic.  Only to be used before the mapping is activated.
 * Probably needs a barrier...
 */
static inline void mapping_set_gfp_mask(struct address_space *m, gfp_t mask)
{
	m->gfp_mask = mask;
}

/**
 * mapping_set_large_folios() - Indicate the file supports large folios.
 * @mapping: The file.
 *
 * The filesystem should call this function in its inode constructor to
 * indicate that the VFS can use large folios to cache the contents of
 * the file.
 *
 * Context: This should not be called while the inode is active as it
 * is non-atomic.
 */
static inline void mapping_set_large_folios(struct address_space *mapping)
{
	__set_bit(AS_LARGE_FOLIO_SUPPORT, &mapping->flags);
}

/*
 * Large folio support currently depends on THP.  These dependencies are
 * being worked on but are not yet fixed.
 */
static inline bool mapping_large_folio_support(struct address_space *mapping)
{
	return IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE) &&
		test_bit(AS_LARGE_FOLIO_SUPPORT, &mapping->flags);
}

static inline int filemap_nr_thps(struct address_space *mapping)
{
#ifdef CONFIG_READ_ONLY_THP_FOR_FS
	return atomic_read(&mapping->nr_thps);
#else
	return 0;
#endif
}

static inline void filemap_nr_thps_inc(struct address_space *mapping)
{
#ifdef CONFIG_READ_ONLY_THP_FOR_FS
	if (!mapping_large_folio_support(mapping))
		atomic_inc(&mapping->nr_thps);
#else
	WARN_ON_ONCE(mapping_large_folio_support(mapping) == 0);
#endif
}

static inline void filemap_nr_thps_dec(struct address_space *mapping)
{
#ifdef CONFIG_READ_ONLY_THP_FOR_FS
	if (!mapping_large_folio_support(mapping))
		atomic_dec(&mapping->nr_thps);
#else
	WARN_ON_ONCE(mapping_large_folio_support(mapping) == 0);
#endif
}

struct address_space *page_mapping(struct page *);
struct address_space *folio_mapping(struct folio *);
struct address_space *swapcache_mapping(struct folio *);

/**
 * folio_file_mapping - Find the mapping this folio belongs to.
 * @folio: The folio.
 *
 * For folios which are in the page cache, return the mapping that this
 * page belongs to.  Folios in the swap cache return the mapping of the
 * swap file or swap device where the data is stored.  This is different
 * from the mapping returned by folio_mapping().  The only reason to
 * use it is if, like NFS, you return 0 from ->activate_swapfile.
 *
 * Do not call this for folios which aren't in the page cache or swap cache.
 */
static inline struct address_space *folio_file_mapping(struct folio *folio)
{
	if (unlikely(folio_test_swapcache(folio)))
		return swapcache_mapping(folio);

	return folio->mapping;
}

static inline struct address_space *page_file_mapping(struct page *page)
{
	return folio_file_mapping(page_folio(page));
}

/*
 * For file cache pages, return the address_space, otherwise return NULL
 */
static inline struct address_space *page_mapping_file(struct page *page)
{
	struct folio *folio = page_folio(page);

	if (unlikely(folio_test_swapcache(folio)))
		return NULL;
	return folio_mapping(folio);
}

/**
 * folio_inode - Get the host inode for this folio.
 * @folio: The folio.
 *
 * For folios which are in the page cache, return the inode that this folio
 * belongs to.
 *
 * Do not call this for folios which aren't in the page cache.
 */
static inline struct inode *folio_inode(struct folio *folio)
{
	return folio->mapping->host;
}

/**
 * folio_attach_private - Attach private data to a folio.
 * @folio: Folio to attach data to.
 * @data: Data to attach to folio.
 *
 * Attaching private data to a folio increments the page's reference count.
 * The data must be detached before the folio will be freed.
 */
static inline void folio_attach_private(struct folio *folio, void *data)
{
	folio_get(folio);
	folio->private = data;
	folio_set_private(folio);
}

/**
 * folio_change_private - Change private data on a folio.
 * @folio: Folio to change the data on.
 * @data: Data to set on the folio.
 *
 * Change the private data attached to a folio and return the old
 * data.  The page must previously have had data attached and the data
 * must be detached before the folio will be freed.
 *
 * Return: Data that was previously attached to the folio.
 */
static inline void *folio_change_private(struct folio *folio, void *data)
{
	void *old = folio_get_private(folio);

	folio->private = data;
	return old;
}

/**
 * folio_detach_private - Detach private data from a folio.
 * @folio: Folio to detach data from.
 *
 * Removes the data that was previously attached to the folio and decrements
 * the refcount on the page.
 *
 * Return: Data that was attached to the folio.
 */
static inline void *folio_detach_private(struct folio *folio)
{
	void *data = folio_get_private(folio);

	if (!folio_test_private(folio))
		return NULL;
	folio_clear_private(folio);
	folio->private = NULL;
	folio_put(folio);

	return data;
}

static inline void attach_page_private(struct page *page, void *data)
{
	folio_attach_private(page_folio(page), data);
}

static inline void *detach_page_private(struct page *page)
{
	return folio_detach_private(page_folio(page));
}

#ifdef CONFIG_NUMA
struct folio *filemap_alloc_folio(gfp_t gfp, unsigned int order);
#else
static inline struct folio *filemap_alloc_folio(gfp_t gfp, unsigned int order)
{
	return folio_alloc(gfp, order);
}
#endif

static inline struct page *__page_cache_alloc(gfp_t gfp)
{
	return &filemap_alloc_folio(gfp, 0)->page;
}

static inline struct page *page_cache_alloc(struct address_space *x)
{
	return __page_cache_alloc(mapping_gfp_mask(x));
}

static inline gfp_t readahead_gfp_mask(struct address_space *x)
{
	return mapping_gfp_mask(x) | __GFP_NORETRY | __GFP_NOWARN;
}

typedef int filler_t(struct file *, struct folio *);

pgoff_t page_cache_next_miss(struct address_space *mapping,
			     pgoff_t index, unsigned long max_scan);
pgoff_t page_cache_prev_miss(struct address_space *mapping,
			     pgoff_t index, unsigned long max_scan);

#define FGP_ACCESSED		0x00000001
#define FGP_LOCK		0x00000002
#define FGP_CREAT		0x00000004
#define FGP_WRITE		0x00000008
#define FGP_NOFS		0x00000010
#define FGP_NOWAIT		0x00000020
#define FGP_FOR_MMAP		0x00000040
#define FGP_HEAD		0x00000080
#define FGP_ENTRY		0x00000100
#define FGP_STABLE		0x00000200

struct folio *__filemap_get_folio(struct address_space *mapping, pgoff_t index,
		int fgp_flags, gfp_t gfp);
struct page *pagecache_get_page(struct address_space *mapping, pgoff_t index,
		int fgp_flags, gfp_t gfp);

/**
 * filemap_get_folio - Find and get a folio.
 * @mapping: The address_space to search.
 * @index: The page index.
 *
 * Looks up the page cache entry at @mapping & @index.  If a folio is
 * present, it is returned with an increased refcount.
 *
 * Otherwise, %NULL is returned.
 */
static inline struct folio *filemap_get_folio(struct address_space *mapping,
					pgoff_t index)
{
	return __filemap_get_folio(mapping, index, 0, 0);
}

/**
 * filemap_lock_folio - Find and lock a folio.
 * @mapping: The address_space to search.
 * @index: The page index.
 *
 * Looks up the page cache entry at @mapping & @index.  If a folio is
 * present, it is returned locked with an increased refcount.
 *
 * Context: May sleep.
 * Return: A folio or %NULL if there is no folio in the cache for this
 * index.  Will not return a shadow, swap or DAX entry.
 */
static inline struct folio *filemap_lock_folio(struct address_space *mapping,
					pgoff_t index)
{
	return __filemap_get_folio(mapping, index, FGP_LOCK, 0);
}

/**
 * find_get_page - find and get a page reference
 * @mapping: the address_space to search
 * @offset: the page index
 *
 * Looks up the page cache slot at @mapping & @offset.  If there is a
 * page cache page, it is returned with an increased refcount.
 *
 * Otherwise, %NULL is returned.
 */
static inline struct page *find_get_page(struct address_space *mapping,
					pgoff_t offset)
{
	return pagecache_get_page(mapping, offset, 0, 0);
}

static inline struct page *find_get_page_flags(struct address_space *mapping,
					pgoff_t offset, int fgp_flags)
{
	return pagecache_get_page(mapping, offset, fgp_flags, 0);
}

/**
 * find_lock_page - locate, pin and lock a pagecache page
 * @mapping: the address_space to search
 * @index: the page index
 *
 * Looks up the page cache entry at @mapping & @index.  If there is a
 * page cache page, it is returned locked and with an increased
 * refcount.
 *
 * Context: May sleep.
 * Return: A struct page or %NULL if there is no page in the cache for this
 * index.
 */
static inline struct page *find_lock_page(struct address_space *mapping,
					pgoff_t index)
{
	return pagecache_get_page(mapping, index, FGP_LOCK, 0);
}

/**
 * find_or_create_page - locate or add a pagecache page
 * @mapping: the page's address_space
 * @index: the page's index into the mapping
 * @gfp_mask: page allocation mode
 *
 * Looks up the page cache slot at @mapping & @offset.  If there is a
 * page cache page, it is returned locked and with an increased
 * refcount.
 *
 * If the page is not present, a new page is allocated using @gfp_mask
 * and added to the page cache and the VM's LRU list.  The page is
 * returned locked and with an increased refcount.
 *
 * On memory exhaustion, %NULL is returned.
 *
 * find_or_create_page() may sleep, even if @gfp_flags specifies an
 * atomic allocation!
 */
static inline struct page *find_or_create_page(struct address_space *mapping,
					pgoff_t index, gfp_t gfp_mask)
{
	return pagecache_get_page(mapping, index,
					FGP_LOCK|FGP_ACCESSED|FGP_CREAT,
					gfp_mask);
}

/**
 * grab_cache_page_nowait - returns locked page at given index in given cache
 * @mapping: target address_space
 * @index: the page index
 *
 * Same as grab_cache_page(), but do not wait if the page is unavailable.
 * This is intended for speculative data generators, where the data can
 * be regenerated if the page couldn't be grabbed.  This routine should
 * be safe to call while holding the lock for another page.
 *
 * Clear __GFP_FS when allocating the page to avoid recursion into the fs
 * and deadlock against the caller's locked page.
 */
static inline struct page *grab_cache_page_nowait(struct address_space *mapping,
				pgoff_t index)
{
	return pagecache_get_page(mapping, index,
			FGP_LOCK|FGP_CREAT|FGP_NOFS|FGP_NOWAIT,
			mapping_gfp_mask(mapping));
}

#define swapcache_index(folio)	__page_file_index(&(folio)->page)

/**
 * folio_index - File index of a folio.
 * @folio: The folio.
 *
 * For a folio which is either in the page cache or the swap cache,
 * return its index within the address_space it belongs to.  If you know
 * the page is definitely in the page cache, you can look at the folio's
 * index directly.
 *
 * Return: The index (offset in units of pages) of a folio in its file.
 */
static inline pgoff_t folio_index(struct folio *folio)
{
        if (unlikely(folio_test_swapcache(folio)))
                return swapcache_index(folio);
        return folio->index;
}

/**
 * folio_next_index - Get the index of the next folio.
 * @folio: The current folio.
 *
 * Return: The index of the folio which follows this folio in the file.
 */
static inline pgoff_t folio_next_index(struct folio *folio)
{
	return folio->index + folio_nr_pages(folio);
}

/**
 * folio_file_page - The page for a particular index.
 * @folio: The folio which contains this index.
 * @index: The index we want to look up.
 *
 * Sometimes after looking up a folio in the page cache, we need to
 * obtain the specific page for an index (eg a page fault).
 *
 * Return: The page containing the file data for this index.
 */
static inline struct page *folio_file_page(struct folio *folio, pgoff_t index)
{
	/* HugeTLBfs indexes the page cache in units of hpage_size */
	if (folio_test_hugetlb(folio))
		return &folio->page;
	return folio_page(folio, index & (folio_nr_pages(folio) - 1));
}

/**
 * folio_contains - Does this folio contain this index?
 * @folio: The folio.
 * @index: The page index within the file.
 *
 * Context: The caller should have the page locked in order to prevent
 * (eg) shmem from moving the page between the page cache and swap cache
 * and changing its index in the middle of the operation.
 * Return: true or false.
 */
static inline bool folio_contains(struct folio *folio, pgoff_t index)
{
	/* HugeTLBfs indexes the page cache in units of hpage_size */
	if (folio_test_hugetlb(folio))
		return folio->index == index;
	return index - folio_index(folio) < folio_nr_pages(folio);
}

/*
 * Given the page we found in the page cache, return the page corresponding
 * to this index in the file
 */
static inline struct page *find_subpage(struct page *head, pgoff_t index)
{
	/* HugeTLBfs wants the head page regardless */
	if (PageHuge(head))
		return head;

	return head + (index & (thp_nr_pages(head) - 1));
}

unsigned filemap_get_folios(struct address_space *mapping, pgoff_t *start,
		pgoff_t end, struct folio_batch *fbatch);
unsigned filemap_get_folios_contig(struct address_space *mapping,
		pgoff_t *start, pgoff_t end, struct folio_batch *fbatch);
unsigned filemap_get_folios_tag(struct address_space *mapping, pgoff_t *start,
		pgoff_t end, xa_mark_t tag, struct folio_batch *fbatch);
unsigned find_get_pages_range_tag(struct address_space *mapping, pgoff_t *index,
			pgoff_t end, xa_mark_t tag, unsigned int nr_pages,
			struct page **pages);
static inline unsigned find_get_pages_tag(struct address_space *mapping,
			pgoff_t *index, xa_mark_t tag, unsigned int nr_pages,
			struct page **pages)
{
	return find_get_pages_range_tag(mapping, index, (pgoff_t)-1, tag,
					nr_pages, pages);
}

struct page *grab_cache_page_write_begin(struct address_space *mapping,
			pgoff_t index);

/*
 * Returns locked page at given index in given cache, creating it if needed.
 */
static inline struct page *grab_cache_page(struct address_space *mapping,
								pgoff_t index)
{
	return find_or_create_page(mapping, index, mapping_gfp_mask(mapping));
}

struct folio *read_cache_folio(struct address_space *, pgoff_t index,
		filler_t *filler, struct file *file);
struct page *read_cache_page(struct address_space *, pgoff_t index,
		filler_t *filler, struct file *file);
extern struct page * read_cache_page_gfp(struct address_space *mapping,
				pgoff_t index, gfp_t gfp_mask);

static inline struct page *read_mapping_page(struct address_space *mapping,
				pgoff_t index, struct file *file)
{
	return read_cache_page(mapping, index, NULL, file);
}

static inline struct folio *read_mapping_folio(struct address_space *mapping,
				pgoff_t index, struct file *file)
{
	return read_cache_folio(mapping, index, NULL, file);
}

/*
 * Get index of the page within radix-tree (but not for hugetlb pages).
 * (TODO: remove once hugetlb pages will have ->index in PAGE_SIZE)
 */
static inline pgoff_t page_to_index(struct page *page)
{
	struct page *head;

	if (likely(!PageTransTail(page)))
		return page->index;

	head = compound_head(page);
	/*
	 *  We don't initialize ->index for tail pages: calculate based on
	 *  head page
	 */
	return head->index + page - head;
}

extern pgoff_t hugetlb_basepage_index(struct page *page);

/*
 * Get the offset in PAGE_SIZE (even for hugetlb pages).
 * (TODO: hugetlb pages should have ->index in PAGE_SIZE)
 */
static inline pgoff_t page_to_pgoff(struct page *page)
{
	if (unlikely(PageHuge(page)))
		return hugetlb_basepage_index(page);
	return page_to_index(page);
}

/*
 * Return byte-offset into filesystem object for page.
 */
static inline loff_t page_offset(struct page *page)
{
	return ((loff_t)page->index) << PAGE_SHIFT;
}

static inline loff_t page_file_offset(struct page *page)
{
	return ((loff_t)page_index(page)) << PAGE_SHIFT;
}

/**
 * folio_pos - Returns the byte position of this folio in its file.
 * @folio: The folio.
 */
static inline loff_t folio_pos(struct folio *folio)
{
	return page_offset(&folio->page);
}

/**
 * folio_file_pos - Returns the byte position of this folio in its file.
 * @folio: The folio.
 *
 * This differs from folio_pos() for folios which belong to a swap file.
 * NFS is the only filesystem today which needs to use folio_file_pos().
 */
static inline loff_t folio_file_pos(struct folio *folio)
{
	return page_file_offset(&folio->page);
}

/*
 * Get the offset in PAGE_SIZE (even for hugetlb folios).
 * (TODO: hugetlb folios should have ->index in PAGE_SIZE)
 */
static inline pgoff_t folio_pgoff(struct folio *folio)
{
	if (unlikely(folio_test_hugetlb(folio)))
		return hugetlb_basepage_index(&folio->page);
	return folio->index;
}

extern pgoff_t linear_hugepage_index(struct vm_area_struct *vma,
				     unsigned long address);

static inline pgoff_t linear_page_index(struct vm_area_struct *vma,
					unsigned long address)
{
	pgoff_t pgoff;
	if (unlikely(is_vm_hugetlb_page(vma)))
		return linear_hugepage_index(vma, address);
	pgoff = (address - vma->vm_start) >> PAGE_SHIFT;
	pgoff += vma->vm_pgoff;
	return pgoff;
}

struct wait_page_key {
	struct folio *folio;
	int bit_nr;
	int page_match;
};

struct wait_page_queue {
	struct folio *folio;
	int bit_nr;
	wait_queue_entry_t wait;
};

static inline bool wake_page_match(struct wait_page_queue *wait_page,
				  struct wait_page_key *key)
{
	if (wait_page->folio != key->folio)
	       return false;
	key->page_match = 1;

	if (wait_page->bit_nr != key->bit_nr)
		return false;

	return true;
}

void __folio_lock(struct folio *folio);
int __folio_lock_killable(struct folio *folio);
bool __folio_lock_or_retry(struct folio *folio, struct mm_struct *mm,
				unsigned int flags);
void unlock_page(struct page *page);
void folio_unlock(struct folio *folio);

/**
 * folio_trylock() - Attempt to lock a folio.
 * @folio: The folio to attempt to lock.
 *
 * Sometimes it is undesirable to wait for a folio to be unlocked (eg
 * when the locks are being taken in the wrong order, or if making
 * progress through a batch of folios is more important than processing
 * them in order).  Usually folio_lock() is the correct function to call.
 *
 * Context: Any context.
 * Return: Whether the lock was successfully acquired.
 */
static inline bool folio_trylock(struct folio *folio)
{
	return likely(!test_and_set_bit_lock(PG_locked, folio_flags(folio, 0)));
}

/*
 * Return true if the page was successfully locked
 */
static inline int trylock_page(struct page *page)
{
	return folio_trylock(page_folio(page));
}

/**
 * folio_lock() - Lock this folio.
 * @folio: The folio to lock.
 *
 * The folio lock protects against many things, probably more than it
 * should.  It is primarily held while a folio is being brought uptodate,
 * either from its backing file or from swap.  It is also held while a
 * folio is being truncated from its address_space, so holding the lock
 * is sufficient to keep folio->mapping stable.
 *
 * The folio lock is also held while write() is modifying the page to
 * provide POSIX atomicity guarantees (as long as the write does not
 * cross a page boundary).  Other modifications to the data in the folio
 * do not hold the folio lock and can race with writes, eg DMA and stores
 * to mapped pages.
 *
 * Context: May sleep.  If you need to acquire the locks of two or
 * more folios, they must be in order of ascending index, if they are
 * in the same address_space.  If they are in different address_spaces,
 * acquire the lock of the folio which belongs to the address_space which
 * has the lowest address in memory first.
 */
static inline void folio_lock(struct folio *folio)
{
	might_sleep();
	if (!folio_trylock(folio))
		__folio_lock(folio);
}

/**
 * lock_page() - Lock the folio containing this page.
 * @page: The page to lock.
 *
 * See folio_lock() for a description of what the lock protects.
 * This is a legacy function and new code should probably use folio_lock()
 * instead.
 *
 * Context: May sleep.  Pages in the same folio share a lock, so do not
 * attempt to lock two pages which share a folio.
 */
static inline void lock_page(struct page *page)
{
	struct folio *folio;
	might_sleep();

	folio = page_folio(page);
	if (!folio_trylock(folio))
		__folio_lock(folio);
}

/**
 * folio_lock_killable() - Lock this folio, interruptible by a fatal signal.
 * @folio: The folio to lock.
 *
 * Attempts to lock the folio, like folio_lock(), except that the sleep
 * to acquire the lock is interruptible by a fatal signal.
 *
 * Context: May sleep; see folio_lock().
 * Return: 0 if the lock was acquired; -EINTR if a fatal signal was received.
 */
static inline int folio_lock_killable(struct folio *folio)
{
	might_sleep();
	if (!folio_trylock(folio))
		return __folio_lock_killable(folio);
	return 0;
}

/*
 * lock_page_killable is like lock_page but can be interrupted by fatal
 * signals.  It returns 0 if it locked the page and -EINTR if it was
 * killed while waiting.
 */
static inline int lock_page_killable(struct page *page)
{
	return folio_lock_killable(page_folio(page));
}

/*
 * folio_lock_or_retry - Lock the folio, unless this would block and the
 * caller indicated that it can handle a retry.
 *
 * Return value and mmap_lock implications depend on flags; see
 * __folio_lock_or_retry().
 */
static inline bool folio_lock_or_retry(struct folio *folio,
		struct mm_struct *mm, unsigned int flags)
{
	might_sleep();
	return folio_trylock(folio) || __folio_lock_or_retry(folio, mm, flags);
}

/*
 * This is exported only for folio_wait_locked/folio_wait_writeback, etc.,
 * and should not be used directly.
 */
void folio_wait_bit(struct folio *folio, int bit_nr);
int folio_wait_bit_killable(struct folio *folio, int bit_nr);

/* 
 * Wait for a folio to be unlocked.
 *
 * This must be called with the caller "holding" the folio,
 * ie with increased folio reference count so that the folio won't
 * go away during the wait.
 */
static inline void folio_wait_locked(struct folio *folio)
{
	if (folio_test_locked(folio))
		folio_wait_bit(folio, PG_locked);
}

static inline int folio_wait_locked_killable(struct folio *folio)
{
	if (!folio_test_locked(folio))
		return 0;
	return folio_wait_bit_killable(folio, PG_locked);
}

static inline void wait_on_page_locked(struct page *page)
{
	folio_wait_locked(page_folio(page));
}

static inline int wait_on_page_locked_killable(struct page *page)
{
	return folio_wait_locked_killable(page_folio(page));
}

void wait_on_page_writeback(struct page *page);
void folio_wait_writeback(struct folio *folio);
int folio_wait_writeback_killable(struct folio *folio);
void end_page_writeback(struct page *page);
void folio_end_writeback(struct folio *folio);
void wait_for_stable_page(struct page *page);
void folio_wait_stable(struct folio *folio);
void __folio_mark_dirty(struct folio *folio, struct address_space *, int warn);
static inline void __set_page_dirty(struct page *page,
		struct address_space *mapping, int warn)
{
	__folio_mark_dirty(page_folio(page), mapping, warn);
}
void folio_account_cleaned(struct folio *folio, struct bdi_writeback *wb);
void __folio_cancel_dirty(struct folio *folio);
static inline void folio_cancel_dirty(struct folio *folio)
{
	/* Avoid atomic ops, locking, etc. when not actually needed. */
	if (folio_test_dirty(folio))
		__folio_cancel_dirty(folio);
}
bool folio_clear_dirty_for_io(struct folio *folio);
bool clear_page_dirty_for_io(struct page *page);
void folio_invalidate(struct folio *folio, size_t offset, size_t length);
int __must_check folio_write_one(struct folio *folio);
static inline int __must_check write_one_page(struct page *page)
{
	return folio_write_one(page_folio(page));
}

int __set_page_dirty_nobuffers(struct page *page);
bool noop_dirty_folio(struct address_space *mapping, struct folio *folio);

#ifdef CONFIG_MIGRATION
int filemap_migrate_folio(struct address_space *mapping, struct folio *dst,
		struct folio *src, enum migrate_mode mode);
#else
#define filemap_migrate_folio NULL
#endif
void page_endio(struct page *page, bool is_write, int err);

void folio_end_private_2(struct folio *folio);
void folio_wait_private_2(struct folio *folio);
int folio_wait_private_2_killable(struct folio *folio);

/*
 * Add an arbitrary waiter to a page's wait queue
 */
void folio_add_wait_queue(struct folio *folio, wait_queue_entry_t *waiter);

/*
 * Fault in userspace address range.
 */
size_t fault_in_writeable(char __user *uaddr, size_t size);
size_t fault_in_subpage_writeable(char __user *uaddr, size_t size);
size_t fault_in_safe_writeable(const char __user *uaddr, size_t size);
size_t fault_in_readable(const char __user *uaddr, size_t size);

int add_to_page_cache_lru(struct page *page, struct address_space *mapping,
		pgoff_t index, gfp_t gfp);
int filemap_add_folio(struct address_space *mapping, struct folio *folio,
		pgoff_t index, gfp_t gfp);
void filemap_remove_folio(struct folio *folio);
void delete_from_page_cache(struct page *page);
void __filemap_remove_folio(struct folio *folio, void *shadow);
void replace_page_cache_page(struct page *old, struct page *new);
void delete_from_page_cache_batch(struct address_space *mapping,
				  struct folio_batch *fbatch);
int try_to_release_page(struct page *page, gfp_t gfp);
bool filemap_release_folio(struct folio *folio, gfp_t gfp);
loff_t mapping_seek_hole_data(struct address_space *, loff_t start, loff_t end,
		int whence);

/* Must be non-static for BPF error injection */
int __filemap_add_folio(struct address_space *mapping, struct folio *folio,
		pgoff_t index, gfp_t gfp, void **shadowp);

bool filemap_range_has_writeback(struct address_space *mapping,
				 loff_t start_byte, loff_t end_byte);

/**
 * filemap_range_needs_writeback - check if range potentially needs writeback
 * @mapping:           address space within which to check
 * @start_byte:        offset in bytes where the range starts
 * @end_byte:          offset in bytes where the range ends (inclusive)
 *
 * Find at least one page in the range supplied, usually used to check if
 * direct writing in this range will trigger a writeback. Used by O_DIRECT
 * read/write with IOCB_NOWAIT, to see if the caller needs to do
 * filemap_write_and_wait_range() before proceeding.
 *
 * Return: %true if the caller should do filemap_write_and_wait_range() before
 * doing O_DIRECT to a page in this range, %false otherwise.
 */
static inline bool filemap_range_needs_writeback(struct address_space *mapping,
						 loff_t start_byte,
						 loff_t end_byte)
{
	if (!mapping->nrpages)
		return false;
	if (!mapping_tagged(mapping, PAGECACHE_TAG_DIRTY) &&
	    !mapping_tagged(mapping, PAGECACHE_TAG_WRITEBACK))
		return false;
	return filemap_range_has_writeback(mapping, start_byte, end_byte);
}

/**
 * struct readahead_control - Describes a readahead request.
 *
 * A readahead request is for consecutive pages.  Filesystems which
 * implement the ->readahead method should call readahead_page() or
 * readahead_page_batch() in a loop and attempt to start I/O against
 * each page in the request.
 *
 * Most of the fields in this struct are private and should be accessed
 * by the functions below.
 *
 * @file: The file, used primarily by network filesystems for authentication.
 *	  May be NULL if invoked internally by the filesystem.
 * @mapping: Readahead this filesystem object.
 * @ra: File readahead state.  May be NULL.
 */
struct readahead_control {
	struct file *file;
	struct address_space *mapping;
	struct file_ra_state *ra;
/* private: use the readahead_* accessors instead */
	pgoff_t _index;
	unsigned int _nr_pages;
	unsigned int _batch_count;
	bool _workingset;
	unsigned long _pflags;
};

#define DEFINE_READAHEAD(ractl, f, r, m, i)				\
	struct readahead_control ractl = {				\
		.file = f,						\
		.mapping = m,						\
		.ra = r,						\
		._index = i,						\
	}

#define VM_READAHEAD_PAGES	(SZ_128K / PAGE_SIZE)

void page_cache_ra_unbounded(struct readahead_control *,
		unsigned long nr_to_read, unsigned long lookahead_count);
void page_cache_sync_ra(struct readahead_control *, unsigned long req_count);
void page_cache_async_ra(struct readahead_control *, struct folio *,
		unsigned long req_count);
void readahead_expand(struct readahead_control *ractl,
		      loff_t new_start, size_t new_len);

/**
 * page_cache_sync_readahead - generic file readahead
 * @mapping: address_space which holds the pagecache and I/O vectors
 * @ra: file_ra_state which holds the readahead state
 * @file: Used by the filesystem for authentication.
 * @index: Index of first page to be read.
 * @req_count: Total number of pages being read by the caller.
 *
 * page_cache_sync_readahead() should be called when a cache miss happened:
 * it will submit the read.  The readahead logic may decide to piggyback more
 * pages onto the read request if access patterns suggest it will improve
 * performance.
 */
static inline
void page_cache_sync_readahead(struct address_space *mapping,
		struct file_ra_state *ra, struct file *file, pgoff_t index,
		unsigned long req_count)
{
	DEFINE_READAHEAD(ractl, file, ra, mapping, index);
	page_cache_sync_ra(&ractl, req_count);
}

/**
 * page_cache_async_readahead - file readahead for marked pages
 * @mapping: address_space which holds the pagecache and I/O vectors
 * @ra: file_ra_state which holds the readahead state
 * @file: Used by the filesystem for authentication.
 * @folio: The folio at @index which triggered the readahead call.
 * @index: Index of first page to be read.
 * @req_count: Total number of pages being read by the caller.
 *
 * page_cache_async_readahead() should be called when a page is used which
 * is marked as PageReadahead; this is a marker to suggest that the application
 * has used up enough of the readahead window that we should start pulling in
 * more pages.
 */
static inline
void page_cache_async_readahead(struct address_space *mapping,
		struct file_ra_state *ra, struct file *file,
		struct folio *folio, pgoff_t index, unsigned long req_count)
{
	DEFINE_READAHEAD(ractl, file, ra, mapping, index);
	page_cache_async_ra(&ractl, folio, req_count);
}

static inline struct folio *__readahead_folio(struct readahead_control *ractl)
{
	struct folio *folio;

	BUG_ON(ractl->_batch_count > ractl->_nr_pages);
	ractl->_nr_pages -= ractl->_batch_count;
	ractl->_index += ractl->_batch_count;

	if (!ractl->_nr_pages) {
		ractl->_batch_count = 0;
		return NULL;
	}

	folio = xa_load(&ractl->mapping->i_pages, ractl->_index);
	VM_BUG_ON_FOLIO(!folio_test_locked(folio), folio);
	ractl->_batch_count = folio_nr_pages(folio);

	return folio;
}

/**
 * readahead_page - Get the next page to read.
 * @ractl: The current readahead request.
 *
 * Context: The page is locked and has an elevated refcount.  The caller
 * should decreases the refcount once the page has been submitted for I/O
 * and unlock the page once all I/O to that page has completed.
 * Return: A pointer to the next page, or %NULL if we are done.
 */
static inline struct page *readahead_page(struct readahead_control *ractl)
{
	struct folio *folio = __readahead_folio(ractl);

	return &folio->page;
}

/**
 * readahead_folio - Get the next folio to read.
 * @ractl: The current readahead request.
 *
 * Context: The folio is locked.  The caller should unlock the folio once
 * all I/O to that folio has completed.
 * Return: A pointer to the next folio, or %NULL if we are done.
 */
static inline struct folio *readahead_folio(struct readahead_control *ractl)
{
	struct folio *folio = __readahead_folio(ractl);

	if (folio)
		folio_put(folio);
	return folio;
}

static inline unsigned int __readahead_batch(struct readahead_control *rac,
		struct page **array, unsigned int array_sz)
{
	unsigned int i = 0;
	XA_STATE(xas, &rac->mapping->i_pages, 0);
	struct page *page;

	BUG_ON(rac->_batch_count > rac->_nr_pages);
	rac->_nr_pages -= rac->_batch_count;
	rac->_index += rac->_batch_count;
	rac->_batch_count = 0;

	xas_set(&xas, rac->_index);
	rcu_read_lock();
	xas_for_each(&xas, page, rac->_index + rac->_nr_pages - 1) {
		if (xas_retry(&xas, page))
			continue;
		VM_BUG_ON_PAGE(!PageLocked(page), page);
		VM_BUG_ON_PAGE(PageTail(page), page);
		array[i++] = page;
		rac->_batch_count += thp_nr_pages(page);
		if (i == array_sz)
			break;
	}
	rcu_read_unlock();

	return i;
}

/**
 * readahead_page_batch - Get a batch of pages to read.
 * @rac: The current readahead request.
 * @array: An array of pointers to struct page.
 *
 * Context: The pages are locked and have an elevated refcount.  The caller
 * should decreases the refcount once the page has been submitted for I/O
 * and unlock the page once all I/O to that page has completed.
 * Return: The number of pages placed in the array.  0 indicates the request
 * is complete.
 */
#define readahead_page_batch(rac, array)				\
	__readahead_batch(rac, array, ARRAY_SIZE(array))

/**
 * readahead_pos - The byte offset into the file of this readahead request.
 * @rac: The readahead request.
 */
static inline loff_t readahead_pos(struct readahead_control *rac)
{
	return (loff_t)rac->_index * PAGE_SIZE;
}

/**
 * readahead_length - The number of bytes in this readahead request.
 * @rac: The readahead request.
 */
static inline size_t readahead_length(struct readahead_control *rac)
{
	return rac->_nr_pages * PAGE_SIZE;
}

/**
 * readahead_index - The index of the first page in this readahead request.
 * @rac: The readahead request.
 */
static inline pgoff_t readahead_index(struct readahead_control *rac)
{
	return rac->_index;
}

/**
 * readahead_count - The number of pages in this readahead request.
 * @rac: The readahead request.
 */
static inline unsigned int readahead_count(struct readahead_control *rac)
{
	return rac->_nr_pages;
}

/**
 * readahead_batch_length - The number of bytes in the current batch.
 * @rac: The readahead request.
 */
static inline size_t readahead_batch_length(struct readahead_control *rac)
{
	return rac->_batch_count * PAGE_SIZE;
}

static inline unsigned long dir_pages(struct inode *inode)
{
	return (unsigned long)(inode->i_size + PAGE_SIZE - 1) >>
			       PAGE_SHIFT;
}

/**
 * folio_mkwrite_check_truncate - check if folio was truncated
 * @folio: the folio to check
 * @inode: the inode to check the folio against
 *
 * Return: the number of bytes in the folio up to EOF,
 * or -EFAULT if the folio was truncated.
 */
static inline ssize_t folio_mkwrite_check_truncate(struct folio *folio,
					      struct inode *inode)
{
	loff_t size = i_size_read(inode);
	pgoff_t index = size >> PAGE_SHIFT;
	size_t offset = offset_in_folio(folio, size);

	if (!folio->mapping)
		return -EFAULT;

	/* folio is wholly inside EOF */
	if (folio_next_index(folio) - 1 < index)
		return folio_size(folio);
	/* folio is wholly past EOF */
	if (folio->index > index || !offset)
		return -EFAULT;
	/* folio is partially inside EOF */
	return offset;
}

/**
 * page_mkwrite_check_truncate - check if page was truncated
 * @page: the page to check
 * @inode: the inode to check the page against
 *
 * Returns the number of bytes in the page up to EOF,
 * or -EFAULT if the page was truncated.
 */
static inline int page_mkwrite_check_truncate(struct page *page,
					      struct inode *inode)
{
	loff_t size = i_size_read(inode);
	pgoff_t index = size >> PAGE_SHIFT;
	int offset = offset_in_page(size);

	if (page->mapping != inode->i_mapping)
		return -EFAULT;

	/* page is wholly inside EOF */
	if (page->index < index)
		return PAGE_SIZE;
	/* page is wholly past EOF */
	if (page->index > index || !offset)
		return -EFAULT;
	/* page is partially inside EOF */
	return offset;
}

/**
 * i_blocks_per_folio - How many blocks fit in this folio.
 * @inode: The inode which contains the blocks.
 * @folio: The folio.
 *
 * If the block size is larger than the size of this folio, return zero.
 *
 * Context: The caller should hold a refcount on the folio to prevent it
 * from being split.
 * Return: The number of filesystem blocks covered by this folio.
 */
static inline
unsigned int i_blocks_per_folio(struct inode *inode, struct folio *folio)
{
	return folio_size(folio) >> inode->i_blkbits;
}

static inline
unsigned int i_blocks_per_page(struct inode *inode, struct page *page)
{
	return i_blocks_per_folio(inode, page_folio(page));
}
#endif /* _LINUX_PAGEMAP_H */
