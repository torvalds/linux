/*
 *
 * Copyright (c) 2011, Microsoft Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Authors:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 *   K. Y. Srinivasan <kys@microsoft.com>
 *
 */

#ifndef _HYPERV_H
#define _HYPERV_H

#include <linux/types.h>


/*
 * Implementation of host controlled snapshot of the guest.
 */

#define VSS_OP_REGISTER 128

enum hv_vss_op {
	VSS_OP_CREATE = 0,
	VSS_OP_DELETE,
	VSS_OP_HOT_BACKUP,
	VSS_OP_GET_DM_INFO,
	VSS_OP_BU_COMPLETE,
	/*
	 * Following operations are only supported with IC version >= 5.0
	 */
	VSS_OP_FREEZE, /* Freeze the file systems in the VM */
	VSS_OP_THAW, /* Unfreeze the file systems */
	VSS_OP_AUTO_RECOVER,
	VSS_OP_COUNT /* Number of operations, must be last */
};


/*
 * Header for all VSS messages.
 */
struct hv_vss_hdr {
	__u8 operation;
	__u8 reserved[7];
} __attribute__((packed));


/*
 * Flag values for the hv_vss_check_feature. Linux supports only
 * one value.
 */
#define VSS_HBU_NO_AUTO_RECOVERY	0x00000005

struct hv_vss_check_feature {
	__u32 flags;
} __attribute__((packed));

struct hv_vss_check_dm_info {
	__u32 flags;
} __attribute__((packed));

struct hv_vss_msg {
	union {
		struct hv_vss_hdr vss_hdr;
		int error;
	};
	union {
		struct hv_vss_check_feature vss_cf;
		struct hv_vss_check_dm_info dm_info;
	};
} __attribute__((packed));

/*
 * An implementation of HyperV key value pair (KVP) functionality for Linux.
 *
 *
 * Copyright (C) 2010, Novell, Inc.
 * Author : K. Y. Srinivasan <ksrinivasan@novell.com>
 *
 */

/*
 * Maximum value size - used for both key names and value data, and includes
 * any applicable NULL terminators.
 *
 * Note:  This limit is somewhat arbitrary, but falls easily within what is
 * supported for all native guests (back to Win 2000) and what is reasonable
 * for the IC KVP exchange functionality.  Note that Windows Me/98/95 are
 * limited to 255 character key names.
 *
 * MSDN recommends not storing data values larger than 2048 bytes in the
 * registry.
 *
 * Note:  This value is used in defining the KVP exchange message - this value
 * cannot be modified without affecting the message size and compatibility.
 */

/*
 * bytes, including any null terminators
 */
#define HV_KVP_EXCHANGE_MAX_VALUE_SIZE          (2048)


/*
 * Maximum key size - the registry limit for the length of an entry name
 * is 256 characters, including the null terminator
 */

#define HV_KVP_EXCHANGE_MAX_KEY_SIZE            (512)

/*
 * In Linux, we implement the KVP functionality in two components:
 * 1) The kernel component which is packaged as part of the hv_utils driver
 * is responsible for communicating with the host and responsible for
 * implementing the host/guest protocol. 2) A user level daemon that is
 * responsible for data gathering.
 *
 * Host/Guest Protocol: The host iterates over an index and expects the guest
 * to assign a key name to the index and also return the value corresponding to
 * the key. The host will have atmost one KVP transaction outstanding at any
 * given point in time. The host side iteration stops when the guest returns
 * an error. Microsoft has specified the following mapping of key names to
 * host specified index:
 *
 *	Index		Key Name
 *	0		FullyQualifiedDomainName
 *	1		IntegrationServicesVersion
 *	2		NetworkAddressIPv4
 *	3		NetworkAddressIPv6
 *	4		OSBuildNumber
 *	5		OSName
 *	6		OSMajorVersion
 *	7		OSMinorVersion
 *	8		OSVersion
 *	9		ProcessorArchitecture
 *
 * The Windows host expects the Key Name and Key Value to be encoded in utf16.
 *
 * Guest Kernel/KVP Daemon Protocol: As noted earlier, we implement all of the
 * data gathering functionality in a user mode daemon. The user level daemon
 * is also responsible for binding the key name to the index as well. The
 * kernel and user-level daemon communicate using a connector channel.
 *
 * The user mode component first registers with the
 * the kernel component. Subsequently, the kernel component requests, data
 * for the specified keys. In response to this message the user mode component
 * fills in the value corresponding to the specified key. We overload the
 * sequence field in the cn_msg header to define our KVP message types.
 *
 *
 * The kernel component simply acts as a conduit for communication between the
 * Windows host and the user-level daemon. The kernel component passes up the
 * index received from the Host to the user-level daemon. If the index is
 * valid (supported), the corresponding key as well as its
 * value (both are strings) is returned. If the index is invalid
 * (not supported), a NULL key string is returned.
 */


/*
 * Registry value types.
 */

#define REG_SZ 1
#define REG_U32 4
#define REG_U64 8

/*
 * As we look at expanding the KVP functionality to include
 * IP injection functionality, we need to maintain binary
 * compatibility with older daemons.
 *
 * The KVP opcodes are defined by the host and it was unfortunate
 * that I chose to treat the registration operation as part of the
 * KVP operations defined by the host.
 * Here is the level of compatibility
 * (between the user level daemon and the kernel KVP driver) that we
 * will implement:
 *
 * An older daemon will always be supported on a newer driver.
 * A given user level daemon will require a minimal version of the
 * kernel driver.
 * If we cannot handle the version differences, we will fail gracefully
 * (this can happen when we have a user level daemon that is more
 * advanced than the KVP driver.
 *
 * We will use values used in this handshake for determining if we have
 * workable user level daemon and the kernel driver. We begin by taking the
 * registration opcode out of the KVP opcode namespace. We will however,
 * maintain compatibility with the existing user-level daemon code.
 */

/*
 * Daemon code not supporting IP injection (legacy daemon).
 */

#define KVP_OP_REGISTER	4

