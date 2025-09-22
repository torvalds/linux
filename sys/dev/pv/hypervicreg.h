/*-
 * Copyright (c) 2016 Microsoft Corp.
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
 * $FreeBSD: head/sys/dev/hyperv/utilities/vmbus_icreg.h 305281 2016-09-02 06:23:28Z sephe $
 */

#ifndef _DEV_PV_HYPERVIC_H_
#define _DEV_PV_HYPERVIC_H_

#define VMBUS_IC_BUFRINGSIZE		(4 * PAGE_SIZE)

#define VMBUS_ICMSG_TYPE_NEGOTIATE	0
#define VMBUS_ICMSG_TYPE_HEARTBEAT	1
#define VMBUS_ICMSG_TYPE_KVP		2
#define VMBUS_ICMSG_TYPE_SHUTDOWN	3
#define VMBUS_ICMSG_TYPE_TIMESYNC	4
#define VMBUS_ICMSG_TYPE_VSS		5

#define VMBUS_ICMSG_STATUS_OK		0x00000000
#define VMBUS_ICMSG_STATUS_FAIL		0x80004005

#define VMBUS_ICMSG_FLAG_TRANSACTION	1
#define VMBUS_ICMSG_FLAG_REQUEST	2
#define VMBUS_ICMSG_FLAG_RESPONSE	4

#define VMBUS_IC_VERSION(major, minor)	((major) | (((uint32_t)(minor)) << 16))
#define VMBUS_ICVER_MAJOR(ver)		((ver) & 0xffff)
#define VMBUS_ICVER_MINOR(ver)		(((ver) & 0xffff0000) >> 16)

struct vmbus_pipe_hdr {
	uint32_t		ph_flags;
	uint32_t		ph_msgsz;
} __packed;

struct vmbus_icmsg_hdr {
	struct vmbus_pipe_hdr	ic_pipe;
	uint32_t		ic_fwver;	/* framework version */
	uint16_t		ic_type;
	uint32_t		ic_msgver;	/* message version */
	uint16_t		ic_dsize;	/* data size */
	uint32_t		ic_status;	/* VMBUS_ICMSG_STATUS_ */
	uint8_t			ic_tid;
	uint8_t			ic_flags;	/* VMBUS_ICMSG_FLAG_ */
	uint8_t			ic_rsvd[2];
} __packed;

/* VMBUS_ICMSG_TYPE_NEGOTIATE */
struct vmbus_icmsg_negotiate {
	struct vmbus_icmsg_hdr	ic_hdr;
	uint16_t		ic_fwver_cnt;
	uint16_t		ic_msgver_cnt;
	uint32_t		ic_rsvd;
	/*
	 * This version array contains two set of supported
	 * versions:
	 * - The first set consists of #ic_fwver_cnt supported framework
	 *   versions.
	 * - The second set consists of #ic_msgver_cnt supported message
	 *   versions.
	 */
	uint32_t		ic_ver[0];
} __packed;

/* VMBUS_ICMSG_TYPE_HEARTBEAT */
struct vmbus_icmsg_heartbeat {
	struct vmbus_icmsg_hdr	ic_hdr;
	uint64_t		ic_seq;
	uint32_t		ic_rsvd[8];
} __packed;

/* VMBUS_ICMSG_TYPE_SHUTDOWN */
struct vmbus_icmsg_shutdown {
	struct vmbus_icmsg_hdr	ic_hdr;
	uint32_t		ic_code;
	uint32_t		ic_timeo;
	uint32_t 		ic_haltflags;
	uint8_t			ic_msg[2048];
} __packed;

/* VMBUS_ICMSG_TYPE_TIMESYNC */
struct vmbus_icmsg_timesync {
	struct vmbus_icmsg_hdr	ic_hdr;
	uint64_t		ic_hvtime;
	uint64_t		ic_vmtime;
	uint64_t		ic_rtt;
	uint8_t			ic_tsflags;	/* VMBUS_ICMSG_TS_FLAG_ */
} __packed;

#define VMBUS_ICMSG_TS_FLAG_SYNC	0x01
#define VMBUS_ICMSG_TS_FLAG_SAMPLE	0x02

