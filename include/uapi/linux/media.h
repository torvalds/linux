/*
 * Multimedia device API
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

#ifndef __LINUX_MEDIA_H
#define __LINUX_MEDIA_H

#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/version.h>

#define MEDIA_API_VERSION	KERNEL_VERSION(0, 1, 0)

struct media_device_info {
	char driver[16];
	char model[32];
	char serial[40];
	char bus_info[32];
	__u32 media_version;
	__u32 hw_revision;
	__u32 driver_version;
	__u32 reserved[31];
};

#define MEDIA_ENT_ID_FLAG_NEXT		(1 << 31)

/*
 * Initial value to be used when a new entity is created
 * Drivers should change it to something useful
 */
#define MEDIA_ENT_T_UNKNOWN	0x00000000

/*
 * Base numbers for entity types
 *
 * Please notice that the huge gap of 16 bits for each base is overkill!
 * 8 bits is more than enough to avoid starving entity types for each
 * subsystem.
 *
 * However, It is kept this way just to avoid binary breakages with the
 * namespace provided on legacy versions of this header.
 */
#define MEDIA_ENT_T_DVB_BASE		0x00000000
#define MEDIA_ENT_T_V4L2_BASE		0x00010000
#define MEDIA_ENT_T_V4L2_SUBDEV_BASE	0x00020000

/*
 * V4L2 entities - Those are used for DMA (mmap/DMABUF) and
 *	read()/write() data I/O associated with the V4L2 devnodes.
 */
#define MEDIA_ENT_T_V4L2_VIDEO		(MEDIA_ENT_T_V4L2_BASE + 1)
	/*
	 * Please notice that numbers between MEDIA_ENT_T_V4L2_BASE + 2 and
	 * MEDIA_ENT_T_V4L2_BASE + 4 can't be used, as those values used
	 * to be declared for FB, ALSA and DVB entities.
	 * As those values were never actually used in practice, we're just
	 * adding them as backward compatibility macros and keeping the
	 * numberspace clean here. This way, we avoid breaking compilation,
	 * in the case of having some userspace application using the old
	 * symbols.
	 */
#define MEDIA_ENT_T_V4L2_VBI		(MEDIA_ENT_T_V4L2_BASE + 5)
#define MEDIA_ENT_T_V4L2_SWRADIO	(MEDIA_ENT_T_V4L2_BASE + 6)

/* V4L2 Sub-device entities */

/*
 * Subdevs are initialized with MEDIA_ENT_T_V4L2_SUBDEV_UNKNOWN,
 * in order to preserve backward compatibility.
 * Drivers should change to the proper subdev type before
 * registering the entity.
 */
#define MEDIA_ENT_T_V4L2_SUBDEV_UNKNOWN	MEDIA_ENT_T_V4L2_SUBDEV_BASE

#define MEDIA_ENT_T_V4L2_SUBDEV_SENSOR	(MEDIA_ENT_T_V4L2_SUBDEV_BASE + 1)
#define MEDIA_ENT_T_V4L2_SUBDEV_FLASH	(MEDIA_ENT_T_V4L2_SUBDEV_BASE + 2)
#define MEDIA_ENT_T_V4L2_SUBDEV_LENS	(MEDIA_ENT_T_V4L2_SUBDEV_BASE + 3)
	/* A converter of analogue video to its digital representation. */
#define MEDIA_ENT_T_V4L2_SUBDEV_DECODER	(MEDIA_ENT_T_V4L2_SUBDEV_BASE + 4)
	/* Tuner entity is actually both V4L2 and DVB subdev */
#define MEDIA_ENT_T_V4L2_SUBDEV_TUNER	(MEDIA_ENT_T_V4L2_SUBDEV_BASE + 5)

/* DVB entities */
#define MEDIA_ENT_T_DVB_DEMOD		(MEDIA_ENT_T_DVB_BASE + 1)
#define MEDIA_ENT_T_DVB_DEMUX		(MEDIA_ENT_T_DVB_BASE + 2)
#define MEDIA_ENT_T_DVB_TSOUT		(MEDIA_ENT_T_DVB_BASE + 3)
#define MEDIA_ENT_T_DVB_CA		(MEDIA_ENT_T_DVB_BASE + 4)
#define MEDIA_ENT_T_DVB_NET_DECAP	(MEDIA_ENT_T_DVB_BASE + 5)

#ifndef __KERNEL__
/* Legacy symbols used to avoid userspace compilation breakages */
#define MEDIA_ENT_TYPE_SHIFT		16
#define MEDIA_ENT_TYPE_MASK		0x00ff0000
#define MEDIA_ENT_SUBTYPE_MASK		0x0000ffff

#define MEDIA_ENT_T_DEVNODE		MEDIA_ENT_T_V4L2_BASE
#define MEDIA_ENT_T_V4L2_SUBDEV		MEDIA_ENT_T_V4L2_SUBDEV_BASE

#define MEDIA_ENT_T_DEVNODE_V4L		MEDIA_ENT_T_V4L2_VIDEO

