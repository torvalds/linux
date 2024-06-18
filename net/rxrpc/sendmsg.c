// SPDX-License-Identifier: GPL-2.0-or-later
/* AF_RXRPC sendmsg() implementation.
 *
 * Copyright (C) 2007, 2016 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/net.h>
#include <linux/gfp.h>
#include <linux/skbuff.h>
#include <linux/export.h>
#include <linux/sched/signal.h>

#include <net/sock.h>
#include <net/af_rxrpc.h>
#include "ar-internal.h"

/*
 * Propose an abort to be made in the I/O thread.
 */
bool rxrpc_propose_abort(struct rxrpc_call *call, s32 abort_code, int error,
			 enum rxrpc_abort_reason why)
{
	_enter("{%d},%d,%d,%u", call->debug_id, abort_code, error, why);

	if (!call->send_abort && !rxrpc_call_is_complete(call)) {
		call->send_abort_why = why;
		call->send_abort_err = error;
		call->send_abort_seq = 0;
		/* Request abort locklessly vs rxrpc_input_call_event(). */
		smp_store_release(&call->send_abort, abort_code);
		rxrpc_poke_call(call, rxrpc_call_poke_abort);
		return true;
	}

	return false;
}

/*
 * Wait for a call to become connected.  Interruption here doesn't cause the
 * call to be aborted.
 */
static int rxrpc_wait_to_be_connected(struct rxrpc_call *call, long *timeo)
{
	DECLARE_WAITQUEUE(myself, current);
	int ret = 0;

	_enter("%d", call->debug_id);

	if (rxrpc_call_state(call) != RXRPC_CALL_CLIENT_AWAIT_CONN)
		goto no_wait;

	add_wait_queue_exclusive(&call->waitq, &myself);

	for (;;) {
		switch (call->interruptibility) {
		case RXRPC_INTERRUPTIBLE:
		case RXRPC_PREINTERRUPTIBLE:
			set_current_state(TASK_INTERRUPTIBLE);
			break;
		case RXRPC_UNINTERRUPTIBLE:
		default:
			set_current_state(TASK_UNINTERRUPTIBLE);
			break;
		}

		if (rxrpc_call_state(call) != RXRPC_CALL_CLIENT_AWAIT_CONN)
			break;
		if ((call->interruptibility == RXRPC_INTERRUPTIBLE ||
		     call->interruptibility == RXRPC_PREINTERRUPTIBLE) &&
		    signal_pending(current)) {
			ret = sock_intr_errno(*timeo);
			break;
		}
		*timeo = schedule_timeout(*timeo);
	}

	remove_wait_queue(&call->waitq, &myself);
	__set_current_state(TASK_RUNNING);

no_wait:
	if (ret == 0 && rxrpc_call_is_complete(call))
		ret = call->error;

	_leave(" = %d", ret);
	return ret;
}

/*
 * Return true if there's sufficient Tx queue space.
 */
static bool rxrpc_check_tx_space(struct rxrpc_call *call, rxrpc_seq_t *_tx_win)
{
	if (_tx_win)
		*_tx_win = call->tx_bottom;
	return call->tx_prepared - call->tx_bottom < 256;
}

/*
 * Wait for space to appear in the Tx queue or a signal to occur.
 */
static int rxrpc_wait_for_tx_window_intr(struct rxrpc_sock *rx,
					 struct rxrpc_call *call,
					 long *timeo)
{
	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (rxrpc_check_tx_space(call, NULL))
			return 0;

		if (rxrpc_call_is_complete(call))
			return call->error;

		if (signal_pending(current))
			return sock_intr_errno(*timeo);

		trace_rxrpc_txqueue(call, rxrpc_txqueue_wait);
		*timeo = schedule_timeout(*timeo);
	}
}

/*
 * Wait for space to appear in the Tx queue uninterruptibly, but with
 * a timeout of 2*RTT if no progress was made and a signal occurred.
 */
