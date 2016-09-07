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
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include "ar-internal.h"

/*
 * receive a message from an RxRPC socket
 * - we need to be careful about two or more threads calling recvmsg
 *   simultaneously
 */
int rxrpc_recvmsg(struct socket *sock, struct msghdr *msg, size_t len,
		  int flags)
{
	struct rxrpc_skb_priv *sp;
	struct rxrpc_call *call = NULL, *continue_call = NULL;
	struct rxrpc_sock *rx = rxrpc_sk(sock->sk);
	struct sk_buff *skb;
	long timeo;
	int copy, ret, ullen, offset, copied = 0;
	u32 abort_code;

	DEFINE_WAIT(wait);

	_enter(",,,%zu,%d", len, flags);

	if (flags & (MSG_OOB | MSG_TRUNC))
		return -EOPNOTSUPP;

	ullen = msg->msg_flags & MSG_CMSG_COMPAT ? 4 : sizeof(unsigned long);

	timeo = sock_rcvtimeo(&rx->sk, flags & MSG_DONTWAIT);
	msg->msg_flags |= MSG_MORE;

	lock_sock(&rx->sk);

	for (;;) {
		/* return immediately if a client socket has no outstanding
		 * calls */
		if (RB_EMPTY_ROOT(&rx->calls)) {
			if (copied)
				goto out;
			if (rx->sk.sk_state != RXRPC_SERVER_LISTENING) {
				release_sock(&rx->sk);
				if (continue_call)
					rxrpc_put_call(continue_call,
						       rxrpc_call_put);
				return -ENODATA;
			}
		}

		/* get the next message on the Rx queue */
		skb = skb_peek(&rx->sk.sk_receive_queue);
		if (!skb) {
			/* nothing remains on the queue */
			if (copied &&
			    (flags & MSG_PEEK || timeo == 0))
				goto out;

			/* wait for a message to turn up */
			release_sock(&rx->sk);
			prepare_to_wait_exclusive(sk_sleep(&rx->sk), &wait,
						  TASK_INTERRUPTIBLE);
			ret = sock_error(&rx->sk);
			if (ret)
				goto wait_error;

			if (skb_queue_empty(&rx->sk.sk_receive_queue)) {
				if (signal_pending(current))
					goto wait_interrupted;
				timeo = schedule_timeout(timeo);
			}
			finish_wait(sk_sleep(&rx->sk), &wait);
			lock_sock(&rx->sk);
			continue;
		}

	peek_next_packet:
		rxrpc_see_skb(skb);
		sp = rxrpc_skb(skb);
		call = sp->call;
		ASSERT(call != NULL);
		rxrpc_see_call(call);

		_debug("next pkt %s", rxrpc_pkts[sp->hdr.type]);

		/* make sure we wait for the state to be updated in this call */
		spin_lock_bh(&call->lock);
		spin_unlock_bh(&call->lock);

		if (test_bit(RXRPC_CALL_RELEASED, &call->flags)) {
			_debug("packet from released call");
			if (skb_dequeue(&rx->sk.sk_receive_queue) != skb)
				BUG();
			rxrpc_free_skb(skb);
			continue;
		}

		/* determine whether to continue last data receive */
		if (continue_call) {
			_debug("maybe cont");
			if (call != continue_call ||
			    skb->mark != RXRPC_SKB_MARK_DATA) {
				release_sock(&rx->sk);
				rxrpc_put_call(continue_call, rxrpc_call_put);
				_leave(" = %d [noncont]", copied);
				return copied;
			}
		}

		rxrpc_get_call(call, rxrpc_call_got);

		/* copy the peer address and timestamp */
		if (!continue_call) {
			if (msg->msg_name) {
				size_t len =
					sizeof(call->conn->params.peer->srx);
				memcpy(msg->msg_name,
				       &call->conn->params.peer->srx, len);
				msg->msg_namelen = len;
			}
			sock_recv_timestamp(msg, &rx->sk, skb);
		}

		/* receive the message */
		if (skb->mark != RXRPC_SKB_MARK_DATA)
			goto receive_non_data_message;

		_debug("recvmsg DATA #%u { %d, %d }",
		       sp->hdr.seq, skb->len, sp->offset);

		if (!continue_call) {
			/* only set the control data once per recvmsg() */
			ret = put_cmsg(msg, SOL_RXRPC, RXRPC_USER_CALL_ID,
				       ullen, &call->user_call_ID);
			if (ret < 0)
				goto copy_error;
			ASSERT(test_bit(RXRPC_CALL_HAS_USERID, &call->flags));
		}

		ASSERTCMP(sp->hdr.seq, >=, call->rx_data_recv);
		ASSERTCMP(sp->hdr.seq, <=, call->rx_data_recv + 1);
		call->rx_data_recv = sp->hdr.seq;

		ASSERTCMP(sp->hdr.seq, >, call->rx_data_eaten);

		offset = sp->offset;
		copy = skb->len - offset;
		if (copy > len - copied)
			copy = len - copied;

		ret = skb_copy_datagram_msg(skb, offset, msg, copy);

		if (ret < 0)
			goto copy_error;

		/* handle piecemeal consumption of data packets */
		_debug("copied %d+%d", copy, copied);

		offset += copy;
		copied += copy;

		if (!(flags & MSG_PEEK))
			sp->offset = offset;

		if (sp->offset < skb->len) {
			_debug("buffer full");
			ASSERTCMP(copied, ==, len);
			break;
		}

		/* we transferred the whole data packet */
		if (!(flags & MSG_PEEK))
			rxrpc_kernel_data_consumed(call, skb);

		if (sp->hdr.flags & RXRPC_LAST_PACKET) {
			_debug("last");
			if (rxrpc_conn_is_client(call->conn)) {
				 /* last byte of reply received */
				ret = copied;
				goto terminal_message;
			}

			/* last bit of request received */
			if (!(flags & MSG_PEEK)) {
				_debug("eat packet");
				if (skb_dequeue(&rx->sk.sk_receive_queue) !=
				    skb)
					BUG();
				rxrpc_free_skb(skb);
			}
			msg->msg_flags &= ~MSG_MORE;
			break;
		}

		/* move on to the next data message */
		_debug("next");
		if (!continue_call)
			continue_call = sp->call;
		else
			rxrpc_put_call(call, rxrpc_call_put);
		call = NULL;

		if (flags & MSG_PEEK) {
			_debug("peek next");
			skb = skb->next;
			if (skb == (struct sk_buff *) &rx->sk.sk_receive_queue)
				break;
			goto peek_next_packet;
		}

		_debug("eat packet");
		if (skb_dequeue(&rx->sk.sk_receive_queue) != skb)
			BUG();
		rxrpc_free_skb(skb);
	}

	/* end of non-terminal data packet reception for the moment */
	_debug("end rcv data");
out:
	release_sock(&rx->sk);
	if (call)
		rxrpc_put_call(call, rxrpc_call_put);
	if (continue_call)
		rxrpc_put_call(continue_call, rxrpc_call_put);
	_leave(" = %d [data]", copied);
	return copied;

	/* handle non-DATA messages such as aborts, incoming connections and
	 * final ACKs */
receive_non_data_message:
	_debug("non-data");

	if (skb->mark == RXRPC_SKB_MARK_NEW_CALL) {
		_debug("RECV NEW CALL");
		ret = put_cmsg(msg, SOL_RXRPC, RXRPC_NEW_CALL, 0, &abort_code);
		if (ret < 0)
			goto copy_error;
		if (!(flags & MSG_PEEK)) {
			if (skb_dequeue(&rx->sk.sk_receive_queue) != skb)
				BUG();
			rxrpc_free_skb(skb);
		}
		goto out;
	}

	ret = put_cmsg(msg, SOL_RXRPC, RXRPC_USER_CALL_ID,
		       ullen, &call->user_call_ID);
	if (ret < 0)
		goto copy_error;
	ASSERT(test_bit(RXRPC_CALL_HAS_USERID, &call->flags));

	switch (skb->mark) {
	case RXRPC_SKB_MARK_DATA:
		BUG();
	case RXRPC_SKB_MARK_FINAL_ACK:
		ret = put_cmsg(msg, SOL_RXRPC, RXRPC_ACK, 0, &abort_code);
		break;
	case RXRPC_SKB_MARK_BUSY:
		ret = put_cmsg(msg, SOL_RXRPC, RXRPC_BUSY, 0, &abort_code);
		break;
	case RXRPC_SKB_MARK_REMOTE_ABORT:
		abort_code = call->abort_code;
		ret = put_cmsg(msg, SOL_RXRPC, RXRPC_ABORT, 4, &abort_code);
		break;
	case RXRPC_SKB_MARK_LOCAL_ABORT:
		abort_code = call->abort_code;
		ret = put_cmsg(msg, SOL_RXRPC, RXRPC_ABORT, 4, &abort_code);
		if (call->error) {
			abort_code = call->error;
			ret = put_cmsg(msg, SOL_RXRPC, RXRPC_LOCAL_ERROR, 4,
				       &abort_code);
		}
		break;
	case RXRPC_SKB_MARK_NET_ERROR:
		_debug("RECV NET ERROR %d", sp->error);
		abort_code = sp->error;
		ret = put_cmsg(msg, SOL_RXRPC, RXRPC_NET_ERROR, 4, &abort_code);
		break;
	case RXRPC_SKB_MARK_LOCAL_ERROR:
		_debug("RECV LOCAL ERROR %d", sp->error);
		abort_code = sp->error;
		ret = put_cmsg(msg, SOL_RXRPC, RXRPC_LOCAL_ERROR, 4,
			       &abort_code);
		break;
	default:
		pr_err("Unknown packet mark %u\n", skb->mark);
		BUG();
		break;
	}

	if (ret < 0)
		goto copy_error;

terminal_message:
	_debug("terminal");
	msg->msg_flags &= ~MSG_MORE;
	msg->msg_flags |= MSG_EOR;

	if (!(flags & MSG_PEEK)) {
		_net("free terminal skb %p", skb);
		if (skb_dequeue(&rx->sk.sk_receive_queue) != skb)
			BUG();
		rxrpc_free_skb(skb);
		rxrpc_release_call(rx, call);
	}

	release_sock(&rx->sk);
	rxrpc_put_call(call, rxrpc_call_put);
	if (continue_call)
		rxrpc_put_call(continue_call, rxrpc_call_put);
	_leave(" = %d", ret);
	return ret;

copy_error:
	_debug("copy error");
	release_sock(&rx->sk);
	rxrpc_put_call(call, rxrpc_call_put);
	if (continue_call)
		rxrpc_put_call(continue_call, rxrpc_call_put);
	_leave(" = %d", ret);
	return ret;

wait_interrupted:
	ret = sock_intr_errno(timeo);
wait_error:
	finish_wait(sk_sleep(&rx->sk), &wait);
	if (continue_call)
		rxrpc_put_call(continue_call, rxrpc_call_put);
	if (copied)
		copied = ret;
	_leave(" = %d [waitfail %d]", copied, ret);
	return copied;

}

