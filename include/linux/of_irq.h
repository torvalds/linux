#ifndef __OF_IRQ_H
#define __OF_IRQ_H

#if defined(CONFIG_OF)
struct of_irq;
#include <linux/types.h>
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

extern int of_irq_map_one(struct device_node *device, int index,
			  struct of_irq *out_irq);
extern unsigned int irq_create_of_mapping(struct device_node *controller,
					  const u32 *intspec,
					  unsigned int intsize);

#endif /* CONFIG_OF_IRQ */
#endif /* CONFIG_OF */
#endif /* __OF_IRQ_H */
