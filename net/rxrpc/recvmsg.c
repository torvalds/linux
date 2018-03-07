/* RxRPC recvmsg() implementation
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/export.h>
#include <linux/sched/signal.h>

#include <net/sock.h>
#include <net/af_rxrpc.h>
#include "ar-internal.h"

/*
 * Post a call for attention by the socket or kernel service.  Further
 * notifications are suppressed by putting recvmsg_link on a dummy queue.
 */
void rxrpc_notify_socket(struct rxrpc_call *call)
{
	struct rxrpc_sock *rx;
	struct sock *sk;

	_enter("%d", call->debug_id);

	if (!list_empty(&call->recvmsg_link))
		return;

	rcu_read_lock();

	rx = rcu_dereference(call->socket);
	sk = &rx->sk;
	if (rx && sk->sk_state < RXRPC_CLOSE) {
		if (call->notify_rx) {
			spin_lock_bh(&call->notify_lock);
			call->notify_rx(sk, call, call->user_call_ID);
			spin_unlock_bh(&call->notify_lock);
		} else {
			write_lock_bh(&rx->recvmsg_lock);
			if (list_empty(&call->recvmsg_link)) {
				rxrpc_get_call(call, rxrpc_call_got);
				list_add_tail(&call->recvmsg_link, &rx->recvmsg_q);
			}
			write_unlock_bh(&rx->recvmsg_lock);

			if (!sock_flag(sk, SOCK_DEAD)) {
				_debug("call %ps", sk->sk_data_ready);
				sk->sk_data_ready(sk);
			}
		}
	}

	rcu_read_unlock();
	_leave("");
}

/*
 * Pass a call terminating message to userspace.
 */
static int rxrpc_recvmsg_term(struct rxrpc_call *call, struct msghdr *msg)
{
	u32 tmp = 0;
	int ret;

	switch (call->completion) {
	case RXRPC_CALL_SUCCEEDED:
		ret = 0;
		if (rxrpc_is_service_call(call))
			ret = put_cmsg(msg, SOL_RXRPC, RXRPC_ACK, 0, &tmp);
		break;
	case RXRPC_CALL_REMOTELY_ABORTED:
		tmp = call->abort_code;
		ret = put_cmsg(msg, SOL_RXRPC, RXRPC_ABORT, 4, &tmp);
		break;
	case RXRPC_CALL_LOCALLY_ABORTED:
		tmp = call->abort_code;
		ret = put_cmsg(msg, SOL_RXRPC, RXRPC_ABORT, 4, &tmp);
		break;
	case RXRPC_CALL_NETWORK_ERROR:
		tmp = -call->error;
		ret = put_cmsg(msg, SOL_RXRPC, RXRPC_NET_ERROR, 4, &tmp);
		break;
	case RXRPC_CALL_LOCAL_ERROR:
		tmp = -call->error;
		ret = put_cmsg(msg, SOL_RXRPC, RXRPC_LOCAL_ERROR, 4, &tmp);
		break;
	default:
		pr_err("Invalid terminal call state %u\n", call->state);
		BUG();
		break;
	}

	trace_rxrpc_recvmsg(call, rxrpc_recvmsg_terminal, call->rx_hard_ack,
			    call->rx_pkt_offset, call->rx_pkt_len, ret);
	return ret;
}

/*
 * Pass back notification of a new call.  The call is added to the
 * to-be-accepted list.  This means that the next call to be accepted might not
 * be the last call seen awaiting acceptance, but unless we leave this on the
 * front of the queue and block all other messages until someone gives us a
 * user_ID for it, there's not a lot we can do.
 */
static int rxrpc_recvmsg_new_call(struct rxrpc_sock *rx,
				  struct rxrpc_call *call,
				  struct msghdr *msg, int flags)
{
	int tmp = 0, ret;

	ret = put_cmsg(msg, SOL_RXRPC, RXRPC_NEW_CALL, 0, &tmp);

	if (ret == 0 && !(flags & MSG_PEEK)) {
		_debug("to be accepted");
		write_lock_bh(&rx->recvmsg_lock);
		list_del_init(&call->recvmsg_link);
		write_unlock_bh(&rx->recvmsg_lock);

		rxrpc_get_call(call, rxrpc_call_got);
		write_lock(&rx->call_lock);
		list_add_tail(&call->accept_link, &rx->to_be_accepted);
		write_unlock(&rx->call_lock);
	}

