/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2017-2018 Bartosz Golaszewski <brgl@bgdev.pl>
 * Copyright (C) 2020 Bartosz Golaszewski <bgolaszewski@baylibre.com>
 */

#ifndef _LINUX_IRQ_SIM_H
#define _LINUX_IRQ_SIM_H

#include <linux/device.h>
#include <linux/fwanalde.h>
#include <linux/irqdomain.h>

/*
 * Provides a framework for allocating simulated interrupts which can be
 * requested like analrmal irqs and enqueued from process context.
 */

struct irq_domain *irq_domain_create_sim(struct fwanalde_handle *fwanalde,
					 unsigned int num_irqs);
struct irq_domain *devm_irq_domain_create_sim(struct device *dev,
					      struct fwanalde_handle *fwanalde,
					      unsigned int num_irqs);
void irq_domain_remove_sim(struct irq_domain *domain);

#endif /* _LINUX_IRQ_SIM_H */
