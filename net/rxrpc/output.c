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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/net.h>
#include <linux/gfp.h>
#include <linux/skbuff.h>
#include <linux/export.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include "ar-internal.h"

struct rxrpc_ack_buffer {
	struct rxrpc_wire_header whdr;
	struct rxrpc_ackpacket ack;
	u8 acks[255];
	u8 pad[3];
	struct rxrpc_ackinfo ackinfo;
};

struct rxrpc_abort_buffer {
	struct rxrpc_wire_header whdr;
	__be32 abort_code;
};

static const char rxrpc_keepalive_string[] = "";

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
				 struct rxrpc_ack_buffer *pkt,
				 rxrpc_seq_t *_hard_ack,
				 rxrpc_seq_t *_top,
				 u8 reason)
{
	rxrpc_serial_t serial;
	rxrpc_seq_t hard_ack, top, seq;
	int ix;
	u32 mtu, jmax;
	u8 *ackp = pkt->acks;

	/* Barrier against rxrpc_input_data(). */
	serial = call->ackr_serial;
	hard_ack = READ_ONCE(call->rx_hard_ack);
	top = smp_load_acquire(&call->rx_top);
	*_hard_ack = hard_ack;
	*_top = top;

	pkt->ack.bufferSpace	= htons(8);
	pkt->ack.maxSkew	= htons(call->ackr_skew);
	pkt->ack.firstPacket	= htonl(hard_ack + 1);
	pkt->ack.previousPacket	= htonl(call->ackr_prev_seq);
	pkt->ack.serial		= htonl(serial);
	pkt->ack.reason		= reason;
	pkt->ack.nAcks		= top - hard_ack;

	if (reason == RXRPC_ACK_PING)
		pkt->whdr.flags |= RXRPC_REQUEST_ACK;

	if (after(top, hard_ack)) {
		seq = hard_ack + 1;
		do {
			ix = seq & RXRPC_RXTX_BUFF_MASK;
			if (call->rxtx_buffer[ix])
				*ackp++ = RXRPC_ACK_TYPE_ACK;
			else
				*ackp++ = RXRPC_ACK_TYPE_NACK;
			seq++;
		} while (before_eq(seq, top));
	}

	mtu = conn->params.peer->if_mtu;
	mtu -= conn->params.peer->hdrsize;
	jmax = (call->nr_jumbo_bad > 3) ? 1 : rxrpc_rx_jumbo_max;
	pkt->ackinfo.rxMTU	= htonl(rxrpc_rx_mtu);
	pkt->ackinfo.maxMTU	= htonl(mtu);
	pkt->ackinfo.rwind	= htonl(call->rx_winsize);
	pkt->ackinfo.jumbo_max	= htonl(jmax);

	*ackp++ = 0;
	*ackp++ = 0;
	*ackp++ = 0;
	return top - hard_ack + 3;
}

/*
 * Send an ACK call packet.
 */
