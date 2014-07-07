#ifndef __OF_IRQ_H
#define __OF_IRQ_H

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/ioport.h>
#include <linux/of.h>

typedef int (*of_irq_init_cb_t)(struct device_node *, struct device_node *);

/*
 * Workarounds only applied to 32bit powermac machines
 */
#define OF_IMAP_OLDWORLD_MAC	0x00000001
#define OF_IMAP_NO_PHANDLE	0x00000002

#if defined(CONFIG_PPC32) && defined(CONFIG_PPC_PMAC)
extern unsigned int of_irq_workarounds;
extern struct device_node *of_irq_dflt_pic;
extern int of_irq_parse_oldworld(struct device_node *device, int index,
			       struct of_phandle_args *out_irq);
#else /* CONFIG_PPC32 && CONFIG_PPC_PMAC */
#define of_irq_workarounds (0)
#define of_irq_dflt_pic (NULL)
static inline int of_irq_parse_oldworld(struct device_node *device, int index,
				      struct of_phandle_args *out_irq)
{
	return -EINVAL;
}
#endif /* CONFIG_PPC32 && CONFIG_PPC_PMAC */

extern int of_irq_parse_raw(const __be32 *addr, struct of_phandle_args *out_irq);
extern int of_irq_parse_one(struct device_node *device, int index,
			  struct of_phandle_args *out_irq);
extern unsigned int irq_create_of_mapping(struct of_phandle_args *irq_data);
extern int of_irq_to_resource(struct device_node *dev, int index,
			      struct resource *r);
extern int of_irq_to_resource_table(struct device_node *dev,
		struct resource *res, int nr_irqs);

extern void of_irq_init(const struct of_device_id *matches);

#ifdef CONFIG_OF_IRQ
extern int of_irq_count(struct device_node *dev);
extern int of_irq_get(struct device_node *dev, int index);
extern int of_irq_get_byname(struct device_node *dev, const char *name);
#else
static inline int of_irq_count(struct device_node *dev)
{
	return 0;
}
static inline int of_irq_get(struct device_node *dev, int index)
{
	return 0;
}
static inline int of_irq_get_byname(struct device_node *dev, const char *name)
{
	return 0;
}
#endif

#if defined(CONFIG_OF)
/*
 * irq_of_parse_and_map() is used by all OF enabled platforms; but SPARC
 * implements it differently.  However, the prototype is the same for all,
 * so declare it here regardless of the CONFIG_OF_IRQ setting.
 */
extern unsigned int irq_of_parse_and_map(struct device_node *node, int index);
extern struct device_node *of_irq_find_parent(struct device_node *child);

#else /* !CONFIG_OF */
static inline unsigned int irq_of_parse_and_map(struct device_node *dev,
						int index)
{
	return 0;
}

static inline void *of_irq_find_parent(struct device_node *child)
{
	return NULL;
}
#endif /* !CONFIG_OF */

#endif /* __OF_IRQ_H */
