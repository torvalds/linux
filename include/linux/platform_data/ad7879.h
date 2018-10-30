/* SPDX-License-Identifier: GPL-2.0 */
/* linux/platform_data/ad7879.h */

/* Touchscreen characteristics vary between boards and models.  The
 * platform_data for the device's "struct device" holds this information.
 *
 * It's OK if the min/max values are zero.
 */
struct ad7879_platform_data {
	u16	model;			/* 7879 */
	u16	x_plate_ohms;
	u16	x_min, x_max;
	u16	y_min, y_max;
	u16	pressure_min, pressure_max;

	bool	swap_xy;		/* swap x and y axes */

	/* [0..255] 0=OFF Starts at 1=550us and goes
	 * all the way to 9.440ms in steps of 35us.
	 */
	u8	pen_down_acc_interval;
	/* [0..15] Starts at 0=128us and goes all the
	 * way to 4.096ms in steps of 128us.
	 */
	u8	first_conversion_delay;
	/* [0..3] 0 = 2us, 1 = 4us, 2 = 8us, 3 = 16us */
	u8	acquisition_time;
	/* [0..3] Average X middle samples 0 = 2, 1 = 4, 2 = 8, 3 = 16 */
	u8	averaging;
	/* [0..3] Perform X measurements 0 = OFF,
	 * 1 = 4, 2 = 8, 3 = 16 (median > averaging)
	 */
	u8	median;
	/* 1 = AUX/VBAT/GPIO export GPIO to gpiolib
	 * requires CONFIG_GPIOLIB
	 */
	bool	gpio_export;
	/* identifies the first GPIO number handled by this chip;
	 * or, if negative, requests dynamic ID allocation.
	 */
	s32	gpio_base;
};
