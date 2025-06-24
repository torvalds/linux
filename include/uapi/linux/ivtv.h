/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
    Public ivtv API header
    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>
    Copyright (C) 2004-2007  Hans Verkuil <hverkuil@kernel.org>

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

#ifndef __LINUX_IVTV_H__
#define __LINUX_IVTV_H__

#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/videodev2.h>

/* ivtv knows several distinct output modes: MPEG streaming,
   YUV streaming, YUV updates through user DMA and the passthrough
   mode.

   In order to clearly tell the driver that we are in user DMA
   YUV mode you need to call IVTV_IOC_DMA_FRAME with y_source == NULL
   first (althrough if you don't then the first time
   DMA_FRAME is called the mode switch is done automatically).

   When you close the file handle the user DMA mode is exited again.

   While in one mode, you cannot use another mode (EBUSY is returned).

   All this means that if you want to change the YUV interlacing
   for the user DMA YUV mode you first need to do call IVTV_IOC_DMA_FRAME
   with y_source == NULL before you can set the correct format using
   VIDIOC_S_FMT.

   Eventually all this should be replaced with a proper V4L2 API,
   but for now we have to do it this way. */

struct ivtv_dma_frame {
	enum v4l2_buf_type type; /* V4L2_BUF_TYPE_VIDEO_OUTPUT */
	__u32 pixelformat;	 /* 0 == same as destination */
	void __user *y_source;   /* if NULL and type == V4L2_BUF_TYPE_VIDEO_OUTPUT,
				    then just switch to user DMA YUV output mode */
	void __user *uv_source;  /* Unused for RGB pixelformats */
	struct v4l2_rect src;
	struct v4l2_rect dst;
	__u32 src_width;
	__u32 src_height;
};

#define IVTV_IOC_DMA_FRAME		_IOW ('V', BASE_VIDIOC_PRIVATE+0, struct ivtv_dma_frame)

/* Select the passthrough mode (if the argument is non-zero). In the passthrough
   mode the output of the encoder is passed immediately into the decoder. */
#define IVTV_IOC_PASSTHROUGH_MODE	_IOW ('V', BASE_VIDIOC_PRIVATE+1, int)

/* Deprecated defines: applications should use the defines from videodev2.h */
#define IVTV_SLICED_TYPE_TELETEXT_B     V4L2_MPEG_VBI_IVTV_TELETEXT_B
#define IVTV_SLICED_TYPE_CAPTION_525    V4L2_MPEG_VBI_IVTV_CAPTION_525
#define IVTV_SLICED_TYPE_WSS_625        V4L2_MPEG_VBI_IVTV_WSS_625
#define IVTV_SLICED_TYPE_VPS            V4L2_MPEG_VBI_IVTV_VPS

#endif /* _LINUX_IVTV_H */