	trace_rxrpc_recvmsg(call, rxrpc_recvmsg_to_be_accepted, 1, 0, 0, ret);
	return ret;
}

/*
 * End the packet reception phase.
 */
static void rxrpc_end_rx_phase(struct rxrpc_call *call, rxrpc_serial_t serial)
{
	_enter("%d,%s", call->debug_id, rxrpc_call_states[call->state]);

	trace_rxrpc_receive(call, rxrpc_receive_end, 0, call->rx_top);
	ASSERTCMP(call->rx_hard_ack, ==, call->rx_top);

#if 0 // TODO: May want to transmit final ACK under some circumstances anyway
	if (call->state == RXRPC_CALL_CLIENT_RECV_REPLY) {
		rxrpc_propose_ACK(call, RXRPC_ACK_IDLE, 0, serial, true, false,
				  rxrpc_propose_ack_terminal_ack);
		rxrpc_send_ack_packet(call, false, NULL);
	}
#endif

	write_lock_bh(&call->state_lock);

	switch (call->state) {
	case RXRPC_CALL_CLIENT_RECV_REPLY:
		__rxrpc_call_completed(call);
		write_unlock_bh(&call->state_lock);
		break;

	case RXRPC_CALL_SERVER_RECV_REQUEST:
		call->tx_phase = true;
		call->state = RXRPC_CALL_SERVER_ACK_REQUEST;
		call->expect_req_by = jiffies + MAX_JIFFY_OFFSET;
		write_unlock_bh(&call->state_lock);
		rxrpc_propose_ACK(call, RXRPC_ACK_DELAY, 0, serial, false, true,
				  rxrpc_propose_ack_processing_op);
		break;
	default:
		write_unlock_bh(&call->state_lock);
		break;
	}
}

/*
 * Discard a packet we've used up and advance the Rx window by one.
 */
static void rxrpc_rotate_rx_window(struct rxrpc_call *call)
{
	struct rxrpc_skb_priv *sp;
	struct sk_buff *skb;
	rxrpc_serial_t serial;
	rxrpc_seq_t hard_ack, top;
	u8 flags;
	int ix;

	_enter("%d", call->debug_id);

	hard_ack = call->rx_hard_ack;
	top = smp_load_acquire(&call->rx_top);
	ASSERT(before(hard_ack, top));

	hard_ack++;
	ix = hard_ack & RXRPC_RXTX_BUFF_MASK;
	skb = call->rxtx_buffer[ix];
	rxrpc_see_skb(skb, rxrpc_skb_rx_rotated);
	sp = rxrpc_skb(skb);
	flags = sp->hdr.flags;
	serial = sp->hdr.serial;
	if (call->rxtx_annotations[ix] & RXRPC_RX_ANNO_JUMBO)
		serial += (call->rxtx_annotations[ix] & RXRPC_RX_ANNO_JUMBO) - 1;

	call->rxtx_buffer[ix] = NULL;
	call->rxtx_annotations[ix] = 0;
	/* Barrier against rxrpc_input_data(). */
	smp_store_release(&call->rx_hard_ack, hard_ack);

	rxrpc_free_skb(skb, rxrpc_skb_rx_freed);

	_debug("%u,%u,%02x", hard_ack, top, flags);
	trace_rxrpc_receive(call, rxrpc_receive_rotate, serial, hard_ack);
	if (flags & RXRPC_LAST_PACKET) {
		rxrpc_end_rx_phase(call, serial);
	} else {
		/* Check to see if there's an ACK that needs sending. */
		if (after_eq(hard_ack, call->ackr_consumed + 2) ||
		    after_eq(top, call->ackr_seen + 2) ||
		    (hard_ack == top && after(hard_ack, call->ackr_consumed)))
			rxrpc_propose_ACK(call, RXRPC_ACK_DELAY, 0, serial,
					  true, true,
					  rxrpc_propose_ack_rotate_rx);
		if (call->ackr_reason && call->ackr_reason != RXRPC_ACK_DELAY)
			rxrpc_send_ack_packet(call, false, NULL);
	}
}

/*
 * Decrypt and verify a (sub)packet.  The packet's length may be changed due to
 * padding, but if this is the case, the packet length will be resident in the
 * socket buffer.  Note that we can't modify the master skb info as the skb may
 * be the home to multiple subpackets.
 */
