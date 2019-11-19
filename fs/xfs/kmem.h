// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_SUPPORT_KMEM_H__
#define __XFS_SUPPORT_KMEM_H__

#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>

/*
 * General memory allocation interfaces
 */

typedef unsigned __bitwise xfs_km_flags_t;
#define KM_NOFS		((__force xfs_km_flags_t)0x0004u)
#define KM_MAYFAIL	((__force xfs_km_flags_t)0x0008u)
#define KM_ZERO		((__force xfs_km_flags_t)0x0010u)

/*
 * We use a special process flag to avoid recursive callbacks into
 * the filesystem during transactions.  We will also issue our own
 * warnings, so we explicitly skip any generic ones (silly of us).
 */
static inline gfp_t
kmem_flags_convert(xfs_km_flags_t flags)
{
	gfp_t	lflags;

	BUG_ON(flags & ~(KM_NOFS|KM_MAYFAIL|KM_ZERO));

	lflags = GFP_KERNEL | __GFP_NOWARN;
	if (flags & KM_NOFS)
		lflags &= ~__GFP_FS;

	/*
	 * Default page/slab allocator behavior is to retry for ever
	 * for small allocations. We can override this behavior by using
	 * __GFP_RETRY_MAYFAIL which will tell the allocator to retry as long
	 * as it is feasible but rather fail than retry forever for all
	 * request sizes.
	 */
	if (flags & KM_MAYFAIL)
		lflags |= __GFP_RETRY_MAYFAIL;

	if (flags & KM_ZERO)
		lflags |= __GFP_ZERO;

	return lflags;
}

extern void *kmem_alloc(size_t, xfs_km_flags_t);
extern void *kmem_alloc_io(size_t size, int align_mask, xfs_km_flags_t flags);
extern void *kmem_alloc_large(size_t size, xfs_km_flags_t);
extern void *kmem_realloc(const void *, size_t, xfs_km_flags_t);
static inline void  kmem_free(const void *ptr)
{
	kvfree(ptr);
}


static inline void *
kmem_zalloc(size_t size, xfs_km_flags_t flags)
{
	return kmem_alloc(size, flags | KM_ZERO);
}

static inline void *
kmem_zalloc_large(size_t size, xfs_km_flags_t flags)
{
	return kmem_alloc_large(size, flags | KM_ZERO);
}

/*
 * Zone interfaces
 */

#define KM_ZONE_HWALIGN	SLAB_HWCACHE_ALIGN
#define KM_ZONE_RECLAIM	SLAB_RECLAIM_ACCOUNT
#define KM_ZONE_SPREAD	SLAB_MEM_SPREAD
#define KM_ZONE_ACCOUNT	SLAB_ACCOUNT

#define kmem_zone	kmem_cache
#define kmem_zone_t	struct kmem_cache

static inline kmem_zone_t *
kmem_zone_init(int size, char *zone_name)
{
	return kmem_cache_create(zone_name, size, 0, 0, NULL);
}

static inline kmem_zone_t *
kmem_zone_init_flags(int size, char *zone_name, slab_flags_t flags,
		     void (*construct)(void *))
{
	return kmem_cache_create(zone_name, size, 0, flags, construct);
}

static inline void
kmem_zone_free(kmem_zone_t *zone, void *ptr)
{
	kmem_cache_free(zone, ptr);
}

static inline void
kmem_zone_destroy(kmem_zone_t *zone)
{
	kmem_cache_destroy(zone);
}

extern void *kmem_zone_alloc(kmem_zone_t *, xfs_km_flags_t);

static inline void *
kmem_zone_zalloc(kmem_zone_t *zone, xfs_km_flags_t flags)
{
	return kmem_zone_alloc(zone, flags | KM_ZERO);
}

static inline struct page *
kmem_to_page(void *addr)
{
	if (is_vmalloc_addr(addr))
		return vmalloc_to_page(addr);
	return virt_to_page(addr);
}

#endif /* __XFS_SUPPORT_KMEM_H__ */
