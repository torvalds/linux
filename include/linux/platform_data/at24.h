/*
 * at24.h - platform_data for the at24 (generic eeprom) driver
 * (C) Copyright 2008 by Pengutronix
 * (C) Copyright 2012 by Wolfram Sang
 * same license as the driver
 */

#ifndef _LINUX_AT24_H
#define _LINUX_AT24_H

#include <linux/types.h>
#include <linux/nvmem-consumer.h>
#include <linux/bitops.h>

/**
 * struct at24_platform_data - data to set up at24 (generic eeprom) driver
 * @byte_len: size of eeprom in byte
 * @page_size: number of byte which can be written in one go
 * @flags: tunable options, check AT24_FLAG_* defines
 * @setup: an optional callback invoked after eeprom is probed; enables kernel
	code to access eeprom via nvmem, see example
 * @context: optional parameter passed to setup()
 *
 * If you set up a custom eeprom type, please double-check the parameters.
 * Especially page_size needs extra care, as you risk data loss if your value
 * is bigger than what the chip actually supports!
 *
 * An example in pseudo code for a setup() callback:
 *
 * void get_mac_addr(struct nvmem_device *nvmem, void *context)
 * {
 *	u8 *mac_addr = ethernet_pdata->mac_addr;
 *	off_t offset = context;
 *
 *	// Read MAC addr from EEPROM
 *	if (nvmem_device_read(nvmem, offset, ETH_ALEN, mac_addr) == ETH_ALEN)
 *		pr_info("Read MAC addr from EEPROM: %pM\n", mac_addr);
 * }
 *
 * This function pointer and context can now be set up in at24_platform_data.
 */

struct at24_platform_data {
	u32		byte_len;		/* size (sum of all addr) */
	u16		page_size;		/* for writes */
	u8		flags;
#define AT24_FLAG_ADDR16	BIT(7)	/* address pointer is 16 bit */
#define AT24_FLAG_READONLY	BIT(6)	/* sysfs-entry will be read-only */
#define AT24_FLAG_IRUGO		BIT(5)	/* sysfs-entry will be world-readable */
#define AT24_FLAG_TAKE8ADDR	BIT(4)	/* take always 8 addresses (24c00) */
#define AT24_FLAG_SERIAL	BIT(3)	/* factory-programmed serial number */
#define AT24_FLAG_MAC		BIT(2)	/* factory-programmed mac address */

	void		(*setup)(struct nvmem_device *nvmem, void *context);
	void		*context;
};

#endif /* _LINUX_AT24_H */
