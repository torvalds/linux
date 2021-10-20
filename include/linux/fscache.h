/* SPDX-License-Identifier: GPL-2.0-or-later */
/* General filesystem caching interface
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * NOTE!!! See:
 *
 *	Documentation/filesystems/caching/netfs-api.rst
 *
 * for a description of the network filesystem interface declared here.
 */

#ifndef _LINUX_FSCACHE_H
#define _LINUX_FSCACHE_H

#include <linux/fs.h>
#include <linux/netfs.h>
#include <linux/writeback.h>

#if defined(CONFIG_FSCACHE) || defined(CONFIG_FSCACHE_MODULE)
#define __fscache_available (1)
#define fscache_available() (1)
#define fscache_volume_valid(volume) (volume)
#define fscache_cookie_valid(cookie) (cookie)
#define fscache_resources_valid(cres) ((cres)->cache_priv)
#define fscache_cookie_enabled(cookie) (cookie && !test_bit(FSCACHE_COOKIE_DISABLED, &cookie->flags))
#else
#define __fscache_available (0)
#define fscache_available() (0)
#define fscache_volume_valid(volume) (0)
#define fscache_cookie_valid(cookie) (0)
#define fscache_resources_valid(cres) (false)
#define fscache_cookie_enabled(cookie) (0)
#endif

struct fscache_cookie;

#define FSCACHE_ADV_SINGLE_CHUNK	0x01 /* The object is a single chunk of data */
#define FSCACHE_ADV_WRITE_CACHE		0x00 /* Do cache if written to locally */
#define FSCACHE_ADV_WRITE_NOCACHE	0x02 /* Don't cache if written to locally */

#define FSCACHE_INVAL_DIO_WRITE		0x01 /* Invalidate due to DIO write */

enum fscache_want_state {
	FSCACHE_WANT_PARAMS,
	FSCACHE_WANT_WRITE,
	FSCACHE_WANT_READ,
};

/*
 * Data object state.
 */
enum fscache_cookie_state {
	FSCACHE_COOKIE_STATE_QUIESCENT,		/* The cookie is uncached */
	FSCACHE_COOKIE_STATE_LOOKING_UP,	/* The cache object is being looked up */
	FSCACHE_COOKIE_STATE_CREATING,		/* The cache object is being created */
	FSCACHE_COOKIE_STATE_ACTIVE,		/* The cache is active, readable and writable */
	FSCACHE_COOKIE_STATE_INVALIDATING,	/* The cache is being invalidated */
	FSCACHE_COOKIE_STATE_FAILED,		/* The cache failed, withdraw to clear */
	FSCACHE_COOKIE_STATE_LRU_DISCARDING,	/* The cookie is being discarded by the LRU */
	FSCACHE_COOKIE_STATE_WITHDRAWING,	/* The cookie is being withdrawn */
	FSCACHE_COOKIE_STATE_RELINQUISHING,	/* The cookie is being relinquished */
	FSCACHE_COOKIE_STATE_DROPPED,		/* The cookie has been dropped */
#define FSCACHE_COOKIE_STATE__NR (FSCACHE_COOKIE_STATE_DROPPED + 1)
} __attribute__((mode(byte)));

/*
 * Volume representation cookie.
 */
struct fscache_volume {
	refcount_t			ref;
	atomic_t			n_cookies;	/* Number of data cookies in volume */
	atomic_t			n_accesses;	/* Number of cache accesses in progress */
	unsigned int			debug_id;
	unsigned int			key_hash;	/* Hash of key string */
	char				*key;		/* Volume ID, eg. "afs@example.com@1234" */
	struct list_head		proc_link;	/* Link in /proc/fs/fscache/volumes */
	struct hlist_bl_node		hash_link;	/* Link in hash table */
	struct work_struct		work;
	struct fscache_cache		*cache;		/* The cache in which this resides */
	void				*cache_priv;	/* Cache private data */
	spinlock_t			lock;
	unsigned long			flags;
#define FSCACHE_VOLUME_RELINQUISHED	0	/* Volume is being cleaned up */
#define FSCACHE_VOLUME_INVALIDATE	1	/* Volume was invalidated */
#define FSCACHE_VOLUME_COLLIDED_WITH	2	/* Volume was collided with */
#define FSCACHE_VOLUME_ACQUIRE_PENDING	3	/* Volume is waiting to complete acquisition */
#define FSCACHE_VOLUME_CREATING		4	/* Volume is being created on disk */
};

