/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-2-Clause) */
/*
 * Copyright 2018-2020 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#ifndef EFA_ABI_USER_H
#define EFA_ABI_USER_H

#include <linux/types.h>

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

struct efa_ibv_create_cq {
	__u32 comp_mask;
	__u32 cq_entry_size;
	__u16 num_sub_cqs;
	__u8 reserved_50[6];
};

struct efa_ibv_create_cq_resp {
	__u32 comp_mask;
	__u8 reserved_20[4];
	__aligned_u64 q_mmap_key;
	__aligned_u64 q_mmap_size;
	__u16 cq_idx;
	__u8 reserved_d0[6];
};

enum {
	EFA_QP_DRIVER_TYPE_SRD = 0,
};

struct efa_ibv_create_qp {
	__u32 comp_mask;
	__u32 rq_ring_size; /* bytes */
	__u32 sq_ring_size; /* bytes */
	__u32 driver_qp_type;
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

#endif /* EFA_ABI_USER_H */
