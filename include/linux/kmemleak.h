/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * include/linux/kmemleak.h
 *
 * Copyright (C) 2008 ARM Limited
 * Written by Catalin Marinas <catalin.marinas@arm.com>
 */

#ifndef __KMEMLEAK_H
#define __KMEMLEAK_H

#include <linux/slab.h>
#include <linux/vmalloc.h>

#ifdef CONFIG_DEBUG_KMEMLEAK

extern void kmemleak_init(void) __init;
extern void kmemleak_alloc(const void *ptr, size_t size, int min_count,
			   gfp_t gfp) __ref;
extern void kmemleak_alloc_percpu(const void __percpu *ptr, size_t size,
				  gfp_t gfp) __ref;
extern void kmemleak_vmalloc(const struct vm_struct *area, size_t size,
			     gfp_t gfp) __ref;
extern void kmemleak_free(const void *ptr) __ref;
extern void kmemleak_free_part(const void *ptr, size_t size) __ref;
extern void kmemleak_free_percpu(const void __percpu *ptr) __ref;
extern void kmemleak_update_trace(const void *ptr) __ref;
extern void kmemleak_not_leak(const void *ptr) __ref;
extern void kmemleak_transient_leak(const void *ptr) __ref;
extern void kmemleak_ignore(const void *ptr) __ref;
extern void kmemleak_ignore_percpu(const void __percpu *ptr) __ref;
extern void kmemleak_scan_area(const void *ptr, size_t size, gfp_t gfp) __ref;
extern void kmemleak_no_scan(const void *ptr) __ref;
extern void kmemleak_alloc_phys(phys_addr_t phys, size_t size,
				gfp_t gfp) __ref;
extern void kmemleak_free_part_phys(phys_addr_t phys, size_t size) __ref;
extern void kmemleak_ignore_phys(phys_addr_t phys) __ref;

static inline void kmemleak_alloc_recursive(const void *ptr, size_t size,
					    int min_count, slab_flags_t flags,
					    gfp_t gfp)
{
	if (!(flags & SLAB_NOLEAKTRACE))
		kmemleak_alloc(ptr, size, min_count, gfp);
}

static inline void kmemleak_free_recursive(const void *ptr, slab_flags_t flags)
{
	if (!(flags & SLAB_NOLEAKTRACE))
		kmemleak_free(ptr);
}

static inline void kmemleak_erase(void **ptr)
{
	*ptr = NULL;
}

#else

static inline void kmemleak_init(void)
{
}
static inline void kmemleak_alloc(const void *ptr, size_t size, int min_count,
				  gfp_t gfp)
{
}
static inline void kmemleak_alloc_recursive(const void *ptr, size_t size,
					    int min_count, slab_flags_t flags,
					    gfp_t gfp)
{
}
static inline void kmemleak_alloc_percpu(const void __percpu *ptr, size_t size,
					 gfp_t gfp)
{
}
static inline void kmemleak_vmalloc(const struct vm_struct *area, size_t size,
				    gfp_t gfp)
{
}
static inline void kmemleak_free(const void *ptr)
{
}
static inline void kmemleak_free_part(const void *ptr, size_t size)
{
}
static inline void kmemleak_free_recursive(const void *ptr, slab_flags_t flags)
{
}
static inline void kmemleak_free_percpu(const void __percpu *ptr)
{
}
static inline void kmemleak_update_trace(const void *ptr)
{
}
static inline void kmemleak_not_leak(const void *ptr)
{
}
static inline void kmemleak_transient_leak(const void *ptr)
{
}
static inline void kmemleak_ignore_percpu(const void __percpu *ptr)
{
}
static inline void kmemleak_ignore(const void *ptr)
{
}
static inline void kmemleak_scan_area(const void *ptr, size_t size, gfp_t gfp)
{
}
static inline void kmemleak_erase(void **ptr)
{
}
static inline void kmemleak_no_scan(const void *ptr)
{
}
static inline void kmemleak_alloc_phys(phys_addr_t phys, size_t size,
				       gfp_t gfp)
{
}
static inline void kmemleak_free_part_phys(phys_addr_t phys, size_t size)
{
}
static inline void kmemleak_ignore_phys(phys_addr_t phys)
{
}

#endif	/* CONFIG_DEBUG_KMEMLEAK */

#endif	/* __KMEMLEAK_H */
