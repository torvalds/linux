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

/**
 * struct dmem_cgroup_ops - Operations for a dmem cgroup region.
 * @reclaim: Optional callback invoked when dmem.max is set below the current
 *           usage of a pool. The driver should attempt to free at least
 *           @target_bytes from @pool. May be called multiple times if usage
 *           remains above the limit after returning.
 *
 *           Return: 0 if some progress was made (even if less than
 *           @target_bytes was freed), -ENOSPC if no progress could be made
 *           (the caller will retry up to a bounded number of times), or
 *           another negative error code if a fatal error occurred (stops
 *           further reclaim attempts immediately).
 */
struct dmem_cgroup_ops {
	int (*reclaim)(struct dmem_cgroup_pool_state *pool,
		       u64 target_bytes, void *priv);
};

/**
 * struct dmem_cgroup_init - Initialization parameters for a dmem cgroup region.
 * @size: Size of the region in bytes.
 * @ops: Optional operations for this region. May be NULL.
 * @reclaim_priv: Opaque pointer passed to @ops->reclaim. May be NULL.
 */
struct dmem_cgroup_init {
	u64 size;
	const struct dmem_cgroup_ops *ops;
	void *reclaim_priv;
};

#if IS_ENABLED(CONFIG_CGROUP_DMEM)
struct dmem_cgroup_region *
dmem_cgroup_register_region(const struct dmem_cgroup_init *init,
			    const char *name_fmt, ...) __printf(2, 3);
void dmem_cgroup_unregister_region(struct dmem_cgroup_region *region);
int dmem_cgroup_try_charge(struct dmem_cgroup_region *region, u64 size,
			   struct dmem_cgroup_pool_state **ret_pool,
			   struct dmem_cgroup_pool_state **ret_limit_pool);
void dmem_cgroup_uncharge(struct dmem_cgroup_pool_state *pool, u64 size);
bool dmem_cgroup_state_evict_valuable(struct dmem_cgroup_pool_state *limit_pool,
				      struct dmem_cgroup_pool_state *test_pool,
				      bool ignore_low, bool *ret_hit_low);
bool dmem_cgroup_below_min(struct dmem_cgroup_pool_state *root,
			   struct dmem_cgroup_pool_state *test);
bool dmem_cgroup_below_low(struct dmem_cgroup_pool_state *root,
			   struct dmem_cgroup_pool_state *test);
struct dmem_cgroup_pool_state *dmem_cgroup_get_common_ancestor(struct dmem_cgroup_pool_state *a,
							       struct dmem_cgroup_pool_state *b);

void dmem_cgroup_pool_state_put(struct dmem_cgroup_pool_state *pool);
#else
static inline __printf(2, 3) struct dmem_cgroup_region *
dmem_cgroup_register_region(const struct dmem_cgroup_init *init, const char *name_fmt, ...)
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

static inline bool dmem_cgroup_below_min(struct dmem_cgroup_pool_state *root,
					 struct dmem_cgroup_pool_state *test)
{
	return false;
}

static inline bool dmem_cgroup_below_low(struct dmem_cgroup_pool_state *root,
					 struct dmem_cgroup_pool_state *test)
{
	return false;
}

static inline
struct dmem_cgroup_pool_state *dmem_cgroup_get_common_ancestor(struct dmem_cgroup_pool_state *a,
							       struct dmem_cgroup_pool_state *b)
{
	return NULL;
}

static inline void dmem_cgroup_pool_state_put(struct dmem_cgroup_pool_state *pool)
{ }

#endif
#endif	/* _CGROUP_DMEM_H */