static int rxrpc_verify_packet(struct rxrpc_call *call, struct sk_buff *skb,
			       u8 annotation,
			       unsigned int offset, unsigned int len)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	rxrpc_seq_t seq = sp->hdr.seq;
	u16 cksum = sp->hdr.cksum;

	_enter("");

	/* For all but the head jumbo subpacket, the security checksum is in a
	 * jumbo header immediately prior to the data.
	 */
	if ((annotation & RXRPC_RX_ANNO_JUMBO) > 1) {
		__be16 tmp;
		if (skb_copy_bits(skb, offset - 2, &tmp, 2) < 0)
			BUG();
		cksum = ntohs(tmp);
		seq += (annotation & RXRPC_RX_ANNO_JUMBO) - 1;
	}

	return call->conn->security->verify_packet(call, skb, offset, len,
						   seq, cksum);
}

/*
 * Locate the data within a packet.  This is complicated by:
 *
 * (1) An skb may contain a jumbo packet - so we have to find the appropriate
 *     subpacket.
 *
 * (2) The (sub)packets may be encrypted and, if so, the encrypted portion
 *     contains an extra header which includes the true length of the data,
 *     excluding any encrypted padding.
 */
static int rxrpc_locate_data(struct rxrpc_call *call, struct sk_buff *skb,
			     u8 *_annotation,
			     unsigned int *_offset, unsigned int *_len)
{
	unsigned int offset = sizeof(struct rxrpc_wire_header);
	unsigned int len = *_len;
	int ret;
	u8 annotation = *_annotation;

	/* Locate the subpacket */
	len = skb->len - offset;
	if ((annotation & RXRPC_RX_ANNO_JUMBO) > 0) {
		offset += (((annotation & RXRPC_RX_ANNO_JUMBO) - 1) *
			   RXRPC_JUMBO_SUBPKTLEN);
		len = (annotation & RXRPC_RX_ANNO_JLAST) ?
			skb->len - offset : RXRPC_JUMBO_SUBPKTLEN;
	}

	if (!(annotation & RXRPC_RX_ANNO_VERIFIED)) {
		ret = rxrpc_verify_packet(call, skb, annotation, offset, len);
		if (ret < 0)
			return ret;
		*_annotation |= RXRPC_RX_ANNO_VERIFIED;
	}

	*_offset = offset;
	*_len = len;
	call->conn->security->locate_data(call, skb, _offset, _len);
	return 0;
}

/*
 * Deliver messages to a call.  This keeps processing packets until the buffer
 * is filled and we find either more DATA (returns 0) or the end of the DATA
 * (returns 1).  If more packets are required, it returns -EAGAIN.
 */
