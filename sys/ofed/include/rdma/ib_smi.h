/*-
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0
 *
 * Copyright (c) 2004 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2004 Infinicon Corporation.  All rights reserved.
 * Copyright (c) 2004 Intel Corporation.  All rights reserved.
 * Copyright (c) 2004 Topspin Corporation.  All rights reserved.
 * Copyright (c) 2004 Voltaire Corporation.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $FreeBSD$
 */

#if !defined(IB_SMI_H)
#define IB_SMI_H

#include <rdma/ib_mad.h>

#define IB_SMP_DATA_SIZE			64
#define IB_SMP_MAX_PATH_HOPS			64

struct ib_smp {
	u8	base_version;
	u8	mgmt_class;
	u8	class_version;
	u8	method;
	__be16	status;
	u8	hop_ptr;
	u8	hop_cnt;
	__be64	tid;
	__be16	attr_id;
	__be16	resv;
	__be32	attr_mod;
	__be64	mkey;
	__be16	dr_slid;
	__be16	dr_dlid;
	u8	reserved[28];
	u8	data[IB_SMP_DATA_SIZE];
	u8	initial_path[IB_SMP_MAX_PATH_HOPS];
	u8	return_path[IB_SMP_MAX_PATH_HOPS];
} __attribute__ ((packed));

#define IB_SMP_DIRECTION			cpu_to_be16(0x8000)

/* Subnet management attributes */
#define IB_SMP_ATTR_NOTICE			cpu_to_be16(0x0002)
#define IB_SMP_ATTR_NODE_DESC			cpu_to_be16(0x0010)
#define IB_SMP_ATTR_NODE_INFO			cpu_to_be16(0x0011)
#define IB_SMP_ATTR_SWITCH_INFO			cpu_to_be16(0x0012)
#define IB_SMP_ATTR_GUID_INFO			cpu_to_be16(0x0014)
#define IB_SMP_ATTR_PORT_INFO			cpu_to_be16(0x0015)
#define IB_SMP_ATTR_PKEY_TABLE			cpu_to_be16(0x0016)
#define IB_SMP_ATTR_SL_TO_VL_TABLE		cpu_to_be16(0x0017)
#define IB_SMP_ATTR_VL_ARB_TABLE		cpu_to_be16(0x0018)
#define IB_SMP_ATTR_LINEAR_FORWARD_TABLE	cpu_to_be16(0x0019)
#define IB_SMP_ATTR_RANDOM_FORWARD_TABLE	cpu_to_be16(0x001A)
#define IB_SMP_ATTR_MCAST_FORWARD_TABLE		cpu_to_be16(0x001B)
#define IB_SMP_ATTR_SM_INFO			cpu_to_be16(0x0020)
#define IB_SMP_ATTR_VENDOR_DIAG			cpu_to_be16(0x0030)
#define IB_SMP_ATTR_LED_INFO			cpu_to_be16(0x0031)
#define IB_SMP_ATTR_VENDOR_MASK			cpu_to_be16(0xFF00)

struct ib_port_info {
	__be64 mkey;
	__be64 gid_prefix;
	__be16 lid;
	__be16 sm_lid;
	__be32 cap_mask;
	__be16 diag_code;
	__be16 mkey_lease_period;
	u8 local_port_num;
	u8 link_width_enabled;
	u8 link_width_supported;
	u8 link_width_active;
	u8 linkspeed_portstate;			/* 4 bits, 4 bits */
	u8 portphysstate_linkdown;		/* 4 bits, 4 bits */
	u8 mkeyprot_resv_lmc;			/* 2 bits, 3, 3 */
	u8 linkspeedactive_enabled;		/* 4 bits, 4 bits */
	u8 neighbormtu_mastersmsl;		/* 4 bits, 4 bits */
	u8 vlcap_inittype;			/* 4 bits, 4 bits */
	u8 vl_high_limit;
	u8 vl_arb_high_cap;
	u8 vl_arb_low_cap;
	u8 inittypereply_mtucap;		/* 4 bits, 4 bits */
	u8 vlstallcnt_hoqlife;			/* 3 bits, 5 bits */
	u8 operationalvl_pei_peo_fpi_fpo;	/* 4 bits, 1, 1, 1, 1 */
	__be16 mkey_violations;
	__be16 pkey_violations;
	__be16 qkey_violations;
	u8 guid_cap;
	u8 clientrereg_resv_subnetto;		/* 1 bit, 2 bits, 5 */
	u8 resv_resptimevalue;			/* 3 bits, 5 bits */
	u8 localphyerrors_overrunerrors;	/* 4 bits, 4 bits */
	__be16 max_credit_hint;
	u8 resv;
	u8 link_roundtrip_latency[3];
};

struct ib_node_info {
	u8 base_version;
	u8 class_version;
	u8 node_type;
	u8 num_ports;
	__be64 sys_guid;
	__be64 node_guid;
	__be64 port_guid;
	__be16 partition_cap;
	__be16 device_id;
	__be32 revision;
	u8 local_port_num;
	u8 vendor_id[3];
} __packed;

struct ib_vl_weight_elem {
	u8      vl;     /* IB: VL is low 4 bits, upper 4 bits reserved */
                        /* OPA: VL is low 5 bits, upper 3 bits reserved */
	u8      weight;
};

static inline u8
ib_get_smp_direction(const struct ib_smp *smp)
{
	return ((smp->status & IB_SMP_DIRECTION) == IB_SMP_DIRECTION);
}

/*
 * SM Trap/Notice numbers
 */
#define IB_NOTICE_TRAP_LLI_THRESH	cpu_to_be16(129)
#define IB_NOTICE_TRAP_EBO_THRESH	cpu_to_be16(130)
#define IB_NOTICE_TRAP_FLOW_UPDATE	cpu_to_be16(131)
#define IB_NOTICE_TRAP_CAP_MASK_CHG	cpu_to_be16(144)
#define IB_NOTICE_TRAP_SYS_GUID_CHG	cpu_to_be16(145)
#define IB_NOTICE_TRAP_BAD_MKEY		cpu_to_be16(256)
#define IB_NOTICE_TRAP_BAD_PKEY		cpu_to_be16(257)
#define IB_NOTICE_TRAP_BAD_QKEY		cpu_to_be16(258)

/*
 * Other local changes flags (trap 144).
 */
#define IB_NOTICE_TRAP_LSE_CHG		0x04	/* Link Speed Enable changed */
#define IB_NOTICE_TRAP_LWE_CHG		0x02	/* Link Width Enable changed */
#define IB_NOTICE_TRAP_NODE_DESC_CHG	0x01

/*
 * M_Key volation flags in dr_trunc_hop (trap 256).
 */
#define IB_NOTICE_TRAP_DR_NOTICE	0x80
#define IB_NOTICE_TRAP_DR_TRUNC		0x40


#endif /* IB_SMI_H */
