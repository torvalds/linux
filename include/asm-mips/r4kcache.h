/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Inline assembly cache operations.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1997 - 2002 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 2004 Ralf Baechle (ralf@linux-mips.org)
 */
#ifndef _ASM_R4KCACHE_H
#define _ASM_R4KCACHE_H

#include <asm/asm.h>
#include <asm/cacheops.h>
#include <asm/cpu-features.h>

/*
 * This macro return a properly sign-extended address suitable as base address
 * for indexed cache operations.  Two issues here:
 *
 *  - The MIPS32 and MIPS64 specs permit an implementation to directly derive
 *    the index bits from the virtual address.  This breaks with tradition
 *    set by the R4000.  To keep unpleasant surprises from happening we pick
 *    an address in KSEG0 / CKSEG0.
 *  - We need a properly sign extended address for 64-bit code.  To get away
 *    without ifdefs we let the compiler do it by a type cast.
 */
#define INDEX_BASE	CKSEG0

#define cache_op(op,addr)						\
	__asm__ __volatile__(						\
	"	.set	push					\n"	\
	"	.set	noreorder				\n"	\
	"	.set	mips3\n\t				\n"	\
	"	cache	%0, %1					\n"	\
	"	.set	pop					\n"	\
	:								\
	: "i" (op), "R" (*(unsigned char *)(addr)))

static inline void flush_icache_line_indexed(unsigned long addr)
{
	cache_op(Index_Invalidate_I, addr);
}

static inline void flush_dcache_line_indexed(unsigned long addr)
{
	cache_op(Index_Writeback_Inv_D, addr);
}

static inline void flush_scache_line_indexed(unsigned long addr)
{
	cache_op(Index_Writeback_Inv_SD, addr);
}

static inline void flush_icache_line(unsigned long addr)
{
	cache_op(Hit_Invalidate_I, addr);
}

static inline void flush_dcache_line(unsigned long addr)
{
	cache_op(Hit_Writeback_Inv_D, addr);
}

static inline void invalidate_dcache_line(unsigned long addr)
{
	cache_op(Hit_Invalidate_D, addr);
}

static inline void invalidate_scache_line(unsigned long addr)
{
	cache_op(Hit_Invalidate_SD, addr);
}

static inline void flush_scache_line(unsigned long addr)
{
	cache_op(Hit_Writeback_Inv_SD, addr);
}

#define protected_cache_op(op,addr)				\
	__asm__ __volatile__(					\
	"	.set	push			\n"		\
	"	.set	noreorder		\n"		\
	"	.set	mips3			\n"		\
	"1:	cache	%0, (%1)		\n"		\
	"2:	.set	pop			\n"		\
	"	.section __ex_table,\"a\"	\n"		\
	"	"STR(PTR)" 1b, 2b		\n"		\
	"	.previous"					\
	:							\
	: "i" (op), "r" (addr))

/*
 * The next two are for badland addresses like signal trampolines.
 */
static inline void protected_flush_icache_line(unsigned long addr)
{
	protected_cache_op(Hit_Invalidate_I, addr);
}

/*
 * R10000 / R12000 hazard - these processors don't support the Hit_Writeback_D
 * cacheop so we use Hit_Writeback_Inv_D which is supported by all R4000-style
 * caches.  We're talking about one cacheline unnecessarily getting invalidated
 * here so the penalty isn't overly hard.
 */
static inline void protected_writeback_dcache_line(unsigned long addr)
{
	protected_cache_op(Hit_Writeback_Inv_D, addr);
}

static inline void protected_writeback_scache_line(unsigned long addr)
{
	protected_cache_op(Hit_Writeback_Inv_SD, addr);
}

/*
 * This one is RM7000-specific
 */
static inline void invalidate_tcache_page(unsigned long addr)
{
	cache_op(Page_Invalidate_T, addr);
}

