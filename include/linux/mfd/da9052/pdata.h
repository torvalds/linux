/*
 * Platform data declarations for DA9052 PMICs.
 *
 * Copyright(c) 2011 Dialog Semiconductor Ltd.
 *
 * Author: David Dajun Chen <dchen@diasemi.com>
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef __MFD_DA9052_PDATA_H__
#define __MFD_DA9052_PDATA_H__

#define DA9052_MAX_REGULATORS	14

struct da9052;

struct da9052_pdata {
	struct led_platform_data *pled;
	int (*init) (struct da9052 *da9052);
	int irq_base;
	int gpio_base;
	int use_for_apm;
	struct regulator_init_data *regulators[DA9052_MAX_REGULATORS];
};

#endif
