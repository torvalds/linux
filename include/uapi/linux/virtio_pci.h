/*
 * Virtio PCI driver
 *
 * This module allows virtio devices to be used over a virtual PCI device.
 * This can be used with QEMU based VMMs like KVM or Xen.
 *
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Anthony Liguori  <aliguori@us.ibm.com>
 *
 * This header is BSD licensed so anyone can use the definitions to implement
 * compatible drivers/servers.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of IBM nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _LINUX_VIRTIO_PCI_H
#define _LINUX_VIRTIO_PCI_H

#include <linux/types.h>
#include <linux/kernel.h>

#ifndef VIRTIO_PCI_NO_LEGACY

/* A 32-bit r/o bitmask of the features supported by the host */
#define VIRTIO_PCI_HOST_FEATURES	0

/* A 32-bit r/w bitmask of features activated by the guest */
#define VIRTIO_PCI_GUEST_FEATURES	4

/* A 32-bit r/w PFN for the currently selected queue */
#define VIRTIO_PCI_QUEUE_PFN		8

/* A 16-bit r/o queue size for the currently selected queue */
#define VIRTIO_PCI_QUEUE_NUM		12

/* A 16-bit r/w queue selector */
#define VIRTIO_PCI_QUEUE_SEL		14

/* A 16-bit r/w queue notifier */
#define VIRTIO_PCI_QUEUE_NOTIFY		16

/* An 8-bit device status register.  */
#define VIRTIO_PCI_STATUS		18

/* An 8-bit r/o interrupt status register.  Reading the value will return the
 * current contents of the ISR and will also clear it.  This is effectively
 * a read-and-acknowledge. */
#define VIRTIO_PCI_ISR			19

/* MSI-X registers: only enabled if MSI-X is enabled. */
/* A 16-bit vector for configuration changes. */
#define VIRTIO_MSI_CONFIG_VECTOR        20
/* A 16-bit vector for selected queue notifications. */
#define VIRTIO_MSI_QUEUE_VECTOR         22

/* The remaining space is defined by each driver as the per-driver
 * configuration space */
#define VIRTIO_PCI_CONFIG_OFF(msix_enabled)	((msix_enabled) ? 24 : 20)
/* Deprecated: please use VIRTIO_PCI_CONFIG_OFF instead */
#define VIRTIO_PCI_CONFIG(dev)	VIRTIO_PCI_CONFIG_OFF((dev)->msix_enabled)

/* Virtio ABI version, this must match exactly */
#define VIRTIO_PCI_ABI_VERSION		0

/* How many bits to shift physical queue address written to QUEUE_PFN.
 * 12 is historical, and due to x86 page size. */
#define VIRTIO_PCI_QUEUE_ADDR_SHIFT	12

/* The alignment to use between consumer and producer parts of vring.
 * x86 pagesize again. */
#define VIRTIO_PCI_VRING_ALIGN		4096

#endif /* VIRTIO_PCI_NO_LEGACY */

/* The bit of the ISR which indicates a device configuration change. */
#define VIRTIO_PCI_ISR_CONFIG		0x2
/* Vector value used to disable MSI for queue */
#define VIRTIO_MSI_NO_VECTOR            0xffff

#ifndef VIRTIO_PCI_NO_MODERN

/* IDs for different capabilities.  Must all exist. */

/* Common configuration */
#define VIRTIO_PCI_CAP_COMMON_CFG	1
/* Notifications */
#define VIRTIO_PCI_CAP_NOTIFY_CFG	2
/* ISR access */
#define VIRTIO_PCI_CAP_ISR_CFG		3
/* Device specific configuration */
#define VIRTIO_PCI_CAP_DEVICE_CFG	4
/* PCI configuration access */
#define VIRTIO_PCI_CAP_PCI_CFG		5
/* Additional shared memory capability */
#define VIRTIO_PCI_CAP_SHARED_MEMORY_CFG 8
/* PCI vendor data configuration */
#define VIRTIO_PCI_CAP_VENDOR_CFG	9

