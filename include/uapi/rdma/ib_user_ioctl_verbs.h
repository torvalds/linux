/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR Linux-OpenIB) */
/*
 * Copyright (c) 2017-2018, Mellanox Technologies inc.  All rights reserved.
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
 */

#ifndef IB_USER_IOCTL_VERBS_H
#define IB_USER_IOCTL_VERBS_H

#include <linux/types.h>
#include <rdma/ib_user_verbs.h>

#ifndef RDMA_UAPI_PTR
#define RDMA_UAPI_PTR(_type, _name)	__aligned_u64 _name
#endif

#define IB_UVERBS_ACCESS_OPTIONAL_FIRST (1 << 20)
#define IB_UVERBS_ACCESS_OPTIONAL_LAST (1 << 29)

enum ib_uverbs_core_support {
	IB_UVERBS_CORE_SUPPORT_OPTIONAL_MR_ACCESS = 1 << 0,
};

enum ib_uverbs_access_flags {
	IB_UVERBS_ACCESS_LOCAL_WRITE = 1 << 0,
	IB_UVERBS_ACCESS_REMOTE_WRITE = 1 << 1,
	IB_UVERBS_ACCESS_REMOTE_READ = 1 << 2,
	IB_UVERBS_ACCESS_REMOTE_ATOMIC = 1 << 3,
	IB_UVERBS_ACCESS_MW_BIND = 1 << 4,
	IB_UVERBS_ACCESS_ZERO_BASED = 1 << 5,
	IB_UVERBS_ACCESS_ON_DEMAND = 1 << 6,
	IB_UVERBS_ACCESS_HUGETLB = 1 << 7,
	IB_UVERBS_ACCESS_FLUSH_GLOBAL = 1 << 8,
	IB_UVERBS_ACCESS_FLUSH_PERSISTENT = 1 << 9,

	IB_UVERBS_ACCESS_RELAXED_ORDERING = IB_UVERBS_ACCESS_OPTIONAL_FIRST,
	IB_UVERBS_ACCESS_OPTIONAL_RANGE =
		((IB_UVERBS_ACCESS_OPTIONAL_LAST << 1) - 1) &
		~(IB_UVERBS_ACCESS_OPTIONAL_FIRST - 1)
};

enum ib_uverbs_srq_type {
	IB_UVERBS_SRQT_BASIC,
	IB_UVERBS_SRQT_XRC,
	IB_UVERBS_SRQT_TM,
};

enum ib_uverbs_wq_type {
	IB_UVERBS_WQT_RQ,
};

enum ib_uverbs_wq_flags {
	IB_UVERBS_WQ_FLAGS_CVLAN_STRIPPING = 1 << 0,
	IB_UVERBS_WQ_FLAGS_SCATTER_FCS = 1 << 1,
	IB_UVERBS_WQ_FLAGS_DELAY_DROP = 1 << 2,
	IB_UVERBS_WQ_FLAGS_PCI_WRITE_END_PADDING = 1 << 3,
};

enum ib_uverbs_qp_type {
	IB_UVERBS_QPT_RC = 2,
	IB_UVERBS_QPT_UC,
	IB_UVERBS_QPT_UD,
	IB_UVERBS_QPT_RAW_PACKET = 8,
	IB_UVERBS_QPT_XRC_INI,
	IB_UVERBS_QPT_XRC_TGT,
	IB_UVERBS_QPT_DRIVER = 0xFF,
};

enum ib_uverbs_qp_create_flags {
	IB_UVERBS_QP_CREATE_BLOCK_MULTICAST_LOOPBACK = 1 << 1,
	IB_UVERBS_QP_CREATE_SCATTER_FCS = 1 << 8,
	IB_UVERBS_QP_CREATE_CVLAN_STRIPPING = 1 << 9,
	IB_UVERBS_QP_CREATE_PCI_WRITE_END_PADDING = 1 << 11,
	IB_UVERBS_QP_CREATE_SQ_SIG_ALL = 1 << 12,
};

