/*
 * Definitions for TI EMIF device platform data
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 *
 * Aneesh V <aneesh@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __EMIF_PLAT_H
#define __EMIF_PLAT_H

/* Low power modes - EMIF_PWR_MGMT_CTRL */
#define EMIF_LP_MODE_DISABLE				0
#define EMIF_LP_MODE_CLOCK_STOP				1
#define EMIF_LP_MODE_SELF_REFRESH			2
#define EMIF_LP_MODE_PWR_DN				4

/* Hardware capabilities */
#define	EMIF_HW_CAPS_LL_INTERFACE			0x00000001

/*
 * EMIF IP Revisions
 *	EMIF4D  - Used in OMAP4
 *	EMIF4D5 - Used in OMAP5
 */
#define	EMIF_4D						1
#define	EMIF_4D5					2

/*
 * PHY types
 *	ATTILAPHY  - Used in OMAP4
 *	INTELLIPHY - Used in OMAP5
 */
#define	EMIF_PHY_TYPE_ATTILAPHY				1
#define	EMIF_PHY_TYPE_INTELLIPHY			2

/* Custom config requests */
#define EMIF_CUSTOM_CONFIG_LPMODE			0x00000001
#define EMIF_CUSTOM_CONFIG_TEMP_ALERT_POLL_INTERVAL	0x00000002
#define EMIF_CUSTOM_CONFIG_EXTENDED_TEMP_PART		0x00000004

#ifndef __ASSEMBLY__
/**
 * struct ddr_device_info - All information about the DDR device except AC
 *		timing parameters
 * @type:	Device type (LPDDR2-S4, LPDDR2-S2 etc)
 * @density:	Device density
 * @io_width:	Bus width
 * @cs1_used:	Whether there is a DDR device attached to the second
 *		chip-select(CS1) of this EMIF instance
 * @cal_resistors_per_cs: Whether there is one calibration resistor per
 *		chip-select or whether it's a single one for both
 * @manufacturer: Manufacturer name string
 */
struct ddr_device_info {
	u32	type;
	u32	density;
	u32	io_width;
	u32	cs1_used;
	u32	cal_resistors_per_cs;
	char	manufacturer[10];
};

/**
 * struct emif_custom_configs - Custom configuration parameters/policies
 *		passed from the platform layer
 * @mask:	Mask to indicate which configs are requested
 * @lpmode:	LPMODE to be used in PWR_MGMT_CTRL register
 * @lpmode_timeout_performance: Timeout before LPMODE entry when higher
 *		performance is desired at the cost of power (typically
 *		at higher OPPs)
 * @lpmode_timeout_power: Timeout before LPMODE entry when better power
 *		savings is desired and performance is not important
 *		(typically at lower loads indicated by lower OPPs)
 * @lpmode_freq_threshold: The DDR frequency threshold to identify between
 *		the above two cases:
 *		timeout = (freq >= lpmode_freq_threshold) ?
 *			lpmode_timeout_performance :
 *			lpmode_timeout_power;
 * @temp_alert_poll_interval_ms: LPDDR2 MR4 polling interval at nominal
 *		temperature(in milliseconds). When temperature is high
 *		polling is done 4 times as frequently.
 */
struct emif_custom_configs {
	u32 mask;
	u32 lpmode;
	u32 lpmode_timeout_performance;
	u32 lpmode_timeout_power;
	u32 lpmode_freq_threshold;
	u32 temp_alert_poll_interval_ms;
};

/**
 * struct emif_platform_data - Platform data passed on EMIF platform
 *				device creation. Used by the driver.
 * @hw_caps:		Hw capabilities of the EMIF IP in the respective SoC
 * @device_info:	Device info structure containing information such
 *			as type, bus width, density etc
 * @timings:		Timings information from device datasheet passed
 *			as an array of 'struct lpddr2_timings'. Can be NULL
 *			if if default timings are ok
 * @timings_arr_size:	Size of the timings array. Depends on the number
 *			of different frequencies for which timings data
 *			is provided
 * @min_tck:		Minimum value of some timing parameters in terms
 *			of number of cycles. Can be NULL if default values
 *			are ok
 * @custom_configs:	Custom configurations requested by SoC or board
 *			code and the data for them. Can be NULL if default
 *			configurations done by the driver are ok. See
 *			documentation for 'struct emif_custom_configs' for
 *			more details
 */
struct emif_platform_data {
	u32 hw_caps;
	struct ddr_device_info *device_info;
	const struct lpddr2_timings *timings;
	u32 timings_arr_size;
	const struct lpddr2_min_tck *min_tck;
	struct emif_custom_configs *custom_configs;
	u32 ip_rev;
	u32 phy_type;
};
#endif /* __ASSEMBLY__ */

#endif /* __LINUX_EMIF_H */
