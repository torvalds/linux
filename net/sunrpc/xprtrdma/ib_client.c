// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (c) 2024 Oracle.  All rights reserved.
 */

/* #include <linux/module.h>
#include <linux/slab.h> */
#include <linux/xarray.h>
#include <linux/types.h>
#include <linux/kref.h>
#include <linux/completion.h>

#include <linux/sunrpc/svc_rdma.h>
#include <linux/sunrpc/rdma_rn.h>

#include "xprt_rdma.h"
#include <trace/events/rpcrdma.h>

/* Per-ib_device private data for rpcrdma */
struct rpcrdma_device {
	struct kref		rd_kref;
	unsigned long		rd_flags;
	struct ib_device	*rd_device;
	struct xarray		rd_xa;
	struct completion	rd_done;
};

#define RPCRDMA_RD_F_REMOVING	(0)

static struct ib_client rpcrdma_ib_client;

/*
 * Listeners have no associated device, so we never register them.
 * Note that ib_get_client_data() does not check if @device is
 * NULL for us.
 */
static struct rpcrdma_device *rpcrdma_get_client_data(struct ib_device *device)
{
	if (!device)
		return NULL;
	return ib_get_client_data(device, &rpcrdma_ib_client);
}

/**
 * rpcrdma_rn_register - register to get device removal notifications
 * @device: device to monitor
 * @rn: notification object that wishes to be notified
 * @done: callback to notify caller of device removal
 *
 * Returns zero on success. The callback in rn_done is guaranteed
 * to be invoked when the device is removed, unless this notification
 * is unregistered first.
 *
 * On failure, a negative errno is returned. rn->rn_done is left
 * NULL on every failure path (it is armed before xa_alloc but
 * cleared again if xa_alloc fails), so the @rn may safely be
 * passed to rpcrdma_rn_unregister() without a separate
 * registered/unregistered flag in the caller.
 */
int rpcrdma_rn_register(struct ib_device *device,
			struct rpcrdma_notification *rn,
			void (*done)(struct rpcrdma_notification *rn))
{
	struct rpcrdma_device *rd = rpcrdma_get_client_data(device);

	if (!rd || test_bit(RPCRDMA_RD_F_REMOVING, &rd->rd_flags))
		return -ENETUNREACH;

	/*
	 * Arm rn_done before xa_alloc() publishes @rn: once @rn is
	 * visible in rd_xa, a concurrent rpcrdma_remove_one() can
	 * call rn->rn_done(), so the pointer must already be set.
	 *
	 * Restore NULL if xa_alloc() fails. rn_done doubles as the
	 * registration sentinel for rpcrdma_rn_unregister(); a stale
	 * value would unregister an @rn that was never inserted.
	 */
	rn->rn_done = done;
	if (xa_alloc(&rd->rd_xa, &rn->rn_index, rn, xa_limit_32b, GFP_KERNEL) < 0) {
		rn->rn_done = NULL;
		return -ENOMEM;
	}
	kref_get(&rd->rd_kref);
	trace_rpcrdma_client_register(device, rn);
	return 0;
}

static void rpcrdma_rn_release(struct kref *kref)
{
	struct rpcrdma_device *rd = container_of(kref, struct rpcrdma_device,
						 rd_kref);

	trace_rpcrdma_client_completion(rd->rd_device);
	complete(&rd->rd_done);
}

/**
 * rpcrdma_rn_unregister - stop device removal notifications
 * @device: monitored device
 * @rn: notification object that no longer wishes to be notified
 *
 * It is safe to call this on an @rn whose registration never
 * completed or failed; rn_done == NULL is treated as
 * never-registered and the call is a no-op.
 */
void rpcrdma_rn_unregister(struct ib_device *device,
			   struct rpcrdma_notification *rn)
{
	struct rpcrdma_device *rd = rpcrdma_get_client_data(device);

	if (!rd)
		return;

	/*
	 * rn_done is the registration sentinel: rpcrdma_rn_register
	 * leaves it NULL on every failure path, clearing it again if
	 * xa_alloc fails, so a non-NULL rn_done marks a completed
	 * registration. A NULL rn_done means this notification was
	 * never registered (or its registration failed) or has
	 * already been unregistered, and the call is a no-op.
	 * Without this guard, rn_index == 0 from a kzalloc'd
	 * parent would erase another caller's slot 0 and underflow
	 * rd_kref.
	 */
	if (!rn->rn_done)
		return;
	rn->rn_done = NULL;

	trace_rpcrdma_client_unregister(device, rn);
	xa_erase(&rd->rd_xa, rn->rn_index);
	kref_put(&rd->rd_kref, rpcrdma_rn_release);
}

/**
 * rpcrdma_add_one - ib_client device insertion callback
 * @device: device about to be inserted
 *
 * Returns zero on success. xprtrdma private data has been allocated
 * for this device. On failure, a negative errno is returned.
 */
static int rpcrdma_add_one(struct ib_device *device)
{
	struct rpcrdma_device *rd;

	rd = kzalloc_obj(*rd);
	if (!rd)
		return -ENOMEM;

	kref_init(&rd->rd_kref);
	xa_init_flags(&rd->rd_xa, XA_FLAGS_ALLOC);
	rd->rd_device = device;
	init_completion(&rd->rd_done);
	ib_set_client_data(device, &rpcrdma_ib_client, rd);

	trace_rpcrdma_client_add_one(device);
	return 0;
}

/**
 * rpcrdma_remove_one - ib_client device removal callback
 * @device: device about to be removed
 * @client_data: this module's private per-device data
 *
 * Upon return, all transports associated with @device have divested
 * themselves from IB hardware resources.
 */
static void rpcrdma_remove_one(struct ib_device *device,
			       void *client_data)
{
	struct rpcrdma_device *rd = client_data;
	struct rpcrdma_notification *rn;
	unsigned long index;

	trace_rpcrdma_client_remove_one(device);

	set_bit(RPCRDMA_RD_F_REMOVING, &rd->rd_flags);
	xa_for_each(&rd->rd_xa, index, rn)
		rn->rn_done(rn);

	/*
	 * Wait only if there are still outstanding notification
	 * registrants for this device.
	 */
	if (!refcount_dec_and_test(&rd->rd_kref.refcount)) {
		trace_rpcrdma_client_wait_on(device);
		wait_for_completion(&rd->rd_done);
	}

	trace_rpcrdma_client_remove_one_done(device);
	xa_destroy(&rd->rd_xa);
	kfree(rd);
}

static struct ib_client rpcrdma_ib_client = {
	.name		= "rpcrdma",
	.add		= rpcrdma_add_one,
	.remove		= rpcrdma_remove_one,
};

/**
 * rpcrdma_ib_client_unregister - unregister ib_client for xprtrdma
 *
 * cel: watch for orphaned rpcrdma_device objects on module unload
 */
void rpcrdma_ib_client_unregister(void)
{
	ib_unregister_client(&rpcrdma_ib_client);
}

/**
 * rpcrdma_ib_client_register - register ib_client for rpcrdma
 *
 * Returns zero on success, or a negative errno.
 */
int rpcrdma_ib_client_register(void)
{
	return ib_register_client(&rpcrdma_ib_client);
}
