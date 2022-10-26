// SPDX-License-Identifier: GPL-2.0-or-later
/* RxRPC recvmsg() implementation
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
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
			spin_lock(&call->notify_lock);
			call->notify_rx(sk, call, call->user_call_ID);
			spin_unlock(&call->notify_lock);
		} else {
			write_lock(&rx->recvmsg_lock);
			if (list_empty(&call->recvmsg_link)) {
				rxrpc_get_call(call, rxrpc_call_get_notify_socket);
				list_add_tail(&call->recvmsg_link, &rx->recvmsg_q);
			}
			write_unlock(&rx->recvmsg_lock);

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
		pr_err("Invalid terminal call state %u\n", call->completion);
		BUG();
		break;
	}

	trace_rxrpc_recvdata(call, rxrpc_recvmsg_terminal,
			     lower_32_bits(atomic64_read(&call->ackr_window)) - 1,
			     call->rx_pkt_offset, call->rx_pkt_len, ret);
	return ret;
}

/*
 * Discard a packet we've used up and advance the Rx window by one.
 */
static void rxrpc_rotate_rx_window(struct rxrpc_call *call)
{
	struct rxrpc_skb_priv *sp;
	struct sk_buff *skb;
	rxrpc_serial_t serial;
	rxrpc_seq_t old_consumed = call->rx_consumed, tseq;
	bool last;
	int acked;

	_enter("%d", call->debug_id);

	skb = skb_dequeue(&call->recvmsg_queue);
	rxrpc_see_skb(skb, rxrpc_skb_see_rotate);

	sp = rxrpc_skb(skb);
	tseq   = sp->hdr.seq;
	serial = sp->hdr.serial;
	last   = sp->hdr.flags & RXRPC_LAST_PACKET;

	/* Barrier against rxrpc_input_data(). */
	if (after(tseq, call->rx_consumed))
		smp_store_release(&call->rx_consumed, tseq);

	rxrpc_free_skb(skb, rxrpc_skb_put_rotate);

	trace_rxrpc_receive(call, last ? rxrpc_receive_rotate_last : rxrpc_receive_rotate,
			    serial, call->rx_consumed);

	if (last)
		set_bit(RXRPC_CALL_RECVMSG_READ_ALL, &call->flags);

	/* Check to see if there's an ACK that needs sending. */
	acked = atomic_add_return(call->rx_consumed - old_consumed,
				  &call->ackr_nr_consumed);
	if (acked > 2 &&
	    !test_and_set_bit(RXRPC_CALL_RX_IS_IDLE, &call->flags))
		rxrpc_poke_call(call, rxrpc_call_poke_idle);
}

/*
 * Decrypt and verify a DATA packet.
 */
static int rxrpc_verify_data(struct rxrpc_call *call, struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);

	if (sp->flags & RXRPC_RX_VERIFIED)
		return 0;
	return call->security->verify_packet(call, skb);
}

/*
 * Deliver messages to a call.  This keeps processing packets until the buffer
 * is filled and we find either more DATA (returns 0) or the end of the DATA
 * (returns 1).  If more packets are required, it returns -EAGAIN and if the
 * call has failed it returns -EIO.
 */
