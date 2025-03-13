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
 * Allocate transmission buffers for an ACK and attach them to local->kv[].
 */
static int rxrpc_alloc_ack(struct rxrpc_call *call, size_t sack_size)
{
	struct rxrpc_wire_header *whdr;
	struct rxrpc_acktrailer *trailer;
	struct rxrpc_ackpacket *ack;
	struct kvec *kv = call->local->kvec;
	gfp_t gfp = rcu_read_lock_held() ? GFP_ATOMIC | __GFP_NOWARN : GFP_NOFS;
	void *buf, *buf2 = NULL;
	u8 *filler;

	buf = page_frag_alloc(&call->local->tx_alloc,
			      sizeof(*whdr) + sizeof(*ack) + 1 + 3 + sizeof(*trailer), gfp);
	if (!buf)
		return -ENOMEM;

	if (sack_size) {
		buf2 = page_frag_alloc(&call->local->tx_alloc, sack_size, gfp);
		if (!buf2) {
			page_frag_free(buf);
			return -ENOMEM;
		}
	}

	whdr	= buf;
	ack	= buf + sizeof(*whdr);
	filler	= buf + sizeof(*whdr) + sizeof(*ack) + 1;
	trailer	= buf + sizeof(*whdr) + sizeof(*ack) + 1 + 3;

	kv[0].iov_base	= whdr;
	kv[0].iov_len	= sizeof(*whdr) + sizeof(*ack);
	kv[1].iov_base	= buf2;
	kv[1].iov_len	= sack_size;
	kv[2].iov_base	= filler;
	kv[2].iov_len	= 3 + sizeof(*trailer);
	return 3; /* Number of kvec[] used. */
}

