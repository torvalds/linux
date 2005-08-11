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
 *	the request struct, and calls xprt_call().
 *  -	xprt_call transmits the message and installs the caller on the
 *	socket's wait list. At the same time, it installs a timer that
 *	is run after the packet's timeout has expired.
 *  -	When a packet arrives, the data_ready handler walks the list of
 *	pending requests for that socket. If a matching XID is found, the
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

#define XPRT_MAX_BACKOFF	(8)

/*
 * Local functions
 */
static void	xprt_request_init(struct rpc_task *, struct rpc_xprt *);
static inline void	do_xprt_reserve(struct rpc_task *);
static void	xprt_connect_status(struct rpc_task *task);
static int      __xprt_get_cong(struct rpc_xprt *, struct rpc_task *);

static int	xprt_clear_backlog(struct rpc_xprt *xprt);

/*
 * Serialize write access to sockets, in order to prevent different
 * requests from interfering with each other.
 * Also prevents TCP socket connects from colliding with writes.
 */
static int
__xprt_lock_write(struct rpc_xprt *xprt, struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;

	if (test_and_set_bit(XPRT_LOCKED, &xprt->sockstate)) {
		if (task == xprt->snd_task)
			return 1;
		goto out_sleep;
	}
	if (xprt->nocong || __xprt_get_cong(xprt, task)) {
		xprt->snd_task = task;
		if (req) {
			req->rq_bytes_sent = 0;
			req->rq_ntrans++;
		}
		return 1;
	}
	smp_mb__before_clear_bit();
	clear_bit(XPRT_LOCKED, &xprt->sockstate);
	smp_mb__after_clear_bit();
out_sleep:
	dprintk("RPC: %4d failed to lock socket %p\n", task->tk_pid, xprt);
	task->tk_timeout = 0;
	task->tk_status = -EAGAIN;
	if (req && req->rq_ntrans)
		rpc_sleep_on(&xprt->resend, task, NULL, NULL);
	else
		rpc_sleep_on(&xprt->sending, task, NULL, NULL);
	return 0;
}

static inline int
xprt_lock_write(struct rpc_xprt *xprt, struct rpc_task *task)
{
	int retval;

	spin_lock_bh(&xprt->transport_lock);
	retval = __xprt_lock_write(xprt, task);
	spin_unlock_bh(&xprt->transport_lock);
	return retval;
}


static void
__xprt_lock_write_next(struct rpc_xprt *xprt)
{
	struct rpc_task *task;

	if (test_and_set_bit(XPRT_LOCKED, &xprt->sockstate))
		return;
	if (!xprt->nocong && RPCXPRT_CONGESTED(xprt))
		goto out_unlock;
	task = rpc_wake_up_next(&xprt->resend);
	if (!task) {
		task = rpc_wake_up_next(&xprt->sending);
		if (!task)
			goto out_unlock;
	}
	if (xprt->nocong || __xprt_get_cong(xprt, task)) {
		struct rpc_rqst *req = task->tk_rqstp;
		xprt->snd_task = task;
		if (req) {
			req->rq_bytes_sent = 0;
			req->rq_ntrans++;
		}
		return;
	}
out_unlock:
	smp_mb__before_clear_bit();
	clear_bit(XPRT_LOCKED, &xprt->sockstate);
	smp_mb__after_clear_bit();
}

/*
 * Releases the socket for use by other requests.
 */
static void
__xprt_release_write(struct rpc_xprt *xprt, struct rpc_task *task)
{
	if (xprt->snd_task == task) {
		xprt->snd_task = NULL;
		smp_mb__before_clear_bit();
		clear_bit(XPRT_LOCKED, &xprt->sockstate);
		smp_mb__after_clear_bit();
		__xprt_lock_write_next(xprt);
	}
}

static inline void
xprt_release_write(struct rpc_xprt *xprt, struct rpc_task *task)
{
	spin_lock_bh(&xprt->transport_lock);
	__xprt_release_write(xprt, task);
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
	__xprt_lock_write_next(xprt);
}

/*
 * Adjust RPC congestion window
 * We use a time-smoothed congestion estimator to avoid heavy oscillation.
 */
