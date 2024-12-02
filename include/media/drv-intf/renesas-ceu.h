// SPDX-License-Identifier: GPL-2.0
/*
 * renesas-ceu.h - Renesas CEU driver interface
 *
 * Copyright 2017-2018 Jacopo Mondi <jacopo+renesas@jmondi.org>
 */

#ifndef __MEDIA_DRV_INTF_RENESAS_CEU_H__
#define __MEDIA_DRV_INTF_RENESAS_CEU_H__

#define CEU_MAX_SUBDEVS		2

struct ceu_async_subdev {
	unsigned long flags;
	unsigned char bus_width;
	unsigned char bus_shift;
	unsigned int i2c_adapter_id;
	unsigned int i2c_address;
};

struct ceu_platform_data {
	unsigned int num_subdevs;
	struct ceu_async_subdev subdevs[CEU_MAX_SUBDEVS];
};

#endif /* ___MEDIA_DRV_INTF_RENESAS_CEU_H__ */
