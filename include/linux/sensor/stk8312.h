/*
 *  stk8312.h - Linux kernel driver definition for stk8312 accelerometer
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
	#define ACC_IDEVICE_NAME		"stk8312"
#else
	#define ACC_IDEVICE_NAME		"accelerometer"
#endif
#define STKDIR				0x3D
#define STK_LSB_1G		       21
/* registers for stk8312 registers */

#define	STK831X_XOUT	0x00	/* x-axis acceleration*/
#define	STK831X_YOUT	0x01	/* y-axis acceleration*/
#define	STK831X_ZOUT	0x02	/* z-axis acceleration*/
#define	STK831X_TILT	 	0x03	/* Tilt Status */
#define	STK831X_SRST	0x04	/* Sampling Rate Status */
#define	STK831X_SPCNT	0x05	/* Sleep Count */
#define	STK831X_INTSU	0x06	/* Interrupt setup*/
#define	STK831X_MODE	0x07
#define	STK831X_SR		0x08	/* Sample rate */
#define	STK831X_PDET	0x09	/* Tap Detection */
#define	STK831X_DEVID	0x0B	/* Device ID */
#define	STK831X_OFSX	0x0C	/* X-Axis offset */
#define	STK831X_OFSY	0x0D	/* Y-Axis offset */
#define	STK831X_OFSZ	0x0E	/* Z-Axis offset */
#define	STK831X_PLAT	0x0F	/* Tap Latency */
#define	STK831X_PWIN	0x10	/* Tap Window */	
#define	STK831X_FTH		0x11	/* Free-Fall Threshold */
#define	STK831X_FTM	0x12	/* Free-Fall Time */
#define	STK831X_STH	0x13	/* Shake Threshold */
#define	STK831X_CTRL	0x14	/* Control Register */
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