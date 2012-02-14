#include <linux/debugfs.h>
#include <linux/hardirq.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/fs.h>

static LIST_HEAD(irq_domain_list);
static DEFINE_MUTEX(irq_domain_mutex);

#ifdef CONFIG_PPC
static DEFINE_MUTEX(revmap_trees_mutex);
static unsigned int irq_virq_count = NR_IRQS;
static struct irq_domain *irq_default_domain;

static int default_irq_domain_match(struct irq_domain *d, struct device_node *np)
{
	return d->of_node != NULL && d->of_node == np;
}

/**
 * irq_alloc_host() - Allocate a new irq_domain data structure
 * @of_node: optional device-tree node of the interrupt controller
 * @revmap_type: type of reverse mapping to use
 * @revmap_arg: for IRQ_DOMAIN_MAP_LINEAR linear only: size of the map
 * @ops: map/unmap domain callbacks
 * @inval_irq: provide a hw number in that domain space that is always invalid
 *
 * Allocates and initialize and irq_domain structure. Note that in the case of
 * IRQ_DOMAIN_MAP_LEGACY, the map() callback will be called before this returns
 * for all legacy interrupts except 0 (which is always the invalid irq for
 * a legacy controller). For a IRQ_DOMAIN_MAP_LINEAR, the map is allocated by
 * this call as well. For a IRQ_DOMAIN_MAP_TREE, the radix tree will be
 * allocated later during boot automatically (the reverse mapping will use the
 * slow path until that happens).
 */
struct irq_domain *irq_alloc_host(struct device_node *of_node,
				unsigned int revmap_type,
				unsigned int revmap_arg,
				struct irq_domain_ops *ops,
				irq_hw_number_t inval_irq)
{
	struct irq_domain *domain, *h;
	unsigned int size = sizeof(struct irq_domain);
	unsigned int i;
	unsigned int *rmap;

	/* Allocate structure and revmap table if using linear mapping */
	if (revmap_type == IRQ_DOMAIN_MAP_LINEAR)
		size += revmap_arg * sizeof(unsigned int);
	domain = kzalloc(size, GFP_KERNEL);
	if (domain == NULL)
		return NULL;

	/* Fill structure */
	domain->revmap_type = revmap_type;
	domain->inval_irq = inval_irq;
	domain->ops = ops;
	domain->of_node = of_node_get(of_node);

	if (domain->ops->match == NULL)
		domain->ops->match = default_irq_domain_match;

	mutex_lock(&irq_domain_mutex);
	/* Make sure only one legacy controller can be created */
	if (revmap_type == IRQ_DOMAIN_MAP_LEGACY) {
		list_for_each_entry(h, &irq_domain_list, link) {
			if (WARN_ON(h->revmap_type == IRQ_DOMAIN_MAP_LEGACY)) {
				mutex_unlock(&irq_domain_mutex);
				of_node_put(domain->of_node);
				kfree(domain);
				return NULL;
			}
		}
	}
	list_add(&domain->link, &irq_domain_list);
	mutex_unlock(&irq_domain_mutex);

	/* Additional setups per revmap type */
	switch(revmap_type) {
	case IRQ_DOMAIN_MAP_LEGACY:
		/* 0 is always the invalid number for legacy */
		domain->inval_irq = 0;
		/* setup us as the domain for all legacy interrupts */
		for (i = 1; i < NUM_ISA_INTERRUPTS; i++) {
			struct irq_data *irq_data = irq_get_irq_data(i);
			irq_data->hwirq = i;
			irq_data->domain = domain;

			/* Legacy flags are left to default at this point,
			 * one can then use irq_create_mapping() to
			 * explicitly change them
			 */
			ops->map(domain, i, i);

			/* Clear norequest flags */
			irq_clear_status_flags(i, IRQ_NOREQUEST);
		}
		break;
	case IRQ_DOMAIN_MAP_LINEAR:
		rmap = (unsigned int *)(domain + 1);
		for (i = 0; i < revmap_arg; i++)
			rmap[i] = 0;
		domain->revmap_data.linear.size = revmap_arg;
		domain->revmap_data.linear.revmap = rmap;
		break;
	case IRQ_DOMAIN_MAP_TREE:
		INIT_RADIX_TREE(&domain->revmap_data.tree, GFP_KERNEL);
		break;
	default:
		break;
	}

	pr_debug("irq: Allocated domain of type %d @0x%p\n", revmap_type, domain);

	return domain;
}