static int rxrpc_recvmsg_data(struct socket *sock, struct rxrpc_call *call,
			      struct msghdr *msg, struct iov_iter *iter,
			      size_t len, int flags, size_t *_offset)
{
	struct rxrpc_skb_priv *sp;
	struct sk_buff *skb;
	rxrpc_seq_t seq = 0;
	size_t remain;
	unsigned int rx_pkt_offset, rx_pkt_len;
	int copy, ret = -EAGAIN, ret2;

	rx_pkt_offset = call->rx_pkt_offset;
	rx_pkt_len = call->rx_pkt_len;

	if (rxrpc_call_has_failed(call)) {
		seq = lower_32_bits(atomic64_read(&call->ackr_window)) - 1;
		ret = -EIO;
		goto done;
	}

	if (test_bit(RXRPC_CALL_RECVMSG_READ_ALL, &call->flags)) {
		seq = lower_32_bits(atomic64_read(&call->ackr_window)) - 1;
		ret = 1;
		goto done;
	}

	/* No one else can be removing stuff from the queue, so we shouldn't
	 * need the Rx lock to walk it.
	 */
	skb = skb_peek(&call->recvmsg_queue);
	while (skb) {
		rxrpc_see_skb(skb, rxrpc_skb_see_recvmsg);
		sp = rxrpc_skb(skb);
		seq = sp->hdr.seq;

		if (!(flags & MSG_PEEK))
			trace_rxrpc_receive(call, rxrpc_receive_front,
					    sp->hdr.serial, seq);

		if (msg)
			sock_recv_timestamp(msg, sock->sk, skb);

		if (rx_pkt_offset == 0) {
			ret2 = rxrpc_verify_data(call, skb);
			trace_rxrpc_recvdata(call, rxrpc_recvmsg_next, seq,
					     sp->offset, sp->len, ret2);
			if (ret2 < 0) {
				kdebug("verify = %d", ret2);
				ret = ret2;
				goto out;
			}
			rx_pkt_offset = sp->offset;
			rx_pkt_len = sp->len;
		} else {
			trace_rxrpc_recvdata(call, rxrpc_recvmsg_cont, seq,
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
			trace_rxrpc_recvdata(call, rxrpc_recvmsg_full, seq,
					     rx_pkt_offset, rx_pkt_len, 0);
			ASSERTCMP(*_offset, ==, len);
			ret = 0;
			break;
		}

		/* The whole packet has been transferred. */
		if (sp->hdr.flags & RXRPC_LAST_PACKET)
			ret = 1;
		rx_pkt_offset = 0;
		rx_pkt_len = 0;

		skb = skb_peek_next(skb, &call->recvmsg_queue);

		if (!(flags & MSG_PEEK))
			rxrpc_rotate_rx_window(call);
	}

out:
	if (!(flags & MSG_PEEK)) {
		call->rx_pkt_offset = rx_pkt_offset;
		call->rx_pkt_len = rx_pkt_len;
	}
done:
	trace_rxrpc_recvdata(call, rxrpc_recvmsg_data_return, seq,
			     rx_pkt_offset, rx_pkt_len, ret);
	if (ret == -EAGAIN)
		set_bit(RXRPC_CALL_RX_IS_IDLE, &call->flags);
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
	unsigned int call_debug_id = 0;
	size_t copied = 0;
	long timeo;
	int ret;

	DEFINE_WAIT(wait);

	trace_rxrpc_recvmsg(0, rxrpc_recvmsg_enter, 0);

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
		return -EAGAIN;
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
			trace_rxrpc_recvmsg(0, rxrpc_recvmsg_wait, 0);
			timeo = schedule_timeout(timeo);
		}
		finish_wait(sk_sleep(&rx->sk), &wait);
		goto try_again;
	}

	/* Find the next call and dequeue it if we're not just peeking.  If we
	 * do dequeue it, that comes with a ref that we will need to release.
	 */
	write_lock(&rx->recvmsg_lock);
	l = rx->recvmsg_q.next;
	call = list_entry(l, struct rxrpc_call, recvmsg_link);
	if (!(flags & MSG_PEEK))
		list_del_init(&call->recvmsg_link);
	else
		rxrpc_get_call(call, rxrpc_call_get_recvmsg);
	write_unlock(&rx->recvmsg_lock);

	call_debug_id = call->debug_id;
	trace_rxrpc_recvmsg(call_debug_id, rxrpc_recvmsg_dequeue, 0);

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
			unsigned long idl = call->user_call_ID;

			ret = put_cmsg(msg, SOL_RXRPC, RXRPC_USER_CALL_ID,
				       sizeof(unsigned long), &idl);
		}
		if (ret < 0)
			goto error_unlock_call;
	}

	if (msg->msg_name && call->peer) {
		size_t len = sizeof(call->dest_srx);

		memcpy(msg->msg_name, &call->dest_srx, len);
		msg->msg_namelen = len;
	}

	ret = rxrpc_recvmsg_data(sock, call, msg, &msg->msg_iter, len,
				 flags, &copied);
	if (ret == -EAGAIN)
		ret = 0;
	if (ret == -EIO)
		goto call_failed;
	if (ret < 0)
		goto error_unlock_call;

	if (rxrpc_call_is_complete(call) &&
	    skb_queue_empty(&call->recvmsg_queue))
		goto call_complete;
	if (rxrpc_call_has_failed(call))
		goto call_failed;

	rxrpc_notify_socket(call);
	goto not_yet_complete;

