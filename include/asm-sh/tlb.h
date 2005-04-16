#ifndef __ASM_SH_TLB_H
#define __ASM_SH_TLB_H

#define tlb_start_vma(tlb, vma) \
	flush_cache_range(vma, vma->vm_start, vma->vm_end)

#define tlb_end_vma(tlb, vma)	\
	flush_tlb_range(vma, vma->vm_start, vma->vm_end)

#define __tlb_remove_tlb_entry(tlb, pte, address)	do { } while (0)

/*
 * Flush whole TLBs for MM
 */
#define tlb_flush(tlb)				flush_tlb_mm((tlb)->mm)

#include <asm-generic/tlb.h>
#endif
