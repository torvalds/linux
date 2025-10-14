// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2023-2024 Intel Corporation (Maarten Lankhorst <dev@lankhorst.se>)
 * Copyright 2024 Red Hat (Maxime Ripard <mripard@kernel.org>)
 * Partially based on the rdma and misc controllers, which bear the following copyrights:
 *
 * Copyright 2020 Google LLC
 * Copyright (C) 2016 Parav Pandit <pandit.parav@gmail.com>
 */

#include <linux/cgroup.h>
#include <linux/cgroup_dmem.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/page_counter.h>
#include <linux/parser.h>
#include <linux/rculist.h>
#include <linux/slab.h>

struct dmem_cgroup_region {
	/**
	 * @ref: References keeping the region alive.
	 * Keeps the region reference alive after a succesful RCU lookup.
	 */
	struct kref ref;

	/** @rcu: RCU head for freeing */
	struct rcu_head rcu;

	/**
	 * @region_node: Linked into &dmem_cgroup_regions list.
	 * Protected by RCU and global spinlock.
	 */
	struct list_head region_node;

	/**
	 * @pools: List of pools linked to this region.
	 * Protected by global spinlock only
	 */
	struct list_head pools;

	/** @size: Size of region, in bytes */
	u64 size;

	/** @name: Name describing the node, set by dmem_cgroup_register_region */
	char *name;

	/**
	 * @unregistered: Whether the region is unregistered by its caller.
	 * No new pools should be added to the region afterwards.
	 */
	bool unregistered;
};

struct dmemcg_state {
	struct cgroup_subsys_state css;

	struct list_head pools;
};

struct dmem_cgroup_pool_state {
	struct dmem_cgroup_region *region;
	struct dmemcg_state *cs;

	/* css node, RCU protected against region teardown */
	struct list_head	css_node;

	/* dev node, no RCU protection required */
	struct list_head	region_node;

	struct rcu_head rcu;

	struct page_counter cnt;

	bool inited;
};

/*
 * 3 operations require locking protection:
 * - Registering and unregistering region to/from list, requires global lock.
 * - Adding a dmem_cgroup_pool_state to a CSS, removing when CSS is freed.
 * - Adding a dmem_cgroup_pool_state to a region list.
 *
 * Since for the most common operations RCU provides enough protection, I
 * do not think more granular locking makes sense. Most protection is offered
 * by RCU and the lockless operating page_counter.
 */
static DEFINE_SPINLOCK(dmemcg_lock);
static LIST_HEAD(dmem_cgroup_regions);

static inline struct dmemcg_state *
css_to_dmemcs(struct cgroup_subsys_state *css)
{
	return container_of(css, struct dmemcg_state, css);
}

static inline struct dmemcg_state *get_current_dmemcs(void)
{
	return css_to_dmemcs(task_get_css(current, dmem_cgrp_id));
}

static struct dmemcg_state *parent_dmemcs(struct dmemcg_state *cg)
{
	return cg->css.parent ? css_to_dmemcs(cg->css.parent) : NULL;
}

static void free_cg_pool(struct dmem_cgroup_pool_state *pool)
{
	list_del(&pool->region_node);
	kfree(pool);
}

static void
set_resource_min(struct dmem_cgroup_pool_state *pool, u64 val)
{
	page_counter_set_min(&pool->cnt, val);
}

static void
set_resource_low(struct dmem_cgroup_pool_state *pool, u64 val)
{
	page_counter_set_low(&pool->cnt, val);
}

static void
set_resource_max(struct dmem_cgroup_pool_state *pool, u64 val)
{
	page_counter_set_max(&pool->cnt, val);
}

static u64 get_resource_low(struct dmem_cgroup_pool_state *pool)
{
	return pool ? READ_ONCE(pool->cnt.low) : 0;
}

static u64 get_resource_min(struct dmem_cgroup_pool_state *pool)
{
	return pool ? READ_ONCE(pool->cnt.min) : 0;
}

static u64 get_resource_max(struct dmem_cgroup_pool_state *pool)
{
	return pool ? READ_ONCE(pool->cnt.max) : PAGE_COUNTER_MAX;
}

static u64 get_resource_current(struct dmem_cgroup_pool_state *pool)
{
	return pool ? page_counter_read(&pool->cnt) : 0;
}

