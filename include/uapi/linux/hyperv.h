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

#ifndef _UAPI_HYPERV_H
#define _UAPI_HYPERV_H

#include <linux/uuid.h>

/*
 * Framework version for util services.
 */
#define UTIL_FW_MINOR  0

#define UTIL_WS2K8_FW_MAJOR  1
#define UTIL_WS2K8_FW_VERSION     (UTIL_WS2K8_FW_MAJOR << 16 | UTIL_FW_MINOR)

#define UTIL_FW_MAJOR  3
#define UTIL_FW_VERSION     (UTIL_FW_MAJOR << 16 | UTIL_FW_MINOR)


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
 * Implementation of a host to guest copy facility.
 */

#define FCOPY_VERSION_0 0
#define FCOPY_CURRENT_VERSION FCOPY_VERSION_0
#define W_MAX_PATH 260

enum hv_fcopy_op {
	START_FILE_COPY = 0,
	WRITE_TO_FILE,
	COMPLETE_FCOPY,
	CANCEL_FCOPY,
};

struct hv_fcopy_hdr {
	__u32 operation;
	uuid_le service_id0; /* currently unused */
	uuid_le service_id1; /* currently unused */
} __attribute__((packed));

#define OVER_WRITE	0x1
#define CREATE_PATH	0x2

struct hv_start_fcopy {
	struct hv_fcopy_hdr hdr;
	__u16 file_name[W_MAX_PATH];
	__u16 path_name[W_MAX_PATH];
	__u32 copy_flags;
	__u64 file_size;
} __attribute__((packed));

/*
 * The file is chunked into fragments.
 */
#define DATA_FRAGMENT	(6 * 1024)

struct hv_do_fcopy {
	struct hv_fcopy_hdr hdr;
	__u64	offset;
	__u32	size;
	__u8	data[DATA_FRAGMENT];
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
#define HV_ERROR_ALREADY_EXISTS		0x80070050

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

#endif /* _UAPI_HYPERV_H */