int rxrpc_send_ack_packet(struct rxrpc_call *call, bool ping,
			  rxrpc_serial_t *_serial)
{
	struct rxrpc_connection *conn = NULL;
	struct rxrpc_ack_buffer *pkt;
	struct msghdr msg;
	struct kvec iov[2];
	rxrpc_serial_t serial;
	rxrpc_seq_t hard_ack, top;
	size_t len, n;
	int ret;
	u8 reason;

	spin_lock_bh(&call->lock);
	if (call->conn)
		conn = rxrpc_get_connection_maybe(call->conn);
	spin_unlock_bh(&call->lock);
	if (!conn)
		return -ECONNRESET;

	pkt = kzalloc(sizeof(*pkt), GFP_KERNEL);
	if (!pkt) {
		rxrpc_put_connection(conn);
		return -ENOMEM;
	}

	msg.msg_name	= &call->peer->srx.transport;
	msg.msg_namelen	= call->peer->srx.transport_len;
	msg.msg_control	= NULL;
	msg.msg_controllen = 0;
	msg.msg_flags	= 0;

	pkt->whdr.epoch		= htonl(conn->proto.epoch);
	pkt->whdr.cid		= htonl(call->cid);
	pkt->whdr.callNumber	= htonl(call->call_id);
	pkt->whdr.seq		= 0;
	pkt->whdr.type		= RXRPC_PACKET_TYPE_ACK;
	pkt->whdr.flags		= RXRPC_SLOW_START_OK | conn->out_clientflag;
	pkt->whdr.userStatus	= 0;
	pkt->whdr.securityIndex	= call->security_ix;
	pkt->whdr._rsvd		= 0;
	pkt->whdr.serviceId	= htons(call->service_id);

	spin_lock_bh(&call->lock);
	if (ping) {
		reason = RXRPC_ACK_PING;
	} else {
		reason = call->ackr_reason;
		if (!call->ackr_reason) {
			spin_unlock_bh(&call->lock);
			ret = 0;
			goto out;
		}
		call->ackr_reason = 0;
	}
	n = rxrpc_fill_out_ack(conn, call, pkt, &hard_ack, &top, reason);

	spin_unlock_bh(&call->lock);

	iov[0].iov_base	= pkt;
	iov[0].iov_len	= sizeof(pkt->whdr) + sizeof(pkt->ack) + n;
	iov[1].iov_base = &pkt->ackinfo;
	iov[1].iov_len	= sizeof(pkt->ackinfo);
	len = iov[0].iov_len + iov[1].iov_len;

	serial = atomic_inc_return(&conn->serial);
	pkt->whdr.serial = htonl(serial);
	trace_rxrpc_tx_ack(call->debug_id, serial,
			   ntohl(pkt->ack.firstPacket),
			   ntohl(pkt->ack.serial),
			   pkt->ack.reason, pkt->ack.nAcks);
	if (_serial)
		*_serial = serial;

	if (ping) {
		call->ping_serial = serial;
		smp_wmb();
		/* We need to stick a time in before we send the packet in case
		 * the reply gets back before kernel_sendmsg() completes - but
		 * asking UDP to send the packet can take a relatively long
		 * time.
		 */
		call->ping_time = ktime_get_real();
		set_bit(RXRPC_CALL_PINGING, &call->flags);
		trace_rxrpc_rtt_tx(call, rxrpc_rtt_tx_ping, serial);
	}

	ret = kernel_sendmsg(conn->params.local->socket, &msg, iov, 2, len);
	conn->params.peer->last_tx_at = ktime_get_seconds();
	if (ret < 0)
		trace_rxrpc_tx_fail(call->debug_id, serial, ret,
				    rxrpc_tx_point_call_ack);
	else
		trace_rxrpc_tx_packet(call->debug_id, &pkt->whdr,
				      rxrpc_tx_point_call_ack);

	if (call->state < RXRPC_CALL_COMPLETE) {
		if (ret < 0) {
			if (ping)
				clear_bit(RXRPC_CALL_PINGING, &call->flags);
			rxrpc_propose_ACK(call, pkt->ack.reason,
					  ntohs(pkt->ack.maxSkew),
					  ntohl(pkt->ack.serial),
					  true, true,
					  rxrpc_propose_ack_retry_tx);
		} else {
			spin_lock_bh(&call->lock);
			if (after(hard_ack, call->ackr_consumed))
				call->ackr_consumed = hard_ack;
			if (after(top, call->ackr_seen))
				call->ackr_seen = top;
			spin_unlock_bh(&call->lock);
		}

		rxrpc_set_keepalive(call);
	}

out:
	rxrpc_put_connection(conn);
	kfree(pkt);
	return ret;
}

/*
 * Send an ABORT call packet.
 */
int rxrpc_send_abort_packet(struct rxrpc_call *call)
{
	struct rxrpc_connection *conn = NULL;
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
	    test_bit(RXRPC_CALL_TX_LAST, &call->flags))
		return 0;

	spin_lock_bh(&call->lock);
	if (call->conn)
		conn = rxrpc_get_connection_maybe(call->conn);
	spin_unlock_bh(&call->lock);
	if (!conn)
		return -ECONNRESET;

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
	pkt.whdr.serviceId	= htons(call->service_id);
	pkt.abort_code		= htonl(call->abort_code);

	iov[0].iov_base	= &pkt;
	iov[0].iov_len	= sizeof(pkt);

	serial = atomic_inc_return(&conn->serial);
	pkt.whdr.serial = htonl(serial);

	ret = kernel_sendmsg(conn->params.local->socket,
			     &msg, iov, 1, sizeof(pkt));
	conn->params.peer->last_tx_at = ktime_get_seconds();
	if (ret < 0)
		trace_rxrpc_tx_fail(call->debug_id, serial, ret,
				    rxrpc_tx_point_call_abort);
	else
		trace_rxrpc_tx_packet(call->debug_id, &pkt.whdr,
				      rxrpc_tx_point_call_abort);


	rxrpc_put_connection(conn);
	return ret;
}

/*
 * send a packet through the transport endpoint
 */