/*
 * Deliver messages to a call.  This keeps processing packets until the buffer
 * is filled and we find either more DATA (returns 0) or the end of the DATA
 * (returns 1).  If more packets are required, it returns -EAGAIN.
 *
 * TODO: Note that this is hacked in at the moment and will be replaced.
 */
static int temp_deliver_data(struct socket *sock, struct rxrpc_call *call,
			     struct iov_iter *iter, size_t size,
			     size_t *_offset)
{
	struct rxrpc_skb_priv *sp;
	struct sk_buff *skb;
	size_t remain;
	int ret, copy;

	_enter("%d", call->debug_id);

next:
	local_bh_disable();
	skb = skb_dequeue(&call->knlrecv_queue);
	local_bh_enable();
	if (!skb) {
		if (test_bit(RXRPC_CALL_RX_NO_MORE, &call->flags))
			return 1;
		_leave(" = -EAGAIN [empty]");
		return -EAGAIN;
	}

	sp = rxrpc_skb(skb);
	_debug("dequeued %p %u/%zu", skb, sp->offset, size);

	switch (skb->mark) {
	case RXRPC_SKB_MARK_DATA:
		remain = size - *_offset;
		if (remain > 0) {
			copy = skb->len - sp->offset;
			if (copy > remain)
				copy = remain;
			ret = skb_copy_datagram_iter(skb, sp->offset, iter,
						     copy);
			if (ret < 0)
				goto requeue_and_leave;

			/* handle piecemeal consumption of data packets */
			sp->offset += copy;
			*_offset += copy;
		}

		if (sp->offset < skb->len)
			goto partially_used_skb;

		/* We consumed the whole packet */
		ASSERTCMP(sp->offset, ==, skb->len);
		if (sp->hdr.flags & RXRPC_LAST_PACKET)
			set_bit(RXRPC_CALL_RX_NO_MORE, &call->flags);
		rxrpc_kernel_data_consumed(call, skb);
		rxrpc_free_skb(skb);
		goto next;

	default:
		rxrpc_free_skb(skb);
		goto next;
	}

partially_used_skb:
	ASSERTCMP(*_offset, ==, size);
	ret = 0;
requeue_and_leave:
	skb_queue_head(&call->knlrecv_queue, skb);
	return ret;
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
			   bool want_more, u32 *_abort)
{
	struct iov_iter iter;
	struct kvec iov;
	int ret;

	_enter("{%d,%s},%zu,%d",
	       call->debug_id, rxrpc_call_states[call->state], size, want_more);

	ASSERTCMP(*_offset, <=, size);
	ASSERTCMP(call->state, !=, RXRPC_CALL_SERVER_ACCEPTING);

	iov.iov_base = buf + *_offset;
	iov.iov_len = size - *_offset;
	iov_iter_kvec(&iter, ITER_KVEC | READ, &iov, 1, size - *_offset);

	lock_sock(sock->sk);

	switch (call->state) {
	case RXRPC_CALL_CLIENT_RECV_REPLY:
	case RXRPC_CALL_SERVER_RECV_REQUEST:
	case RXRPC_CALL_SERVER_ACK_REQUEST:
		ret = temp_deliver_data(sock, call, &iter, size, _offset);
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
		*_offset = 0;
		ret = -EINPROGRESS;
		goto out;
	}

read_phase_complete:
	ret = 1;
out:
	release_sock(sock->sk);
	_leave(" = %d [%zu,%d]", ret, *_offset, *_abort);
	return ret;

short_data:
	ret = -EBADMSG;
	goto out;
excess_data:
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
