// SPDX-License-Identifier: GPL-2.0-only
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
	struct zpool_driver *driver;
	void *pool;
};

static LIST_HEAD(drivers_head);
static DEFINE_SPINLOCK(drivers_lock);

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

/* this assumes @type is null-terminated. */
static struct zpool_driver *zpool_get_driver(const char *type)
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
 * zpool_has_pool() - Check if the pool driver is available
 * @type:	The type of the zpool to check (e.g. zsmalloc)
 *
 * This checks if the @type pool driver is available.  This will try to load
 * the requested module, if needed, but there is no guarantee the module will
 * still be loaded and available immediately after calling.  If this returns
 * true, the caller should assume the pool is available, but must be prepared
 * to handle the @zpool_create_pool() returning failure.  However if this
 * returns false, the caller should assume the requested pool type is not
 * available; either the requested pool type module does not exist, or could
 * not be loaded, and calling @zpool_create_pool() with the pool type will
 * fail.
 *
 * The @type string must be null-terminated.
 *
 * Returns: true if @type pool is available, false if not
 */
bool zpool_has_pool(char *type)
{
	struct zpool_driver *driver = zpool_get_driver(type);

	if (!driver) {
		request_module("zpool-%s", type);
		driver = zpool_get_driver(type);
	}

	if (!driver)
		return false;

	zpool_put_driver(driver);
	return true;
}
EXPORT_SYMBOL(zpool_has_pool);

/**
 * zpool_create_pool() - Create a new zpool
 * @type:	The type of the zpool to create (e.g. zsmalloc)
 * @name:	The name of the zpool (e.g. zram0, zswap)
 * @gfp:	The GFP flags to use when allocating the pool.
 *
 * This creates a new zpool of the specified type.  The gfp flags will be
 * used when allocating memory, if the implementation supports it.  If the
 * ops param is NULL, then the created zpool will not be evictable.
 *
 * Implementations must guarantee this to be thread-safe.
 *
 * The @type and @name strings must be null-terminated.
 *
 * Returns: New zpool on success, NULL on failure.
 */
struct zpool *zpool_create_pool(const char *type, const char *name, gfp_t gfp)
{
	struct zpool_driver *driver;
	struct zpool *zpool;

	pr_debug("creating pool type %s\n", type);

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

	zpool->driver = driver;
	zpool->pool = driver->create(name, gfp);

	if (!zpool->pool) {
		pr_err("couldn't create %s pool\n", type);
		zpool_put_driver(driver);
		kfree(zpool);
		return NULL;
	}

	pr_debug("created pool type %s\n", type);

	return zpool;
}

/**
 * zpool_destroy_pool() - Destroy a zpool
 * @zpool:	The zpool to destroy.
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
	pr_debug("destroying pool type %s\n", zpool->driver->type);

	zpool->driver->destroy(zpool->pool);
	zpool_put_driver(zpool->driver);
	kfree(zpool);
}

/**
 * zpool_get_type() - Get the type of the zpool
 * @zpool:	The zpool to check
 *
 * This returns the type of the pool.
 *
 * Implementations must guarantee this to be thread-safe.
 *
 * Returns: The type of zpool.
 */
const char *zpool_get_type(struct zpool *zpool)
{
	return zpool->driver->type;
}

/**
 * zpool_malloc_support_movable() - Check if the zpool supports
 *	allocating movable memory
 * @zpool:	The zpool to check
 *
 * This returns if the zpool supports allocating movable memory.
 *
 * Implementations must guarantee this to be thread-safe.
 *
 * Returns: true if the zpool supports allocating movable memory, false if not
 */
bool zpool_malloc_support_movable(struct zpool *zpool)
{
	return zpool->driver->malloc_support_movable;
}

