/* General filesystem caching interface
 *
 * Copyright (C) 2004-2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * NOTE!!! See:
 *
 *	Documentation/filesystems/caching/netfs-api.txt
 *
 * for a description of the network filesystem interface declared here.
 */

#ifndef _LINUX_FSCACHE_H
#define _LINUX_FSCACHE_H

#include <linux/fs.h>
#include <linux/list.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>
#include <linux/list_bl.h>

#if defined(CONFIG_FSCACHE) || defined(CONFIG_FSCACHE_MODULE)
#define fscache_available() (1)
#define fscache_cookie_valid(cookie) (cookie)
#else
#define fscache_available() (0)
#define fscache_cookie_valid(cookie) (0)
#endif


/*
 * overload PG_private_2 to give us PG_fscache - this is used to indicate that
 * a page is currently backed by a local disk cache
 */
#define PageFsCache(page)		PagePrivate2((page))
#define SetPageFsCache(page)		SetPagePrivate2((page))
#define ClearPageFsCache(page)		ClearPagePrivate2((page))
#define TestSetPageFsCache(page)	TestSetPagePrivate2((page))
#define TestClearPageFsCache(page)	TestClearPagePrivate2((page))

/* pattern used to fill dead space in an index entry */
#define FSCACHE_INDEX_DEADFILL_PATTERN 0x79

struct pagevec;
struct fscache_cache_tag;
struct fscache_cookie;
struct fscache_netfs;

typedef void (*fscache_rw_complete_t)(struct page *page,
				      void *context,
				      int error);

/* result of index entry consultation */
enum fscache_checkaux {
	FSCACHE_CHECKAUX_OKAY,		/* entry okay as is */
	FSCACHE_CHECKAUX_NEEDS_UPDATE,	/* entry requires update */
	FSCACHE_CHECKAUX_OBSOLETE,	/* entry requires deletion */
};

/*
 * fscache cookie definition
 */
struct fscache_cookie_def {
	/* name of cookie type */
	char name[16];

	/* cookie type */
	uint8_t type;
#define FSCACHE_COOKIE_TYPE_INDEX	0
#define FSCACHE_COOKIE_TYPE_DATAFILE	1

	/* select the cache into which to insert an entry in this index
	 * - optional
	 * - should return a cache identifier or NULL to cause the cache to be
	 *   inherited from the parent if possible or the first cache picked
	 *   for a non-index file if not
	 */
	struct fscache_cache_tag *(*select_cache)(
		const void *parent_netfs_data,
		const void *cookie_netfs_data);

	/* consult the netfs about the state of an object
	 * - this function can be absent if the index carries no state data
	 * - the netfs data from the cookie being used as the target is
	 *   presented, as is the auxiliary data and the object size
	 */
	enum fscache_checkaux (*check_aux)(void *cookie_netfs_data,
					   const void *data,
					   uint16_t datalen,
					   loff_t object_size);

	/* get an extra reference on a read context
	 * - this function can be absent if the completion function doesn't
	 *   require a context
	 */
	void (*get_context)(void *cookie_netfs_data, void *context);

	/* release an extra reference on a read context
	 * - this function can be absent if the completion function doesn't
	 *   require a context
	 */
	void (*put_context)(void *cookie_netfs_data, void *context);

	/* indicate page that now have cache metadata retained
	 * - this function should mark the specified page as now being cached
	 * - the page will have been marked with PG_fscache before this is
	 *   called, so this is optional
	 */
	void (*mark_page_cached)(void *cookie_netfs_data,
				 struct address_space *mapping,
				 struct page *page);
};

/*
 * fscache cached network filesystem type
 * - name, version and ops must be filled in before registration
 * - all other fields will be set during registration
 */
struct fscache_netfs {
	uint32_t			version;	/* indexing version */
	const char			*name;		/* filesystem name */
	struct fscache_cookie		*primary_index;
};

/*
 * data file or index object cookie
 * - a file will only appear in one cache
 * - a request to cache a file may or may not be honoured, subject to
 *   constraints such as disk space
 * - indices are created on disk just-in-time
 */
