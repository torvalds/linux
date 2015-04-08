/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef MXC_SIM_INTERFACE_H
#define MXC_SIM_INTERFACE_H

#define SIM_ATR_LENGTH_MAX 32

/* Raw ATR SIM_IOCTL_GET_ATR */
typedef struct {
	unsigned int size;/* length of ATR received */
	unsigned char *atr_buffer;/* raw ATR string received */
	int errval;/* The error vale reported to user space after completing ATR*/
} sim_atr_t;

/* ISO7816-3 protocols */
#define SIM_PROTOCOL_T0  1
#define SIM_PROTOCOL_T1  2

/* Transfer types for SIM_IOCTL_XFER */
#define SIM_XFER_TYPE_TPDU 1
#define SIM_XFER_TYPE_PTS  2

typedef struct {
	unsigned int wwt;
	unsigned int cwt;
	unsigned int bwt;
	unsigned int bgt;
	unsigned int cgt;
} sim_timing_t;

/* Transfer data for SIM_IOCTL_XFER */
typedef struct {
	unsigned char *xmt_buffer;	/* transmit buffer pointer */
	int xmt_length;/* transmit buffer length */
	int timeout;/* transfer timeout in milliseconds */
	int errval;/* The error vale reported to user space after completing transmitting*/
} sim_xmt_t;

typedef struct {
	unsigned char *rcv_buffer;	/* receive buffer pointer */
	int rcv_length;	/* receive buffer length */
	int timeout;/* transfer timeout in milliseconds */
	int errval;/* The error vale reported to user space after receiving*/
} sim_rcv_t;

typedef struct {
	unsigned char di;
	unsigned char fi;
} sim_baud_t;

/* Interface power states */
#define SIM_POWER_OFF			(0)
#define SIM_POWER_ON			(1)

/* Return values for SIM_IOCTL_GET_PRESENSE */
#define SIM_PRESENT_REMOVED		(0)
#define SIM_PRESENT_DETECTED		(1)
#define SIM_PRESENT_OPERATIONAL		(2)

/* The error value */
#define SIM_OK				(0)
#define SIM_ERROR_CWT			(1 << 0)
#define SIM_ERROR_BWT			(1 << 1)
#define SIM_ERROR_PARITY		(1 << 2)
#define SIM_ERROR_INVALID_TS		(1 << 3)
#define SIM_ERROR_FRAME			(1 << 4)
#define SIM_ERROR_ATR_TIMEROUT		(1 << 5)
#define SIM_ERROR_NACK_THRESHOLD	(1 << 6)
#define SIM_ERROR_BGT			(1 << 7)
#define SIM_ERROR_ATR_DELAY		(1 << 8)

/* Return values for SIM_IOCTL_GET_ERROR */
#define SIM_E_ACCESS			(1)
#define SIM_E_TPDUSHORT			(2)
#define SIM_E_PTSEMPTY			(3)
#define SIM_E_INVALIDXFERTYPE		(4)
#define SIM_E_INVALIDXMTLENGTH		(5)
#define SIM_E_INVALIDRCVLENGTH		(6)
#define SIM_E_NACK			(7)
#define SIM_E_TIMEOUT			(8)
#define SIM_E_NOCARD			(9)
#define SIM_E_PARAM_FI_INVALID		(10)
#define SIM_E_PARAM_DI_INVALID		(11)
#define SIM_E_PARAM_FBYD_WITHFRACTION	(12)
#define SIM_E_PARAM_FBYD_NOTDIVBY8OR12	(13)
#define SIM_E_PARAM_DIVISOR_RANGE	(14)
#define SIM_E_MALLOC			(15)
#define SIM_E_IRQ			(16)
#define SIM_E_POWERED_ON		(17)
#define SIM_E_POWERED_OFF		(18)

/* ioctl encodings */
#define SIM_IOCTL_BASE			(0xc0)
#define SIM_IOCTL_GET_PRESENSE		_IOR(SIM_IOCTL_BASE, 1, int)
#define SIM_IOCTL_GET_ATR		_IOR(SIM_IOCTL_BASE, 2, sim_atr_t)
#define SIM_IOCTL_XMT			_IOR(SIM_IOCTL_BASE, 3, sim_xmt_t)
#define SIM_IOCTL_RCV			_IOR(SIM_IOCTL_BASE, 4, sim_rcv_t)
#define SIM_IOCTL_ACTIVATE		_IO(SIM_IOCTL_BASE, 5)
#define SIM_IOCTL_DEACTIVATE		_IO(SIM_IOCTL_BASE, 6)
#define SIM_IOCTL_WARM_RESET		_IO(SIM_IOCTL_BASE, 7)
#define SIM_IOCTL_COLD_RESET		_IO(SIM_IOCTL_BASE, 8)
#define SIM_IOCTL_CARD_LOCK		_IO(SIM_IOCTL_BASE, 9)
#define SIM_IOCTL_CARD_EJECT		_IO(SIM_IOCTL_BASE, 10)
#define SIM_IOCTL_SET_PROTOCOL		_IOR(SIM_IOCTL_BASE, 11, unsigned int)
#define SIM_IOCTL_SET_TIMING		_IOR(SIM_IOCTL_BASE, 12, sim_timing_t)
#define SIM_IOCTL_SET_BAUD		_IOR(SIM_IOCTL_BASE, 13, sim_baud_t)
#define SIM_IOCTL_WAIT			_IOR(SIM_IOCTL_BASE, 14, unsigned int)

#endif
