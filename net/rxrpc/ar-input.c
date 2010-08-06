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

unsigned long rxrpc_ack_timeout = 1;

const char *rxrpc_pkts[] = {
	"?00",
	"DATA", "ACK", "BUSY", "ABORT", "ACKALL", "CHALL", "RESP", "DEBUG",
	"?09", "?10", "?11", "?12", "?13", "?14", "?15"
};

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
	int skb_len, ret;

	_enter(",,%d,%d", force, terminal);

	ASSERT(!irqs_disabled());

	sp = rxrpc_skb(skb);
	ASSERTCMP(sp->call, ==, call);

	/* if we've already posted the terminal message for a call, then we
	 * don't post any more */
	if (test_bit(RXRPC_CALL_TERMINAL_MSG, &call->flags)) {
		_debug("already terminated");
		ASSERTCMP(call->state, >=, RXRPC_CALL_COMPLETE);
		skb->destructor = NULL;
		sp->call = NULL;
		rxrpc_put_call(call);
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
//		    (unsigned) sk->sk_rcvbuf)
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

			/* Cache the SKB length before we tack it onto the
			 * receive queue.  Once it is added it no longer
			 * belongs to us and may be freed by other threads of
			 * control pulling packets from the queue */
			skb_len = skb->len;

			_net("post skb %p", skb);
			__skb_queue_tail(&sk->sk_receive_queue, skb);
			spin_unlock_bh(&sk->sk_receive_queue.lock);

			if (!sock_flag(sk, SOCK_DEAD))
				sk->sk_data_ready(sk, skb_len);
		}
		skb = NULL;
	} else {
		spin_unlock_bh(&sk->sk_receive_queue.lock);
	}
	ret = 0;

out:
	/* release the socket buffer */
	if (skb) {
		skb->destructor = NULL;
		sp->call = NULL;
		rxrpc_put_call(call);
		rxrpc_free_skb(skb);
	}

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

	_enter("{%u,%u},,{%u}", call->rx_data_post, call->rx_first_oos, seq);

	sp = rxrpc_skb(skb);
	ASSERTCMP(sp->call, ==, NULL);

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
	if (call->conn->security)
		goto enqueue_packet;

	sp->call = call;
	rxrpc_get_call(call);
	terminal = ((sp->hdr.flags & RXRPC_LAST_PACKET) &&
		    !(sp->hdr.flags & RXRPC_CLIENT_INITIATED));
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

	_debug("post #%u", seq);
	ASSERTCMP(call->rx_data_post, ==, seq);
	call->rx_data_post++;

	if (sp->hdr.flags & RXRPC_LAST_PACKET)
		set_bit(RXRPC_CALL_RCVD_LAST, &call->flags);

	/* if we've reached an out of sequence packet then we need to drain
	 * that queue into the socket Rx queue now */
	if (call->rx_data_post == call->rx_first_oos) {
		_debug("drain rx oos now");
		read_lock(&call->state_lock);
		if (call->state < RXRPC_CALL_COMPLETE &&
		    !test_and_set_bit(RXRPC_CALL_DRAIN_RX_OOS, &call->events))
			rxrpc_queue_call(call);
		read_unlock(&call->state_lock);
	}

	spin_unlock(&call->lock);
	atomic_inc(&call->ackr_not_idle);
	rxrpc_propose_ACK(call, RXRPC_ACK_DELAY, sp->hdr.serial, false);
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
	__rxrpc_propose_ACK(call, ack, sp->hdr.serial, true);
discard:
	spin_unlock(&call->lock);
	rxrpc_free_skb(skb);
	_leave(" = 0 [discarded]");
	return 0;

