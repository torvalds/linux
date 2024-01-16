/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Platform data for NXP SC18IS602/603
 *
 * Copyright (C) 2012 Guenter Roeck <linux@roeck-us.net>
 *
 * For further information, see the Documentation/spi/spi-sc18is602.rst file.
 */

/**
 * struct sc18is602_platform_data - sc18is602 info
 * @clock_frequency		SC18IS603 oscillator frequency
 */
struct sc18is602_platform_data {
	u32 clock_frequency;
};
