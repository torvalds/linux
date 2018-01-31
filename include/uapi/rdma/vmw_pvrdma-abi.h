/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-2-Clause) */
/*
 * Copyright (c) 2012-2016 VMware, Inc.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of EITHER the GNU General Public License
 * version 2 as published by the Free Software Foundation or the BSD
 * 2-Clause License. This program is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; WITHOUT EVEN THE IMPLIED
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License version 2 for more details at
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program available in the file COPYING in the main
 * directory of this source tree.
 *
 * The BSD 2-Clause License
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __VMW_PVRDMA_ABI_H__
#define __VMW_PVRDMA_ABI_H__

#include <linux/types.h>

#define PVRDMA_UVERBS_ABI_VERSION	3		/* ABI Version. */
#define PVRDMA_UAR_HANDLE_MASK		0x00FFFFFF	/* Bottom 24 bits. */
#define PVRDMA_UAR_QP_OFFSET		0		/* QP doorbell. */
#define PVRDMA_UAR_QP_SEND		(1 << 30)	/* Send bit. */
#define PVRDMA_UAR_QP_RECV		(1 << 31)	/* Recv bit. */
#define PVRDMA_UAR_CQ_OFFSET		4		/* CQ doorbell. */
#define PVRDMA_UAR_CQ_ARM_SOL		(1 << 29)	/* Arm solicited bit. */
#define PVRDMA_UAR_CQ_ARM		(1 << 30)	/* Arm bit. */
#define PVRDMA_UAR_CQ_POLL		(1 << 31)	/* Poll bit. */
#define PVRDMA_UAR_SRQ_OFFSET		8		/* SRQ doorbell. */
#define PVRDMA_UAR_SRQ_RECV		(1 << 30)	/* Recv bit. */

enum pvrdma_wr_opcode {
	PVRDMA_WR_RDMA_WRITE,
	PVRDMA_WR_RDMA_WRITE_WITH_IMM,
	PVRDMA_WR_SEND,
	PVRDMA_WR_SEND_WITH_IMM,
	PVRDMA_WR_RDMA_READ,
	PVRDMA_WR_ATOMIC_CMP_AND_SWP,
	PVRDMA_WR_ATOMIC_FETCH_AND_ADD,
	PVRDMA_WR_LSO,
	PVRDMA_WR_SEND_WITH_INV,
	PVRDMA_WR_RDMA_READ_WITH_INV,
	PVRDMA_WR_LOCAL_INV,
	PVRDMA_WR_FAST_REG_MR,
	PVRDMA_WR_MASKED_ATOMIC_CMP_AND_SWP,
	PVRDMA_WR_MASKED_ATOMIC_FETCH_AND_ADD,
	PVRDMA_WR_BIND_MW,
	PVRDMA_WR_REG_SIG_MR,
};

enum pvrdma_wc_status {
	PVRDMA_WC_SUCCESS,
	PVRDMA_WC_LOC_LEN_ERR,
	PVRDMA_WC_LOC_QP_OP_ERR,
	PVRDMA_WC_LOC_EEC_OP_ERR,
	PVRDMA_WC_LOC_PROT_ERR,
	PVRDMA_WC_WR_FLUSH_ERR,
	PVRDMA_WC_MW_BIND_ERR,
	PVRDMA_WC_BAD_RESP_ERR,
	PVRDMA_WC_LOC_ACCESS_ERR,
	PVRDMA_WC_REM_INV_REQ_ERR,
	PVRDMA_WC_REM_ACCESS_ERR,
	PVRDMA_WC_REM_OP_ERR,
	PVRDMA_WC_RETRY_EXC_ERR,
	PVRDMA_WC_RNR_RETRY_EXC_ERR,
	PVRDMA_WC_LOC_RDD_VIOL_ERR,
	PVRDMA_WC_REM_INV_RD_REQ_ERR,
	PVRDMA_WC_REM_ABORT_ERR,
	PVRDMA_WC_INV_EECN_ERR,
	PVRDMA_WC_INV_EEC_STATE_ERR,
	PVRDMA_WC_FATAL_ERR,
	PVRDMA_WC_RESP_TIMEOUT_ERR,
	PVRDMA_WC_GENERAL_ERR,
};

enum pvrdma_wc_opcode {
	PVRDMA_WC_SEND,
	PVRDMA_WC_RDMA_WRITE,
	PVRDMA_WC_RDMA_READ,
	PVRDMA_WC_COMP_SWAP,
	PVRDMA_WC_FETCH_ADD,
	PVRDMA_WC_BIND_MW,
	PVRDMA_WC_LSO,
	PVRDMA_WC_LOCAL_INV,
	PVRDMA_WC_FAST_REG_MR,
	PVRDMA_WC_MASKED_COMP_SWAP,
	PVRDMA_WC_MASKED_FETCH_ADD,
	PVRDMA_WC_RECV = 1 << 7,
	PVRDMA_WC_RECV_RDMA_WITH_IMM,
};