/*
 * Daemon code supporting IP injection.
 * The KVP opcode field is used to communicate the
 * registration information; so define a namespace that
 * will be distinct from the host defined KVP opcode.
 */

#define KVP_OP_REGISTER1 100

enum hv_kvp_exchg_op {
	KVP_OP_GET = 0,
	KVP_OP_SET,
	KVP_OP_DELETE,
	KVP_OP_ENUMERATE,
	KVP_OP_GET_IP_INFO,
	KVP_OP_SET_IP_INFO,
	KVP_OP_COUNT /* Number of operations, must be last. */
};

enum hv_kvp_exchg_pool {
	KVP_POOL_EXTERNAL = 0,
	KVP_POOL_GUEST,
	KVP_POOL_AUTO,
	KVP_POOL_AUTO_EXTERNAL,
	KVP_POOL_AUTO_INTERNAL,
	KVP_POOL_COUNT /* Number of pools, must be last. */
};

/*
 * Some Hyper-V status codes.
 */

#define HV_S_OK				0x00000000
#define HV_E_FAIL			0x80004005
#define HV_S_CONT			0x80070103
#define HV_ERROR_NOT_SUPPORTED		0x80070032
#define HV_ERROR_MACHINE_LOCKED		0x800704F7
#define HV_ERROR_DEVICE_NOT_CONNECTED	0x8007048F
#define HV_INVALIDARG			0x80070057
#define HV_GUID_NOTFOUND		0x80041002

#define ADDR_FAMILY_NONE	0x00
#define ADDR_FAMILY_IPV4	0x01
#define ADDR_FAMILY_IPV6	0x02

#define MAX_ADAPTER_ID_SIZE	128
#define MAX_IP_ADDR_SIZE	1024
#define MAX_GATEWAY_SIZE	512


struct hv_kvp_ipaddr_value {
	__u16	adapter_id[MAX_ADAPTER_ID_SIZE];
	__u8	addr_family;
	__u8	dhcp_enabled;
	__u16	ip_addr[MAX_IP_ADDR_SIZE];
	__u16	sub_net[MAX_IP_ADDR_SIZE];
	__u16	gate_way[MAX_GATEWAY_SIZE];
	__u16	dns_addr[MAX_IP_ADDR_SIZE];
} __attribute__((packed));


struct hv_kvp_hdr {
	__u8 operation;
	__u8 pool;
	__u16 pad;
} __attribute__((packed));

struct hv_kvp_exchg_msg_value {
	__u32 value_type;
	__u32 key_size;
	__u32 value_size;
	__u8 key[HV_KVP_EXCHANGE_MAX_KEY_SIZE];
	union {
		__u8 value[HV_KVP_EXCHANGE_MAX_VALUE_SIZE];
		__u32 value_u32;
		__u64 value_u64;
	};
} __attribute__((packed));

struct hv_kvp_msg_enumerate {
	__u32 index;
	struct hv_kvp_exchg_msg_value data;
} __attribute__((packed));

struct hv_kvp_msg_get {
	struct hv_kvp_exchg_msg_value data;
};

struct hv_kvp_msg_set {
	struct hv_kvp_exchg_msg_value data;
};

struct hv_kvp_msg_delete {
	__u32 key_size;
	__u8 key[HV_KVP_EXCHANGE_MAX_KEY_SIZE];
};

struct hv_kvp_register {
	__u8 version[HV_KVP_EXCHANGE_MAX_KEY_SIZE];
};

struct hv_kvp_msg {
	union {
		struct hv_kvp_hdr	kvp_hdr;
		int error;
	};
	union {
		struct hv_kvp_msg_get		kvp_get;
		struct hv_kvp_msg_set		kvp_set;
		struct hv_kvp_msg_delete	kvp_delete;
		struct hv_kvp_msg_enumerate	kvp_enum_data;
		struct hv_kvp_ipaddr_value      kvp_ip_val;
		struct hv_kvp_register		kvp_register;
	} body;
} __attribute__((packed));

struct hv_kvp_ip_msg {
	__u8 operation;
	__u8 pool;
	struct hv_kvp_ipaddr_value      kvp_ip_val;
} __attribute__((packed));

#ifdef __KERNEL__
#include <linux/scatterlist.h>
#include <linux/list.h>
#include <linux/uuid.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>


#define MAX_PAGE_BUFFER_COUNT				19
#define MAX_MULTIPAGE_BUFFER_COUNT			32 /* 128K */

#pragma pack(push, 1)

/* Single-page buffer */
struct hv_page_buffer {
	u32 len;
	u32 offset;
	u64 pfn;
};

/* Multiple-page buffer */
struct hv_multipage_buffer {
	/* Length and Offset determines the # of pfns in the array */
	u32 len;
	u32 offset;
	u64 pfn_array[MAX_MULTIPAGE_BUFFER_COUNT];
};

/* 0x18 includes the proprietary packet header */
#define MAX_PAGE_BUFFER_PACKET		(0x18 +			\
					(sizeof(struct hv_page_buffer) * \
					 MAX_PAGE_BUFFER_COUNT))
#define MAX_MULTIPAGE_BUFFER_PACKET	(0x18 +			\
					 sizeof(struct hv_multipage_buffer))


#pragma pack(pop)

struct hv_ring_buffer {
	/* Offset in bytes from the start of ring data below */
	u32 write_index;

	/* Offset in bytes from the start of ring data below */
	u32 read_index;

	u32 interrupt_mask;

	/*
	 * Win8 uses some of the reserved bits to implement
	 * interrupt driven flow management. On the send side
	 * we can request that the receiver interrupt the sender
	 * when the ring transitions from being full to being able
	 * to handle a message of size "pending_send_sz".
	 *
	 * Add necessary state for this enhancement.
	 */
	u32 pending_send_sz;

	u32 reserved1[12];

	union {
		struct {
			u32 feat_pending_send_sz:1;
		};
		u32 value;
	} feature_bits;

	/* Pad it to PAGE_SIZE so that data starts on page boundary */
	u8	reserved2[4028];

