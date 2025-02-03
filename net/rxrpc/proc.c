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
	rxrpc_seq_t tx_bottom;
	char lbuff[50], rbuff[50];
	long timeout = 0;

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
	if (state != RXRPC_CALL_SERVER_PREALLOC)
		timeout = ktime_ms_delta(READ_ONCE(call->expect_rx_by), ktime_get_real());

	tx_bottom = READ_ONCE(call->tx_bottom);
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
		   tx_bottom, READ_ONCE(call->tx_top) - tx_bottom,
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
		   conn->tx_serial,
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
 * generate a list of extant virtual bundles in /proc/net/rxrpc/bundles
 */
static void *rxrpc_bundle_seq_start(struct seq_file *seq, loff_t *_pos)
	__acquires(rxnet->conn_lock)
{
	struct rxrpc_net *rxnet = rxrpc_net(seq_file_net(seq));

	read_lock(&rxnet->conn_lock);
	return seq_list_start_head(&rxnet->bundle_proc_list, *_pos);
}

static void *rxrpc_bundle_seq_next(struct seq_file *seq, void *v,
				       loff_t *pos)
{
	struct rxrpc_net *rxnet = rxrpc_net(seq_file_net(seq));

	return seq_list_next(v, &rxnet->bundle_proc_list, pos);
}

static void rxrpc_bundle_seq_stop(struct seq_file *seq, void *v)
	__releases(rxnet->conn_lock)
{
	struct rxrpc_net *rxnet = rxrpc_net(seq_file_net(seq));

	read_unlock(&rxnet->conn_lock);
}

static int rxrpc_bundle_seq_show(struct seq_file *seq, void *v)
{
	struct rxrpc_bundle *bundle;
	struct rxrpc_net *rxnet = rxrpc_net(seq_file_net(seq));
	char lbuff[50], rbuff[50];

	if (v == &rxnet->bundle_proc_list) {
		seq_puts(seq,
			 "Proto Local                                          "
			 " Remote                                         "
			 " SvID Ref Act Flg Key      |"
			 " Bundle   Conn_0   Conn_1   Conn_2   Conn_3\n"
			 );
		return 0;
	}

	bundle = list_entry(v, struct rxrpc_bundle, proc_link);

	sprintf(lbuff, "%pISpc", &bundle->local->srx.transport);
	sprintf(rbuff, "%pISpc", &bundle->peer->srx.transport);
	seq_printf(seq,
		   "UDP   %-47.47s %-47.47s %4x %3u %3d"
		   " %c%c%c %08x | %08x %08x %08x %08x %08x\n",
		   lbuff,
		   rbuff,
		   bundle->service_id,
		   refcount_read(&bundle->ref),
		   atomic_read(&bundle->active),
		   bundle->try_upgrade ? 'U' : '-',
		   bundle->exclusive ? 'e' : '-',
		   bundle->upgrade ? 'u' : '-',
		   key_serial(bundle->key),
		   bundle->debug_id,
		   bundle->conn_ids[0],
		   bundle->conn_ids[1],
		   bundle->conn_ids[2],
		   bundle->conn_ids[3]);

	return 0;
}

