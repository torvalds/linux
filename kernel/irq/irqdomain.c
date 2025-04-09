// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt)  "irq: " fmt

#include <linux/acpi.h>
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
#include <linux/of_irq.h>
#include <linux/topology.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/fs.h>

static LIST_HEAD(irq_domain_list);
static DEFINE_MUTEX(irq_domain_mutex);

static struct irq_domain *irq_default_domain;

static int irq_domain_alloc_irqs_locked(struct irq_domain *domain, int irq_base,
					unsigned int nr_irqs, int node, void *arg,
					bool realloc, const struct irq_affinity_desc *affinity);
static void irq_domain_check_hierarchy(struct irq_domain *domain);
static void irq_domain_free_one_irq(struct irq_domain *domain, unsigned int virq);

struct irqchip_fwid {
	struct fwnode_handle	fwnode;
	unsigned int		type;
	char			*name;
	phys_addr_t		*pa;
};

#ifdef CONFIG_GENERIC_IRQ_DEBUGFS
static void debugfs_add_domain_dir(struct irq_domain *d);
static void debugfs_remove_domain_dir(struct irq_domain *d);
#else
static inline void debugfs_add_domain_dir(struct irq_domain *d) { }
static inline void debugfs_remove_domain_dir(struct irq_domain *d) { }
#endif

static const char *irqchip_fwnode_get_name(const struct fwnode_handle *fwnode)
{
	struct irqchip_fwid *fwid = container_of(fwnode, struct irqchip_fwid, fwnode);

	return fwid->name;
}

const struct fwnode_operations irqchip_fwnode_ops = {
	.get_name = irqchip_fwnode_get_name,
};
EXPORT_SYMBOL_GPL(irqchip_fwnode_ops);

/**
 * __irq_domain_alloc_fwnode - Allocate a fwnode_handle suitable for
 *                           identifying an irq domain
 * @type:	Type of irqchip_fwnode. See linux/irqdomain.h
 * @id:		Optional user provided id if name != NULL
 * @name:	Optional user provided domain name
 * @pa:		Optional user-provided physical address
 *
 * Allocate a struct irqchip_fwid, and return a pointer to the embedded
 * fwnode_handle (or NULL on failure).
 *
 * Note: The types IRQCHIP_FWNODE_NAMED and IRQCHIP_FWNODE_NAMED_ID are
 * solely to transport name information to irqdomain creation code. The
 * node is not stored. For other types the pointer is kept in the irq
 * domain struct.
 */
struct fwnode_handle *__irq_domain_alloc_fwnode(unsigned int type, int id,
						const char *name,
						phys_addr_t *pa)
{
	struct irqchip_fwid *fwid;
	char *n;

	fwid = kzalloc(sizeof(*fwid), GFP_KERNEL);

	switch (type) {
	case IRQCHIP_FWNODE_NAMED:
		n = kasprintf(GFP_KERNEL, "%s", name);
		break;
	case IRQCHIP_FWNODE_NAMED_ID:
		n = kasprintf(GFP_KERNEL, "%s-%d", name, id);
		break;
	default:
		n = kasprintf(GFP_KERNEL, "irqchip@%pa", pa);
		break;
	}

	if (!fwid || !n) {
		kfree(fwid);
		kfree(n);
		return NULL;
	}

	fwid->type = type;
	fwid->name = n;
	fwid->pa = pa;
	fwnode_init(&fwid->fwnode, &irqchip_fwnode_ops);
	return &fwid->fwnode;
}
EXPORT_SYMBOL_GPL(__irq_domain_alloc_fwnode);

/**
 * irq_domain_free_fwnode - Free a non-OF-backed fwnode_handle
 * @fwnode: fwnode_handle to free
 *
 * Free a fwnode_handle allocated with irq_domain_alloc_fwnode.
 */
void irq_domain_free_fwnode(struct fwnode_handle *fwnode)
{
	struct irqchip_fwid *fwid;

	if (!fwnode || WARN_ON(!is_fwnode_irqchip(fwnode)))
		return;

	fwid = container_of(fwnode, struct irqchip_fwid, fwnode);
	kfree(fwid->name);
	kfree(fwid);
}
EXPORT_SYMBOL_GPL(irq_domain_free_fwnode);

static int alloc_name(struct irq_domain *domain, char *base, enum irq_domain_bus_token bus_token)
{
	if (bus_token == DOMAIN_BUS_ANY)
		domain->name = kasprintf(GFP_KERNEL, "%s", base);
	else
		domain->name = kasprintf(GFP_KERNEL, "%s-%d", base, bus_token);
	if (!domain->name)
		return -ENOMEM;

	domain->flags |= IRQ_DOMAIN_NAME_ALLOCATED;
	return 0;
}

static int alloc_fwnode_name(struct irq_domain *domain, const struct fwnode_handle *fwnode,
			     enum irq_domain_bus_token bus_token, const char *suffix)
{
	const char *sep = suffix ? "-" : "";
	const char *suf = suffix ? : "";
	char *name;

	if (bus_token == DOMAIN_BUS_ANY)
		name = kasprintf(GFP_KERNEL, "%pfw%s%s", fwnode, sep, suf);
	else
		name = kasprintf(GFP_KERNEL, "%pfw%s%s-%d", fwnode, sep, suf, bus_token);
	if (!name)
		return -ENOMEM;

	/*
	 * fwnode paths contain '/', which debugfs is legitimately unhappy
	 * about. Replace them with ':', which does the trick and is not as
	 * offensive as '\'...
	 */
	domain->name = strreplace(name, '/', ':');
	domain->flags |= IRQ_DOMAIN_NAME_ALLOCATED;
	return 0;
}

static int alloc_unknown_name(struct irq_domain *domain, enum irq_domain_bus_token bus_token)
{
	static atomic_t unknown_domains;
	int id = atomic_inc_return(&unknown_domains);

	if (bus_token == DOMAIN_BUS_ANY)
		domain->name = kasprintf(GFP_KERNEL, "unknown-%d", id);
	else
		domain->name = kasprintf(GFP_KERNEL, "unknown-%d-%d", id, bus_token);
	if (!domain->name)
		return -ENOMEM;

	domain->flags |= IRQ_DOMAIN_NAME_ALLOCATED;
	return 0;
}

static int irq_domain_set_name(struct irq_domain *domain, const struct irq_domain_info *info)
{
	enum irq_domain_bus_token bus_token = info->bus_token;
	const struct fwnode_handle *fwnode = info->fwnode;

	if (is_fwnode_irqchip(fwnode)) {
		struct irqchip_fwid *fwid = container_of(fwnode, struct irqchip_fwid, fwnode);

		/*
		 * The name_suffix is only intended to be used to avoid a name
		 * collision when multiple domains are created for a single
		 * device and the name is picked using a real device node.
		 * (Typical use-case is regmap-IRQ controllers for devices
		 * providing more than one physical IRQ.) There should be no
		 * need to use name_suffix with irqchip-fwnode.
		 */
		if (info->name_suffix)
			return -EINVAL;

		switch (fwid->type) {
		case IRQCHIP_FWNODE_NAMED:
		case IRQCHIP_FWNODE_NAMED_ID:
			return alloc_name(domain, fwid->name, bus_token);
		default:
			domain->name = fwid->name;
			if (bus_token != DOMAIN_BUS_ANY)
				return alloc_name(domain, fwid->name, bus_token);
		}

	} else if (is_of_node(fwnode) || is_acpi_device_node(fwnode) || is_software_node(fwnode)) {
		return alloc_fwnode_name(domain, fwnode, bus_token, info->name_suffix);
	}

	if (domain->name)
		return 0;

	if (fwnode)
		pr_err("Invalid fwnode type for irqdomain\n");
	return alloc_unknown_name(domain, bus_token);
}

static struct irq_domain *__irq_domain_create(const struct irq_domain_info *info)
{
	struct irq_domain *domain;
	int err;

	if (WARN_ON((info->size && info->direct_max) ||
		    (!IS_ENABLED(CONFIG_IRQ_DOMAIN_NOMAP) && info->direct_max) ||
		    (info->direct_max && info->direct_max != info->hwirq_max)))
		return ERR_PTR(-EINVAL);

	domain = kzalloc_node(struct_size(domain, revmap, info->size),
			      GFP_KERNEL, of_node_to_nid(to_of_node(info->fwnode)));
	if (!domain)
		return ERR_PTR(-ENOMEM);

	err = irq_domain_set_name(domain, info);
	if (err) {
		kfree(domain);
		return ERR_PTR(err);
	}

	domain->fwnode = fwnode_handle_get(info->fwnode);
	fwnode_dev_initialized(domain->fwnode, true);