/**
 * irq_find_host() - Locates a domain for a given device node
 * @node: device-tree node of the interrupt controller
 */
struct irq_domain *irq_find_host(struct device_node *node)
{
	struct irq_domain *h, *found = NULL;

	/* We might want to match the legacy controller last since
	 * it might potentially be set to match all interrupts in
	 * the absence of a device node. This isn't a problem so far
	 * yet though...
	 */
	mutex_lock(&irq_domain_mutex);
	list_for_each_entry(h, &irq_domain_list, link)
		if (h->ops->match(h, node)) {
			found = h;
			break;
		}
	mutex_unlock(&irq_domain_mutex);
	return found;
}
EXPORT_SYMBOL_GPL(irq_find_host);

/**
 * irq_set_default_host() - Set a "default" irq domain
 * @domain: default domain pointer
 *
 * For convenience, it's possible to set a "default" domain that will be used
 * whenever NULL is passed to irq_create_mapping(). It makes life easier for
 * platforms that want to manipulate a few hard coded interrupt numbers that
 * aren't properly represented in the device-tree.
 */
void irq_set_default_host(struct irq_domain *domain)
{
	pr_debug("irq: Default domain set to @0x%p\n", domain);

	irq_default_domain = domain;
}

/**
 * irq_set_virq_count() - Set the maximum number of linux irqs
 * @count: number of linux irqs, capped with NR_IRQS
 *
 * This is mainly for use by platforms like iSeries who want to program
 * the virtual irq number in the controller to avoid the reverse mapping
 */
void irq_set_virq_count(unsigned int count)
{
	pr_debug("irq: Trying to set virq count to %d\n", count);

	BUG_ON(count < NUM_ISA_INTERRUPTS);
	if (count < NR_IRQS)
		irq_virq_count = count;
}

static int irq_setup_virq(struct irq_domain *domain, unsigned int virq,
			    irq_hw_number_t hwirq)
{
	struct irq_data *irq_data = irq_get_irq_data(virq);

	irq_data->hwirq = hwirq;
	irq_data->domain = domain;
	if (domain->ops->map(domain, virq, hwirq)) {
		pr_debug("irq: -> mapping failed, freeing\n");
		irq_data->domain = NULL;
		irq_data->hwirq = 0;
		return -1;
	}

	irq_clear_status_flags(virq, IRQ_NOREQUEST);

	return 0;
}

/**
 * irq_create_direct_mapping() - Allocate an irq for direct mapping
 * @domain: domain to allocate the irq for or NULL for default domain
 *
 * This routine is used for irq controllers which can choose the hardware
 * interrupt numbers they generate. In such a case it's simplest to use
 * the linux irq as the hardware interrupt number.
 */
unsigned int irq_create_direct_mapping(struct irq_domain *domain)
{
	unsigned int virq;

	if (domain == NULL)
		domain = irq_default_domain;

	BUG_ON(domain == NULL);
	WARN_ON(domain->revmap_type != IRQ_DOMAIN_MAP_NOMAP);

	virq = irq_alloc_desc_from(1, 0);
	if (!virq) {
		pr_debug("irq: create_direct virq allocation failed\n");
		return 0;
	}
	if (virq >= irq_virq_count) {
		pr_err("ERROR: no free irqs available below %i maximum\n",
			irq_virq_count);
		irq_free_desc(virq);
		return 0;
	}

	pr_debug("irq: create_direct obtained virq %d\n", virq);

	if (irq_setup_virq(domain, virq, virq)) {
		irq_free_desc(virq);
		return 0;
	}

	return virq;
}

/**
 * irq_create_mapping() - Map a hardware interrupt into linux irq space
 * @domain: domain owning this hardware interrupt or NULL for default domain
 * @hwirq: hardware irq number in that domain space
 *
 * Only one mapping per hardware interrupt is permitted. Returns a linux
 * irq number.
 * If the sense/trigger is to be specified, set_irq_type() should be called
 * on the number returned from that call.
 */
