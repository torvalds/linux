// SPDX-License-Identifier: GPL-2.0-or-later
/* RxRPC packet transmission
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/net.h>
#include <linux/gfp.h>
#include <linux/skbuff.h>
#include <linux/export.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include <net/udp.h>
#include "ar-internal.h"

extern int udpv6_sendmsg(struct sock *sk, struct msghdr *msg, size_t len);

static ssize_t do_udp_sendmsg(struct socket *socket, struct msghdr *msg, size_t len)
{
	struct sockaddr *sa = msg->msg_name;
	struct sock *sk = socket->sk;

	if (IS_ENABLED(CONFIG_AF_RXRPC_IPV6)) {
		if (sa->sa_family == AF_INET6) {
			if (sk->sk_family != AF_INET6) {
				pr_warn("AF_INET6 address on AF_INET socket\n");
				return -ENOPROTOOPT;
			}
			return udpv6_sendmsg(sk, msg, len);
		}
	}
	return udp_sendmsg(sk, msg, len);
}

struct rxrpc_abort_buffer {
	struct rxrpc_wire_header whdr;
	__be32 abort_code;
};

static const char rxrpc_keepalive_string[] = "";

/*
 * Increase Tx backoff on transmission failure and clear it on success.
 */
static void rxrpc_tx_backoff(struct rxrpc_call *call, int ret)
{
	if (ret < 0) {
		u16 tx_backoff = READ_ONCE(call->tx_backoff);

		if (tx_backoff < HZ)
			WRITE_ONCE(call->tx_backoff, tx_backoff + 1);
	} else {
		WRITE_ONCE(call->tx_backoff, 0);
	}
}

/*
 * Arrange for a keepalive ping a certain time after we last transmitted.  This
 * lets the far side know we're still interested in this call and helps keep
 * the route through any intervening firewall open.
 *
 * Receiving a response to the ping will prevent the ->expect_rx_by timer from
 * expiring.
 */
static void rxrpc_set_keepalive(struct rxrpc_call *call)
{
	unsigned long now = jiffies, keepalive_at = call->next_rx_timo / 6;

	keepalive_at += now;
	WRITE_ONCE(call->keepalive_at, keepalive_at);
	rxrpc_reduce_call_timer(call, keepalive_at, now,
				rxrpc_timer_set_for_keepalive);
}

/*
 * Fill out an ACK packet.
 */
static size_t rxrpc_fill_out_ack(struct rxrpc_connection *conn,
				 struct rxrpc_call *call,
				 struct rxrpc_txbuf *txb,
				 u16 *_rwind)
{
	struct rxrpc_ackinfo ackinfo;
	unsigned int qsize, sack, wrap, to;
	rxrpc_seq_t window, wtop;
	int rsize;
	u32 mtu, jmax;
	u8 *ackp = txb->acks;

	call->ackr_nr_unacked = 0;
	atomic_set(&call->ackr_nr_consumed, 0);
	rxrpc_inc_stat(call->rxnet, stat_tx_ack_fill);
	clear_bit(RXRPC_CALL_RX_IS_IDLE, &call->flags);

	window = call->ackr_window;
	wtop   = call->ackr_wtop;
	sack   = call->ackr_sack_base % RXRPC_SACK_SIZE;
	txb->ack.firstPacket = htonl(window);
	txb->ack.nAcks = wtop - window;

	if (after(wtop, window)) {
		wrap = RXRPC_SACK_SIZE - sack;
		to = min_t(unsigned int, txb->ack.nAcks, RXRPC_SACK_SIZE);

		if (sack + txb->ack.nAcks <= RXRPC_SACK_SIZE) {
			memcpy(txb->acks, call->ackr_sack_table + sack, txb->ack.nAcks);
		} else {
			memcpy(txb->acks, call->ackr_sack_table + sack, wrap);
			memcpy(txb->acks + wrap, call->ackr_sack_table,
			       to - wrap);
		}

		ackp += to;
	} else if (before(wtop, window)) {
		pr_warn("ack window backward %x %x", window, wtop);
	} else if (txb->ack.reason == RXRPC_ACK_DELAY) {
		txb->ack.reason = RXRPC_ACK_IDLE;
	}

	mtu = conn->peer->if_mtu;
	mtu -= conn->peer->hdrsize;
	jmax = rxrpc_rx_jumbo_max;
	qsize = (window - 1) - call->rx_consumed;
	rsize = max_t(int, call->rx_winsize - qsize, 0);
	*_rwind = rsize;
	ackinfo.rxMTU		= htonl(rxrpc_rx_mtu);
	ackinfo.maxMTU		= htonl(mtu);
	ackinfo.rwind		= htonl(rsize);
	ackinfo.jumbo_max	= htonl(jmax);

	*ackp++ = 0;
	*ackp++ = 0;
	*ackp++ = 0;
	memcpy(ackp, &ackinfo, sizeof(ackinfo));
	return txb->ack.nAcks + 3 + sizeof(ackinfo);
}