	/*
	 * Ring data starts here + RingDataStartOffset
	 * !!! DO NOT place any fields below this !!!
	 */
	u8 buffer[0];
} __packed;

struct hv_ring_buffer_info {
	struct hv_ring_buffer *ring_buffer;
	u32 ring_size;			/* Include the shared header */
	spinlock_t ring_lock;

	u32 ring_datasize;		/* < ring_size */
	u32 ring_data_startoffset;
};

struct hv_ring_buffer_debug_info {
	u32 current_interrupt_mask;
	u32 current_read_index;
	u32 current_write_index;
	u32 bytes_avail_toread;
	u32 bytes_avail_towrite;
};


/*
 *
 * hv_get_ringbuffer_availbytes()
 *
 * Get number of bytes available to read and to write to
 * for the specified ring buffer
 */
static inline void
hv_get_ringbuffer_availbytes(struct hv_ring_buffer_info *rbi,
			  u32 *read, u32 *write)
{
	u32 read_loc, write_loc, dsize;

	smp_read_barrier_depends();

	/* Capture the read/write indices before they changed */
	read_loc = rbi->ring_buffer->read_index;
	write_loc = rbi->ring_buffer->write_index;
	dsize = rbi->ring_datasize;

	*write = write_loc >= read_loc ? dsize - (write_loc - read_loc) :
		read_loc - write_loc;
	*read = dsize - *write;
}


/*
 * We use the same version numbering for all Hyper-V modules.
 *
 * Definition of versioning is as follows;
 *
 *	Major Number	Changes for these scenarios;
 *			1.	When a new version of Windows Hyper-V
 *				is released.
 *			2.	A Major change has occurred in the
 *				Linux IC's.
 *			(For example the merge for the first time
 *			into the kernel) Every time the Major Number
 *			changes, the Revision number is reset to 0.
 *	Minor Number	Changes when new functionality is added
 *			to the Linux IC's that is not a bug fix.
 *
 * 3.1 - Added completed hv_utils driver. Shutdown/Heartbeat/Timesync
 */
#define HV_DRV_VERSION           "3.1"

/*
 * VMBUS version is 32 bit entity broken up into
 * two 16 bit quantities: major_number. minor_number.
 *
 * 0 . 13 (Windows Server 2008)
 * 1 . 1  (Windows 7)
 * 2 . 4  (Windows 8)
 */

#define VERSION_WS2008  ((0 << 16) | (13))
#define VERSION_WIN7    ((1 << 16) | (1))
#define VERSION_WIN8    ((2 << 16) | (4))

#define VERSION_INVAL -1

#define VERSION_CURRENT VERSION_WIN8

/* Make maximum size of pipe payload of 16K */
#define MAX_PIPE_DATA_PAYLOAD		(sizeof(u8) * 16384)

/* Define PipeMode values. */
#define VMBUS_PIPE_TYPE_BYTE		0x00000000
#define VMBUS_PIPE_TYPE_MESSAGE		0x00000004

/* The size of the user defined data buffer for non-pipe offers. */
#define MAX_USER_DEFINED_BYTES		120

/* The size of the user defined data buffer for pipe offers. */
#define MAX_PIPE_USER_DEFINED_BYTES	116

/*
 * At the center of the Channel Management library is the Channel Offer. This
 * struct contains the fundamental information about an offer.
 */
struct vmbus_channel_offer {
	uuid_le if_type;
	uuid_le if_instance;

	/*
	 * These two fields are not currently used.
	 */
	u64 reserved1;
	u64 reserved2;

	u16 chn_flags;
	u16 mmio_megabytes;		/* in bytes * 1024 * 1024 */

	union {
		/* Non-pipes: The user has MAX_USER_DEFINED_BYTES bytes. */
		struct {
			unsigned char user_def[MAX_USER_DEFINED_BYTES];
		} std;

		/*
		 * Pipes:
		 * The following sructure is an integrated pipe protocol, which
		 * is implemented on top of standard user-defined data. Pipe
		 * clients have MAX_PIPE_USER_DEFINED_BYTES left for their own
		 * use.
		 */
		struct {
			u32  pipe_mode;
			unsigned char user_def[MAX_PIPE_USER_DEFINED_BYTES];
		} pipe;
	} u;
	/*
	 * The sub_channel_index is defined in win8.
	 */
	u16 sub_channel_index;
	u16 reserved3;
} __packed;

/* Server Flags */
#define VMBUS_CHANNEL_ENUMERATE_DEVICE_INTERFACE	1
#define VMBUS_CHANNEL_SERVER_SUPPORTS_TRANSFER_PAGES	2
#define VMBUS_CHANNEL_SERVER_SUPPORTS_GPADLS		4
#define VMBUS_CHANNEL_NAMED_PIPE_MODE			0x10
#define VMBUS_CHANNEL_LOOPBACK_OFFER			0x100
#define VMBUS_CHANNEL_PARENT_OFFER			0x200
#define VMBUS_CHANNEL_REQUEST_MONITORED_NOTIFICATION	0x400

struct vmpacket_descriptor {
	u16 type;
	u16 offset8;
	u16 len8;
	u16 flags;
	u64 trans_id;
} __packed;

struct vmpacket_header {
	u32 prev_pkt_start_offset;
	struct vmpacket_descriptor descriptor;
} __packed;

struct vmtransfer_page_range {
	u32 byte_count;
	u32 byte_offset;
} __packed;

struct vmtransfer_page_packet_header {
	struct vmpacket_descriptor d;
	u16 xfer_pageset_id;
	u8  sender_owns_set;
	u8 reserved;
	u32 range_cnt;
	struct vmtransfer_page_range ranges[1];
} __packed;

struct vmgpadl_packet_header {
	struct vmpacket_descriptor d;
	u32 gpadl;
	u32 reserved;
} __packed;

struct vmadd_remove_transfer_page_set {
	struct vmpacket_descriptor d;
	u32 gpadl;
	u16 xfer_pageset_id;
	u16 reserved;
} __packed;

/*
 * This structure defines a range in guest physical space that can be made to
 * look virtually contiguous.
 */
