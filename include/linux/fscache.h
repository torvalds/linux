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

	/* get an index key
	 * - should store the key data in the buffer
	 * - should return the amount of amount stored
	 * - not permitted to return an error
	 * - the netfs data from the cookie being used as the source is
	 *   presented
	 */
	uint16_t (*get_key)(const void *cookie_netfs_data,
			    void *buffer,
			    uint16_t bufmax);

	/* get certain file attributes from the netfs data
	 * - this function can be absent for an index
	 * - not permitted to return an error
	 * - the netfs data from the cookie being used as the source is
	 *   presented
	 */
	void (*get_attr)(const void *cookie_netfs_data, uint64_t *size);

	/* get the auxilliary data from netfs data
	 * - this function can be absent if the index carries no state data
	 * - should store the auxilliary data in the buffer
	 * - should return the amount of amount stored
	 * - not permitted to return an error
	 * - the netfs data from the cookie being used as the source is
	 *   presented
	 */
	uint16_t (*get_aux)(const void *cookie_netfs_data,
			    void *buffer,
			    uint16_t bufmax);

	/* consult the netfs about the state of an object
	 * - this function can be absent if the index carries no state data
	 * - the netfs data from the cookie being used as the target is
	 *   presented, as is the auxilliary data
	 */
	enum fscache_checkaux (*check_aux)(void *cookie_netfs_data,
					   const void *data,
					   uint16_t datalen);

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

	/* indicate pages that now have cache metadata retained
	 * - this function should mark the specified pages as now being cached
	 * - the pages will have been marked with PG_fscache before this is
	 *   called, so this is optional
	 */
	void (*mark_pages_cached)(void *cookie_netfs_data,
				  struct address_space *mapping,
				  struct pagevec *cached_pvec);

	/* indicate the cookie is no longer cached
	 * - this function is called when the backing store currently caching
	 *   a cookie is removed
	 * - the netfs should use this to clean up any markers indicating
	 *   cached pages
	 * - this is mandatory for any object that may have data
	 */
	void (*now_uncached)(void *cookie_netfs_data);
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
	struct list_head		link;		/* internal link */
};

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
	void *);
extern void __fscache_relinquish_cookie(struct fscache_cookie *, int);
extern void __fscache_update_cookie(struct fscache_cookie *);
extern int __fscache_attr_changed(struct fscache_cookie *);
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
extern int __fscache_write_page(struct fscache_cookie *, struct page *, gfp_t);
extern void __fscache_uncache_page(struct fscache_cookie *, struct page *);
extern bool __fscache_check_page_write(struct fscache_cookie *, struct page *);
extern void __fscache_wait_on_page_write(struct fscache_cookie *, struct page *);
extern bool __fscache_maybe_release_page(struct fscache_cookie *, struct page *,
					 gfp_t);

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
 * @netfs_data: An arbitrary piece of data to be kept in the cookie to
 * represent the cache object to the netfs
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
	void *netfs_data)
{
	if (fscache_cookie_valid(parent))
		return __fscache_acquire_cookie(parent, def, netfs_data);
	else
		return NULL;
}

/**
 * fscache_relinquish_cookie - Return the cookie to the cache, maybe discarding
 * it
 * @cookie: The cookie being returned
 * @retire: True if the cache object the cookie represents is to be discarded
 *
 * This function returns a cookie to the cache, forcibly discarding the
 * associated cache object if retire is set to true.
 *
 * See Documentation/filesystems/caching/netfs-api.txt for a complete
 * description.
 */
static inline
void fscache_relinquish_cookie(struct fscache_cookie *cookie, int retire)
{
	if (fscache_cookie_valid(cookie))
		__fscache_relinquish_cookie(cookie, retire);
}

/**
 * fscache_update_cookie - Request that a cache object be updated
 * @cookie: The cookie representing the cache object
 *
 * Request an update of the index data for the cache object associated with the
 * cookie.
 *
 * See Documentation/filesystems/caching/netfs-api.txt for a complete
 * description.
 */
static inline
void fscache_update_cookie(struct fscache_cookie *cookie)
{
	if (fscache_cookie_valid(cookie))
		__fscache_update_cookie(cookie);
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
	if (fscache_cookie_valid(cookie))
		return __fscache_attr_changed(cookie);
	else
		return -ENOBUFS;
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
	if (fscache_cookie_valid(cookie))
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
	if (fscache_cookie_valid(cookie))
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
	if (fscache_cookie_valid(cookie))
		return __fscache_alloc_page(cookie, page, gfp);
	else
		return -ENOBUFS;
}

/**
 * fscache_write_page - Request storage of a page in the cache
 * @cookie: The cookie representing the cache object
 * @page: The netfs page to store
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
		       gfp_t gfp)
{
	if (fscache_cookie_valid(cookie))
		return __fscache_write_page(cookie, page, gfp);
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
	return false;
}

#endif /* _LINUX_FSCACHE_H */
