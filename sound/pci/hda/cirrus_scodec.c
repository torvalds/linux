// SPDX-License-Identifier: GPL-2.0-only
//
// Common code for Cirrus side-codecs.
//
// Copyright (C) 2021, 2023 Cirrus Logic, Inc. and
//               Cirrus Logic International Semiconductor Ltd.

#include <linux/dev_printk.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>

#include "cirrus_scodec.h"

int cirrus_scodec_get_speaker_id(struct device *dev, int amp_index,
				 int num_amps, int fixed_gpio_id)
{
	struct gpio_desc *speaker_id_desc;
	int speaker_id = -ENOENT;

	if (fixed_gpio_id >= 0) {
		dev_dbg(dev, "Found Fixed Speaker ID GPIO (index = %d)\n", fixed_gpio_id);
		speaker_id_desc = gpiod_get_index(dev, NULL, fixed_gpio_id, GPIOD_IN);
		if (IS_ERR(speaker_id_desc)) {
			speaker_id = PTR_ERR(speaker_id_desc);
			return speaker_id;
		}
		speaker_id = gpiod_get_value_cansleep(speaker_id_desc);
		gpiod_put(speaker_id_desc);
	} else {
		int base_index;
		int gpios_per_amp;
		int count;
		int tmp;
		int i;

		count = gpiod_count(dev, "spk-id");
		if (count > 0) {
			speaker_id = 0;
			gpios_per_amp = count / num_amps;
			base_index = gpios_per_amp * amp_index;

			if (count % num_amps)
				return -EINVAL;

			dev_dbg(dev, "Found %d Speaker ID GPIOs per Amp\n", gpios_per_amp);

			for (i = 0; i < gpios_per_amp; i++) {
				speaker_id_desc = gpiod_get_index(dev, "spk-id", i + base_index,
								  GPIOD_IN);
				if (IS_ERR(speaker_id_desc)) {
					speaker_id = PTR_ERR(speaker_id_desc);
					break;
				}
				tmp = gpiod_get_value_cansleep(speaker_id_desc);
				gpiod_put(speaker_id_desc);
				if (tmp < 0) {
					speaker_id = tmp;
					break;
				}
				speaker_id |= tmp << i;
			}
		}
	}

	dev_dbg(dev, "Speaker ID = %d\n", speaker_id);

	return speaker_id;
}
EXPORT_SYMBOL_NS_GPL(cirrus_scodec_get_speaker_id, SND_HDA_CIRRUS_SCODEC);

MODULE_DESCRIPTION("HDA Cirrus side-codec library");
MODULE_AUTHOR("Richard Fitzgerald <rf@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
