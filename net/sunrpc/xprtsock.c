/*
 * linux/net/sunrpc/xprtsock.c
 *
 * Client-side transport implementation for sockets.
 *
 * TCP callback races fixes (C) 1998 Red Hat
 * TCP send fixes (C) 1998 Red Hat
 * TCP NFS related read + write fixes
 *  (C) 1999 Dave Airlie, University of Limerick, Ireland <airlied@linux.ie>
 *
 * Rewrite of larges part of the code in order to stabilize TCP stuff.
 * Fix behaviour when socket buffer is full.
 *  (C) 1999 Trond Myklebust <trond.myklebust@fys.uio.no>
 *
 * IP socket transport implementation, (C) 2005 Chuck Lever <cel@netapp.com>
 *
 * IPv6 support contributed by Gilles Quillard, Bull Open Source, 2005.
 *   <gilles.quillard@bull.net>
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/capability.h>
#include <linux/pagemap.h>
#include <linux/errno.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/net.h>
#include <linux/mm.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/sunrpc/xprtsock.h>
#include <linux/file.h>
#ifdef CONFIG_NFS_V4_1
#include <linux/sunrpc/bc_xprt.h>
#endif

#include <net/sock.h>
#include <net/checksum.h>
#include <net/udp.h>
#include <net/tcp.h>

#include "sunrpc.h"
/*
 * xprtsock tunables
 */
unsigned int xprt_udp_slot_table_entries = RPC_DEF_SLOT_TABLE;
unsigned int xprt_tcp_slot_table_entries = RPC_DEF_SLOT_TABLE;

unsigned int xprt_min_resvport = RPC_DEF_MIN_RESVPORT;
unsigned int xprt_max_resvport = RPC_DEF_MAX_RESVPORT;

#define XS_TCP_LINGER_TO	(15U * HZ)
static unsigned int xs_tcp_fin_timeout __read_mostly = XS_TCP_LINGER_TO;

/*
 * We can register our own files under /proc/sys/sunrpc by
 * calling register_sysctl_table() again.  The files in that
 * directory become the union of all files registered there.
 *
 * We simply need to make sure that we don't collide with
 * someone else's file names!
 */

#ifdef RPC_DEBUG

static unsigned int min_slot_table_size = RPC_MIN_SLOT_TABLE;
static unsigned int max_slot_table_size = RPC_MAX_SLOT_TABLE;
static unsigned int xprt_min_resvport_limit = RPC_MIN_RESVPORT;
static unsigned int xprt_max_resvport_limit = RPC_MAX_RESVPORT;

static struct ctl_table_header *sunrpc_table_header;

/*
 * FIXME: changing the UDP slot table size should also resize the UDP
 *        socket buffers for existing UDP transports
 */
static ctl_table xs_tunables_table[] = {
	{
		.ctl_name	= CTL_SLOTTABLE_UDP,
		.procname	= "udp_slot_table_entries",
		.data		= &xprt_udp_slot_table_entries,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &min_slot_table_size,
		.extra2		= &max_slot_table_size
	},
	{
		.ctl_name	= CTL_SLOTTABLE_TCP,
		.procname	= "tcp_slot_table_entries",
		.data		= &xprt_tcp_slot_table_entries,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &min_slot_table_size,
		.extra2		= &max_slot_table_size
	},
	{
		.ctl_name	= CTL_MIN_RESVPORT,
		.procname	= "min_resvport",
		.data		= &xprt_min_resvport,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &xprt_min_resvport_limit,
		.extra2		= &xprt_max_resvport_limit
	},
	{
		.ctl_name	= CTL_MAX_RESVPORT,
		.procname	= "max_resvport",
		.data		= &xprt_max_resvport,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &xprt_min_resvport_limit,
		.extra2		= &xprt_max_resvport_limit
	},
	{
		.procname	= "tcp_fin_timeout",
		.data		= &xs_tcp_fin_timeout,
		.maxlen		= sizeof(xs_tcp_fin_timeout),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
		.strategy	= sysctl_jiffies
	},
	{
		.ctl_name = 0,
	},
};

static ctl_table sunrpc_table[] = {
	{
		.ctl_name	= CTL_SUNRPC,
		.procname	= "sunrpc",
		.mode		= 0555,
		.child		= xs_tunables_table
	},
	{
		.ctl_name = 0,
	},
};

#endif

/*
 * Time out for an RPC UDP socket connect.  UDP socket connects are
 * synchronous, but we set a timeout anyway in case of resource
 * exhaustion on the local host.
 */
#define XS_UDP_CONN_TO		(5U * HZ)

/*
 * Wait duration for an RPC TCP connection to be established.  Solaris
 * NFS over TCP uses 60 seconds, for example, which is in line with how
 * long a server takes to reboot.
 */
#define XS_TCP_CONN_TO		(60U * HZ)

/*
 * Wait duration for a reply from the RPC portmapper.
 */
#define XS_BIND_TO		(60U * HZ)

/*
 * Delay if a UDP socket connect error occurs.  This is most likely some
 * kind of resource problem on the local host.
 */
#define XS_UDP_REEST_TO		(2U * HZ)

/*
 * The reestablish timeout allows clients to delay for a bit before attempting
 * to reconnect to a server that just dropped our connection.
 *
 * We implement an exponential backoff when trying to reestablish a TCP
 * transport connection with the server.  Some servers like to drop a TCP
 * connection when they are overworked, so we start with a short timeout and
 * increase over time if the server is down or not responding.
 */
#define XS_TCP_INIT_REEST_TO	(3U * HZ)
#define XS_TCP_MAX_REEST_TO	(5U * 60 * HZ)

/*
 * TCP idle timeout; client drops the transport socket if it is idle
 * for this long.  Note that we also timeout UDP sockets to prevent
 * holding port numbers when there is no RPC traffic.
 */
#define XS_IDLE_DISC_TO		(5U * 60 * HZ)

#ifdef RPC_DEBUG
# undef  RPC_DEBUG_DATA
# define RPCDBG_FACILITY	RPCDBG_TRANS
#endif

