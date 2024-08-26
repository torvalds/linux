/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-2-Clause) */
/*
 * Copyright 2018-2024 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#ifndef EFA_ABI_USER_H
#define EFA_ABI_USER_H

#include <linux/types.h>
#include <rdma/ib_user_ioctl_cmds.h>

/*
 * Increment this value if any changes that break userspace ABI
 * compatibility are made.
 */
#define EFA_UVERBS_ABI_VERSION 1

/*
 * Keep structs aligned to 8 bytes.
 * Keep reserved fields as arrays of __u8 named reserved_XXX where XXX is the
 * hex bit offset of the field.
 */

enum {
	EFA_ALLOC_UCONTEXT_CMD_COMP_TX_BATCH  = 1 << 0,
	EFA_ALLOC_UCONTEXT_CMD_COMP_MIN_SQ_WR = 1 << 1,
};

struct efa_ibv_alloc_ucontext_cmd {
	__u32 comp_mask;
	__u8 reserved_20[4];
};

enum efa_ibv_user_cmds_supp_udata {
	EFA_USER_CMDS_SUPP_UDATA_QUERY_DEVICE = 1 << 0,
	EFA_USER_CMDS_SUPP_UDATA_CREATE_AH    = 1 << 1,
};

struct efa_ibv_alloc_ucontext_resp {
	__u32 comp_mask;
	__u32 cmds_supp_udata_mask;
	__u16 sub_cqs_per_cq;
	__u16 inline_buf_size;
	__u32 max_llq_size; /* bytes */
	__u16 max_tx_batch; /* units of 64 bytes */
	__u16 min_sq_wr;
	__u8 reserved_a0[4];
};

struct efa_ibv_alloc_pd_resp {
	__u32 comp_mask;
	__u16 pdn;
	__u8 reserved_30[2];
};

enum {
	EFA_CREATE_CQ_WITH_COMPLETION_CHANNEL = 1 << 0,
	EFA_CREATE_CQ_WITH_SGID               = 1 << 1,
};

struct efa_ibv_create_cq {
	__u32 comp_mask;
	__u32 cq_entry_size;
	__u16 num_sub_cqs;
	__u8 flags;
	__u8 reserved_58[5];
};

enum {
	EFA_CREATE_CQ_RESP_DB_OFF = 1 << 0,
};

struct efa_ibv_create_cq_resp {
	__u32 comp_mask;
	__u8 reserved_20[4];
	__aligned_u64 q_mmap_key;
	__aligned_u64 q_mmap_size;
	__u16 cq_idx;
	__u8 reserved_d0[2];
	__u32 db_off;
	__aligned_u64 db_mmap_key;
};

enum {
	EFA_QP_DRIVER_TYPE_SRD = 0,
};

enum {
	EFA_CREATE_QP_WITH_UNSOLICITED_WRITE_RECV = 1 << 0,
};

struct efa_ibv_create_qp {
	__u32 comp_mask;
	__u32 rq_ring_size; /* bytes */
	__u32 sq_ring_size; /* bytes */
	__u32 driver_qp_type;
	__u16 flags;
	__u8 reserved_90[6];
};

struct efa_ibv_create_qp_resp {
	__u32 comp_mask;
	/* the offset inside the page of the rq db */
	__u32 rq_db_offset;
	/* the offset inside the page of the sq db */
	__u32 sq_db_offset;
	/* the offset inside the page of descriptors buffer */
	__u32 llq_desc_offset;
	__aligned_u64 rq_mmap_key;
	__aligned_u64 rq_mmap_size;
	__aligned_u64 rq_db_mmap_key;
	__aligned_u64 sq_db_mmap_key;
	__aligned_u64 llq_desc_mmap_key;
	__u16 send_sub_cq_idx;
	__u16 recv_sub_cq_idx;
	__u8 reserved_1e0[4];
};

struct efa_ibv_create_ah_resp {
	__u32 comp_mask;
	__u16 efa_address_handle;
	__u8 reserved_30[2];
};

enum {
	EFA_QUERY_DEVICE_CAPS_RDMA_READ = 1 << 0,
	EFA_QUERY_DEVICE_CAPS_RNR_RETRY = 1 << 1,
	EFA_QUERY_DEVICE_CAPS_CQ_NOTIFICATIONS = 1 << 2,
	EFA_QUERY_DEVICE_CAPS_CQ_WITH_SGID     = 1 << 3,
	EFA_QUERY_DEVICE_CAPS_DATA_POLLING_128 = 1 << 4,
	EFA_QUERY_DEVICE_CAPS_RDMA_WRITE = 1 << 5,
	EFA_QUERY_DEVICE_CAPS_UNSOLICITED_WRITE_RECV = 1 << 6,
};

struct efa_ibv_ex_query_device_resp {
	__u32 comp_mask;
	__u32 max_sq_wr;
	__u32 max_rq_wr;
	__u16 max_sq_sge;
	__u16 max_rq_sge;
	__u32 max_rdma_size;
	__u32 device_caps;
};

enum {
	EFA_QUERY_MR_VALIDITY_RECV_IC_ID = 1 << 0,
	EFA_QUERY_MR_VALIDITY_RDMA_READ_IC_ID = 1 << 1,
	EFA_QUERY_MR_VALIDITY_RDMA_RECV_IC_ID = 1 << 2,
};

enum efa_query_mr_attrs {
	EFA_IB_ATTR_QUERY_MR_HANDLE = (1U << UVERBS_ID_NS_SHIFT),
	EFA_IB_ATTR_QUERY_MR_RESP_IC_ID_VALIDITY,
	EFA_IB_ATTR_QUERY_MR_RESP_RECV_IC_ID,
	EFA_IB_ATTR_QUERY_MR_RESP_RDMA_READ_IC_ID,
	EFA_IB_ATTR_QUERY_MR_RESP_RDMA_RECV_IC_ID,
};

enum efa_mr_methods {
	EFA_IB_METHOD_MR_QUERY = (1U << UVERBS_ID_NS_SHIFT),
};

#endif /* EFA_ABI_USER_H */