#define MEDIA_ENT_T_DEVNODE_FB		(MEDIA_ENT_T_DEVNODE + 2)
#define MEDIA_ENT_T_DEVNODE_ALSA	(MEDIA_ENT_T_DEVNODE + 3)
#define MEDIA_ENT_T_DEVNODE_DVB		(MEDIA_ENT_T_DEVNODE + 4)
#endif

/* Entity types */

#define MEDIA_ENT_FL_DEFAULT		(1 << 0)

struct media_entity_desc {
	__u32 id;
	char name[32];
	__u32 type;
	__u32 revision;
	__u32 flags;
	__u32 group_id;
	__u16 pads;
	__u16 links;

	__u32 reserved[4];

	union {
		/* Node specifications */
		struct {
			__u32 major;
			__u32 minor;
		} dev;

#if 1
		/*
		 * TODO: this shouldn't have been added without
		 * actual drivers that use this. When the first real driver
		 * appears that sets this information, special attention
		 * should be given whether this information is 1) enough, and
		 * 2) can deal with udev rules that rename devices. The struct
		 * dev would not be sufficient for this since that does not
		 * contain the subdevice information. In addition, struct dev
		 * can only refer to a single device, and not to multiple (e.g.
		 * pcm and mixer devices).
		 *
		 * So for now mark this as a to do.
		 */
		struct {
			__u32 card;
			__u32 device;
			__u32 subdevice;
		} alsa;
#endif

#if 1
		/*
		 * DEPRECATED: previous node specifications. Kept just to
		 * avoid breaking compilation, but media_entity_desc.dev
		 * should be used instead. In particular, alsa and dvb
		 * fields below are wrong: for all devnodes, there should
		 * be just major/minor inside the struct, as this is enough
		 * to represent any devnode, no matter what type.
		 */
		struct {
			__u32 major;
			__u32 minor;
		} v4l;
		struct {
			__u32 major;
			__u32 minor;
		} fb;
		int dvb;
#endif

		/* Sub-device specifications */
		/* Nothing needed yet */
		__u8 raw[184];
	};
};

#define MEDIA_PAD_FL_SINK		(1 << 0)
#define MEDIA_PAD_FL_SOURCE		(1 << 1)
#define MEDIA_PAD_FL_MUST_CONNECT	(1 << 2)

struct media_pad_desc {
	__u32 entity;		/* entity ID */
	__u16 index;		/* pad index */
	__u32 flags;		/* pad flags */
	__u32 reserved[2];
};

#define MEDIA_LNK_FL_ENABLED		(1 << 0)
#define MEDIA_LNK_FL_IMMUTABLE		(1 << 1)
#define MEDIA_LNK_FL_DYNAMIC		(1 << 2)

struct media_link_desc {
	struct media_pad_desc source;
	struct media_pad_desc sink;
	__u32 flags;
	__u32 reserved[2];
};

struct media_links_enum {
	__u32 entity;
	/* Should have enough room for pads elements */
	struct media_pad_desc __user *pads;
	/* Should have enough room for links elements */
	struct media_link_desc __user *links;
	__u32 reserved[4];
};

/* Interface type ranges */

#define MEDIA_INTF_T_DVB_BASE	0x00000100
#define MEDIA_INTF_T_V4L_BASE	0x00000200

/* Interface types */

#define MEDIA_INTF_T_DVB_FE    	(MEDIA_INTF_T_DVB_BASE)
#define MEDIA_INTF_T_DVB_DEMUX  (MEDIA_INTF_T_DVB_BASE + 1)
#define MEDIA_INTF_T_DVB_DVR    (MEDIA_INTF_T_DVB_BASE + 2)
#define MEDIA_INTF_T_DVB_CA     (MEDIA_INTF_T_DVB_BASE + 3)
#define MEDIA_INTF_T_DVB_NET    (MEDIA_INTF_T_DVB_BASE + 4)

#define MEDIA_INTF_T_V4L_VIDEO  (MEDIA_INTF_T_V4L_BASE)
#define MEDIA_INTF_T_V4L_VBI    (MEDIA_INTF_T_V4L_BASE + 1)
#define MEDIA_INTF_T_V4L_RADIO  (MEDIA_INTF_T_V4L_BASE + 2)
#define MEDIA_INTF_T_V4L_SUBDEV (MEDIA_INTF_T_V4L_BASE + 3)
#define MEDIA_INTF_T_V4L_SWRADIO (MEDIA_INTF_T_V4L_BASE + 4)

/* TBD: declare the structs needed for the new G_TOPOLOGY ioctl */

#define MEDIA_IOC_DEVICE_INFO		_IOWR('|', 0x00, struct media_device_info)
#define MEDIA_IOC_ENUM_ENTITIES		_IOWR('|', 0x01, struct media_entity_desc)
#define MEDIA_IOC_ENUM_LINKS		_IOWR('|', 0x02, struct media_links_enum)
#define MEDIA_IOC_SETUP_LINK		_IOWR('|', 0x03, struct media_link_desc)

#endif /* __LINUX_MEDIA_H */
