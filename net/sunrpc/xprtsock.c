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
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/capability.h>
#include <linux/pagemap.h>
#include <linux/errno.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/net.h>
#include <linux/mm.h>
#include <linux/un.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/addr.h>
#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/sunrpc/xprtsock.h>
#include <linux/file.h>
#ifdef CONFIG_SUNRPC_BACKCHANNEL
#include <linux/sunrpc/bc_xprt.h>
#endif

#include <net/sock.h>
#include <net/checksum.h>
#include <net/udp.h>
#include <net/tcp.h>

#include <trace/events/sunrpc.h>

#include "sunrpc.h"

static void xs_close(struct rpc_xprt *xprt);

/*
 * xprtsock tunables
 */
static unsigned int xprt_udp_slot_table_entries = RPC_DEF_SLOT_TABLE;
static unsigned int xprt_tcp_slot_table_entries = RPC_MIN_SLOT_TABLE;
static unsigned int xprt_max_tcp_slot_table_entries = RPC_MAX_SLOT_TABLE;

static unsigned int xprt_min_resvport = RPC_DEF_MIN_RESVPORT;
static unsigned int xprt_max_resvport = RPC_DEF_MAX_RESVPORT;

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)

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

static unsigned int min_slot_table_size = RPC_MIN_SLOT_TABLE;
static unsigned int max_slot_table_size = RPC_MAX_SLOT_TABLE;
static unsigned int max_tcp_slot_table_limit = RPC_MAX_SLOT_TABLE_LIMIT;
static unsigned int xprt_min_resvport_limit = RPC_MIN_RESVPORT;
static unsigned int xprt_max_resvport_limit = RPC_MAX_RESVPORT;

static struct ctl_table_header *sunrpc_table_header;

/*
 * FIXME: changing the UDP slot table size should also resize the UDP
 *        socket buffers for existing UDP transports
 */