#ifdef RPC_DEBUG_DATA
static void xs_pktdump(char *msg, u32 *packet, unsigned int count)
{
	u8 *buf = (u8 *) packet;
	int j;

	dprintk("RPC:       %s\n", msg);
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
static inline void xs_pktdump(char *msg, u32 *packet, unsigned int count)
{
	/* NOP */
}
#endif

struct sock_xprt {
	struct rpc_xprt		xprt;

	/*
	 * Network layer
	 */
	struct socket *		sock;
	struct sock *		inet;

	/*
	 * State of TCP reply receive
	 */
	__be32			tcp_fraghdr,
				tcp_xid,
				tcp_calldir;

	u32			tcp_offset,
				tcp_reclen;

	unsigned long		tcp_copied,
				tcp_flags;

	/*
	 * Connection of transports
	 */
	struct delayed_work	connect_worker;
	struct sockaddr_storage	srcaddr;
	unsigned short		srcport;

	/*
	 * UDP socket buffer size parameters
	 */
	size_t			rcvsize,
				sndsize;

	/*
	 * Saved socket callback addresses
	 */
	void			(*old_data_ready)(struct sock *, int);
	void			(*old_state_change)(struct sock *);
	void			(*old_write_space)(struct sock *);
	void			(*old_error_report)(struct sock *);
};

/*
 * TCP receive state flags
 */
#define TCP_RCV_LAST_FRAG	(1UL << 0)
#define TCP_RCV_COPY_FRAGHDR	(1UL << 1)
#define TCP_RCV_COPY_XID	(1UL << 2)
#define TCP_RCV_COPY_DATA	(1UL << 3)
#define TCP_RCV_READ_CALLDIR	(1UL << 4)
#define TCP_RCV_COPY_CALLDIR	(1UL << 5)

/*
 * TCP RPC flags
 */
#define TCP_RPC_REPLY		(1UL << 6)

static inline struct sockaddr *xs_addr(struct rpc_xprt *xprt)
{
	return (struct sockaddr *) &xprt->addr;
}

static inline struct sockaddr_in *xs_addr_in(struct rpc_xprt *xprt)
{
	return (struct sockaddr_in *) &xprt->addr;
}

static inline struct sockaddr_in6 *xs_addr_in6(struct rpc_xprt *xprt)
{
	return (struct sockaddr_in6 *) &xprt->addr;
}

static void xs_format_common_peer_addresses(struct rpc_xprt *xprt)
{
	struct sockaddr *sap = xs_addr(xprt);
	struct sockaddr_in6 *sin6;
	struct sockaddr_in *sin;
	char buf[128];

	(void)rpc_ntop(sap, buf, sizeof(buf));
	xprt->address_strings[RPC_DISPLAY_ADDR] = kstrdup(buf, GFP_KERNEL);

	switch (sap->sa_family) {
	case AF_INET:
		sin = xs_addr_in(xprt);
		(void)snprintf(buf, sizeof(buf), "%02x%02x%02x%02x",
					NIPQUAD(sin->sin_addr.s_addr));
		break;
	case AF_INET6:
		sin6 = xs_addr_in6(xprt);
		(void)snprintf(buf, sizeof(buf), "%pi6", &sin6->sin6_addr);
		break;
	default:
		BUG();
	}
	xprt->address_strings[RPC_DISPLAY_HEX_ADDR] = kstrdup(buf, GFP_KERNEL);
}

static void xs_format_common_peer_ports(struct rpc_xprt *xprt)
{
	struct sockaddr *sap = xs_addr(xprt);
	char buf[128];

	(void)snprintf(buf, sizeof(buf), "%u", rpc_get_port(sap));
	xprt->address_strings[RPC_DISPLAY_PORT] = kstrdup(buf, GFP_KERNEL);

	(void)snprintf(buf, sizeof(buf), "%4hx", rpc_get_port(sap));
	xprt->address_strings[RPC_DISPLAY_HEX_PORT] = kstrdup(buf, GFP_KERNEL);
}

static void xs_format_peer_addresses(struct rpc_xprt *xprt,
				     const char *protocol,
				     const char *netid)
{
	xprt->address_strings[RPC_DISPLAY_PROTO] = protocol;
	xprt->address_strings[RPC_DISPLAY_NETID] = netid;
	xs_format_common_peer_addresses(xprt);
	xs_format_common_peer_ports(xprt);
}

static void xs_update_peer_port(struct rpc_xprt *xprt)
{
	kfree(xprt->address_strings[RPC_DISPLAY_HEX_PORT]);
	kfree(xprt->address_strings[RPC_DISPLAY_PORT]);

	xs_format_common_peer_ports(xprt);
}

static void xs_free_peer_addresses(struct rpc_xprt *xprt)
{
	unsigned int i;

	for (i = 0; i < RPC_DISPLAY_MAX; i++)
		switch (i) {
		case RPC_DISPLAY_PROTO:
		case RPC_DISPLAY_NETID:
			continue;
		default:
			kfree(xprt->address_strings[i]);
		}
}

#define XS_SENDMSG_FLAGS	(MSG_DONTWAIT | MSG_NOSIGNAL)

static int xs_send_kvec(struct socket *sock, struct sockaddr *addr, int addrlen, struct kvec *vec, unsigned int base, int more)
{
	struct msghdr msg = {
		.msg_name	= addr,
		.msg_namelen	= addrlen,
		.msg_flags	= XS_SENDMSG_FLAGS | (more ? MSG_MORE : 0),
	};
	struct kvec iov = {
		.iov_base	= vec->iov_base + base,
		.iov_len	= vec->iov_len - base,
	};

	if (iov.iov_len != 0)
		return kernel_sendmsg(sock, &msg, &iov, 1, iov.iov_len);
	return kernel_sendmsg(sock, &msg, NULL, 0, 0);
}

static int xs_send_pagedata(struct socket *sock, struct xdr_buf *xdr, unsigned int base, int more)
{
	struct page **ppage;
	unsigned int remainder;
	int err, sent = 0;

	remainder = xdr->page_len - base;
	base += xdr->page_base;
	ppage = xdr->pages + (base >> PAGE_SHIFT);
	base &= ~PAGE_MASK;
	for(;;) {
		unsigned int len = min_t(unsigned int, PAGE_SIZE - base, remainder);
		int flags = XS_SENDMSG_FLAGS;

		remainder -= len;
		if (remainder != 0 || more)
			flags |= MSG_MORE;
		err = sock->ops->sendpage(sock, *ppage, base, len, flags);
		if (remainder == 0 || err != len)
			break;
		sent += err;
		ppage++;
		base = 0;
	}
	if (sent == 0)
		return err;
	if (err > 0)
		sent += err;
	return sent;
}

/**
 * xs_sendpages - write pages directly to a socket
 * @sock: socket to send on
 * @addr: UDP only -- address of destination
 * @addrlen: UDP only -- length of destination address
 * @xdr: buffer containing this request
 * @base: starting position in the buffer
 *
 */
static int xs_sendpages(struct socket *sock, struct sockaddr *addr, int addrlen, struct xdr_buf *xdr, unsigned int base)
{
	unsigned int remainder = xdr->len - base;
	int err, sent = 0;

	if (unlikely(!sock))
		return -ENOTSOCK;

	clear_bit(SOCK_ASYNC_NOSPACE, &sock->flags);
	if (base != 0) {
		addr = NULL;
		addrlen = 0;
	}

	if (base < xdr->head[0].iov_len || addr != NULL) {
		unsigned int len = xdr->head[0].iov_len - base;
		remainder -= len;
		err = xs_send_kvec(sock, addr, addrlen, &xdr->head[0], base, remainder != 0);
		if (remainder == 0 || err != len)
			goto out;
		sent += err;
		base = 0;
	} else
		base -= xdr->head[0].iov_len;

	if (base < xdr->page_len) {
		unsigned int len = xdr->page_len - base;
		remainder -= len;
		err = xs_send_pagedata(sock, xdr, base, remainder != 0);
		if (remainder == 0 || err != len)
			goto out;
		sent += err;
		base = 0;
	} else
		base -= xdr->page_len;

	if (base >= xdr->tail[0].iov_len)
		return sent;
	err = xs_send_kvec(sock, NULL, 0, &xdr->tail[0], base, 0);
out:
	if (sent == 0)
		return err;
	if (err > 0)
		sent += err;
	return sent;
}

static void xs_nospace_callback(struct rpc_task *task)
{
	struct sock_xprt *transport = container_of(task->tk_rqstp->rq_xprt, struct sock_xprt, xprt);

	transport->inet->sk_write_pending--;
	clear_bit(SOCK_ASYNC_NOSPACE, &transport->sock->flags);
}

/**
 * xs_nospace - place task on wait queue if transmit was incomplete
 * @task: task to put to sleep
 *
 */
static int xs_nospace(struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_xprt *xprt = req->rq_xprt;
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	int ret = 0;

	dprintk("RPC: %5u xmit incomplete (%u left of %u)\n",
			task->tk_pid, req->rq_slen - req->rq_bytes_sent,
			req->rq_slen);

	/* Protect against races with write_space */
	spin_lock_bh(&xprt->transport_lock);

	/* Don't race with disconnect */
	if (xprt_connected(xprt)) {
		if (test_bit(SOCK_ASYNC_NOSPACE, &transport->sock->flags)) {
			ret = -EAGAIN;
			/*
			 * Notify TCP that we're limited by the application
			 * window size
			 */
			set_bit(SOCK_NOSPACE, &transport->sock->flags);
			transport->inet->sk_write_pending++;
			/* ...and wait for more buffer space */
			xprt_wait_for_buffer_space(task, xs_nospace_callback);
		}
	} else {
		clear_bit(SOCK_ASYNC_NOSPACE, &transport->sock->flags);
		ret = -ENOTCONN;
	}

	spin_unlock_bh(&xprt->transport_lock);
	return ret;
}

/**
 * xs_udp_send_request - write an RPC request to a UDP socket
 * @task: address of RPC task that manages the state of an RPC request
 *
 * Return values:
 *        0:	The request has been sent
 *   EAGAIN:	The socket was blocked, please call again later to
 *		complete the request
 * ENOTCONN:	Caller needs to invoke connect logic then call again
 *    other:	Some other error occured, the request was not sent
 */
static int xs_udp_send_request(struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_xprt *xprt = req->rq_xprt;
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	struct xdr_buf *xdr = &req->rq_snd_buf;
	int status;

	xs_pktdump("packet data:",
				req->rq_svec->iov_base,
				req->rq_svec->iov_len);

	if (!xprt_bound(xprt))
		return -ENOTCONN;
	status = xs_sendpages(transport->sock,
			      xs_addr(xprt),
			      xprt->addrlen, xdr,
			      req->rq_bytes_sent);

	dprintk("RPC:       xs_udp_send_request(%u) = %d\n",
			xdr->len - req->rq_bytes_sent, status);

	if (status >= 0) {
		task->tk_bytes_sent += status;
		if (status >= req->rq_slen)
			return 0;
		/* Still some bytes left; set up for a retry later. */
		status = -EAGAIN;
	}
	if (!transport->sock)
		goto out;

	switch (status) {
	case -ENOTSOCK:
		status = -ENOTCONN;
		/* Should we call xs_close() here? */
		break;
	case -EAGAIN:
		status = xs_nospace(task);
		break;
	default:
		dprintk("RPC:       sendmsg returned unrecognized error %d\n",
			-status);
	case -ENETUNREACH:
	case -EPIPE:
	case -ECONNREFUSED:
		/* When the server has died, an ICMP port unreachable message
		 * prompts ECONNREFUSED. */
		clear_bit(SOCK_ASYNC_NOSPACE, &transport->sock->flags);
	}
out:
	return status;
}

/**
 * xs_tcp_shutdown - gracefully shut down a TCP socket
 * @xprt: transport
 *
 * Initiates a graceful shutdown of the TCP socket by calling the
 * equivalent of shutdown(SHUT_WR);
 */
static void xs_tcp_shutdown(struct rpc_xprt *xprt)
{
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	struct socket *sock = transport->sock;

	if (sock != NULL)
		kernel_sock_shutdown(sock, SHUT_WR);
}

static inline void xs_encode_tcp_record_marker(struct xdr_buf *buf)
{
	u32 reclen = buf->len - sizeof(rpc_fraghdr);
	rpc_fraghdr *base = buf->head[0].iov_base;
	*base = htonl(RPC_LAST_STREAM_FRAGMENT | reclen);
}

/**
 * xs_tcp_send_request - write an RPC request to a TCP socket
 * @task: address of RPC task that manages the state of an RPC request
 *
 * Return values:
 *        0:	The request has been sent
 *   EAGAIN:	The socket was blocked, please call again later to
 *		complete the request
 * ENOTCONN:	Caller needs to invoke connect logic then call again
 *    other:	Some other error occured, the request was not sent
 *
 * XXX: In the case of soft timeouts, should we eventually give up
 *	if sendmsg is not able to make progress?
 */
static int xs_tcp_send_request(struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_xprt *xprt = req->rq_xprt;
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	struct xdr_buf *xdr = &req->rq_snd_buf;
	int status;

	xs_encode_tcp_record_marker(&req->rq_snd_buf);

	xs_pktdump("packet data:",
				req->rq_svec->iov_base,
				req->rq_svec->iov_len);

	/* Continue transmitting the packet/record. We must be careful
	 * to cope with writespace callbacks arriving _after_ we have
	 * called sendmsg(). */
	while (1) {
		status = xs_sendpages(transport->sock,
					NULL, 0, xdr, req->rq_bytes_sent);

		dprintk("RPC:       xs_tcp_send_request(%u) = %d\n",
				xdr->len - req->rq_bytes_sent, status);

		if (unlikely(status < 0))
			break;

		/* If we've sent the entire packet, immediately
		 * reset the count of bytes sent. */
		req->rq_bytes_sent += status;
		task->tk_bytes_sent += status;
		if (likely(req->rq_bytes_sent >= req->rq_slen)) {
			req->rq_bytes_sent = 0;
			return 0;
		}

		if (status != 0)
			continue;
		status = -EAGAIN;
		break;
	}
	if (!transport->sock)
		goto out;

	switch (status) {
	case -ENOTSOCK:
		status = -ENOTCONN;
		/* Should we call xs_close() here? */
		break;
	case -EAGAIN:
		status = xs_nospace(task);
		break;
	default:
		dprintk("RPC:       sendmsg returned unrecognized error %d\n",
			-status);
	case -ECONNRESET:
	case -EPIPE:
		xs_tcp_shutdown(xprt);
	case -ECONNREFUSED:
	case -ENOTCONN:
		clear_bit(SOCK_ASYNC_NOSPACE, &transport->sock->flags);
	}
out:
	return status;
}

/**
 * xs_tcp_release_xprt - clean up after a tcp transmission
 * @xprt: transport
 * @task: rpc task
 *
 * This cleans up if an error causes us to abort the transmission of a request.
 * In this case, the socket may need to be reset in order to avoid confusing
 * the server.
 */
static void xs_tcp_release_xprt(struct rpc_xprt *xprt, struct rpc_task *task)
{
	struct rpc_rqst *req;

	if (task != xprt->snd_task)
		return;
	if (task == NULL)
		goto out_release;
	req = task->tk_rqstp;
	if (req->rq_bytes_sent == 0)
		goto out_release;
	if (req->rq_bytes_sent == req->rq_snd_buf.len)
		goto out_release;
	set_bit(XPRT_CLOSE_WAIT, &task->tk_xprt->state);
out_release:
	xprt_release_xprt(xprt, task);
}

static void xs_save_old_callbacks(struct sock_xprt *transport, struct sock *sk)
{
	transport->old_data_ready = sk->sk_data_ready;
	transport->old_state_change = sk->sk_state_change;
	transport->old_write_space = sk->sk_write_space;
	transport->old_error_report = sk->sk_error_report;
}

static void xs_restore_old_callbacks(struct sock_xprt *transport, struct sock *sk)
{
	sk->sk_data_ready = transport->old_data_ready;
	sk->sk_state_change = transport->old_state_change;
	sk->sk_write_space = transport->old_write_space;
	sk->sk_error_report = transport->old_error_report;
}

static void xs_reset_transport(struct sock_xprt *transport)
{
	struct socket *sock = transport->sock;
	struct sock *sk = transport->inet;

	if (sk == NULL)
		return;

	transport->srcport = 0;

	write_lock_bh(&sk->sk_callback_lock);
	transport->inet = NULL;
	transport->sock = NULL;

	sk->sk_user_data = NULL;

	xs_restore_old_callbacks(transport, sk);
	write_unlock_bh(&sk->sk_callback_lock);

	sk->sk_no_check = 0;

	sock_release(sock);
}

/**
 * xs_close - close a socket
 * @xprt: transport
 *
 * This is used when all requests are complete; ie, no DRC state remains
 * on the server we want to save.
 *
 * The caller _must_ be holding XPRT_LOCKED in order to avoid issues with
 * xs_reset_transport() zeroing the socket from underneath a writer.
 */
static void xs_close(struct rpc_xprt *xprt)
{
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);

	dprintk("RPC:       xs_close xprt %p\n", xprt);

	xs_reset_transport(transport);
	xprt->reestablish_timeout = 0;

	smp_mb__before_clear_bit();
	clear_bit(XPRT_CONNECTION_ABORT, &xprt->state);
	clear_bit(XPRT_CLOSE_WAIT, &xprt->state);
	clear_bit(XPRT_CLOSING, &xprt->state);
	smp_mb__after_clear_bit();
	xprt_disconnect_done(xprt);
}

