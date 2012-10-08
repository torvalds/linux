/*
 * Copyright (c) 2006 Oracle.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <net/sock.h>
#include <linux/in.h>

#include "rds.h"

void rds_inc_init(struct rds_incoming *inc, struct rds_connection *conn,
		  __be32 saddr)
{
	atomic_set(&inc->i_refcount, 1);
	INIT_LIST_HEAD(&inc->i_item);
	inc->i_conn = conn;
	inc->i_saddr = saddr;
	inc->i_rdma_cookie = 0;
}
EXPORT_SYMBOL_GPL(rds_inc_init);

static void rds_inc_addref(struct rds_incoming *inc)
{
	rdsdebug("addref inc %p ref %d\n", inc, atomic_read(&inc->i_refcount));
	atomic_inc(&inc->i_refcount);
}

void rds_inc_put(struct rds_incoming *inc)
{
	rdsdebug("put inc %p ref %d\n", inc, atomic_read(&inc->i_refcount));
	if (atomic_dec_and_test(&inc->i_refcount)) {
		BUG_ON(!list_empty(&inc->i_item));

		inc->i_conn->c_trans->inc_free(inc);
	}
}
EXPORT_SYMBOL_GPL(rds_inc_put);

static void rds_recv_rcvbuf_delta(struct rds_sock *rs, struct sock *sk,
				  struct rds_cong_map *map,
				  int delta, __be16 port)
{
	int now_congested;

	if (delta == 0)
		return;

	rs->rs_rcv_bytes += delta;
	now_congested = rs->rs_rcv_bytes > rds_sk_rcvbuf(rs);

	rdsdebug("rs %p (%pI4:%u) recv bytes %d buf %d "
	  "now_cong %d delta %d\n",
	  rs, &rs->rs_bound_addr,
	  ntohs(rs->rs_bound_port), rs->rs_rcv_bytes,
	  rds_sk_rcvbuf(rs), now_congested, delta);

	/* wasn't -> am congested */
	if (!rs->rs_congested && now_congested) {
		rs->rs_congested = 1;
		rds_cong_set_bit(map, port);
		rds_cong_queue_updates(map);
	}
	/* was -> aren't congested */
	/* Require more free space before reporting uncongested to prevent
	   bouncing cong/uncong state too often */
	else if (rs->rs_congested && (rs->rs_rcv_bytes < (rds_sk_rcvbuf(rs)/2))) {
		rs->rs_congested = 0;
		rds_cong_clear_bit(map, port);
		rds_cong_queue_updates(map);
	}

	/* do nothing if no change in cong state */
}

/*
 * Process all extension headers that come with this message.
 */
static void rds_recv_incoming_exthdrs(struct rds_incoming *inc, struct rds_sock *rs)
{
	struct rds_header *hdr = &inc->i_hdr;
	unsigned int pos = 0, type, len;
	union {
		struct rds_ext_header_version version;
		struct rds_ext_header_rdma rdma;
		struct rds_ext_header_rdma_dest rdma_dest;
	} buffer;

	while (1) {
		len = sizeof(buffer);
		type = rds_message_next_extension(hdr, &pos, &buffer, &len);
		if (type == RDS_EXTHDR_NONE)
			break;
		/* Process extension header here */
		switch (type) {
		case RDS_EXTHDR_RDMA:
			rds_rdma_unuse(rs, be32_to_cpu(buffer.rdma.h_rdma_rkey), 0);
			break;

		case RDS_EXTHDR_RDMA_DEST:
			/* We ignore the size for now. We could stash it
			 * somewhere and use it for error checking. */
			inc->i_rdma_cookie = rds_rdma_make_cookie(
					be32_to_cpu(buffer.rdma_dest.h_rdma_rkey),
					be32_to_cpu(buffer.rdma_dest.h_rdma_offset));

			break;
		}
	}
}

/*
 * The transport must make sure that this is serialized against other
 * rx and conn reset on this specific conn.
 *
 * We currently assert that only one fragmented message will be sent
 * down a connection at a time.  This lets us reassemble in the conn
 * instead of per-flow which means that we don't have to go digging through
 * flows to tear down partial reassembly progress on conn failure and
 * we save flow lookup and locking for each frag arrival.  It does mean
 * that small messages will wait behind large ones.  Fragmenting at all
 * is only to reduce the memory consumption of pre-posted buffers.
 *
 * The caller passes in saddr and daddr instead of us getting it from the
 * conn.  This lets loopback, who only has one conn for both directions,
 * tell us which roles the addrs in the conn are playing for this message.
 */
