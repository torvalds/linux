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

#include <trace/events/sunrpc.h>

#include "sunrpc.h"

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
static void	xprt_request_init(struct rpc_task *, struct rpc_xprt *);
static void	xprt_connect_status(struct rpc_task *task);
static int      __xprt_get_cong(struct rpc_xprt *, struct rpc_task *);
static void     __xprt_put_cong(struct rpc_xprt *, struct rpc_rqst *);
static void	 xprt_destroy(struct rpc_xprt *xprt);

static DEFINE_SPINLOCK(xprt_list_lock);
static LIST_HEAD(xprt_list);

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

/**
 * xprt_load_transport - load a transport implementation
 * @transport_name: transport to load
 *
 * Returns:
 * 0:		transport successfully loaded
 * -ENOENT:	transport module not available
 */
int xprt_load_transport(const char *transport_name)
{
	struct xprt_class *t;
	int result;

	result = 0;
	spin_lock(&xprt_list_lock);
	list_for_each_entry(t, &xprt_list, list) {
		if (strcmp(t->name, transport_name) == 0) {
			spin_unlock(&xprt_list_lock);
			goto out;
		}
	}
	spin_unlock(&xprt_list_lock);
	result = request_module("xprt%s", transport_name);
out:
	return result;
}
EXPORT_SYMBOL_GPL(xprt_load_transport);

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
	int priority;

	if (test_and_set_bit(XPRT_LOCKED, &xprt->state)) {
		if (task == xprt->snd_task)
			return 1;
		goto out_sleep;
	}
	xprt->snd_task = task;
	if (req != NULL)
		req->rq_ntrans++;

	return 1;

out_sleep:
	dprintk("RPC: %5u failed to lock transport %p\n",
			task->tk_pid, xprt);
	task->tk_timeout = 0;
	task->tk_status = -EAGAIN;
	if (req == NULL)
		priority = RPC_PRIORITY_LOW;
	else if (!req->rq_ntrans)
		priority = RPC_PRIORITY_NORMAL;
	else
		priority = RPC_PRIORITY_HIGH;
	rpc_sleep_on_priority(&xprt->sending, task, NULL, priority);
	return 0;
}
EXPORT_SYMBOL_GPL(xprt_reserve_xprt);

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

/*
 * xprt_reserve_xprt_cong - serialize write access to transports
 * @task: task that is requesting access to the transport
 *
 * Same as xprt_reserve_xprt, but Van Jacobson congestion control is
 * integrated into the decision of whether a request is allowed to be
 * woken up and given access to the transport.
 */
int xprt_reserve_xprt_cong(struct rpc_xprt *xprt, struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;
	int priority;

	if (test_and_set_bit(XPRT_LOCKED, &xprt->state)) {
		if (task == xprt->snd_task)
			return 1;
		goto out_sleep;
	}
	if (req == NULL) {
		xprt->snd_task = task;
		return 1;
	}
	if (__xprt_get_cong(xprt, task)) {
		xprt->snd_task = task;
		req->rq_ntrans++;
		return 1;
	}
	xprt_clear_locked(xprt);
out_sleep:
	if (req)
		__xprt_put_cong(xprt, req);
	dprintk("RPC: %5u failed to lock transport %p\n", task->tk_pid, xprt);
	task->tk_timeout = 0;
	task->tk_status = -EAGAIN;
	if (req == NULL)
		priority = RPC_PRIORITY_LOW;
	else if (!req->rq_ntrans)
		priority = RPC_PRIORITY_NORMAL;
	else
		priority = RPC_PRIORITY_HIGH;
	rpc_sleep_on_priority(&xprt->sending, task, NULL, priority);
	return 0;
}
EXPORT_SYMBOL_GPL(xprt_reserve_xprt_cong);

static inline int xprt_lock_write(struct rpc_xprt *xprt, struct rpc_task *task)
{
	int retval;

	spin_lock_bh(&xprt->transport_lock);
	retval = xprt->ops->reserve_xprt(xprt, task);
	spin_unlock_bh(&xprt->transport_lock);
	return retval;
}

static bool __xprt_lock_write_func(struct rpc_task *task, void *data)
{
	struct rpc_xprt *xprt = data;
	struct rpc_rqst *req;

	req = task->tk_rqstp;
	xprt->snd_task = task;
	if (req)
		req->rq_ntrans++;
	return true;
}

