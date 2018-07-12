/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/*
 * Copyright (c) 2018 Mellanox Technologies. All rights reserved.
 */

#ifndef _MLX5_ESWITCH_
#define _MLX5_ESWITCH_

#include <linux/mlx5/driver.h>

enum {
	SRIOV_NONE,
	SRIOV_LEGACY,
	SRIOV_OFFLOADS
};

enum {
	REP_ETH,
	REP_IB,
	NUM_REP_TYPES,
};

struct mlx5_eswitch_rep;
struct mlx5_eswitch_rep_if {
	int		       (*load)(struct mlx5_core_dev *dev,
				       struct mlx5_eswitch_rep *rep);
	void		       (*unload)(struct mlx5_eswitch_rep *rep);
	void		       *(*get_proto_dev)(struct mlx5_eswitch_rep *rep);
	void			*priv;
	bool		       valid;
};

struct mlx5_eswitch_rep {
	struct mlx5_eswitch_rep_if rep_if[NUM_REP_TYPES];
	u16		       vport;
	u8		       hw_id[ETH_ALEN];
	u16		       vlan;
	u32		       vlan_refcount;
};

void mlx5_eswitch_register_vport_rep(struct mlx5_eswitch *esw,
				     int vport_index,
				     struct mlx5_eswitch_rep_if *rep_if,
				     u8 rep_type);
void mlx5_eswitch_unregister_vport_rep(struct mlx5_eswitch *esw,
				       int vport_index,
				       u8 rep_type);
void *mlx5_eswitch_get_proto_dev(struct mlx5_eswitch *esw,
				 int vport,
				 u8 rep_type);
struct mlx5_eswitch_rep *mlx5_eswitch_vport_rep(struct mlx5_eswitch *esw,
						int vport);
void *mlx5_eswitch_uplink_get_proto_dev(struct mlx5_eswitch *esw, u8 rep_type);
u8 mlx5_eswitch_mode(struct mlx5_eswitch *esw);
struct mlx5_flow_handle *
mlx5_eswitch_add_send_to_vport_rule(struct mlx5_eswitch *esw,
				    int vport, u32 sqn);
#endif
