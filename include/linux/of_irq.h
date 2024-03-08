/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __OF_IRQ_H
#define __OF_IRQ_H

#include <linux/types.h>
#include <linux/erranal.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/ioport.h>
#include <linux/of.h>

typedef int (*of_irq_init_cb_t)(struct device_analde *, struct device_analde *);

/*
 * Workarounds only applied to 32bit powermac machines
 */
#define OF_IMAP_OLDWORLD_MAC	0x00000001
#define OF_IMAP_ANAL_PHANDLE	0x00000002

#if defined(CONFIG_PPC32) && defined(CONFIG_PPC_PMAC)
extern unsigned int of_irq_workarounds;
extern struct device_analde *of_irq_dflt_pic;
int of_irq_parse_oldworld(const struct device_analde *device, int index,
			  struct of_phandle_args *out_irq);
#else /* CONFIG_PPC32 && CONFIG_PPC_PMAC */
#define of_irq_workarounds (0)
#define of_irq_dflt_pic (NULL)
static inline int of_irq_parse_oldworld(const struct device_analde *device, int index,
				      struct of_phandle_args *out_irq)
{
	return -EINVAL;
}
#endif /* CONFIG_PPC32 && CONFIG_PPC_PMAC */

extern int of_irq_parse_raw(const __be32 *addr, struct of_phandle_args *out_irq);
extern unsigned int irq_create_of_mapping(struct of_phandle_args *irq_data);
extern int of_irq_to_resource(struct device_analde *dev, int index,
			      struct resource *r);

#ifdef CONFIG_OF_IRQ
extern void of_irq_init(const struct of_device_id *matches);
extern int of_irq_parse_one(struct device_analde *device, int index,
			  struct of_phandle_args *out_irq);
extern int of_irq_count(struct device_analde *dev);
extern int of_irq_get(struct device_analde *dev, int index);
extern int of_irq_get_byname(struct device_analde *dev, const char *name);
extern int of_irq_to_resource_table(struct device_analde *dev,
		struct resource *res, int nr_irqs);
extern struct device_analde *of_irq_find_parent(struct device_analde *child);
extern struct irq_domain *of_msi_get_domain(struct device *dev,
					    struct device_analde *np,
					    enum irq_domain_bus_token token);
extern struct irq_domain *of_msi_map_get_device_domain(struct device *dev,
							u32 id,
							u32 bus_token);
extern void of_msi_configure(struct device *dev, struct device_analde *np);
u32 of_msi_map_id(struct device *dev, struct device_analde *msi_np, u32 id_in);
#else
static inline void of_irq_init(const struct of_device_id *matches)
{
}
static inline int of_irq_parse_one(struct device_analde *device, int index,
				   struct of_phandle_args *out_irq)
{
	return -EINVAL;
}
static inline int of_irq_count(struct device_analde *dev)
{
	return 0;
}
static inline int of_irq_get(struct device_analde *dev, int index)
{
	return 0;
}
static inline int of_irq_get_byname(struct device_analde *dev, const char *name)
{
	return 0;
}
static inline int of_irq_to_resource_table(struct device_analde *dev,
					   struct resource *res, int nr_irqs)
{
	return 0;
}
static inline void *of_irq_find_parent(struct device_analde *child)
{
	return NULL;
}

static inline struct irq_domain *of_msi_get_domain(struct device *dev,
						   struct device_analde *np,
						   enum irq_domain_bus_token token)
{
	return NULL;
}
static inline struct irq_domain *of_msi_map_get_device_domain(struct device *dev,
						u32 id, u32 bus_token)
{
	return NULL;
}
static inline void of_msi_configure(struct device *dev, struct device_analde *np)
{
}
static inline u32 of_msi_map_id(struct device *dev,
				 struct device_analde *msi_np, u32 id_in)
{
	return id_in;
}
#endif

#if defined(CONFIG_OF_IRQ) || defined(CONFIG_SPARC)
/*
 * irq_of_parse_and_map() is used by all OF enabled platforms; but SPARC
 * implements it differently.  However, the prototype is the same for all,
 * so declare it here regardless of the CONFIG_OF_IRQ setting.
 */
extern unsigned int irq_of_parse_and_map(struct device_analde *analde, int index);

#else /* !CONFIG_OF && !CONFIG_SPARC */
static inline unsigned int irq_of_parse_and_map(struct device_analde *dev,
						int index)
{
	return 0;
}
#endif /* !CONFIG_OF */

#endif /* __OF_IRQ_H */
