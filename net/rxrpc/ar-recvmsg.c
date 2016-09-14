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

#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/export.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include "ar-internal.h"

/*
 * removal a call's user ID from the socket tree to make the user ID available
 * again and so that it won't be seen again in association with that call
 */
void rxrpc_remove_user_ID(struct rxrpc_sock *rx, struct rxrpc_call *call)
{
	_debug("RELEASE CALL %d", call->debug_id);

	if (test_bit(RXRPC_CALL_HAS_USERID, &call->flags)) {
		write_lock_bh(&rx->call_lock);
		rb_erase(&call->sock_node, &call->socket->calls);
		clear_bit(RXRPC_CALL_HAS_USERID, &call->flags);
		write_unlock_bh(&rx->call_lock);
	}

	read_lock_bh(&call->state_lock);
	if (!test_bit(RXRPC_CALL_RELEASED, &call->flags) &&
	    !test_and_set_bit(RXRPC_CALL_EV_RELEASE, &call->events))
		rxrpc_queue_call(call);
	read_unlock_bh(&call->state_lock);
}

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
					rxrpc_put_call(continue_call);
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
		sp = rxrpc_skb(skb);
		call = sp->call;
		ASSERT(call != NULL);

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
				rxrpc_put_call(continue_call);
				_leave(" = %d [noncont]", copied);
				return copied;
			}
		}

		rxrpc_get_call(call);

		/* copy the peer address and timestamp */
		if (!continue_call) {
			if (msg->msg_name) {
				size_t len =
					sizeof(call->conn->trans->peer->srx);
				memcpy(msg->msg_name,
				       &call->conn->trans->peer->srx, len);
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
		if (sp->hdr.flags & RXRPC_LAST_PACKET) {
			_debug("last");
			if (call->conn->out_clientflag) {
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
			rxrpc_put_call(call);
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
		rxrpc_put_call(call);
	if (continue_call)
		rxrpc_put_call(continue_call);
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
		abort_code = call->remote_abort;
		ret = put_cmsg(msg, SOL_RXRPC, RXRPC_ABORT, 4, &abort_code);
		break;
	case RXRPC_SKB_MARK_LOCAL_ABORT:
		abort_code = call->local_abort;
		ret = put_cmsg(msg, SOL_RXRPC, RXRPC_ABORT, 4, &abort_code);
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
		pr_err("RxRPC: Unknown packet mark %u\n", skb->mark);
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
		rxrpc_remove_user_ID(rx, call);
	}

	release_sock(&rx->sk);
	rxrpc_put_call(call);
	if (continue_call)
		rxrpc_put_call(continue_call);
	_leave(" = %d", ret);
	return ret;

copy_error:
	_debug("copy error");
	release_sock(&rx->sk);
	rxrpc_put_call(call);
	if (continue_call)
		rxrpc_put_call(continue_call);
	_leave(" = %d", ret);
	return ret;

wait_interrupted:
	ret = sock_intr_errno(timeo);
wait_error:
	finish_wait(sk_sleep(&rx->sk), &wait);
	if (continue_call)
		rxrpc_put_call(continue_call);
	if (copied)
		copied = ret;
	_leave(" = %d [waitfail %d]", copied, ret);
	return copied;

}

/**
 * rxrpc_kernel_data_delivered - Record delivery of data message
 * @skb: Message holding data
 *
 * Record the delivery of a data message.  This permits RxRPC to keep its
 * tracking correct.  The socket buffer will be deleted.
 */
void rxrpc_kernel_data_delivered(struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct rxrpc_call *call = sp->call;

	ASSERTCMP(sp->hdr.seq, >=, call->rx_data_recv);
	ASSERTCMP(sp->hdr.seq, <=, call->rx_data_recv + 1);
	call->rx_data_recv = sp->hdr.seq;

	ASSERTCMP(sp->hdr.seq, >, call->rx_data_eaten);
	rxrpc_free_skb(skb);
}

EXPORT_SYMBOL(rxrpc_kernel_data_delivered);

/**
 * rxrpc_kernel_is_data_last - Determine if data message is last one
 * @skb: Message holding data
 *
 * Determine if data message is last one for the parent call.
 */
bool rxrpc_kernel_is_data_last(struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);

	ASSERTCMP(skb->mark, ==, RXRPC_SKB_MARK_DATA);

	return sp->hdr.flags & RXRPC_LAST_PACKET;
}

EXPORT_SYMBOL(rxrpc_kernel_is_data_last);

/**
 * rxrpc_kernel_get_abort_code - Get the abort code from an RxRPC abort message
 * @skb: Message indicating an abort
 *
 * Get the abort code from an RxRPC abort message.
 */
u32 rxrpc_kernel_get_abort_code(struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);

	switch (skb->mark) {
	case RXRPC_SKB_MARK_REMOTE_ABORT:
		return sp->call->remote_abort;
	case RXRPC_SKB_MARK_LOCAL_ABORT:
		return sp->call->local_abort;
	default:
		BUG();
	}
}

EXPORT_SYMBOL(rxrpc_kernel_get_abort_code);

/**
 * rxrpc_kernel_get_error - Get the error number from an RxRPC error message
 * @skb: Message indicating an error
 *
 * Get the error number from an RxRPC error message.
 */
int rxrpc_kernel_get_error_number(struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);

	return sp->error;
}

EXPORT_SYMBOL(rxrpc_kernel_get_error_number);
