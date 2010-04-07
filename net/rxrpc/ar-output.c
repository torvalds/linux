/* RxRPC packet transmission
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
#include <linux/gfp.h>
#include <linux/skbuff.h>
#include <linux/circ_buf.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include "ar-internal.h"

int rxrpc_resend_timeout = 4;

static int rxrpc_send_data(struct kiocb *iocb,
			   struct rxrpc_sock *rx,
			   struct rxrpc_call *call,
			   struct msghdr *msg, size_t len);

/*
 * extract control messages from the sendmsg() control buffer
 */
static int rxrpc_sendmsg_cmsg(struct rxrpc_sock *rx, struct msghdr *msg,
			      unsigned long *user_call_ID,
			      enum rxrpc_command *command,
			      u32 *abort_code,
			      bool server)
{
	struct cmsghdr *cmsg;
	int len;

	*command = RXRPC_CMD_SEND_DATA;

	if (msg->msg_controllen == 0)
		return -EINVAL;

	for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
		if (!CMSG_OK(msg, cmsg))
			return -EINVAL;

		len = cmsg->cmsg_len - CMSG_ALIGN(sizeof(struct cmsghdr));
		_debug("CMSG %d, %d, %d",
		       cmsg->cmsg_level, cmsg->cmsg_type, len);

		if (cmsg->cmsg_level != SOL_RXRPC)
			continue;

		switch (cmsg->cmsg_type) {
		case RXRPC_USER_CALL_ID:
			if (msg->msg_flags & MSG_CMSG_COMPAT) {
				if (len != sizeof(u32))
					return -EINVAL;
				*user_call_ID = *(u32 *) CMSG_DATA(cmsg);
			} else {
				if (len != sizeof(unsigned long))
					return -EINVAL;
				*user_call_ID = *(unsigned long *)
					CMSG_DATA(cmsg);
			}
			_debug("User Call ID %lx", *user_call_ID);
			break;

		case RXRPC_ABORT:
			if (*command != RXRPC_CMD_SEND_DATA)
				return -EINVAL;
			*command = RXRPC_CMD_SEND_ABORT;
			if (len != sizeof(*abort_code))
				return -EINVAL;
			*abort_code = *(unsigned int *) CMSG_DATA(cmsg);
			_debug("Abort %x", *abort_code);
			if (*abort_code == 0)
				return -EINVAL;
			break;

		case RXRPC_ACCEPT:
			if (*command != RXRPC_CMD_SEND_DATA)
				return -EINVAL;
			*command = RXRPC_CMD_ACCEPT;
			if (len != 0)
				return -EINVAL;
			if (!server)
				return -EISCONN;
			break;

		default:
			return -EINVAL;
		}
	}

	_leave(" = 0");
	return 0;
}

/*
 * abort a call, sending an ABORT packet to the peer
 */
static void rxrpc_send_abort(struct rxrpc_call *call, u32 abort_code)
{
	write_lock_bh(&call->state_lock);

	if (call->state <= RXRPC_CALL_COMPLETE) {
		call->state = RXRPC_CALL_LOCALLY_ABORTED;
		call->abort_code = abort_code;
		set_bit(RXRPC_CALL_ABORT, &call->events);
		del_timer_sync(&call->resend_timer);
		del_timer_sync(&call->ack_timer);
		clear_bit(RXRPC_CALL_RESEND_TIMER, &call->events);
		clear_bit(RXRPC_CALL_ACK, &call->events);
		clear_bit(RXRPC_CALL_RUN_RTIMER, &call->flags);
		rxrpc_queue_call(call);
	}

	write_unlock_bh(&call->state_lock);
}

/*
 * send a message forming part of a client call through an RxRPC socket
 * - caller holds the socket locked
 * - the socket may be either a client socket or a server socket
 */
