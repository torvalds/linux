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
 *
 *  TCP callback races fixes (C) 1998 Red Hat Software <alan@redhat.com>
 *  TCP send fixes (C) 1998 Red Hat Software <alan@redhat.com>
 *  TCP NFS related read + write fixes
 *   (C) 1999 Dave Airlie, University of Limerick, Ireland <airlied@linux.ie>
 *
 *  Rewrite of larges part of the code in order to stabilize TCP stuff.
 *  Fix behaviour when socket buffer is full.
 *   (C) 1999 Trond Myklebust <trond.myklebust@fys.uio.no>
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/capability.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/net.h>
#include <linux/mm.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/sunrpc/clnt.h>
#include <linux/file.h>
#include <linux/workqueue.h>
#include <linux/random.h>

#include <net/sock.h>
#include <net/checksum.h>
#include <net/udp.h>
#include <net/tcp.h>

/*
 * Local variables
 */

#ifdef RPC_DEBUG
# undef  RPC_DEBUG_DATA
# define RPCDBG_FACILITY	RPCDBG_XPRT
#endif

#define XPRT_MAX_BACKOFF	(8)
#define XPRT_IDLE_TIMEOUT	(5*60*HZ)
#define XPRT_MAX_RESVPORT	(800)

/*
 * Local functions
 */
static void	xprt_request_init(struct rpc_task *, struct rpc_xprt *);
static inline void	do_xprt_reserve(struct rpc_task *);
static void	xprt_disconnect(struct rpc_xprt *);
static void	xprt_connect_status(struct rpc_task *task);
static struct rpc_xprt * xprt_setup(int proto, struct sockaddr_in *ap,
						struct rpc_timeout *to);
static struct socket *xprt_create_socket(struct rpc_xprt *, int, int);
static void	xprt_bind_socket(struct rpc_xprt *, struct socket *);
static int      __xprt_get_cong(struct rpc_xprt *, struct rpc_task *);

static int	xprt_clear_backlog(struct rpc_xprt *xprt);

#ifdef RPC_DEBUG_DATA
/*
 * Print the buffer contents (first 128 bytes only--just enough for
 * diropres return).
 */
static void
xprt_pktdump(char *msg, u32 *packet, unsigned int count)
{
	u8	*buf = (u8 *) packet;
	int	j;

	dprintk("RPC:      %s\n", msg);
	for (j = 0; j < count && j < 128; j += 4) {
		if (!(j & 31)) {
			if (j)
				dprintk("\n");
			dprintk("0x%04x ", j);
		}
		dprintk("%02x%02x%02x%02x ",
			buf[j], buf[j+1], buf[j+2], buf[j+3]);
	}
	dprintk("\n");
}
#else
static inline void
xprt_pktdump(char *msg, u32 *packet, unsigned int count)
{
	/* NOP */
}
#endif

/*
 * Look up RPC transport given an INET socket
 */
static inline struct rpc_xprt *
xprt_from_sock(struct sock *sk)
{
	return (struct rpc_xprt *) sk->sk_user_data;
}

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

	spin_lock_bh(&xprt->sock_lock);
	retval = __xprt_lock_write(xprt, task);
	spin_unlock_bh(&xprt->sock_lock);
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
	spin_lock_bh(&xprt->sock_lock);
	__xprt_release_write(xprt, task);
	spin_unlock_bh(&xprt->sock_lock);
}

/*
 * Write data to socket.
 */