void rds_recv_incoming(struct rds_connection *conn, __be32 saddr, __be32 daddr,
		       struct rds_incoming *inc, gfp_t gfp, enum km_type km)
{
	struct rds_sock *rs = NULL;
	struct sock *sk;
	unsigned long flags;

	inc->i_conn = conn;
	inc->i_rx_jiffies = jiffies;

	rdsdebug("conn %p next %llu inc %p seq %llu len %u sport %u dport %u "
		 "flags 0x%x rx_jiffies %lu\n", conn,
		 (unsigned long long)conn->c_next_rx_seq,
		 inc,
		 (unsigned long long)be64_to_cpu(inc->i_hdr.h_sequence),
		 be32_to_cpu(inc->i_hdr.h_len),
		 be16_to_cpu(inc->i_hdr.h_sport),
		 be16_to_cpu(inc->i_hdr.h_dport),
		 inc->i_hdr.h_flags,
		 inc->i_rx_jiffies);

	/*
	 * Sequence numbers should only increase.  Messages get their
	 * sequence number as they're queued in a sending conn.  They
	 * can be dropped, though, if the sending socket is closed before
	 * they hit the wire.  So sequence numbers can skip forward
	 * under normal operation.  They can also drop back in the conn
	 * failover case as previously sent messages are resent down the
	 * new instance of a conn.  We drop those, otherwise we have
	 * to assume that the next valid seq does not come after a
	 * hole in the fragment stream.
	 *
	 * The headers don't give us a way to realize if fragments of
	 * a message have been dropped.  We assume that frags that arrive
	 * to a flow are part of the current message on the flow that is
	 * being reassembled.  This means that senders can't drop messages
	 * from the sending conn until all their frags are sent.
	 *
	 * XXX we could spend more on the wire to get more robust failure
	 * detection, arguably worth it to avoid data corruption.
	 */
	if (be64_to_cpu(inc->i_hdr.h_sequence) < conn->c_next_rx_seq &&
	    (inc->i_hdr.h_flags & RDS_FLAG_RETRANSMITTED)) {
		rds_stats_inc(s_recv_drop_old_seq);
		goto out;
	}
	conn->c_next_rx_seq = be64_to_cpu(inc->i_hdr.h_sequence) + 1;

	if (rds_sysctl_ping_enable && inc->i_hdr.h_dport == 0) {
		rds_stats_inc(s_recv_ping);
		rds_send_pong(conn, inc->i_hdr.h_sport);
		goto out;
	}

	rs = rds_find_bound(daddr, inc->i_hdr.h_dport);
	if (!rs) {
		rds_stats_inc(s_recv_drop_no_sock);
		goto out;
	}

	/* Process extension headers */
	rds_recv_incoming_exthdrs(inc, rs);

	/* We can be racing with rds_release() which marks the socket dead. */
	sk = rds_rs_to_sk(rs);

	/* serialize with rds_release -> sock_orphan */
	write_lock_irqsave(&rs->rs_recv_lock, flags);
	if (!sock_flag(sk, SOCK_DEAD)) {
		rdsdebug("adding inc %p to rs %p's recv queue\n", inc, rs);
		rds_stats_inc(s_recv_queued);
		rds_recv_rcvbuf_delta(rs, sk, inc->i_conn->c_lcong,
				      be32_to_cpu(inc->i_hdr.h_len),
				      inc->i_hdr.h_dport);
		rds_inc_addref(inc);
		list_add_tail(&inc->i_item, &rs->rs_recv_queue);
		__rds_wake_sk_sleep(sk);
	} else {
		rds_stats_inc(s_recv_drop_dead_sock);
	}
	write_unlock_irqrestore(&rs->rs_recv_lock, flags);

out:
	if (rs)
		rds_sock_put(rs);
}
EXPORT_SYMBOL_GPL(rds_recv_incoming);

/*
 * be very careful here.  This is being called as the condition in
 * wait_event_*() needs to cope with being called many times.
 */
static int rds_next_incoming(struct rds_sock *rs, struct rds_incoming **inc)
{
	unsigned long flags;

	if (!*inc) {
		read_lock_irqsave(&rs->rs_recv_lock, flags);
		if (!list_empty(&rs->rs_recv_queue)) {
			*inc = list_entry(rs->rs_recv_queue.next,
					  struct rds_incoming,
					  i_item);
			rds_inc_addref(*inc);
		}
		read_unlock_irqrestore(&rs->rs_recv_lock, flags);
	}

	return *inc != NULL;
}