static void __xprt_lock_write_next(struct rpc_xprt *xprt)
{
	if (test_and_set_bit(XPRT_LOCKED, &xprt->state))
		return;

	if (rpc_wake_up_first_on_wq(xprtiod_workqueue, &xprt->sending,
				__xprt_lock_write_func, xprt))
		return;
	xprt_clear_locked(xprt);
}

static bool __xprt_lock_write_cong_func(struct rpc_task *task, void *data)
{
	struct rpc_xprt *xprt = data;
	struct rpc_rqst *req;

	req = task->tk_rqstp;
	if (req == NULL) {
		xprt->snd_task = task;
		return true;
	}
	if (__xprt_get_cong(xprt, task)) {
		xprt->snd_task = task;
		req->rq_ntrans++;
		return true;
	}
	return false;
}

static void __xprt_lock_write_next_cong(struct rpc_xprt *xprt)
{
	if (test_and_set_bit(XPRT_LOCKED, &xprt->state))
		return;
	if (RPCXPRT_CONGESTED(xprt))
		goto out_unlock;
	if (rpc_wake_up_first_on_wq(xprtiod_workqueue, &xprt->sending,
				__xprt_lock_write_cong_func, xprt))
		return;
out_unlock:
	xprt_clear_locked(xprt);
}

static void xprt_task_clear_bytes_sent(struct rpc_task *task)
{
	if (task != NULL) {
		struct rpc_rqst *req = task->tk_rqstp;
		if (req != NULL)
			req->rq_bytes_sent = 0;
	}
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
		xprt_task_clear_bytes_sent(task);
		xprt_clear_locked(xprt);
		__xprt_lock_write_next(xprt);
	}
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
		xprt_task_clear_bytes_sent(task);
		xprt_clear_locked(xprt);
		__xprt_lock_write_next_cong(xprt);
	}
}
EXPORT_SYMBOL_GPL(xprt_release_xprt_cong);

static inline void xprt_release_write(struct rpc_xprt *xprt, struct rpc_task *task)
{
	spin_lock_bh(&xprt->transport_lock);
	xprt->ops->release_xprt(xprt, task);
	spin_unlock_bh(&xprt->transport_lock);
}

/*
 * Van Jacobson congestion avoidance. Check if the congestion window
 * overflowed. Put the task to sleep if this is the case.
 */
static int
__xprt_get_cong(struct rpc_xprt *xprt, struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;

	if (req->rq_cong)
		return 1;
	dprintk("RPC: %5u xprt_cwnd_limited cong = %lu cwnd = %lu\n",
			task->tk_pid, xprt->cong, xprt->cwnd);
	if (RPCXPRT_CONGESTED(xprt))
		return 0;
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
	__xprt_lock_write_next_cong(xprt);
}

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
 * @task: task to be put to sleep
 * @action: function pointer to be executed after wait
 *
 * Note that we only set the timer for the case of RPC_IS_SOFT(), since
 * we don't in general want to force a socket disconnection due to
 * an incomplete RPC call transmission.
 */
void xprt_wait_for_buffer_space(struct rpc_task *task, rpc_action action)
{
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_xprt *xprt = req->rq_xprt;

	task->tk_timeout = RPC_IS_SOFT(task) ? req->rq_timeout : 0;
	rpc_sleep_on(&xprt->pending, task, action);
}
EXPORT_SYMBOL_GPL(xprt_wait_for_buffer_space);

/**
 * xprt_write_space - wake the task waiting for transport output buffer space
 * @xprt: transport with waiting tasks
 *
 * Can be called in a soft IRQ context, so xprt_write_space never sleeps.
 */
void xprt_write_space(struct rpc_xprt *xprt)
{
	spin_lock_bh(&xprt->transport_lock);
	if (xprt->snd_task) {
		dprintk("RPC:       write space: waking waiting task on "
				"xprt %p\n", xprt);
		rpc_wake_up_queued_task(&xprt->pending, xprt->snd_task);
	}
	spin_unlock_bh(&xprt->transport_lock);
}
EXPORT_SYMBOL_GPL(xprt_write_space);

/**
 * xprt_set_retrans_timeout_def - set a request's retransmit timeout
 * @task: task whose timeout is to be set
 *
 * Set a request's retransmit timeout based on the transport's
 * default timeout parameters.  Used by transports that don't adjust
 * the retransmit timeout based on round-trip time estimation.
 */
