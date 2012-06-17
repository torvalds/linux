#define pr_fmt(fmt)  "irq: " fmt

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
#include <linux/topology.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/fs.h>

#define IRQ_DOMAIN_MAP_LEGACY 0 /* driver allocated fixed range of irqs.
				 * ie. legacy 8259, gets irqs 1..15 */
#define IRQ_DOMAIN_MAP_NOMAP 1 /* no fast reverse mapping */
#define IRQ_DOMAIN_MAP_LINEAR 2 /* linear map of interrupts */
#define IRQ_DOMAIN_MAP_TREE 3 /* radix tree */

static LIST_HEAD(irq_domain_list);
static DEFINE_MUTEX(irq_domain_mutex);

static DEFINE_MUTEX(revmap_trees_mutex);
static struct irq_domain *irq_default_domain;

/**
 * irq_domain_alloc() - Allocate a new irq_domain data structure
 * @of_node: optional device-tree node of the interrupt controller
 * @revmap_type: type of reverse mapping to use
 * @ops: map/unmap domain callbacks
 * @host_data: Controller private data pointer
 *
 * Allocates and initialize and irq_domain structure.  Caller is expected to
 * register allocated irq_domain with irq_domain_register().  Returns pointer
 * to IRQ domain, or NULL on failure.
 */
static struct irq_domain *irq_domain_alloc(struct device_node *of_node,
					   unsigned int revmap_type,
					   const struct irq_domain_ops *ops,
					   void *host_data)
{
	struct irq_domain *domain;

	domain = kzalloc_node(sizeof(*domain), GFP_KERNEL,
			      of_node_to_nid(of_node));
	if (WARN_ON(!domain))
		return NULL;

	/* Fill structure */
	domain->revmap_type = revmap_type;
	domain->ops = ops;
	domain->host_data = host_data;
	domain->of_node = of_node_get(of_node);

	return domain;
}

static void irq_domain_free(struct irq_domain *domain)
{
	of_node_put(domain->of_node);
	kfree(domain);
}

static void irq_domain_add(struct irq_domain *domain)
{
	mutex_lock(&irq_domain_mutex);
	list_add(&domain->link, &irq_domain_list);
	mutex_unlock(&irq_domain_mutex);
	pr_debug("Allocated domain of type %d @0x%p\n",
		 domain->revmap_type, domain);
}

/**
 * irq_domain_remove() - Remove an irq domain.
 * @domain: domain to remove
 *
 * This routine is used to remove an irq domain. The caller must ensure
 * that all mappings within the domain have been disposed of prior to
 * use, depending on the revmap type.
 */
void irq_domain_remove(struct irq_domain *domain)
{
	mutex_lock(&irq_domain_mutex);

	switch (domain->revmap_type) {
	case IRQ_DOMAIN_MAP_LEGACY:
		/*
		 * Legacy domains don't manage their own irq_desc
		 * allocations, we expect the caller to handle irq_desc
		 * freeing on their own.
		 */
		break;
	case IRQ_DOMAIN_MAP_TREE:
		/*
		 * radix_tree_delete() takes care of destroying the root
		 * node when all entries are removed. Shout if there are
		 * any mappings left.
		 */
		WARN_ON(domain->revmap_data.tree.height);
		break;
	case IRQ_DOMAIN_MAP_LINEAR:
		kfree(domain->revmap_data.linear.revmap);
		domain->revmap_data.linear.size = 0;
		break;
	case IRQ_DOMAIN_MAP_NOMAP:
		break;
	}

	list_del(&domain->link);

	/*
	 * If the going away domain is the default one, reset it.
	 */
	if (unlikely(irq_default_domain == domain))
		irq_set_default_host(NULL);

	mutex_unlock(&irq_domain_mutex);

	pr_debug("Removed domain of type %d @0x%p\n",
		 domain->revmap_type, domain);

	irq_domain_free(domain);
}
EXPORT_SYMBOL_GPL(irq_domain_remove);

