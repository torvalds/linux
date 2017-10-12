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

#ifndef __KERNEL__
#include <stdint.h>
#endif
#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/version.h>

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
#define MEDIA_ENT_F_UNKNOWN	0x00000000

/*
 * Base number ranges for entity functions
 *
 * NOTE: those ranges and entity function number are phased just to
 * make it easier to maintain this file. Userspace should not rely on
 * the ranges to identify a group of function types, as newer
 * functions can be added with any name within the full u32 range.
 */
#define MEDIA_ENT_F_BASE		0x00000000
#define MEDIA_ENT_F_OLD_BASE		0x00010000
#define MEDIA_ENT_F_OLD_SUBDEV_BASE	0x00020000

/*
 * DVB entities
 */
#define MEDIA_ENT_F_DTV_DEMOD		(MEDIA_ENT_F_BASE + 0x00001)
#define MEDIA_ENT_F_TS_DEMUX		(MEDIA_ENT_F_BASE + 0x00002)
#define MEDIA_ENT_F_DTV_CA		(MEDIA_ENT_F_BASE + 0x00003)
#define MEDIA_ENT_F_DTV_NET_DECAP	(MEDIA_ENT_F_BASE + 0x00004)

/*
 * I/O entities
 */
#define MEDIA_ENT_F_IO_DTV		(MEDIA_ENT_F_BASE + 0x01001)
#define MEDIA_ENT_F_IO_VBI		(MEDIA_ENT_F_BASE + 0x01002)
#define MEDIA_ENT_F_IO_SWRADIO		(MEDIA_ENT_F_BASE + 0x01003)

/*
 * Analog TV IF-PLL decoders
 *
 * It is a responsibility of the master/bridge drivers to create links
 * for MEDIA_ENT_F_IF_VID_DECODER and MEDIA_ENT_F_IF_AUD_DECODER.
 */
#define MEDIA_ENT_F_IF_VID_DECODER	(MEDIA_ENT_F_BASE + 0x02001)
#define MEDIA_ENT_F_IF_AUD_DECODER	(MEDIA_ENT_F_BASE + 0x02002)

/*
 * Audio Entity Functions
 */
#define MEDIA_ENT_F_AUDIO_CAPTURE	(MEDIA_ENT_F_BASE + 0x03001)
#define MEDIA_ENT_F_AUDIO_PLAYBACK	(MEDIA_ENT_F_BASE + 0x03002)
#define MEDIA_ENT_F_AUDIO_MIXER		(MEDIA_ENT_F_BASE + 0x03003)

/*
 * Processing entities
 */
#define MEDIA_ENT_F_PROC_VIDEO_COMPOSER		(MEDIA_ENT_F_BASE + 0x4001)
#define MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER	(MEDIA_ENT_F_BASE + 0x4002)
#define MEDIA_ENT_F_PROC_VIDEO_PIXEL_ENC_CONV	(MEDIA_ENT_F_BASE + 0x4003)
#define MEDIA_ENT_F_PROC_VIDEO_LUT		(MEDIA_ENT_F_BASE + 0x4004)
#define MEDIA_ENT_F_PROC_VIDEO_SCALER		(MEDIA_ENT_F_BASE + 0x4005)
#define MEDIA_ENT_F_PROC_VIDEO_STATISTICS	(MEDIA_ENT_F_BASE + 0x4006)

/*
 * Switch and bridge entitites
 */
#define MEDIA_ENT_F_VID_MUX			(MEDIA_ENT_F_BASE + 0x5001)
#define MEDIA_ENT_F_VID_IF_BRIDGE		(MEDIA_ENT_F_BASE + 0x5002)

/*
 * Connectors
 */
/* It is a responsibility of the entity drivers to add connectors and links */
#ifdef __KERNEL__
	/*
	 * For now, it should not be used in userspace, as some
	 * definitions may change
	 */

#define MEDIA_ENT_F_CONN_RF		(MEDIA_ENT_F_BASE + 0x30001)
#define MEDIA_ENT_F_CONN_SVIDEO		(MEDIA_ENT_F_BASE + 0x30002)
#define MEDIA_ENT_F_CONN_COMPOSITE	(MEDIA_ENT_F_BASE + 0x30003)

#endif