static int rxrpc_wait_for_tx_window_waitall(struct rxrpc_sock *rx,
					    struct rxrpc_call *call)
{
	rxrpc_seq_t tx_start, tx_win;
	signed long rtt, timeout;

	rtt = READ_ONCE(call->peer->srtt_us) >> 3;
	rtt = usecs_to_jiffies(rtt) * 2;
	if (rtt < 2)
		rtt = 2;

	timeout = rtt;
	tx_start = smp_load_acquire(&call->acks_hard_ack);

	for (;;) {
		set_current_state(TASK_UNINTERRUPTIBLE);

		if (rxrpc_check_tx_space(call, &tx_win))
			return 0;

		if (rxrpc_call_is_complete(call))
			return call->error;

		if (timeout == 0 &&
		    tx_win == tx_start && signal_pending(current))
			return -EINTR;

		if (tx_win != tx_start) {
			timeout = rtt;
			tx_start = tx_win;
		}

		trace_rxrpc_txqueue(call, rxrpc_txqueue_wait);
		timeout = schedule_timeout(timeout);
	}
}

/*
 * Wait for space to appear in the Tx queue uninterruptibly.
 */
static int rxrpc_wait_for_tx_window_nonintr(struct rxrpc_sock *rx,
					    struct rxrpc_call *call,
					    long *timeo)
{
	for (;;) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		if (rxrpc_check_tx_space(call, NULL))
			return 0;

		if (rxrpc_call_is_complete(call))
			return call->error;

		trace_rxrpc_txqueue(call, rxrpc_txqueue_wait);
		*timeo = schedule_timeout(*timeo);
	}
}

/*
 * wait for space to appear in the transmit/ACK window
 * - caller holds the socket locked
 */
static int rxrpc_wait_for_tx_window(struct rxrpc_sock *rx,
				    struct rxrpc_call *call,
				    long *timeo,
				    bool waitall)
{
	DECLARE_WAITQUEUE(myself, current);
	int ret;

	_enter(",{%u,%u,%u,%u}",
	       call->tx_bottom, call->acks_hard_ack, call->tx_top, call->tx_winsize);

	add_wait_queue(&call->waitq, &myself);

	switch (call->interruptibility) {
	case RXRPC_INTERRUPTIBLE:
		if (waitall)
			ret = rxrpc_wait_for_tx_window_waitall(rx, call);
		else
			ret = rxrpc_wait_for_tx_window_intr(rx, call, timeo);
		break;
	case RXRPC_PREINTERRUPTIBLE:
	case RXRPC_UNINTERRUPTIBLE:
	default:
		ret = rxrpc_wait_for_tx_window_nonintr(rx, call, timeo);
		break;
	}

	remove_wait_queue(&call->waitq, &myself);
	set_current_state(TASK_RUNNING);
	_leave(" = %d", ret);
	return ret;
}

/*
 * Notify the owner of the call that the transmit phase is ended and the last
 * packet has been queued.
 */
static void rxrpc_notify_end_tx(struct rxrpc_sock *rx, struct rxrpc_call *call,
				rxrpc_notify_end_tx_t notify_end_tx)
{
	if (notify_end_tx)
		notify_end_tx(&rx->sk, call, call->user_call_ID);
}

/*
 * Queue a DATA packet for transmission, set the resend timeout and send
 * the packet immediately.  Returns the error from rxrpc_send_data_packet()
 * in case the caller wants to do something with it.
 */