enum pvrdma_wc_flags {
	PVRDMA_WC_GRH			= 1 << 0,
	PVRDMA_WC_WITH_IMM		= 1 << 1,
	PVRDMA_WC_WITH_INVALIDATE	= 1 << 2,
	PVRDMA_WC_IP_CSUM_OK		= 1 << 3,
	PVRDMA_WC_WITH_SMAC		= 1 << 4,
	PVRDMA_WC_WITH_VLAN		= 1 << 5,
	PVRDMA_WC_WITH_NETWORK_HDR_TYPE	= 1 << 6,
	PVRDMA_WC_FLAGS_MAX		= PVRDMA_WC_WITH_NETWORK_HDR_TYPE,
};

struct pvrdma_alloc_ucontext_resp {
	__u32 qp_tab_size;
	__u32 reserved;
};

struct pvrdma_alloc_pd_resp {
	__u32 pdn;
	__u32 reserved;
};

struct pvrdma_create_cq {
	__u64 buf_addr;
	__u32 buf_size;
	__u32 reserved;
};

struct pvrdma_create_cq_resp {
	__u32 cqn;
	__u32 reserved;
};

struct pvrdma_resize_cq {
	__u64 buf_addr;
	__u32 buf_size;
	__u32 reserved;
};

struct pvrdma_create_srq {
	__u64 buf_addr;
	__u32 buf_size;
	__u32 reserved;
};

struct pvrdma_create_srq_resp {
	__u32 srqn;
	__u32 reserved;
};

struct pvrdma_create_qp {
	__u64 rbuf_addr;
	__u64 sbuf_addr;
	__u32 rbuf_size;
	__u32 sbuf_size;
	__u64 qp_addr;
};

/* PVRDMA masked atomic compare and swap */
struct pvrdma_ex_cmp_swap {
	__u64 swap_val;
	__u64 compare_val;
	__u64 swap_mask;
	__u64 compare_mask;
};

/* PVRDMA masked atomic fetch and add */
struct pvrdma_ex_fetch_add {
	__u64 add_val;
	__u64 field_boundary;
};

/* PVRDMA address vector. */
struct pvrdma_av {
	__u32 port_pd;
	__u32 sl_tclass_flowlabel;
	__u8 dgid[16];
	__u8 src_path_bits;
	__u8 gid_index;
	__u8 stat_rate;
	__u8 hop_limit;
	__u8 dmac[6];
	__u8 reserved[6];
};

/* PVRDMA scatter/gather entry */
struct pvrdma_sge {
	__u64   addr;
	__u32   length;
	__u32   lkey;
};

/* PVRDMA receive queue work request */
struct pvrdma_rq_wqe_hdr {
	__u64 wr_id;		/* wr id */
	__u32 num_sge;		/* size of s/g array */
	__u32 total_len;	/* reserved */
};
/* Use pvrdma_sge (ib_sge) for receive queue s/g array elements. */

/* PVRDMA send queue work request */
struct pvrdma_sq_wqe_hdr {
	__u64 wr_id;		/* wr id */
	__u32 num_sge;		/* size of s/g array */
	__u32 total_len;	/* reserved */
	__u32 opcode;		/* operation type */
	__u32 send_flags;	/* wr flags */
	union {
		__be32 imm_data;
		__u32 invalidate_rkey;
	} ex;
	__u32 reserved;
	union {
		struct {
			__u64 remote_addr;
			__u32 rkey;
			__u8 reserved[4];
		} rdma;
		struct {
			__u64 remote_addr;
			__u64 compare_add;
			__u64 swap;
			__u32 rkey;
			__u32 reserved;
		} atomic;
		struct {
			__u64 remote_addr;
			__u32 log_arg_sz;
			__u32 rkey;
			union {
				struct pvrdma_ex_cmp_swap  cmp_swap;
				struct pvrdma_ex_fetch_add fetch_add;
			} wr_data;
		} masked_atomics;
		struct {
			__u64 iova_start;
			__u64 pl_pdir_dma;
			__u32 page_shift;
			__u32 page_list_len;
			__u32 length;
			__u32 access_flags;
			__u32 rkey;
		} fast_reg;
		struct {
			__u32 remote_qpn;
			__u32 remote_qkey;
			struct pvrdma_av av;
		} ud;
	} wr;
};
/* Use pvrdma_sge (ib_sge) for send queue s/g array elements. */

/* Completion queue element. */
struct pvrdma_cqe {
	__u64 wr_id;
	__u64 qp;
	__u32 opcode;
	__u32 status;
	__u32 byte_len;
	__be32 imm_data;
	__u32 src_qp;
	__u32 wc_flags;
	__u32 vendor_err;
	__u16 pkey_index;
	__u16 slid;
	__u8 sl;
	__u8 dlid_path_bits;
	__u8 port_num;
	__u8 smac[6];
	__u8 network_hdr_type;
	__u8 reserved2[6]; /* Pad to next power of 2 (64). */
};

#endif /* __VMW_PVRDMA_ABI_H__ */
