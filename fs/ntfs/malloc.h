/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * malloc.h - NTFS kernel memory handling. Part of the Linux-NTFS project.
 *
 * Copyright (c) 2001-2005 Anton Altaparmakov
 */

#ifndef _LINUX_NTFS_MALLOC_H
#define _LINUX_NTFS_MALLOC_H

#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/highmem.h>

/**
 * __ntfs_malloc - allocate memory in multiples of pages
 * @size:	number of bytes to allocate
 * @gfp_mask:	extra flags for the allocator
 *
 * Internal function.  You probably want ntfs_malloc_nofs()...
 *
 * Allocates @size bytes of memory, rounded up to multiples of PAGE_SIZE and
 * returns a pointer to the allocated memory.
 *
 * If there was insufficient memory to complete the request, return NULL.
 * Depending on @gfp_mask the allocation may be guaranteed to succeed.
 */
static inline void *__ntfs_malloc(unsigned long size, gfp_t gfp_mask)
{
	if (likely(size <= PAGE_SIZE)) {
		BUG_ON(!size);
		/* kmalloc() has per-CPU caches so is faster for now. */
		return kmalloc(PAGE_SIZE, gfp_mask & ~__GFP_HIGHMEM);
		/* return (void *)__get_free_page(gfp_mask); */
	}
	if (likely((size >> PAGE_SHIFT) < totalram_pages()))
		return __vmalloc(size, gfp_mask, PAGE_KERNEL);
	return NULL;
}

/**
 * ntfs_malloc_nofs - allocate memory in multiples of pages
 * @size:	number of bytes to allocate
 *
 * Allocates @size bytes of memory, rounded up to multiples of PAGE_SIZE and
 * returns a pointer to the allocated memory.
 *
 * If there was insufficient memory to complete the request, return NULL.
 */
static inline void *ntfs_malloc_nofs(unsigned long size)
{
	return __ntfs_malloc(size, GFP_NOFS | __GFP_HIGHMEM);
}

/**
 * ntfs_malloc_nofs_nofail - allocate memory in multiples of pages
 * @size:	number of bytes to allocate
 *
 * Allocates @size bytes of memory, rounded up to multiples of PAGE_SIZE and
 * returns a pointer to the allocated memory.
 *
 * This function guarantees that the allocation will succeed.  It will sleep
 * for as long as it takes to complete the allocation.
 *
 * If there was insufficient memory to complete the request, return NULL.
 */
static inline void *ntfs_malloc_nofs_nofail(unsigned long size)
{
	return __ntfs_malloc(size, GFP_NOFS | __GFP_HIGHMEM | __GFP_NOFAIL);
}

static inline void ntfs_free(void *addr)
{
	kvfree(addr);
}

#endif /* _LINUX_NTFS_MALLOC_H */
