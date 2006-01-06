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
 *	transport's wait list. At the same time, it installs a timer that
 *	is run after the packet's timeout has expired.
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
#include <linux/random.h>

#include <linux/sunrpc/clnt.h>

/*
 * Local variables
 */

#ifdef RPC_DEBUG
# undef  RPC_DEBUG_DATA
# define RPCDBG_FACILITY	RPCDBG_XPRT
#endif

/*
 * Local functions
 */
static void	xprt_request_init(struct rpc_task *, struct rpc_xprt *);
static inline void	do_xprt_reserve(struct rpc_task *);
static void	xprt_connect_status(struct rpc_task *task);
static int      __xprt_get_cong(struct rpc_xprt *, struct rpc_task *);

/*
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
#define RPC_CWNDSHIFT		(8U)
#define RPC_CWNDSCALE		(1U << RPC_CWNDSHIFT)
#define RPC_INITCWND		RPC_CWNDSCALE
#define RPC_MAXCWND(xprt)	((xprt)->max_reqs << RPC_CWNDSHIFT)

#define RPCXPRT_CONGESTED(xprt) ((xprt)->cong >= (xprt)->cwnd)

/**
 * xprt_reserve_xprt - serialize write access to transports
 * @task: task that is requesting access to the transport
 *
 * This prevents mixing the payload of separate requests, and prevents
 * transport connects from colliding with writes.  No congestion control
 * is provided.
 */
int xprt_reserve_xprt(struct rpc_task *task)
{
	struct rpc_xprt	*xprt = task->tk_xprt;
	struct rpc_rqst *req = task->tk_rqstp;

	if (test_and_set_bit(XPRT_LOCKED, &xprt->state)) {
		if (task == xprt->snd_task)
			return 1;
		if (task == NULL)
			return 0;
		goto out_sleep;
	}
	xprt->snd_task = task;
	if (req) {
		req->rq_bytes_sent = 0;
		req->rq_ntrans++;
	}
	return 1;

out_sleep:
	dprintk("RPC: %4d failed to lock transport %p\n",
			task->tk_pid, xprt);
	task->tk_timeout = 0;
	task->tk_status = -EAGAIN;
	if (req && req->rq_ntrans)
		rpc_sleep_on(&xprt->resend, task, NULL, NULL);
	else
		rpc_sleep_on(&xprt->sending, task, NULL, NULL);
	return 0;
}

static void xprt_clear_locked(struct rpc_xprt *xprt)
{
	xprt->snd_task = NULL;
	if (!test_bit(XPRT_CLOSE_WAIT, &xprt->state) || xprt->shutdown) {
		smp_mb__before_clear_bit();
		clear_bit(XPRT_LOCKED, &xprt->state);
		smp_mb__after_clear_bit();
	} else
		schedule_work(&xprt->task_cleanup);
}

/*
 * xprt_reserve_xprt_cong - serialize write access to transports
 * @task: task that is requesting access to the transport
 *
 * Same as xprt_reserve_xprt, but Van Jacobson congestion control is
 * integrated into the decision of whether a request is allowed to be
 * woken up and given access to the transport.
 */
int xprt_reserve_xprt_cong(struct rpc_task *task)
{
	struct rpc_xprt	*xprt = task->tk_xprt;
	struct rpc_rqst *req = task->tk_rqstp;

	if (test_and_set_bit(XPRT_LOCKED, &xprt->state)) {
		if (task == xprt->snd_task)
			return 1;
		goto out_sleep;
	}
	if (__xprt_get_cong(xprt, task)) {
		xprt->snd_task = task;
		if (req) {
			req->rq_bytes_sent = 0;
			req->rq_ntrans++;
		}
		return 1;
	}
	xprt_clear_locked(xprt);
out_sleep:
	dprintk("RPC: %4d failed to lock transport %p\n", task->tk_pid, xprt);
	task->tk_timeout = 0;
	task->tk_status = -EAGAIN;
	if (req && req->rq_ntrans)
		rpc_sleep_on(&xprt->resend, task, NULL, NULL);
	else
		rpc_sleep_on(&xprt->sending, task, NULL, NULL);
	return 0;
}