static void xs_tcp_close(struct rpc_xprt *xprt)
{
	if (test_and_clear_bit(XPRT_CONNECTION_CLOSE, &xprt->state))
		xs_close(xprt);
	else
		xs_tcp_shutdown(xprt);
}

/**
 * xs_destroy - prepare to shutdown a transport
 * @xprt: doomed transport
 *
 */
static void xs_destroy(struct rpc_xprt *xprt)
{
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);

	dprintk("RPC:       xs_destroy xprt %p\n", xprt);

	cancel_rearming_delayed_work(&transport->connect_worker);

	xs_close(xprt);
	xs_free_peer_addresses(xprt);
	kfree(xprt->slot);
	kfree(xprt);
	module_put(THIS_MODULE);
}

static inline struct rpc_xprt *xprt_from_sock(struct sock *sk)
{
	return (struct rpc_xprt *) sk->sk_user_data;
}

/**
 * xs_udp_data_ready - "data ready" callback for UDP sockets
 * @sk: socket with data to read
 * @len: how much data to read
 *
 */
static void xs_udp_data_ready(struct sock *sk, int len)
{
	struct rpc_task *task;
	struct rpc_xprt *xprt;
	struct rpc_rqst *rovr;
	struct sk_buff *skb;
	int err, repsize, copied;
	u32 _xid;
	__be32 *xp;

	read_lock(&sk->sk_callback_lock);
	dprintk("RPC:       xs_udp_data_ready...\n");
	if (!(xprt = xprt_from_sock(sk)))
		goto out;

	if ((skb = skb_recv_datagram(sk, 0, 1, &err)) == NULL)
		goto out;

	if (xprt->shutdown)
		goto dropit;

	repsize = skb->len - sizeof(struct udphdr);
	if (repsize < 4) {
		dprintk("RPC:       impossible RPC reply size %d!\n", repsize);
		goto dropit;
	}

	/* Copy the XID from the skb... */
	xp = skb_header_pointer(skb, sizeof(struct udphdr),
				sizeof(_xid), &_xid);
	if (xp == NULL)
		goto dropit;

	/* Look up and lock the request corresponding to the given XID */
	spin_lock(&xprt->transport_lock);
	rovr = xprt_lookup_rqst(xprt, *xp);
	if (!rovr)
		goto out_unlock;
	task = rovr->rq_task;

	if ((copied = rovr->rq_private_buf.buflen) > repsize)
		copied = repsize;

	/* Suck it into the iovec, verify checksum if not done by hw. */
	if (csum_partial_copy_to_xdr(&rovr->rq_private_buf, skb)) {
		UDPX_INC_STATS_BH(sk, UDP_MIB_INERRORS);
		goto out_unlock;
	}

	UDPX_INC_STATS_BH(sk, UDP_MIB_INDATAGRAMS);

	/* Something worked... */
	dst_confirm(skb_dst(skb));

	xprt_adjust_cwnd(task, copied);
	xprt_update_rtt(task);
	xprt_complete_rqst(task, copied);

 out_unlock:
	spin_unlock(&xprt->transport_lock);
 dropit:
	skb_free_datagram(sk, skb);
 out:
	read_unlock(&sk->sk_callback_lock);
}

static inline void xs_tcp_read_fraghdr(struct rpc_xprt *xprt, struct xdr_skb_reader *desc)
{
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	size_t len, used;
	char *p;

	p = ((char *) &transport->tcp_fraghdr) + transport->tcp_offset;
	len = sizeof(transport->tcp_fraghdr) - transport->tcp_offset;
	used = xdr_skb_read_bits(desc, p, len);
	transport->tcp_offset += used;
	if (used != len)
		return;

	transport->tcp_reclen = ntohl(transport->tcp_fraghdr);
	if (transport->tcp_reclen & RPC_LAST_STREAM_FRAGMENT)
		transport->tcp_flags |= TCP_RCV_LAST_FRAG;
	else
		transport->tcp_flags &= ~TCP_RCV_LAST_FRAG;
	transport->tcp_reclen &= RPC_FRAGMENT_SIZE_MASK;

	transport->tcp_flags &= ~TCP_RCV_COPY_FRAGHDR;
	transport->tcp_offset = 0;

	/* Sanity check of the record length */
	if (unlikely(transport->tcp_reclen < 8)) {
		dprintk("RPC:       invalid TCP record fragment length\n");
		xprt_force_disconnect(xprt);
		return;
	}
	dprintk("RPC:       reading TCP record fragment of length %d\n",
			transport->tcp_reclen);
}

static void xs_tcp_check_fraghdr(struct sock_xprt *transport)
{
	if (transport->tcp_offset == transport->tcp_reclen) {
		transport->tcp_flags |= TCP_RCV_COPY_FRAGHDR;
		transport->tcp_offset = 0;
		if (transport->tcp_flags & TCP_RCV_LAST_FRAG) {
			transport->tcp_flags &= ~TCP_RCV_COPY_DATA;
			transport->tcp_flags |= TCP_RCV_COPY_XID;
			transport->tcp_copied = 0;
		}
	}
}

static inline void xs_tcp_read_xid(struct sock_xprt *transport, struct xdr_skb_reader *desc)
{
	size_t len, used;
	char *p;

	len = sizeof(transport->tcp_xid) - transport->tcp_offset;
	dprintk("RPC:       reading XID (%Zu bytes)\n", len);
	p = ((char *) &transport->tcp_xid) + transport->tcp_offset;
	used = xdr_skb_read_bits(desc, p, len);
	transport->tcp_offset += used;
	if (used != len)
		return;
	transport->tcp_flags &= ~TCP_RCV_COPY_XID;
	transport->tcp_flags |= TCP_RCV_READ_CALLDIR;
	transport->tcp_copied = 4;
	dprintk("RPC:       reading %s XID %08x\n",
			(transport->tcp_flags & TCP_RPC_REPLY) ? "reply for"
							      : "request with",
			ntohl(transport->tcp_xid));
	xs_tcp_check_fraghdr(transport);
}

static inline void xs_tcp_read_calldir(struct sock_xprt *transport,
				       struct xdr_skb_reader *desc)
{
	size_t len, used;
	u32 offset;
	char *p;

	/*
	 * We want transport->tcp_offset to be 8 at the end of this routine
	 * (4 bytes for the xid and 4 bytes for the call/reply flag).
	 * When this function is called for the first time,
	 * transport->tcp_offset is 4 (after having already read the xid).
	 */
	offset = transport->tcp_offset - sizeof(transport->tcp_xid);
	len = sizeof(transport->tcp_calldir) - offset;
	dprintk("RPC:       reading CALL/REPLY flag (%Zu bytes)\n", len);
	p = ((char *) &transport->tcp_calldir) + offset;
	used = xdr_skb_read_bits(desc, p, len);
	transport->tcp_offset += used;
	if (used != len)
		return;
	transport->tcp_flags &= ~TCP_RCV_READ_CALLDIR;
	/*
	 * We don't yet have the XDR buffer, so we will write the calldir
	 * out after we get the buffer from the 'struct rpc_rqst'
	 */
	switch (ntohl(transport->tcp_calldir)) {
	case RPC_REPLY:
		transport->tcp_flags |= TCP_RCV_COPY_CALLDIR;
		transport->tcp_flags |= TCP_RCV_COPY_DATA;
		transport->tcp_flags |= TCP_RPC_REPLY;
		break;
	case RPC_CALL:
		transport->tcp_flags |= TCP_RCV_COPY_CALLDIR;
		transport->tcp_flags |= TCP_RCV_COPY_DATA;
		transport->tcp_flags &= ~TCP_RPC_REPLY;
		break;
	default:
		dprintk("RPC:       invalid request message type\n");
		xprt_force_disconnect(&transport->xprt);
	}
	xs_tcp_check_fraghdr(transport);
}

static inline void xs_tcp_read_common(struct rpc_xprt *xprt,
				     struct xdr_skb_reader *desc,
				     struct rpc_rqst *req)
{
	struct sock_xprt *transport =
				container_of(xprt, struct sock_xprt, xprt);
	struct xdr_buf *rcvbuf;
	size_t len;
	ssize_t r;

	rcvbuf = &req->rq_private_buf;

	if (transport->tcp_flags & TCP_RCV_COPY_CALLDIR) {
		/*
		 * Save the RPC direction in the XDR buffer
		 */
		memcpy(rcvbuf->head[0].iov_base + transport->tcp_copied,
			&transport->tcp_calldir,
			sizeof(transport->tcp_calldir));
		transport->tcp_copied += sizeof(transport->tcp_calldir);
		transport->tcp_flags &= ~TCP_RCV_COPY_CALLDIR;
	}

	len = desc->count;
	if (len > transport->tcp_reclen - transport->tcp_offset) {
		struct xdr_skb_reader my_desc;

		len = transport->tcp_reclen - transport->tcp_offset;
		memcpy(&my_desc, desc, sizeof(my_desc));
		my_desc.count = len;
		r = xdr_partial_copy_from_skb(rcvbuf, transport->tcp_copied,
					  &my_desc, xdr_skb_read_bits);
		desc->count -= r;
		desc->offset += r;
	} else
		r = xdr_partial_copy_from_skb(rcvbuf, transport->tcp_copied,
					  desc, xdr_skb_read_bits);