static unsigned int irq_domain_legacy_revmap(struct irq_domain *domain,
					     irq_hw_number_t hwirq)
{
	irq_hw_number_t first_hwirq = domain->revmap_data.legacy.first_hwirq;
	int size = domain->revmap_data.legacy.size;

	if (WARN_ON(hwirq < first_hwirq || hwirq >= first_hwirq + size))
		return 0;
	return hwirq - first_hwirq + domain->revmap_data.legacy.first_irq;
}

/**
 * irq_domain_add_simple() - Allocate and register a simple irq_domain.
 * @of_node: pointer to interrupt controller's device tree node.
 * @size: total number of irqs in mapping
 * @first_irq: first number of irq block assigned to the domain
 * @ops: map/unmap domain callbacks
 * @host_data: Controller private data pointer
 *
 * Allocates a legacy irq_domain if irq_base is positive or a linear
 * domain otherwise.
 *
 * This is intended to implement the expected behaviour for most
 * interrupt controllers which is that a linear mapping should
 * normally be used unless the system requires a legacy mapping in
 * order to support supplying interrupt numbers during non-DT
 * registration of devices.
 */
struct irq_domain *irq_domain_add_simple(struct device_node *of_node,
					 unsigned int size,
					 unsigned int first_irq,
					 const struct irq_domain_ops *ops,
					 void *host_data)
{
	if (first_irq > 0)
		return irq_domain_add_legacy(of_node, size, first_irq, 0,
					     ops, host_data);
	else
		return irq_domain_add_linear(of_node, size, ops, host_data);
}

/**
 * irq_domain_add_legacy() - Allocate and register a legacy revmap irq_domain.
 * @of_node: pointer to interrupt controller's device tree node.
 * @size: total number of irqs in legacy mapping
 * @first_irq: first number of irq block assigned to the domain
 * @first_hwirq: first hwirq number to use for the translation. Should normally
 *               be '0', but a positive integer can be used if the effective
 *               hwirqs numbering does not begin at zero.
 * @ops: map/unmap domain callbacks
 * @host_data: Controller private data pointer
 *
 * Note: the map() callback will be called before this function returns
 * for all legacy interrupts except 0 (which is always the invalid irq for
 * a legacy controller).
 */
struct irq_domain *irq_domain_add_legacy(struct device_node *of_node,
					 unsigned int size,
					 unsigned int first_irq,
					 irq_hw_number_t first_hwirq,
					 const struct irq_domain_ops *ops,
					 void *host_data)
{
	struct irq_domain *domain;
	unsigned int i;

	domain = irq_domain_alloc(of_node, IRQ_DOMAIN_MAP_LEGACY, ops, host_data);
	if (!domain)
		return NULL;

	domain->revmap_data.legacy.first_irq = first_irq;
	domain->revmap_data.legacy.first_hwirq = first_hwirq;
	domain->revmap_data.legacy.size = size;

	mutex_lock(&irq_domain_mutex);
	/* Verify that all the irqs are available */
	for (i = 0; i < size; i++) {
		int irq = first_irq + i;
		struct irq_data *irq_data = irq_get_irq_data(irq);

		if (WARN_ON(!irq_data || irq_data->domain)) {
			mutex_unlock(&irq_domain_mutex);
			irq_domain_free(domain);
			return NULL;
		}
	}

	/* Claim all of the irqs before registering a legacy domain */
	for (i = 0; i < size; i++) {
		struct irq_data *irq_data = irq_get_irq_data(first_irq + i);
		irq_data->hwirq = first_hwirq + i;
		irq_data->domain = domain;
	}
	mutex_unlock(&irq_domain_mutex);

	for (i = 0; i < size; i++) {
		int irq = first_irq + i;
		int hwirq = first_hwirq + i;

		/* IRQ0 gets ignored */
		if (!irq)
			continue;

		/* Legacy flags are left to default at this point,
		 * one can then use irq_create_mapping() to
		 * explicitly change them
		 */
		if (ops->map)
			ops->map(domain, irq, hwirq);

		/* Clear norequest flags */
		irq_clear_status_flags(irq, IRQ_NOREQUEST);
	}

	irq_domain_add(domain);
	return domain;
}
EXPORT_SYMBOL_GPL(irq_domain_add_legacy);

