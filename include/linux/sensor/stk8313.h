/*
 *  stk8313.h - Linux kernel driver definition for stk8313 accelerometer
 *
 *  Copyright (C) 2011~2013 Lex Hsieh / sensortek <lex_hsieh@sensortek.com.tw>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _STK831X_H_
#define _STK831X_H_

#include <linux/ioctl.h>
#define STK831X_I2C_NAME		"stk831x"
#ifdef STK_ALLWINNER_PLATFORM
	#define ACC_IDEVICE_NAME		"stk8313"
#else
	#define ACC_IDEVICE_NAME		"accelerometer"
#endif
#define STKDIR				0x3D
#define STK_LSB_1G			256
/* register for stk8313 registers */

#define	STK831X_XOUT	0x00
#define	STK831X_YOUT	0x02
#define	STK831X_ZOUT	0x04
#define	STK831X_TILT		0x06	/* Tilt Status */
#define	STK831X_SRST	0x07	/* Sampling Rate Status */
#define	STK831X_SPCNT	0x08	/* Sleep Count */
#define	STK831X_INTSU	0x09	/* Interrupt setup*/
#define	STK831X_MODE	0x0A
#define	STK831X_SR		0x0B	/* Sample rate */
#define	STK831X_PDET	0x0C	/* Tap Detection */
#define	STK831X_DEVID	0x0E	/* Device ID */
#define	STK831X_OFSX	0x0F	/* X-Axis offset */
#define	STK831X_OFSY	0x10	/* Y-Axis offset */
#define	STK831X_OFSZ	0x11	/* Z-Axis offset */
#define	STK831X_PLAT	0x12	/* Tap Latency */
#define	STK831X_PWIN	0x13	/* Tap Window */	
#define	STK831X_FTH		0x14	/* Fre	e-Fall Threshold */
#define	STK831X_FTM	0x15	/* Free-Fall Time */
#define	STK831X_STH	0x16	/* Shake Threshold */
#define	STK831X_ISTMP	0x17	/* Interrupt Setup */
#define 	STK831X_INTMAP	0x18	/*Interrupt Map*/
#define	STK831X_RESET	0x20	/*software reset*/

/* IOCTLs*/
#define STK_IOCTL_WRITE				_IOW(STKDIR, 0x01, char[8])
#define STK_IOCTL_READ				_IOWR(STKDIR, 0x02, char[8])
#define STK_IOCTL_SET_ENABLE			_IOW(STKDIR, 0x03, char)
#define STK_IOCTL_GET_ENABLE			_IOR(STKDIR, 0x04, char)
#define STK_IOCTL_SET_DELAY			_IOW(STKDIR, 0x05, char)
#define STK_IOCTL_GET_DELAY			_IOR(STKDIR, 0x06, char)
#define STK_IOCTL_SET_OFFSET			_IOW(STKDIR, 0x07, char[3])
#define STK_IOCTL_GET_OFFSET			_IOR(STKDIR, 0x08, char[3])
#define STK_IOCTL_GET_ACCELERATION	_IOR(STKDIR, 0x09, int[3])
#define STK_IOCTL_SET_RANGE			_IOW(STKDIR, 0x10, char)
#define STK_IOCTL_GET_RANGE			_IOR(STKDIR, 0x11, char)
#define STK_IOCTL_SET_CALI			_IOW(STKDIR, 0x12, char)


#endif