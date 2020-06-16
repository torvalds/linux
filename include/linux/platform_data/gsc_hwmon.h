/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _GSC_HWMON_H
#define _GSC_HWMON_H

enum gsc_hwmon_mode {
	mode_temperature,
	mode_voltage,
	mode_voltage_raw,
	mode_max,
};

/**
 * struct gsc_hwmon_channel - configuration parameters
 * @reg:  I2C register offset
 * @mode: channel mode
 * @name: channel name
 * @mvoffset: voltage offset
 * @vdiv: voltage divider array (2 resistor values in milli-ohms)
 */
struct gsc_hwmon_channel {
	unsigned int reg;
	unsigned int mode;
	const char *name;
	unsigned int mvoffset;
	unsigned int vdiv[2];
};

/**
 * struct gsc_hwmon_platform_data - platform data for gsc_hwmon driver
 * @channels:	pointer to array of gsc_hwmon_channel structures
 *		describing channels
 * @nchannels:	number of elements in @channels array
 * @vreference: voltage reference (mV)
 * @resolution: ADC bit resolution
 * @fan_base: register base for FAN controller
 */
struct gsc_hwmon_platform_data {
	const struct gsc_hwmon_channel *channels;
	int nchannels;
	unsigned int resolution;
	unsigned int vreference;
	unsigned int fan_base;
};
#endif
