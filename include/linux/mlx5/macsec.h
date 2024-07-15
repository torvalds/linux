/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. */

#ifndef MLX5_MACSEC_H
#define MLX5_MACSEC_H

#ifdef CONFIG_MLX5_MACSEC
struct mlx5_macsec_event_data {
	struct mlx5_macsec_fs *macsec_fs;
	void *macdev;
	u32 fs_id;
	bool is_tx;
};

int mlx5_macsec_add_roce_rule(void *macdev, const struct sockaddr *addr, u16 gid_idx,
			      struct list_head *tx_rules_list, struct list_head *rx_rules_list,
			      struct mlx5_macsec_fs *macsec_fs);

void mlx5_macsec_del_roce_rule(u16 gid_idx, struct mlx5_macsec_fs *macsec_fs,
			       struct list_head *tx_rules_list, struct list_head *rx_rules_list);

void mlx5_macsec_add_roce_sa_rules(u32 fs_id, const struct sockaddr *addr, u16 gid_idx,
				   struct list_head *tx_rules_list,
				   struct list_head *rx_rules_list,
				   struct mlx5_macsec_fs *macsec_fs, bool is_tx);

void mlx5_macsec_del_roce_sa_rules(u32 fs_id, struct mlx5_macsec_fs *macsec_fs,
				   struct list_head *tx_rules_list,
				   struct list_head *rx_rules_list, bool is_tx);

#endif
#endif /* MLX5_MACSEC_H */
