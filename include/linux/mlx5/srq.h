/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2013-2018, Mellanox Technologies. All rights reserved.
 */

#ifndef MLX5_SRQ_H
#define MLX5_SRQ_H

struct mlx5_core_dev;

void mlx5_init_srq_table(struct mlx5_core_dev *dev);
void mlx5_cleanup_srq_table(struct mlx5_core_dev *dev);

#endif /* MLX5_SRQ_H */
