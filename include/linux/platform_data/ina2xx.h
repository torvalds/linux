/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for Texas Instruments INA219, INA226 power monitor chips
 *
 * Copyright (C) 2012 Lothar Felten <lothar.felten@gmail.com>
 *
 * For further information, see the Documentation/hwmon/ina2xx.rst file.
 */

/**
 * struct ina2xx_platform_data - ina2xx info
 * @shunt_uohms		shunt resistance in microohms
 */
struct ina2xx_platform_data {
	long shunt_uohms;
};
