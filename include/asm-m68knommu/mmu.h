#ifndef __M68KNOMMU_MMU_H
#define __M68KNOMMU_MMU_H

/* Copyright (C) 2002, David McCullough <davidm@snapgear.com> */

typedef struct {
	struct vm_list_struct	*vmlist;
	unsigned long		end_brk;
} mm_context_t;

#endif /* __M68KNOMMU_MMU_H */
