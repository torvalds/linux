#ifndef __LINUX_PCF857X_H
#define __LINUX_PCF857X_H

/**
 * struct pcf857x_platform_data - data to set up pcf857x driver
 * @gpio_base: number of the chip's first GPIO
 * @n_latch: optional bit-inverse of initial register value; if
 *	you leave this initialized to zero the driver will act
 *	like the chip was just reset
 * @setup: optional callback issued once the GPIOs are valid
 * @teardown: optional callback issued before the GPIOs are invalidated
 * @context: optional parameter passed to setup() and teardown()
 * @irq: optional interrupt number
 *
 * In addition to the I2C_BOARD_INFO() state appropriate to each chip,
 * the i2c_board_info used with the pcf875x driver must provide its
 * platform_data (pointer to one of these structures) with at least
 * the gpio_base value initialized.
 *
 * The @setup callback may be used with the kind of board-specific glue
 * which hands the (now-valid) GPIOs to other drivers, or which puts
 * devices in their initial states using these GPIOs.
 *
 * These GPIO chips are only "quasi-bidirectional"; read the chip specs
 * to understand the behavior.  They don't have separate registers to
 * record which pins are used for input or output, record which output
 * values are driven, or provide access to input values.  That must be
 * inferred by reading the chip's value and knowing the last value written
 * to it.  If you leave n_latch initialized to zero, that last written
 * value is presumed to be all ones (as if the chip were just reset).
 */
struct pcf857x_platform_data {
	unsigned	gpio_base;
	unsigned	n_latch;

	int		(*setup)(struct i2c_client *client,
					int gpio, unsigned ngpio,
					void *context);
	int		(*teardown)(struct i2c_client *client,
					int gpio, unsigned ngpio,
					void *context);
	void		*context;

	int		irq;
};

#endif /* __LINUX_PCF857X_H */
