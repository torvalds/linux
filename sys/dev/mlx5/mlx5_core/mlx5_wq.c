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

#include <dev/mlx5/driver.h>
#include "wq.h"
#include "mlx5_core.h"

u32 mlx5_wq_cyc_get_size(struct mlx5_wq_cyc *wq)
{
	return (u32)wq->sz_m1 + 1;
}

u32 mlx5_cqwq_get_size(struct mlx5_cqwq *wq)
{
	return wq->sz_m1 + 1;
}

u32 mlx5_wq_ll_get_size(struct mlx5_wq_ll *wq)
{
	return (u32)wq->sz_m1 + 1;
}

static u32 mlx5_wq_cyc_get_byte_size(struct mlx5_wq_cyc *wq)
{
	return mlx5_wq_cyc_get_size(wq) << wq->log_stride;
}

static u32 mlx5_cqwq_get_byte_size(struct mlx5_cqwq *wq)
{
	return mlx5_cqwq_get_size(wq) << wq->log_stride;
}

static u32 mlx5_wq_ll_get_byte_size(struct mlx5_wq_ll *wq)
{
	return mlx5_wq_ll_get_size(wq) << wq->log_stride;
}

int mlx5_wq_cyc_create(struct mlx5_core_dev *mdev, struct mlx5_wq_param *param,
		       void *wqc, struct mlx5_wq_cyc *wq,
		       struct mlx5_wq_ctrl *wq_ctrl)
{
	int max_direct = param->linear ? INT_MAX : 0;
	int err;

	wq->log_stride = MLX5_GET(wq, wqc, log_wq_stride);
	wq->sz_m1 = (1 << MLX5_GET(wq, wqc, log_wq_sz)) - 1;

	err = mlx5_db_alloc_node(mdev, &wq_ctrl->db, param->db_numa_node);
	if (err) {
		mlx5_core_warn(mdev, "mlx5_db_alloc() failed, %d\n", err);
		return err;
	}

	err = mlx5_buf_alloc_node(mdev, mlx5_wq_cyc_get_byte_size(wq),
				  max_direct, &wq_ctrl->buf,
				  param->buf_numa_node);
	if (err) {
		mlx5_core_warn(mdev, "mlx5_buf_alloc() failed, %d\n", err);
		goto err_db_free;
	}

	wq->buf = wq_ctrl->buf.direct.buf;
	wq->db  = wq_ctrl->db.db;

	wq_ctrl->mdev = mdev;

	return 0;

err_db_free:
	mlx5_db_free(mdev, &wq_ctrl->db);

	return err;
}

int mlx5_cqwq_create(struct mlx5_core_dev *mdev, struct mlx5_wq_param *param,
		     void *cqc, struct mlx5_cqwq *wq,
		     struct mlx5_wq_ctrl *wq_ctrl)
{
	int max_direct = param->linear ? INT_MAX : 0;
	int err;

	wq->log_stride = 6 + MLX5_GET(cqc, cqc, cqe_sz);
	wq->log_sz = MLX5_GET(cqc, cqc, log_cq_size);
	wq->sz_m1 = (1 << wq->log_sz) - 1;

	err = mlx5_db_alloc_node(mdev, &wq_ctrl->db, param->db_numa_node);
	if (err) {
		mlx5_core_warn(mdev, "mlx5_db_alloc() failed, %d\n", err);
		return err;
	}

	err = mlx5_buf_alloc_node(mdev, mlx5_cqwq_get_byte_size(wq),
				  max_direct, &wq_ctrl->buf,
				  param->buf_numa_node);
	if (err) {
		mlx5_core_warn(mdev, "mlx5_buf_alloc() failed, %d\n", err);
		goto err_db_free;
	}

	wq->buf = wq_ctrl->buf.direct.buf;
	wq->db  = wq_ctrl->db.db;

	wq_ctrl->mdev = mdev;

	return 0;

err_db_free:
	mlx5_db_free(mdev, &wq_ctrl->db);

	return err;
}

int mlx5_wq_ll_create(struct mlx5_core_dev *mdev, struct mlx5_wq_param *param,
		      void *wqc, struct mlx5_wq_ll *wq,
		      struct mlx5_wq_ctrl *wq_ctrl)
{
	struct mlx5_wqe_srq_next_seg *next_seg;
	int max_direct = param->linear ? INT_MAX : 0;
	int err;
	int i;

	wq->log_stride = MLX5_GET(wq, wqc, log_wq_stride);
	wq->sz_m1 = (1 << MLX5_GET(wq, wqc, log_wq_sz)) - 1;

	err = mlx5_db_alloc_node(mdev, &wq_ctrl->db, param->db_numa_node);
	if (err) {
		mlx5_core_warn(mdev, "mlx5_db_alloc() failed, %d\n", err);
		return err;
	}

	err = mlx5_buf_alloc_node(mdev, mlx5_wq_ll_get_byte_size(wq),
				  max_direct, &wq_ctrl->buf,
				  param->buf_numa_node);
	if (err) {
		mlx5_core_warn(mdev, "mlx5_buf_alloc() failed, %d\n", err);
		goto err_db_free;
	}

	wq->buf = wq_ctrl->buf.direct.buf;
	wq->db  = wq_ctrl->db.db;

	for (i = 0; i < wq->sz_m1; i++) {
		next_seg = mlx5_wq_ll_get_wqe(wq, i);
		next_seg->next_wqe_index = cpu_to_be16(i + 1);
	}
	next_seg = mlx5_wq_ll_get_wqe(wq, i);
	wq->tail_next = &next_seg->next_wqe_index;

	wq_ctrl->mdev = mdev;

	return 0;

err_db_free:
	mlx5_db_free(mdev, &wq_ctrl->db);

	return err;
}

void mlx5_wq_destroy(struct mlx5_wq_ctrl *wq_ctrl)
{
	mlx5_buf_free(wq_ctrl->mdev, &wq_ctrl->buf);
	mlx5_db_free(wq_ctrl->mdev, &wq_ctrl->db);
}