static inline int xprt_lock_write(struct rpc_xprt *xprt, struct rpc_task *task)
{
	int retval;

	spin_lock_bh(&xprt->transport_lock);
	retval = xprt->ops->reserve_xprt(task);
	spin_unlock_bh(&xprt->transport_lock);
	return retval;
}

static void __xprt_lock_write_next(struct rpc_xprt *xprt)
{
	struct rpc_task *task;
	struct rpc_rqst *req;

	if (test_and_set_bit(XPRT_LOCKED, &xprt->state))
		return;

	task = rpc_wake_up_next(&xprt->resend);
	if (!task) {
		task = rpc_wake_up_next(&xprt->sending);
		if (!task)
			goto out_unlock;
	}

	req = task->tk_rqstp;
	xprt->snd_task = task;
	if (req) {
		req->rq_bytes_sent = 0;
		req->rq_ntrans++;
	}
	return;

out_unlock:
	xprt_clear_locked(xprt);
}

static void __xprt_lock_write_next_cong(struct rpc_xprt *xprt)
{
	struct rpc_task *task;

	if (test_and_set_bit(XPRT_LOCKED, &xprt->state))
		return;
	if (RPCXPRT_CONGESTED(xprt))
		goto out_unlock;
	task = rpc_wake_up_next(&xprt->resend);
	if (!task) {
		task = rpc_wake_up_next(&xprt->sending);
		if (!task)
			goto out_unlock;
	}
	if (__xprt_get_cong(xprt, task)) {
		struct rpc_rqst *req = task->tk_rqstp;
		xprt->snd_task = task;
		if (req) {
			req->rq_bytes_sent = 0;
			req->rq_ntrans++;
		}
		return;
	}
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
}

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
}

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
	dprintk("RPC: %4d xprt_cwnd_limited cong = %ld cwnd = %ld\n",
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
	__xprt_put_cong(task->tk_xprt, task->tk_rqstp);
}

/**
 * xprt_adjust_cwnd - adjust transport congestion window
 * @task: recently completed RPC request used to adjust window
 * @result: result code of completed RPC request
 *
 * We use a time-smoothed congestion estimator to avoid heavy oscillation.
 */
void xprt_adjust_cwnd(struct rpc_task *task, int result)
{
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_xprt *xprt = task->tk_xprt;
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
	dprintk("RPC:      cong %ld, cwnd was %ld, now %ld\n",
			xprt->cong, xprt->cwnd, cwnd);
	xprt->cwnd = cwnd;
	__xprt_put_cong(xprt, req);
}

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

/**
 * xprt_wait_for_buffer_space - wait for transport output buffer to clear
 * @task: task to be put to sleep
 *
 */
void xprt_wait_for_buffer_space(struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_xprt *xprt = req->rq_xprt;

	task->tk_timeout = req->rq_timeout;
	rpc_sleep_on(&xprt->pending, task, NULL, NULL);
}

/**
 * xprt_write_space - wake the task waiting for transport output buffer space
 * @xprt: transport with waiting tasks
 *
 * Can be called in a soft IRQ context, so xprt_write_space never sleeps.
 */
void xprt_write_space(struct rpc_xprt *xprt)
{
	if (unlikely(xprt->shutdown))
		return;

	spin_lock_bh(&xprt->transport_lock);
	if (xprt->snd_task) {
		dprintk("RPC:      write space: waking waiting task on xprt %p\n",
				xprt);
		rpc_wake_up_task(xprt->snd_task);
	}
	spin_unlock_bh(&xprt->transport_lock);
}

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

/*
 * xprt_set_retrans_timeout_rtt - set a request's retransmit timeout
 * @task: task whose timeout is to be set
 * 
 * Set a request's retransmit timeout using the RTT estimator.
 */
void xprt_set_retrans_timeout_rtt(struct rpc_task *task)
{
	int timer = task->tk_msg.rpc_proc->p_timer;
	struct rpc_rtt *rtt = task->tk_client->cl_rtt;
	struct rpc_rqst *req = task->tk_rqstp;
	unsigned long max_timeout = req->rq_xprt->timeout.to_maxval;

	task->tk_timeout = rpc_calc_rto(rtt, timer);
	task->tk_timeout <<= rpc_ntimeo(rtt, timer) + req->rq_retries;
	if (task->tk_timeout > max_timeout || task->tk_timeout == 0)
		task->tk_timeout = max_timeout;
}

