/*
 * Copyright (c) 2000-2004 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/highmem.h>
#include <linux/swap.h>
#include <linux/blkdev.h>

#include "time.h"
#include "kmem.h"

#define MAX_VMALLOCS	6
#define MAX_SLAB_SIZE	0x20000


void *
kmem_alloc(size_t size, int flags)
{
	int	retries = 0;
	int	lflags = kmem_flags_convert(flags);
	void	*ptr;

	do {
		if (size < MAX_SLAB_SIZE || retries > MAX_VMALLOCS)
			ptr = kmalloc(size, lflags);
		else
			ptr = __vmalloc(size, lflags, PAGE_KERNEL);
		if (ptr || (flags & (KM_MAYFAIL|KM_NOSLEEP)))
			return ptr;
		if (!(++retries % 100))
			printk(KERN_ERR "XFS: possible memory allocation "
					"deadlock in %s (mode:0x%x)\n",
					__FUNCTION__, lflags);
		blk_congestion_wait(WRITE, HZ/50);
	} while (1);
}

void *
kmem_zalloc(size_t size, int flags)
{
	void	*ptr;

	ptr = kmem_alloc(size, flags);
	if (ptr)
		memset((char *)ptr, 0, (int)size);
	return ptr;
}

void
kmem_free(void *ptr, size_t size)
{
	if (((unsigned long)ptr < VMALLOC_START) ||
	    ((unsigned long)ptr >= VMALLOC_END)) {
		kfree(ptr);
	} else {
		vfree(ptr);
	}
}

void *
kmem_realloc(void *ptr, size_t newsize, size_t oldsize, int flags)
{
	void	*new;

	new = kmem_alloc(newsize, flags);
	if (ptr) {
		if (new)
			memcpy(new, ptr,
				((oldsize < newsize) ? oldsize : newsize));
		kmem_free(ptr, oldsize);
	}
	return new;
}

void *
kmem_zone_alloc(kmem_zone_t *zone, int flags)
{
	int	retries = 0;
	int	lflags = kmem_flags_convert(flags);
	void	*ptr;

	do {
		ptr = kmem_cache_alloc(zone, lflags);
		if (ptr || (flags & (KM_MAYFAIL|KM_NOSLEEP)))
			return ptr;
		if (!(++retries % 100))
			printk(KERN_ERR "XFS: possible memory allocation "
					"deadlock in %s (mode:0x%x)\n",
					__FUNCTION__, lflags);
		blk_congestion_wait(WRITE, HZ/50);
	} while (1);
}

void *
kmem_zone_zalloc(kmem_zone_t *zone, int flags)
{
	void	*ptr;

	ptr = kmem_zone_alloc(zone, flags);
	if (ptr)
		memset((char *)ptr, 0, kmem_cache_size(zone));
	return ptr;
}