/*
 * Data file representation cookie.
 * - a file will only appear in one cache
 * - a request to cache a file may or may not be honoured, subject to
 *   constraints such as disk space
 * - indices are created on disk just-in-time
 */
struct fscache_cookie {
	refcount_t			ref;
	atomic_t			n_active;	/* number of active users of cookie */
	atomic_t			n_accesses;	/* Number of cache accesses in progress */
	unsigned int			debug_id;
	unsigned int			inval_counter;	/* Number of invalidations made */
	spinlock_t			lock;
	struct fscache_volume		*volume;	/* Parent volume of this file. */
	void				*cache_priv;	/* Cache-side representation */
	struct hlist_bl_node		hash_link;	/* Link in hash table */
	struct list_head		proc_link;	/* Link in proc list */
	struct list_head		commit_link;	/* Link in commit queue */
	struct work_struct		work;		/* Commit/relinq/withdraw work */
	loff_t				object_size;	/* Size of the netfs object */
	unsigned long			unused_at;	/* Time at which unused (jiffies) */
	unsigned long			flags;
#define FSCACHE_COOKIE_RELINQUISHED	0		/* T if cookie has been relinquished */
#define FSCACHE_COOKIE_RETIRED		1		/* T if this cookie has retired on relinq */
#define FSCACHE_COOKIE_IS_CACHING	2		/* T if this cookie is cached */
#define FSCACHE_COOKIE_NO_DATA_TO_READ	3		/* T if this cookie has nothing to read */
#define FSCACHE_COOKIE_NEEDS_UPDATE	4		/* T if attrs have been updated */
#define FSCACHE_COOKIE_HAS_BEEN_CACHED	5		/* T if cookie needs withdraw-on-relinq */
#define FSCACHE_COOKIE_DISABLED		6		/* T if cookie has been disabled */
#define FSCACHE_COOKIE_LOCAL_WRITE	7		/* T if cookie has been modified locally */
#define FSCACHE_COOKIE_NO_ACCESS_WAKE	8		/* T if no wake when n_accesses goes 0 */
#define FSCACHE_COOKIE_DO_RELINQUISH	9		/* T if this cookie needs relinquishment */
#define FSCACHE_COOKIE_DO_WITHDRAW	10		/* T if this cookie needs withdrawing */
#define FSCACHE_COOKIE_DO_LRU_DISCARD	11		/* T if this cookie needs LRU discard */
#define FSCACHE_COOKIE_DO_PREP_TO_WRITE	12		/* T if cookie needs write preparation */
#define FSCACHE_COOKIE_HAVE_DATA	13		/* T if this cookie has data stored */
#define FSCACHE_COOKIE_IS_HASHED	14		/* T if this cookie is hashed */

	enum fscache_cookie_state	state;
	u8				advice;		/* FSCACHE_ADV_* */
	u8				key_len;	/* Length of index key */
	u8				aux_len;	/* Length of auxiliary data */
	u32				key_hash;	/* Hash of volume, key, len */
	union {
		void			*key;		/* Index key */
		u8			inline_key[16];	/* - If the key is short enough */
	};
	union {
		void			*aux;		/* Auxiliary data */
		u8			inline_aux[8];	/* - If the aux data is short enough */
	};
};

/*
 * slow-path functions for when there is actually caching available, and the
 * netfs does actually have a valid token
 * - these are not to be called directly
 * - these are undefined symbols when FS-Cache is not configured and the
 *   optimiser takes care of not using them
 */
extern struct fscache_volume *__fscache_acquire_volume(const char *, const char *,
						       const void *, size_t);
extern void __fscache_relinquish_volume(struct fscache_volume *, const void *, bool);

extern struct fscache_cookie *__fscache_acquire_cookie(
	struct fscache_volume *,
	u8,
	const void *, size_t,
	const void *, size_t,
	loff_t);
extern void __fscache_use_cookie(struct fscache_cookie *, bool);
extern void __fscache_unuse_cookie(struct fscache_cookie *, const void *, const loff_t *);
extern void __fscache_relinquish_cookie(struct fscache_cookie *, bool);
extern void __fscache_resize_cookie(struct fscache_cookie *, loff_t);
extern void __fscache_invalidate(struct fscache_cookie *, const void *, loff_t, unsigned int);
extern int __fscache_begin_read_operation(struct netfs_cache_resources *, struct fscache_cookie *);

