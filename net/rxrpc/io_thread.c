// SPDX-License-Identifier: GPL-2.0-or-later
/* RxRPC packet reception
 *
 * Copyright (C) 2007, 2016, 2022 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "ar-internal.h"

static int rxrpc_input_packet_on_conn(struct rxrpc_connection *conn,
				      struct sockaddr_rxrpc *peer_srx,
				      struct sk_buff *skb);

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
 * Process event packets targeted at a local endpoint.
 */
static void rxrpc_input_version(struct rxrpc_local *local, struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	char v;

	_enter("");

	rxrpc_see_skb(skb, rxrpc_skb_see_version);
	if (skb_copy_bits(skb, sizeof(struct rxrpc_wire_header), &v, 1) >= 0) {
		if (v == 0)
			rxrpc_send_version_request(local, &sp->hdr, skb);
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
	struct rxrpc_skb_priv *sp;
	struct rxrpc_peer *peer = NULL;
	struct sk_buff *skb = *_skb;
	int ret = 0;

	skb_pull(skb, sizeof(struct udphdr));

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

	trace_rxrpc_rx_packet(sp);

	switch (sp->hdr.type) {
	case RXRPC_PACKET_TYPE_VERSION:
		if (rxrpc_to_client(sp))
			return 0;
		rxrpc_input_version(local, skb);
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
		return true; /* Unsupported address type - discard. */

	if (peer_srx.transport.family != local->srx.transport.family &&
	    (peer_srx.transport.family == AF_INET &&
	     local->srx.transport.family != AF_INET6)) {
		pr_warn_ratelimited("AF_RXRPC: Protocol mismatch %u not %u\n",
				    peer_srx.transport.family,
				    local->srx.transport.family);
		return true; /* Wrong address type - discard. */
	}

	if (rxrpc_to_client(sp)) {
		rcu_read_lock();
		conn = rxrpc_find_client_connection_rcu(local, &peer_srx, skb);
		conn = rxrpc_get_connection_maybe(conn, rxrpc_conn_get_call_input);
		rcu_read_unlock();
		if (!conn) {
			trace_rxrpc_abort(0, "NCC", sp->hdr.cid,
					  sp->hdr.callNumber, sp->hdr.seq,
					  RXKADINCONSISTENCY, EBADMSG);
			goto protocol_error;
		}

		ret = rxrpc_input_packet_on_conn(conn, &peer_srx, skb);
		rxrpc_put_connection(conn, rxrpc_conn_put_call_input);
		return ret;
	}

	/* We need to look up service connections by the full protocol
	 * parameter set.  We look up the peer first as an intermediate step
	 * and then the connection from the peer's tree.
	 */
	rcu_read_lock();

	peer = rxrpc_lookup_peer_rcu(local, &peer_srx);
	if (!peer) {
		rcu_read_unlock();
		return rxrpc_new_incoming_call(local, NULL, NULL, &peer_srx, skb);
	}

	conn = rxrpc_find_service_conn_rcu(peer, skb);
	conn = rxrpc_get_connection_maybe(conn, rxrpc_conn_get_call_input);
	if (conn) {
		rcu_read_unlock();
		ret = rxrpc_input_packet_on_conn(conn, &peer_srx, skb);
		rxrpc_put_connection(conn, rxrpc_conn_put_call_input);
		return ret;
	}

	peer = rxrpc_get_peer_maybe(peer, rxrpc_peer_get_input);
	rcu_read_unlock();

	ret = rxrpc_new_incoming_call(local, peer, NULL, &peer_srx, skb);
	rxrpc_put_peer(peer, rxrpc_peer_put_input);
	if (ret < 0)
		goto reject_packet;
	return 0;

bad_message:
	trace_rxrpc_abort(0, "BAD", sp->hdr.cid, sp->hdr.callNumber, sp->hdr.seq,
			  RX_PROTOCOL_ERROR, EBADMSG);
protocol_error:
	skb->priority = RX_PROTOCOL_ERROR;
	skb->mark = RXRPC_SKB_MARK_REJECT_ABORT;
reject_packet:
	rxrpc_reject_packet(local, skb);
	return 0;
}

/*
 * Deal with a packet that's associated with an extant connection.
 */
static int rxrpc_input_packet_on_conn(struct rxrpc_connection *conn,
				      struct sockaddr_rxrpc *peer_srx,
				      struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct rxrpc_channel *chan;
	struct rxrpc_call *call = NULL;
	unsigned int channel;

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

	if (after(sp->hdr.serial, conn->hi_serial))
		conn->hi_serial = sp->hdr.serial;

	/* It's a connection-level packet if the call number is 0. */
	if (sp->hdr.callNumber == 0)
		return rxrpc_input_conn_packet(conn, skb);

	/* Call-bound packets are routed by connection channel. */
	channel = sp->hdr.cid & RXRPC_CHANNELMASK;
	chan = &conn->channels[channel];

	/* Ignore really old calls */
	if (sp->hdr.callNumber < chan->last_call)
		return 0;

	if (sp->hdr.callNumber == chan->last_call) {
		if (chan->call ||
		    sp->hdr.type == RXRPC_PACKET_TYPE_ABORT)
			return 0;

		/* For the previous service call, if completed successfully, we
		 * discard all further packets.
		 */
		if (rxrpc_conn_is_service(conn) &&
		    chan->last_type == RXRPC_PACKET_TYPE_ACK)
			return 0;

		/* But otherwise we need to retransmit the final packet from
		 * data cached in the connection record.
		 */
		if (sp->hdr.type == RXRPC_PACKET_TYPE_DATA)
			trace_rxrpc_rx_data(chan->call_debug_id,
					    sp->hdr.seq,
					    sp->hdr.serial,
					    sp->hdr.flags);
		rxrpc_input_conn_packet(conn, skb);
		return 0;
	}

	rcu_read_lock();
	call = rxrpc_try_get_call(rcu_dereference(chan->call),
				  rxrpc_call_get_input);
	rcu_read_unlock();

	if (sp->hdr.callNumber > chan->call_id) {
		if (rxrpc_to_client(sp)) {
			rxrpc_put_call(call, rxrpc_call_put_input);
			goto reject_packet;
		}

		if (call) {
			rxrpc_implicit_end_call(call, skb);
			rxrpc_put_call(call, rxrpc_call_put_input);
			call = NULL;
		}
	}

	if (!call) {
		if (rxrpc_to_client(sp))
			goto bad_message;
		if (rxrpc_new_incoming_call(conn->local, conn->peer, conn,
					    peer_srx, skb) == 0)
			return 0;
		goto reject_packet;
	}

	rxrpc_input_call_event(call, skb);
	rxrpc_put_call(call, rxrpc_call_put_input);
	return 0;

wrong_security:
	trace_rxrpc_abort(0, "SEC", sp->hdr.cid, sp->hdr.callNumber, sp->hdr.seq,
			  RXKADINCONSISTENCY, EBADMSG);
	skb->priority = RXKADINCONSISTENCY;
	goto post_abort;

reupgrade:
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
	rxrpc_reject_packet(conn->local, skb);
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
	bool should_stop;

	complete(&local->io_thread_ready);

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
		should_stop = kthread_should_stop();
		if (!skb_queue_empty(&local->rx_queue) ||
		    !list_empty(&local->call_attend_q)) {
			__set_current_state(TASK_RUNNING);
			continue;
		}

		if (should_stop)
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
