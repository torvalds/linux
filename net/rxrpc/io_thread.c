// SPDX-License-Identifier: GPL-2.0-or-later
/* RxRPC packet reception
 *
 * Copyright (C) 2007, 2016, 2022 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "ar-internal.h"

/*
 * handle data received on the local endpoint
 * - may be called in interrupt context
 *
 * [!] Note that as this is called from the encap_rcv hook, the socket is not
 * held locked by the caller and nothing prevents sk_user_data on the UDP from
 * being cleared in the middle of processing this function.
 *
 * Called with the RCU read lock held from the IP layer via UDP.
 */
int rxrpc_encap_rcv(struct sock *udp_sk, struct sk_buff *skb)
{
	struct rxrpc_local *local = rcu_dereference_sk_user_data(udp_sk);

	if (unlikely(!local)) {
		kfree_skb(skb);
		return 0;
	}
	if (skb->tstamp == 0)
		skb->tstamp = ktime_get_real();

	skb->mark = RXRPC_SKB_MARK_PACKET;
	rxrpc_new_skb(skb, rxrpc_skb_new_encap_rcv);
	skb_queue_tail(&local->rx_queue, skb);
	rxrpc_wake_up_io_thread(local);
	return 0;
}

/*
 * Handle an error received on the local endpoint.
 */
void rxrpc_error_report(struct sock *sk)
{
	struct rxrpc_local *local;
	struct sk_buff *skb;

	rcu_read_lock();
	local = rcu_dereference_sk_user_data(sk);
	if (unlikely(!local)) {
		rcu_read_unlock();
		return;
	}

	while ((skb = skb_dequeue(&sk->sk_error_queue))) {
		skb->mark = RXRPC_SKB_MARK_ERROR;
		rxrpc_new_skb(skb, rxrpc_skb_new_error_report);
		skb_queue_tail(&local->rx_queue, skb);
	}

	rxrpc_wake_up_io_thread(local);
	rcu_read_unlock();
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

	rxrpc_get_skb(skb, rxrpc_skb_get_conn_work);
	skb_queue_tail(&conn->rx_queue, skb);
	rxrpc_queue_conn(conn, rxrpc_conn_queue_rx_work);
}

/*
 * post endpoint-level events to the local endpoint
 * - this includes debug and version messages
 */
static void rxrpc_post_packet_to_local(struct rxrpc_local *local,
				       struct sk_buff *skb)
{
	_enter("%p,%p", local, skb);

	if (rxrpc_get_local_maybe(local, rxrpc_local_get_queue)) {
		rxrpc_get_skb(skb, rxrpc_skb_get_local_work);
		skb_queue_tail(&local->event_queue, skb);
		rxrpc_queue_local(local);
	}
}

/*
 * put a packet up for transport-level abort
 */
static void rxrpc_reject_packet(struct rxrpc_local *local, struct sk_buff *skb)
{
	if (rxrpc_get_local_maybe(local, rxrpc_local_get_queue)) {
		rxrpc_get_skb(skb, rxrpc_skb_get_reject_work);
		skb_queue_tail(&local->reject_queue, skb);
		rxrpc_queue_local(local);
	}
}

/*
 * Extract the wire header from a packet and translate the byte order.
 */
