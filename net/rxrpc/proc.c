// SPDX-License-Identifier: GPL-2.0-or-later
/* /proc/net/ support for AF_RXRPC
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/module.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include "ar-internal.h"

static const char *const rxrpc_conn_states[RXRPC_CONN__NR_STATES] = {
	[RXRPC_CONN_UNUSED]			= "Unused  ",
	[RXRPC_CONN_CLIENT]			= "Client  ",
	[RXRPC_CONN_SERVICE_PREALLOC]		= "SvPrealc",
	[RXRPC_CONN_SERVICE_UNSECURED]		= "SvUnsec ",
	[RXRPC_CONN_SERVICE_CHALLENGING]	= "SvChall ",
	[RXRPC_CONN_SERVICE]			= "SvSecure",
	[RXRPC_CONN_REMOTELY_ABORTED]		= "RmtAbort",
	[RXRPC_CONN_LOCALLY_ABORTED]		= "LocAbort",
};

/*
 * generate a list of extant and dead calls in /proc/net/rxrpc_calls
 */
static void *rxrpc_call_seq_start(struct seq_file *seq, loff_t *_pos)
	__acquires(rcu)
	__acquires(rxnet->call_lock)
{
	struct rxrpc_net *rxnet = rxrpc_net(seq_file_net(seq));

	rcu_read_lock();
	read_lock(&rxnet->call_lock);
	return seq_list_start_head(&rxnet->calls, *_pos);
}

static void *rxrpc_call_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct rxrpc_net *rxnet = rxrpc_net(seq_file_net(seq));

	return seq_list_next(v, &rxnet->calls, pos);
}

static void rxrpc_call_seq_stop(struct seq_file *seq, void *v)
	__releases(rxnet->call_lock)
	__releases(rcu)
{
	struct rxrpc_net *rxnet = rxrpc_net(seq_file_net(seq));

	read_unlock(&rxnet->call_lock);
	rcu_read_unlock();
}

static int rxrpc_call_seq_show(struct seq_file *seq, void *v)
{
	struct rxrpc_local *local;
	struct rxrpc_sock *rx;
	struct rxrpc_peer *peer;
	struct rxrpc_call *call;
	struct rxrpc_net *rxnet = rxrpc_net(seq_file_net(seq));
	unsigned long timeout = 0;
	rxrpc_seq_t tx_hard_ack, rx_hard_ack;
	char lbuff[50], rbuff[50];

	if (v == &rxnet->calls) {
		seq_puts(seq,
			 "Proto Local                                          "
			 " Remote                                         "
			 " SvID ConnID   CallID   End Use State    Abort   "
			 " UserID           TxSeq    TW RxSeq    RW RxSerial RxTimo\n");
		return 0;
	}

	call = list_entry(v, struct rxrpc_call, link);

	rx = rcu_dereference(call->socket);
	if (rx) {
		local = READ_ONCE(rx->local);
		if (local)
			sprintf(lbuff, "%pISpc", &local->srx.transport);
		else
			strcpy(lbuff, "no_local");
	} else {
		strcpy(lbuff, "no_socket");
	}

	peer = call->peer;
	if (peer)
		sprintf(rbuff, "%pISpc", &peer->srx.transport);
	else
		strcpy(rbuff, "no_connection");

	if (call->state != RXRPC_CALL_SERVER_PREALLOC) {
		timeout = READ_ONCE(call->expect_rx_by);
		timeout -= jiffies;
	}

	tx_hard_ack = READ_ONCE(call->tx_hard_ack);
	rx_hard_ack = READ_ONCE(call->rx_hard_ack);
	seq_printf(seq,
		   "UDP   %-47.47s %-47.47s %4x %08x %08x %s %3u"
		   " %-8.8s %08x %lx %08x %02x %08x %02x %08x %06lx\n",
		   lbuff,
		   rbuff,
		   call->service_id,
		   call->cid,
		   call->call_id,
		   rxrpc_is_service_call(call) ? "Svc" : "Clt",
		   atomic_read(&call->usage),
		   rxrpc_call_states[call->state],
		   call->abort_code,
		   call->user_call_ID,
		   tx_hard_ack, READ_ONCE(call->tx_top) - tx_hard_ack,
		   rx_hard_ack, READ_ONCE(call->rx_top) - rx_hard_ack,
		   call->rx_serial,
		   timeout);

	return 0;
}

const struct seq_operations rxrpc_call_seq_ops = {
	.start  = rxrpc_call_seq_start,
	.next   = rxrpc_call_seq_next,
	.stop   = rxrpc_call_seq_stop,
	.show   = rxrpc_call_seq_show,
};

/*
 * generate a list of extant virtual connections in /proc/net/rxrpc_conns
 */
static void *rxrpc_connection_seq_start(struct seq_file *seq, loff_t *_pos)
	__acquires(rxnet->conn_lock)
{
	struct rxrpc_net *rxnet = rxrpc_net(seq_file_net(seq));

	read_lock(&rxnet->conn_lock);
	return seq_list_start_head(&rxnet->conn_proc_list, *_pos);
}

static void *rxrpc_connection_seq_next(struct seq_file *seq, void *v,
				       loff_t *pos)
{
	struct rxrpc_net *rxnet = rxrpc_net(seq_file_net(seq));

	return seq_list_next(v, &rxnet->conn_proc_list, pos);
}

static void rxrpc_connection_seq_stop(struct seq_file *seq, void *v)
	__releases(rxnet->conn_lock)
{
	struct rxrpc_net *rxnet = rxrpc_net(seq_file_net(seq));

	read_unlock(&rxnet->conn_lock);
}

