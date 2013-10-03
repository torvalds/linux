/*
 * async.h: Asynchronous function calls for boot performance
 *
 * (C) Copyright 2009 Intel Corporation
 * Author: Arjan van de Ven <arjan@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */
#ifndef __ASYNC_H__
#define __ASYNC_H__

#include <linux/types.h>
#include <linux/list.h>

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

extern async_cookie_t async_schedule(async_func_t func, void *data);
extern async_cookie_t async_schedule_domain(async_func_t func, void *data,
					    struct async_domain *domain);
void async_unregister_domain(struct async_domain *domain);
extern void async_synchronize_full(void);
extern void async_synchronize_full_domain(struct async_domain *domain);
extern void async_synchronize_cookie(async_cookie_t cookie);
extern void async_synchronize_cookie_domain(async_cookie_t cookie,
					    struct async_domain *domain);
extern bool current_is_async(void);
#endif
