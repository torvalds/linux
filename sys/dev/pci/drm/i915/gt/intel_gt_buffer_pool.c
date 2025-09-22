// SPDX-License-Identifier: MIT
/*
 * Copyright © 2014-2018 Intel Corporation
 */

#include "gem/i915_gem_internal.h"
#include "gem/i915_gem_object.h"

#include "i915_drv.h"
#include "intel_engine_pm.h"
#include "intel_gt_buffer_pool.h"

static struct list_head *
bucket_for_size(struct intel_gt_buffer_pool *pool, size_t sz)
{
	int n;

	/*
	 * Compute a power-of-two bucket, but throw everything greater than
	 * 16KiB into the same bucket: i.e. the buckets hold objects of
	 * (1 page, 2 pages, 4 pages, 8+ pages).
	 */
	n = fls(sz >> PAGE_SHIFT) - 1;
	if (n >= ARRAY_SIZE(pool->cache_list))
		n = ARRAY_SIZE(pool->cache_list) - 1;

	return &pool->cache_list[n];
}

static void node_free(struct intel_gt_buffer_pool_node *node)
{
	i915_gem_object_put(node->obj);
	i915_active_fini(&node->active);
	kfree_rcu(node, rcu);
}

static bool pool_free_older_than(struct intel_gt_buffer_pool *pool, long keep)
{
	struct intel_gt_buffer_pool_node *node, *stale = NULL;
	bool active = false;
	int n;

	/* Free buffers that have not been used in the past second */
	for (n = 0; n < ARRAY_SIZE(pool->cache_list); n++) {
		struct list_head *list = &pool->cache_list[n];

		if (list_empty(list))
			continue;

		if (spin_trylock_irq(&pool->lock)) {
			struct list_head *pos;

			/* Most recent at head; oldest at tail */
			list_for_each_prev(pos, list) {
				unsigned long age;

				node = list_entry(pos, typeof(*node), link);

				age = READ_ONCE(node->age);
				if (!age || jiffies - age < keep)
					break;

				/* Check we are the first to claim this node */
				if (!xchg(&node->age, 0))
					break;

				node->free = stale;
				stale = node;
			}
			if (!list_is_last(pos, list))
				__list_del_many(pos, list);

			spin_unlock_irq(&pool->lock);
		}

		active |= !list_empty(list);
	}

	while ((node = stale)) {
		stale = stale->free;
		node_free(node);
	}

	return active;
}

static void pool_free_work(struct work_struct *wrk)
{
	struct intel_gt_buffer_pool *pool =
		container_of(wrk, typeof(*pool), work.work);
	struct intel_gt *gt = container_of(pool, struct intel_gt, buffer_pool);

	if (pool_free_older_than(pool, HZ))
		queue_delayed_work(gt->i915->unordered_wq, &pool->work,
				   round_jiffies_up_relative(HZ));
}

static void pool_retire(struct i915_active *ref)
{
	struct intel_gt_buffer_pool_node *node =
		container_of(ref, typeof(*node), active);
	struct intel_gt_buffer_pool *pool = node->pool;
	struct intel_gt *gt = container_of(pool, struct intel_gt, buffer_pool);
	struct list_head *list = bucket_for_size(pool, node->obj->base.size);
	unsigned long flags;

	if (node->pinned) {
		i915_gem_object_unpin_pages(node->obj);

		/* Return this object to the shrinker pool */
		i915_gem_object_make_purgeable(node->obj);
		node->pinned = false;
	}

	GEM_BUG_ON(node->age);
	spin_lock_irqsave(&pool->lock, flags);
	list_add_rcu(&node->link, list);
	WRITE_ONCE(node->age, jiffies ?: 1); /* 0 reserved for active nodes */
	spin_unlock_irqrestore(&pool->lock, flags);

	queue_delayed_work(gt->i915->unordered_wq, &pool->work,
			   round_jiffies_up_relative(HZ));
}

void intel_gt_buffer_pool_mark_used(struct intel_gt_buffer_pool_node *node)
{
	assert_object_held(node->obj);

	if (node->pinned)
		return;

	__i915_gem_object_pin_pages(node->obj);
	/* Hide this pinned object from the shrinker until retired */
	i915_gem_object_make_unshrinkable(node->obj);
	node->pinned = true;
}

static struct intel_gt_buffer_pool_node *
node_create(struct intel_gt_buffer_pool *pool, size_t sz,
	    enum i915_map_type type)
{
	struct intel_gt *gt = container_of(pool, struct intel_gt, buffer_pool);
	struct intel_gt_buffer_pool_node *node;
	struct drm_i915_gem_object *obj;

	node = kmalloc(sizeof(*node),
		       GFP_KERNEL | __GFP_RETRY_MAYFAIL | __GFP_NOWARN);
	if (!node)
		return ERR_PTR(-ENOMEM);

	node->age = 0;
	node->pool = pool;
	node->pinned = false;
	i915_active_init(&node->active, NULL, pool_retire, 0);

	obj = i915_gem_object_create_internal(gt->i915, sz);
	if (IS_ERR(obj)) {
		i915_active_fini(&node->active);
		kfree(node);
		return ERR_CAST(obj);
	}

	i915_gem_object_set_readonly(obj);

	node->type = type;
	node->obj = obj;
	return node;
}

struct intel_gt_buffer_pool_node *
intel_gt_get_buffer_pool(struct intel_gt *gt, size_t size,
			 enum i915_map_type type)
{
	struct intel_gt_buffer_pool *pool = &gt->buffer_pool;
	struct intel_gt_buffer_pool_node *node;
	struct list_head *list;
	int ret;

	size = PAGE_ALIGN(size);
	list = bucket_for_size(pool, size);

	rcu_read_lock();
	list_for_each_entry_rcu(node, list, link) {
		unsigned long age;

		if (node->obj->base.size < size)
			continue;

		if (node->type != type)
			continue;

		age = READ_ONCE(node->age);
		if (!age)
			continue;

		if (cmpxchg(&node->age, age, 0) == age) {
			spin_lock_irq(&pool->lock);
			list_del_rcu(&node->link);
			spin_unlock_irq(&pool->lock);
			break;
		}
	}
	rcu_read_unlock();

	if (&node->link == list) {
		node = node_create(pool, size, type);
		if (IS_ERR(node))
			return node;
	}

	ret = i915_active_acquire(&node->active);
	if (ret) {
		node_free(node);
		return ERR_PTR(ret);
	}

	return node;
}

void intel_gt_init_buffer_pool(struct intel_gt *gt)
{
	struct intel_gt_buffer_pool *pool = &gt->buffer_pool;
	int n;

	mtx_init(&pool->lock, IPL_TTY);
	for (n = 0; n < ARRAY_SIZE(pool->cache_list); n++)
		INIT_LIST_HEAD(&pool->cache_list[n]);
	INIT_DELAYED_WORK(&pool->work, pool_free_work);
}

void intel_gt_flush_buffer_pool(struct intel_gt *gt)
{
	struct intel_gt_buffer_pool *pool = &gt->buffer_pool;

	do {
		while (pool_free_older_than(pool, 0))
			;
	} while (cancel_delayed_work_sync(&pool->work));
}

void intel_gt_fini_buffer_pool(struct intel_gt *gt)
{
	struct intel_gt_buffer_pool *pool = &gt->buffer_pool;
	int n;

	for (n = 0; n < ARRAY_SIZE(pool->cache_list); n++)
		GEM_BUG_ON(!list_empty(&pool->cache_list[n]));
}
