// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/net/sunrpc/xprt.c
 *
 *  This is a generic RPC call interface supporting congestion avoidance,
 *  and asynchronous calls.
 *
 *  The interface works like this:
 *
 *  -	When a process places a call, it allocates a request slot if
 *	one is available. Otherwise, it sleeps on the backlog queue
 *	(xprt_reserve).
 *  -	Next, the caller puts together the RPC message, stuffs it into
 *	the request struct, and calls xprt_transmit().
 *  -	xprt_transmit sends the message and installs the caller on the
 *	transport's wait list. At the same time, if a reply is expected,
 *	it installs a timer that is run after the packet's timeout has
 *	expired.
 *  -	When a packet arrives, the data_ready handler walks the list of
 *	pending requests for that transport. If a matching XID is found, the
 *	caller is woken up, and the timer removed.
 *  -	When no reply arrives within the timeout interval, the timer is
 *	fired by the kernel and runs xprt_timer(). It either adjusts the
 *	timeout values (minor timeout) or wakes up the caller with a status
 *	of -ETIMEDOUT.
 *  -	When the caller receives a notification from RPC that a reply arrived,
 *	it should release the RPC slot, and process the reply.
 *	If the call timed out, it may choose to retry the operation by
 *	adjusting the initial timeout value, and simply calling rpc_call
 *	again.
 *
 *  Support for async RPC is done through a set of RPC-specific scheduling
 *  primitives that `transparently' work for processes as well as async
 *  tasks that rely on callbacks.
 *
 *  Copyright (C) 1995-1997, Olaf Kirch <okir@monad.swb.de>
 *
 *  Transport switch API copyright (C) 2005, Chuck Lever <cel@netapp.com>
 */

#include <linux/module.h>

#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/net.h>
#include <linux/ktime.h>

#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/metrics.h>
#include <linux/sunrpc/bc_xprt.h>
#include <linux/rcupdate.h>
#include <linux/sched/mm.h>

#include <trace/events/sunrpc.h>

#include "sunrpc.h"
#include "sysfs.h"
#include "fail.h"

/*
 * Local variables
 */

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
# define RPCDBG_FACILITY	RPCDBG_XPRT
#endif

/*
 * Local functions
 */
static void	 xprt_init(struct rpc_xprt *xprt, struct net *net);
static __be32	xprt_alloc_xid(struct rpc_xprt *xprt);
static void	 xprt_destroy(struct rpc_xprt *xprt);
static void	 xprt_request_init(struct rpc_task *task);

static DEFINE_SPINLOCK(xprt_list_lock);
static LIST_HEAD(xprt_list);

static unsigned long xprt_request_timeout(const struct rpc_rqst *req)
{
	unsigned long timeout = jiffies + req->rq_timeout;

	if (time_before(timeout, req->rq_majortimeo))
		return timeout;
	return req->rq_majortimeo;
}

/**
 * xprt_register_transport - register a transport implementation
 * @transport: transport to register
 *
 * If a transport implementation is loaded as a kernel module, it can
 * call this interface to make itself known to the RPC client.
 *
 * Returns:
 * 0:		transport successfully registered
 * -EEXIST:	transport already registered
 * -EINVAL:	transport module being unloaded
 */
int xprt_register_transport(struct xprt_class *transport)
{
	struct xprt_class *t;
	int result;

	result = -EEXIST;
	spin_lock(&xprt_list_lock);
	list_for_each_entry(t, &xprt_list, list) {
		/* don't register the same transport class twice */
		if (t->ident == transport->ident)
			goto out;
	}

	list_add_tail(&transport->list, &xprt_list);
	printk(KERN_INFO "RPC: Registered %s transport module.\n",
	       transport->name);
	result = 0;

out:
	spin_unlock(&xprt_list_lock);
	return result;
}
EXPORT_SYMBOL_GPL(xprt_register_transport);

/**
 * xprt_unregister_transport - unregister a transport implementation
 * @transport: transport to unregister
 *
 * Returns:
 * 0:		transport successfully unregistered
 * -ENOENT:	transport never registered
 */
int xprt_unregister_transport(struct xprt_class *transport)
{
	struct xprt_class *t;
	int result;

	result = 0;
	spin_lock(&xprt_list_lock);
	list_for_each_entry(t, &xprt_list, list) {
		if (t == transport) {
			printk(KERN_INFO
				"RPC: Unregistered %s transport module.\n",
				transport->name);
			list_del_init(&transport->list);
			goto out;
		}
	}
	result = -ENOENT;

out:
	spin_unlock(&xprt_list_lock);
	return result;
}
EXPORT_SYMBOL_GPL(xprt_unregister_transport);

static void
xprt_class_release(const struct xprt_class *t)
{
	module_put(t->owner);
}

static const struct xprt_class *
xprt_class_find_by_ident_locked(int ident)
{
	const struct xprt_class *t;

	list_for_each_entry(t, &xprt_list, list) {
		if (t->ident != ident)
			continue;
		if (!try_module_get(t->owner))
			continue;
		return t;
	}
	return NULL;
}

static const struct xprt_class *
xprt_class_find_by_ident(int ident)
{
	const struct xprt_class *t;

	spin_lock(&xprt_list_lock);
	t = xprt_class_find_by_ident_locked(ident);
	spin_unlock(&xprt_list_lock);
	return t;
}

static const struct xprt_class *
xprt_class_find_by_netid_locked(const char *netid)
{
	const struct xprt_class *t;
	unsigned int i;

	list_for_each_entry(t, &xprt_list, list) {
		for (i = 0; t->netid[i][0] != '\0'; i++) {
			if (strcmp(t->netid[i], netid) != 0)
				continue;
			if (!try_module_get(t->owner))
				continue;
			return t;
		}
	}
	return NULL;
}

static const struct xprt_class *
xprt_class_find_by_netid(const char *netid)
{
	const struct xprt_class *t;

	spin_lock(&xprt_list_lock);
	t = xprt_class_find_by_netid_locked(netid);
	if (!t) {
		spin_unlock(&xprt_list_lock);
		request_module("rpc%s", netid);
		spin_lock(&xprt_list_lock);
		t = xprt_class_find_by_netid_locked(netid);
	}
	spin_unlock(&xprt_list_lock);
	return t;
}

/**
 * xprt_find_transport_ident - convert a netid into a transport identifier
 * @netid: transport to load
 *
 * Returns:
 * > 0:		transport identifier
 * -ENOENT:	transport module not available
 */
int xprt_find_transport_ident(const char *netid)
{
	const struct xprt_class *t;
	int ret;

	t = xprt_class_find_by_netid(netid);
	if (!t)
		return -ENOENT;
	ret = t->ident;
	xprt_class_release(t);
	return ret;
}
EXPORT_SYMBOL_GPL(xprt_find_transport_ident);

static void xprt_clear_locked(struct rpc_xprt *xprt)
{
	xprt->snd_task = NULL;
	if (!test_bit(XPRT_CLOSE_WAIT, &xprt->state)) {
		smp_mb__before_atomic();
		clear_bit(XPRT_LOCKED, &xprt->state);
		smp_mb__after_atomic();
	} else
		queue_work(xprtiod_workqueue, &xprt->task_cleanup);
}

/**
 * xprt_reserve_xprt - serialize write access to transports
 * @task: task that is requesting access to the transport
 * @xprt: pointer to the target transport
 *
 * This prevents mixing the payload of separate requests, and prevents
 * transport connects from colliding with writes.  No congestion control
 * is provided.
 */
int xprt_reserve_xprt(struct rpc_xprt *xprt, struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;

	if (test_and_set_bit(XPRT_LOCKED, &xprt->state)) {
		if (task == xprt->snd_task)
			goto out_locked;
		goto out_sleep;
	}
	if (test_bit(XPRT_WRITE_SPACE, &xprt->state))
		goto out_unlock;
	xprt->snd_task = task;

out_locked:
	trace_xprt_reserve_xprt(xprt, task);
	return 1;

out_unlock:
	xprt_clear_locked(xprt);
out_sleep:
	task->tk_status = -EAGAIN;
	if  (RPC_IS_SOFT(task))
		rpc_sleep_on_timeout(&xprt->sending, task, NULL,
				xprt_request_timeout(req));
	else
		rpc_sleep_on(&xprt->sending, task, NULL);
	return 0;
}
EXPORT_SYMBOL_GPL(xprt_reserve_xprt);

static bool
xprt_need_congestion_window_wait(struct rpc_xprt *xprt)
{
	return test_bit(XPRT_CWND_WAIT, &xprt->state);
}

static void
xprt_set_congestion_window_wait(struct rpc_xprt *xprt)
{
	if (!list_empty(&xprt->xmit_queue)) {
		/* Peek at head of queue to see if it can make progress */
		if (list_first_entry(&xprt->xmit_queue, struct rpc_rqst,
					rq_xmit)->rq_cong)
			return;
	}
	set_bit(XPRT_CWND_WAIT, &xprt->state);
}

static void
xprt_test_and_clear_congestion_window_wait(struct rpc_xprt *xprt)
{
	if (!RPCXPRT_CONGESTED(xprt))
		clear_bit(XPRT_CWND_WAIT, &xprt->state);
}

/*
 * xprt_reserve_xprt_cong - serialize write access to transports
 * @task: task that is requesting access to the transport
 *
 * Same as xprt_reserve_xprt, but Van Jacobson congestion control is
 * integrated into the decision of whether a request is allowed to be
 * woken up and given access to the transport.
 * Note that the lock is only granted if we know there are free slots.
 */
int xprt_reserve_xprt_cong(struct rpc_xprt *xprt, struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;

	if (test_and_set_bit(XPRT_LOCKED, &xprt->state)) {
		if (task == xprt->snd_task)
			goto out_locked;
		goto out_sleep;
	}
	if (req == NULL) {
		xprt->snd_task = task;
		goto out_locked;
	}
	if (test_bit(XPRT_WRITE_SPACE, &xprt->state))
		goto out_unlock;
	if (!xprt_need_congestion_window_wait(xprt)) {
		xprt->snd_task = task;
		goto out_locked;
	}
out_unlock:
	xprt_clear_locked(xprt);
out_sleep:
	task->tk_status = -EAGAIN;
	if (RPC_IS_SOFT(task))
		rpc_sleep_on_timeout(&xprt->sending, task, NULL,
				xprt_request_timeout(req));
	else
		rpc_sleep_on(&xprt->sending, task, NULL);
	return 0;
out_locked:
	trace_xprt_reserve_cong(xprt, task);
	return 1;
}
EXPORT_SYMBOL_GPL(xprt_reserve_xprt_cong);

static inline int xprt_lock_write(struct rpc_xprt *xprt, struct rpc_task *task)
{
	int retval;

	if (test_bit(XPRT_LOCKED, &xprt->state) && xprt->snd_task == task)
		return 1;
	spin_lock(&xprt->transport_lock);
	retval = xprt->ops->reserve_xprt(xprt, task);
	spin_unlock(&xprt->transport_lock);
	return retval;
}

static bool __xprt_lock_write_func(struct rpc_task *task, void *data)
{
	struct rpc_xprt *xprt = data;

	xprt->snd_task = task;
	return true;
}

static void __xprt_lock_write_next(struct rpc_xprt *xprt)
{
	if (test_and_set_bit(XPRT_LOCKED, &xprt->state))
		return;
	if (test_bit(XPRT_WRITE_SPACE, &xprt->state))
		goto out_unlock;
	if (rpc_wake_up_first_on_wq(xprtiod_workqueue, &xprt->sending,
				__xprt_lock_write_func, xprt))
		return;
out_unlock:
	xprt_clear_locked(xprt);
}

static void __xprt_lock_write_next_cong(struct rpc_xprt *xprt)
{
	if (test_and_set_bit(XPRT_LOCKED, &xprt->state))
		return;
	if (test_bit(XPRT_WRITE_SPACE, &xprt->state))
		goto out_unlock;
	if (xprt_need_congestion_window_wait(xprt))
		goto out_unlock;
	if (rpc_wake_up_first_on_wq(xprtiod_workqueue, &xprt->sending,
				__xprt_lock_write_func, xprt))
		return;
out_unlock:
	xprt_clear_locked(xprt);
}

/**
 * xprt_release_xprt - allow other requests to use a transport
 * @xprt: transport with other tasks potentially waiting
 * @task: task that is releasing access to the transport
 *
 * Note that "task" can be NULL.  No congestion control is provided.
 */
void xprt_release_xprt(struct rpc_xprt *xprt, struct rpc_task *task)
{
	if (xprt->snd_task == task) {
		xprt_clear_locked(xprt);
		__xprt_lock_write_next(xprt);
	}
	trace_xprt_release_xprt(xprt, task);
}
EXPORT_SYMBOL_GPL(xprt_release_xprt);

/**
 * xprt_release_xprt_cong - allow other requests to use a transport
 * @xprt: transport with other tasks potentially waiting
 * @task: task that is releasing access to the transport
 *
 * Note that "task" can be NULL.  Another task is awoken to use the
 * transport if the transport's congestion window allows it.
 */
void xprt_release_xprt_cong(struct rpc_xprt *xprt, struct rpc_task *task)
{
	if (xprt->snd_task == task) {
		xprt_clear_locked(xprt);
		__xprt_lock_write_next_cong(xprt);
	}
	trace_xprt_release_cong(xprt, task);
}
EXPORT_SYMBOL_GPL(xprt_release_xprt_cong);

void xprt_release_write(struct rpc_xprt *xprt, struct rpc_task *task)
{
	if (xprt->snd_task != task)
		return;
	spin_lock(&xprt->transport_lock);
	xprt->ops->release_xprt(xprt, task);
	spin_unlock(&xprt->transport_lock);
}

/*
 * Van Jacobson congestion avoidance. Check if the congestion window
 * overflowed. Put the task to sleep if this is the case.
 */
static int
__xprt_get_cong(struct rpc_xprt *xprt, struct rpc_rqst *req)
{
	if (req->rq_cong)
		return 1;
	trace_xprt_get_cong(xprt, req->rq_task);
	if (RPCXPRT_CONGESTED(xprt)) {
		xprt_set_congestion_window_wait(xprt);
		return 0;
	}
	req->rq_cong = 1;
	xprt->cong += RPC_CWNDSCALE;
	return 1;
}

/*
 * Adjust the congestion window, and wake up the next task
 * that has been sleeping due to congestion
 */
static void
__xprt_put_cong(struct rpc_xprt *xprt, struct rpc_rqst *req)
{
	if (!req->rq_cong)
		return;
	req->rq_cong = 0;
	xprt->cong -= RPC_CWNDSCALE;
	xprt_test_and_clear_congestion_window_wait(xprt);
	trace_xprt_put_cong(xprt, req->rq_task);
	__xprt_lock_write_next_cong(xprt);
}

/**
 * xprt_request_get_cong - Request congestion control credits
 * @xprt: pointer to transport
 * @req: pointer to RPC request
 *
 * Useful for transports that require congestion control.
 */
bool
xprt_request_get_cong(struct rpc_xprt *xprt, struct rpc_rqst *req)
{
	bool ret = false;

	if (req->rq_cong)
		return true;
	spin_lock(&xprt->transport_lock);
	ret = __xprt_get_cong(xprt, req) != 0;
	spin_unlock(&xprt->transport_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(xprt_request_get_cong);

/**
 * xprt_release_rqst_cong - housekeeping when request is complete
 * @task: RPC request that recently completed
 *
 * Useful for transports that require congestion control.
 */
void xprt_release_rqst_cong(struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;

	__xprt_put_cong(req->rq_xprt, req);
}
EXPORT_SYMBOL_GPL(xprt_release_rqst_cong);

static void xprt_clear_congestion_window_wait_locked(struct rpc_xprt *xprt)
{
	if (test_and_clear_bit(XPRT_CWND_WAIT, &xprt->state))
		__xprt_lock_write_next_cong(xprt);
}

/*
 * Clear the congestion window wait flag and wake up the next
 * entry on xprt->sending
 */
static void
xprt_clear_congestion_window_wait(struct rpc_xprt *xprt)
{
	if (test_and_clear_bit(XPRT_CWND_WAIT, &xprt->state)) {
		spin_lock(&xprt->transport_lock);
		__xprt_lock_write_next_cong(xprt);
		spin_unlock(&xprt->transport_lock);
	}
}

/**
 * xprt_adjust_cwnd - adjust transport congestion window
 * @xprt: pointer to xprt
 * @task: recently completed RPC request used to adjust window
 * @result: result code of completed RPC request
 *
 * The transport code maintains an estimate on the maximum number of out-
 * standing RPC requests, using a smoothed version of the congestion
 * avoidance implemented in 44BSD. This is basically the Van Jacobson
 * congestion algorithm: If a retransmit occurs, the congestion window is
 * halved; otherwise, it is incremented by 1/cwnd when
 *
 *	-	a reply is received and
 *	-	a full number of requests are outstanding and
 *	-	the congestion window hasn't been updated recently.
 */
void xprt_adjust_cwnd(struct rpc_xprt *xprt, struct rpc_task *task, int result)
{
	struct rpc_rqst *req = task->tk_rqstp;
	unsigned long cwnd = xprt->cwnd;

	if (result >= 0 && cwnd <= xprt->cong) {
		/* The (cwnd >> 1) term makes sure
		 * the result gets rounded properly. */
		cwnd += (RPC_CWNDSCALE * RPC_CWNDSCALE + (cwnd >> 1)) / cwnd;
		if (cwnd > RPC_MAXCWND(xprt))
			cwnd = RPC_MAXCWND(xprt);
		__xprt_lock_write_next_cong(xprt);
	} else if (result == -ETIMEDOUT) {
		cwnd >>= 1;
		if (cwnd < RPC_CWNDSCALE)
			cwnd = RPC_CWNDSCALE;
	}
	dprintk("RPC:       cong %ld, cwnd was %ld, now %ld\n",
			xprt->cong, xprt->cwnd, cwnd);
	xprt->cwnd = cwnd;
	__xprt_put_cong(xprt, req);
}
EXPORT_SYMBOL_GPL(xprt_adjust_cwnd);

/**
 * xprt_wake_pending_tasks - wake all tasks on a transport's pending queue
 * @xprt: transport with waiting tasks
 * @status: result code to plant in each task before waking it
 *
 */
void xprt_wake_pending_tasks(struct rpc_xprt *xprt, int status)
{
	if (status < 0)
		rpc_wake_up_status(&xprt->pending, status);
	else
		rpc_wake_up(&xprt->pending);
}
EXPORT_SYMBOL_GPL(xprt_wake_pending_tasks);

/**
 * xprt_wait_for_buffer_space - wait for transport output buffer to clear
 * @xprt: transport
 *
 * Note that we only set the timer for the case of RPC_IS_SOFT(), since
 * we don't in general want to force a socket disconnection due to
 * an incomplete RPC call transmission.
 */
void xprt_wait_for_buffer_space(struct rpc_xprt *xprt)
{
	set_bit(XPRT_WRITE_SPACE, &xprt->state);
}
EXPORT_SYMBOL_GPL(xprt_wait_for_buffer_space);

static bool
xprt_clear_write_space_locked(struct rpc_xprt *xprt)
{
	if (test_and_clear_bit(XPRT_WRITE_SPACE, &xprt->state)) {
		__xprt_lock_write_next(xprt);
		dprintk("RPC:       write space: waking waiting task on "
				"xprt %p\n", xprt);
		return true;
	}
	return false;
}

/**
 * xprt_write_space - wake the task waiting for transport output buffer space
 * @xprt: transport with waiting tasks
 *
 * Can be called in a soft IRQ context, so xprt_write_space never sleeps.
 */
bool xprt_write_space(struct rpc_xprt *xprt)
{
	bool ret;

	if (!test_bit(XPRT_WRITE_SPACE, &xprt->state))
		return false;
	spin_lock(&xprt->transport_lock);
	ret = xprt_clear_write_space_locked(xprt);
	spin_unlock(&xprt->transport_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(xprt_write_space);

static unsigned long xprt_abs_ktime_to_jiffies(ktime_t abstime)
{
	s64 delta = ktime_to_ns(ktime_get() - abstime);
	return likely(delta >= 0) ?
		jiffies - nsecs_to_jiffies(delta) :
		jiffies + nsecs_to_jiffies(-delta);
}

static unsigned long xprt_calc_majortimeo(struct rpc_rqst *req)
{
	const struct rpc_timeout *to = req->rq_task->tk_client->cl_timeout;
	unsigned long majortimeo = req->rq_timeout;

	if (to->to_exponential)
		majortimeo <<= to->to_retries;
	else
		majortimeo += to->to_increment * to->to_retries;
	if (majortimeo > to->to_maxval || majortimeo == 0)
		majortimeo = to->to_maxval;
	return majortimeo;
}

static void xprt_reset_majortimeo(struct rpc_rqst *req)
{
	req->rq_majortimeo += xprt_calc_majortimeo(req);
}

static void xprt_reset_minortimeo(struct rpc_rqst *req)
{
	req->rq_minortimeo += req->rq_timeout;
}

static void xprt_init_majortimeo(struct rpc_task *task, struct rpc_rqst *req)
{
	unsigned long time_init;
	struct rpc_xprt *xprt = req->rq_xprt;

	if (likely(xprt && xprt_connected(xprt)))
		time_init = jiffies;
	else
		time_init = xprt_abs_ktime_to_jiffies(task->tk_start);
	req->rq_timeout = task->tk_client->cl_timeout->to_initval;
	req->rq_majortimeo = time_init + xprt_calc_majortimeo(req);
	req->rq_minortimeo = time_init + req->rq_timeout;
}

/**
 * xprt_adjust_timeout - adjust timeout values for next retransmit
 * @req: RPC request containing parameters to use for the adjustment
 *
 */
int xprt_adjust_timeout(struct rpc_rqst *req)
{
	struct rpc_xprt *xprt = req->rq_xprt;
	const struct rpc_timeout *to = req->rq_task->tk_client->cl_timeout;
	int status = 0;

	if (time_before(jiffies, req->rq_majortimeo)) {
		if (time_before(jiffies, req->rq_minortimeo))
			return status;
		if (to->to_exponential)
			req->rq_timeout <<= 1;
		else
			req->rq_timeout += to->to_increment;
		if (to->to_maxval && req->rq_timeout >= to->to_maxval)
			req->rq_timeout = to->to_maxval;
		req->rq_retries++;
	} else {
		req->rq_timeout = to->to_initval;
		req->rq_retries = 0;
		xprt_reset_majortimeo(req);
		/* Reset the RTT counters == "slow start" */
		spin_lock(&xprt->transport_lock);
		rpc_init_rtt(req->rq_task->tk_client->cl_rtt, to->to_initval);
		spin_unlock(&xprt->transport_lock);
		status = -ETIMEDOUT;
	}
	xprt_reset_minortimeo(req);

	if (req->rq_timeout == 0) {
		printk(KERN_WARNING "xprt_adjust_timeout: rq_timeout = 0!\n");
		req->rq_timeout = 5 * HZ;
	}
	return status;
}

static void xprt_autoclose(struct work_struct *work)
{
	struct rpc_xprt *xprt =
		container_of(work, struct rpc_xprt, task_cleanup);
	unsigned int pflags = memalloc_nofs_save();

	trace_xprt_disconnect_auto(xprt);
	clear_bit(XPRT_CLOSE_WAIT, &xprt->state);
	xprt->ops->close(xprt);
	xprt_release_write(xprt, NULL);
	wake_up_bit(&xprt->state, XPRT_LOCKED);
	memalloc_nofs_restore(pflags);
}

/**
 * xprt_disconnect_done - mark a transport as disconnected
 * @xprt: transport to flag for disconnect
 *
 */
void xprt_disconnect_done(struct rpc_xprt *xprt)
{
	trace_xprt_disconnect_done(xprt);
	spin_lock(&xprt->transport_lock);
	xprt_clear_connected(xprt);
	xprt_clear_write_space_locked(xprt);
	xprt_clear_congestion_window_wait_locked(xprt);
	xprt_wake_pending_tasks(xprt, -ENOTCONN);
	spin_unlock(&xprt->transport_lock);
}
EXPORT_SYMBOL_GPL(xprt_disconnect_done);

/**
 * xprt_schedule_autoclose_locked - Try to schedule an autoclose RPC call
 * @xprt: transport to disconnect
 */
static void xprt_schedule_autoclose_locked(struct rpc_xprt *xprt)
{
	set_bit(XPRT_CLOSE_WAIT, &xprt->state);
	if (test_and_set_bit(XPRT_LOCKED, &xprt->state) == 0)
		queue_work(xprtiod_workqueue, &xprt->task_cleanup);
	else if (xprt->snd_task && !test_bit(XPRT_SND_IS_COOKIE, &xprt->state))
		rpc_wake_up_queued_task_set_status(&xprt->pending,
						   xprt->snd_task, -ENOTCONN);
}

/**
 * xprt_force_disconnect - force a transport to disconnect
 * @xprt: transport to disconnect
 *
 */
void xprt_force_disconnect(struct rpc_xprt *xprt)
{
	trace_xprt_disconnect_force(xprt);

	/* Don't race with the test_bit() in xprt_clear_locked() */
	spin_lock(&xprt->transport_lock);
	xprt_schedule_autoclose_locked(xprt);
	spin_unlock(&xprt->transport_lock);
}
EXPORT_SYMBOL_GPL(xprt_force_disconnect);

static unsigned int
xprt_connect_cookie(struct rpc_xprt *xprt)
{
	return READ_ONCE(xprt->connect_cookie);
}

static bool
xprt_request_retransmit_after_disconnect(struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_xprt *xprt = req->rq_xprt;

	return req->rq_connect_cookie != xprt_connect_cookie(xprt) ||
		!xprt_connected(xprt);
}

/**
 * xprt_conditional_disconnect - force a transport to disconnect
 * @xprt: transport to disconnect
 * @cookie: 'connection cookie'
 *
 * This attempts to break the connection if and only if 'cookie' matches
 * the current transport 'connection cookie'. It ensures that we don't
 * try to break the connection more than once when we need to retransmit
 * a batch of RPC requests.
 *
 */
void xprt_conditional_disconnect(struct rpc_xprt *xprt, unsigned int cookie)
{
	/* Don't race with the test_bit() in xprt_clear_locked() */
	spin_lock(&xprt->transport_lock);
	if (cookie != xprt->connect_cookie)
		goto out;
	if (test_bit(XPRT_CLOSING, &xprt->state))
		goto out;
	xprt_schedule_autoclose_locked(xprt);
out:
	spin_unlock(&xprt->transport_lock);
}

static bool
xprt_has_timer(const struct rpc_xprt *xprt)
{
	return xprt->idle_timeout != 0;
}

static void
xprt_schedule_autodisconnect(struct rpc_xprt *xprt)
	__must_hold(&xprt->transport_lock)
{
	xprt->last_used = jiffies;
	if (RB_EMPTY_ROOT(&xprt->recv_queue) && xprt_has_timer(xprt))
		mod_timer(&xprt->timer, xprt->last_used + xprt->idle_timeout);
}

static void
xprt_init_autodisconnect(struct timer_list *t)
{
	struct rpc_xprt *xprt = from_timer(xprt, t, timer);

	if (!RB_EMPTY_ROOT(&xprt->recv_queue))
		return;
	/* Reset xprt->last_used to avoid connect/autodisconnect cycling */
	xprt->last_used = jiffies;
	if (test_and_set_bit(XPRT_LOCKED, &xprt->state))
		return;
	queue_work(xprtiod_workqueue, &xprt->task_cleanup);
}

#if IS_ENABLED(CONFIG_FAIL_SUNRPC)
static void xprt_inject_disconnect(struct rpc_xprt *xprt)
{
	if (!fail_sunrpc.ignore_client_disconnect &&
	    should_fail(&fail_sunrpc.attr, 1))
		xprt->ops->inject_disconnect(xprt);
}
#else
static inline void xprt_inject_disconnect(struct rpc_xprt *xprt)
{
}
#endif

bool xprt_lock_connect(struct rpc_xprt *xprt,
		struct rpc_task *task,
		void *cookie)
{
	bool ret = false;

	spin_lock(&xprt->transport_lock);
	if (!test_bit(XPRT_LOCKED, &xprt->state))
		goto out;
	if (xprt->snd_task != task)
		goto out;
	set_bit(XPRT_SND_IS_COOKIE, &xprt->state);
	xprt->snd_task = cookie;
	ret = true;
out:
	spin_unlock(&xprt->transport_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(xprt_lock_connect);

void xprt_unlock_connect(struct rpc_xprt *xprt, void *cookie)
{
	spin_lock(&xprt->transport_lock);
	if (xprt->snd_task != cookie)
		goto out;
	if (!test_bit(XPRT_LOCKED, &xprt->state))
		goto out;
	xprt->snd_task =NULL;
	clear_bit(XPRT_SND_IS_COOKIE, &xprt->state);
	xprt->ops->release_xprt(xprt, NULL);
	xprt_schedule_autodisconnect(xprt);
out:
	spin_unlock(&xprt->transport_lock);
	wake_up_bit(&xprt->state, XPRT_LOCKED);
}
EXPORT_SYMBOL_GPL(xprt_unlock_connect);

/**
 * xprt_connect - schedule a transport connect operation
 * @task: RPC task that is requesting the connect
 *
 */
void xprt_connect(struct rpc_task *task)
{
	struct rpc_xprt	*xprt = task->tk_rqstp->rq_xprt;

	trace_xprt_connect(xprt);

	if (!xprt_bound(xprt)) {
		task->tk_status = -EAGAIN;
		return;
	}
	if (!xprt_lock_write(xprt, task))
		return;

	if (test_and_clear_bit(XPRT_CLOSE_WAIT, &xprt->state)) {
		trace_xprt_disconnect_cleanup(xprt);
		xprt->ops->close(xprt);
	}

	if (!xprt_connected(xprt)) {
		task->tk_rqstp->rq_connect_cookie = xprt->connect_cookie;
		rpc_sleep_on_timeout(&xprt->pending, task, NULL,
				xprt_request_timeout(task->tk_rqstp));

		if (test_bit(XPRT_CLOSING, &xprt->state))
			return;
		if (xprt_test_and_set_connecting(xprt))
			return;
		/* Race breaker */
		if (!xprt_connected(xprt)) {
			xprt->stat.connect_start = jiffies;
			xprt->ops->connect(xprt, task);
		} else {
			xprt_clear_connecting(xprt);
			task->tk_status = 0;
			rpc_wake_up_queued_task(&xprt->pending, task);
		}
	}
	xprt_release_write(xprt, task);
}

/**
 * xprt_reconnect_delay - compute the wait before scheduling a connect
 * @xprt: transport instance
 *
 */
unsigned long xprt_reconnect_delay(const struct rpc_xprt *xprt)
{
	unsigned long start, now = jiffies;

	start = xprt->stat.connect_start + xprt->reestablish_timeout;
	if (time_after(start, now))
		return start - now;
	return 0;
}
EXPORT_SYMBOL_GPL(xprt_reconnect_delay);

/**
 * xprt_reconnect_backoff - compute the new re-establish timeout
 * @xprt: transport instance
 * @init_to: initial reestablish timeout
 *
 */
void xprt_reconnect_backoff(struct rpc_xprt *xprt, unsigned long init_to)
{
	xprt->reestablish_timeout <<= 1;
	if (xprt->reestablish_timeout > xprt->max_reconnect_timeout)
		xprt->reestablish_timeout = xprt->max_reconnect_timeout;
	if (xprt->reestablish_timeout < init_to)
		xprt->reestablish_timeout = init_to;
}
EXPORT_SYMBOL_GPL(xprt_reconnect_backoff);

enum xprt_xid_rb_cmp {
	XID_RB_EQUAL,
	XID_RB_LEFT,
	XID_RB_RIGHT,
};
static enum xprt_xid_rb_cmp
xprt_xid_cmp(__be32 xid1, __be32 xid2)
{
	if (xid1 == xid2)
		return XID_RB_EQUAL;
	if ((__force u32)xid1 < (__force u32)xid2)
		return XID_RB_LEFT;
	return XID_RB_RIGHT;
}

static struct rpc_rqst *
xprt_request_rb_find(struct rpc_xprt *xprt, __be32 xid)
{
	struct rb_node *n = xprt->recv_queue.rb_node;
	struct rpc_rqst *req;

	while (n != NULL) {
		req = rb_entry(n, struct rpc_rqst, rq_recv);
		switch (xprt_xid_cmp(xid, req->rq_xid)) {
		case XID_RB_LEFT:
			n = n->rb_left;
			break;
		case XID_RB_RIGHT:
			n = n->rb_right;
			break;
		case XID_RB_EQUAL:
			return req;
		}
	}
	return NULL;
}

static void
xprt_request_rb_insert(struct rpc_xprt *xprt, struct rpc_rqst *new)
{
	struct rb_node **p = &xprt->recv_queue.rb_node;
	struct rb_node *n = NULL;
	struct rpc_rqst *req;

	while (*p != NULL) {
		n = *p;
		req = rb_entry(n, struct rpc_rqst, rq_recv);
		switch(xprt_xid_cmp(new->rq_xid, req->rq_xid)) {
		case XID_RB_LEFT:
			p = &n->rb_left;
			break;
		case XID_RB_RIGHT:
			p = &n->rb_right;
			break;
		case XID_RB_EQUAL:
			WARN_ON_ONCE(new != req);
			return;
		}
	}
	rb_link_node(&new->rq_recv, n, p);
	rb_insert_color(&new->rq_recv, &xprt->recv_queue);
}

static void
xprt_request_rb_remove(struct rpc_xprt *xprt, struct rpc_rqst *req)
{
	rb_erase(&req->rq_recv, &xprt->recv_queue);
}

/**
 * xprt_lookup_rqst - find an RPC request corresponding to an XID
 * @xprt: transport on which the original request was transmitted
 * @xid: RPC XID of incoming reply
 *
 * Caller holds xprt->queue_lock.
 */
struct rpc_rqst *xprt_lookup_rqst(struct rpc_xprt *xprt, __be32 xid)
{
	struct rpc_rqst *entry;

	entry = xprt_request_rb_find(xprt, xid);
	if (entry != NULL) {
		trace_xprt_lookup_rqst(xprt, xid, 0);
		entry->rq_rtt = ktime_sub(ktime_get(), entry->rq_xtime);
		return entry;
	}

	dprintk("RPC:       xprt_lookup_rqst did not find xid %08x\n",
			ntohl(xid));
	trace_xprt_lookup_rqst(xprt, xid, -ENOENT);
	xprt->stat.bad_xids++;
	return NULL;
}
EXPORT_SYMBOL_GPL(xprt_lookup_rqst);

static bool
xprt_is_pinned_rqst(struct rpc_rqst *req)
{
	return atomic_read(&req->rq_pin) != 0;
}

/**
 * xprt_pin_rqst - Pin a request on the transport receive list
 * @req: Request to pin
 *
 * Caller must ensure this is atomic with the call to xprt_lookup_rqst()
 * so should be holding xprt->queue_lock.
 */
void xprt_pin_rqst(struct rpc_rqst *req)
{
	atomic_inc(&req->rq_pin);
}
EXPORT_SYMBOL_GPL(xprt_pin_rqst);

/**
 * xprt_unpin_rqst - Unpin a request on the transport receive list
 * @req: Request to pin
 *
 * Caller should be holding xprt->queue_lock.
 */
void xprt_unpin_rqst(struct rpc_rqst *req)
{
	if (!test_bit(RPC_TASK_MSG_PIN_WAIT, &req->rq_task->tk_runstate)) {
		atomic_dec(&req->rq_pin);
		return;
	}
	if (atomic_dec_and_test(&req->rq_pin))
		wake_up_var(&req->rq_pin);
}
EXPORT_SYMBOL_GPL(xprt_unpin_rqst);

static void xprt_wait_on_pinned_rqst(struct rpc_rqst *req)
{
	wait_var_event(&req->rq_pin, !xprt_is_pinned_rqst(req));
}

static bool
xprt_request_data_received(struct rpc_task *task)
{
	return !test_bit(RPC_TASK_NEED_RECV, &task->tk_runstate) &&
		READ_ONCE(task->tk_rqstp->rq_reply_bytes_recvd) != 0;
}

static bool
xprt_request_need_enqueue_receive(struct rpc_task *task, struct rpc_rqst *req)
{
	return !test_bit(RPC_TASK_NEED_RECV, &task->tk_runstate) &&
		READ_ONCE(task->tk_rqstp->rq_reply_bytes_recvd) == 0;
}

/**
 * xprt_request_enqueue_receive - Add an request to the receive queue
 * @task: RPC task
 *
 */
void
xprt_request_enqueue_receive(struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_xprt *xprt = req->rq_xprt;

	if (!xprt_request_need_enqueue_receive(task, req))
		return;

	xprt_request_prepare(task->tk_rqstp);
	spin_lock(&xprt->queue_lock);

	/* Update the softirq receive buffer */
	memcpy(&req->rq_private_buf, &req->rq_rcv_buf,
			sizeof(req->rq_private_buf));

	/* Add request to the receive list */
	xprt_request_rb_insert(xprt, req);
	set_bit(RPC_TASK_NEED_RECV, &task->tk_runstate);
	spin_unlock(&xprt->queue_lock);

	/* Turn off autodisconnect */
	del_singleshot_timer_sync(&xprt->timer);
}

/**
 * xprt_request_dequeue_receive_locked - Remove a request from the receive queue
 * @task: RPC task
 *
 * Caller must hold xprt->queue_lock.
 */
static void
xprt_request_dequeue_receive_locked(struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;

	if (test_and_clear_bit(RPC_TASK_NEED_RECV, &task->tk_runstate))
		xprt_request_rb_remove(req->rq_xprt, req);
}

/**
 * xprt_update_rtt - Update RPC RTT statistics
 * @task: RPC request that recently completed
 *
 * Caller holds xprt->queue_lock.
 */
void xprt_update_rtt(struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_rtt *rtt = task->tk_client->cl_rtt;
	unsigned int timer = task->tk_msg.rpc_proc->p_timer;
	long m = usecs_to_jiffies(ktime_to_us(req->rq_rtt));

	if (timer) {
		if (req->rq_ntrans == 1)
			rpc_update_rtt(rtt, timer, m);
		rpc_set_timeo(rtt, timer, req->rq_ntrans - 1);
	}
}
EXPORT_SYMBOL_GPL(xprt_update_rtt);

/**
 * xprt_complete_rqst - called when reply processing is complete
 * @task: RPC request that recently completed
 * @copied: actual number of bytes received from the transport
 *
 * Caller holds xprt->queue_lock.
 */
void xprt_complete_rqst(struct rpc_task *task, int copied)
{
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_xprt *xprt = req->rq_xprt;

	xprt->stat.recvs++;

	req->rq_private_buf.len = copied;
	/* Ensure all writes are done before we update */
	/* req->rq_reply_bytes_recvd */
	smp_wmb();
	req->rq_reply_bytes_recvd = copied;
	xprt_request_dequeue_receive_locked(task);
	rpc_wake_up_queued_task(&xprt->pending, task);
}
EXPORT_SYMBOL_GPL(xprt_complete_rqst);

static void xprt_timer(struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_xprt *xprt = req->rq_xprt;

	if (task->tk_status != -ETIMEDOUT)
		return;

	trace_xprt_timer(xprt, req->rq_xid, task->tk_status);
	if (!req->rq_reply_bytes_recvd) {
		if (xprt->ops->timer)
			xprt->ops->timer(xprt, task);
	} else
		task->tk_status = 0;
}

/**
 * xprt_wait_for_reply_request_def - wait for reply
 * @task: pointer to rpc_task
 *
 * Set a request's retransmit timeout based on the transport's
 * default timeout parameters.  Used by transports that don't adjust
 * the retransmit timeout based on round-trip time estimation,
 * and put the task to sleep on the pending queue.
 */
void xprt_wait_for_reply_request_def(struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;

	rpc_sleep_on_timeout(&req->rq_xprt->pending, task, xprt_timer,
			xprt_request_timeout(req));
}
EXPORT_SYMBOL_GPL(xprt_wait_for_reply_request_def);

/**
 * xprt_wait_for_reply_request_rtt - wait for reply using RTT estimator
 * @task: pointer to rpc_task
 *
 * Set a request's retransmit timeout using the RTT estimator,
 * and put the task to sleep on the pending queue.
 */
void xprt_wait_for_reply_request_rtt(struct rpc_task *task)
{
	int timer = task->tk_msg.rpc_proc->p_timer;
	struct rpc_clnt *clnt = task->tk_client;
	struct rpc_rtt *rtt = clnt->cl_rtt;
	struct rpc_rqst *req = task->tk_rqstp;
	unsigned long max_timeout = clnt->cl_timeout->to_maxval;
	unsigned long timeout;

	timeout = rpc_calc_rto(rtt, timer);
	timeout <<= rpc_ntimeo(rtt, timer) + req->rq_retries;
	if (timeout > max_timeout || timeout == 0)
		timeout = max_timeout;
	rpc_sleep_on_timeout(&req->rq_xprt->pending, task, xprt_timer,
			jiffies + timeout);
}
EXPORT_SYMBOL_GPL(xprt_wait_for_reply_request_rtt);

/**
 * xprt_request_wait_receive - wait for the reply to an RPC request
 * @task: RPC task about to send a request
 *
 */
void xprt_request_wait_receive(struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_xprt *xprt = req->rq_xprt;

	if (!test_bit(RPC_TASK_NEED_RECV, &task->tk_runstate))
		return;
	/*
	 * Sleep on the pending queue if we're expecting a reply.
	 * The spinlock ensures atomicity between the test of
	 * req->rq_reply_bytes_recvd, and the call to rpc_sleep_on().
	 */
	spin_lock(&xprt->queue_lock);
	if (test_bit(RPC_TASK_NEED_RECV, &task->tk_runstate)) {
		xprt->ops->wait_for_reply_request(task);
		/*
		 * Send an extra queue wakeup call if the
		 * connection was dropped in case the call to
		 * rpc_sleep_on() raced.
		 */
		if (xprt_request_retransmit_after_disconnect(task))
			rpc_wake_up_queued_task_set_status(&xprt->pending,
					task, -ENOTCONN);
	}
	spin_unlock(&xprt->queue_lock);
}

static bool
xprt_request_need_enqueue_transmit(struct rpc_task *task, struct rpc_rqst *req)
{
	return !test_bit(RPC_TASK_NEED_XMIT, &task->tk_runstate);
}

/**
 * xprt_request_enqueue_transmit - queue a task for transmission
 * @task: pointer to rpc_task
 *
 * Add a task to the transmission queue.
 */
void
xprt_request_enqueue_transmit(struct rpc_task *task)
{
	struct rpc_rqst *pos, *req = task->tk_rqstp;
	struct rpc_xprt *xprt = req->rq_xprt;

	if (xprt_request_need_enqueue_transmit(task, req)) {
		req->rq_bytes_sent = 0;
		spin_lock(&xprt->queue_lock);
		/*
		 * Requests that carry congestion control credits are added
		 * to the head of the list to avoid starvation issues.
		 */
		if (req->rq_cong) {
			xprt_clear_congestion_window_wait(xprt);
			list_for_each_entry(pos, &xprt->xmit_queue, rq_xmit) {
				if (pos->rq_cong)
					continue;
				/* Note: req is added _before_ pos */
				list_add_tail(&req->rq_xmit, &pos->rq_xmit);
				INIT_LIST_HEAD(&req->rq_xmit2);
				goto out;
			}
		} else if (RPC_IS_SWAPPER(task)) {
			list_for_each_entry(pos, &xprt->xmit_queue, rq_xmit) {
				if (pos->rq_cong || pos->rq_bytes_sent)
					continue;
				if (RPC_IS_SWAPPER(pos->rq_task))
					continue;
				/* Note: req is added _before_ pos */
				list_add_tail(&req->rq_xmit, &pos->rq_xmit);
				INIT_LIST_HEAD(&req->rq_xmit2);
				goto out;
			}
		} else if (!req->rq_seqno) {
			list_for_each_entry(pos, &xprt->xmit_queue, rq_xmit) {
				if (pos->rq_task->tk_owner != task->tk_owner)
					continue;
				list_add_tail(&req->rq_xmit2, &pos->rq_xmit2);
				INIT_LIST_HEAD(&req->rq_xmit);
				goto out;
			}
		}
		list_add_tail(&req->rq_xmit, &xprt->xmit_queue);
		INIT_LIST_HEAD(&req->rq_xmit2);
out:
		atomic_long_inc(&xprt->xmit_queuelen);
		set_bit(RPC_TASK_NEED_XMIT, &task->tk_runstate);
		spin_unlock(&xprt->queue_lock);
	}
}

/**
 * xprt_request_dequeue_transmit_locked - remove a task from the transmission queue
 * @task: pointer to rpc_task
 *
 * Remove a task from the transmission queue
 * Caller must hold xprt->queue_lock
 */
static void
xprt_request_dequeue_transmit_locked(struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;

	if (!test_and_clear_bit(RPC_TASK_NEED_XMIT, &task->tk_runstate))
		return;
	if (!list_empty(&req->rq_xmit)) {
		list_del(&req->rq_xmit);
		if (!list_empty(&req->rq_xmit2)) {
			struct rpc_rqst *next = list_first_entry(&req->rq_xmit2,
					struct rpc_rqst, rq_xmit2);
			list_del(&req->rq_xmit2);
			list_add_tail(&next->rq_xmit, &next->rq_xprt->xmit_queue);
		}
	} else
		list_del(&req->rq_xmit2);
	atomic_long_dec(&req->rq_xprt->xmit_queuelen);
}

/**
 * xprt_request_dequeue_transmit - remove a task from the transmission queue
 * @task: pointer to rpc_task
 *
 * Remove a task from the transmission queue
 */
static void
xprt_request_dequeue_transmit(struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_xprt *xprt = req->rq_xprt;

	spin_lock(&xprt->queue_lock);
	xprt_request_dequeue_transmit_locked(task);
	spin_unlock(&xprt->queue_lock);
}

/**
 * xprt_request_dequeue_xprt - remove a task from the transmit+receive queue
 * @task: pointer to rpc_task
 *
 * Remove a task from the transmit and receive queues, and ensure that
 * it is not pinned by the receive work item.
 */
void
xprt_request_dequeue_xprt(struct rpc_task *task)
{
	struct rpc_rqst	*req = task->tk_rqstp;
	struct rpc_xprt *xprt = req->rq_xprt;

	if (test_bit(RPC_TASK_NEED_XMIT, &task->tk_runstate) ||
	    test_bit(RPC_TASK_NEED_RECV, &task->tk_runstate) ||
	    xprt_is_pinned_rqst(req)) {
		spin_lock(&xprt->queue_lock);
		xprt_request_dequeue_transmit_locked(task);
		xprt_request_dequeue_receive_locked(task);
		while (xprt_is_pinned_rqst(req)) {
			set_bit(RPC_TASK_MSG_PIN_WAIT, &task->tk_runstate);
			spin_unlock(&xprt->queue_lock);
			xprt_wait_on_pinned_rqst(req);
			spin_lock(&xprt->queue_lock);
			clear_bit(RPC_TASK_MSG_PIN_WAIT, &task->tk_runstate);
		}
		spin_unlock(&xprt->queue_lock);
	}
}

/**
 * xprt_request_prepare - prepare an encoded request for transport
 * @req: pointer to rpc_rqst
 *
 * Calls into the transport layer to do whatever is needed to prepare
 * the request for transmission or receive.
 */
void
xprt_request_prepare(struct rpc_rqst *req)
{
	struct rpc_xprt *xprt = req->rq_xprt;

	if (xprt->ops->prepare_request)
		xprt->ops->prepare_request(req);
}

/**
 * xprt_request_need_retransmit - Test if a task needs retransmission
 * @task: pointer to rpc_task
 *
 * Test for whether a connection breakage requires the task to retransmit
 */
bool
xprt_request_need_retransmit(struct rpc_task *task)
{
	return xprt_request_retransmit_after_disconnect(task);
}

/**
 * xprt_prepare_transmit - reserve the transport before sending a request
 * @task: RPC task about to send a request
 *
 */
bool xprt_prepare_transmit(struct rpc_task *task)
{
	struct rpc_rqst	*req = task->tk_rqstp;
	struct rpc_xprt	*xprt = req->rq_xprt;

	if (!xprt_lock_write(xprt, task)) {
		/* Race breaker: someone may have transmitted us */
		if (!test_bit(RPC_TASK_NEED_XMIT, &task->tk_runstate))
			rpc_wake_up_queued_task_set_status(&xprt->sending,
					task, 0);
		return false;

	}
	return true;
}

void xprt_end_transmit(struct rpc_task *task)
{
	struct rpc_xprt	*xprt = task->tk_rqstp->rq_xprt;

	xprt_inject_disconnect(xprt);
	xprt_release_write(xprt, task);
}

/**
 * xprt_request_transmit - send an RPC request on a transport
 * @req: pointer to request to transmit
 * @snd_task: RPC task that owns the transport lock
 *
 * This performs the transmission of a single request.
 * Note that if the request is not the same as snd_task, then it
 * does need to be pinned.
 * Returns '0' on success.
 */
static int
xprt_request_transmit(struct rpc_rqst *req, struct rpc_task *snd_task)
{
	struct rpc_xprt *xprt = req->rq_xprt;
	struct rpc_task *task = req->rq_task;
	unsigned int connect_cookie;
	int is_retrans = RPC_WAS_SENT(task);
	int status;

	if (!req->rq_bytes_sent) {
		if (xprt_request_data_received(task)) {
			status = 0;
			goto out_dequeue;
		}
		/* Verify that our message lies in the RPCSEC_GSS window */
		if (rpcauth_xmit_need_reencode(task)) {
			status = -EBADMSG;
			goto out_dequeue;
		}
		if (RPC_SIGNALLED(task)) {
			status = -ERESTARTSYS;
			goto out_dequeue;
		}
	}

	/*
	 * Update req->rq_ntrans before transmitting to avoid races with
	 * xprt_update_rtt(), which needs to know that it is recording a
	 * reply to the first transmission.
	 */
	req->rq_ntrans++;

	trace_rpc_xdr_sendto(task, &req->rq_snd_buf);
	connect_cookie = xprt->connect_cookie;
	status = xprt->ops->send_request(req);
	if (status != 0) {
		req->rq_ntrans--;
		trace_xprt_transmit(req, status);
		return status;
	}

	if (is_retrans) {
		task->tk_client->cl_stats->rpcretrans++;
		trace_xprt_retransmit(req);
	}

	xprt_inject_disconnect(xprt);

	task->tk_flags |= RPC_TASK_SENT;
	spin_lock(&xprt->transport_lock);

	xprt->stat.sends++;
	xprt->stat.req_u += xprt->stat.sends - xprt->stat.recvs;
	xprt->stat.bklog_u += xprt->backlog.qlen;
	xprt->stat.sending_u += xprt->sending.qlen;
	xprt->stat.pending_u += xprt->pending.qlen;
	spin_unlock(&xprt->transport_lock);

	req->rq_connect_cookie = connect_cookie;
out_dequeue:
	trace_xprt_transmit(req, status);
	xprt_request_dequeue_transmit(task);
	rpc_wake_up_queued_task_set_status(&xprt->sending, task, status);
	return status;
}

/**
 * xprt_transmit - send an RPC request on a transport
 * @task: controlling RPC task
 *
 * Attempts to drain the transmit queue. On exit, either the transport
 * signalled an error that needs to be handled before transmission can
 * resume, or @task finished transmitting, and detected that it already
 * received a reply.
 */
void
xprt_transmit(struct rpc_task *task)
{
	struct rpc_rqst *next, *req = task->tk_rqstp;
	struct rpc_xprt	*xprt = req->rq_xprt;
	int status;

	spin_lock(&xprt->queue_lock);
	for (;;) {
		next = list_first_entry_or_null(&xprt->xmit_queue,
						struct rpc_rqst, rq_xmit);
		if (!next)
			break;
		xprt_pin_rqst(next);
		spin_unlock(&xprt->queue_lock);
		status = xprt_request_transmit(next, task);
		if (status == -EBADMSG && next != req)
			status = 0;
		spin_lock(&xprt->queue_lock);
		xprt_unpin_rqst(next);
		if (status < 0) {
			if (test_bit(RPC_TASK_NEED_XMIT, &task->tk_runstate))
				task->tk_status = status;
			break;
		}
		/* Was @task transmitted, and has it received a reply? */
		if (xprt_request_data_received(task) &&
		    !test_bit(RPC_TASK_NEED_XMIT, &task->tk_runstate))
			break;
		cond_resched_lock(&xprt->queue_lock);
	}
	spin_unlock(&xprt->queue_lock);
}

static void xprt_complete_request_init(struct rpc_task *task)
{
	if (task->tk_rqstp)
		xprt_request_init(task);
}

void xprt_add_backlog(struct rpc_xprt *xprt, struct rpc_task *task)
{
	set_bit(XPRT_CONGESTED, &xprt->state);
	rpc_sleep_on(&xprt->backlog, task, xprt_complete_request_init);
}
EXPORT_SYMBOL_GPL(xprt_add_backlog);

static bool __xprt_set_rq(struct rpc_task *task, void *data)
{
	struct rpc_rqst *req = data;

	if (task->tk_rqstp == NULL) {
		memset(req, 0, sizeof(*req));	/* mark unused */
		task->tk_rqstp = req;
		return true;
	}
	return false;
}

bool xprt_wake_up_backlog(struct rpc_xprt *xprt, struct rpc_rqst *req)
{
	if (rpc_wake_up_first(&xprt->backlog, __xprt_set_rq, req) == NULL) {
		clear_bit(XPRT_CONGESTED, &xprt->state);
		return false;
	}
	return true;
}
EXPORT_SYMBOL_GPL(xprt_wake_up_backlog);

static bool xprt_throttle_congested(struct rpc_xprt *xprt, struct rpc_task *task)
{
	bool ret = false;

	if (!test_bit(XPRT_CONGESTED, &xprt->state))
		goto out;
	spin_lock(&xprt->reserve_lock);
	if (test_bit(XPRT_CONGESTED, &xprt->state)) {
		xprt_add_backlog(xprt, task);
		ret = true;
	}
	spin_unlock(&xprt->reserve_lock);
out:
	return ret;
}

static struct rpc_rqst *xprt_dynamic_alloc_slot(struct rpc_xprt *xprt)
{
	struct rpc_rqst *req = ERR_PTR(-EAGAIN);
	gfp_t gfp_mask = GFP_KERNEL;

	if (xprt->num_reqs >= xprt->max_reqs)
		goto out;
	++xprt->num_reqs;
	spin_unlock(&xprt->reserve_lock);
	if (current->flags & PF_WQ_WORKER)
		gfp_mask |= __GFP_NORETRY | __GFP_NOWARN;
	req = kzalloc(sizeof(*req), gfp_mask);
	spin_lock(&xprt->reserve_lock);
	if (req != NULL)
		goto out;
	--xprt->num_reqs;
	req = ERR_PTR(-ENOMEM);
out:
	return req;
}

static bool xprt_dynamic_free_slot(struct rpc_xprt *xprt, struct rpc_rqst *req)
{
	if (xprt->num_reqs > xprt->min_reqs) {
		--xprt->num_reqs;
		kfree(req);
		return true;
	}
	return false;
}

void xprt_alloc_slot(struct rpc_xprt *xprt, struct rpc_task *task)
{
	struct rpc_rqst *req;

	spin_lock(&xprt->reserve_lock);
	if (!list_empty(&xprt->free)) {
		req = list_entry(xprt->free.next, struct rpc_rqst, rq_list);
		list_del(&req->rq_list);
		goto out_init_req;
	}
	req = xprt_dynamic_alloc_slot(xprt);
	if (!IS_ERR(req))
		goto out_init_req;
	switch (PTR_ERR(req)) {
	case -ENOMEM:
		dprintk("RPC:       dynamic allocation of request slot "
				"failed! Retrying\n");
		task->tk_status = -ENOMEM;
		break;
	case -EAGAIN:
		xprt_add_backlog(xprt, task);
		dprintk("RPC:       waiting for request slot\n");
		fallthrough;
	default:
		task->tk_status = -EAGAIN;
	}
	spin_unlock(&xprt->reserve_lock);
	return;
out_init_req:
	xprt->stat.max_slots = max_t(unsigned int, xprt->stat.max_slots,
				     xprt->num_reqs);
	spin_unlock(&xprt->reserve_lock);

	task->tk_status = 0;
	task->tk_rqstp = req;
}
EXPORT_SYMBOL_GPL(xprt_alloc_slot);

void xprt_free_slot(struct rpc_xprt *xprt, struct rpc_rqst *req)
{
	spin_lock(&xprt->reserve_lock);
	if (!xprt_wake_up_backlog(xprt, req) &&
	    !xprt_dynamic_free_slot(xprt, req)) {
		memset(req, 0, sizeof(*req));	/* mark unused */
		list_add(&req->rq_list, &xprt->free);
	}
	spin_unlock(&xprt->reserve_lock);
}
EXPORT_SYMBOL_GPL(xprt_free_slot);

static void xprt_free_all_slots(struct rpc_xprt *xprt)
{
	struct rpc_rqst *req;
	while (!list_empty(&xprt->free)) {
		req = list_first_entry(&xprt->free, struct rpc_rqst, rq_list);
		list_del(&req->rq_list);
		kfree(req);
	}
}

static DEFINE_IDA(rpc_xprt_ids);

void xprt_cleanup_ids(void)
{
	ida_destroy(&rpc_xprt_ids);
}

static int xprt_alloc_id(struct rpc_xprt *xprt)
{
	int id;

	id = ida_simple_get(&rpc_xprt_ids, 0, 0, GFP_KERNEL);
	if (id < 0)
		return id;

	xprt->id = id;
	return 0;
}

static void xprt_free_id(struct rpc_xprt *xprt)
{
	ida_simple_remove(&rpc_xprt_ids, xprt->id);
}

struct rpc_xprt *xprt_alloc(struct net *net, size_t size,
		unsigned int num_prealloc,
		unsigned int max_alloc)
{
	struct rpc_xprt *xprt;
	struct rpc_rqst *req;
	int i;

	xprt = kzalloc(size, GFP_KERNEL);
	if (xprt == NULL)
		goto out;

	xprt_alloc_id(xprt);
	xprt_init(xprt, net);

	for (i = 0; i < num_prealloc; i++) {
		req = kzalloc(sizeof(struct rpc_rqst), GFP_KERNEL);
		if (!req)
			goto out_free;
		list_add(&req->rq_list, &xprt->free);
	}
	if (max_alloc > num_prealloc)
		xprt->max_reqs = max_alloc;
	else
		xprt->max_reqs = num_prealloc;
	xprt->min_reqs = num_prealloc;
	xprt->num_reqs = num_prealloc;

	return xprt;

out_free:
	xprt_free(xprt);
out:
	return NULL;
}
EXPORT_SYMBOL_GPL(xprt_alloc);

void xprt_free(struct rpc_xprt *xprt)
{
	put_net(xprt->xprt_net);
	xprt_free_all_slots(xprt);
	xprt_free_id(xprt);
	rpc_sysfs_xprt_destroy(xprt);
	kfree_rcu(xprt, rcu);
}
EXPORT_SYMBOL_GPL(xprt_free);

static void
xprt_init_connect_cookie(struct rpc_rqst *req, struct rpc_xprt *xprt)
{
	req->rq_connect_cookie = xprt_connect_cookie(xprt) - 1;
}

static __be32
xprt_alloc_xid(struct rpc_xprt *xprt)
{
	__be32 xid;

	spin_lock(&xprt->reserve_lock);
	xid = (__force __be32)xprt->xid++;
	spin_unlock(&xprt->reserve_lock);
	return xid;
}

static void
xprt_init_xid(struct rpc_xprt *xprt)
{
	xprt->xid = prandom_u32();
}

static void
xprt_request_init(struct rpc_task *task)
{
	struct rpc_xprt *xprt = task->tk_xprt;
	struct rpc_rqst	*req = task->tk_rqstp;

	req->rq_task	= task;
	req->rq_xprt    = xprt;
	req->rq_buffer  = NULL;
	req->rq_xid	= xprt_alloc_xid(xprt);
	xprt_init_connect_cookie(req, xprt);
	req->rq_snd_buf.len = 0;
	req->rq_snd_buf.buflen = 0;
	req->rq_rcv_buf.len = 0;
	req->rq_rcv_buf.buflen = 0;
	req->rq_snd_buf.bvec = NULL;
	req->rq_rcv_buf.bvec = NULL;
	req->rq_release_snd_buf = NULL;
	xprt_init_majortimeo(task, req);

	trace_xprt_reserve(req);
}

static void
xprt_do_reserve(struct rpc_xprt *xprt, struct rpc_task *task)
{
	xprt->ops->alloc_slot(xprt, task);
	if (task->tk_rqstp != NULL)
		xprt_request_init(task);
}

/**
 * xprt_reserve - allocate an RPC request slot
 * @task: RPC task requesting a slot allocation
 *
 * If the transport is marked as being congested, or if no more
 * slots are available, place the task on the transport's
 * backlog queue.
 */
void xprt_reserve(struct rpc_task *task)
{
	struct rpc_xprt *xprt = task->tk_xprt;

	task->tk_status = 0;
	if (task->tk_rqstp != NULL)
		return;

	task->tk_status = -EAGAIN;
	if (!xprt_throttle_congested(xprt, task))
		xprt_do_reserve(xprt, task);
}

/**
 * xprt_retry_reserve - allocate an RPC request slot
 * @task: RPC task requesting a slot allocation
 *
 * If no more slots are available, place the task on the transport's
 * backlog queue.
 * Note that the only difference with xprt_reserve is that we now
 * ignore the value of the XPRT_CONGESTED flag.
 */
void xprt_retry_reserve(struct rpc_task *task)
{
	struct rpc_xprt *xprt = task->tk_xprt;

	task->tk_status = 0;
	if (task->tk_rqstp != NULL)
		return;

	task->tk_status = -EAGAIN;
	xprt_do_reserve(xprt, task);
}

/**
 * xprt_release - release an RPC request slot
 * @task: task which is finished with the slot
 *
 */
void xprt_release(struct rpc_task *task)
{
	struct rpc_xprt	*xprt;
	struct rpc_rqst	*req = task->tk_rqstp;

	if (req == NULL) {
		if (task->tk_client) {
			xprt = task->tk_xprt;
			xprt_release_write(xprt, task);
		}
		return;
	}

	xprt = req->rq_xprt;
	xprt_request_dequeue_xprt(task);
	spin_lock(&xprt->transport_lock);
	xprt->ops->release_xprt(xprt, task);
	if (xprt->ops->release_request)
		xprt->ops->release_request(task);
	xprt_schedule_autodisconnect(xprt);
	spin_unlock(&xprt->transport_lock);
	if (req->rq_buffer)
		xprt->ops->buf_free(task);
	xdr_free_bvec(&req->rq_rcv_buf);
	xdr_free_bvec(&req->rq_snd_buf);
	if (req->rq_cred != NULL)
		put_rpccred(req->rq_cred);
	if (req->rq_release_snd_buf)
		req->rq_release_snd_buf(req);

	task->tk_rqstp = NULL;
	if (likely(!bc_prealloc(req)))
		xprt->ops->free_slot(xprt, req);
	else
		xprt_free_bc_request(req);
}

#ifdef CONFIG_SUNRPC_BACKCHANNEL
void
xprt_init_bc_request(struct rpc_rqst *req, struct rpc_task *task)
{
	struct xdr_buf *xbufp = &req->rq_snd_buf;

	task->tk_rqstp = req;
	req->rq_task = task;
	xprt_init_connect_cookie(req, req->rq_xprt);
	/*
	 * Set up the xdr_buf length.
	 * This also indicates that the buffer is XDR encoded already.
	 */
	xbufp->len = xbufp->head[0].iov_len + xbufp->page_len +
		xbufp->tail[0].iov_len;
}
#endif

static void xprt_init(struct rpc_xprt *xprt, struct net *net)
{
	kref_init(&xprt->kref);

	spin_lock_init(&xprt->transport_lock);
	spin_lock_init(&xprt->reserve_lock);
	spin_lock_init(&xprt->queue_lock);

	INIT_LIST_HEAD(&xprt->free);
	xprt->recv_queue = RB_ROOT;
	INIT_LIST_HEAD(&xprt->xmit_queue);
#if defined(CONFIG_SUNRPC_BACKCHANNEL)
	spin_lock_init(&xprt->bc_pa_lock);
	INIT_LIST_HEAD(&xprt->bc_pa_list);
#endif /* CONFIG_SUNRPC_BACKCHANNEL */
	INIT_LIST_HEAD(&xprt->xprt_switch);

	xprt->last_used = jiffies;
	xprt->cwnd = RPC_INITCWND;
	xprt->bind_index = 0;

	rpc_init_wait_queue(&xprt->binding, "xprt_binding");
	rpc_init_wait_queue(&xprt->pending, "xprt_pending");
	rpc_init_wait_queue(&xprt->sending, "xprt_sending");
	rpc_init_priority_wait_queue(&xprt->backlog, "xprt_backlog");

	xprt_init_xid(xprt);

	xprt->xprt_net = get_net(net);
}

/**
 * xprt_create_transport - create an RPC transport
 * @args: rpc transport creation arguments
 *
 */
struct rpc_xprt *xprt_create_transport(struct xprt_create *args)
{
	struct rpc_xprt	*xprt;
	const struct xprt_class *t;

	t = xprt_class_find_by_ident(args->ident);
	if (!t) {
		dprintk("RPC: transport (%d) not supported\n", args->ident);
		return ERR_PTR(-EIO);
	}

	xprt = t->setup(args);
	xprt_class_release(t);

	if (IS_ERR(xprt))
		goto out;
	if (args->flags & XPRT_CREATE_NO_IDLE_TIMEOUT)
		xprt->idle_timeout = 0;
	INIT_WORK(&xprt->task_cleanup, xprt_autoclose);
	if (xprt_has_timer(xprt))
		timer_setup(&xprt->timer, xprt_init_autodisconnect, 0);
	else
		timer_setup(&xprt->timer, NULL, 0);

	if (strlen(args->servername) > RPC_MAXNETNAMELEN) {
		xprt_destroy(xprt);
		return ERR_PTR(-EINVAL);
	}
	xprt->servername = kstrdup(args->servername, GFP_KERNEL);
	if (xprt->servername == NULL) {
		xprt_destroy(xprt);
		return ERR_PTR(-ENOMEM);
	}

	rpc_xprt_debugfs_register(xprt);

	trace_xprt_create(xprt);
out:
	return xprt;
}

static void xprt_destroy_cb(struct work_struct *work)
{
	struct rpc_xprt *xprt =
		container_of(work, struct rpc_xprt, task_cleanup);

	trace_xprt_destroy(xprt);

	rpc_xprt_debugfs_unregister(xprt);
	rpc_destroy_wait_queue(&xprt->binding);
	rpc_destroy_wait_queue(&xprt->pending);
	rpc_destroy_wait_queue(&xprt->sending);
	rpc_destroy_wait_queue(&xprt->backlog);
	kfree(xprt->servername);
	/*
	 * Destroy any existing back channel
	 */
	xprt_destroy_backchannel(xprt, UINT_MAX);

	/*
	 * Tear down transport state and free the rpc_xprt
	 */
	xprt->ops->destroy(xprt);
}

/**
 * xprt_destroy - destroy an RPC transport, killing off all requests.
 * @xprt: transport to destroy
 *
 */
static void xprt_destroy(struct rpc_xprt *xprt)
{
	/*
	 * Exclude transport connect/disconnect handlers and autoclose
	 */
	wait_on_bit_lock(&xprt->state, XPRT_LOCKED, TASK_UNINTERRUPTIBLE);

	/*
	 * xprt_schedule_autodisconnect() can run after XPRT_LOCKED
	 * is cleared.  We use ->transport_lock to ensure the mod_timer()
	 * can only run *before* del_time_sync(), never after.
	 */
	spin_lock(&xprt->transport_lock);
	del_timer_sync(&xprt->timer);
	spin_unlock(&xprt->transport_lock);

	/*
	 * Destroy sockets etc from the system workqueue so they can
	 * safely flush receive work running on rpciod.
	 */
	INIT_WORK(&xprt->task_cleanup, xprt_destroy_cb);
	schedule_work(&xprt->task_cleanup);
}

static void xprt_destroy_kref(struct kref *kref)
{
	xprt_destroy(container_of(kref, struct rpc_xprt, kref));
}

/**
 * xprt_get - return a reference to an RPC transport.
 * @xprt: pointer to the transport
 *
 */
struct rpc_xprt *xprt_get(struct rpc_xprt *xprt)
{
	if (xprt != NULL && kref_get_unless_zero(&xprt->kref))
		return xprt;
	return NULL;
}
EXPORT_SYMBOL_GPL(xprt_get);

/**
 * xprt_put - release a reference to an RPC transport.
 * @xprt: pointer to the transport
 *
 */
void xprt_put(struct rpc_xprt *xprt)
{
	if (xprt != NULL)
		kref_put(&xprt->kref, xprt_destroy_kref);
}
EXPORT_SYMBOL_GPL(xprt_put);
