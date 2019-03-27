/*-
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0
 *
 * Copyright (c) 2013-2015, Mellanox Technologies. All rights reserved.
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
 * $FreeBSD$
 */

#ifndef MLX5_ABI_USER_H
#define MLX5_ABI_USER_H

#ifdef _KERNEL
#include <linux/types.h>
#else
#include <infiniband/types.h>
#endif

enum {
	MLX5_QP_FLAG_SIGNATURE		= 1 << 0,
	MLX5_QP_FLAG_SCATTER_CQE	= 1 << 1,
};

enum {
	MLX5_SRQ_FLAG_SIGNATURE		= 1 << 0,
};

enum {
	MLX5_WQ_FLAG_SIGNATURE		= 1 << 0,
};

/* Increment this value if any changes that break userspace ABI
 * compatibility are made.
 */
#define MLX5_IB_UVERBS_ABI_VERSION	1

/* Make sure that all structs defined in this file remain laid out so
 * that they pack the same way on 32-bit and 64-bit architectures (to
 * avoid incompatibility between 32-bit userspace and 64-bit kernels).
 * In particular do not use pointer types -- pass pointers in __u64
 * instead.
 */

struct mlx5_ib_alloc_ucontext_req {
	__u32	total_num_uuars;
	__u32	num_low_latency_uuars;
};

struct mlx5_ib_alloc_ucontext_req_v2 {
	__u32	total_num_uuars;
	__u32	num_low_latency_uuars;
	__u32	flags;
	__u32	comp_mask;
	__u8	max_cqe_version;
	__u8	reserved0;
	__u16	reserved1;
	__u32	reserved2;
};

enum mlx5_ib_alloc_ucontext_resp_mask {
	MLX5_IB_ALLOC_UCONTEXT_RESP_MASK_CORE_CLOCK_OFFSET = 1UL << 0,
};

enum mlx5_user_cmds_supp_uhw {
	MLX5_USER_CMDS_SUPP_UHW_QUERY_DEVICE = 1 << 0,
	MLX5_USER_CMDS_SUPP_UHW_CREATE_AH    = 1 << 1,
};

struct mlx5_ib_alloc_ucontext_resp {
	__u32	qp_tab_size;
	__u32	bf_reg_size;
	__u32	tot_uuars;
	__u32	cache_line_size;
	__u16	max_sq_desc_sz;
	__u16	max_rq_desc_sz;
	__u32	max_send_wqebb;
	__u32	max_recv_wr;
	__u32	max_srq_recv_wr;
	__u16	num_ports;
	__u16	reserved1;
	__u32	comp_mask;
	__u32	response_length;
	__u8	cqe_version;
	__u8	cmds_supp_uhw;
	__u16	reserved2;
	__u64	hca_core_clock_offset;
};

struct mlx5_ib_alloc_pd_resp {
	__u32	pdn;
};

struct mlx5_ib_tso_caps {
	__u32 max_tso; /* Maximum tso payload size in bytes */

	/* Corresponding bit will be set if qp type from
	 * 'enum ib_qp_type' is supported, e.g.
	 * supported_qpts |= 1 << IB_QPT_UD
	 */
	__u32 supported_qpts;
};

struct mlx5_ib_rss_caps {
	__u64 rx_hash_fields_mask; /* enum mlx5_rx_hash_fields */
	__u8 rx_hash_function; /* enum mlx5_rx_hash_function_flags */
	__u8 reserved[7];
};

struct mlx5_ib_query_device_resp {
	__u32	comp_mask;
	__u32	response_length;
	struct	mlx5_ib_tso_caps tso_caps;
	struct	mlx5_ib_rss_caps rss_caps;
};

struct mlx5_ib_create_cq {
	__u64	buf_addr;
	__u64	db_addr;
	__u32	cqe_size;
	__u32	reserved; /* explicit padding (optional on i386) */
};

struct mlx5_ib_create_cq_resp {
	__u32	cqn;
	__u32	reserved;
};

struct mlx5_ib_resize_cq {
	__u64	buf_addr;
	__u16	cqe_size;
	__u16	reserved0;
	__u32	reserved1;
};

struct mlx5_ib_create_srq {
	__u64	buf_addr;
	__u64	db_addr;
	__u32	flags;
	__u32	reserved0; /* explicit padding (optional on i386) */
	__u32	uidx;
	__u32	reserved1;
};

struct mlx5_ib_create_srq_resp {
	__u32	srqn;
	__u32	reserved;
};

struct mlx5_ib_create_qp {
	__u64	buf_addr;
	__u64	db_addr;
	__u32	sq_wqe_count;
	__u32	rq_wqe_count;
	__u32	rq_wqe_shift;
	__u32	flags;
	__u32	uidx;
	__u32	reserved0;
	__u64	sq_buf_addr;
};

/* RX Hash function flags */
enum mlx5_rx_hash_function_flags {
	MLX5_RX_HASH_FUNC_TOEPLITZ	= 1 << 0,
};

/*
 * RX Hash flags, these flags allows to set which incoming packet's field should
 * participates in RX Hash. Each flag represent certain packet's field,
 * when the flag is set the field that is represented by the flag will
 * participate in RX Hash calculation.
 * Note: *IPV4 and *IPV6 flags can't be enabled together on the same QP
 * and *TCP and *UDP flags can't be enabled together on the same QP.
*/
enum mlx5_rx_hash_fields {
	MLX5_RX_HASH_SRC_IPV4	= 1 << 0,
	MLX5_RX_HASH_DST_IPV4	= 1 << 1,
	MLX5_RX_HASH_SRC_IPV6	= 1 << 2,
	MLX5_RX_HASH_DST_IPV6	= 1 << 3,
	MLX5_RX_HASH_SRC_PORT_TCP	= 1 << 4,
	MLX5_RX_HASH_DST_PORT_TCP	= 1 << 5,
	MLX5_RX_HASH_SRC_PORT_UDP	= 1 << 6,
	MLX5_RX_HASH_DST_PORT_UDP	= 1 << 7
};

struct mlx5_ib_create_qp_rss {
	__u64 rx_hash_fields_mask; /* enum mlx5_rx_hash_fields */
	__u8 rx_hash_function; /* enum mlx5_rx_hash_function_flags */
	__u8 rx_key_len; /* valid only for Toeplitz */
	__u8 reserved[6];
	__u8 rx_hash_key[128]; /* valid only for Toeplitz */
	__u32   comp_mask;
	__u32   reserved1;
};

struct mlx5_ib_create_qp_resp {
	__u32	uuar_index;
};

struct mlx5_ib_alloc_mw {
	__u32	comp_mask;
	__u8	num_klms;
	__u8	reserved1;
	__u16	reserved2;
};

struct mlx5_ib_create_wq {
	__u64   buf_addr;
	__u64   db_addr;
	__u32   rq_wqe_count;
	__u32   rq_wqe_shift;
	__u32   user_index;
	__u32   flags;
	__u32   comp_mask;
	__u32   reserved;
};

struct mlx5_ib_create_ah_resp {
	__u32	response_length;
	__u8	dmac[ETH_ALEN];
	__u8	reserved[6];
};

struct mlx5_ib_create_wq_resp {
	__u32	response_length;
	__u32	reserved;
};

struct mlx5_ib_create_rwq_ind_tbl_resp {
	__u32	response_length;
	__u32	reserved;
};

struct mlx5_ib_modify_wq {
	__u32	comp_mask;
	__u32	reserved;
};
#endif /* MLX5_ABI_USER_H */