static inline int
xprt_sendmsg(struct rpc_xprt *xprt, struct rpc_rqst *req)
{
	struct socket	*sock = xprt->sock;
	struct xdr_buf	*xdr = &req->rq_snd_buf;
	struct sockaddr *addr = NULL;
	int addrlen = 0;
	unsigned int	skip;
	int		result;

	if (!sock)
		return -ENOTCONN;

	xprt_pktdump("packet data:",
				req->rq_svec->iov_base,
				req->rq_svec->iov_len);

	/* For UDP, we need to provide an address */
	if (!xprt->stream) {
		addr = (struct sockaddr *) &xprt->addr;
		addrlen = sizeof(xprt->addr);
	}
	/* Dont repeat bytes */
	skip = req->rq_bytes_sent;

	clear_bit(SOCK_ASYNC_NOSPACE, &sock->flags);
	result = xdr_sendpages(sock, addr, addrlen, xdr, skip, MSG_DONTWAIT);

	dprintk("RPC:      xprt_sendmsg(%d) = %d\n", xdr->len - skip, result);

	if (result >= 0)
		return result;

	switch (result) {
	case -ECONNREFUSED:
		/* When the server has died, an ICMP port unreachable message
		 * prompts ECONNREFUSED.
		 */
	case -EAGAIN:
		break;
	case -ECONNRESET:
	case -ENOTCONN:
	case -EPIPE:
		/* connection broken */
		if (xprt->stream)
			result = -ENOTCONN;
		break;
	default:
		printk(KERN_NOTICE "RPC: sendmsg returned error %d\n", -result);
	}
	return result;
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

/*
 * Reset the major timeout value
 */
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

/*
 * Adjust timeout values etc for next retransmit
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
		spin_lock_bh(&xprt->sock_lock);
		rpc_init_rtt(req->rq_task->tk_client->cl_rtt, to->to_initval);
		spin_unlock_bh(&xprt->sock_lock);
		pprintk("RPC: %lu timeout\n", jiffies);
		status = -ETIMEDOUT;
	}

	if (req->rq_timeout == 0) {
		printk(KERN_WARNING "xprt_adjust_timeout: rq_timeout = 0!\n");
		req->rq_timeout = 5 * HZ;
	}
	return status;
}

/*
 * Close down a transport socket
 */
static void
xprt_close(struct rpc_xprt *xprt)
{
	struct socket	*sock = xprt->sock;
	struct sock	*sk = xprt->inet;

	if (!sk)
		return;

	write_lock_bh(&sk->sk_callback_lock);
	xprt->inet = NULL;
	xprt->sock = NULL;

	sk->sk_user_data    = NULL;
	sk->sk_data_ready   = xprt->old_data_ready;
	sk->sk_state_change = xprt->old_state_change;
	sk->sk_write_space  = xprt->old_write_space;
	write_unlock_bh(&sk->sk_callback_lock);

	sk->sk_no_check	 = 0;

	sock_release(sock);
}

static void
xprt_socket_autoclose(void *args)
{
	struct rpc_xprt *xprt = (struct rpc_xprt *)args;

	xprt_disconnect(xprt);
	xprt_close(xprt);
	xprt_release_write(xprt, NULL);
}

/*
 * Mark a transport as disconnected
 */
static void
xprt_disconnect(struct rpc_xprt *xprt)
{
	dprintk("RPC:      disconnected transport %p\n", xprt);
	spin_lock_bh(&xprt->sock_lock);
	xprt_clear_connected(xprt);
	rpc_wake_up_status(&xprt->pending, -ENOTCONN);
	spin_unlock_bh(&xprt->sock_lock);
}

/*
 * Used to allow disconnection when we've been idle
 */
static void
xprt_init_autodisconnect(unsigned long data)
{
	struct rpc_xprt *xprt = (struct rpc_xprt *)data;

	spin_lock(&xprt->sock_lock);
	if (!list_empty(&xprt->recv) || xprt->shutdown)
		goto out_abort;
	if (test_and_set_bit(XPRT_LOCKED, &xprt->sockstate))
		goto out_abort;
	spin_unlock(&xprt->sock_lock);
	/* Let keventd close the socket */
	if (test_bit(XPRT_CONNECTING, &xprt->sockstate) != 0)
		xprt_release_write(xprt, NULL);
	else
		schedule_work(&xprt->task_cleanup);
	return;
out_abort:
	spin_unlock(&xprt->sock_lock);
}

static void xprt_socket_connect(void *args)
{
	struct rpc_xprt *xprt = (struct rpc_xprt *)args;
	struct socket *sock = xprt->sock;
	int status = -EIO;

	if (xprt->shutdown || xprt->addr.sin_port == 0)
		goto out;

	/*
	 * Start by resetting any existing state
	 */
	xprt_close(xprt);
	sock = xprt_create_socket(xprt, xprt->prot, xprt->resvport);
	if (sock == NULL) {
		/* couldn't create socket or bind to reserved port;
		 * this is likely a permanent error, so cause an abort */
		goto out;
	}
	xprt_bind_socket(xprt, sock);
	xprt_sock_setbufsize(xprt);

	status = 0;
	if (!xprt->stream)
		goto out;

	/*
	 * Tell the socket layer to start connecting...
	 */
	status = sock->ops->connect(sock, (struct sockaddr *) &xprt->addr,
			sizeof(xprt->addr), O_NONBLOCK);
	dprintk("RPC: %p  connect status %d connected %d sock state %d\n",
			xprt, -status, xprt_connected(xprt), sock->sk->sk_state);
	if (status < 0) {
		switch (status) {
			case -EINPROGRESS:
			case -EALREADY:
				goto out_clear;
		}
	}
out:
	if (status < 0)
		rpc_wake_up_status(&xprt->pending, status);
	else
		rpc_wake_up(&xprt->pending);
out_clear:
	smp_mb__before_clear_bit();
	clear_bit(XPRT_CONNECTING, &xprt->sockstate);
	smp_mb__after_clear_bit();
}

/*
 * Attempt to connect a TCP socket.
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
		goto out_write;

	if (task->tk_rqstp)
		task->tk_rqstp->rq_bytes_sent = 0;

	task->tk_timeout = RPC_CONNECT_TIMEOUT;
	rpc_sleep_on(&xprt->pending, task, xprt_connect_status, NULL);
	if (!test_and_set_bit(XPRT_CONNECTING, &xprt->sockstate)) {
		/* Note: if we are here due to a dropped connection
		 * 	 we delay reconnecting by RPC_REESTABLISH_TIMEOUT/HZ
		 * 	 seconds
		 */
		if (xprt->sock != NULL)
			schedule_delayed_work(&xprt->sock_connect,
					RPC_REESTABLISH_TIMEOUT);
		else {
			schedule_work(&xprt->sock_connect);
			if (!RPC_IS_ASYNC(task))
				flush_scheduled_work();
		}
	}
	return;
 out_write:
	xprt_release_write(xprt, task);
}

/*
 * We arrive here when awoken from waiting on connection establishment.
 */
static void
xprt_connect_status(struct rpc_task *task)
{
	struct rpc_xprt	*xprt = task->tk_xprt;

	if (task->tk_status >= 0) {
		dprintk("RPC: %4d xprt_connect_status: connection established\n",
				task->tk_pid);
		return;
	}

	/* if soft mounted, just cause this RPC to fail */
	if (RPC_IS_SOFT(task))
		task->tk_status = -EIO;

	switch (task->tk_status) {
	case -ECONNREFUSED:
	case -ECONNRESET:
	case -ENOTCONN:
		return;
	case -ETIMEDOUT:
		dprintk("RPC: %4d xprt_connect_status: timed out\n",
				task->tk_pid);
		break;
	default:
		printk(KERN_ERR "RPC: error %d connecting to server %s\n",
				-task->tk_status, task->tk_client->cl_server);
	}
	xprt_release_write(xprt, task);
}

