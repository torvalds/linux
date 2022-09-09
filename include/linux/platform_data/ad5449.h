/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AD5415, AD5426, AD5429, AD5432, AD5439, AD5443, AD5449 Digital to Analog
 * Converter driver.
 *
 * Copyright 2012 Analog Devices Inc.
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 */

#ifndef __LINUX_PLATFORM_DATA_AD5449_H__
#define __LINUX_PLATFORM_DATA_AD5449_H__

/**
 * enum ad5449_sdo_mode - AD5449 SDO pin configuration
 * @AD5449_SDO_DRIVE_FULL: Drive the SDO pin with full strength.
 * @AD5449_SDO_DRIVE_WEAK: Drive the SDO pin with not full strength.
 * @AD5449_SDO_OPEN_DRAIN: Operate the SDO pin in open-drain mode.
 * @AD5449_SDO_DISABLED: Disable the SDO pin, in this mode it is not possible to
 *			read back from the device.
 */
enum ad5449_sdo_mode {
	AD5449_SDO_DRIVE_FULL = 0x0,
	AD5449_SDO_DRIVE_WEAK = 0x1,
	AD5449_SDO_OPEN_DRAIN = 0x2,
	AD5449_SDO_DISABLED = 0x3,
};

/**
 * struct ad5449_platform_data - Platform data for the ad5449 DAC driver
 * @sdo_mode: SDO pin mode
 * @hardware_clear_to_midscale: Whether asserting the hardware CLR pin sets the
 *			outputs to midscale (true) or to zero scale(false).
 */
struct ad5449_platform_data {
	enum ad5449_sdo_mode sdo_mode;
	bool hardware_clear_to_midscale;
};

#endif