static int rds_still_queued(struct rds_sock *rs, struct rds_incoming *inc,
			    int drop)
{
	struct sock *sk = rds_rs_to_sk(rs);
	int ret = 0;
	unsigned long flags;

	write_lock_irqsave(&rs->rs_recv_lock, flags);
	if (!list_empty(&inc->i_item)) {
		ret = 1;
		if (drop) {
			/* XXX make sure this i_conn is reliable */
			rds_recv_rcvbuf_delta(rs, sk, inc->i_conn->c_lcong,
					      -be32_to_cpu(inc->i_hdr.h_len),
					      inc->i_hdr.h_dport);
			list_del_init(&inc->i_item);
			rds_inc_put(inc);
		}
	}
	write_unlock_irqrestore(&rs->rs_recv_lock, flags);

	rdsdebug("inc %p rs %p still %d dropped %d\n", inc, rs, ret, drop);
	return ret;
}

/*
 * Pull errors off the error queue.
 * If msghdr is NULL, we will just purge the error queue.
 */
int rds_notify_queue_get(struct rds_sock *rs, struct msghdr *msghdr)
{
	struct rds_notifier *notifier;
	struct rds_rdma_notify cmsg = { 0 }; /* fill holes with zero */
	unsigned int count = 0, max_messages = ~0U;
	unsigned long flags;
	LIST_HEAD(copy);
	int err = 0;


	/* put_cmsg copies to user space and thus may sleep. We can't do this
	 * with rs_lock held, so first grab as many notifications as we can stuff
	 * in the user provided cmsg buffer. We don't try to copy more, to avoid
	 * losing notifications - except when the buffer is so small that it wouldn't
	 * even hold a single notification. Then we give him as much of this single
	 * msg as we can squeeze in, and set MSG_CTRUNC.
	 */
	if (msghdr) {
		max_messages = msghdr->msg_controllen / CMSG_SPACE(sizeof(cmsg));
		if (!max_messages)
			max_messages = 1;
	}

	spin_lock_irqsave(&rs->rs_lock, flags);
	while (!list_empty(&rs->rs_notify_queue) && count < max_messages) {
		notifier = list_entry(rs->rs_notify_queue.next,
				struct rds_notifier, n_list);
		list_move(&notifier->n_list, &copy);
		count++;
	}
	spin_unlock_irqrestore(&rs->rs_lock, flags);

	if (!count)
		return 0;

	while (!list_empty(&copy)) {
		notifier = list_entry(copy.next, struct rds_notifier, n_list);

		if (msghdr) {
			cmsg.user_token = notifier->n_user_token;
			cmsg.status = notifier->n_status;

			err = put_cmsg(msghdr, SOL_RDS, RDS_CMSG_RDMA_STATUS,
				       sizeof(cmsg), &cmsg);
			if (err)
				break;
		}

		list_del_init(&notifier->n_list);
		kfree(notifier);
	}

	/* If we bailed out because of an error in put_cmsg,
	 * we may be left with one or more notifications that we
	 * didn't process. Return them to the head of the list. */
	if (!list_empty(&copy)) {
		spin_lock_irqsave(&rs->rs_lock, flags);
		list_splice(&copy, &rs->rs_notify_queue);
		spin_unlock_irqrestore(&rs->rs_lock, flags);
	}

	return err;
}

/*
 * Queue a congestion notification
 */
static int rds_notify_cong(struct rds_sock *rs, struct msghdr *msghdr)
{
	uint64_t notify = rs->rs_cong_notify;
	unsigned long flags;
	int err;

	err = put_cmsg(msghdr, SOL_RDS, RDS_CMSG_CONG_UPDATE,
			sizeof(notify), &notify);
	if (err)
		return err;

	spin_lock_irqsave(&rs->rs_lock, flags);
	rs->rs_cong_notify &= ~notify;
	spin_unlock_irqrestore(&rs->rs_lock, flags);

	return 0;
}

/*
 * Receive any control messages.
 */
static int rds_cmsg_recv(struct rds_incoming *inc, struct msghdr *msg)
{
	int ret = 0;

	if (inc->i_rdma_cookie) {
		ret = put_cmsg(msg, SOL_RDS, RDS_CMSG_RDMA_DEST,
				sizeof(inc->i_rdma_cookie), &inc->i_rdma_cookie);
		if (ret)
			return ret;
	}

	return 0;
}