static void xprt_reset_majortimeo(struct rpc_rqst *req)
{
	struct rpc_timeout *to = &req->rq_xprt->timeout;

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
	struct rpc_timeout *to = &xprt->timeout;
	int status = 0;

	if (time_before(jiffies, req->rq_majortimeo)) {
		if (to->to_exponential)
			req->rq_timeout <<= 1;
		else
			req->rq_timeout += to->to_increment;
		if (to->to_maxval && req->rq_timeout >= to->to_maxval)
			req->rq_timeout = to->to_maxval;
		req->rq_retries++;
		pprintk("RPC: %lu retrans\n", jiffies);
	} else {
		req->rq_timeout = to->to_initval;
		req->rq_retries = 0;
		xprt_reset_majortimeo(req);
		/* Reset the RTT counters == "slow start" */
		spin_lock_bh(&xprt->transport_lock);
		rpc_init_rtt(req->rq_task->tk_client->cl_rtt, to->to_initval);
		spin_unlock_bh(&xprt->transport_lock);
		pprintk("RPC: %lu timeout\n", jiffies);
		status = -ETIMEDOUT;
	}

	if (req->rq_timeout == 0) {
		printk(KERN_WARNING "xprt_adjust_timeout: rq_timeout = 0!\n");
		req->rq_timeout = 5 * HZ;
	}
	return status;
}

static void xprt_autoclose(void *args)
{
	struct rpc_xprt *xprt = (struct rpc_xprt *)args;

	xprt_disconnect(xprt);
	xprt->ops->close(xprt);
	xprt_release_write(xprt, NULL);
}

/**
 * xprt_disconnect - mark a transport as disconnected
 * @xprt: transport to flag for disconnect
 *
 */
void xprt_disconnect(struct rpc_xprt *xprt)
{
	dprintk("RPC:      disconnected transport %p\n", xprt);
	spin_lock_bh(&xprt->transport_lock);
	xprt_clear_connected(xprt);
	xprt_wake_pending_tasks(xprt, -ENOTCONN);
	spin_unlock_bh(&xprt->transport_lock);
}

static void
xprt_init_autodisconnect(unsigned long data)
{
	struct rpc_xprt *xprt = (struct rpc_xprt *)data;

	spin_lock(&xprt->transport_lock);
	if (!list_empty(&xprt->recv) || xprt->shutdown)
		goto out_abort;
	if (test_and_set_bit(XPRT_LOCKED, &xprt->state))
		goto out_abort;
	spin_unlock(&xprt->transport_lock);
	if (xprt_connecting(xprt))
		xprt_release_write(xprt, NULL);
	else
		schedule_work(&xprt->task_cleanup);
	return;
out_abort:
	spin_unlock(&xprt->transport_lock);
}

/**
 * xprt_connect - schedule a transport connect operation
 * @task: RPC task that is requesting the connect
 *
 */
void xprt_connect(struct rpc_task *task)
{
	struct rpc_xprt	*xprt = task->tk_xprt;

	dprintk("RPC: %4d xprt_connect xprt %p %s connected\n", task->tk_pid,
			xprt, (xprt_connected(xprt) ? "is" : "is not"));

	if (!xprt->addr.sin_port) {
		task->tk_status = -EIO;
		return;
	}
	if (!xprt_lock_write(xprt, task))
		return;
	if (xprt_connected(xprt))
		xprt_release_write(xprt, task);
	else {
		if (task->tk_rqstp)
			task->tk_rqstp->rq_bytes_sent = 0;

		task->tk_timeout = xprt->connect_timeout;
		rpc_sleep_on(&xprt->pending, task, xprt_connect_status, NULL);
		xprt->ops->connect(task);
	}
	return;
}

