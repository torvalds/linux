/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/highmem.h>
#include <linux/swap.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include "time.h"
#include "kmem.h"

#define MAX_VMALLOCS	6
#define MAX_SLAB_SIZE	0x20000

void *
kmem_alloc(size_t size, unsigned int __nocast flags)
{
	int	retries = 0;
	gfp_t	lflags = kmem_flags_convert(flags);
	void	*ptr;

#ifdef DEBUG
	if (unlikely(!(flags & KM_LARGE) && (size > PAGE_SIZE))) {
		printk(KERN_WARNING "Large %s attempt, size=%ld\n",
			__FUNCTION__, (long)size);
		dump_stack();
	}
#endif

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
		congestion_wait(WRITE, HZ/50);
	} while (1);
}

void *
kmem_zalloc(size_t size, unsigned int __nocast flags)
{
	void	*ptr;

	ptr = kmem_alloc(size, flags);
	if (ptr)
		memset((char *)ptr, 0, (int)size);
	return ptr;
}

void *
kmem_zalloc_greedy(size_t *size, size_t minsize, size_t maxsize,
		   unsigned int __nocast flags)
{
	void		*ptr;
	size_t		kmsize = maxsize;
	unsigned int	kmflags = (flags & ~KM_SLEEP) | KM_NOSLEEP;

	while (!(ptr = kmem_zalloc(kmsize, kmflags))) {
		if ((kmsize <= minsize) && (flags & KM_NOSLEEP))
			break;
		if ((kmsize >>= 1) <= minsize) {
			kmsize = minsize;
			kmflags = flags;
		}
	}
	if (ptr)
		*size = kmsize;
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
kmem_realloc(void *ptr, size_t newsize, size_t oldsize,
	     unsigned int __nocast flags)
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
kmem_zone_alloc(kmem_zone_t *zone, unsigned int __nocast flags)
{
	int	retries = 0;
	gfp_t	lflags = kmem_flags_convert(flags);
	void	*ptr;

	do {
		ptr = kmem_cache_alloc(zone, lflags);
		if (ptr || (flags & (KM_MAYFAIL|KM_NOSLEEP)))
			return ptr;
		if (!(++retries % 100))
			printk(KERN_ERR "XFS: possible memory allocation "
					"deadlock in %s (mode:0x%x)\n",
					__FUNCTION__, lflags);
		congestion_wait(WRITE, HZ/50);
	} while (1);
}

void *
kmem_zone_zalloc(kmem_zone_t *zone, unsigned int __nocast flags)
{
	void	*ptr;

	ptr = kmem_zone_alloc(zone, flags);
	if (ptr)
		memset((char *)ptr, 0, kmem_cache_size(zone));
	return ptr;
}