void xprt_set_retrans_timeout_def(struct rpc_task *task)
{
	task->tk_timeout = task->tk_rqstp->rq_timeout;
}
EXPORT_SYMBOL_GPL(xprt_set_retrans_timeout_def);

/**
 * xprt_set_retrans_timeout_rtt - set a request's retransmit timeout
 * @task: task whose timeout is to be set
 *
 * Set a request's retransmit timeout using the RTT estimator.
 */
void xprt_set_retrans_timeout_rtt(struct rpc_task *task)
{
	int timer = task->tk_msg.rpc_proc->p_timer;
	struct rpc_clnt *clnt = task->tk_client;
	struct rpc_rtt *rtt = clnt->cl_rtt;
	struct rpc_rqst *req = task->tk_rqstp;
	unsigned long max_timeout = clnt->cl_timeout->to_maxval;

	task->tk_timeout = rpc_calc_rto(rtt, timer);
	task->tk_timeout <<= rpc_ntimeo(rtt, timer) + req->rq_retries;
	if (task->tk_timeout > max_timeout || task->tk_timeout == 0)
		task->tk_timeout = max_timeout;
}
EXPORT_SYMBOL_GPL(xprt_set_retrans_timeout_rtt);

static void xprt_reset_majortimeo(struct rpc_rqst *req)
{
	const struct rpc_timeout *to = req->rq_task->tk_client->cl_timeout;

	req->rq_majortimeo = req->rq_timeout;
	if (to->to_exponential)
		req->rq_majortimeo <<= to->to_retries;
	else
		req->rq_majortimeo += to->to_increment * to->to_retries;
	if (req->rq_majortimeo > to->to_maxval || req->rq_majortimeo == 0)
		req->rq_majortimeo = to->to_maxval;
	req->rq_majortimeo += jiffies;
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
		spin_lock_bh(&xprt->transport_lock);
		rpc_init_rtt(req->rq_task->tk_client->cl_rtt, to->to_initval);
		spin_unlock_bh(&xprt->transport_lock);
		status = -ETIMEDOUT;
	}

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

	clear_bit(XPRT_CLOSE_WAIT, &xprt->state);
	xprt->ops->close(xprt);
	xprt_release_write(xprt, NULL);
	wake_up_bit(&xprt->state, XPRT_LOCKED);
}

/**
 * xprt_disconnect_done - mark a transport as disconnected
 * @xprt: transport to flag for disconnect
 *
 */
void xprt_disconnect_done(struct rpc_xprt *xprt)
{
	dprintk("RPC:       disconnected transport %p\n", xprt);
	spin_lock_bh(&xprt->transport_lock);
	xprt_clear_connected(xprt);
	xprt_wake_pending_tasks(xprt, -EAGAIN);
	spin_unlock_bh(&xprt->transport_lock);
}
EXPORT_SYMBOL_GPL(xprt_disconnect_done);

/**
 * xprt_force_disconnect - force a transport to disconnect
 * @xprt: transport to disconnect
 *
 */
void xprt_force_disconnect(struct rpc_xprt *xprt)
{
	/* Don't race with the test_bit() in xprt_clear_locked() */
	spin_lock_bh(&xprt->transport_lock);
	set_bit(XPRT_CLOSE_WAIT, &xprt->state);
	/* Try to schedule an autoclose RPC call */
	if (test_and_set_bit(XPRT_LOCKED, &xprt->state) == 0)
		queue_work(xprtiod_workqueue, &xprt->task_cleanup);
	xprt_wake_pending_tasks(xprt, -EAGAIN);
	spin_unlock_bh(&xprt->transport_lock);
}
EXPORT_SYMBOL_GPL(xprt_force_disconnect);

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
	spin_lock_bh(&xprt->transport_lock);
	if (cookie != xprt->connect_cookie)
		goto out;
	if (test_bit(XPRT_CLOSING, &xprt->state))
		goto out;
	set_bit(XPRT_CLOSE_WAIT, &xprt->state);
	/* Try to schedule an autoclose RPC call */
	if (test_and_set_bit(XPRT_LOCKED, &xprt->state) == 0)
		queue_work(xprtiod_workqueue, &xprt->task_cleanup);
	xprt_wake_pending_tasks(xprt, -EAGAIN);
out:
	spin_unlock_bh(&xprt->transport_lock);
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
	if (list_empty(&xprt->recv) && xprt_has_timer(xprt))
		mod_timer(&xprt->timer, xprt->last_used + xprt->idle_timeout);
}