static void xprt_connect_status(struct rpc_task *task)
{
	struct rpc_xprt	*xprt = task->tk_xprt;

	if (task->tk_status >= 0) {
		dprintk("RPC: %4d xprt_connect_status: connection established\n",
				task->tk_pid);
		return;
	}

	switch (task->tk_status) {
	case -ECONNREFUSED:
	case -ECONNRESET:
		dprintk("RPC: %4d xprt_connect_status: server %s refused connection\n",
				task->tk_pid, task->tk_client->cl_server);
		break;
	case -ENOTCONN:
		dprintk("RPC: %4d xprt_connect_status: connection broken\n",
				task->tk_pid);
		break;
	case -ETIMEDOUT:
		dprintk("RPC: %4d xprt_connect_status: connect attempt timed out\n",
				task->tk_pid);
		break;
	default:
		dprintk("RPC: %4d xprt_connect_status: error %d connecting to server %s\n",
				task->tk_pid, -task->tk_status, task->tk_client->cl_server);
		xprt_release_write(xprt, task);
		task->tk_status = -EIO;
		return;
	}

	/* if soft mounted, just cause this RPC to fail */
	if (RPC_IS_SOFT(task)) {
		xprt_release_write(xprt, task);
		task->tk_status = -EIO;
	}
}

/**
 * xprt_lookup_rqst - find an RPC request corresponding to an XID
 * @xprt: transport on which the original request was transmitted
 * @xid: RPC XID of incoming reply
 *
 */
struct rpc_rqst *xprt_lookup_rqst(struct rpc_xprt *xprt, u32 xid)
{
	struct list_head *pos;
	struct rpc_rqst	*req = NULL;

	list_for_each(pos, &xprt->recv) {
		struct rpc_rqst *entry = list_entry(pos, struct rpc_rqst, rq_list);
		if (entry->rq_xid == xid) {
			req = entry;
			break;
		}
	}
	return req;
}

/**
 * xprt_update_rtt - update an RPC client's RTT state after receiving a reply
 * @task: RPC request that recently completed
 *
 */