#define cache16_unroll32(base,op)					\
	__asm__ __volatile__(						\
	"	.set push					\n"	\
	"	.set noreorder					\n"	\
	"	.set mips3					\n"	\
	"	cache %1, 0x000(%0); cache %1, 0x010(%0)	\n"	\
	"	cache %1, 0x020(%0); cache %1, 0x030(%0)	\n"	\
	"	cache %1, 0x040(%0); cache %1, 0x050(%0)	\n"	\
	"	cache %1, 0x060(%0); cache %1, 0x070(%0)	\n"	\
	"	cache %1, 0x080(%0); cache %1, 0x090(%0)	\n"	\
	"	cache %1, 0x0a0(%0); cache %1, 0x0b0(%0)	\n"	\
	"	cache %1, 0x0c0(%0); cache %1, 0x0d0(%0)	\n"	\
	"	cache %1, 0x0e0(%0); cache %1, 0x0f0(%0)	\n"	\
	"	cache %1, 0x100(%0); cache %1, 0x110(%0)	\n"	\
	"	cache %1, 0x120(%0); cache %1, 0x130(%0)	\n"	\
	"	cache %1, 0x140(%0); cache %1, 0x150(%0)	\n"	\
	"	cache %1, 0x160(%0); cache %1, 0x170(%0)	\n"	\
	"	cache %1, 0x180(%0); cache %1, 0x190(%0)	\n"	\
	"	cache %1, 0x1a0(%0); cache %1, 0x1b0(%0)	\n"	\
	"	cache %1, 0x1c0(%0); cache %1, 0x1d0(%0)	\n"	\
	"	cache %1, 0x1e0(%0); cache %1, 0x1f0(%0)	\n"	\
	"	.set pop					\n"	\
		:							\
		: "r" (base),						\
		  "i" (op));

#define cache32_unroll32(base,op)					\
	__asm__ __volatile__(						\
	"	.set push					\n"	\
	"	.set noreorder					\n"	\
	"	.set mips3					\n"	\
	"	cache %1, 0x000(%0); cache %1, 0x020(%0)	\n"	\
	"	cache %1, 0x040(%0); cache %1, 0x060(%0)	\n"	\
	"	cache %1, 0x080(%0); cache %1, 0x0a0(%0)	\n"	\
	"	cache %1, 0x0c0(%0); cache %1, 0x0e0(%0)	\n"	\
	"	cache %1, 0x100(%0); cache %1, 0x120(%0)	\n"	\
	"	cache %1, 0x140(%0); cache %1, 0x160(%0)	\n"	\
	"	cache %1, 0x180(%0); cache %1, 0x1a0(%0)	\n"	\
	"	cache %1, 0x1c0(%0); cache %1, 0x1e0(%0)	\n"	\
	"	cache %1, 0x200(%0); cache %1, 0x220(%0)	\n"	\
	"	cache %1, 0x240(%0); cache %1, 0x260(%0)	\n"	\
	"	cache %1, 0x280(%0); cache %1, 0x2a0(%0)	\n"	\
	"	cache %1, 0x2c0(%0); cache %1, 0x2e0(%0)	\n"	\
	"	cache %1, 0x300(%0); cache %1, 0x320(%0)	\n"	\
	"	cache %1, 0x340(%0); cache %1, 0x360(%0)	\n"	\
	"	cache %1, 0x380(%0); cache %1, 0x3a0(%0)	\n"	\
	"	cache %1, 0x3c0(%0); cache %1, 0x3e0(%0)	\n"	\
	"	.set pop					\n"	\
		:							\
		: "r" (base),						\
		  "i" (op));

