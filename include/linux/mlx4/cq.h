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

#ifndef MLX4_CQ_H
#define MLX4_CQ_H

#include <linux/types.h>

#include <linux/mlx4/device.h>
#include <linux/mlx4/doorbell.h>

struct mlx4_cqe {
	__be32			vlan_my_qpn;
	__be32			immed_rss_invalid;
	__be32			g_mlpath_rqpn;
	__be16			sl_vid;
	__be16			rlid;
	__be16			status;
	u8			ipv6_ext_mask;
	u8			badfcs_enc;
	__be32			byte_cnt;
	__be16			wqe_index;
	__be16			checksum;
	u8			reserved[3];
	u8			owner_sr_opcode;
};

struct mlx4_err_cqe {
	__be32			my_qpn;
	u32			reserved1[5];
	__be16			wqe_index;
	u8			vendor_err_syndrome;
	u8			syndrome;
	u8			reserved2[3];
	u8			owner_sr_opcode;
};

enum {
	MLX4_CQE_VLAN_PRESENT_MASK	= 1 << 29,
	MLX4_CQE_QPN_MASK		= 0xffffff,
};

enum {
	MLX4_CQE_OWNER_MASK	= 0x80,
	MLX4_CQE_IS_SEND_MASK	= 0x40,
	MLX4_CQE_OPCODE_MASK	= 0x1f
};

enum {
	MLX4_CQE_SYNDROME_LOCAL_LENGTH_ERR		= 0x01,
	MLX4_CQE_SYNDROME_LOCAL_QP_OP_ERR		= 0x02,
	MLX4_CQE_SYNDROME_LOCAL_PROT_ERR		= 0x04,
	MLX4_CQE_SYNDROME_WR_FLUSH_ERR			= 0x05,
	MLX4_CQE_SYNDROME_MW_BIND_ERR			= 0x06,
	MLX4_CQE_SYNDROME_BAD_RESP_ERR			= 0x10,
	MLX4_CQE_SYNDROME_LOCAL_ACCESS_ERR		= 0x11,
	MLX4_CQE_SYNDROME_REMOTE_INVAL_REQ_ERR		= 0x12,
	MLX4_CQE_SYNDROME_REMOTE_ACCESS_ERR		= 0x13,
	MLX4_CQE_SYNDROME_REMOTE_OP_ERR			= 0x14,
	MLX4_CQE_SYNDROME_TRANSPORT_RETRY_EXC_ERR	= 0x15,
	MLX4_CQE_SYNDROME_RNR_RETRY_EXC_ERR		= 0x16,
	MLX4_CQE_SYNDROME_REMOTE_ABORTED_ERR		= 0x22,
};

enum {
	MLX4_CQE_STATUS_IPV4		= 1 << 6,
	MLX4_CQE_STATUS_IPV4F		= 1 << 7,
	MLX4_CQE_STATUS_IPV6		= 1 << 8,
	MLX4_CQE_STATUS_IPV4OPT		= 1 << 9,
	MLX4_CQE_STATUS_TCP		= 1 << 10,
	MLX4_CQE_STATUS_UDP		= 1 << 11,
	MLX4_CQE_STATUS_IPOK		= 1 << 12,
};

enum {
	MLX4_CQE_LLC                     = 1,
	MLX4_CQE_SNAP                    = 1 << 1,
	MLX4_CQE_BAD_FCS                 = 1 << 4,
};

static inline void mlx4_cq_arm(struct mlx4_cq *cq, u32 cmd,
			       void __iomem *uar_page,
			       spinlock_t *doorbell_lock)
{
	__be32 doorbell[2];
	u32 sn;
	u32 ci;

	sn = cq->arm_sn & 3;
	ci = cq->cons_index & 0xffffff;

	*cq->arm_db = cpu_to_be32(sn << 28 | cmd | ci);

	/*
	 * Make sure that the doorbell record in host memory is
	 * written before ringing the doorbell via PCI MMIO.
	 */
	wmb();

	doorbell[0] = cpu_to_be32(sn << 28 | cmd | cq->cqn);
	doorbell[1] = cpu_to_be32(ci);

	mlx4_write64(doorbell, uar_page + MLX4_CQ_DOORBELL, doorbell_lock);
}

static inline void mlx4_cq_set_ci(struct mlx4_cq *cq)
{
	*cq->set_ci_db = cpu_to_be32(cq->cons_index & 0xffffff);
}

enum {
	MLX4_CQ_DB_REQ_NOT_SOL		= 1 << 24,
	MLX4_CQ_DB_REQ_NOT		= 2 << 24
};

int mlx4_cq_modify(struct mlx4_dev *dev, struct mlx4_cq *cq,
		   u16 count, u16 period);
int mlx4_cq_resize(struct mlx4_dev *dev, struct mlx4_cq *cq,
		   int entries, struct mlx4_mtt *mtt);

#endif /* MLX4_CQ_H */
