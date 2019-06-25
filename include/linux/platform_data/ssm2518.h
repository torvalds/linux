/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SSM2518 amplifier audio driver
 *
 * Copyright 2013 Analog Devices Inc.
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 */

#ifndef __LINUX_PLATFORM_DATA_SSM2518_H__
#define __LINUX_PLATFORM_DATA_SSM2518_H__

/**
 * struct ssm2518_platform_data - Platform data for the ssm2518 driver
 * @enable_gpio: GPIO connected to the nSD pin. Set to -1 if the nSD pin is
 *            hardwired.
 */
struct ssm2518_platform_data {
	int enable_gpio;
};

#endif