static void reset_all_resource_limits(struct dmem_cgroup_pool_state *rpool)
{
	set_resource_min(rpool, 0);
	set_resource_low(rpool, 0);
	set_resource_max(rpool, PAGE_COUNTER_MAX);
}

static void dmemcs_offline(struct cgroup_subsys_state *css)
{
	struct dmemcg_state *dmemcs = css_to_dmemcs(css);
	struct dmem_cgroup_pool_state *pool;

	rcu_read_lock();
	list_for_each_entry_rcu(pool, &dmemcs->pools, css_node)
		reset_all_resource_limits(pool);
	rcu_read_unlock();
}

static void dmemcs_free(struct cgroup_subsys_state *css)
{
	struct dmemcg_state *dmemcs = css_to_dmemcs(css);
	struct dmem_cgroup_pool_state *pool, *next;

	spin_lock(&dmemcg_lock);
	list_for_each_entry_safe(pool, next, &dmemcs->pools, css_node) {
		/*
		 *The pool is dead and all references are 0,
		 * no need for RCU protection with list_del_rcu or freeing.
		 */
		list_del(&pool->css_node);
		free_cg_pool(pool);
	}
	spin_unlock(&dmemcg_lock);

	kfree(dmemcs);
}

static struct cgroup_subsys_state *
dmemcs_alloc(struct cgroup_subsys_state *parent_css)
{
	struct dmemcg_state *dmemcs = kzalloc(sizeof(*dmemcs), GFP_KERNEL);
	if (!dmemcs)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&dmemcs->pools);
	return &dmemcs->css;
}

static struct dmem_cgroup_pool_state *
find_cg_pool_locked(struct dmemcg_state *dmemcs, struct dmem_cgroup_region *region)
{
	struct dmem_cgroup_pool_state *pool;

	list_for_each_entry_rcu(pool, &dmemcs->pools, css_node, spin_is_locked(&dmemcg_lock))
		if (pool->region == region)
			return pool;

	return NULL;
}

static struct dmem_cgroup_pool_state *pool_parent(struct dmem_cgroup_pool_state *pool)
{
	if (!pool->cnt.parent)
		return NULL;

	return container_of(pool->cnt.parent, typeof(*pool), cnt);
}

static void
dmem_cgroup_calculate_protection(struct dmem_cgroup_pool_state *limit_pool,
				 struct dmem_cgroup_pool_state *test_pool)
{
	struct page_counter *climit;
	struct cgroup_subsys_state *css;
	struct dmemcg_state *dmemcg_iter;
	struct dmem_cgroup_pool_state *pool, *found_pool;

	climit = &limit_pool->cnt;

	rcu_read_lock();

	css_for_each_descendant_pre(css, &limit_pool->cs->css) {
		dmemcg_iter = container_of(css, struct dmemcg_state, css);
		found_pool = NULL;

		list_for_each_entry_rcu(pool, &dmemcg_iter->pools, css_node) {
			if (pool->region == limit_pool->region) {
				found_pool = pool;
				break;
			}
		}
		if (!found_pool)
			continue;

		page_counter_calculate_protection(
			climit, &found_pool->cnt, true);

		if (found_pool == test_pool)
			break;
	}
	rcu_read_unlock();
}

/**
 * dmem_cgroup_state_evict_valuable() - Check if we should evict from test_pool
 * @limit_pool: The pool for which we hit limits
 * @test_pool: The pool for which to test
 * @ignore_low: Whether we have to respect low watermarks.
 * @ret_hit_low: Pointer to whether it makes sense to consider low watermark.
 *
 * This function returns true if we can evict from @test_pool, false if not.
 * When returning false and @ignore_low is false, @ret_hit_low may
 * be set to true to indicate this function can be retried with @ignore_low
 * set to true.
 *
 * Return: bool
 */
