#ifndef __ARM_MMU_H
#define __ARM_MMU_H

#ifdef CONFIG_MMU

typedef struct {
#if __LINUX_ARM_ARCH__ >= 6
	unsigned int id;
#endif
	unsigned int kvm_seq;
} mm_context_t;

#if __LINUX_ARM_ARCH__ >= 6
#define ASID(mm)	((mm)->context.id & 255)
#else
#define ASID(mm)	(0)
#endif

#else

/*
 * From nommu.h:
 *  Copyright (C) 2002, David McCullough <davidm@snapgear.com>
 *  modified for 2.6 by Hyok S. Choi <hyok.choi@samsung.com>
 */
typedef struct {
	struct vm_list_struct	*vmlist;
	unsigned long		end_brk;
} mm_context_t;

#endif

#endif
