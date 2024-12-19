// SPDX-License-Identifier: GPL-2.0

#include <linux/device.h>

int rust_helper_devm_add_action(struct device *dev,
				void (*action)(void *),
				void *data)
{
	return devm_add_action(dev, action, data);
}