/*
 * Record the beginning of an RTT probe.
 */
static int rxrpc_begin_rtt_probe(struct rxrpc_call *call, rxrpc_serial_t serial,
				 enum rxrpc_rtt_tx_trace why)
{
	unsigned long avail = call->rtt_avail;
	int rtt_slot = 9;

	if (!(avail & RXRPC_CALL_RTT_AVAIL_MASK))
		goto no_slot;

	rtt_slot = __ffs(avail & RXRPC_CALL_RTT_AVAIL_MASK);
	if (!test_and_clear_bit(rtt_slot, &call->rtt_avail))
		goto no_slot;

	call->rtt_serial[rtt_slot] = serial;
	call->rtt_sent_at[rtt_slot] = ktime_get_real();
	smp_wmb(); /* Write data before avail bit */
	set_bit(rtt_slot + RXRPC_CALL_RTT_PEND_SHIFT, &call->rtt_avail);

	trace_rxrpc_rtt_tx(call, why, rtt_slot, serial);
	return rtt_slot;

no_slot:
	trace_rxrpc_rtt_tx(call, rxrpc_rtt_tx_no_slot, rtt_slot, serial);
	return -1;
}

/*
 * Cancel an RTT probe.
 */
static void rxrpc_cancel_rtt_probe(struct rxrpc_call *call,
				   rxrpc_serial_t serial, int rtt_slot)
{
	if (rtt_slot != -1) {
		clear_bit(rtt_slot + RXRPC_CALL_RTT_PEND_SHIFT, &call->rtt_avail);
		smp_wmb(); /* Clear pending bit before setting slot */
		set_bit(rtt_slot, &call->rtt_avail);
		trace_rxrpc_rtt_tx(call, rxrpc_rtt_tx_cancel, rtt_slot, serial);
	}
}

/*
 * Transmit an ACK packet.
 */
