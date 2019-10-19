// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include <linux/backing-dev.h>
#include "xfs_message.h"
#include "xfs_trace.h"

void *
kmem_alloc(size_t size, xfs_km_flags_t flags)
{
	int	retries = 0;
	gfp_t	lflags = kmem_flags_convert(flags);
	void	*ptr;

	trace_kmem_alloc(size, flags, _RET_IP_);

	do {
		ptr = kmalloc(size, lflags);
		if (ptr || (flags & KM_MAYFAIL))
			return ptr;
		if (!(++retries % 100))
			xfs_err(NULL,
	"%s(%u) possible memory allocation deadlock size %u in %s (mode:0x%x)",
				current->comm, current->pid,
				(unsigned int)size, __func__, lflags);
		congestion_wait(BLK_RW_ASYNC, HZ/50);
	} while (1);
}


/*
 * __vmalloc() will allocate data pages and auxillary structures (e.g.
 * pagetables) with GFP_KERNEL, yet we may be under GFP_NOFS context here. Hence
 * we need to tell memory reclaim that we are in such a context via
 * PF_MEMALLOC_NOFS to prevent memory reclaim re-entering the filesystem here
 * and potentially deadlocking.
 */
static void *
__kmem_vmalloc(size_t size, xfs_km_flags_t flags)
{
	unsigned nofs_flag = 0;
	void	*ptr;
	gfp_t	lflags = kmem_flags_convert(flags);

	if (flags & KM_NOFS)
		nofs_flag = memalloc_nofs_save();

	ptr = __vmalloc(size, lflags, PAGE_KERNEL);

	if (flags & KM_NOFS)
		memalloc_nofs_restore(nofs_flag);

	return ptr;
}

/*
 * Same as kmem_alloc_large, except we guarantee the buffer returned is aligned
 * to the @align_mask. We only guarantee alignment up to page size, we'll clamp
 * alignment at page size if it is larger. vmalloc always returns a PAGE_SIZE
 * aligned region.
 */
void *
kmem_alloc_io(size_t size, int align_mask, xfs_km_flags_t flags)
{
	void	*ptr;

	trace_kmem_alloc_io(size, flags, _RET_IP_);

	if (WARN_ON_ONCE(align_mask >= PAGE_SIZE))
		align_mask = PAGE_SIZE - 1;

	ptr = kmem_alloc(size, flags | KM_MAYFAIL);
	if (ptr) {
		if (!((uintptr_t)ptr & align_mask))
			return ptr;
		kfree(ptr);
	}
	return __kmem_vmalloc(size, flags);
}

void *
kmem_alloc_large(size_t size, xfs_km_flags_t flags)
{
	void	*ptr;

	trace_kmem_alloc_large(size, flags, _RET_IP_);

	ptr = kmem_alloc(size, flags | KM_MAYFAIL);
	if (ptr)
		return ptr;
	return __kmem_vmalloc(size, flags);
}

void *
kmem_realloc(const void *old, size_t newsize, xfs_km_flags_t flags)
{
	int	retries = 0;
	gfp_t	lflags = kmem_flags_convert(flags);
	void	*ptr;

	trace_kmem_realloc(newsize, flags, _RET_IP_);

	do {
		ptr = krealloc(old, newsize, lflags);
		if (ptr || (flags & KM_MAYFAIL))
			return ptr;
		if (!(++retries % 100))
			xfs_err(NULL,
	"%s(%u) possible memory allocation deadlock size %zu in %s (mode:0x%x)",
				current->comm, current->pid,
				newsize, __func__, lflags);
		congestion_wait(BLK_RW_ASYNC, HZ/50);
	} while (1);
}

void *
kmem_zone_alloc(kmem_zone_t *zone, xfs_km_flags_t flags)
{
	int	retries = 0;
	gfp_t	lflags = kmem_flags_convert(flags);
	void	*ptr;

	trace_kmem_zone_alloc(kmem_cache_size(zone), flags, _RET_IP_);
	do {
		ptr = kmem_cache_alloc(zone, lflags);
		if (ptr || (flags & KM_MAYFAIL))
			return ptr;
		if (!(++retries % 100))
			xfs_err(NULL,
		"%s(%u) possible memory allocation deadlock in %s (mode:0x%x)",
				current->comm, current->pid,
				__func__, lflags);
		congestion_wait(BLK_RW_ASYNC, HZ/50);
	} while (1);
}
