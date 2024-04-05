/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * vdpa device management interface
 * Copyright (c) 2020 Mellanox Technologies Ltd. All rights reserved.
 */

#ifndef _UAPI_LINUX_VDPA_H_
#define _UAPI_LINUX_VDPA_H_

#define VDPA_GENL_NAME "vdpa"
#define VDPA_GENL_VERSION 0x1

enum vdpa_command {
	VDPA_CMD_UNSPEC,
	VDPA_CMD_MGMTDEV_NEW,
	VDPA_CMD_MGMTDEV_GET,		/* can dump */
	VDPA_CMD_DEV_NEW,
	VDPA_CMD_DEV_DEL,
	VDPA_CMD_DEV_GET,		/* can dump */
	VDPA_CMD_DEV_CONFIG_GET,	/* can dump */
	VDPA_CMD_DEV_VSTATS_GET,
};

enum vdpa_attr {
	VDPA_ATTR_UNSPEC,

	/* Pad attribute for 64b alignment */
	VDPA_ATTR_PAD = VDPA_ATTR_UNSPEC,

	/* bus name (optional) + dev name together make the parent device handle */
	VDPA_ATTR_MGMTDEV_BUS_NAME,		/* string */
	VDPA_ATTR_MGMTDEV_DEV_NAME,		/* string */
	VDPA_ATTR_MGMTDEV_SUPPORTED_CLASSES,	/* u64 */

	VDPA_ATTR_DEV_NAME,			/* string */
	VDPA_ATTR_DEV_ID,			/* u32 */
	VDPA_ATTR_DEV_VENDOR_ID,		/* u32 */
	VDPA_ATTR_DEV_MAX_VQS,			/* u32 */
	VDPA_ATTR_DEV_MAX_VQ_SIZE,		/* u16 */
	VDPA_ATTR_DEV_MIN_VQ_SIZE,		/* u16 */

	VDPA_ATTR_DEV_NET_CFG_MACADDR,		/* binary */
	VDPA_ATTR_DEV_NET_STATUS,		/* u8 */
	VDPA_ATTR_DEV_NET_CFG_MAX_VQP,		/* u16 */
	VDPA_ATTR_DEV_NET_CFG_MTU,		/* u16 */

	VDPA_ATTR_DEV_NEGOTIATED_FEATURES,	/* u64 */
	VDPA_ATTR_DEV_MGMTDEV_MAX_VQS,		/* u32 */
	/* virtio features that are supported by the vDPA management device */
	VDPA_ATTR_DEV_SUPPORTED_FEATURES,	/* u64 */

	VDPA_ATTR_DEV_QUEUE_INDEX,              /* u32 */
	VDPA_ATTR_DEV_VENDOR_ATTR_NAME,		/* string */
	VDPA_ATTR_DEV_VENDOR_ATTR_VALUE,        /* u64 */

	/* virtio features that are provisioned to the vDPA device */
	VDPA_ATTR_DEV_FEATURES,                 /* u64 */

	VDPA_ATTR_DEV_BLK_CFG_CAPACITY,		/* u64 */
	VDPA_ATTR_DEV_BLK_CFG_SEG_SIZE,		/* u32 */
	VDPA_ATTR_DEV_BLK_CFG_BLK_SIZE,		/* u32 */
	VDPA_ATTR_DEV_BLK_CFG_SEG_MAX,		/* u32 */
	VDPA_ATTR_DEV_BLK_CFG_NUM_QUEUES,	/* u16 */
	VDPA_ATTR_DEV_BLK_CFG_PHY_BLK_EXP,	/* u8 */
	VDPA_ATTR_DEV_BLK_CFG_ALIGN_OFFSET,	/* u8 */
	VDPA_ATTR_DEV_BLK_CFG_MIN_IO_SIZE,	/* u16 */
	VDPA_ATTR_DEV_BLK_CFG_OPT_IO_SIZE,	/* u32 */
	VDPA_ATTR_DEV_BLK_CFG_MAX_DISCARD_SEC,	/* u32 */
	VDPA_ATTR_DEV_BLK_CFG_MAX_DISCARD_SEG,	/* u32 */
	VDPA_ATTR_DEV_BLK_CFG_DISCARD_SEC_ALIGN,/* u32 */
	VDPA_ATTR_DEV_BLK_CFG_MAX_WRITE_ZEROES_SEC,	/* u32 */
	VDPA_ATTR_DEV_BLK_CFG_MAX_WRITE_ZEROES_SEG,	/* u32 */
	VDPA_ATTR_DEV_BLK_CFG_READ_ONLY,		/* u8 */
	VDPA_ATTR_DEV_BLK_CFG_FLUSH,		/* u8 */

	/* new attributes must be added above here */
	VDPA_ATTR_MAX,
};

#endif
