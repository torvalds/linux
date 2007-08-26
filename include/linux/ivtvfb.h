/*
    On Screen Display cx23415 Framebuffer driver

    Copyright (C) 2006, 2007  Ian Armstrong <ian@iarmst.demon.co.uk>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __LINUX_IVTVFB_H__
#define __LINUX_IVTVFB_H__

#ifdef __KERNEL__
#include <linux/compiler.h> /* need __user */
#else
#define __user
#endif
#include <linux/types.h>

/* Framebuffer external API */

struct ivtvfb_dma_frame {
	void __user *source;
	unsigned long dest_offset;
	int count;
};

#define IVTVFB_IOC_DMA_FRAME 	_IOW('V', BASE_VIDIOC_PRIVATE+0, struct ivtvfb_dma_frame)
#define FBIO_WAITFORVSYNC	_IOW('F', 0x20, u_int32_t)

#endif
