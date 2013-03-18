/*
 * Retu MFD driver interface
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 */

#ifndef __LINUX_MFD_RETU_H
#define __LINUX_MFD_RETU_H

struct retu_dev;

int retu_read(struct retu_dev *, u8);
int retu_write(struct retu_dev *, u8, u16);

/* Registers */
#define RETU_REG_WATCHDOG	0x17		/* Watchdog */
#define RETU_REG_CC1		0x0d		/* Common control register 1 */
#define RETU_REG_STATUS		0x16		/* Status register */

#endif /* __LINUX_MFD_RETU_H */
