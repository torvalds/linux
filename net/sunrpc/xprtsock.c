/*
 * linux/net/sunrpc/xprtsock.c
 *
 * Client-side transport implementation for sockets.
 *
 * TCP callback races fixes (C) 1998 Red Hat Software <alan@redhat.com>
 * TCP send fixes (C) 1998 Red Hat Software <alan@redhat.com>
 * TCP NFS related read + write fixes
 *  (C) 1999 Dave Airlie, University of Limerick, Ireland <airlied@linux.ie>
 *
 * Rewrite of larges part of the code in order to stabilize TCP stuff.
 * Fix behaviour when socket buffer is full.
 *  (C) 1999 Trond Myklebust <trond.myklebust@fys.uio.no>
 *
 * IP socket transport implementation, (C) 2005 Chuck Lever <cel@netapp.com>
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/capability.h>
#include <linux/sched.h>
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
#include <linux/file.h>

#include <net/sock.h>
#include <net/checksum.h>
#include <net/udp.h>
#include <net/tcp.h>

/*
 * xprtsock tunables
 */
unsigned int xprt_udp_slot_table_entries = RPC_DEF_SLOT_TABLE;
unsigned int xprt_tcp_slot_table_entries = RPC_DEF_SLOT_TABLE;

unsigned int xprt_min_resvport = RPC_DEF_MIN_RESVPORT;
unsigned int xprt_max_resvport = RPC_DEF_MAX_RESVPORT;

/*
 * How many times to try sending a request on a socket before waiting
 * for the socket buffer to clear.
 */
#define XS_SENDMSG_RETRY	(10U)

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
				tcp_xid;

	u32			tcp_offset,
				tcp_reclen;

	unsigned long		tcp_copied,
				tcp_flags;
};

/*
 * TCP receive state flags
 */
#define TCP_RCV_LAST_FRAG	(1UL << 0)
#define TCP_RCV_COPY_FRAGHDR	(1UL << 1)
#define TCP_RCV_COPY_XID	(1UL << 2)
#define TCP_RCV_COPY_DATA	(1UL << 3)

static void xs_format_peer_addresses(struct rpc_xprt *xprt)
{
	struct sockaddr_in *addr = (struct sockaddr_in *) &xprt->addr;
	char *buf;

	buf = kzalloc(20, GFP_KERNEL);
	if (buf) {
		snprintf(buf, 20, "%u.%u.%u.%u",
				NIPQUAD(addr->sin_addr.s_addr));
	}
	xprt->address_strings[RPC_DISPLAY_ADDR] = buf;

	buf = kzalloc(8, GFP_KERNEL);
	if (buf) {
		snprintf(buf, 8, "%u",
				ntohs(addr->sin_port));
	}
	xprt->address_strings[RPC_DISPLAY_PORT] = buf;

	if (xprt->prot == IPPROTO_UDP)
		xprt->address_strings[RPC_DISPLAY_PROTO] = "udp";
	else
		xprt->address_strings[RPC_DISPLAY_PROTO] = "tcp";

	buf = kzalloc(48, GFP_KERNEL);
	if (buf) {
		snprintf(buf, 48, "addr=%u.%u.%u.%u port=%u proto=%s",
			NIPQUAD(addr->sin_addr.s_addr),
			ntohs(addr->sin_port),
			xprt->prot == IPPROTO_UDP ? "udp" : "tcp");
	}
	xprt->address_strings[RPC_DISPLAY_ALL] = buf;
}