static void rxrpc_queue_packet(struct rxrpc_sock *rx, struct rxrpc_call *call,
			       struct rxrpc_txbuf *txb,
			       rxrpc_notify_end_tx_t notify_end_tx)
{
	rxrpc_seq_t seq = txb->seq;
	bool poke, last = txb->flags & RXRPC_LAST_PACKET;

	rxrpc_inc_stat(call->rxnet, stat_tx_data);

	ASSERTCMP(txb->seq, ==, call->tx_prepared + 1);

	/* We have to set the timestamp before queueing as the retransmit
	 * algorithm can see the packet as soon as we queue it.
	 */
	txb->last_sent = ktime_get_real();

	if (last)
		trace_rxrpc_txqueue(call, rxrpc_txqueue_queue_last);
	else
		trace_rxrpc_txqueue(call, rxrpc_txqueue_queue);

	/* Add the packet to the call's output buffer */
	spin_lock(&call->tx_lock);
	poke = list_empty(&call->tx_sendmsg);
	list_add_tail(&txb->call_link, &call->tx_sendmsg);
	call->tx_prepared = seq;
	if (last)
		rxrpc_notify_end_tx(rx, call, notify_end_tx);
	spin_unlock(&call->tx_lock);

	if (poke)
		rxrpc_poke_call(call, rxrpc_call_poke_start);
}

/*
 * send data through a socket
 * - must be called in process context
 * - The caller holds the call user access mutex, but not the socket lock.
 */
static int rxrpc_send_data(struct rxrpc_sock *rx,
			   struct rxrpc_call *call,
			   struct msghdr *msg, size_t len,
			   rxrpc_notify_end_tx_t notify_end_tx,
			   bool *_dropped_lock)
{
	struct rxrpc_txbuf *txb;
	struct sock *sk = &rx->sk;
	enum rxrpc_call_state state;
	long timeo;
	bool more = msg->msg_flags & MSG_MORE;
	int ret, copied = 0;

	timeo = sock_sndtimeo(sk, msg->msg_flags & MSG_DONTWAIT);

	ret = rxrpc_wait_to_be_connected(call, &timeo);
	if (ret < 0)
		return ret;

	if (call->conn->state == RXRPC_CONN_CLIENT_UNSECURED) {
		ret = rxrpc_init_client_conn_security(call->conn);
		if (ret < 0)
			return ret;
	}

	/* this should be in poll */
	sk_clear_bit(SOCKWQ_ASYNC_NOSPACE, sk);

reload:
	ret = -EPIPE;
	if (sk->sk_shutdown & SEND_SHUTDOWN)
		goto maybe_error;
	state = rxrpc_call_state(call);
	ret = -ESHUTDOWN;
	if (state >= RXRPC_CALL_COMPLETE)
		goto maybe_error;
	ret = -EPROTO;
	if (state != RXRPC_CALL_CLIENT_SEND_REQUEST &&
	    state != RXRPC_CALL_SERVER_ACK_REQUEST &&
	    state != RXRPC_CALL_SERVER_SEND_REPLY) {
		/* Request phase complete for this client call */
		trace_rxrpc_abort(call->debug_id, rxrpc_sendmsg_late_send,
				  call->cid, call->call_id, call->rx_consumed,
				  0, -EPROTO);
		goto maybe_error;
	}

	ret = -EMSGSIZE;
	if (call->tx_total_len != -1) {
		if (len - copied > call->tx_total_len)
			goto maybe_error;
		if (!more && len - copied != call->tx_total_len)
			goto maybe_error;
	}

	txb = call->tx_pending;
	call->tx_pending = NULL;
	if (txb)
		rxrpc_see_txbuf(txb, rxrpc_txbuf_see_send_more);

	do {
		if (!txb) {
			size_t remain;

			_debug("alloc");

			if (!rxrpc_check_tx_space(call, NULL))
				goto wait_for_space;

			/* Work out the maximum size of a packet.  Assume that
			 * the security header is going to be in the padded
			 * region (enc blocksize), but the trailer is not.
			 */
			remain = more ? INT_MAX : msg_data_left(msg);
			txb = call->conn->security->alloc_txbuf(call, remain, sk->sk_allocation);
			if (!txb) {
				ret = -ENOMEM;
				goto maybe_error;
			}
		}

		_debug("append");

		/* append next segment of data to the current buffer */
		if (msg_data_left(msg) > 0) {
			size_t copy = min_t(size_t, txb->space, msg_data_left(msg));

			_debug("add %zu", copy);
			if (!copy_from_iter_full(txb->kvec[0].iov_base + txb->offset,
						 copy, &msg->msg_iter))
				goto efault;
			_debug("added");
			txb->space -= copy;
			txb->len += copy;
			txb->offset += copy;
			copied += copy;
			if (call->tx_total_len != -1)
				call->tx_total_len -= copy;
		}

		/* check for the far side aborting the call or a network error
		 * occurring */
		if (rxrpc_call_is_complete(call))
			goto call_terminated;

		/* add the packet to the send queue if it's now full */
		if (!txb->space ||
		    (msg_data_left(msg) == 0 && !more)) {
			if (msg_data_left(msg) == 0 && !more)
				txb->flags |= RXRPC_LAST_PACKET;
			else if (call->tx_top - call->acks_hard_ack <
				 call->tx_winsize)
				txb->flags |= RXRPC_MORE_PACKETS;

			ret = call->security->secure_packet(call, txb);
			if (ret < 0)
				goto out;

			txb->kvec[0].iov_len += txb->len;
			txb->len = txb->kvec[0].iov_len;
			rxrpc_queue_packet(rx, call, txb, notify_end_tx);
			txb = NULL;
		}
	} while (msg_data_left(msg) > 0);

success:
	ret = copied;
	if (rxrpc_call_is_complete(call) &&
	    call->error < 0)
		ret = call->error;
out:
	call->tx_pending = txb;
	_leave(" = %d", ret);
	return ret;

call_terminated:
	rxrpc_put_txbuf(txb, rxrpc_txbuf_put_send_aborted);
	_leave(" = %d", call->error);
	return call->error;

maybe_error:
	if (copied)
		goto success;
	goto out;

efault:
	ret = -EFAULT;
	goto out;

wait_for_space:
	ret = -EAGAIN;
	if (msg->msg_flags & MSG_DONTWAIT)
		goto maybe_error;
	mutex_unlock(&call->user_mutex);
	*_dropped_lock = true;
	ret = rxrpc_wait_for_tx_window(rx, call, &timeo,
				       msg->msg_flags & MSG_WAITALL);
	if (ret < 0)
		goto maybe_error;
	if (call->interruptibility == RXRPC_INTERRUPTIBLE) {
		if (mutex_lock_interruptible(&call->user_mutex) < 0) {
			ret = sock_intr_errno(timeo);
			goto maybe_error;
		}
	} else {
		mutex_lock(&call->user_mutex);
	}
	*_dropped_lock = false;
	goto reload;
}

