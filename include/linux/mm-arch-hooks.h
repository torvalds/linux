/*
 * Generic mm no-op hooks.
 *
 * Copyright (C) 2015, IBM Corporation
 * Author: Laurent Dufour <ldufour@linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _LINUX_MM_ARCH_HOOKS_H
#define _LINUX_MM_ARCH_HOOKS_H

#include <asm/mm-arch-hooks.h>

#ifndef arch_remap
static inline void arch_remap(struct mm_struct *mm,
			      unsigned long old_start, unsigned long old_end,
			      unsigned long new_start, unsigned long new_end)
{
}
#define arch_remap arch_remap
#endif

#endif /* _LINUX_MM_ARCH_HOOKS_H */