int rxrpc_client_sendmsg(struct kiocb *iocb, struct rxrpc_sock *rx,
			 struct rxrpc_transport *trans, struct msghdr *msg,
			 size_t len)
{
	struct rxrpc_conn_bundle *bundle;
	enum rxrpc_command cmd;
	struct rxrpc_call *call;
	unsigned long user_call_ID = 0;
	struct key *key;
	__be16 service_id;
	u32 abort_code = 0;
	int ret;

	_enter("");

	ASSERT(trans != NULL);

	ret = rxrpc_sendmsg_cmsg(rx, msg, &user_call_ID, &cmd, &abort_code,
				 false);
	if (ret < 0)
		return ret;

	bundle = NULL;
	if (trans) {
		service_id = rx->service_id;
		if (msg->msg_name) {
			struct sockaddr_rxrpc *srx =
				(struct sockaddr_rxrpc *) msg->msg_name;
			service_id = htons(srx->srx_service);
		}
		key = rx->key;
		if (key && !rx->key->payload.data)
			key = NULL;
		bundle = rxrpc_get_bundle(rx, trans, key, service_id,
					  GFP_KERNEL);
		if (IS_ERR(bundle))
			return PTR_ERR(bundle);
	}

	call = rxrpc_get_client_call(rx, trans, bundle, user_call_ID,
				     abort_code == 0, GFP_KERNEL);
	if (trans)
		rxrpc_put_bundle(trans, bundle);
	if (IS_ERR(call)) {
		_leave(" = %ld", PTR_ERR(call));
		return PTR_ERR(call);
	}

	_debug("CALL %d USR %lx ST %d on CONN %p",
	       call->debug_id, call->user_call_ID, call->state, call->conn);

	if (call->state >= RXRPC_CALL_COMPLETE) {
		/* it's too late for this call */
		ret = -ESHUTDOWN;
	} else if (cmd == RXRPC_CMD_SEND_ABORT) {
		rxrpc_send_abort(call, abort_code);
	} else if (cmd != RXRPC_CMD_SEND_DATA) {
		ret = -EINVAL;
	} else if (call->state != RXRPC_CALL_CLIENT_SEND_REQUEST) {
		/* request phase complete for this client call */
		ret = -EPROTO;
	} else {
		ret = rxrpc_send_data(iocb, rx, call, msg, len);
	}

	rxrpc_put_call(call);
	_leave(" = %d", ret);
	return ret;
}

/**
 * rxrpc_kernel_send_data - Allow a kernel service to send data on a call
 * @call: The call to send data through
 * @msg: The data to send
 * @len: The amount of data to send
 *
 * Allow a kernel service to send data on a call.  The call must be in an state
 * appropriate to sending data.  No control data should be supplied in @msg,
 * nor should an address be supplied.  MSG_MORE should be flagged if there's
 * more data to come, otherwise this data will end the transmission phase.
 */
int rxrpc_kernel_send_data(struct rxrpc_call *call, struct msghdr *msg,
			   size_t len)
{
	int ret;

	_enter("{%d,%s},", call->debug_id, rxrpc_call_states[call->state]);

	ASSERTCMP(msg->msg_name, ==, NULL);
	ASSERTCMP(msg->msg_control, ==, NULL);

	lock_sock(&call->socket->sk);

	_debug("CALL %d USR %lx ST %d on CONN %p",
	       call->debug_id, call->user_call_ID, call->state, call->conn);

	if (call->state >= RXRPC_CALL_COMPLETE) {
		ret = -ESHUTDOWN; /* it's too late for this call */
	} else if (call->state != RXRPC_CALL_CLIENT_SEND_REQUEST &&
		   call->state != RXRPC_CALL_SERVER_ACK_REQUEST &&
		   call->state != RXRPC_CALL_SERVER_SEND_REPLY) {
		ret = -EPROTO; /* request phase complete for this client call */
	} else {
		mm_segment_t oldfs = get_fs();
		set_fs(KERNEL_DS);
		ret = rxrpc_send_data(NULL, call->socket, call, msg, len);
		set_fs(oldfs);
	}

	release_sock(&call->socket->sk);
	_leave(" = %d", ret);
	return ret;
}

