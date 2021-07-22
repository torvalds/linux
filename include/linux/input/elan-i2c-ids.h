/*
 * Elan I2C/SMBus Touchpad device whitelist
 *
 * Copyright (c) 2013 ELAN Microelectronics Corp.
 *
 * Author: æ維 (Duson Lin) <dusonlin@emc.com.tw>
 * Author: KT Liao <kt.liao@emc.com.tw>
 * Version: 1.6.3
 *
 * Based on cyapa driver:
 * copyright (c) 2011-2012 Cypress Semiconductor, Inc.
 * copyright (c) 2011-2012 Google, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Trademarks are the property of their respective owners.
 */

#ifndef __ELAN_I2C_IDS_H
#define __ELAN_I2C_IDS_H

#include <linux/mod_devicetable.h>

static const struct acpi_device_id elan_acpi_id[] = {
	{ "ELAN0000", 0 },
	{ "ELAN0100", 0 },
	{ "ELAN0600", 0 },
	{ "ELAN0601", 0 },
	{ "ELAN0602", 0 },
	{ "ELAN0603", 0 },
	{ "ELAN0604", 0 },
	{ "ELAN0605", 0 },
	{ "ELAN0606", 0 },
	{ "ELAN0607", 0 },
	{ "ELAN0608", 0 },
	{ "ELAN0609", 0 },
	{ "ELAN060B", 0 },
	{ "ELAN060C", 0 },
	{ "ELAN060F", 0 },
	{ "ELAN0610", 0 },
	{ "ELAN0611", 0 },
	{ "ELAN0612", 0 },
	{ "ELAN0615", 0 },
	{ "ELAN0616", 0 },
	{ "ELAN0617", 0 },
	{ "ELAN0618", 0 },
	{ "ELAN0619", 0 },
	{ "ELAN061A", 0 },
/*	{ "ELAN061B", 0 }, not working on the Lenovo Legion Y7000 */
	{ "ELAN061C", 0 },
	{ "ELAN061D", 0 },
	{ "ELAN061E", 0 },
	{ "ELAN061F", 0 },
	{ "ELAN0620", 0 },
	{ "ELAN0621", 0 },
	{ "ELAN0622", 0 },
	{ "ELAN0623", 0 },
	{ "ELAN0624", 0 },
	{ "ELAN0625", 0 },
	{ "ELAN0626", 0 },
	{ "ELAN0627", 0 },
	{ "ELAN0628", 0 },
	{ "ELAN0629", 0 },
	{ "ELAN062A", 0 },
	{ "ELAN062B", 0 },
	{ "ELAN062C", 0 },
	{ "ELAN062D", 0 },
	{ "ELAN062E", 0 }, /* Lenovo V340 Whiskey Lake U */
	{ "ELAN062F", 0 }, /* Lenovo V340 Comet Lake U */
	{ "ELAN0631", 0 },
	{ "ELAN0632", 0 },
	{ "ELAN0633", 0 }, /* Lenovo S145 */
	{ "ELAN0634", 0 }, /* Lenovo V340 Ice lake */
	{ "ELAN0635", 0 }, /* Lenovo V1415-IIL */
	{ "ELAN0636", 0 }, /* Lenovo V1415-Dali */
	{ "ELAN0637", 0 }, /* Lenovo V1415-IGLR */
	{ "ELAN1000", 0 },
	{ }
};

#endif /* __ELAN_I2C_IDS_H */