static void xs_free_peer_addresses(struct rpc_xprt *xprt)
{
	kfree(xprt->address_strings[RPC_DISPLAY_ADDR]);
	kfree(xprt->address_strings[RPC_DISPLAY_PORT]);
	kfree(xprt->address_strings[RPC_DISPLAY_ALL]);
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
		return -ENOTCONN;

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

/**
 * xs_nospace - place task on wait queue if transmit was incomplete
 * @task: task to put to sleep
 *
 */
static void xs_nospace(struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_xprt *xprt = req->rq_xprt;
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);

	dprintk("RPC: %4d xmit incomplete (%u left of %u)\n",
			task->tk_pid, req->rq_slen - req->rq_bytes_sent,
			req->rq_slen);

	if (test_bit(SOCK_ASYNC_NOSPACE, &transport->sock->flags)) {
		/* Protect against races with write_space */
		spin_lock_bh(&xprt->transport_lock);

		/* Don't race with disconnect */
		if (!xprt_connected(xprt))
			task->tk_status = -ENOTCONN;
		else if (test_bit(SOCK_NOSPACE, &transport->sock->flags))
			xprt_wait_for_buffer_space(task);

		spin_unlock_bh(&xprt->transport_lock);
	} else
		/* Keep holding the socket if it is blocked */
		rpc_delay(task, HZ>>4);
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

	req->rq_xtime = jiffies;
	status = xs_sendpages(transport->sock,
			      (struct sockaddr *) &xprt->addr,
			      xprt->addrlen, xdr,
			      req->rq_bytes_sent);

	dprintk("RPC:      xs_udp_send_request(%u) = %d\n",
			xdr->len - req->rq_bytes_sent, status);

	if (likely(status >= (int) req->rq_slen))
		return 0;

	/* Still some bytes left; set up for a retry later. */
	if (status > 0)
		status = -EAGAIN;

	switch (status) {
	case -ENETUNREACH:
	case -EPIPE:
	case -ECONNREFUSED:
		/* When the server has died, an ICMP port unreachable message
		 * prompts ECONNREFUSED. */
		break;
	case -EAGAIN:
		xs_nospace(task);
		break;
	default:
		dprintk("RPC:      sendmsg returned unrecognized error %d\n",
			-status);
		break;
	}

	return status;
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
	int status, retry = 0;

	xs_encode_tcp_record_marker(&req->rq_snd_buf);

	xs_pktdump("packet data:",
				req->rq_svec->iov_base,
				req->rq_svec->iov_len);

	/* Continue transmitting the packet/record. We must be careful
	 * to cope with writespace callbacks arriving _after_ we have
	 * called sendmsg(). */
	while (1) {
		req->rq_xtime = jiffies;
		status = xs_sendpages(transport->sock,
					NULL, 0, xdr, req->rq_bytes_sent);

		dprintk("RPC:      xs_tcp_send_request(%u) = %d\n",
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

		status = -EAGAIN;
		if (retry++ > XS_SENDMSG_RETRY)
			break;
	}

	switch (status) {
	case -EAGAIN:
		xs_nospace(task);
		break;
	case -ECONNREFUSED:
	case -ECONNRESET:
	case -ENOTCONN:
	case -EPIPE:
		status = -ENOTCONN;
		break;
	default:
		dprintk("RPC:      sendmsg returned unrecognized error %d\n",
			-status);
		xprt_disconnect(xprt);
		break;
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
	if (req->rq_bytes_sent == 0)
		goto out_release;
	if (req->rq_bytes_sent == req->rq_snd_buf.len)
		goto out_release;
	set_bit(XPRT_CLOSE_WAIT, &task->tk_xprt->state);
out_release:
	xprt_release_xprt(xprt, task);
}

/**
 * xs_close - close a socket
 * @xprt: transport
 *
 * This is used when all requests are complete; ie, no DRC state remains
 * on the server we want to save.
 */
static void xs_close(struct rpc_xprt *xprt)
{
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	struct socket *sock = transport->sock;
	struct sock *sk = transport->inet;

	if (!sk)
		goto clear_close_wait;

	dprintk("RPC:      xs_close xprt %p\n", xprt);

	write_lock_bh(&sk->sk_callback_lock);
	transport->inet = NULL;
	transport->sock = NULL;

	sk->sk_user_data = NULL;
	sk->sk_data_ready = xprt->old_data_ready;
	sk->sk_state_change = xprt->old_state_change;
	sk->sk_write_space = xprt->old_write_space;
	write_unlock_bh(&sk->sk_callback_lock);

	sk->sk_no_check = 0;

	sock_release(sock);
clear_close_wait:
	smp_mb__before_clear_bit();
	clear_bit(XPRT_CLOSE_WAIT, &xprt->state);
	smp_mb__after_clear_bit();
}

/**
 * xs_destroy - prepare to shutdown a transport
 * @xprt: doomed transport
 *
 */
static void xs_destroy(struct rpc_xprt *xprt)
{
	dprintk("RPC:      xs_destroy xprt %p\n", xprt);

	cancel_delayed_work(&xprt->connect_worker);
	flush_scheduled_work();

	xprt_disconnect(xprt);
	xs_close(xprt);
	xs_free_peer_addresses(xprt);
	kfree(xprt->slot);
	kfree(xprt);
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
	dprintk("RPC:      xs_udp_data_ready...\n");
	if (!(xprt = xprt_from_sock(sk)))
		goto out;

	if ((skb = skb_recv_datagram(sk, 0, 1, &err)) == NULL)
		goto out;

	if (xprt->shutdown)
		goto dropit;

	repsize = skb->len - sizeof(struct udphdr);
	if (repsize < 4) {
		dprintk("RPC:      impossible RPC reply size %d!\n", repsize);
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
	if (csum_partial_copy_to_xdr(&rovr->rq_private_buf, skb))
		goto out_unlock;

	/* Something worked... */
	dst_confirm(skb->dst);

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

static inline size_t xs_tcp_copy_data(skb_reader_t *desc, void *p, size_t len)
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

static inline void xs_tcp_read_fraghdr(struct rpc_xprt *xprt, skb_reader_t *desc)
{
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	size_t len, used;
	char *p;

	p = ((char *) &transport->tcp_fraghdr) + transport->tcp_offset;
	len = sizeof(transport->tcp_fraghdr) - transport->tcp_offset;
	used = xs_tcp_copy_data(desc, p, len);
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
	if (unlikely(transport->tcp_reclen < 4)) {
		dprintk("RPC:      invalid TCP record fragment length\n");
		xprt_disconnect(xprt);
		return;
	}
	dprintk("RPC:      reading TCP record fragment of length %d\n",
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

static inline void xs_tcp_read_xid(struct sock_xprt *transport, skb_reader_t *desc)
{
	size_t len, used;
	char *p;

	len = sizeof(transport->tcp_xid) - transport->tcp_offset;
	dprintk("RPC:      reading XID (%Zu bytes)\n", len);
	p = ((char *) &transport->tcp_xid) + transport->tcp_offset;
	used = xs_tcp_copy_data(desc, p, len);
	transport->tcp_offset += used;
	if (used != len)
		return;
	transport->tcp_flags &= ~TCP_RCV_COPY_XID;
	transport->tcp_flags |= TCP_RCV_COPY_DATA;
	transport->tcp_copied = 4;
	dprintk("RPC:      reading reply for XID %08x\n",
			ntohl(transport->tcp_xid));
	xs_tcp_check_fraghdr(transport);
}

static inline void xs_tcp_read_request(struct rpc_xprt *xprt, skb_reader_t *desc)
{
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	struct rpc_rqst *req;
	struct xdr_buf *rcvbuf;
	size_t len;
	ssize_t r;

	/* Find and lock the request corresponding to this xid */
	spin_lock(&xprt->transport_lock);
	req = xprt_lookup_rqst(xprt, transport->tcp_xid);
	if (!req) {
		transport->tcp_flags &= ~TCP_RCV_COPY_DATA;
		dprintk("RPC:      XID %08x request not found!\n",
				ntohl(transport->tcp_xid));
		spin_unlock(&xprt->transport_lock);
		return;
	}

	rcvbuf = &req->rq_private_buf;
	len = desc->count;
	if (len > transport->tcp_reclen - transport->tcp_offset) {
		skb_reader_t my_desc;

		len = transport->tcp_reclen - transport->tcp_offset;
		memcpy(&my_desc, desc, sizeof(my_desc));
		my_desc.count = len;
		r = xdr_partial_copy_from_skb(rcvbuf, transport->tcp_copied,
					  &my_desc, xs_tcp_copy_data);
		desc->count -= r;
		desc->offset += r;
	} else
		r = xdr_partial_copy_from_skb(rcvbuf, transport->tcp_copied,
					  desc, xs_tcp_copy_data);

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
		dprintk("RPC:      XID %08x truncated request\n",
				ntohl(transport->tcp_xid));
		dprintk("RPC:      xprt = %p, tcp_copied = %lu, tcp_offset = %u, tcp_reclen = %u\n",
				xprt, transport->tcp_copied, transport->tcp_offset,
					transport->tcp_reclen);
		goto out;
	}

	dprintk("RPC:      XID %08x read %Zd bytes\n",
			ntohl(transport->tcp_xid), r);
	dprintk("RPC:      xprt = %p, tcp_copied = %lu, tcp_offset = %u, tcp_reclen = %u\n",
			xprt, transport->tcp_copied, transport->tcp_offset,
				transport->tcp_reclen);

	if (transport->tcp_copied == req->rq_private_buf.buflen)
		transport->tcp_flags &= ~TCP_RCV_COPY_DATA;
	else if (transport->tcp_offset == transport->tcp_reclen) {
		if (transport->tcp_flags & TCP_RCV_LAST_FRAG)
			transport->tcp_flags &= ~TCP_RCV_COPY_DATA;
	}

out:
	if (!(transport->tcp_flags & TCP_RCV_COPY_DATA))
		xprt_complete_rqst(req->rq_task, transport->tcp_copied);
	spin_unlock(&xprt->transport_lock);
	xs_tcp_check_fraghdr(transport);
}

static inline void xs_tcp_read_discard(struct sock_xprt *transport, skb_reader_t *desc)
{
	size_t len;

	len = transport->tcp_reclen - transport->tcp_offset;
	if (len > desc->count)
		len = desc->count;
	desc->count -= len;
	desc->offset += len;
	transport->tcp_offset += len;
	dprintk("RPC:      discarded %Zu bytes\n", len);
	xs_tcp_check_fraghdr(transport);
}

static int xs_tcp_data_recv(read_descriptor_t *rd_desc, struct sk_buff *skb, unsigned int offset, size_t len)
{
	struct rpc_xprt *xprt = rd_desc->arg.data;
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	skb_reader_t desc = {
		.skb	= skb,
		.offset	= offset,
		.count	= len,
	};

	dprintk("RPC:      xs_tcp_data_recv started\n");
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
		/* Read in the request data */
		if (transport->tcp_flags & TCP_RCV_COPY_DATA) {
			xs_tcp_read_request(xprt, &desc);
			continue;
		}
		/* Skip over any trailing bytes on short reads */
		xs_tcp_read_discard(transport, &desc);
	} while (desc.count);
	dprintk("RPC:      xs_tcp_data_recv done\n");
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

	read_lock(&sk->sk_callback_lock);
	dprintk("RPC:      xs_tcp_data_ready...\n");
	if (!(xprt = xprt_from_sock(sk)))
		goto out;
	if (xprt->shutdown)
		goto out;

	/* We use rd_desc to pass struct xprt to xs_tcp_data_recv */
	rd_desc.arg.data = xprt;
	rd_desc.count = 65536;
	tcp_read_sock(sk, &rd_desc, xs_tcp_data_recv);
out:
	read_unlock(&sk->sk_callback_lock);
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
	dprintk("RPC:      xs_tcp_state_change client %p...\n", xprt);
	dprintk("RPC:      state %x conn %d dead %d zapped %d\n",
				sk->sk_state, xprt_connected(xprt),
				sock_flag(sk, SOCK_DEAD),
				sock_flag(sk, SOCK_ZAPPED));

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

			xprt->reestablish_timeout = XS_TCP_INIT_REEST_TO;
			xprt_wake_pending_tasks(xprt, 0);
		}
		spin_unlock_bh(&xprt->transport_lock);
		break;
	case TCP_SYN_SENT:
	case TCP_SYN_RECV:
		break;
	case TCP_CLOSE_WAIT:
		/* Try to schedule an autoclose RPC calls */
		set_bit(XPRT_CLOSE_WAIT, &xprt->state);
		if (test_and_set_bit(XPRT_LOCKED, &xprt->state) == 0)
			schedule_work(&xprt->task_cleanup);
	default:
		xprt_disconnect(xprt);
	}
 out:
	read_unlock(&sk->sk_callback_lock);
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
	if (sock_writeable(sk)) {
		struct socket *sock;
		struct rpc_xprt *xprt;

		if (unlikely(!(sock = sk->sk_socket)))
			goto out;
		if (unlikely(!(xprt = xprt_from_sock(sk))))
			goto out;
		if (unlikely(!test_and_clear_bit(SOCK_NOSPACE, &sock->flags)))
			goto out;

		xprt_write_space(xprt);
	}

 out:
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
	if (sk_stream_wspace(sk) >= sk_stream_min_wspace(sk)) {
		struct socket *sock;
		struct rpc_xprt *xprt;

		if (unlikely(!(sock = sk->sk_socket)))
			goto out;
		if (unlikely(!(xprt = xprt_from_sock(sk))))
			goto out;
		if (unlikely(!test_and_clear_bit(SOCK_NOSPACE, &sock->flags)))
			goto out;

		xprt_write_space(xprt);
	}

 out:
	read_unlock(&sk->sk_callback_lock);
}

static void xs_udp_do_set_buffer_size(struct rpc_xprt *xprt)
{
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	struct sock *sk = transport->inet;

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
	xprt->sndsize = 0;
	if (sndsize)
		xprt->sndsize = sndsize + 1024;
	xprt->rcvsize = 0;
	if (rcvsize)
		xprt->rcvsize = rcvsize + 1024;

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
 * xs_print_peer_address - format an IPv4 address for printing
 * @xprt: generic transport
 * @format: flags field indicating which parts of the address to render
 */
static char *xs_print_peer_address(struct rpc_xprt *xprt, enum rpc_display_format_t format)
{
	if (xprt->address_strings[format] != NULL)
		return xprt->address_strings[format];
	else
		return "unprintable";
}

/**
 * xs_set_port - reset the port number in the remote endpoint address
 * @xprt: generic transport
 * @port: new port number
 *
 */
static void xs_set_port(struct rpc_xprt *xprt, unsigned short port)
{
	struct sockaddr_in *sap = (struct sockaddr_in *) &xprt->addr;

	dprintk("RPC:      setting port for xprt %p to %u\n", xprt, port);

	sap->sin_port = htons(port);
}

static int xs_bindresvport(struct rpc_xprt *xprt, struct socket *sock)
{
	struct sockaddr_in myaddr = {
		.sin_family = AF_INET,
	};
	int err;
	unsigned short port = xprt->port;

	do {
		myaddr.sin_port = htons(port);
		err = kernel_bind(sock, (struct sockaddr *) &myaddr,
						sizeof(myaddr));
		if (err == 0) {
			xprt->port = port;
			dprintk("RPC:      xs_bindresvport bound to port %u\n",
					port);
			return 0;
		}
		if (port <= xprt_min_resvport)
			port = xprt_max_resvport;
		else
			port--;
	} while (err == -EADDRINUSE && port != xprt->port);

	dprintk("RPC:      can't bind to reserved port (%d).\n", -err);
	return err;
}

/**
 * xs_udp_connect_worker - set up a UDP socket
 * @args: RPC transport to connect
 *
 * Invoked by a work queue tasklet.
 */
static void xs_udp_connect_worker(void *args)
{
	struct rpc_xprt *xprt = (struct rpc_xprt *) args;
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	struct socket *sock = transport->sock;
	int err, status = -EIO;

	if (xprt->shutdown || !xprt_bound(xprt))
		goto out;

	/* Start by resetting any existing state */
	xs_close(xprt);

	if ((err = sock_create_kern(PF_INET, SOCK_DGRAM, IPPROTO_UDP, &sock)) < 0) {
		dprintk("RPC:      can't create UDP transport socket (%d).\n", -err);
		goto out;
	}

	if (xprt->resvport && xs_bindresvport(xprt, sock) < 0) {
		sock_release(sock);
		goto out;
	}

	dprintk("RPC:      worker connecting xprt %p to address: %s\n",
			xprt, xs_print_peer_address(xprt, RPC_DISPLAY_ALL));

	if (!transport->inet) {
		struct sock *sk = sock->sk;

		write_lock_bh(&sk->sk_callback_lock);

		sk->sk_user_data = xprt;
		xprt->old_data_ready = sk->sk_data_ready;
		xprt->old_state_change = sk->sk_state_change;
		xprt->old_write_space = sk->sk_write_space;
		sk->sk_data_ready = xs_udp_data_ready;
		sk->sk_write_space = xs_udp_write_space;
		sk->sk_no_check = UDP_CSUM_NORCV;
		sk->sk_allocation = GFP_ATOMIC;

		xprt_set_connected(xprt);

		/* Reset to new socket */
		transport->sock = sock;
		transport->inet = sk;

		write_unlock_bh(&sk->sk_callback_lock);
	}
	xs_udp_do_set_buffer_size(xprt);
	status = 0;
out:
	xprt_wake_pending_tasks(xprt, status);
	xprt_clear_connecting(xprt);
}

/*
 * We need to preserve the port number so the reply cache on the server can
 * find our cached RPC replies when we get around to reconnecting.
 */
static void xs_tcp_reuse_connection(struct rpc_xprt *xprt)
{
	int result;
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	struct sockaddr any;

	dprintk("RPC:      disconnecting xprt %p to reuse port\n", xprt);

	/*
	 * Disconnect the transport socket by doing a connect operation
	 * with AF_UNSPEC.  This should return immediately...
	 */
	memset(&any, 0, sizeof(any));
	any.sa_family = AF_UNSPEC;
	result = kernel_connect(transport->sock, &any, sizeof(any), 0);
	if (result)
		dprintk("RPC:      AF_UNSPEC connect return code %d\n",
				result);
}

/**
 * xs_tcp_connect_worker - connect a TCP socket to a remote endpoint
 * @args: RPC transport to connect
 *
 * Invoked by a work queue tasklet.
 */
static void xs_tcp_connect_worker(void *args)
{
	struct rpc_xprt *xprt = (struct rpc_xprt *)args;
	struct sock_xprt *transport = container_of(xprt, struct sock_xprt, xprt);
	struct socket *sock = transport->sock;
	int err, status = -EIO;

	if (xprt->shutdown || !xprt_bound(xprt))
		goto out;

	if (!sock) {
		/* start from scratch */
		if ((err = sock_create_kern(PF_INET, SOCK_STREAM, IPPROTO_TCP, &sock)) < 0) {
			dprintk("RPC:      can't create TCP transport socket (%d).\n", -err);
			goto out;
		}

		if (xprt->resvport && xs_bindresvport(xprt, sock) < 0) {
			sock_release(sock);
			goto out;
		}
	} else
		/* "close" the socket, preserving the local port */
		xs_tcp_reuse_connection(xprt);

	dprintk("RPC:      worker connecting xprt %p to address: %s\n",
			xprt, xs_print_peer_address(xprt, RPC_DISPLAY_ALL));

	if (!transport->inet) {
		struct sock *sk = sock->sk;

		write_lock_bh(&sk->sk_callback_lock);

		sk->sk_user_data = xprt;
		xprt->old_data_ready = sk->sk_data_ready;
		xprt->old_state_change = sk->sk_state_change;
		xprt->old_write_space = sk->sk_write_space;
		sk->sk_data_ready = xs_tcp_data_ready;
		sk->sk_state_change = xs_tcp_state_change;
		sk->sk_write_space = xs_tcp_write_space;
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

	/* Tell the socket layer to start connecting... */
	xprt->stat.connect_count++;
	xprt->stat.connect_start = jiffies;
	status = kernel_connect(sock, (struct sockaddr *) &xprt->addr,
			xprt->addrlen, O_NONBLOCK);
	dprintk("RPC: %p  connect status %d connected %d sock state %d\n",
			xprt, -status, xprt_connected(xprt), sock->sk->sk_state);
	if (status < 0) {
		switch (status) {
			case -EINPROGRESS:
			case -EALREADY:
				goto out_clear;
			case -ECONNREFUSED:
			case -ECONNRESET:
				/* retry with existing socket, after a delay */
				break;
			default:
				/* get rid of existing socket, and retry */
				xs_close(xprt);
				break;
		}
	}
out:
	xprt_wake_pending_tasks(xprt, status);
out_clear:
	xprt_clear_connecting(xprt);
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
		dprintk("RPC:      xs_connect delayed xprt %p for %lu seconds\n",
				xprt, xprt->reestablish_timeout / HZ);
		schedule_delayed_work(&xprt->connect_worker,
					xprt->reestablish_timeout);
		xprt->reestablish_timeout <<= 1;
		if (xprt->reestablish_timeout > XS_TCP_MAX_REEST_TO)
			xprt->reestablish_timeout = XS_TCP_MAX_REEST_TO;
	} else {
		dprintk("RPC:      xs_connect scheduled xprt %p\n", xprt);
		schedule_work(&xprt->connect_worker);

		/* flush_scheduled_work can sleep... */
		if (!RPC_IS_ASYNC(task))
			flush_scheduled_work();
	}
}

/**
 * xs_udp_print_stats - display UDP socket-specifc stats
 * @xprt: rpc_xprt struct containing statistics
 * @seq: output file
 *
 */
static void xs_udp_print_stats(struct rpc_xprt *xprt, struct seq_file *seq)
{
	seq_printf(seq, "\txprt:\tudp %u %lu %lu %lu %lu %Lu %Lu\n",
			xprt->port,
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
	long idle_time = 0;

	if (xprt_connected(xprt))
		idle_time = (long)(jiffies - xprt->last_used) / HZ;

	seq_printf(seq, "\txprt:\ttcp %u %lu %lu %lu %ld %lu %lu %lu %Lu %Lu\n",
			xprt->port,
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

static struct rpc_xprt_ops xs_udp_ops = {
	.set_buffer_size	= xs_udp_set_buffer_size,
	.print_addr		= xs_print_peer_address,
	.reserve_xprt		= xprt_reserve_xprt_cong,
	.release_xprt		= xprt_release_xprt_cong,
	.rpcbind		= rpc_getport,
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
	.print_addr		= xs_print_peer_address,
	.reserve_xprt		= xprt_reserve_xprt,
	.release_xprt		= xs_tcp_release_xprt,
	.rpcbind		= rpc_getport,
	.set_port		= xs_set_port,
	.connect		= xs_connect,
	.buf_alloc		= rpc_malloc,
	.buf_free		= rpc_free,
	.send_request		= xs_tcp_send_request,
	.set_retrans_timeout	= xprt_set_retrans_timeout_def,
	.close			= xs_close,
	.destroy		= xs_destroy,
	.print_stats		= xs_tcp_print_stats,
};

static struct rpc_xprt *xs_setup_xprt(struct sockaddr *addr, size_t addrlen, unsigned int slot_table_size)
{
	struct rpc_xprt *xprt;
	struct sock_xprt *new;

	if (addrlen > sizeof(xprt->addr)) {
		dprintk("RPC:      xs_setup_xprt: address too large\n");
		return ERR_PTR(-EBADF);
	}

	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (new == NULL) {
		dprintk("RPC:      xs_setup_xprt: couldn't allocate rpc_xprt\n");
		return ERR_PTR(-ENOMEM);
	}
	xprt = &new->xprt;

	xprt->max_reqs = slot_table_size;
	xprt->slot = kcalloc(xprt->max_reqs, sizeof(struct rpc_rqst), GFP_KERNEL);
	if (xprt->slot == NULL) {
		kfree(xprt);
		dprintk("RPC:      xs_setup_xprt: couldn't allocate slot table\n");
		return ERR_PTR(-ENOMEM);
	}

	memcpy(&xprt->addr, addr, addrlen);
	xprt->addrlen = addrlen;
	xprt->port = xs_get_random_port();

	return xprt;
}

/**
 * xs_setup_udp - Set up transport to use a UDP socket
 * @addr: address of remote server
 * @addrlen: length of address in bytes
 * @to:   timeout parameters
 *
 */
struct rpc_xprt *xs_setup_udp(struct sockaddr *addr, size_t addrlen, struct rpc_timeout *to)
{
	struct rpc_xprt *xprt;

	xprt = xs_setup_xprt(addr, addrlen, xprt_udp_slot_table_entries);
	if (IS_ERR(xprt))
		return xprt;

	if (ntohs(((struct sockaddr_in *)addr)->sin_port) != 0)
		xprt_set_bound(xprt);

	xprt->prot = IPPROTO_UDP;
	xprt->tsh_size = 0;
	/* XXX: header size can vary due to auth type, IPv6, etc. */
	xprt->max_payload = (1U << 16) - (MAX_HEADER << 3);

	INIT_WORK(&xprt->connect_worker, xs_udp_connect_worker, xprt);
	xprt->bind_timeout = XS_BIND_TO;
	xprt->connect_timeout = XS_UDP_CONN_TO;
	xprt->reestablish_timeout = XS_UDP_REEST_TO;
	xprt->idle_timeout = XS_IDLE_DISC_TO;

	xprt->ops = &xs_udp_ops;

	if (to)
		xprt->timeout = *to;
	else
		xprt_set_timeout(&xprt->timeout, 5, 5 * HZ);

	xs_format_peer_addresses(xprt);
	dprintk("RPC:      set up transport to address %s\n",
			xs_print_peer_address(xprt, RPC_DISPLAY_ALL));

	return xprt;
}

/**
 * xs_setup_tcp - Set up transport to use a TCP socket
 * @addr: address of remote server
 * @addrlen: length of address in bytes
 * @to: timeout parameters
 *
 */
struct rpc_xprt *xs_setup_tcp(struct sockaddr *addr, size_t addrlen, struct rpc_timeout *to)
{
	struct rpc_xprt *xprt;

	xprt = xs_setup_xprt(addr, addrlen, xprt_tcp_slot_table_entries);
	if (IS_ERR(xprt))
		return xprt;

	if (ntohs(((struct sockaddr_in *)addr)->sin_port) != 0)
		xprt_set_bound(xprt);

	xprt->prot = IPPROTO_TCP;
	xprt->tsh_size = sizeof(rpc_fraghdr) / sizeof(u32);
	xprt->max_payload = RPC_MAX_FRAGMENT_SIZE;

	INIT_WORK(&xprt->connect_worker, xs_tcp_connect_worker, xprt);
	xprt->bind_timeout = XS_BIND_TO;
	xprt->connect_timeout = XS_TCP_CONN_TO;
	xprt->reestablish_timeout = XS_TCP_INIT_REEST_TO;
	xprt->idle_timeout = XS_IDLE_DISC_TO;

	xprt->ops = &xs_tcp_ops;

	if (to)
		xprt->timeout = *to;
	else
		xprt_set_timeout(&xprt->timeout, 2, 60 * HZ);

	xs_format_peer_addresses(xprt);
	dprintk("RPC:      set up transport to address %s\n",
			xs_print_peer_address(xprt, RPC_DISPLAY_ALL));

	return xprt;
}
