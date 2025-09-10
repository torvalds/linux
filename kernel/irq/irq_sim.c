// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017-2018 Bartosz Golaszewski <brgl@bgdev.pl>
 * Copyright (C) 2020 Bartosz Golaszewski <bgolaszewski@baylibre.com>
 */

#include <linux/cleanup.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irq_sim.h>
#include <linux/irq_work.h>
#include <linux/slab.h>

struct irq_sim_work_ctx {
	struct irq_work		work;
	unsigned int		irq_count;
	unsigned long		*pending;
	struct irq_domain	*domain;
	struct irq_sim_ops	ops;
	void			*user_data;
};

struct irq_sim_irq_ctx {
	bool			enabled;
	struct irq_sim_work_ctx	*work_ctx;
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

static int irq_sim_set_type(struct irq_data *data, unsigned int type)
{
	/* We only support rising and falling edge trigger types. */
	if (type & ~IRQ_TYPE_EDGE_BOTH)
		return -EINVAL;

	irqd_set_trigger_type(data, type);

	return 0;
}

static int irq_sim_get_irqchip_state(struct irq_data *data,
				     enum irqchip_irq_state which, bool *state)
{
	struct irq_sim_irq_ctx *irq_ctx = irq_data_get_irq_chip_data(data);
	irq_hw_number_t hwirq = irqd_to_hwirq(data);

