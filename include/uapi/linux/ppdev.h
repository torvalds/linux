/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * linux/include/linux/ppdev.h
 *
 * User-space parallel port device driver (header file).
 *
 * Copyright (C) 1998-9 Tim Waugh <tim@cyberelk.demon.co.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Added PPGETTIME/PPSETTIME, Fred Barnes, 1999
 * Added PPGETMODES/PPGETMODE/PPGETPHASE, Fred Barnes <frmb2@ukc.ac.uk>, 03/01/2001
 */

#define PP_IOCTL	'p'

/* Set mode for read/write (e.g. IEEE1284_MODE_EPP) */
#define PPSETMODE	_IOW(PP_IOCTL, 0x80, int)

/* Read status */
#define PPRSTATUS	_IOR(PP_IOCTL, 0x81, unsigned char)
#define PPWSTATUS	OBSOLETE__IOW(PP_IOCTL, 0x82, unsigned char)

/* Read/write control */
#define PPRCONTROL	_IOR(PP_IOCTL, 0x83, unsigned char)
#define PPWCONTROL	_IOW(PP_IOCTL, 0x84, unsigned char)

struct ppdev_frob_struct {
	unsigned char mask;
	unsigned char val;
};
#define PPFCONTROL      _IOW(PP_IOCTL, 0x8e, struct ppdev_frob_struct)

/* Read/write data */
#define PPRDATA		_IOR(PP_IOCTL, 0x85, unsigned char)
#define PPWDATA		_IOW(PP_IOCTL, 0x86, unsigned char)

/* Read/write econtrol (not used) */
#define PPRECONTROL	OBSOLETE__IOR(PP_IOCTL, 0x87, unsigned char)
#define PPWECONTROL	OBSOLETE__IOW(PP_IOCTL, 0x88, unsigned char)

/* Read/write FIFO (not used) */
#define PPRFIFO		OBSOLETE__IOR(PP_IOCTL, 0x89, unsigned char)
#define PPWFIFO		OBSOLETE__IOW(PP_IOCTL, 0x8a, unsigned char)

/* Claim the port to start using it */
#define PPCLAIM		_IO(PP_IOCTL, 0x8b)

/* Release the port when you aren't using it */
#define PPRELEASE	_IO(PP_IOCTL, 0x8c)

/* Yield the port (release it if another driver is waiting,
 * then reclaim) */
#define PPYIELD		_IO(PP_IOCTL, 0x8d)

/* Register device exclusively (must be before PPCLAIM). */
#define PPEXCL		_IO(PP_IOCTL, 0x8f)

/* Data line direction: non-zero for input mode. */
#define PPDATADIR	_IOW(PP_IOCTL, 0x90, int)

/* Negotiate a particular IEEE 1284 mode. */
#define PPNEGOT		_IOW(PP_IOCTL, 0x91, int)

/* Set control lines when an interrupt occurs. */
#define PPWCTLONIRQ	_IOW(PP_IOCTL, 0x92, unsigned char)

/* Clear (and return) interrupt count. */
#define PPCLRIRQ	_IOR(PP_IOCTL, 0x93, int)

/* Set the IEEE 1284 phase that we're in (e.g. IEEE1284_PH_FWD_IDLE) */
#define PPSETPHASE	_IOW(PP_IOCTL, 0x94, int)

/* Set and get port timeout (struct timeval's) */
#define PPGETTIME	_IOR(PP_IOCTL, 0x95, struct timeval)
#define PPSETTIME	_IOW(PP_IOCTL, 0x96, struct timeval)

/* Get available modes (what the hardware can do) */
#define PPGETMODES	_IOR(PP_IOCTL, 0x97, unsigned int)

/* Get the current mode and phaze */
#define PPGETMODE	_IOR(PP_IOCTL, 0x98, int)
#define PPGETPHASE	_IOR(PP_IOCTL, 0x99, int)

/* get/set flags */
#define PPGETFLAGS	_IOR(PP_IOCTL, 0x9a, int)
#define PPSETFLAGS	_IOW(PP_IOCTL, 0x9b, int)

/* flags visible to the world */
#define PP_FASTWRITE	(1<<2)
#define PP_FASTREAD	(1<<3)
#define PP_W91284PIC	(1<<4)

/* only masks user-visible flags */
#define PP_FLAGMASK	(PP_FASTWRITE | PP_FASTREAD | PP_W91284PIC)