	/* Fill structure */
	INIT_RADIX_TREE(&domain->revmap_tree, GFP_KERNEL);
	domain->ops = info->ops;
	domain->host_data = info->host_data;
	domain->bus_token = info->bus_token;
	domain->hwirq_max = info->hwirq_max;

	if (info->direct_max)
		domain->flags |= IRQ_DOMAIN_FLAG_NO_MAP;

	domain->revmap_size = info->size;

	/*
	 * Hierarchical domains use the domain lock of the root domain
	 * (innermost domain).
	 *
	 * For non-hierarchical domains (as for root domains), the root
	 * pointer is set to the domain itself so that &domain->root->mutex
	 * always points to the right lock.
	 */
	mutex_init(&domain->mutex);
	domain->root = domain;

	irq_domain_check_hierarchy(domain);

	return domain;
}

static void __irq_domain_publish(struct irq_domain *domain)
{
	mutex_lock(&irq_domain_mutex);
	debugfs_add_domain_dir(domain);
	list_add(&domain->link, &irq_domain_list);
	mutex_unlock(&irq_domain_mutex);

	pr_debug("Added domain %s\n", domain->name);
}

static void irq_domain_free(struct irq_domain *domain)
{
	fwnode_dev_initialized(domain->fwnode, false);
	fwnode_handle_put(domain->fwnode);
	if (domain->flags & IRQ_DOMAIN_NAME_ALLOCATED)
		kfree(domain->name);
	kfree(domain);
}

static void irq_domain_instantiate_descs(const struct irq_domain_info *info)
{
	if (!IS_ENABLED(CONFIG_SPARSE_IRQ))
		return;

	if (irq_alloc_descs(info->virq_base, info->virq_base, info->size,
			    of_node_to_nid(to_of_node(info->fwnode))) < 0) {
		pr_info("Cannot allocate irq_descs @ IRQ%d, assuming pre-allocated\n",
			info->virq_base);
	}
}

static struct irq_domain *__irq_domain_instantiate(const struct irq_domain_info *info,
						   bool cond_alloc_descs, bool force_associate)
{
	struct irq_domain *domain;
	int err;

	domain = __irq_domain_create(info);
	if (IS_ERR(domain))
		return domain;

	domain->flags |= info->domain_flags;
	domain->exit = info->exit;

#ifdef CONFIG_IRQ_DOMAIN_HIERARCHY
	if (info->parent) {
		domain->root = info->parent->root;
		domain->parent = info->parent;
	}
#endif

	if (info->dgc_info) {
		err = irq_domain_alloc_generic_chips(domain, info->dgc_info);
		if (err)
			goto err_domain_free;
	}

	if (info->init) {
		err = info->init(domain);
		if (err)
			goto err_domain_gc_remove;
	}

	__irq_domain_publish(domain);

	if (cond_alloc_descs && info->virq_base > 0)
		irq_domain_instantiate_descs(info);

	/*
	 * Legacy interrupt domains have a fixed Linux interrupt number
	 * associated. Other interrupt domains can request association by
	 * providing a Linux interrupt number > 0.
	 */
	if (force_associate || info->virq_base > 0) {
		irq_domain_associate_many(domain, info->virq_base, info->hwirq_base,
					  info->size - info->hwirq_base);
	}

	return domain;

err_domain_gc_remove:
	if (info->dgc_info)
		irq_domain_remove_generic_chips(domain);
err_domain_free:
	irq_domain_free(domain);
	return ERR_PTR(err);
}

/**
 * irq_domain_instantiate() - Instantiate a new irq domain data structure
 * @info: Domain information pointer pointing to the information for this domain
 *
 * Return: A pointer to the instantiated irq domain or an ERR_PTR value.
 */
struct irq_domain *irq_domain_instantiate(const struct irq_domain_info *info)
{
	return __irq_domain_instantiate(info, false, false);
}
EXPORT_SYMBOL_GPL(irq_domain_instantiate);

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
	if (domain->exit)
		domain->exit(domain);

	mutex_lock(&irq_domain_mutex);
	debugfs_remove_domain_dir(domain);

	WARN_ON(!radix_tree_empty(&domain->revmap_tree));

	list_del(&domain->link);

	/*
	 * If the going away domain is the default one, reset it.
	 */
	if (unlikely(irq_default_domain == domain))
		irq_set_default_domain(NULL);

	mutex_unlock(&irq_domain_mutex);

	if (domain->flags & IRQ_DOMAIN_FLAG_DESTROY_GC)
		irq_domain_remove_generic_chips(domain);

	pr_debug("Removed domain %s\n", domain->name);
	irq_domain_free(domain);
}
EXPORT_SYMBOL_GPL(irq_domain_remove);

void irq_domain_update_bus_token(struct irq_domain *domain,
				 enum irq_domain_bus_token bus_token)
{
	char *name;

	if (domain->bus_token == bus_token)
		return;

	mutex_lock(&irq_domain_mutex);

	domain->bus_token = bus_token;

	name = kasprintf(GFP_KERNEL, "%s-%d", domain->name, bus_token);
	if (!name) {
		mutex_unlock(&irq_domain_mutex);
		return;
	}

	debugfs_remove_domain_dir(domain);

	if (domain->flags & IRQ_DOMAIN_NAME_ALLOCATED)
		kfree(domain->name);
	else
		domain->flags |= IRQ_DOMAIN_NAME_ALLOCATED;

	domain->name = name;
	debugfs_add_domain_dir(domain);

	mutex_unlock(&irq_domain_mutex);
}
EXPORT_SYMBOL_GPL(irq_domain_update_bus_token);

/**
 * irq_domain_create_simple() - Register an irq_domain and optionally map a range of irqs
 * @fwnode: firmware node for the interrupt controller
 * @size: total number of irqs in mapping
 * @first_irq: first number of irq block assigned to the domain,
 *	pass zero to assign irqs on-the-fly. If first_irq is non-zero, then
 *	pre-map all of the irqs in the domain to virqs starting at first_irq.
 * @ops: domain callbacks
 * @host_data: Controller private data pointer
 *
 * Allocates an irq_domain, and optionally if first_irq is positive then also
 * allocate irq_descs and map all of the hwirqs to virqs starting at first_irq.
 *
 * This is intended to implement the expected behaviour for most
 * interrupt controllers. If device tree is used, then first_irq will be 0 and
 * irqs get mapped dynamically on the fly. However, if the controller requires
 * static virq assignments (non-DT boot) then it will set that up correctly.
 */
struct irq_domain *irq_domain_create_simple(struct fwnode_handle *fwnode,
					    unsigned int size,
					    unsigned int first_irq,
					    const struct irq_domain_ops *ops,
					    void *host_data)
{
	struct irq_domain_info info = {
		.fwnode		= fwnode,
		.size		= size,
		.hwirq_max	= size,
		.virq_base	= first_irq,
		.ops		= ops,
		.host_data	= host_data,
	};
	struct irq_domain *domain = __irq_domain_instantiate(&info, true, false);

	return IS_ERR(domain) ? NULL : domain;
}
EXPORT_SYMBOL_GPL(irq_domain_create_simple);

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
	return irq_domain_create_legacy(of_node_to_fwnode(of_node), size,
					first_irq, first_hwirq, ops, host_data);
}
EXPORT_SYMBOL_GPL(irq_domain_add_legacy);

struct irq_domain *irq_domain_create_legacy(struct fwnode_handle *fwnode,
					 unsigned int size,
					 unsigned int first_irq,
					 irq_hw_number_t first_hwirq,
					 const struct irq_domain_ops *ops,
					 void *host_data)
{
	struct irq_domain_info info = {
		.fwnode		= fwnode,
		.size		= first_hwirq + size,
		.hwirq_max	= first_hwirq + size,
		.hwirq_base	= first_hwirq,
		.virq_base	= first_irq,
		.ops		= ops,
		.host_data	= host_data,
	};
	struct irq_domain *domain = __irq_domain_instantiate(&info, false, true);

	return IS_ERR(domain) ? NULL : domain;
}
EXPORT_SYMBOL_GPL(irq_domain_create_legacy);

/**
 * irq_find_matching_fwspec() - Locates a domain for a given fwspec
 * @fwspec: FW specifier for an interrupt
 * @bus_token: domain-specific data
 */
struct irq_domain *irq_find_matching_fwspec(struct irq_fwspec *fwspec,
					    enum irq_domain_bus_token bus_token)
{
	struct irq_domain *h, *found = NULL;
	struct fwnode_handle *fwnode = fwspec->fwnode;
	int rc;

