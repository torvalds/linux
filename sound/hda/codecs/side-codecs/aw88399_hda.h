/* SPDX-License-Identifier: GPL-2.0-only
 *
 * AW88399 HDA side codec driver
 */

#ifndef __AW88399_HDA_H__
#define __AW88399_HDA_H__

#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <sound/aw88399.h>

struct aw88399;
struct aw_device;

struct aw88399_hda {
	struct device *dev;
	struct regmap *regmap;
	struct gpio_desc *reset_gpio;
	struct aw_device *aw_dev;
	struct aw88399 *core;
	bool bsts_unreliable;

	const char *acpi_subsystem_id;
	int index;
	int channel;

	bool playing;
};

int aw88399_hda_probe(struct device *dev, struct regmap *regmap);
void aw88399_hda_remove(struct device *dev);

extern const struct dev_pm_ops aw88399_hda_pm_ops;

#endif /* __AW88399_HDA_H__ */
