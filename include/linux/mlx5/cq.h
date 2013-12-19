/*
 * Copyright (c) 2013, Mellanox Technologies inc.  All rights reserved.
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

#ifndef MLX5_CORE_CQ_H
#define MLX5_CORE_CQ_H

#include <rdma/ib_verbs.h>
#include <linux/mlx5/driver.h>


struct mlx5_core_cq {
	u32			cqn;
	int			cqe_sz;
	__be32		       *set_ci_db;
	__be32		       *arm_db;
	atomic_t		refcount;
	struct completion	free;
	unsigned		vector;
	int			irqn;
	void (*comp)		(struct mlx5_core_cq *);
	void (*event)		(struct mlx5_core_cq *, enum mlx5_event);
	struct mlx5_uar	       *uar;
	u32			cons_index;
	unsigned		arm_sn;
	struct mlx5_rsc_debug	*dbg;
	int			pid;
};


enum {
	MLX5_CQE_SYNDROME_LOCAL_LENGTH_ERR		= 0x01,
	MLX5_CQE_SYNDROME_LOCAL_QP_OP_ERR		= 0x02,
	MLX5_CQE_SYNDROME_LOCAL_PROT_ERR		= 0x04,
	MLX5_CQE_SYNDROME_WR_FLUSH_ERR			= 0x05,
	MLX5_CQE_SYNDROME_MW_BIND_ERR			= 0x06,
	MLX5_CQE_SYNDROME_BAD_RESP_ERR			= 0x10,
	MLX5_CQE_SYNDROME_LOCAL_ACCESS_ERR		= 0x11,
	MLX5_CQE_SYNDROME_REMOTE_INVAL_REQ_ERR		= 0x12,
	MLX5_CQE_SYNDROME_REMOTE_ACCESS_ERR		= 0x13,
	MLX5_CQE_SYNDROME_REMOTE_OP_ERR			= 0x14,
	MLX5_CQE_SYNDROME_TRANSPORT_RETRY_EXC_ERR	= 0x15,
	MLX5_CQE_SYNDROME_RNR_RETRY_EXC_ERR		= 0x16,
	MLX5_CQE_SYNDROME_REMOTE_ABORTED_ERR		= 0x22,
};

enum {
	MLX5_CQE_OWNER_MASK	= 1,
	MLX5_CQE_REQ		= 0,
	MLX5_CQE_RESP_WR_IMM	= 1,
	MLX5_CQE_RESP_SEND	= 2,
	MLX5_CQE_RESP_SEND_IMM	= 3,
	MLX5_CQE_RESP_SEND_INV	= 4,
	MLX5_CQE_RESIZE_CQ	= 0xff, /* TBD */
	MLX5_CQE_REQ_ERR	= 13,
	MLX5_CQE_RESP_ERR	= 14,
};

enum {
	MLX5_CQ_MODIFY_RESEIZE = 0,
	MLX5_CQ_MODIFY_MODER = 1,
	MLX5_CQ_MODIFY_MAPPING = 2,
};

struct mlx5_cq_modify_params {
	int	type;
	union {
		struct {
			u32	page_offset;
			u8	log_cq_size;
		} resize;

		struct {
		} moder;

		struct {
		} mapping;
	} params;
};

enum {
	CQE_SIZE_64 = 0,
	CQE_SIZE_128 = 1,
};

static inline int cqe_sz_to_mlx_sz(u8 size)
{
	return size == 64 ? CQE_SIZE_64 : CQE_SIZE_128;
}

static inline void mlx5_cq_set_ci(struct mlx5_core_cq *cq)
{
	*cq->set_ci_db = cpu_to_be32(cq->cons_index & 0xffffff);
}

enum {
	MLX5_CQ_DB_REQ_NOT_SOL		= 1 << 24,
	MLX5_CQ_DB_REQ_NOT		= 0 << 24
};

static inline void mlx5_cq_arm(struct mlx5_core_cq *cq, u32 cmd,
			       void __iomem *uar_page,
			       spinlock_t *doorbell_lock)
{
	__be32 doorbell[2];
	u32 sn;
	u32 ci;

	sn = cq->arm_sn & 3;
	ci = cq->cons_index & 0xffffff;

	*cq->arm_db = cpu_to_be32(sn << 28 | cmd | ci);

	/* Make sure that the doorbell record in host memory is
	 * written before ringing the doorbell via PCI MMIO.
	 */
	wmb();

	doorbell[0] = cpu_to_be32(sn << 28 | cmd | ci);
	doorbell[1] = cpu_to_be32(cq->cqn);

	mlx5_write64(doorbell, uar_page + MLX5_CQ_DOORBELL, doorbell_lock);
}

int mlx5_init_cq_table(struct mlx5_core_dev *dev);
void mlx5_cleanup_cq_table(struct mlx5_core_dev *dev);
int mlx5_core_create_cq(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq,
			struct mlx5_create_cq_mbox_in *in, int inlen);
int mlx5_core_destroy_cq(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq);
int mlx5_core_query_cq(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq,
		       struct mlx5_query_cq_mbox_out *out);
int mlx5_core_modify_cq(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq,
			int type, struct mlx5_cq_modify_params *params);
int mlx5_debug_cq_add(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq);
void mlx5_debug_cq_remove(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq);

#endif /* MLX5_CORE_CQ_H */
