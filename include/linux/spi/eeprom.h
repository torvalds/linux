#ifndef __LINUX_SPI_EEPROM_H
#define __LINUX_SPI_EEPROM_H

#include <linux/memory.h>

/*
 * Put one of these structures in platform_data for SPI EEPROMS handled
 * by the "at25" driver.  On SPI, most EEPROMS understand the same core
 * command set.  If you need to support EEPROMs that don't yet fit, add
 * flags to support those protocol options.  These values all come from
 * the chip datasheets.
 */
struct spi_eeprom {
	u32		byte_len;
	char		name[10];
	u16		page_size;		/* for writes */
	u16		flags;
#define	EE_ADDR1	0x0001			/*  8 bit addrs */
#define	EE_ADDR2	0x0002			/* 16 bit addrs */
#define	EE_ADDR3	0x0004			/* 24 bit addrs */
#define	EE_READONLY	0x0008			/* disallow writes */

	/*
	 * Certain EEPROMS have a size that is larger than the number of address
	 * bytes would allow (e.g. like M95040 from ST that has 512 Byte size
	 * but uses only one address byte (A0 to A7) for addressing.) For
	 * the extra address bit (A8, A16 or A24) bit 3 of the instruction byte
	 * is used. This instruction bit is normally defined as don't care for
	 * other AT25 like chips.
	 */
#define EE_INSTR_BIT3_IS_ADDR	0x0010

	void *context;
};

#endif /* __LINUX_SPI_EEPROM_H */
