/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _LINUX_VHOST_TYPES_H
#define _LINUX_VHOST_TYPES_H
/* Userspace interface for in-kernel virtio accelerators. */

/* vhost is used to reduce the number of system calls involved in virtio.
 *
 * Existing virtio net code is used in the guest without modification.
 *
 * This header includes interface used by userspace hypervisor for
 * device configuration.
 */

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ring.h>

struct vhost_vring_state {
	unsigned int index;
	unsigned int num;
};

struct vhost_vring_file {
	unsigned int index;
	int fd; /* Pass -1 to unbind from file. */

};

struct vhost_vring_addr {
	unsigned int index;
	/* Option flags. */
	unsigned int flags;
	/* Flag values: */
	/* Whether log address is valid. If set enables logging. */
#define VHOST_VRING_F_LOG 0

	/* Start of array of descriptors (virtually contiguous) */
	__u64 desc_user_addr;
	/* Used structure address. Must be 32 bit aligned */
	__u64 used_user_addr;
	/* Available structure address. Must be 16 bit aligned */
	__u64 avail_user_addr;
	/* Logging support. */
	/* Log writes to used structure, at offset calculated from specified
	 * address. Address must be 32 bit aligned. */
	__u64 log_guest_addr;
};

/* no alignment requirement */
struct vhost_iotlb_msg {
	__u64 iova;
	__u64 size;
	__u64 uaddr;
#define VHOST_ACCESS_RO      0x1
#define VHOST_ACCESS_WO      0x2
#define VHOST_ACCESS_RW      0x3
	__u8 perm;
#define VHOST_IOTLB_MISS           1
#define VHOST_IOTLB_UPDATE         2
#define VHOST_IOTLB_INVALIDATE     3
#define VHOST_IOTLB_ACCESS_FAIL    4
/*
 * VHOST_IOTLB_BATCH_BEGIN and VHOST_IOTLB_BATCH_END allow modifying
 * multiple mappings in one go: beginning with
 * VHOST_IOTLB_BATCH_BEGIN, followed by any number of
 * VHOST_IOTLB_UPDATE messages, and ending with VHOST_IOTLB_BATCH_END.
 * When one of these two values is used as the message type, the rest
 * of the fields in the message are ignored. There's no guarantee that
 * these changes take place automatically in the device.
 */
#define VHOST_IOTLB_BATCH_BEGIN    5
#define VHOST_IOTLB_BATCH_END      6
	__u8 type;
};

#define VHOST_IOTLB_MSG 0x1
#define VHOST_IOTLB_MSG_V2 0x2

struct vhost_msg {
	int type;
	union {
		struct vhost_iotlb_msg iotlb;
		__u8 padding[64];
	};
};

struct vhost_msg_v2 {
	__u32 type;
	__u32 asid;
	union {
		struct vhost_iotlb_msg iotlb;
		__u8 padding[64];
	};
};

struct vhost_memory_region {
	__u64 guest_phys_addr;
	__u64 memory_size; /* bytes */
	__u64 userspace_addr;
	__u64 flags_padding; /* No flags are currently specified. */
};

/* All region addresses and sizes must be 4K aligned. */
#define VHOST_PAGE_SIZE 0x1000

struct vhost_memory {
	__u32 nregions;
	__u32 padding;
	struct vhost_memory_region regions[];
};

/* VHOST_SCSI specific definitions */

/*
 * Used by QEMU userspace to ensure a consistent vhost-scsi ABI.
 *
 * ABI Rev 0: July 2012 version starting point for v3.6-rc merge candidate +
 *            RFC-v2 vhost-scsi userspace.  Add GET_ABI_VERSION ioctl usage
 * ABI Rev 1: January 2013. Ignore vhost_tpgt field in struct vhost_scsi_target.
 *            All the targets under vhost_wwpn can be seen and used by guset.
 */

#define VHOST_SCSI_ABI_VERSION	1

struct vhost_scsi_target {
	int abi_version;
	char vhost_wwpn[224]; /* TRANSPORT_IQN_LEN */
	unsigned short vhost_tpgt;
	unsigned short reserved;
};

/* VHOST_VDPA specific definitions */

struct vhost_vdpa_config {
	__u32 off;
	__u32 len;
	__u8 buf[];
};

/* vhost vdpa IOVA range
 * @first: First address that can be mapped by vhost-vDPA
 * @last: Last address that can be mapped by vhost-vDPA
 */
struct vhost_vdpa_iova_range {
	__u64 first;
	__u64 last;
};

/* Feature bits */
/* Log all write descriptors. Can be changed while device is active. */
#define VHOST_F_LOG_ALL 26
/* vhost-net should add virtio_net_hdr for RX, and strip for TX packets. */
#define VHOST_NET_F_VIRTIO_NET_HDR 27

/* Use message type V2 */
#define VHOST_BACKEND_F_IOTLB_MSG_V2 0x1
/* IOTLB can accept batching hints */
#define VHOST_BACKEND_F_IOTLB_BATCH  0x2
/* IOTLB can accept address space identifier through V2 type of IOTLB
 * message
 */
#define VHOST_BACKEND_F_IOTLB_ASID  0x3
/* Device can be suspended */
#define VHOST_BACKEND_F_SUSPEND  0x4

#endif
