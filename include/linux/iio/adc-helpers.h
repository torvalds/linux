/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * The industrial I/O ADC firmware property parsing helpers
 *
 * Copyright (c) 2025 Matti Vaittinen <mazziesaccount@gmail.com>
 */

#ifndef _INDUSTRIAL_IO_ADC_HELPERS_H_
#define _INDUSTRIAL_IO_ADC_HELPERS_H_

#include <linux/property.h>

struct device;
struct iio_chan_spec;

static inline int iio_adc_device_num_channels(struct device *dev)
{
	return device_get_named_child_node_count(dev, "channel");
}

int devm_iio_adc_device_alloc_chaninfo_se(struct device *dev,
					  const struct iio_chan_spec *template,
					  int max_chan_id,
					  struct iio_chan_spec **cs);

#endif /* _INDUSTRIAL_IO_ADC_HELPERS_H_ */