int rxrpc_send_ack_packet(struct rxrpc_call *call, struct rxrpc_txbuf *txb)
{
	struct rxrpc_connection *conn;
	struct msghdr msg;
	struct kvec iov[1];
	rxrpc_serial_t serial;
	size_t len, n;
	int ret, rtt_slot = -1;
	u16 rwind;

	if (test_bit(RXRPC_CALL_DISCONNECTED, &call->flags))
		return -ECONNRESET;

	conn = call->conn;

	msg.msg_name	= &call->peer->srx.transport;
	msg.msg_namelen	= call->peer->srx.transport_len;
	msg.msg_control	= NULL;
	msg.msg_controllen = 0;
	msg.msg_flags	= 0;

	if (txb->ack.reason == RXRPC_ACK_PING)
		txb->wire.flags |= RXRPC_REQUEST_ACK;

	n = rxrpc_fill_out_ack(conn, call, txb, &rwind);
	if (n == 0)
		return 0;

	iov[0].iov_base	= &txb->wire;
	iov[0].iov_len	= sizeof(txb->wire) + sizeof(txb->ack) + n;
	len = iov[0].iov_len;

	serial = atomic_inc_return(&conn->serial);
	txb->wire.serial = htonl(serial);
	trace_rxrpc_tx_ack(call->debug_id, serial,
			   ntohl(txb->ack.firstPacket),
			   ntohl(txb->ack.serial), txb->ack.reason, txb->ack.nAcks,
			   rwind);

	if (txb->ack.reason == RXRPC_ACK_PING)
		rtt_slot = rxrpc_begin_rtt_probe(call, serial, rxrpc_rtt_tx_ping);

	rxrpc_inc_stat(call->rxnet, stat_tx_ack_send);

	/* Grab the highest received seq as late as possible */
	txb->ack.previousPacket	= htonl(call->rx_highest_seq);

	iov_iter_kvec(&msg.msg_iter, WRITE, iov, 1, len);
	ret = do_udp_sendmsg(conn->local->socket, &msg, len);
	call->peer->last_tx_at = ktime_get_seconds();
	if (ret < 0) {
		trace_rxrpc_tx_fail(call->debug_id, serial, ret,
				    rxrpc_tx_point_call_ack);
	} else {
		trace_rxrpc_tx_packet(call->debug_id, &txb->wire,
				      rxrpc_tx_point_call_ack);
		if (txb->wire.flags & RXRPC_REQUEST_ACK)
			call->peer->rtt_last_req = ktime_get_real();
	}
	rxrpc_tx_backoff(call, ret);

	if (!__rxrpc_call_is_complete(call)) {
		if (ret < 0)
			rxrpc_cancel_rtt_probe(call, serial, rtt_slot);
		rxrpc_set_keepalive(call);
	}

	return ret;
}

/*
 * Send an ABORT call packet.
 */
int rxrpc_send_abort_packet(struct rxrpc_call *call)
{
	struct rxrpc_connection *conn;
	struct rxrpc_abort_buffer pkt;
	struct msghdr msg;
	struct kvec iov[1];
	rxrpc_serial_t serial;
	int ret;

	/* Don't bother sending aborts for a client call once the server has
	 * hard-ACK'd all of its request data.  After that point, we're not
	 * going to stop the operation proceeding, and whilst we might limit
	 * the reply, it's not worth it if we can send a new call on the same
	 * channel instead, thereby closing off this call.
	 */
	if (rxrpc_is_client_call(call) &&
	    test_bit(RXRPC_CALL_TX_ALL_ACKED, &call->flags))
		return 0;

	if (test_bit(RXRPC_CALL_DISCONNECTED, &call->flags))
		return -ECONNRESET;

	conn = call->conn;

	msg.msg_name	= &call->peer->srx.transport;
	msg.msg_namelen	= call->peer->srx.transport_len;
	msg.msg_control	= NULL;
	msg.msg_controllen = 0;
	msg.msg_flags	= 0;

	pkt.whdr.epoch		= htonl(conn->proto.epoch);
	pkt.whdr.cid		= htonl(call->cid);
	pkt.whdr.callNumber	= htonl(call->call_id);
	pkt.whdr.seq		= 0;
	pkt.whdr.type		= RXRPC_PACKET_TYPE_ABORT;
	pkt.whdr.flags		= conn->out_clientflag;
	pkt.whdr.userStatus	= 0;
	pkt.whdr.securityIndex	= call->security_ix;
	pkt.whdr._rsvd		= 0;
	pkt.whdr.serviceId	= htons(call->dest_srx.srx_service);
	pkt.abort_code		= htonl(call->abort_code);

	iov[0].iov_base	= &pkt;
	iov[0].iov_len	= sizeof(pkt);

	serial = atomic_inc_return(&conn->serial);
	pkt.whdr.serial = htonl(serial);

	iov_iter_kvec(&msg.msg_iter, WRITE, iov, 1, sizeof(pkt));
	ret = do_udp_sendmsg(conn->local->socket, &msg, sizeof(pkt));
	conn->peer->last_tx_at = ktime_get_seconds();
	if (ret < 0)
		trace_rxrpc_tx_fail(call->debug_id, serial, ret,
				    rxrpc_tx_point_call_abort);
	else
		trace_rxrpc_tx_packet(call->debug_id, &pkt.whdr,
				      rxrpc_tx_point_call_abort);
	rxrpc_tx_backoff(call, ret);
	return ret;
}