static struct ctl_table xs_tunables_table[] = {
	{
		.procname	= "udp_slot_table_entries",
		.data		= &xprt_udp_slot_table_entries,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_slot_table_size,
		.extra2		= &max_slot_table_size
	},
	{
		.procname	= "tcp_slot_table_entries",
		.data		= &xprt_tcp_slot_table_entries,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_slot_table_size,
		.extra2		= &max_slot_table_size
	},
	{
		.procname	= "tcp_max_slot_table_entries",
		.data		= &xprt_max_tcp_slot_table_entries,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_slot_table_size,
		.extra2		= &max_tcp_slot_table_limit
	},
	{
		.procname	= "min_resvport",
		.data		= &xprt_min_resvport,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &xprt_min_resvport_limit,
		.extra2		= &xprt_max_resvport
	},
	{
		.procname	= "max_resvport",
		.data		= &xprt_max_resvport,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &xprt_min_resvport,
		.extra2		= &xprt_max_resvport_limit
	},
	{
		.procname	= "tcp_fin_timeout",
		.data		= &xs_tcp_fin_timeout,
		.maxlen		= sizeof(xs_tcp_fin_timeout),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{ },
};

static struct ctl_table sunrpc_table[] = {
	{
		.procname	= "sunrpc",
		.mode		= 0555,
		.child		= xs_tunables_table
	},
	{ },
};

#endif

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

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
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

static inline struct rpc_xprt *xprt_from_sock(struct sock *sk)
{
	return (struct rpc_xprt *) sk->sk_user_data;
}

static inline struct sockaddr *xs_addr(struct rpc_xprt *xprt)
{
	return (struct sockaddr *) &xprt->addr;
}

static inline struct sockaddr_un *xs_addr_un(struct rpc_xprt *xprt)
{
	return (struct sockaddr_un *) &xprt->addr;
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
	struct sockaddr_un *sun;
	char buf[128];

	switch (sap->sa_family) {
	case AF_LOCAL:
		sun = xs_addr_un(xprt);
		strlcpy(buf, sun->sun_path, sizeof(buf));
		xprt->address_strings[RPC_DISPLAY_ADDR] =
						kstrdup(buf, GFP_KERNEL);
		break;
	case AF_INET:
		(void)rpc_ntop(sap, buf, sizeof(buf));
		xprt->address_strings[RPC_DISPLAY_ADDR] =
						kstrdup(buf, GFP_KERNEL);
		sin = xs_addr_in(xprt);
		snprintf(buf, sizeof(buf), "%08x", ntohl(sin->sin_addr.s_addr));
		break;
	case AF_INET6:
		(void)rpc_ntop(sap, buf, sizeof(buf));
		xprt->address_strings[RPC_DISPLAY_ADDR] =
						kstrdup(buf, GFP_KERNEL);
		sin6 = xs_addr_in6(xprt);
		snprintf(buf, sizeof(buf), "%pi6", &sin6->sin6_addr);
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

	snprintf(buf, sizeof(buf), "%u", rpc_get_port(sap));
	xprt->address_strings[RPC_DISPLAY_PORT] = kstrdup(buf, GFP_KERNEL);

	snprintf(buf, sizeof(buf), "%4hx", rpc_get_port(sap));
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

static int xs_send_pagedata(struct socket *sock, struct xdr_buf *xdr, unsigned int base, int more, bool zerocopy, int *sent_p)
{
	ssize_t (*do_sendpage)(struct socket *sock, struct page *page,
			int offset, size_t size, int flags);
	struct page **ppage;
	unsigned int remainder;
	int err;

	remainder = xdr->page_len - base;
	base += xdr->page_base;
	ppage = xdr->pages + (base >> PAGE_SHIFT);
	base &= ~PAGE_MASK;
	do_sendpage = sock->ops->sendpage;
	if (!zerocopy)
		do_sendpage = sock_no_sendpage;
	for(;;) {
		unsigned int len = min_t(unsigned int, PAGE_SIZE - base, remainder);
		int flags = XS_SENDMSG_FLAGS;

		remainder -= len;
		if (more)
			flags |= MSG_MORE;
		if (remainder != 0)
			flags |= MSG_SENDPAGE_NOTLAST | MSG_MORE;
		err = do_sendpage(sock, *ppage, base, len, flags);
		if (remainder == 0 || err != len)
			break;
		*sent_p += err;
		ppage++;
		base = 0;
	}
	if (err > 0) {
		*sent_p += err;
		err = 0;
	}
	return err;
}

/**
 * xs_sendpages - write pages directly to a socket
 * @sock: socket to send on
 * @addr: UDP only -- address of destination
 * @addrlen: UDP only -- length of destination address
 * @xdr: buffer containing this request
 * @base: starting position in the buffer
 * @zerocopy: true if it is safe to use sendpage()
 * @sent_p: return the total number of bytes successfully queued for sending
 *
 */
static int xs_sendpages(struct socket *sock, struct sockaddr *addr, int addrlen, struct xdr_buf *xdr, unsigned int base, bool zerocopy, int *sent_p)
{
	unsigned int remainder = xdr->len - base;
	int err = 0;
	int sent = 0;

	if (unlikely(!sock))
		return -ENOTSOCK;

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
		*sent_p += err;
		base = 0;
	} else
		base -= xdr->head[0].iov_len;

	if (base < xdr->page_len) {
		unsigned int len = xdr->page_len - base;
		remainder -= len;
		err = xs_send_pagedata(sock, xdr, base, remainder != 0, zerocopy, &sent);
		*sent_p += sent;
		if (remainder == 0 || sent != len)
			goto out;
		base = 0;
	} else
		base -= xdr->page_len;

	if (base >= xdr->tail[0].iov_len)
		return 0;
	err = xs_send_kvec(sock, NULL, 0, &xdr->tail[0], base, 0);
out:
	if (err > 0) {
		*sent_p += err;
		err = 0;
	}
	return err;
}

static void xs_nospace_callback(struct rpc_task *task)
{
	struct sock_xprt *transport = container_of(task->tk_rqstp->rq_xprt, struct sock_xprt, xprt);

	transport->inet->sk_write_pending--;
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
	struct sock *sk = transport->inet;
	int ret = -EAGAIN;

	dprintk("RPC: %5u xmit incomplete (%u left of %u)\n",
			task->tk_pid, req->rq_slen - req->rq_bytes_sent,
			req->rq_slen);

	/* Protect against races with write_space */
	spin_lock_bh(&xprt->transport_lock);

	/* Don't race with disconnect */
	if (xprt_connected(xprt)) {
		/* wait for more buffer space */
		sk->sk_write_pending++;
		xprt_wait_for_buffer_space(task, xs_nospace_callback);
	} else
		ret = -ENOTCONN;

	spin_unlock_bh(&xprt->transport_lock);

	/* Race breaker in case memory is freed before above code is called */
	sk->sk_write_space(sk);
	return ret;
}

/*
 * Construct a stream transport record marker in @buf.
 */
static inline void xs_encode_stream_record_marker(struct xdr_buf *buf)
{
	u32 reclen = buf->len - sizeof(rpc_fraghdr);
	rpc_fraghdr *base = buf->head[0].iov_base;
	*base = cpu_to_be32(RPC_LAST_STREAM_FRAGMENT | reclen);
}

/**
 * xs_local_send_request - write an RPC request to an AF_LOCAL socket
 * @task: RPC task that manages the state of an RPC request
 *
 * Return values:
 *        0:	The request has been sent
 *   EAGAIN:	The socket was blocked, please call again later to
 *		complete the request
 * ENOTCONN:	Caller needs to invoke connect logic then call again
 *    other:	Some other error occured, the request was not sent
 */
static int xs_local_send_request(struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_xprt *xprt = req->rq_xprt;
	struct sock_xprt *transport =
				container_of(xprt, struct sock_xprt, xprt);
	struct xdr_buf *xdr = &req->rq_snd_buf;
	int status;
	int sent = 0;

	xs_encode_stream_record_marker(&req->rq_snd_buf);

	xs_pktdump("packet data:",
			req->rq_svec->iov_base, req->rq_svec->iov_len);

	status = xs_sendpages(transport->sock, NULL, 0, xdr, req->rq_bytes_sent,
			      true, &sent);
	dprintk("RPC:       %s(%u) = %d\n",
			__func__, xdr->len - req->rq_bytes_sent, status);

	if (status == -EAGAIN && sock_writeable(transport->inet))
		status = -ENOBUFS;

	if (likely(sent > 0) || status == 0) {
		req->rq_bytes_sent += sent;
		req->rq_xmit_bytes_sent += sent;
		if (likely(req->rq_bytes_sent >= req->rq_slen)) {
			req->rq_bytes_sent = 0;
			return 0;
		}
		status = -EAGAIN;
	}

	switch (status) {
	case -ENOBUFS:
		break;
	case -EAGAIN:
		status = xs_nospace(task);
		break;
	default:
		dprintk("RPC:       sendmsg returned unrecognized error %d\n",
			-status);
	case -EPIPE:
		xs_close(xprt);
		status = -ENOTCONN;
	}

	return status;
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
 *    other:	Some other error occurred, the request was not sent
 */
static int xs_udp_send_request(struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_xprt *xprt = req->rq_xprt;
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	struct xdr_buf *xdr = &req->rq_snd_buf;
	int sent = 0;
	int status;

	xs_pktdump("packet data:",
				req->rq_svec->iov_base,
				req->rq_svec->iov_len);

	if (!xprt_bound(xprt))
		return -ENOTCONN;
	status = xs_sendpages(transport->sock, xs_addr(xprt), xprt->addrlen,
			      xdr, req->rq_bytes_sent, true, &sent);

	dprintk("RPC:       xs_udp_send_request(%u) = %d\n",
			xdr->len - req->rq_bytes_sent, status);

	/* firewall is blocking us, don't return -EAGAIN or we end up looping */
	if (status == -EPERM)
		goto process_status;

	if (status == -EAGAIN && sock_writeable(transport->inet))
		status = -ENOBUFS;

	if (sent > 0 || status == 0) {
		req->rq_xmit_bytes_sent += sent;
		if (sent >= req->rq_slen)
			return 0;
		/* Still some bytes left; set up for a retry later. */
		status = -EAGAIN;
	}

process_status:
	switch (status) {
	case -ENOTSOCK:
		status = -ENOTCONN;
		/* Should we call xs_close() here? */
		break;
	case -EAGAIN:
		status = xs_nospace(task);
		break;
	case -ENETUNREACH:
	case -ENOBUFS:
	case -EPIPE:
	case -ECONNREFUSED:
	case -EPERM:
		/* When the server has died, an ICMP port unreachable message
		 * prompts ECONNREFUSED. */
		break;
	default:
		dprintk("RPC:       sendmsg returned unrecognized error %d\n",
			-status);
	}

	return status;
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
 *    other:	Some other error occurred, the request was not sent
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
	bool zerocopy = true;
	int status;
	int sent;

	xs_encode_stream_record_marker(&req->rq_snd_buf);

	xs_pktdump("packet data:",
				req->rq_svec->iov_base,
				req->rq_svec->iov_len);
	/* Don't use zero copy if this is a resend. If the RPC call
	 * completes while the socket holds a reference to the pages,
	 * then we may end up resending corrupted data.
	 */
	if (task->tk_flags & RPC_TASK_SENT)
		zerocopy = false;

	/* Continue transmitting the packet/record. We must be careful
	 * to cope with writespace callbacks arriving _after_ we have
	 * called sendmsg(). */
	while (1) {
		sent = 0;
		status = xs_sendpages(transport->sock, NULL, 0, xdr,
				      req->rq_bytes_sent, zerocopy, &sent);

		dprintk("RPC:       xs_tcp_send_request(%u) = %d\n",
				xdr->len - req->rq_bytes_sent, status);

		/* If we've sent the entire packet, immediately
		 * reset the count of bytes sent. */
		req->rq_bytes_sent += sent;
		req->rq_xmit_bytes_sent += sent;
		if (likely(req->rq_bytes_sent >= req->rq_slen)) {
			req->rq_bytes_sent = 0;
			return 0;
		}

		if (status < 0)
			break;
		if (sent == 0) {
			status = -EAGAIN;
			break;
		}
	}
	if (status == -EAGAIN && sk_stream_is_writeable(transport->inet))
		status = -ENOBUFS;

	switch (status) {
	case -ENOTSOCK:
		status = -ENOTCONN;
		/* Should we call xs_close() here? */
		break;
	case -EAGAIN:
		status = xs_nospace(task);
		break;
	case -ECONNRESET:
	case -ECONNREFUSED:
	case -ENOTCONN:
	case -EADDRINUSE:
	case -ENOBUFS:
	case -EPIPE:
		break;
	default:
		dprintk("RPC:       sendmsg returned unrecognized error %d\n",
			-status);
	}

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
	if (req == NULL)
		goto out_release;
	if (req->rq_bytes_sent == 0)
		goto out_release;
	if (req->rq_bytes_sent == req->rq_snd_buf.len)
		goto out_release;
	set_bit(XPRT_CLOSE_WAIT, &xprt->state);
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

static void xs_sock_reset_connection_flags(struct rpc_xprt *xprt)
{
	smp_mb__before_atomic();
	clear_bit(XPRT_CLOSE_WAIT, &xprt->state);
	clear_bit(XPRT_CLOSING, &xprt->state);
	smp_mb__after_atomic();
}

static void xs_sock_mark_closed(struct rpc_xprt *xprt)
{
	xs_sock_reset_connection_flags(xprt);
	/* Mark transport as closed and wake up all pending tasks */
	xprt_disconnect_done(xprt);
}

/**
 * xs_error_report - callback to handle TCP socket state errors
 * @sk: socket
 *
 * Note: we don't call sock_error() since there may be a rpc_task
 * using the socket, and so we don't want to clear sk->sk_err.
 */
static void xs_error_report(struct sock *sk)
{
	struct rpc_xprt *xprt;
	int err;

	read_lock_bh(&sk->sk_callback_lock);
	if (!(xprt = xprt_from_sock(sk)))
		goto out;

	err = -sk->sk_err;
	if (err == 0)
		goto out;
	/* Is this a reset event? */
	if (sk->sk_state == TCP_CLOSE)
		xs_sock_mark_closed(xprt);
	dprintk("RPC:       xs_error_report client %p, error=%d...\n",
			xprt, -err);
	trace_rpc_socket_error(xprt, sk->sk_socket, err);
	xprt_wake_pending_tasks(xprt, err);
 out:
	read_unlock_bh(&sk->sk_callback_lock);
}

static void xs_reset_transport(struct sock_xprt *transport)
{
	struct socket *sock = transport->sock;
	struct sock *sk = transport->inet;
	struct rpc_xprt *xprt = &transport->xprt;

	if (sk == NULL)
		return;

	if (atomic_read(&transport->xprt.swapper))
		sk_clear_memalloc(sk);

	kernel_sock_shutdown(sock, SHUT_RDWR);

	mutex_lock(&transport->recv_mutex);
	write_lock_bh(&sk->sk_callback_lock);
	transport->inet = NULL;
	transport->sock = NULL;

	sk->sk_user_data = NULL;

	xs_restore_old_callbacks(transport, sk);
	xprt_clear_connected(xprt);
	write_unlock_bh(&sk->sk_callback_lock);
	xs_sock_reset_connection_flags(xprt);
	mutex_unlock(&transport->recv_mutex);

	trace_rpc_socket_close(xprt, sock);
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

	xprt_disconnect_done(xprt);
}

static void xs_inject_disconnect(struct rpc_xprt *xprt)
{
	dprintk("RPC:       injecting transport disconnect on xprt=%p\n",
		xprt);
	xprt_disconnect_done(xprt);
}

static void xs_xprt_free(struct rpc_xprt *xprt)
{
	xs_free_peer_addresses(xprt);
	xprt_free(xprt);
}

/**
 * xs_destroy - prepare to shutdown a transport
 * @xprt: doomed transport
 *
 */
static void xs_destroy(struct rpc_xprt *xprt)
{
	struct sock_xprt *transport = container_of(xprt,
			struct sock_xprt, xprt);
	dprintk("RPC:       xs_destroy xprt %p\n", xprt);

	cancel_delayed_work_sync(&transport->connect_worker);
	xs_close(xprt);
	cancel_work_sync(&transport->recv_worker);
	xs_xprt_free(xprt);
	module_put(THIS_MODULE);
}

static int xs_local_copy_to_xdr(struct xdr_buf *xdr, struct sk_buff *skb)
{
	struct xdr_skb_reader desc = {
		.skb		= skb,
		.offset		= sizeof(rpc_fraghdr),
		.count		= skb->len - sizeof(rpc_fraghdr),
	};

	if (xdr_partial_copy_from_skb(xdr, 0, &desc, xdr_skb_read_bits) < 0)
		return -1;
	if (desc.count)
		return -1;
	return 0;
}

/**
 * xs_local_data_read_skb
 * @xprt: transport
 * @sk: socket
 * @skb: skbuff
 *
 * Currently this assumes we can read the whole reply in a single gulp.
 */
static void xs_local_data_read_skb(struct rpc_xprt *xprt,
		struct sock *sk,
		struct sk_buff *skb)
{
	struct rpc_task *task;
	struct rpc_rqst *rovr;
	int repsize, copied;
	u32 _xid;
	__be32 *xp;

	repsize = skb->len - sizeof(rpc_fraghdr);
	if (repsize < 4) {
		dprintk("RPC:       impossible RPC reply size %d\n", repsize);
		return;
	}

	/* Copy the XID from the skb... */
	xp = skb_header_pointer(skb, sizeof(rpc_fraghdr), sizeof(_xid), &_xid);
	if (xp == NULL)
		return;

	/* Look up and lock the request corresponding to the given XID */
	spin_lock_bh(&xprt->transport_lock);
	rovr = xprt_lookup_rqst(xprt, *xp);
	if (!rovr)
		goto out_unlock;
	task = rovr->rq_task;

	copied = rovr->rq_private_buf.buflen;
	if (copied > repsize)
		copied = repsize;

	if (xs_local_copy_to_xdr(&rovr->rq_private_buf, skb)) {
		dprintk("RPC:       sk_buff copy failed\n");
		goto out_unlock;
	}

	xprt_complete_rqst(task, copied);

 out_unlock:
	spin_unlock_bh(&xprt->transport_lock);
}

static void xs_local_data_receive(struct sock_xprt *transport)
{
	struct sk_buff *skb;
	struct sock *sk;
	int err;

	mutex_lock(&transport->recv_mutex);
	sk = transport->inet;
	if (sk == NULL)
		goto out;
	for (;;) {
		skb = skb_recv_datagram(sk, 0, 1, &err);
		if (skb == NULL)
			break;
		xs_local_data_read_skb(&transport->xprt, sk, skb);
		skb_free_datagram(sk, skb);
	}
out:
	mutex_unlock(&transport->recv_mutex);
}

static void xs_local_data_receive_workfn(struct work_struct *work)
{
	struct sock_xprt *transport =
		container_of(work, struct sock_xprt, recv_worker);
	xs_local_data_receive(transport);
}

/**
 * xs_udp_data_read_skb - receive callback for UDP sockets
 * @xprt: transport
 * @sk: socket
 * @skb: skbuff
 *
 */
static void xs_udp_data_read_skb(struct rpc_xprt *xprt,
		struct sock *sk,
		struct sk_buff *skb)
{
	struct rpc_task *task;
	struct rpc_rqst *rovr;
	int repsize, copied;
	u32 _xid;
	__be32 *xp;

	repsize = skb->len;
	if (repsize < 4) {
		dprintk("RPC:       impossible RPC reply size %d!\n", repsize);
		return;
	}

	/* Copy the XID from the skb... */
	xp = skb_header_pointer(skb, 0, sizeof(_xid), &_xid);
	if (xp == NULL)
		return;

	/* Look up and lock the request corresponding to the given XID */
	spin_lock_bh(&xprt->transport_lock);
	rovr = xprt_lookup_rqst(xprt, *xp);
	if (!rovr)
		goto out_unlock;
	task = rovr->rq_task;

	if ((copied = rovr->rq_private_buf.buflen) > repsize)
		copied = repsize;

	/* Suck it into the iovec, verify checksum if not done by hw. */
	if (csum_partial_copy_to_xdr(&rovr->rq_private_buf, skb)) {
		__UDPX_INC_STATS(sk, UDP_MIB_INERRORS);
		goto out_unlock;
	}

	__UDPX_INC_STATS(sk, UDP_MIB_INDATAGRAMS);

	xprt_adjust_cwnd(xprt, task, copied);
	xprt_complete_rqst(task, copied);

 out_unlock:
	spin_unlock_bh(&xprt->transport_lock);
}

static void xs_udp_data_receive(struct sock_xprt *transport)
{
	struct sk_buff *skb;
	struct sock *sk;
	int err;

	mutex_lock(&transport->recv_mutex);
	sk = transport->inet;
	if (sk == NULL)
		goto out;
	for (;;) {
		skb = skb_recv_datagram(sk, 0, 1, &err);
		if (skb == NULL)
			break;
		xs_udp_data_read_skb(&transport->xprt, sk, skb);
		skb_free_datagram(sk, skb);
	}
out:
	mutex_unlock(&transport->recv_mutex);
}

static void xs_udp_data_receive_workfn(struct work_struct *work)
{
	struct sock_xprt *transport =
		container_of(work, struct sock_xprt, recv_worker);
	xs_udp_data_receive(transport);
}

/**
 * xs_data_ready - "data ready" callback for UDP sockets
 * @sk: socket with data to read
 *
 */
static void xs_data_ready(struct sock *sk)
{
	struct rpc_xprt *xprt;

	read_lock_bh(&sk->sk_callback_lock);
	dprintk("RPC:       xs_data_ready...\n");
	xprt = xprt_from_sock(sk);
	if (xprt != NULL) {
		struct sock_xprt *transport = container_of(xprt,
				struct sock_xprt, xprt);
		queue_work(rpciod_workqueue, &transport->recv_worker);
	}
	read_unlock_bh(&sk->sk_callback_lock);
}

/*
 * Helper function to force a TCP close if the server is sending
 * junk and/or it has put us in CLOSE_WAIT
 */
static void xs_tcp_force_close(struct rpc_xprt *xprt)
{
	xprt_force_disconnect(xprt);
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
		xs_tcp_force_close(xprt);
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
		xs_tcp_force_close(&transport->xprt);
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
	spin_lock_bh(&xprt->transport_lock);
	req = xprt_lookup_rqst(xprt, transport->tcp_xid);
	if (!req) {
		dprintk("RPC:       XID %08x request not found!\n",
				ntohl(transport->tcp_xid));
		spin_unlock_bh(&xprt->transport_lock);
		return -1;
	}

	xs_tcp_read_common(xprt, desc, req);

	if (!(transport->tcp_flags & TCP_RCV_COPY_DATA))
		xprt_complete_rqst(req->rq_task, transport->tcp_copied);

	spin_unlock_bh(&xprt->transport_lock);
	return 0;
}

#if defined(CONFIG_SUNRPC_BACKCHANNEL)
/*
 * Obtains an rpc_rqst previously allocated and invokes the common
 * tcp read code to read the data.  The result is placed in the callback
 * queue.
 * If we're unable to obtain the rpc_rqst we schedule the closing of the
 * connection and return -1.
 */
static int xs_tcp_read_callback(struct rpc_xprt *xprt,
				       struct xdr_skb_reader *desc)
{
	struct sock_xprt *transport =
				container_of(xprt, struct sock_xprt, xprt);
	struct rpc_rqst *req;

	/* Look up and lock the request corresponding to the given XID */
	spin_lock_bh(&xprt->transport_lock);
	req = xprt_lookup_bc_request(xprt, transport->tcp_xid);
	if (req == NULL) {
		spin_unlock_bh(&xprt->transport_lock);
		printk(KERN_WARNING "Callback slot table overflowed\n");
		xprt_force_disconnect(xprt);
		return -1;
	}

	dprintk("RPC:       read callback  XID %08x\n", ntohl(req->rq_xid));
	xs_tcp_read_common(xprt, desc, req);

	if (!(transport->tcp_flags & TCP_RCV_COPY_DATA))
		xprt_complete_bc_request(req, transport->tcp_copied);
	spin_unlock_bh(&xprt->transport_lock);

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

static int xs_tcp_bc_up(struct svc_serv *serv, struct net *net)
{
	int ret;

	ret = svc_create_xprt(serv, "tcp-bc", net, PF_INET, 0,
			      SVC_SOCK_ANONYMOUS);
	if (ret < 0)
		return ret;
	return 0;
}

static size_t xs_tcp_bc_maxpayload(struct rpc_xprt *xprt)
{
	return PAGE_SIZE;
}
#else
static inline int _xs_tcp_read_data(struct rpc_xprt *xprt,
					struct xdr_skb_reader *desc)
{
	return xs_tcp_read_reply(xprt, desc);
}
#endif /* CONFIG_SUNRPC_BACKCHANNEL */

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
		trace_xs_tcp_data_recv(transport);
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
	trace_xs_tcp_data_recv(transport);
	dprintk("RPC:       xs_tcp_data_recv done\n");
	return len - desc.count;
}

static void xs_tcp_data_receive(struct sock_xprt *transport)
{
	struct rpc_xprt *xprt = &transport->xprt;
	struct sock *sk;
	read_descriptor_t rd_desc = {
		.count = 2*1024*1024,
		.arg.data = xprt,
	};
	unsigned long total = 0;
	int read = 0;

	mutex_lock(&transport->recv_mutex);
	sk = transport->inet;
	if (sk == NULL)
		goto out;

	/* We use rd_desc to pass struct xprt to xs_tcp_data_recv */
	for (;;) {
		lock_sock(sk);
		read = tcp_read_sock(sk, &rd_desc, xs_tcp_data_recv);
		release_sock(sk);
		if (read <= 0)
			break;
		total += read;
		rd_desc.count = 65536;
	}
out:
	mutex_unlock(&transport->recv_mutex);
	trace_xs_tcp_data_ready(xprt, read, total);
}

static void xs_tcp_data_receive_workfn(struct work_struct *work)
{
	struct sock_xprt *transport =
		container_of(work, struct sock_xprt, recv_worker);
	xs_tcp_data_receive(transport);
}

/**
 * xs_tcp_data_ready - "data ready" callback for TCP sockets
 * @sk: socket with data to read
 *
 */
static void xs_tcp_data_ready(struct sock *sk)
{
	struct sock_xprt *transport;
	struct rpc_xprt *xprt;

	dprintk("RPC:       xs_tcp_data_ready...\n");

	read_lock_bh(&sk->sk_callback_lock);
	if (!(xprt = xprt_from_sock(sk)))
		goto out;
	transport = container_of(xprt, struct sock_xprt, xprt);

	/* Any data means we had a useful conversation, so
	 * the we don't need to delay the next reconnect
	 */
	if (xprt->reestablish_timeout)
		xprt->reestablish_timeout = 0;
	queue_work(rpciod_workqueue, &transport->recv_worker);

out:
	read_unlock_bh(&sk->sk_callback_lock);
}

/**
 * xs_tcp_state_change - callback to handle TCP socket state changes
 * @sk: socket whose state has changed
 *
 */
static void xs_tcp_state_change(struct sock *sk)
{
	struct rpc_xprt *xprt;
	struct sock_xprt *transport;

	read_lock_bh(&sk->sk_callback_lock);
	if (!(xprt = xprt_from_sock(sk)))
		goto out;
	dprintk("RPC:       xs_tcp_state_change client %p...\n", xprt);
	dprintk("RPC:       state %x conn %d dead %d zapped %d sk_shutdown %d\n",
			sk->sk_state, xprt_connected(xprt),
			sock_flag(sk, SOCK_DEAD),
			sock_flag(sk, SOCK_ZAPPED),
			sk->sk_shutdown);

	transport = container_of(xprt, struct sock_xprt, xprt);
	trace_rpc_socket_state_change(xprt, sk->sk_socket);
	switch (sk->sk_state) {
	case TCP_ESTABLISHED:
		spin_lock(&xprt->transport_lock);
		if (!xprt_test_and_set_connected(xprt)) {

			/* Reset TCP record info */
			transport->tcp_offset = 0;
			transport->tcp_reclen = 0;
			transport->tcp_copied = 0;
			transport->tcp_flags =
				TCP_RCV_COPY_FRAGHDR | TCP_RCV_COPY_XID;
			xprt->connect_cookie++;
			clear_bit(XPRT_SOCK_CONNECTING, &transport->sock_state);
			xprt_clear_connecting(xprt);

			xprt_wake_pending_tasks(xprt, -EAGAIN);
		}
		spin_unlock(&xprt->transport_lock);
		break;
	case TCP_FIN_WAIT1:
		/* The client initiated a shutdown of the socket */
		xprt->connect_cookie++;
		xprt->reestablish_timeout = 0;
		set_bit(XPRT_CLOSING, &xprt->state);
		smp_mb__before_atomic();
		clear_bit(XPRT_CONNECTED, &xprt->state);
		clear_bit(XPRT_CLOSE_WAIT, &xprt->state);
		smp_mb__after_atomic();
		break;
	case TCP_CLOSE_WAIT:
		/* The server initiated a shutdown of the socket */
		xprt->connect_cookie++;
		clear_bit(XPRT_CONNECTED, &xprt->state);
		xs_tcp_force_close(xprt);
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
		smp_mb__before_atomic();
		clear_bit(XPRT_CONNECTED, &xprt->state);
		smp_mb__after_atomic();
		break;
	case TCP_CLOSE:
		if (test_and_clear_bit(XPRT_SOCK_CONNECTING,
					&transport->sock_state))
			xprt_clear_connecting(xprt);
		xs_sock_mark_closed(xprt);
	}
 out:
	read_unlock_bh(&sk->sk_callback_lock);
}

static void xs_write_space(struct sock *sk)
{
	struct socket_wq *wq;
	struct rpc_xprt *xprt;

	if (!sk->sk_socket)
		return;
	clear_bit(SOCK_NOSPACE, &sk->sk_socket->flags);

	if (unlikely(!(xprt = xprt_from_sock(sk))))
		return;
	rcu_read_lock();
	wq = rcu_dereference(sk->sk_wq);
	if (!wq || test_and_clear_bit(SOCKWQ_ASYNC_NOSPACE, &wq->flags) == 0)
		goto out;

	xprt_write_space(xprt);
out:
	rcu_read_unlock();
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
	read_lock_bh(&sk->sk_callback_lock);

	/* from net/core/sock.c:sock_def_write_space */
	if (sock_writeable(sk))
		xs_write_space(sk);

	read_unlock_bh(&sk->sk_callback_lock);
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
	read_lock_bh(&sk->sk_callback_lock);

	/* from net/core/stream.c:sk_stream_write_space */
	if (sk_stream_is_writeable(sk))
		xs_write_space(sk);

	read_unlock_bh(&sk->sk_callback_lock);
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
static void xs_udp_timer(struct rpc_xprt *xprt, struct rpc_task *task)
{
	xprt_adjust_cwnd(xprt, task, -ETIMEDOUT);
}

static unsigned short xs_get_random_port(void)
{
	unsigned short range = xprt_max_resvport - xprt_min_resvport + 1;
	unsigned short rand = (unsigned short) prandom_u32() % range;
	return rand + xprt_min_resvport;
}

/**
 * xs_set_reuseaddr_port - set the socket's port and address reuse options
 * @sock: socket
 *
 * Note that this function has to be called on all sockets that share the
 * same port, and it must be called before binding.
 */
static void xs_sock_set_reuseport(struct socket *sock)
{
	int opt = 1;

	kernel_setsockopt(sock, SOL_SOCKET, SO_REUSEPORT,
			(char *)&opt, sizeof(opt));
}

static unsigned short xs_sock_getport(struct socket *sock)
{
	struct sockaddr_storage buf;
	int buflen;
	unsigned short port = 0;

	if (kernel_getsockname(sock, (struct sockaddr *)&buf, &buflen) < 0)
		goto out;
	switch (buf.ss_family) {
	case AF_INET6:
		port = ntohs(((struct sockaddr_in6 *)&buf)->sin6_port);
		break;
	case AF_INET:
		port = ntohs(((struct sockaddr_in *)&buf)->sin_port);
	}
out:
	return port;
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

static void xs_set_srcport(struct sock_xprt *transport, struct socket *sock)
{
	if (transport->srcport == 0)
		transport->srcport = xs_sock_getport(sock);
}

static unsigned short xs_get_srcport(struct sock_xprt *transport)
{
	unsigned short port = transport->srcport;

	if (port == 0 && transport->xprt.resvport)
		port = xs_get_random_port();
	return port;
}

static unsigned short xs_next_srcport(struct sock_xprt *transport, unsigned short port)
{
	if (transport->srcport != 0)
		transport->srcport = 0;
	if (!transport->xprt.resvport)
		return 0;
	if (port <= xprt_min_resvport || port > xprt_max_resvport)
		return xprt_max_resvport;
	return --port;
}
static int xs_bind(struct sock_xprt *transport, struct socket *sock)
{
	struct sockaddr_storage myaddr;
	int err, nloop = 0;
	unsigned short port = xs_get_srcport(transport);
	unsigned short last;

	/*
	 * If we are asking for any ephemeral port (i.e. port == 0 &&
	 * transport->xprt.resvport == 0), don't bind.  Let the local
	 * port selection happen implicitly when the socket is used
	 * (for example at connect time).
	 *
	 * This ensures that we can continue to establish TCP
	 * connections even when all local ephemeral ports are already
	 * a part of some TCP connection.  This makes no difference
	 * for UDP sockets, but also doens't harm them.
	 *
	 * If we're asking for any reserved port (i.e. port == 0 &&
	 * transport->xprt.resvport == 1) xs_get_srcport above will
	 * ensure that port is non-zero and we will bind as needed.
	 */
	if (port == 0)
		return 0;

	memcpy(&myaddr, &transport->srcaddr, transport->xprt.addrlen);
	do {
		rpc_set_port((struct sockaddr *)&myaddr, port);
		err = kernel_bind(sock, (struct sockaddr *)&myaddr,
				transport->xprt.addrlen);
		if (err == 0) {
			transport->srcport = port;
			break;
		}
		last = port;
		port = xs_next_srcport(transport, port);
		if (port > last)
			nloop++;
	} while (err == -EADDRINUSE && nloop != 2);

	if (myaddr.ss_family == AF_INET)
		dprintk("RPC:       %s %pI4:%u: %s (%d)\n", __func__,
				&((struct sockaddr_in *)&myaddr)->sin_addr,
				port, err ? "failed" : "ok", err);
	else
		dprintk("RPC:       %s %pI6:%u: %s (%d)\n", __func__,
				&((struct sockaddr_in6 *)&myaddr)->sin6_addr,
				port, err ? "failed" : "ok", err);
	return err;
}

/*
 * We don't support autobind on AF_LOCAL sockets
 */
static void xs_local_rpcbind(struct rpc_task *task)
{
	xprt_set_bound(task->tk_xprt);
}

static void xs_local_set_port(struct rpc_xprt *xprt, unsigned short port)
{
}

#ifdef CONFIG_DEBUG_LOCK_ALLOC
static struct lock_class_key xs_key[2];
static struct lock_class_key xs_slock_key[2];

static inline void xs_reclassify_socketu(struct socket *sock)
{
	struct sock *sk = sock->sk;

	sock_lock_init_class_and_name(sk, "slock-AF_LOCAL-RPC",
		&xs_slock_key[1], "sk_lock-AF_LOCAL-RPC", &xs_key[1]);
}

static inline void xs_reclassify_socket4(struct socket *sock)
{
	struct sock *sk = sock->sk;

	sock_lock_init_class_and_name(sk, "slock-AF_INET-RPC",
		&xs_slock_key[0], "sk_lock-AF_INET-RPC", &xs_key[0]);
}

static inline void xs_reclassify_socket6(struct socket *sock)
{
	struct sock *sk = sock->sk;

	sock_lock_init_class_and_name(sk, "slock-AF_INET6-RPC",
		&xs_slock_key[1], "sk_lock-AF_INET6-RPC", &xs_key[1]);
}

static inline void xs_reclassify_socket(int family, struct socket *sock)
{
	if (WARN_ON_ONCE(!sock_allow_reclassification(sock->sk)))
		return;

	switch (family) {
	case AF_LOCAL:
		xs_reclassify_socketu(sock);
		break;
	case AF_INET:
		xs_reclassify_socket4(sock);
		break;
	case AF_INET6:
		xs_reclassify_socket6(sock);
		break;
	}
}
#else
static inline void xs_reclassify_socket(int family, struct socket *sock)
{
}
#endif

static void xs_dummy_setup_socket(struct work_struct *work)
{
}

static struct socket *xs_create_sock(struct rpc_xprt *xprt,
		struct sock_xprt *transport, int family, int type,
		int protocol, bool reuseport)
{
	struct socket *sock;
	int err;

	err = __sock_create(xprt->xprt_net, family, type, protocol, &sock, 1);
	if (err < 0) {
		dprintk("RPC:       can't create %d transport socket (%d).\n",
				protocol, -err);
		goto out;
	}
	xs_reclassify_socket(family, sock);

	if (reuseport)
		xs_sock_set_reuseport(sock);

	err = xs_bind(transport, sock);
	if (err) {
		sock_release(sock);
		goto out;
	}

	return sock;
out:
	return ERR_PTR(err);
}

static int xs_local_finish_connecting(struct rpc_xprt *xprt,
				      struct socket *sock)
{
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt,
									xprt);

	if (!transport->inet) {
		struct sock *sk = sock->sk;

		write_lock_bh(&sk->sk_callback_lock);

		xs_save_old_callbacks(transport, sk);

		sk->sk_user_data = xprt;
		sk->sk_data_ready = xs_data_ready;
		sk->sk_write_space = xs_udp_write_space;
		sock_set_flag(sk, SOCK_FASYNC);
		sk->sk_error_report = xs_error_report;
		sk->sk_allocation = GFP_NOIO;

		xprt_clear_connected(xprt);

		/* Reset to new socket */
		transport->sock = sock;
		transport->inet = sk;

		write_unlock_bh(&sk->sk_callback_lock);
	}

	/* Tell the socket layer to start connecting... */
	xprt->stat.connect_count++;
	xprt->stat.connect_start = jiffies;
	return kernel_connect(sock, xs_addr(xprt), xprt->addrlen, 0);
}

/**
 * xs_local_setup_socket - create AF_LOCAL socket, connect to a local endpoint
 * @transport: socket transport to connect
 */
static int xs_local_setup_socket(struct sock_xprt *transport)
{
	struct rpc_xprt *xprt = &transport->xprt;
	struct socket *sock;
	int status = -EIO;

	status = __sock_create(xprt->xprt_net, AF_LOCAL,
					SOCK_STREAM, 0, &sock, 1);
	if (status < 0) {
		dprintk("RPC:       can't create AF_LOCAL "
			"transport socket (%d).\n", -status);
		goto out;
	}
	xs_reclassify_socket(AF_LOCAL, sock);

	dprintk("RPC:       worker connecting xprt %p via AF_LOCAL to %s\n",
			xprt, xprt->address_strings[RPC_DISPLAY_ADDR]);

	status = xs_local_finish_connecting(xprt, sock);
	trace_rpc_socket_connect(xprt, sock, status);
	switch (status) {
	case 0:
		dprintk("RPC:       xprt %p connected to %s\n",
				xprt, xprt->address_strings[RPC_DISPLAY_ADDR]);
		xprt_set_connected(xprt);
	case -ENOBUFS:
		break;
	case -ENOENT:
		dprintk("RPC:       xprt %p: socket %s does not exist\n",
				xprt, xprt->address_strings[RPC_DISPLAY_ADDR]);
		break;
	case -ECONNREFUSED:
		dprintk("RPC:       xprt %p: connection refused for %s\n",
				xprt, xprt->address_strings[RPC_DISPLAY_ADDR]);
		break;
	default:
		printk(KERN_ERR "%s: unhandled error (%d) connecting to %s\n",
				__func__, -status,
				xprt->address_strings[RPC_DISPLAY_ADDR]);
	}

out:
	xprt_clear_connecting(xprt);
	xprt_wake_pending_tasks(xprt, status);
	return status;
}

static void xs_local_connect(struct rpc_xprt *xprt, struct rpc_task *task)
{
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	int ret;

	 if (RPC_IS_ASYNC(task)) {
		/*
		 * We want the AF_LOCAL connect to be resolved in the
		 * filesystem namespace of the process making the rpc
		 * call.  Thus we connect synchronously.
		 *
		 * If we want to support asynchronous AF_LOCAL calls,
		 * we'll need to figure out how to pass a namespace to
		 * connect.
		 */
		rpc_exit(task, -ENOTCONN);
		return;
	}
	ret = xs_local_setup_socket(transport);
	if (ret && !RPC_IS_SOFTCONN(task))
		msleep_interruptible(15000);
}

#if IS_ENABLED(CONFIG_SUNRPC_SWAP)
/*
 * Note that this should be called with XPRT_LOCKED held (or when we otherwise
 * know that we have exclusive access to the socket), to guard against
 * races with xs_reset_transport.
 */
static void xs_set_memalloc(struct rpc_xprt *xprt)
{
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt,
			xprt);

	/*
	 * If there's no sock, then we have nothing to set. The
	 * reconnecting process will get it for us.
	 */
	if (!transport->inet)
		return;
	if (atomic_read(&xprt->swapper))
		sk_set_memalloc(transport->inet);
}

/**
 * xs_enable_swap - Tag this transport as being used for swap.
 * @xprt: transport to tag
 *
 * Take a reference to this transport on behalf of the rpc_clnt, and
 * optionally mark it for swapping if it wasn't already.
 */
static int
xs_enable_swap(struct rpc_xprt *xprt)
{
	struct sock_xprt *xs = container_of(xprt, struct sock_xprt, xprt);

	if (atomic_inc_return(&xprt->swapper) != 1)
		return 0;
	if (wait_on_bit_lock(&xprt->state, XPRT_LOCKED, TASK_KILLABLE))
		return -ERESTARTSYS;
	if (xs->inet)
		sk_set_memalloc(xs->inet);
	xprt_release_xprt(xprt, NULL);
	return 0;
}

/**
 * xs_disable_swap - Untag this transport as being used for swap.
 * @xprt: transport to tag
 *
 * Drop a "swapper" reference to this xprt on behalf of the rpc_clnt. If the
 * swapper refcount goes to 0, untag the socket as a memalloc socket.
 */
static void
xs_disable_swap(struct rpc_xprt *xprt)
{
	struct sock_xprt *xs = container_of(xprt, struct sock_xprt, xprt);

	if (!atomic_dec_and_test(&xprt->swapper))
		return;
	if (wait_on_bit_lock(&xprt->state, XPRT_LOCKED, TASK_KILLABLE))
		return;
	if (xs->inet)
		sk_clear_memalloc(xs->inet);
	xprt_release_xprt(xprt, NULL);
}
#else
static void xs_set_memalloc(struct rpc_xprt *xprt)
{
}

static int
xs_enable_swap(struct rpc_xprt *xprt)
{
	return -EINVAL;
}

static void
xs_disable_swap(struct rpc_xprt *xprt)
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
		sk->sk_data_ready = xs_data_ready;
		sk->sk_write_space = xs_udp_write_space;
		sock_set_flag(sk, SOCK_FASYNC);
		sk->sk_allocation = GFP_NOIO;

		xprt_set_connected(xprt);

		/* Reset to new socket */
		transport->sock = sock;
		transport->inet = sk;

		xs_set_memalloc(xprt);

		write_unlock_bh(&sk->sk_callback_lock);
	}
	xs_udp_do_set_buffer_size(xprt);
}

static void xs_udp_setup_socket(struct work_struct *work)
{
	struct sock_xprt *transport =
		container_of(work, struct sock_xprt, connect_worker.work);
	struct rpc_xprt *xprt = &transport->xprt;
	struct socket *sock = transport->sock;
	int status = -EIO;

	sock = xs_create_sock(xprt, transport,
			xs_addr(xprt)->sa_family, SOCK_DGRAM,
			IPPROTO_UDP, false);
	if (IS_ERR(sock))
		goto out;

	dprintk("RPC:       worker connecting xprt %p via %s to "
				"%s (port %s)\n", xprt,
			xprt->address_strings[RPC_DISPLAY_PROTO],
			xprt->address_strings[RPC_DISPLAY_ADDR],
			xprt->address_strings[RPC_DISPLAY_PORT]);

	xs_udp_finish_connecting(xprt, sock);
	trace_rpc_socket_connect(xprt, sock, 0);
	status = 0;
out:
	xprt_unlock_connect(xprt, transport);
	xprt_clear_connecting(xprt);
	xprt_wake_pending_tasks(xprt, status);
}

/**
 * xs_tcp_shutdown - gracefully shut down a TCP socket
 * @xprt: transport
 *
 * Initiates a graceful shutdown of the TCP socket by calling the
 * equivalent of shutdown(SHUT_RDWR);
 */
static void xs_tcp_shutdown(struct rpc_xprt *xprt)
{
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	struct socket *sock = transport->sock;

	if (sock == NULL)
		return;
	if (xprt_connected(xprt)) {
		kernel_sock_shutdown(sock, SHUT_RDWR);
		trace_rpc_socket_shutdown(xprt, sock);
	} else
		xs_reset_transport(transport);
}

static int xs_tcp_finish_connecting(struct rpc_xprt *xprt, struct socket *sock)
{
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	int ret = -ENOTCONN;

	if (!transport->inet) {
		struct sock *sk = sock->sk;
		unsigned int keepidle = xprt->timeout->to_initval / HZ;
		unsigned int keepcnt = xprt->timeout->to_retries + 1;
		unsigned int opt_on = 1;
		unsigned int timeo;

		/* TCP Keepalive options */
		kernel_setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE,
				(char *)&opt_on, sizeof(opt_on));
		kernel_setsockopt(sock, SOL_TCP, TCP_KEEPIDLE,
				(char *)&keepidle, sizeof(keepidle));
		kernel_setsockopt(sock, SOL_TCP, TCP_KEEPINTVL,
				(char *)&keepidle, sizeof(keepidle));
		kernel_setsockopt(sock, SOL_TCP, TCP_KEEPCNT,
				(char *)&keepcnt, sizeof(keepcnt));

		/* TCP user timeout (see RFC5482) */
		timeo = jiffies_to_msecs(xprt->timeout->to_initval) *
			(xprt->timeout->to_retries + 1);
		kernel_setsockopt(sock, SOL_TCP, TCP_USER_TIMEOUT,
				(char *)&timeo, sizeof(timeo));

		write_lock_bh(&sk->sk_callback_lock);

		xs_save_old_callbacks(transport, sk);

		sk->sk_user_data = xprt;
		sk->sk_data_ready = xs_tcp_data_ready;
		sk->sk_state_change = xs_tcp_state_change;
		sk->sk_write_space = xs_tcp_write_space;
		sock_set_flag(sk, SOCK_FASYNC);
		sk->sk_error_report = xs_error_report;
		sk->sk_allocation = GFP_NOIO;

		/* socket options */
		sock_reset_flag(sk, SOCK_LINGER);
		tcp_sk(sk)->nonagle |= TCP_NAGLE_OFF;

		xprt_clear_connected(xprt);

		/* Reset to new socket */
		transport->sock = sock;
		transport->inet = sk;

		write_unlock_bh(&sk->sk_callback_lock);
	}

	if (!xprt_bound(xprt))
		goto out;

	xs_set_memalloc(xprt);

	/* Tell the socket layer to start connecting... */
	xprt->stat.connect_count++;
	xprt->stat.connect_start = jiffies;
	set_bit(XPRT_SOCK_CONNECTING, &transport->sock_state);
	ret = kernel_connect(sock, xs_addr(xprt), xprt->addrlen, O_NONBLOCK);
	switch (ret) {
	case 0:
		xs_set_srcport(transport, sock);
	case -EINPROGRESS:
		/* SYN_SENT! */
		if (xprt->reestablish_timeout < XS_TCP_INIT_REEST_TO)
			xprt->reestablish_timeout = XS_TCP_INIT_REEST_TO;
	}
out:
	return ret;
}

/**
 * xs_tcp_setup_socket - create a TCP socket and connect to a remote endpoint
 *
 * Invoked by a work queue tasklet.
 */
static void xs_tcp_setup_socket(struct work_struct *work)
{
	struct sock_xprt *transport =
		container_of(work, struct sock_xprt, connect_worker.work);
	struct socket *sock = transport->sock;
	struct rpc_xprt *xprt = &transport->xprt;
	int status = -EIO;

	if (!sock) {
		sock = xs_create_sock(xprt, transport,
				xs_addr(xprt)->sa_family, SOCK_STREAM,
				IPPROTO_TCP, true);
		if (IS_ERR(sock)) {
			status = PTR_ERR(sock);
			goto out;
		}
	}

	dprintk("RPC:       worker connecting xprt %p via %s to "
				"%s (port %s)\n", xprt,
			xprt->address_strings[RPC_DISPLAY_PROTO],
			xprt->address_strings[RPC_DISPLAY_ADDR],
			xprt->address_strings[RPC_DISPLAY_PORT]);

	status = xs_tcp_finish_connecting(xprt, sock);
	trace_rpc_socket_connect(xprt, sock, status);
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
		xs_tcp_force_close(xprt);
		break;
	case 0:
	case -EINPROGRESS:
	case -EALREADY:
		xprt_unlock_connect(xprt, transport);
		return;
	case -EINVAL:
		/* Happens, for instance, if the user specified a link
		 * local IPv6 address without a scope-id.
		 */
	case -ECONNREFUSED:
	case -ECONNRESET:
	case -ENETUNREACH:
	case -EADDRINUSE:
	case -ENOBUFS:
		/* retry with existing socket, after a delay */
		xs_tcp_force_close(xprt);
		goto out;
	}
	status = -EAGAIN;
out:
	xprt_unlock_connect(xprt, transport);
	xprt_clear_connecting(xprt);
	xprt_wake_pending_tasks(xprt, status);
}