struct fscache_cookie {
	atomic_t			usage;		/* number of users of this cookie */
	atomic_t			n_children;	/* number of children of this cookie */
	atomic_t			n_active;	/* number of active users of netfs ptrs */
	spinlock_t			lock;
	spinlock_t			stores_lock;	/* lock on page store tree */
	struct hlist_head		backing_objects; /* object(s) backing this file/index */
	const struct fscache_cookie_def	*def;		/* definition */
	struct fscache_cookie		*parent;	/* parent of this entry */
	struct hlist_bl_node		hash_link;	/* Link in hash table */
	void				*netfs_data;	/* back pointer to netfs */
	struct radix_tree_root		stores;		/* pages to be stored on this cookie */
#define FSCACHE_COOKIE_PENDING_TAG	0		/* pages tag: pending write to cache */
#define FSCACHE_COOKIE_STORING_TAG	1		/* pages tag: writing to cache */

	unsigned long			flags;
#define FSCACHE_COOKIE_LOOKING_UP	0	/* T if non-index cookie being looked up still */
#define FSCACHE_COOKIE_NO_DATA_YET	1	/* T if new object with no cached data yet */
#define FSCACHE_COOKIE_UNAVAILABLE	2	/* T if cookie is unavailable (error, etc) */
#define FSCACHE_COOKIE_INVALIDATING	3	/* T if cookie is being invalidated */
#define FSCACHE_COOKIE_RELINQUISHED	4	/* T if cookie has been relinquished */
#define FSCACHE_COOKIE_ENABLED		5	/* T if cookie is enabled */
#define FSCACHE_COOKIE_ENABLEMENT_LOCK	6	/* T if cookie is being en/disabled */
#define FSCACHE_COOKIE_AUX_UPDATED	8	/* T if the auxiliary data was updated */
#define FSCACHE_COOKIE_ACQUIRED		9	/* T if cookie is in use */
#define FSCACHE_COOKIE_RELINQUISHING	10	/* T if cookie is being relinquished */

	u8				type;		/* Type of object */
	u8				key_len;	/* Length of index key */
	u8				aux_len;	/* Length of auxiliary data */
	u32				key_hash;	/* Hash of parent, type, key, len */
	union {
		void			*key;		/* Index key */
		u8			inline_key[16];	/* - If the key is short enough */
	};
	union {
		void			*aux;		/* Auxiliary data */
		u8			inline_aux[8];	/* - If the aux data is short enough */
	};
};

static inline bool fscache_cookie_enabled(struct fscache_cookie *cookie)
{
	return test_bit(FSCACHE_COOKIE_ENABLED, &cookie->flags);
}

/*
 * slow-path functions for when there is actually caching available, and the
 * netfs does actually have a valid token
 * - these are not to be called directly
 * - these are undefined symbols when FS-Cache is not configured and the
 *   optimiser takes care of not using them
 */
extern int __fscache_register_netfs(struct fscache_netfs *);
extern void __fscache_unregister_netfs(struct fscache_netfs *);
extern struct fscache_cache_tag *__fscache_lookup_cache_tag(const char *);
extern void __fscache_release_cache_tag(struct fscache_cache_tag *);

extern struct fscache_cookie *__fscache_acquire_cookie(
	struct fscache_cookie *,
	const struct fscache_cookie_def *,
	const void *, size_t,
	const void *, size_t,
	void *, loff_t, bool);
extern void __fscache_relinquish_cookie(struct fscache_cookie *, const void *, bool);
extern int __fscache_check_consistency(struct fscache_cookie *, const void *);
extern void __fscache_update_cookie(struct fscache_cookie *, const void *);
extern int __fscache_attr_changed(struct fscache_cookie *);
extern void __fscache_invalidate(struct fscache_cookie *);
extern void __fscache_wait_on_invalidate(struct fscache_cookie *);
extern int __fscache_read_or_alloc_page(struct fscache_cookie *,
					struct page *,
					fscache_rw_complete_t,
					void *,
					gfp_t);
extern int __fscache_read_or_alloc_pages(struct fscache_cookie *,
					 struct address_space *,
					 struct list_head *,
					 unsigned *,
					 fscache_rw_complete_t,
					 void *,
					 gfp_t);
extern int __fscache_alloc_page(struct fscache_cookie *, struct page *, gfp_t);
extern int __fscache_write_page(struct fscache_cookie *, struct page *, loff_t, gfp_t);
extern void __fscache_uncache_page(struct fscache_cookie *, struct page *);
extern bool __fscache_check_page_write(struct fscache_cookie *, struct page *);
extern void __fscache_wait_on_page_write(struct fscache_cookie *, struct page *);
extern bool __fscache_maybe_release_page(struct fscache_cookie *, struct page *,
					 gfp_t);