/*
 * send a packet through the transport endpoint
 */
int rxrpc_send_data_packet(struct rxrpc_call *call, struct rxrpc_txbuf *txb)
{
	enum rxrpc_req_ack_trace why;
	struct rxrpc_connection *conn = call->conn;
	struct msghdr msg;
	struct kvec iov[1];
	rxrpc_serial_t serial;
	size_t len;
	int ret, rtt_slot = -1;

	_enter("%x,{%d}", txb->seq, txb->len);

	/* Each transmission of a Tx packet needs a new serial number */
	serial = atomic_inc_return(&conn->serial);
	txb->wire.serial = htonl(serial);

	if (test_bit(RXRPC_CONN_PROBING_FOR_UPGRADE, &conn->flags) &&
	    txb->seq == 1)
		txb->wire.userStatus = RXRPC_USERSTATUS_SERVICE_UPGRADE;

	iov[0].iov_base = &txb->wire;
	iov[0].iov_len = sizeof(txb->wire) + txb->len;
	len = iov[0].iov_len;
	iov_iter_kvec(&msg.msg_iter, WRITE, iov, 1, len);

	msg.msg_name = &call->peer->srx.transport;
	msg.msg_namelen = call->peer->srx.transport_len;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;

	/* If our RTT cache needs working on, request an ACK.  Also request
	 * ACKs if a DATA packet appears to have been lost.
	 *
	 * However, we mustn't request an ACK on the last reply packet of a
	 * service call, lest OpenAFS incorrectly send us an ACK with some
	 * soft-ACKs in it and then never follow up with a proper hard ACK.
	 */
	if (txb->wire.flags & RXRPC_REQUEST_ACK)
		why = rxrpc_reqack_already_on;
	else if (test_bit(RXRPC_TXBUF_LAST, &txb->flags) && rxrpc_sending_to_client(txb))
		why = rxrpc_reqack_no_srv_last;
	else if (test_and_clear_bit(RXRPC_CALL_EV_ACK_LOST, &call->events))
		why = rxrpc_reqack_ack_lost;
	else if (test_bit(RXRPC_TXBUF_RESENT, &txb->flags))
		why = rxrpc_reqack_retrans;
	else if (call->cong_mode == RXRPC_CALL_SLOW_START && call->cong_cwnd <= 2)
		why = rxrpc_reqack_slow_start;
	else if (call->tx_winsize <= 2)
		why = rxrpc_reqack_small_txwin;
	else if (call->peer->rtt_count < 3 && txb->seq & 1)
		why = rxrpc_reqack_more_rtt;
	else if (ktime_before(ktime_add_ms(call->peer->rtt_last_req, 1000), ktime_get_real()))
		why = rxrpc_reqack_old_rtt;
	else
		goto dont_set_request_ack;

	rxrpc_inc_stat(call->rxnet, stat_why_req_ack[why]);
	trace_rxrpc_req_ack(call->debug_id, txb->seq, why);
	if (why != rxrpc_reqack_no_srv_last)
		txb->wire.flags |= RXRPC_REQUEST_ACK;
dont_set_request_ack:

	if (IS_ENABLED(CONFIG_AF_RXRPC_INJECT_LOSS)) {
		static int lose;
		if ((lose++ & 7) == 7) {
			ret = 0;
			trace_rxrpc_tx_data(call, txb->seq, serial,
					    txb->wire.flags,
					    test_bit(RXRPC_TXBUF_RESENT, &txb->flags),
					    true);
			goto done;
		}
	}

	trace_rxrpc_tx_data(call, txb->seq, serial, txb->wire.flags,
			    test_bit(RXRPC_TXBUF_RESENT, &txb->flags), false);

	/* Track what we've attempted to transmit at least once so that the
	 * retransmission algorithm doesn't try to resend what we haven't sent
	 * yet.  However, this can race as we can receive an ACK before we get
	 * to this point.  But, OTOH, if we won't get an ACK mentioning this
	 * packet unless the far side received it (though it could have
	 * discarded it anyway and NAK'd it).
	 */
	cmpxchg(&call->tx_transmitted, txb->seq - 1, txb->seq);

	/* send the packet with the don't fragment bit set if we currently
	 * think it's small enough */
	if (txb->len >= call->peer->maxdata)
		goto send_fragmentable;

	txb->last_sent = ktime_get_real();
	if (txb->wire.flags & RXRPC_REQUEST_ACK)
		rtt_slot = rxrpc_begin_rtt_probe(call, serial, rxrpc_rtt_tx_data);

	/* send the packet by UDP
	 * - returns -EMSGSIZE if UDP would have to fragment the packet
	 *   to go out of the interface
	 *   - in which case, we'll have processed the ICMP error
	 *     message and update the peer record
	 */
	rxrpc_inc_stat(call->rxnet, stat_tx_data_send);
	ret = do_udp_sendmsg(conn->local->socket, &msg, len);
	conn->peer->last_tx_at = ktime_get_seconds();

	if (ret < 0) {
		rxrpc_inc_stat(call->rxnet, stat_tx_data_send_fail);
		rxrpc_cancel_rtt_probe(call, serial, rtt_slot);
		trace_rxrpc_tx_fail(call->debug_id, serial, ret,
				    rxrpc_tx_point_call_data_nofrag);
	} else {
		trace_rxrpc_tx_packet(call->debug_id, &txb->wire,
				      rxrpc_tx_point_call_data_nofrag);
	}

	rxrpc_tx_backoff(call, ret);
	if (ret == -EMSGSIZE)
		goto send_fragmentable;

done:
	if (ret >= 0) {
		call->tx_last_sent = txb->last_sent;
		if (txb->wire.flags & RXRPC_REQUEST_ACK) {
			call->peer->rtt_last_req = txb->last_sent;
			if (call->peer->rtt_count > 1) {
				unsigned long nowj = jiffies, ack_lost_at;

				ack_lost_at = rxrpc_get_rto_backoff(call->peer, false);
				ack_lost_at += nowj;
				WRITE_ONCE(call->ack_lost_at, ack_lost_at);
				rxrpc_reduce_call_timer(call, ack_lost_at, nowj,
							rxrpc_timer_set_for_lost_ack);
			}
		}

		if (txb->seq == 1 &&
		    !test_and_set_bit(RXRPC_CALL_BEGAN_RX_TIMER,
				      &call->flags)) {
			unsigned long nowj = jiffies, expect_rx_by;

			expect_rx_by = nowj + call->next_rx_timo;
			WRITE_ONCE(call->expect_rx_by, expect_rx_by);
			rxrpc_reduce_call_timer(call, expect_rx_by, nowj,
						rxrpc_timer_set_for_normal);
		}

		rxrpc_set_keepalive(call);
	} else {
		/* Cancel the call if the initial transmission fails,
		 * particularly if that's due to network routing issues that
		 * aren't going away anytime soon.  The layer above can arrange
		 * the retransmission.
		 */
		if (!test_and_set_bit(RXRPC_CALL_BEGAN_RX_TIMER, &call->flags))
			rxrpc_set_call_completion(call, RXRPC_CALL_LOCAL_ERROR,
						  RX_USER_ABORT, ret);
	}

	_leave(" = %d [%u]", ret, call->peer->maxdata);
	return ret;

send_fragmentable:
	/* attempt to send this message with fragmentation enabled */
	_debug("send fragment");

	txb->last_sent = ktime_get_real();
	if (txb->wire.flags & RXRPC_REQUEST_ACK)
		rtt_slot = rxrpc_begin_rtt_probe(call, serial, rxrpc_rtt_tx_data);

	switch (conn->local->srx.transport.family) {
	case AF_INET6:
	case AF_INET:
		rxrpc_local_dont_fragment(conn->local, false);
		rxrpc_inc_stat(call->rxnet, stat_tx_data_send_frag);
		ret = do_udp_sendmsg(conn->local->socket, &msg, len);
		conn->peer->last_tx_at = ktime_get_seconds();

		rxrpc_local_dont_fragment(conn->local, true);
		break;

	default:
		BUG();
	}

	if (ret < 0) {
		rxrpc_inc_stat(call->rxnet, stat_tx_data_send_fail);
		rxrpc_cancel_rtt_probe(call, serial, rtt_slot);
		trace_rxrpc_tx_fail(call->debug_id, serial, ret,
				    rxrpc_tx_point_call_data_frag);
	} else {
		trace_rxrpc_tx_packet(call->debug_id, &txb->wire,
				      rxrpc_tx_point_call_data_frag);
	}
	rxrpc_tx_backoff(call, ret);
	goto done;
}