bool dmem_cgroup_state_evict_valuable(struct dmem_cgroup_pool_state *limit_pool,
				      struct dmem_cgroup_pool_state *test_pool,
				      bool ignore_low, bool *ret_hit_low)
{
	struct dmem_cgroup_pool_state *pool = test_pool;
	struct page_counter *ctest;
	u64 used, min, low;

	/* Can always evict from current pool, despite limits */
	if (limit_pool == test_pool)
		return true;

	if (limit_pool) {
		if (!parent_dmemcs(limit_pool->cs))
			return true;

		for (pool = test_pool; pool && limit_pool != pool; pool = pool_parent(pool))
			{}

		if (!pool)
			return false;
	} else {
		/*
		 * If there is no cgroup limiting memory usage, use the root
		 * cgroup instead for limit calculations.
		 */
		for (limit_pool = test_pool; pool_parent(limit_pool); limit_pool = pool_parent(limit_pool))
			{}
	}

	ctest = &test_pool->cnt;

	dmem_cgroup_calculate_protection(limit_pool, test_pool);

	used = page_counter_read(ctest);
	min = READ_ONCE(ctest->emin);

	if (used <= min)
		return false;

	if (!ignore_low) {
		low = READ_ONCE(ctest->elow);
		if (used > low)
			return true;

		*ret_hit_low = true;
		return false;
	}
	return true;
}
EXPORT_SYMBOL_GPL(dmem_cgroup_state_evict_valuable);

static struct dmem_cgroup_pool_state *
alloc_pool_single(struct dmemcg_state *dmemcs, struct dmem_cgroup_region *region,
		  struct dmem_cgroup_pool_state **allocpool)
{
	struct dmemcg_state *parent = parent_dmemcs(dmemcs);
	struct dmem_cgroup_pool_state *pool, *ppool = NULL;

	if (!*allocpool) {
		pool = kzalloc(sizeof(*pool), GFP_NOWAIT);
		if (!pool)
			return ERR_PTR(-ENOMEM);
	} else {
		pool = *allocpool;
		*allocpool = NULL;
	}

	pool->region = region;
	pool->cs = dmemcs;

	if (parent)
		ppool = find_cg_pool_locked(parent, region);

	page_counter_init(&pool->cnt,
			  ppool ? &ppool->cnt : NULL, true);
	reset_all_resource_limits(pool);

	list_add_tail_rcu(&pool->css_node, &dmemcs->pools);
	list_add_tail(&pool->region_node, &region->pools);

	if (!parent)
		pool->inited = true;
	else
		pool->inited = ppool ? ppool->inited : false;
	return pool;
}

static struct dmem_cgroup_pool_state *
get_cg_pool_locked(struct dmemcg_state *dmemcs, struct dmem_cgroup_region *region,
		   struct dmem_cgroup_pool_state **allocpool)
{
	struct dmem_cgroup_pool_state *pool, *ppool, *retpool;
	struct dmemcg_state *p, *pp;

	/*
	 * Recursively create pool, we may not initialize yet on
	 * recursion, this is done as a separate step.
	 */
	for (p = dmemcs; p; p = parent_dmemcs(p)) {
		pool = find_cg_pool_locked(p, region);
		if (!pool)
			pool = alloc_pool_single(p, region, allocpool);

		if (IS_ERR(pool))
			return pool;

		if (p == dmemcs && pool->inited)
			return pool;

		if (pool->inited)
			break;
	}

	retpool = pool = find_cg_pool_locked(dmemcs, region);
	for (p = dmemcs, pp = parent_dmemcs(dmemcs); pp; p = pp, pp = parent_dmemcs(p)) {
		if (pool->inited)
			break;

		/* ppool was created if it didn't exist by above loop. */
		ppool = find_cg_pool_locked(pp, region);

		/* Fix up parent links, mark as inited. */
		pool->cnt.parent = &ppool->cnt;
		pool->inited = true;

		pool = ppool;
	}

	return retpool;
}

static void dmemcg_free_rcu(struct rcu_head *rcu)
{
	struct dmem_cgroup_region *region = container_of(rcu, typeof(*region), rcu);
	struct dmem_cgroup_pool_state *pool, *next;

	list_for_each_entry_safe(pool, next, &region->pools, region_node)
		free_cg_pool(pool);
	kfree(region->name);
	kfree(region);
}

static void dmemcg_free_region(struct kref *ref)
{
	struct dmem_cgroup_region *cgregion = container_of(ref, typeof(*cgregion), ref);

	call_rcu(&cgregion->rcu, dmemcg_free_rcu);
}