extern void __fscache_write_to_cache(struct fscache_cookie *, struct address_space *,
				     loff_t, size_t, loff_t, netfs_io_terminated_t, void *,
				     bool);
extern void __fscache_clear_page_bits(struct address_space *, loff_t, size_t);

/**
 * fscache_acquire_volume - Register a volume as desiring caching services
 * @volume_key: An identification string for the volume
 * @cache_name: The name of the cache to use (or NULL for the default)
 * @coherency_data: Piece of arbitrary coherency data to check (or NULL)
 * @coherency_len: The size of the coherency data
 *
 * Register a volume as desiring caching services if they're available.  The
 * caller must provide an identifier for the volume and may also indicate which
 * cache it should be in.  If a preexisting volume entry is found in the cache,
 * the coherency data must match otherwise the entry will be invalidated.
 *
 * Returns a cookie pointer on success, -ENOMEM if out of memory or -EBUSY if a
 * cache volume of that name is already acquired.  Note that "NULL" is a valid
 * cookie pointer and can be returned if caching is refused.
 */
static inline
struct fscache_volume *fscache_acquire_volume(const char *volume_key,
					      const char *cache_name,
					      const void *coherency_data,
					      size_t coherency_len)
{
	if (!fscache_available())
		return NULL;
	return __fscache_acquire_volume(volume_key, cache_name,
					coherency_data, coherency_len);
}

/**
 * fscache_relinquish_volume - Cease caching a volume
 * @volume: The volume cookie
 * @coherency_data: Piece of arbitrary coherency data to set (or NULL)
 * @invalidate: True if the volume should be invalidated
 *
 * Indicate that a filesystem no longer desires caching services for a volume.
 * The caller must have relinquished all file cookies prior to calling this.
 * The stored coherency data is updated.
 */
static inline
void fscache_relinquish_volume(struct fscache_volume *volume,
			       const void *coherency_data,
			       bool invalidate)
{
	if (fscache_volume_valid(volume))
		__fscache_relinquish_volume(volume, coherency_data, invalidate);
}

/**
 * fscache_acquire_cookie - Acquire a cookie to represent a cache object
 * @volume: The volume in which to locate/create this cookie
 * @advice: Advice flags (FSCACHE_COOKIE_ADV_*)
 * @index_key: The index key for this cookie
 * @index_key_len: Size of the index key
 * @aux_data: The auxiliary data for the cookie (may be NULL)
 * @aux_data_len: Size of the auxiliary data buffer
 * @object_size: The initial size of object
 *
 * Acquire a cookie to represent a data file within the given cache volume.
 *
 * See Documentation/filesystems/caching/netfs-api.rst for a complete
 * description.
 */
static inline
struct fscache_cookie *fscache_acquire_cookie(struct fscache_volume *volume,
					      u8 advice,
					      const void *index_key,
					      size_t index_key_len,
					      const void *aux_data,
					      size_t aux_data_len,
					      loff_t object_size)
{
	if (!fscache_volume_valid(volume))
		return NULL;
	return __fscache_acquire_cookie(volume, advice,
					index_key, index_key_len,
					aux_data, aux_data_len,
					object_size);
}

/**
 * fscache_use_cookie - Request usage of cookie attached to an object
 * @object: Object description
 * @will_modify: If cache is expected to be modified locally
 *
 * Request usage of the cookie attached to an object.  The caller should tell
 * the cache if the object's contents are about to be modified locally and then
 * the cache can apply the policy that has been set to handle this case.
 */
static inline void fscache_use_cookie(struct fscache_cookie *cookie,
				      bool will_modify)
{
	if (fscache_cookie_valid(cookie))
		__fscache_use_cookie(cookie, will_modify);
}

/**
 * fscache_unuse_cookie - Cease usage of cookie attached to an object
 * @object: Object description
 * @aux_data: Updated auxiliary data (or NULL)
 * @object_size: Revised size of the object (or NULL)
 *
 * Cease usage of the cookie attached to an object.  When the users count
 * reaches zero then the cookie relinquishment will be permitted to proceed.
 */