	if (r > 0) {
		transport->tcp_copied += r;
		transport->tcp_offset += r;
	}
	if (r != len) {
		/* Error when copying to the receive buffer,
		 * usually because we weren't able to allocate
		 * additional buffer pages. All we can do now
		 * is turn off TCP_RCV_COPY_DATA, so the request
		 * will not receive any additional updates,
		 * and time out.
		 * Any remaining data from this record will
		 * be discarded.
		 */
		transport->tcp_flags &= ~TCP_RCV_COPY_DATA;
		dprintk("RPC:       XID %08x truncated request\n",
				ntohl(transport->tcp_xid));
		dprintk("RPC:       xprt = %p, tcp_copied = %lu, "
				"tcp_offset = %u, tcp_reclen = %u\n",
				xprt, transport->tcp_copied,
				transport->tcp_offset, transport->tcp_reclen);
		return;
	}

	dprintk("RPC:       XID %08x read %Zd bytes\n",
			ntohl(transport->tcp_xid), r);
	dprintk("RPC:       xprt = %p, tcp_copied = %lu, tcp_offset = %u, "
			"tcp_reclen = %u\n", xprt, transport->tcp_copied,
			transport->tcp_offset, transport->tcp_reclen);

	if (transport->tcp_copied == req->rq_private_buf.buflen)
		transport->tcp_flags &= ~TCP_RCV_COPY_DATA;
	else if (transport->tcp_offset == transport->tcp_reclen) {
		if (transport->tcp_flags & TCP_RCV_LAST_FRAG)
			transport->tcp_flags &= ~TCP_RCV_COPY_DATA;
	}

	return;
}

/*
 * Finds the request corresponding to the RPC xid and invokes the common
 * tcp read code to read the data.
 */
static inline int xs_tcp_read_reply(struct rpc_xprt *xprt,
				    struct xdr_skb_reader *desc)
{
	struct sock_xprt *transport =
				container_of(xprt, struct sock_xprt, xprt);
	struct rpc_rqst *req;

	dprintk("RPC:       read reply XID %08x\n", ntohl(transport->tcp_xid));

	/* Find and lock the request corresponding to this xid */
	spin_lock(&xprt->transport_lock);
	req = xprt_lookup_rqst(xprt, transport->tcp_xid);
	if (!req) {
		dprintk("RPC:       XID %08x request not found!\n",
				ntohl(transport->tcp_xid));
		spin_unlock(&xprt->transport_lock);
		return -1;
	}

	xs_tcp_read_common(xprt, desc, req);

	if (!(transport->tcp_flags & TCP_RCV_COPY_DATA))
		xprt_complete_rqst(req->rq_task, transport->tcp_copied);

	spin_unlock(&xprt->transport_lock);
	return 0;
}

#if defined(CONFIG_NFS_V4_1)
/*
 * Obtains an rpc_rqst previously allocated and invokes the common
 * tcp read code to read the data.  The result is placed in the callback
 * queue.
 * If we're unable to obtain the rpc_rqst we schedule the closing of the
 * connection and return -1.
 */
static inline int xs_tcp_read_callback(struct rpc_xprt *xprt,
				       struct xdr_skb_reader *desc)
{
	struct sock_xprt *transport =
				container_of(xprt, struct sock_xprt, xprt);
	struct rpc_rqst *req;

	req = xprt_alloc_bc_request(xprt);
	if (req == NULL) {
		printk(KERN_WARNING "Callback slot table overflowed\n");
		xprt_force_disconnect(xprt);
		return -1;
	}

	req->rq_xid = transport->tcp_xid;
	dprintk("RPC:       read callback  XID %08x\n", ntohl(req->rq_xid));
	xs_tcp_read_common(xprt, desc, req);

	if (!(transport->tcp_flags & TCP_RCV_COPY_DATA)) {
		struct svc_serv *bc_serv = xprt->bc_serv;

		/*
		 * Add callback request to callback list.  The callback
		 * service sleeps on the sv_cb_waitq waiting for new
		 * requests.  Wake it up after adding enqueing the
		 * request.
		 */
		dprintk("RPC:       add callback request to list\n");
		spin_lock(&bc_serv->sv_cb_lock);
		list_add(&req->rq_bc_list, &bc_serv->sv_cb_list);
		spin_unlock(&bc_serv->sv_cb_lock);
		wake_up(&bc_serv->sv_cb_waitq);
	}

	req->rq_private_buf.len = transport->tcp_copied;

	return 0;
}

static inline int _xs_tcp_read_data(struct rpc_xprt *xprt,
					struct xdr_skb_reader *desc)
{
	struct sock_xprt *transport =
				container_of(xprt, struct sock_xprt, xprt);

	return (transport->tcp_flags & TCP_RPC_REPLY) ?
		xs_tcp_read_reply(xprt, desc) :
		xs_tcp_read_callback(xprt, desc);
}
#else
static inline int _xs_tcp_read_data(struct rpc_xprt *xprt,
					struct xdr_skb_reader *desc)
{
	return xs_tcp_read_reply(xprt, desc);
}
#endif /* CONFIG_NFS_V4_1 */

/*
 * Read data off the transport.  This can be either an RPC_CALL or an
 * RPC_REPLY.  Relay the processing to helper functions.
 */
static void xs_tcp_read_data(struct rpc_xprt *xprt,
				    struct xdr_skb_reader *desc)
{
	struct sock_xprt *transport =
				container_of(xprt, struct sock_xprt, xprt);

	if (_xs_tcp_read_data(xprt, desc) == 0)
		xs_tcp_check_fraghdr(transport);
	else {
		/*
		 * The transport_lock protects the request handling.
		 * There's no need to hold it to update the tcp_flags.
		 */
		transport->tcp_flags &= ~TCP_RCV_COPY_DATA;
	}
}

static inline void xs_tcp_read_discard(struct sock_xprt *transport, struct xdr_skb_reader *desc)
{
	size_t len;

	len = transport->tcp_reclen - transport->tcp_offset;
	if (len > desc->count)
		len = desc->count;
	desc->count -= len;
	desc->offset += len;
	transport->tcp_offset += len;
	dprintk("RPC:       discarded %Zu bytes\n", len);
	xs_tcp_check_fraghdr(transport);
}

static int xs_tcp_data_recv(read_descriptor_t *rd_desc, struct sk_buff *skb, unsigned int offset, size_t len)
{
	struct rpc_xprt *xprt = rd_desc->arg.data;
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	struct xdr_skb_reader desc = {
		.skb	= skb,
		.offset	= offset,
		.count	= len,
	};

	dprintk("RPC:       xs_tcp_data_recv started\n");
	do {
		/* Read in a new fragment marker if necessary */
		/* Can we ever really expect to get completely empty fragments? */
		if (transport->tcp_flags & TCP_RCV_COPY_FRAGHDR) {
			xs_tcp_read_fraghdr(xprt, &desc);
			continue;
		}
		/* Read in the xid if necessary */
		if (transport->tcp_flags & TCP_RCV_COPY_XID) {
			xs_tcp_read_xid(transport, &desc);
			continue;
		}
		/* Read in the call/reply flag */
		if (transport->tcp_flags & TCP_RCV_READ_CALLDIR) {
			xs_tcp_read_calldir(transport, &desc);
			continue;
		}
		/* Read in the request data */
		if (transport->tcp_flags & TCP_RCV_COPY_DATA) {
			xs_tcp_read_data(xprt, &desc);
			continue;
		}
		/* Skip over any trailing bytes on short reads */
		xs_tcp_read_discard(transport, &desc);
	} while (desc.count);
	dprintk("RPC:       xs_tcp_data_recv done\n");
	return len - desc.count;
}

/**
 * xs_tcp_data_ready - "data ready" callback for TCP sockets
 * @sk: socket with data to read
 * @bytes: how much data to read
 *
 */
static void xs_tcp_data_ready(struct sock *sk, int bytes)
{
	struct rpc_xprt *xprt;
	read_descriptor_t rd_desc;
	int read;

	dprintk("RPC:       xs_tcp_data_ready...\n");

	read_lock(&sk->sk_callback_lock);
	if (!(xprt = xprt_from_sock(sk)))
		goto out;
	if (xprt->shutdown)
		goto out;

	/* Any data means we had a useful conversation, so
	 * the we don't need to delay the next reconnect
	 */
	if (xprt->reestablish_timeout)
		xprt->reestablish_timeout = 0;

	/* We use rd_desc to pass struct xprt to xs_tcp_data_recv */
	rd_desc.arg.data = xprt;
	do {
		rd_desc.count = 65536;
		read = tcp_read_sock(sk, &rd_desc, xs_tcp_data_recv);
	} while (read > 0);
out:
	read_unlock(&sk->sk_callback_lock);
}

/*
 * Do the equivalent of linger/linger2 handling for dealing with
 * broken servers that don't close the socket in a timely
 * fashion
 */
static void xs_tcp_schedule_linger_timeout(struct rpc_xprt *xprt,
		unsigned long timeout)
{
	struct sock_xprt *transport;

	if (xprt_test_and_set_connecting(xprt))
		return;
	set_bit(XPRT_CONNECTION_ABORT, &xprt->state);
	transport = container_of(xprt, struct sock_xprt, xprt);
	queue_delayed_work(rpciod_workqueue, &transport->connect_worker,
			   timeout);
}

static void xs_tcp_cancel_linger_timeout(struct rpc_xprt *xprt)
{
	struct sock_xprt *transport;

	transport = container_of(xprt, struct sock_xprt, xprt);

	if (!test_bit(XPRT_CONNECTION_ABORT, &xprt->state) ||
	    !cancel_delayed_work(&transport->connect_worker))
		return;
	clear_bit(XPRT_CONNECTION_ABORT, &xprt->state);
	xprt_clear_connecting(xprt);
}

static void xs_sock_mark_closed(struct rpc_xprt *xprt)
{
	smp_mb__before_clear_bit();
	clear_bit(XPRT_CLOSE_WAIT, &xprt->state);
	clear_bit(XPRT_CLOSING, &xprt->state);
	smp_mb__after_clear_bit();
	/* Mark transport as closed and wake up all pending tasks */
	xprt_disconnect_done(xprt);
}

/**
 * xs_tcp_state_change - callback to handle TCP socket state changes
 * @sk: socket whose state has changed
 *
 */
