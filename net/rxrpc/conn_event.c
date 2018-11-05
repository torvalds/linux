/* connection-level event handling
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
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include <net/ip.h>
#include "ar-internal.h"

/*
 * Retransmit terminal ACK or ABORT of the previous call.
 */
static void rxrpc_conn_retransmit_call(struct rxrpc_connection *conn,
				       struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct rxrpc_channel *chan;
	struct msghdr msg;
	struct kvec iov;
	struct {
		struct rxrpc_wire_header whdr;
		union {
			struct {
				__be32 code;
			} abort;
			struct {
				struct rxrpc_ackpacket ack;
				u8 padding[3];
				struct rxrpc_ackinfo info;
			};
		};
	} __attribute__((packed)) pkt;
	size_t len;
	u32 serial, mtu, call_id;

	_enter("%d", conn->debug_id);

	chan = &conn->channels[sp->hdr.cid & RXRPC_CHANNELMASK];

	/* If the last call got moved on whilst we were waiting to run, just
	 * ignore this packet.
	 */
	call_id = READ_ONCE(chan->last_call);
	/* Sync with __rxrpc_disconnect_call() */
	smp_rmb();
	if (call_id != sp->hdr.callNumber)
		return;

	msg.msg_name	= &conn->params.peer->srx.transport;
	msg.msg_namelen	= conn->params.peer->srx.transport_len;
	msg.msg_control	= NULL;
	msg.msg_controllen = 0;
	msg.msg_flags	= 0;

	pkt.whdr.epoch		= htonl(sp->hdr.epoch);
	pkt.whdr.cid		= htonl(sp->hdr.cid);
	pkt.whdr.callNumber	= htonl(sp->hdr.callNumber);
	pkt.whdr.seq		= 0;
	pkt.whdr.type		= chan->last_type;
	pkt.whdr.flags		= conn->out_clientflag;
	pkt.whdr.userStatus	= 0;
	pkt.whdr.securityIndex	= conn->security_ix;
	pkt.whdr._rsvd		= 0;
	pkt.whdr.serviceId	= htons(conn->service_id);

	len = sizeof(pkt.whdr);
	switch (chan->last_type) {
	case RXRPC_PACKET_TYPE_ABORT:
		pkt.abort.code	= htonl(chan->last_abort);
		len += sizeof(pkt.abort);
		break;

	case RXRPC_PACKET_TYPE_ACK:
		mtu = conn->params.peer->if_mtu;
		mtu -= conn->params.peer->hdrsize;
		pkt.ack.bufferSpace	= 0;
		pkt.ack.maxSkew		= htons(skb->priority);
		pkt.ack.firstPacket	= htonl(chan->last_seq);
		pkt.ack.previousPacket	= htonl(chan->last_seq - 1);
		pkt.ack.serial		= htonl(sp->hdr.serial);
		pkt.ack.reason		= RXRPC_ACK_DUPLICATE;
		pkt.ack.nAcks		= 0;
		pkt.info.rxMTU		= htonl(rxrpc_rx_mtu);
		pkt.info.maxMTU		= htonl(mtu);
		pkt.info.rwind		= htonl(rxrpc_rx_window_size);
		pkt.info.jumbo_max	= htonl(rxrpc_rx_jumbo_max);
		pkt.whdr.flags		|= RXRPC_SLOW_START_OK;
		len += sizeof(pkt.ack) + sizeof(pkt.info);
		break;
	}

	/* Resync with __rxrpc_disconnect_call() and check that the last call
	 * didn't get advanced whilst we were filling out the packets.
	 */
	smp_rmb();
	if (READ_ONCE(chan->last_call) != call_id)
		return;

	iov.iov_base	= &pkt;
	iov.iov_len	= len;

	serial = atomic_inc_return(&conn->serial);
	pkt.whdr.serial = htonl(serial);

	switch (chan->last_type) {
	case RXRPC_PACKET_TYPE_ABORT:
		_proto("Tx ABORT %%%u { %d } [re]", serial, conn->abort_code);
		break;
	case RXRPC_PACKET_TYPE_ACK:
		trace_rxrpc_tx_ack(NULL, serial, chan->last_seq, 0,
				   RXRPC_ACK_DUPLICATE, 0);
		_proto("Tx ACK %%%u [re]", serial);
		break;
	}

	kernel_sendmsg(conn->params.local->socket, &msg, &iov, 1, len);
	_leave("");
	return;
}