/* This is the PCI capability header: */
struct virtio_pci_cap {
	__u8 cap_vndr;		/* Generic PCI field: PCI_CAP_ID_VNDR */
	__u8 cap_next;		/* Generic PCI field: next ptr. */
	__u8 cap_len;		/* Generic PCI field: capability length */
	__u8 cfg_type;		/* Identifies the structure. */
	__u8 bar;		/* Where to find it. */
	__u8 id;		/* Multiple capabilities of the same type */
	__u8 padding[2];	/* Pad to full dword. */
	__le32 offset;		/* Offset within bar. */
	__le32 length;		/* Length of the structure, in bytes. */
};

/* This is the PCI vendor data capability header: */
struct virtio_pci_vndr_data {
	__u8 cap_vndr;		/* Generic PCI field: PCI_CAP_ID_VNDR */
	__u8 cap_next;		/* Generic PCI field: next ptr. */
	__u8 cap_len;		/* Generic PCI field: capability length */
	__u8 cfg_type;		/* Identifies the structure. */
	__u16 vendor_id;	/* Identifies the vendor-specific format. */
	/* For Vendor Definition */
	/* Pads structure to a multiple of 4 bytes */
	/* Reads must not have side effects */
};

struct virtio_pci_cap64 {
	struct virtio_pci_cap cap;
	__le32 offset_hi;             /* Most sig 32 bits of offset */
	__le32 length_hi;             /* Most sig 32 bits of length */
};

struct virtio_pci_notify_cap {
	struct virtio_pci_cap cap;
	__le32 notify_off_multiplier;	/* Multiplier for queue_notify_off. */
};

/* Fields in VIRTIO_PCI_CAP_COMMON_CFG: */
struct virtio_pci_common_cfg {
	/* About the whole device. */
	__le32 device_feature_select;	/* read-write */
	__le32 device_feature;		/* read-only */
	__le32 guest_feature_select;	/* read-write */
	__le32 guest_feature;		/* read-write */
	__le16 msix_config;		/* read-write */
	__le16 num_queues;		/* read-only */
	__u8 device_status;		/* read-write */
	__u8 config_generation;		/* read-only */

	/* About a specific virtqueue. */
	__le16 queue_select;		/* read-write */
	__le16 queue_size;		/* read-write, power of 2. */
	__le16 queue_msix_vector;	/* read-write */
	__le16 queue_enable;		/* read-write */
	__le16 queue_notify_off;	/* read-only */
	__le32 queue_desc_lo;		/* read-write */
	__le32 queue_desc_hi;		/* read-write */
	__le32 queue_avail_lo;		/* read-write */
	__le32 queue_avail_hi;		/* read-write */
	__le32 queue_used_lo;		/* read-write */
	__le32 queue_used_hi;		/* read-write */
};

/*
 * Warning: do not use sizeof on this: use offsetofend for
 * specific fields you need.
 */
struct virtio_pci_modern_common_cfg {
	struct virtio_pci_common_cfg cfg;

	__le16 queue_notify_data;	/* read-write */
	__le16 queue_reset;		/* read-write */

	__le16 admin_queue_index;	/* read-only */
	__le16 admin_queue_num;		/* read-only */
};

/* Fields in VIRTIO_PCI_CAP_PCI_CFG: */
struct virtio_pci_cfg_cap {
	struct virtio_pci_cap cap;
	__u8 pci_cfg_data[4]; /* Data for BAR access. */
};

/* Macro versions of offsets for the Old Timers! */
#define VIRTIO_PCI_CAP_VNDR		0
#define VIRTIO_PCI_CAP_NEXT		1
#define VIRTIO_PCI_CAP_LEN		2
#define VIRTIO_PCI_CAP_CFG_TYPE		3
#define VIRTIO_PCI_CAP_BAR		4
#define VIRTIO_PCI_CAP_OFFSET		8
#define VIRTIO_PCI_CAP_LENGTH		12

#define VIRTIO_PCI_NOTIFY_CAP_MULT	16

