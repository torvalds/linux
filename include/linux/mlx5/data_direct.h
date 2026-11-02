/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#ifndef _MLX5_DATA_DIRECT_H
#define _MLX5_DATA_DIRECT_H

#include <linux/compiler.h>
#include <linux/list.h>

struct device;
struct mlx5_core_dev;
struct notifier_block;
struct pci_dev;

enum mlx5_data_direct_event {
	MLX5_DATA_DIRECT_UNBIND,
};

struct mlx5_data_direct_dev {
	struct device *device;
	struct pci_dev *pdev;
	char *vuid;
	struct list_head list;
};

struct mlx5_data_direct {
	struct mlx5_data_direct_dev *dev;
	u32 pdn;
	u32 mkey;
	u32 mkey_ro;
	u8 mkey_ro_valid :1;
};

static inline struct mlx5_data_direct_dev *
mlx5_data_direct_get_dev(struct mlx5_data_direct *dd)
{
	return dd ? READ_ONCE(dd->dev) : NULL;
}

int mlx5_data_direct_register(struct mlx5_core_dev *mdev,
			      struct notifier_block *nb);
void mlx5_data_direct_unregister(struct mlx5_core_dev *mdev,
				 struct notifier_block *nb);

#endif