static void
xprt_adjust_cwnd(struct rpc_xprt *xprt, int result)
{
	unsigned long	cwnd;

	cwnd = xprt->cwnd;
	if (result >= 0 && cwnd <= xprt->cong) {
		/* The (cwnd >> 1) term makes sure
		 * the result gets rounded properly. */
		cwnd += (RPC_CWNDSCALE * RPC_CWNDSCALE + (cwnd >> 1)) / cwnd;
		if (cwnd > RPC_MAXCWND(xprt))
			cwnd = RPC_MAXCWND(xprt);
		__xprt_lock_write_next(xprt);
	} else if (result == -ETIMEDOUT) {
		cwnd >>= 1;
		if (cwnd < RPC_CWNDSCALE)
			cwnd = RPC_CWNDSCALE;
	}
	dprintk("RPC:      cong %ld, cwnd was %ld, now %ld\n",
			xprt->cong, xprt->cwnd, cwnd);
	xprt->cwnd = cwnd;
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

static void
xprt_socket_autoclose(void *args)
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
	rpc_wake_up_status(&xprt->pending, -ENOTCONN);
	spin_unlock_bh(&xprt->transport_lock);
}

static void
xprt_init_autodisconnect(unsigned long data)
{
	struct rpc_xprt *xprt = (struct rpc_xprt *)data;

	spin_lock(&xprt->transport_lock);
	if (!list_empty(&xprt->recv) || xprt->shutdown)
		goto out_abort;
	if (test_and_set_bit(XPRT_LOCKED, &xprt->sockstate))
		goto out_abort;
	spin_unlock(&xprt->transport_lock);
	/* Let keventd close the socket */
	if (test_bit(XPRT_CONNECTING, &xprt->sockstate) != 0)
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

	if (xprt->shutdown) {
		task->tk_status = -EIO;
		return;
	}
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

		task->tk_timeout = RPC_CONNECT_TIMEOUT;
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
 * xprt_complete_rqst - called when reply processing is complete
 * @xprt: controlling transport
 * @req: RPC request that just completed
 * @copied: actual number of bytes received from the transport
 *
 */
void xprt_complete_rqst(struct rpc_xprt *xprt, struct rpc_rqst *req, int copied)
{
	struct rpc_task	*task = req->rq_task;
	struct rpc_clnt *clnt = task->tk_client;

	/* Adjust congestion window */
	if (!xprt->nocong) {
		unsigned timer = task->tk_msg.rpc_proc->p_timer;
		xprt_adjust_cwnd(xprt, copied);
		__xprt_put_cong(xprt, req);
		if (timer) {
			if (req->rq_ntrans == 1)
				rpc_update_rtt(clnt->cl_rtt, timer,
						(long)jiffies - req->rq_xtime);
			rpc_set_timeo(clnt->cl_rtt, timer, req->rq_ntrans - 1);
		}
	}

#ifdef RPC_PROFILE
	/* Profile only reads for now */
	if (copied > 1024) {
		static unsigned long	nextstat;
		static unsigned long	pkt_rtt, pkt_len, pkt_cnt;

		pkt_cnt++;
		pkt_len += req->rq_slen + copied;
		pkt_rtt += jiffies - req->rq_xtime;
		if (time_before(nextstat, jiffies)) {
			printk("RPC: %lu %ld cwnd\n", jiffies, xprt->cwnd);
			printk("RPC: %ld %ld %ld %ld stat\n",
					jiffies, pkt_cnt, pkt_len, pkt_rtt);
			pkt_rtt = pkt_len = pkt_cnt = 0;
			nextstat = jiffies + 5 * HZ;
		}
	}
#endif

	dprintk("RPC: %4d has input (%d bytes)\n", task->tk_pid, copied);
	list_del_init(&req->rq_list);
	req->rq_received = req->rq_private_buf.len = copied;

	/* ... and wake up the process. */
	rpc_wake_up_task(task);
	return;
}

/*
 * RPC receive timeout handler.
 */
static void
xprt_timer(struct rpc_task *task)
{
	struct rpc_rqst	*req = task->tk_rqstp;
	struct rpc_xprt *xprt = req->rq_xprt;

	spin_lock(&xprt->transport_lock);
	if (req->rq_received)
		goto out;

	xprt_adjust_cwnd(req->rq_xprt, -ETIMEDOUT);
	__xprt_put_cong(xprt, req);

	dprintk("RPC: %4d xprt_timer (%s request)\n",
		task->tk_pid, req ? "pending" : "backlogged");

	task->tk_status  = -ETIMEDOUT;
out:
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

	if (xprt->shutdown)
		return -EIO;

	spin_lock_bh(&xprt->transport_lock);
	if (req->rq_received && !req->rq_bytes_sent) {
		err = req->rq_received;
		goto out_unlock;
	}
	if (!__xprt_lock_write(xprt, task)) {
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

/**
 * xprt_transmit - send an RPC request on a transport
 * @task: controlling RPC task
 *
 * We have to copy the iovec because sendmsg fiddles with its contents.
 */
void xprt_transmit(struct rpc_task *task)
{
	struct rpc_clnt *clnt = task->tk_client;
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
	if (!status)
		goto out_receive;

	/* Note: at this point, task->tk_sleeping has not yet been set,
	 *	 hence there is no danger of the waking up task being put on
	 *	 schedq, and being picked up by a parallel run of rpciod().
	 */
	task->tk_status = status;

	switch (status) {
	case -ECONNREFUSED:
		task->tk_timeout = RPC_REESTABLISH_TIMEOUT;
		rpc_sleep_on(&xprt->sending, task, NULL, NULL);
	case -EAGAIN:
	case -ENOTCONN:
		return;
	default:
		if (xprt->stream)
			xprt_disconnect(xprt);
	}
	xprt_release_write(xprt, task);
	return;
 out_receive:
	dprintk("RPC: %4d xmit complete\n", task->tk_pid);
	/* Set the task's receive timeout value */
	spin_lock_bh(&xprt->transport_lock);
	if (!xprt->nocong) {
		int timer = task->tk_msg.rpc_proc->p_timer;
		task->tk_timeout = rpc_calc_rto(clnt->cl_rtt, timer);
		task->tk_timeout <<= rpc_ntimeo(clnt->cl_rtt, timer) + req->rq_retries;
		if (task->tk_timeout > xprt->timeout.to_maxval || task->tk_timeout == 0)
			task->tk_timeout = xprt->timeout.to_maxval;
	} else
		task->tk_timeout = req->rq_timeout;
	/* Don't race with disconnect */
	if (!xprt_connected(xprt))
		task->tk_status = -ENOTCONN;
	else if (!req->rq_received)
		rpc_sleep_on(&xprt->pending, task, NULL, xprt_timer);
	__xprt_release_write(xprt, task);
	spin_unlock_bh(&xprt->transport_lock);
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
	if (!xprt->shutdown) {
		spin_lock(&xprt->xprt_lock);
		do_xprt_reserve(task);
		spin_unlock(&xprt->xprt_lock);
	}
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
	req->rq_xid     = xprt_alloc_xid(xprt);
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
	__xprt_release_write(xprt, task);
	__xprt_put_cong(xprt, req);
	if (!list_empty(&req->rq_list))
		list_del(&req->rq_list);
	xprt->last_used = jiffies;
	if (list_empty(&xprt->recv) && !xprt->shutdown)
		mod_timer(&xprt->timer,
				xprt->last_used + RPC_IDLE_DISCONNECT_TIMEOUT);
	spin_unlock_bh(&xprt->transport_lock);
	task->tk_rqstp = NULL;
	memset(req, 0, sizeof(*req));	/* mark unused */

	dprintk("RPC: %4d release request %p\n", task->tk_pid, req);

	spin_lock(&xprt->xprt_lock);
	list_add(&req->rq_list, &xprt->free);
	xprt_clear_backlog(xprt);
	spin_unlock(&xprt->xprt_lock);
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
	spin_lock_init(&xprt->xprt_lock);
	init_waitqueue_head(&xprt->cong_wait);

	INIT_LIST_HEAD(&xprt->free);
	INIT_LIST_HEAD(&xprt->recv);
	INIT_WORK(&xprt->task_cleanup, xprt_socket_autoclose, xprt);
	init_timer(&xprt->timer);
	xprt->timer.function = xprt_init_autodisconnect;
	xprt->timer.data = (unsigned long) xprt;
	xprt->last_used = jiffies;

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

static void xprt_shutdown(struct rpc_xprt *xprt)
{
	xprt->shutdown = 1;
	rpc_wake_up(&xprt->sending);
	rpc_wake_up(&xprt->resend);
	rpc_wake_up(&xprt->pending);
	rpc_wake_up(&xprt->backlog);
	wake_up(&xprt->cong_wait);
	del_timer_sync(&xprt->timer);
}

static int xprt_clear_backlog(struct rpc_xprt *xprt) {
	rpc_wake_up_next(&xprt->backlog);
	wake_up(&xprt->cong_wait);
	return 1;
}

/**
 * xprt_destroy - destroy an RPC transport, killing off all requests.
 * @xprt: transport to destroy
 *
 */
int xprt_destroy(struct rpc_xprt *xprt)
{
	dprintk("RPC:      destroying transport %p\n", xprt);
	xprt_shutdown(xprt);
	xprt->ops->destroy(xprt);
	kfree(xprt);

	return 0;
}