static void rxrpc_free_ack(struct rxrpc_call *call)
{
	page_frag_free(call->local->kvec[0].iov_base);
	if (call->local->kvec[1].iov_base)
		page_frag_free(call->local->kvec[1].iov_base);
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
 * Fill out an ACK packet.
 */
static int rxrpc_fill_out_ack(struct rxrpc_call *call, int nr_kv, u8 ack_reason,
			      rxrpc_serial_t serial_to_ack, rxrpc_serial_t *_ack_serial)
{
	struct kvec *kv = call->local->kvec;
	struct rxrpc_wire_header *whdr = kv[0].iov_base;
	struct rxrpc_acktrailer *trailer = kv[2].iov_base + 3;
	struct rxrpc_ackpacket *ack = (struct rxrpc_ackpacket *)(whdr + 1);
	unsigned int qsize, sack, wrap, to, max_mtu, if_mtu;
	rxrpc_seq_t window, wtop;
	ktime_t now = ktime_get_real();
	int rsize;
	u8 *filler = kv[2].iov_base;
	u8 *sackp = kv[1].iov_base;

	rxrpc_inc_stat(call->rxnet, stat_tx_ack_fill);

	window = call->ackr_window;
	wtop   = call->ackr_wtop;
	sack   = call->ackr_sack_base % RXRPC_SACK_SIZE;

	*_ack_serial = rxrpc_get_next_serial(call->conn);

	whdr->epoch		= htonl(call->conn->proto.epoch);
	whdr->cid		= htonl(call->cid);
	whdr->callNumber	= htonl(call->call_id);
	whdr->serial		= htonl(*_ack_serial);
	whdr->seq		= 0;
	whdr->type		= RXRPC_PACKET_TYPE_ACK;
	whdr->flags		= call->conn->out_clientflag | RXRPC_SLOW_START_OK;
	whdr->userStatus	= 0;
	whdr->securityIndex	= call->security_ix;
	whdr->_rsvd		= 0;
	whdr->serviceId		= htons(call->dest_srx.srx_service);

	ack->bufferSpace	= 0;
	ack->maxSkew		= 0;
	ack->firstPacket	= htonl(window);
	ack->previousPacket	= htonl(call->rx_highest_seq);
	ack->serial		= htonl(serial_to_ack);
	ack->reason		= ack_reason;
	ack->nAcks		= wtop - window;
	filler[0]		= 0;
	filler[1]		= 0;
	filler[2]		= 0;

	if (ack_reason == RXRPC_ACK_PING)
		whdr->flags |= RXRPC_REQUEST_ACK;

	if (after(wtop, window)) {
		kv[1].iov_len = ack->nAcks;

		wrap = RXRPC_SACK_SIZE - sack;
		to = umin(ack->nAcks, RXRPC_SACK_SIZE);

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

	qsize = (window - 1) - call->rx_consumed;
	rsize = max_t(int, call->rx_winsize - qsize, 0);

	if_mtu = call->peer->if_mtu - call->peer->hdrsize;
	if (call->peer->ackr_adv_pmtud) {
		max_mtu = umax(call->peer->max_data, rxrpc_rx_mtu);
	} else {
		if_mtu = umin(if_mtu, 1444);
		max_mtu = if_mtu;
	}

	trailer->maxMTU		= htonl(max_mtu);
	trailer->ifMTU		= htonl(if_mtu);
	trailer->rwind		= htonl(rsize);
	trailer->jumbo_max	= 0; /* Advertise pmtu discovery */

	if (ack_reason == RXRPC_ACK_PING)
		rxrpc_begin_rtt_probe(call, *_ack_serial, now, rxrpc_rtt_tx_ping);
	if (whdr->flags & RXRPC_REQUEST_ACK)
		call->rtt_last_req = now;
	rxrpc_set_keepalive(call, now);
	return nr_kv;
}

/*
 * Transmit an ACK packet.
 */
static void rxrpc_send_ack_packet(struct rxrpc_call *call, int nr_kv, size_t len,
				  rxrpc_serial_t serial, enum rxrpc_propose_ack_trace why)
{
	struct kvec *kv = call->local->kvec;
	struct rxrpc_wire_header *whdr = kv[0].iov_base;
	struct rxrpc_acktrailer *trailer = kv[2].iov_base + 3;
	struct rxrpc_connection *conn;
	struct rxrpc_ackpacket *ack = (struct rxrpc_ackpacket *)(whdr + 1);
	struct msghdr msg;
	int ret;

	if (test_bit(RXRPC_CALL_DISCONNECTED, &call->flags))
		return;

	conn = call->conn;

	msg.msg_name	= &call->peer->srx.transport;
	msg.msg_namelen	= call->peer->srx.transport_len;
	msg.msg_control	= NULL;
	msg.msg_controllen = 0;
	msg.msg_flags	= MSG_SPLICE_PAGES;

	trace_rxrpc_tx_ack(call->debug_id, serial,
			   ntohl(ack->firstPacket),
			   ntohl(ack->serial), ack->reason, ack->nAcks,
			   ntohl(trailer->rwind), why);

	rxrpc_inc_stat(call->rxnet, stat_tx_ack_send);

	iov_iter_kvec(&msg.msg_iter, WRITE, kv, nr_kv, len);
	rxrpc_local_dont_fragment(conn->local, why == rxrpc_propose_ack_ping_for_mtu_probe);

	ret = do_udp_sendmsg(conn->local->socket, &msg, len);
	call->peer->last_tx_at = ktime_get_seconds();
	if (ret < 0) {
		trace_rxrpc_tx_fail(call->debug_id, serial, ret,
				    rxrpc_tx_point_call_ack);
		if (why == rxrpc_propose_ack_ping_for_mtu_probe &&
		    ret == -EMSGSIZE)
			rxrpc_input_probe_for_pmtud(conn, serial, true);
	} else {
		trace_rxrpc_tx_packet(call->debug_id, whdr,
				      rxrpc_tx_point_call_ack);
		if (why == rxrpc_propose_ack_ping_for_mtu_probe) {
			call->peer->pmtud_pending = false;
			call->peer->pmtud_probing = true;
			call->conn->pmtud_probe = serial;
			call->conn->pmtud_call = call->debug_id;
			trace_rxrpc_pmtud_tx(call);
		}
	}
	rxrpc_tx_backoff(call, ret);
}

/*
 * Queue an ACK for immediate transmission.
 */
void rxrpc_send_ACK(struct rxrpc_call *call, u8 ack_reason,
		    rxrpc_serial_t serial_to_ack, enum rxrpc_propose_ack_trace why)
{
	struct kvec *kv = call->local->kvec;
	rxrpc_serial_t ack_serial;
	size_t len;
	int nr_kv;

	if (test_bit(RXRPC_CALL_DISCONNECTED, &call->flags))
		return;

	rxrpc_inc_stat(call->rxnet, stat_tx_acks[ack_reason]);

	nr_kv = rxrpc_alloc_ack(call, call->ackr_wtop - call->ackr_window);
	if (nr_kv < 0) {
		kleave(" = -ENOMEM");
		return;
	}

	nr_kv = rxrpc_fill_out_ack(call, nr_kv, ack_reason, serial_to_ack, &ack_serial);
	len  = kv[0].iov_len;
	len += kv[1].iov_len;
	len += kv[2].iov_len;

	/* Extend a path MTU probe ACK. */
	if (why == rxrpc_propose_ack_ping_for_mtu_probe) {
		size_t probe_mtu = call->peer->pmtud_trial + sizeof(struct rxrpc_wire_header);

		if (len > probe_mtu)
			goto skip;
		while (len < probe_mtu) {
			size_t part = umin(probe_mtu - len, PAGE_SIZE);

			kv[nr_kv].iov_base = page_address(ZERO_PAGE(0));
			kv[nr_kv].iov_len = part;
			len += part;
			nr_kv++;
		}
	}

	call->ackr_nr_unacked = 0;
	atomic_set(&call->ackr_nr_consumed, 0);
	clear_bit(RXRPC_CALL_RX_IS_IDLE, &call->flags);

	trace_rxrpc_send_ack(call, why, ack_reason, ack_serial);
	rxrpc_send_ack_packet(call, nr_kv, len, ack_serial, why);
skip:
	rxrpc_free_ack(call);
}

/*
 * Send an ACK probe for path MTU discovery.
 */
void rxrpc_send_probe_for_pmtud(struct rxrpc_call *call)
{
	rxrpc_send_ACK(call, RXRPC_ACK_PING, 0,
		       rxrpc_propose_ack_ping_for_mtu_probe);
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
static size_t rxrpc_prepare_data_subpacket(struct rxrpc_call *call,
					   struct rxrpc_send_data_req *req,
					   struct rxrpc_txbuf *txb,
					   struct rxrpc_wire_header *whdr,
					   rxrpc_serial_t serial, int subpkt)
{
	struct rxrpc_jumbo_header *jumbo = txb->data - sizeof(*jumbo);
	enum rxrpc_req_ack_trace why;
	struct rxrpc_connection *conn = call->conn;
	struct kvec *kv = &call->local->kvec[1 + subpkt];
	size_t len = txb->pkt_len;
	bool last;
	u8 flags;

	_enter("%x,%zd", txb->seq, len);

	txb->serial = serial;

	if (test_bit(RXRPC_CONN_PROBING_FOR_UPGRADE, &conn->flags) &&
	    txb->seq == 1)
		whdr->userStatus = RXRPC_USERSTATUS_SERVICE_UPGRADE;

	txb->flags &= ~RXRPC_REQUEST_ACK;
	flags = txb->flags & RXRPC_TXBUF_WIRE_FLAGS;
	last = txb->flags & RXRPC_LAST_PACKET;

	if (subpkt < req->n - 1) {
		len = RXRPC_JUMBO_DATALEN;
		goto dont_set_request_ack;
	}

	/* If our RTT cache needs working on, request an ACK.  Also request
	 * ACKs if a DATA packet appears to have been lost.
	 *
	 * However, we mustn't request an ACK on the last reply packet of a
	 * service call, lest OpenAFS incorrectly send us an ACK with some
	 * soft-ACKs in it and then never follow up with a proper hard ACK.
	 */
	if (last && rxrpc_sending_to_client(txb))
		why = rxrpc_reqack_no_srv_last;
	else if (test_and_clear_bit(RXRPC_CALL_EV_ACK_LOST, &call->events))
		why = rxrpc_reqack_ack_lost;
	else if (txb->flags & RXRPC_TXBUF_RESENT)
		why = rxrpc_reqack_retrans;
	else if (call->cong_ca_state == RXRPC_CA_SLOW_START && call->cong_cwnd <= RXRPC_MIN_CWND)
		why = rxrpc_reqack_slow_start;
	else if (call->tx_winsize <= 2)
		why = rxrpc_reqack_small_txwin;
	else if (call->rtt_count < 3)
		why = rxrpc_reqack_more_rtt;
	else if (ktime_before(ktime_add_ms(call->rtt_last_req, 1000), ktime_get_real()))
		why = rxrpc_reqack_old_rtt;
	else if (!last && !after(READ_ONCE(call->send_top), txb->seq))
		why = rxrpc_reqack_app_stall;
	else
		goto dont_set_request_ack;

	rxrpc_inc_stat(call->rxnet, stat_why_req_ack[why]);
	trace_rxrpc_req_ack(call->debug_id, txb->seq, why);
	if (why != rxrpc_reqack_no_srv_last) {
		flags |= RXRPC_REQUEST_ACK;
		trace_rxrpc_rtt_tx(call, rxrpc_rtt_tx_data, -1, serial);
		call->rtt_last_req = req->now;
	}
dont_set_request_ack:

	/* There's a jumbo header prepended to the data if we need it. */
	if (subpkt < req->n - 1)
		flags |= RXRPC_JUMBO_PACKET;
	else
		flags &= ~RXRPC_JUMBO_PACKET;
	if (subpkt == 0) {
		whdr->flags	= flags;
		whdr->cksum	= txb->cksum;
		kv->iov_base	= txb->data;
	} else {
		jumbo->flags	= flags;
		jumbo->pad	= 0;
		jumbo->cksum	= txb->cksum;
		kv->iov_base	= jumbo;
		len += sizeof(*jumbo);
	}

	trace_rxrpc_tx_data(call, txb->seq, txb->serial, flags, req->trace);
	kv->iov_len = len;
	return len;
}

/*
 * Prepare a transmission queue object for initial transmission.  Returns the
 * number of microseconds since the transmission queue base timestamp.
 */
static unsigned int rxrpc_prepare_txqueue(struct rxrpc_txqueue *tq,
					  struct rxrpc_send_data_req *req)
{
	if (!tq)
		return 0;
	if (tq->xmit_ts_base == KTIME_MIN) {
		tq->xmit_ts_base = req->now;
		return 0;
	}
	return ktime_to_us(ktime_sub(req->now, tq->xmit_ts_base));
}

/*
 * Prepare a (jumbo) packet for transmission.
 */
static size_t rxrpc_prepare_data_packet(struct rxrpc_call *call,
					struct rxrpc_send_data_req *req,
					struct rxrpc_wire_header *whdr)
{
	struct rxrpc_txqueue *tq = req->tq;
	rxrpc_serial_t serial;
	unsigned int xmit_ts;
	rxrpc_seq_t seq = req->seq;
	size_t len = 0;
	bool start_tlp = false;

	trace_rxrpc_tq(call, tq, seq, rxrpc_tq_transmit);

	/* Each transmission of a Tx packet needs a new serial number */
	serial = rxrpc_get_next_serials(call->conn, req->n);

	whdr->epoch		= htonl(call->conn->proto.epoch);
	whdr->cid		= htonl(call->cid);
	whdr->callNumber	= htonl(call->call_id);
	whdr->seq		= htonl(seq);
	whdr->serial		= htonl(serial);
	whdr->type		= RXRPC_PACKET_TYPE_DATA;
	whdr->flags		= 0;
	whdr->userStatus	= 0;
	whdr->securityIndex	= call->security_ix;
	whdr->_rsvd		= 0;
	whdr->serviceId		= htons(call->conn->service_id);

	call->tx_last_serial = serial + req->n - 1;
	call->tx_last_sent = req->now;
	xmit_ts = rxrpc_prepare_txqueue(tq, req);
	prefetch(tq->next);

	for (int i = 0;;) {
		int ix = seq & RXRPC_TXQ_MASK;
		struct rxrpc_txbuf *txb = tq->bufs[seq & RXRPC_TXQ_MASK];

		_debug("prep[%u] tq=%x q=%x", i, tq->qbase, seq);

		/* Record (re-)transmission for RACK [RFC8985 6.1]. */
		if (__test_and_clear_bit(ix, &tq->segment_lost))
			call->tx_nr_lost--;
		if (req->retrans) {
			__set_bit(ix, &tq->ever_retransmitted);
			__set_bit(ix, &tq->segment_retransmitted);
			call->tx_nr_resent++;
		} else {
			call->tx_nr_sent++;
			start_tlp = true;
		}
		tq->segment_xmit_ts[ix] = xmit_ts;
		tq->segment_serial[ix] = serial;
		if (i + 1 == req->n)
			/* Only sample the last subpacket in a jumbo. */
			__set_bit(ix, &tq->rtt_samples);
		len += rxrpc_prepare_data_subpacket(call, req, txb, whdr, serial, i);
		serial++;
		seq++;
		i++;
		if (i >= req->n)
			break;
		if (!(seq & RXRPC_TXQ_MASK)) {
			tq = tq->next;
			trace_rxrpc_tq(call, tq, seq, rxrpc_tq_transmit_advance);
			xmit_ts = rxrpc_prepare_txqueue(tq, req);
		}
	}

	/* Set timeouts */
	if (req->tlp_probe) {
		/* Sending TLP loss probe [RFC8985 7.3]. */
		call->tlp_serial = serial - 1;
		call->tlp_seq = seq - 1;
	} else if (start_tlp) {
		/* Schedule TLP loss probe [RFC8985 7.2]. */
		ktime_t pto;

		if (!test_bit(RXRPC_CALL_BEGAN_RX_TIMER, &call->flags))
			 /* The first packet may take longer to elicit a response. */
			pto = NSEC_PER_SEC;
		else
			pto = rxrpc_tlp_calc_pto(call, req->now);

		call->rack_timer_mode = RXRPC_CALL_RACKTIMER_TLP_PTO;
		call->rack_timo_at = ktime_add(req->now, pto);
		trace_rxrpc_rack_timer(call, pto, false);
		trace_rxrpc_timer_set(call, pto, rxrpc_timer_trace_rack_tlp_pto);
	}

	if (!test_and_set_bit(RXRPC_CALL_BEGAN_RX_TIMER, &call->flags)) {
		ktime_t delay = ms_to_ktime(READ_ONCE(call->next_rx_timo));

		call->expect_rx_by = ktime_add(req->now, delay);
		trace_rxrpc_timer_set(call, delay, rxrpc_timer_trace_expect_rx);
	}

	rxrpc_set_keepalive(call, req->now);
	page_frag_free(whdr);
	return len;
}

/*
 * Send one or more packets through the transport endpoint
 */
void rxrpc_send_data_packet(struct rxrpc_call *call, struct rxrpc_send_data_req *req)
{
	struct rxrpc_wire_header *whdr;
	struct rxrpc_connection *conn = call->conn;
	enum rxrpc_tx_point frag;
	struct rxrpc_txqueue *tq = req->tq;
	struct rxrpc_txbuf *txb;
	struct msghdr msg;
	rxrpc_seq_t seq = req->seq;
	size_t len = sizeof(*whdr);
	bool new_call = test_bit(RXRPC_CALL_BEGAN_RX_TIMER, &call->flags);
	int ret, stat_ix;

	_enter("%x,%x-%x", tq->qbase, seq, seq + req->n - 1);

	whdr = page_frag_alloc(&call->local->tx_alloc, sizeof(*whdr), GFP_NOFS);
	if (!whdr)
		return; /* Drop the packet if no memory. */

	call->local->kvec[0].iov_base = whdr;
	call->local->kvec[0].iov_len = sizeof(*whdr);

	stat_ix = umin(req->n, ARRAY_SIZE(call->rxnet->stat_tx_jumbo)) - 1;
	atomic_inc(&call->rxnet->stat_tx_jumbo[stat_ix]);

	len += rxrpc_prepare_data_packet(call, req, whdr);
	txb = tq->bufs[seq & RXRPC_TXQ_MASK];

	iov_iter_kvec(&msg.msg_iter, WRITE, call->local->kvec, 1 + req->n, len);

	msg.msg_name	= &call->peer->srx.transport;
	msg.msg_namelen	= call->peer->srx.transport_len;
	msg.msg_control	= NULL;
	msg.msg_controllen = 0;
	msg.msg_flags	= MSG_SPLICE_PAGES;

	/* Send the packet with the don't fragment bit set unless we think it's
	 * too big or if this is a retransmission.
	 */
	if (seq == call->tx_transmitted + 1 &&
	    len >= sizeof(struct rxrpc_wire_header) + call->peer->max_data) {
		rxrpc_local_dont_fragment(conn->local, false);
		frag = rxrpc_tx_point_call_data_frag;
	} else {
		rxrpc_local_dont_fragment(conn->local, true);
		frag = rxrpc_tx_point_call_data_nofrag;
	}

	/* Track what we've attempted to transmit at least once so that the
	 * retransmission algorithm doesn't try to resend what we haven't sent
	 * yet.
	 */
	if (seq == call->tx_transmitted + 1)
		call->tx_transmitted = seq + req->n - 1;

	if (IS_ENABLED(CONFIG_AF_RXRPC_INJECT_LOSS)) {
		static int lose;

		if ((lose++ & 7) == 7) {
			ret = 0;
			trace_rxrpc_tx_data(call, txb->seq, txb->serial, txb->flags,
					    rxrpc_txdata_inject_loss);
			conn->peer->last_tx_at = ktime_get_seconds();
			goto done;
		}
	}

	/* send the packet by UDP
	 * - returns -EMSGSIZE if UDP would have to fragment the packet
	 *   to go out of the interface
	 *   - in which case, we'll have processed the ICMP error
	 *     message and update the peer record
	 */
	rxrpc_inc_stat(call->rxnet, stat_tx_data_send);
	ret = do_udp_sendmsg(conn->local->socket, &msg, len);
	conn->peer->last_tx_at = ktime_get_seconds();

	if (ret == -EMSGSIZE) {
		rxrpc_inc_stat(call->rxnet, stat_tx_data_send_msgsize);
		trace_rxrpc_tx_packet(call->debug_id, whdr, frag);
		ret = 0;
	} else if (ret < 0) {
		rxrpc_inc_stat(call->rxnet, stat_tx_data_send_fail);
		trace_rxrpc_tx_fail(call->debug_id, txb->serial, ret, frag);
	} else {
		trace_rxrpc_tx_packet(call->debug_id, whdr, frag);
	}

	rxrpc_tx_backoff(call, ret);

	if (ret < 0) {
		/* Cancel the call if the initial transmission fails or if we
		 * hit due to network routing issues that aren't going away
		 * anytime soon.  The layer above can arrange the
		 * retransmission.
		 */
		if (new_call ||
		    ret == -ENETUNREACH ||
		    ret == -EHOSTUNREACH ||
		    ret == -ECONNREFUSED)
			rxrpc_set_call_completion(call, RXRPC_CALL_LOCAL_ERROR,
						  RX_USER_ABORT, ret);
	}

done:
	_leave(" = %d [%u]", ret, call->peer->max_data);
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