/*
 * extract control messages from the sendmsg() control buffer
 */
static int rxrpc_sendmsg_cmsg(struct msghdr *msg, struct rxrpc_send_params *p)
{
	struct cmsghdr *cmsg;
	bool got_user_ID = false;
	int len;

	if (msg->msg_controllen == 0)
		return -EINVAL;

	for_each_cmsghdr(cmsg, msg) {
		if (!CMSG_OK(msg, cmsg))
			return -EINVAL;

		len = cmsg->cmsg_len - sizeof(struct cmsghdr);
		_debug("CMSG %d, %d, %d",
		       cmsg->cmsg_level, cmsg->cmsg_type, len);

		if (cmsg->cmsg_level != SOL_RXRPC)
			continue;

		switch (cmsg->cmsg_type) {
		case RXRPC_USER_CALL_ID:
			if (msg->msg_flags & MSG_CMSG_COMPAT) {
				if (len != sizeof(u32))
					return -EINVAL;
				p->call.user_call_ID = *(u32 *)CMSG_DATA(cmsg);
			} else {
				if (len != sizeof(unsigned long))
					return -EINVAL;
				p->call.user_call_ID = *(unsigned long *)
					CMSG_DATA(cmsg);
			}
			got_user_ID = true;
			break;

		case RXRPC_ABORT:
			if (p->command != RXRPC_CMD_SEND_DATA)
				return -EINVAL;
			p->command = RXRPC_CMD_SEND_ABORT;
			if (len != sizeof(p->abort_code))
				return -EINVAL;
			p->abort_code = *(unsigned int *)CMSG_DATA(cmsg);
			if (p->abort_code == 0)
				return -EINVAL;
			break;

		case RXRPC_CHARGE_ACCEPT:
			if (p->command != RXRPC_CMD_SEND_DATA)
				return -EINVAL;
			p->command = RXRPC_CMD_CHARGE_ACCEPT;
			if (len != 0)
				return -EINVAL;
			break;

		case RXRPC_EXCLUSIVE_CALL:
			p->exclusive = true;
			if (len != 0)
				return -EINVAL;
			break;

		case RXRPC_UPGRADE_SERVICE:
			p->upgrade = true;
			if (len != 0)
				return -EINVAL;
			break;

		case RXRPC_TX_LENGTH:
			if (p->call.tx_total_len != -1 || len != sizeof(__s64))
				return -EINVAL;
			p->call.tx_total_len = *(__s64 *)CMSG_DATA(cmsg);
			if (p->call.tx_total_len < 0)
				return -EINVAL;
			break;

		case RXRPC_SET_CALL_TIMEOUT:
			if (len & 3 || len < 4 || len > 12)
				return -EINVAL;
			memcpy(&p->call.timeouts, CMSG_DATA(cmsg), len);
			p->call.nr_timeouts = len / 4;
			if (p->call.timeouts.hard > INT_MAX / HZ)
				return -ERANGE;
			if (p->call.nr_timeouts >= 2 && p->call.timeouts.idle > 60 * 60 * 1000)
				return -ERANGE;
			if (p->call.nr_timeouts >= 3 && p->call.timeouts.normal > 60 * 60 * 1000)
				return -ERANGE;
			break;

		default:
			return -EINVAL;
		}
	}

	if (!got_user_ID)
		return -EINVAL;
	if (p->call.tx_total_len != -1 && p->command != RXRPC_CMD_SEND_DATA)
		return -EINVAL;
	_leave(" = 0");
	return 0;
}

