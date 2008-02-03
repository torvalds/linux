/* /proc/net/ support for AF_RXRPC
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include "ar-internal.h"

static const char *const rxrpc_conn_states[] = {
	[RXRPC_CONN_UNUSED]		= "Unused  ",
	[RXRPC_CONN_CLIENT]		= "Client  ",
	[RXRPC_CONN_SERVER_UNSECURED]	= "SvUnsec ",
	[RXRPC_CONN_SERVER_CHALLENGING]	= "SvChall ",
	[RXRPC_CONN_SERVER]		= "SvSecure",
	[RXRPC_CONN_REMOTELY_ABORTED]	= "RmtAbort",
	[RXRPC_CONN_LOCALLY_ABORTED]	= "LocAbort",
	[RXRPC_CONN_NETWORK_ERROR]	= "NetError",
};

/*
 * generate a list of extant and dead calls in /proc/net/rxrpc_calls
 */
static void *rxrpc_call_seq_start(struct seq_file *seq, loff_t *_pos)
{
	read_lock(&rxrpc_call_lock);
	return seq_list_start_head(&rxrpc_calls, *_pos);
}

static void *rxrpc_call_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	return seq_list_next(v, &rxrpc_calls, pos);
}

static void rxrpc_call_seq_stop(struct seq_file *seq, void *v)
{
	read_unlock(&rxrpc_call_lock);
}

static int rxrpc_call_seq_show(struct seq_file *seq, void *v)
{
	struct rxrpc_transport *trans;
	struct rxrpc_call *call;
	char lbuff[4 + 4 + 4 + 4 + 5 + 1], rbuff[4 + 4 + 4 + 4 + 5 + 1];

	if (v == &rxrpc_calls) {
		seq_puts(seq,
			 "Proto Local                  Remote                "
			 " SvID ConnID   CallID   End Use State    Abort   "
			 " UserID\n");
		return 0;
	}

	call = list_entry(v, struct rxrpc_call, link);
	trans = call->conn->trans;

	sprintf(lbuff, NIPQUAD_FMT":%u",
		NIPQUAD(trans->local->srx.transport.sin.sin_addr),
		ntohs(trans->local->srx.transport.sin.sin_port));

	sprintf(rbuff, NIPQUAD_FMT":%u",
		NIPQUAD(trans->peer->srx.transport.sin.sin_addr),
		ntohs(trans->peer->srx.transport.sin.sin_port));

	seq_printf(seq,
		   "UDP   %-22.22s %-22.22s %4x %08x %08x %s %3u"
		   " %-8.8s %08x %lx\n",
		   lbuff,
		   rbuff,
		   ntohs(call->conn->service_id),
		   ntohl(call->conn->cid),
		   ntohl(call->call_id),
		   call->conn->in_clientflag ? "Svc" : "Clt",
		   atomic_read(&call->usage),
		   rxrpc_call_states[call->state],
		   call->abort_code,
		   call->user_call_ID);

	return 0;
}

static const struct seq_operations rxrpc_call_seq_ops = {
	.start  = rxrpc_call_seq_start,
	.next   = rxrpc_call_seq_next,
	.stop   = rxrpc_call_seq_stop,
	.show   = rxrpc_call_seq_show,
};

static int rxrpc_call_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &rxrpc_call_seq_ops);
}

const struct file_operations rxrpc_call_seq_fops = {
	.owner		= THIS_MODULE,
	.open		= rxrpc_call_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_private,
};

/*
 * generate a list of extant virtual connections in /proc/net/rxrpc_conns
 */
static void *rxrpc_connection_seq_start(struct seq_file *seq, loff_t *_pos)
{
	read_lock(&rxrpc_connection_lock);
	return seq_list_start_head(&rxrpc_connections, *_pos);
}

static void *rxrpc_connection_seq_next(struct seq_file *seq, void *v,
				       loff_t *pos)
{
	return seq_list_next(v, &rxrpc_connections, pos);
}

static void rxrpc_connection_seq_stop(struct seq_file *seq, void *v)
{
	read_unlock(&rxrpc_connection_lock);
}

static int rxrpc_connection_seq_show(struct seq_file *seq, void *v)
{
	struct rxrpc_connection *conn;
	struct rxrpc_transport *trans;
	char lbuff[4 + 4 + 4 + 4 + 5 + 1], rbuff[4 + 4 + 4 + 4 + 5 + 1];

	if (v == &rxrpc_connections) {
		seq_puts(seq,
			 "Proto Local                  Remote                "
			 " SvID ConnID   Calls    End Use State    Key     "
			 " Serial   ISerial\n"
			 );
		return 0;
	}

	conn = list_entry(v, struct rxrpc_connection, link);
	trans = conn->trans;

	sprintf(lbuff, NIPQUAD_FMT":%u",
		NIPQUAD(trans->local->srx.transport.sin.sin_addr),
		ntohs(trans->local->srx.transport.sin.sin_port));

	sprintf(rbuff, NIPQUAD_FMT":%u",
		NIPQUAD(trans->peer->srx.transport.sin.sin_addr),
		ntohs(trans->peer->srx.transport.sin.sin_port));

	seq_printf(seq,
		   "UDP   %-22.22s %-22.22s %4x %08x %08x %s %3u"
		   " %s %08x %08x %08x\n",
		   lbuff,
		   rbuff,
		   ntohs(conn->service_id),
		   ntohl(conn->cid),
		   conn->call_counter,
		   conn->in_clientflag ? "Svc" : "Clt",
		   atomic_read(&conn->usage),
		   rxrpc_conn_states[conn->state],
		   key_serial(conn->key),
		   atomic_read(&conn->serial),
		   atomic_read(&conn->hi_serial));

	return 0;
}

static const struct seq_operations rxrpc_connection_seq_ops = {
	.start  = rxrpc_connection_seq_start,
	.next   = rxrpc_connection_seq_next,
	.stop   = rxrpc_connection_seq_stop,
	.show   = rxrpc_connection_seq_show,
};


static int rxrpc_connection_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &rxrpc_connection_seq_ops);
}

const struct file_operations rxrpc_connection_seq_fops = {
	.owner		= THIS_MODULE,
	.open		= rxrpc_connection_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_private,
};