/**
 * dmem_cgroup_unregister_region() - Unregister a previously registered region.
 * @region: The region to unregister.
 *
 * This function undoes dmem_cgroup_register_region.
 */
void dmem_cgroup_unregister_region(struct dmem_cgroup_region *region)
{
	struct list_head *entry;

	if (!region)
		return;

	spin_lock(&dmemcg_lock);

	/* Remove from global region list */
	list_del_rcu(&region->region_node);

	list_for_each_rcu(entry, &region->pools) {
		struct dmem_cgroup_pool_state *pool =
			container_of(entry, typeof(*pool), region_node);

		list_del_rcu(&pool->css_node);
	}

	/*
	 * Ensure any RCU based lookups fail. Additionally,
	 * no new pools should be added to the dead region
	 * by get_cg_pool_unlocked.
	 */
	region->unregistered = true;
	spin_unlock(&dmemcg_lock);

	kref_put(&region->ref, dmemcg_free_region);
}
EXPORT_SYMBOL_GPL(dmem_cgroup_unregister_region);

/**
 * dmem_cgroup_register_region() - Register a regions for dev cgroup.
 * @size: Size of region to register, in bytes.
 * @fmt: Region parameters to register
 *
 * This function registers a node in the dmem cgroup with the
 * name given. After calling this function, the region can be
 * used for allocations.
 *
 * Return: NULL or a struct on success, PTR_ERR on failure.
 */
struct dmem_cgroup_region *dmem_cgroup_register_region(u64 size, const char *fmt, ...)
{
	struct dmem_cgroup_region *ret;
	char *region_name;
	va_list ap;

	if (!size)
		return NULL;

	va_start(ap, fmt);
	region_name = kvasprintf(GFP_KERNEL, fmt, ap);
	va_end(ap);
	if (!region_name)
		return ERR_PTR(-ENOMEM);

	ret = kzalloc(sizeof(*ret), GFP_KERNEL);
	if (!ret) {
		kfree(region_name);
		return ERR_PTR(-ENOMEM);
	}

	INIT_LIST_HEAD(&ret->pools);
	ret->name = region_name;
	ret->size = size;
	kref_init(&ret->ref);

