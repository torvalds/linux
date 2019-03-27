/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013-2017 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef	_LINUX_GFP_H_
#define	_LINUX_GFP_H_

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/malloc.h>

#include <linux/page.h>

#include <vm/vm_param.h>
#include <vm/vm_object.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>

#define	__GFP_NOWARN	0
#define	__GFP_HIGHMEM	0
#define	__GFP_ZERO	M_ZERO
#define	__GFP_NORETRY	0
#define	__GFP_RECLAIM   0
#define	__GFP_RECLAIMABLE   0
#define	__GFP_RETRY_MAYFAIL 0
#define	__GFP_MOVABLE	0
#define	__GFP_COMP	0
#define        __GFP_KSWAPD_RECLAIM 0

#define	__GFP_IO	0
#define	__GFP_NO_KSWAPD	0
#define	__GFP_WAIT	M_WAITOK
#define	__GFP_DMA32	(1U << 24) /* LinuxKPI only */
#define	__GFP_BITS_SHIFT 25
#define	__GFP_BITS_MASK	((1 << __GFP_BITS_SHIFT) - 1)
#define	__GFP_NOFAIL	M_WAITOK

#define	GFP_NOWAIT	M_NOWAIT
#define	GFP_ATOMIC	(M_NOWAIT | M_USE_RESERVE)
#define	GFP_KERNEL	M_WAITOK
#define	GFP_USER	M_WAITOK
#define	GFP_HIGHUSER	M_WAITOK
#define	GFP_HIGHUSER_MOVABLE	M_WAITOK
#define	GFP_IOFS	M_NOWAIT
#define	GFP_NOIO	M_NOWAIT
#define	GFP_DMA32	__GFP_DMA32
#define	GFP_TEMPORARY	M_NOWAIT
#define	GFP_NATIVE_MASK	(M_NOWAIT | M_WAITOK | M_USE_RESERVE | M_ZERO)
#define	GFP_TRANSHUGE	0
#define        GFP_TRANSHUGE_LIGHT 0

CTASSERT((__GFP_DMA32 & GFP_NATIVE_MASK) == 0);
CTASSERT((__GFP_BITS_MASK & GFP_NATIVE_MASK) == GFP_NATIVE_MASK);

/*
 * Resolve a page into a virtual address:
 *
 * NOTE: This function only works for pages allocated by the kernel.
 */
extern void *linux_page_address(struct page *);

#define	page_address(page) linux_page_address(page)

/*
 * Page management for unmapped pages:
 */
extern vm_page_t linux_alloc_pages(gfp_t flags, unsigned int order);
extern void linux_free_pages(vm_page_t page, unsigned int order);

static inline struct page *
alloc_page(gfp_t flags)
{

	return (linux_alloc_pages(flags, 0));
}

static inline struct page *
alloc_pages(gfp_t flags, unsigned int order)
{

	return (linux_alloc_pages(flags, order));
}

static inline struct page *
alloc_pages_node(int node_id, gfp_t flags, unsigned int order)
{

	return (linux_alloc_pages(flags, order));
}

static inline void
__free_pages(struct page *page, unsigned int order)
{

	linux_free_pages(page, order);
}

static inline void
__free_page(struct page *page)
{

	linux_free_pages(page, 0);
}

/*
 * Page management for mapped pages:
 */
extern vm_offset_t linux_alloc_kmem(gfp_t flags, unsigned int order);
extern void linux_free_kmem(vm_offset_t, unsigned int order);

static inline vm_offset_t
get_zeroed_page(gfp_t flags)
{

	return (linux_alloc_kmem(flags | __GFP_ZERO, 0));
}

static inline vm_offset_t
__get_free_page(gfp_t flags)
{

	return (linux_alloc_kmem(flags, 0));
}

static inline vm_offset_t
__get_free_pages(gfp_t flags, unsigned int order)
{

	return (linux_alloc_kmem(flags, order));
}

static inline void
free_pages(uintptr_t addr, unsigned int order)
{
	if (addr == 0)
		return;

	linux_free_kmem(addr, order);
}

static inline void
free_page(uintptr_t addr)
{
	if (addr == 0)
		return;

	linux_free_kmem(addr, 0);
}

static inline bool
gfpflags_allow_blocking(const gfp_t gfp_flags)
{
	return ((gfp_flags & (M_WAITOK | M_NOWAIT)) == M_WAITOK);
}

#define	SetPageReserved(page)	do { } while (0)	/* NOP */
#define	ClearPageReserved(page)	do { } while (0)	/* NOP */

#endif	/* _LINUX_GFP_H_ */
