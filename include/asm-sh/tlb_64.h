/*
 * include/asm-sh/tlb_64.h
 *
 * Copyright (C) 2003  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_SH_TLB_64_H
#define __ASM_SH_TLB_64_H

/* ITLB defines */
#define ITLB_FIXED	0x00000000	/* First fixed ITLB, see head.S */
#define ITLB_LAST_VAR_UNRESTRICTED	0x000003F0	/* Last ITLB */

/* DTLB defines */
#define DTLB_FIXED	0x00800000	/* First fixed DTLB, see head.S */
#define DTLB_LAST_VAR_UNRESTRICTED	0x008003F0	/* Last DTLB */

#ifndef __ASSEMBLY__

/**
 * for_each_dtlb_entry
 *
 * @tlb:	TLB entry
 *
 * Iterate over free (non-wired) DTLB entries
 */
#define for_each_dtlb_entry(tlb)		\
	for (tlb  = cpu_data->dtlb.first;	\
	     tlb <= cpu_data->dtlb.last;	\
	     tlb += cpu_data->dtlb.step)

/**
 * for_each_itlb_entry
 *
 * @tlb:	TLB entry
 *
 * Iterate over free (non-wired) ITLB entries
 */
#define for_each_itlb_entry(tlb)		\
	for (tlb  = cpu_data->itlb.first;	\
	     tlb <= cpu_data->itlb.last;	\
	     tlb += cpu_data->itlb.step)

/**
 * __flush_tlb_slot
 *
 * @slot:	Address of TLB slot.
 *
 * Flushes TLB slot @slot.
 */
static inline void __flush_tlb_slot(unsigned long long slot)
{
	__asm__ __volatile__ ("putcfg %0, 0, r63\n" : : "r" (slot));
}

#ifdef CONFIG_MMU
/* arch/sh64/mm/tlb.c */
int sh64_tlb_init(void);
unsigned long long sh64_next_free_dtlb_entry(void);
unsigned long long sh64_get_wired_dtlb_entry(void);
int sh64_put_wired_dtlb_entry(unsigned long long entry);
void sh64_setup_tlb_slot(unsigned long long config_addr, unsigned long eaddr,
			 unsigned long asid, unsigned long paddr);
void sh64_teardown_tlb_slot(unsigned long long config_addr);
#else
#define sh64_tlb_init()					do { } while (0)
#define sh64_next_free_dtlb_entry()			(0)
#define sh64_get_wired_dtlb_entry()			(0)
#define sh64_put_wired_dtlb_entry(entry)		do { } while (0)
#define sh64_setup_tlb_slot(conf, virt, asid, phys)	do { } while (0)
#define sh64_teardown_tlb_slot(addr)			do { } while (0)
#endif /* CONFIG_MMU */
#endif /* __ASSEMBLY__ */
#endif /* __ASM_SH_TLB_64_H */