struct gpa_range {
	u32 byte_count;
	u32 byte_offset;
	u64 pfn_array[0];
};

/*
 * This is the format for an Establish Gpadl packet, which contains a handle by
 * which this GPADL will be known and a set of GPA ranges associated with it.
 * This can be converted to a MDL by the guest OS.  If there are multiple GPA
 * ranges, then the resulting MDL will be "chained," representing multiple VA
 * ranges.
 */
struct vmestablish_gpadl {
	struct vmpacket_descriptor d;
	u32 gpadl;
	u32 range_cnt;
	struct gpa_range range[1];
} __packed;

/*
 * This is the format for a Teardown Gpadl packet, which indicates that the
 * GPADL handle in the Establish Gpadl packet will never be referenced again.
 */
struct vmteardown_gpadl {
	struct vmpacket_descriptor d;
	u32 gpadl;
	u32 reserved;	/* for alignment to a 8-byte boundary */
} __packed;

/*
 * This is the format for a GPA-Direct packet, which contains a set of GPA
 * ranges, in addition to commands and/or data.
 */
struct vmdata_gpa_direct {
	struct vmpacket_descriptor d;
	u32 reserved;
	u32 range_cnt;
	struct gpa_range range[1];
} __packed;

/* This is the format for a Additional Data Packet. */
struct vmadditional_data {
	struct vmpacket_descriptor d;
	u64 total_bytes;
	u32 offset;
	u32 byte_cnt;
	unsigned char data[1];
} __packed;

union vmpacket_largest_possible_header {
	struct vmpacket_descriptor simple_hdr;
	struct vmtransfer_page_packet_header xfer_page_hdr;
	struct vmgpadl_packet_header gpadl_hdr;
	struct vmadd_remove_transfer_page_set add_rm_xfer_page_hdr;
	struct vmestablish_gpadl establish_gpadl_hdr;
	struct vmteardown_gpadl teardown_gpadl_hdr;
	struct vmdata_gpa_direct data_gpa_direct_hdr;
};

#define VMPACKET_DATA_START_ADDRESS(__packet)	\
	(void *)(((unsigned char *)__packet) +	\
	 ((struct vmpacket_descriptor)__packet)->offset8 * 8)

#define VMPACKET_DATA_LENGTH(__packet)		\
	((((struct vmpacket_descriptor)__packet)->len8 -	\
	  ((struct vmpacket_descriptor)__packet)->offset8) * 8)

#define VMPACKET_TRANSFER_MODE(__packet)	\
	(((struct IMPACT)__packet)->type)

enum vmbus_packet_type {
	VM_PKT_INVALID				= 0x0,
	VM_PKT_SYNCH				= 0x1,
	VM_PKT_ADD_XFER_PAGESET			= 0x2,
	VM_PKT_RM_XFER_PAGESET			= 0x3,
	VM_PKT_ESTABLISH_GPADL			= 0x4,
	VM_PKT_TEARDOWN_GPADL			= 0x5,
	VM_PKT_DATA_INBAND			= 0x6,
	VM_PKT_DATA_USING_XFER_PAGES		= 0x7,
	VM_PKT_DATA_USING_GPADL			= 0x8,
	VM_PKT_DATA_USING_GPA_DIRECT		= 0x9,
	VM_PKT_CANCEL_REQUEST			= 0xa,
	VM_PKT_COMP				= 0xb,
	VM_PKT_DATA_USING_ADDITIONAL_PKT	= 0xc,
	VM_PKT_ADDITIONAL_DATA			= 0xd
};

#define VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED	1


/* Version 1 messages */
enum vmbus_channel_message_type {
	CHANNELMSG_INVALID			=  0,
	CHANNELMSG_OFFERCHANNEL		=  1,
	CHANNELMSG_RESCIND_CHANNELOFFER	=  2,
	CHANNELMSG_REQUESTOFFERS		=  3,
	CHANNELMSG_ALLOFFERS_DELIVERED	=  4,
	CHANNELMSG_OPENCHANNEL		=  5,
	CHANNELMSG_OPENCHANNEL_RESULT		=  6,
	CHANNELMSG_CLOSECHANNEL		=  7,
	CHANNELMSG_GPADL_HEADER		=  8,
	CHANNELMSG_GPADL_BODY			=  9,
	CHANNELMSG_GPADL_CREATED		= 10,
	CHANNELMSG_GPADL_TEARDOWN		= 11,
	CHANNELMSG_GPADL_TORNDOWN		= 12,
	CHANNELMSG_RELID_RELEASED		= 13,
	CHANNELMSG_INITIATE_CONTACT		= 14,
	CHANNELMSG_VERSION_RESPONSE		= 15,
	CHANNELMSG_UNLOAD			= 16,
#ifdef VMBUS_FEATURE_PARENT_OR_PEER_MEMORY_MAPPED_INTO_A_CHILD
	CHANNELMSG_VIEWRANGE_ADD		= 17,
	CHANNELMSG_VIEWRANGE_REMOVE		= 18,
#endif
	CHANNELMSG_COUNT
};

struct vmbus_channel_message_header {
	enum vmbus_channel_message_type msgtype;
	u32 padding;
} __packed;

/* Query VMBus Version parameters */
struct vmbus_channel_query_vmbus_version {
	struct vmbus_channel_message_header header;
	u32 version;
} __packed;

/* VMBus Version Supported parameters */
struct vmbus_channel_version_supported {
	struct vmbus_channel_message_header header;
	u8 version_supported;
} __packed;

/* Offer Channel parameters */
struct vmbus_channel_offer_channel {
	struct vmbus_channel_message_header header;
	struct vmbus_channel_offer offer;
	u32 child_relid;
	u8 monitorid;
	/*
	 * win7 and beyond splits this field into a bit field.
	 */
	u8 monitor_allocated:1;
	u8 reserved:7;
	/*
	 * These are new fields added in win7 and later.
	 * Do not access these fields without checking the
	 * negotiated protocol.
	 *
	 * If "is_dedicated_interrupt" is set, we must not set the
	 * associated bit in the channel bitmap while sending the
	 * interrupt to the host.
	 *
	 * connection_id is to be used in signaling the host.
	 */
	u16 is_dedicated_interrupt:1;
	u16 reserved1:15;
	u32 connection_id;
} __packed;

