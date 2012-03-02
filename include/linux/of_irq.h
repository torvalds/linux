#ifndef __OF_IRQ_H
#define __OF_IRQ_H

#if defined(CONFIG_OF)
struct of_irq;
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/ioport.h>
#include <linux/of.h>

/*
 * irq_of_parse_and_map() is used ba all OF enabled platforms; but SPARC
 * implements it differently.  However, the prototype is the same for all,
 * so declare it here regardless of the CONFIG_OF_IRQ setting.
 */
extern unsigned int irq_of_parse_and_map(struct device_node *node, int index);

#if defined(CONFIG_OF_IRQ)
/**
 * of_irq - container for device_node/irq_specifier pair for an irq controller
 * @controller: pointer to interrupt controller device tree node
 * @size: size of interrupt specifier
 * @specifier: array of cells @size long specifing the specific interrupt
 *
 * This structure is returned when an interrupt is mapped. The controller
 * field needs to be put() after use
 */
#define OF_MAX_IRQ_SPEC		4 /* We handle specifiers of at most 4 cells */
struct of_irq {
	struct device_node *controller; /* Interrupt controller node */
	u32 size; /* Specifier size */
	u32 specifier[OF_MAX_IRQ_SPEC]; /* Specifier copy */
};

typedef int (*of_irq_init_cb_t)(struct device_node *, struct device_node *);

/*
 * Workarounds only applied to 32bit powermac machines
 */
#define OF_IMAP_OLDWORLD_MAC	0x00000001
#define OF_IMAP_NO_PHANDLE	0x00000002

#if defined(CONFIG_PPC32) && defined(CONFIG_PPC_PMAC)
extern unsigned int of_irq_workarounds;
extern struct device_node *of_irq_dflt_pic;
extern int of_irq_map_oldworld(struct device_node *device, int index,
			       struct of_irq *out_irq);
#else /* CONFIG_PPC32 && CONFIG_PPC_PMAC */
#define of_irq_workarounds (0)
#define of_irq_dflt_pic (NULL)
static inline int of_irq_map_oldworld(struct device_node *device, int index,
				      struct of_irq *out_irq)
{
	return -EINVAL;
}
#endif /* CONFIG_PPC32 && CONFIG_PPC_PMAC */


extern int of_irq_map_raw(struct device_node *parent, const u32 *intspec,
			  u32 ointsize, const u32 *addr,
			  struct of_irq *out_irq);
extern int of_irq_map_one(struct device_node *device, int index,
			  struct of_irq *out_irq);
extern unsigned int irq_create_of_mapping(struct device_node *controller,
					  const u32 *intspec,
					  unsigned int intsize);
extern int of_irq_to_resource(struct device_node *dev, int index,
			      struct resource *r);
extern int of_irq_count(struct device_node *dev);
extern int of_irq_to_resource_table(struct device_node *dev,
		struct resource *res, int nr_irqs);
extern struct device_node *of_irq_find_parent(struct device_node *child);

extern void of_irq_init(const struct of_device_id *matches);

#endif /* CONFIG_OF_IRQ */
#endif /* CONFIG_OF */
#endif /* __OF_IRQ_H */
