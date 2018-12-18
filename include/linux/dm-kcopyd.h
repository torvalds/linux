/*
 * Copyright (C) 2001 - 2003 Sistina Software
 * Copyright (C) 2004 - 2008 Red Hat, Inc. All rights reserved.
 *
 * kcopyd provides a simple interface for copying an area of one
 * block-device to one or more other block-devices, either synchronous
 * or with an asynchronous completion notification.
 *
 * This file is released under the GPL.
 */

#ifndef _LINUX_DM_KCOPYD_H
#define _LINUX_DM_KCOPYD_H

#ifdef __KERNEL__

#include <linux/dm-io.h>

/* FIXME: make this configurable */
#define DM_KCOPYD_MAX_REGIONS 8

#define DM_KCOPYD_IGNORE_ERROR 1
#define DM_KCOPYD_WRITE_SEQ    2

struct dm_kcopyd_throttle {
	unsigned throttle;
	unsigned num_io_jobs;
	unsigned io_period;
	unsigned total_period;
	unsigned last_jiffies;
};

/*
 * kcopyd clients that want to support throttling must pass an initialised
 * dm_kcopyd_throttle struct into dm_kcopyd_client_create().
 * Two or more clients may share the same instance of this struct between
 * them if they wish to be throttled as a group.
 *
 * This macro also creates a corresponding module parameter to configure
 * the amount of throttling.
 */
#define DECLARE_DM_KCOPYD_THROTTLE_WITH_MODULE_PARM(name, description)	\
static struct dm_kcopyd_throttle dm_kcopyd_throttle = { 100, 0, 0, 0, 0 }; \
module_param_named(name, dm_kcopyd_throttle.throttle, uint, 0644); \
MODULE_PARM_DESC(name, description)

/*
 * To use kcopyd you must first create a dm_kcopyd_client object.
 * throttle can be NULL if you don't want any throttling.
 */
struct dm_kcopyd_client;
struct dm_kcopyd_client *dm_kcopyd_client_create(struct dm_kcopyd_throttle *throttle);
void dm_kcopyd_client_destroy(struct dm_kcopyd_client *kc);

/*
 * Submit a copy job to kcopyd.  This is built on top of the
 * previous three fns.
 *
 * read_err is a boolean,
 * write_err is a bitset, with 1 bit for each destination region
 */
typedef void (*dm_kcopyd_notify_fn)(int read_err, unsigned long write_err,
				    void *context);

void dm_kcopyd_copy(struct dm_kcopyd_client *kc, struct dm_io_region *from,
		    unsigned num_dests, struct dm_io_region *dests,
		    unsigned flags, dm_kcopyd_notify_fn fn, void *context);

/*
 * Prepare a callback and submit it via the kcopyd thread.
 *
 * dm_kcopyd_prepare_callback allocates a callback structure and returns it.
 * It must not be called from interrupt context.
 * The returned value should be passed into dm_kcopyd_do_callback.
 *
 * dm_kcopyd_do_callback submits the callback.
 * It may be called from interrupt context.
 * The callback is issued from the kcopyd thread.
 */
void *dm_kcopyd_prepare_callback(struct dm_kcopyd_client *kc,
				 dm_kcopyd_notify_fn fn, void *context);
void dm_kcopyd_do_callback(void *job, int read_err, unsigned long write_err);

void dm_kcopyd_zero(struct dm_kcopyd_client *kc,
		    unsigned num_dests, struct dm_io_region *dests,
		    unsigned flags, dm_kcopyd_notify_fn fn, void *context);

#endif	/* __KERNEL__ */
#endif	/* _LINUX_DM_KCOPYD_H */
