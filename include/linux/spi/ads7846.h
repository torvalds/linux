/* linux/spi/ads7846.h */

/* Touchscreen characteristics vary between boards and models.  The
 * platform_data for the device's "struct device" holds this information.
 *
 * It's OK if the min/max values are zero.
 */
struct ads7846_platform_data {
	u16	model;			/* 7843, 7845, 7846. */
	u16	vref_delay_usecs;	/* 0 for external vref; etc */
	u16	x_plate_ohms;
	u16	y_plate_ohms;

	u16	x_min, x_max;
	u16	y_min, y_max;
	u16	pressure_min, pressure_max;

	u16	debounce_max;		/* max number of additional readings
					 * per sample */
	u16	debounce_tol;		/* tolerance used for filtering */
	u16	debounce_rep;		/* additional consecutive good readings
					 * required after the first two */
	int	(*get_pendown_state)(void);
};

