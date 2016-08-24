/* RxRPC packet reception
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

#include <linux/module.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/errqueue.h>
#include <linux/udp.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/icmp.h>
#include <linux/gfp.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include <net/ip.h>
#include <net/udp.h>
#include <net/net_namespace.h>
#include "ar-internal.h"

/*
 * queue a packet for recvmsg to pass to userspace
 * - the caller must hold a lock on call->lock
 * - must not be called with interrupts disabled (sk_filter() disables BH's)
 * - eats the packet whether successful or not
 * - there must be just one reference to the packet, which the caller passes to
 *   this function
 */
int rxrpc_queue_rcv_skb(struct rxrpc_call *call, struct sk_buff *skb,
			bool force, bool terminal)
{
	struct rxrpc_skb_priv *sp;
	struct rxrpc_sock *rx = call->socket;
	struct sock *sk;
	int ret;

	_enter(",,%d,%d", force, terminal);

	ASSERT(!irqs_disabled());

	sp = rxrpc_skb(skb);
	ASSERTCMP(sp->call, ==, call);

	/* if we've already posted the terminal message for a call, then we
	 * don't post any more */
	if (test_bit(RXRPC_CALL_TERMINAL_MSG, &call->flags)) {
		_debug("already terminated");
		ASSERTCMP(call->state, >=, RXRPC_CALL_COMPLETE);
		rxrpc_free_skb(skb);
		return 0;
	}

	sk = &rx->sk;

	if (!force) {
		/* cast skb->rcvbuf to unsigned...  It's pointless, but
		 * reduces number of warnings when compiling with -W
		 * --ANK */
//		ret = -ENOBUFS;
//		if (atomic_read(&sk->sk_rmem_alloc) + skb->truesize >=
//		    (unsigned int) sk->sk_rcvbuf)
//			goto out;

		ret = sk_filter(sk, skb);
		if (ret < 0)
			goto out;
	}

	spin_lock_bh(&sk->sk_receive_queue.lock);
	if (!test_bit(RXRPC_CALL_TERMINAL_MSG, &call->flags) &&
	    !test_bit(RXRPC_CALL_RELEASED, &call->flags) &&
	    call->socket->sk.sk_state != RXRPC_CLOSE) {
		skb->destructor = rxrpc_packet_destructor;
		skb->dev = NULL;
		skb->sk = sk;
		atomic_add(skb->truesize, &sk->sk_rmem_alloc);

		if (terminal) {
			_debug("<<<< TERMINAL MESSAGE >>>>");
			set_bit(RXRPC_CALL_TERMINAL_MSG, &call->flags);
		}

		/* allow interception by a kernel service */
		if (rx->interceptor) {
			rx->interceptor(sk, call->user_call_ID, skb);
			spin_unlock_bh(&sk->sk_receive_queue.lock);
		} else {
			_net("post skb %p", skb);
			__skb_queue_tail(&sk->sk_receive_queue, skb);
			spin_unlock_bh(&sk->sk_receive_queue.lock);

			if (!sock_flag(sk, SOCK_DEAD))
				sk->sk_data_ready(sk);
		}
		skb = NULL;
	} else {
		spin_unlock_bh(&sk->sk_receive_queue.lock);
	}
	ret = 0;

out:
	rxrpc_free_skb(skb);

	_leave(" = %d", ret);
	return ret;
}

/*
 * process a DATA packet, posting the packet to the appropriate queue
 * - eats the packet if successful
 */
static int rxrpc_fast_process_data(struct rxrpc_call *call,
				   struct sk_buff *skb, u32 seq)
{
	struct rxrpc_skb_priv *sp;
	bool terminal;
	int ret, ackbit, ack;
	u32 serial;
	u16 skew;
	u8 flags;

	_enter("{%u,%u},,{%u}", call->rx_data_post, call->rx_first_oos, seq);

	sp = rxrpc_skb(skb);
	ASSERTCMP(sp->call, ==, NULL);
	flags = sp->hdr.flags;
	serial = sp->hdr.serial;
	skew = skb->priority;

	spin_lock(&call->lock);

	if (call->state > RXRPC_CALL_COMPLETE)
		goto discard;

	ASSERTCMP(call->rx_data_expect, >=, call->rx_data_post);
	ASSERTCMP(call->rx_data_post, >=, call->rx_data_recv);
	ASSERTCMP(call->rx_data_recv, >=, call->rx_data_eaten);

