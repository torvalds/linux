/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Device driver for monitoring ambient light intensity (lux)
 * and proximity (prox) within the TAOS TSL2772 family of devices.
 *
 * Copyright (c) 2012, TAOS Corporation.
 * Copyright (c) 2017-2018 Brian Masney <masneyb@onstation.org>
 */

#ifndef __TSL2772_H
#define __TSL2772_H

struct tsl2772_lux {
	unsigned int ch0;
	unsigned int ch1;
};

/* Max number of segments allowable in LUX table */
#define TSL2772_MAX_LUX_TABLE_SIZE		6
/* The default LUX tables all have 3 elements.  */
#define TSL2772_DEF_LUX_TABLE_SZ		3
#define TSL2772_DEFAULT_TABLE_BYTES (sizeof(struct tsl2772_lux) * \
				     TSL2772_DEF_LUX_TABLE_SZ)

/* Proximity diode to use */
#define TSL2772_DIODE0                  0x01
#define TSL2772_DIODE1                  0x02
#define TSL2772_DIODE_BOTH              0x03

/* LED Power */
#define TSL2772_100_mA                  0x00
#define TSL2772_50_mA                   0x01
#define TSL2772_25_mA                   0x02
#define TSL2772_13_mA                   0x03

/**
 * struct tsl2772_settings - Settings for the tsl2772 driver
 *  @als_time:              Integration time of the ALS channel ADCs in 2.73 ms
 *                          increments. Total integration time is
 *                          (256 - als_time) * 2.73.
 *  @als_gain:              Index into the tsl2772_als_gain array.
 *  @als_gain_trim:         Default gain trim to account for aperture effects.
 *  @wait_time:             Time between proximity and ALS cycles in 2.73
 *                          periods.
 *  @prox_time:             Integration time of the proximity ADC in 2.73 ms
 *                          increments. Total integration time is
 *                          (256 - prx_time) * 2.73.
 *  @prox_gain:             Index into the tsl2772_prx_gain array.
 *  @als_prox_config:       The value of the ALS / Proximity configuration
 *                          register.
 *  @als_cal_target:        Known external ALS reading for calibration.
 *  @als_persistence:       H/W Filters, Number of 'out of limits' ALS readings.
 *  @als_interrupt_en:      Enable/Disable ALS interrupts
 *  @als_thresh_low:        CH0 'low' count to trigger interrupt.
 *  @als_thresh_high:       CH0 'high' count to trigger interrupt.
 *  @prox_persistence:      H/W Filters, Number of 'out of limits' proximity
 *                          readings.
 *  @prox_interrupt_en:     Enable/Disable proximity interrupts.
 *  @prox_thres_low:        Low threshold proximity detection.
 *  @prox_thres_high:       High threshold proximity detection.
 *  @prox_pulse_count:      Number if proximity emitter pulses.
 *  @prox_max_samples_cal:  The number of samples that are taken when performing
 *                          a proximity calibration.
 *  @prox_diode             Which diode(s) to use for driving the external
 *                          LED(s) for proximity sensing.
 *  @prox_power             The amount of power to use for the external LED(s).
 */
struct tsl2772_settings {
	int als_time;
	int als_gain;
	int als_gain_trim;
	int wait_time;
	int prox_time;
	int prox_gain;
	int als_prox_config;
	int als_cal_target;
	u8 als_persistence;
	bool als_interrupt_en;
	int als_thresh_low;
	int als_thresh_high;
	u8 prox_persistence;
	bool prox_interrupt_en;
	int prox_thres_low;
	int prox_thres_high;
	int prox_pulse_count;
	int prox_max_samples_cal;
	int prox_diode;
	int prox_power;
};

/**
 * struct tsl2772_platform_data - Platform callback, glass and defaults
 * @platform_lux_table:        Device specific glass coefficents
 * @platform_default_settings: Device specific power on defaults
 */
struct tsl2772_platform_data {
	struct tsl2772_lux platform_lux_table[TSL2772_MAX_LUX_TABLE_SIZE];
	struct tsl2772_settings *platform_default_settings;
};

#endif /* __TSL2772_H */
