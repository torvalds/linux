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

#ifndef __TRANSOBJ_H__
#define __TRANSOBJ_H__

int mlx5_alloc_transport_domain(struct mlx5_core_dev *dev, u32 *tdn);
void mlx5_dealloc_transport_domain(struct mlx5_core_dev *dev, u32 tdn);
int mlx5_core_create_rq(struct mlx5_core_dev *dev, u32 *in, int inlen,
			u32 *rqn);
int mlx5_core_modify_rq(struct mlx5_core_dev *dev, u32 *in, int inlen);
void mlx5_core_destroy_rq(struct mlx5_core_dev *dev, u32 rqn);
int mlx5_core_query_rq(struct mlx5_core_dev *dev, u32 rqn, u32 *out);
int mlx5_core_create_sq(struct mlx5_core_dev *dev, u32 *in, int inlen,
			u32 *sqn);
int mlx5_core_modify_sq(struct mlx5_core_dev *dev, u32 *in, int inlen);
void mlx5_core_destroy_sq(struct mlx5_core_dev *dev, u32 sqn);
int mlx5_core_query_sq(struct mlx5_core_dev *dev, u32 sqn, u32 *out);
int mlx5_core_create_tir(struct mlx5_core_dev *dev, u32 *in, int inlen,
			 u32 *tirn);
void mlx5_core_destroy_tir(struct mlx5_core_dev *dev, u32 tirn);
int mlx5_core_create_tis(struct mlx5_core_dev *dev, u32 *in, int inlen,
			 u32 *tisn);
int mlx5_core_modify_tis(struct mlx5_core_dev *dev, u32 tisn, u32 *in,
			 int inlen);
void mlx5_core_destroy_tis(struct mlx5_core_dev *dev, u32 tisn);
int mlx5_core_create_rmp(struct mlx5_core_dev *dev, u32 *in, int inlen, u32 *rmpn);
int mlx5_core_modify_rmp(struct mlx5_core_dev *dev, u32 *in, int inlen);
int mlx5_core_destroy_rmp(struct mlx5_core_dev *dev, u32 rmpn);
int mlx5_core_query_rmp(struct mlx5_core_dev *dev, u32 rmpn, u32 *out);
int mlx5_core_arm_rmp(struct mlx5_core_dev *dev, u32 rmpn, u16 lwm);
int mlx5_core_create_xsrq(struct mlx5_core_dev *dev, u32 *in, int inlen, u32 *rmpn);
int mlx5_core_destroy_xsrq(struct mlx5_core_dev *dev, u32 rmpn);
int mlx5_core_query_xsrq(struct mlx5_core_dev *dev, u32 rmpn, u32 *out);
int mlx5_core_arm_xsrq(struct mlx5_core_dev *dev, u32 rmpn, u16 lwm);

int mlx5_core_create_rqt(struct mlx5_core_dev *dev, u32 *in, int inlen,
			 u32 *rqtn);
int mlx5_core_modify_rqt(struct mlx5_core_dev *dev, u32 rqtn, u32 *in,
			 int inlen);
void mlx5_core_destroy_rqt(struct mlx5_core_dev *dev, u32 rqtn);

#endif /* __TRANSOBJ_H__ */