	if (seq < call->rx_data_post) {
		_debug("dup #%u [-%u]", seq, call->rx_data_post);
		ack = RXRPC_ACK_DUPLICATE;
		ret = -ENOBUFS;
		goto discard_and_ack;
	}

	/* we may already have the packet in the out of sequence queue */
	ackbit = seq - (call->rx_data_eaten + 1);
	ASSERTCMP(ackbit, >=, 0);
	if (__test_and_set_bit(ackbit, call->ackr_window)) {
		_debug("dup oos #%u [%u,%u]",
		       seq, call->rx_data_eaten, call->rx_data_post);
		ack = RXRPC_ACK_DUPLICATE;
		goto discard_and_ack;
	}

	if (seq >= call->ackr_win_top) {
		_debug("exceed #%u [%u]", seq, call->ackr_win_top);
		__clear_bit(ackbit, call->ackr_window);
		ack = RXRPC_ACK_EXCEEDS_WINDOW;
		goto discard_and_ack;
	}

	if (seq == call->rx_data_expect) {
		clear_bit(RXRPC_CALL_EXPECT_OOS, &call->flags);
		call->rx_data_expect++;
	} else if (seq > call->rx_data_expect) {
		_debug("oos #%u [%u]", seq, call->rx_data_expect);
		call->rx_data_expect = seq + 1;
		if (test_and_set_bit(RXRPC_CALL_EXPECT_OOS, &call->flags)) {
			ack = RXRPC_ACK_OUT_OF_SEQUENCE;
			goto enqueue_and_ack;
		}
		goto enqueue_packet;
	}

	if (seq != call->rx_data_post) {
		_debug("ahead #%u [%u]", seq, call->rx_data_post);
		goto enqueue_packet;
	}

	if (test_bit(RXRPC_CALL_RCVD_LAST, &call->flags))
		goto protocol_error;

	/* if the packet need security things doing to it, then it goes down
	 * the slow path */
	if (call->conn->security_ix)
		goto enqueue_packet;

	sp->call = call;
	rxrpc_get_call(call);
	atomic_inc(&call->skb_count);
	terminal = ((flags & RXRPC_LAST_PACKET) &&
		    !(flags & RXRPC_CLIENT_INITIATED));
	ret = rxrpc_queue_rcv_skb(call, skb, false, terminal);
	if (ret < 0) {
		if (ret == -ENOMEM || ret == -ENOBUFS) {
			__clear_bit(ackbit, call->ackr_window);
			ack = RXRPC_ACK_NOSPACE;
			goto discard_and_ack;
		}
		goto out;
	}

	skb = NULL;
	sp = NULL;

	_debug("post #%u", seq);
	ASSERTCMP(call->rx_data_post, ==, seq);
	call->rx_data_post++;

	if (flags & RXRPC_LAST_PACKET)
		set_bit(RXRPC_CALL_RCVD_LAST, &call->flags);

	/* if we've reached an out of sequence packet then we need to drain
	 * that queue into the socket Rx queue now */
	if (call->rx_data_post == call->rx_first_oos) {
		_debug("drain rx oos now");
		read_lock(&call->state_lock);
		if (call->state < RXRPC_CALL_COMPLETE &&
		    !test_and_set_bit(RXRPC_CALL_EV_DRAIN_RX_OOS, &call->events))
			rxrpc_queue_call(call);
		read_unlock(&call->state_lock);
	}

	spin_unlock(&call->lock);
	atomic_inc(&call->ackr_not_idle);
	rxrpc_propose_ACK(call, RXRPC_ACK_DELAY, skew, serial, false);
	_leave(" = 0 [posted]");
	return 0;

protocol_error:
	ret = -EBADMSG;
out:
	spin_unlock(&call->lock);
	_leave(" = %d", ret);
	return ret;

discard_and_ack:
	_debug("discard and ACK packet %p", skb);
	__rxrpc_propose_ACK(call, ack, skew, serial, true);
discard:
	spin_unlock(&call->lock);
	rxrpc_free_skb(skb);
	_leave(" = 0 [discarded]");
	return 0;

enqueue_and_ack:
	__rxrpc_propose_ACK(call, ack, skew, serial, true);
enqueue_packet:
	_net("defer skb %p", skb);
	spin_unlock(&call->lock);
	skb_queue_tail(&call->rx_queue, skb);
	atomic_inc(&call->ackr_not_idle);
	read_lock(&call->state_lock);
	if (call->state < RXRPC_CALL_DEAD)
		rxrpc_queue_call(call);
	read_unlock(&call->state_lock);
	_leave(" = 0 [queued]");
	return 0;
}

