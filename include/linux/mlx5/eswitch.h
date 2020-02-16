/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/*
 * Copyright (c) 2018 Mellanox Technologies. All rights reserved.
 */

#ifndef _MLX5_ESWITCH_
#define _MLX5_ESWITCH_

#include <linux/mlx5/driver.h>
#include <net/devlink.h>

#define MLX5_ESWITCH_MANAGER(mdev) MLX5_CAP_GEN(mdev, eswitch_manager)

enum {
	MLX5_ESWITCH_NONE,
	MLX5_ESWITCH_LEGACY,
	MLX5_ESWITCH_OFFLOADS
};

enum {
	REP_ETH,
	REP_IB,
	NUM_REP_TYPES,
};

enum {
	REP_UNREGISTERED,
	REP_REGISTERED,
	REP_LOADED,
};

struct mlx5_eswitch_rep;
struct mlx5_eswitch_rep_ops {
	int (*load)(struct mlx5_core_dev *dev, struct mlx5_eswitch_rep *rep);
	void (*unload)(struct mlx5_eswitch_rep *rep);
	void *(*get_proto_dev)(struct mlx5_eswitch_rep *rep);
};

struct mlx5_eswitch_rep_data {
	void *priv;
	atomic_t state;
};

struct mlx5_eswitch_rep {
	struct mlx5_eswitch_rep_data rep_data[NUM_REP_TYPES];
	u16		       vport;
	u16		       vlan;
	/* Only IB rep is using vport_index */
	u16		       vport_index;
	u32		       vlan_refcount;
};

void mlx5_eswitch_register_vport_reps(struct mlx5_eswitch *esw,
				      const struct mlx5_eswitch_rep_ops *ops,
				      u8 rep_type);
void mlx5_eswitch_unregister_vport_reps(struct mlx5_eswitch *esw, u8 rep_type);
void *mlx5_eswitch_get_proto_dev(struct mlx5_eswitch *esw,
				 u16 vport_num,
				 u8 rep_type);
struct mlx5_eswitch_rep *mlx5_eswitch_vport_rep(struct mlx5_eswitch *esw,
						u16 vport_num);
void *mlx5_eswitch_uplink_get_proto_dev(struct mlx5_eswitch *esw, u8 rep_type);
struct mlx5_flow_handle *
mlx5_eswitch_add_send_to_vport_rule(struct mlx5_eswitch *esw,
				    u16 vport_num, u32 sqn);

u16 mlx5_eswitch_get_total_vports(const struct mlx5_core_dev *dev);

#ifdef CONFIG_MLX5_ESWITCH
enum devlink_eswitch_encap_mode
mlx5_eswitch_get_encap_mode(const struct mlx5_core_dev *dev);

bool mlx5_eswitch_vport_match_metadata_enabled(const struct mlx5_eswitch *esw);

/* Reg C0 usage:
 * Reg C0 = < ESW_VHCA_ID_BITS(8) | ESW_VPORT BITS(8) | ESW_CHAIN_TAG(16) >
 *
 * Highest 8 bits of the reg c0 is the vhca_id, next 8 bits is vport_num,
 * the rest (lowest 16 bits) is left for tc chain tag restoration.
 * VHCA_ID + VPORT comprise the SOURCE_PORT matching.
 */
#define ESW_VHCA_ID_BITS 8
#define ESW_VPORT_BITS 8
#define ESW_SOURCE_PORT_METADATA_BITS (ESW_VHCA_ID_BITS + ESW_VPORT_BITS)
#define ESW_SOURCE_PORT_METADATA_OFFSET (32 - ESW_SOURCE_PORT_METADATA_BITS)
#define ESW_CHAIN_TAG_METADATA_BITS (32 - ESW_SOURCE_PORT_METADATA_BITS)

static inline u32 mlx5_eswitch_get_vport_metadata_mask(void)
{
	return GENMASK(31, 32 - ESW_SOURCE_PORT_METADATA_BITS);
}

u32 mlx5_eswitch_get_vport_metadata_for_match(struct mlx5_eswitch *esw,
					      u16 vport_num);
u8 mlx5_eswitch_mode(struct mlx5_eswitch *esw);
#else  /* CONFIG_MLX5_ESWITCH */

static inline u8 mlx5_eswitch_mode(struct mlx5_eswitch *esw)
{
	return MLX5_ESWITCH_NONE;
}

static inline enum devlink_eswitch_encap_mode
mlx5_eswitch_get_encap_mode(const struct mlx5_core_dev *dev)
{
	return DEVLINK_ESWITCH_ENCAP_MODE_NONE;
}

static inline bool
mlx5_eswitch_vport_match_metadata_enabled(const struct mlx5_eswitch *esw)
{
	return false;
};

static inline u32
mlx5_eswitch_get_vport_metadata_for_match(struct mlx5_eswitch *esw,
					  int vport_num)
{
	return 0;
};

static inline u32
mlx5_eswitch_get_vport_metadata_mask(void)
{
	return 0;
}
#endif /* CONFIG_MLX5_ESWITCH */

#endif
