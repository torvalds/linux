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
	struct sk_buff_head *rx_queue;
	struct rxrpc_local *local = rcu_dereference_sk_user_data(udp_sk);
	struct task_struct *io_thread;

	if (unlikely(!local)) {
		kfree_skb(skb);
		return 0;
	}
	io_thread = READ_ONCE(local->io_thread);
	if (!io_thread) {
		kfree_skb(skb);
		return 0;
	}
	if (skb->tstamp == 0)
		skb->tstamp = ktime_get_real();

	skb->mark = RXRPC_SKB_MARK_PACKET;
	rxrpc_new_skb(skb, rxrpc_skb_new_encap_rcv);
	rx_queue = &local->rx_queue;
#ifdef CONFIG_AF_RXRPC_INJECT_RX_DELAY
	if (rxrpc_inject_rx_delay ||
	    !skb_queue_empty(&local->rx_delay_queue)) {
		skb->tstamp = ktime_add_ms(skb->tstamp, rxrpc_inject_rx_delay);
		rx_queue = &local->rx_delay_queue;
	}
#endif

	skb_queue_tail(rx_queue, skb);
	wake_up_process(io_thread);
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
 * Directly produce an abort from a packet.
 */
bool rxrpc_direct_abort(struct sk_buff *skb, enum rxrpc_abort_reason why,
			s32 abort_code, int err)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);

	trace_rxrpc_abort(0, why, sp->hdr.cid, sp->hdr.callNumber, sp->hdr.seq,
			  abort_code, err);
	skb->mark = RXRPC_SKB_MARK_REJECT_ABORT;
	skb->priority = abort_code;
	return false;
}

/*
 * Directly produce a connection abort from a packet.
 */
bool rxrpc_direct_conn_abort(struct sk_buff *skb, enum rxrpc_abort_reason why,
			     s32 abort_code, int err)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);

	trace_rxrpc_abort(0, why, sp->hdr.cid, 0, sp->hdr.seq, abort_code, err);
	skb->mark = RXRPC_SKB_MARK_REJECT_CONN_ABORT;
	skb->priority = abort_code;
	return false;
}

static bool rxrpc_bad_message(struct sk_buff *skb, enum rxrpc_abort_reason why)
{
	return rxrpc_direct_abort(skb, why, RX_PROTOCOL_ERROR, -EBADMSG);
}

#define just_discard true

/*
 * Process event packets targeted at a local endpoint.
 */
static bool rxrpc_input_version(struct rxrpc_local *local, struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	char v;

	_enter("");

	rxrpc_see_skb(skb, rxrpc_skb_see_version);
	if (skb_copy_bits(skb, sizeof(struct rxrpc_wire_header), &v, 1) >= 0) {
		if (v == 0)
			rxrpc_send_version_request(local, &sp->hdr, skb);
	}

	return true;
}

/*
 * Extract the wire header from a packet and translate the byte order.
 */
static bool rxrpc_extract_header(struct rxrpc_skb_priv *sp,
				 struct sk_buff *skb)
{
	struct rxrpc_wire_header whdr;
	struct rxrpc_ackpacket ack;

	/* dig out the RxRPC connection details */
	if (skb_copy_bits(skb, 0, &whdr, sizeof(whdr)) < 0)
		return rxrpc_bad_message(skb, rxrpc_badmsg_short_hdr);

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