#define VIRTIO_PCI_COMMON_DFSELECT	0
#define VIRTIO_PCI_COMMON_DF		4
#define VIRTIO_PCI_COMMON_GFSELECT	8
#define VIRTIO_PCI_COMMON_GF		12
#define VIRTIO_PCI_COMMON_MSIX		16
#define VIRTIO_PCI_COMMON_NUMQ		18
#define VIRTIO_PCI_COMMON_STATUS	20
#define VIRTIO_PCI_COMMON_CFGGENERATION	21
#define VIRTIO_PCI_COMMON_Q_SELECT	22
#define VIRTIO_PCI_COMMON_Q_SIZE	24
#define VIRTIO_PCI_COMMON_Q_MSIX	26
#define VIRTIO_PCI_COMMON_Q_ENABLE	28
#define VIRTIO_PCI_COMMON_Q_NOFF	30
#define VIRTIO_PCI_COMMON_Q_DESCLO	32
#define VIRTIO_PCI_COMMON_Q_DESCHI	36
#define VIRTIO_PCI_COMMON_Q_AVAILLO	40
#define VIRTIO_PCI_COMMON_Q_AVAILHI	44
#define VIRTIO_PCI_COMMON_Q_USEDLO	48
#define VIRTIO_PCI_COMMON_Q_USEDHI	52
#define VIRTIO_PCI_COMMON_Q_NDATA	56
#define VIRTIO_PCI_COMMON_Q_RESET	58
#define VIRTIO_PCI_COMMON_ADM_Q_IDX	60
#define VIRTIO_PCI_COMMON_ADM_Q_NUM	62

#endif /* VIRTIO_PCI_NO_MODERN */

/* Admin command status. */
#define VIRTIO_ADMIN_STATUS_OK		0

/* Admin command opcode. */
#define VIRTIO_ADMIN_CMD_LIST_QUERY	0x0
#define VIRTIO_ADMIN_CMD_LIST_USE	0x1

/* Admin command group type. */
#define VIRTIO_ADMIN_GROUP_TYPE_SELF	0x0
#define VIRTIO_ADMIN_GROUP_TYPE_SRIOV	0x1

/* Transitional device admin command. */
#define VIRTIO_ADMIN_CMD_LEGACY_COMMON_CFG_WRITE	0x2
#define VIRTIO_ADMIN_CMD_LEGACY_COMMON_CFG_READ		0x3
#define VIRTIO_ADMIN_CMD_LEGACY_DEV_CFG_WRITE		0x4
#define VIRTIO_ADMIN_CMD_LEGACY_DEV_CFG_READ		0x5
#define VIRTIO_ADMIN_CMD_LEGACY_NOTIFY_INFO		0x6

/* Device parts access commands. */
#define VIRTIO_ADMIN_CMD_CAP_ID_LIST_QUERY		0x7
#define VIRTIO_ADMIN_CMD_DEVICE_CAP_GET			0x8
#define VIRTIO_ADMIN_CMD_DRIVER_CAP_SET			0x9
#define VIRTIO_ADMIN_CMD_RESOURCE_OBJ_CREATE		0xa
#define VIRTIO_ADMIN_CMD_RESOURCE_OBJ_DESTROY		0xd
#define VIRTIO_ADMIN_CMD_DEV_PARTS_METADATA_GET		0xe
#define VIRTIO_ADMIN_CMD_DEV_PARTS_GET			0xf
#define VIRTIO_ADMIN_CMD_DEV_PARTS_SET			0x10
#define VIRTIO_ADMIN_CMD_DEV_MODE_SET			0x11

struct virtio_admin_cmd_hdr {
	__le16 opcode;
	/*
	 * 1 - SR-IOV
	 * 2-65535 - reserved
	 */
	__le16 group_type;
	/* Unused, reserved for future extensions. */
	__u8 reserved1[12];
	__le64 group_member_id;
};

struct virtio_admin_cmd_status {
	__le16 status;
	__le16 status_qualifier;
	/* Unused, reserved for future extensions. */
	__u8 reserved2[4];
};

struct virtio_admin_cmd_legacy_wr_data {
	__u8 offset; /* Starting offset of the register(s) to write. */
	__u8 reserved[7];
	__u8 registers[];
};

struct virtio_admin_cmd_legacy_rd_data {
	__u8 offset; /* Starting offset of the register(s) to read. */
};

#define VIRTIO_ADMIN_CMD_NOTIFY_INFO_FLAGS_END 0
#define VIRTIO_ADMIN_CMD_NOTIFY_INFO_FLAGS_OWNER_DEV 0x1
#define VIRTIO_ADMIN_CMD_NOTIFY_INFO_FLAGS_OWNER_MEM 0x2

#define VIRTIO_ADMIN_CMD_MAX_NOTIFY_INFO 4

