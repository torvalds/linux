/*
 * Copyright (c) 2007 Cisco Systems, Inc.  All rights reserved.
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

#ifndef MLX4_QP_H
#define MLX4_QP_H

#include <linux/types.h>

#include <linux/mlx4/device.h>

#define MLX4_INVALID_LKEY	0x100

enum mlx4_qp_optpar {
	MLX4_QP_OPTPAR_ALT_ADDR_PATH		= 1 << 0,
	MLX4_QP_OPTPAR_RRE			= 1 << 1,
	MLX4_QP_OPTPAR_RAE			= 1 << 2,
	MLX4_QP_OPTPAR_RWE			= 1 << 3,
	MLX4_QP_OPTPAR_PKEY_INDEX		= 1 << 4,
	MLX4_QP_OPTPAR_Q_KEY			= 1 << 5,
	MLX4_QP_OPTPAR_RNR_TIMEOUT		= 1 << 6,
	MLX4_QP_OPTPAR_PRIMARY_ADDR_PATH	= 1 << 7,
	MLX4_QP_OPTPAR_SRA_MAX			= 1 << 8,
	MLX4_QP_OPTPAR_RRA_MAX			= 1 << 9,
	MLX4_QP_OPTPAR_PM_STATE			= 1 << 10,
	MLX4_QP_OPTPAR_RETRY_COUNT		= 1 << 12,
	MLX4_QP_OPTPAR_RNR_RETRY		= 1 << 13,
	MLX4_QP_OPTPAR_ACK_TIMEOUT		= 1 << 14,
	MLX4_QP_OPTPAR_SCHED_QUEUE		= 1 << 16
};

enum mlx4_qp_state {
	MLX4_QP_STATE_RST			= 0,
	MLX4_QP_STATE_INIT			= 1,
	MLX4_QP_STATE_RTR			= 2,
	MLX4_QP_STATE_RTS			= 3,
	MLX4_QP_STATE_SQER			= 4,
	MLX4_QP_STATE_SQD			= 5,
	MLX4_QP_STATE_ERR			= 6,
	MLX4_QP_STATE_SQ_DRAINING		= 7,
	MLX4_QP_NUM_STATE
};

enum {
	MLX4_QP_ST_RC				= 0x0,
	MLX4_QP_ST_UC				= 0x1,
	MLX4_QP_ST_RD				= 0x2,
	MLX4_QP_ST_UD				= 0x3,
	MLX4_QP_ST_MLX				= 0x7
};

enum {
	MLX4_QP_PM_MIGRATED			= 0x3,
	MLX4_QP_PM_ARMED			= 0x0,
	MLX4_QP_PM_REARM			= 0x1
};

enum {
	/* params1 */
	MLX4_QP_BIT_SRE				= 1 << 15,
	MLX4_QP_BIT_SWE				= 1 << 14,
	MLX4_QP_BIT_SAE				= 1 << 13,
	/* params2 */
	MLX4_QP_BIT_RRE				= 1 << 15,
	MLX4_QP_BIT_RWE				= 1 << 14,
	MLX4_QP_BIT_RAE				= 1 << 13,
	MLX4_QP_BIT_RIC				= 1 <<	4,
};

struct mlx4_qp_path {
	u8			fl;
	u8			reserved1[2];
	u8			pkey_index;
	u8			reserved2;
	u8			grh_mylmc;
	__be16			rlid;
	u8			ackto;
	u8			mgid_index;
	u8			static_rate;
	u8			hop_limit;
	__be32			tclass_flowlabel;
	u8			rgid[16];
	u8			sched_queue;
	u8			snooper_flags;
	u8			reserved3[2];
	u8			counter_index;
	u8			reserved4[7];
};

struct mlx4_qp_context {
	__be32			flags;
	__be32			pd;
	u8			mtu_msgmax;
	u8			rq_size_stride;
	u8			sq_size_stride;
	u8			rlkey;
	__be32			usr_page;
	__be32			local_qpn;
	__be32			remote_qpn;
	struct			mlx4_qp_path pri_path;
	struct			mlx4_qp_path alt_path;
	__be32			params1;
	u32			reserved1;
	__be32			next_send_psn;
	__be32			cqn_send;
	u32			reserved2[2];
	__be32			last_acked_psn;
	__be32			ssn;
	__be32			params2;
	__be32			rnr_nextrecvpsn;
	__be32			srcd;
	__be32			cqn_recv;
	__be64			db_rec_addr;
	__be32			qkey;
	__be32			srqn;
	__be32			msn;
	__be16			rq_wqe_counter;
	__be16			sq_wqe_counter;
	u32			reserved3[2];
	__be32			param3;
	__be32			nummmcpeers_basemkey;
	u8			log_page_size;
	u8			reserved4[2];
	u8			mtt_base_addr_h;
	__be32			mtt_base_addr_l;
	u32			reserved5[10];
};

