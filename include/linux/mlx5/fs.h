/*
 * Copyright (c) 2015, Mellanox Technologies. All rights reserved.
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

#ifndef _MLX5_FS_
#define _MLX5_FS_

#include <linux/mlx5/driver.h>
#include <linux/mlx5/mlx5_ifc.h>

#define MLX5_FS_DEFAULT_FLOW_TAG 0x0

enum {
	MLX5_FLOW_CONTEXT_ACTION_FWD_NEXT_PRIO	= 1 << 16,
};

#define LEFTOVERS_RULE_NUM	 2
static inline void build_leftovers_ft_param(int *priority,
					    int *n_ent,
					    int *n_grp)
{
	*priority = 0; /* Priority of leftovers_prio-0 */
	*n_ent = LEFTOVERS_RULE_NUM;
	*n_grp = LEFTOVERS_RULE_NUM;
}

enum mlx5_flow_namespace_type {
	MLX5_FLOW_NAMESPACE_BYPASS,
	MLX5_FLOW_NAMESPACE_OFFLOADS,
	MLX5_FLOW_NAMESPACE_ETHTOOL,
	MLX5_FLOW_NAMESPACE_KERNEL,
	MLX5_FLOW_NAMESPACE_LEFTOVERS,
	MLX5_FLOW_NAMESPACE_ANCHOR,
	MLX5_FLOW_NAMESPACE_FDB,
	MLX5_FLOW_NAMESPACE_ESW_EGRESS,
	MLX5_FLOW_NAMESPACE_ESW_INGRESS,
};

struct mlx5_flow_table;
struct mlx5_flow_group;
struct mlx5_flow_rule;
struct mlx5_flow_namespace;

struct mlx5_flow_spec {
	u8   match_criteria_enable;
	u32  match_criteria[MLX5_ST_SZ_DW(fte_match_param)];
	u32  match_value[MLX5_ST_SZ_DW(fte_match_param)];
};

struct mlx5_flow_destination {
	enum mlx5_flow_destination_type	type;
	union {
		u32			tir_num;
		struct mlx5_flow_table	*ft;
		u32			vport_num;
		struct mlx5_fc		*counter;
	};
};

struct mlx5_flow_namespace *
mlx5_get_flow_namespace(struct mlx5_core_dev *dev,
			enum mlx5_flow_namespace_type type);

struct mlx5_flow_table *
mlx5_create_auto_grouped_flow_table(struct mlx5_flow_namespace *ns,
				    int prio,
				    int num_flow_table_entries,
				    int max_num_groups,
				    u32 level);

struct mlx5_flow_table *
mlx5_create_flow_table(struct mlx5_flow_namespace *ns,
		       int prio,
		       int num_flow_table_entries,
		       u32 level);
struct mlx5_flow_table *
mlx5_create_vport_flow_table(struct mlx5_flow_namespace *ns,
			     int prio,
			     int num_flow_table_entries,
			     u32 level, u16 vport);
struct mlx5_flow_table *mlx5_create_lag_demux_flow_table(
					       struct mlx5_flow_namespace *ns,
					       int prio, u32 level);
int mlx5_destroy_flow_table(struct mlx5_flow_table *ft);

/* inbox should be set with the following values:
 * start_flow_index
 * end_flow_index
 * match_criteria_enable
 * match_criteria
 */
struct mlx5_flow_group *
mlx5_create_flow_group(struct mlx5_flow_table *ft, u32 *in);
void mlx5_destroy_flow_group(struct mlx5_flow_group *fg);

/* Single destination per rule.
 * Group ID is implied by the match criteria.
 */
struct mlx5_flow_rule *
mlx5_add_flow_rule(struct mlx5_flow_table *ft,
		   struct mlx5_flow_spec *spec,
		   u32 action,
		   u32 flow_tag,
		   struct mlx5_flow_destination *dest);
void mlx5_del_flow_rule(struct mlx5_flow_rule *fr);

int mlx5_modify_rule_destination(struct mlx5_flow_rule *rule,
				 struct mlx5_flow_destination *dest);

struct mlx5_fc *mlx5_flow_rule_counter(struct mlx5_flow_rule *rule);
struct mlx5_fc *mlx5_fc_create(struct mlx5_core_dev *dev, bool aging);
void mlx5_fc_destroy(struct mlx5_core_dev *dev, struct mlx5_fc *counter);
void mlx5_fc_query_cached(struct mlx5_fc *counter,
			  u64 *bytes, u64 *packets, u64 *lastuse);

#endif