/*
 * Create a new client call for sendmsg().
 * - Called with the socket lock held, which it must release.
 * - If it returns a call, the call's lock will need releasing by the caller.
 */
static struct rxrpc_call *
rxrpc_new_client_call_for_sendmsg(struct rxrpc_sock *rx, struct msghdr *msg,
				  struct rxrpc_send_params *p)
	__releases(&rx->sk.sk_lock.slock)
	__acquires(&call->user_mutex)
{
	struct rxrpc_conn_parameters cp;
	struct rxrpc_peer *peer;
	struct rxrpc_call *call;
	struct key *key;

	DECLARE_SOCKADDR(struct sockaddr_rxrpc *, srx, msg->msg_name);

	_enter("");

	if (!msg->msg_name) {
		release_sock(&rx->sk);
		return ERR_PTR(-EDESTADDRREQ);
	}

	peer = rxrpc_lookup_peer(rx->local, srx, GFP_KERNEL);
	if (!peer) {
		release_sock(&rx->sk);
		return ERR_PTR(-ENOMEM);
	}

	key = rx->key;
	if (key && !rx->key->payload.data[0])
		key = NULL;

	memset(&cp, 0, sizeof(cp));
	cp.local		= rx->local;
	cp.peer			= peer;
	cp.key			= rx->key;
	cp.security_level	= rx->min_sec_level;
	cp.exclusive		= rx->exclusive | p->exclusive;
	cp.upgrade		= p->upgrade;
	cp.service_id		= srx->srx_service;
	call = rxrpc_new_client_call(rx, &cp, &p->call, GFP_KERNEL,
				     atomic_inc_return(&rxrpc_debug_id));
	/* The socket is now unlocked */

	rxrpc_put_peer(peer, rxrpc_peer_put_application);
	_leave(" = %p\n", call);
	return call;
}

