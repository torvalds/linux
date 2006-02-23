/*
 * include/asm-sh/cpu-sh4/mmu_context.h
 *
 * Copyright (C) 1999 Niibe Yutaka
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_CPU_SH4_MMU_CONTEXT_H
#define __ASM_CPU_SH4_MMU_CONTEXT_H

#define MMU_PTEH	0xFF000000	/* Page table entry register HIGH */
#define MMU_PTEL	0xFF000004	/* Page table entry register LOW */
#define MMU_TTB		0xFF000008	/* Translation table base register */
#define MMU_TEA		0xFF00000C	/* TLB Exception Address */
#define MMU_PTEA	0xFF000034	/* Page table entry assistance register */

#define MMUCR		0xFF000010	/* MMU Control Register */

#define MMU_ITLB_ADDRESS_ARRAY	0xF2000000
#define MMU_UTLB_ADDRESS_ARRAY	0xF6000000
#define MMU_PAGE_ASSOC_BIT	0x80

#define MMU_NTLB_ENTRIES	64	/* for 7750 */
#ifdef CONFIG_SH_STORE_QUEUES
#define MMU_CONTROL_INIT	0x05	/* SQMD=0, SV=0, TI=1, AT=1 */
#else
#define MMU_CONTROL_INIT	0x205	/* SQMD=1, SV=0, TI=1, AT=1 */
#endif

#define MMU_ITLB_DATA_ARRAY	0xF3000000
#define MMU_UTLB_DATA_ARRAY	0xF7000000

#define MMU_UTLB_ENTRIES	   64
#define MMU_U_ENTRY_SHIFT	    8
#define MMU_UTLB_VALID		0x100
#define MMU_ITLB_ENTRIES	    4
#define MMU_I_ENTRY_SHIFT	    8
#define MMU_ITLB_VALID		0x100

#define TRA	0xff000020
#define EXPEVT	0xff000024
#define INTEVT	0xff000028

#endif /* __ASM_CPU_SH4_MMU_CONTEXT_H */