/**
 * xs_connect - connect a socket to a remote endpoint
 * @xprt: pointer to transport structure
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
static void xs_connect(struct rpc_xprt *xprt, struct rpc_task *task)
{
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);

	WARN_ON_ONCE(!xprt_lock_connect(xprt, task, transport));

	if (transport->sock != NULL) {
		dprintk("RPC:       xs_connect delayed xprt %p for %lu "
				"seconds\n",
				xprt, xprt->reestablish_timeout / HZ);

		/* Start by resetting any existing state */
		xs_reset_transport(transport);

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

/**
 * xs_local_print_stats - display AF_LOCAL socket-specifc stats
 * @xprt: rpc_xprt struct containing statistics
 * @seq: output file
 *
 */
static void xs_local_print_stats(struct rpc_xprt *xprt, struct seq_file *seq)
{
	long idle_time = 0;

	if (xprt_connected(xprt))
		idle_time = (long)(jiffies - xprt->last_used) / HZ;

	seq_printf(seq, "\txprt:\tlocal %lu %lu %lu %ld %lu %lu %lu "
			"%llu %llu %lu %llu %llu\n",
			xprt->stat.bind_count,
			xprt->stat.connect_count,
			xprt->stat.connect_time,
			idle_time,
			xprt->stat.sends,
			xprt->stat.recvs,
			xprt->stat.bad_xids,
			xprt->stat.req_u,
			xprt->stat.bklog_u,
			xprt->stat.max_slots,
			xprt->stat.sending_u,
			xprt->stat.pending_u);
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

	seq_printf(seq, "\txprt:\tudp %u %lu %lu %lu %lu %llu %llu "
			"%lu %llu %llu\n",
			transport->srcport,
			xprt->stat.bind_count,
			xprt->stat.sends,
			xprt->stat.recvs,
			xprt->stat.bad_xids,
			xprt->stat.req_u,
			xprt->stat.bklog_u,
			xprt->stat.max_slots,
			xprt->stat.sending_u,
			xprt->stat.pending_u);
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

	seq_printf(seq, "\txprt:\ttcp %u %lu %lu %lu %ld %lu %lu %lu "
			"%llu %llu %lu %llu %llu\n",
			transport->srcport,
			xprt->stat.bind_count,
			xprt->stat.connect_count,
			xprt->stat.connect_time,
			idle_time,
			xprt->stat.sends,
			xprt->stat.recvs,
			xprt->stat.bad_xids,
			xprt->stat.req_u,
			xprt->stat.bklog_u,
			xprt->stat.max_slots,
			xprt->stat.sending_u,
			xprt->stat.pending_u);
}

