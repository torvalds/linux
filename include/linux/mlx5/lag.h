/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_LAG_API_H__
#define __MLX5_LAG_API_H__

#include <linux/types.h>

struct mlx5_core_dev;
struct mlx5_flow_table;
struct mlx5_flow_table_attr;

int mlx5_lag_demux_init(struct mlx5_core_dev *dev,
			struct mlx5_flow_table_attr *ft_attr);
void mlx5_lag_demux_cleanup(struct mlx5_core_dev *dev);
int mlx5_lag_demux_rule_add(struct mlx5_core_dev *dev, u16 vport_num,
			    int vport_index);
void mlx5_lag_demux_rule_del(struct mlx5_core_dev *dev, int vport_index);
int mlx5_lag_get_dev_seq(struct mlx5_core_dev *dev);

#endif /* __MLX5_LAG_API_H__ */
