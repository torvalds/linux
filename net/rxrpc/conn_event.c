// SPDX-License-Identifier: GPL-2.0-or-later
/* connection-level event handling
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
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
				       struct sk_buff *skb,
				       unsigned int channel)
{
	struct rxrpc_skb_priv *sp = skb ? rxrpc_skb(skb) : NULL;
	struct rxrpc_channel *chan;
	struct msghdr msg;
	struct kvec iov[3];
	struct {
		struct rxrpc_wire_header whdr;
		union {
			__be32 abort_code;
			struct rxrpc_ackpacket ack;
		};
	} __attribute__((packed)) pkt;
	struct rxrpc_ackinfo ack_info;
	size_t len;
	int ret, ioc;
	u32 serial, mtu, call_id, padding;

	_enter("%d", conn->debug_id);

	chan = &conn->channels[channel];

	/* If the last call got moved on whilst we were waiting to run, just
	 * ignore this packet.
	 */
	call_id = READ_ONCE(chan->last_call);
	/* Sync with __rxrpc_disconnect_call() */
	smp_rmb();
	if (skb && call_id != sp->hdr.callNumber)
		return;

	msg.msg_name	= &conn->params.peer->srx.transport;
	msg.msg_namelen	= conn->params.peer->srx.transport_len;
	msg.msg_control	= NULL;
	msg.msg_controllen = 0;
	msg.msg_flags	= 0;

	iov[0].iov_base	= &pkt;
	iov[0].iov_len	= sizeof(pkt.whdr);
	iov[1].iov_base	= &padding;
	iov[1].iov_len	= 3;
	iov[2].iov_base	= &ack_info;
	iov[2].iov_len	= sizeof(ack_info);

	pkt.whdr.epoch		= htonl(conn->proto.epoch);
	pkt.whdr.cid		= htonl(conn->proto.cid | channel);
	pkt.whdr.callNumber	= htonl(call_id);
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
		pkt.abort_code	= htonl(chan->last_abort);
		iov[0].iov_len += sizeof(pkt.abort_code);
		len += sizeof(pkt.abort_code);
		ioc = 1;
		break;

	case RXRPC_PACKET_TYPE_ACK:
		mtu = conn->params.peer->if_mtu;
		mtu -= conn->params.peer->hdrsize;
		pkt.ack.bufferSpace	= 0;
		pkt.ack.maxSkew		= htons(skb ? skb->priority : 0);
		pkt.ack.firstPacket	= htonl(chan->last_seq + 1);
		pkt.ack.previousPacket	= htonl(chan->last_seq);
		pkt.ack.serial		= htonl(skb ? sp->hdr.serial : 0);
		pkt.ack.reason		= skb ? RXRPC_ACK_DUPLICATE : RXRPC_ACK_IDLE;
		pkt.ack.nAcks		= 0;
		ack_info.rxMTU		= htonl(rxrpc_rx_mtu);
		ack_info.maxMTU		= htonl(mtu);
		ack_info.rwind		= htonl(rxrpc_rx_window_size);
		ack_info.jumbo_max	= htonl(rxrpc_rx_jumbo_max);
		pkt.whdr.flags		|= RXRPC_SLOW_START_OK;
		padding			= 0;
		iov[0].iov_len += sizeof(pkt.ack);
		len += sizeof(pkt.ack) + 3 + sizeof(ack_info);
		ioc = 3;
		break;

	default:
		return;
	}

	/* Resync with __rxrpc_disconnect_call() and check that the last call
	 * didn't get advanced whilst we were filling out the packets.
	 */
	smp_rmb();
	if (READ_ONCE(chan->last_call) != call_id)
		return;

	serial = atomic_inc_return(&conn->serial);
	pkt.whdr.serial = htonl(serial);

	switch (chan->last_type) {
	case RXRPC_PACKET_TYPE_ABORT:
		_proto("Tx ABORT %%%u { %d } [re]", serial, conn->abort_code);
		break;
	case RXRPC_PACKET_TYPE_ACK:
		trace_rxrpc_tx_ack(chan->call_debug_id, serial,
				   ntohl(pkt.ack.firstPacket),
				   ntohl(pkt.ack.serial),
				   pkt.ack.reason, 0);
		_proto("Tx ACK %%%u [re]", serial);
		break;
	}

	ret = kernel_sendmsg(conn->params.local->socket, &msg, iov, ioc, len);
	conn->params.peer->last_tx_at = ktime_get_seconds();
	if (ret < 0)
		trace_rxrpc_tx_fail(chan->call_debug_id, serial, ret,
				    rxrpc_tx_point_call_final_resend);
	else
		trace_rxrpc_tx_packet(chan->call_debug_id, &pkt.whdr,
				      rxrpc_tx_point_call_final_resend);

	_leave("");
}

/*
 * pass a connection-level abort onto all calls on that connection
 */
