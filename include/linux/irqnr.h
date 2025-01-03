/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_IRQNR_H
#define _LINUX_IRQNR_H

#include <uapi/linux/irqnr.h>


unsigned int irq_get_nr_irqs(void) __pure;
unsigned int irq_set_nr_irqs(unsigned int nr);
extern struct irq_desc *irq_to_desc(unsigned int irq);
unsigned int irq_get_next_irq(unsigned int offset);

#define for_each_irq_desc(irq, desc)                                      \
	for (unsigned int __nr_irqs__ = irq_get_nr_irqs(); __nr_irqs__;   \
	     __nr_irqs__ = 0)                                             \
		for (irq = 0, desc = irq_to_desc(irq); irq < __nr_irqs__; \
		     irq++, desc = irq_to_desc(irq))                      \
			if (!desc)                                        \
				;                                         \
			else

# define for_each_irq_desc_reverse(irq, desc)				\
	for (irq = irq_get_nr_irqs() - 1, desc = irq_to_desc(irq);	\
	     irq >= 0; irq--, desc = irq_to_desc(irq))			\
		if (!desc)						\
			;						\
		else

#define for_each_active_irq(irq)                                        \
	for (unsigned int __nr_irqs__ = irq_get_nr_irqs(); __nr_irqs__; \
	     __nr_irqs__ = 0)                                           \
		for (irq = irq_get_next_irq(0); irq < __nr_irqs__;      \
		     irq = irq_get_next_irq(irq + 1))

#define for_each_irq_nr(irq)                                            \
	for (unsigned int __nr_irqs__ = irq_get_nr_irqs(); __nr_irqs__; \
	     __nr_irqs__ = 0)                                           \
		for (irq = 0; irq < __nr_irqs__; irq++)

#endif
