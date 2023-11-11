/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
 * Copyright (c) 2021 Mellanox Technologies Ltd.
 */

#ifndef _MLX5_MPFS_
#define _MLX5_MPFS_

struct mlx5_core_dev;

#ifdef CONFIG_MLX5_MPFS
int  mlx5_mpfs_add_mac(struct mlx5_core_dev *dev, u8 *mac);
int  mlx5_mpfs_del_mac(struct mlx5_core_dev *dev, u8 *mac);
#else /* #ifndef CONFIG_MLX5_MPFS */
static inline int  mlx5_mpfs_add_mac(struct mlx5_core_dev *dev, u8 *mac) { return 0; }
static inline int  mlx5_mpfs_del_mac(struct mlx5_core_dev *dev, u8 *mac) { return 0; }
#endif

#endif
