/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-2-Clause) */
/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005, 2006 Cisco Systems.  All rights reserved.
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

#ifndef MTHCA_ABI_USER_H
#define MTHCA_ABI_USER_H

#include <linux/types.h>

/*
 * Increment this value if any changes that break userspace ABI
 * compatibility are made.
 */
#define MTHCA_UVERBS_ABI_VERSION	1

/*
 * Make sure that all structs defined in this file remain laid out so
 * that they pack the same way on 32-bit and 64-bit architectures (to
 * avoid incompatibility between 32-bit userspace and 64-bit kernels).
 * In particular do not use pointer types -- pass pointers in __u64
 * instead.
 */
struct mthca_alloc_ucontext_resp {
	__u32 qp_tab_size;
	__u32 uarc_size;
};

struct mthca_alloc_pd_resp {
	__u32 pdn;
	__u32 reserved;
};

/*
 * Mark the memory region with a DMA attribute that causes
 * in-flight DMA to be flushed when the region is written to:
 */
#define MTHCA_MR_DMASYNC	0x1

struct mthca_reg_mr {
	__u32 mr_attrs;
	__u32 reserved;
};

struct mthca_create_cq {
	__u32 lkey;
	__u32 pdn;
	__aligned_u64 arm_db_page;
	__aligned_u64 set_db_page;
	__u32 arm_db_index;
	__u32 set_db_index;
};

struct mthca_create_cq_resp {
	__u32 cqn;
	__u32 reserved;
};

struct mthca_resize_cq {
	__u32 lkey;
	__u32 reserved;
};

struct mthca_create_srq {
	__u32 lkey;
	__u32 db_index;
	__aligned_u64 db_page;
};

struct mthca_create_srq_resp {
	__u32 srqn;
	__u32 reserved;
};

struct mthca_create_qp {
	__u32 lkey;
	__u32 reserved;
	__aligned_u64 sq_db_page;
	__aligned_u64 rq_db_page;
	__u32 sq_db_index;
	__u32 rq_db_index;
};
#endif /* MTHCA_ABI_USER_H */