static void
xprt_init_autodisconnect(unsigned long data)
{
	struct rpc_xprt *xprt = (struct rpc_xprt *)data;

	spin_lock(&xprt->transport_lock);
	if (!list_empty(&xprt->recv))
		goto out_abort;
	/* Reset xprt->last_used to avoid connect/autodisconnect cycling */
	xprt->last_used = jiffies;
	if (test_and_set_bit(XPRT_LOCKED, &xprt->state))
		goto out_abort;
	spin_unlock(&xprt->transport_lock);
	queue_work(xprtiod_workqueue, &xprt->task_cleanup);
	return;
out_abort:
	spin_unlock(&xprt->transport_lock);
}

bool xprt_lock_connect(struct rpc_xprt *xprt,
		struct rpc_task *task,
		void *cookie)
{
	bool ret = false;

	spin_lock_bh(&xprt->transport_lock);
	if (!test_bit(XPRT_LOCKED, &xprt->state))
		goto out;
	if (xprt->snd_task != task)
		goto out;
	xprt_task_clear_bytes_sent(task);
	xprt->snd_task = cookie;
	ret = true;
out:
	spin_unlock_bh(&xprt->transport_lock);
	return ret;
}

void xprt_unlock_connect(struct rpc_xprt *xprt, void *cookie)
{
	spin_lock_bh(&xprt->transport_lock);
	if (xprt->snd_task != cookie)
		goto out;
	if (!test_bit(XPRT_LOCKED, &xprt->state))
		goto out;
	xprt->snd_task =NULL;
	xprt->ops->release_xprt(xprt, NULL);
	xprt_schedule_autodisconnect(xprt);
out:
	spin_unlock_bh(&xprt->transport_lock);
	wake_up_bit(&xprt->state, XPRT_LOCKED);
}

/**
 * xprt_connect - schedule a transport connect operation
 * @task: RPC task that is requesting the connect
 *
 */
void xprt_connect(struct rpc_task *task)
{
	struct rpc_xprt	*xprt = task->tk_rqstp->rq_xprt;

	dprintk("RPC: %5u xprt_connect xprt %p %s connected\n", task->tk_pid,
			xprt, (xprt_connected(xprt) ? "is" : "is not"));

	if (!xprt_bound(xprt)) {
		task->tk_status = -EAGAIN;
		return;
	}
	if (!xprt_lock_write(xprt, task))
		return;

	if (test_and_clear_bit(XPRT_CLOSE_WAIT, &xprt->state))
		xprt->ops->close(xprt);

	if (!xprt_connected(xprt)) {
		task->tk_rqstp->rq_bytes_sent = 0;
		task->tk_timeout = task->tk_rqstp->rq_timeout;
		task->tk_rqstp->rq_connect_cookie = xprt->connect_cookie;
		rpc_sleep_on(&xprt->pending, task, xprt_connect_status);

		if (test_bit(XPRT_CLOSING, &xprt->state))
			return;
		if (xprt_test_and_set_connecting(xprt))
			return;
		xprt->stat.connect_start = jiffies;
		xprt->ops->connect(xprt, task);
	}
	xprt_release_write(xprt, task);
}

static void xprt_connect_status(struct rpc_task *task)
{
	struct rpc_xprt	*xprt = task->tk_rqstp->rq_xprt;

	if (task->tk_status == 0) {
		xprt->stat.connect_count++;
		xprt->stat.connect_time += (long)jiffies - xprt->stat.connect_start;
		dprintk("RPC: %5u xprt_connect_status: connection established\n",
				task->tk_pid);
		return;
	}

	switch (task->tk_status) {
	case -ECONNREFUSED:
	case -ECONNRESET:
	case -ECONNABORTED:
	case -ENETUNREACH:
	case -EHOSTUNREACH:
	case -EPIPE:
	case -EAGAIN:
		dprintk("RPC: %5u xprt_connect_status: retrying\n", task->tk_pid);
		break;
	case -ETIMEDOUT:
		dprintk("RPC: %5u xprt_connect_status: connect attempt timed "
				"out\n", task->tk_pid);
		break;
	default:
		dprintk("RPC: %5u xprt_connect_status: error %d connecting to "
				"server %s\n", task->tk_pid, -task->tk_status,
				xprt->servername);
		task->tk_status = -EIO;
	}
}