/*
 * send a message forming part of a client call through an RxRPC socket
 * - caller holds the socket locked
 * - the socket may be either a client socket or a server socket
 */
int rxrpc_do_sendmsg(struct rxrpc_sock *rx, struct msghdr *msg, size_t len)
	__releases(&rx->sk.sk_lock.slock)
{
	struct rxrpc_call *call;
	bool dropped_lock = false;
	int ret;

	struct rxrpc_send_params p = {
		.call.tx_total_len	= -1,
		.call.user_call_ID	= 0,
		.call.nr_timeouts	= 0,
		.call.interruptibility	= RXRPC_INTERRUPTIBLE,
		.abort_code		= 0,
		.command		= RXRPC_CMD_SEND_DATA,
		.exclusive		= false,
		.upgrade		= false,
	};

	_enter("");

	ret = rxrpc_sendmsg_cmsg(msg, &p);
	if (ret < 0)
		goto error_release_sock;

	if (p.command == RXRPC_CMD_CHARGE_ACCEPT) {
		ret = -EINVAL;
		if (rx->sk.sk_state != RXRPC_SERVER_LISTENING)
			goto error_release_sock;
		ret = rxrpc_user_charge_accept(rx, p.call.user_call_ID);
		goto error_release_sock;
	}

	call = rxrpc_find_call_by_user_ID(rx, p.call.user_call_ID);
	if (!call) {
		ret = -EBADSLT;
		if (p.command != RXRPC_CMD_SEND_DATA)
			goto error_release_sock;
		call = rxrpc_new_client_call_for_sendmsg(rx, msg, &p);
		/* The socket is now unlocked... */
		if (IS_ERR(call))
			return PTR_ERR(call);
		/* ... and we have the call lock. */
		p.call.nr_timeouts = 0;
		ret = 0;
		if (rxrpc_call_is_complete(call))
			goto out_put_unlock;
	} else {
		switch (rxrpc_call_state(call)) {
		case RXRPC_CALL_CLIENT_AWAIT_CONN:
		case RXRPC_CALL_SERVER_SECURING:
			if (p.command == RXRPC_CMD_SEND_ABORT)
				break;
			fallthrough;
		case RXRPC_CALL_UNINITIALISED:
		case RXRPC_CALL_SERVER_PREALLOC:
			rxrpc_put_call(call, rxrpc_call_put_sendmsg);
			ret = -EBUSY;
			goto error_release_sock;
		default:
			break;
		}

		ret = mutex_lock_interruptible(&call->user_mutex);
		release_sock(&rx->sk);
		if (ret < 0) {
			ret = -ERESTARTSYS;
			goto error_put;
		}

		if (p.call.tx_total_len != -1) {
			ret = -EINVAL;
			if (call->tx_total_len != -1 ||
			    call->tx_pending ||
			    call->tx_top != 0)
				goto out_put_unlock;
			call->tx_total_len = p.call.tx_total_len;
		}
	}

	switch (p.call.nr_timeouts) {
	case 3:
		WRITE_ONCE(call->next_rx_timo, p.call.timeouts.normal);
		fallthrough;
	case 2:
		WRITE_ONCE(call->next_req_timo, p.call.timeouts.idle);
		fallthrough;
	case 1:
		if (p.call.timeouts.hard > 0) {
			ktime_t delay = ms_to_ktime(p.call.timeouts.hard * MSEC_PER_SEC);

			WRITE_ONCE(call->expect_term_by,
				   ktime_add(p.call.timeouts.hard,
					     ktime_get_real()));
			trace_rxrpc_timer_set(call, delay, rxrpc_timer_trace_hard);
			rxrpc_poke_call(call, rxrpc_call_poke_set_timeout);

		}
		break;
	}

	if (rxrpc_call_is_complete(call)) {
		/* it's too late for this call */
		ret = -ESHUTDOWN;
	} else if (p.command == RXRPC_CMD_SEND_ABORT) {
		rxrpc_propose_abort(call, p.abort_code, -ECONNABORTED,
				    rxrpc_abort_call_sendmsg);
		ret = 0;
	} else if (p.command != RXRPC_CMD_SEND_DATA) {
		ret = -EINVAL;
	} else {
		ret = rxrpc_send_data(rx, call, msg, len, NULL, &dropped_lock);
	}

out_put_unlock:
	if (!dropped_lock)
		mutex_unlock(&call->user_mutex);
error_put:
	rxrpc_put_call(call, rxrpc_call_put_sendmsg);
	_leave(" = %d", ret);
	return ret;

error_release_sock:
	release_sock(&rx->sk);
	return ret;
}

