// SPDX-License-Identifier: GPL-2.0+
#ifndef __LINUX_GPIO_PROPERTY_H
#define __LINUX_GPIO_PROPERTY_H

#include <linux/property.h>

struct software_node;

#define PROPERTY_ENTRY_GPIO(_name_, _chip_node_, _idx_, _flags_) \
	PROPERTY_ENTRY_REF(_name_, _chip_node_, _idx_, _flags_)

extern const struct software_node swnode_gpio_undefined;

#endif /* __LINUX_GPIO_PROPERTY_H */