/**
 * xprt_lookup_rqst - find an RPC request corresponding to an XID
 * @xprt: transport on which the original request was transmitted
 * @xid: RPC XID of incoming reply
 *
 */
struct rpc_rqst *xprt_lookup_rqst(struct rpc_xprt *xprt, __be32 xid)
{
	struct rpc_rqst *entry;

	list_for_each_entry(entry, &xprt->recv, rq_list)
		if (entry->rq_xid == xid) {
			trace_xprt_lookup_rqst(xprt, xid, 0);
			return entry;
		}

	dprintk("RPC:       xprt_lookup_rqst did not find xid %08x\n",
			ntohl(xid));
	trace_xprt_lookup_rqst(xprt, xid, -ENOENT);
	xprt->stat.bad_xids++;
	return NULL;
}
EXPORT_SYMBOL_GPL(xprt_lookup_rqst);

/**
 * xprt_pin_rqst - Pin a request on the transport receive list
 * @req: Request to pin
 *
 * Caller must ensure this is atomic with the call to xprt_lookup_rqst()
 * so should be holding the xprt transport lock.
 */
void xprt_pin_rqst(struct rpc_rqst *req)
{
	set_bit(RPC_TASK_MSG_RECV, &req->rq_task->tk_runstate);
}

/**
 * xprt_unpin_rqst - Unpin a request on the transport receive list
 * @req: Request to pin
 *
 * Caller should be holding the xprt transport lock.
 */
void xprt_unpin_rqst(struct rpc_rqst *req)
{
	struct rpc_task *task = req->rq_task;

	clear_bit(RPC_TASK_MSG_RECV, &task->tk_runstate);
	if (test_bit(RPC_TASK_MSG_RECV_WAIT, &task->tk_runstate))
		wake_up_bit(&task->tk_runstate, RPC_TASK_MSG_RECV);
}

static void xprt_wait_on_pinned_rqst(struct rpc_rqst *req)
__must_hold(&req->rq_xprt->recv_lock)
{
	struct rpc_task *task = req->rq_task;
	
	if (task && test_bit(RPC_TASK_MSG_RECV, &task->tk_runstate)) {
		spin_unlock(&req->rq_xprt->recv_lock);
		set_bit(RPC_TASK_MSG_RECV_WAIT, &task->tk_runstate);
		wait_on_bit(&task->tk_runstate, RPC_TASK_MSG_RECV,
				TASK_UNINTERRUPTIBLE);
		clear_bit(RPC_TASK_MSG_RECV_WAIT, &task->tk_runstate);
		spin_lock(&req->rq_xprt->recv_lock);
	}
}

static void xprt_update_rtt(struct rpc_task *task)
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

/**
 * xprt_complete_rqst - called when reply processing is complete
 * @task: RPC request that recently completed
 * @copied: actual number of bytes received from the transport
 *
 * Caller holds transport lock.
 */
void xprt_complete_rqst(struct rpc_task *task, int copied)
{
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_xprt *xprt = req->rq_xprt;

	dprintk("RPC: %5u xid %08x complete (%d bytes received)\n",
			task->tk_pid, ntohl(req->rq_xid), copied);
	trace_xprt_complete_rqst(xprt, req->rq_xid, copied);

	xprt->stat.recvs++;
	req->rq_rtt = ktime_sub(ktime_get(), req->rq_xtime);
	if (xprt->ops->timer != NULL)
		xprt_update_rtt(task);

	list_del_init(&req->rq_list);
	req->rq_private_buf.len = copied;
	/* Ensure all writes are done before we update */
	/* req->rq_reply_bytes_recvd */
	smp_wmb();
	req->rq_reply_bytes_recvd = copied;
	rpc_wake_up_queued_task(&xprt->pending, task);
}
EXPORT_SYMBOL_GPL(xprt_complete_rqst);