/*
 * Transmit a connection-level abort.
 */
void rxrpc_send_conn_abort(struct rxrpc_connection *conn)
{
	struct rxrpc_wire_header whdr;
	struct msghdr msg;
	struct kvec iov[2];
	__be32 word;
	size_t len;
	u32 serial;
	int ret;

	msg.msg_name	= &conn->peer->srx.transport;
	msg.msg_namelen	= conn->peer->srx.transport_len;
	msg.msg_control	= NULL;
	msg.msg_controllen = 0;
	msg.msg_flags	= 0;

	whdr.epoch	= htonl(conn->proto.epoch);
	whdr.cid	= htonl(conn->proto.cid);
	whdr.callNumber	= 0;
	whdr.seq	= 0;
	whdr.type	= RXRPC_PACKET_TYPE_ABORT;
	whdr.flags	= conn->out_clientflag;
	whdr.userStatus	= 0;
	whdr.securityIndex = conn->security_ix;
	whdr._rsvd	= 0;
	whdr.serviceId	= htons(conn->service_id);

	word		= htonl(conn->abort_code);

	iov[0].iov_base	= &whdr;
	iov[0].iov_len	= sizeof(whdr);
	iov[1].iov_base	= &word;
	iov[1].iov_len	= sizeof(word);

	len = iov[0].iov_len + iov[1].iov_len;

	serial = atomic_inc_return(&conn->serial);
	whdr.serial = htonl(serial);

	iov_iter_kvec(&msg.msg_iter, WRITE, iov, 2, len);
	ret = do_udp_sendmsg(conn->local->socket, &msg, len);
	if (ret < 0) {
		trace_rxrpc_tx_fail(conn->debug_id, serial, ret,
				    rxrpc_tx_point_conn_abort);
		_debug("sendmsg failed: %d", ret);
		return;
	}

	trace_rxrpc_tx_packet(conn->debug_id, &whdr, rxrpc_tx_point_conn_abort);

	conn->peer->last_tx_at = ktime_get_seconds();
}