EXPORT_SYMBOL(rxrpc_kernel_send_data);

/*
 * rxrpc_kernel_abort_call - Allow a kernel service to abort a call
 * @call: The call to be aborted
 * @abort_code: The abort code to stick into the ABORT packet
 *
 * Allow a kernel service to abort a call, if it's still in an abortable state.
 */
void rxrpc_kernel_abort_call(struct rxrpc_call *call, u32 abort_code)
{
	_enter("{%d},%d", call->debug_id, abort_code);

	lock_sock(&call->socket->sk);

	_debug("CALL %d USR %lx ST %d on CONN %p",
	       call->debug_id, call->user_call_ID, call->state, call->conn);

	if (call->state < RXRPC_CALL_COMPLETE)
		rxrpc_send_abort(call, abort_code);

	release_sock(&call->socket->sk);
	_leave("");
}

EXPORT_SYMBOL(rxrpc_kernel_abort_call);

/*
 * send a message through a server socket
 * - caller holds the socket locked
 */
int rxrpc_server_sendmsg(struct kiocb *iocb, struct rxrpc_sock *rx,
			 struct msghdr *msg, size_t len)
{
	enum rxrpc_command cmd;
	struct rxrpc_call *call;
	unsigned long user_call_ID = 0;
	u32 abort_code = 0;
	int ret;

	_enter("");

	ret = rxrpc_sendmsg_cmsg(rx, msg, &user_call_ID, &cmd, &abort_code,
				 true);
	if (ret < 0)
		return ret;

	if (cmd == RXRPC_CMD_ACCEPT) {
		call = rxrpc_accept_call(rx, user_call_ID);
		if (IS_ERR(call))
			return PTR_ERR(call);
		rxrpc_put_call(call);
		return 0;
	}

	call = rxrpc_find_server_call(rx, user_call_ID);
	if (!call)
		return -EBADSLT;
	if (call->state >= RXRPC_CALL_COMPLETE) {
		ret = -ESHUTDOWN;
		goto out;
	}

	switch (cmd) {
	case RXRPC_CMD_SEND_DATA:
		if (call->state != RXRPC_CALL_CLIENT_SEND_REQUEST &&
		    call->state != RXRPC_CALL_SERVER_ACK_REQUEST &&
		    call->state != RXRPC_CALL_SERVER_SEND_REPLY) {
			/* Tx phase not yet begun for this call */
			ret = -EPROTO;
			break;
		}

		ret = rxrpc_send_data(iocb, rx, call, msg, len);
		break;

	case RXRPC_CMD_SEND_ABORT:
		rxrpc_send_abort(call, abort_code);
		break;
	default:
		BUG();
	}

	out:
	rxrpc_put_call(call);
	_leave(" = %d", ret);
	return ret;
}

/*
 * send a packet through the transport endpoint
 */
