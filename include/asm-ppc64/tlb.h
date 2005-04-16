/*
 *	TLB shootdown specifics for PPC64
 *
 * Copyright (C) 2002 Anton Blanchard, IBM Corp.
 * Copyright (C) 2002 Paul Mackerras, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _PPC64_TLB_H
#define _PPC64_TLB_H

#include <asm/tlbflush.h>

struct mmu_gather;

extern void pte_free_finish(void);

static inline void tlb_flush(struct mmu_gather *tlb)
{
	flush_tlb_pending();
	pte_free_finish();
}

/* Avoid pulling in another include just for this */
#define check_pgt_cache()	do { } while (0)

/* Get the generic bits... */
#include <asm-generic/tlb.h>

/* Nothing needed here in fact... */
#define tlb_start_vma(tlb, vma)	do { } while (0)
#define tlb_end_vma(tlb, vma)	do { } while (0)

#define __tlb_remove_tlb_entry(tlb, pte, address) do { } while (0)

#endif /* _PPC64_TLB_H */