/**
 * irq_domain_add_linear() - Allocate and register a linear revmap irq_domain.
 * @of_node: pointer to interrupt controller's device tree node.
 * @size: Number of interrupts in the domain.
 * @ops: map/unmap domain callbacks
 * @host_data: Controller private data pointer
 */
struct irq_domain *irq_domain_add_linear(struct device_node *of_node,
					 unsigned int size,
					 const struct irq_domain_ops *ops,
					 void *host_data)
{
	struct irq_domain *domain;
	unsigned int *revmap;

	revmap = kzalloc_node(sizeof(*revmap) * size, GFP_KERNEL,
			      of_node_to_nid(of_node));
	if (WARN_ON(!revmap))
		return NULL;

	domain = irq_domain_alloc(of_node, IRQ_DOMAIN_MAP_LINEAR, ops, host_data);
	if (!domain) {
		kfree(revmap);
		return NULL;
	}
	domain->revmap_data.linear.size = size;
	domain->revmap_data.linear.revmap = revmap;
	irq_domain_add(domain);
	return domain;
}
EXPORT_SYMBOL_GPL(irq_domain_add_linear);

struct irq_domain *irq_domain_add_nomap(struct device_node *of_node,
					 unsigned int max_irq,
					 const struct irq_domain_ops *ops,
					 void *host_data)
{
	struct irq_domain *domain = irq_domain_alloc(of_node,
					IRQ_DOMAIN_MAP_NOMAP, ops, host_data);
	if (domain) {
		domain->revmap_data.nomap.max_irq = max_irq ? max_irq : ~0;
		irq_domain_add(domain);
	}
	return domain;
}
EXPORT_SYMBOL_GPL(irq_domain_add_nomap);

/**
 * irq_domain_add_tree()
 * @of_node: pointer to interrupt controller's device tree node.
 * @ops: map/unmap domain callbacks
 *
 * Note: The radix tree will be allocated later during boot automatically
 * (the reverse mapping will use the slow path until that happens).
 */
struct irq_domain *irq_domain_add_tree(struct device_node *of_node,
					 const struct irq_domain_ops *ops,
					 void *host_data)
{
	struct irq_domain *domain = irq_domain_alloc(of_node,
					IRQ_DOMAIN_MAP_TREE, ops, host_data);
	if (domain) {
		INIT_RADIX_TREE(&domain->revmap_data.tree, GFP_KERNEL);
		irq_domain_add(domain);
	}
	return domain;
}
EXPORT_SYMBOL_GPL(irq_domain_add_tree);

/**
 * irq_find_host() - Locates a domain for a given device node
 * @node: device-tree node of the interrupt controller
 */
struct irq_domain *irq_find_host(struct device_node *node)
{
	struct irq_domain *h, *found = NULL;
	int rc;