int rxrpc_send_packet(struct rxrpc_transport *trans, struct sk_buff *skb)
{
	struct kvec iov[1];
	struct msghdr msg;
	int ret, opt;

	_enter(",{%d}", skb->len);

	iov[0].iov_base = skb->head;
	iov[0].iov_len = skb->len;

	msg.msg_name = &trans->peer->srx.transport.sin;
	msg.msg_namelen = sizeof(trans->peer->srx.transport.sin);
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;

	/* send the packet with the don't fragment bit set if we currently
	 * think it's small enough */
	if (skb->len - sizeof(struct rxrpc_header) < trans->peer->maxdata) {
		down_read(&trans->local->defrag_sem);
		/* send the packet by UDP
		 * - returns -EMSGSIZE if UDP would have to fragment the packet
		 *   to go out of the interface
		 *   - in which case, we'll have processed the ICMP error
		 *     message and update the peer record
		 */
		ret = kernel_sendmsg(trans->local->socket, &msg, iov, 1,
				     iov[0].iov_len);

		up_read(&trans->local->defrag_sem);
		if (ret == -EMSGSIZE)
			goto send_fragmentable;

		_leave(" = %d [%u]", ret, trans->peer->maxdata);
		return ret;
	}

send_fragmentable:
	/* attempt to send this message with fragmentation enabled */
	_debug("send fragment");

	down_write(&trans->local->defrag_sem);
	opt = IP_PMTUDISC_DONT;
	ret = kernel_setsockopt(trans->local->socket, SOL_IP, IP_MTU_DISCOVER,
				(char *) &opt, sizeof(opt));
	if (ret == 0) {
		ret = kernel_sendmsg(trans->local->socket, &msg, iov, 1,
				     iov[0].iov_len);

		opt = IP_PMTUDISC_DO;
		kernel_setsockopt(trans->local->socket, SOL_IP,
				  IP_MTU_DISCOVER, (char *) &opt, sizeof(opt));
	}

	up_write(&trans->local->defrag_sem);
	_leave(" = %d [frag %u]", ret, trans->peer->maxdata);
	return ret;
}

/*
 * wait for space to appear in the transmit/ACK window
 * - caller holds the socket locked
 */
static int rxrpc_wait_for_tx_window(struct rxrpc_sock *rx,
				    struct rxrpc_call *call,
				    long *timeo)
{
	DECLARE_WAITQUEUE(myself, current);
	int ret;

	_enter(",{%d},%ld",
	       CIRC_SPACE(call->acks_head, call->acks_tail, call->acks_winsz),
	       *timeo);

	add_wait_queue(&call->tx_waitq, &myself);

	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		ret = 0;
		if (CIRC_SPACE(call->acks_head, call->acks_tail,
			       call->acks_winsz) > 0)
			break;
		if (signal_pending(current)) {
			ret = sock_intr_errno(*timeo);
			break;
		}

		release_sock(&rx->sk);
		*timeo = schedule_timeout(*timeo);
		lock_sock(&rx->sk);
	}

	remove_wait_queue(&call->tx_waitq, &myself);
	set_current_state(TASK_RUNNING);
	_leave(" = %d", ret);
	return ret;
}

/*
 * attempt to schedule an instant Tx resend
 */
static inline void rxrpc_instant_resend(struct rxrpc_call *call)
{
	read_lock_bh(&call->state_lock);
	if (try_to_del_timer_sync(&call->resend_timer) >= 0) {
		clear_bit(RXRPC_CALL_RUN_RTIMER, &call->flags);
		if (call->state < RXRPC_CALL_COMPLETE &&
		    !test_and_set_bit(RXRPC_CALL_RESEND_TIMER, &call->events))
			rxrpc_queue_call(call);
	}
	read_unlock_bh(&call->state_lock);
}

/*
 * queue a packet for transmission, set the resend timer and attempt
 * to send the packet immediately
 */
