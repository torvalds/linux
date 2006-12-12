/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2002, 2004 by Ralf Baechle
 */
#ifndef _ASM_WAR_H
#define _ASM_WAR_H


/*
 * Another R4600 erratum.  Due to the lack of errata information the exact
 * technical details aren't known.  I've experimentally found that disabling
 * interrupts during indexed I-cache flushes seems to be sufficient to deal
 * with the issue.
 *
 * #define R4600_V1_INDEX_ICACHEOP_WAR 1
 */

/*
 * Pleasures of the R4600 V1.x.  Cite from the IDT R4600 V1.7 errata:
 *
 *  18. The CACHE instructions Hit_Writeback_Invalidate_D, Hit_Writeback_D,
 *      Hit_Invalidate_D and Create_Dirty_Excl_D should only be
 *      executed if there is no other dcache activity. If the dcache is
 *      accessed for another instruction immeidately preceding when these
 *      cache instructions are executing, it is possible that the dcache
 *      tag match outputs used by these cache instructions will be
 *      incorrect. These cache instructions should be preceded by at least
 *      four instructions that are not any kind of load or store
 *      instruction.
 *
 *      This is not allowed:    lw
 *                              nop
 *                              nop
 *                              nop
 *                              cache       Hit_Writeback_Invalidate_D
 *
 *      This is allowed:        lw
 *                              nop
 *                              nop
 *                              nop
 *                              nop
 *                              cache       Hit_Writeback_Invalidate_D
 *
 * #define R4600_V1_HIT_CACHEOP_WAR 1
 */


/*
 * Writeback and invalidate the primary cache dcache before DMA.
 *
 * R4600 v2.0 bug: "The CACHE instructions Hit_Writeback_Inv_D,
 * Hit_Writeback_D, Hit_Invalidate_D and Create_Dirty_Exclusive_D will only
 * operate correctly if the internal data cache refill buffer is empty.  These
 * CACHE instructions should be separated from any potential data cache miss
 * by a load instruction to an uncached address to empty the response buffer."
 * (Revision 2.0 device errata from IDT available on http://www.idt.com/
 * in .pdf format.)
 *
 * #define R4600_V2_HIT_CACHEOP_WAR 1
 */

/*
 * R4600 CPU modules for the Indy come with both V1.7 and V2.0 processors.
 */
#ifdef CONFIG_SGI_IP22

#define R4600_V1_INDEX_ICACHEOP_WAR	1
#define R4600_V1_HIT_CACHEOP_WAR	1
#define R4600_V2_HIT_CACHEOP_WAR	1

#endif

/*
 * But the RM200C seems to have been shipped only with V2.0 R4600s
 */
#ifdef CONFIG_SNI_RM

#define R4600_V2_HIT_CACHEOP_WAR	1

#endif

#ifdef CONFIG_CPU_R5432

/*
 * When an interrupt happens on a CP0 register read instruction, CPU may
 * lock up or read corrupted values of CP0 registers after it enters
 * the exception handler.
 *
 * This workaround makes sure that we read a "safe" CP0 register as the
 * first thing in the exception handler, which breaks one of the
 * pre-conditions for this problem.
 */
#define	R5432_CP0_INTERRUPT_WAR 1

#endif

#if defined(CONFIG_SB1_PASS_1_WORKAROUNDS) || \
    defined(CONFIG_SB1_PASS_2_WORKAROUNDS)

/*
 * Workaround for the Sibyte M3 errata the text of which can be found at
 *
 *   http://sibyte.broadcom.com/hw/bcm1250/docs/pass2errata.txt
 *
 * This will enable the use of a special TLB refill handler which does a
 * consistency check on the information in c0_badvaddr and c0_entryhi and
 * will just return and take the exception again if the information was
 * found to be inconsistent.
 */
#define BCM1250_M3_WAR 1

/*
 * This is a DUART workaround related to glitches around register accesses
 */
#define SIBYTE_1956_WAR 1

#endif

/*
 * Fill buffers not flushed on CACHE instructions
 *
 * Hit_Invalidate_I cacheops invalidate an icache line but the refill
 * for that line can get stale data from the fill buffer instead of
 * accessing memory if the previous icache miss was also to that line.
 *
 * Workaround: generate an icache refill from a different line
 *
 * Affects:
 *  MIPS 4K		RTL revision <3.0, PRID revision <4
 */