static noinline
int rxrpc_extract_header(struct rxrpc_skb_priv *sp, struct sk_buff *skb)
{
	struct rxrpc_wire_header whdr;

	/* dig out the RxRPC connection details */
	if (skb_copy_bits(skb, 0, &whdr, sizeof(whdr)) < 0) {
		trace_rxrpc_rx_eproto(NULL, sp->hdr.serial,
				      tracepoint_string("bad_hdr"));
		return -EBADMSG;
	}

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
 * Extract the abort code from an ABORT packet and stash it in skb->priority.
 */
static bool rxrpc_extract_abort(struct sk_buff *skb)
{
	__be32 wtmp;

	if (skb_copy_bits(skb, sizeof(struct rxrpc_wire_header),
			  &wtmp, sizeof(wtmp)) < 0)
		return false;
	skb->priority = ntohl(wtmp);
	return true;
}

/*
 * Process packets received on the local endpoint
 */
static int rxrpc_input_packet(struct rxrpc_local *local, struct sk_buff **_skb)
{
	struct rxrpc_connection *conn;
	struct sockaddr_rxrpc peer_srx;
	struct rxrpc_channel *chan;
	struct rxrpc_call *call = NULL;
	struct rxrpc_skb_priv *sp;
	struct rxrpc_peer *peer = NULL;
	struct rxrpc_sock *rx = NULL;
	struct sk_buff *skb = *_skb;
	unsigned int channel;

	if (skb->tstamp == 0)
		skb->tstamp = ktime_get_real();

	skb_pull(skb, sizeof(struct udphdr));

	/* The UDP protocol already released all skb resources;
	 * we are free to add our own data there.
	 */
	sp = rxrpc_skb(skb);

	/* dig out the RxRPC connection details */
	if (rxrpc_extract_header(sp, skb) < 0)
		goto bad_message;

	if (IS_ENABLED(CONFIG_AF_RXRPC_INJECT_LOSS)) {
		static int lose;
		if ((lose++ & 7) == 7) {
			trace_rxrpc_rx_lose(sp);
			return 0;
		}
	}

	if (skb->tstamp == 0)
		skb->tstamp = ktime_get_real();
	trace_rxrpc_rx_packet(sp);

	switch (sp->hdr.type) {
	case RXRPC_PACKET_TYPE_VERSION:
		if (rxrpc_to_client(sp))
			return 0;
		rxrpc_post_packet_to_local(local, skb);
		return 0;

	case RXRPC_PACKET_TYPE_BUSY:
		if (rxrpc_to_server(sp))
			return 0;
		fallthrough;
	case RXRPC_PACKET_TYPE_ACK:
	case RXRPC_PACKET_TYPE_ACKALL:
		if (sp->hdr.callNumber == 0)
			goto bad_message;
		break;
	case RXRPC_PACKET_TYPE_ABORT:
		if (!rxrpc_extract_abort(skb))
			return 0; /* Just discard if malformed */
		break;

	case RXRPC_PACKET_TYPE_DATA:
		if (sp->hdr.callNumber == 0 ||
		    sp->hdr.seq == 0)
			goto bad_message;

		/* Unshare the packet so that it can be modified for in-place
		 * decryption.
		 */
		if (sp->hdr.securityIndex != 0) {
			skb = skb_unshare(skb, GFP_ATOMIC);
			if (!skb) {
				rxrpc_eaten_skb(*_skb, rxrpc_skb_eaten_by_unshare_nomem);
				*_skb = NULL;
				return 0;
			}

			if (skb != *_skb) {
				rxrpc_eaten_skb(*_skb, rxrpc_skb_eaten_by_unshare);
				*_skb = skb;
				rxrpc_new_skb(skb, rxrpc_skb_new_unshared);
				sp = rxrpc_skb(skb);
			}
		}
		break;

	case RXRPC_PACKET_TYPE_CHALLENGE:
		if (rxrpc_to_server(sp))
			return 0;
		break;
	case RXRPC_PACKET_TYPE_RESPONSE:
		if (rxrpc_to_client(sp))
			return 0;
		break;

		/* Packet types 9-11 should just be ignored. */
	case RXRPC_PACKET_TYPE_PARAMS:
	case RXRPC_PACKET_TYPE_10:
	case RXRPC_PACKET_TYPE_11:
		return 0;

	default:
		goto bad_message;
	}

	if (sp->hdr.serviceId == 0)
		goto bad_message;

	if (WARN_ON_ONCE(rxrpc_extract_addr_from_skb(&peer_srx, skb) < 0))
		return 0; /* Unsupported address type - discard. */

	if (peer_srx.transport.family != local->srx.transport.family &&
	    (peer_srx.transport.family == AF_INET &&
	     local->srx.transport.family != AF_INET6)) {
		pr_warn_ratelimited("AF_RXRPC: Protocol mismatch %u not %u\n",
				    peer_srx.transport.family,
				    local->srx.transport.family);
		return 0; /* Wrong address type - discard. */
	}

	rcu_read_lock();

	if (rxrpc_to_server(sp)) {
		/* Weed out packets to services we're not offering.  Packets
		 * that would begin a call are explicitly rejected and the rest
		 * are just discarded.
		 */
		rx = rcu_dereference(local->service);
		if (!rx || (sp->hdr.serviceId != rx->srx.srx_service &&
			    sp->hdr.serviceId != rx->second_service)
		    ) {
			rcu_read_unlock();
			if (sp->hdr.type == RXRPC_PACKET_TYPE_DATA &&
			    sp->hdr.seq == 1)
				goto unsupported_service;
			return 0;
		}
	}

	conn = rxrpc_find_connection_rcu(local, &peer_srx, skb, &peer);
	if (conn) {
		if (sp->hdr.securityIndex != conn->security_ix)
			goto wrong_security;

		if (sp->hdr.serviceId != conn->service_id) {
			int old_id;

			if (!test_bit(RXRPC_CONN_PROBING_FOR_UPGRADE, &conn->flags))
				goto reupgrade;
			old_id = cmpxchg(&conn->service_id, conn->orig_service_id,
					 sp->hdr.serviceId);

			if (old_id != conn->orig_service_id &&
			    old_id != sp->hdr.serviceId)
				goto reupgrade;
		}

		if (sp->hdr.callNumber == 0) {
			/* Connection-level packet */
			_debug("CONN %p {%d}", conn, conn->debug_id);
			conn = rxrpc_get_connection_maybe(conn, rxrpc_conn_get_conn_input);
			rcu_read_unlock();
			if (conn) {
				rxrpc_post_packet_to_conn(conn, skb);
				rxrpc_put_connection(conn, rxrpc_conn_put_conn_input);
			}
			return 0;
		}

		if ((int)sp->hdr.serial - (int)conn->hi_serial > 0)
			conn->hi_serial = sp->hdr.serial;

		/* Call-bound packets are routed by connection channel. */
		channel = sp->hdr.cid & RXRPC_CHANNELMASK;
		chan = &conn->channels[channel];

		/* Ignore really old calls */
		if (sp->hdr.callNumber < chan->last_call) {
			rcu_read_unlock();
			return 0;
		}

		if (sp->hdr.callNumber == chan->last_call) {
			if (chan->call ||
			    sp->hdr.type == RXRPC_PACKET_TYPE_ABORT) {
				rcu_read_unlock();
				return 0;
			}

			/* For the previous service call, if completed
			 * successfully, we discard all further packets.
			 */
			if (rxrpc_conn_is_service(conn) &&
			    chan->last_type == RXRPC_PACKET_TYPE_ACK) {
				rcu_read_unlock();
				return 0;
			}

			/* But otherwise we need to retransmit the final packet
			 * from data cached in the connection record.
			 */
			if (sp->hdr.type == RXRPC_PACKET_TYPE_DATA)
				trace_rxrpc_rx_data(chan->call_debug_id,
						    sp->hdr.seq,
						    sp->hdr.serial,
						    sp->hdr.flags);
			conn = rxrpc_get_connection_maybe(conn, rxrpc_conn_get_call_input);
			rcu_read_unlock();
			if (conn) {
				rxrpc_post_packet_to_conn(conn, skb);
				rxrpc_put_connection(conn, rxrpc_conn_put_call_input);
			}
			return 0;
		}

		call = rcu_dereference(chan->call);

		if (sp->hdr.callNumber > chan->call_id) {
			if (rxrpc_to_client(sp)) {
				rcu_read_unlock();
				goto reject_packet;
			}
			if (call) {
				rxrpc_input_implicit_end_call(conn, call);
				chan->call = NULL;
				call = NULL;
			}
		}

		if (call && !rxrpc_try_get_call(call, rxrpc_call_get_input))
			call = NULL;

		if (call) {
			if (sp->hdr.serviceId != call->dest_srx.srx_service)
				call->dest_srx.srx_service = sp->hdr.serviceId;
			if ((int)sp->hdr.serial - (int)call->rx_serial > 0)
				call->rx_serial = sp->hdr.serial;
			if (!test_bit(RXRPC_CALL_RX_HEARD, &call->flags))
				set_bit(RXRPC_CALL_RX_HEARD, &call->flags);
		}
	}

	if (!call) {
		if (rxrpc_to_client(sp) ||
		    sp->hdr.type != RXRPC_PACKET_TYPE_DATA) {
			rcu_read_unlock();
			goto bad_message;
		}
		if (sp->hdr.seq != 1) {
			rcu_read_unlock();
			return 0;
		}
		call = rxrpc_new_incoming_call(local, rx, &peer_srx, skb);
		if (!call) {
			rcu_read_unlock();
			goto reject_packet;
		}
	}

	rcu_read_unlock();

	/* Process a call packet. */
	rxrpc_input_call_event(call, skb);
	rxrpc_put_call(call, rxrpc_call_put_input);
	trace_rxrpc_rx_done(0, 0);
	return 0;

wrong_security:
	rcu_read_unlock();
	trace_rxrpc_abort(0, "SEC", sp->hdr.cid, sp->hdr.callNumber, sp->hdr.seq,
			  RXKADINCONSISTENCY, EBADMSG);
	skb->priority = RXKADINCONSISTENCY;
	goto post_abort;

unsupported_service:
	trace_rxrpc_abort(0, "INV", sp->hdr.cid, sp->hdr.callNumber, sp->hdr.seq,
			  RX_INVALID_OPERATION, EOPNOTSUPP);
	skb->priority = RX_INVALID_OPERATION;
	goto post_abort;

reupgrade:
	rcu_read_unlock();
	trace_rxrpc_abort(0, "UPG", sp->hdr.cid, sp->hdr.callNumber, sp->hdr.seq,
			  RX_PROTOCOL_ERROR, EBADMSG);
	goto protocol_error;

bad_message:
	trace_rxrpc_abort(0, "BAD", sp->hdr.cid, sp->hdr.callNumber, sp->hdr.seq,
			  RX_PROTOCOL_ERROR, EBADMSG);
protocol_error:
	skb->priority = RX_PROTOCOL_ERROR;
post_abort:
	skb->mark = RXRPC_SKB_MARK_REJECT_ABORT;
reject_packet:
	rxrpc_reject_packet(local, skb);
	return 0;
}

/*
 * I/O and event handling thread.
 */
int rxrpc_io_thread(void *data)
{
	struct sk_buff_head rx_queue;
	struct rxrpc_local *local = data;
	struct rxrpc_call *call;
	struct sk_buff *skb;

	skb_queue_head_init(&rx_queue);

	set_user_nice(current, MIN_NICE);

	for (;;) {
		rxrpc_inc_stat(local->rxnet, stat_io_loop);

		/* Deal with calls that want immediate attention. */
		if ((call = list_first_entry_or_null(&local->call_attend_q,
						     struct rxrpc_call,
						     attend_link))) {
			spin_lock_bh(&local->lock);
			list_del_init(&call->attend_link);
			spin_unlock_bh(&local->lock);

			trace_rxrpc_call_poked(call);
			rxrpc_input_call_event(call, NULL);
			rxrpc_put_call(call, rxrpc_call_put_poke);
			continue;
		}

		/* Process received packets and errors. */
		if ((skb = __skb_dequeue(&rx_queue))) {
			switch (skb->mark) {
			case RXRPC_SKB_MARK_PACKET:
				skb->priority = 0;
				rxrpc_input_packet(local, &skb);
				trace_rxrpc_rx_done(skb->mark, skb->priority);
				rxrpc_free_skb(skb, rxrpc_skb_put_input);
				break;
			case RXRPC_SKB_MARK_ERROR:
				rxrpc_input_error(local, skb);
				rxrpc_free_skb(skb, rxrpc_skb_put_error_report);
				break;
			default:
				WARN_ON_ONCE(1);
				rxrpc_free_skb(skb, rxrpc_skb_put_unknown);
				break;
			}
			continue;
		}

		if (!skb_queue_empty(&local->rx_queue)) {
			spin_lock_irq(&local->rx_queue.lock);
			skb_queue_splice_tail_init(&local->rx_queue, &rx_queue);
			spin_unlock_irq(&local->rx_queue.lock);
			continue;
		}

		set_current_state(TASK_INTERRUPTIBLE);
		if (!skb_queue_empty(&local->rx_queue) ||
		    !list_empty(&local->call_attend_q)) {
			__set_current_state(TASK_RUNNING);
			continue;
		}

		if (kthread_should_stop())
			break;
		schedule();
	}

	__set_current_state(TASK_RUNNING);
	rxrpc_see_local(local, rxrpc_local_stop);
	rxrpc_destroy_local(local);
	local->io_thread = NULL;
	rxrpc_see_local(local, rxrpc_local_stopped);
	return 0;
}