	/* We might want to match the legacy controller last since
	 * it might potentially be set to match all interrupts in
	 * the absence of a device node. This isn't a problem so far
	 * yet though...
	 */
	mutex_lock(&irq_domain_mutex);
	list_for_each_entry(h, &irq_domain_list, link) {
		if (h->ops->match)
			rc = h->ops->match(h, node);
		else
			rc = (h->of_node != NULL) && (h->of_node == node);

		if (rc) {
			found = h;
			break;
		}
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
	pr_debug("Default domain set to @0x%p\n", domain);

	irq_default_domain = domain;
}
EXPORT_SYMBOL_GPL(irq_set_default_host);

static void irq_domain_disassociate_many(struct irq_domain *domain,
					 unsigned int irq_base, int count)
{
	/*
	 * disassociate in reverse order;
	 * not strictly necessary, but nice for unwinding
	 */
	while (count--) {
		int irq = irq_base + count;
		struct irq_data *irq_data = irq_get_irq_data(irq);
		irq_hw_number_t hwirq = irq_data->hwirq;

		if (WARN_ON(!irq_data || irq_data->domain != domain))
			continue;

		irq_set_status_flags(irq, IRQ_NOREQUEST);

		/* remove chip and handler */
		irq_set_chip_and_handler(irq, NULL, NULL);

		/* Make sure it's completed */
		synchronize_irq(irq);

		/* Tell the PIC about it */
		if (domain->ops->unmap)
			domain->ops->unmap(domain, irq);
		smp_mb();

		irq_data->domain = NULL;
		irq_data->hwirq = 0;

		/* Clear reverse map */
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
	}
}

int irq_domain_associate_many(struct irq_domain *domain, unsigned int irq_base,
			      irq_hw_number_t hwirq_base, int count)
{
	unsigned int virq = irq_base;
	irq_hw_number_t hwirq = hwirq_base;
	int i;

	pr_debug("%s(%s, irqbase=%i, hwbase=%i, count=%i)\n", __func__,
		of_node_full_name(domain->of_node), irq_base, (int)hwirq_base, count);

	for (i = 0; i < count; i++) {
		struct irq_data *irq_data = irq_get_irq_data(virq + i);

		if (WARN(!irq_data, "error: irq_desc not allocated; "
			 "irq=%i hwirq=0x%x\n", virq + i, (int)hwirq + i))
			return -EINVAL;
		if (WARN(irq_data->domain, "error: irq_desc already associated; "
			 "irq=%i hwirq=0x%x\n", virq + i, (int)hwirq + i))
			return -EINVAL;
	};

	for (i = 0; i < count; i++, virq++, hwirq++) {
		struct irq_data *irq_data = irq_get_irq_data(virq);

		irq_data->hwirq = hwirq;
		irq_data->domain = domain;
		if (domain->ops->map && domain->ops->map(domain, virq, hwirq)) {
			pr_err("irq-%i==>hwirq-0x%lx mapping failed\n", virq, hwirq);
			irq_data->domain = NULL;
			irq_data->hwirq = 0;
			goto err_unmap;
		}

		switch (domain->revmap_type) {
		case IRQ_DOMAIN_MAP_LINEAR:
			if (hwirq < domain->revmap_data.linear.size)
				domain->revmap_data.linear.revmap[hwirq] = virq;
			break;
		case IRQ_DOMAIN_MAP_TREE:
			mutex_lock(&revmap_trees_mutex);
			irq_radix_revmap_insert(domain, virq, hwirq);
			mutex_unlock(&revmap_trees_mutex);
			break;
		}

		irq_clear_status_flags(virq, IRQ_NOREQUEST);
	}

	return 0;

 err_unmap:
	irq_domain_disassociate_many(domain, irq_base, i);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(irq_domain_associate_many);

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

	virq = irq_alloc_desc_from(1, of_node_to_nid(domain->of_node));
	if (!virq) {
		pr_debug("create_direct virq allocation failed\n");
		return 0;
	}
	if (virq >= domain->revmap_data.nomap.max_irq) {
		pr_err("ERROR: no free irqs available below %i maximum\n",
			domain->revmap_data.nomap.max_irq);
		irq_free_desc(virq);
		return 0;
	}
	pr_debug("create_direct obtained virq %d\n", virq);

	if (irq_domain_associate(domain, virq, virq)) {
		irq_free_desc(virq);
		return 0;
	}

	return virq;
}
EXPORT_SYMBOL_GPL(irq_create_direct_mapping);

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
	unsigned int hint;
	int virq;

	pr_debug("irq_create_mapping(0x%p, 0x%lx)\n", domain, hwirq);

	/* Look for default domain if nececssary */
	if (domain == NULL)
		domain = irq_default_domain;
	if (domain == NULL) {
		pr_warning("irq_create_mapping called for"
			   " NULL domain, hwirq=%lx\n", hwirq);
		WARN_ON(1);
		return 0;
	}
	pr_debug("-> using domain @%p\n", domain);

	/* Check if mapping already exists */
	virq = irq_find_mapping(domain, hwirq);
	if (virq) {
		pr_debug("-> existing mapping on virq %d\n", virq);
		return virq;
	}

	/* Get a virtual interrupt number */
	if (domain->revmap_type == IRQ_DOMAIN_MAP_LEGACY)
		return irq_domain_legacy_revmap(domain, hwirq);