enum ib_uverbs_query_port_cap_flags {
	IB_UVERBS_PCF_SM = 1 << 1,
	IB_UVERBS_PCF_NOTICE_SUP = 1 << 2,
	IB_UVERBS_PCF_TRAP_SUP = 1 << 3,
	IB_UVERBS_PCF_OPT_IPD_SUP = 1 << 4,
	IB_UVERBS_PCF_AUTO_MIGR_SUP = 1 << 5,
	IB_UVERBS_PCF_SL_MAP_SUP = 1 << 6,
	IB_UVERBS_PCF_MKEY_NVRAM = 1 << 7,
	IB_UVERBS_PCF_PKEY_NVRAM = 1 << 8,
	IB_UVERBS_PCF_LED_INFO_SUP = 1 << 9,
	IB_UVERBS_PCF_SM_DISABLED = 1 << 10,
	IB_UVERBS_PCF_SYS_IMAGE_GUID_SUP = 1 << 11,
	IB_UVERBS_PCF_PKEY_SW_EXT_PORT_TRAP_SUP = 1 << 12,
	IB_UVERBS_PCF_EXTENDED_SPEEDS_SUP = 1 << 14,
	IB_UVERBS_PCF_CM_SUP = 1 << 16,
	IB_UVERBS_PCF_SNMP_TUNNEL_SUP = 1 << 17,
	IB_UVERBS_PCF_REINIT_SUP = 1 << 18,
	IB_UVERBS_PCF_DEVICE_MGMT_SUP = 1 << 19,
	IB_UVERBS_PCF_VENDOR_CLASS_SUP = 1 << 20,
	IB_UVERBS_PCF_DR_NOTICE_SUP = 1 << 21,
	IB_UVERBS_PCF_CAP_MASK_NOTICE_SUP = 1 << 22,
	IB_UVERBS_PCF_BOOT_MGMT_SUP = 1 << 23,
	IB_UVERBS_PCF_LINK_LATENCY_SUP = 1 << 24,
	IB_UVERBS_PCF_CLIENT_REG_SUP = 1 << 25,
	/*
	 * IsOtherLocalChangesNoticeSupported is aliased by IP_BASED_GIDS and
	 * is inaccessible
	 */
	IB_UVERBS_PCF_LINK_SPEED_WIDTH_TABLE_SUP = 1 << 27,
	IB_UVERBS_PCF_VENDOR_SPECIFIC_MADS_TABLE_SUP = 1 << 28,
	IB_UVERBS_PCF_MCAST_PKEY_TRAP_SUPPRESSION_SUP = 1 << 29,
	IB_UVERBS_PCF_MCAST_FDB_TOP_SUP = 1 << 30,
	IB_UVERBS_PCF_HIERARCHY_INFO_SUP = 1ULL << 31,

	/* NOTE this is an internal flag, not an IBA flag */
	IB_UVERBS_PCF_IP_BASED_GIDS = 1 << 26,
};

enum ib_uverbs_query_port_flags {
	IB_UVERBS_QPF_GRH_REQUIRED = 1 << 0,
};

enum ib_uverbs_flow_action_esp_keymat {
	IB_UVERBS_FLOW_ACTION_ESP_KEYMAT_AES_GCM,
};

enum ib_uverbs_flow_action_esp_keymat_aes_gcm_iv_algo {
	IB_UVERBS_FLOW_ACTION_IV_ALGO_SEQ,
};

struct ib_uverbs_flow_action_esp_keymat_aes_gcm {
	__aligned_u64	iv;
	__u32		iv_algo; /* Use enum ib_uverbs_flow_action_esp_keymat_aes_gcm_iv_algo */

	__u32		salt;
	__u32		icv_len;

	__u32		key_len;
	__u32		aes_key[256 / 32];
};

enum ib_uverbs_flow_action_esp_replay {
	IB_UVERBS_FLOW_ACTION_ESP_REPLAY_NONE,
	IB_UVERBS_FLOW_ACTION_ESP_REPLAY_BMP,
};

struct ib_uverbs_flow_action_esp_replay_bmp {
	__u32	size;
};

enum ib_uverbs_flow_action_esp_flags {
	IB_UVERBS_FLOW_ACTION_ESP_FLAGS_INLINE_CRYPTO	= 0UL << 0,	/* Default */
	IB_UVERBS_FLOW_ACTION_ESP_FLAGS_FULL_OFFLOAD	= 1UL << 0,

