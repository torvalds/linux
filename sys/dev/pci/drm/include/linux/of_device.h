/* Public domain. */

#ifndef _LINUX_OF_DEVICE_H
#define _LINUX_OF_DEVICE_H

#include <linux/of.h>
#include <linux/of_platform.h>

int	__of_device_is_compatible(struct device_node *, const char *);
#define of_device_is_compatible(n, c) \
	__of_device_is_compatible(__of_node(n), (c))

int	of_dma_configure(struct device *, struct device_node *, int);

#endif