	spin_lock(&dmemcg_lock);
	list_add_tail_rcu(&ret->region_node, &dmem_cgroup_regions);
	spin_unlock(&dmemcg_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(dmem_cgroup_register_region);

static struct dmem_cgroup_region *dmemcg_get_region_by_name(const char *name)
{
	struct dmem_cgroup_region *region;

	list_for_each_entry_rcu(region, &dmem_cgroup_regions, region_node, spin_is_locked(&dmemcg_lock))
		if (!strcmp(name, region->name) &&
		    kref_get_unless_zero(&region->ref))
			return region;

	return NULL;
}

/**
 * dmem_cgroup_pool_state_put() - Drop a reference to a dmem_cgroup_pool_state
 * @pool: &dmem_cgroup_pool_state
 *
 * Called to drop a reference to the limiting pool returned by
 * dmem_cgroup_try_charge().
 */
void dmem_cgroup_pool_state_put(struct dmem_cgroup_pool_state *pool)
{
	if (pool)
		css_put(&pool->cs->css);
}
EXPORT_SYMBOL_GPL(dmem_cgroup_pool_state_put);

static struct dmem_cgroup_pool_state *
get_cg_pool_unlocked(struct dmemcg_state *cg, struct dmem_cgroup_region *region)
{
	struct dmem_cgroup_pool_state *pool, *allocpool = NULL;

	/* fastpath lookup? */
	rcu_read_lock();
	pool = find_cg_pool_locked(cg, region);
	if (pool && !READ_ONCE(pool->inited))
		pool = NULL;
	rcu_read_unlock();

	while (!pool) {
		spin_lock(&dmemcg_lock);
		if (!region->unregistered)
			pool = get_cg_pool_locked(cg, region, &allocpool);
		else
			pool = ERR_PTR(-ENODEV);
		spin_unlock(&dmemcg_lock);

		if (pool == ERR_PTR(-ENOMEM)) {
			pool = NULL;
			if (WARN_ON(allocpool))
				continue;

			allocpool = kzalloc(sizeof(*allocpool), GFP_KERNEL);
			if (allocpool) {
				pool = NULL;
				continue;
			}
		}
	}

	kfree(allocpool);
	return pool;
}

/**
 * dmem_cgroup_uncharge() - Uncharge a pool.
 * @pool: Pool to uncharge.
 * @size: Size to uncharge.
 *
 * Undoes the effects of dmem_cgroup_try_charge.
 * Must be called with the returned pool as argument,
 * and same @index and @size.
 */
void dmem_cgroup_uncharge(struct dmem_cgroup_pool_state *pool, u64 size)
{
	if (!pool)
		return;

	page_counter_uncharge(&pool->cnt, size);
	css_put(&pool->cs->css);
}
EXPORT_SYMBOL_GPL(dmem_cgroup_uncharge);

/**
 * dmem_cgroup_try_charge() - Try charging a new allocation to a region.
 * @region: dmem region to charge
 * @size: Size (in bytes) to charge.
 * @ret_pool: On succesfull allocation, the pool that is charged.
 * @ret_limit_pool: On a failed allocation, the limiting pool.
 *
 * This function charges the @region region for a size of @size bytes.
 *
 * If the function succeeds, @ret_pool is set, which must be passed to
 * dmem_cgroup_uncharge() when undoing the allocation.
 *
 * When this function fails with -EAGAIN and @ret_limit_pool is non-null, it
 * will be set to the pool for which the limit is hit. This can be used for
 * eviction as argument to dmem_cgroup_evict_valuable(). This reference must be freed
 * with @dmem_cgroup_pool_state_put().
 *
 * Return: 0 on success, -EAGAIN on hitting a limit, or a negative errno on failure.
 */
int dmem_cgroup_try_charge(struct dmem_cgroup_region *region, u64 size,
			  struct dmem_cgroup_pool_state **ret_pool,
			  struct dmem_cgroup_pool_state **ret_limit_pool)
{
	struct dmemcg_state *cg;
	struct dmem_cgroup_pool_state *pool;
	struct page_counter *fail;
	int ret;

	*ret_pool = NULL;
	if (ret_limit_pool)
		*ret_limit_pool = NULL;

	/*
	 * hold on to css, as cgroup can be removed but resource
	 * accounting happens on css.
	 */
	cg = get_current_dmemcs();

	pool = get_cg_pool_unlocked(cg, region);
	if (IS_ERR(pool)) {
		ret = PTR_ERR(pool);
		goto err;
	}

	if (!page_counter_try_charge(&pool->cnt, size, &fail)) {
		if (ret_limit_pool) {
			*ret_limit_pool = container_of(fail, struct dmem_cgroup_pool_state, cnt);
			css_get(&(*ret_limit_pool)->cs->css);
		}
		ret = -EAGAIN;
		goto err;
	}

	/* On success, reference from get_current_dmemcs is transferred to *ret_pool */
	*ret_pool = pool;
	return 0;

err:
	css_put(&cg->css);
	return ret;
}
EXPORT_SYMBOL_GPL(dmem_cgroup_try_charge);

static int dmem_cgroup_region_capacity_show(struct seq_file *sf, void *v)
{
	struct dmem_cgroup_region *region;

	rcu_read_lock();
	list_for_each_entry_rcu(region, &dmem_cgroup_regions, region_node) {
		seq_puts(sf, region->name);
		seq_printf(sf, " %llu\n", region->size);
	}
	rcu_read_unlock();
	return 0;
}

static int dmemcg_parse_limit(char *options, struct dmem_cgroup_region *region,
			      u64 *new_limit)
{
	char *end;

	if (!strcmp(options, "max")) {
		*new_limit = PAGE_COUNTER_MAX;
		return 0;
	}

	*new_limit = memparse(options, &end);
	if (*end != '\0')
		return -EINVAL;

	return 0;
}

static ssize_t dmemcg_limit_write(struct kernfs_open_file *of,
				 char *buf, size_t nbytes, loff_t off,
				 void (*apply)(struct dmem_cgroup_pool_state *, u64))
{
	struct dmemcg_state *dmemcs = css_to_dmemcs(of_css(of));
	int err = 0;

	while (buf && !err) {
		struct dmem_cgroup_pool_state *pool = NULL;
		char *options, *region_name;
		struct dmem_cgroup_region *region;
		u64 new_limit;

		options = buf;
		buf = strchr(buf, '\n');
		if (buf)
			*buf++ = '\0';

		options = strstrip(options);

		/* eat empty lines */
		if (!options[0])
			continue;

		region_name = strsep(&options, " \t");
		if (!region_name[0])
			continue;

		rcu_read_lock();
		region = dmemcg_get_region_by_name(region_name);
		rcu_read_unlock();

		if (!region)
			return -EINVAL;

		err = dmemcg_parse_limit(options, region, &new_limit);
		if (err < 0)
			goto out_put;

		pool = get_cg_pool_unlocked(dmemcs, region);
		if (IS_ERR(pool)) {
			err = PTR_ERR(pool);
			goto out_put;
		}

		/* And commit */
		apply(pool, new_limit);

out_put:
		kref_put(&region->ref, dmemcg_free_region);
	}


	return err ?: nbytes;
}

static int dmemcg_limit_show(struct seq_file *sf, void *v,
			    u64 (*fn)(struct dmem_cgroup_pool_state *))
{
	struct dmemcg_state *dmemcs = css_to_dmemcs(seq_css(sf));
	struct dmem_cgroup_region *region;

	rcu_read_lock();
	list_for_each_entry_rcu(region, &dmem_cgroup_regions, region_node) {
		struct dmem_cgroup_pool_state *pool = find_cg_pool_locked(dmemcs, region);
		u64 val;

		seq_puts(sf, region->name);

		val = fn(pool);
		if (val < PAGE_COUNTER_MAX)
			seq_printf(sf, " %lld\n", val);
		else
			seq_puts(sf, " max\n");
	}
	rcu_read_unlock();

	return 0;
}

static int dmem_cgroup_region_current_show(struct seq_file *sf, void *v)
{
	return dmemcg_limit_show(sf, v, get_resource_current);
}

static int dmem_cgroup_region_min_show(struct seq_file *sf, void *v)
{
	return dmemcg_limit_show(sf, v, get_resource_min);
}

static ssize_t dmem_cgroup_region_min_write(struct kernfs_open_file *of,
				      char *buf, size_t nbytes, loff_t off)
{
	return dmemcg_limit_write(of, buf, nbytes, off, set_resource_min);
}

static int dmem_cgroup_region_low_show(struct seq_file *sf, void *v)
{
	return dmemcg_limit_show(sf, v, get_resource_low);
}

static ssize_t dmem_cgroup_region_low_write(struct kernfs_open_file *of,
				      char *buf, size_t nbytes, loff_t off)
{
	return dmemcg_limit_write(of, buf, nbytes, off, set_resource_low);
}

static int dmem_cgroup_region_max_show(struct seq_file *sf, void *v)
{
	return dmemcg_limit_show(sf, v, get_resource_max);
}

static ssize_t dmem_cgroup_region_max_write(struct kernfs_open_file *of,
				      char *buf, size_t nbytes, loff_t off)
{
	return dmemcg_limit_write(of, buf, nbytes, off, set_resource_max);
}

static struct cftype files[] = {
	{
		.name = "capacity",
		.seq_show = dmem_cgroup_region_capacity_show,
		.flags = CFTYPE_ONLY_ON_ROOT,
	},
	{
		.name = "current",
		.seq_show = dmem_cgroup_region_current_show,
	},
	{
		.name = "min",
		.write = dmem_cgroup_region_min_write,
		.seq_show = dmem_cgroup_region_min_show,
		.flags = CFTYPE_NOT_ON_ROOT,
	},
	{
		.name = "low",
		.write = dmem_cgroup_region_low_write,
		.seq_show = dmem_cgroup_region_low_show,
		.flags = CFTYPE_NOT_ON_ROOT,
	},
	{
		.name = "max",
		.write = dmem_cgroup_region_max_write,
		.seq_show = dmem_cgroup_region_max_show,
		.flags = CFTYPE_NOT_ON_ROOT,
	},
	{ } /* Zero entry terminates. */
};

struct cgroup_subsys dmem_cgrp_subsys = {
	.css_alloc	= dmemcs_alloc,
	.css_free	= dmemcs_free,
	.css_offline	= dmemcs_offline,
	.legacy_cftypes	= files,
	.dfl_cftypes	= files,
};
