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

#if defined(CONFIG_FSCACHE) || defined(CONFIG_FSCACHE_MODULE)
#define __fscache_available (1)
#define fscache_available() (1)
#define fscache_volume_valid(volume) (volume)
#define fscache_cookie_valid(cookie) (cookie)
#define fscache_cookie_enabled(cookie) (cookie)
#else
#define __fscache_available (0)
#define fscache_available() (0)
#define fscache_volume_valid(volume) (0)
#define fscache_cookie_valid(cookie) (0)
#define fscache_cookie_enabled(cookie) (0)
#endif

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
 * slow-path functions for when there is actually caching available, and the
 * netfs does actually have a valid token
 * - these are not to be called directly
 * - these are undefined symbols when FS-Cache is not configured and the
 *   optimiser takes care of not using them
 */
extern struct fscache_volume *__fscache_acquire_volume(const char *, const char *,
						       const void *, size_t);
extern void __fscache_relinquish_volume(struct fscache_volume *, const void *, bool);

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

#endif /* _LINUX_FSCACHE_H */
