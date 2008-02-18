
/* FIXME driver should be able to handle all four slaves that
 * can be hooked up to each chipselect, as well as IRQs...
 */

struct mcp23s08_platform_data {
	/* four slaves can share one SPI chipselect */
	u8		slave;

	/* number assigned to the first GPIO */
	unsigned	base;

	/* pins with pullups */
	u8		pullups;

	void		*context;	/* param to setup/teardown */

	int		(*setup)(struct spi_device *spi,
					int gpio, unsigned ngpio,
					void *context);
	int		(*teardown)(struct spi_device *spi,
					int gpio, unsigned ngpio,
					void *context);
};