static void xprt_timer(struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_xprt *xprt = req->rq_xprt;

	if (task->tk_status != -ETIMEDOUT)
		return;
	dprintk("RPC: %5u xprt_timer\n", task->tk_pid);

	if (!req->rq_reply_bytes_recvd) {
		if (xprt->ops->timer)
			xprt->ops->timer(xprt, task);
	} else
		task->tk_status = 0;
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
	bool ret = false;

	dprintk("RPC: %5u xprt_prepare_transmit\n", task->tk_pid);

	spin_lock_bh(&xprt->transport_lock);
	if (!req->rq_bytes_sent) {
		if (req->rq_reply_bytes_recvd) {
			task->tk_status = req->rq_reply_bytes_recvd;
			goto out_unlock;
		}
		if ((task->tk_flags & RPC_TASK_NO_RETRANS_TIMEOUT)
		    && xprt_connected(xprt)
		    && req->rq_connect_cookie == xprt->connect_cookie) {
			xprt->ops->set_retrans_timeout(task);
			rpc_sleep_on(&xprt->pending, task, xprt_timer);
			goto out_unlock;
		}
	}
	if (!xprt->ops->reserve_xprt(xprt, task)) {
		task->tk_status = -EAGAIN;
		goto out_unlock;
	}
	ret = true;
out_unlock:
	spin_unlock_bh(&xprt->transport_lock);
	return ret;
}

void xprt_end_transmit(struct rpc_task *task)
{
	xprt_release_write(task->tk_rqstp->rq_xprt, task);
}

/**
 * xprt_transmit - send an RPC request on a transport
 * @task: controlling RPC task
 *
 * We have to copy the iovec because sendmsg fiddles with its contents.
 */
void xprt_transmit(struct rpc_task *task)
{
	struct rpc_rqst	*req = task->tk_rqstp;
	struct rpc_xprt	*xprt = req->rq_xprt;
	int status, numreqs;

	dprintk("RPC: %5u xprt_transmit(%u)\n", task->tk_pid, req->rq_slen);

	if (!req->rq_reply_bytes_recvd) {
		if (list_empty(&req->rq_list) && rpc_reply_expected(task)) {
			/*
			 * Add to the list only if we're expecting a reply
			 */
			/* Update the softirq receive buffer */
			memcpy(&req->rq_private_buf, &req->rq_rcv_buf,
					sizeof(req->rq_private_buf));
			/* Add request to the receive list */
			spin_lock(&xprt->recv_lock);
			list_add_tail(&req->rq_list, &xprt->recv);
			spin_unlock(&xprt->recv_lock);
			xprt_reset_majortimeo(req);
			/* Turn off autodisconnect */
			del_singleshot_timer_sync(&xprt->timer);
		}
	} else if (!req->rq_bytes_sent)
		return;

	req->rq_xtime = ktime_get();
	status = xprt->ops->send_request(task);
	trace_xprt_transmit(xprt, req->rq_xid, status);
	if (status != 0) {
		task->tk_status = status;
		return;
	}
	xprt_inject_disconnect(xprt);

	dprintk("RPC: %5u xmit complete\n", task->tk_pid);
	task->tk_flags |= RPC_TASK_SENT;
	spin_lock_bh(&xprt->transport_lock);

	xprt->ops->set_retrans_timeout(task);

	numreqs = atomic_read(&xprt->num_reqs);
	if (numreqs > xprt->stat.max_slots)
		xprt->stat.max_slots = numreqs;
	xprt->stat.sends++;
	xprt->stat.req_u += xprt->stat.sends - xprt->stat.recvs;
	xprt->stat.bklog_u += xprt->backlog.qlen;
	xprt->stat.sending_u += xprt->sending.qlen;
	xprt->stat.pending_u += xprt->pending.qlen;

	/* Don't race with disconnect */
	if (!xprt_connected(xprt))
		task->tk_status = -ENOTCONN;
	else {
		/*
		 * Sleep on the pending queue since
		 * we're expecting a reply.
		 */
		if (!req->rq_reply_bytes_recvd && rpc_reply_expected(task))
			rpc_sleep_on(&xprt->pending, task, xprt_timer);
		req->rq_connect_cookie = xprt->connect_cookie;
	}
	spin_unlock_bh(&xprt->transport_lock);
}

static void xprt_add_backlog(struct rpc_xprt *xprt, struct rpc_task *task)
{
	set_bit(XPRT_CONGESTED, &xprt->state);
	rpc_sleep_on(&xprt->backlog, task, NULL);
}

static void xprt_wake_up_backlog(struct rpc_xprt *xprt)
{
	if (rpc_wake_up_next(&xprt->backlog) == NULL)
		clear_bit(XPRT_CONGESTED, &xprt->state);
}

