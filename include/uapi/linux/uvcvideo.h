/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __LINUX_UVCVIDEO_H_
#define __LINUX_UVCVIDEO_H_

#include <linux/ioctl.h>
#include <linux/types.h>

/*
 * Dynamic controls
 */

/* Data types for UVC control data */
#define UVC_CTRL_DATA_TYPE_RAW		0
#define UVC_CTRL_DATA_TYPE_SIGNED	1
#define UVC_CTRL_DATA_TYPE_UNSIGNED	2
#define UVC_CTRL_DATA_TYPE_BOOLEAN	3
#define UVC_CTRL_DATA_TYPE_ENUM		4
#define UVC_CTRL_DATA_TYPE_BITMASK	5

/* Control flags */
#define UVC_CTRL_FLAG_SET_CUR		(1 << 0)
#define UVC_CTRL_FLAG_GET_CUR		(1 << 1)
#define UVC_CTRL_FLAG_GET_MIN		(1 << 2)
#define UVC_CTRL_FLAG_GET_MAX		(1 << 3)
#define UVC_CTRL_FLAG_GET_RES		(1 << 4)
#define UVC_CTRL_FLAG_GET_DEF		(1 << 5)
/* Control should be saved at suspend and restored at resume. */
#define UVC_CTRL_FLAG_RESTORE		(1 << 6)
/* Control can be updated by the camera. */
#define UVC_CTRL_FLAG_AUTO_UPDATE	(1 << 7)
/* Control supports asynchronous reporting */
#define UVC_CTRL_FLAG_ASYNCHRONOUS	(1 << 8)

#define UVC_CTRL_FLAG_GET_RANGE \
	(UVC_CTRL_FLAG_GET_CUR | UVC_CTRL_FLAG_GET_MIN | \
	 UVC_CTRL_FLAG_GET_MAX | UVC_CTRL_FLAG_GET_RES | \
	 UVC_CTRL_FLAG_GET_DEF)

struct uvc_menu_info {
	__u32 value;
	__u8 name[32];
};

struct uvc_xu_control_mapping {
	__u32 id;
	__u8 name[32];
	__u8 entity[16];
	__u8 selector;

	__u8 size;
	__u8 offset;
	__u32 v4l2_type;
	__u32 data_type;

	struct uvc_menu_info __user *menu_info;
	__u32 menu_count;

	__u32 reserved[4];
};

struct uvc_xu_control_query {
	__u8 unit;
	__u8 selector;
	__u8 query;		/* Video Class-Specific Request Code, */
				/* defined in linux/usb/video.h A.8.  */
	__u16 size;
	__u8 __user *data;
};

#define UVCIOC_CTRL_MAP		_IOWR('u', 0x20, struct uvc_xu_control_mapping)
#define UVCIOC_CTRL_QUERY	_IOWR('u', 0x21, struct uvc_xu_control_query)

/*
 * Metadata node
 */

/**
 * struct uvc_meta_buf - metadata buffer building block
 * @ns: system timestamp of the payload in nanoseconds
 * @sof: USB Frame Number
 * @length: length of the payload header
 * @flags: payload header flags
 * @buf: optional device-specific header data
 *
 * UVC metadata nodes fill buffers with possibly multiple instances of this
 * struct. The first two fields are added by the driver, they can be used for
 * clock synchronisation. The rest is an exact copy of a UVC payload header.
 * Only complete objects with complete buffers are included. Therefore it's
 * always sizeof(meta->ns) + sizeof(meta->sof) + meta->length bytes large.
 */
struct uvc_meta_buf {
	__u64 ns;
	__u16 sof;
	__u8 length;
	__u8 flags;
	__u8 buf[];
} __packed;

#endif