/*
 * Allocate a bunch of pages for a scratch buffer for the rpc code. The reason
 * we allocate pages instead doing a kmalloc like rpc_malloc is because we want
 * to use the server side send routines.
 */
static void *bc_malloc(struct rpc_task *task, size_t size)
{
	struct page *page;
	struct rpc_buffer *buf;

	WARN_ON_ONCE(size > PAGE_SIZE - sizeof(struct rpc_buffer));
	if (size > PAGE_SIZE - sizeof(struct rpc_buffer))
		return NULL;

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
static void bc_free(void *buffer)
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

	xs_encode_stream_record_marker(xbufp);

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
	int len;

	dprintk("sending request with xid: %08x\n", ntohl(req->rq_xid));
	/*
	 * Get the server socket associated with this callback xprt
	 */
	xprt = req->rq_xprt->bc_xprt;

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
}

/*
 * The xprt destroy routine. Again, because this connection is client
 * initiated, we do nothing
 */

static void bc_destroy(struct rpc_xprt *xprt)
{
	dprintk("RPC:       bc_destroy xprt %p\n", xprt);

	xs_xprt_free(xprt);
	module_put(THIS_MODULE);
}

static struct rpc_xprt_ops xs_local_ops = {
	.reserve_xprt		= xprt_reserve_xprt,
	.release_xprt		= xs_tcp_release_xprt,
	.alloc_slot		= xprt_alloc_slot,
	.rpcbind		= xs_local_rpcbind,
	.set_port		= xs_local_set_port,
	.connect		= xs_local_connect,
	.buf_alloc		= rpc_malloc,
	.buf_free		= rpc_free,
	.send_request		= xs_local_send_request,
	.set_retrans_timeout	= xprt_set_retrans_timeout_def,
	.close			= xs_close,
	.destroy		= xs_destroy,
	.print_stats		= xs_local_print_stats,
	.enable_swap		= xs_enable_swap,
	.disable_swap		= xs_disable_swap,
};

