/*
 * Platform data for NXP SC18IS602/603
 *
 * Copyright (C) 2012 Guenter Roeck <linux@roeck-us.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * For further information, see the Documentation/spi/sc18is602 file.
 */

/**
 * struct sc18is602_platform_data - sc18is602 info
 * @clock_frequency		SC18IS603 oscillator frequency
 */
struct sc18is602_platform_data {
	u32 clock_frequency;
};
