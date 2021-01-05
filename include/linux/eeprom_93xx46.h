/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Module: eeprom_93xx46
 * platform description for 93xx46 EEPROMs.
 */
#include <linux/gpio/consumer.h>

struct eeprom_93xx46_platform_data {
	unsigned char	flags;
#define EE_ADDR8	0x01		/*  8 bit addr. cfg */
#define EE_ADDR16	0x02		/* 16 bit addr. cfg */
#define EE_READONLY	0x08		/* forbid writing */

	unsigned int	quirks;
/* Single word read transfers only; no sequential read. */
#define EEPROM_93XX46_QUIRK_SINGLE_WORD_READ		(1 << 0)
/* Instructions such as EWEN are (addrlen + 2) in length. */
#define EEPROM_93XX46_QUIRK_INSTRUCTION_LENGTH		(1 << 1)
/* Add extra cycle after address during a read */
#define EEPROM_93XX46_QUIRK_EXTRA_READ_CYCLE		BIT(2)

	/*
	 * optional hooks to control additional logic
	 * before and after spi transfer.
	 */
	void (*prepare)(void *);
	void (*finish)(void *);
	struct gpio_desc *select;
};
