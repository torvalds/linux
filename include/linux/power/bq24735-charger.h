/*
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __CHARGER_BQ24735_H_
#define __CHARGER_BQ24735_H_

#include <linux/types.h>
#include <linux/power_supply.h>

struct bq24735_platform {
	uint32_t charge_current;
	uint32_t charge_voltage;
	uint32_t input_current;

	const char *name;

	int status_gpio;
	int status_gpio_active_low;
	bool status_gpio_valid;

	bool ext_control;

	char **supplied_to;
	size_t num_supplicants;
};

#endif /* __CHARGER_BQ24735_H_ */