static void rxrpc_queue_packet(struct rxrpc_call *call, struct sk_buff *skb,
			       bool last)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	int ret;

	_net("queue skb %p [%d]", skb, call->acks_head);

	ASSERT(call->acks_window != NULL);
	call->acks_window[call->acks_head] = (unsigned long) skb;
	smp_wmb();
	call->acks_head = (call->acks_head + 1) & (call->acks_winsz - 1);

	if (last || call->state == RXRPC_CALL_SERVER_ACK_REQUEST) {
		_debug("________awaiting reply/ACK__________");
		write_lock_bh(&call->state_lock);
		switch (call->state) {
		case RXRPC_CALL_CLIENT_SEND_REQUEST:
			call->state = RXRPC_CALL_CLIENT_AWAIT_REPLY;
			break;
		case RXRPC_CALL_SERVER_ACK_REQUEST:
			call->state = RXRPC_CALL_SERVER_SEND_REPLY;
			if (!last)
				break;
		case RXRPC_CALL_SERVER_SEND_REPLY:
			call->state = RXRPC_CALL_SERVER_AWAIT_ACK;
			break;
		default:
			break;
		}
		write_unlock_bh(&call->state_lock);
	}

	_proto("Tx DATA %%%u { #%u }",
	       ntohl(sp->hdr.serial), ntohl(sp->hdr.seq));

	sp->need_resend = 0;
	sp->resend_at = jiffies + rxrpc_resend_timeout * HZ;
	if (!test_and_set_bit(RXRPC_CALL_RUN_RTIMER, &call->flags)) {
		_debug("run timer");
		call->resend_timer.expires = sp->resend_at;
		add_timer(&call->resend_timer);
	}

	/* attempt to cancel the rx-ACK timer, deferring reply transmission if
	 * we're ACK'ing the request phase of an incoming call */
	ret = -EAGAIN;
	if (try_to_del_timer_sync(&call->ack_timer) >= 0) {
		/* the packet may be freed by rxrpc_process_call() before this
		 * returns */
		ret = rxrpc_send_packet(call->conn->trans, skb);
		_net("sent skb %p", skb);
	} else {
		_debug("failed to delete ACK timer");
	}

	if (ret < 0) {
		_debug("need instant resend %d", ret);
		sp->need_resend = 1;
		rxrpc_instant_resend(call);
	}

	_leave("");
}

/*
 * send data through a socket
 * - must be called in process context
 * - caller holds the socket locked
 */
static int rxrpc_send_data(struct kiocb *iocb,
			   struct rxrpc_sock *rx,
			   struct rxrpc_call *call,
			   struct msghdr *msg, size_t len)
{
	struct rxrpc_skb_priv *sp;
	unsigned char __user *from;
	struct sk_buff *skb;
	struct iovec *iov;
	struct sock *sk = &rx->sk;
	long timeo;
	bool more;
	int ret, ioc, segment, copied;

	_enter(",,,{%zu},%zu", msg->msg_iovlen, len);

	timeo = sock_sndtimeo(sk, msg->msg_flags & MSG_DONTWAIT);

	/* this should be in poll */
	clear_bit(SOCK_ASYNC_NOSPACE, &sk->sk_socket->flags);

	if (sk->sk_err || (sk->sk_shutdown & SEND_SHUTDOWN))
		return -EPIPE;

	iov = msg->msg_iov;
	ioc = msg->msg_iovlen - 1;
	from = iov->iov_base;
	segment = iov->iov_len;
	iov++;
	more = msg->msg_flags & MSG_MORE;

	skb = call->tx_pending;
	call->tx_pending = NULL;

