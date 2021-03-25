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

bool mlx5_eswitch_reg_c1_loopback_enabled(const struct mlx5_eswitch *esw);
bool mlx5_eswitch_vport_match_metadata_enabled(const struct mlx5_eswitch *esw);

/* Reg C0 usage:
 * Reg C0 = < ESW_PFNUM_BITS(4) | ESW_VPORT BITS(12) | ESW_CHAIN_TAG(16) >
 *
 * Highest 4 bits of the reg c0 is the PF_NUM (range 0-15), 12 bits of
 * unique non-zero vport id (range 1-4095). The rest (lowest 16 bits) is left
 * for tc chain tag restoration.
 * PFNUM + VPORT comprise the SOURCE_PORT matching.
 */
#define ESW_VPORT_BITS 12
#define ESW_PFNUM_BITS 4
#define ESW_SOURCE_PORT_METADATA_BITS (ESW_PFNUM_BITS + ESW_VPORT_BITS)
#define ESW_SOURCE_PORT_METADATA_OFFSET (32 - ESW_SOURCE_PORT_METADATA_BITS)
#define ESW_CHAIN_TAG_METADATA_BITS (32 - ESW_SOURCE_PORT_METADATA_BITS)
#define ESW_CHAIN_TAG_METADATA_MASK GENMASK(ESW_CHAIN_TAG_METADATA_BITS - 1,\
					    0)

static inline u32 mlx5_eswitch_get_vport_metadata_mask(void)
{
	return GENMASK(31, 32 - ESW_SOURCE_PORT_METADATA_BITS);
}

u32 mlx5_eswitch_get_vport_metadata_for_match(struct mlx5_eswitch *esw,
					      u16 vport_num);
u32 mlx5_eswitch_get_vport_metadata_for_set(struct mlx5_eswitch *esw,
					    u16 vport_num);

/* Reg C1 usage:
 * Reg C1 = < ESW_TUN_ID(12) | ESW_TUN_OPTS(12) | ESW_ZONE_ID(8) >
 *
 * Highest 12 bits of reg c1 is the encapsulation tunnel id, next 12 bits is
 * encapsulation tunnel options, and the lowest 8 bits are used for zone id.
 *
 * Zone id is used to restore CT flow when packet misses on chain.
 *
 * Tunnel id and options are used together to restore the tunnel info metadata
 * on miss and to support inner header rewrite by means of implicit chain 0
 * flows.
 */
#define ESW_ZONE_ID_BITS 8
#define ESW_TUN_OPTS_BITS 12
#define ESW_TUN_ID_BITS 12
#define ESW_TUN_OPTS_OFFSET ESW_ZONE_ID_BITS
#define ESW_TUN_OFFSET ESW_TUN_OPTS_OFFSET
#define ESW_ZONE_ID_MASK GENMASK(ESW_ZONE_ID_BITS - 1, 0)
#define ESW_TUN_OPTS_MASK GENMASK(32 - ESW_TUN_ID_BITS - 1, ESW_TUN_OPTS_OFFSET)
#define ESW_TUN_MASK GENMASK(31, ESW_TUN_OFFSET)
#define ESW_TUN_ID_SLOW_TABLE_GOTO_VPORT 0 /* 0 is not a valid tunnel id */
#define ESW_TUN_OPTS_SLOW_TABLE_GOTO_VPORT 0xFFF /* 0xFFF is a reserved mapping */
#define ESW_TUN_SLOW_TABLE_GOTO_VPORT ((ESW_TUN_ID_SLOW_TABLE_GOTO_VPORT << ESW_TUN_OPTS_BITS) | \
				       ESW_TUN_OPTS_SLOW_TABLE_GOTO_VPORT)
#define ESW_TUN_SLOW_TABLE_GOTO_VPORT_MARK ESW_TUN_OPTS_MASK

u8 mlx5_eswitch_mode(struct mlx5_core_dev *dev);
#else  /* CONFIG_MLX5_ESWITCH */

static inline u8 mlx5_eswitch_mode(struct mlx5_core_dev *dev)
{
	return MLX5_ESWITCH_NONE;
}

static inline enum devlink_eswitch_encap_mode
mlx5_eswitch_get_encap_mode(const struct mlx5_core_dev *dev)
{
	return DEVLINK_ESWITCH_ENCAP_MODE_NONE;
}

static inline bool
mlx5_eswitch_reg_c1_loopback_enabled(const struct mlx5_eswitch *esw)
{
	return false;
};

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

static inline bool is_mdev_switchdev_mode(struct mlx5_core_dev *dev)
{
	return mlx5_eswitch_mode(dev) == MLX5_ESWITCH_OFFLOADS;
}
#endif
