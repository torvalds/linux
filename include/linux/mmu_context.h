/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MMU_CONTEXT_H
#define _LINUX_MMU_CONTEXT_H

#include <asm/mmu_context.h>

struct mm_struct;

void use_mm(struct mm_struct *mm);
void unuse_mm(struct mm_struct *mm);

/* Architectures that care about IRQ state in switch_mm can override this. */
#ifndef switch_mm_irqs_off
# define switch_mm_irqs_off switch_mm
#endif

#endif
