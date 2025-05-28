/* SPDX-License-Identifier: GPL-2.0 */
/*
 * zpool memory storage api
 *
 * Copyright (C) 2014 Dan Streetman
 *
 * This is a common frontend for the zswap compressed memory storage
 * implementations.
 */

#ifndef _ZPOOL_H_
#define _ZPOOL_H_

struct zpool;

bool zpool_has_pool(char *type);

struct zpool *zpool_create_pool(const char *type, const char *name, gfp_t gfp);

const char *zpool_get_type(struct zpool *pool);

void zpool_destroy_pool(struct zpool *pool);

int zpool_malloc(struct zpool *pool, size_t size, gfp_t gfp,
			unsigned long *handle);

void zpool_free(struct zpool *pool, unsigned long handle);

void *zpool_obj_read_begin(struct zpool *zpool, unsigned long handle,
			   void *local_copy);

void zpool_obj_read_end(struct zpool *zpool, unsigned long handle,
			void *handle_mem);

void zpool_obj_write(struct zpool *zpool, unsigned long handle,
		     void *handle_mem, size_t mem_len);

u64 zpool_get_total_pages(struct zpool *pool);


/**
 * struct zpool_driver - driver implementation for zpool
 * @type:	name of the driver.
 * @list:	entry in the list of zpool drivers.
 * @create:	create a new pool.
 * @destroy:	destroy a pool.
 * @malloc:	allocate mem from a pool.
 * @free:	free mem from a pool.
 * @sleep_mapped: whether zpool driver can sleep during map.
 * @map:	map a handle.
 * @unmap:	unmap a handle.
 * @total_size:	get total size of a pool.
 *
 * This is created by a zpool implementation and registered
 * with zpool.
 */
struct zpool_driver {
	char *type;
	struct module *owner;
	atomic_t refcount;
	struct list_head list;

	void *(*create)(const char *name, gfp_t gfp);
	void (*destroy)(void *pool);

	int (*malloc)(void *pool, size_t size, gfp_t gfp,
				unsigned long *handle);
	void (*free)(void *pool, unsigned long handle);

	void *(*obj_read_begin)(void *pool, unsigned long handle,
				void *local_copy);
	void (*obj_read_end)(void *pool, unsigned long handle,
			     void *handle_mem);
	void (*obj_write)(void *pool, unsigned long handle,
			  void *handle_mem, size_t mem_len);

	u64 (*total_pages)(void *pool);
};

void zpool_register_driver(struct zpool_driver *driver);

int zpool_unregister_driver(struct zpool_driver *driver);

bool zpool_can_sleep_mapped(struct zpool *pool);

#endif