call_failed:
	rxrpc_purge_queue(&call->recvmsg_queue);
call_complete:
	ret = rxrpc_recvmsg_term(call, msg);
	if (ret < 0)
		goto error_unlock_call;
	if (!(flags & MSG_PEEK))
		rxrpc_release_call(rx, call);
	msg->msg_flags |= MSG_EOR;
	ret = 1;

not_yet_complete:
	if (ret == 0)
		msg->msg_flags |= MSG_MORE;
	else
		msg->msg_flags &= ~MSG_MORE;
	ret = copied;

error_unlock_call:
	mutex_unlock(&call->user_mutex);
	rxrpc_put_call(call, rxrpc_call_put_recvmsg);
	trace_rxrpc_recvmsg(call_debug_id, rxrpc_recvmsg_return, ret);
	return ret;

error_requeue_call:
	if (!(flags & MSG_PEEK)) {
		write_lock(&rx->recvmsg_lock);
		list_add(&call->recvmsg_link, &rx->recvmsg_q);
		write_unlock(&rx->recvmsg_lock);
		trace_rxrpc_recvmsg(call_debug_id, rxrpc_recvmsg_requeue, 0);
	} else {
		rxrpc_put_call(call, rxrpc_call_put_recvmsg);
	}
error_no_call:
	release_sock(&rx->sk);
error_trace:
	trace_rxrpc_recvmsg(call_debug_id, rxrpc_recvmsg_return, ret);
	return ret;

wait_interrupted:
	ret = sock_intr_errno(timeo);
wait_error:
	finish_wait(sk_sleep(&rx->sk), &wait);
	call = NULL;
	goto error_trace;
}

/**
 * rxrpc_kernel_recv_data - Allow a kernel service to receive data/info
 * @sock: The socket that the call exists on
 * @call: The call to send data through
 * @iter: The buffer to receive into
 * @_len: The amount of data we want to receive (decreased on return)
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
 * *_abort should also be initialised to 0.
 */
int rxrpc_kernel_recv_data(struct socket *sock, struct rxrpc_call *call,
			   struct iov_iter *iter, size_t *_len,
			   bool want_more, u32 *_abort, u16 *_service)
{
	size_t offset = 0;
	int ret;

	_enter("{%d},%zu,%d", call->debug_id, *_len, want_more);

	mutex_lock(&call->user_mutex);

	ret = rxrpc_recvmsg_data(sock, call, NULL, iter, *_len, 0, &offset);
	*_len -= offset;
	if (ret == -EIO)
		goto call_failed;
	if (ret < 0)
		goto out;

	/* We can only reach here with a partially full buffer if we have
	 * reached the end of the data.  We must otherwise have a full buffer
	 * or have been given -EAGAIN.
	 */
	if (ret == 1) {
		if (iov_iter_count(iter) > 0)
			goto short_data;
		if (!want_more)
			goto read_phase_complete;
		ret = 0;
		goto out;
	}

	if (!want_more)
		goto excess_data;
	goto out;

read_phase_complete:
	ret = 1;
out:
	if (_service)
		*_service = call->dest_srx.srx_service;
	mutex_unlock(&call->user_mutex);
	_leave(" = %d [%zu,%d]", ret, iov_iter_count(iter), *_abort);
	return ret;

short_data:
	trace_rxrpc_abort(call->debug_id, rxrpc_recvmsg_short_data,
			  call->cid, call->call_id, call->rx_consumed,
			  0, -EBADMSG);
	ret = -EBADMSG;
	goto out;
excess_data:
	trace_rxrpc_abort(call->debug_id, rxrpc_recvmsg_excess_data,
			  call->cid, call->call_id, call->rx_consumed,
			  0, -EMSGSIZE);
	ret = -EMSGSIZE;
	goto out;
call_failed:
	*_abort = call->abort_code;
	ret = call->error;
	if (call->completion == RXRPC_CALL_SUCCEEDED) {
		ret = 1;
		if (iov_iter_count(iter) > 0)
			ret = -ECONNRESET;
	}
	goto out;
}
EXPORT_SYMBOL(rxrpc_kernel_recv_data);