extern void __fscache_uncache_all_inode_pages(struct fscache_cookie *,
					      struct inode *);
extern void __fscache_readpages_cancel(struct fscache_cookie *cookie,
				       struct list_head *pages);
extern void __fscache_disable_cookie(struct fscache_cookie *, const void *, bool);
extern void __fscache_enable_cookie(struct fscache_cookie *, const void *, loff_t,
				    bool (*)(void *), void *);

/**
 * fscache_register_netfs - Register a filesystem as desiring caching services
 * @netfs: The description of the filesystem
 *
 * Register a filesystem as desiring caching services if they're available.
 *
 * See Documentation/filesystems/caching/netfs-api.txt for a complete
 * description.
 */
static inline
int fscache_register_netfs(struct fscache_netfs *netfs)
{
	if (fscache_available())
		return __fscache_register_netfs(netfs);
	else
		return 0;
}

/**
 * fscache_unregister_netfs - Indicate that a filesystem no longer desires
 * caching services
 * @netfs: The description of the filesystem
 *
 * Indicate that a filesystem no longer desires caching services for the
 * moment.
 *
 * See Documentation/filesystems/caching/netfs-api.txt for a complete
 * description.
 */
static inline
void fscache_unregister_netfs(struct fscache_netfs *netfs)
{
	if (fscache_available())
		__fscache_unregister_netfs(netfs);
}

/**
 * fscache_lookup_cache_tag - Look up a cache tag
 * @name: The name of the tag to search for
 *
 * Acquire a specific cache referral tag that can be used to select a specific
 * cache in which to cache an index.
 *
 * See Documentation/filesystems/caching/netfs-api.txt for a complete
 * description.
 */
static inline
struct fscache_cache_tag *fscache_lookup_cache_tag(const char *name)
{
	if (fscache_available())
		return __fscache_lookup_cache_tag(name);
	else
		return NULL;
}

/**
 * fscache_release_cache_tag - Release a cache tag
 * @tag: The tag to release
 *
 * Release a reference to a cache referral tag previously looked up.
 *
 * See Documentation/filesystems/caching/netfs-api.txt for a complete
 * description.
 */
static inline
void fscache_release_cache_tag(struct fscache_cache_tag *tag)
{
	if (fscache_available())
		__fscache_release_cache_tag(tag);
}

/**
 * fscache_acquire_cookie - Acquire a cookie to represent a cache object
 * @parent: The cookie that's to be the parent of this one
 * @def: A description of the cache object, including callback operations
 * @index_key: The index key for this cookie
 * @index_key_len: Size of the index key
 * @aux_data: The auxiliary data for the cookie (may be NULL)
 * @aux_data_len: Size of the auxiliary data buffer
 * @netfs_data: An arbitrary piece of data to be kept in the cookie to
 * represent the cache object to the netfs
 * @object_size: The initial size of object
 * @enable: Whether or not to enable a data cookie immediately
 *
 * This function is used to inform FS-Cache about part of an index hierarchy
 * that can be used to locate files.  This is done by requesting a cookie for
 * each index in the path to the file.
 *
 * See Documentation/filesystems/caching/netfs-api.txt for a complete
 * description.
 */
static inline
struct fscache_cookie *fscache_acquire_cookie(
	struct fscache_cookie *parent,
	const struct fscache_cookie_def *def,
	const void *index_key,
	size_t index_key_len,
	const void *aux_data,
	size_t aux_data_len,
	void *netfs_data,
	loff_t object_size,
	bool enable)
{
	if (fscache_cookie_valid(parent) && fscache_cookie_enabled(parent))
		return __fscache_acquire_cookie(parent, def,
						index_key, index_key_len,
						aux_data, aux_data_len,
						netfs_data, object_size, enable);
	else
		return NULL;
}

/**
 * fscache_relinquish_cookie - Return the cookie to the cache, maybe discarding
 * it
 * @cookie: The cookie being returned
 * @aux_data: The updated auxiliary data for the cookie (may be NULL)
 * @retire: True if the cache object the cookie represents is to be discarded
 *
 * This function returns a cookie to the cache, forcibly discarding the
 * associated cache object if retire is set to true.  The opportunity is
 * provided to update the auxiliary data in the cache before the object is
 * disconnected.
 *
 * See Documentation/filesystems/caching/netfs-api.txt for a complete
 * description.
 */
