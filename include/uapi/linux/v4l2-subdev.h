/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * V4L2 subdev userspace API
 *
 * Copyright (C) 2010 Nokia Corporation
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 */

#ifndef __LINUX_V4L2_SUBDEV_H
#define __LINUX_V4L2_SUBDEV_H

#include <linux/const.h>
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
 * @stream: stream number, defined in subdev routing
 * @reserved: drivers and applications must zero this array
 */
struct v4l2_subdev_format {
	__u32 which;
	__u32 pad;
	struct v4l2_mbus_framefmt format;
	__u32 stream;
	__u32 reserved[7];
};

/**
 * struct v4l2_subdev_crop - Pad-level crop settings
 * @which: format type (from enum v4l2_subdev_format_whence)
 * @pad: pad number, as reported by the media API
 * @rect: pad crop rectangle boundaries
 * @stream: stream number, defined in subdev routing
 * @reserved: drivers and applications must zero this array
 */
struct v4l2_subdev_crop {
	__u32 which;
	__u32 pad;
	struct v4l2_rect rect;
	__u32 stream;
	__u32 reserved[7];
};

#define V4L2_SUBDEV_MBUS_CODE_CSC_COLORSPACE	0x00000001
#define V4L2_SUBDEV_MBUS_CODE_CSC_XFER_FUNC	0x00000002
#define V4L2_SUBDEV_MBUS_CODE_CSC_YCBCR_ENC	0x00000004
#define V4L2_SUBDEV_MBUS_CODE_CSC_HSV_ENC	V4L2_SUBDEV_MBUS_CODE_CSC_YCBCR_ENC
#define V4L2_SUBDEV_MBUS_CODE_CSC_QUANTIZATION	0x00000008

/**
 * struct v4l2_subdev_mbus_code_enum - Media bus format enumeration
 * @pad: pad number, as reported by the media API
 * @index: format index during enumeration
 * @code: format code (MEDIA_BUS_FMT_ definitions)
 * @which: format type (from enum v4l2_subdev_format_whence)
 * @flags: flags set by the driver, (V4L2_SUBDEV_MBUS_CODE_*)
 * @stream: stream number, defined in subdev routing
 * @reserved: drivers and applications must zero this array
 */
struct v4l2_subdev_mbus_code_enum {
	__u32 pad;
	__u32 index;
	__u32 code;
	__u32 which;
	__u32 flags;
	__u32 stream;
	__u32 reserved[6];
};

/**
 * struct v4l2_subdev_frame_size_enum - Media bus format enumeration
 * @index: format index during enumeration
 * @pad: pad number, as reported by the media API
 * @code: format code (MEDIA_BUS_FMT_ definitions)
 * @min_width: minimum frame width, in pixels
 * @max_width: maximum frame width, in pixels
 * @min_height: minimum frame height, in pixels
 * @max_height: maximum frame height, in pixels
 * @which: format type (from enum v4l2_subdev_format_whence)
 * @stream: stream number, defined in subdev routing
 * @reserved: drivers and applications must zero this array
 */
struct v4l2_subdev_frame_size_enum {
	__u32 index;
	__u32 pad;
	__u32 code;
	__u32 min_width;
	__u32 max_width;
	__u32 min_height;
	__u32 max_height;
	__u32 which;
	__u32 stream;
	__u32 reserved[7];
};

/**
 * struct v4l2_subdev_frame_interval - Pad-level frame rate
 * @pad: pad number, as reported by the media API
 * @interval: frame interval in seconds
 * @stream: stream number, defined in subdev routing
 * @reserved: drivers and applications must zero this array
 */
struct v4l2_subdev_frame_interval {
	__u32 pad;
	struct v4l2_fract interval;
	__u32 stream;
	__u32 reserved[8];
};

/**
 * struct v4l2_subdev_frame_interval_enum - Frame interval enumeration
 * @pad: pad number, as reported by the media API
 * @index: frame interval index during enumeration
 * @code: format code (MEDIA_BUS_FMT_ definitions)
 * @width: frame width in pixels
 * @height: frame height in pixels
 * @interval: frame interval in seconds
 * @which: format type (from enum v4l2_subdev_format_whence)
 * @stream: stream number, defined in subdev routing
 * @reserved: drivers and applications must zero this array
 */
struct v4l2_subdev_frame_interval_enum {
	__u32 index;
	__u32 pad;
	__u32 code;
	__u32 width;
	__u32 height;
	struct v4l2_fract interval;
	__u32 which;
	__u32 stream;
	__u32 reserved[7];
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
 * @stream: stream number, defined in subdev routing
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
	__u32 stream;
	__u32 reserved[7];
};

/**
 * struct v4l2_subdev_capability - subdev capabilities
 * @version: the driver versioning number
 * @capabilities: the subdev capabilities, see V4L2_SUBDEV_CAP_*
 * @reserved: for future use, set to zero for now
 */
struct v4l2_subdev_capability {
	__u32 version;
	__u32 capabilities;
	__u32 reserved[14];
};

/* The v4l2 sub-device video device node is registered in read-only mode. */
#define V4L2_SUBDEV_CAP_RO_SUBDEV		0x00000001

/* The v4l2 sub-device supports routing and multiplexed streams. */
#define V4L2_SUBDEV_CAP_STREAMS			0x00000002

/*
 * Is the route active? An active route will start when streaming is enabled
 * on a video node.
 */
