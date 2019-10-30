/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LTE_H
#define __LTE_H

#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/miscdevice.h>

struct lte_data {
	struct device *dev;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *power_gpio;
	struct gpio_desc *vbat_gpio;
};

#endif /* __LTE_H */