#if defined(CONFIG_MIPS_MALTA) || defined(CONFIG_MIPS_ATLAS) || \
    defined(CONFIG_MIPS_SEAD)
#define MIPS4K_ICACHE_REFILL_WAR 1
#endif

/*
 * Missing implicit forced flush of evictions caused by CACHE
 * instruction
 *
 * Evictions caused by a CACHE instructions are not forced on to the
 * bus. The BIU gives higher priority to fetches than to the data from
 * the eviction buffer and no collision detection is performed between
 * fetches and pending data from the eviction buffer.
 *
 * Workaround: Execute a SYNC instruction after the cache instruction
 *
 * Affects:
 *   MIPS 5Kc,5Kf	RTL revision <2.3, PRID revision <8
 *   MIPS 20Kc		RTL revision <4.0, PRID revision <?
 */
#if defined(CONFIG_MIPS_MALTA) || defined(CONFIG_MIPS_ATLAS) || \
    defined(CONFIG_MIPS_SEAD)
#define MIPS_CACHE_SYNC_WAR 1
#endif

/*
 * From TX49/H2 manual: "If the instruction (i.e. CACHE) is issued for
 * the line which this instruction itself exists, the following
 * operation is not guaranteed."
 *
 * Workaround: do two phase flushing for Index_Invalidate_I
 */
#ifdef CONFIG_CPU_TX49XX
#define TX49XX_ICACHE_INDEX_INV_WAR 1
#endif

/*
 * On the RM9000 there is a problem which makes the CreateDirtyExclusive
 * cache operation unusable on SMP systems.
 */
#if defined(CONFIG_MOMENCO_JAGUAR_ATX) || defined(CONFIG_PMC_YOSEMITE) || \
    defined(CONFIG_BASLER_EXCITE)
#define  RM9000_CDEX_SMP_WAR		1
#endif

/*
 * The RM9000 has a bug (though PMC-Sierra opposes it being called that)
 * where invalid instructions in the same I-cache line worth of instructions
 * being fetched may case spurious exceptions.
 */
#if defined(CONFIG_MOMENCO_JAGUAR_ATX) || defined(CONFIG_MOMENCO_OCELOT_3) || \
    defined(CONFIG_PMC_YOSEMITE) || defined(CONFIG_BASLER_EXCITE)
#define ICACHE_REFILLS_WORKAROUND_WAR	1
#endif


/*
 * ON the R10000 upto version 2.6 (not sure about 2.7) there is a bug that
 * may cause ll / sc and lld / scd sequences to execute non-atomically.
 */
#ifdef CONFIG_SGI_IP27
#define R10000_LLSC_WAR 1
#endif

/*
 * Workarounds default to off
 */
#ifndef ICACHE_REFILLS_WORKAROUND_WAR
#define ICACHE_REFILLS_WORKAROUND_WAR	0
#endif
#ifndef R4600_V1_INDEX_ICACHEOP_WAR
#define R4600_V1_INDEX_ICACHEOP_WAR	0
#endif
#ifndef R4600_V1_HIT_CACHEOP_WAR
#define R4600_V1_HIT_CACHEOP_WAR	0
#endif
#ifndef R4600_V2_HIT_CACHEOP_WAR
#define R4600_V2_HIT_CACHEOP_WAR	0
#endif
#ifndef R5432_CP0_INTERRUPT_WAR
#define R5432_CP0_INTERRUPT_WAR		0
#endif
#ifndef BCM1250_M3_WAR
#define BCM1250_M3_WAR			0
#endif
#ifndef SIBYTE_1956_WAR
#define SIBYTE_1956_WAR			0
#endif
#ifndef MIPS4K_ICACHE_REFILL_WAR
#define MIPS4K_ICACHE_REFILL_WAR	0
#endif
#ifndef MIPS_CACHE_SYNC_WAR
#define MIPS_CACHE_SYNC_WAR		0
#endif
#ifndef TX49XX_ICACHE_INDEX_INV_WAR
#define TX49XX_ICACHE_INDEX_INV_WAR	0
#endif
#ifndef RM9000_CDEX_SMP_WAR
#define RM9000_CDEX_SMP_WAR		0
#endif
#ifndef R10000_LLSC_WAR
#define R10000_LLSC_WAR			0
#endif

#endif /* _ASM_WAR_H */
