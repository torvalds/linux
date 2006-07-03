/*
 * include/asm-xtensa/irq.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_IRQ_H
#define _XTENSA_IRQ_H

#include <asm/platform/hardware.h>

#include <xtensa/config/core.h>

#ifndef PLATFORM_NR_IRQS
# define PLATFORM_NR_IRQS 0
#endif
#define XTENSA_NR_IRQS XCHAL_NUM_INTERRUPTS
#define NR_IRQS (XTENSA_NR_IRQS + PLATFORM_NR_IRQS)

static __inline__ int irq_canonicalize(int irq)
{
	return (irq);
}

struct irqaction;
#if 0 // FIXME
extern void disable_irq_nosync(unsigned int);
extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);
#endif

#endif	/* _XTENSA_IRQ_H */
