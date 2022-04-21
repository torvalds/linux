/*
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
 */

#ifndef MLX5_CORE_CQ_H
#define MLX5_CORE_CQ_H

#include <linux/mlx5/driver.h>
#include <linux/refcount.h>

struct mlx5_core_cq {
	u32			cqn;
	int			cqe_sz;
	__be32		       *set_ci_db;
	__be32		       *arm_db;
	struct mlx5_uars_page  *uar;
	refcount_t		refcount;
	struct completion	free;
	unsigned		vector;
	unsigned int		irqn;
	void (*comp)(struct mlx5_core_cq *cq, struct mlx5_eqe *eqe);
	void (*event)		(struct mlx5_core_cq *, enum mlx5_event);
	u32			cons_index;
	unsigned		arm_sn;
	struct mlx5_rsc_debug	*dbg;
	int			pid;
	struct {
		struct list_head list;
		void (*comp)(struct mlx5_core_cq *cq, struct mlx5_eqe *eqe);
		void		*priv;
	} tasklet_ctx;
	int			reset_notify_added;
	struct list_head	reset_notify;
	struct mlx5_eq_comp	*eq;
	u16 uid;
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
	MLX5_CQE_RESIZE_CQ	= 5,
	MLX5_CQE_SIG_ERR	= 12,
	MLX5_CQE_REQ_ERR	= 13,
	MLX5_CQE_RESP_ERR	= 14,
	MLX5_CQE_INVALID	= 15,
};

enum {
	MLX5_CQ_MODIFY_PERIOD	= 1 << 0,
	MLX5_CQ_MODIFY_COUNT	= 1 << 1,
	MLX5_CQ_MODIFY_OVERRUN	= 1 << 2,
};

enum {
	MLX5_CQ_OPMOD_RESIZE		= 1,
	MLX5_MODIFY_CQ_MASK_LOG_SIZE	= 1 << 0,
	MLX5_MODIFY_CQ_MASK_PG_OFFSET	= 1 << 1,
	MLX5_MODIFY_CQ_MASK_PG_SIZE	= 1 << 2,
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
	CQE_STRIDE_64 = 0,
	CQE_STRIDE_128 = 1,
	CQE_STRIDE_128_PAD = 2,
};

#define MLX5_MAX_CQ_PERIOD (BIT(__mlx5_bit_sz(cqc, cq_period)) - 1)
#define MLX5_MAX_CQ_COUNT (BIT(__mlx5_bit_sz(cqc, cq_max_count)) - 1)

static inline int cqe_sz_to_mlx_sz(u8 size, int padding_128_en)
{
	return padding_128_en ? CQE_STRIDE_128_PAD :
				size == 64 ? CQE_STRIDE_64 : CQE_STRIDE_128;
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
			       u32 cons_index)
{
	__be32 doorbell[2];
	u32 sn;
	u32 ci;

	sn = cq->arm_sn & 3;
	ci = cons_index & 0xffffff;

	*cq->arm_db = cpu_to_be32(sn << 28 | cmd | ci);

	/* Make sure that the doorbell record in host memory is
	 * written before ringing the doorbell via PCI MMIO.
	 */
	wmb();

	doorbell[0] = cpu_to_be32(sn << 28 | cmd | ci);
	doorbell[1] = cpu_to_be32(cq->cqn);

	mlx5_write64(doorbell, uar_page + MLX5_CQ_DOORBELL);
}

static inline void mlx5_cq_hold(struct mlx5_core_cq *cq)
{
	refcount_inc(&cq->refcount);
}

static inline void mlx5_cq_put(struct mlx5_core_cq *cq)
{
	if (refcount_dec_and_test(&cq->refcount))
		complete(&cq->free);
}

int mlx5_create_cq(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq,
		   u32 *in, int inlen, u32 *out, int outlen);
int mlx5_core_create_cq(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq,
			u32 *in, int inlen, u32 *out, int outlen);
int mlx5_core_destroy_cq(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq);
int mlx5_core_query_cq(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq,
		       u32 *out);
int mlx5_core_modify_cq(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq,
			u32 *in, int inlen);
int mlx5_core_modify_cq_moderation(struct mlx5_core_dev *dev,
				   struct mlx5_core_cq *cq, u16 cq_period,
				   u16 cq_max_count);
static inline void mlx5_dump_err_cqe(struct mlx5_core_dev *dev,
				     struct mlx5_err_cqe *err_cqe)
{
	print_hex_dump(KERN_WARNING, "", DUMP_PREFIX_OFFSET, 16, 1, err_cqe,
		       sizeof(*err_cqe), false);
}
int mlx5_debug_cq_add(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq);
void mlx5_debug_cq_remove(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq);

#endif /* MLX5_CORE_CQ_H */
