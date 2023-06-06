/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-2-Clause) */
/*
 * Broadcom NetXtreme-E RoCE driver.
 *
 * Copyright (c) 2016 - 2017, Broadcom. All rights reserved.  The term
 * Broadcom refers to Broadcom Limited and/or its subsidiaries.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Description: Uverbs ABI header file
 */

#ifndef __BNXT_RE_UVERBS_ABI_H__
#define __BNXT_RE_UVERBS_ABI_H__

#include <linux/types.h>

#define BNXT_RE_ABI_VERSION	1

#define BNXT_RE_CHIP_ID0_CHIP_NUM_SFT		0x00
#define BNXT_RE_CHIP_ID0_CHIP_REV_SFT		0x10
#define BNXT_RE_CHIP_ID0_CHIP_MET_SFT		0x18

enum {
	BNXT_RE_UCNTX_CMASK_HAVE_CCTX = 0x1ULL,
	BNXT_RE_UCNTX_CMASK_HAVE_MODE = 0x02ULL,
};

enum bnxt_re_wqe_mode {
	BNXT_QPLIB_WQE_MODE_STATIC	= 0x00,
	BNXT_QPLIB_WQE_MODE_VARIABLE	= 0x01,
	BNXT_QPLIB_WQE_MODE_INVALID	= 0x02,
};

struct bnxt_re_uctx_resp {
	__u32 dev_id;
	__u32 max_qp;
	__u32 pg_size;
	__u32 cqe_sz;
	__u32 max_cqd;
	__u32 rsvd;
	__aligned_u64 comp_mask;
	__u32 chip_id0;
	__u32 chip_id1;
	__u32 mode;
	__u32 rsvd1; /* padding */
};

/*
 * This struct is placed after the ib_uverbs_alloc_pd_resp struct, which is
 * not 8 byted aligned. To avoid undesired padding in various cases we have to
 * set this struct to packed.
 */
struct bnxt_re_pd_resp {
	__u32 pdid;
	__u32 dpi;
	__u64 dbr;
} __attribute__((packed, aligned(4)));

struct bnxt_re_cq_req {
	__aligned_u64 cq_va;
	__aligned_u64 cq_handle;
};

struct bnxt_re_cq_resp {
	__u32 cqid;
	__u32 tail;
	__u32 phase;
	__u32 rsvd;
};

struct bnxt_re_resize_cq_req {
	__aligned_u64 cq_va;
};

struct bnxt_re_qp_req {
	__aligned_u64 qpsva;
	__aligned_u64 qprva;
	__aligned_u64 qp_handle;
};

struct bnxt_re_qp_resp {
	__u32 qpid;
	__u32 rsvd;
};

struct bnxt_re_srq_req {
	__aligned_u64 srqva;
	__aligned_u64 srq_handle;
};

struct bnxt_re_srq_resp {
	__u32 srqid;
};

enum bnxt_re_shpg_offt {
	BNXT_RE_BEG_RESV_OFFT	= 0x00,
	BNXT_RE_AVID_OFFT	= 0x10,
	BNXT_RE_AVID_SIZE	= 0x04,
	BNXT_RE_END_RESV_OFFT	= 0xFF0
};

#endif /* __BNXT_RE_UVERBS_ABI_H__*/
