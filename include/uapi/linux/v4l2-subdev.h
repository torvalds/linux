/*
 * V4L2 subdev userspace API
 *
 * Copyright (C) 2010 Nokia Corporation
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __LINUX_V4L2_SUBDEV_H
#define __LINUX_V4L2_SUBDEV_H

#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/v4l2-common.h>
#include <linux/v4l2-mediabus.h>

/**
 * enum v4l2_subdev_format_whence - Media bus format type
 * @V4L2_SUBDEV_FORMAT_TRY: try format, for negotiation only
 * @V4L2_SUBDEV_FORMAT_ACTIVE: active format, applied to the device
 */
enum v4l2_subdev_format_whence {
	V4L2_SUBDEV_FORMAT_TRY = 0,
	V4L2_SUBDEV_FORMAT_ACTIVE = 1,
};

/**
 * struct v4l2_subdev_format - Pad-level media bus format
 * @which: format type (from enum v4l2_subdev_format_whence)
 * @pad: pad number, as reported by the media API
 * @format: media bus format (format code and frame size)
 */
struct v4l2_subdev_format {
	__u32 which;
	__u32 pad;
	struct v4l2_mbus_framefmt format;
	__u32 reserved[8];
};

/**
 * struct v4l2_subdev_crop - Pad-level crop settings
 * @which: format type (from enum v4l2_subdev_format_whence)
 * @pad: pad number, as reported by the media API
 * @rect: pad crop rectangle boundaries
 */
struct v4l2_subdev_crop {
	__u32 which;
	__u32 pad;
	struct v4l2_rect rect;
	__u32 reserved[8];
};

/**
 * struct v4l2_subdev_mbus_code_enum - Media bus format enumeration
 * @pad: pad number, as reported by the media API
 * @index: format index during enumeration
 * @code: format code (from enum v4l2_mbus_pixelcode)
 */
struct v4l2_subdev_mbus_code_enum {
	__u32 pad;
	__u32 index;
	__u32 code;
	__u32 reserved[9];
};

/**
 * struct v4l2_subdev_frame_size_enum - Media bus format enumeration
 * @pad: pad number, as reported by the media API
 * @index: format index during enumeration
 * @code: format code (from enum v4l2_mbus_pixelcode)
 */
struct v4l2_subdev_frame_size_enum {
	__u32 index;
	__u32 pad;
	__u32 code;
	__u32 min_width;
	__u32 max_width;
	__u32 min_height;
	__u32 max_height;
	__u32 reserved[9];
};

/**
 * struct v4l2_subdev_frame_interval - Pad-level frame rate
 * @pad: pad number, as reported by the media API
 * @interval: frame interval in seconds
 */
struct v4l2_subdev_frame_interval {
	__u32 pad;
	struct v4l2_fract interval;
	__u32 reserved[9];
};

/**
 * struct v4l2_subdev_frame_interval_enum - Frame interval enumeration
 * @pad: pad number, as reported by the media API
 * @index: frame interval index during enumeration
 * @code: format code (from enum v4l2_mbus_pixelcode)
 * @width: frame width in pixels
 * @height: frame height in pixels
 * @interval: frame interval in seconds
 */
struct v4l2_subdev_frame_interval_enum {
	__u32 index;
	__u32 pad;
	__u32 code;
	__u32 width;
	__u32 height;
	struct v4l2_fract interval;
	__u32 reserved[9];
};

/**
 * struct v4l2_subdev_selection - selection info
 *
 * @which: either V4L2_SUBDEV_FORMAT_ACTIVE or V4L2_SUBDEV_FORMAT_TRY
 * @pad: pad number, as reported by the media API
 * @target: Selection target, used to choose one of possible rectangles,
 *	    defined in v4l2-common.h; V4L2_SEL_TGT_* .
 * @flags: constraint flags, defined in v4l2-common.h; V4L2_SEL_FLAG_*.
 * @r: coordinates of the selection window
 * @reserved: for future use, set to zero for now
 *
 * Hardware may use multiple helper windows to process a video stream.
 * The structure is used to exchange this selection areas between
 * an application and a driver.
 */
struct v4l2_subdev_selection {
	__u32 which;
	__u32 pad;
	__u32 target;
	__u32 flags;
	struct v4l2_rect r;
	__u32 reserved[8];
};

/* Backwards compatibility define --- to be removed */
#define v4l2_subdev_edid v4l2_edid

#define VIDIOC_SUBDEV_G_FMT	_IOWR('V',  4, struct v4l2_subdev_format)
#define VIDIOC_SUBDEV_S_FMT	_IOWR('V',  5, struct v4l2_subdev_format)
#define VIDIOC_SUBDEV_G_FRAME_INTERVAL \
			_IOWR('V', 21, struct v4l2_subdev_frame_interval)
#define VIDIOC_SUBDEV_S_FRAME_INTERVAL \
			_IOWR('V', 22, struct v4l2_subdev_frame_interval)
#define VIDIOC_SUBDEV_ENUM_MBUS_CODE \
			_IOWR('V',  2, struct v4l2_subdev_mbus_code_enum)
#define VIDIOC_SUBDEV_ENUM_FRAME_SIZE \
			_IOWR('V', 74, struct v4l2_subdev_frame_size_enum)
#define VIDIOC_SUBDEV_ENUM_FRAME_INTERVAL \
			_IOWR('V', 75, struct v4l2_subdev_frame_interval_enum)
#define VIDIOC_SUBDEV_G_CROP	_IOWR('V', 59, struct v4l2_subdev_crop)
#define VIDIOC_SUBDEV_S_CROP	_IOWR('V', 60, struct v4l2_subdev_crop)
#define VIDIOC_SUBDEV_G_SELECTION \
	_IOWR('V', 61, struct v4l2_subdev_selection)
#define VIDIOC_SUBDEV_S_SELECTION \
	_IOWR('V', 62, struct v4l2_subdev_selection)
/* These two G/S_EDID ioctls are identical to the ioctls in videodev2.h */
#define VIDIOC_SUBDEV_G_EDID	_IOWR('V', 40, struct v4l2_edid)
#define VIDIOC_SUBDEV_S_EDID	_IOWR('V', 41, struct v4l2_edid)

#endif