/*
 * pass a connection-level abort onto all calls on that connection
 */
static void rxrpc_abort_calls(struct rxrpc_connection *conn,
			      enum rxrpc_call_completion compl)
{
	struct rxrpc_call *call;
	int i;

	_enter("{%d},%x", conn->debug_id, conn->abort_code);

	spin_lock(&conn->channel_lock);

	for (i = 0; i < RXRPC_MAXCALLS; i++) {
		call = rcu_dereference_protected(
			conn->channels[i].call,
			lockdep_is_held(&conn->channel_lock));
		if (call) {
			if (compl == RXRPC_CALL_LOCALLY_ABORTED)
				trace_rxrpc_abort("CON", call->cid,
						  call->call_id, 0,
						  conn->abort_code,
						  conn->error);
			if (rxrpc_set_call_completion(call, compl,
						      conn->abort_code,
						      conn->error))
				rxrpc_notify_socket(call);
		}
	}

	spin_unlock(&conn->channel_lock);
	_leave("");
}

/*
 * generate a connection-level abort
 */
static int rxrpc_abort_connection(struct rxrpc_connection *conn,
				  int error, u32 abort_code)
{
	struct rxrpc_wire_header whdr;
	struct msghdr msg;
	struct kvec iov[2];
	__be32 word;
	size_t len;
	u32 serial;
	int ret;

	_enter("%d,,%u,%u", conn->debug_id, error, abort_code);

	/* generate a connection-level abort */
	spin_lock_bh(&conn->state_lock);
	if (conn->state >= RXRPC_CONN_REMOTELY_ABORTED) {
		spin_unlock_bh(&conn->state_lock);
		_leave(" = 0 [already dead]");
		return 0;
	}

	conn->error = error;
	conn->abort_code = abort_code;
	conn->state = RXRPC_CONN_LOCALLY_ABORTED;
	spin_unlock_bh(&conn->state_lock);

	rxrpc_abort_calls(conn, RXRPC_CALL_LOCALLY_ABORTED);

	msg.msg_name	= &conn->params.peer->srx.transport;
	msg.msg_namelen	= conn->params.peer->srx.transport_len;
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
	_proto("Tx CONN ABORT %%%u { %d }", serial, conn->abort_code);

	ret = kernel_sendmsg(conn->params.local->socket, &msg, iov, 2, len);
	if (ret < 0) {
		_debug("sendmsg failed: %d", ret);
		return -EAGAIN;
	}

	_leave(" = 0");
	return 0;
}

/*
 * mark a call as being on a now-secured channel
 * - must be called with BH's disabled.
 */
static void rxrpc_call_is_secure(struct rxrpc_call *call)
{
	_enter("%p", call);
	if (call) {
		write_lock_bh(&call->state_lock);
		if (call->state == RXRPC_CALL_SERVER_SECURING) {
			call->state = RXRPC_CALL_SERVER_ACCEPTING;
			rxrpc_notify_socket(call);
		}
		write_unlock_bh(&call->state_lock);
	}
}

/*
 * connection-level Rx packet processor
 */
