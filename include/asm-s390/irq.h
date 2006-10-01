#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

#ifdef __KERNEL__
#include <linux/hardirq.h>

/*
 * the definition of irqs has changed in 2.5.46:
 * NR_IRQS is no longer the number of i/o
 * interrupts (65536), but rather the number
 * of interrupt classes (2).
 * Only external and i/o interrupts make much sense here (CH).
 */

enum interruption_class {
	EXTERNAL_INTERRUPT,
	IO_INTERRUPT,

	NR_IRQS,
};

#endif /* __KERNEL__ */
#endif