unsigned int irq_create_mapping(struct irq_domain *domain,
				irq_hw_number_t hwirq)
{
	unsigned int virq, hint;

	pr_debug("irq: irq_create_mapping(0x%p, 0x%lx)\n", domain, hwirq);

	/* Look for default domain if nececssary */
	if (domain == NULL)
		domain = irq_default_domain;
	if (domain == NULL) {
		printk(KERN_WARNING "irq_create_mapping called for"
		       " NULL domain, hwirq=%lx\n", hwirq);
		WARN_ON(1);
		return 0;
	}
	pr_debug("irq: -> using domain @%p\n", domain);

	/* Check if mapping already exists */
	virq = irq_find_mapping(domain, hwirq);
	if (virq) {
		pr_debug("irq: -> existing mapping on virq %d\n", virq);
		return virq;
	}

	/* Get a virtual interrupt number */
	if (domain->revmap_type == IRQ_DOMAIN_MAP_LEGACY) {
		/* Handle legacy */
		virq = (unsigned int)hwirq;
		if (virq == 0 || virq >= NUM_ISA_INTERRUPTS)
			return 0;
		return virq;
	} else {
		/* Allocate a virtual interrupt number */
		hint = hwirq % irq_virq_count;
		if (hint == 0)
			hint++;
		virq = irq_alloc_desc_from(hint, 0);
		if (!virq)
			virq = irq_alloc_desc_from(1, 0);
		if (!virq) {
			pr_debug("irq: -> virq allocation failed\n");
			return 0;
		}
	}

	if (irq_setup_virq(domain, virq, hwirq)) {
		if (domain->revmap_type != IRQ_DOMAIN_MAP_LEGACY)
			irq_free_desc(virq);
		return 0;
	}

	pr_debug("irq: irq %lu on domain %s mapped to virtual irq %u\n",
		hwirq, domain->of_node ? domain->of_node->full_name : "null", virq);

	return virq;
}
EXPORT_SYMBOL_GPL(irq_create_mapping);

unsigned int irq_create_of_mapping(struct device_node *controller,
				   const u32 *intspec, unsigned int intsize)
{
	struct irq_domain *domain;
	irq_hw_number_t hwirq;
	unsigned int type = IRQ_TYPE_NONE;
	unsigned int virq;

	domain = controller ? irq_find_host(controller) : irq_default_domain;
	if (!domain) {
		printk(KERN_WARNING "irq: no irq domain found for %s !\n",
		       controller->full_name);
		return 0;
	}

	/* If domain has no translation, then we assume interrupt line */
	if (domain->ops->xlate == NULL)
		hwirq = intspec[0];
	else {
		if (domain->ops->xlate(domain, controller, intspec, intsize,
				     &hwirq, &type))
			return 0;
	}

	/* Create mapping */
	virq = irq_create_mapping(domain, hwirq);
	if (!virq)
		return virq;

	/* Set type if specified and different than the current one */
	if (type != IRQ_TYPE_NONE &&
	    type != (irqd_get_trigger_type(irq_get_irq_data(virq))))
		irq_set_irq_type(virq, type);
	return virq;
}
EXPORT_SYMBOL_GPL(irq_create_of_mapping);

/**
 * irq_dispose_mapping() - Unmap an interrupt
 * @virq: linux irq number of the interrupt to unmap
 */
