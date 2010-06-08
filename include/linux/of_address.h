#ifndef __OF_ADDRESS_H
#define __OF_ADDRESS_H
#include <linux/ioport.h>
#include <linux/of.h>

extern u64 of_translate_address(struct device_node *np, const u32 *addr);
extern int of_address_to_resource(struct device_node *dev, int index,
				  struct resource *r);
extern void __iomem *of_iomap(struct device_node *device, int index);

#endif /* __OF_ADDRESS_H */

