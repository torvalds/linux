/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved */

#ifndef _OBJAGG_H
#define _OBJAGG_H

struct objagg_ops {
	size_t obj_size;
	bool (*delta_check)(void *priv, const void *parent_obj,
			    const void *obj);
	int (*hints_obj_cmp)(const void *obj1, const void *obj2);
	void * (*delta_create)(void *priv, void *parent_obj, void *obj);
	void (*delta_destroy)(void *priv, void *delta_priv);
	void * (*root_create)(void *priv, void *obj, unsigned int root_id);
#define OBJAGG_OBJ_ROOT_ID_INVALID UINT_MAX
	void (*root_destroy)(void *priv, void *root_priv);
};

struct objagg;
struct objagg_obj;
struct objagg_hints;

const void *objagg_obj_root_priv(const struct objagg_obj *objagg_obj);
const void *objagg_obj_delta_priv(const struct objagg_obj *objagg_obj);
const void *objagg_obj_raw(const struct objagg_obj *objagg_obj);

struct objagg_obj *objagg_obj_get(struct objagg *objagg, void *obj);
void objagg_obj_put(struct objagg *objagg, struct objagg_obj *objagg_obj);
struct objagg *objagg_create(const struct objagg_ops *ops,
			     struct objagg_hints *hints, void *priv);
void objagg_destroy(struct objagg *objagg);

struct objagg_obj_stats {
	unsigned int user_count;
	unsigned int delta_user_count; /* includes delta object users */
};

struct objagg_obj_stats_info {
	struct objagg_obj_stats stats;
	struct objagg_obj *objagg_obj; /* associated object */
	bool is_root;
};

struct objagg_stats {
	unsigned int root_count;
	unsigned int stats_info_count;
	struct objagg_obj_stats_info stats_info[];
};

const struct objagg_stats *objagg_stats_get(struct objagg *objagg);
void objagg_stats_put(const struct objagg_stats *objagg_stats);

enum objagg_opt_algo_type {
	OBJAGG_OPT_ALGO_SIMPLE_GREEDY,
};

struct objagg_hints *objagg_hints_get(struct objagg *objagg,
				      enum objagg_opt_algo_type opt_algo_type);
void objagg_hints_put(struct objagg_hints *objagg_hints);
const struct objagg_stats *
objagg_hints_stats_get(struct objagg_hints *objagg_hints);

#endif
