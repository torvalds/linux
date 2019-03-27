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

#ifndef _MLX5_FS_
#define _MLX5_FS_

#include <linux/list.h>

#include <dev/mlx5/mlx5_ifc.h>
#include <dev/mlx5/device.h>
#include <dev/mlx5/driver.h>

enum {
	MLX5_FLOW_CONTEXT_ACTION_FWD_NEXT_PRIO	= 1 << 16,
};

/*Flow tag*/
enum {
	MLX5_FS_DEFAULT_FLOW_TAG  = 0xFFFFFF,
	MLX5_FS_ETH_FLOW_TAG  = 0xFFFFFE,
	MLX5_FS_SNIFFER_FLOW_TAG  = 0xFFFFFD,
};

enum {
	MLX5_FS_FLOW_TAG_MASK = 0xFFFFFF,
};

#define FS_MAX_TYPES		10
#define FS_MAX_ENTRIES		32000U

enum mlx5_flow_namespace_type {
	MLX5_FLOW_NAMESPACE_BYPASS,
	MLX5_FLOW_NAMESPACE_KERNEL,
	MLX5_FLOW_NAMESPACE_LEFTOVERS,
	MLX5_FLOW_NAMESPACE_SNIFFER_RX,
	MLX5_FLOW_NAMESPACE_SNIFFER_TX,
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
	u32	type;
	union {
		u32			tir_num;
		struct mlx5_flow_table	*ft;
		u32			vport_num;
	};
};

#define FT_NAME_STR_SZ 20
#define LEFTOVERS_RULE_NUM 2
static inline void build_leftovers_ft_param(char *name,
	unsigned int *priority,
	int *n_ent,
	int *n_grp)
{
	snprintf(name, FT_NAME_STR_SZ, "leftovers");
	*priority = 0; /*Priority of leftovers_prio-0*/
	*n_ent = LEFTOVERS_RULE_NUM + 1; /*1: star rules*/
	*n_grp = LEFTOVERS_RULE_NUM;
}

static inline bool outer_header_zero(u32 *match_criteria)
{
	int size = MLX5_ST_SZ_BYTES(fte_match_param);
	char *outer_headers_c = MLX5_ADDR_OF(fte_match_param, match_criteria,
					     outer_headers);

	return outer_headers_c[0] == 0 && !memcmp(outer_headers_c,
						  outer_headers_c + 1,
						  size - 1);
}

struct mlx5_flow_namespace *
mlx5_get_flow_namespace(struct mlx5_core_dev *dev,
			enum mlx5_flow_namespace_type type);

/* The underlying implementation create two more entries for
 * chaining flow tables. the user should be aware that if he pass
 * max_num_ftes as 2^N it will result in doubled size flow table
 */
struct mlx5_flow_table *
mlx5_create_auto_grouped_flow_table(struct mlx5_flow_namespace *ns,
				    int prio,
				    const char *name,
				    int num_flow_table_entries,
				    int max_num_groups);

struct mlx5_flow_table *
mlx5_create_vport_flow_table(struct mlx5_flow_namespace *ns,
							 u16 vport,
							 int prio,
							 const char *name,
							 int num_flow_table_entries);

struct mlx5_flow_table *
mlx5_create_flow_table(struct mlx5_flow_namespace *ns,
		       int prio,
		       const char *name,
		       int num_flow_table_entries);
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
		   u8 match_criteria_enable,
		   u32 *match_criteria,
		   u32 *match_value,
		   u32 action,
		   u32 flow_tag,
		   struct mlx5_flow_destination *dest);
void mlx5_del_flow_rule(struct mlx5_flow_rule *fr);

/*The following API is for sniffer*/
typedef int (*rule_event_fn)(struct mlx5_flow_rule *rule,
			     bool ctx_changed,
			     void *client_data,
			     void *context);

struct mlx5_flow_handler;

struct flow_client_priv_data;

void mlx5e_sniffer_roce_mode_notify(
	struct mlx5_core_dev *mdev,
	int action);

int mlx5_set_rule_private_data(struct mlx5_flow_rule *rule, struct
			       mlx5_flow_handler *handler,  void
			       *client_data);

struct mlx5_flow_handler *mlx5_register_rule_notifier(struct mlx5_core_dev *dev,
						      enum mlx5_flow_namespace_type ns_type,
						      rule_event_fn add_cb,
						      rule_event_fn del_cb,
						      void *context);

void mlx5_unregister_rule_notifier(struct mlx5_flow_handler *handler);

void mlx5_flow_iterate_existing_rules(struct mlx5_flow_namespace *ns,
					     rule_event_fn cb,
					     void *context);

void mlx5_get_match_criteria(u32 *match_criteria,
			     struct mlx5_flow_rule *rule);

void mlx5_get_match_value(u32 *match_value,
			  struct mlx5_flow_rule *rule);

u8 mlx5_get_match_criteria_enable(struct mlx5_flow_rule *rule);

struct mlx5_flow_rules_list *get_roce_flow_rules(u8 roce_mode);

void mlx5_del_flow_rules_list(struct mlx5_flow_rules_list *rules_list);

struct mlx5_flow_rules_list {
	struct list_head head;
};

struct mlx5_flow_rule_node {
	struct	list_head list;
	u32	match_criteria[MLX5_ST_SZ_DW(fte_match_param)];
	u32	match_value[MLX5_ST_SZ_DW(fte_match_param)];
	u8	match_criteria_enable;
};

struct mlx5_core_fs_mask {
	u8	match_criteria_enable;
	u32	match_criteria[MLX5_ST_SZ_DW(fte_match_param)];
};

bool fs_match_exact_val(
		struct mlx5_core_fs_mask *mask,
		void *val1,
		void *val2);

bool fs_match_exact_mask(
		u8 match_criteria_enable1,
		u8 match_criteria_enable2,
		void *mask1,
		void *mask2);
/**********end API for sniffer**********/

#endif