/*
 * Look up the RPC request corresponding to a reply, and then lock it.
 */
static inline struct rpc_rqst *
xprt_lookup_rqst(struct rpc_xprt *xprt, u32 xid)
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

/*
 * Complete reply received.
 * The TCP code relies on us to remove the request from xprt->pending.
 */
static void
xprt_complete_rqst(struct rpc_xprt *xprt, struct rpc_rqst *req, int copied)
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

static size_t
skb_read_bits(skb_reader_t *desc, void *to, size_t len)
{
	if (len > desc->count)
		len = desc->count;
	if (skb_copy_bits(desc->skb, desc->offset, to, len))
		return 0;
	desc->count -= len;
	desc->offset += len;
	return len;
}

static size_t
skb_read_and_csum_bits(skb_reader_t *desc, void *to, size_t len)
{
	unsigned int csum2, pos;

	if (len > desc->count)
		len = desc->count;
	pos = desc->offset;
	csum2 = skb_copy_and_csum_bits(desc->skb, pos, to, len, 0);
	desc->csum = csum_block_add(desc->csum, csum2, pos);
	desc->count -= len;
	desc->offset += len;
	return len;
}

/*
 * We have set things up such that we perform the checksum of the UDP
 * packet in parallel with the copies into the RPC client iovec.  -DaveM
 */
int
csum_partial_copy_to_xdr(struct xdr_buf *xdr, struct sk_buff *skb)
{
	skb_reader_t desc;

	desc.skb = skb;
	desc.offset = sizeof(struct udphdr);
	desc.count = skb->len - desc.offset;

	if (skb->ip_summed == CHECKSUM_UNNECESSARY)
		goto no_checksum;

	desc.csum = csum_partial(skb->data, desc.offset, skb->csum);
	if (xdr_partial_copy_from_skb(xdr, 0, &desc, skb_read_and_csum_bits) < 0)
		return -1;
	if (desc.offset != skb->len) {
		unsigned int csum2;
		csum2 = skb_checksum(skb, desc.offset, skb->len - desc.offset, 0);
		desc.csum = csum_block_add(desc.csum, csum2, desc.offset);
	}
	if (desc.count)
		return -1;
	if ((unsigned short)csum_fold(desc.csum))
		return -1;
	return 0;
no_checksum:
	if (xdr_partial_copy_from_skb(xdr, 0, &desc, skb_read_bits) < 0)
		return -1;
	if (desc.count)
		return -1;
	return 0;
}

/*
 * Input handler for RPC replies. Called from a bottom half and hence
 * atomic.
 */
static void
udp_data_ready(struct sock *sk, int len)
{
	struct rpc_task	*task;
	struct rpc_xprt	*xprt;
	struct rpc_rqst *rovr;
	struct sk_buff	*skb;
	int err, repsize, copied;
	u32 _xid, *xp;

	read_lock(&sk->sk_callback_lock);
	dprintk("RPC:      udp_data_ready...\n");
	if (!(xprt = xprt_from_sock(sk))) {
		printk("RPC:      udp_data_ready request not found!\n");
		goto out;
	}

	dprintk("RPC:      udp_data_ready client %p\n", xprt);

	if ((skb = skb_recv_datagram(sk, 0, 1, &err)) == NULL)
		goto out;

	if (xprt->shutdown)
		goto dropit;

	repsize = skb->len - sizeof(struct udphdr);
	if (repsize < 4) {
		printk("RPC: impossible RPC reply size %d!\n", repsize);
		goto dropit;
	}

	/* Copy the XID from the skb... */
	xp = skb_header_pointer(skb, sizeof(struct udphdr),
				sizeof(_xid), &_xid);
	if (xp == NULL)
		goto dropit;

	/* Look up and lock the request corresponding to the given XID */
	spin_lock(&xprt->sock_lock);
	rovr = xprt_lookup_rqst(xprt, *xp);
	if (!rovr)
		goto out_unlock;
	task = rovr->rq_task;

	dprintk("RPC: %4d received reply\n", task->tk_pid);

	if ((copied = rovr->rq_private_buf.buflen) > repsize)
		copied = repsize;

	/* Suck it into the iovec, verify checksum if not done by hw. */
	if (csum_partial_copy_to_xdr(&rovr->rq_private_buf, skb))
		goto out_unlock;

	/* Something worked... */
	dst_confirm(skb->dst);

	xprt_complete_rqst(xprt, rovr, copied);

 out_unlock:
	spin_unlock(&xprt->sock_lock);
 dropit:
	skb_free_datagram(sk, skb);
 out:
	read_unlock(&sk->sk_callback_lock);
}

/*
 * Copy from an skb into memory and shrink the skb.
 */
static inline size_t
tcp_copy_data(skb_reader_t *desc, void *p, size_t len)
{
	if (len > desc->count)
		len = desc->count;
	if (skb_copy_bits(desc->skb, desc->offset, p, len)) {
		dprintk("RPC:      failed to copy %zu bytes from skb. %zu bytes remain\n",
				len, desc->count);
		return 0;
	}
	desc->offset += len;
	desc->count -= len;
	dprintk("RPC:      copied %zu bytes from skb. %zu bytes remain\n",
			len, desc->count);
	return len;
}

/*
 * TCP read fragment marker
 */