int rds_recvmsg(struct kiocb *iocb, struct socket *sock, struct msghdr *msg,
		size_t size, int msg_flags)
{
	struct sock *sk = sock->sk;
	struct rds_sock *rs = rds_sk_to_rs(sk);
	long timeo;
	int ret = 0, nonblock = msg_flags & MSG_DONTWAIT;
	struct sockaddr_in *sin;
	struct rds_incoming *inc = NULL;

	/* udp_recvmsg()->sock_recvtimeo() gets away without locking too.. */
	timeo = sock_rcvtimeo(sk, nonblock);

	rdsdebug("size %zu flags 0x%x timeo %ld\n", size, msg_flags, timeo);

	msg->msg_namelen = 0;

	if (msg_flags & MSG_OOB)
		goto out;

	while (1) {
		/* If there are pending notifications, do those - and nothing else */
		if (!list_empty(&rs->rs_notify_queue)) {
			ret = rds_notify_queue_get(rs, msg);
			break;
		}

		if (rs->rs_cong_notify) {
			ret = rds_notify_cong(rs, msg);
			break;
		}

		if (!rds_next_incoming(rs, &inc)) {
			if (nonblock) {
				ret = -EAGAIN;
				break;
			}

			timeo = wait_event_interruptible_timeout(*sk_sleep(sk),
					(!list_empty(&rs->rs_notify_queue) ||
					 rs->rs_cong_notify ||
					 rds_next_incoming(rs, &inc)), timeo);
			rdsdebug("recvmsg woke inc %p timeo %ld\n", inc,
				 timeo);
			if (timeo > 0 || timeo == MAX_SCHEDULE_TIMEOUT)
				continue;

			ret = timeo;
			if (ret == 0)
				ret = -ETIMEDOUT;
			break;
		}

		rdsdebug("copying inc %p from %pI4:%u to user\n", inc,
			 &inc->i_conn->c_faddr,
			 ntohs(inc->i_hdr.h_sport));
		ret = inc->i_conn->c_trans->inc_copy_to_user(inc, msg->msg_iov,
							     size);
		if (ret < 0)
			break;

		/*
		 * if the message we just copied isn't at the head of the
		 * recv queue then someone else raced us to return it, try
		 * to get the next message.
		 */
		if (!rds_still_queued(rs, inc, !(msg_flags & MSG_PEEK))) {
			rds_inc_put(inc);
			inc = NULL;
			rds_stats_inc(s_recv_deliver_raced);
			continue;
		}

		if (ret < be32_to_cpu(inc->i_hdr.h_len)) {
			if (msg_flags & MSG_TRUNC)
				ret = be32_to_cpu(inc->i_hdr.h_len);
			msg->msg_flags |= MSG_TRUNC;
		}

		if (rds_cmsg_recv(inc, msg)) {
			ret = -EFAULT;
			goto out;
		}

		rds_stats_inc(s_recv_delivered);

		sin = (struct sockaddr_in *)msg->msg_name;
		if (sin) {
			sin->sin_family = AF_INET;
			sin->sin_port = inc->i_hdr.h_sport;
			sin->sin_addr.s_addr = inc->i_saddr;
			memset(sin->sin_zero, 0, sizeof(sin->sin_zero));
			msg->msg_namelen = sizeof(*sin);
		}
		break;
	}

	if (inc)
		rds_inc_put(inc);

out:
	return ret;
}

/*
 * The socket is being shut down and we're asked to drop messages that were
 * queued for recvmsg.  The caller has unbound the socket so the receive path
 * won't queue any more incoming fragments or messages on the socket.
 */
void rds_clear_recv_queue(struct rds_sock *rs)
{
	struct sock *sk = rds_rs_to_sk(rs);
	struct rds_incoming *inc, *tmp;
	unsigned long flags;

	write_lock_irqsave(&rs->rs_recv_lock, flags);
	list_for_each_entry_safe(inc, tmp, &rs->rs_recv_queue, i_item) {
		rds_recv_rcvbuf_delta(rs, sk, inc->i_conn->c_lcong,
				      -be32_to_cpu(inc->i_hdr.h_len),
				      inc->i_hdr.h_dport);
		list_del_init(&inc->i_item);
		rds_inc_put(inc);
	}
	write_unlock_irqrestore(&rs->rs_recv_lock, flags);
}

/*
 * inc->i_saddr isn't used here because it is only set in the receive
 * path.
 */
void rds_inc_info_copy(struct rds_incoming *inc,
		       struct rds_info_iterator *iter,
		       __be32 saddr, __be32 daddr, int flip)
{
	struct rds_info_message minfo;

	minfo.seq = be64_to_cpu(inc->i_hdr.h_sequence);
	minfo.len = be32_to_cpu(inc->i_hdr.h_len);

	if (flip) {
		minfo.laddr = daddr;
		minfo.faddr = saddr;
		minfo.lport = inc->i_hdr.h_dport;
		minfo.fport = inc->i_hdr.h_sport;
	} else {
		minfo.laddr = saddr;
		minfo.faddr = daddr;
		minfo.lport = inc->i_hdr.h_sport;
		minfo.fport = inc->i_hdr.h_dport;
	}

	rds_info_copy(iter, &minfo, sizeof(minfo));
}