	switch (which) {
	case IRQCHIP_STATE_PENDING:
		if (irq_ctx->enabled)
			*state = test_bit(hwirq, irq_ctx->work_ctx->pending);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int irq_sim_set_irqchip_state(struct irq_data *data,
				     enum irqchip_irq_state which, bool state)
{
	struct irq_sim_irq_ctx *irq_ctx = irq_data_get_irq_chip_data(data);
	irq_hw_number_t hwirq = irqd_to_hwirq(data);

	switch (which) {
	case IRQCHIP_STATE_PENDING:
		if (irq_ctx->enabled) {
			assign_bit(hwirq, irq_ctx->work_ctx->pending, state);
			if (state)
				irq_work_queue(&irq_ctx->work_ctx->work);
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int irq_sim_request_resources(struct irq_data *data)
{
	struct irq_sim_irq_ctx *irq_ctx = irq_data_get_irq_chip_data(data);
	struct irq_sim_work_ctx *work_ctx = irq_ctx->work_ctx;
	irq_hw_number_t hwirq = irqd_to_hwirq(data);

	if (work_ctx->ops.irq_sim_irq_requested)
		return work_ctx->ops.irq_sim_irq_requested(work_ctx->domain,
							   hwirq,
							   work_ctx->user_data);

	return 0;
}

static void irq_sim_release_resources(struct irq_data *data)
{
	struct irq_sim_irq_ctx *irq_ctx = irq_data_get_irq_chip_data(data);
	struct irq_sim_work_ctx *work_ctx = irq_ctx->work_ctx;
	irq_hw_number_t hwirq = irqd_to_hwirq(data);

	if (work_ctx->ops.irq_sim_irq_released)
		work_ctx->ops.irq_sim_irq_released(work_ctx->domain, hwirq,
						   work_ctx->user_data);
}

static struct irq_chip irq_sim_irqchip = {
	.name			= "irq_sim",
	.irq_mask		= irq_sim_irqmask,
	.irq_unmask		= irq_sim_irqunmask,
	.irq_set_type		= irq_sim_set_type,
	.irq_get_irqchip_state	= irq_sim_get_irqchip_state,
	.irq_set_irqchip_state	= irq_sim_set_irqchip_state,
	.irq_request_resources	= irq_sim_request_resources,
	.irq_release_resources	= irq_sim_release_resources,
};

static void irq_sim_handle_irq(struct irq_work *work)
{
	struct irq_sim_work_ctx *work_ctx;
	unsigned int offset = 0;
	int irqnum;

	work_ctx = container_of(work, struct irq_sim_work_ctx, work);

	while (!bitmap_empty(work_ctx->pending, work_ctx->irq_count)) {
		offset = find_next_bit(work_ctx->pending,
				       work_ctx->irq_count, offset);
		clear_bit(offset, work_ctx->pending);
		irqnum = irq_find_mapping(work_ctx->domain, offset);
		handle_simple_irq(irq_to_desc(irqnum));
	}
}

static int irq_sim_domain_map(struct irq_domain *domain,
			      unsigned int virq, irq_hw_number_t hw)
{
	struct irq_sim_work_ctx *work_ctx = domain->host_data;
	struct irq_sim_irq_ctx *irq_ctx;

	irq_ctx = kzalloc(sizeof(*irq_ctx), GFP_KERNEL);
	if (!irq_ctx)
		return -ENOMEM;

	irq_set_chip(virq, &irq_sim_irqchip);
	irq_set_chip_data(virq, irq_ctx);
	irq_set_handler(virq, handle_simple_irq);
	irq_modify_status(virq, IRQ_NOREQUEST | IRQ_NOAUTOEN, IRQ_NOPROBE);
	irq_ctx->work_ctx = work_ctx;

	return 0;
}

static void irq_sim_domain_unmap(struct irq_domain *domain, unsigned int virq)
{
	struct irq_sim_irq_ctx *irq_ctx;
	struct irq_data *irqd;

	irqd = irq_domain_get_irq_data(domain, virq);
	irq_ctx = irq_data_get_irq_chip_data(irqd);

	irq_set_handler(virq, NULL);
	irq_domain_reset_irq_data(irqd);
	kfree(irq_ctx);
}

static const struct irq_domain_ops irq_sim_domain_ops = {
	.map		= irq_sim_domain_map,
	.unmap		= irq_sim_domain_unmap,
};

/**
 * irq_domain_create_sim - Create a new interrupt simulator irq_domain and
 *                         allocate a range of dummy interrupts.
 *
 * @fwnode:     struct fwnode_handle to be associated with this domain.
 * @num_irqs:   Number of interrupts to allocate.
 *
 * On success: return a new irq_domain object.
 * On failure: a negative errno wrapped with ERR_PTR().
 */
struct irq_domain *irq_domain_create_sim(struct fwnode_handle *fwnode,
					 unsigned int num_irqs)
{
	return irq_domain_create_sim_full(fwnode, num_irqs, NULL, NULL);
}
EXPORT_SYMBOL_GPL(irq_domain_create_sim);

struct irq_domain *irq_domain_create_sim_full(struct fwnode_handle *fwnode,
					      unsigned int num_irqs,
					      const struct irq_sim_ops *ops,
					      void *data)
{
	struct irq_sim_work_ctx *work_ctx __free(kfree) =
				kzalloc(sizeof(*work_ctx), GFP_KERNEL);

	if (!work_ctx)
		return ERR_PTR(-ENOMEM);

	unsigned long *pending __free(bitmap) = bitmap_zalloc(num_irqs, GFP_KERNEL);
	if (!pending)
		return ERR_PTR(-ENOMEM);

	work_ctx->domain = irq_domain_create_linear(fwnode, num_irqs,
						    &irq_sim_domain_ops,
						    work_ctx);
	if (!work_ctx->domain)
		return ERR_PTR(-ENOMEM);

	work_ctx->irq_count = num_irqs;
	work_ctx->work = IRQ_WORK_INIT_HARD(irq_sim_handle_irq);
	work_ctx->pending = no_free_ptr(pending);
	work_ctx->user_data = data;

	if (ops)
		memcpy(&work_ctx->ops, ops, sizeof(*ops));

	return no_free_ptr(work_ctx)->domain;
}
EXPORT_SYMBOL_GPL(irq_domain_create_sim_full);

/**
 * irq_domain_remove_sim - Deinitialize the interrupt simulator domain: free
 *                         the interrupt descriptors and allocated memory.
 *
 * @domain:     The interrupt simulator domain to tear down.
 */
void irq_domain_remove_sim(struct irq_domain *domain)
{
	struct irq_sim_work_ctx *work_ctx = domain->host_data;

	irq_work_sync(&work_ctx->work);
	bitmap_free(work_ctx->pending);
	kfree(work_ctx);

	irq_domain_remove(domain);
}
EXPORT_SYMBOL_GPL(irq_domain_remove_sim);

static void devm_irq_domain_remove_sim(void *data)
{
	struct irq_domain *domain = data;

	irq_domain_remove_sim(domain);
}

/**
 * devm_irq_domain_create_sim - Create a new interrupt simulator for
 *                              a managed device.
 *
 * @dev:        Device to initialize the simulator object for.
 * @fwnode:     struct fwnode_handle to be associated with this domain.
 * @num_irqs:   Number of interrupts to allocate
 *
 * On success: return a new irq_domain object.
 * On failure: a negative errno wrapped with ERR_PTR().
 */
struct irq_domain *devm_irq_domain_create_sim(struct device *dev,
					      struct fwnode_handle *fwnode,
					      unsigned int num_irqs)
{
	return devm_irq_domain_create_sim_full(dev, fwnode, num_irqs,
					       NULL, NULL);
}
EXPORT_SYMBOL_GPL(devm_irq_domain_create_sim);

struct irq_domain *
devm_irq_domain_create_sim_full(struct device *dev,
				struct fwnode_handle *fwnode,
				unsigned int num_irqs,
				const struct irq_sim_ops *ops,
				void *data)
{
	struct irq_domain *domain;
	int ret;

	domain = irq_domain_create_sim_full(fwnode, num_irqs, ops, data);
	if (IS_ERR(domain))
		return domain;

	ret = devm_add_action_or_reset(dev, devm_irq_domain_remove_sim, domain);
	if (ret)
		return ERR_PTR(ret);

	return domain;
}
EXPORT_SYMBOL_GPL(devm_irq_domain_create_sim_full);