void irq_dispose_mapping(unsigned int virq)
{
	struct irq_data *irq_data = irq_get_irq_data(virq);
	struct irq_domain *domain;
	irq_hw_number_t hwirq;

	if (!virq || !irq_data)
		return;

	domain = irq_data->domain;
	if (WARN_ON(domain == NULL))
		return;

	/* Never unmap legacy interrupts */
	if (domain->revmap_type == IRQ_DOMAIN_MAP_LEGACY)
		return;

	irq_set_status_flags(virq, IRQ_NOREQUEST);

	/* remove chip and handler */
	irq_set_chip_and_handler(virq, NULL, NULL);

	/* Make sure it's completed */
	synchronize_irq(virq);

	/* Tell the PIC about it */
	if (domain->ops->unmap)
		domain->ops->unmap(domain, virq);
	smp_mb();

	/* Clear reverse map */
	hwirq = irq_data->hwirq;
	switch(domain->revmap_type) {
	case IRQ_DOMAIN_MAP_LINEAR:
		if (hwirq < domain->revmap_data.linear.size)
			domain->revmap_data.linear.revmap[hwirq] = 0;
		break;
	case IRQ_DOMAIN_MAP_TREE:
		mutex_lock(&revmap_trees_mutex);
		radix_tree_delete(&domain->revmap_data.tree, hwirq);
		mutex_unlock(&revmap_trees_mutex);
		break;
	}

	/* Destroy map */
	irq_data->hwirq = domain->inval_irq;

	irq_free_desc(virq);
}
EXPORT_SYMBOL_GPL(irq_dispose_mapping);

/**
 * irq_find_mapping() - Find a linux irq from an hw irq number.
 * @domain: domain owning this hardware interrupt
 * @hwirq: hardware irq number in that domain space
 *
 * This is a slow path, for use by generic code. It's expected that an
 * irq controller implementation directly calls the appropriate low level
 * mapping function.
 */
unsigned int irq_find_mapping(struct irq_domain *domain,
			      irq_hw_number_t hwirq)
{
	unsigned int i;
	unsigned int hint = hwirq % irq_virq_count;

	/* Look for default domain if nececssary */
	if (domain == NULL)
		domain = irq_default_domain;
	if (domain == NULL)
		return 0;

	/* legacy -> bail early */
	if (domain->revmap_type == IRQ_DOMAIN_MAP_LEGACY)
		return hwirq;

	/* Slow path does a linear search of the map */
	if (hint == 0)
		hint = 1;
	i = hint;
	do {
		struct irq_data *data = irq_get_irq_data(i);
		if (data && (data->domain == domain) && (data->hwirq == hwirq))
			return i;
		i++;
		if (i >= irq_virq_count)
			i = 1;
	} while(i != hint);
	return 0;
}
EXPORT_SYMBOL_GPL(irq_find_mapping);

/**
 * irq_radix_revmap_lookup() - Find a linux irq from a hw irq number.
 * @domain: domain owning this hardware interrupt
 * @hwirq: hardware irq number in that domain space
 *
 * This is a fast path, for use by irq controller code that uses radix tree
 * revmaps
 */
unsigned int irq_radix_revmap_lookup(struct irq_domain *domain,
				     irq_hw_number_t hwirq)
{
	struct irq_data *irq_data;

	if (WARN_ON_ONCE(domain->revmap_type != IRQ_DOMAIN_MAP_TREE))
		return irq_find_mapping(domain, hwirq);

	/*
	 * Freeing an irq can delete nodes along the path to
	 * do the lookup via call_rcu.
	 */
	rcu_read_lock();
	irq_data = radix_tree_lookup(&domain->revmap_data.tree, hwirq);
	rcu_read_unlock();

	/*
	 * If found in radix tree, then fine.
	 * Else fallback to linear lookup - this should not happen in practice
	 * as it means that we failed to insert the node in the radix tree.
	 */
	return irq_data ? irq_data->irq : irq_find_mapping(domain, hwirq);
}

/**
 * irq_radix_revmap_insert() - Insert a hw irq to linux irq number mapping.
 * @domain: domain owning this hardware interrupt
 * @virq: linux irq number
 * @hwirq: hardware irq number in that domain space
 *
 * This is for use by irq controllers that use a radix tree reverse
 * mapping for fast lookup.
 */
void irq_radix_revmap_insert(struct irq_domain *domain, unsigned int virq,
			     irq_hw_number_t hwirq)
{
	struct irq_data *irq_data = irq_get_irq_data(virq);

	if (WARN_ON(domain->revmap_type != IRQ_DOMAIN_MAP_TREE))
		return;

	if (virq) {
		mutex_lock(&revmap_trees_mutex);
		radix_tree_insert(&domain->revmap_data.tree, hwirq, irq_data);
		mutex_unlock(&revmap_trees_mutex);
	}
}