	/*
	 * We might want to match the legacy controller last since
	 * it might potentially be set to match all interrupts in
	 * the absence of a device node. This isn't a problem so far
	 * yet though...
	 *
	 * bus_token == DOMAIN_BUS_ANY matches any domain, any other
	 * values must generate an exact match for the domain to be
	 * selected.
	 */
	mutex_lock(&irq_domain_mutex);
	list_for_each_entry(h, &irq_domain_list, link) {
		if (h->ops->select && bus_token != DOMAIN_BUS_ANY)
			rc = h->ops->select(h, fwspec, bus_token);
		else if (h->ops->match)
			rc = h->ops->match(h, to_of_node(fwnode), bus_token);
		else
			rc = ((fwnode != NULL) && (h->fwnode == fwnode) &&
			      ((bus_token == DOMAIN_BUS_ANY) ||
			       (h->bus_token == bus_token)));

		if (rc) {
			found = h;
			break;
		}
	}
	mutex_unlock(&irq_domain_mutex);
	return found;
}
EXPORT_SYMBOL_GPL(irq_find_matching_fwspec);

/**
 * irq_set_default_domain() - Set a "default" irq domain
 * @domain: default domain pointer
 *
 * For convenience, it's possible to set a "default" domain that will be used
 * whenever NULL is passed to irq_create_mapping(). It makes life easier for
 * platforms that want to manipulate a few hard coded interrupt numbers that
 * aren't properly represented in the device-tree.
 */
void irq_set_default_domain(struct irq_domain *domain)
{
	pr_debug("Default domain set to @0x%p\n", domain);

	irq_default_domain = domain;
}
EXPORT_SYMBOL_GPL(irq_set_default_domain);

/**
 * irq_get_default_domain() - Retrieve the "default" irq domain
 *
 * Returns: the default domain, if any.
 *
 * Modern code should never use this. This should only be used on
 * systems that cannot implement a firmware->fwnode mapping (which
 * both DT and ACPI provide).
 */
struct irq_domain *irq_get_default_domain(void)
{
	return irq_default_domain;
}
EXPORT_SYMBOL_GPL(irq_get_default_domain);

static bool irq_domain_is_nomap(struct irq_domain *domain)
{
	return IS_ENABLED(CONFIG_IRQ_DOMAIN_NOMAP) &&
	       (domain->flags & IRQ_DOMAIN_FLAG_NO_MAP);
}

static void irq_domain_clear_mapping(struct irq_domain *domain,
				     irq_hw_number_t hwirq)
{
	lockdep_assert_held(&domain->root->mutex);

	if (irq_domain_is_nomap(domain))
		return;

	if (hwirq < domain->revmap_size)
		rcu_assign_pointer(domain->revmap[hwirq], NULL);
	else
		radix_tree_delete(&domain->revmap_tree, hwirq);
}

static void irq_domain_set_mapping(struct irq_domain *domain,
				   irq_hw_number_t hwirq,
				   struct irq_data *irq_data)
{
	/*
	 * This also makes sure that all domains point to the same root when
	 * called from irq_domain_insert_irq() for each domain in a hierarchy.
	 */
	lockdep_assert_held(&domain->root->mutex);

	if (irq_domain_is_nomap(domain))
		return;

	if (hwirq < domain->revmap_size)
		rcu_assign_pointer(domain->revmap[hwirq], irq_data);
	else
		radix_tree_insert(&domain->revmap_tree, hwirq, irq_data);
}

static void irq_domain_disassociate(struct irq_domain *domain, unsigned int irq)
{
	struct irq_data *irq_data = irq_get_irq_data(irq);
	irq_hw_number_t hwirq;

	if (WARN(!irq_data || irq_data->domain != domain,
		 "virq%i doesn't exist; cannot disassociate\n", irq))
		return;

	hwirq = irq_data->hwirq;

	mutex_lock(&domain->root->mutex);

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
	domain->mapcount--;

	/* Clear reverse map for this hwirq */
	irq_domain_clear_mapping(domain, hwirq);

	mutex_unlock(&domain->root->mutex);
}

static int irq_domain_associate_locked(struct irq_domain *domain, unsigned int virq,
				       irq_hw_number_t hwirq)
{
	struct irq_data *irq_data = irq_get_irq_data(virq);
	int ret;

	if (WARN(hwirq >= domain->hwirq_max,
		 "error: hwirq 0x%x is too large for %s\n", (int)hwirq, domain->name))
		return -EINVAL;
	if (WARN(!irq_data, "error: virq%i is not allocated", virq))
		return -EINVAL;
	if (WARN(irq_data->domain, "error: virq%i is already associated", virq))
		return -EINVAL;

	irq_data->hwirq = hwirq;
	irq_data->domain = domain;
	if (domain->ops->map) {
		ret = domain->ops->map(domain, virq, hwirq);
		if (ret != 0) {
			/*
			 * If map() returns -EPERM, this interrupt is protected
			 * by the firmware or some other service and shall not
			 * be mapped. Don't bother telling the user about it.
			 */
			if (ret != -EPERM) {
				pr_info("%s didn't like hwirq-0x%lx to VIRQ%i mapping (rc=%d)\n",
				       domain->name, hwirq, virq, ret);
			}
			irq_data->domain = NULL;
			irq_data->hwirq = 0;
			return ret;
		}
	}

	domain->mapcount++;
	irq_domain_set_mapping(domain, hwirq, irq_data);

	irq_clear_status_flags(virq, IRQ_NOREQUEST);

	return 0;
}