enqueue_and_ack:
	__rxrpc_propose_ACK(call, ack, sp->hdr.serial, true);
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
		set_bit(RXRPC_CALL_RCVD_ACKALL, &call->events);
		write_unlock_bh(&call->state_lock);

		if (try_to_del_timer_sync(&call->resend_timer) >= 0) {
			clear_bit(RXRPC_CALL_RESEND_TIMER, &call->events);
			clear_bit(RXRPC_CALL_RESEND, &call->events);
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
	__be32 _abort_code;
	u32 serial, hi_serial, seq, abort_code;

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

	/* track the latest serial number on this connection for ACK packet
	 * information */
	serial = ntohl(sp->hdr.serial);
	hi_serial = atomic_read(&call->conn->hi_serial);
	while (serial > hi_serial)
		hi_serial = atomic_cmpxchg(&call->conn->hi_serial, hi_serial,
					   serial);

	/* request ACK generation for any ACK or DATA packet that requests
	 * it */
	if (sp->hdr.flags & RXRPC_REQUEST_ACK) {
		_proto("ACK Requested on %%%u", serial);
		rxrpc_propose_ACK(call, RXRPC_ACK_REQUESTED, sp->hdr.serial,
				  !(sp->hdr.flags & RXRPC_MORE_PACKETS));
	}

	switch (sp->hdr.type) {
	case RXRPC_PACKET_TYPE_ABORT:
		_debug("abort");

		if (skb_copy_bits(skb, 0, &_abort_code,
				  sizeof(_abort_code)) < 0)
			goto protocol_error;

		abort_code = ntohl(_abort_code);
		_proto("Rx ABORT %%%u { %x }", serial, abort_code);

		write_lock_bh(&call->state_lock);
		if (call->state < RXRPC_CALL_COMPLETE) {
			call->state = RXRPC_CALL_REMOTELY_ABORTED;
			call->abort_code = abort_code;
			set_bit(RXRPC_CALL_RCVD_ABORT, &call->events);
			rxrpc_queue_call(call);
		}
		goto free_packet_unlock;

	case RXRPC_PACKET_TYPE_BUSY:
		_proto("Rx BUSY %%%u", serial);

		if (call->conn->out_clientflag)
			goto protocol_error;

		write_lock_bh(&call->state_lock);
		switch (call->state) {
		case RXRPC_CALL_CLIENT_SEND_REQUEST:
			call->state = RXRPC_CALL_SERVER_BUSY;
			set_bit(RXRPC_CALL_RCVD_BUSY, &call->events);
			rxrpc_queue_call(call);
		case RXRPC_CALL_SERVER_BUSY:
			goto free_packet_unlock;
		default:
			goto protocol_error_locked;
		}

	default:
		_proto("Rx %s %%%u", rxrpc_pkts[sp->hdr.type], serial);
		goto protocol_error;

	case RXRPC_PACKET_TYPE_DATA:
		seq = ntohl(sp->hdr.seq);

		_proto("Rx DATA %%%u { #%u }", serial, seq);

		if (seq == 0)
			goto protocol_error;

		call->ackr_prev_seq = sp->hdr.seq;

		/* received data implicitly ACKs all of the request packets we
		 * sent when we're acting as a client */
		if (call->state == RXRPC_CALL_CLIENT_AWAIT_REPLY)
			rxrpc_assume_implicit_ackall(call, serial);

		switch (rxrpc_fast_process_data(call, skb, seq)) {
		case 0:
			skb = NULL;
			goto done;

		default:
			BUG();

			/* data packet received beyond the last packet */
		case -EBADMSG:
			goto protocol_error;
		}

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
		call->abort_code = RX_PROTOCOL_ERROR;
		set_bit(RXRPC_CALL_ABORT, &call->events);
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

		sp->hdr.seq	= htonl(ntohl(sp->hdr.seq) + 1);
		sp->hdr.serial	= htonl(ntohl(sp->hdr.serial) + 1);
		sp->hdr.flags	= jhdr.flags;
		sp->hdr._rsvd	= jhdr._rsvd;

		_proto("Rx DATA Jumbo %%%u", ntohl(sp->hdr.serial) - 1);

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
		call->abort_code = RX_PROTOCOL_ERROR;
		set_bit(RXRPC_CALL_ABORT, &call->events);
		rxrpc_queue_call(call);
	}
	write_unlock_bh(&call->state_lock);
	_leave("");
}

/*
 * post an incoming packet to the appropriate call/socket to deal with
 * - must get rid of the sk_buff, either by freeing it or by queuing it
 */