#define cache64_unroll32(base,op)					\
	__asm__ __volatile__(						\
	"	.set push					\n"	\
	"	.set noreorder					\n"	\
	"	.set mips3					\n"	\
	"	cache %1, 0x000(%0); cache %1, 0x040(%0)	\n"	\
	"	cache %1, 0x080(%0); cache %1, 0x0c0(%0)	\n"	\
	"	cache %1, 0x100(%0); cache %1, 0x140(%0)	\n"	\
	"	cache %1, 0x180(%0); cache %1, 0x1c0(%0)	\n"	\
	"	cache %1, 0x200(%0); cache %1, 0x240(%0)	\n"	\
	"	cache %1, 0x280(%0); cache %1, 0x2c0(%0)	\n"	\
	"	cache %1, 0x300(%0); cache %1, 0x340(%0)	\n"	\
	"	cache %1, 0x380(%0); cache %1, 0x3c0(%0)	\n"	\
	"	cache %1, 0x400(%0); cache %1, 0x440(%0)	\n"	\
	"	cache %1, 0x480(%0); cache %1, 0x4c0(%0)	\n"	\
	"	cache %1, 0x500(%0); cache %1, 0x540(%0)	\n"	\
	"	cache %1, 0x580(%0); cache %1, 0x5c0(%0)	\n"	\
	"	cache %1, 0x600(%0); cache %1, 0x640(%0)	\n"	\
	"	cache %1, 0x680(%0); cache %1, 0x6c0(%0)	\n"	\
	"	cache %1, 0x700(%0); cache %1, 0x740(%0)	\n"	\
	"	cache %1, 0x780(%0); cache %1, 0x7c0(%0)	\n"	\
	"	.set pop					\n"	\
		:							\
		: "r" (base),						\
		  "i" (op));

#define cache128_unroll32(base,op)					\
	__asm__ __volatile__(						\
	"	.set push					\n"	\
	"	.set noreorder					\n"	\
	"	.set mips3					\n"	\
	"	cache %1, 0x000(%0); cache %1, 0x080(%0)	\n"	\
	"	cache %1, 0x100(%0); cache %1, 0x180(%0)	\n"	\
	"	cache %1, 0x200(%0); cache %1, 0x280(%0)	\n"	\
	"	cache %1, 0x300(%0); cache %1, 0x380(%0)	\n"	\
	"	cache %1, 0x400(%0); cache %1, 0x480(%0)	\n"	\
	"	cache %1, 0x500(%0); cache %1, 0x580(%0)	\n"	\
	"	cache %1, 0x600(%0); cache %1, 0x680(%0)	\n"	\
	"	cache %1, 0x700(%0); cache %1, 0x780(%0)	\n"	\
	"	cache %1, 0x800(%0); cache %1, 0x880(%0)	\n"	\
	"	cache %1, 0x900(%0); cache %1, 0x980(%0)	\n"	\
	"	cache %1, 0xa00(%0); cache %1, 0xa80(%0)	\n"	\
	"	cache %1, 0xb00(%0); cache %1, 0xb80(%0)	\n"	\
	"	cache %1, 0xc00(%0); cache %1, 0xc80(%0)	\n"	\
	"	cache %1, 0xd00(%0); cache %1, 0xd80(%0)	\n"	\
	"	cache %1, 0xe00(%0); cache %1, 0xe80(%0)	\n"	\
	"	cache %1, 0xf00(%0); cache %1, 0xf80(%0)	\n"	\
	"	.set pop					\n"	\
		:							\
		: "r" (base),						\
		  "i" (op));

/* build blast_xxx, blast_xxx_page, blast_xxx_page_indexed */
#define __BUILD_BLAST_CACHE(pfx, desc, indexop, hitop, lsize) \
static inline void blast_##pfx##cache##lsize(void)			\
{									\
	unsigned long start = INDEX_BASE;				\
	unsigned long end = start + current_cpu_data.desc.waysize;	\
	unsigned long ws_inc = 1UL << current_cpu_data.desc.waybit;	\
	unsigned long ws_end = current_cpu_data.desc.ways <<		\
	                       current_cpu_data.desc.waybit;		\
	unsigned long ws, addr;						\
									\
	for (ws = 0; ws < ws_end; ws += ws_inc)				\
		for (addr = start; addr < end; addr += lsize * 32)	\
			cache##lsize##_unroll32(addr|ws,indexop);	\
}									\
									\