	copied = 0;
	do {
		int copy;

		if (segment > len)
			segment = len;

		_debug("SEGMENT %d @%p", segment, from);

		if (!skb) {
			size_t size, chunk, max, space;

			_debug("alloc");

			if (CIRC_SPACE(call->acks_head, call->acks_tail,
				       call->acks_winsz) <= 0) {
				ret = -EAGAIN;
				if (msg->msg_flags & MSG_DONTWAIT)
					goto maybe_error;
				ret = rxrpc_wait_for_tx_window(rx, call,
							       &timeo);
				if (ret < 0)
					goto maybe_error;
			}

			max = call->conn->trans->peer->maxdata;
			max -= call->conn->security_size;
			max &= ~(call->conn->size_align - 1UL);

			chunk = max;
			if (chunk > len && !more)
				chunk = len;

			space = chunk + call->conn->size_align;
			space &= ~(call->conn->size_align - 1UL);

			size = space + call->conn->header_size;

			_debug("SIZE: %zu/%zu/%zu", chunk, space, size);

			/* create a buffer that we can retain until it's ACK'd */
			skb = sock_alloc_send_skb(
				sk, size, msg->msg_flags & MSG_DONTWAIT, &ret);
			if (!skb)
				goto maybe_error;

			rxrpc_new_skb(skb);

			_debug("ALLOC SEND %p", skb);

			ASSERTCMP(skb->mark, ==, 0);

			_debug("HS: %u", call->conn->header_size);
			skb_reserve(skb, call->conn->header_size);
			skb->len += call->conn->header_size;

			sp = rxrpc_skb(skb);
			sp->remain = chunk;
			if (sp->remain > skb_tailroom(skb))
				sp->remain = skb_tailroom(skb);

			_net("skb: hr %d, tr %d, hl %d, rm %d",
			       skb_headroom(skb),
			       skb_tailroom(skb),
			       skb_headlen(skb),
			       sp->remain);

			skb->ip_summed = CHECKSUM_UNNECESSARY;
		}

		_debug("append");
		sp = rxrpc_skb(skb);

		/* append next segment of data to the current buffer */
		copy = skb_tailroom(skb);
		ASSERTCMP(copy, >, 0);
		if (copy > segment)
			copy = segment;
		if (copy > sp->remain)
			copy = sp->remain;

		_debug("add");
		ret = skb_add_data(skb, from, copy);
		_debug("added");
		if (ret < 0)
			goto efault;
		sp->remain -= copy;
		skb->mark += copy;
		copied += copy;

		len -= copy;
		segment -= copy;
		from += copy;
		while (segment == 0 && ioc > 0) {
			from = iov->iov_base;
			segment = iov->iov_len;
			iov++;
			ioc--;
		}
		if (len == 0) {
			segment = 0;
			ioc = 0;
		}

		/* check for the far side aborting the call or a network error
		 * occurring */
		if (call->state > RXRPC_CALL_COMPLETE)
			goto call_aborted;

		/* add the packet to the send queue if it's now full */
		if (sp->remain <= 0 || (segment == 0 && !more)) {
			struct rxrpc_connection *conn = call->conn;
			size_t pad;

			/* pad out if we're using security */
			if (conn->security) {
				pad = conn->security_size + skb->mark;
				pad = conn->size_align - pad;
				pad &= conn->size_align - 1;
				_debug("pad %zu", pad);
				if (pad)
					memset(skb_put(skb, pad), 0, pad);
			}

			sp->hdr.epoch = conn->epoch;
			sp->hdr.cid = call->cid;
			sp->hdr.callNumber = call->call_id;
			sp->hdr.seq =
				htonl(atomic_inc_return(&call->sequence));
			sp->hdr.serial =
				htonl(atomic_inc_return(&conn->serial));
			sp->hdr.type = RXRPC_PACKET_TYPE_DATA;
			sp->hdr.userStatus = 0;
			sp->hdr.securityIndex = conn->security_ix;
			sp->hdr._rsvd = 0;
			sp->hdr.serviceId = conn->service_id;

			sp->hdr.flags = conn->out_clientflag;
			if (len == 0 && !more)
				sp->hdr.flags |= RXRPC_LAST_PACKET;
			else if (CIRC_SPACE(call->acks_head, call->acks_tail,
					    call->acks_winsz) > 1)
				sp->hdr.flags |= RXRPC_MORE_PACKETS;

			ret = rxrpc_secure_packet(
				call, skb, skb->mark,
				skb->head + sizeof(struct rxrpc_header));
			if (ret < 0)
				goto out;

			memcpy(skb->head, &sp->hdr,
			       sizeof(struct rxrpc_header));
			rxrpc_queue_packet(call, skb, segment == 0 && !more);
			skb = NULL;
		}

	} while (segment > 0);

success:
	ret = copied;
out:
	call->tx_pending = skb;
	_leave(" = %d", ret);
	return ret;

call_aborted:
	rxrpc_free_skb(skb);
	if (call->state == RXRPC_CALL_NETWORK_ERROR)
		ret = call->conn->trans->peer->net_error;
	else
		ret = -ECONNABORTED;
	_leave(" = %d", ret);
	return ret;

maybe_error:
	if (copied)
		goto success;
	goto out;

efault:
	ret = -EFAULT;
	goto out;
}
