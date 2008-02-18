#ifndef __ASM_SH_TLB_H
#define __ASM_SH_TLB_H

#ifdef CONFIG_SUPERH64
# include "tlb_64.h"
#endif

#ifndef __ASSEMBLY__

#define tlb_start_vma(tlb, vma) \
	flush_cache_range(vma, vma->vm_start, vma->vm_end)

#define tlb_end_vma(tlb, vma)	\
	flush_tlb_range(vma, vma->vm_start, vma->vm_end)

#define __tlb_remove_tlb_entry(tlb, pte, address)	do { } while (0)

/*
 * Flush whole TLBs for MM
 */
#define tlb_flush(tlb)				flush_tlb_mm((tlb)->mm)

#include <linux/pagemap.h>
#include <asm-generic/tlb.h>

#endif /* __ASSEMBLY__ */
#endif /* __ASM_SH_TLB_H */