/*
 * Don't touch on those. The ranges MEDIA_ENT_F_OLD_BASE and
 * MEDIA_ENT_F_OLD_SUBDEV_BASE are kept to keep backward compatibility
 * with the legacy v1 API.The number range is out of range by purpose:
 * several previously reserved numbers got excluded from this range.
 *
 * Subdevs are initialized with MEDIA_ENT_T_V4L2_SUBDEV_UNKNOWN,
 * in order to preserve backward compatibility.
 * Drivers must change to the proper subdev type before
 * registering the entity.
 */

#define MEDIA_ENT_F_IO_V4L  		(MEDIA_ENT_F_OLD_BASE + 1)

#define MEDIA_ENT_F_CAM_SENSOR		(MEDIA_ENT_F_OLD_SUBDEV_BASE + 1)
#define MEDIA_ENT_F_FLASH		(MEDIA_ENT_F_OLD_SUBDEV_BASE + 2)
#define MEDIA_ENT_F_LENS		(MEDIA_ENT_F_OLD_SUBDEV_BASE + 3)
#define MEDIA_ENT_F_ATV_DECODER		(MEDIA_ENT_F_OLD_SUBDEV_BASE + 4)
/*
 * It is a responsibility of the master/bridge drivers to add connectors
 * and links for MEDIA_ENT_F_TUNER. Please notice that some old tuners
 * may require the usage of separate I2C chips to decode analog TV signals,
 * when the master/bridge chipset doesn't have its own TV standard decoder.
 * On such cases, the IF-PLL staging is mapped via one or two entities:
 * MEDIA_ENT_F_IF_VID_DECODER and/or MEDIA_ENT_F_IF_AUD_DECODER.
 */
#define MEDIA_ENT_F_TUNER		(MEDIA_ENT_F_OLD_SUBDEV_BASE + 5)

#define MEDIA_ENT_F_V4L2_SUBDEV_UNKNOWN	MEDIA_ENT_F_OLD_SUBDEV_BASE

#if !defined(__KERNEL__) || defined(__NEED_MEDIA_LEGACY_API)

/*
 * Legacy symbols used to avoid userspace compilation breakages
 *
 * Those symbols map the entity function into types and should be
 * used only on legacy programs for legacy hardware. Don't rely
 * on those for MEDIA_IOC_G_TOPOLOGY.
 */
#define MEDIA_ENT_TYPE_SHIFT		16
#define MEDIA_ENT_TYPE_MASK		0x00ff0000
#define MEDIA_ENT_SUBTYPE_MASK		0x0000ffff

/* End of the old subdev reserved numberspace */
#define MEDIA_ENT_T_DEVNODE_UNKNOWN	(MEDIA_ENT_T_DEVNODE | \
					 MEDIA_ENT_SUBTYPE_MASK)

#define MEDIA_ENT_T_DEVNODE		MEDIA_ENT_F_OLD_BASE
#define MEDIA_ENT_T_DEVNODE_V4L		MEDIA_ENT_F_IO_V4L
#define MEDIA_ENT_T_DEVNODE_FB		(MEDIA_ENT_T_DEVNODE + 2)
#define MEDIA_ENT_T_DEVNODE_ALSA	(MEDIA_ENT_T_DEVNODE + 3)
#define MEDIA_ENT_T_DEVNODE_DVB		(MEDIA_ENT_T_DEVNODE + 4)

#define MEDIA_ENT_T_UNKNOWN		MEDIA_ENT_F_UNKNOWN
#define MEDIA_ENT_T_V4L2_VIDEO		MEDIA_ENT_F_IO_V4L
#define MEDIA_ENT_T_V4L2_SUBDEV		MEDIA_ENT_F_V4L2_SUBDEV_UNKNOWN
#define MEDIA_ENT_T_V4L2_SUBDEV_SENSOR	MEDIA_ENT_F_CAM_SENSOR
#define MEDIA_ENT_T_V4L2_SUBDEV_FLASH	MEDIA_ENT_F_FLASH
#define MEDIA_ENT_T_V4L2_SUBDEV_LENS	MEDIA_ENT_F_LENS
#define MEDIA_ENT_T_V4L2_SUBDEV_DECODER	MEDIA_ENT_F_ATV_DECODER
#define MEDIA_ENT_T_V4L2_SUBDEV_TUNER	MEDIA_ENT_F_TUNER

/* Obsolete symbol for media_version, no longer used in the kernel */
#define MEDIA_API_VERSION		KERNEL_VERSION(0, 1, 0)
#endif

/* Entity flags */
#define MEDIA_ENT_FL_DEFAULT		(1 << 0)
#define MEDIA_ENT_FL_CONNECTOR		(1 << 1)

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