/**
 * irq_linear_revmap() - Find a linux irq from a hw irq number.
 * @domain: domain owning this hardware interrupt
 * @hwirq: hardware irq number in that domain space
 *
 * This is a fast path, for use by irq controller code that uses linear
 * revmaps. It does fallback to the slow path if the revmap doesn't exist
 * yet and will create the revmap entry with appropriate locking
 */
unsigned int irq_linear_revmap(struct irq_domain *domain,
			       irq_hw_number_t hwirq)
{
	unsigned int *revmap;

	if (WARN_ON_ONCE(domain->revmap_type != IRQ_DOMAIN_MAP_LINEAR))
		return irq_find_mapping(domain, hwirq);

	/* Check revmap bounds */
	if (unlikely(hwirq >= domain->revmap_data.linear.size))
		return irq_find_mapping(domain, hwirq);

	/* Check if revmap was allocated */
	revmap = domain->revmap_data.linear.revmap;
	if (unlikely(revmap == NULL))
		return irq_find_mapping(domain, hwirq);

	/* Fill up revmap with slow path if no mapping found */
	if (unlikely(!revmap[hwirq]))
		revmap[hwirq] = irq_find_mapping(domain, hwirq);

	return revmap[hwirq];
}

#ifdef CONFIG_VIRQ_DEBUG
static int virq_debug_show(struct seq_file *m, void *private)
{
	unsigned long flags;
	struct irq_desc *desc;
	const char *p;
	static const char none[] = "none";
	void *data;
	int i;

	seq_printf(m, "%-5s  %-7s  %-15s  %-18s  %s\n", "virq", "hwirq",
		      "chip name", "chip data", "domain name");

	for (i = 1; i < nr_irqs; i++) {
		desc = irq_to_desc(i);
		if (!desc)
			continue;

		raw_spin_lock_irqsave(&desc->lock, flags);

		if (desc->action && desc->action->handler) {
			struct irq_chip *chip;

			seq_printf(m, "%5d  ", i);
			seq_printf(m, "0x%05lx  ", desc->irq_data.hwirq);

			chip = irq_desc_get_chip(desc);
			if (chip && chip->name)
				p = chip->name;
			else
				p = none;
			seq_printf(m, "%-15s  ", p);

			data = irq_desc_get_chip_data(desc);
			seq_printf(m, "0x%16p  ", data);

			if (desc->irq_data.domain->of_node)
				p = desc->irq_data.domain->of_node->full_name;
			else
				p = none;
			seq_printf(m, "%s\n", p);
		}

		raw_spin_unlock_irqrestore(&desc->lock, flags);
	}

	return 0;
}

static int virq_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, virq_debug_show, inode->i_private);
}

static const struct file_operations virq_debug_fops = {
	.open = virq_debug_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int __init irq_debugfs_init(void)
{
	if (debugfs_create_file("virq_mapping", S_IRUGO, powerpc_debugfs_root,
				 NULL, &virq_debug_fops) == NULL)
		return -ENOMEM;

	return 0;
}
__initcall(irq_debugfs_init);
#endif /* CONFIG_VIRQ_DEBUG */

#else /* CONFIG_PPC */

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
	list_add(&domain->link, &irq_domain_list);
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
	list_del(&domain->link);
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
	list_for_each_entry(domain, &irq_domain_list, link) {
		if (!domain->ops->xlate)
			continue;
		rc = domain->ops->xlate(domain, controller,
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

int irq_domain_simple_xlate(struct irq_domain *d,
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
	pr_debug("looking for phys_base=%llx, irq_start=%i\n",
		(unsigned long long) phys_base, (int) irq_start);
	node = of_find_matching_node_by_address(NULL, match, phys_base);
	if (node)
		irq_domain_add_simple(node, irq_start);
}
EXPORT_SYMBOL_GPL(irq_domain_generate_simple);
#endif /* CONFIG_OF_IRQ */

struct irq_domain_ops irq_domain_simple_ops = {
#ifdef CONFIG_OF_IRQ
	.xlate = irq_domain_simple_xlate,
#endif /* CONFIG_OF_IRQ */
};
EXPORT_SYMBOL_GPL(irq_domain_simple_ops);

#endif /* !CONFIG_PPC */
