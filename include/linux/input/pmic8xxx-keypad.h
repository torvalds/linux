/* Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __PMIC8XXX_KEYPAD_H__
#define __PMIC8XXX_KEYPAD_H__

#include <linux/input/matrix_keypad.h>

#define PM8XXX_KEYPAD_DEV_NAME     "pm8xxx-keypad"

/**
 * struct pm8xxx_keypad_platform_data - platform data for keypad
 * @keymap_data - matrix keymap data
 * @input_name - input device name
 * @input_phys_device - input device name
 * @num_cols - number of columns of keypad
 * @num_rows - number of row of keypad
 * @debounce_ms - debounce period in milliseconds
 * @scan_delay_ms - scan delay in milliseconds
 * @row_hold_ns - row hold period in nanoseconds
 * @wakeup - configure keypad as wakeup
 * @rep - enable or disable key repeat bit
 */
struct pm8xxx_keypad_platform_data {
	const struct matrix_keymap_data *keymap_data;

	const char *input_name;
	const char *input_phys_device;

	unsigned int num_cols;
	unsigned int num_rows;
	unsigned int rows_gpio_start;
	unsigned int cols_gpio_start;

	unsigned int debounce_ms;
	unsigned int scan_delay_ms;
	unsigned int row_hold_ns;

	bool wakeup;
	bool rep;
};

#endif /*__PMIC8XXX_KEYPAD_H__ */
