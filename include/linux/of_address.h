#ifndef __OF_ADDRESS_H
#define __OF_ADDRESS_H
#include <linux/ioport.h>
#include <linux/of.h>

extern void __iomem *of_iomap(struct device_node *device, int index);

#endif /* __OF_ADDRESS_H */