const struct seq_operations rxrpc_bundle_seq_ops = {
	.start  = rxrpc_bundle_seq_start,
	.next   = rxrpc_bundle_seq_next,
	.stop   = rxrpc_bundle_seq_stop,
	.show   = rxrpc_bundle_seq_show,
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
			 "Proto Local                                           Remote                                          Use SST   Maxd LastUse      RTT      RTO\n"
			 );
		return 0;
	}

	peer = list_entry(v, struct rxrpc_peer, hash_link);

	sprintf(lbuff, "%pISpc", &peer->local->srx.transport);

	sprintf(rbuff, "%pISpc", &peer->srx.transport);

	now = ktime_get_seconds();
	seq_printf(seq,
		   "UDP   %-47.47s %-47.47s %3u %4u %5u %6llus %8d %8d\n",
		   lbuff,
		   rbuff,
		   refcount_read(&peer->ref),
		   peer->cong_ssthresh,
		   peer->max_data,
		   now - peer->last_tx_at,
		   READ_ONCE(peer->recent_srtt_us),
		   READ_ONCE(peer->recent_rto_us));

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
		   "Data     : send=%u sendf=%u fail=%u emsz=%u\n",
		   atomic_read(&rxnet->stat_tx_data_send),
		   atomic_read(&rxnet->stat_tx_data_send_frag),
		   atomic_read(&rxnet->stat_tx_data_send_fail),
		   atomic_read(&rxnet->stat_tx_data_send_msgsize));
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
		   "Ack-Rx   : req=%u dup=%u oos=%u exw=%u nos=%u png=%u prs=%u dly=%u idl=%u z=%u\n",
		   atomic_read(&rxnet->stat_rx_acks[RXRPC_ACK_REQUESTED]),
		   atomic_read(&rxnet->stat_rx_acks[RXRPC_ACK_DUPLICATE]),
		   atomic_read(&rxnet->stat_rx_acks[RXRPC_ACK_OUT_OF_SEQUENCE]),
		   atomic_read(&rxnet->stat_rx_acks[RXRPC_ACK_EXCEEDS_WINDOW]),
		   atomic_read(&rxnet->stat_rx_acks[RXRPC_ACK_NOSPACE]),
		   atomic_read(&rxnet->stat_rx_acks[RXRPC_ACK_PING]),
		   atomic_read(&rxnet->stat_rx_acks[RXRPC_ACK_PING_RESPONSE]),
		   atomic_read(&rxnet->stat_rx_acks[RXRPC_ACK_DELAY]),
		   atomic_read(&rxnet->stat_rx_acks[RXRPC_ACK_IDLE]),
		   atomic_read(&rxnet->stat_rx_acks[0]));
	seq_printf(seq,
		   "Why-Req-A: acklost=%u mrtt=%u ortt=%u stall=%u\n",
		   atomic_read(&rxnet->stat_why_req_ack[rxrpc_reqack_ack_lost]),
		   atomic_read(&rxnet->stat_why_req_ack[rxrpc_reqack_more_rtt]),
		   atomic_read(&rxnet->stat_why_req_ack[rxrpc_reqack_old_rtt]),
		   atomic_read(&rxnet->stat_why_req_ack[rxrpc_reqack_app_stall]));
	seq_printf(seq,
		   "Why-Req-A: nolast=%u retx=%u slows=%u smtxw=%u\n",
		   atomic_read(&rxnet->stat_why_req_ack[rxrpc_reqack_no_srv_last]),
		   atomic_read(&rxnet->stat_why_req_ack[rxrpc_reqack_retrans]),
		   atomic_read(&rxnet->stat_why_req_ack[rxrpc_reqack_slow_start]),
		   atomic_read(&rxnet->stat_why_req_ack[rxrpc_reqack_small_txwin]));
	seq_printf(seq,
		   "Jumbo-Tx : %u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
		   atomic_read(&rxnet->stat_tx_jumbo[0]),
		   atomic_read(&rxnet->stat_tx_jumbo[1]),
		   atomic_read(&rxnet->stat_tx_jumbo[2]),
		   atomic_read(&rxnet->stat_tx_jumbo[3]),
		   atomic_read(&rxnet->stat_tx_jumbo[4]),
		   atomic_read(&rxnet->stat_tx_jumbo[5]),
		   atomic_read(&rxnet->stat_tx_jumbo[6]),
		   atomic_read(&rxnet->stat_tx_jumbo[7]),
		   atomic_read(&rxnet->stat_tx_jumbo[8]),
		   atomic_read(&rxnet->stat_tx_jumbo[9]));
	seq_printf(seq,
		   "Jumbo-Rx : %u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
		   atomic_read(&rxnet->stat_rx_jumbo[0]),
		   atomic_read(&rxnet->stat_rx_jumbo[1]),
		   atomic_read(&rxnet->stat_rx_jumbo[2]),
		   atomic_read(&rxnet->stat_rx_jumbo[3]),
		   atomic_read(&rxnet->stat_rx_jumbo[4]),
		   atomic_read(&rxnet->stat_rx_jumbo[5]),
		   atomic_read(&rxnet->stat_rx_jumbo[6]),
		   atomic_read(&rxnet->stat_rx_jumbo[7]),
		   atomic_read(&rxnet->stat_rx_jumbo[8]),
		   atomic_read(&rxnet->stat_rx_jumbo[9]));
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
	memset(&rxnet->stat_tx_jumbo, 0, sizeof(rxnet->stat_tx_jumbo));
	memset(&rxnet->stat_rx_jumbo, 0, sizeof(rxnet->stat_rx_jumbo));

	memset(&rxnet->stat_why_req_ack, 0, sizeof(rxnet->stat_why_req_ack));

	atomic_set(&rxnet->stat_io_loop, 0);
	return size;
}
