/*
 * rfkill-regulator.c - Regulator consumer driver for rfkill
 *
 * Copyright (C) 2009  Guiming Zhuo <gmzhuo@gmail.com>
 * Copyright (C) 2011  Antonio Ospite <ospite@studenti.unina.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __LINUX_RFKILL_REGULATOR_H
#define __LINUX_RFKILL_REGULATOR_H

/*
 * Use "vrfkill" as supply id when declaring the regulator consumer:
 *
 * static struct regulator_consumer_supply pcap_regulator_V6_consumers [] = {
 * 	{ .dev_name = "rfkill-regulator.0", .supply = "vrfkill" },
 * };
 *
 * If you have several regulator driven rfkill, you can append a numerical id to
 * .dev_name as done above, and use the same id when declaring the platform
 * device:
 *
 * static struct rfkill_regulator_platform_data ezx_rfkill_bt_data = {
 * 	.name  = "ezx-bluetooth",
 * 	.type  = RFKILL_TYPE_BLUETOOTH,
 * };
 *
 * static struct platform_device a910_rfkill = {
 * 	.name  = "rfkill-regulator",
 * 	.id    = 0,
 * 	.dev   = {
 * 		.platform_data = &ezx_rfkill_bt_data,
 * 	},
 * };
 */

#include <linux/rfkill.h>

struct rfkill_regulator_platform_data {
	char *name;             /* the name for the rfkill switch */
	enum rfkill_type type;  /* the type as specified in rfkill.h */
};

#endif /* __LINUX_RFKILL_REGULATOR_H */