void xprt_update_rtt(struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_rtt *rtt = task->tk_client->cl_rtt;
	unsigned timer = task->tk_msg.rpc_proc->p_timer;

	if (timer) {
		if (req->rq_ntrans == 1)
			rpc_update_rtt(rtt, timer,
					(long)jiffies - req->rq_xtime);
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

	dprintk("RPC: %5u xid %08x complete (%d bytes received)\n",
			task->tk_pid, ntohl(req->rq_xid), copied);

	list_del_init(&req->rq_list);
	req->rq_received = req->rq_private_buf.len = copied;
	rpc_wake_up_task(task);
}

static void xprt_timer(struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_xprt *xprt = req->rq_xprt;

	dprintk("RPC: %4d xprt_timer\n", task->tk_pid);

	spin_lock(&xprt->transport_lock);
	if (!req->rq_received) {
		if (xprt->ops->timer)
			xprt->ops->timer(task);
		task->tk_status = -ETIMEDOUT;
	}
	task->tk_timeout = 0;
	rpc_wake_up_task(task);
	spin_unlock(&xprt->transport_lock);
}

/**
 * xprt_prepare_transmit - reserve the transport before sending a request
 * @task: RPC task about to send a request
 *
 */
int xprt_prepare_transmit(struct rpc_task *task)
{
	struct rpc_rqst	*req = task->tk_rqstp;
	struct rpc_xprt	*xprt = req->rq_xprt;
	int err = 0;

	dprintk("RPC: %4d xprt_prepare_transmit\n", task->tk_pid);

	spin_lock_bh(&xprt->transport_lock);
	if (req->rq_received && !req->rq_bytes_sent) {
		err = req->rq_received;
		goto out_unlock;
	}
	if (!xprt->ops->reserve_xprt(task)) {
		err = -EAGAIN;
		goto out_unlock;
	}

	if (!xprt_connected(xprt)) {
		err = -ENOTCONN;
		goto out_unlock;
	}
out_unlock:
	spin_unlock_bh(&xprt->transport_lock);
	return err;
}

void
xprt_abort_transmit(struct rpc_task *task)
{
	struct rpc_xprt	*xprt = task->tk_xprt;

	xprt_release_write(xprt, task);
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
	int status;

	dprintk("RPC: %4d xprt_transmit(%u)\n", task->tk_pid, req->rq_slen);

	smp_rmb();
	if (!req->rq_received) {
		if (list_empty(&req->rq_list)) {
			spin_lock_bh(&xprt->transport_lock);
			/* Update the softirq receive buffer */
			memcpy(&req->rq_private_buf, &req->rq_rcv_buf,
					sizeof(req->rq_private_buf));
			/* Add request to the receive list */
			list_add_tail(&req->rq_list, &xprt->recv);
			spin_unlock_bh(&xprt->transport_lock);
			xprt_reset_majortimeo(req);
			/* Turn off autodisconnect */
			del_singleshot_timer_sync(&xprt->timer);
		}
	} else if (!req->rq_bytes_sent)
		return;

	status = xprt->ops->send_request(task);
	if (status == 0) {
		dprintk("RPC: %4d xmit complete\n", task->tk_pid);
		spin_lock_bh(&xprt->transport_lock);
		xprt->ops->set_retrans_timeout(task);
		/* Don't race with disconnect */
		if (!xprt_connected(xprt))
			task->tk_status = -ENOTCONN;
		else if (!req->rq_received)
			rpc_sleep_on(&xprt->pending, task, NULL, xprt_timer);
		xprt->ops->release_xprt(xprt, task);
		spin_unlock_bh(&xprt->transport_lock);
		return;
	}

	/* Note: at this point, task->tk_sleeping has not yet been set,
	 *	 hence there is no danger of the waking up task being put on
	 *	 schedq, and being picked up by a parallel run of rpciod().
	 */
	task->tk_status = status;

	switch (status) {
	case -ECONNREFUSED:
		rpc_sleep_on(&xprt->sending, task, NULL, NULL);
	case -EAGAIN:
	case -ENOTCONN:
		return;
	default:
		break;
	}
	xprt_release_write(xprt, task);
	return;
}

static inline void do_xprt_reserve(struct rpc_task *task)
{
	struct rpc_xprt	*xprt = task->tk_xprt;

	task->tk_status = 0;
	if (task->tk_rqstp)
		return;
	if (!list_empty(&xprt->free)) {
		struct rpc_rqst	*req = list_entry(xprt->free.next, struct rpc_rqst, rq_list);
		list_del_init(&req->rq_list);
		task->tk_rqstp = req;
		xprt_request_init(task, xprt);
		return;
	}
	dprintk("RPC:      waiting for request slot\n");
	task->tk_status = -EAGAIN;
	task->tk_timeout = 0;
	rpc_sleep_on(&xprt->backlog, task, NULL, NULL);
}

/**
 * xprt_reserve - allocate an RPC request slot
 * @task: RPC task requesting a slot allocation
 *
 * If no more slots are available, place the task on the transport's
 * backlog queue.
 */
void xprt_reserve(struct rpc_task *task)
{
	struct rpc_xprt	*xprt = task->tk_xprt;

	task->tk_status = -EIO;
	spin_lock(&xprt->reserve_lock);
	do_xprt_reserve(task);
	spin_unlock(&xprt->reserve_lock);
}

static inline u32 xprt_alloc_xid(struct rpc_xprt *xprt)
{
	return xprt->xid++;
}

static inline void xprt_init_xid(struct rpc_xprt *xprt)
{
	get_random_bytes(&xprt->xid, sizeof(xprt->xid));
}

static void xprt_request_init(struct rpc_task *task, struct rpc_xprt *xprt)
{
	struct rpc_rqst	*req = task->tk_rqstp;

	req->rq_timeout = xprt->timeout.to_initval;
	req->rq_task	= task;
	req->rq_xprt    = xprt;
	req->rq_buffer  = NULL;
	req->rq_bufsize = 0;
	req->rq_xid     = xprt_alloc_xid(xprt);
	req->rq_release_snd_buf = NULL;
	dprintk("RPC: %4d reserved req %p xid %08x\n", task->tk_pid,
			req, ntohl(req->rq_xid));
}

/**
 * xprt_release - release an RPC request slot
 * @task: task which is finished with the slot
 *
 */
void xprt_release(struct rpc_task *task)
{
	struct rpc_xprt	*xprt = task->tk_xprt;
	struct rpc_rqst	*req;

	if (!(req = task->tk_rqstp))
		return;
	spin_lock_bh(&xprt->transport_lock);
	xprt->ops->release_xprt(xprt, task);
	if (xprt->ops->release_request)
		xprt->ops->release_request(task);
	if (!list_empty(&req->rq_list))
		list_del(&req->rq_list);
	xprt->last_used = jiffies;
	if (list_empty(&xprt->recv))
		mod_timer(&xprt->timer,
				xprt->last_used + xprt->idle_timeout);
	spin_unlock_bh(&xprt->transport_lock);
	xprt->ops->buf_free(task);
	task->tk_rqstp = NULL;
	if (req->rq_release_snd_buf)
		req->rq_release_snd_buf(req);
	memset(req, 0, sizeof(*req));	/* mark unused */

	dprintk("RPC: %4d release request %p\n", task->tk_pid, req);

	spin_lock(&xprt->reserve_lock);
	list_add(&req->rq_list, &xprt->free);
	rpc_wake_up_next(&xprt->backlog);
	spin_unlock(&xprt->reserve_lock);
}

/**
 * xprt_set_timeout - set constant RPC timeout
 * @to: RPC timeout parameters to set up
 * @retr: number of retries
 * @incr: amount of increase after each retry
 *
 */
void xprt_set_timeout(struct rpc_timeout *to, unsigned int retr, unsigned long incr)
{
	to->to_initval   = 
	to->to_increment = incr;
	to->to_maxval    = to->to_initval + (incr * retr);
	to->to_retries   = retr;
	to->to_exponential = 0;
}

static struct rpc_xprt *xprt_setup(int proto, struct sockaddr_in *ap, struct rpc_timeout *to)
{
	int result;
	struct rpc_xprt	*xprt;
	struct rpc_rqst	*req;

	if ((xprt = kmalloc(sizeof(struct rpc_xprt), GFP_KERNEL)) == NULL)
		return ERR_PTR(-ENOMEM);
	memset(xprt, 0, sizeof(*xprt)); /* Nnnngh! */

	xprt->addr = *ap;

	switch (proto) {
	case IPPROTO_UDP:
		result = xs_setup_udp(xprt, to);
		break;
	case IPPROTO_TCP:
		result = xs_setup_tcp(xprt, to);
		break;
	default:
		printk(KERN_ERR "RPC: unrecognized transport protocol: %d\n",
				proto);
		result = -EIO;
		break;
	}
	if (result) {
		kfree(xprt);
		return ERR_PTR(result);
	}

	spin_lock_init(&xprt->transport_lock);
	spin_lock_init(&xprt->reserve_lock);

	INIT_LIST_HEAD(&xprt->free);
	INIT_LIST_HEAD(&xprt->recv);
	INIT_WORK(&xprt->task_cleanup, xprt_autoclose, xprt);
	init_timer(&xprt->timer);
	xprt->timer.function = xprt_init_autodisconnect;
	xprt->timer.data = (unsigned long) xprt;
	xprt->last_used = jiffies;
	xprt->cwnd = RPC_INITCWND;

	rpc_init_wait_queue(&xprt->pending, "xprt_pending");
	rpc_init_wait_queue(&xprt->sending, "xprt_sending");
	rpc_init_wait_queue(&xprt->resend, "xprt_resend");
	rpc_init_priority_wait_queue(&xprt->backlog, "xprt_backlog");

	/* initialize free list */
	for (req = &xprt->slot[xprt->max_reqs-1]; req >= &xprt->slot[0]; req--)
		list_add(&req->rq_list, &xprt->free);

	xprt_init_xid(xprt);

	dprintk("RPC:      created transport %p with %u slots\n", xprt,
			xprt->max_reqs);
	
	return xprt;
}

/**
 * xprt_create_proto - create an RPC client transport
 * @proto: requested transport protocol
 * @sap: remote peer's address
 * @to: timeout parameters for new transport
 *
 */
struct rpc_xprt *xprt_create_proto(int proto, struct sockaddr_in *sap, struct rpc_timeout *to)
{
	struct rpc_xprt	*xprt;

	xprt = xprt_setup(proto, sap, to);
	if (IS_ERR(xprt))
		dprintk("RPC:      xprt_create_proto failed\n");
	else
		dprintk("RPC:      xprt_create_proto created xprt %p\n", xprt);
	return xprt;
}

/**
 * xprt_destroy - destroy an RPC transport, killing off all requests.
 * @xprt: transport to destroy
 *
 */
int xprt_destroy(struct rpc_xprt *xprt)
{
	dprintk("RPC:      destroying transport %p\n", xprt);
	xprt->shutdown = 1;
	del_timer_sync(&xprt->timer);
	xprt->ops->destroy(xprt);
	kfree(xprt);

	return 0;
}
