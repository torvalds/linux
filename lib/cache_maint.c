// SPDX-License-Identifier: GPL-2.0
/*
 * Generic support for Memory System Cache Maintenance operations.
 *
 * Coherency maintenance drivers register with this simple framework that will
 * iterate over each registered instance to first kick off invalidation and
 * then to wait until it is complete.
 *
 * If no implementations are registered yet cpu_cache_has_invalidate_memregion()
 * will return false. If this runs concurrently with unregistration then a
 * race exists but this is no worse than the case where the operations instance
 * responsible for a given memory region has not yet registered.
 */
#include <linux/cache_coherency.h>
#include <linux/cleanup.h>
#include <linux/container_of.h>
#include <linux/export.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/memregion.h>
#include <linux/module.h>
#include <linux/rwsem.h>
#include <linux/slab.h>

static LIST_HEAD(cache_ops_instance_list);
static DECLARE_RWSEM(cache_ops_instance_list_lock);

static void __cache_coherency_ops_instance_free(struct kref *kref)
{
	struct cache_coherency_ops_inst *cci =
		container_of(kref, struct cache_coherency_ops_inst, kref);
	kfree(cci);
}

void cache_coherency_ops_instance_put(struct cache_coherency_ops_inst *cci)
{
	kref_put(&cci->kref, __cache_coherency_ops_instance_free);
}
EXPORT_SYMBOL_GPL(cache_coherency_ops_instance_put);

static int cache_inval_one(struct cache_coherency_ops_inst *cci, void *data)
{
	if (!cci->ops)
		return -EINVAL;

	return cci->ops->wbinv(cci, data);
}

static int cache_inval_done_one(struct cache_coherency_ops_inst *cci)
{
	if (!cci->ops)
		return -EINVAL;

	if (!cci->ops->done)
		return 0;

	return cci->ops->done(cci);
}

static int cache_invalidate_memregion(phys_addr_t addr, size_t size)
{
	int ret;
	struct cache_coherency_ops_inst *cci;
	struct cc_inval_params params = {
		.addr = addr,
		.size = size,
	};

	guard(rwsem_read)(&cache_ops_instance_list_lock);
	list_for_each_entry(cci, &cache_ops_instance_list, node) {
		ret = cache_inval_one(cci, &params);
		if (ret)
			return ret;
	}
	list_for_each_entry(cci, &cache_ops_instance_list, node) {
		ret = cache_inval_done_one(cci);
		if (ret)
			return ret;
	}

	return 0;
}

struct cache_coherency_ops_inst *
_cache_coherency_ops_instance_alloc(const struct cache_coherency_ops *ops,
				    size_t size)
{
	struct cache_coherency_ops_inst *cci;

	if (!ops || !ops->wbinv)
		return NULL;

	cci = kzalloc(size, GFP_KERNEL);
	if (!cci)
		return NULL;

	cci->ops = ops;
	INIT_LIST_HEAD(&cci->node);
	kref_init(&cci->kref);

	return cci;
}
EXPORT_SYMBOL_NS_GPL(_cache_coherency_ops_instance_alloc, "CACHE_COHERENCY");

int cache_coherency_ops_instance_register(struct cache_coherency_ops_inst *cci)
{
	guard(rwsem_write)(&cache_ops_instance_list_lock);
	list_add(&cci->node, &cache_ops_instance_list);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cache_coherency_ops_instance_register, "CACHE_COHERENCY");

void cache_coherency_ops_instance_unregister(struct cache_coherency_ops_inst *cci)
{
	guard(rwsem_write)(&cache_ops_instance_list_lock);
	list_del(&cci->node);
}
EXPORT_SYMBOL_NS_GPL(cache_coherency_ops_instance_unregister, "CACHE_COHERENCY");

int cpu_cache_invalidate_memregion(phys_addr_t start, size_t len)
{
	return cache_invalidate_memregion(start, len);
}
EXPORT_SYMBOL_NS_GPL(cpu_cache_invalidate_memregion, "DEVMEM");

/*
 * Used for optimization / debug purposes only as removal can race
 *
 * Machines that do not support invalidation, e.g. VMs, will not have any
 * operations instance to register and so this will always return false.
 */
bool cpu_cache_has_invalidate_memregion(void)
{
	guard(rwsem_read)(&cache_ops_instance_list_lock);
	return !list_empty(&cache_ops_instance_list);
}
EXPORT_SYMBOL_NS_GPL(cpu_cache_has_invalidate_memregion, "DEVMEM");