	if (sp->hdr.type == RXRPC_PACKET_TYPE_ACK) {
		if (skb_copy_bits(skb, sizeof(whdr), &ack, sizeof(ack)) < 0)
			return rxrpc_bad_message(skb, rxrpc_badmsg_short_ack);
		sp->ack.first_ack	= ntohl(ack.firstPacket);
		sp->ack.prev_ack	= ntohl(ack.previousPacket);
		sp->ack.acked_serial	= ntohl(ack.serial);
		sp->ack.reason		= ack.reason;
		sp->ack.nr_acks		= ack.nAcks;
	}
	return true;
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
static bool rxrpc_input_packet(struct rxrpc_local *local, struct sk_buff **_skb)
{
	struct rxrpc_connection *conn;
	struct sockaddr_rxrpc peer_srx;
	struct rxrpc_skb_priv *sp;
	struct rxrpc_peer *peer = NULL;
	struct sk_buff *skb = *_skb;
	bool ret = false;

	skb_pull(skb, sizeof(struct udphdr));

	sp = rxrpc_skb(skb);

	/* dig out the RxRPC connection details */
	if (!rxrpc_extract_header(sp, skb))
		return just_discard;

	if (IS_ENABLED(CONFIG_AF_RXRPC_INJECT_LOSS)) {
		static int lose;
		if ((lose++ & 7) == 7) {
			trace_rxrpc_rx_lose(sp);
			return just_discard;
		}
	}

	trace_rxrpc_rx_packet(sp);

	switch (sp->hdr.type) {
	case RXRPC_PACKET_TYPE_VERSION:
		if (rxrpc_to_client(sp))
			return just_discard;
		return rxrpc_input_version(local, skb);

	case RXRPC_PACKET_TYPE_BUSY:
		if (rxrpc_to_server(sp))
			return just_discard;
		fallthrough;
	case RXRPC_PACKET_TYPE_ACK:
	case RXRPC_PACKET_TYPE_ACKALL:
		if (sp->hdr.callNumber == 0)
			return rxrpc_bad_message(skb, rxrpc_badmsg_zero_call);
		break;
	case RXRPC_PACKET_TYPE_ABORT:
		if (!rxrpc_extract_abort(skb))
			return just_discard; /* Just discard if malformed */
		break;

	case RXRPC_PACKET_TYPE_DATA:
		if (sp->hdr.callNumber == 0)
			return rxrpc_bad_message(skb, rxrpc_badmsg_zero_call);
		if (sp->hdr.seq == 0)
			return rxrpc_bad_message(skb, rxrpc_badmsg_zero_seq);

		/* Unshare the packet so that it can be modified for in-place
		 * decryption.
		 */
		if (sp->hdr.securityIndex != 0) {
			skb = skb_unshare(skb, GFP_ATOMIC);
			if (!skb) {
				rxrpc_eaten_skb(*_skb, rxrpc_skb_eaten_by_unshare_nomem);
				*_skb = NULL;
				return just_discard;
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
			return just_discard;
		break;
	case RXRPC_PACKET_TYPE_RESPONSE:
		if (rxrpc_to_client(sp))
			return just_discard;
		break;

		/* Packet types 9-11 should just be ignored. */
	case RXRPC_PACKET_TYPE_PARAMS:
	case RXRPC_PACKET_TYPE_10:
	case RXRPC_PACKET_TYPE_11:
		return just_discard;

	default:
		return rxrpc_bad_message(skb, rxrpc_badmsg_unsupported_packet);
	}

	if (sp->hdr.serviceId == 0)
		return rxrpc_bad_message(skb, rxrpc_badmsg_zero_service);

	if (WARN_ON_ONCE(rxrpc_extract_addr_from_skb(&peer_srx, skb) < 0))
		return just_discard; /* Unsupported address type. */

	if (peer_srx.transport.family != local->srx.transport.family &&
	    (peer_srx.transport.family == AF_INET &&
	     local->srx.transport.family != AF_INET6)) {
		pr_warn_ratelimited("AF_RXRPC: Protocol mismatch %u not %u\n",
				    peer_srx.transport.family,
				    local->srx.transport.family);
		return just_discard; /* Wrong address type. */
	}

	if (rxrpc_to_client(sp)) {
		rcu_read_lock();
		conn = rxrpc_find_client_connection_rcu(local, &peer_srx, skb);
		conn = rxrpc_get_connection_maybe(conn, rxrpc_conn_get_call_input);
		rcu_read_unlock();
		if (!conn)
			return rxrpc_protocol_error(skb, rxrpc_eproto_no_client_conn);

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
	return ret;
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
		return rxrpc_direct_abort(skb, rxrpc_eproto_wrong_security,
					  RXKADINCONSISTENCY, -EBADMSG);

	if (sp->hdr.serviceId != conn->service_id) {
		int old_id;

		if (!test_bit(RXRPC_CONN_PROBING_FOR_UPGRADE, &conn->flags))
			return rxrpc_protocol_error(skb, rxrpc_eproto_reupgrade);

		old_id = cmpxchg(&conn->service_id, conn->orig_service_id,
				 sp->hdr.serviceId);
		if (old_id != conn->orig_service_id &&
		    old_id != sp->hdr.serviceId)
			return rxrpc_protocol_error(skb, rxrpc_eproto_bad_upgrade);
	}

	if (after(sp->hdr.serial, conn->hi_serial))
		conn->hi_serial = sp->hdr.serial;

	/* It's a connection-level packet if the call number is 0. */
	if (sp->hdr.callNumber == 0)
		return rxrpc_input_conn_packet(conn, skb);

	/* Deal with path MTU discovery probing. */
	if (sp->hdr.type == RXRPC_PACKET_TYPE_ACK &&
	    conn->pmtud_probe &&
	    after_eq(sp->ack.acked_serial, conn->pmtud_probe))
		rxrpc_input_probe_for_pmtud(conn, sp->ack.acked_serial, false);

	/* Call-bound packets are routed by connection channel. */
	channel = sp->hdr.cid & RXRPC_CHANNELMASK;
	chan = &conn->channels[channel];

	/* Ignore really old calls */
	if (sp->hdr.callNumber < chan->last_call)
		return just_discard;

	if (sp->hdr.callNumber == chan->last_call) {
		if (chan->call ||
		    sp->hdr.type == RXRPC_PACKET_TYPE_ABORT)
			return just_discard;

		/* For the previous service call, if completed successfully, we
		 * discard all further packets.
		 */
		if (rxrpc_conn_is_service(conn) &&
		    chan->last_type == RXRPC_PACKET_TYPE_ACK)
			return just_discard;

		/* But otherwise we need to retransmit the final packet from
		 * data cached in the connection record.
		 */
		if (sp->hdr.type == RXRPC_PACKET_TYPE_DATA)
			trace_rxrpc_rx_data(chan->call_debug_id,
					    sp->hdr.seq,
					    sp->hdr.serial,
					    sp->hdr.flags);
		rxrpc_conn_retransmit_call(conn, skb, channel);
		return just_discard;
	}

	call = rxrpc_try_get_call(chan->call, rxrpc_call_get_input);

	if (sp->hdr.callNumber > chan->call_id) {
		if (rxrpc_to_client(sp)) {
			rxrpc_put_call(call, rxrpc_call_put_input);
			return rxrpc_protocol_error(skb,
						    rxrpc_eproto_unexpected_implicit_end);
		}

		if (call) {
			rxrpc_implicit_end_call(call, skb);
			rxrpc_put_call(call, rxrpc_call_put_input);
			call = NULL;
		}
	}

	if (!call) {
		if (rxrpc_to_client(sp))
			return rxrpc_protocol_error(skb, rxrpc_eproto_no_client_call);
		return rxrpc_new_incoming_call(conn->local, conn->peer, conn,
					       peer_srx, skb);
	}

	rxrpc_queue_rx_call_packet(call, skb);
	rxrpc_put_call(call, rxrpc_call_put_input);
	return true;
}

/*
 * I/O and event handling thread.
 */
int rxrpc_io_thread(void *data)
{
	struct rxrpc_connection *conn;
	struct sk_buff_head rx_queue;
	struct rxrpc_local *local = data;
	struct rxrpc_call *call;
	struct sk_buff *skb;
#ifdef CONFIG_AF_RXRPC_INJECT_RX_DELAY
	ktime_t now;
#endif
	bool should_stop;
	LIST_HEAD(conn_attend_q);
	LIST_HEAD(call_attend_q);

	complete(&local->io_thread_ready);

	skb_queue_head_init(&rx_queue);

	set_user_nice(current, MIN_NICE);

	for (;;) {
		rxrpc_inc_stat(local->rxnet, stat_io_loop);

		/* Inject a delay into packets if requested. */
#ifdef CONFIG_AF_RXRPC_INJECT_RX_DELAY
		now = ktime_get_real();
		while ((skb = skb_peek(&local->rx_delay_queue))) {
			if (ktime_before(now, skb->tstamp))
				break;
			skb = skb_dequeue(&local->rx_delay_queue);
			skb_queue_tail(&local->rx_queue, skb);
		}
#endif

		if (!skb_queue_empty(&local->rx_queue)) {
			spin_lock_irq(&local->rx_queue.lock);
			skb_queue_splice_tail_init(&local->rx_queue, &rx_queue);
			spin_unlock_irq(&local->rx_queue.lock);
			trace_rxrpc_iothread_rx(local, skb_queue_len(&rx_queue));
		}

		/* Distribute packets and errors. */
		while ((skb = __skb_dequeue(&rx_queue))) {
			struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
			switch (skb->mark) {
			case RXRPC_SKB_MARK_PACKET:
				skb->priority = 0;
				if (!rxrpc_input_packet(local, &skb))
					rxrpc_reject_packet(local, skb);
				trace_rxrpc_rx_done(skb->mark, skb->priority);
				rxrpc_free_skb(skb, rxrpc_skb_put_input);
				break;
			case RXRPC_SKB_MARK_ERROR:
				rxrpc_input_error(local, skb);
				rxrpc_free_skb(skb, rxrpc_skb_put_error_report);
				break;
			case RXRPC_SKB_MARK_SERVICE_CONN_SECURED:
				rxrpc_input_conn_event(sp->poke_conn, skb);
				rxrpc_put_connection(sp->poke_conn, rxrpc_conn_put_poke);
				rxrpc_free_skb(skb, rxrpc_skb_put_conn_secured);
				break;
			default:
				WARN_ON_ONCE(1);
				rxrpc_free_skb(skb, rxrpc_skb_put_unknown);
				break;
			}
		}

		/* Deal with connections that want immediate attention. */
		if (!list_empty_careful(&local->conn_attend_q)) {
			spin_lock_irq(&local->lock);
			list_splice_tail_init(&local->conn_attend_q, &conn_attend_q);
			spin_unlock_irq(&local->lock);
		}

		while ((conn = list_first_entry_or_null(&conn_attend_q,
							struct rxrpc_connection,
							attend_link))) {
			spin_lock_irq(&local->lock);
			list_del_init(&conn->attend_link);
			spin_unlock_irq(&local->lock);
			rxrpc_input_conn_event(conn, NULL);
			rxrpc_put_connection(conn, rxrpc_conn_put_poke);
		}

		if (test_and_clear_bit(RXRPC_CLIENT_CONN_REAP_TIMER,
				       &local->client_conn_flags))
			rxrpc_discard_expired_client_conns(local);

		/* Deal with calls that want immediate attention. */
		spin_lock_irq(&local->lock);
		list_splice_tail_init(&local->call_attend_q, &call_attend_q);
		spin_unlock_irq(&local->lock);

		while ((call = list_first_entry_or_null(&call_attend_q,
							struct rxrpc_call,
							attend_link))) {
			spin_lock_irq(&local->lock);
			list_del_init(&call->attend_link);
			spin_unlock_irq(&local->lock);
			trace_rxrpc_call_poked(call);
			rxrpc_input_call_event(call);
			rxrpc_put_call(call, rxrpc_call_put_poke);
		}

		if (!list_empty(&local->new_client_calls))
			rxrpc_connect_client_calls(local);

		set_current_state(TASK_INTERRUPTIBLE);
		should_stop = kthread_should_stop();
		if (!skb_queue_empty(&local->rx_queue) ||
		    !list_empty(&local->call_attend_q) ||
		    !list_empty(&local->conn_attend_q) ||
		    !list_empty(&local->new_client_calls) ||
		    test_bit(RXRPC_CLIENT_CONN_REAP_TIMER,
			     &local->client_conn_flags)) {
			__set_current_state(TASK_RUNNING);
			continue;
		}

		if (should_stop)
			break;

#ifdef CONFIG_AF_RXRPC_INJECT_RX_DELAY
		skb = skb_peek(&local->rx_delay_queue);
		if (skb) {
			unsigned long timeout;
			ktime_t tstamp = skb->tstamp;
			ktime_t now = ktime_get_real();
			s64 delay_ns = ktime_to_ns(ktime_sub(tstamp, now));

			if (delay_ns <= 0) {
				__set_current_state(TASK_RUNNING);
				continue;
			}

			timeout = nsecs_to_jiffies(delay_ns);
			timeout = umax(timeout, 1);
			schedule_timeout(timeout);
			__set_current_state(TASK_RUNNING);
			continue;
		}
#endif

		schedule();
	}

	__set_current_state(TASK_RUNNING);
	rxrpc_see_local(local, rxrpc_local_stop);
	rxrpc_destroy_local(local);
	WRITE_ONCE(local->io_thread, NULL);
	rxrpc_see_local(local, rxrpc_local_stopped);
	return 0;
}
