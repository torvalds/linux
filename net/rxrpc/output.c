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

struct rxrpc_pkt_buffer {
	struct rxrpc_wire_header whdr;
	union {
		struct {
			struct rxrpc_ackpacket ack;
			u8 acks[255];
			u8 pad[3];
		};
		__be32 abort_code;
	};
	struct rxrpc_ackinfo ackinfo;
};

/*
 * Fill out an ACK packet.
 */
static size_t rxrpc_fill_out_ack(struct rxrpc_call *call,
				 struct rxrpc_pkt_buffer *pkt)
{
	rxrpc_seq_t hard_ack, top, seq;
	int ix;
	u32 mtu, jmax;
	u8 *ackp = pkt->acks;

	/* Barrier against rxrpc_input_data(). */
	hard_ack = READ_ONCE(call->rx_hard_ack);
	top = smp_load_acquire(&call->rx_top);

	pkt->ack.bufferSpace	= htons(8);
	pkt->ack.maxSkew	= htons(call->ackr_skew);
	pkt->ack.firstPacket	= htonl(hard_ack + 1);
	pkt->ack.previousPacket	= htonl(call->ackr_prev_seq);
	pkt->ack.serial		= htonl(call->ackr_serial);
	pkt->ack.reason		= call->ackr_reason;
	pkt->ack.nAcks		= top - hard_ack;

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

	mtu = call->conn->params.peer->if_mtu;
	mtu -= call->conn->params.peer->hdrsize;
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
 * Send an ACK or ABORT call packet.
 */
int rxrpc_send_call_packet(struct rxrpc_call *call, u8 type)
{
	struct rxrpc_connection *conn = NULL;
	struct rxrpc_pkt_buffer *pkt;
	struct msghdr msg;
	struct kvec iov[2];
	rxrpc_serial_t serial;
	size_t len, n;
	int ioc, ret;
	u32 abort_code;

	_enter("%u,%s", call->debug_id, rxrpc_pkts[type]);

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

	serial = atomic_inc_return(&conn->serial);

	msg.msg_name	= &call->peer->srx.transport;
	msg.msg_namelen	= call->peer->srx.transport_len;
	msg.msg_control	= NULL;
	msg.msg_controllen = 0;
	msg.msg_flags	= 0;

	pkt->whdr.epoch		= htonl(conn->proto.epoch);
	pkt->whdr.cid		= htonl(call->cid);
	pkt->whdr.callNumber	= htonl(call->call_id);
	pkt->whdr.seq		= 0;
	pkt->whdr.serial	= htonl(serial);
	pkt->whdr.type		= type;
	pkt->whdr.flags		= conn->out_clientflag;
	pkt->whdr.userStatus	= 0;
	pkt->whdr.securityIndex	= call->security_ix;
	pkt->whdr._rsvd		= 0;
	pkt->whdr.serviceId	= htons(call->service_id);

	iov[0].iov_base	= pkt;
	iov[0].iov_len	= sizeof(pkt->whdr);
	len = sizeof(pkt->whdr);

	switch (type) {
	case RXRPC_PACKET_TYPE_ACK:
		spin_lock_bh(&call->lock);
		n = rxrpc_fill_out_ack(call, pkt);
		call->ackr_reason = 0;

		spin_unlock_bh(&call->lock);

		_proto("Tx ACK %%%u { m=%hu f=#%u p=#%u s=%%%u r=%s n=%u }",
		       serial,
		       ntohs(pkt->ack.maxSkew),
		       ntohl(pkt->ack.firstPacket),
		       ntohl(pkt->ack.previousPacket),
		       ntohl(pkt->ack.serial),
		       rxrpc_acks(pkt->ack.reason),
		       pkt->ack.nAcks);

		iov[0].iov_len += sizeof(pkt->ack) + n;
		iov[1].iov_base = &pkt->ackinfo;
		iov[1].iov_len	= sizeof(pkt->ackinfo);
		len += sizeof(pkt->ack) + n + sizeof(pkt->ackinfo);
		ioc = 2;
		break;

	case RXRPC_PACKET_TYPE_ABORT:
		abort_code = call->abort_code;
		pkt->abort_code = htonl(abort_code);
		_proto("Tx ABORT %%%u { %d }", serial, abort_code);
		iov[0].iov_len += sizeof(pkt->abort_code);
		len += sizeof(pkt->abort_code);
		ioc = 1;
		break;

	default:
		BUG();
		ret = -ENOANO;
		goto out;
	}

	ret = kernel_sendmsg(conn->params.local->socket,
			     &msg, iov, ioc, len);

	if (ret < 0 && call->state < RXRPC_CALL_COMPLETE) {
		switch (pkt->whdr.type) {
		case RXRPC_PACKET_TYPE_ACK:
			rxrpc_propose_ACK(call, pkt->ack.reason,
					  ntohs(pkt->ack.maxSkew),
					  ntohl(pkt->ack.serial),
					  true, true);
			break;
		case RXRPC_PACKET_TYPE_ABORT:
			break;
		}
	}

out:
	rxrpc_put_connection(conn);
	kfree(pkt);
	return ret;
}

/*
 * send a packet through the transport endpoint
 */
int rxrpc_send_data_packet(struct rxrpc_connection *conn, struct sk_buff *skb)
{
	struct kvec iov[1];
	struct msghdr msg;
	int ret, opt;

	_enter(",{%d}", skb->len);

	iov[0].iov_base = skb->head;
	iov[0].iov_len = skb->len;

	msg.msg_name = &conn->params.peer->srx.transport;
	msg.msg_namelen = conn->params.peer->srx.transport_len;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;

	/* send the packet with the don't fragment bit set if we currently
	 * think it's small enough */
	if (skb->len - sizeof(struct rxrpc_wire_header) < conn->params.peer->maxdata) {
		down_read(&conn->params.local->defrag_sem);
		/* send the packet by UDP
		 * - returns -EMSGSIZE if UDP would have to fragment the packet
		 *   to go out of the interface
		 *   - in which case, we'll have processed the ICMP error
		 *     message and update the peer record
		 */
		ret = kernel_sendmsg(conn->params.local->socket, &msg, iov, 1,
				     iov[0].iov_len);

		up_read(&conn->params.local->defrag_sem);
		if (ret == -EMSGSIZE)
			goto send_fragmentable;

		_leave(" = %d [%u]", ret, conn->params.peer->maxdata);
		return ret;
	}

send_fragmentable:
	/* attempt to send this message with fragmentation enabled */
	_debug("send fragment");

	down_write(&conn->params.local->defrag_sem);

	switch (conn->params.local->srx.transport.family) {
	case AF_INET:
		opt = IP_PMTUDISC_DONT;
		ret = kernel_setsockopt(conn->params.local->socket,
					SOL_IP, IP_MTU_DISCOVER,
					(char *)&opt, sizeof(opt));
		if (ret == 0) {
			ret = kernel_sendmsg(conn->params.local->socket, &msg, iov, 1,
					     iov[0].iov_len);

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
					     iov, 1, iov[0].iov_len);

			opt = IPV6_PMTUDISC_DO;
			kernel_setsockopt(conn->params.local->socket,
					  SOL_IPV6, IPV6_MTU_DISCOVER,
					  (char *)&opt, sizeof(opt));
		}
		break;
#endif
	}

