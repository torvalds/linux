/* linux/spi/ad7877.h */

/* Touchscreen characteristics vary between boards and models.  The
 * platform_data for the device's "struct device" holds this information.
 *
 * It's OK if the min/max values are zero.
 */
struct ad7877_platform_data {
	u16	model;			/* 7877 */
	u16	vref_delay_usecs;	/* 0 for external vref; etc */
	u16	x_plate_ohms;
	u16	y_plate_ohms;

	u16	x_min, x_max;
	u16	y_min, y_max;
	u16	pressure_min, pressure_max;

	u8	stopacq_polarity;	/* 1 = Active HIGH, 0 = Active LOW */
	u8	first_conversion_delay;	/* 0 = 0.5us, 1 = 128us, 2 = 1ms, 3 = 8ms */
	u8	acquisition_time;	/* 0 = 2us, 1 = 4us, 2 = 8us, 3 = 16us */
	u8	averaging;		/* 0 = 1, 1 = 4, 2 = 8, 3 = 16 */
	u8	pen_down_acc_interval;	/* 0 = covert once, 1 = every 0.5 ms,
					   2 = ever 1 ms,   3 = every 8 ms,*/
};