static inline void
tcp_read_fraghdr(struct rpc_xprt *xprt, skb_reader_t *desc)
{
	size_t len, used;
	char *p;

	p = ((char *) &xprt->tcp_recm) + xprt->tcp_offset;
	len = sizeof(xprt->tcp_recm) - xprt->tcp_offset;
	used = tcp_copy_data(desc, p, len);
	xprt->tcp_offset += used;
	if (used != len)
		return;
	xprt->tcp_reclen = ntohl(xprt->tcp_recm);
	if (xprt->tcp_reclen & 0x80000000)
		xprt->tcp_flags |= XPRT_LAST_FRAG;
	else
		xprt->tcp_flags &= ~XPRT_LAST_FRAG;
	xprt->tcp_reclen &= 0x7fffffff;
	xprt->tcp_flags &= ~XPRT_COPY_RECM;
	xprt->tcp_offset = 0;
	/* Sanity check of the record length */
	if (xprt->tcp_reclen < 4) {
		printk(KERN_ERR "RPC: Invalid TCP record fragment length\n");
		xprt_disconnect(xprt);
	}
	dprintk("RPC:      reading TCP record fragment of length %d\n",
			xprt->tcp_reclen);
}

static void
tcp_check_recm(struct rpc_xprt *xprt)
{
	dprintk("RPC:      xprt = %p, tcp_copied = %lu, tcp_offset = %u, tcp_reclen = %u, tcp_flags = %lx\n",
			xprt, xprt->tcp_copied, xprt->tcp_offset, xprt->tcp_reclen, xprt->tcp_flags);
	if (xprt->tcp_offset == xprt->tcp_reclen) {
		xprt->tcp_flags |= XPRT_COPY_RECM;
		xprt->tcp_offset = 0;
		if (xprt->tcp_flags & XPRT_LAST_FRAG) {
			xprt->tcp_flags &= ~XPRT_COPY_DATA;
			xprt->tcp_flags |= XPRT_COPY_XID;
			xprt->tcp_copied = 0;
		}
	}
}

/*
 * TCP read xid
 */
static inline void
tcp_read_xid(struct rpc_xprt *xprt, skb_reader_t *desc)
{
	size_t len, used;
	char *p;

	len = sizeof(xprt->tcp_xid) - xprt->tcp_offset;
	dprintk("RPC:      reading XID (%Zu bytes)\n", len);
	p = ((char *) &xprt->tcp_xid) + xprt->tcp_offset;
	used = tcp_copy_data(desc, p, len);
	xprt->tcp_offset += used;
	if (used != len)
		return;
	xprt->tcp_flags &= ~XPRT_COPY_XID;
	xprt->tcp_flags |= XPRT_COPY_DATA;
	xprt->tcp_copied = 4;
	dprintk("RPC:      reading reply for XID %08x\n",
						ntohl(xprt->tcp_xid));
	tcp_check_recm(xprt);
}

/*
 * TCP read and complete request
 */
static inline void
tcp_read_request(struct rpc_xprt *xprt, skb_reader_t *desc)
{
	struct rpc_rqst *req;
	struct xdr_buf *rcvbuf;
	size_t len;
	ssize_t r;

	/* Find and lock the request corresponding to this xid */
	spin_lock(&xprt->sock_lock);
	req = xprt_lookup_rqst(xprt, xprt->tcp_xid);
	if (!req) {
		xprt->tcp_flags &= ~XPRT_COPY_DATA;
		dprintk("RPC:      XID %08x request not found!\n",
				ntohl(xprt->tcp_xid));
		spin_unlock(&xprt->sock_lock);
		return;
	}

	rcvbuf = &req->rq_private_buf;
	len = desc->count;
	if (len > xprt->tcp_reclen - xprt->tcp_offset) {
		skb_reader_t my_desc;

		len = xprt->tcp_reclen - xprt->tcp_offset;
		memcpy(&my_desc, desc, sizeof(my_desc));
		my_desc.count = len;
		r = xdr_partial_copy_from_skb(rcvbuf, xprt->tcp_copied,
					  &my_desc, tcp_copy_data);
		desc->count -= r;
		desc->offset += r;
	} else
		r = xdr_partial_copy_from_skb(rcvbuf, xprt->tcp_copied,
					  desc, tcp_copy_data);

	if (r > 0) {
		xprt->tcp_copied += r;
		xprt->tcp_offset += r;
	}
	if (r != len) {
		/* Error when copying to the receive buffer,
		 * usually because we weren't able to allocate
		 * additional buffer pages. All we can do now
		 * is turn off XPRT_COPY_DATA, so the request
		 * will not receive any additional updates,
		 * and time out.
		 * Any remaining data from this record will
		 * be discarded.
		 */
		xprt->tcp_flags &= ~XPRT_COPY_DATA;
		dprintk("RPC:      XID %08x truncated request\n",
				ntohl(xprt->tcp_xid));
		dprintk("RPC:      xprt = %p, tcp_copied = %lu, tcp_offset = %u, tcp_reclen = %u\n",
				xprt, xprt->tcp_copied, xprt->tcp_offset, xprt->tcp_reclen);
		goto out;
	}

	dprintk("RPC:      XID %08x read %Zd bytes\n",
			ntohl(xprt->tcp_xid), r);
	dprintk("RPC:      xprt = %p, tcp_copied = %lu, tcp_offset = %u, tcp_reclen = %u\n",
			xprt, xprt->tcp_copied, xprt->tcp_offset, xprt->tcp_reclen);

	if (xprt->tcp_copied == req->rq_private_buf.buflen)
		xprt->tcp_flags &= ~XPRT_COPY_DATA;
	else if (xprt->tcp_offset == xprt->tcp_reclen) {
		if (xprt->tcp_flags & XPRT_LAST_FRAG)
			xprt->tcp_flags &= ~XPRT_COPY_DATA;
	}

out:
	if (!(xprt->tcp_flags & XPRT_COPY_DATA)) {
		dprintk("RPC: %4d received reply complete\n",
				req->rq_task->tk_pid);
		xprt_complete_rqst(xprt, req, xprt->tcp_copied);
	}
	spin_unlock(&xprt->sock_lock);
	tcp_check_recm(xprt);
}