static void xs_tcp_state_change(struct sock *sk)
{
	struct rpc_xprt *xprt;

	read_lock(&sk->sk_callback_lock);
	if (!(xprt = xprt_from_sock(sk)))
		goto out;
	dprintk("RPC:       xs_tcp_state_change client %p...\n", xprt);
	dprintk("RPC:       state %x conn %d dead %d zapped %d sk_shutdown %d\n",
			sk->sk_state, xprt_connected(xprt),
			sock_flag(sk, SOCK_DEAD),
			sock_flag(sk, SOCK_ZAPPED),
			sk->sk_shutdown);

	switch (sk->sk_state) {
	case TCP_ESTABLISHED:
		spin_lock_bh(&xprt->transport_lock);
		if (!xprt_test_and_set_connected(xprt)) {
			struct sock_xprt *transport = container_of(xprt,
					struct sock_xprt, xprt);

			/* Reset TCP record info */
			transport->tcp_offset = 0;
			transport->tcp_reclen = 0;
			transport->tcp_copied = 0;
			transport->tcp_flags =
				TCP_RCV_COPY_FRAGHDR | TCP_RCV_COPY_XID;

			xprt_wake_pending_tasks(xprt, -EAGAIN);
		}
		spin_unlock_bh(&xprt->transport_lock);
		break;
	case TCP_FIN_WAIT1:
		/* The client initiated a shutdown of the socket */
		xprt->connect_cookie++;
		xprt->reestablish_timeout = 0;
		set_bit(XPRT_CLOSING, &xprt->state);
		smp_mb__before_clear_bit();
		clear_bit(XPRT_CONNECTED, &xprt->state);
		clear_bit(XPRT_CLOSE_WAIT, &xprt->state);
		smp_mb__after_clear_bit();
		xs_tcp_schedule_linger_timeout(xprt, xs_tcp_fin_timeout);
		break;
	case TCP_CLOSE_WAIT:
		/* The server initiated a shutdown of the socket */
		xprt_force_disconnect(xprt);
	case TCP_SYN_SENT:
		xprt->connect_cookie++;
	case TCP_CLOSING:
		/*
		 * If the server closed down the connection, make sure that
		 * we back off before reconnecting
		 */
		if (xprt->reestablish_timeout < XS_TCP_INIT_REEST_TO)
			xprt->reestablish_timeout = XS_TCP_INIT_REEST_TO;
		break;
	case TCP_LAST_ACK:
		set_bit(XPRT_CLOSING, &xprt->state);
		xs_tcp_schedule_linger_timeout(xprt, xs_tcp_fin_timeout);
		smp_mb__before_clear_bit();
		clear_bit(XPRT_CONNECTED, &xprt->state);
		smp_mb__after_clear_bit();
		break;
	case TCP_CLOSE:
		xs_tcp_cancel_linger_timeout(xprt);
		xs_sock_mark_closed(xprt);
	}
 out:
	read_unlock(&sk->sk_callback_lock);
}

/**
 * xs_error_report - callback mainly for catching socket errors
 * @sk: socket
 */
static void xs_error_report(struct sock *sk)
{
	struct rpc_xprt *xprt;

	read_lock(&sk->sk_callback_lock);
	if (!(xprt = xprt_from_sock(sk)))
		goto out;
	dprintk("RPC:       %s client %p...\n"
			"RPC:       error %d\n",
			__func__, xprt, sk->sk_err);
	xprt_wake_pending_tasks(xprt, -EAGAIN);
out:
	read_unlock(&sk->sk_callback_lock);
}

static void xs_write_space(struct sock *sk)
{
	struct socket *sock;
	struct rpc_xprt *xprt;

	if (unlikely(!(sock = sk->sk_socket)))
		return;
	clear_bit(SOCK_NOSPACE, &sock->flags);

	if (unlikely(!(xprt = xprt_from_sock(sk))))
		return;
	if (test_and_clear_bit(SOCK_ASYNC_NOSPACE, &sock->flags) == 0)
		return;

	xprt_write_space(xprt);
}

/**
 * xs_udp_write_space - callback invoked when socket buffer space
 *                             becomes available
 * @sk: socket whose state has changed
 *
 * Called when more output buffer space is available for this socket.
 * We try not to wake our writers until they can make "significant"
 * progress, otherwise we'll waste resources thrashing kernel_sendmsg
 * with a bunch of small requests.
 */
static void xs_udp_write_space(struct sock *sk)
{
	read_lock(&sk->sk_callback_lock);

	/* from net/core/sock.c:sock_def_write_space */
	if (sock_writeable(sk))
		xs_write_space(sk);

	read_unlock(&sk->sk_callback_lock);
}

/**
 * xs_tcp_write_space - callback invoked when socket buffer space
 *                             becomes available
 * @sk: socket whose state has changed
 *
 * Called when more output buffer space is available for this socket.
 * We try not to wake our writers until they can make "significant"
 * progress, otherwise we'll waste resources thrashing kernel_sendmsg
 * with a bunch of small requests.
 */
static void xs_tcp_write_space(struct sock *sk)
{
	read_lock(&sk->sk_callback_lock);

	/* from net/core/stream.c:sk_stream_write_space */
	if (sk_stream_wspace(sk) >= sk_stream_min_wspace(sk))
		xs_write_space(sk);

	read_unlock(&sk->sk_callback_lock);
}

static void xs_udp_do_set_buffer_size(struct rpc_xprt *xprt)
{
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	struct sock *sk = transport->inet;

	if (transport->rcvsize) {
		sk->sk_userlocks |= SOCK_RCVBUF_LOCK;
		sk->sk_rcvbuf = transport->rcvsize * xprt->max_reqs * 2;
	}
	if (transport->sndsize) {
		sk->sk_userlocks |= SOCK_SNDBUF_LOCK;
		sk->sk_sndbuf = transport->sndsize * xprt->max_reqs * 2;
		sk->sk_write_space(sk);
	}
}

/**
 * xs_udp_set_buffer_size - set send and receive limits
 * @xprt: generic transport
 * @sndsize: requested size of send buffer, in bytes
 * @rcvsize: requested size of receive buffer, in bytes
 *
 * Set socket send and receive buffer size limits.
 */
static void xs_udp_set_buffer_size(struct rpc_xprt *xprt, size_t sndsize, size_t rcvsize)
{
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);

	transport->sndsize = 0;
	if (sndsize)
		transport->sndsize = sndsize + 1024;
	transport->rcvsize = 0;
	if (rcvsize)
		transport->rcvsize = rcvsize + 1024;

	xs_udp_do_set_buffer_size(xprt);
}

/**
 * xs_udp_timer - called when a retransmit timeout occurs on a UDP transport
 * @task: task that timed out
 *
 * Adjust the congestion window after a retransmit timeout has occurred.
 */
static void xs_udp_timer(struct rpc_task *task)
{
	xprt_adjust_cwnd(task, -ETIMEDOUT);
}

static unsigned short xs_get_random_port(void)
{
	unsigned short range = xprt_max_resvport - xprt_min_resvport;
	unsigned short rand = (unsigned short) net_random() % range;
	return rand + xprt_min_resvport;
}

/**
 * xs_set_port - reset the port number in the remote endpoint address
 * @xprt: generic transport
 * @port: new port number
 *
 */
static void xs_set_port(struct rpc_xprt *xprt, unsigned short port)
{
	dprintk("RPC:       setting port for xprt %p to %u\n", xprt, port);

	rpc_set_port(xs_addr(xprt), port);
	xs_update_peer_port(xprt);
}

static unsigned short xs_get_srcport(struct sock_xprt *transport, struct socket *sock)
{
	unsigned short port = transport->srcport;

	if (port == 0 && transport->xprt.resvport)
		port = xs_get_random_port();
	return port;
}

static unsigned short xs_next_srcport(struct sock_xprt *transport, struct socket *sock, unsigned short port)
{
	if (transport->srcport != 0)
		transport->srcport = 0;
	if (!transport->xprt.resvport)
		return 0;
	if (port <= xprt_min_resvport || port > xprt_max_resvport)
		return xprt_max_resvport;
	return --port;
}

static int xs_bind4(struct sock_xprt *transport, struct socket *sock)
{
	struct sockaddr_in myaddr = {
		.sin_family = AF_INET,
	};
	struct sockaddr_in *sa;
	int err, nloop = 0;
	unsigned short port = xs_get_srcport(transport, sock);
	unsigned short last;

	sa = (struct sockaddr_in *)&transport->srcaddr;
	myaddr.sin_addr = sa->sin_addr;
	do {
		myaddr.sin_port = htons(port);
		err = kernel_bind(sock, (struct sockaddr *) &myaddr,
						sizeof(myaddr));
		if (port == 0)
			break;
		if (err == 0) {
			transport->srcport = port;
			break;
		}
		last = port;
		port = xs_next_srcport(transport, sock, port);
		if (port > last)
			nloop++;
	} while (err == -EADDRINUSE && nloop != 2);
	dprintk("RPC:       %s %pI4:%u: %s (%d)\n",
			__func__, &myaddr.sin_addr,
			port, err ? "failed" : "ok", err);
	return err;
}

static int xs_bind6(struct sock_xprt *transport, struct socket *sock)
{
	struct sockaddr_in6 myaddr = {
		.sin6_family = AF_INET6,
	};
	struct sockaddr_in6 *sa;
	int err, nloop = 0;
	unsigned short port = xs_get_srcport(transport, sock);
	unsigned short last;

	sa = (struct sockaddr_in6 *)&transport->srcaddr;
	myaddr.sin6_addr = sa->sin6_addr;
	do {
		myaddr.sin6_port = htons(port);
		err = kernel_bind(sock, (struct sockaddr *) &myaddr,
						sizeof(myaddr));
		if (port == 0)
			break;
		if (err == 0) {
			transport->srcport = port;
			break;
		}
		last = port;
		port = xs_next_srcport(transport, sock, port);
		if (port > last)
			nloop++;
	} while (err == -EADDRINUSE && nloop != 2);
	dprintk("RPC:       xs_bind6 %pI6:%u: %s (%d)\n",
		&myaddr.sin6_addr, port, err ? "failed" : "ok", err);
	return err;
}

#ifdef CONFIG_DEBUG_LOCK_ALLOC
static struct lock_class_key xs_key[2];
static struct lock_class_key xs_slock_key[2];

static inline void xs_reclassify_socket4(struct socket *sock)
{
	struct sock *sk = sock->sk;

	BUG_ON(sock_owned_by_user(sk));
	sock_lock_init_class_and_name(sk, "slock-AF_INET-RPC",
		&xs_slock_key[0], "sk_lock-AF_INET-RPC", &xs_key[0]);
}

static inline void xs_reclassify_socket6(struct socket *sock)
{
	struct sock *sk = sock->sk;

	BUG_ON(sock_owned_by_user(sk));
	sock_lock_init_class_and_name(sk, "slock-AF_INET6-RPC",
		&xs_slock_key[1], "sk_lock-AF_INET6-RPC", &xs_key[1]);
}
#else
static inline void xs_reclassify_socket4(struct socket *sock)
{
}

static inline void xs_reclassify_socket6(struct socket *sock)
{
}
#endif

static void xs_udp_finish_connecting(struct rpc_xprt *xprt, struct socket *sock)
{
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);

	if (!transport->inet) {
		struct sock *sk = sock->sk;

		write_lock_bh(&sk->sk_callback_lock);

		xs_save_old_callbacks(transport, sk);

		sk->sk_user_data = xprt;
		sk->sk_data_ready = xs_udp_data_ready;
		sk->sk_write_space = xs_udp_write_space;
		sk->sk_error_report = xs_error_report;
		sk->sk_no_check = UDP_CSUM_NORCV;
		sk->sk_allocation = GFP_ATOMIC;

		xprt_set_connected(xprt);

		/* Reset to new socket */
		transport->sock = sock;
		transport->inet = sk;

		write_unlock_bh(&sk->sk_callback_lock);
	}
	xs_udp_do_set_buffer_size(xprt);
}

/**
 * xs_udp_connect_worker4 - set up a UDP socket
 * @work: RPC transport to connect
 *
 * Invoked by a work queue tasklet.
 */