static inline
void fscache_relinquish_cookie(struct fscache_cookie *cookie,
			       const void *aux_data,
			       bool retire)
{
	if (fscache_cookie_valid(cookie))
		__fscache_relinquish_cookie(cookie, aux_data, retire);
}

/**
 * fscache_check_consistency - Request validation of a cache's auxiliary data
 * @cookie: The cookie representing the cache object
 * @aux_data: The updated auxiliary data for the cookie (may be NULL)
 *
 * Request an consistency check from fscache, which passes the request to the
 * backing cache.  The auxiliary data on the cookie will be updated first if
 * @aux_data is set.
 *
 * Returns 0 if consistent and -ESTALE if inconsistent.  May also
 * return -ENOMEM and -ERESTARTSYS.
 */
static inline
int fscache_check_consistency(struct fscache_cookie *cookie,
			      const void *aux_data)
{
	if (fscache_cookie_valid(cookie) && fscache_cookie_enabled(cookie))
		return __fscache_check_consistency(cookie, aux_data);
	else
		return 0;
}

/**
 * fscache_update_cookie - Request that a cache object be updated
 * @cookie: The cookie representing the cache object
 * @aux_data: The updated auxiliary data for the cookie (may be NULL)
 *
 * Request an update of the index data for the cache object associated with the
 * cookie.  The auxiliary data on the cookie will be updated first if @aux_data
 * is set.
 *
 * See Documentation/filesystems/caching/netfs-api.txt for a complete
 * description.
 */
static inline
void fscache_update_cookie(struct fscache_cookie *cookie, const void *aux_data)
{
	if (fscache_cookie_valid(cookie) && fscache_cookie_enabled(cookie))
		__fscache_update_cookie(cookie, aux_data);
}

/**
 * fscache_pin_cookie - Pin a data-storage cache object in its cache
 * @cookie: The cookie representing the cache object
 *
 * Permit data-storage cache objects to be pinned in the cache.
 *
 * See Documentation/filesystems/caching/netfs-api.txt for a complete
 * description.
 */
static inline
int fscache_pin_cookie(struct fscache_cookie *cookie)
{
	return -ENOBUFS;
}

/**
 * fscache_pin_cookie - Unpin a data-storage cache object in its cache
 * @cookie: The cookie representing the cache object
 *
 * Permit data-storage cache objects to be unpinned from the cache.
 *
 * See Documentation/filesystems/caching/netfs-api.txt for a complete
 * description.
 */
static inline
void fscache_unpin_cookie(struct fscache_cookie *cookie)
{
}

/**
 * fscache_attr_changed - Notify cache that an object's attributes changed
 * @cookie: The cookie representing the cache object
 *
 * Send a notification to the cache indicating that an object's attributes have
 * changed.  This includes the data size.  These attributes will be obtained
 * through the get_attr() cookie definition op.
 *
 * See Documentation/filesystems/caching/netfs-api.txt for a complete
 * description.
 */
static inline
int fscache_attr_changed(struct fscache_cookie *cookie)
{
	if (fscache_cookie_valid(cookie) && fscache_cookie_enabled(cookie))
		return __fscache_attr_changed(cookie);
	else
		return -ENOBUFS;
}

/**
 * fscache_invalidate - Notify cache that an object needs invalidation
 * @cookie: The cookie representing the cache object
 *
 * Notify the cache that an object is needs to be invalidated and that it
 * should abort any retrievals or stores it is doing on the cache.  The object
 * is then marked non-caching until such time as the invalidation is complete.
 *
 * This can be called with spinlocks held.
 *
 * See Documentation/filesystems/caching/netfs-api.txt for a complete
 * description.
 */
static inline
void fscache_invalidate(struct fscache_cookie *cookie)
{
	if (fscache_cookie_valid(cookie) && fscache_cookie_enabled(cookie))
		__fscache_invalidate(cookie);
}

/**
 * fscache_wait_on_invalidate - Wait for invalidation to complete
 * @cookie: The cookie representing the cache object
 *
 * Wait for the invalidation of an object to complete.
 *
 * See Documentation/filesystems/caching/netfs-api.txt for a complete
 * description.
 */
