/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * bd6107.h - Rohm BD6107 LEDs Driver
 */
#ifndef __BD6107_H__
#define __BD6107_H__

struct device;

struct bd6107_platform_data {
	struct device *fbdev;
	int reset;			/* Reset GPIO */
	unsigned int def_value;
};

#endif
