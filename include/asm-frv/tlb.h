#ifndef _ASM_TLB_H
#define _ASM_TLB_H

#include <asm/tlbflush.h>

#define check_pgt_cache() do {} while(0)

/*
 * we don't need any special per-pte or per-vma handling...
 */
#define tlb_start_vma(tlb, vma)				do { } while (0)
#define tlb_end_vma(tlb, vma)				do { } while (0)
#define __tlb_remove_tlb_entry(tlb, ptep, address)	do { } while (0)

/*
 * .. because we flush the whole mm when it fills up
 */
#define tlb_flush(tlb)		flush_tlb_mm((tlb)->mm)

#include <asm-generic/tlb.h>

#endif /* _ASM_TLB_H */