int rxrpc_send_data_packet(struct rxrpc_call *call, struct sk_buff *skb,
			   bool retrans)
{
	struct rxrpc_connection *conn = call->conn;
	struct rxrpc_wire_header whdr;
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct msghdr msg;
	struct kvec iov[2];
	rxrpc_serial_t serial;
	size_t len;
	bool lost = false;
	int ret, opt;

	_enter(",{%d}", skb->len);

	/* Each transmission of a Tx packet needs a new serial number */
	serial = atomic_inc_return(&conn->serial);

	whdr.epoch	= htonl(conn->proto.epoch);
	whdr.cid	= htonl(call->cid);
	whdr.callNumber	= htonl(call->call_id);
	whdr.seq	= htonl(sp->hdr.seq);
	whdr.serial	= htonl(serial);
	whdr.type	= RXRPC_PACKET_TYPE_DATA;
	whdr.flags	= sp->hdr.flags;
	whdr.userStatus	= 0;
	whdr.securityIndex = call->security_ix;
	whdr._rsvd	= htons(sp->hdr._rsvd);
	whdr.serviceId	= htons(call->service_id);

	if (test_bit(RXRPC_CONN_PROBING_FOR_UPGRADE, &conn->flags) &&
	    sp->hdr.seq == 1)
		whdr.userStatus	= RXRPC_USERSTATUS_SERVICE_UPGRADE;

	iov[0].iov_base = &whdr;
	iov[0].iov_len = sizeof(whdr);
	iov[1].iov_base = skb->head;
	iov[1].iov_len = skb->len;
	len = iov[0].iov_len + iov[1].iov_len;

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
	if ((!(sp->hdr.flags & RXRPC_LAST_PACKET) ||
	     rxrpc_to_server(sp)
	     ) &&
	    (test_and_clear_bit(RXRPC_CALL_EV_ACK_LOST, &call->events) ||
	     retrans ||
	     call->cong_mode == RXRPC_CALL_SLOW_START ||
	     (call->peer->rtt_usage < 3 && sp->hdr.seq & 1) ||
	     ktime_before(ktime_add_ms(call->peer->rtt_last_req, 1000),
			  ktime_get_real())))
		whdr.flags |= RXRPC_REQUEST_ACK;

	if (IS_ENABLED(CONFIG_AF_RXRPC_INJECT_LOSS)) {
		static int lose;
		if ((lose++ & 7) == 7) {
			ret = 0;
			lost = true;
			goto done;
		}
	}

	_proto("Tx DATA %%%u { #%u }", serial, sp->hdr.seq);

	/* send the packet with the don't fragment bit set if we currently
	 * think it's small enough */
	if (iov[1].iov_len >= call->peer->maxdata)
		goto send_fragmentable;

	down_read(&conn->params.local->defrag_sem);

	sp->hdr.serial = serial;
	smp_wmb(); /* Set serial before timestamp */
	skb->tstamp = ktime_get_real();

	/* send the packet by UDP
	 * - returns -EMSGSIZE if UDP would have to fragment the packet
	 *   to go out of the interface
	 *   - in which case, we'll have processed the ICMP error
	 *     message and update the peer record
	 */
	ret = kernel_sendmsg(conn->params.local->socket, &msg, iov, 2, len);
	conn->params.peer->last_tx_at = ktime_get_seconds();

	up_read(&conn->params.local->defrag_sem);
	if (ret < 0)
		trace_rxrpc_tx_fail(call->debug_id, serial, ret,
				    rxrpc_tx_point_call_data_nofrag);
	else
		trace_rxrpc_tx_packet(call->debug_id, &whdr,
				      rxrpc_tx_point_call_data_nofrag);
	if (ret == -EMSGSIZE)
		goto send_fragmentable;

done:
	trace_rxrpc_tx_data(call, sp->hdr.seq, serial, whdr.flags,
			    retrans, lost);
	if (ret >= 0) {
		if (whdr.flags & RXRPC_REQUEST_ACK) {
			call->peer->rtt_last_req = skb->tstamp;
			trace_rxrpc_rtt_tx(call, rxrpc_rtt_tx_data, serial);
			if (call->peer->rtt_usage > 1) {
				unsigned long nowj = jiffies, ack_lost_at;

				ack_lost_at = nsecs_to_jiffies(2 * call->peer->rtt);
				if (ack_lost_at < 1)
					ack_lost_at = 1;

				ack_lost_at += nowj;
				WRITE_ONCE(call->ack_lost_at, ack_lost_at);
				rxrpc_reduce_call_timer(call, ack_lost_at, nowj,
							rxrpc_timer_set_for_lost_ack);
			}
		}

		if (sp->hdr.seq == 1 &&
		    !test_and_set_bit(RXRPC_CALL_BEGAN_RX_TIMER,
				      &call->flags)) {
			unsigned long nowj = jiffies, expect_rx_by;

			expect_rx_by = nowj + call->next_rx_timo;
			WRITE_ONCE(call->expect_rx_by, expect_rx_by);
			rxrpc_reduce_call_timer(call, expect_rx_by, nowj,
						rxrpc_timer_set_for_normal);
		}
	}

	rxrpc_set_keepalive(call);

	_leave(" = %d [%u]", ret, call->peer->maxdata);
	return ret;

send_fragmentable:
	/* attempt to send this message with fragmentation enabled */
	_debug("send fragment");

	down_write(&conn->params.local->defrag_sem);

	sp->hdr.serial = serial;
	smp_wmb(); /* Set serial before timestamp */
	skb->tstamp = ktime_get_real();

	switch (conn->params.local->srx.transport.family) {
	case AF_INET:
		opt = IP_PMTUDISC_DONT;
		ret = kernel_setsockopt(conn->params.local->socket,
					SOL_IP, IP_MTU_DISCOVER,
					(char *)&opt, sizeof(opt));
		if (ret == 0) {
			ret = kernel_sendmsg(conn->params.local->socket, &msg,
					     iov, 2, len);
			conn->params.peer->last_tx_at = ktime_get_seconds();

			opt = IP_PMTUDISC_DO;
			kernel_setsockopt(conn->params.local->socket, SOL_IP,
					  IP_MTU_DISCOVER,
					  (char *)&opt, sizeof(opt));
		}
		break;

#ifdef CONFIG_AF_RXRPC_IPV6
	case AF_INET6:
		opt = IPV6_PMTUDISC_DONT;
		ret = kernel_setsockopt(conn->params.local->socket,
					SOL_IPV6, IPV6_MTU_DISCOVER,
					(char *)&opt, sizeof(opt));
		if (ret == 0) {
			ret = kernel_sendmsg(conn->params.local->socket, &msg,
					     iov, 2, len);
			conn->params.peer->last_tx_at = ktime_get_seconds();

			opt = IPV6_PMTUDISC_DO;
			kernel_setsockopt(conn->params.local->socket,
					  SOL_IPV6, IPV6_MTU_DISCOVER,
					  (char *)&opt, sizeof(opt));
		}
		break;
#endif
	}

	if (ret < 0)
		trace_rxrpc_tx_fail(call->debug_id, serial, ret,
				    rxrpc_tx_point_call_data_frag);
	else
		trace_rxrpc_tx_packet(call->debug_id, &whdr,
				      rxrpc_tx_point_call_data_frag);

	up_write(&conn->params.local->defrag_sem);
	goto done;
}

