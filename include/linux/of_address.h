#ifndef __OF_ADDRESS_H
#define __OF_ADDRESS_H
#include <linux/ioport.h>
#include <linux/of.h>

extern int __of_address_to_resource(struct device_node *dev, const u32 *addrp,
				    u64 size, unsigned int flags,
				    struct resource *r);
extern int of_address_to_resource(struct device_node *dev, int index,
				  struct resource *r);
extern void __iomem *of_iomap(struct device_node *device, int index);

#endif /* __OF_ADDRESS_H */

