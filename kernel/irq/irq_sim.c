// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017-2018 Bartosz Golaszewski <brgl@bgdev.pl>
 */

#include <linux/slab.h>
#include <linux/irq_sim.h>
#include <linux/irq.h>

struct irq_sim_devres {
	struct irq_sim		*sim;
};

static void irq_sim_irqmask(struct irq_data *data)
{
	struct irq_sim_irq_ctx *irq_ctx = irq_data_get_irq_chip_data(data);

	irq_ctx->enabled = false;
}

static void irq_sim_irqunmask(struct irq_data *data)
{
	struct irq_sim_irq_ctx *irq_ctx = irq_data_get_irq_chip_data(data);

	irq_ctx->enabled = true;
}

static struct irq_chip irq_sim_irqchip = {
	.name		= "irq_sim",
	.irq_mask	= irq_sim_irqmask,
	.irq_unmask	= irq_sim_irqunmask,
};

static void irq_sim_handle_irq(struct irq_work *work)
{
	struct irq_sim_work_ctx *work_ctx;

	work_ctx = container_of(work, struct irq_sim_work_ctx, work);
	handle_simple_irq(irq_to_desc(work_ctx->irq));
}

/**
 * irq_sim_init - Initialize the interrupt simulator: allocate a range of
 *                dummy interrupts.
 *
 * @sim:        The interrupt simulator object to initialize.
 * @num_irqs:   Number of interrupts to allocate
 *
 * On success: return the base of the allocated interrupt range.
 * On failure: a negative errno.
 */
int irq_sim_init(struct irq_sim *sim, unsigned int num_irqs)
{
	int i;

	sim->irqs = kmalloc_array(num_irqs, sizeof(*sim->irqs), GFP_KERNEL);
	if (!sim->irqs)
		return -ENOMEM;

	sim->irq_base = irq_alloc_descs(-1, 0, num_irqs, 0);
	if (sim->irq_base < 0) {
		kfree(sim->irqs);
		return sim->irq_base;
	}

	for (i = 0; i < num_irqs; i++) {
		sim->irqs[i].irqnum = sim->irq_base + i;
		sim->irqs[i].enabled = false;
		irq_set_chip(sim->irq_base + i, &irq_sim_irqchip);
		irq_set_chip_data(sim->irq_base + i, &sim->irqs[i]);
		irq_set_handler(sim->irq_base + i, &handle_simple_irq);
		irq_modify_status(sim->irq_base + i,
				  IRQ_NOREQUEST | IRQ_NOAUTOEN, IRQ_NOPROBE);
	}

	init_irq_work(&sim->work_ctx.work, irq_sim_handle_irq);
	sim->irq_count = num_irqs;

	return sim->irq_base;
}
EXPORT_SYMBOL_GPL(irq_sim_init);

/**
 * irq_sim_fini - Deinitialize the interrupt simulator: free the interrupt
 *                descriptors and allocated memory.
 *
 * @sim:        The interrupt simulator to tear down.
 */
void irq_sim_fini(struct irq_sim *sim)
{
	irq_work_sync(&sim->work_ctx.work);
	irq_free_descs(sim->irq_base, sim->irq_count);
	kfree(sim->irqs);
}
EXPORT_SYMBOL_GPL(irq_sim_fini);

static void devm_irq_sim_release(struct device *dev, void *res)
{
	struct irq_sim_devres *this = res;

	irq_sim_fini(this->sim);
}

/**
 * irq_sim_init - Initialize the interrupt simulator for a managed device.
 *
 * @dev:        Device to initialize the simulator object for.
 * @sim:        The interrupt simulator object to initialize.
 * @num_irqs:   Number of interrupts to allocate
 *
 * On success: return the base of the allocated interrupt range.
 * On failure: a negative errno.
 */
int devm_irq_sim_init(struct device *dev, struct irq_sim *sim,
		      unsigned int num_irqs)
{
	struct irq_sim_devres *dr;
	int rv;

	dr = devres_alloc(devm_irq_sim_release, sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	rv = irq_sim_init(sim, num_irqs);
	if (rv < 0) {
		devres_free(dr);
		return rv;
	}

	dr->sim = sim;
	devres_add(dev, dr);

	return rv;
}
EXPORT_SYMBOL_GPL(devm_irq_sim_init);

/**
 * irq_sim_fire - Enqueue an interrupt.
 *
 * @sim:        The interrupt simulator object.
 * @offset:     Offset of the simulated interrupt which should be fired.
 */
void irq_sim_fire(struct irq_sim *sim, unsigned int offset)
{
	if (sim->irqs[offset].enabled) {
		sim->work_ctx.irq = irq_sim_irqnum(sim, offset);
		irq_work_queue(&sim->work_ctx.work);
	}
}
EXPORT_SYMBOL_GPL(irq_sim_fire);

/**
 * irq_sim_irqnum - Get the allocated number of a dummy interrupt.
 *
 * @sim:        The interrupt simulator object.
 * @offset:     Offset of the simulated interrupt for which to retrieve
 *              the number.
 */
int irq_sim_irqnum(struct irq_sim *sim, unsigned int offset)
{
	return sim->irqs[offset].irqnum;
}
EXPORT_SYMBOL_GPL(irq_sim_irqnum);