/*
 * TCP discard extra bytes from a short read
 */
static inline void
tcp_read_discard(struct rpc_xprt *xprt, skb_reader_t *desc)
{
	size_t len;

	len = xprt->tcp_reclen - xprt->tcp_offset;
	if (len > desc->count)
		len = desc->count;
	desc->count -= len;
	desc->offset += len;
	xprt->tcp_offset += len;
	dprintk("RPC:      discarded %Zu bytes\n", len);
	tcp_check_recm(xprt);
}

/*
 * TCP record receive routine
 * We first have to grab the record marker, then the XID, then the data.
 */
static int
tcp_data_recv(read_descriptor_t *rd_desc, struct sk_buff *skb,
		unsigned int offset, size_t len)
{
	struct rpc_xprt *xprt = rd_desc->arg.data;
	skb_reader_t desc = {
		.skb	= skb,
		.offset	= offset,
		.count	= len,
		.csum	= 0
       	};

	dprintk("RPC:      tcp_data_recv\n");
	do {
		/* Read in a new fragment marker if necessary */
		/* Can we ever really expect to get completely empty fragments? */
		if (xprt->tcp_flags & XPRT_COPY_RECM) {
			tcp_read_fraghdr(xprt, &desc);
			continue;
		}
		/* Read in the xid if necessary */
		if (xprt->tcp_flags & XPRT_COPY_XID) {
			tcp_read_xid(xprt, &desc);
			continue;
		}
		/* Read in the request data */
		if (xprt->tcp_flags & XPRT_COPY_DATA) {
			tcp_read_request(xprt, &desc);
			continue;
		}
		/* Skip over any trailing bytes on short reads */
		tcp_read_discard(xprt, &desc);
	} while (desc.count);
	dprintk("RPC:      tcp_data_recv done\n");
	return len - desc.count;
}

static void tcp_data_ready(struct sock *sk, int bytes)
{
	struct rpc_xprt *xprt;
	read_descriptor_t rd_desc;

	read_lock(&sk->sk_callback_lock);
	dprintk("RPC:      tcp_data_ready...\n");
	if (!(xprt = xprt_from_sock(sk))) {
		printk("RPC:      tcp_data_ready socket info not found!\n");
		goto out;
	}
	if (xprt->shutdown)
		goto out;

	/* We use rd_desc to pass struct xprt to tcp_data_recv */
	rd_desc.arg.data = xprt;
	rd_desc.count = 65536;
	tcp_read_sock(sk, &rd_desc, tcp_data_recv);
out:
	read_unlock(&sk->sk_callback_lock);
}

static void
tcp_state_change(struct sock *sk)
{
	struct rpc_xprt	*xprt;

	read_lock(&sk->sk_callback_lock);
	if (!(xprt = xprt_from_sock(sk)))
		goto out;
	dprintk("RPC:      tcp_state_change client %p...\n", xprt);
	dprintk("RPC:      state %x conn %d dead %d zapped %d\n",
				sk->sk_state, xprt_connected(xprt),
				sock_flag(sk, SOCK_DEAD),
				sock_flag(sk, SOCK_ZAPPED));

	switch (sk->sk_state) {
	case TCP_ESTABLISHED:
		spin_lock_bh(&xprt->sock_lock);
		if (!xprt_test_and_set_connected(xprt)) {
			/* Reset TCP record info */
			xprt->tcp_offset = 0;
			xprt->tcp_reclen = 0;
			xprt->tcp_copied = 0;
			xprt->tcp_flags = XPRT_COPY_RECM | XPRT_COPY_XID;
			rpc_wake_up(&xprt->pending);
		}
		spin_unlock_bh(&xprt->sock_lock);
		break;
	case TCP_SYN_SENT:
	case TCP_SYN_RECV:
		break;
	default:
		xprt_disconnect(xprt);
		break;
	}
 out:
	read_unlock(&sk->sk_callback_lock);
}

/*
 * Called when more output buffer space is available for this socket.
 * We try not to wake our writers until they can make "significant"
 * progress, otherwise we'll waste resources thrashing sock_sendmsg
 * with a bunch of small requests.
 */
static void
xprt_write_space(struct sock *sk)
{
	struct rpc_xprt	*xprt;
	struct socket	*sock;

	read_lock(&sk->sk_callback_lock);
	if (!(xprt = xprt_from_sock(sk)) || !(sock = sk->sk_socket))
		goto out;
	if (xprt->shutdown)
		goto out;

	/* Wait until we have enough socket memory */
	if (xprt->stream) {
		/* from net/core/stream.c:sk_stream_write_space */
		if (sk_stream_wspace(sk) < sk_stream_min_wspace(sk))
			goto out;
	} else {
		/* from net/core/sock.c:sock_def_write_space */
		if (!sock_writeable(sk))
			goto out;
	}

	if (!test_and_clear_bit(SOCK_NOSPACE, &sock->flags))
		goto out;

	spin_lock_bh(&xprt->sock_lock);
	if (xprt->snd_task)
		rpc_wake_up_task(xprt->snd_task);
	spin_unlock_bh(&xprt->sock_lock);
out:
	read_unlock(&sk->sk_callback_lock);
}

