#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>

static LIST_HEAD(irq_domain_list);
static DEFINE_MUTEX(irq_domain_mutex);

/**
 * irq_domain_add() - Register an irq_domain
 * @domain: ptr to initialized irq_domain structure
 *
 * Registers an irq_domain structure.  The irq_domain must at a minimum be
 * initialized with an ops structure pointer, and either a ->to_irq hook or
 * a valid irq_base value.  Everything else is optional.
 */
void irq_domain_add(struct irq_domain *domain)
{
	struct irq_data *d;
	int hwirq, irq;

	/*
	 * This assumes that the irq_domain owner has already allocated
	 * the irq_descs.  This block will be removed when support for dynamic
	 * allocation of irq_descs is added to irq_domain.
	 */
	irq_domain_for_each_irq(domain, hwirq, irq) {
		d = irq_get_irq_data(irq);
		if (!d) {
			WARN(1, "error: assigning domain to non existant irq_desc");
			return;
		}
		if (d->domain) {
			/* things are broken; just report, don't clean up */
			WARN(1, "error: irq_desc already assigned to a domain");
			return;
		}
		d->domain = domain;
		d->hwirq = hwirq;
	}

	mutex_lock(&irq_domain_mutex);
	list_add(&domain->list, &irq_domain_list);
	mutex_unlock(&irq_domain_mutex);
}

/**
 * irq_domain_del() - Unregister an irq_domain
 * @domain: ptr to registered irq_domain.
 */
void irq_domain_del(struct irq_domain *domain)
{
	struct irq_data *d;
	int hwirq, irq;

	mutex_lock(&irq_domain_mutex);
	list_del(&domain->list);
	mutex_unlock(&irq_domain_mutex);

	/* Clear the irq_domain assignments */
	irq_domain_for_each_irq(domain, hwirq, irq) {
		d = irq_get_irq_data(irq);
		d->domain = NULL;
	}
}

#if defined(CONFIG_OF_IRQ)
/**
 * irq_create_of_mapping() - Map a linux irq number from a DT interrupt spec
 *
 * Used by the device tree interrupt mapping code to translate a device tree
 * interrupt specifier to a valid linux irq number.  Returns either a valid
 * linux IRQ number or 0.
 *
 * When the caller no longer need the irq number returned by this function it
 * should arrange to call irq_dispose_mapping().
 */
unsigned int irq_create_of_mapping(struct device_node *controller,
				   const u32 *intspec, unsigned int intsize)
{
	struct irq_domain *domain;
	unsigned long hwirq;
	unsigned int irq, type;
	int rc = -EINVAL;

	/* Find a domain which can translate the irq spec */
	mutex_lock(&irq_domain_mutex);
	list_for_each_entry(domain, &irq_domain_list, list) {
		if (!domain->ops->dt_translate)
			continue;
		rc = domain->ops->dt_translate(domain, controller,
					intspec, intsize, &hwirq, &type);
		if (rc == 0)
			break;
	}
	mutex_unlock(&irq_domain_mutex);

	if (rc != 0)
		return 0;

	irq = irq_domain_to_irq(domain, hwirq);
	if (type != IRQ_TYPE_NONE)
		irq_set_irq_type(irq, type);
	pr_debug("%s: mapped hwirq=%i to irq=%i, flags=%x\n",
		 controller->full_name, (int)hwirq, irq, type);
	return irq;
}
EXPORT_SYMBOL_GPL(irq_create_of_mapping);

/**
 * irq_dispose_mapping() - Discard a mapping created by irq_create_of_mapping()
 * @irq: linux irq number to be discarded
 *
 * Calling this function indicates the caller no longer needs a reference to
 * the linux irq number returned by a prior call to irq_create_of_mapping().
 */
void irq_dispose_mapping(unsigned int irq)
{
	/*
	 * nothing yet; will be filled when support for dynamic allocation of
	 * irq_descs is added to irq_domain
	 */
}
EXPORT_SYMBOL_GPL(irq_dispose_mapping);

int irq_domain_simple_dt_translate(struct irq_domain *d,
			    struct device_node *controller,
			    const u32 *intspec, unsigned int intsize,
			    unsigned long *out_hwirq, unsigned int *out_type)
{
	if (d->of_node != controller)
		return -EINVAL;
	if (intsize < 1)
		return -EINVAL;
	if (d->nr_irq && ((intspec[0] < d->hwirq_base) ||
	    (intspec[0] >= d->hwirq_base + d->nr_irq)))
		return -EINVAL;

	*out_hwirq = intspec[0];
	*out_type = IRQ_TYPE_NONE;
	if (intsize > 1)
		*out_type = intspec[1] & IRQ_TYPE_SENSE_MASK;
	return 0;
}

/**
 * irq_domain_create_simple() - Set up a 'simple' translation range
 */
void irq_domain_add_simple(struct device_node *controller, int irq_base)
{
	struct irq_domain *domain;

	domain = kzalloc(sizeof(*domain), GFP_KERNEL);
	if (!domain) {
		WARN_ON(1);
		return;
	}

	domain->irq_base = irq_base;
	domain->of_node = of_node_get(controller);
	domain->ops = &irq_domain_simple_ops;
	irq_domain_add(domain);
}
EXPORT_SYMBOL_GPL(irq_domain_add_simple);

void irq_domain_generate_simple(const struct of_device_id *match,
				u64 phys_base, unsigned int irq_start)
{
	struct device_node *node;
	pr_info("looking for phys_base=%llx, irq_start=%i\n",
		(unsigned long long) phys_base, (int) irq_start);
	node = of_find_matching_node_by_address(NULL, match, phys_base);
	if (node)
		irq_domain_add_simple(node, irq_start);
	else
		pr_info("no node found\n");
}
EXPORT_SYMBOL_GPL(irq_domain_generate_simple);
#endif /* CONFIG_OF_IRQ */

struct irq_domain_ops irq_domain_simple_ops = {
#ifdef CONFIG_OF_IRQ
	.dt_translate = irq_domain_simple_dt_translate,
#endif /* CONFIG_OF_IRQ */
};
EXPORT_SYMBOL_GPL(irq_domain_simple_ops);
