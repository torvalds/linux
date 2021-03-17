/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __IRQ_IXP4XX_H
#define __IRQ_IXP4XX_H

#include <linux/ioport.h>
struct irq_domain;

void ixp4xx_irq_init(resource_size_t irqbase,
		     bool is_356);
struct irq_domain *ixp4xx_get_irq_domain(void);

#endif /* __IRQ_IXP4XX_H */
