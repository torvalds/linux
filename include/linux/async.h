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

#include <linux/types.h>
#include <linux/list.h>

typedef u64 async_cookie_t;
typedef void (async_func_ptr) (void *data, async_cookie_t cookie);

extern async_cookie_t async_schedule(async_func_ptr *ptr, void *data);
extern async_cookie_t async_schedule_special(async_func_ptr *ptr, void *data, struct list_head *list);
extern void async_synchronize_full(void);
extern void async_synchronize_full_special(struct list_head *list);
extern void async_synchronize_cookie(async_cookie_t cookie);
extern void async_synchronize_cookie_special(async_cookie_t cookie, struct list_head *list);

