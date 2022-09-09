/*
 * Copyright (c) 2013-2015, Mellanox Technologies, Ltd.  All rights reserved.
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

#ifndef __TRANSOBJ_H__
#define __TRANSOBJ_H__

#include <linux/mlx5/driver.h>

int mlx5_core_alloc_transport_domain(struct mlx5_core_dev *dev, u32 *tdn);
void mlx5_core_dealloc_transport_domain(struct mlx5_core_dev *dev, u32 tdn);
int mlx5_core_create_rq(struct mlx5_core_dev *dev, u32 *in, int inlen,
			u32 *rqn);
int mlx5_core_modify_rq(struct mlx5_core_dev *dev, u32 rqn, u32 *in);
void mlx5_core_destroy_rq(struct mlx5_core_dev *dev, u32 rqn);
int mlx5_core_query_rq(struct mlx5_core_dev *dev, u32 rqn, u32 *out);
int mlx5_core_create_sq(struct mlx5_core_dev *dev, u32 *in, int inlen,
			u32 *sqn);
int mlx5_core_modify_sq(struct mlx5_core_dev *dev, u32 sqn, u32 *in);
void mlx5_core_destroy_sq(struct mlx5_core_dev *dev, u32 sqn);
int mlx5_core_query_sq(struct mlx5_core_dev *dev, u32 sqn, u32 *out);
int mlx5_core_query_sq_state(struct mlx5_core_dev *dev, u32 sqn, u8 *state);
int mlx5_core_create_tir(struct mlx5_core_dev *dev, u32 *in, u32 *tirn);
int mlx5_core_modify_tir(struct mlx5_core_dev *dev, u32 tirn, u32 *in);
void mlx5_core_destroy_tir(struct mlx5_core_dev *dev, u32 tirn);
int mlx5_core_create_tis(struct mlx5_core_dev *dev, u32 *in, u32 *tisn);
int mlx5_core_modify_tis(struct mlx5_core_dev *dev, u32 tisn, u32 *in);
void mlx5_core_destroy_tis(struct mlx5_core_dev *dev, u32 tisn);
int mlx5_core_create_rqt(struct mlx5_core_dev *dev, u32 *in, int inlen,
			 u32 *rqtn);
int mlx5_core_modify_rqt(struct mlx5_core_dev *dev, u32 rqtn, u32 *in,
			 int inlen);
void mlx5_core_destroy_rqt(struct mlx5_core_dev *dev, u32 rqtn);

struct mlx5_hairpin_params {
	u8  log_data_size;
	u8  log_num_packets;
	u16 q_counter;
	int num_channels;
};

struct mlx5_hairpin {
	struct mlx5_core_dev *func_mdev;
	struct mlx5_core_dev *peer_mdev;

	int num_channels;

	u32 *rqn;
	u32 *sqn;

	bool peer_gone;
};

struct mlx5_hairpin *
mlx5_core_hairpin_create(struct mlx5_core_dev *func_mdev,
			 struct mlx5_core_dev *peer_mdev,
			 struct mlx5_hairpin_params *params);

void mlx5_core_hairpin_destroy(struct mlx5_hairpin *pair);
void mlx5_core_hairpin_clear_dead_peer(struct mlx5_hairpin *hp);
#endif /* __TRANSOBJ_H__ */
