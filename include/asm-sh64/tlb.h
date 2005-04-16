/*
 * include/asm-sh64/tlb.h
 *
 * Copyright (C) 2003  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */
#ifndef __ASM_SH64_TLB_H
#define __ASM_SH64_TLB_H

/*
 * Note! These are mostly unused, we just need the xTLB_LAST_VAR_UNRESTRICTED
 * for head.S! Once this limitation is gone, we can clean the rest of this up.
 */

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

/* arch/sh64/mm/tlb.c */
extern int sh64_tlb_init(void);
extern unsigned long long sh64_next_free_dtlb_entry(void);
extern unsigned long long sh64_get_wired_dtlb_entry(void);
extern int sh64_put_wired_dtlb_entry(unsigned long long entry);

extern void sh64_setup_tlb_slot(unsigned long long config_addr, unsigned long eaddr, unsigned long asid, unsigned long paddr);
extern void sh64_teardown_tlb_slot(unsigned long long config_addr);

#define tlb_start_vma(tlb, vma) \
	flush_cache_range(vma, vma->vm_start, vma->vm_end)

#define tlb_end_vma(tlb, vma)	\
	flush_tlb_range(vma, vma->vm_start, vma->vm_end)

#define __tlb_remove_tlb_entry(tlb, pte, address)	do { } while (0)

/*
 * Flush whole TLBs for MM
 */
#define tlb_flush(tlb)		flush_tlb_mm((tlb)->mm)

#include <asm-generic/tlb.h>

#endif /* __ASSEMBLY__ */

#endif /* __ASM_SH64_TLB_H */