static inline
void fscache_wait_on_invalidate(struct fscache_cookie *cookie)
{
	if (fscache_cookie_valid(cookie))
		__fscache_wait_on_invalidate(cookie);
}

/**
 * fscache_reserve_space - Reserve data space for a cached object
 * @cookie: The cookie representing the cache object
 * @i_size: The amount of space to be reserved
 *
 * Reserve an amount of space in the cache for the cache object attached to a
 * cookie so that a write to that object within the space can always be
 * honoured.
 *
 * See Documentation/filesystems/caching/netfs-api.txt for a complete
 * description.
 */
static inline
int fscache_reserve_space(struct fscache_cookie *cookie, loff_t size)
{
	return -ENOBUFS;
}

/**
 * fscache_read_or_alloc_page - Read a page from the cache or allocate a block
 * in which to store it
 * @cookie: The cookie representing the cache object
 * @page: The netfs page to fill if possible
 * @end_io_func: The callback to invoke when and if the page is filled
 * @context: An arbitrary piece of data to pass on to end_io_func()
 * @gfp: The conditions under which memory allocation should be made
 *
 * Read a page from the cache, or if that's not possible make a potential
 * one-block reservation in the cache into which the page may be stored once
 * fetched from the server.
 *
 * If the page is not backed by the cache object, or if it there's some reason
 * it can't be, -ENOBUFS will be returned and nothing more will be done for
 * that page.
 *
 * Else, if that page is backed by the cache, a read will be initiated directly
 * to the netfs's page and 0 will be returned by this function.  The
 * end_io_func() callback will be invoked when the operation terminates on a
 * completion or failure.  Note that the callback may be invoked before the
 * return.
 *
 * Else, if the page is unbacked, -ENODATA is returned and a block may have
 * been allocated in the cache.
 *
 * See Documentation/filesystems/caching/netfs-api.txt for a complete
 * description.
 */
static inline
int fscache_read_or_alloc_page(struct fscache_cookie *cookie,
			       struct page *page,
			       fscache_rw_complete_t end_io_func,
			       void *context,
			       gfp_t gfp)
{
	if (fscache_cookie_valid(cookie) && fscache_cookie_enabled(cookie))
		return __fscache_read_or_alloc_page(cookie, page, end_io_func,
						    context, gfp);
	else
		return -ENOBUFS;
}

/**
 * fscache_read_or_alloc_pages - Read pages from the cache and/or allocate
 * blocks in which to store them
 * @cookie: The cookie representing the cache object
 * @mapping: The netfs inode mapping to which the pages will be attached
 * @pages: A list of potential netfs pages to be filled
 * @nr_pages: Number of pages to be read and/or allocated
 * @end_io_func: The callback to invoke when and if each page is filled
 * @context: An arbitrary piece of data to pass on to end_io_func()
 * @gfp: The conditions under which memory allocation should be made
 *
 * Read a set of pages from the cache, or if that's not possible, attempt to
 * make a potential one-block reservation for each page in the cache into which
 * that page may be stored once fetched from the server.
 *
 * If some pages are not backed by the cache object, or if it there's some
 * reason they can't be, -ENOBUFS will be returned and nothing more will be
 * done for that pages.
 *
 * Else, if some of the pages are backed by the cache, a read will be initiated
 * directly to the netfs's page and 0 will be returned by this function.  The
 * end_io_func() callback will be invoked when the operation terminates on a
 * completion or failure.  Note that the callback may be invoked before the
 * return.
 *
 * Else, if a page is unbacked, -ENODATA is returned and a block may have
 * been allocated in the cache.
 *
 * Because the function may want to return all of -ENOBUFS, -ENODATA and 0 in
 * regard to different pages, the return values are prioritised in that order.
 * Any pages submitted for reading are removed from the pages list.
 *
 * See Documentation/filesystems/caching/netfs-api.txt for a complete
 * description.
 */
static inline
int fscache_read_or_alloc_pages(struct fscache_cookie *cookie,
				struct address_space *mapping,
				struct list_head *pages,
				unsigned *nr_pages,
				fscache_rw_complete_t end_io_func,
				void *context,
				gfp_t gfp)
{
	if (fscache_cookie_valid(cookie) && fscache_cookie_enabled(cookie))
		return __fscache_read_or_alloc_pages(cookie, mapping, pages,
						     nr_pages, end_io_func,
						     context, gfp);
	else
		return -ENOBUFS;
}

