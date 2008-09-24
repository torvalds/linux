
/* FIXME driver should be able to handle IRQs...  */

struct mcp23s08_chip_info {
	bool	is_present;		/* true iff populated */
	u8	pullups;		/* BIT(x) means enable pullup x */
};

struct mcp23s08_platform_data {
	/* Four slaves (numbered 0..3) can share one SPI chipselect, and
	 * will provide 8..32 GPIOs using 1..4 gpio_chip instances.
	 */
	struct mcp23s08_chip_info	chip[4];

	/* "base" is the number of the first GPIO.  Dynamic assignment is
	 * not currently supported, and even if there are gaps in chip
	 * addressing the GPIO numbers are sequential .. so for example
	 * if only slaves 0 and 3 are present, their GPIOs range from
	 * base to base+15.
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
