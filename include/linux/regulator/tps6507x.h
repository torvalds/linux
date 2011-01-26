/*
 * tps6507x.h  --  Voltage regulation for the Texas Instruments TPS6507X
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef REGULATOR_TPS6507X
#define REGULATOR_TPS6507X

/**
 * tps6507x_reg_platform_data - platform data for tps6507x
 * @defdcdc_default: Defines whether DCDC high or the low register controls
 *	output voltage by default. Valid for DCDC2 and DCDC3 outputs only.
 */
struct tps6507x_reg_platform_data {
	bool defdcdc_default;
};

#endif