static struct rpc_xprt_ops xs_udp_ops = {
	.set_buffer_size	= xs_udp_set_buffer_size,
	.reserve_xprt		= xprt_reserve_xprt_cong,
	.release_xprt		= xprt_release_xprt_cong,
	.alloc_slot		= xprt_alloc_slot,
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
	.enable_swap		= xs_enable_swap,
	.disable_swap		= xs_disable_swap,
	.inject_disconnect	= xs_inject_disconnect,
};

static struct rpc_xprt_ops xs_tcp_ops = {
	.reserve_xprt		= xprt_reserve_xprt,
	.release_xprt		= xs_tcp_release_xprt,
	.alloc_slot		= xprt_lock_and_alloc_slot,
	.rpcbind		= rpcb_getport_async,
	.set_port		= xs_set_port,
	.connect		= xs_connect,
	.buf_alloc		= rpc_malloc,
	.buf_free		= rpc_free,
	.send_request		= xs_tcp_send_request,
	.set_retrans_timeout	= xprt_set_retrans_timeout_def,
	.close			= xs_tcp_shutdown,
	.destroy		= xs_destroy,
	.print_stats		= xs_tcp_print_stats,
	.enable_swap		= xs_enable_swap,
	.disable_swap		= xs_disable_swap,
	.inject_disconnect	= xs_inject_disconnect,
#ifdef CONFIG_SUNRPC_BACKCHANNEL
	.bc_setup		= xprt_setup_bc,
	.bc_up			= xs_tcp_bc_up,
	.bc_maxpayload		= xs_tcp_bc_maxpayload,
	.bc_free_rqst		= xprt_free_bc_rqst,
	.bc_destroy		= xprt_destroy_bc,
#endif
};