/*
 * assume an implicit ACKALL of the transmission phase of a client socket upon
 * reception of the first reply packet
 */
static void rxrpc_assume_implicit_ackall(struct rxrpc_call *call, u32 serial)
{
	write_lock_bh(&call->state_lock);

	switch (call->state) {
	case RXRPC_CALL_CLIENT_AWAIT_REPLY:
		call->state = RXRPC_CALL_CLIENT_RECV_REPLY;
		call->acks_latest = serial;

		_debug("implicit ACKALL %%%u", call->acks_latest);
		set_bit(RXRPC_CALL_EV_RCVD_ACKALL, &call->events);
		write_unlock_bh(&call->state_lock);

		if (try_to_del_timer_sync(&call->resend_timer) >= 0) {
			clear_bit(RXRPC_CALL_EV_RESEND_TIMER, &call->events);
			clear_bit(RXRPC_CALL_EV_RESEND, &call->events);
			clear_bit(RXRPC_CALL_RUN_RTIMER, &call->flags);
		}
		break;

	default:
		write_unlock_bh(&call->state_lock);
		break;
	}
}

/*
 * post an incoming packet to the nominated call to deal with
 * - must get rid of the sk_buff, either by freeing it or by queuing it
 */
void rxrpc_fast_process_packet(struct rxrpc_call *call, struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	__be32 wtmp;
	u32 abort_code;

	_enter("%p,%p", call, skb);

	ASSERT(!irqs_disabled());

#if 0 // INJECT RX ERROR
	if (sp->hdr.type == RXRPC_PACKET_TYPE_DATA) {
		static int skip = 0;
		if (++skip == 3) {
			printk("DROPPED 3RD PACKET!!!!!!!!!!!!!\n");
			skip = 0;
			goto free_packet;
		}
	}
#endif

	/* request ACK generation for any ACK or DATA packet that requests
	 * it */
	if (sp->hdr.flags & RXRPC_REQUEST_ACK) {
		_proto("ACK Requested on %%%u", sp->hdr.serial);
		rxrpc_propose_ACK(call, RXRPC_ACK_REQUESTED,
				  skb->priority, sp->hdr.serial, false);
	}

	switch (sp->hdr.type) {
	case RXRPC_PACKET_TYPE_ABORT:
		_debug("abort");

		if (skb_copy_bits(skb, 0, &wtmp, sizeof(wtmp)) < 0)
			goto protocol_error;

		abort_code = ntohl(wtmp);
		_proto("Rx ABORT %%%u { %x }", sp->hdr.serial, abort_code);

		write_lock_bh(&call->state_lock);
		if (call->state < RXRPC_CALL_COMPLETE) {
			call->state = RXRPC_CALL_REMOTELY_ABORTED;
			call->remote_abort = abort_code;
			set_bit(RXRPC_CALL_EV_RCVD_ABORT, &call->events);
			rxrpc_queue_call(call);
		}
		goto free_packet_unlock;

	case RXRPC_PACKET_TYPE_BUSY:
		_proto("Rx BUSY %%%u", sp->hdr.serial);

		if (rxrpc_conn_is_service(call->conn))
			goto protocol_error;

		write_lock_bh(&call->state_lock);
		switch (call->state) {
		case RXRPC_CALL_CLIENT_SEND_REQUEST:
			call->state = RXRPC_CALL_SERVER_BUSY;
			set_bit(RXRPC_CALL_EV_RCVD_BUSY, &call->events);
			rxrpc_queue_call(call);
		case RXRPC_CALL_SERVER_BUSY:
			goto free_packet_unlock;
		default:
			goto protocol_error_locked;
		}

	default:
		_proto("Rx %s %%%u", rxrpc_pkts[sp->hdr.type], sp->hdr.serial);
		goto protocol_error;

	case RXRPC_PACKET_TYPE_DATA:
		_proto("Rx DATA %%%u { #%u }", sp->hdr.serial, sp->hdr.seq);

		if (sp->hdr.seq == 0)
			goto protocol_error;

		call->ackr_prev_seq = sp->hdr.seq;

		/* received data implicitly ACKs all of the request packets we
		 * sent when we're acting as a client */
		if (call->state == RXRPC_CALL_CLIENT_AWAIT_REPLY)
			rxrpc_assume_implicit_ackall(call, sp->hdr.serial);

		switch (rxrpc_fast_process_data(call, skb, sp->hdr.seq)) {
		case 0:
			skb = NULL;
			goto done;

		default:
			BUG();

			/* data packet received beyond the last packet */
		case -EBADMSG:
			goto protocol_error;
		}

	case RXRPC_PACKET_TYPE_ACKALL:
	case RXRPC_PACKET_TYPE_ACK:
		/* ACK processing is done in process context */
		read_lock_bh(&call->state_lock);
		if (call->state < RXRPC_CALL_DEAD) {
			skb_queue_tail(&call->rx_queue, skb);
			rxrpc_queue_call(call);
			skb = NULL;
		}
		read_unlock_bh(&call->state_lock);
		goto free_packet;
	}

protocol_error:
	_debug("protocol error");
	write_lock_bh(&call->state_lock);
protocol_error_locked:
	if (call->state <= RXRPC_CALL_COMPLETE) {
		call->state = RXRPC_CALL_LOCALLY_ABORTED;
		call->local_abort = RX_PROTOCOL_ERROR;
		set_bit(RXRPC_CALL_EV_ABORT, &call->events);
		rxrpc_queue_call(call);
	}
free_packet_unlock:
	write_unlock_bh(&call->state_lock);
free_packet:
	rxrpc_free_skb(skb);
done:
	_leave("");
}

