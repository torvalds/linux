/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * async.h: Asynchronous function calls for boot performance
 *
 * (C) Copyright 2009 Intel Corporation
 * Author: Arjan van de Ven <arjan@linux.intel.com>
 */
#ifndef __ASYNC_H__
#define __ASYNC_H__

#include <linux/types.h>
#include <linux/list.h>
#include <linux/numa.h>
#include <linux/device.h>

typedef u64 async_cookie_t;
typedef void (*async_func_t) (void *data, async_cookie_t cookie);
struct async_domain {
	struct list_head pending;
	unsigned registered:1;
};

/*
 * domain participates in global async_synchronize_full
 */
#define ASYNC_DOMAIN(_name) \
	struct async_domain _name = { .pending = LIST_HEAD_INIT(_name.pending),	\
				      .registered = 1 }

/*
 * domain is free to go out of scope as soon as all pending work is
 * complete, this domain does not participate in async_synchronize_full
 */
#define ASYNC_DOMAIN_EXCLUSIVE(_name) \
	struct async_domain _name = { .pending = LIST_HEAD_INIT(_name.pending), \
				      .registered = 0 }

async_cookie_t async_schedule_node(async_func_t func, void *data,
				   int node);
async_cookie_t async_schedule_node_domain(async_func_t func, void *data,
					  int node,
					  struct async_domain *domain);

/**
 * async_schedule - schedule a function for asynchronous execution
 * @func: function to execute asynchronously
 * @data: data pointer to pass to the function
 *
 * Returns an async_cookie_t that may be used for checkpointing later.
 * Note: This function may be called from atomic or non-atomic contexts.
 */
static inline async_cookie_t async_schedule(async_func_t func, void *data)
{
	return async_schedule_node(func, data, NUMA_NO_NODE);
}

/**
 * async_schedule_domain - schedule a function for asynchronous execution within a certain domain
 * @func: function to execute asynchronously
 * @data: data pointer to pass to the function
 * @domain: the domain
 *
 * Returns an async_cookie_t that may be used for checkpointing later.
 * @domain may be used in the async_synchronize_*_domain() functions to
 * wait within a certain synchronization domain rather than globally.
 * Note: This function may be called from atomic or non-atomic contexts.
 */
static inline async_cookie_t
async_schedule_domain(async_func_t func, void *data,
		      struct async_domain *domain)
{
	return async_schedule_node_domain(func, data, NUMA_NO_NODE, domain);
}

/**
 * async_schedule_dev - A device specific version of async_schedule
 * @func: function to execute asynchronously
 * @dev: device argument to be passed to function
 *
 * Returns an async_cookie_t that may be used for checkpointing later.
 * @dev is used as both the argument for the function and to provide NUMA
 * context for where to run the function. By doing this we can try to
 * provide for the best possible outcome by operating on the device on the
 * CPUs closest to the device.
 * Note: This function may be called from atomic or non-atomic contexts.
 */
static inline async_cookie_t
async_schedule_dev(async_func_t func, struct device *dev)
{
	return async_schedule_node(func, dev, dev_to_node(dev));
}

bool async_schedule_dev_nocall(async_func_t func, struct device *dev);

/**
 * async_schedule_dev_domain - A device specific version of async_schedule_domain
 * @func: function to execute asynchronously
 * @dev: device argument to be passed to function
 * @domain: the domain
 *
 * Returns an async_cookie_t that may be used for checkpointing later.
 * @dev is used as both the argument for the function and to provide NUMA
 * context for where to run the function. By doing this we can try to
 * provide for the best possible outcome by operating on the device on the
 * CPUs closest to the device.
 * @domain may be used in the async_synchronize_*_domain() functions to
 * wait within a certain synchronization domain rather than globally.
 * Note: This function may be called from atomic or non-atomic contexts.
 */
static inline async_cookie_t
async_schedule_dev_domain(async_func_t func, struct device *dev,
			  struct async_domain *domain)
{
	return async_schedule_node_domain(func, dev, dev_to_node(dev), domain);
}

extern void async_synchronize_full(void);
extern void async_synchronize_full_domain(struct async_domain *domain);
extern void async_synchronize_cookie(async_cookie_t cookie);
extern void async_synchronize_cookie_domain(async_cookie_t cookie,
					    struct async_domain *domain);
extern bool current_is_async(void);
#endif