static inline void fscache_unuse_cookie(struct fscache_cookie *cookie,
					const void *aux_data,
					const loff_t *object_size)
{
	if (fscache_cookie_valid(cookie))
		__fscache_unuse_cookie(cookie, aux_data, object_size);
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
 * See Documentation/filesystems/caching/netfs-api.rst for a complete
 * description.
 */
static inline
void fscache_relinquish_cookie(struct fscache_cookie *cookie, bool retire)
{
	if (fscache_cookie_valid(cookie))
		__fscache_relinquish_cookie(cookie, retire);
}

/*
 * Find the auxiliary data on a cookie.
 */
static inline void *fscache_get_aux(struct fscache_cookie *cookie)
{
	if (cookie->aux_len <= sizeof(cookie->inline_aux))
		return cookie->inline_aux;
	else
		return cookie->aux;
}

/*
 * Update the auxiliary data on a cookie.
 */
static inline
void fscache_update_aux(struct fscache_cookie *cookie,
			const void *aux_data, const loff_t *object_size)
{
	void *p = fscache_get_aux(cookie);

	if (aux_data && p)
		memcpy(p, aux_data, cookie->aux_len);
	if (object_size)
		cookie->object_size = *object_size;
}

#ifdef CONFIG_FSCACHE_STATS
extern atomic_t fscache_n_updates;
#endif

static inline
void __fscache_update_cookie(struct fscache_cookie *cookie, const void *aux_data,
			     const loff_t *object_size)
{
#ifdef CONFIG_FSCACHE_STATS
	atomic_inc(&fscache_n_updates);
#endif
	fscache_update_aux(cookie, aux_data, object_size);
	smp_wmb();
	set_bit(FSCACHE_COOKIE_NEEDS_UPDATE, &cookie->flags);
}

/**
 * fscache_update_cookie - Request that a cache object be updated
 * @cookie: The cookie representing the cache object
 * @aux_data: The updated auxiliary data for the cookie (may be NULL)
 * @object_size: The current size of the object (may be NULL)
 *
 * Request an update of the index data for the cache object associated with the
 * cookie.  The auxiliary data on the cookie will be updated first if @aux_data
 * is set and the object size will be updated and the object possibly trimmed
 * if @object_size is set.
 *
 * See Documentation/filesystems/caching/netfs-api.rst for a complete
 * description.
 */
static inline
void fscache_update_cookie(struct fscache_cookie *cookie, const void *aux_data,
			   const loff_t *object_size)
{
	if (fscache_cookie_enabled(cookie))
		__fscache_update_cookie(cookie, aux_data, object_size);
}

/**
 * fscache_resize_cookie - Request that a cache object be resized
 * @cookie: The cookie representing the cache object
 * @new_size: The new size of the object (may be NULL)
 *
 * Request that the size of an object be changed.
 *
 * See Documentation/filesystems/caching/netfs-api.txt for a complete
 * description.
 */
static inline
void fscache_resize_cookie(struct fscache_cookie *cookie, loff_t new_size)
{
	if (fscache_cookie_enabled(cookie))
		__fscache_resize_cookie(cookie, new_size);
}

/**
 * fscache_invalidate - Notify cache that an object needs invalidation
 * @cookie: The cookie representing the cache object
 * @aux_data: The updated auxiliary data for the cookie (may be NULL)
 * @size: The revised size of the object.
 * @flags: Invalidation flags (FSCACHE_INVAL_*)
 *
 * Notify the cache that an object is needs to be invalidated and that it
 * should abort any retrievals or stores it is doing on the cache.  This
 * increments inval_counter on the cookie which can be used by the caller to
 * reconsider I/O requests as they complete.
 *
 * If @flags has FSCACHE_INVAL_DIO_WRITE set, this indicates that this is due
 * to a direct I/O write and will cause caching to be disabled on this cookie
 * until it is completely unused.
 *
 * See Documentation/filesystems/caching/netfs-api.rst for a complete
 * description.
 */
static inline
void fscache_invalidate(struct fscache_cookie *cookie,
			const void *aux_data, loff_t size, unsigned int flags)
{
	if (fscache_cookie_enabled(cookie))
		__fscache_invalidate(cookie, aux_data, size, flags);
}

/**
 * fscache_operation_valid - Return true if operations resources are usable
 * @cres: The resources to check.
 *
 * Returns a pointer to the operations table if usable or NULL if not.
 */
static inline
const struct netfs_cache_ops *fscache_operation_valid(const struct netfs_cache_resources *cres)
{
	return fscache_resources_valid(cres) ? cres->ops : NULL;
}

/**
 * fscache_begin_read_operation - Begin a read operation for the netfs lib
 * @cres: The cache resources for the read being performed
 * @cookie: The cookie representing the cache object
 *
 * Begin a read operation on behalf of the netfs helper library.  @cres
 * indicates the cache resources to which the operation state should be
 * attached; @cookie indicates the cache object that will be accessed.
 *
 * This is intended to be called from the ->begin_cache_operation() netfs lib
 * operation as implemented by the network filesystem.
 *
 * @cres->inval_counter is set from @cookie->inval_counter for comparison at
 * the end of the operation.  This allows invalidation during the operation to
 * be detected by the caller.
 *
 * Returns:
 * * 0		- Success
 * * -ENOBUFS	- No caching available
 * * Other error code from the cache, such as -ENOMEM.
 */
static inline
int fscache_begin_read_operation(struct netfs_cache_resources *cres,
				 struct fscache_cookie *cookie)
{
	if (fscache_cookie_enabled(cookie))
		return __fscache_begin_read_operation(cres, cookie);
	return -ENOBUFS;
}

/**
 * fscache_read - Start a read from the cache.
 * @cres: The cache resources to use
 * @start_pos: The beginning file offset in the cache file
 * @iter: The buffer to fill - and also the length
 * @read_hole: How to handle a hole in the data.
 * @term_func: The function to call upon completion
 * @term_func_priv: The private data for @term_func
 *
 * Start a read from the cache.  @cres indicates the cache object to read from
 * and must be obtained by a call to fscache_begin_operation() beforehand.
 *
 * The data is read into the iterator, @iter, and that also indicates the size
 * of the operation.  @start_pos is the start position in the file, though if
 * @seek_data is set appropriately, the cache can use SEEK_DATA to find the
 * next piece of data, writing zeros for the hole into the iterator.
 *
 * Upon termination of the operation, @term_func will be called and supplied
 * with @term_func_priv plus the amount of data written, if successful, or the
 * error code otherwise.
 *
 * @read_hole indicates how a partially populated region in the cache should be
 * handled.  It can be one of a number of settings:
 *
 *	NETFS_READ_HOLE_IGNORE - Just try to read (may return a short read).
 *
 *	NETFS_READ_HOLE_CLEAR - Seek for data, clearing the part of the buffer
 *				skipped over, then do as for IGNORE.
 *
 *	NETFS_READ_HOLE_FAIL - Give ENODATA if we encounter a hole.
 */
static inline
int fscache_read(struct netfs_cache_resources *cres,
		 loff_t start_pos,
		 struct iov_iter *iter,
		 enum netfs_read_from_hole read_hole,
		 netfs_io_terminated_t term_func,
		 void *term_func_priv)
{
	const struct netfs_cache_ops *ops = fscache_operation_valid(cres);
	return ops->read(cres, start_pos, iter, read_hole,
			 term_func, term_func_priv);
}

/**
 * fscache_write - Start a write to the cache.
 * @cres: The cache resources to use
 * @start_pos: The beginning file offset in the cache file
 * @iter: The data to write - and also the length
 * @term_func: The function to call upon completion
 * @term_func_priv: The private data for @term_func
 *
 * Start a write to the cache.  @cres indicates the cache object to write to and
 * must be obtained by a call to fscache_begin_operation() beforehand.
 *
 * The data to be written is obtained from the iterator, @iter, and that also
 * indicates the size of the operation.  @start_pos is the start position in
 * the file.
 *
 * Upon termination of the operation, @term_func will be called and supplied
 * with @term_func_priv plus the amount of data written, if successful, or the
 * error code otherwise.
 */
static inline
int fscache_write(struct netfs_cache_resources *cres,
		  loff_t start_pos,
		  struct iov_iter *iter,
		  netfs_io_terminated_t term_func,
		  void *term_func_priv)
{
	const struct netfs_cache_ops *ops = fscache_operation_valid(cres);
	return ops->write(cres, start_pos, iter, term_func, term_func_priv);
}

/**
 * fscache_clear_page_bits - Clear the PG_fscache bits from a set of pages
 * @cookie: The cookie representing the cache object
 * @mapping: The netfs inode to use as the source
 * @start: The start position in @mapping
 * @len: The amount of data to unlock
 * @caching: If PG_fscache has been set
 *
 * Clear the PG_fscache flag from a sequence of pages and wake up anyone who's
 * waiting.
 */
static inline void fscache_clear_page_bits(struct fscache_cookie *cookie,
					   struct address_space *mapping,
					   loff_t start, size_t len,
					   bool caching)
{
	if (caching)
		__fscache_clear_page_bits(mapping, start, len);
}

/**
 * fscache_write_to_cache - Save a write to the cache and clear PG_fscache
 * @cookie: The cookie representing the cache object
 * @mapping: The netfs inode to use as the source
 * @start: The start position in @mapping
 * @len: The amount of data to write back
 * @i_size: The new size of the inode
 * @term_func: The function to call upon completion
 * @term_func_priv: The private data for @term_func
 * @caching: If PG_fscache has been set
 *
 * Helper function for a netfs to write dirty data from an inode into the cache
 * object that's backing it.
 *
 * @start and @len describe the range of the data.  This does not need to be
 * page-aligned, but to satisfy DIO requirements, the cache may expand it up to
 * the page boundaries on either end.  All the pages covering the range must be
 * marked with PG_fscache.
 *
 * If given, @term_func will be called upon completion and supplied with
 * @term_func_priv.  Note that the PG_fscache flags will have been cleared by
 * this point, so the netfs must retain its own pin on the mapping.
 */
static inline void fscache_write_to_cache(struct fscache_cookie *cookie,
					  struct address_space *mapping,
					  loff_t start, size_t len, loff_t i_size,
					  netfs_io_terminated_t term_func,
					  void *term_func_priv,
					  bool caching)
{
	if (caching)
		__fscache_write_to_cache(cookie, mapping, start, len, i_size,
					 term_func, term_func_priv, caching);
	else if (term_func)
		term_func(term_func_priv, -ENOBUFS, false);

}

#if __fscache_available
extern int fscache_set_page_dirty(struct page *page, struct fscache_cookie *cookie);
#else
#define fscache_set_page_dirty(PAGE, COOKIE) (__set_page_dirty_nobuffers((PAGE)))
#endif

/**
 * fscache_unpin_writeback - Unpin writeback resources
 * @wbc: The writeback control
 * @cookie: The cookie referring to the cache object
 *
 * Unpin the writeback resources pinned by fscache_set_page_dirty().  This is
 * intended to be called by the netfs's ->write_inode() method.
 */
static inline void fscache_unpin_writeback(struct writeback_control *wbc,
					   struct fscache_cookie *cookie)
{
	if (wbc->unpinned_fscache_wb)
		fscache_unuse_cookie(cookie, NULL, NULL);
}

/**
 * fscache_clear_inode_writeback - Clear writeback resources pinned by an inode
 * @cookie: The cookie referring to the cache object
 * @inode: The inode to clean up
 * @aux: Auxiliary data to apply to the inode
 *
 * Clear any writeback resources held by an inode when the inode is evicted.
 * This must be called before clear_inode() is called.
 */
static inline void fscache_clear_inode_writeback(struct fscache_cookie *cookie,
						 struct inode *inode,
						 const void *aux)
{
	if (inode->i_state & I_PINNING_FSCACHE_WB) {
		loff_t i_size = i_size_read(inode);
		fscache_unuse_cookie(cookie, aux, &i_size);
	}
}

/**
 * fscache_note_page_release - Note that a netfs page got released
 * @cookie: The cookie corresponding to the file
 *
 * Note that a page that has been copied to the cache has been released.  This
 * means that future reads will need to look in the cache to see if it's there.
 */
static inline
void fscache_note_page_release(struct fscache_cookie *cookie)
{
	if (cookie &&
	    test_bit(FSCACHE_COOKIE_HAVE_DATA, &cookie->flags) &&
	    test_bit(FSCACHE_COOKIE_NO_DATA_TO_READ, &cookie->flags))
		clear_bit(FSCACHE_COOKIE_NO_DATA_TO_READ, &cookie->flags);
}

#endif /* _LINUX_FSCACHE_H */
