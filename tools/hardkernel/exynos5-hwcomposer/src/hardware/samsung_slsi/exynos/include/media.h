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
#ifndef __LINUX_MEDIA_H
#define __LINUX_MEDIA_H
#include <linux/ioctl.h>
#include <linux/types.h>
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#include <linux/version.h>
#define MEDIA_API_VERSION KERNEL_VERSION(0, 1, 0)
struct media_device_info {
 char driver[16];
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 char model[32];
 char serial[40];
 char bus_info[32];
 __u32 media_version;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 __u32 hw_revision;
 __u32 driver_version;
 __u32 reserved[31];
};
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define MEDIA_ENT_ID_FLAG_NEXT (1 << 31)
#define MEDIA_ENT_TYPE_SHIFT 16
#define MEDIA_ENT_TYPE_MASK 0x00ff0000
#define MEDIA_ENT_SUBTYPE_MASK 0x0000ffff
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define MEDIA_ENT_T_DEVNODE (1 << MEDIA_ENT_TYPE_SHIFT)
#define MEDIA_ENT_T_DEVNODE_V4L (MEDIA_ENT_T_DEVNODE + 1)
#define MEDIA_ENT_T_DEVNODE_FB (MEDIA_ENT_T_DEVNODE + 2)
#define MEDIA_ENT_T_DEVNODE_ALSA (MEDIA_ENT_T_DEVNODE + 3)
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define MEDIA_ENT_T_DEVNODE_DVB (MEDIA_ENT_T_DEVNODE + 4)
#define MEDIA_ENT_T_V4L2_SUBDEV (2 << MEDIA_ENT_TYPE_SHIFT)
#define MEDIA_ENT_T_V4L2_SUBDEV_SENSOR (MEDIA_ENT_T_V4L2_SUBDEV + 1)
#define MEDIA_ENT_T_V4L2_SUBDEV_FLASH (MEDIA_ENT_T_V4L2_SUBDEV + 2)
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define MEDIA_ENT_T_V4L2_SUBDEV_LENS (MEDIA_ENT_T_V4L2_SUBDEV + 3)
#define MEDIA_ENT_FL_DEFAULT (1 << 0)
struct media_entity_desc {
 __u32 id;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 char name[32];
 __u32 type;
 __u32 revision;
 __u32 flags;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 __u32 group_id;
 __u16 pads;
 __u16 links;
 __u32 reserved[4];
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 union {
 struct {
 __u32 major;
 __u32 minor;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 } v4l;
 struct {
 __u32 major;
 __u32 minor;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 } fb;
 struct {
 __u32 card;
 __u32 device;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 __u32 subdevice;
 } alsa;
 int dvb;
 __u8 raw[184];
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 };
};
#define MEDIA_PAD_FL_SINK (1 << 0)
#define MEDIA_PAD_FL_SOURCE (1 << 1)
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
struct media_pad_desc {
 __u32 entity;
 __u16 index;
 __u32 flags;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 __u32 reserved[2];
};
#define MEDIA_LNK_FL_ENABLED (1 << 0)
#define MEDIA_LNK_FL_IMMUTABLE (1 << 1)
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define MEDIA_LNK_FL_DYNAMIC (1 << 2)
struct media_link_desc {
 struct media_pad_desc source;
 struct media_pad_desc sink;
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 __u32 flags;
 __u32 reserved[2];
};
struct media_links_enum {
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
 __u32 entity;
#if defined(ANDROID)
 struct media_pad_desc __user *pads;
 struct media_link_desc __user *links;
#else
 struct media_pad_desc *pads;
 struct media_link_desc *links;
#endif
 __u32 reserved[4];
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
};
#define MEDIA_IOC_DEVICE_INFO _IOWR('|', 0x00, struct media_device_info)
#define MEDIA_IOC_ENUM_ENTITIES _IOWR('|', 0x01, struct media_entity_desc)
#define MEDIA_IOC_ENUM_LINKS _IOWR('|', 0x02, struct media_links_enum)
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define MEDIA_IOC_SETUP_LINK _IOWR('|', 0x03, struct media_link_desc)
#endif
