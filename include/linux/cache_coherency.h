/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Cache coherency maintenance operation device drivers
 *
 * Copyright Huawei 2025
 */
#ifndef _LINUX_CACHE_COHERENCY_H_
#define _LINUX_CACHE_COHERENCY_H_

#include <linux/list.h>
#include <linux/kref.h>
#include <linux/types.h>

struct cc_inval_params {
	phys_addr_t addr;
	size_t size;
};

struct cache_coherency_ops_inst;

struct cache_coherency_ops {
	int (*wbinv)(struct cache_coherency_ops_inst *cci,
		     struct cc_inval_params *invp);
	int (*done)(struct cache_coherency_ops_inst *cci);
};

struct cache_coherency_ops_inst {
	struct kref kref;
	struct list_head node;
	const struct cache_coherency_ops *ops;
};

int cache_coherency_ops_instance_register(struct cache_coherency_ops_inst *cci);
void cache_coherency_ops_instance_unregister(struct cache_coherency_ops_inst *cci);

struct cache_coherency_ops_inst *
_cache_coherency_ops_instance_alloc(const struct cache_coherency_ops *ops,
				    size_t size);
/**
 * cache_coherency_ops_instance_alloc - Allocate cache coherency ops instance
 * @ops: Cache maintenance operations
 * @drv_struct: structure that contains the struct cache_coherency_ops_inst
 * @member: Name of the struct cache_coherency_ops_inst member in @drv_struct.
 *
 * This allocates a driver specific structure and initializes the
 * cache_coherency_ops_inst embedded in the drv_struct. Upon success the
 * pointer must be freed via cache_coherency_ops_instance_put().
 *
 * Returns a &drv_struct * on success, %NULL on error.
 */
#define cache_coherency_ops_instance_alloc(ops, drv_struct, member)	    \
	({								    \
		static_assert(__same_type(struct cache_coherency_ops_inst,  \
					  ((drv_struct *)NULL)->member));   \
		static_assert(offsetof(drv_struct, member) == 0);	    \
		(drv_struct *)_cache_coherency_ops_instance_alloc(ops,	    \
			sizeof(drv_struct));				    \
	})
void cache_coherency_ops_instance_put(struct cache_coherency_ops_inst *cci);

#endif
