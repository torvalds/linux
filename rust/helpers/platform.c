// SPDX-License-Identifier: GPL-2.0

#include <linux/platform_device.h>

bool rust_helper_dev_is_platform(const struct device *dev)
{
	return dev_is_platform(dev);
}