/* Rescind Offer parameters */
struct vmbus_channel_rescind_offer {
	struct vmbus_channel_message_header header;
	u32 child_relid;
} __packed;

/*
 * Request Offer -- no parameters, SynIC message contains the partition ID
 * Set Snoop -- no parameters, SynIC message contains the partition ID
 * Clear Snoop -- no parameters, SynIC message contains the partition ID
 * All Offers Delivered -- no parameters, SynIC message contains the partition
 *		           ID
 * Flush Client -- no parameters, SynIC message contains the partition ID
 */

/* Open Channel parameters */
struct vmbus_channel_open_channel {
	struct vmbus_channel_message_header header;

	/* Identifies the specific VMBus channel that is being opened. */
	u32 child_relid;

	/* ID making a particular open request at a channel offer unique. */
	u32 openid;

	/* GPADL for the channel's ring buffer. */
	u32 ringbuffer_gpadlhandle;

	/*
	 * Starting with win8, this field will be used to specify
	 * the target virtual processor on which to deliver the interrupt for
	 * the host to guest communication.
	 * Prior to win8, incoming channel interrupts would only
	 * be delivered on cpu 0. Setting this value to 0 would
	 * preserve the earlier behavior.
	 */
	u32 target_vp;

	/*
	* The upstream ring buffer begins at offset zero in the memory
	* described by RingBufferGpadlHandle. The downstream ring buffer
	* follows it at this offset (in pages).
	*/
	u32 downstream_ringbuffer_pageoffset;

	/* User-specific data to be passed along to the server endpoint. */
	unsigned char userdata[MAX_USER_DEFINED_BYTES];
} __packed;

/* Open Channel Result parameters */
struct vmbus_channel_open_result {
	struct vmbus_channel_message_header header;
	u32 child_relid;
	u32 openid;
	u32 status;
} __packed;

/* Close channel parameters; */
struct vmbus_channel_close_channel {
	struct vmbus_channel_message_header header;
	u32 child_relid;
} __packed;

/* Channel Message GPADL */
#define GPADL_TYPE_RING_BUFFER		1
#define GPADL_TYPE_SERVER_SAVE_AREA	2
#define GPADL_TYPE_TRANSACTION		8

/*
 * The number of PFNs in a GPADL message is defined by the number of
 * pages that would be spanned by ByteCount and ByteOffset.  If the
 * implied number of PFNs won't fit in this packet, there will be a
 * follow-up packet that contains more.
 */
struct vmbus_channel_gpadl_header {
	struct vmbus_channel_message_header header;
	u32 child_relid;
	u32 gpadl;
	u16 range_buflen;
	u16 rangecount;
	struct gpa_range range[0];
} __packed;

/* This is the followup packet that contains more PFNs. */
struct vmbus_channel_gpadl_body {
	struct vmbus_channel_message_header header;
	u32 msgnumber;
	u32 gpadl;
	u64 pfn[0];
} __packed;

struct vmbus_channel_gpadl_created {
	struct vmbus_channel_message_header header;
	u32 child_relid;
	u32 gpadl;
	u32 creation_status;
} __packed;

struct vmbus_channel_gpadl_teardown {
	struct vmbus_channel_message_header header;
	u32 child_relid;
	u32 gpadl;
} __packed;

struct vmbus_channel_gpadl_torndown {
	struct vmbus_channel_message_header header;
	u32 gpadl;
} __packed;

#ifdef VMBUS_FEATURE_PARENT_OR_PEER_MEMORY_MAPPED_INTO_A_CHILD
struct vmbus_channel_view_range_add {
	struct vmbus_channel_message_header header;
	PHYSICAL_ADDRESS viewrange_base;
	u64 viewrange_length;
	u32 child_relid;
} __packed;

struct vmbus_channel_view_range_remove {
	struct vmbus_channel_message_header header;
	PHYSICAL_ADDRESS viewrange_base;
	u32 child_relid;
} __packed;
#endif

struct vmbus_channel_relid_released {
	struct vmbus_channel_message_header header;
	u32 child_relid;
} __packed;

struct vmbus_channel_initiate_contact {
	struct vmbus_channel_message_header header;
	u32 vmbus_version_requested;
	u32 padding2;
	u64 interrupt_page;
	u64 monitor_page1;
	u64 monitor_page2;
} __packed;

struct vmbus_channel_version_response {
	struct vmbus_channel_message_header header;
	u8 version_supported;
} __packed;

enum vmbus_channel_state {
	CHANNEL_OFFER_STATE,
	CHANNEL_OPENING_STATE,
	CHANNEL_OPEN_STATE,
	CHANNEL_OPENED_STATE,
};

struct vmbus_channel_debug_info {
	u32 relid;
	enum vmbus_channel_state state;
	uuid_le interfacetype;
	uuid_le interface_instance;
	u32 monitorid;
	u32 servermonitor_pending;
	u32 servermonitor_latency;
	u32 servermonitor_connectionid;
	u32 clientmonitor_pending;
	u32 clientmonitor_latency;
	u32 clientmonitor_connectionid;

	struct hv_ring_buffer_debug_info inbound;
	struct hv_ring_buffer_debug_info outbound;
};

/*
 * Represents each channel msg on the vmbus connection This is a
 * variable-size data structure depending on the msg type itself
 */
struct vmbus_channel_msginfo {
	/* Bookkeeping stuff */
	struct list_head msglistentry;

	/* So far, this is only used to handle gpadl body message */
	struct list_head submsglist;

	/* Synchronize the request/response if needed */
	struct completion  waitevent;
	union {
		struct vmbus_channel_version_supported version_supported;
		struct vmbus_channel_open_result open_result;
		struct vmbus_channel_gpadl_torndown gpadl_torndown;
		struct vmbus_channel_gpadl_created gpadl_created;
		struct vmbus_channel_version_response version_response;
	} response;