#define MEDIA_LNK_FL_LINK_TYPE		(0xf << 28)
#  define MEDIA_LNK_FL_DATA_LINK	(0 << 28)
#  define MEDIA_LNK_FL_INTERFACE_LINK	(1 << 28)

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
#define MEDIA_INTF_T_ALSA_BASE	0x00000300

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
#define MEDIA_INTF_T_V4L_TOUCH	(MEDIA_INTF_T_V4L_BASE + 5)

#define MEDIA_INTF_T_ALSA_PCM_CAPTURE   (MEDIA_INTF_T_ALSA_BASE)
#define MEDIA_INTF_T_ALSA_PCM_PLAYBACK  (MEDIA_INTF_T_ALSA_BASE + 1)
#define MEDIA_INTF_T_ALSA_CONTROL       (MEDIA_INTF_T_ALSA_BASE + 2)
#define MEDIA_INTF_T_ALSA_COMPRESS      (MEDIA_INTF_T_ALSA_BASE + 3)
#define MEDIA_INTF_T_ALSA_RAWMIDI       (MEDIA_INTF_T_ALSA_BASE + 4)
#define MEDIA_INTF_T_ALSA_HWDEP         (MEDIA_INTF_T_ALSA_BASE + 5)
#define MEDIA_INTF_T_ALSA_SEQUENCER     (MEDIA_INTF_T_ALSA_BASE + 6)
#define MEDIA_INTF_T_ALSA_TIMER         (MEDIA_INTF_T_ALSA_BASE + 7)

/*
 * MC next gen API definitions
 *
 * NOTE: The declarations below are close to the MC RFC for the Media
 *	 Controller, the next generation. Yet, there are a few adjustments
 *	 to do, as we want to be able to have a functional API before
 *	 the MC properties change. Those will be properly marked below.
 *	 Please also notice that I removed "num_pads", "num_links",
 *	 from the proposal, as a proper userspace application will likely
 *	 use lists for pads/links, just as we intend to do in Kernelspace.
 *	 The API definition should be freed from fields that are bound to
 *	 some specific data structure.
 *
 * FIXME: Currently, I opted to name the new types as "media_v2", as this
 *	  won't cause any conflict with the Kernelspace namespace, nor with
 *	  the previous kAPI media_*_desc namespace. This can be changed
 *	  later, before the adding this API upstream.
 */


struct media_v2_entity {
	__u32 id;
	char name[64];		/* FIXME: move to a property? (RFC says so) */
	__u32 function;		/* Main function of the entity */
	__u32 reserved[6];
} __attribute__ ((packed));

/* Should match the specific fields at media_intf_devnode */
struct media_v2_intf_devnode {
	__u32 major;
	__u32 minor;
} __attribute__ ((packed));

struct media_v2_interface {
	__u32 id;
	__u32 intf_type;
	__u32 flags;
	__u32 reserved[9];

	union {
		struct media_v2_intf_devnode devnode;
		__u32 raw[16];
	};
} __attribute__ ((packed));

struct media_v2_pad {
	__u32 id;
	__u32 entity_id;
	__u32 flags;
	__u32 reserved[5];
} __attribute__ ((packed));

struct media_v2_link {
	__u32 id;
	__u32 source_id;
	__u32 sink_id;
	__u32 flags;
	__u32 reserved[6];
} __attribute__ ((packed));

struct media_v2_topology {
	__u64 topology_version;

	__u32 num_entities;
	__u32 reserved1;
	__u64 ptr_entities;

	__u32 num_interfaces;
	__u32 reserved2;
	__u64 ptr_interfaces;

	__u32 num_pads;
	__u32 reserved3;
	__u64 ptr_pads;

	__u32 num_links;
	__u32 reserved4;
	__u64 ptr_links;
} __attribute__ ((packed));

/* ioctls */

#define MEDIA_IOC_DEVICE_INFO		_IOWR('|', 0x00, struct media_device_info)
#define MEDIA_IOC_ENUM_ENTITIES		_IOWR('|', 0x01, struct media_entity_desc)
#define MEDIA_IOC_ENUM_LINKS		_IOWR('|', 0x02, struct media_links_enum)
#define MEDIA_IOC_SETUP_LINK		_IOWR('|', 0x03, struct media_link_desc)
#define MEDIA_IOC_G_TOPOLOGY		_IOWR('|', 0x04, struct media_v2_topology)

#endif /* __LINUX_MEDIA_H */
