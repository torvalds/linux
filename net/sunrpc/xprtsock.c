// SPDX-License-Identifier: GPL-2.0
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
#include <linux/bvec.h>
#include <linux/highmem.h>
#include <linux/uio.h>
#include <linux/sched/mm.h>

#include <trace/events/sunrpc.h>

#include "socklib.h"
#include "sunrpc.h"

static void xs_close(struct rpc_xprt *xprt);
static void xs_tcp_set_socket_timeouts(struct rpc_xprt *xprt,
		struct socket *sock);

/*
 * xprtsock tunables
 */
static unsigned int xprt_udp_slot_table_entries = RPC_DEF_SLOT_TABLE;
static unsigned int xprt_tcp_slot_table_entries = RPC_MIN_SLOT_TABLE;
static unsigned int xprt_max_tcp_slot_table_entries = RPC_MAX_SLOT_TABLE;

static unsigned int xprt_min_resvport = RPC_DEF_MIN_RESVPORT;
static unsigned int xprt_max_resvport = RPC_DEF_MAX_RESVPORT;

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
		.extra2		= &xprt_max_resvport_limit
	},
	{
		.procname	= "max_resvport",
		.data		= &xprt_max_resvport,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &xprt_min_resvport_limit,
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

static size_t
xs_alloc_sparse_pages(struct xdr_buf *buf, size_t want, gfp_t gfp)
{
	size_t i,n;

	if (!want || !(buf->flags & XDRBUF_SPARSE_PAGES))
		return want;
	n = (buf->page_base + want + PAGE_SIZE - 1) >> PAGE_SHIFT;
	for (i = 0; i < n; i++) {
		if (buf->pages[i])
			continue;
		buf->bvec[i].bv_page = buf->pages[i] = alloc_page(gfp);
		if (!buf->pages[i]) {
			i *= PAGE_SIZE;
			return i > buf->page_base ? i - buf->page_base : 0;
		}
	}
	return want;
}

static ssize_t
xs_sock_recvmsg(struct socket *sock, struct msghdr *msg, int flags, size_t seek)
{
	ssize_t ret;
	if (seek != 0)
		iov_iter_advance(&msg->msg_iter, seek);
	ret = sock_recvmsg(sock, msg, flags);
	return ret > 0 ? ret + seek : ret;
}

static ssize_t
xs_read_kvec(struct socket *sock, struct msghdr *msg, int flags,
		struct kvec *kvec, size_t count, size_t seek)
{
	iov_iter_kvec(&msg->msg_iter, READ, kvec, 1, count);
	return xs_sock_recvmsg(sock, msg, flags, seek);
}

static ssize_t
xs_read_bvec(struct socket *sock, struct msghdr *msg, int flags,
		struct bio_vec *bvec, unsigned long nr, size_t count,
		size_t seek)
{
	iov_iter_bvec(&msg->msg_iter, READ, bvec, nr, count);
	return xs_sock_recvmsg(sock, msg, flags, seek);
}

static ssize_t
xs_read_discard(struct socket *sock, struct msghdr *msg, int flags,
		size_t count)
{
	iov_iter_discard(&msg->msg_iter, READ, count);
	return sock_recvmsg(sock, msg, flags);
}

#if ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE
static void
xs_flush_bvec(const struct bio_vec *bvec, size_t count, size_t seek)
{
	struct bvec_iter bi = {
		.bi_size = count,
	};
	struct bio_vec bv;

	bvec_iter_advance(bvec, &bi, seek & PAGE_MASK);
	for_each_bvec(bv, bvec, bi, bi)
		flush_dcache_page(bv.bv_page);
}
#else
static inline void
xs_flush_bvec(const struct bio_vec *bvec, size_t count, size_t seek)
{
}
#endif

static ssize_t
xs_read_xdr_buf(struct socket *sock, struct msghdr *msg, int flags,
		struct xdr_buf *buf, size_t count, size_t seek, size_t *read)
{
	size_t want, seek_init = seek, offset = 0;
	ssize_t ret;

	want = min_t(size_t, count, buf->head[0].iov_len);
	if (seek < want) {
		ret = xs_read_kvec(sock, msg, flags, &buf->head[0], want, seek);
		if (ret <= 0)
			goto sock_err;
		offset += ret;
		if (offset == count || msg->msg_flags & (MSG_EOR|MSG_TRUNC))
			goto out;
		if (ret != want)
			goto out;
		seek = 0;
	} else {
		seek -= want;
		offset += want;
	}

	want = xs_alloc_sparse_pages(buf,
			min_t(size_t, count - offset, buf->page_len),
			GFP_KERNEL);
	if (seek < want) {
		ret = xs_read_bvec(sock, msg, flags, buf->bvec,
				xdr_buf_pagecount(buf),
				want + buf->page_base,
				seek + buf->page_base);
		if (ret <= 0)
			goto sock_err;
		xs_flush_bvec(buf->bvec, ret, seek + buf->page_base);
		ret -= buf->page_base;
		offset += ret;
		if (offset == count || msg->msg_flags & (MSG_EOR|MSG_TRUNC))
			goto out;
		if (ret != want)
			goto out;
		seek = 0;
	} else {
		seek -= want;
		offset += want;
	}

	want = min_t(size_t, count - offset, buf->tail[0].iov_len);
	if (seek < want) {
		ret = xs_read_kvec(sock, msg, flags, &buf->tail[0], want, seek);
		if (ret <= 0)
			goto sock_err;
		offset += ret;
		if (offset == count || msg->msg_flags & (MSG_EOR|MSG_TRUNC))
			goto out;
		if (ret != want)
			goto out;
	} else if (offset < seek_init)
		offset = seek_init;
	ret = -EMSGSIZE;
out:
	*read = offset - seek_init;
	return ret;
sock_err:
	offset += seek;
	goto out;
}

static void
xs_read_header(struct sock_xprt *transport, struct xdr_buf *buf)
{
	if (!transport->recv.copied) {
		if (buf->head[0].iov_len >= transport->recv.offset)
			memcpy(buf->head[0].iov_base,
					&transport->recv.xid,
					transport->recv.offset);
		transport->recv.copied = transport->recv.offset;
	}
}

static bool
xs_read_stream_request_done(struct sock_xprt *transport)
{
	return transport->recv.fraghdr & cpu_to_be32(RPC_LAST_STREAM_FRAGMENT);
}

static void
xs_read_stream_check_eor(struct sock_xprt *transport,
		struct msghdr *msg)
{
	if (xs_read_stream_request_done(transport))
		msg->msg_flags |= MSG_EOR;
}

static ssize_t
xs_read_stream_request(struct sock_xprt *transport, struct msghdr *msg,
		int flags, struct rpc_rqst *req)
{
	struct xdr_buf *buf = &req->rq_private_buf;
	size_t want, read;
	ssize_t ret;

	xs_read_header(transport, buf);

	want = transport->recv.len - transport->recv.offset;
	if (want != 0) {
		ret = xs_read_xdr_buf(transport->sock, msg, flags, buf,
				transport->recv.copied + want,
				transport->recv.copied,
				&read);
		transport->recv.offset += read;
		transport->recv.copied += read;
	}

	if (transport->recv.offset == transport->recv.len)
		xs_read_stream_check_eor(transport, msg);

	if (want == 0)
		return 0;

	switch (ret) {
	default:
		break;
	case -EFAULT:
	case -EMSGSIZE:
		msg->msg_flags |= MSG_TRUNC;
		return read;
	case 0:
		return -ESHUTDOWN;
	}
	return ret < 0 ? ret : read;
}

static size_t
xs_read_stream_headersize(bool isfrag)
{
	if (isfrag)
		return sizeof(__be32);
	return 3 * sizeof(__be32);
}

static ssize_t
xs_read_stream_header(struct sock_xprt *transport, struct msghdr *msg,
		int flags, size_t want, size_t seek)
{
	struct kvec kvec = {
		.iov_base = &transport->recv.fraghdr,
		.iov_len = want,
	};
	return xs_read_kvec(transport->sock, msg, flags, &kvec, want, seek);
}

#if defined(CONFIG_SUNRPC_BACKCHANNEL)
static ssize_t
xs_read_stream_call(struct sock_xprt *transport, struct msghdr *msg, int flags)
{
	struct rpc_xprt *xprt = &transport->xprt;
	struct rpc_rqst *req;
	ssize_t ret;

	/* Look up and lock the request corresponding to the given XID */
	req = xprt_lookup_bc_request(xprt, transport->recv.xid);
	if (!req) {
		printk(KERN_WARNING "Callback slot table overflowed\n");
		return -ESHUTDOWN;
	}
	if (transport->recv.copied && !req->rq_private_buf.len)
		return -ESHUTDOWN;

	ret = xs_read_stream_request(transport, msg, flags, req);
	if (msg->msg_flags & (MSG_EOR|MSG_TRUNC))
		xprt_complete_bc_request(req, transport->recv.copied);
	else
		req->rq_private_buf.len = transport->recv.copied;

	return ret;
}
#else /* CONFIG_SUNRPC_BACKCHANNEL */
static ssize_t
xs_read_stream_call(struct sock_xprt *transport, struct msghdr *msg, int flags)
{
	return -ESHUTDOWN;
}
#endif /* CONFIG_SUNRPC_BACKCHANNEL */

static ssize_t
xs_read_stream_reply(struct sock_xprt *transport, struct msghdr *msg, int flags)
{
	struct rpc_xprt *xprt = &transport->xprt;
	struct rpc_rqst *req;
	ssize_t ret = 0;

	/* Look up and lock the request corresponding to the given XID */
	spin_lock(&xprt->queue_lock);
	req = xprt_lookup_rqst(xprt, transport->recv.xid);
	if (!req || (transport->recv.copied && !req->rq_private_buf.len)) {
		msg->msg_flags |= MSG_TRUNC;
		goto out;
	}
	xprt_pin_rqst(req);
	spin_unlock(&xprt->queue_lock);

	ret = xs_read_stream_request(transport, msg, flags, req);

	spin_lock(&xprt->queue_lock);
	if (msg->msg_flags & (MSG_EOR|MSG_TRUNC))
		xprt_complete_rqst(req->rq_task, transport->recv.copied);
	else
		req->rq_private_buf.len = transport->recv.copied;
	xprt_unpin_rqst(req);
out:
	spin_unlock(&xprt->queue_lock);
	return ret;
}

static ssize_t
xs_read_stream(struct sock_xprt *transport, int flags)
{
	struct msghdr msg = { 0 };
	size_t want, read = 0;
	ssize_t ret = 0;

	if (transport->recv.len == 0) {
		want = xs_read_stream_headersize(transport->recv.copied != 0);
		ret = xs_read_stream_header(transport, &msg, flags, want,
				transport->recv.offset);
		if (ret <= 0)
			goto out_err;
		transport->recv.offset = ret;
		if (transport->recv.offset != want)
			return transport->recv.offset;
		transport->recv.len = be32_to_cpu(transport->recv.fraghdr) &
			RPC_FRAGMENT_SIZE_MASK;
		transport->recv.offset -= sizeof(transport->recv.fraghdr);
		read = ret;
	}

	switch (be32_to_cpu(transport->recv.calldir)) {
	default:
		msg.msg_flags |= MSG_TRUNC;
		break;
	case RPC_CALL:
		ret = xs_read_stream_call(transport, &msg, flags);
		break;
	case RPC_REPLY:
		ret = xs_read_stream_reply(transport, &msg, flags);
	}
	if (msg.msg_flags & MSG_TRUNC) {
		transport->recv.calldir = cpu_to_be32(-1);
		transport->recv.copied = -1;
	}
	if (ret < 0)
		goto out_err;
	read += ret;
	if (transport->recv.offset < transport->recv.len) {
		if (!(msg.msg_flags & MSG_TRUNC))
			return read;
		msg.msg_flags = 0;
		ret = xs_read_discard(transport->sock, &msg, flags,
				transport->recv.len - transport->recv.offset);
		if (ret <= 0)
			goto out_err;
		transport->recv.offset += ret;
		read += ret;
		if (transport->recv.offset != transport->recv.len)
			return read;
	}
	if (xs_read_stream_request_done(transport)) {
		trace_xs_stream_read_request(transport);
		transport->recv.copied = 0;
	}
	transport->recv.offset = 0;
	transport->recv.len = 0;
	return read;
out_err:
	return ret != 0 ? ret : -ESHUTDOWN;
}

static __poll_t xs_poll_socket(struct sock_xprt *transport)
{
	return transport->sock->ops->poll(transport->file, transport->sock,
			NULL);
}

static bool xs_poll_socket_readable(struct sock_xprt *transport)
{
	__poll_t events = xs_poll_socket(transport);

	return (events & (EPOLLIN | EPOLLRDNORM)) && !(events & EPOLLRDHUP);
}

static void xs_poll_check_readable(struct sock_xprt *transport)
{

	clear_bit(XPRT_SOCK_DATA_READY, &transport->sock_state);
	if (!xs_poll_socket_readable(transport))
		return;
	if (!test_and_set_bit(XPRT_SOCK_DATA_READY, &transport->sock_state))
		queue_work(xprtiod_workqueue, &transport->recv_worker);
}

static void xs_stream_data_receive(struct sock_xprt *transport)
{
	size_t read = 0;
	ssize_t ret = 0;

	mutex_lock(&transport->recv_mutex);
	if (transport->sock == NULL)
		goto out;
	for (;;) {
		ret = xs_read_stream(transport, MSG_DONTWAIT);
		if (ret < 0)
			break;
		read += ret;
		cond_resched();
	}
	if (ret == -ESHUTDOWN)
		kernel_sock_shutdown(transport->sock, SHUT_RDWR);
	else
		xs_poll_check_readable(transport);
out:
	mutex_unlock(&transport->recv_mutex);
	trace_xs_stream_read_data(&transport->xprt, ret, read);
}

static void xs_stream_data_receive_workfn(struct work_struct *work)
{
	struct sock_xprt *transport =
		container_of(work, struct sock_xprt, recv_worker);
	unsigned int pflags = memalloc_nofs_save();

	xs_stream_data_receive(transport);
	memalloc_nofs_restore(pflags);
}

static void
xs_stream_reset_connect(struct sock_xprt *transport)
{
	transport->recv.offset = 0;
	transport->recv.len = 0;
	transport->recv.copied = 0;
	transport->xmit.offset = 0;
}

static void
xs_stream_start_connect(struct sock_xprt *transport)
{
	transport->xprt.stat.connect_count++;
	transport->xprt.stat.connect_start = jiffies;
}

#define XS_SENDMSG_FLAGS	(MSG_DONTWAIT | MSG_NOSIGNAL)

/**
 * xs_nospace - handle transmit was incomplete
 * @req: pointer to RPC request
 *
 */
static int xs_nospace(struct rpc_rqst *req)
{
	struct rpc_xprt *xprt = req->rq_xprt;
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	struct sock *sk = transport->inet;
	int ret = -EAGAIN;

	trace_rpc_socket_nospace(req, transport);

	/* Protect against races with write_space */
	spin_lock(&xprt->transport_lock);

	/* Don't race with disconnect */
	if (xprt_connected(xprt)) {
		/* wait for more buffer space */
		sk->sk_write_pending++;
		xprt_wait_for_buffer_space(xprt);
	} else
		ret = -ENOTCONN;

	spin_unlock(&xprt->transport_lock);

	/* Race breaker in case memory is freed before above code is called */
	if (ret == -EAGAIN) {
		struct socket_wq *wq;

		rcu_read_lock();
		wq = rcu_dereference(sk->sk_wq);
		set_bit(SOCKWQ_ASYNC_NOSPACE, &wq->flags);
		rcu_read_unlock();

		sk->sk_write_space(sk);
	}
	return ret;
}

static void
xs_stream_prepare_request(struct rpc_rqst *req)
{
	xdr_free_bvec(&req->rq_rcv_buf);
	req->rq_task->tk_status = xdr_alloc_bvec(&req->rq_rcv_buf, GFP_KERNEL);
}

/*
 * Determine if the previous message in the stream was aborted before it
 * could complete transmission.
 */
static bool
xs_send_request_was_aborted(struct sock_xprt *transport, struct rpc_rqst *req)
{
	return transport->xmit.offset != 0 && req->rq_bytes_sent == 0;
}

/*
 * Return the stream record marker field for a record of length < 2^31-1
 */
static rpc_fraghdr
xs_stream_record_marker(struct xdr_buf *xdr)
{
	if (!xdr->len)
		return 0;
	return cpu_to_be32(RPC_LAST_STREAM_FRAGMENT | (u32)xdr->len);
}

/**
 * xs_local_send_request - write an RPC request to an AF_LOCAL socket
 * @req: pointer to RPC request
 *
 * Return values:
 *        0:	The request has been sent
 *   EAGAIN:	The socket was blocked, please call again later to
 *		complete the request
 * ENOTCONN:	Caller needs to invoke connect logic then call again
 *    other:	Some other error occured, the request was not sent
 */
static int xs_local_send_request(struct rpc_rqst *req)
{
	struct rpc_xprt *xprt = req->rq_xprt;
	struct sock_xprt *transport =
				container_of(xprt, struct sock_xprt, xprt);
	struct xdr_buf *xdr = &req->rq_snd_buf;
	rpc_fraghdr rm = xs_stream_record_marker(xdr);
	unsigned int msglen = rm ? req->rq_slen + sizeof(rm) : req->rq_slen;
	struct msghdr msg = {
		.msg_flags	= XS_SENDMSG_FLAGS,
	};
	unsigned int sent;
	int status;

	/* Close the stream if the previous transmission was incomplete */
	if (xs_send_request_was_aborted(transport, req)) {
		xs_close(xprt);
		return -ENOTCONN;
	}

	xs_pktdump("packet data:",
			req->rq_svec->iov_base, req->rq_svec->iov_len);

	req->rq_xtime = ktime_get();
	status = xprt_sock_sendmsg(transport->sock, &msg, xdr,
				   transport->xmit.offset, rm, &sent);
	dprintk("RPC:       %s(%u) = %d\n",
			__func__, xdr->len - transport->xmit.offset, status);

	if (status == -EAGAIN && sock_writeable(transport->inet))
		status = -ENOBUFS;

	if (likely(sent > 0) || status == 0) {
		transport->xmit.offset += sent;
		req->rq_bytes_sent = transport->xmit.offset;
		if (likely(req->rq_bytes_sent >= msglen)) {
			req->rq_xmit_bytes_sent += transport->xmit.offset;
			transport->xmit.offset = 0;
			return 0;
		}
		status = -EAGAIN;
	}

	switch (status) {
	case -ENOBUFS:
		break;
	case -EAGAIN:
		status = xs_nospace(req);
		break;
	default:
		dprintk("RPC:       sendmsg returned unrecognized error %d\n",
			-status);
		fallthrough;
	case -EPIPE:
		xs_close(xprt);
		status = -ENOTCONN;
	}

	return status;
}

/**
 * xs_udp_send_request - write an RPC request to a UDP socket
 * @req: pointer to RPC request
 *
 * Return values:
 *        0:	The request has been sent
 *   EAGAIN:	The socket was blocked, please call again later to
 *		complete the request
 * ENOTCONN:	Caller needs to invoke connect logic then call again
 *    other:	Some other error occurred, the request was not sent
 */
static int xs_udp_send_request(struct rpc_rqst *req)
{
	struct rpc_xprt *xprt = req->rq_xprt;
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	struct xdr_buf *xdr = &req->rq_snd_buf;
	struct msghdr msg = {
		.msg_name	= xs_addr(xprt),
		.msg_namelen	= xprt->addrlen,
		.msg_flags	= XS_SENDMSG_FLAGS,
	};
	unsigned int sent;
	int status;

	xs_pktdump("packet data:",
				req->rq_svec->iov_base,
				req->rq_svec->iov_len);

	if (!xprt_bound(xprt))
		return -ENOTCONN;

	if (!xprt_request_get_cong(xprt, req))
		return -EBADSLT;

	req->rq_xtime = ktime_get();
	status = xprt_sock_sendmsg(transport->sock, &msg, xdr, 0, 0, &sent);

	dprintk("RPC:       xs_udp_send_request(%u) = %d\n",
			xdr->len, status);

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
		status = xs_nospace(req);
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
 * @req: pointer to RPC request
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
static int xs_tcp_send_request(struct rpc_rqst *req)
{
	struct rpc_xprt *xprt = req->rq_xprt;
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	struct xdr_buf *xdr = &req->rq_snd_buf;
	rpc_fraghdr rm = xs_stream_record_marker(xdr);
	unsigned int msglen = rm ? req->rq_slen + sizeof(rm) : req->rq_slen;
	struct msghdr msg = {
		.msg_flags	= XS_SENDMSG_FLAGS,
	};
	bool vm_wait = false;
	unsigned int sent;
	int status;

	/* Close the stream if the previous transmission was incomplete */
	if (xs_send_request_was_aborted(transport, req)) {
		if (transport->sock != NULL)
			kernel_sock_shutdown(transport->sock, SHUT_RDWR);
		return -ENOTCONN;
	}

	xs_pktdump("packet data:",
				req->rq_svec->iov_base,
				req->rq_svec->iov_len);

	if (test_bit(XPRT_SOCK_UPD_TIMEOUT, &transport->sock_state))
		xs_tcp_set_socket_timeouts(xprt, transport->sock);

	/* Continue transmitting the packet/record. We must be careful
	 * to cope with writespace callbacks arriving _after_ we have
	 * called sendmsg(). */
	req->rq_xtime = ktime_get();
	while (1) {
		status = xprt_sock_sendmsg(transport->sock, &msg, xdr,
					   transport->xmit.offset, rm, &sent);

		dprintk("RPC:       xs_tcp_send_request(%u) = %d\n",
				xdr->len - transport->xmit.offset, status);

		/* If we've sent the entire packet, immediately
		 * reset the count of bytes sent. */
		transport->xmit.offset += sent;
		req->rq_bytes_sent = transport->xmit.offset;
		if (likely(req->rq_bytes_sent >= msglen)) {
			req->rq_xmit_bytes_sent += transport->xmit.offset;
			transport->xmit.offset = 0;
			return 0;
		}

		WARN_ON_ONCE(sent == 0 && status == 0);

		if (status == -EAGAIN ) {
			/*
			 * Return EAGAIN if we're sure we're hitting the
			 * socket send buffer limits.
			 */
			if (test_bit(SOCK_NOSPACE, &transport->sock->flags))
				break;
			/*
			 * Did we hit a memory allocation failure?
			 */
			if (sent == 0) {
				status = -ENOBUFS;
				if (vm_wait)
					break;
				/* Retry, knowing now that we're below the
				 * socket send buffer limit
				 */
				vm_wait = true;
			}
			continue;
		}
		if (status < 0)
			break;
		vm_wait = false;
	}

	switch (status) {
	case -ENOTSOCK:
		status = -ENOTCONN;
		/* Should we call xs_close() here? */
		break;
	case -EAGAIN:
		status = xs_nospace(req);
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

static void xs_sock_reset_state_flags(struct rpc_xprt *xprt)
{
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);

	clear_bit(XPRT_SOCK_DATA_READY, &transport->sock_state);
	clear_bit(XPRT_SOCK_WAKE_ERROR, &transport->sock_state);
	clear_bit(XPRT_SOCK_WAKE_WRITE, &transport->sock_state);
	clear_bit(XPRT_SOCK_WAKE_DISCONNECT, &transport->sock_state);
}

static void xs_run_error_worker(struct sock_xprt *transport, unsigned int nr)
{
	set_bit(nr, &transport->sock_state);
	queue_work(xprtiod_workqueue, &transport->error_worker);
}

static void xs_sock_reset_connection_flags(struct rpc_xprt *xprt)
{
	smp_mb__before_atomic();
	clear_bit(XPRT_CLOSE_WAIT, &xprt->state);
	clear_bit(XPRT_CLOSING, &xprt->state);
	xs_sock_reset_state_flags(xprt);
	smp_mb__after_atomic();
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
	struct sock_xprt *transport;
	struct rpc_xprt *xprt;

	read_lock_bh(&sk->sk_callback_lock);
	if (!(xprt = xprt_from_sock(sk)))
		goto out;

	transport = container_of(xprt, struct sock_xprt, xprt);
	transport->xprt_err = -sk->sk_err;
	if (transport->xprt_err == 0)
		goto out;
	dprintk("RPC:       xs_error_report client %p, error=%d...\n",
			xprt, -transport->xprt_err);
	trace_rpc_socket_error(xprt, sk->sk_socket, transport->xprt_err);

	/* barrier ensures xprt_err is set before XPRT_SOCK_WAKE_ERROR */
	smp_mb__before_atomic();
	xs_run_error_worker(transport, XPRT_SOCK_WAKE_ERROR);
 out:
	read_unlock_bh(&sk->sk_callback_lock);
}

static void xs_reset_transport(struct sock_xprt *transport)
{
	struct socket *sock = transport->sock;
	struct sock *sk = transport->inet;
	struct rpc_xprt *xprt = &transport->xprt;
	struct file *filp = transport->file;

	if (sk == NULL)
		return;

	if (atomic_read(&transport->xprt.swapper))
		sk_clear_memalloc(sk);

	kernel_sock_shutdown(sock, SHUT_RDWR);

	mutex_lock(&transport->recv_mutex);
	write_lock_bh(&sk->sk_callback_lock);
	transport->inet = NULL;
	transport->sock = NULL;
	transport->file = NULL;

	sk->sk_user_data = NULL;

	xs_restore_old_callbacks(transport, sk);
	xprt_clear_connected(xprt);
	write_unlock_bh(&sk->sk_callback_lock);
	xs_sock_reset_connection_flags(xprt);
	/* Reset stream record info */
	xs_stream_reset_connect(transport);
	mutex_unlock(&transport->recv_mutex);

	trace_rpc_socket_close(xprt, sock);
	fput(filp);

	xprt_disconnect_done(xprt);
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
	cancel_work_sync(&transport->error_worker);
	xs_xprt_free(xprt);
	module_put(THIS_MODULE);
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
	spin_lock(&xprt->queue_lock);
	rovr = xprt_lookup_rqst(xprt, *xp);
	if (!rovr)
		goto out_unlock;
	xprt_pin_rqst(rovr);
	xprt_update_rtt(rovr->rq_task);
	spin_unlock(&xprt->queue_lock);
	task = rovr->rq_task;

	if ((copied = rovr->rq_private_buf.buflen) > repsize)
		copied = repsize;

	/* Suck it into the iovec, verify checksum if not done by hw. */
	if (csum_partial_copy_to_xdr(&rovr->rq_private_buf, skb)) {
		spin_lock(&xprt->queue_lock);
		__UDPX_INC_STATS(sk, UDP_MIB_INERRORS);
		goto out_unpin;
	}


	spin_lock(&xprt->transport_lock);
	xprt_adjust_cwnd(xprt, task, copied);
	spin_unlock(&xprt->transport_lock);
	spin_lock(&xprt->queue_lock);
	xprt_complete_rqst(task, copied);
	__UDPX_INC_STATS(sk, UDP_MIB_INDATAGRAMS);
out_unpin:
	xprt_unpin_rqst(rovr);
 out_unlock:
	spin_unlock(&xprt->queue_lock);
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
		skb = skb_recv_udp(sk, 0, 1, &err);
		if (skb == NULL)
			break;
		xs_udp_data_read_skb(&transport->xprt, sk, skb);
		consume_skb(skb);
		cond_resched();
	}
	xs_poll_check_readable(transport);
out:
	mutex_unlock(&transport->recv_mutex);
}

static void xs_udp_data_receive_workfn(struct work_struct *work)
{
	struct sock_xprt *transport =
		container_of(work, struct sock_xprt, recv_worker);
	unsigned int pflags = memalloc_nofs_save();

	xs_udp_data_receive(transport);
	memalloc_nofs_restore(pflags);
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
		transport->old_data_ready(sk);
		/* Any data means we had a useful conversation, so
		 * then we don't need to delay the next reconnect
		 */
		if (xprt->reestablish_timeout)
			xprt->reestablish_timeout = 0;
		if (!test_and_set_bit(XPRT_SOCK_DATA_READY, &transport->sock_state))
			queue_work(xprtiod_workqueue, &transport->recv_worker);
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

#if defined(CONFIG_SUNRPC_BACKCHANNEL)
static size_t xs_tcp_bc_maxpayload(struct rpc_xprt *xprt)
{
	return PAGE_SIZE;
}
#endif /* CONFIG_SUNRPC_BACKCHANNEL */

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
		if (!xprt_test_and_set_connected(xprt)) {
			xprt->connect_cookie++;
			clear_bit(XPRT_SOCK_CONNECTING, &transport->sock_state);
			xprt_clear_connecting(xprt);

			xprt->stat.connect_count++;
			xprt->stat.connect_time += (long)jiffies -
						   xprt->stat.connect_start;
			xs_run_error_worker(transport, XPRT_SOCK_WAKE_PENDING);
		}
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
		xs_run_error_worker(transport, XPRT_SOCK_WAKE_DISCONNECT);
		fallthrough;
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
		clear_bit(XPRT_CLOSING, &xprt->state);
		/* Trigger the socket release */
		xs_run_error_worker(transport, XPRT_SOCK_WAKE_DISCONNECT);
	}
 out:
	read_unlock_bh(&sk->sk_callback_lock);
}

static void xs_write_space(struct sock *sk)
{
	struct socket_wq *wq;
	struct sock_xprt *transport;
	struct rpc_xprt *xprt;

	if (!sk->sk_socket)
		return;
	clear_bit(SOCK_NOSPACE, &sk->sk_socket->flags);

	if (unlikely(!(xprt = xprt_from_sock(sk))))
		return;
	transport = container_of(xprt, struct sock_xprt, xprt);
	rcu_read_lock();
	wq = rcu_dereference(sk->sk_wq);
	if (!wq || test_and_clear_bit(SOCKWQ_ASYNC_NOSPACE, &wq->flags) == 0)
		goto out;

	xs_run_error_worker(transport, XPRT_SOCK_WAKE_WRITE);
	sk->sk_write_pending--;
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
 * @xprt: controlling transport
 * @task: task that timed out
 *
 * Adjust the congestion window after a retransmit timeout has occurred.
 */
static void xs_udp_timer(struct rpc_xprt *xprt, struct rpc_task *task)
{
	spin_lock(&xprt->transport_lock);
	xprt_adjust_cwnd(xprt, task, -ETIMEDOUT);
	spin_unlock(&xprt->transport_lock);
}

static int xs_get_random_port(void)
{
	unsigned short min = xprt_min_resvport, max = xprt_max_resvport;
	unsigned short range;
	unsigned short rand;

	if (max < min)
		return -EADDRINUSE;
	range = max - min + 1;
	rand = (unsigned short) prandom_u32() % range;
	return rand + min;
}

static unsigned short xs_sock_getport(struct socket *sock)
{
	struct sockaddr_storage buf;
	unsigned short port = 0;

	if (kernel_getsockname(sock, (struct sockaddr *)&buf) < 0)
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
	if (transport->srcport == 0 && transport->xprt.reuseport)
		transport->srcport = xs_sock_getport(sock);
}

static int xs_get_srcport(struct sock_xprt *transport)
{
	int port = transport->srcport;

	if (port == 0 && transport->xprt.resvport)
		port = xs_get_random_port();
	return port;
}

unsigned short get_srcport(struct rpc_xprt *xprt)
{
	struct sock_xprt *sock = container_of(xprt, struct sock_xprt, xprt);
	return xs_sock_getport(sock->sock);
}
EXPORT_SYMBOL(get_srcport);

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
	int port = xs_get_srcport(transport);
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
	if (port <= 0)
		return port;

	memcpy(&myaddr, &transport->srcaddr, transport->xprt.addrlen);
	do {
		rpc_set_port((struct sockaddr *)&myaddr, port);
		err = kernel_bind(sock, (struct sockaddr *)&myaddr,
				transport->xprt.addrlen);
		if (err == 0) {
			if (transport->xprt.reuseport)
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
	struct file *filp;
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
		sock_set_reuseport(sock->sk);

	err = xs_bind(transport, sock);
	if (err) {
		sock_release(sock);
		goto out;
	}

	filp = sock_alloc_file(sock, O_NONBLOCK, NULL);
	if (IS_ERR(filp))
		return ERR_CAST(filp);
	transport->file = filp;

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

		xprt_clear_connected(xprt);

		/* Reset to new socket */
		transport->sock = sock;
		transport->inet = sk;

		write_unlock_bh(&sk->sk_callback_lock);
	}

	xs_stream_start_connect(transport);

	return kernel_connect(sock, xs_addr(xprt), xprt->addrlen, 0);
}

/**
 * xs_local_setup_socket - create AF_LOCAL socket, connect to a local endpoint
 * @transport: socket transport to connect
 */
static int xs_local_setup_socket(struct sock_xprt *transport)
{
	struct rpc_xprt *xprt = &transport->xprt;
	struct file *filp;
	struct socket *sock;
	int status;

	status = __sock_create(xprt->xprt_net, AF_LOCAL,
					SOCK_STREAM, 0, &sock, 1);
	if (status < 0) {
		dprintk("RPC:       can't create AF_LOCAL "
			"transport socket (%d).\n", -status);
		goto out;
	}
	xs_reclassify_socket(AF_LOCAL, sock);

	filp = sock_alloc_file(sock, O_NONBLOCK, NULL);
	if (IS_ERR(filp)) {
		status = PTR_ERR(filp);
		goto out;
	}
	transport->file = filp;

	dprintk("RPC:       worker connecting xprt %p via AF_LOCAL to %s\n",
			xprt, xprt->address_strings[RPC_DISPLAY_ADDR]);

	status = xs_local_finish_connecting(xprt, sock);
	trace_rpc_socket_connect(xprt, sock, status);
	switch (status) {
	case 0:
		dprintk("RPC:       xprt %p connected to %s\n",
				xprt, xprt->address_strings[RPC_DISPLAY_ADDR]);
		xprt->stat.connect_count++;
		xprt->stat.connect_time += (long)jiffies -
					   xprt->stat.connect_start;
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
		task->tk_rpc_status = -ENOTCONN;
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

		xprt_set_connected(xprt);

		/* Reset to new socket */
		transport->sock = sock;
		transport->inet = sk;

		xs_set_memalloc(xprt);

		write_unlock_bh(&sk->sk_callback_lock);
	}
	xs_udp_do_set_buffer_size(xprt);

	xprt->stat.connect_start = jiffies;
}

static void xs_udp_setup_socket(struct work_struct *work)
{
	struct sock_xprt *transport =
		container_of(work, struct sock_xprt, connect_worker.work);
	struct rpc_xprt *xprt = &transport->xprt;
	struct socket *sock;
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
	xprt_clear_connecting(xprt);
	xprt_unlock_connect(xprt, transport);
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
	int skst = transport->inet ? transport->inet->sk_state : TCP_CLOSE;

	if (sock == NULL)
		return;
	switch (skst) {
	default:
		kernel_sock_shutdown(sock, SHUT_RDWR);
		trace_rpc_socket_shutdown(xprt, sock);
		break;
	case TCP_CLOSE:
	case TCP_TIME_WAIT:
		xs_reset_transport(transport);
	}
}

static void xs_tcp_set_socket_timeouts(struct rpc_xprt *xprt,
		struct socket *sock)
{
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	unsigned int keepidle;
	unsigned int keepcnt;
	unsigned int timeo;

	spin_lock(&xprt->transport_lock);
	keepidle = DIV_ROUND_UP(xprt->timeout->to_initval, HZ);
	keepcnt = xprt->timeout->to_retries + 1;
	timeo = jiffies_to_msecs(xprt->timeout->to_initval) *
		(xprt->timeout->to_retries + 1);
	clear_bit(XPRT_SOCK_UPD_TIMEOUT, &transport->sock_state);
	spin_unlock(&xprt->transport_lock);

	/* TCP Keepalive options */
	sock_set_keepalive(sock->sk);
	tcp_sock_set_keepidle(sock->sk, keepidle);
	tcp_sock_set_keepintvl(sock->sk, keepidle);
	tcp_sock_set_keepcnt(sock->sk, keepcnt);

	/* TCP user timeout (see RFC5482) */
	tcp_sock_set_user_timeout(sock->sk, timeo);
}

static void xs_tcp_set_connect_timeout(struct rpc_xprt *xprt,
		unsigned long connect_timeout,
		unsigned long reconnect_timeout)
{
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	struct rpc_timeout to;
	unsigned long initval;

	spin_lock(&xprt->transport_lock);
	if (reconnect_timeout < xprt->max_reconnect_timeout)
		xprt->max_reconnect_timeout = reconnect_timeout;
	if (connect_timeout < xprt->connect_timeout) {
		memcpy(&to, xprt->timeout, sizeof(to));
		initval = DIV_ROUND_UP(connect_timeout, to.to_retries + 1);
		/* Arbitrary lower limit */
		if (initval <  XS_TCP_INIT_REEST_TO << 1)
			initval = XS_TCP_INIT_REEST_TO << 1;
		to.to_initval = initval;
		to.to_maxval = initval;
		memcpy(&transport->tcp_timeout, &to,
				sizeof(transport->tcp_timeout));
		xprt->timeout = &transport->tcp_timeout;
		xprt->connect_timeout = connect_timeout;
	}
	set_bit(XPRT_SOCK_UPD_TIMEOUT, &transport->sock_state);
	spin_unlock(&xprt->transport_lock);
}

static int xs_tcp_finish_connecting(struct rpc_xprt *xprt, struct socket *sock)
{
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	int ret = -ENOTCONN;

	if (!transport->inet) {
		struct sock *sk = sock->sk;

		/* Avoid temporary address, they are bad for long-lived
		 * connections such as NFS mounts.
		 * RFC4941, section 3.6 suggests that:
		 *    Individual applications, which have specific
		 *    knowledge about the normal duration of connections,
		 *    MAY override this as appropriate.
		 */
		if (xs_addr(xprt)->sa_family == PF_INET6) {
			ip6_sock_set_addr_preferences(sk,
				IPV6_PREFER_SRC_PUBLIC);
		}

		xs_tcp_set_socket_timeouts(xprt, sock);

		write_lock_bh(&sk->sk_callback_lock);

		xs_save_old_callbacks(transport, sk);

		sk->sk_user_data = xprt;
		sk->sk_data_ready = xs_data_ready;
		sk->sk_state_change = xs_tcp_state_change;
		sk->sk_write_space = xs_tcp_write_space;
		sock_set_flag(sk, SOCK_FASYNC);
		sk->sk_error_report = xs_error_report;

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

	xs_stream_start_connect(transport);

	/* Tell the socket layer to start connecting... */
	set_bit(XPRT_SOCK_CONNECTING, &transport->sock_state);
	ret = kernel_connect(sock, xs_addr(xprt), xprt->addrlen, O_NONBLOCK);
	switch (ret) {
	case 0:
		xs_set_srcport(transport, sock);
		fallthrough;
	case -EINPROGRESS:
		/* SYN_SENT! */
		if (xprt->reestablish_timeout < XS_TCP_INIT_REEST_TO)
			xprt->reestablish_timeout = XS_TCP_INIT_REEST_TO;
		break;
	case -EADDRNOTAVAIL:
		/* Source port number is unavailable. Try a new one! */
		transport->srcport = 0;
	}
out:
	return ret;
}

/**
 * xs_tcp_setup_socket - create a TCP socket and connect to a remote endpoint
 * @work: queued work item
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
		fallthrough;
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
	case -ENETDOWN:
	case -ENETUNREACH:
	case -EHOSTUNREACH:
	case -EADDRINUSE:
	case -ENOBUFS:
		/*
		 * xs_tcp_force_close() wakes tasks with -EIO.
		 * We need to wake them first to ensure the
		 * correct error code.
		 */
		xprt_wake_pending_tasks(xprt, status);
		xs_tcp_force_close(xprt);
		goto out;
	}
	status = -EAGAIN;
out:
	xprt_clear_connecting(xprt);
	xprt_unlock_connect(xprt, transport);
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
	unsigned long delay = 0;

	WARN_ON_ONCE(!xprt_lock_connect(xprt, task, transport));

	if (transport->sock != NULL) {
		dprintk("RPC:       xs_connect delayed xprt %p for %lu "
				"seconds\n",
				xprt, xprt->reestablish_timeout / HZ);

		/* Start by resetting any existing state */
		xs_reset_transport(transport);

		delay = xprt_reconnect_delay(xprt);
		xprt_reconnect_backoff(xprt, XS_TCP_INIT_REEST_TO);

	} else
		dprintk("RPC:       xs_connect scheduled xprt %p\n", xprt);

	queue_delayed_work(xprtiod_workqueue,
			&transport->connect_worker,
			delay);
}

static void xs_wake_disconnect(struct sock_xprt *transport)
{
	if (test_and_clear_bit(XPRT_SOCK_WAKE_DISCONNECT, &transport->sock_state))
		xs_tcp_force_close(&transport->xprt);
}

static void xs_wake_write(struct sock_xprt *transport)
{
	if (test_and_clear_bit(XPRT_SOCK_WAKE_WRITE, &transport->sock_state))
		xprt_write_space(&transport->xprt);
}

static void xs_wake_error(struct sock_xprt *transport)
{
	int sockerr;

	if (!test_bit(XPRT_SOCK_WAKE_ERROR, &transport->sock_state))
		return;
	mutex_lock(&transport->recv_mutex);
	if (transport->sock == NULL)
		goto out;
	if (!test_and_clear_bit(XPRT_SOCK_WAKE_ERROR, &transport->sock_state))
		goto out;
	sockerr = xchg(&transport->xprt_err, 0);
	if (sockerr < 0)
		xprt_wake_pending_tasks(&transport->xprt, sockerr);
out:
	mutex_unlock(&transport->recv_mutex);
}

static void xs_wake_pending(struct sock_xprt *transport)
{
	if (test_and_clear_bit(XPRT_SOCK_WAKE_PENDING, &transport->sock_state))
		xprt_wake_pending_tasks(&transport->xprt, -EAGAIN);
}

static void xs_error_handle(struct work_struct *work)
{
	struct sock_xprt *transport = container_of(work,
			struct sock_xprt, error_worker);

	xs_wake_disconnect(transport);
	xs_wake_write(transport);
	xs_wake_error(transport);
	xs_wake_pending(transport);
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
			xprt->stat.connect_time / HZ,
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
			xprt->stat.connect_time / HZ,
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
static int bc_malloc(struct rpc_task *task)
{
	struct rpc_rqst *rqst = task->tk_rqstp;
	size_t size = rqst->rq_callsize;
	struct page *page;
	struct rpc_buffer *buf;

	if (size > PAGE_SIZE - sizeof(struct rpc_buffer)) {
		WARN_ONCE(1, "xprtsock: large bc buffer request (size %zu)\n",
			  size);
		return -EINVAL;
	}

	page = alloc_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	buf = page_address(page);
	buf->len = PAGE_SIZE;

	rqst->rq_buffer = buf->data;
	rqst->rq_rbuffer = (char *)rqst->rq_buffer + rqst->rq_callsize;
	return 0;
}

/*
 * Free the space allocated in the bc_alloc routine
 */
static void bc_free(struct rpc_task *task)
{
	void *buffer = task->tk_rqstp->rq_buffer;
	struct rpc_buffer *buf;

	buf = container_of(buffer, struct rpc_buffer, data);
	free_page((unsigned long)buf);
}

static int bc_sendto(struct rpc_rqst *req)
{
	struct xdr_buf *xdr = &req->rq_snd_buf;
	struct sock_xprt *transport =
			container_of(req->rq_xprt, struct sock_xprt, xprt);
	struct msghdr msg = {
		.msg_flags	= 0,
	};
	rpc_fraghdr marker = cpu_to_be32(RPC_LAST_STREAM_FRAGMENT |
					 (u32)xdr->len);
	unsigned int sent = 0;
	int err;

	req->rq_xtime = ktime_get();
	err = xprt_sock_sendmsg(transport->sock, &msg, xdr, 0, marker, &sent);
	xdr_free_bvec(xdr);
	if (err < 0 || sent != (xdr->len + sizeof(marker)))
		return -EAGAIN;
	return sent;
}

/**
 * bc_send_request - Send a backchannel Call on a TCP socket
 * @req: rpc_rqst containing Call message to be sent
 *
 * xpt_mutex ensures @rqstp's whole message is written to the socket
 * without interruption.
 *
 * Return values:
 *   %0 if the message was sent successfully
 *   %ENOTCONN if the message was not sent
 */
static int bc_send_request(struct rpc_rqst *req)
{
	struct svc_xprt	*xprt;
	int len;

	/*
	 * Get the server socket associated with this callback xprt
	 */
	xprt = req->rq_xprt->bc_xprt;

	/*
	 * Grab the mutex to serialize data as the connection is shared
	 * with the fore channel
	 */
	mutex_lock(&xprt->xpt_mutex);
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
	xprt_disconnect_done(xprt);
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

static const struct rpc_xprt_ops xs_local_ops = {
	.reserve_xprt		= xprt_reserve_xprt,
	.release_xprt		= xprt_release_xprt,
	.alloc_slot		= xprt_alloc_slot,
	.free_slot		= xprt_free_slot,
	.rpcbind		= xs_local_rpcbind,
	.set_port		= xs_local_set_port,
	.connect		= xs_local_connect,
	.buf_alloc		= rpc_malloc,
	.buf_free		= rpc_free,
	.prepare_request	= xs_stream_prepare_request,
	.send_request		= xs_local_send_request,
	.wait_for_reply_request	= xprt_wait_for_reply_request_def,
	.close			= xs_close,
	.destroy		= xs_destroy,
	.print_stats		= xs_local_print_stats,
	.enable_swap		= xs_enable_swap,
	.disable_swap		= xs_disable_swap,
};

static const struct rpc_xprt_ops xs_udp_ops = {
	.set_buffer_size	= xs_udp_set_buffer_size,
	.reserve_xprt		= xprt_reserve_xprt_cong,
	.release_xprt		= xprt_release_xprt_cong,
	.alloc_slot		= xprt_alloc_slot,
	.free_slot		= xprt_free_slot,
	.rpcbind		= rpcb_getport_async,
	.set_port		= xs_set_port,
	.connect		= xs_connect,
	.buf_alloc		= rpc_malloc,
	.buf_free		= rpc_free,
	.send_request		= xs_udp_send_request,
	.wait_for_reply_request	= xprt_wait_for_reply_request_rtt,
	.timer			= xs_udp_timer,
	.release_request	= xprt_release_rqst_cong,
	.close			= xs_close,
	.destroy		= xs_destroy,
	.print_stats		= xs_udp_print_stats,
	.enable_swap		= xs_enable_swap,
	.disable_swap		= xs_disable_swap,
	.inject_disconnect	= xs_inject_disconnect,
};

static const struct rpc_xprt_ops xs_tcp_ops = {
	.reserve_xprt		= xprt_reserve_xprt,
	.release_xprt		= xprt_release_xprt,
	.alloc_slot		= xprt_alloc_slot,
	.free_slot		= xprt_free_slot,
	.rpcbind		= rpcb_getport_async,
	.set_port		= xs_set_port,
	.connect		= xs_connect,
	.buf_alloc		= rpc_malloc,
	.buf_free		= rpc_free,
	.prepare_request	= xs_stream_prepare_request,
	.send_request		= xs_tcp_send_request,
	.wait_for_reply_request	= xprt_wait_for_reply_request_def,
	.close			= xs_tcp_shutdown,
	.destroy		= xs_destroy,
	.set_connect_timeout	= xs_tcp_set_connect_timeout,
	.print_stats		= xs_tcp_print_stats,
	.enable_swap		= xs_enable_swap,
	.disable_swap		= xs_disable_swap,
	.inject_disconnect	= xs_inject_disconnect,
#ifdef CONFIG_SUNRPC_BACKCHANNEL
	.bc_setup		= xprt_setup_bc,
	.bc_maxpayload		= xs_tcp_bc_maxpayload,
	.bc_num_slots		= xprt_bc_max_slots,
	.bc_free_rqst		= xprt_free_bc_rqst,
	.bc_destroy		= xprt_destroy_bc,
#endif
};

/*
 * The rpc_xprt_ops for the server backchannel
 */

static const struct rpc_xprt_ops bc_tcp_ops = {
	.reserve_xprt		= xprt_reserve_xprt,
	.release_xprt		= xprt_release_xprt,
	.alloc_slot		= xprt_alloc_slot,
	.free_slot		= xprt_free_slot,
	.buf_alloc		= bc_malloc,
	.buf_free		= bc_free,
	.send_request		= bc_send_request,
	.wait_for_reply_request	= xprt_wait_for_reply_request_def,
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
	xprt->max_payload = RPC_MAX_FRAGMENT_SIZE;

	xprt->bind_timeout = XS_BIND_TO;
	xprt->reestablish_timeout = XS_TCP_INIT_REEST_TO;
	xprt->idle_timeout = XS_IDLE_DISC_TO;

	xprt->ops = &xs_local_ops;
	xprt->timeout = &xs_local_default_timeout;

	INIT_WORK(&transport->recv_worker, xs_stream_data_receive_workfn);
	INIT_WORK(&transport->error_worker, xs_error_handle);
	INIT_DELAYED_WORK(&transport->connect_worker, xs_dummy_setup_socket);

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
	/* XXX: header size can vary due to auth type, IPv6, etc. */
	xprt->max_payload = (1U << 16) - (MAX_HEADER << 3);

	xprt->bind_timeout = XS_BIND_TO;
	xprt->reestablish_timeout = XS_UDP_REEST_TO;
	xprt->idle_timeout = XS_IDLE_DISC_TO;

	xprt->ops = &xs_udp_ops;

	xprt->timeout = &xs_udp_default_timeout;

	INIT_WORK(&transport->recv_worker, xs_udp_data_receive_workfn);
	INIT_WORK(&transport->error_worker, xs_error_handle);
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
	xprt->max_payload = RPC_MAX_FRAGMENT_SIZE;

	xprt->bind_timeout = XS_BIND_TO;
	xprt->reestablish_timeout = XS_TCP_INIT_REEST_TO;
	xprt->idle_timeout = XS_IDLE_DISC_TO;

	xprt->ops = &xs_tcp_ops;
	xprt->timeout = &xs_tcp_default_timeout;

	xprt->max_reconnect_timeout = xprt->timeout->to_maxval;
	xprt->connect_timeout = xprt->timeout->to_initval *
		(xprt->timeout->to_retries + 1);

	INIT_WORK(&transport->recv_worker, xs_stream_data_receive_workfn);
	INIT_WORK(&transport->error_worker, xs_error_handle);
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
	.netid		= { "" },
};

static struct xprt_class	xs_udp_transport = {
	.list		= LIST_HEAD_INIT(xs_udp_transport.list),
	.name		= "udp",
	.owner		= THIS_MODULE,
	.ident		= XPRT_TRANSPORT_UDP,
	.setup		= xs_setup_udp,
	.netid		= { "udp", "udp6", "" },
};

static struct xprt_class	xs_tcp_transport = {
	.list		= LIST_HEAD_INIT(xs_tcp_transport.list),
	.name		= "tcp",
	.owner		= THIS_MODULE,
	.ident		= XPRT_TRANSPORT_TCP,
	.setup		= xs_setup_tcp,
	.netid		= { "tcp", "tcp6", "" },
};

static struct xprt_class	xs_bc_tcp_transport = {
	.list		= LIST_HEAD_INIT(xs_bc_tcp_transport.list),
	.name		= "tcp NFSv4.1 backchannel",
	.owner		= THIS_MODULE,
	.ident		= XPRT_TRANSPORT_BC_TCP,
	.setup		= xs_setup_bc_tcp,
	.netid		= { "" },
};

/**
 * init_socket_xprt - set up xprtsock's sysctls, register with RPC client
 *
 */
int init_socket_xprt(void)
{
	if (!sunrpc_table_header)
		sunrpc_table_header = register_sysctl_table(sunrpc_table);

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
	if (sunrpc_table_header) {
		unregister_sysctl_table(sunrpc_table_header);
		sunrpc_table_header = NULL;
	}

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
	if (ret)
		return ret;
	if (num < min || num > max)
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