/* Which firmware version adds support for NEC (NoErrorCompletion) bit */
#define MLX4_FW_VER_WQE_CTRL_NEC mlx4_fw_ver(2, 2, 232)

enum {
	MLX4_WQE_CTRL_NEC	= 1 << 29,
	MLX4_WQE_CTRL_FENCE	= 1 << 6,
	MLX4_WQE_CTRL_CQ_UPDATE	= 3 << 2,
	MLX4_WQE_CTRL_SOLICITED	= 1 << 1,
};

struct mlx4_wqe_ctrl_seg {
	__be32			owner_opcode;
	u8			reserved2[3];
	u8			fence_size;
	/*
	 * High 24 bits are SRC remote buffer; low 8 bits are flags:
	 * [7]   SO (strong ordering)
	 * [5]   TCP/UDP checksum
	 * [4]   IP checksum
	 * [3:2] C (generate completion queue entry)
	 * [1]   SE (solicited event)
	 */
	__be32			srcrb_flags;
	/*
	 * imm is immediate data for send/RDMA write w/ immediate;
	 * also invalidation key for send with invalidate; input
	 * modifier for WQEs on CCQs.
	 */
	__be32			imm;
};

enum {
	MLX4_WQE_MLX_VL15	= 1 << 17,
	MLX4_WQE_MLX_SLR	= 1 << 16
};

struct mlx4_wqe_mlx_seg {
	u8			owner;
	u8			reserved1[2];
	u8			opcode;
	u8			reserved2[3];
	u8			size;
	/*
	 * [17]    VL15
	 * [16]    SLR
	 * [15:12] static rate
	 * [11:8]  SL
	 * [4]     ICRC
	 * [3:2]   C
	 * [0]     FL (force loopback)
	 */
	__be32			flags;
	__be16			rlid;
	u16			reserved3;
};

struct mlx4_wqe_datagram_seg {
	__be32			av[8];
	__be32			dqpn;
	__be32			qkey;
	__be32			reservd[2];
};

struct mlx4_wqe_bind_seg {
	__be32			flags1;
	__be32			flags2;
	__be32			new_rkey;
	__be32			lkey;
	__be64			addr;
	__be64			length;
};

struct mlx4_wqe_fmr_seg {
	__be32			flags;
	__be32			mem_key;
	__be64			buf_list;
	__be64			start_addr;
	__be64			reg_len;
	__be32			offset;
	__be32			page_size;
	u32			reserved[2];
};

struct mlx4_wqe_fmr_ext_seg {
	u8			flags;
	u8			reserved;
	__be16			app_mask;
	__be16			wire_app_tag;
	__be16			mem_app_tag;
	__be32			wire_ref_tag_base;
	__be32			mem_ref_tag_base;
};

struct mlx4_wqe_local_inval_seg {
	u8			flags;
	u8			reserved1[3];
	__be32			mem_key;
	u8			reserved2[3];
	u8			guest_id;
	__be64			pa;
};

struct mlx4_wqe_raddr_seg {
	__be64			raddr;
	__be32			rkey;
	u32			reserved;
};

struct mlx4_wqe_atomic_seg {
	__be64			swap_add;
	__be64			compare;
};

struct mlx4_wqe_data_seg {
	__be32			byte_count;
	__be32			lkey;
	__be64			addr;
};

enum {
	MLX4_INLINE_ALIGN	= 64,
};

struct mlx4_wqe_inline_seg {
	__be32			byte_count;
};

int mlx4_qp_modify(struct mlx4_dev *dev, struct mlx4_mtt *mtt,
		   enum mlx4_qp_state cur_state, enum mlx4_qp_state new_state,
		   struct mlx4_qp_context *context, enum mlx4_qp_optpar optpar,
		   int sqd_event, struct mlx4_qp *qp);

int mlx4_qp_query(struct mlx4_dev *dev, struct mlx4_qp *qp,
		  struct mlx4_qp_context *context);

static inline struct mlx4_qp *__mlx4_qp_lookup(struct mlx4_dev *dev, u32 qpn)
{
	return radix_tree_lookup(&dev->qp_table_tree, qpn & (dev->caps.num_qps - 1));
}

void mlx4_qp_remove(struct mlx4_dev *dev, struct mlx4_qp *qp);

#endif /* MLX4_QP_H */
