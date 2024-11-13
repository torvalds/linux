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
 * On failure, a negative errno is returned.
 */
int rpcrdma_rn_register(struct ib_device *device,
			struct rpcrdma_notification *rn,
			void (*done)(struct rpcrdma_notification *rn))
{
	struct rpcrdma_device *rd = rpcrdma_get_client_data(device);

	if (!rd || test_bit(RPCRDMA_RD_F_REMOVING, &rd->rd_flags))
		return -ENETUNREACH;

	if (xa_alloc(&rd->rd_xa, &rn->rn_index, rn, xa_limit_32b, GFP_KERNEL) < 0)
		return -ENOMEM;
	kref_get(&rd->rd_kref);
	rn->rn_done = done;
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
 */
void rpcrdma_rn_unregister(struct ib_device *device,
			   struct rpcrdma_notification *rn)
{
	struct rpcrdma_device *rd = rpcrdma_get_client_data(device);

	if (!rd)
		return;

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

	rd = kzalloc(sizeof(*rd), GFP_KERNEL);
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
