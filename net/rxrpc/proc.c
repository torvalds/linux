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
	[RXRPC_CONN_CLIENT_UNSECURED]		= "ClUnsec ",
	[RXRPC_CONN_CLIENT]			= "Client  ",
	[RXRPC_CONN_SERVICE_PREALLOC]		= "SvPrealc",
	[RXRPC_CONN_SERVICE_UNSECURED]		= "SvUnsec ",
	[RXRPC_CONN_SERVICE_CHALLENGING]	= "SvChall ",
	[RXRPC_CONN_SERVICE]			= "SvSecure",
	[RXRPC_CONN_ABORTED]			= "Aborted ",
};

/*
 * generate a list of extant and dead calls in /proc/net/rxrpc_calls
 */
static void *rxrpc_call_seq_start(struct seq_file *seq, loff_t *_pos)
	__acquires(rcu)
{
	struct rxrpc_net *rxnet = rxrpc_net(seq_file_net(seq));

	rcu_read_lock();
	return seq_list_start_head_rcu(&rxnet->calls, *_pos);
}

static void *rxrpc_call_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct rxrpc_net *rxnet = rxrpc_net(seq_file_net(seq));

	return seq_list_next_rcu(v, &rxnet->calls, pos);
}

static void rxrpc_call_seq_stop(struct seq_file *seq, void *v)
	__releases(rcu)
{
	rcu_read_unlock();
}

