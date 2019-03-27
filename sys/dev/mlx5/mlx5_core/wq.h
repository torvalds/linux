/*-
 * Copyright (c) 2013-2017, Mellanox Technologies, Ltd.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS `AS IS' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef __MLX5_WQ_H__
#define __MLX5_WQ_H__

#include <dev/mlx5/mlx5_ifc.h>

struct mlx5_wq_param {
	int		linear;
	int		buf_numa_node;
	int		db_numa_node;
};

struct mlx5_wq_ctrl {
	struct mlx5_core_dev	*mdev;
	struct mlx5_buf		buf;
	struct mlx5_db		db;
};

struct mlx5_frag_wq_ctrl {
	struct mlx5_core_dev	*mdev;
	struct mlx5_frag_buf	frag_buf;
	struct mlx5_db		db;
};

struct mlx5_wq_cyc {
	void			*buf;
	__be32			*db;
	u16			sz_m1;
	u8			log_stride;
};

struct mlx5_wq_qp {
	struct mlx5_wq_cyc	rq;
	struct mlx5_wq_cyc	sq;
};

struct mlx5_cqwq {
	void			*buf;
	__be32			*db;
	u32			sz_m1;
	u32			cc; /* consumer counter */
	u8			log_sz;
	u8			log_stride;
};

struct mlx5_wq_ll {
	void			*buf;
	__be32			*db;
	__be16			*tail_next;
	u16			sz_m1;
	u16			head;
	u16			wqe_ctr;
	u16			cur_sz;
	u8			log_stride;
};

int mlx5_wq_cyc_create(struct mlx5_core_dev *mdev, struct mlx5_wq_param *param,
		       void *wqc, struct mlx5_wq_cyc *wq,
		       struct mlx5_wq_ctrl *wq_ctrl);
u32 mlx5_wq_cyc_get_size(struct mlx5_wq_cyc *wq);

int mlx5_cqwq_create(struct mlx5_core_dev *mdev, struct mlx5_wq_param *param,
		     void *cqc, struct mlx5_cqwq *wq,
		     struct mlx5_wq_ctrl *wq_ctrl);
u32 mlx5_cqwq_get_size(struct mlx5_cqwq *wq);

int mlx5_wq_ll_create(struct mlx5_core_dev *mdev, struct mlx5_wq_param *param,
		      void *wqc, struct mlx5_wq_ll *wq,
		      struct mlx5_wq_ctrl *wq_ctrl);
u32 mlx5_wq_ll_get_size(struct mlx5_wq_ll *wq);

void mlx5_wq_destroy(struct mlx5_wq_ctrl *wq_ctrl);

static inline u16 mlx5_wq_cyc_ctr2ix(struct mlx5_wq_cyc *wq, u16 ctr)
{
	return ctr & wq->sz_m1;
}

static inline void *mlx5_wq_cyc_get_wqe(struct mlx5_wq_cyc *wq, u16 ix)
{
	return wq->buf + (ix << wq->log_stride);
}

static inline int mlx5_wq_cyc_cc_bigger(u16 cc1, u16 cc2)
{
	int equal   = (cc1 == cc2);
	int smaller = 0x8000 & (cc1 - cc2);

	return !equal && !smaller;
}

static inline u32 mlx5_cqwq_get_ci(struct mlx5_cqwq *wq)
{
	return wq->cc & wq->sz_m1;
}

static inline void *mlx5_cqwq_get_wqe(struct mlx5_cqwq *wq, u32 ix)
{
	return wq->buf + (ix << wq->log_stride);
}

static inline u32 mlx5_cqwq_get_wrap_cnt(struct mlx5_cqwq *wq)
{
	return wq->cc >> wq->log_sz;
}

static inline void mlx5_cqwq_pop(struct mlx5_cqwq *wq)
{
	wq->cc++;
}

static inline void mlx5_cqwq_update_db_record(struct mlx5_cqwq *wq)
{
	*wq->db = cpu_to_be32(wq->cc & 0xffffff);
}

static inline int mlx5_wq_ll_is_full(struct mlx5_wq_ll *wq)
{
	return wq->cur_sz == wq->sz_m1;
}

static inline int mlx5_wq_ll_is_empty(struct mlx5_wq_ll *wq)
{
	return !wq->cur_sz;
}

static inline void mlx5_wq_ll_push(struct mlx5_wq_ll *wq, u16 head_next)
{
	wq->head = head_next;
	wq->wqe_ctr++;
	wq->cur_sz++;
}

static inline void mlx5_wq_ll_pop(struct mlx5_wq_ll *wq, __be16 ix,
				  __be16 *next_tail_next)
{
	*wq->tail_next = ix;
	wq->tail_next = next_tail_next;
	wq->cur_sz--;
}
static inline void mlx5_wq_ll_update_db_record(struct mlx5_wq_ll *wq)
{
	*wq->db = cpu_to_be32(wq->wqe_ctr);
}

static inline void *mlx5_wq_ll_get_wqe(struct mlx5_wq_ll *wq, u16 ix)
{
	return wq->buf + (ix << wq->log_stride);
}

#endif /* __MLX5_WQ_H__ */
