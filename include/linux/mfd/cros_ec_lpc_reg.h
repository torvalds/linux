/*
 * cros_ec_lpc_reg - LPC access to the Chrome OS Embedded Controller
 *
 * Copyright (C) 2016 Google, Inc
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This driver uses the Chrome OS EC byte-level message-based protocol for
 * communicating the keyboard state (which keys are pressed) from a keyboard EC
 * to the AP over some bus (such as i2c, lpc, spi).  The EC does debouncing,
 * but everything else (including deghosting) is done here.  The main
 * motivation for this is to keep the EC firmware as simple as possible, since
 * it cannot be easily upgraded and EC flash/IRAM space is relatively
 * expensive.
 */

#ifndef __LINUX_MFD_CROS_EC_REG_H
#define __LINUX_MFD_CROS_EC_REG_H

/**
 * cros_ec_lpc_read_bytes - Read bytes from a given LPC-mapped address.
 * Returns 8-bit checksum of all bytes read.
 *
 * @offset: Base read address
 * @length: Number of bytes to read
 * @dest: Destination buffer
 */
u8 cros_ec_lpc_read_bytes(unsigned int offset, unsigned int length, u8 *dest);

/**
 * cros_ec_lpc_write_bytes - Write bytes to a given LPC-mapped address.
 * Returns 8-bit checksum of all bytes written.
 *
 * @offset: Base write address
 * @length: Number of bytes to write
 * @msg: Write data buffer
 */
u8 cros_ec_lpc_write_bytes(unsigned int offset, unsigned int length, u8 *msg);

/**
 * cros_ec_lpc_reg_init
 *
 * Initialize register I/O.
 */
void cros_ec_lpc_reg_init(void);

/**
 * cros_ec_lpc_reg_destroy
 *
 * Cleanup reg I/O.
 */
void cros_ec_lpc_reg_destroy(void);

#endif /* __LINUX_MFD_CROS_EC_REG_H */
