#ifndef __LINUX_SPI_DS1305_H
#define __LINUX_SPI_DS1305_H

/*
 * One-time configuration for ds1305 and ds1306 RTC chips.
 *
 * Put a pointer to this in spi_board_info.platform_data if you want to
 * be sure that Linux (re)initializes this as needed ... after losing
 * backup power, and potentially on the first boot.
 */
struct ds1305_platform_data {

	/* Trickle charge configuration:  it's OK to leave out the MAGIC
	 * bitmask; mask in either DS1 or DS2, and then one of 2K/4k/8K.
	 */
#define DS1305_TRICKLE_MAGIC	0xa0
#define DS1305_TRICKLE_DS2	0x08	/* two diodes */
#define DS1305_TRICKLE_DS1	0x04	/* one diode */
#define DS1305_TRICKLE_2K	0x01	/* 2 KOhm resistance */
#define DS1305_TRICKLE_4K	0x02	/* 4 KOhm resistance */
#define DS1305_TRICKLE_8K	0x03	/* 8 KOhm resistance */
	u8	trickle;

	/* set only on ds1306 parts */
	bool	is_ds1306;

	/* ds1306 only:  enable 1 Hz output */
	bool	en_1hz;

	/* REVISIT:  the driver currently expects nINT0 to be wired
	 * as the alarm IRQ.  ALM1 may also need to be set up ...
	 */
};

#endif /* __LINUX_SPI_DS1305_H */