int irq_domain_associate(struct irq_domain *domain, unsigned int virq,
			 irq_hw_number_t hwirq)
{
	int ret;

	mutex_lock(&domain->root->mutex);
	ret = irq_domain_associate_locked(domain, virq, hwirq);
	mutex_unlock(&domain->root->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(irq_domain_associate);

void irq_domain_associate_many(struct irq_domain *domain, unsigned int irq_base,
			       irq_hw_number_t hwirq_base, int count)
{
	struct device_node *of_node;
	int i;

	of_node = irq_domain_get_of_node(domain);
	pr_debug("%s(%s, irqbase=%i, hwbase=%i, count=%i)\n", __func__,
		of_node_full_name(of_node), irq_base, (int)hwirq_base, count);

	for (i = 0; i < count; i++)
		irq_domain_associate(domain, irq_base + i, hwirq_base + i);
}
EXPORT_SYMBOL_GPL(irq_domain_associate_many);

#ifdef CONFIG_IRQ_DOMAIN_NOMAP
/**
 * irq_create_direct_mapping() - Allocate an irq for direct mapping
 * @domain: domain to allocate the irq for or NULL for default domain
 *
 * This routine is used for irq controllers which can choose the hardware
 * interrupt numbers they generate. In such a case it's simplest to use
 * the linux irq as the hardware interrupt number. It still uses the linear
 * or radix tree to store the mapping, but the irq controller can optimize
 * the revmap path by using the hwirq directly.
 */
unsigned int irq_create_direct_mapping(struct irq_domain *domain)
{
	struct device_node *of_node;
	unsigned int virq;

	if (domain == NULL)
		domain = irq_default_domain;

	of_node = irq_domain_get_of_node(domain);
	virq = irq_alloc_desc_from(1, of_node_to_nid(of_node));
	if (!virq) {
		pr_debug("create_direct virq allocation failed\n");
		return 0;
	}
	if (virq >= domain->hwirq_max) {
		pr_err("ERROR: no free irqs available below %lu maximum\n",
			domain->hwirq_max);
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
#endif

static unsigned int irq_create_mapping_affinity_locked(struct irq_domain *domain,
						       irq_hw_number_t hwirq,
						       const struct irq_affinity_desc *affinity)
{
	struct device_node *of_node = irq_domain_get_of_node(domain);
	int virq;

	pr_debug("irq_create_mapping(0x%p, 0x%lx)\n", domain, hwirq);

	/* Allocate a virtual interrupt number */
	virq = irq_domain_alloc_descs(-1, 1, hwirq, of_node_to_nid(of_node),
				      affinity);
	if (virq <= 0) {
		pr_debug("-> virq allocation failed\n");
		return 0;
	}

	if (irq_domain_associate_locked(domain, virq, hwirq)) {
		irq_free_desc(virq);
		return 0;
	}

	pr_debug("irq %lu on domain %s mapped to virtual irq %u\n",
		hwirq, of_node_full_name(of_node), virq);

	return virq;
}

/**
 * irq_create_mapping_affinity() - Map a hardware interrupt into linux irq space
 * @domain: domain owning this hardware interrupt or NULL for default domain
 * @hwirq: hardware irq number in that domain space
 * @affinity: irq affinity
 *
 * Only one mapping per hardware interrupt is permitted. Returns a linux
 * irq number.
 * If the sense/trigger is to be specified, set_irq_type() should be called
 * on the number returned from that call.
 */
unsigned int irq_create_mapping_affinity(struct irq_domain *domain,
					 irq_hw_number_t hwirq,
					 const struct irq_affinity_desc *affinity)
{
	int virq;

	/* Look for default domain if necessary */
	if (domain == NULL)
		domain = irq_default_domain;
	if (domain == NULL) {
		WARN(1, "%s(, %lx) called with NULL domain\n", __func__, hwirq);
		return 0;
	}

	mutex_lock(&domain->root->mutex);

	/* Check if mapping already exists */
	virq = irq_find_mapping(domain, hwirq);
	if (virq) {
		pr_debug("existing mapping on virq %d\n", virq);
		goto out;
	}

	virq = irq_create_mapping_affinity_locked(domain, hwirq, affinity);
out:
	mutex_unlock(&domain->root->mutex);

	return virq;
}
EXPORT_SYMBOL_GPL(irq_create_mapping_affinity);

static int irq_domain_translate(struct irq_domain *d,
				struct irq_fwspec *fwspec,
				irq_hw_number_t *hwirq, unsigned int *type)
{
#ifdef CONFIG_IRQ_DOMAIN_HIERARCHY
	if (d->ops->translate)
		return d->ops->translate(d, fwspec, hwirq, type);
#endif
	if (d->ops->xlate)
		return d->ops->xlate(d, to_of_node(fwspec->fwnode),
				     fwspec->param, fwspec->param_count,
				     hwirq, type);

	/* If domain has no translation, then we assume interrupt line */
	*hwirq = fwspec->param[0];
	return 0;
}

void of_phandle_args_to_fwspec(struct device_node *np, const u32 *args,
			       unsigned int count, struct irq_fwspec *fwspec)
{
	int i;

	fwspec->fwnode = of_node_to_fwnode(np);
	fwspec->param_count = count;

	for (i = 0; i < count; i++)
		fwspec->param[i] = args[i];
}
EXPORT_SYMBOL_GPL(of_phandle_args_to_fwspec);

unsigned int irq_create_fwspec_mapping(struct irq_fwspec *fwspec)
{
	struct irq_domain *domain;
	struct irq_data *irq_data;
	irq_hw_number_t hwirq;
	unsigned int type = IRQ_TYPE_NONE;
	int virq;

	if (fwspec->fwnode) {
		domain = irq_find_matching_fwspec(fwspec, DOMAIN_BUS_WIRED);
		if (!domain)
			domain = irq_find_matching_fwspec(fwspec, DOMAIN_BUS_ANY);
	} else {
		domain = irq_default_domain;
	}

	if (!domain) {
		pr_warn("no irq domain found for %s !\n",
			of_node_full_name(to_of_node(fwspec->fwnode)));
		return 0;
	}

	if (irq_domain_translate(domain, fwspec, &hwirq, &type))
		return 0;

	/*
	 * WARN if the irqchip returns a type with bits
	 * outside the sense mask set and clear these bits.
	 */
	if (WARN_ON(type & ~IRQ_TYPE_SENSE_MASK))
		type &= IRQ_TYPE_SENSE_MASK;

	mutex_lock(&domain->root->mutex);

	/*
	 * If we've already configured this interrupt,
	 * don't do it again, or hell will break loose.
	 */
	virq = irq_find_mapping(domain, hwirq);
	if (virq) {
		/*
		 * If the trigger type is not specified or matches the
		 * current trigger type then we are done so return the
		 * interrupt number.
		 */
		if (type == IRQ_TYPE_NONE || type == irq_get_trigger_type(virq))
			goto out;

		/*
		 * If the trigger type has not been set yet, then set
		 * it now and return the interrupt number.
		 */
		if (irq_get_trigger_type(virq) == IRQ_TYPE_NONE) {
			irq_data = irq_get_irq_data(virq);
			if (!irq_data) {
				virq = 0;
				goto out;
			}

			irqd_set_trigger_type(irq_data, type);
			goto out;
		}

		pr_warn("type mismatch, failed to map hwirq-%lu for %s!\n",
			hwirq, of_node_full_name(to_of_node(fwspec->fwnode)));
		virq = 0;
		goto out;
	}

	if (irq_domain_is_hierarchy(domain)) {
		if (irq_domain_is_msi_device(domain)) {
			mutex_unlock(&domain->root->mutex);
			virq = msi_device_domain_alloc_wired(domain, hwirq, type);
			mutex_lock(&domain->root->mutex);
		} else
			virq = irq_domain_alloc_irqs_locked(domain, -1, 1, NUMA_NO_NODE,
							    fwspec, false, NULL);
		if (virq <= 0) {
			virq = 0;
			goto out;
		}
	} else {
		/* Create mapping */
		virq = irq_create_mapping_affinity_locked(domain, hwirq, NULL);
		if (!virq)
			goto out;
	}

	irq_data = irq_get_irq_data(virq);
	if (WARN_ON(!irq_data)) {
		virq = 0;
		goto out;
	}

	/* Store trigger type */
	irqd_set_trigger_type(irq_data, type);
out:
	mutex_unlock(&domain->root->mutex);

	return virq;
}
EXPORT_SYMBOL_GPL(irq_create_fwspec_mapping);

unsigned int irq_create_of_mapping(struct of_phandle_args *irq_data)
{
	struct irq_fwspec fwspec;

	of_phandle_args_to_fwspec(irq_data->np, irq_data->args,
				  irq_data->args_count, &fwspec);

	return irq_create_fwspec_mapping(&fwspec);
}
EXPORT_SYMBOL_GPL(irq_create_of_mapping);

/**
 * irq_dispose_mapping() - Unmap an interrupt
 * @virq: linux irq number of the interrupt to unmap
 */
void irq_dispose_mapping(unsigned int virq)
{
	struct irq_data *irq_data;
	struct irq_domain *domain;

	irq_data = virq ? irq_get_irq_data(virq) : NULL;
	if (!irq_data)
		return;

	domain = irq_data->domain;
	if (WARN_ON(domain == NULL))
		return;

	if (irq_domain_is_hierarchy(domain)) {
		irq_domain_free_one_irq(domain, virq);
	} else {
		irq_domain_disassociate(domain, virq);
		irq_free_desc(virq);
	}
}
EXPORT_SYMBOL_GPL(irq_dispose_mapping);

/**
 * __irq_resolve_mapping() - Find a linux irq from a hw irq number.
 * @domain: domain owning this hardware interrupt
 * @hwirq: hardware irq number in that domain space
 * @irq: optional pointer to return the Linux irq if required
 *
 * Returns the interrupt descriptor.
 */
struct irq_desc *__irq_resolve_mapping(struct irq_domain *domain,
				       irq_hw_number_t hwirq,
				       unsigned int *irq)
{
	struct irq_desc *desc = NULL;
	struct irq_data *data;

	/* Look for default domain if necessary */
	if (domain == NULL)
		domain = irq_default_domain;
	if (domain == NULL)
		return desc;

	if (irq_domain_is_nomap(domain)) {
		if (hwirq < domain->hwirq_max) {
			data = irq_domain_get_irq_data(domain, hwirq);
			if (data && data->hwirq == hwirq)
				desc = irq_data_to_desc(data);
			if (irq && desc)
				*irq = hwirq;
		}

		return desc;
	}

	rcu_read_lock();
	/* Check if the hwirq is in the linear revmap. */
	if (hwirq < domain->revmap_size)
		data = rcu_dereference(domain->revmap[hwirq]);
	else
		data = radix_tree_lookup(&domain->revmap_tree, hwirq);

	if (likely(data)) {
		desc = irq_data_to_desc(data);
		if (irq)
			*irq = data->irq;
	}

	rcu_read_unlock();
	return desc;
}
EXPORT_SYMBOL_GPL(__irq_resolve_mapping);

/**
 * irq_domain_xlate_onecell() - Generic xlate for direct one cell bindings
 * @d:		Interrupt domain involved in the translation
 * @ctrlr:	The device tree node for the device whose interrupt is translated
 * @intspec:	The interrupt specifier data from the device tree
 * @intsize:	The number of entries in @intspec
 * @out_hwirq:	Pointer to storage for the hardware interrupt number
 * @out_type:	Pointer to storage for the interrupt type
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
 * @d:		Interrupt domain involved in the translation
 * @ctrlr:	The device tree node for the device whose interrupt is translated
 * @intspec:	The interrupt specifier data from the device tree
 * @intsize:	The number of entries in @intspec
 * @out_hwirq:	Pointer to storage for the hardware interrupt number
 * @out_type:	Pointer to storage for the interrupt type
 *
 * Device Tree IRQ specifier translation function which works with two cell
 * bindings where the cell values map directly to the hwirq number
 * and linux irq flags.
 */
int irq_domain_xlate_twocell(struct irq_domain *d, struct device_node *ctrlr,
			const u32 *intspec, unsigned int intsize,
			irq_hw_number_t *out_hwirq, unsigned int *out_type)
{
	struct irq_fwspec fwspec;

	of_phandle_args_to_fwspec(ctrlr, intspec, intsize, &fwspec);
	return irq_domain_translate_twocell(d, &fwspec, out_hwirq, out_type);
}
EXPORT_SYMBOL_GPL(irq_domain_xlate_twocell);

/**
 * irq_domain_xlate_onetwocell() - Generic xlate for one or two cell bindings
 * @d:		Interrupt domain involved in the translation
 * @ctrlr:	The device tree node for the device whose interrupt is translated
 * @intspec:	The interrupt specifier data from the device tree
 * @intsize:	The number of entries in @intspec
 * @out_hwirq:	Pointer to storage for the hardware interrupt number
 * @out_type:	Pointer to storage for the interrupt type
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
	if (intsize > 1)
		*out_type = intspec[1] & IRQ_TYPE_SENSE_MASK;
	else
		*out_type = IRQ_TYPE_NONE;
	return 0;
}
EXPORT_SYMBOL_GPL(irq_domain_xlate_onetwocell);

const struct irq_domain_ops irq_domain_simple_ops = {
	.xlate = irq_domain_xlate_onetwocell,
};
EXPORT_SYMBOL_GPL(irq_domain_simple_ops);

/**
 * irq_domain_translate_onecell() - Generic translate for direct one cell
 * bindings
 * @d:		Interrupt domain involved in the translation
 * @fwspec:	The firmware interrupt specifier to translate
 * @out_hwirq:	Pointer to storage for the hardware interrupt number
 * @out_type:	Pointer to storage for the interrupt type
 */
int irq_domain_translate_onecell(struct irq_domain *d,
				 struct irq_fwspec *fwspec,
				 unsigned long *out_hwirq,
				 unsigned int *out_type)
{
	if (WARN_ON(fwspec->param_count < 1))
		return -EINVAL;
	*out_hwirq = fwspec->param[0];
	*out_type = IRQ_TYPE_NONE;
	return 0;
}
EXPORT_SYMBOL_GPL(irq_domain_translate_onecell);

/**
 * irq_domain_translate_twocell() - Generic translate for direct two cell
 * bindings
 * @d:		Interrupt domain involved in the translation
 * @fwspec:	The firmware interrupt specifier to translate
 * @out_hwirq:	Pointer to storage for the hardware interrupt number
 * @out_type:	Pointer to storage for the interrupt type
 *
 * Device Tree IRQ specifier translation function which works with two cell
 * bindings where the cell values map directly to the hwirq number
 * and linux irq flags.
 */
int irq_domain_translate_twocell(struct irq_domain *d,
				 struct irq_fwspec *fwspec,
				 unsigned long *out_hwirq,
				 unsigned int *out_type)
{
	if (WARN_ON(fwspec->param_count < 2))
		return -EINVAL;
	*out_hwirq = fwspec->param[0];
	*out_type = fwspec->param[1] & IRQ_TYPE_SENSE_MASK;
	return 0;
}
EXPORT_SYMBOL_GPL(irq_domain_translate_twocell);

int irq_domain_alloc_descs(int virq, unsigned int cnt, irq_hw_number_t hwirq,
			   int node, const struct irq_affinity_desc *affinity)
{
	unsigned int hint;

	if (virq >= 0) {
		virq = __irq_alloc_descs(virq, virq, cnt, node, THIS_MODULE,
					 affinity);
	} else {
		hint = hwirq % irq_get_nr_irqs();
		if (hint == 0)
			hint++;
		virq = __irq_alloc_descs(-1, hint, cnt, node, THIS_MODULE,
					 affinity);
		if (virq <= 0 && hint > 1) {
			virq = __irq_alloc_descs(-1, 1, cnt, node, THIS_MODULE,
						 affinity);
		}
	}

	return virq;
}

/**
 * irq_domain_reset_irq_data - Clear hwirq, chip and chip_data in @irq_data
 * @irq_data:	The pointer to irq_data
 */
void irq_domain_reset_irq_data(struct irq_data *irq_data)
{
	irq_data->hwirq = 0;
	irq_data->chip = &no_irq_chip;
	irq_data->chip_data = NULL;
}
EXPORT_SYMBOL_GPL(irq_domain_reset_irq_data);

#ifdef	CONFIG_IRQ_DOMAIN_HIERARCHY
/**
 * irq_domain_create_hierarchy - Add a irqdomain into the hierarchy
 * @parent:	Parent irq domain to associate with the new domain
 * @flags:	Irq domain flags associated to the domain
 * @size:	Size of the domain. See below
 * @fwnode:	Optional fwnode of the interrupt controller
 * @ops:	Pointer to the interrupt domain callbacks
 * @host_data:	Controller private data pointer
 *
 * If @size is 0 a tree domain is created, otherwise a linear domain.
 *
 * If successful the parent is associated to the new domain and the
 * domain flags are set.
 * Returns pointer to IRQ domain, or NULL on failure.
 */
struct irq_domain *irq_domain_create_hierarchy(struct irq_domain *parent,
					    unsigned int flags,
					    unsigned int size,
					    struct fwnode_handle *fwnode,
					    const struct irq_domain_ops *ops,
					    void *host_data)
{
	struct irq_domain_info info = {
		.fwnode		= fwnode,
		.size		= size,
		.hwirq_max	= size,
		.ops		= ops,
		.host_data	= host_data,
		.domain_flags	= flags,
		.parent		= parent,
	};
	struct irq_domain *d;

	if (!info.size)
		info.hwirq_max = ~0U;

	d = irq_domain_instantiate(&info);
	return IS_ERR(d) ? NULL : d;
}
EXPORT_SYMBOL_GPL(irq_domain_create_hierarchy);

static void irq_domain_insert_irq(int virq)
{
	struct irq_data *data;

	for (data = irq_get_irq_data(virq); data; data = data->parent_data) {
		struct irq_domain *domain = data->domain;

		domain->mapcount++;
		irq_domain_set_mapping(domain, data->hwirq, data);
	}

	irq_clear_status_flags(virq, IRQ_NOREQUEST);
}

static void irq_domain_remove_irq(int virq)
{
	struct irq_data *data;

	irq_set_status_flags(virq, IRQ_NOREQUEST);
	irq_set_chip_and_handler(virq, NULL, NULL);
	synchronize_irq(virq);
	smp_mb();

	for (data = irq_get_irq_data(virq); data; data = data->parent_data) {
		struct irq_domain *domain = data->domain;
		irq_hw_number_t hwirq = data->hwirq;

		domain->mapcount--;
		irq_domain_clear_mapping(domain, hwirq);
	}
}

static struct irq_data *irq_domain_insert_irq_data(struct irq_domain *domain,
						   struct irq_data *child)
{
	struct irq_data *irq_data;

	irq_data = kzalloc_node(sizeof(*irq_data), GFP_KERNEL,
				irq_data_get_node(child));
	if (irq_data) {
		child->parent_data = irq_data;
		irq_data->irq = child->irq;
		irq_data->common = child->common;
		irq_data->domain = domain;
	}

	return irq_data;
}

static void __irq_domain_free_hierarchy(struct irq_data *irq_data)
{
	struct irq_data *tmp;

	while (irq_data) {
		tmp = irq_data;
		irq_data = irq_data->parent_data;
		kfree(tmp);
	}
}

static void irq_domain_free_irq_data(unsigned int virq, unsigned int nr_irqs)
{
	struct irq_data *irq_data, *tmp;
	int i;

	for (i = 0; i < nr_irqs; i++) {
		irq_data = irq_get_irq_data(virq + i);
		tmp = irq_data->parent_data;
		irq_data->parent_data = NULL;
		irq_data->domain = NULL;

		__irq_domain_free_hierarchy(tmp);
	}
}

/**
 * irq_domain_disconnect_hierarchy - Mark the first unused level of a hierarchy
 * @domain:	IRQ domain from which the hierarchy is to be disconnected
 * @virq:	IRQ number where the hierarchy is to be trimmed
 *
 * Marks the @virq level belonging to @domain as disconnected.
 * Returns -EINVAL if @virq doesn't have a valid irq_data pointing
 * to @domain.
 *
 * Its only use is to be able to trim levels of hierarchy that do not
 * have any real meaning for this interrupt, and that the driver marks
 * as such from its .alloc() callback.
 */
int irq_domain_disconnect_hierarchy(struct irq_domain *domain,
				    unsigned int virq)
{
	struct irq_data *irqd;

	irqd = irq_domain_get_irq_data(domain, virq);
	if (!irqd)
		return -EINVAL;

	irqd->chip = ERR_PTR(-ENOTCONN);
	return 0;
}
EXPORT_SYMBOL_GPL(irq_domain_disconnect_hierarchy);

static int irq_domain_trim_hierarchy(unsigned int virq)
{
	struct irq_data *tail, *irqd, *irq_data;

	irq_data = irq_get_irq_data(virq);
	tail = NULL;

	/* The first entry must have a valid irqchip */
	if (IS_ERR_OR_NULL(irq_data->chip))
		return -EINVAL;

	/*
	 * Validate that the irq_data chain is sane in the presence of
	 * a hierarchy trimming marker.
	 */
	for (irqd = irq_data->parent_data; irqd; irq_data = irqd, irqd = irqd->parent_data) {
		/* Can't have a valid irqchip after a trim marker */
		if (irqd->chip && tail)
			return -EINVAL;

		/* Can't have an empty irqchip before a trim marker */
		if (!irqd->chip && !tail)
			return -EINVAL;

		if (IS_ERR(irqd->chip)) {
			/* Only -ENOTCONN is a valid trim marker */
			if (PTR_ERR(irqd->chip) != -ENOTCONN)
				return -EINVAL;

			tail = irq_data;
		}
	}

	/* No trim marker, nothing to do */
	if (!tail)
		return 0;

	pr_info("IRQ%d: trimming hierarchy from %s\n",
		virq, tail->parent_data->domain->name);

	/* Sever the inner part of the hierarchy...  */
	irqd = tail;
	tail = tail->parent_data;
	irqd->parent_data = NULL;
	__irq_domain_free_hierarchy(tail);

	return 0;
}

static int irq_domain_alloc_irq_data(struct irq_domain *domain,
				     unsigned int virq, unsigned int nr_irqs)
{
	struct irq_data *irq_data;
	struct irq_domain *parent;
	int i;

	/* The outermost irq_data is embedded in struct irq_desc */
	for (i = 0; i < nr_irqs; i++) {
		irq_data = irq_get_irq_data(virq + i);
		irq_data->domain = domain;

		for (parent = domain->parent; parent; parent = parent->parent) {
			irq_data = irq_domain_insert_irq_data(parent, irq_data);
			if (!irq_data) {
				irq_domain_free_irq_data(virq, i + 1);
				return -ENOMEM;
			}
		}
	}

	return 0;
}

/**
 * irq_domain_get_irq_data - Get irq_data associated with @virq and @domain
 * @domain:	domain to match
 * @virq:	IRQ number to get irq_data
 */
struct irq_data *irq_domain_get_irq_data(struct irq_domain *domain,
					 unsigned int virq)
{
	struct irq_data *irq_data;

	for (irq_data = irq_get_irq_data(virq); irq_data;
	     irq_data = irq_data->parent_data)
		if (irq_data->domain == domain)
			return irq_data;

	return NULL;
}
EXPORT_SYMBOL_GPL(irq_domain_get_irq_data);

/**
 * irq_domain_set_hwirq_and_chip - Set hwirq and irqchip of @virq at @domain
 * @domain:	Interrupt domain to match
 * @virq:	IRQ number
 * @hwirq:	The hwirq number
 * @chip:	The associated interrupt chip
 * @chip_data:	The associated chip data
 */
int irq_domain_set_hwirq_and_chip(struct irq_domain *domain, unsigned int virq,
				  irq_hw_number_t hwirq,
				  const struct irq_chip *chip,
				  void *chip_data)
{
	struct irq_data *irq_data = irq_domain_get_irq_data(domain, virq);

	if (!irq_data)
		return -ENOENT;

	irq_data->hwirq = hwirq;
	irq_data->chip = (struct irq_chip *)(chip ? chip : &no_irq_chip);
	irq_data->chip_data = chip_data;

	return 0;
}
EXPORT_SYMBOL_GPL(irq_domain_set_hwirq_and_chip);

/**
 * irq_domain_set_info - Set the complete data for a @virq in @domain
 * @domain:		Interrupt domain to match
 * @virq:		IRQ number
 * @hwirq:		The hardware interrupt number
 * @chip:		The associated interrupt chip
 * @chip_data:		The associated interrupt chip data
 * @handler:		The interrupt flow handler
 * @handler_data:	The interrupt flow handler data
 * @handler_name:	The interrupt handler name
 */
void irq_domain_set_info(struct irq_domain *domain, unsigned int virq,
			 irq_hw_number_t hwirq, const struct irq_chip *chip,
			 void *chip_data, irq_flow_handler_t handler,
			 void *handler_data, const char *handler_name)
{
	irq_domain_set_hwirq_and_chip(domain, virq, hwirq, chip, chip_data);
	__irq_set_handler(virq, handler, 0, handler_name);
	irq_set_handler_data(virq, handler_data);
}
EXPORT_SYMBOL(irq_domain_set_info);

/**
 * irq_domain_free_irqs_common - Clear irq_data and free the parent
 * @domain:	Interrupt domain to match
 * @virq:	IRQ number to start with
 * @nr_irqs:	The number of irqs to free
 */
void irq_domain_free_irqs_common(struct irq_domain *domain, unsigned int virq,
				 unsigned int nr_irqs)
{
	struct irq_data *irq_data;
	int i;

	for (i = 0; i < nr_irqs; i++) {
		irq_data = irq_domain_get_irq_data(domain, virq + i);
		if (irq_data)
			irq_domain_reset_irq_data(irq_data);
	}
	irq_domain_free_irqs_parent(domain, virq, nr_irqs);
}
EXPORT_SYMBOL_GPL(irq_domain_free_irqs_common);

/**
 * irq_domain_free_irqs_top - Clear handler and handler data, clear irqdata and free parent
 * @domain:	Interrupt domain to match
 * @virq:	IRQ number to start with
 * @nr_irqs:	The number of irqs to free
 */
void irq_domain_free_irqs_top(struct irq_domain *domain, unsigned int virq,
			      unsigned int nr_irqs)
{
	int i;

	for (i = 0; i < nr_irqs; i++) {
		irq_set_handler_data(virq + i, NULL);
		irq_set_handler(virq + i, NULL);
	}
	irq_domain_free_irqs_common(domain, virq, nr_irqs);
}

static void irq_domain_free_irqs_hierarchy(struct irq_domain *domain,
					   unsigned int irq_base,
					   unsigned int nr_irqs)
{
	unsigned int i;

	if (!domain->ops->free)
		return;

	for (i = 0; i < nr_irqs; i++) {
		if (irq_domain_get_irq_data(domain, irq_base + i))
			domain->ops->free(domain, irq_base + i, 1);
	}
}

static int irq_domain_alloc_irqs_hierarchy(struct irq_domain *domain, unsigned int irq_base,
					   unsigned int nr_irqs, void *arg)
{
	if (!domain->ops->alloc) {
		pr_debug("domain->ops->alloc() is NULL\n");
		return -ENOSYS;
	}

	return domain->ops->alloc(domain, irq_base, nr_irqs, arg);
}

static int irq_domain_alloc_irqs_locked(struct irq_domain *domain, int irq_base,
					unsigned int nr_irqs, int node, void *arg,
					bool realloc, const struct irq_affinity_desc *affinity)
{
	int i, ret, virq;

	if (realloc && irq_base >= 0) {
		virq = irq_base;
	} else {
		virq = irq_domain_alloc_descs(irq_base, nr_irqs, 0, node,
					      affinity);
		if (virq < 0) {
			pr_debug("cannot allocate IRQ(base %d, count %d)\n",
				 irq_base, nr_irqs);
			return virq;
		}
	}

	if (irq_domain_alloc_irq_data(domain, virq, nr_irqs)) {
		pr_debug("cannot allocate memory for IRQ%d\n", virq);
		ret = -ENOMEM;
		goto out_free_desc;
	}

	ret = irq_domain_alloc_irqs_hierarchy(domain, virq, nr_irqs, arg);
	if (ret < 0)
		goto out_free_irq_data;

	for (i = 0; i < nr_irqs; i++) {
		ret = irq_domain_trim_hierarchy(virq + i);
		if (ret)
			goto out_free_irq_data;
	}

	for (i = 0; i < nr_irqs; i++)
		irq_domain_insert_irq(virq + i);

	return virq;

out_free_irq_data:
	irq_domain_free_irq_data(virq, nr_irqs);
out_free_desc:
	irq_free_descs(virq, nr_irqs);
	return ret;
}

/**
 * __irq_domain_alloc_irqs - Allocate IRQs from domain
 * @domain:	domain to allocate from
 * @irq_base:	allocate specified IRQ number if irq_base >= 0
 * @nr_irqs:	number of IRQs to allocate
 * @node:	NUMA node id for memory allocation
 * @arg:	domain specific argument
 * @realloc:	IRQ descriptors have already been allocated if true
 * @affinity:	Optional irq affinity mask for multiqueue devices
 *
 * Allocate IRQ numbers and initialized all data structures to support
 * hierarchy IRQ domains.
 * Parameter @realloc is mainly to support legacy IRQs.
 * Returns error code or allocated IRQ number
 *
 * The whole process to setup an IRQ has been split into two steps.
 * The first step, __irq_domain_alloc_irqs(), is to allocate IRQ
 * descriptor and required hardware resources. The second step,
 * irq_domain_activate_irq(), is to program the hardware with preallocated
 * resources. In this way, it's easier to rollback when failing to
 * allocate resources.
 */
int __irq_domain_alloc_irqs(struct irq_domain *domain, int irq_base,
			    unsigned int nr_irqs, int node, void *arg,
			    bool realloc, const struct irq_affinity_desc *affinity)
{
	int ret;

	if (domain == NULL) {
		domain = irq_default_domain;
		if (WARN(!domain, "domain is NULL; cannot allocate IRQ\n"))
			return -EINVAL;
	}

	mutex_lock(&domain->root->mutex);
	ret = irq_domain_alloc_irqs_locked(domain, irq_base, nr_irqs, node, arg,
					   realloc, affinity);
	mutex_unlock(&domain->root->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(__irq_domain_alloc_irqs);

/* The irq_data was moved, fix the revmap to refer to the new location */
static void irq_domain_fix_revmap(struct irq_data *d)
{
	void __rcu **slot;

	lockdep_assert_held(&d->domain->root->mutex);

	if (irq_domain_is_nomap(d->domain))
		return;

	/* Fix up the revmap. */
	if (d->hwirq < d->domain->revmap_size) {
		/* Not using radix tree */
		rcu_assign_pointer(d->domain->revmap[d->hwirq], d);
	} else {
		slot = radix_tree_lookup_slot(&d->domain->revmap_tree, d->hwirq);
		if (slot)
			radix_tree_replace_slot(&d->domain->revmap_tree, slot, d);
	}
}

/**
 * irq_domain_push_irq() - Push a domain in to the top of a hierarchy.
 * @domain:	Domain to push.
 * @virq:	Irq to push the domain in to.
 * @arg:	Passed to the irq_domain_ops alloc() function.
 *
 * For an already existing irqdomain hierarchy, as might be obtained
 * via a call to pci_enable_msix(), add an additional domain to the
 * head of the processing chain.  Must be called before request_irq()
 * has been called.
 */
int irq_domain_push_irq(struct irq_domain *domain, int virq, void *arg)
{
	struct irq_data *irq_data = irq_get_irq_data(virq);
	struct irq_data *parent_irq_data;
	struct irq_desc *desc;
	int rv = 0;

	/*
	 * Check that no action has been set, which indicates the virq
	 * is in a state where this function doesn't have to deal with
	 * races between interrupt handling and maintaining the
	 * hierarchy.  This will catch gross misuse.  Attempting to
	 * make the check race free would require holding locks across
	 * calls to struct irq_domain_ops->alloc(), which could lead
	 * to deadlock, so we just do a simple check before starting.
	 */
	desc = irq_to_desc(virq);
	if (!desc)
		return -EINVAL;
	if (WARN_ON(desc->action))
		return -EBUSY;

	if (domain == NULL)
		return -EINVAL;

	if (WARN_ON(!irq_domain_is_hierarchy(domain)))
		return -EINVAL;

	if (!irq_data)
		return -EINVAL;

	if (domain->parent != irq_data->domain)
		return -EINVAL;

	parent_irq_data = kzalloc_node(sizeof(*parent_irq_data), GFP_KERNEL,
				       irq_data_get_node(irq_data));
	if (!parent_irq_data)
		return -ENOMEM;

	mutex_lock(&domain->root->mutex);

	/* Copy the original irq_data. */
	*parent_irq_data = *irq_data;

	/*
	 * Overwrite the irq_data, which is embedded in struct irq_desc, with
	 * values for this domain.
	 */
	irq_data->parent_data = parent_irq_data;
	irq_data->domain = domain;
	irq_data->mask = 0;
	irq_data->hwirq = 0;
	irq_data->chip = NULL;
	irq_data->chip_data = NULL;

	/* May (probably does) set hwirq, chip, etc. */
	rv = irq_domain_alloc_irqs_hierarchy(domain, virq, 1, arg);
	if (rv) {
		/* Restore the original irq_data. */
		*irq_data = *parent_irq_data;
		kfree(parent_irq_data);
		goto error;
	}

	irq_domain_fix_revmap(parent_irq_data);
	irq_domain_set_mapping(domain, irq_data->hwirq, irq_data);
error:
	mutex_unlock(&domain->root->mutex);

	return rv;
}
EXPORT_SYMBOL_GPL(irq_domain_push_irq);

/**
 * irq_domain_pop_irq() - Remove a domain from the top of a hierarchy.
 * @domain:	Domain to remove.
 * @virq:	Irq to remove the domain from.
 *
 * Undo the effects of a call to irq_domain_push_irq().  Must be
 * called either before request_irq() or after free_irq().
 */
int irq_domain_pop_irq(struct irq_domain *domain, int virq)
{
	struct irq_data *irq_data = irq_get_irq_data(virq);
	struct irq_data *parent_irq_data;
	struct irq_data *tmp_irq_data;
	struct irq_desc *desc;

	/*
	 * Check that no action is set, which indicates the virq is in
	 * a state where this function doesn't have to deal with races
	 * between interrupt handling and maintaining the hierarchy.
	 * This will catch gross misuse.  Attempting to make the check
	 * race free would require holding locks across calls to
	 * struct irq_domain_ops->free(), which could lead to
	 * deadlock, so we just do a simple check before starting.
	 */
	desc = irq_to_desc(virq);
	if (!desc)
		return -EINVAL;
	if (WARN_ON(desc->action))
		return -EBUSY;

	if (domain == NULL)
		return -EINVAL;

	if (!irq_data)
		return -EINVAL;

	tmp_irq_data = irq_domain_get_irq_data(domain, virq);

	/* We can only "pop" if this domain is at the top of the list */
	if (WARN_ON(irq_data != tmp_irq_data))
		return -EINVAL;

	if (WARN_ON(irq_data->domain != domain))
		return -EINVAL;

	parent_irq_data = irq_data->parent_data;
	if (WARN_ON(!parent_irq_data))
		return -EINVAL;

	mutex_lock(&domain->root->mutex);

	irq_data->parent_data = NULL;

	irq_domain_clear_mapping(domain, irq_data->hwirq);
	irq_domain_free_irqs_hierarchy(domain, virq, 1);

	/* Restore the original irq_data. */
	*irq_data = *parent_irq_data;

	irq_domain_fix_revmap(irq_data);

	mutex_unlock(&domain->root->mutex);

	kfree(parent_irq_data);

	return 0;
}
EXPORT_SYMBOL_GPL(irq_domain_pop_irq);

/**
 * irq_domain_free_irqs - Free IRQ number and associated data structures
 * @virq:	base IRQ number
 * @nr_irqs:	number of IRQs to free
 */
void irq_domain_free_irqs(unsigned int virq, unsigned int nr_irqs)
{
	struct irq_data *data = irq_get_irq_data(virq);
	struct irq_domain *domain;
	int i;

	if (WARN(!data || !data->domain || !data->domain->ops->free,
		 "NULL pointer, cannot free irq\n"))
		return;

	domain = data->domain;

	mutex_lock(&domain->root->mutex);
	for (i = 0; i < nr_irqs; i++)
		irq_domain_remove_irq(virq + i);
	irq_domain_free_irqs_hierarchy(domain, virq, nr_irqs);
	mutex_unlock(&domain->root->mutex);

	irq_domain_free_irq_data(virq, nr_irqs);
	irq_free_descs(virq, nr_irqs);
}

static void irq_domain_free_one_irq(struct irq_domain *domain, unsigned int virq)
{
	if (irq_domain_is_msi_device(domain))
		msi_device_domain_free_wired(domain, virq);
	else
		irq_domain_free_irqs(virq, 1);
}

/**
 * irq_domain_alloc_irqs_parent - Allocate interrupts from parent domain
 * @domain:	Domain below which interrupts must be allocated
 * @irq_base:	Base IRQ number
 * @nr_irqs:	Number of IRQs to allocate
 * @arg:	Allocation data (arch/domain specific)
 */
int irq_domain_alloc_irqs_parent(struct irq_domain *domain,
				 unsigned int irq_base, unsigned int nr_irqs,
				 void *arg)
{
	if (!domain->parent)
		return -ENOSYS;

	return irq_domain_alloc_irqs_hierarchy(domain->parent, irq_base,
					       nr_irqs, arg);
}
EXPORT_SYMBOL_GPL(irq_domain_alloc_irqs_parent);

/**
 * irq_domain_free_irqs_parent - Free interrupts from parent domain
 * @domain:	Domain below which interrupts must be freed
 * @irq_base:	Base IRQ number
 * @nr_irqs:	Number of IRQs to free
 */
void irq_domain_free_irqs_parent(struct irq_domain *domain,
				 unsigned int irq_base, unsigned int nr_irqs)
{
	if (!domain->parent)
		return;

	irq_domain_free_irqs_hierarchy(domain->parent, irq_base, nr_irqs);
}
EXPORT_SYMBOL_GPL(irq_domain_free_irqs_parent);

static void __irq_domain_deactivate_irq(struct irq_data *irq_data)
{
	if (irq_data && irq_data->domain) {
		struct irq_domain *domain = irq_data->domain;

		if (domain->ops->deactivate)
			domain->ops->deactivate(domain, irq_data);
		if (irq_data->parent_data)
			__irq_domain_deactivate_irq(irq_data->parent_data);
	}
}

static int __irq_domain_activate_irq(struct irq_data *irqd, bool reserve)
{
	int ret = 0;

	if (irqd && irqd->domain) {
		struct irq_domain *domain = irqd->domain;

		if (irqd->parent_data)
			ret = __irq_domain_activate_irq(irqd->parent_data,
							reserve);
		if (!ret && domain->ops->activate) {
			ret = domain->ops->activate(domain, irqd, reserve);
			/* Rollback in case of error */
			if (ret && irqd->parent_data)
				__irq_domain_deactivate_irq(irqd->parent_data);
		}
	}
	return ret;
}

/**
 * irq_domain_activate_irq - Call domain_ops->activate recursively to activate
 *			     interrupt
 * @irq_data:	Outermost irq_data associated with interrupt
 * @reserve:	If set only reserve an interrupt vector instead of assigning one
 *
 * This is the second step to call domain_ops->activate to program interrupt
 * controllers, so the interrupt could actually get delivered.
 */
int irq_domain_activate_irq(struct irq_data *irq_data, bool reserve)
{
	int ret = 0;

	if (!irqd_is_activated(irq_data))
		ret = __irq_domain_activate_irq(irq_data, reserve);
	if (!ret)
		irqd_set_activated(irq_data);
	return ret;
}

/**
 * irq_domain_deactivate_irq - Call domain_ops->deactivate recursively to
 *			       deactivate interrupt
 * @irq_data: outermost irq_data associated with interrupt
 *
 * It calls domain_ops->deactivate to program interrupt controllers to disable
 * interrupt delivery.
 */
void irq_domain_deactivate_irq(struct irq_data *irq_data)
{
	if (irqd_is_activated(irq_data)) {
		__irq_domain_deactivate_irq(irq_data);
		irqd_clr_activated(irq_data);
	}
}

static void irq_domain_check_hierarchy(struct irq_domain *domain)
{
	/* Hierarchy irq_domains must implement callback alloc() */
	if (domain->ops->alloc)
		domain->flags |= IRQ_DOMAIN_FLAG_HIERARCHY;
}
#else	/* CONFIG_IRQ_DOMAIN_HIERARCHY */
/**
 * irq_domain_get_irq_data - Get irq_data associated with @virq and @domain
 * @domain:	domain to match
 * @virq:	IRQ number to get irq_data
 */
struct irq_data *irq_domain_get_irq_data(struct irq_domain *domain,
					 unsigned int virq)
{
	struct irq_data *irq_data = irq_get_irq_data(virq);

	return (irq_data && irq_data->domain == domain) ? irq_data : NULL;
}
EXPORT_SYMBOL_GPL(irq_domain_get_irq_data);

/**
 * irq_domain_set_info - Set the complete data for a @virq in @domain
 * @domain:		Interrupt domain to match
 * @virq:		IRQ number
 * @hwirq:		The hardware interrupt number
 * @chip:		The associated interrupt chip
 * @chip_data:		The associated interrupt chip data
 * @handler:		The interrupt flow handler
 * @handler_data:	The interrupt flow handler data
 * @handler_name:	The interrupt handler name
 */
void irq_domain_set_info(struct irq_domain *domain, unsigned int virq,
			 irq_hw_number_t hwirq, const struct irq_chip *chip,
			 void *chip_data, irq_flow_handler_t handler,
			 void *handler_data, const char *handler_name)
{
	irq_set_chip_and_handler_name(virq, chip, handler, handler_name);
	irq_set_chip_data(virq, chip_data);
	irq_set_handler_data(virq, handler_data);
}

static int irq_domain_alloc_irqs_locked(struct irq_domain *domain, int irq_base,
					unsigned int nr_irqs, int node, void *arg,
					bool realloc, const struct irq_affinity_desc *affinity)
{
	return -EINVAL;
}

static void irq_domain_check_hierarchy(struct irq_domain *domain) { }
static void irq_domain_free_one_irq(struct irq_domain *domain, unsigned int virq) { }

#endif	/* CONFIG_IRQ_DOMAIN_HIERARCHY */

#ifdef CONFIG_GENERIC_IRQ_DEBUGFS
#include "internals.h"

static struct dentry *domain_dir;

static const struct irq_bit_descr irqdomain_flags[] = {
	BIT_MASK_DESCR(IRQ_DOMAIN_FLAG_HIERARCHY),
	BIT_MASK_DESCR(IRQ_DOMAIN_NAME_ALLOCATED),
	BIT_MASK_DESCR(IRQ_DOMAIN_FLAG_IPI_PER_CPU),
	BIT_MASK_DESCR(IRQ_DOMAIN_FLAG_IPI_SINGLE),
	BIT_MASK_DESCR(IRQ_DOMAIN_FLAG_MSI),
	BIT_MASK_DESCR(IRQ_DOMAIN_FLAG_ISOLATED_MSI),
	BIT_MASK_DESCR(IRQ_DOMAIN_FLAG_NO_MAP),
	BIT_MASK_DESCR(IRQ_DOMAIN_FLAG_MSI_PARENT),
	BIT_MASK_DESCR(IRQ_DOMAIN_FLAG_MSI_DEVICE),
	BIT_MASK_DESCR(IRQ_DOMAIN_FLAG_NONCORE),
};

static void irq_domain_debug_show_one(struct seq_file *m, struct irq_domain *d, int ind)
{
	seq_printf(m, "%*sname:   %s\n", ind, "", d->name);
	seq_printf(m, "%*ssize:   %u\n", ind + 1, "", d->revmap_size);
	seq_printf(m, "%*smapped: %u\n", ind + 1, "", d->mapcount);
	seq_printf(m, "%*sflags:  0x%08x\n", ind +1 , "", d->flags);
	irq_debug_show_bits(m, ind, d->flags, irqdomain_flags, ARRAY_SIZE(irqdomain_flags));
	if (d->ops && d->ops->debug_show)
		d->ops->debug_show(m, d, NULL, ind + 1);
#ifdef	CONFIG_IRQ_DOMAIN_HIERARCHY
	if (!d->parent)
		return;
	seq_printf(m, "%*sparent: %s\n", ind + 1, "", d->parent->name);
	irq_domain_debug_show_one(m, d->parent, ind + 4);
#endif
}

static int irq_domain_debug_show(struct seq_file *m, void *p)
{
	struct irq_domain *d = m->private;

	/* Default domain? Might be NULL */
	if (!d) {
		if (!irq_default_domain)
			return 0;
		d = irq_default_domain;
	}
	irq_domain_debug_show_one(m, d, 0);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(irq_domain_debug);

static void debugfs_add_domain_dir(struct irq_domain *d)
{
	if (!d->name || !domain_dir)
		return;
	debugfs_create_file(d->name, 0444, domain_dir, d,
			    &irq_domain_debug_fops);
}

static void debugfs_remove_domain_dir(struct irq_domain *d)
{
	debugfs_lookup_and_remove(d->name, domain_dir);
}

void __init irq_domain_debugfs_init(struct dentry *root)
{
	struct irq_domain *d;

	domain_dir = debugfs_create_dir("domains", root);

	debugfs_create_file("default", 0444, domain_dir, NULL,
			    &irq_domain_debug_fops);
	mutex_lock(&irq_domain_mutex);
	list_for_each_entry(d, &irq_domain_list, link)
		debugfs_add_domain_dir(d);
	mutex_unlock(&irq_domain_mutex);
}
#endif
