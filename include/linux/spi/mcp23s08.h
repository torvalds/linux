
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

	void		*context;	/* param to setup/teardown */

	int		(*setup)(struct spi_device *spi,
					int gpio, unsigned ngpio,
					void *context);
	int		(*teardown)(struct spi_device *spi,
					int gpio, unsigned ngpio,
					void *context);
};