/*
 * The rpc_xprt_ops for the server backchannel
 */

static struct rpc_xprt_ops bc_tcp_ops = {
	.reserve_xprt		= xprt_reserve_xprt,
	.release_xprt		= xprt_release_xprt,
	.alloc_slot		= xprt_alloc_slot,
	.buf_alloc		= bc_malloc,
	.buf_free		= bc_free,
	.send_request		= bc_send_request,
	.set_retrans_timeout	= xprt_set_retrans_timeout_def,
	.close			= bc_close,
	.destroy		= bc_destroy,
	.print_stats		= xs_tcp_print_stats,
	.enable_swap		= xs_enable_swap,
	.disable_swap		= xs_disable_swap,
	.inject_disconnect	= xs_inject_disconnect,
};

static int xs_init_anyaddr(const int family, struct sockaddr *sap)
{
	static const struct sockaddr_in sin = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= htonl(INADDR_ANY),
	};
	static const struct sockaddr_in6 sin6 = {
		.sin6_family		= AF_INET6,
		.sin6_addr		= IN6ADDR_ANY_INIT,
	};

	switch (family) {
	case AF_LOCAL:
		break;
	case AF_INET:
		memcpy(sap, &sin, sizeof(sin));
		break;
	case AF_INET6:
		memcpy(sap, &sin6, sizeof(sin6));
		break;
	default:
		dprintk("RPC:       %s: Bad address family\n", __func__);
		return -EAFNOSUPPORT;
	}
	return 0;
}

