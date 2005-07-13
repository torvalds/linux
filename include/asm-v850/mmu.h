/* Copyright (C) 2002, 2005, David McCullough <davidm@snapgear.com> */

#ifndef __V850_MMU_H__
#define __V850_MMU_H__

typedef struct {
	struct vm_list_struct	*vmlist;
	unsigned long		end_brk;
} mm_context_t;

#endif /* __V850_MMU_H__ */
