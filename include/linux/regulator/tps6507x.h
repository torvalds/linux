/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * tps6507x.h  --  Voltage regulation for the Texas Instruments TPS6507X
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
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