/*
 * reject packets through the local endpoint
 */
void rxrpc_reject_packets(struct rxrpc_local *local)
{
	struct sockaddr_rxrpc srx;
	struct rxrpc_skb_priv *sp;
	struct rxrpc_wire_header whdr;
	struct sk_buff *skb;
	struct msghdr msg;
	struct kvec iov[2];
	size_t size;
	__be32 code;
	int ret, ioc;

	_enter("%d", local->debug_id);

	iov[0].iov_base = &whdr;
	iov[0].iov_len = sizeof(whdr);
	iov[1].iov_base = &code;
	iov[1].iov_len = sizeof(code);

	msg.msg_name = &srx.transport;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;

	memset(&whdr, 0, sizeof(whdr));

	while ((skb = skb_dequeue(&local->reject_queue))) {
		rxrpc_see_skb(skb, rxrpc_skb_rx_seen);
		sp = rxrpc_skb(skb);

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
			rxrpc_free_skb(skb, rxrpc_skb_rx_freed);
			continue;
		}

		if (rxrpc_extract_addr_from_skb(local, &srx, skb) == 0) {
			msg.msg_namelen = srx.transport_len;

			whdr.epoch	= htonl(sp->hdr.epoch);
			whdr.cid	= htonl(sp->hdr.cid);
			whdr.callNumber	= htonl(sp->hdr.callNumber);
			whdr.serviceId	= htons(sp->hdr.serviceId);
			whdr.flags	= sp->hdr.flags;
			whdr.flags	^= RXRPC_CLIENT_INITIATED;
			whdr.flags	&= RXRPC_CLIENT_INITIATED;

			ret = kernel_sendmsg(local->socket, &msg, iov, 2, size);
			if (ret < 0)
				trace_rxrpc_tx_fail(local->debug_id, 0, ret,
						    rxrpc_tx_point_reject);
			else
				trace_rxrpc_tx_packet(local->debug_id, &whdr,
						      rxrpc_tx_point_reject);
		}

		rxrpc_free_skb(skb, rxrpc_skb_rx_freed);
	}

	_leave("");
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

	_proto("Tx VERSION (keepalive)");

	ret = kernel_sendmsg(peer->local->socket, &msg, iov, 2, len);
	if (ret < 0)
		trace_rxrpc_tx_fail(peer->debug_id, 0, ret,
				    rxrpc_tx_point_version_keepalive);
	else
		trace_rxrpc_tx_packet(peer->debug_id, &whdr,
				      rxrpc_tx_point_version_keepalive);

	peer->last_tx_at = ktime_get_seconds();
	_leave("");
}
