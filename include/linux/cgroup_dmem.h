/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _CGROUP_DMEM_H
#define _CGROUP_DMEM_H

#include <linux/types.h>
#include <linux/llist.h>

struct dmem_cgroup_pool_state;

/* Opaque definition of a cgroup region, used internally */
struct dmem_cgroup_region;

#if IS_ENABLED(CONFIG_CGROUP_DMEM)
struct dmem_cgroup_region *dmem_cgroup_register_region(u64 size, const char *name_fmt, ...) __printf(2,3);
void dmem_cgroup_unregister_region(struct dmem_cgroup_region *region);
int dmem_cgroup_try_charge(struct dmem_cgroup_region *region, u64 size,
			   struct dmem_cgroup_pool_state **ret_pool,
			   struct dmem_cgroup_pool_state **ret_limit_pool);
void dmem_cgroup_uncharge(struct dmem_cgroup_pool_state *pool, u64 size);
bool dmem_cgroup_state_evict_valuable(struct dmem_cgroup_pool_state *limit_pool,
				      struct dmem_cgroup_pool_state *test_pool,
				      bool ignore_low, bool *ret_hit_low);

void dmem_cgroup_pool_state_put(struct dmem_cgroup_pool_state *pool);
#else
static inline __printf(2,3) struct dmem_cgroup_region *
dmem_cgroup_register_region(u64 size, const char *name_fmt, ...)
{
	return NULL;
}

static inline void dmem_cgroup_unregister_region(struct dmem_cgroup_region *region)
{ }

static inline int dmem_cgroup_try_charge(struct dmem_cgroup_region *region, u64 size,
					 struct dmem_cgroup_pool_state **ret_pool,
					 struct dmem_cgroup_pool_state **ret_limit_pool)
{
	*ret_pool = NULL;

	if (ret_limit_pool)
		*ret_limit_pool = NULL;

	return 0;
}

static inline void dmem_cgroup_uncharge(struct dmem_cgroup_pool_state *pool, u64 size)
{ }

static inline
bool dmem_cgroup_state_evict_valuable(struct dmem_cgroup_pool_state *limit_pool,
				      struct dmem_cgroup_pool_state *test_pool,
				      bool ignore_low, bool *ret_hit_low)
{
	return true;
}

static inline void dmem_cgroup_pool_state_put(struct dmem_cgroup_pool_state *pool)
{ }

#endif
#endif	/* _CGROUP_DMEM_H */
