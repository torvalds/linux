/*
 * zpool memory storage api
 *
 * Copyright (C) 2014 Dan Streetman
 *
 * This is a common frontend for memory storage pool implementations.
 * Typically, this is used to store compressed memory.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/list.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/zpool.h>

struct zpool {
	char *type;

	struct zpool_driver *driver;
	void *pool;
	struct zpool_ops *ops;

	struct list_head list;
};

static LIST_HEAD(drivers_head);
static DEFINE_SPINLOCK(drivers_lock);

static LIST_HEAD(pools_head);
static DEFINE_SPINLOCK(pools_lock);

/**
 * zpool_register_driver() - register a zpool implementation.
 * @driver:	driver to register
 */
void zpool_register_driver(struct zpool_driver *driver)
{
	spin_lock(&drivers_lock);
	atomic_set(&driver->refcount, 0);
	list_add(&driver->list, &drivers_head);
	spin_unlock(&drivers_lock);
}
EXPORT_SYMBOL(zpool_register_driver);

/**
 * zpool_unregister_driver() - unregister a zpool implementation.
 * @driver:	driver to unregister.
 *
 * Module usage counting is used to prevent using a driver
 * while/after unloading, so if this is called from module
 * exit function, this should never fail; if called from
 * other than the module exit function, and this returns
 * failure, the driver is in use and must remain available.
 */
int zpool_unregister_driver(struct zpool_driver *driver)
{
	int ret = 0, refcount;

	spin_lock(&drivers_lock);
	refcount = atomic_read(&driver->refcount);
	WARN_ON(refcount < 0);
	if (refcount > 0)
		ret = -EBUSY;
	else
		list_del(&driver->list);
	spin_unlock(&drivers_lock);

	return ret;
}
EXPORT_SYMBOL(zpool_unregister_driver);

/**
 * zpool_evict() - evict callback from a zpool implementation.
 * @pool:	pool to evict from.
 * @handle:	handle to evict.
 *
 * This can be used by zpool implementations to call the
 * user's evict zpool_ops struct evict callback.
 */
int zpool_evict(void *pool, unsigned long handle)
{
	struct zpool *zpool;

	spin_lock(&pools_lock);
	list_for_each_entry(zpool, &pools_head, list) {
		if (zpool->pool == pool) {
			spin_unlock(&pools_lock);
			if (!zpool->ops || !zpool->ops->evict)
				return -EINVAL;
			return zpool->ops->evict(zpool, handle);
		}
	}
	spin_unlock(&pools_lock);

	return -ENOENT;
}
EXPORT_SYMBOL(zpool_evict);

static struct zpool_driver *zpool_get_driver(char *type)
{
	struct zpool_driver *driver;

	spin_lock(&drivers_lock);
	list_for_each_entry(driver, &drivers_head, list) {
		if (!strcmp(driver->type, type)) {
			bool got = try_module_get(driver->owner);

			if (got)
				atomic_inc(&driver->refcount);
			spin_unlock(&drivers_lock);
			return got ? driver : NULL;
		}
	}

	spin_unlock(&drivers_lock);
	return NULL;
}

static void zpool_put_driver(struct zpool_driver *driver)
{
	atomic_dec(&driver->refcount);
	module_put(driver->owner);
}

/**
 * zpool_create_pool() - Create a new zpool
 * @type	The type of the zpool to create (e.g. zbud, zsmalloc)
 * @name	The name of the zpool (e.g. zram0, zswap)
 * @gfp		The GFP flags to use when allocating the pool.
 * @ops		The optional ops callback.
 *
 * This creates a new zpool of the specified type.  The gfp flags will be
 * used when allocating memory, if the implementation supports it.  If the
 * ops param is NULL, then the created zpool will not be shrinkable.
 *
 * Implementations must guarantee this to be thread-safe.
 *
 * Returns: New zpool on success, NULL on failure.
 */
struct zpool *zpool_create_pool(char *type, char *name, gfp_t gfp,
		struct zpool_ops *ops)
{
	struct zpool_driver *driver;
	struct zpool *zpool;

	pr_info("creating pool type %s\n", type);

	driver = zpool_get_driver(type);

	if (!driver) {
		request_module("zpool-%s", type);
		driver = zpool_get_driver(type);
	}

	if (!driver) {
		pr_err("no driver for type %s\n", type);
		return NULL;
	}

	zpool = kmalloc(sizeof(*zpool), gfp);
	if (!zpool) {
		pr_err("couldn't create zpool - out of memory\n");
		zpool_put_driver(driver);
		return NULL;
	}

	zpool->type = driver->type;
	zpool->driver = driver;
	zpool->pool = driver->create(name, gfp, ops);
	zpool->ops = ops;

	if (!zpool->pool) {
		pr_err("couldn't create %s pool\n", type);
		zpool_put_driver(driver);
		kfree(zpool);
		return NULL;
	}

	pr_info("created %s pool\n", type);

	spin_lock(&pools_lock);
	list_add(&zpool->list, &pools_head);
	spin_unlock(&pools_lock);

	return zpool;
}

/**
 * zpool_destroy_pool() - Destroy a zpool
 * @pool	The zpool to destroy.
 *
 * Implementations must guarantee this to be thread-safe,
 * however only when destroying different pools.  The same
 * pool should only be destroyed once, and should not be used
 * after it is destroyed.
 *
 * This destroys an existing zpool.  The zpool should not be in use.
 */