static void xs_udp_connect_worker4(struct work_struct *work)
{
	struct sock_xprt *transport =
		container_of(work, struct sock_xprt, connect_worker.work);
	struct rpc_xprt *xprt = &transport->xprt;
	struct socket *sock = transport->sock;
	int err, status = -EIO;

	if (xprt->shutdown)
		goto out;

	/* Start by resetting any existing state */
	xs_reset_transport(transport);

	err = sock_create_kern(PF_INET, SOCK_DGRAM, IPPROTO_UDP, &sock);
	if (err < 0) {
		dprintk("RPC:       can't create UDP transport socket (%d).\n", -err);
		goto out;
	}
	xs_reclassify_socket4(sock);

	if (xs_bind4(transport, sock)) {
		sock_release(sock);
		goto out;
	}

	dprintk("RPC:       worker connecting xprt %p via %s to "
				"%s (port %s)\n", xprt,
			xprt->address_strings[RPC_DISPLAY_PROTO],
			xprt->address_strings[RPC_DISPLAY_ADDR],
			xprt->address_strings[RPC_DISPLAY_PORT]);

	xs_udp_finish_connecting(xprt, sock);
	status = 0;
out:
	xprt_clear_connecting(xprt);
	xprt_wake_pending_tasks(xprt, status);
}

/**
 * xs_udp_connect_worker6 - set up a UDP socket
 * @work: RPC transport to connect
 *
 * Invoked by a work queue tasklet.
 */
static void xs_udp_connect_worker6(struct work_struct *work)
{
	struct sock_xprt *transport =
		container_of(work, struct sock_xprt, connect_worker.work);
	struct rpc_xprt *xprt = &transport->xprt;
	struct socket *sock = transport->sock;
	int err, status = -EIO;

	if (xprt->shutdown)
		goto out;

	/* Start by resetting any existing state */
	xs_reset_transport(transport);

	err = sock_create_kern(PF_INET6, SOCK_DGRAM, IPPROTO_UDP, &sock);
	if (err < 0) {
		dprintk("RPC:       can't create UDP transport socket (%d).\n", -err);
		goto out;
	}
	xs_reclassify_socket6(sock);

	if (xs_bind6(transport, sock) < 0) {
		sock_release(sock);
		goto out;
	}

	dprintk("RPC:       worker connecting xprt %p via %s to "
				"%s (port %s)\n", xprt,
			xprt->address_strings[RPC_DISPLAY_PROTO],
			xprt->address_strings[RPC_DISPLAY_ADDR],
			xprt->address_strings[RPC_DISPLAY_PORT]);

	xs_udp_finish_connecting(xprt, sock);
	status = 0;
out:
	xprt_clear_connecting(xprt);
	xprt_wake_pending_tasks(xprt, status);
}

/*
 * We need to preserve the port number so the reply cache on the server can
 * find our cached RPC replies when we get around to reconnecting.
 */
static void xs_abort_connection(struct rpc_xprt *xprt, struct sock_xprt *transport)
{
	int result;
	struct sockaddr any;

	dprintk("RPC:       disconnecting xprt %p to reuse port\n", xprt);

	/*
	 * Disconnect the transport socket by doing a connect operation
	 * with AF_UNSPEC.  This should return immediately...
	 */
	memset(&any, 0, sizeof(any));
	any.sa_family = AF_UNSPEC;
	result = kernel_connect(transport->sock, &any, sizeof(any), 0);
	if (!result)
		xs_sock_mark_closed(xprt);
	else
		dprintk("RPC:       AF_UNSPEC connect return code %d\n",
				result);
}

static void xs_tcp_reuse_connection(struct rpc_xprt *xprt, struct sock_xprt *transport)
{
	unsigned int state = transport->inet->sk_state;

	if (state == TCP_CLOSE && transport->sock->state == SS_UNCONNECTED) {
		/* we don't need to abort the connection if the socket
		 * hasn't undergone a shutdown
		 */
		if (transport->inet->sk_shutdown == 0)
			return;
		dprintk("RPC:       %s: TCP_CLOSEd and sk_shutdown set to %d\n",
				__func__, transport->inet->sk_shutdown);
	}
	if ((1 << state) & (TCPF_ESTABLISHED|TCPF_SYN_SENT)) {
		/* we don't need to abort the connection if the socket
		 * hasn't undergone a shutdown
		 */
		if (transport->inet->sk_shutdown == 0)
			return;
		dprintk("RPC:       %s: ESTABLISHED/SYN_SENT "
				"sk_shutdown set to %d\n",
				__func__, transport->inet->sk_shutdown);
	}
	xs_abort_connection(xprt, transport);
}

static int xs_tcp_finish_connecting(struct rpc_xprt *xprt, struct socket *sock)
{
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);

	if (!transport->inet) {
		struct sock *sk = sock->sk;

		write_lock_bh(&sk->sk_callback_lock);

		xs_save_old_callbacks(transport, sk);

		sk->sk_user_data = xprt;
		sk->sk_data_ready = xs_tcp_data_ready;
		sk->sk_state_change = xs_tcp_state_change;
		sk->sk_write_space = xs_tcp_write_space;
		sk->sk_error_report = xs_error_report;
		sk->sk_allocation = GFP_ATOMIC;

		/* socket options */
		sk->sk_userlocks |= SOCK_BINDPORT_LOCK;
		sock_reset_flag(sk, SOCK_LINGER);
		tcp_sk(sk)->linger2 = 0;
		tcp_sk(sk)->nonagle |= TCP_NAGLE_OFF;

		xprt_clear_connected(xprt);

		/* Reset to new socket */
		transport->sock = sock;
		transport->inet = sk;

		write_unlock_bh(&sk->sk_callback_lock);
	}

	if (!xprt_bound(xprt))
		return -ENOTCONN;

	/* Tell the socket layer to start connecting... */
	xprt->stat.connect_count++;
	xprt->stat.connect_start = jiffies;
	return kernel_connect(sock, xs_addr(xprt), xprt->addrlen, O_NONBLOCK);
}

/**
 * xs_tcp_setup_socket - create a TCP socket and connect to a remote endpoint
 * @xprt: RPC transport to connect
 * @transport: socket transport to connect
 * @create_sock: function to create a socket of the correct type
 *
 * Invoked by a work queue tasklet.
 */
static void xs_tcp_setup_socket(struct rpc_xprt *xprt,
		struct sock_xprt *transport,
		struct socket *(*create_sock)(struct rpc_xprt *,
			struct sock_xprt *))
{
	struct socket *sock = transport->sock;
	int status = -EIO;

	if (xprt->shutdown)
		goto out;

	if (!sock) {
		clear_bit(XPRT_CONNECTION_ABORT, &xprt->state);
		sock = create_sock(xprt, transport);
		if (IS_ERR(sock)) {
			status = PTR_ERR(sock);
			goto out;
		}
	} else {
		int abort_and_exit;

		abort_and_exit = test_and_clear_bit(XPRT_CONNECTION_ABORT,
				&xprt->state);
		/* "close" the socket, preserving the local port */
		xs_tcp_reuse_connection(xprt, transport);

		if (abort_and_exit)
			goto out_eagain;
	}

	dprintk("RPC:       worker connecting xprt %p via %s to "
				"%s (port %s)\n", xprt,
			xprt->address_strings[RPC_DISPLAY_PROTO],
			xprt->address_strings[RPC_DISPLAY_ADDR],
			xprt->address_strings[RPC_DISPLAY_PORT]);

	status = xs_tcp_finish_connecting(xprt, sock);
	dprintk("RPC:       %p connect status %d connected %d sock state %d\n",
			xprt, -status, xprt_connected(xprt),
			sock->sk->sk_state);
	switch (status) {
	default:
		printk("%s: connect returned unhandled error %d\n",
			__func__, status);
	case -EADDRNOTAVAIL:
		/* We're probably in TIME_WAIT. Get rid of existing socket,
		 * and retry
		 */
		set_bit(XPRT_CONNECTION_CLOSE, &xprt->state);
		xprt_force_disconnect(xprt);
		break;
	case -ECONNREFUSED:
	case -ECONNRESET:
	case -ENETUNREACH:
		/* retry with existing socket, after a delay */
	case 0:
	case -EINPROGRESS:
	case -EALREADY:
		xprt_clear_connecting(xprt);
		return;
	case -EINVAL:
		/* Happens, for instance, if the user specified a link
		 * local IPv6 address without a scope-id.
		 */
		goto out;
	}
out_eagain:
	status = -EAGAIN;
out:
	xprt_clear_connecting(xprt);
	xprt_wake_pending_tasks(xprt, status);
}

static struct socket *xs_create_tcp_sock4(struct rpc_xprt *xprt,
		struct sock_xprt *transport)
{
	struct socket *sock;
	int err;

	/* start from scratch */
	err = sock_create_kern(PF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
	if (err < 0) {
		dprintk("RPC:       can't create TCP transport socket (%d).\n",
				-err);
		goto out_err;
	}
	xs_reclassify_socket4(sock);

	if (xs_bind4(transport, sock) < 0) {
		sock_release(sock);
		goto out_err;
	}
	return sock;
out_err:
	return ERR_PTR(-EIO);
}

/**
 * xs_tcp_connect_worker4 - connect a TCP socket to a remote endpoint
 * @work: RPC transport to connect
 *
 * Invoked by a work queue tasklet.
 */
static void xs_tcp_connect_worker4(struct work_struct *work)
{
	struct sock_xprt *transport =
		container_of(work, struct sock_xprt, connect_worker.work);
	struct rpc_xprt *xprt = &transport->xprt;

	xs_tcp_setup_socket(xprt, transport, xs_create_tcp_sock4);
}

static struct socket *xs_create_tcp_sock6(struct rpc_xprt *xprt,
		struct sock_xprt *transport)
{
	struct socket *sock;
	int err;

	/* start from scratch */
	err = sock_create_kern(PF_INET6, SOCK_STREAM, IPPROTO_TCP, &sock);
	if (err < 0) {
		dprintk("RPC:       can't create TCP transport socket (%d).\n",
				-err);
		goto out_err;
	}
	xs_reclassify_socket6(sock);

	if (xs_bind6(transport, sock) < 0) {
		sock_release(sock);
		goto out_err;
	}
	return sock;
out_err:
	return ERR_PTR(-EIO);
}

/**
 * xs_tcp_connect_worker6 - connect a TCP socket to a remote endpoint
 * @work: RPC transport to connect
 *
 * Invoked by a work queue tasklet.
 */
static void xs_tcp_connect_worker6(struct work_struct *work)
{
	struct sock_xprt *transport =
		container_of(work, struct sock_xprt, connect_worker.work);
	struct rpc_xprt *xprt = &transport->xprt;

	xs_tcp_setup_socket(xprt, transport, xs_create_tcp_sock6);
}

/**
 * xs_connect - connect a socket to a remote endpoint
 * @task: address of RPC task that manages state of connect request
 *
 * TCP: If the remote end dropped the connection, delay reconnecting.
 *
 * UDP socket connects are synchronous, but we use a work queue anyway
 * to guarantee that even unprivileged user processes can set up a
 * socket on a privileged port.
 *
 * If a UDP socket connect fails, the delay behavior here prevents
 * retry floods (hard mounts).
 */
static void xs_connect(struct rpc_task *task)
{
	struct rpc_xprt *xprt = task->tk_xprt;
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);

