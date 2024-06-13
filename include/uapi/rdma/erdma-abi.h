/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 * Copyright (c) 2020-2022, Alibaba Group.
 */

#ifndef __ERDMA_USER_H__
#define __ERDMA_USER_H__

#include <linux/types.h>

#define ERDMA_ABI_VERSION       1

struct erdma_ureq_create_cq {
	__aligned_u64 db_record_va;
	__aligned_u64 qbuf_va;
	__u32 qbuf_len;
	__u32 rsvd0;
};

struct erdma_uresp_create_cq {
	__u32 cq_id;
	__u32 num_cqe;
};

struct erdma_ureq_create_qp {
	__aligned_u64 db_record_va;
	__aligned_u64 qbuf_va;
	__u32 qbuf_len;
	__u32 rsvd0;
};

struct erdma_uresp_create_qp {
	__u32 qp_id;
	__u32 num_sqe;
	__u32 num_rqe;
	__u32 rq_offset;
};

struct erdma_uresp_alloc_ctx {
	__u32 dev_id;
	__u32 pad;
	__u32 sdb_type;
	__u32 sdb_offset;
	__aligned_u64 sdb;
	__aligned_u64 rdb;
	__aligned_u64 cdb;
};

#endif
