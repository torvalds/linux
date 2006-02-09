/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2005 Ilya A. Volynets-Evenbakh
 * Copyright (C) 2005 Ralf Baechle (ralf@linux-mips.org)
 */
#ifndef __ASM_MACH_IP32_CPU_FEATURE_OVERRIDES_H
#define __ASM_MACH_IP32_CPU_FEATURE_OVERRIDES_H

#include <linux/config.h>

/*
 * R5000 has an interesting "restriction":  ll(d)/sc(d)
 * instructions to XKPHYS region simply do uncached bus
 * requests. This breaks all the atomic bitops functions.
 * so, for 64bit IP32 kernel we just don't use ll/sc.
 * This does not affect luserland.
 */
#if (defined(CONFIG_CPU_R5000) || defined(CONFIG_CPU_NEVADA)) && defined(CONFIG_64BIT)
#define cpu_has_llsc		0
#else
#define cpu_has_llsc		1
#endif

/* Settings which are common for all ip32 CPUs */
#define cpu_has_tlb		1
#define cpu_has_4kex		1
#define cpu_has_fpu		1
#define cpu_has_32fpr		1
#define cpu_has_counter		1
#define cpu_has_mips16		0
#define cpu_has_vce		0
#define cpu_has_cache_cdex_s	0
#define cpu_has_mcheck		0
#define cpu_has_ejtag		0
#define cpu_has_vtag_icache	0
#define cpu_has_ic_fills_f_dc	0
#define cpu_has_dsp		0

#define cpu_has_mips32r1	0
#define cpu_has_mips32r2	0
#define cpu_has_mips64r1	0
#define cpu_has_mips64r2	0

#endif /* __ASM_MACH_IP32_CPU_FEATURE_OVERRIDES_H */