/*
 * RPC receive timeout handler.
 */
static void
xprt_timer(struct rpc_task *task)
{
	struct rpc_rqst	*req = task->tk_rqstp;
	struct rpc_xprt *xprt = req->rq_xprt;

	spin_lock(&xprt->sock_lock);
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
	spin_unlock(&xprt->sock_lock);
}

/*
 * Place the actual RPC call.
 * We have to copy the iovec because sendmsg fiddles with its contents.
 */
int
xprt_prepare_transmit(struct rpc_task *task)
{
	struct rpc_rqst	*req = task->tk_rqstp;
	struct rpc_xprt	*xprt = req->rq_xprt;
	int err = 0;

	dprintk("RPC: %4d xprt_prepare_transmit\n", task->tk_pid);

	if (xprt->shutdown)
		return -EIO;

	spin_lock_bh(&xprt->sock_lock);
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
	spin_unlock_bh(&xprt->sock_lock);
	return err;
}

void
xprt_transmit(struct rpc_task *task)
{
	struct rpc_clnt *clnt = task->tk_client;
	struct rpc_rqst	*req = task->tk_rqstp;
	struct rpc_xprt	*xprt = req->rq_xprt;
	int status, retry = 0;


	dprintk("RPC: %4d xprt_transmit(%u)\n", task->tk_pid, req->rq_slen);

	/* set up everything as needed. */
	/* Write the record marker */
	if (xprt->stream) {
		u32	*marker = req->rq_svec[0].iov_base;

		*marker = htonl(0x80000000|(req->rq_slen-sizeof(*marker)));
	}

	smp_rmb();
	if (!req->rq_received) {
		if (list_empty(&req->rq_list)) {
			spin_lock_bh(&xprt->sock_lock);
			/* Update the softirq receive buffer */
			memcpy(&req->rq_private_buf, &req->rq_rcv_buf,
					sizeof(req->rq_private_buf));
			/* Add request to the receive list */
			list_add_tail(&req->rq_list, &xprt->recv);
			spin_unlock_bh(&xprt->sock_lock);
			xprt_reset_majortimeo(req);
			/* Turn off autodisconnect */
			del_singleshot_timer_sync(&xprt->timer);
		}
	} else if (!req->rq_bytes_sent)
		return;

	/* Continue transmitting the packet/record. We must be careful
	 * to cope with writespace callbacks arriving _after_ we have
	 * called xprt_sendmsg().
	 */
	while (1) {
		req->rq_xtime = jiffies;
		status = xprt_sendmsg(xprt, req);

		if (status < 0)
			break;

		if (xprt->stream) {
			req->rq_bytes_sent += status;

			/* If we've sent the entire packet, immediately
			 * reset the count of bytes sent. */
			if (req->rq_bytes_sent >= req->rq_slen) {
				req->rq_bytes_sent = 0;
				goto out_receive;
			}
		} else {
			if (status >= req->rq_slen)
				goto out_receive;
			status = -EAGAIN;
			break;
		}

		dprintk("RPC: %4d xmit incomplete (%d left of %d)\n",
				task->tk_pid, req->rq_slen - req->rq_bytes_sent,
				req->rq_slen);

		status = -EAGAIN;
		if (retry++ > 50)
			break;
	}

	/* Note: at this point, task->tk_sleeping has not yet been set,
	 *	 hence there is no danger of the waking up task being put on
	 *	 schedq, and being picked up by a parallel run of rpciod().
	 */
	task->tk_status = status;

	switch (status) {
	case -EAGAIN:
		if (test_bit(SOCK_ASYNC_NOSPACE, &xprt->sock->flags)) {
			/* Protect against races with xprt_write_space */
			spin_lock_bh(&xprt->sock_lock);
			/* Don't race with disconnect */
			if (!xprt_connected(xprt))
				task->tk_status = -ENOTCONN;
			else if (test_bit(SOCK_NOSPACE, &xprt->sock->flags)) {
				task->tk_timeout = req->rq_timeout;
				rpc_sleep_on(&xprt->pending, task, NULL, NULL);
			}
			spin_unlock_bh(&xprt->sock_lock);
			return;
		}
		/* Keep holding the socket if it is blocked */
		rpc_delay(task, HZ>>4);
		return;
	case -ECONNREFUSED:
		task->tk_timeout = RPC_REESTABLISH_TIMEOUT;
		rpc_sleep_on(&xprt->sending, task, NULL, NULL);
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
	spin_lock_bh(&xprt->sock_lock);
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
	spin_unlock_bh(&xprt->sock_lock);
}

/*
 * Reserve an RPC call slot.
 */
static inline void
do_xprt_reserve(struct rpc_task *task)
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

void
xprt_reserve(struct rpc_task *task)
{
	struct rpc_xprt	*xprt = task->tk_xprt;

	task->tk_status = -EIO;
	if (!xprt->shutdown) {
		spin_lock(&xprt->xprt_lock);
		do_xprt_reserve(task);
		spin_unlock(&xprt->xprt_lock);
	}
}

/*
 * Allocate a 'unique' XID
 */
static inline u32 xprt_alloc_xid(struct rpc_xprt *xprt)
{
	return xprt->xid++;
}

static inline void xprt_init_xid(struct rpc_xprt *xprt)
{
	get_random_bytes(&xprt->xid, sizeof(xprt->xid));
}

/*
 * Initialize RPC request
 */
static void
xprt_request_init(struct rpc_task *task, struct rpc_xprt *xprt)
{
	struct rpc_rqst	*req = task->tk_rqstp;

	req->rq_timeout = xprt->timeout.to_initval;
	req->rq_task	= task;
	req->rq_xprt    = xprt;
	req->rq_xid     = xprt_alloc_xid(xprt);
	dprintk("RPC: %4d reserved req %p xid %08x\n", task->tk_pid,
			req, ntohl(req->rq_xid));
}

/*
 * Release an RPC call slot
 */
void
xprt_release(struct rpc_task *task)
{
	struct rpc_xprt	*xprt = task->tk_xprt;
	struct rpc_rqst	*req;

	if (!(req = task->tk_rqstp))
		return;
	spin_lock_bh(&xprt->sock_lock);
	__xprt_release_write(xprt, task);
	__xprt_put_cong(xprt, req);
	if (!list_empty(&req->rq_list))
		list_del(&req->rq_list);
	xprt->last_used = jiffies;
	if (list_empty(&xprt->recv) && !xprt->shutdown)
		mod_timer(&xprt->timer, xprt->last_used + XPRT_IDLE_TIMEOUT);
	spin_unlock_bh(&xprt->sock_lock);
	task->tk_rqstp = NULL;
	memset(req, 0, sizeof(*req));	/* mark unused */

	dprintk("RPC: %4d release request %p\n", task->tk_pid, req);

	spin_lock(&xprt->xprt_lock);
	list_add(&req->rq_list, &xprt->free);
	xprt_clear_backlog(xprt);
	spin_unlock(&xprt->xprt_lock);
}

/*
 * Set default timeout parameters
 */
static void
xprt_default_timeout(struct rpc_timeout *to, int proto)
{
	if (proto == IPPROTO_UDP)
		xprt_set_timeout(to, 5,  5 * HZ);
	else
		xprt_set_timeout(to, 5, 60 * HZ);
}

/*
 * Set constant timeout
 */
void
xprt_set_timeout(struct rpc_timeout *to, unsigned int retr, unsigned long incr)
{
	to->to_initval   = 
	to->to_increment = incr;
	to->to_maxval    = incr * retr;
	to->to_retries   = retr;
	to->to_exponential = 0;
}

unsigned int xprt_udp_slot_table_entries = RPC_DEF_SLOT_TABLE;
unsigned int xprt_tcp_slot_table_entries = RPC_DEF_SLOT_TABLE;

/*
 * Initialize an RPC client
 */
static struct rpc_xprt *
xprt_setup(int proto, struct sockaddr_in *ap, struct rpc_timeout *to)
{
	struct rpc_xprt	*xprt;
	unsigned int entries;
	size_t slot_table_size;
	struct rpc_rqst	*req;

	dprintk("RPC:      setting up %s transport...\n",
				proto == IPPROTO_UDP? "UDP" : "TCP");

	entries = (proto == IPPROTO_TCP)?
		xprt_tcp_slot_table_entries : xprt_udp_slot_table_entries;

	if ((xprt = kmalloc(sizeof(struct rpc_xprt), GFP_KERNEL)) == NULL)
		return ERR_PTR(-ENOMEM);
	memset(xprt, 0, sizeof(*xprt)); /* Nnnngh! */
	xprt->max_reqs = entries;
	slot_table_size = entries * sizeof(xprt->slot[0]);
	xprt->slot = kmalloc(slot_table_size, GFP_KERNEL);
	if (xprt->slot == NULL) {
		kfree(xprt);
		return ERR_PTR(-ENOMEM);
	}
	memset(xprt->slot, 0, slot_table_size);

	xprt->addr = *ap;
	xprt->prot = proto;
	xprt->stream = (proto == IPPROTO_TCP)? 1 : 0;
	if (xprt->stream) {
		xprt->cwnd = RPC_MAXCWND(xprt);
		xprt->nocong = 1;
		xprt->max_payload = (1U << 31) - 1;
	} else {
		xprt->cwnd = RPC_INITCWND;
		xprt->max_payload = (1U << 16) - (MAX_HEADER << 3);
	}
	spin_lock_init(&xprt->sock_lock);
	spin_lock_init(&xprt->xprt_lock);
	init_waitqueue_head(&xprt->cong_wait);

	INIT_LIST_HEAD(&xprt->free);
	INIT_LIST_HEAD(&xprt->recv);
	INIT_WORK(&xprt->sock_connect, xprt_socket_connect, xprt);
	INIT_WORK(&xprt->task_cleanup, xprt_socket_autoclose, xprt);
	init_timer(&xprt->timer);
	xprt->timer.function = xprt_init_autodisconnect;
	xprt->timer.data = (unsigned long) xprt;
	xprt->last_used = jiffies;
	xprt->port = XPRT_MAX_RESVPORT;

	/* Set timeout parameters */
	if (to) {
		xprt->timeout = *to;
	} else
		xprt_default_timeout(&xprt->timeout, xprt->prot);

	rpc_init_wait_queue(&xprt->pending, "xprt_pending");
	rpc_init_wait_queue(&xprt->sending, "xprt_sending");
	rpc_init_wait_queue(&xprt->resend, "xprt_resend");
	rpc_init_priority_wait_queue(&xprt->backlog, "xprt_backlog");

	/* initialize free list */
	for (req = &xprt->slot[entries-1]; req >= &xprt->slot[0]; req--)
		list_add(&req->rq_list, &xprt->free);

	xprt_init_xid(xprt);

	/* Check whether we want to use a reserved port */
	xprt->resvport = capable(CAP_NET_BIND_SERVICE) ? 1 : 0;

	dprintk("RPC:      created transport %p with %u slots\n", xprt,
			xprt->max_reqs);
	
	return xprt;
}

/*
 * Bind to a reserved port
 */
static inline int xprt_bindresvport(struct rpc_xprt *xprt, struct socket *sock)
{
	struct sockaddr_in myaddr = {
		.sin_family = AF_INET,
	};
	int		err, port;

	/* Were we already bound to a given port? Try to reuse it */
	port = xprt->port;
	do {
		myaddr.sin_port = htons(port);
		err = sock->ops->bind(sock, (struct sockaddr *) &myaddr,
						sizeof(myaddr));
		if (err == 0) {
			xprt->port = port;
			return 0;
		}
		if (--port == 0)
			port = XPRT_MAX_RESVPORT;
	} while (err == -EADDRINUSE && port != xprt->port);

	printk("RPC: Can't bind to reserved port (%d).\n", -err);
	return err;
}

static void
xprt_bind_socket(struct rpc_xprt *xprt, struct socket *sock)
{
	struct sock	*sk = sock->sk;

	if (xprt->inet)
		return;

	write_lock_bh(&sk->sk_callback_lock);
	sk->sk_user_data = xprt;
	xprt->old_data_ready = sk->sk_data_ready;
	xprt->old_state_change = sk->sk_state_change;
	xprt->old_write_space = sk->sk_write_space;
	if (xprt->prot == IPPROTO_UDP) {
		sk->sk_data_ready = udp_data_ready;
		sk->sk_no_check = UDP_CSUM_NORCV;
		xprt_set_connected(xprt);
	} else {
		tcp_sk(sk)->nonagle = 1;	/* disable Nagle's algorithm */
		sk->sk_data_ready = tcp_data_ready;
		sk->sk_state_change = tcp_state_change;
		xprt_clear_connected(xprt);
	}
	sk->sk_write_space = xprt_write_space;

	/* Reset to new socket */
	xprt->sock = sock;
	xprt->inet = sk;
	write_unlock_bh(&sk->sk_callback_lock);

	return;
}

/*
 * Set socket buffer length
 */
void
xprt_sock_setbufsize(struct rpc_xprt *xprt)
{
	struct sock *sk = xprt->inet;

	if (xprt->stream)
		return;
	if (xprt->rcvsize) {
		sk->sk_userlocks |= SOCK_RCVBUF_LOCK;
		sk->sk_rcvbuf = xprt->rcvsize * xprt->max_reqs *  2;
	}
	if (xprt->sndsize) {
		sk->sk_userlocks |= SOCK_SNDBUF_LOCK;
		sk->sk_sndbuf = xprt->sndsize * xprt->max_reqs * 2;
		sk->sk_write_space(sk);
	}
}

/*
 * Datastream sockets are created here, but xprt_connect will create
 * and connect stream sockets.
 */
static struct socket * xprt_create_socket(struct rpc_xprt *xprt, int proto, int resvport)
{
	struct socket	*sock;
	int		type, err;

	dprintk("RPC:      xprt_create_socket(%s %d)\n",
			   (proto == IPPROTO_UDP)? "udp" : "tcp", proto);

	type = (proto == IPPROTO_UDP)? SOCK_DGRAM : SOCK_STREAM;

	if ((err = sock_create_kern(PF_INET, type, proto, &sock)) < 0) {
		printk("RPC: can't create socket (%d).\n", -err);
		return NULL;
	}

	/* If the caller has the capability, bind to a reserved port */
	if (resvport && xprt_bindresvport(xprt, sock) < 0) {
		printk("RPC: can't bind to reserved port.\n");
		goto failed;
	}

	return sock;

failed:
	sock_release(sock);
	return NULL;
}

/*
 * Create an RPC client transport given the protocol and peer address.
 */
struct rpc_xprt *
xprt_create_proto(int proto, struct sockaddr_in *sap, struct rpc_timeout *to)
{
	struct rpc_xprt	*xprt;

	xprt = xprt_setup(proto, sap, to);
	if (IS_ERR(xprt))
		dprintk("RPC:      xprt_create_proto failed\n");
	else
		dprintk("RPC:      xprt_create_proto created xprt %p\n", xprt);
	return xprt;
}

/*
 * Prepare for transport shutdown.
 */
static void
xprt_shutdown(struct rpc_xprt *xprt)
{
	xprt->shutdown = 1;
	rpc_wake_up(&xprt->sending);
	rpc_wake_up(&xprt->resend);
	rpc_wake_up(&xprt->pending);
	rpc_wake_up(&xprt->backlog);
	wake_up(&xprt->cong_wait);
	del_timer_sync(&xprt->timer);

	/* synchronously wait for connect worker to finish */
	cancel_delayed_work(&xprt->sock_connect);
	flush_scheduled_work();
}

/*
 * Clear the xprt backlog queue
 */
static int
xprt_clear_backlog(struct rpc_xprt *xprt) {
	rpc_wake_up_next(&xprt->backlog);
	wake_up(&xprt->cong_wait);
	return 1;
}

/*
 * Destroy an RPC transport, killing off all requests.
 */
int
xprt_destroy(struct rpc_xprt *xprt)
{
	dprintk("RPC:      destroying transport %p\n", xprt);
	xprt_shutdown(xprt);
	xprt_disconnect(xprt);
	xprt_close(xprt);
	kfree(xprt->slot);
	kfree(xprt);

	return 0;
}
