/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018 Axentia Technologies AB
 */

#ifndef __IIO_RESCALE_H__
#define __IIO_RESCALE_H__

#include <linux/types.h>
#include <linux/iio/iio.h>

struct device;
struct rescale;

struct rescale_cfg {
	enum iio_chan_type type;
	int (*props)(struct device *dev, struct rescale *rescale);
};

struct rescale {
	const struct rescale_cfg *cfg;
	struct iio_channel *source;
	struct iio_chan_spec chan;
	struct iio_chan_spec_ext_info *ext_info;
	bool chan_processed;
	s32 numerator;
	s32 denominator;
};

int rescale_process_scale(struct rescale *rescale, int scale_type,
			  int *val, int *val2);
#endif /* __IIO_RESCALE_H__ */