	IB_UVERBS_FLOW_ACTION_ESP_FLAGS_TUNNEL		= 0UL << 1,	/* Default */
	IB_UVERBS_FLOW_ACTION_ESP_FLAGS_TRANSPORT	= 1UL << 1,

	IB_UVERBS_FLOW_ACTION_ESP_FLAGS_DECRYPT		= 0UL << 2,	/* Default */
	IB_UVERBS_FLOW_ACTION_ESP_FLAGS_ENCRYPT		= 1UL << 2,

	IB_UVERBS_FLOW_ACTION_ESP_FLAGS_ESN_NEW_WINDOW	= 1UL << 3,
};

struct ib_uverbs_flow_action_esp_encap {
	/* This struct represents a list of pointers to flow_xxxx_filter that
	 * encapsulates the payload in ESP tunnel mode.
	 */
	RDMA_UAPI_PTR(void *, val_ptr); /* pointer to a flow_xxxx_filter */
	RDMA_UAPI_PTR(struct ib_uverbs_flow_action_esp_encap *, next_ptr);
	__u16	len;		/* Len of the filter struct val_ptr points to */
	__u16	type;		/* Use flow_spec_type enum */
};

struct ib_uverbs_flow_action_esp {
	__u32		spi;
	__u32		seq;
	__u32		tfc_pad;
	__u32		flags;
	__aligned_u64	hard_limit_pkts;
};

enum ib_uverbs_read_counters_flags {
	/* prefer read values from driver cache */
	IB_UVERBS_READ_COUNTERS_PREFER_CACHED = 1 << 0,
};

enum ib_uverbs_advise_mr_advice {
	IB_UVERBS_ADVISE_MR_ADVICE_PREFETCH,
	IB_UVERBS_ADVISE_MR_ADVICE_PREFETCH_WRITE,
	IB_UVERBS_ADVISE_MR_ADVICE_PREFETCH_NO_FAULT,
};

enum ib_uverbs_advise_mr_flag {
	IB_UVERBS_ADVISE_MR_FLAG_FLUSH = 1 << 0,
};

struct ib_uverbs_query_port_resp_ex {
	struct ib_uverbs_query_port_resp legacy_resp;
	__u16 port_cap_flags2;
	__u8  reserved[6];
};

struct ib_uverbs_qp_cap {
	__u32 max_send_wr;
	__u32 max_recv_wr;
	__u32 max_send_sge;
	__u32 max_recv_sge;
	__u32 max_inline_data;
};

enum rdma_driver_id {
	RDMA_DRIVER_UNKNOWN,
	RDMA_DRIVER_MLX5,
	RDMA_DRIVER_MLX4,
	RDMA_DRIVER_CXGB3,
	RDMA_DRIVER_CXGB4,
	RDMA_DRIVER_MTHCA,
	RDMA_DRIVER_BNXT_RE,
	RDMA_DRIVER_OCRDMA,
	RDMA_DRIVER_NES,
	RDMA_DRIVER_I40IW,
	RDMA_DRIVER_IRDMA = RDMA_DRIVER_I40IW,
	RDMA_DRIVER_VMW_PVRDMA,
	RDMA_DRIVER_QEDR,
	RDMA_DRIVER_HNS,
	RDMA_DRIVER_USNIC,
	RDMA_DRIVER_RXE,
	RDMA_DRIVER_HFI1,
	RDMA_DRIVER_QIB,
	RDMA_DRIVER_EFA,
	RDMA_DRIVER_SIW,
	RDMA_DRIVER_ERDMA,
	RDMA_DRIVER_MANA,
};

enum ib_uverbs_gid_type {
	IB_UVERBS_GID_TYPE_IB,
	IB_UVERBS_GID_TYPE_ROCE_V1,
	IB_UVERBS_GID_TYPE_ROCE_V2,
};

struct ib_uverbs_gid_entry {
	__aligned_u64 gid[2];
	__u32 gid_index;
	__u32 port_num;
	__u32 gid_type;
	__u32 netdev_ifindex; /* It is 0 if there is no netdev associated with it */
};

#endif