	if (xprt_test_and_set_connecting(xprt))
		return;

	if (transport->sock != NULL) {
		dprintk("RPC:       xs_connect delayed xprt %p for %lu "
				"seconds\n",
				xprt, xprt->reestablish_timeout / HZ);
		queue_delayed_work(rpciod_workqueue,
				   &transport->connect_worker,
				   xprt->reestablish_timeout);
		xprt->reestablish_timeout <<= 1;
		if (xprt->reestablish_timeout < XS_TCP_INIT_REEST_TO)
			xprt->reestablish_timeout = XS_TCP_INIT_REEST_TO;
		if (xprt->reestablish_timeout > XS_TCP_MAX_REEST_TO)
			xprt->reestablish_timeout = XS_TCP_MAX_REEST_TO;
	} else {
		dprintk("RPC:       xs_connect scheduled xprt %p\n", xprt);
		queue_delayed_work(rpciod_workqueue,
				   &transport->connect_worker, 0);
	}
}

static void xs_tcp_connect(struct rpc_task *task)
{
	struct rpc_xprt *xprt = task->tk_xprt;

	/* Exit if we need to wait for socket shutdown to complete */
	if (test_bit(XPRT_CLOSING, &xprt->state))
		return;
	xs_connect(task);
}

/**
 * xs_udp_print_stats - display UDP socket-specifc stats
 * @xprt: rpc_xprt struct containing statistics
 * @seq: output file
 *
 */
static void xs_udp_print_stats(struct rpc_xprt *xprt, struct seq_file *seq)
{
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);

	seq_printf(seq, "\txprt:\tudp %u %lu %lu %lu %lu %Lu %Lu\n",
			transport->srcport,
			xprt->stat.bind_count,
			xprt->stat.sends,
			xprt->stat.recvs,
			xprt->stat.bad_xids,
			xprt->stat.req_u,
			xprt->stat.bklog_u);
}

/**
 * xs_tcp_print_stats - display TCP socket-specifc stats
 * @xprt: rpc_xprt struct containing statistics
 * @seq: output file
 *
 */
static void xs_tcp_print_stats(struct rpc_xprt *xprt, struct seq_file *seq)
{
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	long idle_time = 0;

	if (xprt_connected(xprt))
		idle_time = (long)(jiffies - xprt->last_used) / HZ;

	seq_printf(seq, "\txprt:\ttcp %u %lu %lu %lu %ld %lu %lu %lu %Lu %Lu\n",
			transport->srcport,
			xprt->stat.bind_count,
			xprt->stat.connect_count,
			xprt->stat.connect_time,
			idle_time,
			xprt->stat.sends,
			xprt->stat.recvs,
			xprt->stat.bad_xids,
			xprt->stat.req_u,
			xprt->stat.bklog_u);
}

/*
 * Allocate a bunch of pages for a scratch buffer for the rpc code. The reason
 * we allocate pages instead doing a kmalloc like rpc_malloc is because we want
 * to use the server side send routines.
 */
void *bc_malloc(struct rpc_task *task, size_t size)
{
	struct page *page;
	struct rpc_buffer *buf;

	BUG_ON(size > PAGE_SIZE - sizeof(struct rpc_buffer));
	page = alloc_page(GFP_KERNEL);

	if (!page)
		return NULL;

	buf = page_address(page);
	buf->len = PAGE_SIZE;

	return buf->data;
}

/*
 * Free the space allocated in the bc_alloc routine
 */
void bc_free(void *buffer)
{
	struct rpc_buffer *buf;

	if (!buffer)
		return;

	buf = container_of(buffer, struct rpc_buffer, data);
	free_page((unsigned long)buf);
}

/*
 * Use the svc_sock to send the callback. Must be called with svsk->sk_mutex
 * held. Borrows heavily from svc_tcp_sendto and xs_tcp_send_request.
 */
static int bc_sendto(struct rpc_rqst *req)
{
	int len;
	struct xdr_buf *xbufp = &req->rq_snd_buf;
	struct rpc_xprt *xprt = req->rq_xprt;
	struct sock_xprt *transport =
				container_of(xprt, struct sock_xprt, xprt);
	struct socket *sock = transport->sock;
	unsigned long headoff;
	unsigned long tailoff;

	/*
	 * Set up the rpc header and record marker stuff
	 */
	xs_encode_tcp_record_marker(xbufp);

	tailoff = (unsigned long)xbufp->tail[0].iov_base & ~PAGE_MASK;
	headoff = (unsigned long)xbufp->head[0].iov_base & ~PAGE_MASK;
	len = svc_send_common(sock, xbufp,
			      virt_to_page(xbufp->head[0].iov_base), headoff,
			      xbufp->tail[0].iov_base, tailoff);

	if (len != xbufp->len) {
		printk(KERN_NOTICE "Error sending entire callback!\n");
		len = -EAGAIN;
	}

	return len;
}

/*
 * The send routine. Borrows from svc_send
 */
static int bc_send_request(struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;
	struct svc_xprt	*xprt;
	struct svc_sock         *svsk;
	u32                     len;

	dprintk("sending request with xid: %08x\n", ntohl(req->rq_xid));
	/*
	 * Get the server socket associated with this callback xprt
	 */
	xprt = req->rq_xprt->bc_xprt;
	svsk = container_of(xprt, struct svc_sock, sk_xprt);

	/*
	 * Grab the mutex to serialize data as the connection is shared
	 * with the fore channel
	 */
	if (!mutex_trylock(&xprt->xpt_mutex)) {
		rpc_sleep_on(&xprt->xpt_bc_pending, task, NULL);
		if (!mutex_trylock(&xprt->xpt_mutex))
			return -EAGAIN;
		rpc_wake_up_queued_task(&xprt->xpt_bc_pending, task);
	}
	if (test_bit(XPT_DEAD, &xprt->xpt_flags))
		len = -ENOTCONN;
	else
		len = bc_sendto(req);
	mutex_unlock(&xprt->xpt_mutex);

	if (len > 0)
		len = 0;

	return len;
}

/*
 * The close routine. Since this is client initiated, we do nothing
 */

static void bc_close(struct rpc_xprt *xprt)
{
	return;
}

/*
 * The xprt destroy routine. Again, because this connection is client
 * initiated, we do nothing
 */

static void bc_destroy(struct rpc_xprt *xprt)
{
	return;
}

static struct rpc_xprt_ops xs_udp_ops = {
	.set_buffer_size	= xs_udp_set_buffer_size,
	.reserve_xprt		= xprt_reserve_xprt_cong,
	.release_xprt		= xprt_release_xprt_cong,
	.rpcbind		= rpcb_getport_async,
	.set_port		= xs_set_port,
	.connect		= xs_connect,
	.buf_alloc		= rpc_malloc,
	.buf_free		= rpc_free,
	.send_request		= xs_udp_send_request,
	.set_retrans_timeout	= xprt_set_retrans_timeout_rtt,
	.timer			= xs_udp_timer,
	.release_request	= xprt_release_rqst_cong,
	.close			= xs_close,
	.destroy		= xs_destroy,
	.print_stats		= xs_udp_print_stats,
};

static struct rpc_xprt_ops xs_tcp_ops = {
	.reserve_xprt		= xprt_reserve_xprt,
	.release_xprt		= xs_tcp_release_xprt,
	.rpcbind		= rpcb_getport_async,
	.set_port		= xs_set_port,
	.connect		= xs_tcp_connect,
	.buf_alloc		= rpc_malloc,
	.buf_free		= rpc_free,
	.send_request		= xs_tcp_send_request,
	.set_retrans_timeout	= xprt_set_retrans_timeout_def,
#if defined(CONFIG_NFS_V4_1)
	.release_request	= bc_release_request,
#endif /* CONFIG_NFS_V4_1 */
	.close			= xs_tcp_close,
	.destroy		= xs_destroy,
	.print_stats		= xs_tcp_print_stats,
};

/*
 * The rpc_xprt_ops for the server backchannel
 */

static struct rpc_xprt_ops bc_tcp_ops = {
	.reserve_xprt		= xprt_reserve_xprt,
	.release_xprt		= xprt_release_xprt,
	.buf_alloc		= bc_malloc,
	.buf_free		= bc_free,
	.send_request		= bc_send_request,
	.set_retrans_timeout	= xprt_set_retrans_timeout_def,
	.close			= bc_close,
	.destroy		= bc_destroy,
	.print_stats		= xs_tcp_print_stats,
};

static struct rpc_xprt *xs_setup_xprt(struct xprt_create *args,
				      unsigned int slot_table_size)
{
	struct rpc_xprt *xprt;
	struct sock_xprt *new;

	if (args->addrlen > sizeof(xprt->addr)) {
		dprintk("RPC:       xs_setup_xprt: address too large\n");
		return ERR_PTR(-EBADF);
	}

	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (new == NULL) {
		dprintk("RPC:       xs_setup_xprt: couldn't allocate "
				"rpc_xprt\n");
		return ERR_PTR(-ENOMEM);
	}
	xprt = &new->xprt;

	xprt->max_reqs = slot_table_size;
	xprt->slot = kcalloc(xprt->max_reqs, sizeof(struct rpc_rqst), GFP_KERNEL);
	if (xprt->slot == NULL) {
		kfree(xprt);
		dprintk("RPC:       xs_setup_xprt: couldn't allocate slot "
				"table\n");
		return ERR_PTR(-ENOMEM);
	}

	memcpy(&xprt->addr, args->dstaddr, args->addrlen);
	xprt->addrlen = args->addrlen;
	if (args->srcaddr)
		memcpy(&new->srcaddr, args->srcaddr, args->addrlen);

	return xprt;
}

static const struct rpc_timeout xs_udp_default_timeout = {
	.to_initval = 5 * HZ,
	.to_maxval = 30 * HZ,
	.to_increment = 5 * HZ,
	.to_retries = 5,
};

/**
 * xs_setup_udp - Set up transport to use a UDP socket
 * @args: rpc transport creation arguments
 *
 */
static struct rpc_xprt *xs_setup_udp(struct xprt_create *args)
{
	struct sockaddr *addr = args->dstaddr;
	struct rpc_xprt *xprt;
	struct sock_xprt *transport;

	xprt = xs_setup_xprt(args, xprt_udp_slot_table_entries);
	if (IS_ERR(xprt))
		return xprt;
	transport = container_of(xprt, struct sock_xprt, xprt);

	xprt->prot = IPPROTO_UDP;
	xprt->tsh_size = 0;
	/* XXX: header size can vary due to auth type, IPv6, etc. */
	xprt->max_payload = (1U << 16) - (MAX_HEADER << 3);

