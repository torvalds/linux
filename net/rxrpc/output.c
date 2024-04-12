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
		if (call->tx_backoff < 1000)
			call->tx_backoff += 100;
	} else {
		call->tx_backoff = 0;
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
static void rxrpc_set_keepalive(struct rxrpc_call *call, ktime_t now)
{
	ktime_t delay = ms_to_ktime(READ_ONCE(call->next_rx_timo) / 6);

	call->keepalive_at = ktime_add(ktime_get_real(), delay);
	trace_rxrpc_timer_set(call, delay, rxrpc_timer_trace_keepalive);
}

/*
 * Fill out an ACK packet.
 */
static void rxrpc_fill_out_ack(struct rxrpc_call *call,
			       struct rxrpc_txbuf *txb,
			       u8 ack_reason,
			       rxrpc_serial_t serial)
{
	struct rxrpc_wire_header *whdr = txb->kvec[0].iov_base;
	struct rxrpc_acktrailer *trailer = txb->kvec[2].iov_base + 3;
	struct rxrpc_ackpacket *ack = (struct rxrpc_ackpacket *)(whdr + 1);
	unsigned int qsize, sack, wrap, to;
	rxrpc_seq_t window, wtop;
	int rsize;
	u32 mtu, jmax;
	u8 *filler = txb->kvec[2].iov_base;
	u8 *sackp = txb->kvec[1].iov_base;

	rxrpc_inc_stat(call->rxnet, stat_tx_ack_fill);

	window = call->ackr_window;
	wtop   = call->ackr_wtop;
	sack   = call->ackr_sack_base % RXRPC_SACK_SIZE;

	whdr->seq		= 0;
	whdr->type		= RXRPC_PACKET_TYPE_ACK;
	txb->flags		|= RXRPC_SLOW_START_OK;
	ack->bufferSpace	= 0;
	ack->maxSkew		= 0;
	ack->firstPacket	= htonl(window);
	ack->previousPacket	= htonl(call->rx_highest_seq);
	ack->serial		= htonl(serial);
	ack->reason		= ack_reason;
	ack->nAcks		= wtop - window;
	filler[0]		= 0;
	filler[1]		= 0;
	filler[2]		= 0;

	if (ack_reason == RXRPC_ACK_PING)
		txb->flags |= RXRPC_REQUEST_ACK;

	if (after(wtop, window)) {
		txb->len += ack->nAcks;
		txb->kvec[1].iov_base = sackp;
		txb->kvec[1].iov_len = ack->nAcks;

		wrap = RXRPC_SACK_SIZE - sack;
		to = min_t(unsigned int, ack->nAcks, RXRPC_SACK_SIZE);

		if (sack + ack->nAcks <= RXRPC_SACK_SIZE) {
			memcpy(sackp, call->ackr_sack_table + sack, ack->nAcks);
		} else {
			memcpy(sackp, call->ackr_sack_table + sack, wrap);
			memcpy(sackp + wrap, call->ackr_sack_table, to - wrap);
		}
	} else if (before(wtop, window)) {
		pr_warn("ack window backward %x %x", window, wtop);
	} else if (ack->reason == RXRPC_ACK_DELAY) {
		ack->reason = RXRPC_ACK_IDLE;
	}

	mtu = call->peer->if_mtu;
	mtu -= call->peer->hdrsize;
	jmax = rxrpc_rx_jumbo_max;
	qsize = (window - 1) - call->rx_consumed;
	rsize = max_t(int, call->rx_winsize - qsize, 0);
	txb->ack_rwind = rsize;
	trailer->maxMTU		= htonl(rxrpc_rx_mtu);
	trailer->ifMTU		= htonl(mtu);
	trailer->rwind		= htonl(rsize);
	trailer->jumbo_max	= htonl(jmax);
}

/*
 * Record the beginning of an RTT probe.
 */
static void rxrpc_begin_rtt_probe(struct rxrpc_call *call, rxrpc_serial_t serial,
				  ktime_t now, enum rxrpc_rtt_tx_trace why)
{
	unsigned long avail = call->rtt_avail;
	int rtt_slot = 9;

	if (!(avail & RXRPC_CALL_RTT_AVAIL_MASK))
		goto no_slot;

	rtt_slot = __ffs(avail & RXRPC_CALL_RTT_AVAIL_MASK);
	if (!test_and_clear_bit(rtt_slot, &call->rtt_avail))
		goto no_slot;

	call->rtt_serial[rtt_slot] = serial;
	call->rtt_sent_at[rtt_slot] = now;
	smp_wmb(); /* Write data before avail bit */
	set_bit(rtt_slot + RXRPC_CALL_RTT_PEND_SHIFT, &call->rtt_avail);

	trace_rxrpc_rtt_tx(call, why, rtt_slot, serial);
	return;

no_slot:
	trace_rxrpc_rtt_tx(call, rxrpc_rtt_tx_no_slot, rtt_slot, serial);
}

/*
 * Transmit an ACK packet.
 */
static void rxrpc_send_ack_packet(struct rxrpc_call *call, struct rxrpc_txbuf *txb)
{
	struct rxrpc_wire_header *whdr = txb->kvec[0].iov_base;
	struct rxrpc_connection *conn;
	struct rxrpc_ackpacket *ack = (struct rxrpc_ackpacket *)(whdr + 1);
	struct msghdr msg;
	ktime_t now;
	int ret;

	if (test_bit(RXRPC_CALL_DISCONNECTED, &call->flags))
		return;

	conn = call->conn;

	msg.msg_name	= &call->peer->srx.transport;
	msg.msg_namelen	= call->peer->srx.transport_len;
	msg.msg_control	= NULL;
	msg.msg_controllen = 0;
	msg.msg_flags	= MSG_SPLICE_PAGES;

	whdr->flags = txb->flags & RXRPC_TXBUF_WIRE_FLAGS;

	txb->serial = rxrpc_get_next_serial(conn);
	whdr->serial = htonl(txb->serial);
	trace_rxrpc_tx_ack(call->debug_id, txb->serial,
			   ntohl(ack->firstPacket),
			   ntohl(ack->serial), ack->reason, ack->nAcks,
			   txb->ack_rwind);

	rxrpc_inc_stat(call->rxnet, stat_tx_ack_send);

	iov_iter_kvec(&msg.msg_iter, WRITE, txb->kvec, txb->nr_kvec, txb->len);
	rxrpc_local_dont_fragment(conn->local, false);
	ret = do_udp_sendmsg(conn->local->socket, &msg, txb->len);
	call->peer->last_tx_at = ktime_get_seconds();
	if (ret < 0) {
		trace_rxrpc_tx_fail(call->debug_id, txb->serial, ret,
				    rxrpc_tx_point_call_ack);
	} else {
		trace_rxrpc_tx_packet(call->debug_id, whdr,
				      rxrpc_tx_point_call_ack);
		now = ktime_get_real();
		if (ack->reason == RXRPC_ACK_PING)
			rxrpc_begin_rtt_probe(call, txb->serial, now, rxrpc_rtt_tx_ping);
		if (txb->flags & RXRPC_REQUEST_ACK)
			call->peer->rtt_last_req = now;
		rxrpc_set_keepalive(call, now);
	}
	rxrpc_tx_backoff(call, ret);
}

/*
 * Queue an ACK for immediate transmission.
 */
void rxrpc_send_ACK(struct rxrpc_call *call, u8 ack_reason,
		    rxrpc_serial_t serial, enum rxrpc_propose_ack_trace why)
{
	struct rxrpc_txbuf *txb;

	if (test_bit(RXRPC_CALL_DISCONNECTED, &call->flags))
		return;

	rxrpc_inc_stat(call->rxnet, stat_tx_acks[ack_reason]);

	txb = rxrpc_alloc_ack_txbuf(call, call->ackr_wtop - call->ackr_window);
	if (!txb) {
		kleave(" = -ENOMEM");
		return;
	}

	txb->ack_why = why;

	rxrpc_fill_out_ack(call, txb, ack_reason, serial);
	call->ackr_nr_unacked = 0;
	atomic_set(&call->ackr_nr_consumed, 0);
	clear_bit(RXRPC_CALL_RX_IS_IDLE, &call->flags);

	trace_rxrpc_send_ack(call, why, ack_reason, serial);
	rxrpc_send_ack_packet(call, txb);
	rxrpc_put_txbuf(txb, rxrpc_txbuf_put_ack_tx);
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

	serial = rxrpc_get_next_serial(conn);
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
 * Prepare a (sub)packet for transmission.
 */
static void rxrpc_prepare_data_subpacket(struct rxrpc_call *call, struct rxrpc_txbuf *txb,
					 rxrpc_serial_t serial)
{
	struct rxrpc_wire_header *whdr = txb->kvec[0].iov_base;
	enum rxrpc_req_ack_trace why;
	struct rxrpc_connection *conn = call->conn;

	_enter("%x,{%d}", txb->seq, txb->len);

	txb->serial = serial;

	if (test_bit(RXRPC_CONN_PROBING_FOR_UPGRADE, &conn->flags) &&
	    txb->seq == 1)
		whdr->userStatus = RXRPC_USERSTATUS_SERVICE_UPGRADE;

	/* If our RTT cache needs working on, request an ACK.  Also request
	 * ACKs if a DATA packet appears to have been lost.
	 *
	 * However, we mustn't request an ACK on the last reply packet of a
	 * service call, lest OpenAFS incorrectly send us an ACK with some
	 * soft-ACKs in it and then never follow up with a proper hard ACK.
	 */
	if (txb->flags & RXRPC_REQUEST_ACK)
		why = rxrpc_reqack_already_on;
	else if ((txb->flags & RXRPC_LAST_PACKET) && rxrpc_sending_to_client(txb))
		why = rxrpc_reqack_no_srv_last;
	else if (test_and_clear_bit(RXRPC_CALL_EV_ACK_LOST, &call->events))
		why = rxrpc_reqack_ack_lost;
	else if (txb->flags & RXRPC_TXBUF_RESENT)
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
		txb->flags |= RXRPC_REQUEST_ACK;
dont_set_request_ack:

	whdr->flags = txb->flags & RXRPC_TXBUF_WIRE_FLAGS;
	whdr->serial	= htonl(txb->serial);
	whdr->cksum	= txb->cksum;

	trace_rxrpc_tx_data(call, txb->seq, txb->serial, txb->flags, false);
}

/*
 * Prepare a packet for transmission.
 */
static size_t rxrpc_prepare_data_packet(struct rxrpc_call *call, struct rxrpc_txbuf *txb)
{
	rxrpc_serial_t serial;

	/* Each transmission of a Tx packet needs a new serial number */
	serial = rxrpc_get_next_serial(call->conn);

	rxrpc_prepare_data_subpacket(call, txb, serial);

	return txb->len;
}

/*
 * Set timeouts after transmitting a packet.
 */
static void rxrpc_tstamp_data_packets(struct rxrpc_call *call, struct rxrpc_txbuf *txb)
{
	ktime_t now = ktime_get_real();
	bool ack_requested = txb->flags & RXRPC_REQUEST_ACK;

	call->tx_last_sent = now;
	txb->last_sent = now;

	if (ack_requested) {
		rxrpc_begin_rtt_probe(call, txb->serial, now, rxrpc_rtt_tx_data);

		call->peer->rtt_last_req = now;
		if (call->peer->rtt_count > 1) {
			ktime_t delay = rxrpc_get_rto_backoff(call->peer, false);

			call->ack_lost_at = ktime_add(now, delay);
			trace_rxrpc_timer_set(call, delay, rxrpc_timer_trace_lost_ack);
		}
	}

	if (!test_and_set_bit(RXRPC_CALL_BEGAN_RX_TIMER, &call->flags)) {
		ktime_t delay = ms_to_ktime(READ_ONCE(call->next_rx_timo));

		call->expect_rx_by = ktime_add(now, delay);
		trace_rxrpc_timer_set(call, delay, rxrpc_timer_trace_expect_rx);
	}

	rxrpc_set_keepalive(call, now);
}

/*
 * send a packet through the transport endpoint
 */
static int rxrpc_send_data_packet(struct rxrpc_call *call, struct rxrpc_txbuf *txb)
{
	struct rxrpc_wire_header *whdr = txb->kvec[0].iov_base;
	struct rxrpc_connection *conn = call->conn;
	enum rxrpc_tx_point frag;
	struct msghdr msg;
	size_t len;
	int ret;

	_enter("%x,{%d}", txb->seq, txb->len);

	len = rxrpc_prepare_data_packet(call, txb);

	if (IS_ENABLED(CONFIG_AF_RXRPC_INJECT_LOSS)) {
		static int lose;
		if ((lose++ & 7) == 7) {
			ret = 0;
			trace_rxrpc_tx_data(call, txb->seq, txb->serial,
					    txb->flags, true);
			goto done;
		}
	}

	iov_iter_kvec(&msg.msg_iter, WRITE, txb->kvec, txb->nr_kvec, len);

	msg.msg_name	= &call->peer->srx.transport;
	msg.msg_namelen	= call->peer->srx.transport_len;
	msg.msg_control	= NULL;
	msg.msg_controllen = 0;
	msg.msg_flags	= MSG_SPLICE_PAGES;

	/* Track what we've attempted to transmit at least once so that the
	 * retransmission algorithm doesn't try to resend what we haven't sent
	 * yet.
	 */
	if (txb->seq == call->tx_transmitted + 1)
		call->tx_transmitted = txb->seq;

	/* send the packet with the don't fragment bit set if we currently
	 * think it's small enough */
	if (txb->len >= call->peer->maxdata) {
		rxrpc_local_dont_fragment(conn->local, false);
		frag = rxrpc_tx_point_call_data_frag;
	} else {
		rxrpc_local_dont_fragment(conn->local, true);
		frag = rxrpc_tx_point_call_data_nofrag;
	}

retry:
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
		trace_rxrpc_tx_fail(call->debug_id, txb->serial, ret, frag);
	} else {
		trace_rxrpc_tx_packet(call->debug_id, whdr, frag);
	}

	rxrpc_tx_backoff(call, ret);
	if (ret == -EMSGSIZE && frag == rxrpc_tx_point_call_data_frag) {
		rxrpc_local_dont_fragment(conn->local, false);
		frag = rxrpc_tx_point_call_data_frag;
		goto retry;
	}

done:
	if (ret >= 0) {
		rxrpc_tstamp_data_packets(call, txb);
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

	serial = rxrpc_get_next_serial(conn);
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
		ktime_t delay = ns_to_ktime(call->peer->rto_us * NSEC_PER_USEC);

		call->resend_at = ktime_add(ktime_get_real(), delay);
		trace_rxrpc_timer_set(call, delay, rxrpc_timer_trace_resend_tx);
	}
}
