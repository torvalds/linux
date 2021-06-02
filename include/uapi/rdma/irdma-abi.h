/* SPDX-License-Identifier: (GPL-2.0 WITH Linux-syscall-note) OR Linux-OpenIB) */
/*
 * Copyright (c) 2006 - 2021 Intel Corporation.  All rights reserved.
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Cisco Systems.  All rights reserved.
 * Copyright (c) 2005 Open Grid Computing, Inc. All rights reserved.
 */

#ifndef IRDMA_ABI_H
#define IRDMA_ABI_H

#include <linux/types.h>

/* irdma must support legacy GEN_1 i40iw kernel
 * and user-space whose last ABI ver is 5
 */
#define IRDMA_ABI_VER 5

enum irdma_memreg_type {
	IRDMA_MEMREG_TYPE_MEM  = 0,
	IRDMA_MEMREG_TYPE_QP   = 1,
	IRDMA_MEMREG_TYPE_CQ   = 2,
};

struct irdma_alloc_ucontext_req {
	__u32 rsvd32;
	__u8 userspace_ver;
	__u8 rsvd8[3];
};

struct irdma_alloc_ucontext_resp {
	__u32 max_pds;
	__u32 max_qps;
	__u32 wq_size; /* size of the WQs (SQ+RQ) in the mmaped area */
	__u8 kernel_ver;
	__u8 rsvd[3];
	__aligned_u64 feature_flags;
	__aligned_u64 db_mmap_key;
	__u32 max_hw_wq_frags;
	__u32 max_hw_read_sges;
	__u32 max_hw_inline;
	__u32 max_hw_rq_quanta;
	__u32 max_hw_wq_quanta;
	__u32 min_hw_cq_size;
	__u32 max_hw_cq_size;
	__u16 max_hw_sq_chunk;
	__u8 hw_rev;
	__u8 rsvd2;
};

struct irdma_alloc_pd_resp {
	__u32 pd_id;
	__u8 rsvd[4];
};

struct irdma_resize_cq_req {
	__aligned_u64 user_cq_buffer;
};

struct irdma_create_cq_req {
	__aligned_u64 user_cq_buf;
	__aligned_u64 user_shadow_area;
};

struct irdma_create_qp_req {
	__aligned_u64 user_wqe_bufs;
	__aligned_u64 user_compl_ctx;
};

struct irdma_mem_reg_req {
	__u16 reg_type; /* enum irdma_memreg_type */
	__u16 cq_pages;
	__u16 rq_pages;
	__u16 sq_pages;
};

struct irdma_modify_qp_req {
	__u8 sq_flush;
	__u8 rq_flush;
	__u8 rsvd[6];
};

struct irdma_create_cq_resp {
	__u32 cq_id;
	__u32 cq_size;
};

struct irdma_create_qp_resp {
	__u32 qp_id;
	__u32 actual_sq_size;
	__u32 actual_rq_size;
	__u32 irdma_drv_opt;
	__u16 push_idx;
	__u8 lsmm;
	__u8 rsvd;
	__u32 qp_caps;
};

struct irdma_modify_qp_resp {
	__aligned_u64 push_wqe_mmap_key;
	__aligned_u64 push_db_mmap_key;
	__u16 push_offset;
	__u8 push_valid;
	__u8 rsvd[5];
};

struct irdma_create_ah_resp {
	__u32 ah_id;
	__u8 rsvd[4];
};
#endif /* IRDMA_ABI_H */