	/* Allocate a virtual interrupt number */
	hint = hwirq % nr_irqs;
	if (hint == 0)
		hint++;
	virq = irq_alloc_desc_from(hint, of_node_to_nid(domain->of_node));
	if (virq <= 0)
		virq = irq_alloc_desc_from(1, of_node_to_nid(domain->of_node));
	if (virq <= 0) {
		pr_debug("-> virq allocation failed\n");
		return 0;
	}

	if (irq_domain_associate(domain, virq, hwirq)) {
		irq_free_desc(virq);
		return 0;
	}

	pr_debug("irq %lu on domain %s mapped to virtual irq %u\n",
		hwirq, of_node_full_name(domain->of_node), virq);

	return virq;
}
EXPORT_SYMBOL_GPL(irq_create_mapping);

/**
 * irq_create_strict_mappings() - Map a range of hw irqs to fixed linux irqs
 * @domain: domain owning the interrupt range
 * @irq_base: beginning of linux IRQ range
 * @hwirq_base: beginning of hardware IRQ range
 * @count: Number of interrupts to map
 *
 * This routine is used for allocating and mapping a range of hardware
 * irqs to linux irqs where the linux irq numbers are at pre-defined
 * locations. For use by controllers that already have static mappings
 * to insert in to the domain.
 *
 * Non-linear users can use irq_create_identity_mapping() for IRQ-at-a-time
 * domain insertion.
 *
 * 0 is returned upon success, while any failure to establish a static
 * mapping is treated as an error.
 */