/**
 * fscache_alloc_page - Allocate a block in which to store a page
 * @cookie: The cookie representing the cache object
 * @page: The netfs page to allocate a page for
 * @gfp: The conditions under which memory allocation should be made
 *
 * Request Allocation a block in the cache in which to store a netfs page
 * without retrieving any contents from the cache.
 *
 * If the page is not backed by a file then -ENOBUFS will be returned and
 * nothing more will be done, and no reservation will be made.
 *
 * Else, a block will be allocated if one wasn't already, and 0 will be
 * returned
 *
 * See Documentation/filesystems/caching/netfs-api.txt for a complete
 * description.
 */
static inline
int fscache_alloc_page(struct fscache_cookie *cookie,
		       struct page *page,
		       gfp_t gfp)
{
	if (fscache_cookie_valid(cookie) && fscache_cookie_enabled(cookie))
		return __fscache_alloc_page(cookie, page, gfp);
	else
		return -ENOBUFS;
}

/**
 * fscache_readpages_cancel - Cancel read/alloc on pages
 * @cookie: The cookie representing the inode's cache object.
 * @pages: The netfs pages that we canceled write on in readpages()
 *
 * Uncache/unreserve the pages reserved earlier in readpages() via
 * fscache_readpages_or_alloc() and similar.  In most successful caches in
 * readpages() this doesn't do anything.  In cases when the underlying netfs's
 * readahead failed we need to clean up the pagelist (unmark and uncache).
 *
 * This function may sleep as it may have to clean up disk state.
 */
static inline
void fscache_readpages_cancel(struct fscache_cookie *cookie,
			      struct list_head *pages)
{
	if (fscache_cookie_valid(cookie))
		__fscache_readpages_cancel(cookie, pages);
}

/**
 * fscache_write_page - Request storage of a page in the cache
 * @cookie: The cookie representing the cache object
 * @page: The netfs page to store
 * @object_size: Updated size of object
 * @gfp: The conditions under which memory allocation should be made
 *
 * Request the contents of the netfs page be written into the cache.  This
 * request may be ignored if no cache block is currently allocated, in which
 * case it will return -ENOBUFS.
 *
 * If a cache block was already allocated, a write will be initiated and 0 will
 * be returned.  The PG_fscache_write page bit is set immediately and will then
 * be cleared at the completion of the write to indicate the success or failure
 * of the operation.  Note that the completion may happen before the return.
 *
 * See Documentation/filesystems/caching/netfs-api.txt for a complete
 * description.
 */
static inline
int fscache_write_page(struct fscache_cookie *cookie,
		       struct page *page,
		       loff_t object_size,
		       gfp_t gfp)
{
	if (fscache_cookie_valid(cookie) && fscache_cookie_enabled(cookie))
		return __fscache_write_page(cookie, page, object_size, gfp);
	else
		return -ENOBUFS;
}

/**
 * fscache_uncache_page - Indicate that caching is no longer required on a page
 * @cookie: The cookie representing the cache object
 * @page: The netfs page that was being cached.
 *
 * Tell the cache that we no longer want a page to be cached and that it should
 * remove any knowledge of the netfs page it may have.
 *
 * Note that this cannot cancel any outstanding I/O operations between this
 * page and the cache.
 *
 * See Documentation/filesystems/caching/netfs-api.txt for a complete
 * description.
 */
static inline
void fscache_uncache_page(struct fscache_cookie *cookie,
			  struct page *page)
{
	if (fscache_cookie_valid(cookie))
		__fscache_uncache_page(cookie, page);
}

/**
 * fscache_check_page_write - Ask if a page is being writing to the cache
 * @cookie: The cookie representing the cache object
 * @page: The netfs page that is being cached.
 *
 * Ask the cache if a page is being written to the cache.
 *
 * See Documentation/filesystems/caching/netfs-api.txt for a complete
 * description.
 */
static inline
bool fscache_check_page_write(struct fscache_cookie *cookie,
			      struct page *page)
{
	if (fscache_cookie_valid(cookie))
		return __fscache_check_page_write(cookie, page);
	return false;
}

