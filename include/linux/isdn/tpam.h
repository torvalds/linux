/* $Id: tpam.h,v 1.1.2.1 2001/06/08 08:23:46 kai Exp $
 *
 * Turbo PAM ISDN driver for Linux. (Kernel Driver)
 *
 * Copyright 2001 Stelian Pop <stelian.pop@fr.alcove.com>, Alcôve
 *
 * For all support questions please contact: <support@auvertech.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef _TPAM_H_
#define _TPAM_H_

#include <linux/types.h>
#include <linux/pci.h>

/* IOCTL commands */
#define TPAM_CMD_DSPLOAD	0x0001
#define TPAM_CMD_DSPSAVE	0x0002
#define TPAM_CMD_DSPRUN		0x0003
#define TPAM_CMD_LOOPMODEON	0x0004
#define TPAM_CMD_LOOPMODEOFF	0x0005

/* addresses of debug information zones on board */
#define TPAM_TRAPAUDIT_REGISTER		0x005493e4
#define TPAM_NCOAUDIT_REGISTER		0x00500000
#define TPAM_MSGAUDIT_REGISTER		0x008E30F0

/* length of debug information zones on board */
#define TPAM_TRAPAUDIT_LENGTH		10000
#define TPAM_NCOAUDIT_LENGTH		300000
#define TPAM_NCOAUDIT_COUNT		30
#define TPAM_MSGAUDIT_LENGTH		60000

/* IOCTL load/save parameter */
typedef struct tpam_dsp_ioctl {
	__u32 address;	/* address to load/save data */
	__u32 data_len;	/* size of data to be loaded/saved */
	__u8 data[0];	/* data */
} tpam_dsp_ioctl;

#endif /* _TPAM_H_ */