struct virtio_admin_cmd_notify_info_data {
	__u8 flags; /* 0 = end of list, 1 = owner device, 2 = member device */
	__u8 bar; /* BAR of the member or the owner device */
	__u8 padding[6];
	__le64 offset; /* Offset within bar. */
};

struct virtio_admin_cmd_notify_info_result {
	struct virtio_admin_cmd_notify_info_data entries[VIRTIO_ADMIN_CMD_MAX_NOTIFY_INFO];
};

#define VIRTIO_DEV_PARTS_CAP 0x0000

struct virtio_dev_parts_cap {
	__u8 get_parts_resource_objects_limit;
	__u8 set_parts_resource_objects_limit;
};

#define MAX_CAP_ID __KERNEL_DIV_ROUND_UP(VIRTIO_DEV_PARTS_CAP + 1, 64)

struct virtio_admin_cmd_query_cap_id_result {
	__le64 supported_caps[MAX_CAP_ID];
};

struct virtio_admin_cmd_cap_get_data {
	__le16 id;
	__u8 reserved[6];
};

struct virtio_admin_cmd_cap_set_data {
	__le16 id;
	__u8 reserved[6];
	__u8 cap_specific_data[];
};

struct virtio_admin_cmd_resource_obj_cmd_hdr {
	__le16 type;
	__u8 reserved[2];
	__le32 id; /* Indicates unique resource object id per resource object type */
};

struct virtio_admin_cmd_resource_obj_create_data {
	struct virtio_admin_cmd_resource_obj_cmd_hdr hdr;
	__le64 flags;
	__u8 resource_obj_specific_data[];
};

#define VIRTIO_RESOURCE_OBJ_DEV_PARTS 0

#define VIRTIO_RESOURCE_OBJ_DEV_PARTS_TYPE_GET 0
#define VIRTIO_RESOURCE_OBJ_DEV_PARTS_TYPE_SET 1

struct virtio_resource_obj_dev_parts {
	__u8 type;
	__u8 reserved[7];
};

#define VIRTIO_ADMIN_CMD_DEV_PARTS_METADATA_TYPE_SIZE 0
#define VIRTIO_ADMIN_CMD_DEV_PARTS_METADATA_TYPE_COUNT 1
#define VIRTIO_ADMIN_CMD_DEV_PARTS_METADATA_TYPE_LIST 2

struct virtio_admin_cmd_dev_parts_metadata_data {
	struct virtio_admin_cmd_resource_obj_cmd_hdr hdr;
	__u8 type;
	__u8 reserved[7];
};

#define VIRTIO_DEV_PART_F_OPTIONAL 0

struct virtio_dev_part_hdr {
	__le16 part_type;
	__u8 flags;
	__u8 reserved;
	union {
		struct {
			__le32 offset;
			__le32 reserved;
		} pci_common_cfg;
		struct {
			__le16 index;
			__u8 reserved[6];
		} vq_index;
	} selector;
	__le32 length;
};

struct virtio_dev_part {
	struct virtio_dev_part_hdr hdr;
	__u8 value[];
};

struct virtio_admin_cmd_dev_parts_metadata_result {
	union {
		struct {
			__le32 size;
			__le32 reserved;
		} parts_size;
		struct {
			__le32 count;
			__le32 reserved;
		} hdr_list_count;
		struct {
			__le32 count;
			__le32 reserved;
			struct virtio_dev_part_hdr hdrs[];
		} hdr_list;
	};
};

#define VIRTIO_ADMIN_CMD_DEV_PARTS_GET_TYPE_SELECTED 0
#define VIRTIO_ADMIN_CMD_DEV_PARTS_GET_TYPE_ALL 1

struct virtio_admin_cmd_dev_parts_get_data {
	struct virtio_admin_cmd_resource_obj_cmd_hdr hdr;
	__u8 type;
	__u8 reserved[7];
	struct virtio_dev_part_hdr hdr_list[];
};

struct virtio_admin_cmd_dev_parts_set_data {
	struct virtio_admin_cmd_resource_obj_cmd_hdr hdr;
	struct virtio_dev_part parts[];
};

#define VIRTIO_ADMIN_CMD_DEV_MODE_F_STOPPED 0

struct virtio_admin_cmd_dev_mode_set_data {
	__u8 flags;
};

#endif
