#ifndef	_PIXCIR_I2C_TS_H
#define	_PIXCIR_I2C_TS_H

/*
 * Register map
 */
#define PIXCIR_REG_POWER_MODE	51
#define PIXCIR_REG_INT_MODE	52

/*
 * Power modes:
 * active: max scan speed
 * idle: lower scan speed with automatic transition to active on touch
 * halt: datasheet says sleep but this is more like halt as the chip
 *       clocks are cut and it can only be brought out of this mode
 *	 using the RESET pin.
 */
enum pixcir_power_mode {
	PIXCIR_POWER_ACTIVE,
	PIXCIR_POWER_IDLE,
	PIXCIR_POWER_HALT,
};

#define PIXCIR_POWER_MODE_MASK	0x03
#define PIXCIR_POWER_ALLOW_IDLE (1UL << 2)

/*
 * Interrupt modes:
 * periodical: interrupt is asserted periodicaly
 * diff coordinates: interrupt is asserted when coordinates change
 * level on touch: interrupt level asserted during touch
 * pulse on touch: interrupt pulse asserted druing touch
 *
 */
enum pixcir_int_mode {
	PIXCIR_INT_PERIODICAL,
	PIXCIR_INT_DIFF_COORD,
	PIXCIR_INT_LEVEL_TOUCH,
	PIXCIR_INT_PULSE_TOUCH,
};

#define PIXCIR_INT_MODE_MASK	0x03
#define PIXCIR_INT_ENABLE	(1UL << 3)
#define PIXCIR_INT_POL_HIGH	(1UL << 2)

/**
 * struct pixcir_irc_chip_data - chip related data
 * @max_fingers:	Max number of fingers reported simultaneously by h/w
 * @has_hw_ids:		Hardware supports finger tracking IDs
 *
 */
struct pixcir_i2c_chip_data {
	u8 max_fingers;
	bool has_hw_ids;
};

struct pixcir_ts_platform_data {
	int x_max;
	int y_max;
	struct pixcir_i2c_chip_data chip;
};

#endif