static struct rpc_xprt *xs_setup_xprt(struct xprt_create *args,
				      unsigned int slot_table_size,
				      unsigned int max_slot_table_size)
{
	struct rpc_xprt *xprt;
	struct sock_xprt *new;

	if (args->addrlen > sizeof(xprt->addr)) {
		dprintk("RPC:       xs_setup_xprt: address too large\n");
		return ERR_PTR(-EBADF);
	}

	xprt = xprt_alloc(args->net, sizeof(*new), slot_table_size,
			max_slot_table_size);
	if (xprt == NULL) {
		dprintk("RPC:       xs_setup_xprt: couldn't allocate "
				"rpc_xprt\n");
		return ERR_PTR(-ENOMEM);
	}

	new = container_of(xprt, struct sock_xprt, xprt);
	mutex_init(&new->recv_mutex);
	memcpy(&xprt->addr, args->dstaddr, args->addrlen);
	xprt->addrlen = args->addrlen;
	if (args->srcaddr)
		memcpy(&new->srcaddr, args->srcaddr, args->addrlen);
	else {
		int err;
		err = xs_init_anyaddr(args->dstaddr->sa_family,
					(struct sockaddr *)&new->srcaddr);
		if (err != 0) {
			xprt_free(xprt);
			return ERR_PTR(err);
		}
	}

	return xprt;
}

static const struct rpc_timeout xs_local_default_timeout = {
	.to_initval = 10 * HZ,
	.to_maxval = 10 * HZ,
	.to_retries = 2,
};

/**
 * xs_setup_local - Set up transport to use an AF_LOCAL socket
 * @args: rpc transport creation arguments
 *
 * AF_LOCAL is a "tpi_cots_ord" transport, just like TCP
 */
static struct rpc_xprt *xs_setup_local(struct xprt_create *args)
{
	struct sockaddr_un *sun = (struct sockaddr_un *)args->dstaddr;
	struct sock_xprt *transport;
	struct rpc_xprt *xprt;
	struct rpc_xprt *ret;

	xprt = xs_setup_xprt(args, xprt_tcp_slot_table_entries,
			xprt_max_tcp_slot_table_entries);
	if (IS_ERR(xprt))
		return xprt;
	transport = container_of(xprt, struct sock_xprt, xprt);

	xprt->prot = 0;
	xprt->tsh_size = sizeof(rpc_fraghdr) / sizeof(u32);
	xprt->max_payload = RPC_MAX_FRAGMENT_SIZE;

	xprt->bind_timeout = XS_BIND_TO;
	xprt->reestablish_timeout = XS_TCP_INIT_REEST_TO;
	xprt->idle_timeout = XS_IDLE_DISC_TO;

	xprt->ops = &xs_local_ops;
	xprt->timeout = &xs_local_default_timeout;

	INIT_WORK(&transport->recv_worker, xs_local_data_receive_workfn);
	INIT_DELAYED_WORK(&transport->connect_worker,
			xs_dummy_setup_socket);

	switch (sun->sun_family) {
	case AF_LOCAL:
		if (sun->sun_path[0] != '/') {
			dprintk("RPC:       bad AF_LOCAL address: %s\n",
					sun->sun_path);
			ret = ERR_PTR(-EINVAL);
			goto out_err;
		}
		xprt_set_bound(xprt);
		xs_format_peer_addresses(xprt, "local", RPCBIND_NETID_LOCAL);
		ret = ERR_PTR(xs_local_setup_socket(transport));
		if (ret)
			goto out_err;
		break;
	default:
		ret = ERR_PTR(-EAFNOSUPPORT);
		goto out_err;
	}