static int rxrpc_connection_seq_show(struct seq_file *seq, void *v)
{
	struct rxrpc_connection *conn;
	struct rxrpc_net *rxnet = rxrpc_net(seq_file_net(seq));
	char lbuff[50], rbuff[50];

	if (v == &rxnet->conn_proc_list) {
		seq_puts(seq,
			 "Proto Local                                          "
			 " Remote                                         "
			 " SvID ConnID   End Use State    Key     "
			 " Serial   ISerial\n"
			 );
		return 0;
	}

	conn = list_entry(v, struct rxrpc_connection, proc_link);
	if (conn->state == RXRPC_CONN_SERVICE_PREALLOC) {
		strcpy(lbuff, "no_local");
		strcpy(rbuff, "no_connection");
		goto print;
	}

	sprintf(lbuff, "%pISpc", &conn->params.local->srx.transport);

	sprintf(rbuff, "%pISpc", &conn->params.peer->srx.transport);
print:
	seq_printf(seq,
		   "UDP   %-47.47s %-47.47s %4x %08x %s %3u"
		   " %s %08x %08x %08x %08x %08x %08x %08x\n",
		   lbuff,
		   rbuff,
		   conn->service_id,
		   conn->proto.cid,
		   rxrpc_conn_is_service(conn) ? "Svc" : "Clt",
		   atomic_read(&conn->usage),
		   rxrpc_conn_states[conn->state],
		   key_serial(conn->params.key),
		   atomic_read(&conn->serial),
		   conn->hi_serial,
		   conn->channels[0].call_id,
		   conn->channels[1].call_id,
		   conn->channels[2].call_id,
		   conn->channels[3].call_id);

	return 0;
}

const struct seq_operations rxrpc_connection_seq_ops = {
	.start  = rxrpc_connection_seq_start,
	.next   = rxrpc_connection_seq_next,
	.stop   = rxrpc_connection_seq_stop,
	.show   = rxrpc_connection_seq_show,
};

/*
 * generate a list of extant virtual peers in /proc/net/rxrpc/peers
 */
static int rxrpc_peer_seq_show(struct seq_file *seq, void *v)
{
	struct rxrpc_peer *peer;
	time64_t now;
	char lbuff[50], rbuff[50];

	if (v == SEQ_START_TOKEN) {
		seq_puts(seq,
			 "Proto Local                                          "
			 " Remote                                         "
			 " Use CW  MTU   LastUse          RTT Rc\n"
			 );
		return 0;
	}

	peer = list_entry(v, struct rxrpc_peer, hash_link);

	sprintf(lbuff, "%pISpc", &peer->local->srx.transport);

	sprintf(rbuff, "%pISpc", &peer->srx.transport);

	now = ktime_get_seconds();
	seq_printf(seq,
		   "UDP   %-47.47s %-47.47s %3u"
		   " %3u %5u %6llus %12llu %2u\n",
		   lbuff,
		   rbuff,
		   atomic_read(&peer->usage),
		   peer->cong_cwnd,
		   peer->mtu,
		   now - peer->last_tx_at,
		   peer->rtt,
		   peer->rtt_cursor);

	return 0;
}

static void *rxrpc_peer_seq_start(struct seq_file *seq, loff_t *_pos)
	__acquires(rcu)
{
	struct rxrpc_net *rxnet = rxrpc_net(seq_file_net(seq));
	unsigned int bucket, n;
	unsigned int shift = 32 - HASH_BITS(rxnet->peer_hash);
	void *p;

	rcu_read_lock();

	if (*_pos >= UINT_MAX)
		return NULL;

	n = *_pos & ((1U << shift) - 1);
	bucket = *_pos >> shift;
	for (;;) {
		if (bucket >= HASH_SIZE(rxnet->peer_hash)) {
			*_pos = UINT_MAX;
			return NULL;
		}
		if (n == 0) {
			if (bucket == 0)
				return SEQ_START_TOKEN;
			*_pos += 1;
			n++;
		}

		p = seq_hlist_start_rcu(&rxnet->peer_hash[bucket], n - 1);
		if (p)
			return p;
		bucket++;
		n = 1;
		*_pos = (bucket << shift) | n;
	}
}

static void *rxrpc_peer_seq_next(struct seq_file *seq, void *v, loff_t *_pos)
{
	struct rxrpc_net *rxnet = rxrpc_net(seq_file_net(seq));
	unsigned int bucket, n;
	unsigned int shift = 32 - HASH_BITS(rxnet->peer_hash);
	void *p;

	if (*_pos >= UINT_MAX)
		return NULL;

	bucket = *_pos >> shift;

	p = seq_hlist_next_rcu(v, &rxnet->peer_hash[bucket], _pos);
	if (p)
		return p;

	for (;;) {
		bucket++;
		n = 1;
		*_pos = (bucket << shift) | n;

		if (bucket >= HASH_SIZE(rxnet->peer_hash)) {
			*_pos = UINT_MAX;
			return NULL;
		}
		if (n == 0) {
			*_pos += 1;
			n++;
		}

		p = seq_hlist_start_rcu(&rxnet->peer_hash[bucket], n - 1);
		if (p)
			return p;
	}
}

static void rxrpc_peer_seq_stop(struct seq_file *seq, void *v)
	__releases(rcu)
{
	rcu_read_unlock();
}


const struct seq_operations rxrpc_peer_seq_ops = {
	.start  = rxrpc_peer_seq_start,
	.next   = rxrpc_peer_seq_next,
	.stop   = rxrpc_peer_seq_stop,
	.show   = rxrpc_peer_seq_show,
};