static bool xprt_throttle_congested(struct rpc_xprt *xprt, struct rpc_task *task)
{
	bool ret = false;

	if (!test_bit(XPRT_CONGESTED, &xprt->state))
		goto out;
	spin_lock(&xprt->reserve_lock);
	if (test_bit(XPRT_CONGESTED, &xprt->state)) {
		rpc_sleep_on(&xprt->backlog, task, NULL);
		ret = true;
	}
	spin_unlock(&xprt->reserve_lock);
out:
	return ret;
}

static struct rpc_rqst *xprt_dynamic_alloc_slot(struct rpc_xprt *xprt)
{
	struct rpc_rqst *req = ERR_PTR(-EAGAIN);

	if (!atomic_add_unless(&xprt->num_reqs, 1, xprt->max_reqs))
		goto out;
	spin_unlock(&xprt->reserve_lock);
	req = kzalloc(sizeof(struct rpc_rqst), GFP_NOFS);
	spin_lock(&xprt->reserve_lock);
	if (req != NULL)
		goto out;
	atomic_dec(&xprt->num_reqs);
	req = ERR_PTR(-ENOMEM);
out:
	return req;
}

static bool xprt_dynamic_free_slot(struct rpc_xprt *xprt, struct rpc_rqst *req)
{
	if (atomic_add_unless(&xprt->num_reqs, -1, xprt->min_reqs)) {
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
	default:
		task->tk_status = -EAGAIN;
	}
	spin_unlock(&xprt->reserve_lock);
	return;
out_init_req:
	task->tk_status = 0;
	task->tk_rqstp = req;
	xprt_request_init(task, xprt);
	spin_unlock(&xprt->reserve_lock);
}
EXPORT_SYMBOL_GPL(xprt_alloc_slot);

void xprt_lock_and_alloc_slot(struct rpc_xprt *xprt, struct rpc_task *task)
{
	/* Note: grabbing the xprt_lock_write() ensures that we throttle
	 * new slot allocation if the transport is congested (i.e. when
	 * reconnecting a stream transport or when out of socket write
	 * buffer space).
	 */
	if (xprt_lock_write(xprt, task)) {
		xprt_alloc_slot(xprt, task);
		xprt_release_write(xprt, task);
	}
}
EXPORT_SYMBOL_GPL(xprt_lock_and_alloc_slot);

static void xprt_free_slot(struct rpc_xprt *xprt, struct rpc_rqst *req)
{
	spin_lock(&xprt->reserve_lock);
	if (!xprt_dynamic_free_slot(xprt, req)) {
		memset(req, 0, sizeof(*req));	/* mark unused */
		list_add(&req->rq_list, &xprt->free);
	}
	xprt_wake_up_backlog(xprt);
	spin_unlock(&xprt->reserve_lock);
}

static void xprt_free_all_slots(struct rpc_xprt *xprt)
{
	struct rpc_rqst *req;
	while (!list_empty(&xprt->free)) {
		req = list_first_entry(&xprt->free, struct rpc_rqst, rq_list);
		list_del(&req->rq_list);
		kfree(req);
	}
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
	atomic_set(&xprt->num_reqs, num_prealloc);

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
	kfree_rcu(xprt, rcu);
}
EXPORT_SYMBOL_GPL(xprt_free);

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

	task->tk_timeout = 0;
	task->tk_status = -EAGAIN;
	if (!xprt_throttle_congested(xprt, task))
		xprt->ops->alloc_slot(xprt, task);
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

	task->tk_timeout = 0;
	task->tk_status = -EAGAIN;
	xprt->ops->alloc_slot(xprt, task);
}

static inline __be32 xprt_alloc_xid(struct rpc_xprt *xprt)
{
	return (__force __be32)xprt->xid++;
}

static inline void xprt_init_xid(struct rpc_xprt *xprt)
{
	xprt->xid = prandom_u32();
}

