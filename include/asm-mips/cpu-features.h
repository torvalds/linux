/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003, 2004 Ralf Baechle
 */
#ifndef __ASM_CPU_FEATURES_H
#define __ASM_CPU_FEATURES_H

#include <linux/config.h>

#include <asm/cpu.h>
#include <asm/cpu-info.h>
#include <cpu-feature-overrides.h>

/*
 * SMP assumption: Options of CPU 0 are a superset of all processors.
 * This is true for all known MIPS systems.
 */
#ifndef cpu_has_tlb
#define cpu_has_tlb		(cpu_data[0].options & MIPS_CPU_TLB)
#endif
#ifndef cpu_has_4kex
#define cpu_has_4kex		(cpu_data[0].options & MIPS_CPU_4KEX)
#endif
#ifndef cpu_has_4ktlb
#define cpu_has_4ktlb		(cpu_data[0].options & MIPS_CPU_4KTLB)
#endif
#ifndef cpu_has_fpu
#define cpu_has_fpu		(cpu_data[0].options & MIPS_CPU_FPU)
#endif
#ifndef cpu_has_32fpr
#define cpu_has_32fpr		(cpu_data[0].options & MIPS_CPU_32FPR)
#endif
#ifndef cpu_has_counter
#define cpu_has_counter		(cpu_data[0].options & MIPS_CPU_COUNTER)
#endif
#ifndef cpu_has_watch
#define cpu_has_watch		(cpu_data[0].options & MIPS_CPU_WATCH)
#endif
#ifndef cpu_has_mips16
#define cpu_has_mips16		(cpu_data[0].options & MIPS_CPU_MIPS16)
#endif
#ifndef cpu_has_divec
#define cpu_has_divec		(cpu_data[0].options & MIPS_CPU_DIVEC)
#endif
#ifndef cpu_has_vce
#define cpu_has_vce		(cpu_data[0].options & MIPS_CPU_VCE)
#endif
#ifndef cpu_has_cache_cdex_p
#define cpu_has_cache_cdex_p	(cpu_data[0].options & MIPS_CPU_CACHE_CDEX_P)
#endif
#ifndef cpu_has_cache_cdex_s
#define cpu_has_cache_cdex_s	(cpu_data[0].options & MIPS_CPU_CACHE_CDEX_S)
#endif
#ifndef cpu_has_prefetch
#define cpu_has_prefetch	(cpu_data[0].options & MIPS_CPU_PREFETCH)
#endif
#ifndef cpu_has_mcheck
#define cpu_has_mcheck		(cpu_data[0].options & MIPS_CPU_MCHECK)
#endif
#ifndef cpu_has_ejtag
#define cpu_has_ejtag		(cpu_data[0].options & MIPS_CPU_EJTAG)
#endif
#ifndef cpu_has_llsc
#define cpu_has_llsc		(cpu_data[0].options & MIPS_CPU_LLSC)
#endif
#ifndef cpu_has_vtag_icache
#define cpu_has_vtag_icache	(cpu_data[0].icache.flags & MIPS_CACHE_VTAG)
#endif
#ifndef cpu_has_dc_aliases
#define cpu_has_dc_aliases	(cpu_data[0].dcache.flags & MIPS_CACHE_ALIASES)
#endif
#ifndef cpu_has_ic_fills_f_dc
#define cpu_has_ic_fills_f_dc	(cpu_data[0].icache.flags & MIPS_CACHE_IC_F_DC)
#endif

/*
 * I-Cache snoops remote store.  This only matters on SMP.  Some multiprocessors
 * such as the R10000 have I-Caches that snoop local stores; the embedded ones
 * don't.  For maintaining I-cache coherency this means we need to flush the
 * D-cache all the way back to whever the I-cache does refills from, so the
 * I-cache has a chance to see the new data at all.  Then we have to flush the
 * I-cache also.
 * Note we may have been rescheduled and may no longer be running on the CPU
 * that did the store so we can't optimize this into only doing the flush on
 * the local CPU.
 */
#ifndef cpu_icache_snoops_remote_store
#ifdef CONFIG_SMP
#define cpu_icache_snoops_remote_store	(cpu_data[0].icache.flags & MIPS_IC_SNOOPS_REMOTE)
#else
#define cpu_icache_snoops_remote_store	1
#endif
#endif

/*
 * Certain CPUs may throw bizarre exceptions if not the whole cacheline
 * contains valid instructions.  For these we ensure proper alignment of
 * signal trampolines and pad them to the size of a full cache lines with
 * nops.  This is also used in structure definitions so can't be a test macro
 * like the others.
 */
#ifndef PLAT_TRAMPOLINE_STUFF_LINE
#define PLAT_TRAMPOLINE_STUFF_LINE	0UL
#endif

#ifdef CONFIG_MIPS32
# ifndef cpu_has_nofpuex
# define cpu_has_nofpuex	(cpu_data[0].options & MIPS_CPU_NOFPUEX)
# endif
# ifndef cpu_has_64bits
# define cpu_has_64bits		(cpu_data[0].isa_level & MIPS_CPU_ISA_64BIT)
# endif
# ifndef cpu_has_64bit_zero_reg
# define cpu_has_64bit_zero_reg	(cpu_data[0].isa_level & MIPS_CPU_ISA_64BIT)
# endif
# ifndef cpu_has_64bit_gp_regs
# define cpu_has_64bit_gp_regs		0
# endif
# ifndef cpu_has_64bit_addresses
# define cpu_has_64bit_addresses	0
# endif
#endif

#ifdef CONFIG_MIPS64
# ifndef cpu_has_nofpuex
# define cpu_has_nofpuex		0
# endif
# ifndef cpu_has_64bits
# define cpu_has_64bits			1
# endif
# ifndef cpu_has_64bit_zero_reg
# define cpu_has_64bit_zero_reg		1
# endif
# ifndef cpu_has_64bit_gp_regs
# define cpu_has_64bit_gp_regs		1
# endif
# ifndef cpu_has_64bit_addresses
# define cpu_has_64bit_addresses	1
# endif
#endif

#ifndef cpu_has_subset_pcaches
#define cpu_has_subset_pcaches	(cpu_data[0].options & MIPS_CPU_SUBSET_CACHES)
#endif

#ifndef cpu_dcache_line_size
#define cpu_dcache_line_size()	current_cpu_data.dcache.linesz
#endif
#ifndef cpu_icache_line_size
#define cpu_icache_line_size()	current_cpu_data.icache.linesz
#endif
#ifndef cpu_scache_line_size
#define cpu_scache_line_size()	current_cpu_data.scache.linesz
#endif

#endif /* __ASM_CPU_FEATURES_H */
