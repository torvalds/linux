/*
	Copyright (C) 2004 - 2006 rt2x00 SourceForge Project
	<http://rt2x00.serialmonkey.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the
	Free Software Foundation, Inc.,
	59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
	Module: eeprom_93cx6
	Abstract: EEPROM reader datastructures for 93cx6 chipsets.
	Supported chipsets: 93c46 & 93c66.
 */

/*
 * EEPROM operation defines.
 */
#define PCI_EEPROM_WIDTH_93C46	6
#define PCI_EEPROM_WIDTH_93C66	8
#define PCI_EEPROM_WIDTH_OPCODE	3
#define PCI_EEPROM_WRITE_OPCODE	0x05
#define PCI_EEPROM_READ_OPCODE	0x06
#define PCI_EEPROM_EWDS_OPCODE	0x10
#define PCI_EEPROM_EWEN_OPCODE	0x13

/**
 * struct eeprom_93cx6 - control structure for setting the commands
 * for reading the eeprom data.
 * @data: private pointer for the driver.
 * @register_read(struct eeprom_93cx6 *eeprom): handler to
 * read the eeprom register, this function should set all reg_* fields.
 * @register_write(struct eeprom_93cx6 *eeprom): handler to
 * write to the eeprom register by using all reg_* fields.
 * @width: eeprom width, should be one of the PCI_EEPROM_WIDTH_* defines
 * @reg_data_in: register field to indicate data input
 * @reg_data_out: register field to indicate data output
 * @reg_data_clock: register field to set the data clock
 * @reg_chip_select: register field to set the chip select
 *
 * This structure is used for the communication between the driver
 * and the eeprom_93cx6 handlers for reading the eeprom.
 */
struct eeprom_93cx6 {
	void *data;

	void (*register_read)(struct eeprom_93cx6 *eeprom);
	void (*register_write)(struct eeprom_93cx6 *eeprom);

	int width;

	char reg_data_in;
	char reg_data_out;
	char reg_data_clock;
	char reg_chip_select;
};

extern void eeprom_93cx6_read(struct eeprom_93cx6 *eeprom,
	const u8 word, u16 *data);
extern void eeprom_93cx6_multiread(struct eeprom_93cx6 *eeprom,
	const u8 word, __le16 *data, const u16 words);