static int rxrpc_recvmsg_data(struct socket *sock, struct rxrpc_call *call,
			      struct msghdr *msg, struct iov_iter *iter,
			      size_t len, int flags, size_t *_offset)
{
	struct rxrpc_skb_priv *sp;
	struct sk_buff *skb;
	rxrpc_seq_t hard_ack, top, seq;
	size_t remain;
	bool last;
	unsigned int rx_pkt_offset, rx_pkt_len;
	int ix, copy, ret = -EAGAIN, ret2;

	rx_pkt_offset = call->rx_pkt_offset;
	rx_pkt_len = call->rx_pkt_len;

	if (call->state >= RXRPC_CALL_SERVER_ACK_REQUEST) {
		seq = call->rx_hard_ack;
		ret = 1;
		goto done;
	}

	/* Barriers against rxrpc_input_data(). */
	hard_ack = call->rx_hard_ack;
	seq = hard_ack + 1;
	while (top = smp_load_acquire(&call->rx_top),
	       before_eq(seq, top)
	       ) {
		ix = seq & RXRPC_RXTX_BUFF_MASK;
		skb = call->rxtx_buffer[ix];
		if (!skb) {
			trace_rxrpc_recvmsg(call, rxrpc_recvmsg_hole, seq,
					    rx_pkt_offset, rx_pkt_len, 0);
			break;
		}
		smp_rmb();
		rxrpc_see_skb(skb, rxrpc_skb_rx_seen);
		sp = rxrpc_skb(skb);

		if (!(flags & MSG_PEEK))
			trace_rxrpc_receive(call, rxrpc_receive_front,
					    sp->hdr.serial, seq);

		if (msg)
			sock_recv_timestamp(msg, sock->sk, skb);

		if (rx_pkt_offset == 0) {
			ret2 = rxrpc_locate_data(call, skb,
						 &call->rxtx_annotations[ix],
						 &rx_pkt_offset, &rx_pkt_len);
			trace_rxrpc_recvmsg(call, rxrpc_recvmsg_next, seq,
					    rx_pkt_offset, rx_pkt_len, ret2);
			if (ret2 < 0) {
				ret = ret2;
				goto out;
			}
		} else {
			trace_rxrpc_recvmsg(call, rxrpc_recvmsg_cont, seq,
					    rx_pkt_offset, rx_pkt_len, 0);
		}

		/* We have to handle short, empty and used-up DATA packets. */
		remain = len - *_offset;
		copy = rx_pkt_len;
		if (copy > remain)
			copy = remain;
		if (copy > 0) {
			ret2 = skb_copy_datagram_iter(skb, rx_pkt_offset, iter,
						      copy);
			if (ret2 < 0) {
				ret = ret2;
				goto out;
			}

			/* handle piecemeal consumption of data packets */
			rx_pkt_offset += copy;
			rx_pkt_len -= copy;
			*_offset += copy;
		}

		if (rx_pkt_len > 0) {
			trace_rxrpc_recvmsg(call, rxrpc_recvmsg_full, seq,
					    rx_pkt_offset, rx_pkt_len, 0);
			ASSERTCMP(*_offset, ==, len);
			ret = 0;
			break;
		}

		/* The whole packet has been transferred. */
		last = sp->hdr.flags & RXRPC_LAST_PACKET;
		if (!(flags & MSG_PEEK))
			rxrpc_rotate_rx_window(call);
		rx_pkt_offset = 0;
		rx_pkt_len = 0;

		if (last) {
			ASSERTCMP(seq, ==, READ_ONCE(call->rx_top));
			ret = 1;
			goto out;
		}

		seq++;
	}

out:
	if (!(flags & MSG_PEEK)) {
		call->rx_pkt_offset = rx_pkt_offset;
		call->rx_pkt_len = rx_pkt_len;
	}
done:
	trace_rxrpc_recvmsg(call, rxrpc_recvmsg_data_return, seq,
			    rx_pkt_offset, rx_pkt_len, ret);
	return ret;
}

/*
 * Receive a message from an RxRPC socket
 * - we need to be careful about two or more threads calling recvmsg
 *   simultaneously
 */