	up_write(&conn->params.local->defrag_sem);
	_leave(" = %d [frag %u]", ret, conn->params.peer->maxdata);
	return ret;
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

	_enter("%d", local->debug_id);

	iov[0].iov_base = &whdr;
	iov[0].iov_len = sizeof(whdr);
	iov[1].iov_base = &code;
	iov[1].iov_len = sizeof(code);
	size = sizeof(whdr) + sizeof(code);

	msg.msg_name = &srx.transport;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;

	memset(&whdr, 0, sizeof(whdr));
	whdr.type = RXRPC_PACKET_TYPE_ABORT;

	while ((skb = skb_dequeue(&local->reject_queue))) {
		rxrpc_see_skb(skb);
		sp = rxrpc_skb(skb);

		if (rxrpc_extract_addr_from_skb(&srx, skb) == 0) {
			msg.msg_namelen = srx.transport_len;

			code = htonl(skb->priority);

			whdr.epoch	= htonl(sp->hdr.epoch);
			whdr.cid	= htonl(sp->hdr.cid);
			whdr.callNumber	= htonl(sp->hdr.callNumber);
			whdr.serviceId	= htons(sp->hdr.serviceId);
			whdr.flags	= sp->hdr.flags;
			whdr.flags	^= RXRPC_CLIENT_INITIATED;
			whdr.flags	&= RXRPC_CLIENT_INITIATED;

			kernel_sendmsg(local->socket, &msg, iov, 2, size);
		}

		rxrpc_free_skb(skb);
	}

	_leave("");
}