/*
 * split up a jumbo data packet
 */
static void rxrpc_process_jumbo_packet(struct rxrpc_call *call,
				       struct sk_buff *jumbo)
{
	struct rxrpc_jumbo_header jhdr;
	struct rxrpc_skb_priv *sp;
	struct sk_buff *part;

	_enter(",{%u,%u}", jumbo->data_len, jumbo->len);

	sp = rxrpc_skb(jumbo);

	do {
		sp->hdr.flags &= ~RXRPC_JUMBO_PACKET;

		/* make a clone to represent the first subpacket in what's left
		 * of the jumbo packet */
		part = skb_clone(jumbo, GFP_ATOMIC);
		if (!part) {
			/* simply ditch the tail in the event of ENOMEM */
			pskb_trim(jumbo, RXRPC_JUMBO_DATALEN);
			break;
		}
		rxrpc_new_skb(part);

		pskb_trim(part, RXRPC_JUMBO_DATALEN);

		if (!pskb_pull(jumbo, RXRPC_JUMBO_DATALEN))
			goto protocol_error;

		if (skb_copy_bits(jumbo, 0, &jhdr, sizeof(jhdr)) < 0)
			goto protocol_error;
		if (!pskb_pull(jumbo, sizeof(jhdr)))
			BUG();

		sp->hdr.seq	+= 1;
		sp->hdr.serial	+= 1;
		sp->hdr.flags	= jhdr.flags;
		sp->hdr._rsvd	= ntohs(jhdr._rsvd);

		_proto("Rx DATA Jumbo %%%u", sp->hdr.serial - 1);

		rxrpc_fast_process_packet(call, part);
		part = NULL;

	} while (sp->hdr.flags & RXRPC_JUMBO_PACKET);

	rxrpc_fast_process_packet(call, jumbo);
	_leave("");
	return;

protocol_error:
	_debug("protocol error");
	rxrpc_free_skb(part);
	rxrpc_free_skb(jumbo);
	write_lock_bh(&call->state_lock);
	if (call->state <= RXRPC_CALL_COMPLETE) {
		call->state = RXRPC_CALL_LOCALLY_ABORTED;
		call->local_abort = RX_PROTOCOL_ERROR;
		set_bit(RXRPC_CALL_EV_ABORT, &call->events);
		rxrpc_queue_call(call);
	}
	write_unlock_bh(&call->state_lock);
	_leave("");
}

/*
 * post an incoming packet to the appropriate call/socket to deal with
 * - must get rid of the sk_buff, either by freeing it or by queuing it
 */
