/*
 * mxc_mlb.h
 *
 * Copyright 2008-2013 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#ifndef _MXC_MLB_H
#define _MXC_MLB_H

/* define IOCTL command */
#define MLB_DBG_RUNTIME		_IO('S', 0x09)
#define MLB_SET_FPS		_IOW('S', 0x10, unsigned int)
#define MLB_GET_VER		_IOR('S', 0x11, unsigned long)
#define MLB_SET_DEVADDR		_IOR('S', 0x12, unsigned char)

/*!
 * set channel address for each logical channel
 * the MSB 16bits is for tx channel, the left LSB is for rx channel
 */
#define MLB_CHAN_SETADDR	_IOW('S', 0x13, unsigned int)
#define MLB_CHAN_STARTUP	_IO('S', 0x14)
#define MLB_CHAN_SHUTDOWN	_IO('S', 0x15)
#define MLB_CHAN_GETEVENT	_IOR('S', 0x16, unsigned long)

#define MLB_SET_ISOC_BLKSIZE_188 _IO('S', 0x17)
#define MLB_SET_ISOC_BLKSIZE_196 _IO('S', 0x18)
#define MLB_SET_SYNC_QUAD	_IOW('S', 0x19, unsigned int)
#define MLB_IRQ_ENABLE		_IO('S', 0x20)
#define MLB_IRQ_DISABLE		_IO('S', 0x21)

/*!
 * MLB event define
 */
enum {
	MLB_EVT_TX_PROTO_ERR_CUR = 1 << 0,
	MLB_EVT_TX_BRK_DETECT_CUR = 1 << 1,
	MLB_EVT_TX_PROTO_ERR_PREV = 1 << 8,
	MLB_EVT_TX_BRK_DETECT_PREV = 1 << 9,
	MLB_EVT_RX_PROTO_ERR_CUR = 1 << 16,
	MLB_EVT_RX_BRK_DETECT_CUR = 1 << 17,
	MLB_EVT_RX_PROTO_ERR_PREV = 1 << 24,
	MLB_EVT_RX_BRK_DETECT_PREV = 1 << 25,
};


#endif				/* _MXC_MLB_H */
