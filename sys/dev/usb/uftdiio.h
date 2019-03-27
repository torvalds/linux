/*-
 * Copyright 2008-2012 - Symmetricom, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *  $FreeBSD$ 
 */

/*
 * FTDI USB serial converter chip ioctl commands.
 */

#ifndef _USB_UFTDIIO_H_
#define _USB_UFTDIIO_H_

#include <sys/ioccom.h>

enum uftdi_bitmodes
{
	UFTDI_BITMODE_ASYNC = 0,
	UFTDI_BITMODE_MPSSE = 1,
	UFTDI_BITMODE_SYNC = 2,
	UFTDI_BITMODE_CPU_EMUL = 3,
	UFTDI_BITMODE_FAST_SERIAL = 4,
	UFTDI_BITMODE_CBUS = 5,
	UFTDI_BITMODE_NONE = 0xff,	/* aka UART mode. */
};

/*
 * For UFTDIIOC_SET_BITMODE:
 *   mode   = One of the uftdi_bitmodes enum values.
 *   iomask = Mask of bits enabled for bitbang output.
 *
 * For UFTDIIOC_GET_BITMODE:
 *   mode   = Mode most recently set using UFTDIIOC_SET_BITMODE.
 *   iomask = Returned snapshot of DBUS0..DBUS7 pin states at time of call.
 *            Pin states can be read in any mode, not just bitbang modes.
 */
struct uftdi_bitmode
{
	uint8_t mode;
	uint8_t iomask;
};

/*
 * For UFTDIIOC_READ_EEPROM, UFTDIIOC_WRITE_EEPROM:
 *
 * IO is done in 16-bit words at the chip level; offset and length are in bytes,
 * but must each be evenly divisible by two.
 *
 * It is not necessary to erase before writing.  For the FT232R device (only)
 * you must set the latency timer to 0x77 before doing a series of eeprom writes
 * (and restore it to the prior value when done).
 */
struct uftdi_eeio
{
	uint16_t offset;
	uint16_t length;
	uint16_t data[64];
};

/* Pass this value to confirm that eeprom erase request is not accidental. */
#define	UFTDI_CONFIRM_ERASE	0x26139108

#define	UFTDIIOC_RESET_IO	_IO('c', 0)	/* Reset config, flush fifos.*/
#define	UFTDIIOC_RESET_RX	_IO('c', 1)	/* Flush input fifo. */
#define	UFTDIIOC_RESET_TX	_IO('c', 2)	/* Flush output fifo. */
#define	UFTDIIOC_SET_BITMODE	_IOW('c', 3, struct uftdi_bitmode)
#define	UFTDIIOC_GET_BITMODE	_IOR('c', 4, struct uftdi_bitmode)
#define	UFTDIIOC_SET_ERROR_CHAR	_IOW('c', 5, int)	/* -1 to disable */
#define	UFTDIIOC_SET_EVENT_CHAR	_IOW('c', 6, int)	/* -1 to disable */
#define	UFTDIIOC_SET_LATENCY	_IOW('c', 7, int)	/* 1-255 ms */
#define	UFTDIIOC_GET_LATENCY	_IOR('c', 8, int)
#define	UFTDIIOC_GET_HWREV	_IOR('c', 9, int)
#define	UFTDIIOC_READ_EEPROM	_IOWR('c', 10, struct uftdi_eeio)
#define	UFTDIIOC_WRITE_EEPROM	_IOW('c', 11, struct uftdi_eeio)
#define	UFTDIIOC_ERASE_EEPROM	_IOW('c', 12, int)

#endif