/**
 * rxrpc_kernel_send_data - Allow a kernel service to send data on a call
 * @sock: The socket the call is on
 * @call: The call to send data through
 * @msg: The data to send
 * @len: The amount of data to send
 * @notify_end_tx: Notification that the last packet is queued.
 *
 * Allow a kernel service to send data on a call.  The call must be in an state
 * appropriate to sending data.  No control data should be supplied in @msg,
 * nor should an address be supplied.  MSG_MORE should be flagged if there's
 * more data to come, otherwise this data will end the transmission phase.
 */
int rxrpc_kernel_send_data(struct socket *sock, struct rxrpc_call *call,
			   struct msghdr *msg, size_t len,
			   rxrpc_notify_end_tx_t notify_end_tx)
{
	bool dropped_lock = false;
	int ret;

	_enter("{%d},", call->debug_id);

	ASSERTCMP(msg->msg_name, ==, NULL);
	ASSERTCMP(msg->msg_control, ==, NULL);

	mutex_lock(&call->user_mutex);

	ret = rxrpc_send_data(rxrpc_sk(sock->sk), call, msg, len,
			      notify_end_tx, &dropped_lock);
	if (ret == -ESHUTDOWN)
		ret = call->error;

	if (!dropped_lock)
		mutex_unlock(&call->user_mutex);
	_leave(" = %d", ret);
	return ret;
}
EXPORT_SYMBOL(rxrpc_kernel_send_data);

/**
 * rxrpc_kernel_abort_call - Allow a kernel service to abort a call
 * @sock: The socket the call is on
 * @call: The call to be aborted
 * @abort_code: The abort code to stick into the ABORT packet
 * @error: Local error value
 * @why: Indication as to why.
 *
 * Allow a kernel service to abort a call, if it's still in an abortable state
 * and return true if the call was aborted, false if it was already complete.
 */
bool rxrpc_kernel_abort_call(struct socket *sock, struct rxrpc_call *call,
			     u32 abort_code, int error, enum rxrpc_abort_reason why)
{
	bool aborted;

	_enter("{%d},%d,%d,%u", call->debug_id, abort_code, error, why);

	mutex_lock(&call->user_mutex);
	aborted = rxrpc_propose_abort(call, abort_code, error, why);
	mutex_unlock(&call->user_mutex);
	return aborted;
}
EXPORT_SYMBOL(rxrpc_kernel_abort_call);

/**
 * rxrpc_kernel_set_tx_length - Set the total Tx length on a call
 * @sock: The socket the call is on
 * @call: The call to be informed
 * @tx_total_len: The amount of data to be transmitted for this call
 *
 * Allow a kernel service to set the total transmit length on a call.  This
 * allows buffer-to-packet encrypt-and-copy to be performed.
 *
 * This function is primarily for use for setting the reply length since the
 * request length can be set when beginning the call.
 */
void rxrpc_kernel_set_tx_length(struct socket *sock, struct rxrpc_call *call,
				s64 tx_total_len)
{
	WARN_ON(call->tx_total_len != -1);
	call->tx_total_len = tx_total_len;
}
EXPORT_SYMBOL(rxrpc_kernel_set_tx_length);
