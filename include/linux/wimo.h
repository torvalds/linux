/* arch/arm/mack-rk29/include/mach/wimo.h
 *
 * Copyright (C) 2007 Google, Inc.
 * author: chenhengming chm@rock-chips.com
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
 */

#ifndef __ARCH_ARM_MACH_RK29_WIMO_H
#define __ARCH_ARM_MACH_RK29_WIMO_H
#ifdef CONFIG_FB_WIMO
#define FB_WIMO_FLAG  
#endif





#if	1//def	CONFIG_FB_WIMO
#define WIMO_IOCTL_MAGIC 0x60
#define WIMO_START				_IOW(WIMO_IOCTL_MAGIC, 0x1, unsigned int)
#define WIMO_STOP				_IOW(WIMO_IOCTL_MAGIC, 0x2, unsigned int)
#define WIMO_SET_ROTATION			_IOW(WIMO_IOCTL_MAGIC, 0x3, unsigned int)
#define WIMO_DEVICE_OKAY			_IOW(WIMO_IOCTL_MAGIC, 0x4, unsigned int)

#define WIMO_VIDEO_OPEN				_IOW(WIMO_IOCTL_MAGIC, 0x11, unsigned int)
#define WIMO_VIDEO_CLOSE			_IOW(WIMO_IOCTL_MAGIC, 0x12, unsigned int)
#define WIMO_VIDEO_GET_BUF			_IOW(WIMO_IOCTL_MAGIC, 0x13, unsigned int)

#define WIMO_AUDIO_OPEN           	      	_IOW(WIMO_IOCTL_MAGIC, 0x21, unsigned int)
#define WIMO_AUDIO_CLOSE                	_IOW(WIMO_IOCTL_MAGIC, 0x22, unsigned int)
#define WIMO_AUDIO_GET_BUF            		_IOW(WIMO_IOCTL_MAGIC, 0x23, unsigned int)
#define WIMO_AUDIO_SET_PARA                     _IOW(WIMO_IOCTL_MAGIC, 0x24, unsigned int)
#define	WIMO_AUDIO_SET_VOL			_IOW(WIMO_IOCTL_MAGIC, 0x25, unsigned int)
#define	WIMO_AUDIO_GET_VOL			_IOR(WIMO_IOCTL_MAGIC, 0x26, unsigned int)

#define	WIMO_AUDIO_SET_BUFFER_SIZE		0xa1
#define	WIMO_AUDIO_SET_BYTEPERFRAME		0xa2


#define WIMO_COUNT_ZERO				-111
#define VIDEO_ENCODER_CLOSED			-222
#define AUDIO_ENCODER_CLOSED			-222
#define ENCODER_BUFFER_FULL			-333


#define WIMO_IOCTL_ERROR			-1111

#endif

struct wimo_platform_data
{
	const char* name;
	/* starting physical address of memory region */
	unsigned long start;
	/* size of memory region */
	unsigned long size;
	/* set to indicate maps of this region should be cached, if a mix of
	 * cached and uncached is desired, set this and open the device with
	 * O_SYNC to get an uncached region */
	unsigned cached;
	/* The MSM7k has bits to enable a write buffer in the bus controller*/
	unsigned buffered;
};

#endif //__ARCH_ARM_MACH_RK29_WIMO_H