/* Registry value types */
#define HV_KVP_REG_SZ			1
#define HV_KVP_REG_U32			4
#define HV_KVP_REG_U64			8

/* Hyper-V status codes */
#define HV_KVP_S_OK			0x00000000
#define HV_KVP_E_FAIL			0x80004005
#define HV_KVP_S_CONT			0x80070103

#define HV_KVP_MAX_VAL_SIZE		2048
#define HV_KVP_MAX_KEY_SIZE		512

enum hv_kvp_op {
	HV_KVP_OP_GET = 0,
	HV_KVP_OP_SET,
	HV_KVP_OP_DELETE,
	HV_KVP_OP_ENUMERATE,
	HV_KVP_OP_GET_IP_INFO,
	HV_KVP_OP_SET_IP_INFO,
	HV_KVP_OP_COUNT
};

enum hv_kvp_pool {
	HV_KVP_POOL_EXTERNAL = 0,
	HV_KVP_POOL_GUEST,
	HV_KVP_POOL_AUTO,
	HV_KVP_POOL_AUTO_EXTERNAL,
	HV_KVP_POOL_COUNT
};

union hv_kvp_hdr {
	struct {
		uint8_t		kvu_op;
		uint8_t		kvu_pool;
		uint16_t	kvu_pad;
	} req;
	struct {
		uint32_t	kvu_err;
	} rsp;
#define kvh_op			req.kvu_op
#define kvh_pool		req.kvu_pool
#define kvh_err			rsp.kvu_err
} __packed;

struct hv_kvp_msg_val {
	uint32_t		kvm_valtype;
	uint32_t		kvm_keylen;
	uint32_t		kvm_vallen;
	uint8_t			kvm_key[HV_KVP_MAX_KEY_SIZE];
	uint8_t			kvm_val[HV_KVP_MAX_VAL_SIZE];
} __packed;

struct hv_kvp_msg_enum {
	uint32_t		kvm_index;
	uint32_t		kvm_valtype;
	uint32_t		kvm_keylen;
	uint32_t		kvm_vallen;
	uint8_t			kvm_key[HV_KVP_MAX_KEY_SIZE];
	uint8_t			kvm_val[HV_KVP_MAX_VAL_SIZE];
} __packed;

struct hv_kvp_msg_del {
	uint32_t		kvm_keylen;
	uint8_t			kvm_key[HV_KVP_MAX_KEY_SIZE];
} __packed;

#define ADDR_FAMILY_NONE	0x00
#define ADDR_FAMILY_IPV4	0x01
#define ADDR_FAMILY_IPV6	0x02

#define MAX_MAC_ADDR_SIZE	256
#define MAX_IP_ADDR_SIZE	2048
#define MAX_GATEWAY_SIZE	1024

struct hv_kvp_msg_addr {
	uint8_t			kvm_mac[MAX_MAC_ADDR_SIZE];
	uint8_t			kvm_family;
	uint8_t			kvm_dhcp;
	uint8_t			kvm_addr[MAX_IP_ADDR_SIZE];
	uint8_t			kvm_netmask[MAX_IP_ADDR_SIZE];
	uint8_t			kvm_gateway[MAX_GATEWAY_SIZE];
	uint8_t			kvm_dns[MAX_IP_ADDR_SIZE];
} __packed;

union hv_kvp_msg {
	struct hv_kvp_msg_val	kvm_val;
	struct hv_kvp_msg_enum	kvm_enum;
	struct hv_kvp_msg_del	kvm_del;
};

struct vmbus_icmsg_kvp {
	struct vmbus_icmsg_hdr	ic_hdr;
	union hv_kvp_hdr	ic_kvh;
	union hv_kvp_msg	ic_kvm;
} __packed;

struct vmbus_icmsg_kvp_addr {
	struct vmbus_icmsg_hdr	ic_hdr;
	struct {
		struct {
			uint8_t	kvu_op;
			uint8_t	kvu_pool;
		} req;
	}			ic_kvh;
	struct hv_kvp_msg_addr	ic_kvm;
} __packed;

#endif	/* _DEV_PV_HYPERVIC_H_ */
