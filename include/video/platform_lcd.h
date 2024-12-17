/* SPDX-License-Identifier: GPL-2.0-only */
/* include/video/platform_lcd.h
 *
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Generic platform-device LCD power control interface.
*/

struct plat_lcd_data;

struct plat_lcd_data {
	int	(*probe)(struct plat_lcd_data *);
	void	(*set_power)(struct plat_lcd_data *, unsigned int power);
};