int rxrpc_recvmsg(struct socket *sock, struct msghdr *msg, size_t len,
		  int flags)
{
	struct rxrpc_call *call;
	struct rxrpc_sock *rx = rxrpc_sk(sock->sk);
	struct list_head *l;
	size_t copied = 0;
	long timeo;
	int ret;

	DEFINE_WAIT(wait);

	trace_rxrpc_recvmsg(NULL, rxrpc_recvmsg_enter, 0, 0, 0, 0);

	if (flags & (MSG_OOB | MSG_TRUNC))
		return -EOPNOTSUPP;

	timeo = sock_rcvtimeo(&rx->sk, flags & MSG_DONTWAIT);

try_again:
	lock_sock(&rx->sk);

	/* Return immediately if a client socket has no outstanding calls */
	if (RB_EMPTY_ROOT(&rx->calls) &&
	    list_empty(&rx->recvmsg_q) &&
	    rx->sk.sk_state != RXRPC_SERVER_LISTENING) {
		release_sock(&rx->sk);
		return -ENODATA;
	}

	if (list_empty(&rx->recvmsg_q)) {
		ret = -EWOULDBLOCK;
		if (timeo == 0) {
			call = NULL;
			goto error_no_call;
		}

		release_sock(&rx->sk);

		/* Wait for something to happen */
		prepare_to_wait_exclusive(sk_sleep(&rx->sk), &wait,
					  TASK_INTERRUPTIBLE);
		ret = sock_error(&rx->sk);
		if (ret)
			goto wait_error;

		if (list_empty(&rx->recvmsg_q)) {
			if (signal_pending(current))
				goto wait_interrupted;
			trace_rxrpc_recvmsg(NULL, rxrpc_recvmsg_wait,
					    0, 0, 0, 0);
			timeo = schedule_timeout(timeo);
		}
		finish_wait(sk_sleep(&rx->sk), &wait);
		goto try_again;
	}

	/* Find the next call and dequeue it if we're not just peeking.  If we
	 * do dequeue it, that comes with a ref that we will need to release.
	 */
	write_lock_bh(&rx->recvmsg_lock);
	l = rx->recvmsg_q.next;
	call = list_entry(l, struct rxrpc_call, recvmsg_link);
	if (!(flags & MSG_PEEK))
		list_del_init(&call->recvmsg_link);
	else
		rxrpc_get_call(call, rxrpc_call_got);
	write_unlock_bh(&rx->recvmsg_lock);

	trace_rxrpc_recvmsg(call, rxrpc_recvmsg_dequeue, 0, 0, 0, 0);

	/* We're going to drop the socket lock, so we need to lock the call
	 * against interference by sendmsg.
	 */
	if (!mutex_trylock(&call->user_mutex)) {
		ret = -EWOULDBLOCK;
		if (flags & MSG_DONTWAIT)
			goto error_requeue_call;
		ret = -ERESTARTSYS;
		if (mutex_lock_interruptible(&call->user_mutex) < 0)
			goto error_requeue_call;
	}

	release_sock(&rx->sk);

	if (test_bit(RXRPC_CALL_RELEASED, &call->flags))
		BUG();

	if (test_bit(RXRPC_CALL_HAS_USERID, &call->flags)) {
		if (flags & MSG_CMSG_COMPAT) {
			unsigned int id32 = call->user_call_ID;

			ret = put_cmsg(msg, SOL_RXRPC, RXRPC_USER_CALL_ID,
				       sizeof(unsigned int), &id32);
		} else {
			ret = put_cmsg(msg, SOL_RXRPC, RXRPC_USER_CALL_ID,
				       sizeof(unsigned long),
				       &call->user_call_ID);
		}
		if (ret < 0)
			goto error_unlock_call;
	}

	if (msg->msg_name) {
		struct sockaddr_rxrpc *srx = msg->msg_name;
		size_t len = sizeof(call->peer->srx);

		memcpy(msg->msg_name, &call->peer->srx, len);
		srx->srx_service = call->service_id;
		msg->msg_namelen = len;
	}

	switch (READ_ONCE(call->state)) {
	case RXRPC_CALL_SERVER_ACCEPTING:
		ret = rxrpc_recvmsg_new_call(rx, call, msg, flags);
		break;
	case RXRPC_CALL_CLIENT_RECV_REPLY:
	case RXRPC_CALL_SERVER_RECV_REQUEST:
	case RXRPC_CALL_SERVER_ACK_REQUEST:
		ret = rxrpc_recvmsg_data(sock, call, msg, &msg->msg_iter, len,
					 flags, &copied);
		if (ret == -EAGAIN)
			ret = 0;

		if (after(call->rx_top, call->rx_hard_ack) &&
		    call->rxtx_buffer[(call->rx_hard_ack + 1) & RXRPC_RXTX_BUFF_MASK])
			rxrpc_notify_socket(call);
		break;
	default:
		ret = 0;
		break;
	}

	if (ret < 0)
		goto error_unlock_call;

	if (call->state == RXRPC_CALL_COMPLETE) {
		ret = rxrpc_recvmsg_term(call, msg);
		if (ret < 0)
			goto error_unlock_call;
		if (!(flags & MSG_PEEK))
			rxrpc_release_call(rx, call);
		msg->msg_flags |= MSG_EOR;
		ret = 1;
	}

	if (ret == 0)
		msg->msg_flags |= MSG_MORE;
	else
		msg->msg_flags &= ~MSG_MORE;
	ret = copied;

error_unlock_call:
	mutex_unlock(&call->user_mutex);
	rxrpc_put_call(call, rxrpc_call_put);
	trace_rxrpc_recvmsg(call, rxrpc_recvmsg_return, 0, 0, 0, ret);
	return ret;

error_requeue_call:
	if (!(flags & MSG_PEEK)) {
		write_lock_bh(&rx->recvmsg_lock);
		list_add(&call->recvmsg_link, &rx->recvmsg_q);
		write_unlock_bh(&rx->recvmsg_lock);
		trace_rxrpc_recvmsg(call, rxrpc_recvmsg_requeue, 0, 0, 0, 0);
	} else {
		rxrpc_put_call(call, rxrpc_call_put);
	}
error_no_call:
	release_sock(&rx->sk);
	trace_rxrpc_recvmsg(call, rxrpc_recvmsg_return, 0, 0, 0, ret);
	return ret;

wait_interrupted:
	ret = sock_intr_errno(timeo);
wait_error:
	finish_wait(sk_sleep(&rx->sk), &wait);
	call = NULL;
	goto error_no_call;
}

