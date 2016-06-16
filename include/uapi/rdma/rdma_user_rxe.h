/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
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
 *	- Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *	- Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
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

#ifndef RDMA_USER_RXE_H
#define RDMA_USER_RXE_H

#include <linux/types.h>

union rxe_gid {
	__u8	raw[16];
	struct {
		__be64	subnet_prefix;
		__be64	interface_id;
	} global;
};

struct rxe_global_route {
	union rxe_gid	dgid;
	__u32		flow_label;
	__u8		sgid_index;
	__u8		hop_limit;
	__u8		traffic_class;
};

struct rxe_av {
	__u8			port_num;
	__u8			network_type;
	struct rxe_global_route	grh;
	union {
		struct sockaddr		_sockaddr;
		struct sockaddr_in	_sockaddr_in;
		struct sockaddr_in6	_sockaddr_in6;
	} sgid_addr, dgid_addr;
};

struct rxe_send_wr {
	__u64			wr_id;
	__u32			num_sge;
	__u32			opcode;
	__u32			send_flags;
	union {
		__be32		imm_data;
		__u32		invalidate_rkey;
	} ex;
	union {
		struct {
			__u64	remote_addr;
			__u32	rkey;
		} rdma;
		struct {
			__u64	remote_addr;
			__u64	compare_add;
			__u64	swap;
			__u32	rkey;
		} atomic;
		struct {
			__u32	remote_qpn;
			__u32	remote_qkey;
			__u16	pkey_index;
		} ud;
		struct {
			struct ib_mr *mr;
			__u32        key;
			int          access;
		} reg;
	} wr;
};

struct rxe_sge {
	__u64	addr;
	__u32	length;
	__u32	lkey;
};

struct mminfo {
	__u64			offset;
	__u32			size;
	__u32			pad;
};

struct rxe_dma_info {
	__u32			length;
	__u32			resid;
	__u32			cur_sge;
	__u32			num_sge;
	__u32			sge_offset;
	union {
		__u8		inline_data[0];
		struct rxe_sge	sge[0];
	};
};

struct rxe_send_wqe {
	struct rxe_send_wr	wr;
	struct rxe_av		av;
	__u32			status;
	__u32			state;
	__u64			iova;
	__u32			mask;
	__u32			first_psn;
	__u32			last_psn;
	__u32			ack_length;
	__u32			ssn;
	__u32			has_rd_atomic;
	struct rxe_dma_info	dma;
};

struct rxe_recv_wqe {
	__u64			wr_id;
	__u32			num_sge;
	__u32			padding;
	struct rxe_dma_info	dma;
};

#endif /* RDMA_USER_RXE_H */