static void rxrpc_post_packet_to_call(struct rxrpc_call *call,
				      struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp;

	_enter("%p,%p", call, skb);

	sp = rxrpc_skb(skb);

	_debug("extant call [%d]", call->state);

	read_lock(&call->state_lock);
	switch (call->state) {
	case RXRPC_CALL_LOCALLY_ABORTED:
		if (!test_and_set_bit(RXRPC_CALL_EV_ABORT, &call->events)) {
			rxrpc_queue_call(call);
			goto free_unlock;
		}
	case RXRPC_CALL_REMOTELY_ABORTED:
	case RXRPC_CALL_NETWORK_ERROR:
	case RXRPC_CALL_DEAD:
		goto dead_call;
	case RXRPC_CALL_COMPLETE:
	case RXRPC_CALL_CLIENT_FINAL_ACK:
		/* complete server call */
		if (rxrpc_conn_is_service(call->conn))
			goto dead_call;
		/* resend last packet of a completed call */
		_debug("final ack again");
		rxrpc_get_call(call);
		set_bit(RXRPC_CALL_EV_ACK_FINAL, &call->events);
		rxrpc_queue_call(call);
		goto free_unlock;
	default:
		break;
	}

	read_unlock(&call->state_lock);
	rxrpc_get_call(call);

	if (sp->hdr.type == RXRPC_PACKET_TYPE_DATA &&
	    sp->hdr.flags & RXRPC_JUMBO_PACKET)
		rxrpc_process_jumbo_packet(call, skb);
	else
		rxrpc_fast_process_packet(call, skb);

	rxrpc_put_call(call);
	goto done;

dead_call:
	if (sp->hdr.type != RXRPC_PACKET_TYPE_ABORT) {
		skb->priority = RX_CALL_DEAD;
		rxrpc_reject_packet(call->conn->params.local, skb);
		goto unlock;
	}
free_unlock:
	rxrpc_free_skb(skb);
unlock:
	read_unlock(&call->state_lock);
done:
	_leave("");
}

/*
 * post connection-level events to the connection
 * - this includes challenges, responses, some aborts and call terminal packet
 *   retransmission.
 */
static void rxrpc_post_packet_to_conn(struct rxrpc_connection *conn,
				      struct sk_buff *skb)
{
	_enter("%p,%p", conn, skb);

	skb_queue_tail(&conn->rx_queue, skb);
	rxrpc_queue_conn(conn);
}

/*
 * post endpoint-level events to the local endpoint
 * - this includes debug and version messages
 */
static void rxrpc_post_packet_to_local(struct rxrpc_local *local,
				       struct sk_buff *skb)
{
	_enter("%p,%p", local, skb);

	skb_queue_tail(&local->event_queue, skb);
	rxrpc_queue_local(local);
}

/*
 * Extract the wire header from a packet and translate the byte order.
 */
static noinline
int rxrpc_extract_header(struct rxrpc_skb_priv *sp, struct sk_buff *skb)
{
	struct rxrpc_wire_header whdr;

	/* dig out the RxRPC connection details */
	if (skb_copy_bits(skb, 0, &whdr, sizeof(whdr)) < 0)
		return -EBADMSG;
	if (!pskb_pull(skb, sizeof(whdr)))
		BUG();

	memset(sp, 0, sizeof(*sp));
	sp->hdr.epoch		= ntohl(whdr.epoch);
	sp->hdr.cid		= ntohl(whdr.cid);
	sp->hdr.callNumber	= ntohl(whdr.callNumber);
	sp->hdr.seq		= ntohl(whdr.seq);
	sp->hdr.serial		= ntohl(whdr.serial);
	sp->hdr.flags		= whdr.flags;
	sp->hdr.type		= whdr.type;
	sp->hdr.userStatus	= whdr.userStatus;
	sp->hdr.securityIndex	= whdr.securityIndex;
	sp->hdr._rsvd		= ntohs(whdr._rsvd);
	sp->hdr.serviceId	= ntohs(whdr.serviceId);
	return 0;
}

/*
 * handle data received on the local endpoint
 * - may be called in interrupt context
 *
 * The socket is locked by the caller and this prevents the socket from being
 * shut down and the local endpoint from going away, thus sk_user_data will not
 * be cleared until this function returns.
 */
