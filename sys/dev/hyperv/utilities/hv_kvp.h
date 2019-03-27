/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014,2016 Microsoft Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _KVP_H
#define _KVP_H
/*
 * An implementation of HyperV key value pair (KVP) functionality for FreeBSD
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
#define HV_KVP_EXCHANGE_MAX_VALUE_SIZE    (2048)


/*
 * Maximum key size - the registry limit for the length of an entry name
 * is 256 characters, including the null terminator
 */
#define HV_KVP_EXCHANGE_MAX_KEY_SIZE    (512)


/*
 * In FreeBSD, we implement the KVP functionality in two components:
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
 *  Index		Key Name
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
#define HV_REG_SZ     1
#define HV_REG_U32    4
#define HV_REG_U64    8


/*
 * Daemon code supporting IP injection.
 */
#define HV_KVP_OP_REGISTER    4


enum hv_kvp_exchg_op {
	HV_KVP_OP_GET = 0,
	HV_KVP_OP_SET,
	HV_KVP_OP_DELETE,
	HV_KVP_OP_ENUMERATE,
	HV_KVP_OP_GET_IP_INFO,
	HV_KVP_OP_SET_IP_INFO,
	HV_KVP_OP_COUNT /* Number of operations, must be last. */
};

enum hv_kvp_exchg_pool {
	HV_KVP_POOL_EXTERNAL = 0,
	HV_KVP_POOL_GUEST,
	HV_KVP_POOL_AUTO,
	HV_KVP_POOL_AUTO_EXTERNAL,
	HV_KVP_POOL_AUTO_INTERNAL,
	HV_KVP_POOL_COUNT /* Number of pools, must be last. */
};

#define ADDR_FAMILY_NONE                 0x00
#define ADDR_FAMILY_IPV4                 0x01
#define ADDR_FAMILY_IPV6                 0x02

#define MAX_ADAPTER_ID_SIZE              128
#define MAX_IP_ADDR_SIZE                 1024
#define MAX_GATEWAY_SIZE                 512


struct hv_kvp_ipaddr_value {
	uint16_t adapter_id[MAX_ADAPTER_ID_SIZE];
	uint8_t  addr_family;
	uint8_t  dhcp_enabled;
	uint16_t ip_addr[MAX_IP_ADDR_SIZE];
	uint16_t sub_net[MAX_IP_ADDR_SIZE];
	uint16_t gate_way[MAX_GATEWAY_SIZE];
	uint16_t dns_addr[MAX_IP_ADDR_SIZE];
}__attribute__((packed));

struct hv_kvp_hdr {
	uint8_t                 operation;
	uint8_t                 pool;
	uint16_t                pad;
} __attribute__((packed));

struct hv_kvp_exchg_msg_value {
	uint32_t value_type;
	uint32_t key_size;
	uint32_t value_size;
	uint8_t  key[HV_KVP_EXCHANGE_MAX_KEY_SIZE];
	union {
		uint8_t  value[HV_KVP_EXCHANGE_MAX_VALUE_SIZE];
		uint32_t value_u32;
		uint64_t value_u64;
	} msg_value;
} __attribute__((packed));

struct hv_kvp_msg_enumerate {
	uint32_t index;
	struct hv_kvp_exchg_msg_value data;
} __attribute__((packed));

struct hv_kvp_msg_get {
	struct hv_kvp_exchg_msg_value data;
} __attribute__((packed));

struct hv_kvp_msg_set {
	struct hv_kvp_exchg_msg_value data;
} __attribute__((packed));

struct hv_kvp_msg_delete {
	uint32_t key_size;
	uint8_t key[HV_KVP_EXCHANGE_MAX_KEY_SIZE];
} __attribute__((packed));

struct hv_kvp_register {
	uint8_t version[HV_KVP_EXCHANGE_MAX_KEY_SIZE];
} __attribute__((packed));

struct hv_kvp_msg {
	union {
		struct hv_kvp_hdr kvp_hdr;
		uint32_t error;
	} hdr;
	union {
		struct hv_kvp_msg_get		kvp_get;
		struct hv_kvp_msg_set		kvp_set;
		struct hv_kvp_msg_delete	kvp_delete;
		struct hv_kvp_msg_enumerate	kvp_enum_data;
		struct hv_kvp_ipaddr_value	kvp_ip_val;
		struct hv_kvp_register		kvp_register;
	} body;
} __attribute__((packed));

struct hv_kvp_ip_msg {
	uint8_t operation;
	uint8_t pool;
	struct hv_kvp_ipaddr_value      kvp_ip_val;
} __attribute__((packed));

#endif /* _KVP_H */
