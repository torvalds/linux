/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-2-Clause) */
/*
 * Copyright (c) 2007 Cisco Systems, Inc. All rights reserved.
 * Copyright (c) 2007, 2008 Mellanox Technologies. All rights reserved.
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

#ifndef MLX4_ABI_USER_H
#define MLX4_ABI_USER_H

#include <linux/types.h>

/*
 * Increment this value if any changes that break userspace ABI
 * compatibility are made.
 */

#define MLX4_IB_UVERBS_NO_DEV_CAPS_ABI_VERSION	3
#define MLX4_IB_UVERBS_ABI_VERSION		4

/*
 * Make sure that all structs defined in this file remain laid out so
 * that they pack the same way on 32-bit and 64-bit architectures (to
 * avoid incompatibility between 32-bit userspace and 64-bit kernels).
 * In particular do not use pointer types -- pass pointers in __u64
 * instead.
 */

struct mlx4_ib_alloc_ucontext_resp_v3 {
	__u32	qp_tab_size;
	__u16	bf_reg_size;
	__u16	bf_regs_per_page;
};

enum {
	MLX4_USER_DEV_CAP_LARGE_CQE	= 1L << 0,
};

struct mlx4_ib_alloc_ucontext_resp {
	__u32	dev_caps;
	__u32	qp_tab_size;
	__u16	bf_reg_size;
	__u16	bf_regs_per_page;
	__u32	cqe_size;
};

struct mlx4_ib_alloc_pd_resp {
	__u32	pdn;
	__u32	reserved;
};

struct mlx4_ib_create_cq {
	__aligned_u64 buf_addr;
	__aligned_u64 db_addr;
};

struct mlx4_ib_create_cq_resp {
	__u32	cqn;
	__u32	reserved;
};

struct mlx4_ib_resize_cq {
	__aligned_u64 buf_addr;
};

struct mlx4_ib_create_srq {
	__aligned_u64 buf_addr;
	__aligned_u64 db_addr;
};

struct mlx4_ib_create_srq_resp {
	__u32	srqn;
	__u32	reserved;
};

struct mlx4_ib_create_qp_rss {
	__aligned_u64 rx_hash_fields_mask; /* Use  enum mlx4_ib_rx_hash_fields */
	__u8    rx_hash_function; /* Use enum mlx4_ib_rx_hash_function_flags */
	__u8    reserved[7];
	__u8    rx_hash_key[40];
	__u32   comp_mask;
	__u32   reserved1;
};

struct mlx4_ib_create_qp {
	__aligned_u64 buf_addr;
	__aligned_u64 db_addr;
	__u8	log_sq_bb_count;
	__u8	log_sq_stride;
	__u8	sq_no_prefetch;
	__u8	reserved;
	__u32	inl_recv_sz;
};

struct mlx4_ib_create_wq {
	__aligned_u64 buf_addr;
	__aligned_u64 db_addr;
	__u8	log_range_size;
	__u8	reserved[3];
	__u32   comp_mask;
};

struct mlx4_ib_modify_wq {
	__u32	comp_mask;
	__u32	reserved;
};

struct mlx4_ib_create_rwq_ind_tbl_resp {
	__u32	response_length;
	__u32	reserved;
};

/* RX Hash function flags */
enum mlx4_ib_rx_hash_function_flags {
	MLX4_IB_RX_HASH_FUNC_TOEPLITZ	= 1 << 0,
};

/*
 * RX Hash flags, these flags allows to set which incoming packet's field should
 * participates in RX Hash. Each flag represent certain packet's field,
 * when the flag is set the field that is represented by the flag will
 * participate in RX Hash calculation.
 */
enum mlx4_ib_rx_hash_fields {
	MLX4_IB_RX_HASH_SRC_IPV4	= 1 << 0,
	MLX4_IB_RX_HASH_DST_IPV4	= 1 << 1,
	MLX4_IB_RX_HASH_SRC_IPV6	= 1 << 2,
	MLX4_IB_RX_HASH_DST_IPV6	= 1 << 3,
	MLX4_IB_RX_HASH_SRC_PORT_TCP	= 1 << 4,
	MLX4_IB_RX_HASH_DST_PORT_TCP	= 1 << 5,
	MLX4_IB_RX_HASH_SRC_PORT_UDP	= 1 << 6,
	MLX4_IB_RX_HASH_DST_PORT_UDP	= 1 << 7,
	MLX4_IB_RX_HASH_INNER		= 1ULL << 31,
};

struct mlx4_ib_rss_caps {
	__aligned_u64 rx_hash_fields_mask; /* enum mlx4_ib_rx_hash_fields */
	__u8 rx_hash_function; /* enum mlx4_ib_rx_hash_function_flags */
	__u8 reserved[7];
};

enum query_device_resp_mask {
	MLX4_IB_QUERY_DEV_RESP_MASK_CORE_CLOCK_OFFSET = 1UL << 0,
};

struct mlx4_ib_tso_caps {
	__u32 max_tso; /* Maximum tso payload size in bytes */
	/* Corresponding bit will be set if qp type from
	 * 'enum ib_qp_type' is supported.
	 */
	__u32 supported_qpts;
};

struct mlx4_uverbs_ex_query_device_resp {
	__u32			comp_mask;
	__u32			response_length;
	__aligned_u64		hca_core_clock_offset;
	__u32			max_inl_recv_sz;
	__u32			reserved;
	struct mlx4_ib_rss_caps	rss_caps;
	struct mlx4_ib_tso_caps tso_caps;
};

#endif /* MLX4_ABI_USER_H */