static void xprt_request_init(struct rpc_task *task, struct rpc_xprt *xprt)
{
	struct rpc_rqst	*req = task->tk_rqstp;

	INIT_LIST_HEAD(&req->rq_list);
	req->rq_timeout = task->tk_client->cl_timeout->to_initval;
	req->rq_task	= task;
	req->rq_xprt    = xprt;
	req->rq_buffer  = NULL;
	req->rq_xid     = xprt_alloc_xid(xprt);
	req->rq_connect_cookie = xprt->connect_cookie - 1;
	req->rq_bytes_sent = 0;
	req->rq_snd_buf.len = 0;
	req->rq_snd_buf.buflen = 0;
	req->rq_rcv_buf.len = 0;
	req->rq_rcv_buf.buflen = 0;
	req->rq_release_snd_buf = NULL;
	xprt_reset_majortimeo(req);
	dprintk("RPC: %5u reserved req %p xid %08x\n", task->tk_pid,
			req, ntohl(req->rq_xid));
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
			if (xprt->snd_task == task)
				xprt_release_write(xprt, task);
		}
		return;
	}

	xprt = req->rq_xprt;
	if (task->tk_ops->rpc_count_stats != NULL)
		task->tk_ops->rpc_count_stats(task, task->tk_calldata);
	else if (task->tk_client)
		rpc_count_iostats(task, task->tk_client->cl_metrics);
	spin_lock(&xprt->recv_lock);
	if (!list_empty(&req->rq_list)) {
		list_del(&req->rq_list);
		xprt_wait_on_pinned_rqst(req);
	}
	spin_unlock(&xprt->recv_lock);
	spin_lock_bh(&xprt->transport_lock);
	xprt->ops->release_xprt(xprt, task);
	if (xprt->ops->release_request)
		xprt->ops->release_request(task);
	xprt->last_used = jiffies;
	xprt_schedule_autodisconnect(xprt);
	spin_unlock_bh(&xprt->transport_lock);
	if (req->rq_buffer)
		xprt->ops->buf_free(task);
	xprt_inject_disconnect(xprt);
	if (req->rq_cred != NULL)
		put_rpccred(req->rq_cred);
	task->tk_rqstp = NULL;
	if (req->rq_release_snd_buf)
		req->rq_release_snd_buf(req);

	dprintk("RPC: %5u release request %p\n", task->tk_pid, req);
	if (likely(!bc_prealloc(req)))
		xprt_free_slot(xprt, req);
	else
		xprt_free_bc_request(req);
}

static void xprt_init(struct rpc_xprt *xprt, struct net *net)
{
	kref_init(&xprt->kref);

	spin_lock_init(&xprt->transport_lock);
	spin_lock_init(&xprt->reserve_lock);
	spin_lock_init(&xprt->recv_lock);

	INIT_LIST_HEAD(&xprt->free);
	INIT_LIST_HEAD(&xprt->recv);
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
	rpc_init_priority_wait_queue(&xprt->sending, "xprt_sending");
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
	struct xprt_class *t;

	spin_lock(&xprt_list_lock);
	list_for_each_entry(t, &xprt_list, list) {
		if (t->ident == args->ident) {
			spin_unlock(&xprt_list_lock);
			goto found;
		}
	}
	spin_unlock(&xprt_list_lock);
	dprintk("RPC: transport (%d) not supported\n", args->ident);
	return ERR_PTR(-EIO);

found:
	xprt = t->setup(args);
	if (IS_ERR(xprt)) {
		dprintk("RPC:       xprt_create_transport: failed, %ld\n",
				-PTR_ERR(xprt));
		goto out;
	}
	if (args->flags & XPRT_CREATE_NO_IDLE_TIMEOUT)
		xprt->idle_timeout = 0;
	INIT_WORK(&xprt->task_cleanup, xprt_autoclose);
	if (xprt_has_timer(xprt))
		setup_timer(&xprt->timer, xprt_init_autodisconnect,
			    (unsigned long)xprt);
	else
		init_timer(&xprt->timer);

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

	dprintk("RPC:       created transport %p with %u slots\n", xprt,
			xprt->max_reqs);
out:
	return xprt;
}

/**
 * xprt_destroy - destroy an RPC transport, killing off all requests.
 * @xprt: transport to destroy
 *
 */
static void xprt_destroy(struct rpc_xprt *xprt)
{
	dprintk("RPC:       destroying transport %p\n", xprt);

	/* Exclude transport connect/disconnect handlers */
	wait_on_bit_lock(&xprt->state, XPRT_LOCKED, TASK_UNINTERRUPTIBLE);

	del_timer_sync(&xprt->timer);

	rpc_xprt_debugfs_unregister(xprt);
	rpc_destroy_wait_queue(&xprt->binding);
	rpc_destroy_wait_queue(&xprt->pending);
	rpc_destroy_wait_queue(&xprt->sending);
	rpc_destroy_wait_queue(&xprt->backlog);
	cancel_work_sync(&xprt->task_cleanup);
	kfree(xprt->servername);
	/*
	 * Tear down transport state and free the rpc_xprt
	 */
	xprt->ops->destroy(xprt);
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