static inline void blast_##pfx##cache##lsize##_page(unsigned long page)	\
{									\
	unsigned long start = page;					\
	unsigned long end = page + PAGE_SIZE;				\
									\
	do {								\
		cache##lsize##_unroll32(start,hitop);			\
		start += lsize * 32;					\
	} while (start < end);						\
}									\
									\
static inline void blast_##pfx##cache##lsize##_page_indexed(unsigned long page) \
{									\
	unsigned long indexmask = current_cpu_data.desc.waysize - 1;	\
	unsigned long start = INDEX_BASE + (page & indexmask);		\
	unsigned long end = start + PAGE_SIZE;				\
	unsigned long ws_inc = 1UL << current_cpu_data.desc.waybit;	\
	unsigned long ws_end = current_cpu_data.desc.ways <<		\
	                       current_cpu_data.desc.waybit;		\
	unsigned long ws, addr;						\
									\
	for (ws = 0; ws < ws_end; ws += ws_inc)				\
		for (addr = start; addr < end; addr += lsize * 32)	\
			cache##lsize##_unroll32(addr|ws,indexop);	\
}

__BUILD_BLAST_CACHE(d, dcache, Index_Writeback_Inv_D, Hit_Writeback_Inv_D, 16)
__BUILD_BLAST_CACHE(i, icache, Index_Invalidate_I, Hit_Invalidate_I, 16)
__BUILD_BLAST_CACHE(s, scache, Index_Writeback_Inv_SD, Hit_Writeback_Inv_SD, 16)
__BUILD_BLAST_CACHE(d, dcache, Index_Writeback_Inv_D, Hit_Writeback_Inv_D, 32)
__BUILD_BLAST_CACHE(i, icache, Index_Invalidate_I, Hit_Invalidate_I, 32)
__BUILD_BLAST_CACHE(s, scache, Index_Writeback_Inv_SD, Hit_Writeback_Inv_SD, 32)
__BUILD_BLAST_CACHE(i, icache, Index_Invalidate_I, Hit_Invalidate_I, 64)
__BUILD_BLAST_CACHE(s, scache, Index_Writeback_Inv_SD, Hit_Writeback_Inv_SD, 64)
__BUILD_BLAST_CACHE(s, scache, Index_Writeback_Inv_SD, Hit_Writeback_Inv_SD, 128)

/* build blast_xxx_range, protected_blast_xxx_range */
#define __BUILD_BLAST_CACHE_RANGE(pfx, desc, hitop, prot) \
static inline void prot##blast_##pfx##cache##_range(unsigned long start, \
						    unsigned long end)	\
{									\
	unsigned long lsize = cpu_##desc##_line_size();			\
	unsigned long addr = start & ~(lsize - 1);			\
	unsigned long aend = (end - 1) & ~(lsize - 1);			\
	while (1) {							\
		prot##cache_op(hitop, addr);				\
		if (addr == aend)					\
			break;						\
		addr += lsize;						\
	}								\
}

__BUILD_BLAST_CACHE_RANGE(d, dcache, Hit_Writeback_Inv_D, protected_)
__BUILD_BLAST_CACHE_RANGE(s, scache, Hit_Writeback_Inv_SD, protected_)
__BUILD_BLAST_CACHE_RANGE(i, icache, Hit_Invalidate_I, protected_)
__BUILD_BLAST_CACHE_RANGE(d, dcache, Hit_Writeback_Inv_D, )
__BUILD_BLAST_CACHE_RANGE(s, scache, Hit_Writeback_Inv_SD, )
/* blast_inv_dcache_range */
__BUILD_BLAST_CACHE_RANGE(inv_d, dcache, Hit_Invalidate_D, )
__BUILD_BLAST_CACHE_RANGE(inv_s, scache, Hit_Invalidate_SD, )

#endif /* _ASM_R4KCACHE_H */