	u32 msgsize;
	/*
	 * The channel message that goes out on the "wire".
	 * It will contain at minimum the VMBUS_CHANNEL_MESSAGE_HEADER header
	 */
	unsigned char msg[0];
};

struct vmbus_close_msg {
	struct vmbus_channel_msginfo info;
	struct vmbus_channel_close_channel msg;
};

/* Define connection identifier type. */
union hv_connection_id {
	u32 asu32;
	struct {
		u32 id:24;
		u32 reserved:8;
	} u;
};

/* Definition of the hv_signal_event hypercall input structure. */
struct hv_input_signal_event {
	union hv_connection_id connectionid;
	u16 flag_number;
	u16 rsvdz;
};

struct hv_input_signal_event_buffer {
	u64 align8;
	struct hv_input_signal_event event;
};

struct vmbus_channel {
	struct list_head listentry;

	struct hv_device *device_obj;

	struct work_struct work;

	enum vmbus_channel_state state;

	struct vmbus_channel_offer_channel offermsg;
	/*
	 * These are based on the OfferMsg.MonitorId.
	 * Save it here for easy access.
	 */
	u8 monitor_grp;
	u8 monitor_bit;

	u32 ringbuffer_gpadlhandle;

	/* Allocated memory for ring buffer */
	void *ringbuffer_pages;
	u32 ringbuffer_pagecount;
	struct hv_ring_buffer_info outbound;	/* send to parent */
	struct hv_ring_buffer_info inbound;	/* receive from parent */
	spinlock_t inbound_lock;
	struct workqueue_struct *controlwq;

	struct vmbus_close_msg close_msg;

	/* Channel callback are invoked in this workqueue context */
	/* HANDLE dataWorkQueue; */

	void (*onchannel_callback)(void *context);
	void *channel_callback_context;

	/*
	 * A channel can be marked for efficient (batched)
	 * reading:
	 * If batched_reading is set to "true", we read until the
	 * channel is empty and hold off interrupts from the host
	 * during the entire read process.
	 * If batched_reading is set to "false", the client is not
	 * going to perform batched reading.
	 *
	 * By default we will enable batched reading; specific
	 * drivers that don't want this behavior can turn it off.
	 */

	bool batched_reading;

	bool is_dedicated_interrupt;
	struct hv_input_signal_event_buffer sig_buf;
	struct hv_input_signal_event *sig_event;

	/*
	 * Starting with win8, this field will be used to specify
	 * the target virtual processor on which to deliver the interrupt for
	 * the host to guest communication.
	 * Prior to win8, incoming channel interrupts would only
	 * be delivered on cpu 0. Setting this value to 0 would
	 * preserve the earlier behavior.
	 */
	u32 target_vp;
	/*
	 * Support for sub-channels. For high performance devices,
	 * it will be useful to have multiple sub-channels to support
	 * a scalable communication infrastructure with the host.
	 * The support for sub-channels is implemented as an extention
	 * to the current infrastructure.
	 * The initial offer is considered the primary channel and this
	 * offer message will indicate if the host supports sub-channels.
	 * The guest is free to ask for sub-channels to be offerred and can
	 * open these sub-channels as a normal "primary" channel. However,
	 * all sub-channels will have the same type and instance guids as the
	 * primary channel. Requests sent on a given channel will result in a
	 * response on the same channel.
	 */

	/*
	 * Sub-channel creation callback. This callback will be called in
	 * process context when a sub-channel offer is received from the host.
	 * The guest can open the sub-channel in the context of this callback.
	 */
	void (*sc_creation_callback)(struct vmbus_channel *new_sc);

	spinlock_t sc_lock;
	/*
	 * All Sub-channels of a primary channel are linked here.
	 */
	struct list_head sc_list;
	/*
	 * The primary channel this sub-channel belongs to.
	 * This will be NULL for the primary channel.
	 */
	struct vmbus_channel *primary_channel;
};

static inline void set_channel_read_state(struct vmbus_channel *c, bool state)
{
	c->batched_reading = state;
}

void vmbus_onmessage(void *context);

int vmbus_request_offers(void);

/*
 * APIs for managing sub-channels.
 */

void vmbus_set_sc_create_callback(struct vmbus_channel *primary_channel,
			void (*sc_cr_cb)(struct vmbus_channel *new_sc));

/*
 * Retrieve the (sub) channel on which to send an outgoing request.
 * When a primary channel has multiple sub-channels, we choose a
 * channel whose VCPU binding is closest to the VCPU on which
 * this call is being made.
 */
struct vmbus_channel *vmbus_get_outgoing_channel(struct vmbus_channel *primary);

/*
 * Check if sub-channels have already been offerred. This API will be useful
 * when the driver is unloaded after establishing sub-channels. In this case,
 * when the driver is re-loaded, the driver would have to check if the
 * subchannels have already been established before attempting to request
 * the creation of sub-channels.
 * This function returns TRUE to indicate that subchannels have already been
 * created.
 * This function should be invoked after setting the callback function for
 * sub-channel creation.
 */
bool vmbus_are_subchannels_present(struct vmbus_channel *primary);

/* The format must be the same as struct vmdata_gpa_direct */
struct vmbus_channel_packet_page_buffer {
	u16 type;
	u16 dataoffset8;
	u16 length8;
	u16 flags;
	u64 transactionid;
	u32 reserved;
	u32 rangecount;
	struct hv_page_buffer range[MAX_PAGE_BUFFER_COUNT];
} __packed;

/* The format must be the same as struct vmdata_gpa_direct */
struct vmbus_channel_packet_multipage_buffer {
	u16 type;
	u16 dataoffset8;
	u16 length8;
	u16 flags;
	u64 transactionid;
	u32 reserved;
	u32 rangecount;		/* Always 1 in this case */
	struct hv_multipage_buffer range;
} __packed;


