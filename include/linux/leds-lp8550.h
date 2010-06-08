/*
 * Copyright (C) 2010 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#ifndef _LINUX_LED_LD_LP8550_H__
#define _LINUX_LED_LD_LP8550_H__

#ifdef __KERNEL__

#define LD_LP8550_LED_DEV "lcd-backlight"
#define LD_LP8550_NAME "lp8550_led"

struct lp8550_eeprom_data {
	u8 eeprom_data;
};

struct lp8550_platform_data {
	u8 power_up_brightness;
	u8 dev_ctrl_config;
	u8 brightness_control;
	u8 dev_id;
	u8 direct_ctrl;
	struct lp8550_eeprom_data *eeprom_table;
	int eeprom_tbl_sz;
};

#endif	/* __KERNEL__ */
#endif	/* _LINUX_LED_LD_LP8550_H__ */
