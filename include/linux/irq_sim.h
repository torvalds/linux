#ifndef _LINUX_IRQ_SIM_H
#define _LINUX_IRQ_SIM_H
/*
 * Copyright (C) 2017 Bartosz Golaszewski <brgl@bgdev.pl>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/irq_work.h>
#include <linux/device.h>

/*
 * Provides a framework for allocating simulated interrupts which can be
 * requested like normal irqs and enqueued from process context.
 */

struct irq_sim_work_ctx {
	struct irq_work		work;
	int			irq;
};

struct irq_sim_irq_ctx {
	int			irqnum;
	bool			enabled;
};

struct irq_sim {
	struct irq_sim_work_ctx	work_ctx;
	int			irq_base;
	unsigned int		irq_count;
	struct irq_sim_irq_ctx	*irqs;
};

int irq_sim_init(struct irq_sim *sim, unsigned int num_irqs);
int devm_irq_sim_init(struct device *dev, struct irq_sim *sim,
		      unsigned int num_irqs);
void irq_sim_fini(struct irq_sim *sim);
void irq_sim_fire(struct irq_sim *sim, unsigned int offset);
int irq_sim_irqnum(struct irq_sim *sim, unsigned int offset);

#endif /* _LINUX_IRQ_SIM_H */
