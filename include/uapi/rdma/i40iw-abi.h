/*
 * Copyright (c) 2006 - 2016 Intel Corporation.  All rights reserved.
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Cisco Systems.  All rights reserved.
 * Copyright (c) 2005 Open Grid Computing, Inc. All rights reserved.
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
 */

#ifndef I40IW_ABI_H
#define I40IW_ABI_H

#include <linux/types.h>

#define I40IW_ABI_VER 5

struct i40iw_alloc_ucontext_req {
	__u32 reserved32;
	__u8 userspace_ver;
	__u8 reserved8[3];
};

struct i40iw_alloc_ucontext_resp {
	__u32 max_pds;		/* maximum pds allowed for this user process */
	__u32 max_qps;		/* maximum qps allowed for this user process */
	__u32 wq_size;		/* size of the WQs (sq+rq) allocated to the mmaped area */
	__u8 kernel_ver;
	__u8 reserved[3];
};

struct i40iw_alloc_pd_resp {
	__u32 pd_id;
	__u8 reserved[4];
};

struct i40iw_create_cq_req {
	__aligned_u64 user_cq_buffer;
	__aligned_u64 user_shadow_area;
};

struct i40iw_create_qp_req {
	__aligned_u64 user_wqe_buffers;
	__aligned_u64 user_compl_ctx;

	/* UDA QP PHB */
	__aligned_u64 user_sq_phb;	/* place for VA of the sq phb buff */
	__aligned_u64 user_rq_phb;	/* place for VA of the rq phb buff */
};

enum i40iw_memreg_type {
	IW_MEMREG_TYPE_MEM = 0x0000,
	IW_MEMREG_TYPE_QP = 0x0001,
	IW_MEMREG_TYPE_CQ = 0x0002,
};

struct i40iw_mem_reg_req {
	__u16 reg_type;		/* Memory, QP or CQ */
	__u16 cq_pages;
	__u16 rq_pages;
	__u16 sq_pages;
};

struct i40iw_create_cq_resp {
	__u32 cq_id;
	__u32 cq_size;
	__u32 mmap_db_index;
	__u32 reserved;
};

struct i40iw_create_qp_resp {
	__u32 qp_id;
	__u32 actual_sq_size;
	__u32 actual_rq_size;
	__u32 i40iw_drv_opt;
	__u16 push_idx;
	__u8  lsmm;
	__u8  rsvd2;
};

#endif