static void rxrpc_post_packet_to_call(struct rxrpc_connection *conn,
				      struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp;
	struct rxrpc_call *call;
	struct rb_node *p;
	__be32 call_id;

	_enter("%p,%p", conn, skb);

	read_lock_bh(&conn->lock);

	sp = rxrpc_skb(skb);

	/* look at extant calls by channel number first */
	call = conn->channels[ntohl(sp->hdr.cid) & RXRPC_CHANNELMASK];
	if (!call || call->call_id != sp->hdr.callNumber)
		goto call_not_extant;

	_debug("extant call [%d]", call->state);
	ASSERTCMP(call->conn, ==, conn);

	read_lock(&call->state_lock);
	switch (call->state) {
	case RXRPC_CALL_LOCALLY_ABORTED:
		if (!test_and_set_bit(RXRPC_CALL_ABORT, &call->events))
			rxrpc_queue_call(call);
	case RXRPC_CALL_REMOTELY_ABORTED:
	case RXRPC_CALL_NETWORK_ERROR:
	case RXRPC_CALL_DEAD:
		goto free_unlock;
	default:
		break;
	}

	read_unlock(&call->state_lock);
	rxrpc_get_call(call);
	read_unlock_bh(&conn->lock);

	if (sp->hdr.type == RXRPC_PACKET_TYPE_DATA &&
	    sp->hdr.flags & RXRPC_JUMBO_PACKET)
		rxrpc_process_jumbo_packet(call, skb);
	else
		rxrpc_fast_process_packet(call, skb);

	rxrpc_put_call(call);
	goto done;

call_not_extant:
	/* search the completed calls in case what we're dealing with is
	 * there */
	_debug("call not extant");

	call_id = sp->hdr.callNumber;
	p = conn->calls.rb_node;
	while (p) {
		call = rb_entry(p, struct rxrpc_call, conn_node);

		if (call_id < call->call_id)
			p = p->rb_left;
		else if (call_id > call->call_id)
			p = p->rb_right;
		else
			goto found_completed_call;
	}

dead_call:
	/* it's a either a really old call that we no longer remember or its a
	 * new incoming call */
	read_unlock_bh(&conn->lock);

	if (sp->hdr.flags & RXRPC_CLIENT_INITIATED &&
	    sp->hdr.seq == cpu_to_be32(1)) {
		_debug("incoming call");
		skb_queue_tail(&conn->trans->local->accept_queue, skb);
		rxrpc_queue_work(&conn->trans->local->acceptor);
		goto done;
	}

	_debug("dead call");
	skb->priority = RX_CALL_DEAD;
	rxrpc_reject_packet(conn->trans->local, skb);
	goto done;

	/* resend last packet of a completed call
	 * - client calls may have been aborted or ACK'd
	 * - server calls may have been aborted
	 */
found_completed_call:
	_debug("completed call");

	if (atomic_read(&call->usage) == 0)
		goto dead_call;

	/* synchronise any state changes */
	read_lock(&call->state_lock);
	ASSERTIFCMP(call->state != RXRPC_CALL_CLIENT_FINAL_ACK,
		    call->state, >=, RXRPC_CALL_COMPLETE);

	if (call->state == RXRPC_CALL_LOCALLY_ABORTED ||
	    call->state == RXRPC_CALL_REMOTELY_ABORTED ||
	    call->state == RXRPC_CALL_DEAD) {
		read_unlock(&call->state_lock);
		goto dead_call;
	}

	if (call->conn->in_clientflag) {
		read_unlock(&call->state_lock);
		goto dead_call; /* complete server call */
	}

	_debug("final ack again");
	rxrpc_get_call(call);
	set_bit(RXRPC_CALL_ACK_FINAL, &call->events);
	rxrpc_queue_call(call);

free_unlock:
	read_unlock(&call->state_lock);
	read_unlock_bh(&conn->lock);
	rxrpc_free_skb(skb);
done:
	_leave("");
}

/*
 * post connection-level events to the connection
 * - this includes challenges, responses and some aborts
 */
static void rxrpc_post_packet_to_conn(struct rxrpc_connection *conn,
				      struct sk_buff *skb)
{
	_enter("%p,%p", conn, skb);

	atomic_inc(&conn->usage);
	skb_queue_tail(&conn->rx_queue, skb);
	rxrpc_queue_conn(conn);
}

/*
 * handle data received on the local endpoint
 * - may be called in interrupt context
 */
