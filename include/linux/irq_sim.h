/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2017-2018 Bartosz Golaszewski <brgl@bgdev.pl>
 * Copyright (C) 2020 Bartosz Golaszewski <bgolaszewski@baylibre.com>
 */

#ifndef _LINUX_IRQ_SIM_H
#define _LINUX_IRQ_SIM_H

#include <linux/device.h>
#include <linux/fwnode.h>
#include <linux/irqdomain.h>

/*
 * Provides a framework for allocating simulated interrupts which can be
 * requested like normal irqs and enqueued from process context.
 */

struct irq_sim_ops {
	int (*irq_sim_irq_requested)(struct irq_domain *domain,
				     irq_hw_number_t hwirq, void *data);
	void (*irq_sim_irq_released)(struct irq_domain *domain,
				     irq_hw_number_t hwirq, void *data);
};

struct irq_domain *irq_domain_create_sim(struct fwnode_handle *fwnode,
					 unsigned int num_irqs);
struct irq_domain *devm_irq_domain_create_sim(struct device *dev,
					      struct fwnode_handle *fwnode,
					      unsigned int num_irqs);
struct irq_domain *irq_domain_create_sim_full(struct fwnode_handle *fwnode,
					      unsigned int num_irqs,
					      const struct irq_sim_ops *ops,
					      void *data);
struct irq_domain *
devm_irq_domain_create_sim_full(struct device *dev,
				struct fwnode_handle *fwnode,
				unsigned int num_irqs,
				const struct irq_sim_ops *ops,
				void *data);
void irq_domain_remove_sim(struct irq_domain *domain);

#endif /* _LINUX_IRQ_SIM_H */
