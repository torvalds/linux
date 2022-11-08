/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR Linux-OpenIB) */
/*
 * Copyright (c) 2016 Hisilicon Limited.
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

#ifndef HNS_ABI_USER_H
#define HNS_ABI_USER_H

#include <linux/types.h>

struct hns_roce_ib_create_cq {
	__aligned_u64 buf_addr;
	__aligned_u64 db_addr;
	__u32 cqe_size;
	__u32 reserved;
};

enum hns_roce_cq_cap_flags {
	HNS_ROCE_CQ_FLAG_RECORD_DB = 1 << 0,
};

struct hns_roce_ib_create_cq_resp {
	__aligned_u64 cqn; /* Only 32 bits used, 64 for compat */
	__aligned_u64 cap_flags;
};

struct hns_roce_ib_create_srq {
	__aligned_u64 buf_addr;
	__aligned_u64 db_addr;
	__aligned_u64 que_addr;
};

struct hns_roce_ib_create_srq_resp {
	__u32	srqn;
	__u32	reserved;
};

struct hns_roce_ib_create_qp {
	__aligned_u64 buf_addr;
	__aligned_u64 db_addr;
	__u8    log_sq_bb_count;
	__u8    log_sq_stride;
	__u8    sq_no_prefetch;
	__u8    reserved[5];
	__aligned_u64 sdb_addr;
};

enum hns_roce_qp_cap_flags {
	HNS_ROCE_QP_CAP_RQ_RECORD_DB = 1 << 0,
	HNS_ROCE_QP_CAP_SQ_RECORD_DB = 1 << 1,
	HNS_ROCE_QP_CAP_OWNER_DB = 1 << 2,
	HNS_ROCE_QP_CAP_DIRECT_WQE = 1 << 5,
};

struct hns_roce_ib_create_qp_resp {
	__aligned_u64 cap_flags;
	__aligned_u64 dwqe_mmap_key;
};

enum {
	HNS_ROCE_EXSGE_FLAGS = 1 << 0,
};

enum {
	HNS_ROCE_RSP_EXSGE_FLAGS = 1 << 0,
};

struct hns_roce_ib_alloc_ucontext_resp {
	__u32	qp_tab_size;
	__u32	cqe_size;
	__u32	srq_tab_size;
	__u32	reserved;
	__u32	config;
	__u32	max_inline_data;
};

struct hns_roce_ib_alloc_ucontext {
	__u32 config;
	__u32 reserved;
};

struct hns_roce_ib_alloc_pd_resp {
	__u32 pdn;
};

#endif /* HNS_ABI_USER_H */