#define V4L2_SUBDEV_ROUTE_FL_ACTIVE		(1U << 0)

/**
 * struct v4l2_subdev_route - A route inside a subdev
 *
 * @sink_pad: the sink pad index
 * @sink_stream: the sink stream identifier
 * @source_pad: the source pad index
 * @source_stream: the source stream identifier
 * @flags: route flags V4L2_SUBDEV_ROUTE_FL_*
 * @reserved: drivers and applications must zero this array
 */
struct v4l2_subdev_route {
	__u32 sink_pad;
	__u32 sink_stream;
	__u32 source_pad;
	__u32 source_stream;
	__u32 flags;
	__u32 reserved[5];
};

/**
 * struct v4l2_subdev_routing - Subdev routing information
 *
 * @which: configuration type (from enum v4l2_subdev_format_whence)
 * @num_routes: the total number of routes in the routes array
 * @routes: pointer to the routes array
 * @reserved: drivers and applications must zero this array
 */
struct v4l2_subdev_routing {
	__u32 which;
	__u32 num_routes;
	__u64 routes;
	__u32 reserved[6];
};

/*
 * The client is aware of streams. Setting this flag enables the use of 'stream'
 * fields (referring to the stream number) with various ioctls. If this is not
 * set (which is the default), the 'stream' fields will be forced to 0 by the
 * kernel.
 */
 #define V4L2_SUBDEV_CLIENT_CAP_STREAMS		(1ULL << 0)

/**
 * struct v4l2_subdev_client_capability - Capabilities of the client accessing
 *					  the subdev
 *
 * @capabilities: A bitmask of V4L2_SUBDEV_CLIENT_CAP_* flags.
 */
struct v4l2_subdev_client_capability {
	__u64 capabilities;
};

/* Backwards compatibility define --- to be removed */
#define v4l2_subdev_edid v4l2_edid

#define VIDIOC_SUBDEV_QUERYCAP			_IOR('V',  0, struct v4l2_subdev_capability)
#define VIDIOC_SUBDEV_G_FMT			_IOWR('V',  4, struct v4l2_subdev_format)
#define VIDIOC_SUBDEV_S_FMT			_IOWR('V',  5, struct v4l2_subdev_format)
#define VIDIOC_SUBDEV_G_FRAME_INTERVAL		_IOWR('V', 21, struct v4l2_subdev_frame_interval)
#define VIDIOC_SUBDEV_S_FRAME_INTERVAL		_IOWR('V', 22, struct v4l2_subdev_frame_interval)
#define VIDIOC_SUBDEV_ENUM_MBUS_CODE		_IOWR('V',  2, struct v4l2_subdev_mbus_code_enum)
#define VIDIOC_SUBDEV_ENUM_FRAME_SIZE		_IOWR('V', 74, struct v4l2_subdev_frame_size_enum)
#define VIDIOC_SUBDEV_ENUM_FRAME_INTERVAL	_IOWR('V', 75, struct v4l2_subdev_frame_interval_enum)
#define VIDIOC_SUBDEV_G_CROP			_IOWR('V', 59, struct v4l2_subdev_crop)
#define VIDIOC_SUBDEV_S_CROP			_IOWR('V', 60, struct v4l2_subdev_crop)
#define VIDIOC_SUBDEV_G_SELECTION		_IOWR('V', 61, struct v4l2_subdev_selection)
#define VIDIOC_SUBDEV_S_SELECTION		_IOWR('V', 62, struct v4l2_subdev_selection)
#define VIDIOC_SUBDEV_G_ROUTING			_IOWR('V', 38, struct v4l2_subdev_routing)
#define VIDIOC_SUBDEV_S_ROUTING			_IOWR('V', 39, struct v4l2_subdev_routing)
#define VIDIOC_SUBDEV_G_CLIENT_CAP		_IOR('V',  101, struct v4l2_subdev_client_capability)
#define VIDIOC_SUBDEV_S_CLIENT_CAP		_IOWR('V',  102, struct v4l2_subdev_client_capability)

/* The following ioctls are identical to the ioctls in videodev2.h */
#define VIDIOC_SUBDEV_G_STD			_IOR('V', 23, v4l2_std_id)
#define VIDIOC_SUBDEV_S_STD			_IOW('V', 24, v4l2_std_id)
#define VIDIOC_SUBDEV_ENUMSTD			_IOWR('V', 25, struct v4l2_standard)
#define VIDIOC_SUBDEV_G_EDID			_IOWR('V', 40, struct v4l2_edid)
#define VIDIOC_SUBDEV_S_EDID			_IOWR('V', 41, struct v4l2_edid)
#define VIDIOC_SUBDEV_QUERYSTD			_IOR('V', 63, v4l2_std_id)
#define VIDIOC_SUBDEV_S_DV_TIMINGS		_IOWR('V', 87, struct v4l2_dv_timings)
#define VIDIOC_SUBDEV_G_DV_TIMINGS		_IOWR('V', 88, struct v4l2_dv_timings)
#define VIDIOC_SUBDEV_ENUM_DV_TIMINGS		_IOWR('V', 98, struct v4l2_enum_dv_timings)
#define VIDIOC_SUBDEV_QUERY_DV_TIMINGS		_IOR('V', 99, struct v4l2_dv_timings)
#define VIDIOC_SUBDEV_DV_TIMINGS_CAP		_IOWR('V', 100, struct v4l2_dv_timings_cap)

#endif