	xprt->bind_timeout = XS_BIND_TO;
	xprt->connect_timeout = XS_UDP_CONN_TO;
	xprt->reestablish_timeout = XS_UDP_REEST_TO;
	xprt->idle_timeout = XS_IDLE_DISC_TO;

	xprt->ops = &xs_udp_ops;

	xprt->timeout = &xs_udp_default_timeout;

	switch (addr->sa_family) {
	case AF_INET:
		if (((struct sockaddr_in *)addr)->sin_port != htons(0))
			xprt_set_bound(xprt);

		INIT_DELAYED_WORK(&transport->connect_worker,
					xs_udp_connect_worker4);
		xs_format_peer_addresses(xprt, "udp", RPCBIND_NETID_UDP);
		break;
	case AF_INET6:
		if (((struct sockaddr_in6 *)addr)->sin6_port != htons(0))
			xprt_set_bound(xprt);

		INIT_DELAYED_WORK(&transport->connect_worker,
					xs_udp_connect_worker6);
		xs_format_peer_addresses(xprt, "udp", RPCBIND_NETID_UDP6);
		break;
	default:
		kfree(xprt);
		return ERR_PTR(-EAFNOSUPPORT);
	}

	if (xprt_bound(xprt))
		dprintk("RPC:       set up xprt to %s (port %s) via %s\n",
				xprt->address_strings[RPC_DISPLAY_ADDR],
				xprt->address_strings[RPC_DISPLAY_PORT],
				xprt->address_strings[RPC_DISPLAY_PROTO]);
	else
		dprintk("RPC:       set up xprt to %s (autobind) via %s\n",
				xprt->address_strings[RPC_DISPLAY_ADDR],
				xprt->address_strings[RPC_DISPLAY_PROTO]);

	if (try_module_get(THIS_MODULE))
		return xprt;

	kfree(xprt->slot);
	kfree(xprt);
	return ERR_PTR(-EINVAL);
}

static const struct rpc_timeout xs_tcp_default_timeout = {
	.to_initval = 60 * HZ,
	.to_maxval = 60 * HZ,
	.to_retries = 2,
};

/**
 * xs_setup_tcp - Set up transport to use a TCP socket
 * @args: rpc transport creation arguments
 *
 */
static struct rpc_xprt *xs_setup_tcp(struct xprt_create *args)
{
	struct sockaddr *addr = args->dstaddr;
	struct rpc_xprt *xprt;
	struct sock_xprt *transport;

	xprt = xs_setup_xprt(args, xprt_tcp_slot_table_entries);
	if (IS_ERR(xprt))
		return xprt;
	transport = container_of(xprt, struct sock_xprt, xprt);

	xprt->prot = IPPROTO_TCP;
	xprt->tsh_size = sizeof(rpc_fraghdr) / sizeof(u32);
	xprt->max_payload = RPC_MAX_FRAGMENT_SIZE;

	xprt->bind_timeout = XS_BIND_TO;
	xprt->connect_timeout = XS_TCP_CONN_TO;
	xprt->reestablish_timeout = XS_TCP_INIT_REEST_TO;
	xprt->idle_timeout = XS_IDLE_DISC_TO;

	xprt->ops = &xs_tcp_ops;
	xprt->timeout = &xs_tcp_default_timeout;

	switch (addr->sa_family) {
	case AF_INET:
		if (((struct sockaddr_in *)addr)->sin_port != htons(0))
			xprt_set_bound(xprt);

		INIT_DELAYED_WORK(&transport->connect_worker,
					xs_tcp_connect_worker4);
		xs_format_peer_addresses(xprt, "tcp", RPCBIND_NETID_TCP);
		break;
	case AF_INET6:
		if (((struct sockaddr_in6 *)addr)->sin6_port != htons(0))
			xprt_set_bound(xprt);

		INIT_DELAYED_WORK(&transport->connect_worker,
					xs_tcp_connect_worker6);
		xs_format_peer_addresses(xprt, "tcp", RPCBIND_NETID_TCP6);
		break;
	default:
		kfree(xprt);
		return ERR_PTR(-EAFNOSUPPORT);
	}

	if (xprt_bound(xprt))
		dprintk("RPC:       set up xprt to %s (port %s) via %s\n",
				xprt->address_strings[RPC_DISPLAY_ADDR],
				xprt->address_strings[RPC_DISPLAY_PORT],
				xprt->address_strings[RPC_DISPLAY_PROTO]);
	else
		dprintk("RPC:       set up xprt to %s (autobind) via %s\n",
				xprt->address_strings[RPC_DISPLAY_ADDR],
				xprt->address_strings[RPC_DISPLAY_PROTO]);


	if (try_module_get(THIS_MODULE))
		return xprt;

	kfree(xprt->slot);
	kfree(xprt);
	return ERR_PTR(-EINVAL);
}

/**
 * xs_setup_bc_tcp - Set up transport to use a TCP backchannel socket
 * @args: rpc transport creation arguments
 *
 */
static struct rpc_xprt *xs_setup_bc_tcp(struct xprt_create *args)
{
	struct sockaddr *addr = args->dstaddr;
	struct rpc_xprt *xprt;
	struct sock_xprt *transport;
	struct svc_sock *bc_sock;

	if (!args->bc_xprt)
		ERR_PTR(-EINVAL);

	xprt = xs_setup_xprt(args, xprt_tcp_slot_table_entries);
	if (IS_ERR(xprt))
		return xprt;
	transport = container_of(xprt, struct sock_xprt, xprt);

	xprt->prot = IPPROTO_TCP;
	xprt->tsh_size = sizeof(rpc_fraghdr) / sizeof(u32);
	xprt->max_payload = RPC_MAX_FRAGMENT_SIZE;
	xprt->timeout = &xs_tcp_default_timeout;

	/* backchannel */
	xprt_set_bound(xprt);
	xprt->bind_timeout = 0;
	xprt->connect_timeout = 0;
	xprt->reestablish_timeout = 0;
	xprt->idle_timeout = 0;

	/*
	 * The backchannel uses the same socket connection as the
	 * forechannel
	 */
	xprt->bc_xprt = args->bc_xprt;
	bc_sock = container_of(args->bc_xprt, struct svc_sock, sk_xprt);
	bc_sock->sk_bc_xprt = xprt;
	transport->sock = bc_sock->sk_sock;
	transport->inet = bc_sock->sk_sk;

	xprt->ops = &bc_tcp_ops;

	switch (addr->sa_family) {
	case AF_INET:
		xs_format_peer_addresses(xprt, "tcp",
					 RPCBIND_NETID_TCP);
		break;
	case AF_INET6:
		xs_format_peer_addresses(xprt, "tcp",
				   RPCBIND_NETID_TCP6);
		break;
	default:
		kfree(xprt);
		return ERR_PTR(-EAFNOSUPPORT);
	}

	if (xprt_bound(xprt))
		dprintk("RPC:       set up xprt to %s (port %s) via %s\n",
				xprt->address_strings[RPC_DISPLAY_ADDR],
				xprt->address_strings[RPC_DISPLAY_PORT],
				xprt->address_strings[RPC_DISPLAY_PROTO]);
	else
		dprintk("RPC:       set up xprt to %s (autobind) via %s\n",
				xprt->address_strings[RPC_DISPLAY_ADDR],
				xprt->address_strings[RPC_DISPLAY_PROTO]);

	/*
	 * Since we don't want connections for the backchannel, we set
	 * the xprt status to connected
	 */
	xprt_set_connected(xprt);


	if (try_module_get(THIS_MODULE))
		return xprt;
	kfree(xprt->slot);
	kfree(xprt);
	return ERR_PTR(-EINVAL);
}

static struct xprt_class	xs_udp_transport = {
	.list		= LIST_HEAD_INIT(xs_udp_transport.list),
	.name		= "udp",
	.owner		= THIS_MODULE,
	.ident		= XPRT_TRANSPORT_UDP,
	.setup		= xs_setup_udp,
};

static struct xprt_class	xs_tcp_transport = {
	.list		= LIST_HEAD_INIT(xs_tcp_transport.list),
	.name		= "tcp",
	.owner		= THIS_MODULE,
	.ident		= XPRT_TRANSPORT_TCP,
	.setup		= xs_setup_tcp,
};

static struct xprt_class	xs_bc_tcp_transport = {
	.list		= LIST_HEAD_INIT(xs_bc_tcp_transport.list),
	.name		= "tcp NFSv4.1 backchannel",
	.owner		= THIS_MODULE,
	.ident		= XPRT_TRANSPORT_BC_TCP,
	.setup		= xs_setup_bc_tcp,
};

/**
 * init_socket_xprt - set up xprtsock's sysctls, register with RPC client
 *
 */
int init_socket_xprt(void)
{
#ifdef RPC_DEBUG
	if (!sunrpc_table_header)
		sunrpc_table_header = register_sysctl_table(sunrpc_table);
#endif

	xprt_register_transport(&xs_udp_transport);
	xprt_register_transport(&xs_tcp_transport);
	xprt_register_transport(&xs_bc_tcp_transport);

	return 0;
}

/**
 * cleanup_socket_xprt - remove xprtsock's sysctls, unregister
 *
 */
void cleanup_socket_xprt(void)
{
#ifdef RPC_DEBUG
	if (sunrpc_table_header) {
		unregister_sysctl_table(sunrpc_table_header);
		sunrpc_table_header = NULL;
	}
#endif

	xprt_unregister_transport(&xs_udp_transport);
	xprt_unregister_transport(&xs_tcp_transport);
	xprt_unregister_transport(&xs_bc_tcp_transport);
}

static int param_set_uint_minmax(const char *val, struct kernel_param *kp,
		unsigned int min, unsigned int max)
{
	unsigned long num;
	int ret;

	if (!val)
		return -EINVAL;
	ret = strict_strtoul(val, 0, &num);
	if (ret == -EINVAL || num < min || num > max)
		return -EINVAL;
	*((unsigned int *)kp->arg) = num;
	return 0;
}

static int param_set_portnr(const char *val, struct kernel_param *kp)
{
	return param_set_uint_minmax(val, kp,
			RPC_MIN_RESVPORT,
			RPC_MAX_RESVPORT);
}

static int param_get_portnr(char *buffer, struct kernel_param *kp)
{
	return param_get_uint(buffer, kp);
}
#define param_check_portnr(name, p) \
	__param_check(name, p, unsigned int);

module_param_named(min_resvport, xprt_min_resvport, portnr, 0644);
module_param_named(max_resvport, xprt_max_resvport, portnr, 0644);

static int param_set_slot_table_size(const char *val, struct kernel_param *kp)
{
	return param_set_uint_minmax(val, kp,
			RPC_MIN_SLOT_TABLE,
			RPC_MAX_SLOT_TABLE);
}

static int param_get_slot_table_size(char *buffer, struct kernel_param *kp)
{
	return param_get_uint(buffer, kp);
}
#define param_check_slot_table_size(name, p) \
	__param_check(name, p, unsigned int);

module_param_named(tcp_slot_table_entries, xprt_tcp_slot_table_entries,
		   slot_table_size, 0644);
module_param_named(udp_slot_table_entries, xprt_udp_slot_table_entries,
		   slot_table_size, 0644);