static int rxrpc_process_event(struct rxrpc_connection *conn,
			       struct sk_buff *skb,
			       u32 *_abort_code)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	__be32 wtmp;
	u32 abort_code;
	int loop, ret;

	if (conn->state >= RXRPC_CONN_REMOTELY_ABORTED) {
		_leave(" = -ECONNABORTED [%u]", conn->state);
		return -ECONNABORTED;
	}

	_enter("{%d},{%u,%%%u},", conn->debug_id, sp->hdr.type, sp->hdr.serial);

	switch (sp->hdr.type) {
	case RXRPC_PACKET_TYPE_DATA:
	case RXRPC_PACKET_TYPE_ACK:
		rxrpc_conn_retransmit_call(conn, skb);
		return 0;

	case RXRPC_PACKET_TYPE_BUSY:
		/* Just ignore BUSY packets for now. */
		return 0;

	case RXRPC_PACKET_TYPE_ABORT:
		if (skb_copy_bits(skb, sizeof(struct rxrpc_wire_header),
				  &wtmp, sizeof(wtmp)) < 0) {
			trace_rxrpc_rx_eproto(NULL, sp->hdr.serial,
					      tracepoint_string("bad_abort"));
			return -EPROTO;
		}
		abort_code = ntohl(wtmp);
		_proto("Rx ABORT %%%u { ac=%d }", sp->hdr.serial, abort_code);

		conn->error = -ECONNABORTED;
		conn->abort_code = abort_code;
		conn->state = RXRPC_CONN_REMOTELY_ABORTED;
		rxrpc_abort_calls(conn, RXRPC_CALL_REMOTELY_ABORTED);
		return -ECONNABORTED;

	case RXRPC_PACKET_TYPE_CHALLENGE:
		return conn->security->respond_to_challenge(conn, skb,
							    _abort_code);

	case RXRPC_PACKET_TYPE_RESPONSE:
		ret = conn->security->verify_response(conn, skb, _abort_code);
		if (ret < 0)
			return ret;

		ret = conn->security->init_connection_security(conn);
		if (ret < 0)
			return ret;

		ret = conn->security->prime_packet_security(conn);
		if (ret < 0)
			return ret;

		spin_lock(&conn->channel_lock);
		spin_lock(&conn->state_lock);

		if (conn->state == RXRPC_CONN_SERVICE_CHALLENGING) {
			conn->state = RXRPC_CONN_SERVICE;
			spin_unlock(&conn->state_lock);
			for (loop = 0; loop < RXRPC_MAXCALLS; loop++)
				rxrpc_call_is_secure(
					rcu_dereference_protected(
						conn->channels[loop].call,
						lockdep_is_held(&conn->channel_lock)));
		} else {
			spin_unlock(&conn->state_lock);
		}

		spin_unlock(&conn->channel_lock);
		return 0;

	default:
		trace_rxrpc_rx_eproto(NULL, sp->hdr.serial,
				      tracepoint_string("bad_conn_pkt"));
		return -EPROTO;
	}
}

/*
 * set up security and issue a challenge
 */
static void rxrpc_secure_connection(struct rxrpc_connection *conn)
{
	u32 abort_code;
	int ret;

	_enter("{%d}", conn->debug_id);

	ASSERT(conn->security_ix != 0);

	if (!conn->params.key) {
		_debug("set up security");
		ret = rxrpc_init_server_conn_security(conn);
		switch (ret) {
		case 0:
			break;
		case -ENOENT:
			abort_code = RX_CALL_DEAD;
			goto abort;
		default:
			abort_code = RXKADNOAUTH;
			goto abort;
		}
	}

	if (conn->security->issue_challenge(conn) < 0) {
		abort_code = RX_CALL_DEAD;
		ret = -ENOMEM;
		goto abort;
	}

	_leave("");
	return;

abort:
	_debug("abort %d, %d", ret, abort_code);
	rxrpc_abort_connection(conn, ret, abort_code);
	_leave(" [aborted]");
}

/*
 * connection-level event processor
 */
void rxrpc_process_connection(struct work_struct *work)
{
	struct rxrpc_connection *conn =
		container_of(work, struct rxrpc_connection, processor);
	struct sk_buff *skb;
	u32 abort_code = RX_PROTOCOL_ERROR;
	int ret;

	rxrpc_see_connection(conn);

	if (test_and_clear_bit(RXRPC_CONN_EV_CHALLENGE, &conn->events))
		rxrpc_secure_connection(conn);

	/* go through the conn-level event packets, releasing the ref on this
	 * connection that each one has when we've finished with it */
	while ((skb = skb_dequeue(&conn->rx_queue))) {
		rxrpc_see_skb(skb, rxrpc_skb_rx_seen);
		ret = rxrpc_process_event(conn, skb, &abort_code);
		switch (ret) {
		case -EPROTO:
		case -EKEYEXPIRED:
		case -EKEYREJECTED:
			goto protocol_error;
		case -ENOMEM:
		case -EAGAIN:
			goto requeue_and_leave;
		case -ECONNABORTED:
		default:
			rxrpc_free_skb(skb, rxrpc_skb_rx_freed);
			break;
		}
	}

out:
	rxrpc_put_connection(conn);
	_leave("");
	return;

requeue_and_leave:
	skb_queue_head(&conn->rx_queue, skb);
	goto out;

protocol_error:
	if (rxrpc_abort_connection(conn, ret, abort_code) < 0)
		goto requeue_and_leave;
	rxrpc_free_skb(skb, rxrpc_skb_rx_freed);
	goto out;
}