int irq_create_strict_mappings(struct irq_domain *domain, unsigned int irq_base,
			       irq_hw_number_t hwirq_base, int count)
{
	int ret;

	ret = irq_alloc_descs(irq_base, irq_base, count,
			      of_node_to_nid(domain->of_node));
	if (unlikely(ret < 0))
		return ret;

	ret = irq_domain_associate_many(domain, irq_base, hwirq_base, count);
	if (unlikely(ret < 0)) {
		irq_free_descs(irq_base, count);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(irq_create_strict_mappings);

unsigned int irq_create_of_mapping(struct device_node *controller,
				   const u32 *intspec, unsigned int intsize)
{
	struct irq_domain *domain;
	irq_hw_number_t hwirq;
	unsigned int type = IRQ_TYPE_NONE;
	unsigned int virq;

	domain = controller ? irq_find_host(controller) : irq_default_domain;
	if (!domain) {
#ifdef CONFIG_MIPS
		/*
		 * Workaround to avoid breaking interrupt controller drivers
		 * that don't yet register an irq_domain.  This is temporary
		 * code. ~~~gcl, Feb 24, 2012
		 *
		 * Scheduled for removal in Linux v3.6.  That should be enough
		 * time.
		 */
		if (intsize > 0)
			return intspec[0];
#endif
		pr_warning("no irq domain found for %s !\n",
			   of_node_full_name(controller));
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

	if (!virq || !irq_data)
		return;

	domain = irq_data->domain;
	if (WARN_ON(domain == NULL))
		return;

	/* Never unmap legacy interrupts */
	if (domain->revmap_type == IRQ_DOMAIN_MAP_LEGACY)
		return;

	irq_domain_disassociate_many(domain, virq, 1);
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
	unsigned int hint = hwirq % nr_irqs;

	/* Look for default domain if nececssary */
	if (domain == NULL)
		domain = irq_default_domain;
	if (domain == NULL)
		return 0;

	/* legacy -> bail early */
	if (domain->revmap_type == IRQ_DOMAIN_MAP_LEGACY)
		return irq_domain_legacy_revmap(domain, hwirq);

	/* Slow path does a linear search of the map */
	if (hint == 0)
		hint = 1;
	i = hint;
	do {
		struct irq_data *data = irq_get_irq_data(i);
		if (data && (data->domain == domain) && (data->hwirq == hwirq))
			return i;
		i++;
		if (i >= nr_irqs)
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
EXPORT_SYMBOL_GPL(irq_radix_revmap_lookup);

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
EXPORT_SYMBOL_GPL(irq_radix_revmap_insert);

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
EXPORT_SYMBOL_GPL(irq_linear_revmap);

#ifdef CONFIG_IRQ_DOMAIN_DEBUG
static int virq_debug_show(struct seq_file *m, void *private)
{
	unsigned long flags;
	struct irq_desc *desc;
	const char *p;
	static const char none[] = "none";
	void *data;
	int i;

	seq_printf(m, "%-5s  %-7s  %-15s  %-*s  %s\n", "irq", "hwirq",
		      "chip name", (int)(2 * sizeof(void *) + 2), "chip data",
		      "domain name");

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
			seq_printf(m, data ? "0x%p  " : "  %p  ", data);

			if (desc->irq_data.domain)
				p = of_node_full_name(desc->irq_data.domain->of_node);
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
	if (debugfs_create_file("irq_domain_mapping", S_IRUGO, NULL,
				 NULL, &virq_debug_fops) == NULL)
		return -ENOMEM;

	return 0;
}
__initcall(irq_debugfs_init);
#endif /* CONFIG_IRQ_DOMAIN_DEBUG */

/**
 * irq_domain_xlate_onecell() - Generic xlate for direct one cell bindings
 *
 * Device Tree IRQ specifier translation function which works with one cell
 * bindings where the cell value maps directly to the hwirq number.
 */
int irq_domain_xlate_onecell(struct irq_domain *d, struct device_node *ctrlr,
			     const u32 *intspec, unsigned int intsize,
			     unsigned long *out_hwirq, unsigned int *out_type)
{
	if (WARN_ON(intsize < 1))
		return -EINVAL;
	*out_hwirq = intspec[0];
	*out_type = IRQ_TYPE_NONE;
	return 0;
}
EXPORT_SYMBOL_GPL(irq_domain_xlate_onecell);

/**
 * irq_domain_xlate_twocell() - Generic xlate for direct two cell bindings
 *
 * Device Tree IRQ specifier translation function which works with two cell
 * bindings where the cell values map directly to the hwirq number
 * and linux irq flags.
 */
int irq_domain_xlate_twocell(struct irq_domain *d, struct device_node *ctrlr,
			const u32 *intspec, unsigned int intsize,
			irq_hw_number_t *out_hwirq, unsigned int *out_type)
{
	if (WARN_ON(intsize < 2))
		return -EINVAL;
	*out_hwirq = intspec[0];
	*out_type = intspec[1] & IRQ_TYPE_SENSE_MASK;
	return 0;
}
EXPORT_SYMBOL_GPL(irq_domain_xlate_twocell);

/**
 * irq_domain_xlate_onetwocell() - Generic xlate for one or two cell bindings
 *
 * Device Tree IRQ specifier translation function which works with either one
 * or two cell bindings where the cell values map directly to the hwirq number
 * and linux irq flags.
 *
 * Note: don't use this function unless your interrupt controller explicitly
 * supports both one and two cell bindings.  For the majority of controllers
 * the _onecell() or _twocell() variants above should be used.
 */
int irq_domain_xlate_onetwocell(struct irq_domain *d,
				struct device_node *ctrlr,
				const u32 *intspec, unsigned int intsize,
				unsigned long *out_hwirq, unsigned int *out_type)
{
	if (WARN_ON(intsize < 1))
		return -EINVAL;
	*out_hwirq = intspec[0];
	*out_type = (intsize > 1) ? intspec[1] : IRQ_TYPE_NONE;
	return 0;
}
EXPORT_SYMBOL_GPL(irq_domain_xlate_onetwocell);

const struct irq_domain_ops irq_domain_simple_ops = {
	.xlate = irq_domain_xlate_onetwocell,
};
EXPORT_SYMBOL_GPL(irq_domain_simple_ops);

#ifdef CONFIG_OF_IRQ
void irq_domain_generate_simple(const struct of_device_id *match,
				u64 phys_base, unsigned int irq_start)
{
	struct device_node *node;
	pr_debug("looking for phys_base=%llx, irq_start=%i\n",
		(unsigned long long) phys_base, (int) irq_start);
	node = of_find_matching_node_by_address(NULL, match, phys_base);
	if (node)
		irq_domain_add_legacy(node, 32, irq_start, 0,
				      &irq_domain_simple_ops, NULL);
}
EXPORT_SYMBOL_GPL(irq_domain_generate_simple);
#endif