/**
 * fscache_wait_on_page_write - Wait for a page to complete writing to the cache
 * @cookie: The cookie representing the cache object
 * @page: The netfs page that is being cached.
 *
 * Ask the cache to wake us up when a page is no longer being written to the
 * cache.
 *
 * See Documentation/filesystems/caching/netfs-api.txt for a complete
 * description.
 */
static inline
void fscache_wait_on_page_write(struct fscache_cookie *cookie,
				struct page *page)
{
	if (fscache_cookie_valid(cookie))
		__fscache_wait_on_page_write(cookie, page);
}

/**
 * fscache_maybe_release_page - Consider releasing a page, cancelling a store
 * @cookie: The cookie representing the cache object
 * @page: The netfs page that is being cached.
 * @gfp: The gfp flags passed to releasepage()
 *
 * Consider releasing a page for the vmscan algorithm, on behalf of the netfs's
 * releasepage() call.  A storage request on the page may cancelled if it is
 * not currently being processed.
 *
 * The function returns true if the page no longer has a storage request on it,
 * and false if a storage request is left in place.  If true is returned, the
 * page will have been passed to fscache_uncache_page().  If false is returned
 * the page cannot be freed yet.
 */
static inline
bool fscache_maybe_release_page(struct fscache_cookie *cookie,
				struct page *page,
				gfp_t gfp)
{
	if (fscache_cookie_valid(cookie) && PageFsCache(page))
		return __fscache_maybe_release_page(cookie, page, gfp);
	return true;
}

/**
 * fscache_uncache_all_inode_pages - Uncache all an inode's pages
 * @cookie: The cookie representing the inode's cache object.
 * @inode: The inode to uncache pages from.
 *
 * Uncache all the pages in an inode that are marked PG_fscache, assuming them
 * to be associated with the given cookie.
 *
 * This function may sleep.  It will wait for pages that are being written out
 * and will wait whilst the PG_fscache mark is removed by the cache.
 */
static inline
void fscache_uncache_all_inode_pages(struct fscache_cookie *cookie,
				     struct inode *inode)
{
	if (fscache_cookie_valid(cookie))
		__fscache_uncache_all_inode_pages(cookie, inode);
}

/**
 * fscache_disable_cookie - Disable a cookie
 * @cookie: The cookie representing the cache object
 * @aux_data: The updated auxiliary data for the cookie (may be NULL)
 * @invalidate: Invalidate the backing object
 *
 * Disable a cookie from accepting further alloc, read, write, invalidate,
 * update or acquire operations.  Outstanding operations can still be waited
 * upon and pages can still be uncached and the cookie relinquished.
 *
 * This will not return until all outstanding operations have completed.
 *
 * If @invalidate is set, then the backing object will be invalidated and
 * detached, otherwise it will just be detached.
 *
 * If @aux_data is set, then auxiliary data will be updated from that.
 */
static inline
void fscache_disable_cookie(struct fscache_cookie *cookie,
			    const void *aux_data,
			    bool invalidate)
{
	if (fscache_cookie_valid(cookie) && fscache_cookie_enabled(cookie))
		__fscache_disable_cookie(cookie, aux_data, invalidate);
}

/**
 * fscache_enable_cookie - Reenable a cookie
 * @cookie: The cookie representing the cache object
 * @aux_data: The updated auxiliary data for the cookie (may be NULL)
 * @object_size: Current size of object
 * @can_enable: A function to permit enablement once lock is held
 * @data: Data for can_enable()
 *
 * Reenable a previously disabled cookie, allowing it to accept further alloc,
 * read, write, invalidate, update or acquire operations.  An attempt will be
 * made to immediately reattach the cookie to a backing object.  If @aux_data
 * is set, the auxiliary data attached to the cookie will be updated.
 *
 * The can_enable() function is called (if not NULL) once the enablement lock
 * is held to rule on whether enablement is still permitted to go ahead.
 */
static inline
void fscache_enable_cookie(struct fscache_cookie *cookie,
			   const void *aux_data,
			   loff_t object_size,
			   bool (*can_enable)(void *data),
			   void *data)
{
	if (fscache_cookie_valid(cookie) && !fscache_cookie_enabled(cookie))
		__fscache_enable_cookie(cookie, aux_data, object_size,
					can_enable, data);
}

#endif /* _LINUX_FSCACHE_H */