void zpool_destroy_pool(struct zpool *zpool)
{
	pr_info("destroying pool type %s\n", zpool->type);

	spin_lock(&pools_lock);
	list_del(&zpool->list);
	spin_unlock(&pools_lock);
	zpool->driver->destroy(zpool->pool);
	zpool_put_driver(zpool->driver);
	kfree(zpool);
}

/**
 * zpool_get_type() - Get the type of the zpool
 * @pool	The zpool to check
 *
 * This returns the type of the pool.
 *
 * Implementations must guarantee this to be thread-safe.
 *
 * Returns: The type of zpool.
 */
char *zpool_get_type(struct zpool *zpool)
{
	return zpool->type;
}

/**
 * zpool_malloc() - Allocate memory
 * @pool	The zpool to allocate from.
 * @size	The amount of memory to allocate.
 * @gfp		The GFP flags to use when allocating memory.
 * @handle	Pointer to the handle to set
 *
 * This allocates the requested amount of memory from the pool.
 * The gfp flags will be used when allocating memory, if the
 * implementation supports it.  The provided @handle will be
 * set to the allocated object handle.
 *
 * Implementations must guarantee this to be thread-safe.
 *
 * Returns: 0 on success, negative value on error.
 */
int zpool_malloc(struct zpool *zpool, size_t size, gfp_t gfp,
			unsigned long *handle)
{
	return zpool->driver->malloc(zpool->pool, size, gfp, handle);
}

/**
 * zpool_free() - Free previously allocated memory
 * @pool	The zpool that allocated the memory.
 * @handle	The handle to the memory to free.
 *
 * This frees previously allocated memory.  This does not guarantee
 * that the pool will actually free memory, only that the memory
 * in the pool will become available for use by the pool.
 *
 * Implementations must guarantee this to be thread-safe,
 * however only when freeing different handles.  The same
 * handle should only be freed once, and should not be used
 * after freeing.
 */
void zpool_free(struct zpool *zpool, unsigned long handle)
{
	zpool->driver->free(zpool->pool, handle);
}

/**
 * zpool_shrink() - Shrink the pool size
 * @pool	The zpool to shrink.
 * @pages	The number of pages to shrink the pool.
 * @reclaimed	The number of pages successfully evicted.
 *
 * This attempts to shrink the actual memory size of the pool
 * by evicting currently used handle(s).  If the pool was
 * created with no zpool_ops, or the evict call fails for any
 * of the handles, this will fail.  If non-NULL, the @reclaimed
 * parameter will be set to the number of pages reclaimed,
 * which may be more than the number of pages requested.
 *
 * Implementations must guarantee this to be thread-safe.
 *
 * Returns: 0 on success, negative value on error/failure.
 */
int zpool_shrink(struct zpool *zpool, unsigned int pages,
			unsigned int *reclaimed)
{
	return zpool->driver->shrink(zpool->pool, pages, reclaimed);
}

/**
 * zpool_map_handle() - Map a previously allocated handle into memory
 * @pool	The zpool that the handle was allocated from
 * @handle	The handle to map
 * @mm		How the memory should be mapped
 *
 * This maps a previously allocated handle into memory.  The @mm
 * param indicates to the implementation how the memory will be
 * used, i.e. read-only, write-only, read-write.  If the
 * implementation does not support it, the memory will be treated
 * as read-write.
 *
 * This may hold locks, disable interrupts, and/or preemption,
 * and the zpool_unmap_handle() must be called to undo those
 * actions.  The code that uses the mapped handle should complete
 * its operatons on the mapped handle memory quickly and unmap
 * as soon as possible.  As the implementation may use per-cpu
 * data, multiple handles should not be mapped concurrently on
 * any cpu.
 *
 * Returns: A pointer to the handle's mapped memory area.
 */
void *zpool_map_handle(struct zpool *zpool, unsigned long handle,
			enum zpool_mapmode mapmode)
{
	return zpool->driver->map(zpool->pool, handle, mapmode);
}

/**
 * zpool_unmap_handle() - Unmap a previously mapped handle
 * @pool	The zpool that the handle was allocated from
 * @handle	The handle to unmap
 *
 * This unmaps a previously mapped handle.  Any locks or other
 * actions that the implementation took in zpool_map_handle()
 * will be undone here.  The memory area returned from
 * zpool_map_handle() should no longer be used after this.
 */
void zpool_unmap_handle(struct zpool *zpool, unsigned long handle)
{
	zpool->driver->unmap(zpool->pool, handle);
}

/**
 * zpool_get_total_size() - The total size of the pool
 * @pool	The zpool to check
 *
 * This returns the total size in bytes of the pool.
 *
 * Returns: Total size of the zpool in bytes.
 */
u64 zpool_get_total_size(struct zpool *zpool)
{
	return zpool->driver->total_size(zpool->pool);
}

static int __init init_zpool(void)
{
	pr_info("loaded\n");
	return 0;
}

static void __exit exit_zpool(void)
{
	pr_info("unloaded\n");
}

module_init(init_zpool);
module_exit(exit_zpool);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dan Streetman <ddstreet@ieee.org>");
MODULE_DESCRIPTION("Common API for compressed memory storage");
