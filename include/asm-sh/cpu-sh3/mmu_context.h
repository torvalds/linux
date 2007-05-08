/*
 * include/asm-sh/cpu-sh3/mmu_context.h
 *
 * Copyright (C) 1999 Niibe Yutaka
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_CPU_SH3_MMU_CONTEXT_H
#define __ASM_CPU_SH3_MMU_CONTEXT_H

#define MMU_PTEH	0xFFFFFFF0	/* Page table entry register HIGH */
#define MMU_PTEL	0xFFFFFFF4	/* Page table entry register LOW */
#define MMU_TTB		0xFFFFFFF8	/* Translation table base register */
#define MMU_TEA		0xFFFFFFFC	/* TLB Exception Address */

#define MMUCR		0xFFFFFFE0	/* MMU Control Register */

#define MMU_TLB_ADDRESS_ARRAY	0xF2000000
#define MMU_PAGE_ASSOC_BIT	0x80

#define MMU_NTLB_ENTRIES	128	/* for 7708 */
#define MMU_NTLB_WAYS		4
#define MMU_CONTROL_INIT	0x007	/* SV=0, TF=1, IX=1, AT=1 */

#define TRA	0xffffffd0
#define EXPEVT	0xffffffd4

#if defined(CONFIG_CPU_SUBTYPE_SH7707) || \
    defined(CONFIG_CPU_SUBTYPE_SH7709) || \
    defined(CONFIG_CPU_SUBTYPE_SH7706) || \
    defined(CONFIG_CPU_SUBTYPE_SH7300) || \
    defined(CONFIG_CPU_SUBTYPE_SH7705) || \
    defined(CONFIG_CPU_SUBTYPE_SH7712) || \
    defined(CONFIG_CPU_SUBTYPE_SH7710)
#define INTEVT	0xa4000000	/* INTEVTE2(0xa4000000) */
#else
#define INTEVT	0xffffffd8
#endif

#endif /* __ASM_CPU_SH3_MMU_CONTEXT_H */