/**
 * rxrpc_kernel_recv_data - Allow a kernel service to receive data/info
 * @sock: The socket that the call exists on
 * @call: The call to send data through
 * @buf: The buffer to receive into
 * @size: The size of the buffer, including data already read
 * @_offset: The running offset into the buffer.
 * @want_more: True if more data is expected to be read
 * @_abort: Where the abort code is stored if -ECONNABORTED is returned
 * @_service: Where to store the actual service ID (may be upgraded)
 *
 * Allow a kernel service to receive data and pick up information about the
 * state of a call.  Returns 0 if got what was asked for and there's more
 * available, 1 if we got what was asked for and we're at the end of the data
 * and -EAGAIN if we need more data.
 *
 * Note that we may return -EAGAIN to drain empty packets at the end of the
 * data, even if we've already copied over the requested data.
 *
 * This function adds the amount it transfers to *_offset, so this should be
 * precleared as appropriate.  Note that the amount remaining in the buffer is
 * taken to be size - *_offset.
 *
 * *_abort should also be initialised to 0.
 */
int rxrpc_kernel_recv_data(struct socket *sock, struct rxrpc_call *call,
			   void *buf, size_t size, size_t *_offset,
			   bool want_more, u32 *_abort, u16 *_service)
{
	struct iov_iter iter;
	struct kvec iov;
	int ret;

	_enter("{%d,%s},%zu/%zu,%d",
	       call->debug_id, rxrpc_call_states[call->state],
	       *_offset, size, want_more);

	ASSERTCMP(*_offset, <=, size);
	ASSERTCMP(call->state, !=, RXRPC_CALL_SERVER_ACCEPTING);

	iov.iov_base = buf + *_offset;
	iov.iov_len = size - *_offset;
	iov_iter_kvec(&iter, ITER_KVEC | READ, &iov, 1, size - *_offset);

	mutex_lock(&call->user_mutex);

	switch (READ_ONCE(call->state)) {
	case RXRPC_CALL_CLIENT_RECV_REPLY:
	case RXRPC_CALL_SERVER_RECV_REQUEST:
	case RXRPC_CALL_SERVER_ACK_REQUEST:
		ret = rxrpc_recvmsg_data(sock, call, NULL, &iter, size, 0,
					 _offset);
		if (ret < 0)
			goto out;

		/* We can only reach here with a partially full buffer if we
		 * have reached the end of the data.  We must otherwise have a
		 * full buffer or have been given -EAGAIN.
		 */
		if (ret == 1) {
			if (*_offset < size)
				goto short_data;
			if (!want_more)
				goto read_phase_complete;
			ret = 0;
			goto out;
		}

		if (!want_more)
			goto excess_data;
		goto out;

	case RXRPC_CALL_COMPLETE:
		goto call_complete;

	default:
		ret = -EINPROGRESS;
		goto out;
	}

read_phase_complete:
	ret = 1;
out:
	if (_service)
		*_service = call->service_id;
	mutex_unlock(&call->user_mutex);
	_leave(" = %d [%zu,%d]", ret, *_offset, *_abort);
	return ret;

short_data:
	trace_rxrpc_rx_eproto(call, 0, tracepoint_string("short_data"));
	ret = -EBADMSG;
	goto out;
excess_data:
	trace_rxrpc_rx_eproto(call, 0, tracepoint_string("excess_data"));
	ret = -EMSGSIZE;
	goto out;
call_complete:
	*_abort = call->abort_code;
	ret = call->error;
	if (call->completion == RXRPC_CALL_SUCCEEDED) {
		ret = 1;
		if (size > 0)
			ret = -ECONNRESET;
	}
	goto out;
}
EXPORT_SYMBOL(rxrpc_kernel_recv_data);
