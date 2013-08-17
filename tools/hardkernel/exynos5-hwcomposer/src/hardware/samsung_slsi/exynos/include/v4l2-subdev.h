/****************************************************************************
 ****************************************************************************
 ***
 ***   This header was automatically generated from a Linux kernel header
 ***   of the same name, to make information necessary for userspace to
 ***   call into the kernel available to libc.  It contains only constants,
 ***   structures, and macros generated from the original header, and thus,
 ***   contains no copyrightable information.
 ***
 ***   To edit the content of this header, modify the corresponding
 ***   source file (e.g. under external/kernel-headers/original/) then
 ***   run bionic/libc/kernel/tools/update_all.py
 ***
 ***   Any manual change here will be lost the next time this script will
 ***   be run. You've been warned!
 ***
 ****************************************************************************
 ****************************************************************************/
#ifndef __LINUX_V4L2_SUBDEV_H
#define __LINUX_V4L2_SUBDEV_H
#include <linux/ioctl.h>
#include <linux/types.h>
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#include "v4l2-mediabus.h"
enum v4l2_subdev_format_whence {
 V4L2_SUBDEV_FORMAT_TRY = 0,
 V4L2_SUBDEV_FORMAT_ACTIVE = 1,
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
};
struct v4l2_subdev_format {
 __u32 which;
 __u32 pad;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 struct v4l2_mbus_framefmt format;
 __u32 reserved[8];
};
struct v4l2_subdev_crop {
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 __u32 which;
 __u32 pad;
 struct v4l2_rect rect;
 __u32 reserved[8];
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
};
struct v4l2_subdev_mbus_code_enum {
 __u32 pad;
 __u32 index;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 __u32 code;
 __u32 reserved[9];
};
struct v4l2_subdev_frame_size_enum {
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 __u32 index;
 __u32 pad;
 __u32 code;
 __u32 min_width;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 __u32 max_width;
 __u32 min_height;
 __u32 max_height;
 __u32 reserved[9];
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
};
struct v4l2_subdev_frame_interval {
 __u32 pad;
 struct v4l2_fract interval;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 __u32 reserved[9];
};
struct v4l2_subdev_frame_interval_enum {
 __u32 index;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 __u32 pad;
 __u32 code;
 __u32 width;
 __u32 height;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 struct v4l2_fract interval;
 __u32 reserved[9];
};
#define VIDIOC_SUBDEV_G_FMT _IOWR('V', 4, struct v4l2_subdev_format)
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define VIDIOC_SUBDEV_S_FMT _IOWR('V', 5, struct v4l2_subdev_format)
#define VIDIOC_SUBDEV_G_FRAME_INTERVAL   _IOWR('V', 21, struct v4l2_subdev_frame_interval)
#define VIDIOC_SUBDEV_S_FRAME_INTERVAL   _IOWR('V', 22, struct v4l2_subdev_frame_interval)
#define VIDIOC_SUBDEV_ENUM_MBUS_CODE   _IOWR('V', 2, struct v4l2_subdev_mbus_code_enum)
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define VIDIOC_SUBDEV_ENUM_FRAME_SIZE   _IOWR('V', 74, struct v4l2_subdev_frame_size_enum)
#define VIDIOC_SUBDEV_ENUM_FRAME_INTERVAL   _IOWR('V', 75, struct v4l2_subdev_frame_interval_enum)
#define VIDIOC_SUBDEV_G_CROP _IOWR('V', 59, struct v4l2_subdev_crop)
#define VIDIOC_SUBDEV_S_CROP _IOWR('V', 60, struct v4l2_subdev_crop)
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#endif