static void rxrpc_abort_calls(struct rxrpc_connection *conn,
			      enum rxrpc_call_completion compl,
			      rxrpc_serial_t serial)
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
				trace_rxrpc_abort(call->debug_id,
						  "CON", call->cid,
						  call->call_id, 0,
						  conn->abort_code,
						  conn->error);
			else
				trace_rxrpc_rx_abort(call, serial,
						     conn->abort_code);
			rxrpc_set_call_completion(call, compl,
						  conn->abort_code,
						  conn->error);
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
	rxrpc_abort_calls(conn, RXRPC_CALL_LOCALLY_ABORTED, serial);
	whdr.serial = htonl(serial);
	_proto("Tx CONN ABORT %%%u { %d }", serial, conn->abort_code);

	ret = kernel_sendmsg(conn->params.local->socket, &msg, iov, 2, len);
	if (ret < 0) {
		trace_rxrpc_tx_fail(conn->debug_id, serial, ret,
				    rxrpc_tx_point_conn_abort);
		_debug("sendmsg failed: %d", ret);
		return -EAGAIN;
	}

	trace_rxrpc_tx_packet(conn->debug_id, &whdr, rxrpc_tx_point_conn_abort);

	conn->params.peer->last_tx_at = ktime_get_seconds();

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
			call->state = RXRPC_CALL_SERVER_RECV_REQUEST;
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
		rxrpc_conn_retransmit_call(conn, skb,
					   sp->hdr.cid & RXRPC_CHANNELMASK);
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
		rxrpc_abort_calls(conn, RXRPC_CALL_REMOTELY_ABORTED, sp->hdr.serial);
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
		spin_lock_bh(&conn->state_lock);

		if (conn->state == RXRPC_CONN_SERVICE_CHALLENGING) {
			conn->state = RXRPC_CONN_SERVICE;
			spin_unlock_bh(&conn->state_lock);
			for (loop = 0; loop < RXRPC_MAXCALLS; loop++)
				rxrpc_call_is_secure(
					rcu_dereference_protected(
						conn->channels[loop].call,
						lockdep_is_held(&conn->channel_lock)));
		} else {
			spin_unlock_bh(&conn->state_lock);
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
	ASSERT(conn->server_key);

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
 * Process delayed final ACKs that we haven't subsumed into a subsequent call.
 */
static void rxrpc_process_delayed_final_acks(struct rxrpc_connection *conn)
{
	unsigned long j = jiffies, next_j;
	unsigned int channel;
	bool set;

again:
	next_j = j + LONG_MAX;
	set = false;
	for (channel = 0; channel < RXRPC_MAXCALLS; channel++) {
		struct rxrpc_channel *chan = &conn->channels[channel];
		unsigned long ack_at;

		if (!test_bit(RXRPC_CONN_FINAL_ACK_0 + channel, &conn->flags))
			continue;

		smp_rmb(); /* vs rxrpc_disconnect_client_call */
		ack_at = READ_ONCE(chan->final_ack_at);

		if (time_before(j, ack_at)) {
			if (time_before(ack_at, next_j)) {
				next_j = ack_at;
				set = true;
			}
			continue;
		}

		if (test_and_clear_bit(RXRPC_CONN_FINAL_ACK_0 + channel,
				       &conn->flags))
			rxrpc_conn_retransmit_call(conn, NULL, channel);
	}

	j = jiffies;
	if (time_before_eq(next_j, j))
		goto again;
	if (set)
		rxrpc_reduce_conn_timer(conn, next_j);
}

/*
 * connection-level event processor
 */
static void rxrpc_do_process_connection(struct rxrpc_connection *conn)
{
	struct sk_buff *skb;
	u32 abort_code = RX_PROTOCOL_ERROR;
	int ret;

	if (test_and_clear_bit(RXRPC_CONN_EV_CHALLENGE, &conn->events))
		rxrpc_secure_connection(conn);

	/* Process delayed ACKs whose time has come. */
	if (conn->flags & RXRPC_CONN_FINAL_ACK_MASK)
		rxrpc_process_delayed_final_acks(conn);

	/* go through the conn-level event packets, releasing the ref on this
	 * connection that each one has when we've finished with it */
	while ((skb = skb_dequeue(&conn->rx_queue))) {
		rxrpc_see_skb(skb, rxrpc_skb_seen);
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
			rxrpc_free_skb(skb, rxrpc_skb_freed);
			break;
		}
	}

	return;

requeue_and_leave:
	skb_queue_head(&conn->rx_queue, skb);
	return;

protocol_error:
	if (rxrpc_abort_connection(conn, ret, abort_code) < 0)
		goto requeue_and_leave;
	rxrpc_free_skb(skb, rxrpc_skb_freed);
	return;
}

void rxrpc_process_connection(struct work_struct *work)
{
	struct rxrpc_connection *conn =
		container_of(work, struct rxrpc_connection, processor);

	rxrpc_see_connection(conn);

	if (__rxrpc_use_local(conn->params.local)) {
		rxrpc_do_process_connection(conn);
		rxrpc_unuse_local(conn->params.local);
	}

	rxrpc_put_connection(conn);
	_leave("");
	return;
}