/*
 * Reject a packet through the local endpoint.
 */
void rxrpc_reject_packet(struct rxrpc_local *local, struct sk_buff *skb)
{
	struct rxrpc_wire_header whdr;
	struct sockaddr_rxrpc srx;
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct msghdr msg;
	struct kvec iov[2];
	size_t size;
	__be32 code;
	int ret, ioc;

	rxrpc_see_skb(skb, rxrpc_skb_see_reject);

	iov[0].iov_base = &whdr;
	iov[0].iov_len = sizeof(whdr);
	iov[1].iov_base = &code;
	iov[1].iov_len = sizeof(code);

	msg.msg_name = &srx.transport;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;

	memset(&whdr, 0, sizeof(whdr));

	switch (skb->mark) {
	case RXRPC_SKB_MARK_REJECT_BUSY:
		whdr.type = RXRPC_PACKET_TYPE_BUSY;
		size = sizeof(whdr);
		ioc = 1;
		break;
	case RXRPC_SKB_MARK_REJECT_ABORT:
		whdr.type = RXRPC_PACKET_TYPE_ABORT;
		code = htonl(skb->priority);
		size = sizeof(whdr) + sizeof(code);
		ioc = 2;
		break;
	default:
		return;
	}

	if (rxrpc_extract_addr_from_skb(&srx, skb) == 0) {
		msg.msg_namelen = srx.transport_len;

		whdr.epoch	= htonl(sp->hdr.epoch);
		whdr.cid	= htonl(sp->hdr.cid);
		whdr.callNumber	= htonl(sp->hdr.callNumber);
		whdr.serviceId	= htons(sp->hdr.serviceId);
		whdr.flags	= sp->hdr.flags;
		whdr.flags	^= RXRPC_CLIENT_INITIATED;
		whdr.flags	&= RXRPC_CLIENT_INITIATED;

		iov_iter_kvec(&msg.msg_iter, WRITE, iov, ioc, size);
		ret = do_udp_sendmsg(local->socket, &msg, size);
		if (ret < 0)
			trace_rxrpc_tx_fail(local->debug_id, 0, ret,
					    rxrpc_tx_point_reject);
		else
			trace_rxrpc_tx_packet(local->debug_id, &whdr,
					      rxrpc_tx_point_reject);
	}
}