extern int vmbus_open(struct vmbus_channel *channel,
			    u32 send_ringbuffersize,
			    u32 recv_ringbuffersize,
			    void *userdata,
			    u32 userdatalen,
			    void(*onchannel_callback)(void *context),
			    void *context);

extern void vmbus_close(struct vmbus_channel *channel);

extern int vmbus_sendpacket(struct vmbus_channel *channel,
				  const void *buffer,
				  u32 bufferLen,
				  u64 requestid,
				  enum vmbus_packet_type type,
				  u32 flags);

extern int vmbus_sendpacket_pagebuffer(struct vmbus_channel *channel,
					    struct hv_page_buffer pagebuffers[],
					    u32 pagecount,
					    void *buffer,
					    u32 bufferlen,
					    u64 requestid);

extern int vmbus_sendpacket_multipagebuffer(struct vmbus_channel *channel,
					struct hv_multipage_buffer *mpb,
					void *buffer,
					u32 bufferlen,
					u64 requestid);

extern int vmbus_establish_gpadl(struct vmbus_channel *channel,
				      void *kbuffer,
				      u32 size,
				      u32 *gpadl_handle);

extern int vmbus_teardown_gpadl(struct vmbus_channel *channel,
				     u32 gpadl_handle);

extern int vmbus_recvpacket(struct vmbus_channel *channel,
				  void *buffer,
				  u32 bufferlen,
				  u32 *buffer_actual_len,
				  u64 *requestid);

extern int vmbus_recvpacket_raw(struct vmbus_channel *channel,
				     void *buffer,
				     u32 bufferlen,
				     u32 *buffer_actual_len,
				     u64 *requestid);


extern void vmbus_get_debug_info(struct vmbus_channel *channel,
				     struct vmbus_channel_debug_info *debug);

extern void vmbus_ontimer(unsigned long data);

struct hv_dev_port_info {
	u32 int_mask;
	u32 read_idx;
	u32 write_idx;
	u32 bytes_avail_toread;
	u32 bytes_avail_towrite;
};

/* Base driver object */
struct hv_driver {
	const char *name;

	/* the device type supported by this driver */
	uuid_le dev_type;
	const struct hv_vmbus_device_id *id_table;

	struct device_driver driver;

	int (*probe)(struct hv_device *, const struct hv_vmbus_device_id *);
	int (*remove)(struct hv_device *);
	void (*shutdown)(struct hv_device *);

};

/* Base device object */
struct hv_device {
	/* the device type id of this device */
	uuid_le dev_type;

	/* the device instance id of this device */
	uuid_le dev_instance;

	struct device device;

	struct vmbus_channel *channel;
};


static inline struct hv_device *device_to_hv_device(struct device *d)
{
	return container_of(d, struct hv_device, device);
}

static inline struct hv_driver *drv_to_hv_drv(struct device_driver *d)
{
	return container_of(d, struct hv_driver, driver);
}

static inline void hv_set_drvdata(struct hv_device *dev, void *data)
{
	dev_set_drvdata(&dev->device, data);
}

static inline void *hv_get_drvdata(struct hv_device *dev)
{
	return dev_get_drvdata(&dev->device);
}

/* Vmbus interface */
#define vmbus_driver_register(driver)	\
	__vmbus_driver_register(driver, THIS_MODULE, KBUILD_MODNAME)
int __must_check __vmbus_driver_register(struct hv_driver *hv_driver,
					 struct module *owner,
					 const char *mod_name);
void vmbus_driver_unregister(struct hv_driver *hv_driver);

/**
 * VMBUS_DEVICE - macro used to describe a specific hyperv vmbus device
 *
 * This macro is used to create a struct hv_vmbus_device_id that matches a
 * specific device.
 */
#define VMBUS_DEVICE(g0, g1, g2, g3, g4, g5, g6, g7,	\
		     g8, g9, ga, gb, gc, gd, ge, gf)	\
	.guid = { g0, g1, g2, g3, g4, g5, g6, g7,	\
		  g8, g9, ga, gb, gc, gd, ge, gf },

/*
 * GUID definitions of various offer types - services offered to the guest.
 */

/*
 * Network GUID
 * {f8615163-df3e-46c5-913f-f2d2f965ed0e}
 */
#define HV_NIC_GUID \
	.guid = { \
			0x63, 0x51, 0x61, 0xf8, 0x3e, 0xdf, 0xc5, 0x46, \
			0x91, 0x3f, 0xf2, 0xd2, 0xf9, 0x65, 0xed, 0x0e \
		}

/*
 * IDE GUID
 * {32412632-86cb-44a2-9b5c-50d1417354f5}
 */
#define HV_IDE_GUID \
	.guid = { \
			0x32, 0x26, 0x41, 0x32, 0xcb, 0x86, 0xa2, 0x44, \
			0x9b, 0x5c, 0x50, 0xd1, 0x41, 0x73, 0x54, 0xf5 \
		}

/*
 * SCSI GUID
 * {ba6163d9-04a1-4d29-b605-72e2ffb1dc7f}
 */
#define HV_SCSI_GUID \
	.guid = { \
			0xd9, 0x63, 0x61, 0xba, 0xa1, 0x04, 0x29, 0x4d, \
			0xb6, 0x05, 0x72, 0xe2, 0xff, 0xb1, 0xdc, 0x7f \
		}

/*
 * Shutdown GUID
 * {0e0b6031-5213-4934-818b-38d90ced39db}
 */
#define HV_SHUTDOWN_GUID \
	.guid = { \
			0x31, 0x60, 0x0b, 0x0e, 0x13, 0x52, 0x34, 0x49, \
			0x81, 0x8b, 0x38, 0xd9, 0x0c, 0xed, 0x39, 0xdb \
		}

/*
 * Time Synch GUID
 * {9527E630-D0AE-497b-ADCE-E80AB0175CAF}
 */
#define HV_TS_GUID \
	.guid = { \
			0x30, 0xe6, 0x27, 0x95, 0xae, 0xd0, 0x7b, 0x49, \
			0xad, 0xce, 0xe8, 0x0a, 0xb0, 0x17, 0x5c, 0xaf \
		}