/**
 * zpool_malloc() - Allocate memory
 * @zpool:	The zpool to allocate from.
 * @size:	The amount of memory to allocate.
 * @gfp:	The GFP flags to use when allocating memory.
 * @handle:	Pointer to the handle to set
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
 * @zpool:	The zpool that allocated the memory.
 * @handle:	The handle to the memory to free.
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
 * zpool_map_handle() - Map a previously allocated handle into memory
 * @zpool:	The zpool that the handle was allocated from
 * @handle:	The handle to map
 * @mapmode:	How the memory should be mapped
 *
 * This maps a previously allocated handle into memory.  The @mapmode
 * param indicates to the implementation how the memory will be
 * used, i.e. read-only, write-only, read-write.  If the
 * implementation does not support it, the memory will be treated
 * as read-write.
 *
 * This may hold locks, disable interrupts, and/or preemption,
 * and the zpool_unmap_handle() must be called to undo those
 * actions.  The code that uses the mapped handle should complete
 * its operations on the mapped handle memory quickly and unmap
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
 * @zpool:	The zpool that the handle was allocated from
 * @handle:	The handle to unmap
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
 * zpool_obj_read_begin() - Start reading from a previously allocated handle.
 * @zpool:	The zpool that the handle was allocated from
 * @handle:	The handle to read from
 * @local_copy:	A local buffer to use if needed.
 *
 * This starts a read operation of a previously allocated handle. The passed
 * @local_copy buffer may be used if needed by copying the memory into.
 * zpool_obj_read_end() MUST be called after the read is completed to undo any
 * actions taken (e.g. release locks).
 *
 * Returns: A pointer to the handle memory to be read, if @local_copy is used,
 * the returned pointer is @local_copy.
 */
void *zpool_obj_read_begin(struct zpool *zpool, unsigned long handle,
			   void *local_copy)
{
	return zpool->driver->obj_read_begin(zpool->pool, handle, local_copy);
}

/**
 * zpool_obj_read_end() - Finish reading from a previously allocated handle.
 * @zpool:	The zpool that the handle was allocated from
 * @handle:	The handle to read from
 * @handle_mem:	The pointer returned by zpool_obj_read_begin()
 *
 * Finishes a read operation previously started by zpool_obj_read_begin().
 */
void zpool_obj_read_end(struct zpool *zpool, unsigned long handle,
			void *handle_mem)
{
	zpool->driver->obj_read_end(zpool->pool, handle, handle_mem);
}

/**
 * zpool_obj_write() - Write to a previously allocated handle.
 * @zpool:	The zpool that the handle was allocated from
 * @handle:	The handle to read from
 * @handle_mem:	The memory to copy from into the handle.
 * @mem_len:	The length of memory to be written.
 *
 */
void zpool_obj_write(struct zpool *zpool, unsigned long handle,
		     void *handle_mem, size_t mem_len)
{
	zpool->driver->obj_write(zpool->pool, handle, handle_mem, mem_len);
}

/**
 * zpool_get_total_pages() - The total size of the pool
 * @zpool:	The zpool to check
 *
 * This returns the total size in pages of the pool.
 *
 * Returns: Total size of the zpool in pages.
 */
u64 zpool_get_total_pages(struct zpool *zpool)
{
	return zpool->driver->total_pages(zpool->pool);
}

/**
 * zpool_can_sleep_mapped - Test if zpool can sleep when do mapped.
 * @zpool:	The zpool to test
 *
 * Some allocators enter non-preemptible context in ->map() callback (e.g.
 * disable pagefaults) and exit that context in ->unmap(), which limits what
 * we can do with the mapped object. For instance, we cannot wait for
 * asynchronous crypto API to decompress such an object or take mutexes
 * since those will call into the scheduler. This function tells us whether
 * we use such an allocator.
 *
 * Returns: true if zpool can sleep; false otherwise.
 */
bool zpool_can_sleep_mapped(struct zpool *zpool)
{
	return zpool->driver->sleep_mapped;
}

MODULE_AUTHOR("Dan Streetman <ddstreet@ieee.org>");
MODULE_DESCRIPTION("Common API for compressed memory storage");
