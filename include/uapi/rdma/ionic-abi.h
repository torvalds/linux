/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Copyright (C) 2018-2025, Advanced Micro Devices, Inc */

#ifndef IONIC_ABI_H
#define IONIC_ABI_H

#include <linux/types.h>

#define IONIC_ABI_VERSION	1

#define IONIC_EXPDB_64		1
#define IONIC_EXPDB_128		2
#define IONIC_EXPDB_256		4
#define IONIC_EXPDB_512		8

#define IONIC_EXPDB_SQ		1
#define IONIC_EXPDB_RQ		2

#define IONIC_CMB_ENABLE	1
#define IONIC_CMB_REQUIRE	2
#define IONIC_CMB_EXPDB		4
#define IONIC_CMB_WC		8
#define IONIC_CMB_UC		16

struct ionic_ctx_req {
	__u32 rsvd[2];
};

struct ionic_ctx_resp {
	__u32 rsvd;
	__u32 page_shift;

	__aligned_u64 dbell_offset;

	__u16 version;
	__u8 qp_opcodes;
	__u8 admin_opcodes;

	__u8 sq_qtype;
	__u8 rq_qtype;
	__u8 cq_qtype;
	__u8 admin_qtype;

	__u8 max_stride;
	__u8 max_spec;
	__u8 udma_count;
	__u8 expdb_mask;
	__u8 expdb_qtypes;

	__u8 rsvd2[3];
};

struct ionic_qdesc {
	__aligned_u64 addr;
	__u32 size;
	__u16 mask;
	__u8 depth_log2;
	__u8 stride_log2;
};

struct ionic_ah_resp {
	__u32 ahid;
	__u32 pad;
};

struct ionic_cq_req {
	struct ionic_qdesc cq[2];
	__u8 udma_mask;
	__u8 rsvd[7];
};

struct ionic_cq_resp {
	__u32 cqid[2];
	__u8 udma_mask;
	__u8 rsvd[7];
};

struct ionic_qp_req {
	struct ionic_qdesc sq;
	struct ionic_qdesc rq;
	__u8 sq_spec;
	__u8 rq_spec;
	__u8 sq_cmb;
	__u8 rq_cmb;
	__u8 udma_mask;
	__u8 rsvd[3];
};

struct ionic_qp_resp {
	__u32 qpid;
	__u8 sq_cmb;
	__u8 rq_cmb;
	__u8 udma_idx;
	__u8 rsvd[1];
	__aligned_u64 sq_cmb_offset;
	__aligned_u64 rq_cmb_offset;
};

struct ionic_srq_req {
	struct ionic_qdesc rq;
	__u8 rq_spec;
	__u8 rq_cmb;
	__u8 udma_mask;
	__u8 rsvd[5];
};

struct ionic_srq_resp {
	__u32 qpid;
	__u8 rq_cmb;
	__u8 udma_idx;
	__u8 rsvd[2];
	__aligned_u64 rq_cmb_offset;
};

#endif /* IONIC_ABI_H */