/*
 * Heartbeat GUID
 * {57164f39-9115-4e78-ab55-382f3bd5422d}
 */
#define HV_HEART_BEAT_GUID \
	.guid = { \
			0x39, 0x4f, 0x16, 0x57, 0x15, 0x91, 0x78, 0x4e, \
			0xab, 0x55, 0x38, 0x2f, 0x3b, 0xd5, 0x42, 0x2d \
		}

/*
 * KVP GUID
 * {a9a0f4e7-5a45-4d96-b827-8a841e8c03e6}
 */
#define HV_KVP_GUID \
	.guid = { \
			0xe7, 0xf4, 0xa0, 0xa9, 0x45, 0x5a, 0x96, 0x4d, \
			0xb8, 0x27, 0x8a, 0x84, 0x1e, 0x8c, 0x3,  0xe6 \
		}

/*
 * Dynamic memory GUID
 * {525074dc-8985-46e2-8057-a307dc18a502}
 */
#define HV_DM_GUID \
	.guid = { \
			0xdc, 0x74, 0x50, 0X52, 0x85, 0x89, 0xe2, 0x46, \
			0x80, 0x57, 0xa3, 0x07, 0xdc, 0x18, 0xa5, 0x02 \
		}

/*
 * Mouse GUID
 * {cfa8b69e-5b4a-4cc0-b98b-8ba1a1f3f95a}
 */
#define HV_MOUSE_GUID \
	.guid = { \
			0x9e, 0xb6, 0xa8, 0xcf, 0x4a, 0x5b, 0xc0, 0x4c, \
			0xb9, 0x8b, 0x8b, 0xa1, 0xa1, 0xf3, 0xf9, 0x5a \
		}

/*
 * VSS (Backup/Restore) GUID
 */
#define HV_VSS_GUID \
	.guid = { \
			0x29, 0x2e, 0xfa, 0x35, 0x23, 0xea, 0x36, 0x42, \
			0x96, 0xae, 0x3a, 0x6e, 0xba, 0xcb, 0xa4,  0x40 \
		}
/*
 * Synthetic Video GUID
 * {DA0A7802-E377-4aac-8E77-0558EB1073F8}
 */
#define HV_SYNTHVID_GUID \
	.guid = { \
			0x02, 0x78, 0x0a, 0xda, 0x77, 0xe3, 0xac, 0x4a, \
			0x8e, 0x77, 0x05, 0x58, 0xeb, 0x10, 0x73, 0xf8 \
		}

/*
 * Synthetic FC GUID
 * {2f9bcc4a-0069-4af3-b76b-6fd0be528cda}
 */
#define HV_SYNTHFC_GUID \
	.guid = { \
			0x4A, 0xCC, 0x9B, 0x2F, 0x69, 0x00, 0xF3, 0x4A, \
			0xB7, 0x6B, 0x6F, 0xD0, 0xBE, 0x52, 0x8C, 0xDA \
		}

/*
 * Common header for Hyper-V ICs
 */

#define ICMSGTYPE_NEGOTIATE		0
#define ICMSGTYPE_HEARTBEAT		1
#define ICMSGTYPE_KVPEXCHANGE		2
#define ICMSGTYPE_SHUTDOWN		3
#define ICMSGTYPE_TIMESYNC		4
#define ICMSGTYPE_VSS			5

#define ICMSGHDRFLAG_TRANSACTION	1
#define ICMSGHDRFLAG_REQUEST		2
#define ICMSGHDRFLAG_RESPONSE		4


/*
 * While we want to handle util services as regular devices,
 * there is only one instance of each of these services; so
 * we statically allocate the service specific state.
 */

struct hv_util_service {
	u8 *recv_buffer;
	void (*util_cb)(void *);
	int (*util_init)(struct hv_util_service *);
	void (*util_deinit)(void);
};

struct vmbuspipe_hdr {
	u32 flags;
	u32 msgsize;
} __packed;

struct ic_version {
	u16 major;
	u16 minor;
} __packed;

struct icmsg_hdr {
	struct ic_version icverframe;
	u16 icmsgtype;
	struct ic_version icvermsg;
	u16 icmsgsize;
	u32 status;
	u8 ictransaction_id;
	u8 icflags;
	u8 reserved[2];
} __packed;

struct icmsg_negotiate {
	u16 icframe_vercnt;
	u16 icmsg_vercnt;
	u32 reserved;
	struct ic_version icversion_data[1]; /* any size array */
} __packed;

struct shutdown_msg_data {
	u32 reason_code;
	u32 timeout_seconds;
	u32 flags;
	u8  display_message[2048];
} __packed;

struct heartbeat_msg_data {
	u64 seq_num;
	u32 reserved[8];
} __packed;

/* Time Sync IC defs */
#define ICTIMESYNCFLAG_PROBE	0
#define ICTIMESYNCFLAG_SYNC	1
#define ICTIMESYNCFLAG_SAMPLE	2

#ifdef __x86_64__
#define WLTIMEDELTA	116444736000000000L	/* in 100ns unit */
#else
#define WLTIMEDELTA	116444736000000000LL
#endif

struct ictimesync_data {
	u64 parenttime;
	u64 childtime;
	u64 roundtriptime;
	u8 flags;
} __packed;

struct hyperv_service_callback {
	u8 msg_type;
	char *log_msg;
	uuid_le data;
	struct vmbus_channel *channel;
	void (*callback) (void *context);
};

#define MAX_SRV_VER	0x7ffffff
extern void vmbus_prep_negotiate_resp(struct icmsg_hdr *,
					struct icmsg_negotiate *, u8 *, int,
					int);

int hv_kvp_init(struct hv_util_service *);
void hv_kvp_deinit(void);
void hv_kvp_onchannelcallback(void *);

int hv_vss_init(struct hv_util_service *);
void hv_vss_deinit(void);
void hv_vss_onchannelcallback(void *);

/*
 * Negotiated version with the Host.
 */

extern __u32 vmbus_proto_version;

#endif /* __KERNEL__ */
#endif /* _HYPERV_H */
