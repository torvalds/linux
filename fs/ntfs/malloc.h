/*
 * malloc.h - NTFS kernel memory handling. Part of the Linux-NTFS project.
 *
 * Copyright (c) 2001-2004 Anton Altaparmakov
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the Linux-NTFS
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _LINUX_NTFS_MALLOC_H
#define _LINUX_NTFS_MALLOC_H

#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/highmem.h>

/**
 * ntfs_malloc_nofs - allocate memory in multiples of pages
 * @size	number of bytes to allocate
 *
 * Allocates @size bytes of memory, rounded up to multiples of PAGE_SIZE and
 * returns a pointer to the allocated memory.
 *
 * If there was insufficient memory to complete the request, return NULL.
 */
static inline void *ntfs_malloc_nofs(unsigned long size)
{
	if (likely(size <= PAGE_SIZE)) {
		BUG_ON(!size);
		/* kmalloc() has per-CPU caches so is faster for now. */
		return kmalloc(PAGE_SIZE, GFP_NOFS);
		/* return (void *)__get_free_page(GFP_NOFS | __GFP_HIGHMEM); */
	}
	if (likely(size >> PAGE_SHIFT < num_physpages))
		return __vmalloc(size, GFP_NOFS | __GFP_HIGHMEM, PAGE_KERNEL);
	return NULL;
}

static inline void ntfs_free(void *addr)
{
	if (likely(((unsigned long)addr < VMALLOC_START) ||
			((unsigned long)addr >= VMALLOC_END ))) {
		kfree(addr);
		/* free_page((unsigned long)addr); */
		return;
	}
	vfree(addr);
}

#endif /* _LINUX_NTFS_MALLOC_H */