void rxrpc_data_ready(struct sock *sk)
{
	struct rxrpc_connection *conn;
	struct rxrpc_skb_priv *sp;
	struct rxrpc_local *local = sk->sk_user_data;
	struct sk_buff *skb;
	int ret, skew;

	_enter("%p", sk);

	ASSERT(!irqs_disabled());

	skb = skb_recv_datagram(sk, 0, 1, &ret);
	if (!skb) {
		if (ret == -EAGAIN)
			return;
		_debug("UDP socket error %d", ret);
		return;
	}

	rxrpc_new_skb(skb);

	_net("recv skb %p", skb);

	/* we'll probably need to checksum it (didn't call sock_recvmsg) */
	if (skb_checksum_complete(skb)) {
		rxrpc_free_skb(skb);
		__UDP_INC_STATS(&init_net, UDP_MIB_INERRORS, 0);
		_leave(" [CSUM failed]");
		return;
	}

	__UDP_INC_STATS(&init_net, UDP_MIB_INDATAGRAMS, 0);

	/* The socket buffer we have is owned by UDP, with UDP's data all over
	 * it, but we really want our own data there.
	 */
	skb_orphan(skb);
	sp = rxrpc_skb(skb);

	_net("Rx UDP packet from %08x:%04hu",
	     ntohl(ip_hdr(skb)->saddr), ntohs(udp_hdr(skb)->source));

	/* dig out the RxRPC connection details */
	if (rxrpc_extract_header(sp, skb) < 0)
		goto bad_message;

	_net("Rx RxRPC %s ep=%x call=%x:%x",
	     sp->hdr.flags & RXRPC_CLIENT_INITIATED ? "ToServer" : "ToClient",
	     sp->hdr.epoch, sp->hdr.cid, sp->hdr.callNumber);

	if (sp->hdr.type >= RXRPC_N_PACKET_TYPES ||
	    !((RXRPC_SUPPORTED_PACKET_TYPES >> sp->hdr.type) & 1)) {
		_proto("Rx Bad Packet Type %u", sp->hdr.type);
		goto bad_message;
	}

	if (sp->hdr.type == RXRPC_PACKET_TYPE_VERSION) {
		rxrpc_post_packet_to_local(local, skb);
		goto out;
	}

	if (sp->hdr.type == RXRPC_PACKET_TYPE_DATA &&
	    (sp->hdr.callNumber == 0 || sp->hdr.seq == 0))
		goto bad_message;

	rcu_read_lock();

	conn = rxrpc_find_connection_rcu(local, skb);
	if (!conn) {
		skb->priority = 0;
		goto cant_route_call;
	}

	/* Note the serial number skew here */
	skew = (int)sp->hdr.serial - (int)conn->hi_serial;
	if (skew >= 0) {
		if (skew > 0)
			conn->hi_serial = sp->hdr.serial;
		skb->priority = 0;
	} else {
		skew = -skew;
		skb->priority = min(skew, 65535);
	}

	if (sp->hdr.callNumber == 0) {
		/* Connection-level packet */
		_debug("CONN %p {%d}", conn, conn->debug_id);
		rxrpc_post_packet_to_conn(conn, skb);
		goto out_unlock;
	} else {
		/* Call-bound packets are routed by connection channel. */
		unsigned int channel = sp->hdr.cid & RXRPC_CHANNELMASK;
		struct rxrpc_channel *chan = &conn->channels[channel];
		struct rxrpc_call *call;

		/* Ignore really old calls */
		if (sp->hdr.callNumber < chan->last_call)
			goto discard_unlock;

		if (sp->hdr.callNumber == chan->last_call) {
			/* For the previous service call, if completed
			 * successfully, we discard all further packets.
			 */
			if (rxrpc_conn_is_service(call->conn) &&
			    (chan->last_type == RXRPC_PACKET_TYPE_ACK ||
			     sp->hdr.type == RXRPC_PACKET_TYPE_ABORT))
				goto discard_unlock;

			/* But otherwise we need to retransmit the final packet
			 * from data cached in the connection record.
			 */
			rxrpc_post_packet_to_conn(conn, skb);
			goto out_unlock;
		}

		call = rcu_dereference(chan->call);
		if (!call || atomic_read(&call->usage) == 0)
			goto cant_route_call;

		rxrpc_post_packet_to_call(call, skb);
		goto out_unlock;
	}

discard_unlock:
	rxrpc_free_skb(skb);
out_unlock:
	rcu_read_unlock();
out:
	return;

cant_route_call:
	rcu_read_unlock();

	_debug("can't route call");
	if (sp->hdr.flags & RXRPC_CLIENT_INITIATED &&
	    sp->hdr.type == RXRPC_PACKET_TYPE_DATA) {
		if (sp->hdr.seq == 1) {
			_debug("first packet");
			skb_queue_tail(&local->accept_queue, skb);
			rxrpc_queue_work(&local->processor);
			_leave(" [incoming]");
			return;
		}
		skb->priority = RX_INVALID_OPERATION;
	} else {
		skb->priority = RX_CALL_DEAD;
	}

	if (sp->hdr.type != RXRPC_PACKET_TYPE_ABORT) {
		_debug("reject type %d",sp->hdr.type);
		rxrpc_reject_packet(local, skb);
	} else {
		rxrpc_free_skb(skb);
	}
	_leave(" [no call]");
	return;

bad_message:
	skb->priority = RX_PROTOCOL_ERROR;
	rxrpc_reject_packet(local, skb);
	_leave(" [badmsg]");
}
