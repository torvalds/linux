/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003, 2004 Chris Dearman
 */
#ifndef __ASM_MACH_SIM_CPU_FEATURE_OVERRIDES_H
#define __ASM_MACH_SIM_CPU_FEATURE_OVERRIDES_H


/*
 * CPU feature overrides for MIPS boards
 */
#ifdef CONFIG_CPU_MIPS32
#define cpu_has_tlb		1
#define cpu_has_4kex		1
#define cpu_has_4kcache		1
#define cpu_has_fpu		0
/* #define cpu_has_32fpr	? */
#define cpu_has_counter		1
/* #define cpu_has_watch	? */
#define cpu_has_divec		1
#define cpu_has_vce		0
/* #define cpu_has_cache_cdex_p	? */
/* #define cpu_has_cache_cdex_s	? */
/* #define cpu_has_prefetch	? */
#define cpu_has_mcheck		1
/* #define cpu_has_ejtag	? */
#define cpu_has_llsc		1
/* #define cpu_has_vtag_icache	? */
/* #define cpu_has_dc_aliases	? */
/* #define cpu_has_ic_fills_f_dc ? */
#define cpu_has_nofpuex		0
/* #define cpu_has_64bits	? */
/* #define cpu_has_64bit_zero_reg ? */
/* #define cpu_has_subset_pcaches ? */
#endif

#ifdef CONFIG_CPU_MIPS64
#define cpu_has_tlb		1
#define cpu_has_4kex		1
#define cpu_has_4kcache		1
/* #define cpu_has_fpu		? */
/* #define cpu_has_32fpr	? */
#define cpu_has_counter		1
/* #define cpu_has_watch	? */
#define cpu_has_divec		1
#define cpu_has_vce		0
/* #define cpu_has_cache_cdex_p	? */
/* #define cpu_has_cache_cdex_s	? */
/* #define cpu_has_prefetch	? */
#define cpu_has_mcheck		1
/* #define cpu_has_ejtag	? */
#define cpu_has_llsc		1
/* #define cpu_has_vtag_icache	? */
/* #define cpu_has_dc_aliases	? */
/* #define cpu_has_ic_fills_f_dc ? */
#define cpu_has_nofpuex		0
/* #define cpu_has_64bits	? */
/* #define cpu_has_64bit_zero_reg ? */
/* #define cpu_has_subset_pcaches ? */
#endif

#endif /* __ASM_MACH_MIPS_CPU_FEATURE_OVERRIDES_H */
