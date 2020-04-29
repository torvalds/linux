/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AT86RF230/RF231 driver
 *
 * Copyright (C) 2009-2012 Siemens AG
 *
 * Written by:
 * Dmitry Eremin-Solenikov <dmitry.baryshkov@siemens.com>
 */
#ifndef AT86RF230_H
#define AT86RF230_H

struct at86rf230_platform_data {
	int rstn;
	int slp_tr;
	int dig2;
	u8 xtal_trim;
};

#endif