/*
 * Send a VERSION reply to a peer as a keepalive.
 */
void rxrpc_send_keepalive(struct rxrpc_peer *peer)
{
	struct rxrpc_wire_header whdr;
	struct msghdr msg;
	struct kvec iov[2];
	size_t len;
	int ret;

	_enter("");

	msg.msg_name	= &peer->srx.transport;
	msg.msg_namelen	= peer->srx.transport_len;
	msg.msg_control	= NULL;
	msg.msg_controllen = 0;
	msg.msg_flags	= 0;

	whdr.epoch	= htonl(peer->local->rxnet->epoch);
	whdr.cid	= 0;
	whdr.callNumber	= 0;
	whdr.seq	= 0;
	whdr.serial	= 0;
	whdr.type	= RXRPC_PACKET_TYPE_VERSION; /* Not client-initiated */
	whdr.flags	= RXRPC_LAST_PACKET;
	whdr.userStatus	= 0;
	whdr.securityIndex = 0;
	whdr._rsvd	= 0;
	whdr.serviceId	= 0;

	iov[0].iov_base	= &whdr;
	iov[0].iov_len	= sizeof(whdr);
	iov[1].iov_base	= (char *)rxrpc_keepalive_string;
	iov[1].iov_len	= sizeof(rxrpc_keepalive_string);

	len = iov[0].iov_len + iov[1].iov_len;

	iov_iter_kvec(&msg.msg_iter, WRITE, iov, 2, len);
	ret = do_udp_sendmsg(peer->local->socket, &msg, len);
	if (ret < 0)
		trace_rxrpc_tx_fail(peer->debug_id, 0, ret,
				    rxrpc_tx_point_version_keepalive);
	else
		trace_rxrpc_tx_packet(peer->debug_id, &whdr,
				      rxrpc_tx_point_version_keepalive);

	peer->last_tx_at = ktime_get_seconds();
	_leave("");
}

/*
 * Schedule an instant Tx resend.
 */
static inline void rxrpc_instant_resend(struct rxrpc_call *call,
					struct rxrpc_txbuf *txb)
{
	if (!__rxrpc_call_is_complete(call))
		kdebug("resend");
}

/*
 * Transmit one packet.
 */
void rxrpc_transmit_one(struct rxrpc_call *call, struct rxrpc_txbuf *txb)
{
	int ret;

	ret = rxrpc_send_data_packet(call, txb);
	if (ret < 0) {
		switch (ret) {
		case -ENETUNREACH:
		case -EHOSTUNREACH:
		case -ECONNREFUSED:
			rxrpc_set_call_completion(call, RXRPC_CALL_LOCAL_ERROR,
						  0, ret);
			break;
		default:
			_debug("need instant resend %d", ret);
			rxrpc_instant_resend(call, txb);
		}
	} else {
		unsigned long now = jiffies;
		unsigned long resend_at = now + call->peer->rto_j;

		WRITE_ONCE(call->resend_at, resend_at);
		rxrpc_reduce_call_timer(call, resend_at, now,
					rxrpc_timer_set_for_send);
	}
}