void rxrpc_data_ready(struct sock *sk, int count)
{
	struct rxrpc_connection *conn;
	struct rxrpc_transport *trans;
	struct rxrpc_skb_priv *sp;
	struct rxrpc_local *local;
	struct rxrpc_peer *peer;
	struct sk_buff *skb;
	int ret;

	_enter("%p, %d", sk, count);

	ASSERT(!irqs_disabled());

	read_lock_bh(&rxrpc_local_lock);
	local = sk->sk_user_data;
	if (local && atomic_read(&local->usage) > 0)
		rxrpc_get_local(local);
	else
		local = NULL;
	read_unlock_bh(&rxrpc_local_lock);
	if (!local) {
		_leave(" [local dead]");
		return;
	}

	skb = skb_recv_datagram(sk, 0, 1, &ret);
	if (!skb) {
		rxrpc_put_local(local);
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
		rxrpc_put_local(local);
		UDP_INC_STATS_BH(&init_net, UDP_MIB_INERRORS, 0);
		_leave(" [CSUM failed]");
		return;
	}

	UDP_INC_STATS_BH(&init_net, UDP_MIB_INDATAGRAMS, 0);

	/* the socket buffer we have is owned by UDP, with UDP's data all over
	 * it, but we really want our own */
	skb_orphan(skb);
	sp = rxrpc_skb(skb);
	memset(sp, 0, sizeof(*sp));

	_net("Rx UDP packet from %08x:%04hu",
	     ntohl(ip_hdr(skb)->saddr), ntohs(udp_hdr(skb)->source));

	/* dig out the RxRPC connection details */
	if (skb_copy_bits(skb, sizeof(struct udphdr), &sp->hdr,
			  sizeof(sp->hdr)) < 0)
		goto bad_message;
	if (!pskb_pull(skb, sizeof(struct udphdr) + sizeof(sp->hdr)))
		BUG();

	_net("Rx RxRPC %s ep=%x call=%x:%x",
	     sp->hdr.flags & RXRPC_CLIENT_INITIATED ? "ToServer" : "ToClient",
	     ntohl(sp->hdr.epoch),
	     ntohl(sp->hdr.cid),
	     ntohl(sp->hdr.callNumber));

	if (sp->hdr.type == 0 || sp->hdr.type >= RXRPC_N_PACKET_TYPES) {
		_proto("Rx Bad Packet Type %u", sp->hdr.type);
		goto bad_message;
	}

	if (sp->hdr.type == RXRPC_PACKET_TYPE_DATA &&
	    (sp->hdr.callNumber == 0 || sp->hdr.seq == 0))
		goto bad_message;

	peer = rxrpc_find_peer(local, ip_hdr(skb)->saddr, udp_hdr(skb)->source);
	if (IS_ERR(peer))
		goto cant_route_call;

	trans = rxrpc_find_transport(local, peer);
	rxrpc_put_peer(peer);
	if (!trans)
		goto cant_route_call;

	conn = rxrpc_find_connection(trans, &sp->hdr);
	rxrpc_put_transport(trans);
	if (!conn)
		goto cant_route_call;

	_debug("CONN %p {%d}", conn, conn->debug_id);

	if (sp->hdr.callNumber == 0)
		rxrpc_post_packet_to_conn(conn, skb);
	else
		rxrpc_post_packet_to_call(conn, skb);
	rxrpc_put_connection(conn);
	rxrpc_put_local(local);
	return;

cant_route_call:
	_debug("can't route call");
	if (sp->hdr.flags & RXRPC_CLIENT_INITIATED &&
	    sp->hdr.type == RXRPC_PACKET_TYPE_DATA) {
		if (sp->hdr.seq == cpu_to_be32(1)) {
			_debug("first packet");
			skb_queue_tail(&local->accept_queue, skb);
			rxrpc_queue_work(&local->acceptor);
			rxrpc_put_local(local);
			_leave(" [incoming]");
			return;
		}
		skb->priority = RX_INVALID_OPERATION;
	} else {
		skb->priority = RX_CALL_DEAD;
	}

	_debug("reject");
	rxrpc_reject_packet(local, skb);
	rxrpc_put_local(local);
	_leave(" [no call]");
	return;

bad_message:
	skb->priority = RX_PROTOCOL_ERROR;
	rxrpc_reject_packet(local, skb);
	rxrpc_put_local(local);
	_leave(" [badmsg]");
}
