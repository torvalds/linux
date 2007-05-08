/*
 * File:         include/asm-blackfin/cacheflush.h
 * Based on:	 include/asm-m68knommu/cacheflush.h
 * Author:       LG Soft India
 *               Copyright (C) 2004 Analog Devices Inc.
 * Created:      Tue Sep 21 2004
 * Description:  Blackfin low-level cache routines adapted from the i386
 * 		 and PPC versions by Greg Ungerer (gerg@snapgear.com)
 *
 * Modified:
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.
 * If not, write to the Free Software Foundation,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _BLACKFIN_CACHEFLUSH_H
#define _BLACKFIN_CACHEFLUSH_H

#include <asm/cplb.h>

extern void blackfin_icache_dcache_flush_range(unsigned int, unsigned int);
extern void blackfin_icache_flush_range(unsigned int, unsigned int);
extern void blackfin_dcache_flush_range(unsigned int, unsigned int);
extern void blackfin_dcache_invalidate_range(unsigned int, unsigned int);
extern void blackfin_dflush_page(void *);

#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)
#define flush_cache_mm(mm)			do { } while (0)
#define flush_cache_range(vma, start, end)	do { } while (0)
#define flush_cache_page(vma, vmaddr)		do { } while (0)
#define flush_cache_vmap(start, end)		do { } while (0)
#define flush_cache_vunmap(start, end)		do { } while (0)

static inline void flush_icache_range(unsigned start, unsigned end)
{
#if defined(CONFIG_BLKFIN_DCACHE) && defined(CONFIG_BLKFIN_CACHE)

# if defined(CONFIG_BLKFIN_WT)
	blackfin_icache_flush_range((start), (end));
# else
	blackfin_icache_dcache_flush_range((start), (end));
# endif

#else

# if defined(CONFIG_BLKFIN_CACHE)
	blackfin_icache_flush_range((start), (end));
# endif
# if defined(CONFIG_BLKFIN_DCACHE)
	blackfin_dcache_flush_range((start), (end));
# endif

#endif
}

#define copy_to_user_page(vma, page, vaddr, dst, src, len) \
do { memcpy(dst, src, len); \
     flush_icache_range ((unsigned) (dst), (unsigned) (dst) + (len)); \
} while (0)
#define copy_from_user_page(vma, page, vaddr, dst, src, len)	memcpy(dst, src, len)

#if defined(CONFIG_BLKFIN_DCACHE)
# define invalidate_dcache_range(start,end)	blackfin_dcache_invalidate_range((start), (end))
#else
# define invalidate_dcache_range(start,end)	do { } while (0)
#endif
#if defined(CONFIG_BLKFIN_DCACHE) && defined(CONFIG_BLKFIN_WB)
# define flush_dcache_range(start,end)		blackfin_dcache_flush_range((start), (end))
# define flush_dcache_page(page)			blackfin_dflush_page(page_address(page))
#else
# define flush_dcache_range(start,end)		do { } while (0)
# define flush_dcache_page(page)			do { } while (0)
#endif

#endif				/* _BLACKFIN_CACHEFLUSH_H */
