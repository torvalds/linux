/* mmu_context.h: MMU context management routines
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_MMU_CONTEXT_H
#define _ASM_MMU_CONTEXT_H

#include <asm/setup.h>
#include <asm/page.h>
#include <asm/pgalloc.h>

static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
}

#ifdef CONFIG_MMU
extern int init_new_context(struct task_struct *tsk, struct mm_struct *mm);
extern void change_mm_context(mm_context_t *old, mm_context_t *ctx, pgd_t *_pgd);
extern void destroy_context(struct mm_struct *mm);

#else
#define init_new_context(tsk, mm)		({ 0; })
#define change_mm_context(old, ctx, _pml4)	do {} while(0)
#define destroy_context(mm)			do {} while(0)
#endif

#define switch_mm(prev, next, tsk)						\
do {										\
	if (prev != next)							\
		change_mm_context(&prev->context, &next->context, next->pgd);	\
} while(0)

#define activate_mm(prev, next)						\
do {									\
	change_mm_context(&prev->context, &next->context, next->pgd);	\
} while(0)

#define deactivate_mm(tsk, mm)			\
do {						\
} while(0)

#endif