	dprintk("RPC:       set up xprt to %s via AF_LOCAL\n",
			xprt->address_strings[RPC_DISPLAY_ADDR]);

	if (try_module_get(THIS_MODULE))
		return xprt;
	ret = ERR_PTR(-EINVAL);
out_err:
	xs_xprt_free(xprt);
	return ret;
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
	struct rpc_xprt *ret;

	xprt = xs_setup_xprt(args, xprt_udp_slot_table_entries,
			xprt_udp_slot_table_entries);
	if (IS_ERR(xprt))
		return xprt;
	transport = container_of(xprt, struct sock_xprt, xprt);

	xprt->prot = IPPROTO_UDP;
	xprt->tsh_size = 0;
	/* XXX: header size can vary due to auth type, IPv6, etc. */
	xprt->max_payload = (1U << 16) - (MAX_HEADER << 3);

	xprt->bind_timeout = XS_BIND_TO;
	xprt->reestablish_timeout = XS_UDP_REEST_TO;
	xprt->idle_timeout = XS_IDLE_DISC_TO;

	xprt->ops = &xs_udp_ops;

	xprt->timeout = &xs_udp_default_timeout;

	INIT_WORK(&transport->recv_worker, xs_udp_data_receive_workfn);
	INIT_DELAYED_WORK(&transport->connect_worker, xs_udp_setup_socket);

	switch (addr->sa_family) {
	case AF_INET:
		if (((struct sockaddr_in *)addr)->sin_port != htons(0))
			xprt_set_bound(xprt);

		xs_format_peer_addresses(xprt, "udp", RPCBIND_NETID_UDP);
		break;
	case AF_INET6:
		if (((struct sockaddr_in6 *)addr)->sin6_port != htons(0))
			xprt_set_bound(xprt);

		xs_format_peer_addresses(xprt, "udp", RPCBIND_NETID_UDP6);
		break;
	default:
		ret = ERR_PTR(-EAFNOSUPPORT);
		goto out_err;
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
	ret = ERR_PTR(-EINVAL);
out_err:
	xs_xprt_free(xprt);
	return ret;
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
	struct rpc_xprt *ret;
	unsigned int max_slot_table_size = xprt_max_tcp_slot_table_entries;

	if (args->flags & XPRT_CREATE_INFINITE_SLOTS)
		max_slot_table_size = RPC_MAX_SLOT_TABLE_LIMIT;

	xprt = xs_setup_xprt(args, xprt_tcp_slot_table_entries,
			max_slot_table_size);
	if (IS_ERR(xprt))
		return xprt;
	transport = container_of(xprt, struct sock_xprt, xprt);

	xprt->prot = IPPROTO_TCP;
	xprt->tsh_size = sizeof(rpc_fraghdr) / sizeof(u32);
	xprt->max_payload = RPC_MAX_FRAGMENT_SIZE;

	xprt->bind_timeout = XS_BIND_TO;
	xprt->reestablish_timeout = XS_TCP_INIT_REEST_TO;
	xprt->idle_timeout = XS_IDLE_DISC_TO;

	xprt->ops = &xs_tcp_ops;
	xprt->timeout = &xs_tcp_default_timeout;

	INIT_WORK(&transport->recv_worker, xs_tcp_data_receive_workfn);
	INIT_DELAYED_WORK(&transport->connect_worker, xs_tcp_setup_socket);

	switch (addr->sa_family) {
	case AF_INET:
		if (((struct sockaddr_in *)addr)->sin_port != htons(0))
			xprt_set_bound(xprt);

		xs_format_peer_addresses(xprt, "tcp", RPCBIND_NETID_TCP);
		break;
	case AF_INET6:
		if (((struct sockaddr_in6 *)addr)->sin6_port != htons(0))
			xprt_set_bound(xprt);

		xs_format_peer_addresses(xprt, "tcp", RPCBIND_NETID_TCP6);
		break;
	default:
		ret = ERR_PTR(-EAFNOSUPPORT);
		goto out_err;
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
	ret = ERR_PTR(-EINVAL);
out_err:
	xs_xprt_free(xprt);
	return ret;
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
	struct rpc_xprt *ret;

	xprt = xs_setup_xprt(args, xprt_tcp_slot_table_entries,
			xprt_tcp_slot_table_entries);
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
	xprt->reestablish_timeout = 0;
	xprt->idle_timeout = 0;

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
		ret = ERR_PTR(-EAFNOSUPPORT);
		goto out_err;
	}

	dprintk("RPC:       set up xprt to %s (port %s) via %s\n",
			xprt->address_strings[RPC_DISPLAY_ADDR],
			xprt->address_strings[RPC_DISPLAY_PORT],
			xprt->address_strings[RPC_DISPLAY_PROTO]);

	/*
	 * Once we've associated a backchannel xprt with a connection,
	 * we want to keep it around as long as the connection lasts,
	 * in case we need to start using it for a backchannel again;
	 * this reference won't be dropped until bc_xprt is destroyed.
	 */
	xprt_get(xprt);
	args->bc_xprt->xpt_bc_xprt = xprt;
	xprt->bc_xprt = args->bc_xprt;
	bc_sock = container_of(args->bc_xprt, struct svc_sock, sk_xprt);
	transport->sock = bc_sock->sk_sock;
	transport->inet = bc_sock->sk_sk;

	/*
	 * Since we don't want connections for the backchannel, we set
	 * the xprt status to connected
	 */
	xprt_set_connected(xprt);

	if (try_module_get(THIS_MODULE))
		return xprt;

	args->bc_xprt->xpt_bc_xprt = NULL;
	args->bc_xprt->xpt_bc_xps = NULL;
	xprt_put(xprt);
	ret = ERR_PTR(-EINVAL);
out_err:
	xs_xprt_free(xprt);
	return ret;
}

static struct xprt_class	xs_local_transport = {
	.list		= LIST_HEAD_INIT(xs_local_transport.list),
	.name		= "named UNIX socket",
	.owner		= THIS_MODULE,
	.ident		= XPRT_TRANSPORT_LOCAL,
	.setup		= xs_setup_local,
};

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
#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
	if (!sunrpc_table_header)
		sunrpc_table_header = register_sysctl_table(sunrpc_table);
#endif

	xprt_register_transport(&xs_local_transport);
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
#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
	if (sunrpc_table_header) {
		unregister_sysctl_table(sunrpc_table_header);
		sunrpc_table_header = NULL;
	}
#endif

	xprt_unregister_transport(&xs_local_transport);
	xprt_unregister_transport(&xs_udp_transport);
	xprt_unregister_transport(&xs_tcp_transport);
	xprt_unregister_transport(&xs_bc_tcp_transport);
}

static int param_set_uint_minmax(const char *val,
		const struct kernel_param *kp,
		unsigned int min, unsigned int max)
{
	unsigned int num;
	int ret;

	if (!val)
		return -EINVAL;
	ret = kstrtouint(val, 0, &num);
	if (ret == -EINVAL || num < min || num > max)
		return -EINVAL;
	*((unsigned int *)kp->arg) = num;
	return 0;
}

static int param_set_portnr(const char *val, const struct kernel_param *kp)
{
	return param_set_uint_minmax(val, kp,
			RPC_MIN_RESVPORT,
			RPC_MAX_RESVPORT);
}

static const struct kernel_param_ops param_ops_portnr = {
	.set = param_set_portnr,
	.get = param_get_uint,
};

#define param_check_portnr(name, p) \
	__param_check(name, p, unsigned int);

module_param_named(min_resvport, xprt_min_resvport, portnr, 0644);
module_param_named(max_resvport, xprt_max_resvport, portnr, 0644);

static int param_set_slot_table_size(const char *val,
				     const struct kernel_param *kp)
{
	return param_set_uint_minmax(val, kp,
			RPC_MIN_SLOT_TABLE,
			RPC_MAX_SLOT_TABLE);
}

static const struct kernel_param_ops param_ops_slot_table_size = {
	.set = param_set_slot_table_size,
	.get = param_get_uint,
};

#define param_check_slot_table_size(name, p) \
	__param_check(name, p, unsigned int);

static int param_set_max_slot_table_size(const char *val,
				     const struct kernel_param *kp)
{
	return param_set_uint_minmax(val, kp,
			RPC_MIN_SLOT_TABLE,
			RPC_MAX_SLOT_TABLE_LIMIT);
}

static const struct kernel_param_ops param_ops_max_slot_table_size = {
	.set = param_set_max_slot_table_size,
	.get = param_get_uint,
};

#define param_check_max_slot_table_size(name, p) \
	__param_check(name, p, unsigned int);

module_param_named(tcp_slot_table_entries, xprt_tcp_slot_table_entries,
		   slot_table_size, 0644);
module_param_named(tcp_max_slot_table_entries, xprt_max_tcp_slot_table_entries,
		   max_slot_table_size, 0644);
module_param_named(udp_slot_table_entries, xprt_udp_slot_table_entries,
		   slot_table_size, 0644);