static int rxrpc_call_seq_show(struct seq_file *seq, void *v)
{
	struct rxrpc_local *local;
	struct rxrpc_call *call;
	struct rxrpc_net *rxnet = rxrpc_net(seq_file_net(seq));
	enum rxrpc_call_state state;
	unsigned long timeout = 0;
	rxrpc_seq_t acks_hard_ack;
	char lbuff[50], rbuff[50];

	if (v == &rxnet->calls) {
		seq_puts(seq,
			 "Proto Local                                          "
			 " Remote                                         "
			 " SvID ConnID   CallID   End Use State    Abort   "
			 " DebugId  TxSeq    TW RxSeq    RW RxSerial CW RxTimo\n");
		return 0;
	}

	call = list_entry(v, struct rxrpc_call, link);

	local = call->local;
	if (local)
		sprintf(lbuff, "%pISpc", &local->srx.transport);
	else
		strcpy(lbuff, "no_local");

	sprintf(rbuff, "%pISpc", &call->dest_srx.transport);

	state = rxrpc_call_state(call);
	if (state != RXRPC_CALL_SERVER_PREALLOC) {
		timeout = READ_ONCE(call->expect_rx_by);
		timeout -= jiffies;
	}

	acks_hard_ack = READ_ONCE(call->acks_hard_ack);
	seq_printf(seq,
		   "UDP   %-47.47s %-47.47s %4x %08x %08x %s %3u"
		   " %-8.8s %08x %08x %08x %02x %08x %02x %08x %02x %06lx\n",
		   lbuff,
		   rbuff,
		   call->dest_srx.srx_service,
		   call->cid,
		   call->call_id,
		   rxrpc_is_service_call(call) ? "Svc" : "Clt",
		   refcount_read(&call->ref),
		   rxrpc_call_states[state],
		   call->abort_code,
		   call->debug_id,
		   acks_hard_ack, READ_ONCE(call->tx_top) - acks_hard_ack,
		   call->ackr_window, call->ackr_wtop - call->ackr_window,
		   call->rx_serial,
		   call->cong_cwnd,
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
	const char *state;
	char lbuff[50], rbuff[50];

	if (v == &rxnet->conn_proc_list) {
		seq_puts(seq,
			 "Proto Local                                          "
			 " Remote                                         "
			 " SvID ConnID   End Ref Act State    Key     "
			 " Serial   ISerial  CallId0  CallId1  CallId2  CallId3\n"
			 );
		return 0;
	}

	conn = list_entry(v, struct rxrpc_connection, proc_link);
	if (conn->state == RXRPC_CONN_SERVICE_PREALLOC) {
		strcpy(lbuff, "no_local");
		strcpy(rbuff, "no_connection");
		goto print;
	}

	sprintf(lbuff, "%pISpc", &conn->local->srx.transport);
	sprintf(rbuff, "%pISpc", &conn->peer->srx.transport);
print:
	state = rxrpc_is_conn_aborted(conn) ?
		rxrpc_call_completions[conn->completion] :
		rxrpc_conn_states[conn->state];
	seq_printf(seq,
		   "UDP   %-47.47s %-47.47s %4x %08x %s %3u %3d"
		   " %s %08x %08x %08x %08x %08x %08x %08x\n",
		   lbuff,
		   rbuff,
		   conn->service_id,
		   conn->proto.cid,
		   rxrpc_conn_is_service(conn) ? "Svc" : "Clt",
		   refcount_read(&conn->ref),
		   atomic_read(&conn->active),
		   state,
		   key_serial(conn->key),
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
			 " Use SST   MTU LastUse      RTT      RTO\n"
			 );
		return 0;
	}

	peer = list_entry(v, struct rxrpc_peer, hash_link);

	sprintf(lbuff, "%pISpc", &peer->local->srx.transport);

	sprintf(rbuff, "%pISpc", &peer->srx.transport);

	now = ktime_get_seconds();
	seq_printf(seq,
		   "UDP   %-47.47s %-47.47s %3u"
		   " %3u %5u %6llus %8u %8u\n",
		   lbuff,
		   rbuff,
		   refcount_read(&peer->ref),
		   peer->cong_ssthresh,
		   peer->mtu,
		   now - peer->last_tx_at,
		   peer->srtt_us >> 3,
		   jiffies_to_usecs(peer->rto_j));

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

/*
 * Generate a list of extant virtual local endpoints in /proc/net/rxrpc/locals
 */
static int rxrpc_local_seq_show(struct seq_file *seq, void *v)
{
	struct rxrpc_local *local;
	char lbuff[50];

	if (v == SEQ_START_TOKEN) {
		seq_puts(seq,
			 "Proto Local                                          "
			 " Use Act RxQ\n");
		return 0;
	}

	local = hlist_entry(v, struct rxrpc_local, link);

	sprintf(lbuff, "%pISpc", &local->srx.transport);

	seq_printf(seq,
		   "UDP   %-47.47s %3u %3u %3u\n",
		   lbuff,
		   refcount_read(&local->ref),
		   atomic_read(&local->active_users),
		   local->rx_queue.qlen);

	return 0;
}

static void *rxrpc_local_seq_start(struct seq_file *seq, loff_t *_pos)
	__acquires(rcu)
{
	struct rxrpc_net *rxnet = rxrpc_net(seq_file_net(seq));
	unsigned int n;

	rcu_read_lock();

	if (*_pos >= UINT_MAX)
		return NULL;

	n = *_pos;
	if (n == 0)
		return SEQ_START_TOKEN;

	return seq_hlist_start_rcu(&rxnet->local_endpoints, n - 1);
}

static void *rxrpc_local_seq_next(struct seq_file *seq, void *v, loff_t *_pos)
{
	struct rxrpc_net *rxnet = rxrpc_net(seq_file_net(seq));

	if (*_pos >= UINT_MAX)
		return NULL;

	return seq_hlist_next_rcu(v, &rxnet->local_endpoints, _pos);
}

static void rxrpc_local_seq_stop(struct seq_file *seq, void *v)
	__releases(rcu)
{
	rcu_read_unlock();
}

const struct seq_operations rxrpc_local_seq_ops = {
	.start  = rxrpc_local_seq_start,
	.next   = rxrpc_local_seq_next,
	.stop   = rxrpc_local_seq_stop,
	.show   = rxrpc_local_seq_show,
};

/*
 * Display stats in /proc/net/rxrpc/stats
 */
int rxrpc_stats_show(struct seq_file *seq, void *v)
{
	struct rxrpc_net *rxnet = rxrpc_net(seq_file_single_net(seq));

	seq_printf(seq,
		   "Data     : send=%u sendf=%u fail=%u\n",
		   atomic_read(&rxnet->stat_tx_data_send),
		   atomic_read(&rxnet->stat_tx_data_send_frag),
		   atomic_read(&rxnet->stat_tx_data_send_fail));
	seq_printf(seq,
		   "Data-Tx  : nr=%u retrans=%u uf=%u cwr=%u\n",
		   atomic_read(&rxnet->stat_tx_data),
		   atomic_read(&rxnet->stat_tx_data_retrans),
		   atomic_read(&rxnet->stat_tx_data_underflow),
		   atomic_read(&rxnet->stat_tx_data_cwnd_reset));
	seq_printf(seq,
		   "Data-Rx  : nr=%u reqack=%u jumbo=%u\n",
		   atomic_read(&rxnet->stat_rx_data),
		   atomic_read(&rxnet->stat_rx_data_reqack),
		   atomic_read(&rxnet->stat_rx_data_jumbo));
	seq_printf(seq,
		   "Ack      : fill=%u send=%u skip=%u\n",
		   atomic_read(&rxnet->stat_tx_ack_fill),
		   atomic_read(&rxnet->stat_tx_ack_send),
		   atomic_read(&rxnet->stat_tx_ack_skip));
	seq_printf(seq,
		   "Ack-Tx   : req=%u dup=%u oos=%u exw=%u nos=%u png=%u prs=%u dly=%u idl=%u\n",
		   atomic_read(&rxnet->stat_tx_acks[RXRPC_ACK_REQUESTED]),
		   atomic_read(&rxnet->stat_tx_acks[RXRPC_ACK_DUPLICATE]),
		   atomic_read(&rxnet->stat_tx_acks[RXRPC_ACK_OUT_OF_SEQUENCE]),
		   atomic_read(&rxnet->stat_tx_acks[RXRPC_ACK_EXCEEDS_WINDOW]),
		   atomic_read(&rxnet->stat_tx_acks[RXRPC_ACK_NOSPACE]),
		   atomic_read(&rxnet->stat_tx_acks[RXRPC_ACK_PING]),
		   atomic_read(&rxnet->stat_tx_acks[RXRPC_ACK_PING_RESPONSE]),
		   atomic_read(&rxnet->stat_tx_acks[RXRPC_ACK_DELAY]),
		   atomic_read(&rxnet->stat_tx_acks[RXRPC_ACK_IDLE]));
	seq_printf(seq,
		   "Ack-Rx   : req=%u dup=%u oos=%u exw=%u nos=%u png=%u prs=%u dly=%u idl=%u\n",
		   atomic_read(&rxnet->stat_rx_acks[RXRPC_ACK_REQUESTED]),
		   atomic_read(&rxnet->stat_rx_acks[RXRPC_ACK_DUPLICATE]),
		   atomic_read(&rxnet->stat_rx_acks[RXRPC_ACK_OUT_OF_SEQUENCE]),
		   atomic_read(&rxnet->stat_rx_acks[RXRPC_ACK_EXCEEDS_WINDOW]),
		   atomic_read(&rxnet->stat_rx_acks[RXRPC_ACK_NOSPACE]),
		   atomic_read(&rxnet->stat_rx_acks[RXRPC_ACK_PING]),
		   atomic_read(&rxnet->stat_rx_acks[RXRPC_ACK_PING_RESPONSE]),
		   atomic_read(&rxnet->stat_rx_acks[RXRPC_ACK_DELAY]),
		   atomic_read(&rxnet->stat_rx_acks[RXRPC_ACK_IDLE]));
	seq_printf(seq,
		   "Why-Req-A: acklost=%u already=%u mrtt=%u ortt=%u\n",
		   atomic_read(&rxnet->stat_why_req_ack[rxrpc_reqack_ack_lost]),
		   atomic_read(&rxnet->stat_why_req_ack[rxrpc_reqack_already_on]),
		   atomic_read(&rxnet->stat_why_req_ack[rxrpc_reqack_more_rtt]),
		   atomic_read(&rxnet->stat_why_req_ack[rxrpc_reqack_old_rtt]));
	seq_printf(seq,
		   "Why-Req-A: nolast=%u retx=%u slows=%u smtxw=%u\n",
		   atomic_read(&rxnet->stat_why_req_ack[rxrpc_reqack_no_srv_last]),
		   atomic_read(&rxnet->stat_why_req_ack[rxrpc_reqack_retrans]),
		   atomic_read(&rxnet->stat_why_req_ack[rxrpc_reqack_slow_start]),
		   atomic_read(&rxnet->stat_why_req_ack[rxrpc_reqack_small_txwin]));
	seq_printf(seq,
		   "Buffers  : txb=%u rxb=%u\n",
		   atomic_read(&rxrpc_nr_txbuf),
		   atomic_read(&rxrpc_n_rx_skbs));
	seq_printf(seq,
		   "IO-thread: loops=%u\n",
		   atomic_read(&rxnet->stat_io_loop));
	return 0;
}

/*
 * Clear stats if /proc/net/rxrpc/stats is written to.
 */
int rxrpc_stats_clear(struct file *file, char *buf, size_t size)
{
	struct seq_file *m = file->private_data;
	struct rxrpc_net *rxnet = rxrpc_net(seq_file_single_net(m));

	if (size > 1 || (size == 1 && buf[0] != '\n'))
		return -EINVAL;

	atomic_set(&rxnet->stat_tx_data, 0);
	atomic_set(&rxnet->stat_tx_data_retrans, 0);
	atomic_set(&rxnet->stat_tx_data_underflow, 0);
	atomic_set(&rxnet->stat_tx_data_cwnd_reset, 0);
	atomic_set(&rxnet->stat_tx_data_send, 0);
	atomic_set(&rxnet->stat_tx_data_send_frag, 0);
	atomic_set(&rxnet->stat_tx_data_send_fail, 0);
	atomic_set(&rxnet->stat_rx_data, 0);
	atomic_set(&rxnet->stat_rx_data_reqack, 0);
	atomic_set(&rxnet->stat_rx_data_jumbo, 0);

	atomic_set(&rxnet->stat_tx_ack_fill, 0);
	atomic_set(&rxnet->stat_tx_ack_send, 0);
	atomic_set(&rxnet->stat_tx_ack_skip, 0);
	memset(&rxnet->stat_tx_acks, 0, sizeof(rxnet->stat_tx_acks));
	memset(&rxnet->stat_rx_acks, 0, sizeof(rxnet->stat_rx_acks));

	memset(&rxnet->stat_why_req_ack, 0, sizeof(rxnet->stat_why_req_ack));

	atomic_set(&rxnet->stat_io_loop, 0);
	return size;
}
