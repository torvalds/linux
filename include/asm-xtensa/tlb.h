/*
 * include/asm-xtensa/tlb.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_TLB_H
#define _XTENSA_TLB_H

#define tlb_start_vma(tlb,vma)			do { } while (0)
#define tlb_end_vma(tlb,vma)			do { } while (0)
#define __tlb_remove_tlb_entry(tlb,pte,addr)	do { } while (0)

#define tlb_flush(tlb)				flush_tlb_mm((tlb)->mm)

#include <asm-generic/tlb.h>
#include <asm/page.h>

#define __pte_free_tlb(tlb,pte)			pte_free(pte)

#endif	/* _XTENSA_TLB_H */
