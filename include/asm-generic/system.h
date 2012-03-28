/* Generic system definitions, based on MN10300 definitions.
 *
 * It should be possible to use these on really simple architectures,
 * but it serves more as a starting point for new ports.
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef __ASM_GENERIC_SYSTEM_H
#define __ASM_GENERIC_SYSTEM_H

#ifndef __ASSEMBLY__

#include <linux/types.h>

#include <asm/barrier.h>
#include <asm/cmpxchg.h>

struct task_struct;

/* context switching is now performed out-of-line in switch_to.S */
extern struct task_struct *__switch_to(struct task_struct *,
		struct task_struct *);
#define switch_to(prev, next, last)					\
	do {								\
		((last) = __switch_to((prev), (next)));			\
	} while (0)

#define arch_align_stack(x) (x)

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_GENERIC_SYSTEM_H */
