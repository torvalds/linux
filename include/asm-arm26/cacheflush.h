/*
 *  linux/include/asm-arm/cacheflush.h
 *
 *  Copyright (C) 2000-2002 Russell King
 *  Copyright (C) 2003 Ian Molton
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * ARM26 cache 'functions'
 *
 */

#ifndef _ASMARM_CACHEFLUSH_H
#define _ASMARM_CACHEFLUSH_H

#if 1     //FIXME - BAD INCLUDES!!!
#include <linux/sched.h>
#include <linux/mm.h>
#endif

#define flush_cache_all()                       do { } while (0)
#define flush_cache_mm(mm)                      do { } while (0)
#define flush_cache_dup_mm(mm)                  do { } while (0)
#define flush_cache_range(vma,start,end)        do { } while (0)
#define flush_cache_page(vma,vmaddr,pfn)        do { } while (0)
#define flush_cache_vmap(start, end)		do { } while (0)
#define flush_cache_vunmap(start, end)		do { } while (0)

#define invalidate_dcache_range(start,end)      do { } while (0)
#define clean_dcache_range(start,end)           do { } while (0)
#define flush_dcache_range(start,end)           do { } while (0)
#define flush_dcache_page(page)                 do { } while (0)
#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)
#define clean_dcache_entry(_s)                  do { } while (0)
#define clean_cache_entry(_start)               do { } while (0)

#define flush_icache_user_range(start,end, bob, fred) do { } while (0)
#define flush_icache_range(start,end)           do { } while (0)
#define flush_icache_page(vma,page)             do { } while (0)

#define copy_to_user_page(vma, page, vaddr, dst, src, len) \
	memcpy(dst, src, len)
#define copy_from_user_page(vma, page, vaddr, dst, src, len) \
	memcpy(dst, src, len)

/* DAG: ARM3 will flush cache on MEMC updates anyway? so don't bother */
/* IM : Yes, it will, but only if setup to do so (we do this). */
#define clean_cache_area(_start,_size)          do { } while (0)

#endif
