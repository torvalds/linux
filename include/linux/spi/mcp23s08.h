
/* FIXME driver should be able to handle IRQs...  */

struct mcp23s08_chip_info {
	bool		is_present;	/* true if populated */
	unsigned	pullups;	/* BIT(x) means enable pullup x */
};

struct mcp23s08_platform_data {
	/* For mcp23s08, up to 4 slaves (numbered 0..3) can share one SPI
	 * chipselect, each providing 1 gpio_chip instance with 8 gpios.
	 * For mpc23s17, up to 8 slaves (numbered 0..7) can share one SPI
	 * chipselect, each providing 1 gpio_chip (port A + port B) with
	 * 16 gpios.
	 */
	struct mcp23s08_chip_info	chip[8];

	/* "base" is the number of the first GPIO.  Dynamic assignment is
	 * not currently supported, and even if there are gaps in chip
	 * addressing the GPIO numbers are sequential .. so for example
	 * if only slaves 0 and 3 are present, their GPIOs range from
	 * base to base+15 (or base+31 for s17 variant).
	 */
	unsigned	base;
	/* Marks the device as a interrupt controller.
	 * NOTE: The interrupt functionality is only supported for i2c
	 * versions of the chips. The spi chips can also do the interrupts,
	 * but this is not supported by the linux driver yet.
	 */
	bool		irq_controller;

	/* Sets the mirror flag in the IOCON register. Devices
	 * with two interrupt outputs (these are the devices ending with 17 and
	 * those that have 16 IOs) have two IO banks: IO 0-7 form bank 1 and
	 * IO 8-15 are bank 2. These chips have two different interrupt outputs:
	 * One for bank 1 and another for bank 2. If irq-mirror is set, both
	 * interrupts are generated regardless of the bank that an input change
	 * occurred on. If it is not set, the interrupt are only generated for
	 * the bank they belong to.
	 * On devices with only one interrupt output this property is useless.
	 */
	bool		mirror;
};
