/* proc.c: /proc interface for RxRPC
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <rxrpc/rxrpc.h>
#include <rxrpc/transport.h>
#include <rxrpc/peer.h>
#include <rxrpc/connection.h>
#include <rxrpc/call.h>
#include <rxrpc/message.h>
#include "internal.h"

static struct proc_dir_entry *proc_rxrpc;

static int rxrpc_proc_transports_open(struct inode *inode, struct file *file);
static void *rxrpc_proc_transports_start(struct seq_file *p, loff_t *pos);
static void *rxrpc_proc_transports_next(struct seq_file *p, void *v, loff_t *pos);
static void rxrpc_proc_transports_stop(struct seq_file *p, void *v);
static int rxrpc_proc_transports_show(struct seq_file *m, void *v);

static struct seq_operations rxrpc_proc_transports_ops = {
	.start	= rxrpc_proc_transports_start,
	.next	= rxrpc_proc_transports_next,
	.stop	= rxrpc_proc_transports_stop,
	.show	= rxrpc_proc_transports_show,
};

static struct file_operations rxrpc_proc_transports_fops = {
	.open		= rxrpc_proc_transports_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int rxrpc_proc_peers_open(struct inode *inode, struct file *file);
static void *rxrpc_proc_peers_start(struct seq_file *p, loff_t *pos);
static void *rxrpc_proc_peers_next(struct seq_file *p, void *v, loff_t *pos);
static void rxrpc_proc_peers_stop(struct seq_file *p, void *v);
static int rxrpc_proc_peers_show(struct seq_file *m, void *v);

static struct seq_operations rxrpc_proc_peers_ops = {
	.start	= rxrpc_proc_peers_start,
	.next	= rxrpc_proc_peers_next,
	.stop	= rxrpc_proc_peers_stop,
	.show	= rxrpc_proc_peers_show,
};

static struct file_operations rxrpc_proc_peers_fops = {
	.open		= rxrpc_proc_peers_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int rxrpc_proc_conns_open(struct inode *inode, struct file *file);
static void *rxrpc_proc_conns_start(struct seq_file *p, loff_t *pos);
static void *rxrpc_proc_conns_next(struct seq_file *p, void *v, loff_t *pos);
static void rxrpc_proc_conns_stop(struct seq_file *p, void *v);
static int rxrpc_proc_conns_show(struct seq_file *m, void *v);

static struct seq_operations rxrpc_proc_conns_ops = {
	.start	= rxrpc_proc_conns_start,
	.next	= rxrpc_proc_conns_next,
	.stop	= rxrpc_proc_conns_stop,
	.show	= rxrpc_proc_conns_show,
};

static struct file_operations rxrpc_proc_conns_fops = {
	.open		= rxrpc_proc_conns_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int rxrpc_proc_calls_open(struct inode *inode, struct file *file);
static void *rxrpc_proc_calls_start(struct seq_file *p, loff_t *pos);
static void *rxrpc_proc_calls_next(struct seq_file *p, void *v, loff_t *pos);
static void rxrpc_proc_calls_stop(struct seq_file *p, void *v);
static int rxrpc_proc_calls_show(struct seq_file *m, void *v);

static struct seq_operations rxrpc_proc_calls_ops = {
	.start	= rxrpc_proc_calls_start,
	.next	= rxrpc_proc_calls_next,
	.stop	= rxrpc_proc_calls_stop,
	.show	= rxrpc_proc_calls_show,
};

static struct file_operations rxrpc_proc_calls_fops = {
	.open		= rxrpc_proc_calls_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static const char *rxrpc_call_states7[] = {
	"complet",
	"error  ",
	"rcv_op ",
	"rcv_arg",
	"got_arg",
	"snd_rpl",
	"fin_ack",
	"snd_arg",
	"rcv_rpl",
	"got_rpl"
};

static const char *rxrpc_call_error_states7[] = {
	"no_err ",
	"loc_abt",
	"rmt_abt",
	"loc_err",
	"rmt_err"
};

/*****************************************************************************/
/*
 * initialise the /proc/net/rxrpc/ directory
 */
int rxrpc_proc_init(void)
{
	struct proc_dir_entry *p;

	proc_rxrpc = proc_mkdir("rxrpc", proc_net);
	if (!proc_rxrpc)
		goto error;
	proc_rxrpc->owner = THIS_MODULE;

	p = create_proc_entry("calls", 0, proc_rxrpc);
	if (!p)
		goto error_proc;
	p->proc_fops = &rxrpc_proc_calls_fops;
	p->owner = THIS_MODULE;

	p = create_proc_entry("connections", 0, proc_rxrpc);
	if (!p)
		goto error_calls;
	p->proc_fops = &rxrpc_proc_conns_fops;
	p->owner = THIS_MODULE;

	p = create_proc_entry("peers", 0, proc_rxrpc);
	if (!p)
		goto error_calls;
	p->proc_fops = &rxrpc_proc_peers_fops;
	p->owner = THIS_MODULE;

	p = create_proc_entry("transports", 0, proc_rxrpc);
	if (!p)
		goto error_conns;
	p->proc_fops = &rxrpc_proc_transports_fops;
	p->owner = THIS_MODULE;

	return 0;

 error_conns:
	remove_proc_entry("connections", proc_rxrpc);
 error_calls:
	remove_proc_entry("calls", proc_rxrpc);
 error_proc:
	remove_proc_entry("rxrpc", proc_net);
 error:
	return -ENOMEM;
} /* end rxrpc_proc_init() */

/*****************************************************************************/
/*
 * clean up the /proc/net/rxrpc/ directory
 */
void rxrpc_proc_cleanup(void)
{
	remove_proc_entry("transports", proc_rxrpc);
	remove_proc_entry("peers", proc_rxrpc);
	remove_proc_entry("connections", proc_rxrpc);
	remove_proc_entry("calls", proc_rxrpc);

	remove_proc_entry("rxrpc", proc_net);

} /* end rxrpc_proc_cleanup() */

/*****************************************************************************/
/*
 * open "/proc/net/rxrpc/transports" which provides a summary of extant transports
 */
static int rxrpc_proc_transports_open(struct inode *inode, struct file *file)
{
	struct seq_file *m;
	int ret;

	ret = seq_open(file, &rxrpc_proc_transports_ops);
	if (ret < 0)
		return ret;

	m = file->private_data;
	m->private = PDE(inode)->data;

	return 0;
} /* end rxrpc_proc_transports_open() */

/*****************************************************************************/
/*
 * set up the iterator to start reading from the transports list and return the first item
 */
static void *rxrpc_proc_transports_start(struct seq_file *m, loff_t *_pos)
{
	struct list_head *_p;
	loff_t pos = *_pos;

	/* lock the list against modification */
	down_read(&rxrpc_proc_transports_sem);

	/* allow for the header line */
	if (!pos)
		return SEQ_START_TOKEN;
	pos--;

	/* find the n'th element in the list */
	list_for_each(_p, &rxrpc_proc_transports)
		if (!pos--)
			break;

	return _p != &rxrpc_proc_transports ? _p : NULL;
} /* end rxrpc_proc_transports_start() */

/*****************************************************************************/
/*
 * move to next call in transports list
 */
static void *rxrpc_proc_transports_next(struct seq_file *p, void *v, loff_t *pos)
{
	struct list_head *_p;

	(*pos)++;

	_p = v;
	_p = (v == SEQ_START_TOKEN) ? rxrpc_proc_transports.next : _p->next;

	return _p != &rxrpc_proc_transports ? _p : NULL;
} /* end rxrpc_proc_transports_next() */

/*****************************************************************************/
/*
 * clean up after reading from the transports list
 */
static void rxrpc_proc_transports_stop(struct seq_file *p, void *v)
{
	up_read(&rxrpc_proc_transports_sem);

} /* end rxrpc_proc_transports_stop() */

/*****************************************************************************/
/*
 * display a header line followed by a load of call lines
 */
static int rxrpc_proc_transports_show(struct seq_file *m, void *v)
{
	struct rxrpc_transport *trans =
		list_entry(v, struct rxrpc_transport, proc_link);

	/* display header on line 1 */
	if (v == SEQ_START_TOKEN) {
		seq_puts(m, "LOCAL USE\n");
		return 0;
	}

	/* display one transport per line on subsequent lines */
	seq_printf(m, "%5hu %3d\n",
		   trans->port,
		   atomic_read(&trans->usage)
		   );

	return 0;
} /* end rxrpc_proc_transports_show() */

/*****************************************************************************/
/*
 * open "/proc/net/rxrpc/peers" which provides a summary of extant peers
 */
static int rxrpc_proc_peers_open(struct inode *inode, struct file *file)
{
	struct seq_file *m;
	int ret;

	ret = seq_open(file, &rxrpc_proc_peers_ops);
	if (ret < 0)
		return ret;

	m = file->private_data;
	m->private = PDE(inode)->data;

	return 0;
} /* end rxrpc_proc_peers_open() */

/*****************************************************************************/
/*
 * set up the iterator to start reading from the peers list and return the
 * first item
 */
static void *rxrpc_proc_peers_start(struct seq_file *m, loff_t *_pos)
{
	struct list_head *_p;
	loff_t pos = *_pos;

	/* lock the list against modification */
	down_read(&rxrpc_peers_sem);

	/* allow for the header line */
	if (!pos)
		return SEQ_START_TOKEN;
	pos--;

	/* find the n'th element in the list */
	list_for_each(_p, &rxrpc_peers)
		if (!pos--)
			break;

	return _p != &rxrpc_peers ? _p : NULL;
} /* end rxrpc_proc_peers_start() */

/*****************************************************************************/
/*
 * move to next conn in peers list
 */
static void *rxrpc_proc_peers_next(struct seq_file *p, void *v, loff_t *pos)
{
	struct list_head *_p;

	(*pos)++;

	_p = v;
	_p = (v == SEQ_START_TOKEN) ? rxrpc_peers.next : _p->next;

	return _p != &rxrpc_peers ? _p : NULL;
} /* end rxrpc_proc_peers_next() */

/*****************************************************************************/
/*
 * clean up after reading from the peers list
 */
static void rxrpc_proc_peers_stop(struct seq_file *p, void *v)
{
	up_read(&rxrpc_peers_sem);

} /* end rxrpc_proc_peers_stop() */

/*****************************************************************************/
/*
 * display a header line followed by a load of conn lines
 */
static int rxrpc_proc_peers_show(struct seq_file *m, void *v)
{
	struct rxrpc_peer *peer = list_entry(v, struct rxrpc_peer, proc_link);
	long timeout;

	/* display header on line 1 */
	if (v == SEQ_START_TOKEN) {
		seq_puts(m, "LOCAL REMOTE   USAGE CONNS  TIMEOUT"
			 "   MTU RTT(uS)\n");
		return 0;
	}

	/* display one peer per line on subsequent lines */
	timeout = 0;
	if (!list_empty(&peer->timeout.link))
		timeout = (long) peer->timeout.timo_jif -
			(long) jiffies;

	seq_printf(m, "%5hu %08x %5d %5d %8ld %5Zu %7lu\n",
		   peer->trans->port,
		   ntohl(peer->addr.s_addr),
		   atomic_read(&peer->usage),
		   atomic_read(&peer->conn_count),
		   timeout,
		   peer->if_mtu,
		   (long) peer->rtt
		   );

	return 0;
} /* end rxrpc_proc_peers_show() */

/*****************************************************************************/
/*
 * open "/proc/net/rxrpc/connections" which provides a summary of extant
 * connections
 */
static int rxrpc_proc_conns_open(struct inode *inode, struct file *file)
{
	struct seq_file *m;
	int ret;

	ret = seq_open(file, &rxrpc_proc_conns_ops);
	if (ret < 0)
		return ret;

	m = file->private_data;
	m->private = PDE(inode)->data;

	return 0;
} /* end rxrpc_proc_conns_open() */

/*****************************************************************************/
/*
 * set up the iterator to start reading from the conns list and return the
 * first item
 */
static void *rxrpc_proc_conns_start(struct seq_file *m, loff_t *_pos)
{
	struct list_head *_p;
	loff_t pos = *_pos;

	/* lock the list against modification */
	down_read(&rxrpc_conns_sem);

	/* allow for the header line */
	if (!pos)
		return SEQ_START_TOKEN;
	pos--;

	/* find the n'th element in the list */
	list_for_each(_p, &rxrpc_conns)
		if (!pos--)
			break;

	return _p != &rxrpc_conns ? _p : NULL;
} /* end rxrpc_proc_conns_start() */

/*****************************************************************************/
/*
 * move to next conn in conns list
 */
static void *rxrpc_proc_conns_next(struct seq_file *p, void *v, loff_t *pos)
{
	struct list_head *_p;

	(*pos)++;

	_p = v;
	_p = (v == SEQ_START_TOKEN) ? rxrpc_conns.next : _p->next;

	return _p != &rxrpc_conns ? _p : NULL;
} /* end rxrpc_proc_conns_next() */

/*****************************************************************************/
/*
 * clean up after reading from the conns list
 */
static void rxrpc_proc_conns_stop(struct seq_file *p, void *v)
{
	up_read(&rxrpc_conns_sem);

} /* end rxrpc_proc_conns_stop() */

/*****************************************************************************/
/*
 * display a header line followed by a load of conn lines
 */
static int rxrpc_proc_conns_show(struct seq_file *m, void *v)
{
	struct rxrpc_connection *conn;
	long timeout;

	conn = list_entry(v, struct rxrpc_connection, proc_link);

	/* display header on line 1 */
	if (v == SEQ_START_TOKEN) {
		seq_puts(m,
			 "LOCAL REMOTE   RPORT SRVC CONN     END SERIALNO "
			 "CALLNO     MTU  TIMEOUT"
			 "\n");
		return 0;
	}

	/* display one conn per line on subsequent lines */
	timeout = 0;
	if (!list_empty(&conn->timeout.link))
		timeout = (long) conn->timeout.timo_jif -
			(long) jiffies;

	seq_printf(m,
		   "%5hu %08x %5hu %04hx %08x %-3.3s %08x %08x %5Zu %8ld\n",
		   conn->trans->port,
		   ntohl(conn->addr.sin_addr.s_addr),
		   ntohs(conn->addr.sin_port),
		   ntohs(conn->service_id),
		   ntohl(conn->conn_id),
		   conn->out_clientflag ? "CLT" : "SRV",
		   conn->serial_counter,
		   conn->call_counter,
		   conn->mtu_size,
		   timeout
		   );

	return 0;
} /* end rxrpc_proc_conns_show() */

/*****************************************************************************/
/*
 * open "/proc/net/rxrpc/calls" which provides a summary of extant calls
 */
static int rxrpc_proc_calls_open(struct inode *inode, struct file *file)
{
	struct seq_file *m;
	int ret;

	ret = seq_open(file, &rxrpc_proc_calls_ops);
	if (ret < 0)
		return ret;

	m = file->private_data;
	m->private = PDE(inode)->data;

	return 0;
} /* end rxrpc_proc_calls_open() */

/*****************************************************************************/
/*
 * set up the iterator to start reading from the calls list and return the
 * first item
 */
static void *rxrpc_proc_calls_start(struct seq_file *m, loff_t *_pos)
{
	struct list_head *_p;
	loff_t pos = *_pos;

	/* lock the list against modification */
	down_read(&rxrpc_calls_sem);

	/* allow for the header line */
	if (!pos)
		return SEQ_START_TOKEN;
	pos--;

	/* find the n'th element in the list */
	list_for_each(_p, &rxrpc_calls)
		if (!pos--)
			break;

	return _p != &rxrpc_calls ? _p : NULL;
} /* end rxrpc_proc_calls_start() */

/*****************************************************************************/
/*
 * move to next call in calls list
 */
static void *rxrpc_proc_calls_next(struct seq_file *p, void *v, loff_t *pos)
{
	struct list_head *_p;

	(*pos)++;

	_p = v;
	_p = (v == SEQ_START_TOKEN) ? rxrpc_calls.next : _p->next;

	return _p != &rxrpc_calls ? _p : NULL;
} /* end rxrpc_proc_calls_next() */

/*****************************************************************************/
/*
 * clean up after reading from the calls list
 */
static void rxrpc_proc_calls_stop(struct seq_file *p, void *v)
{
	up_read(&rxrpc_calls_sem);

} /* end rxrpc_proc_calls_stop() */

/*****************************************************************************/
/*
 * display a header line followed by a load of call lines
 */
static int rxrpc_proc_calls_show(struct seq_file *m, void *v)
{
	struct rxrpc_call *call = list_entry(v, struct rxrpc_call, call_link);

	/* display header on line 1 */
	if (v == SEQ_START_TOKEN) {
		seq_puts(m,
			 "LOCAL REMOT SRVC CONN     CALL     DIR USE "
			 " L STATE   OPCODE ABORT    ERRNO\n"
			 );
		return 0;
	}

	/* display one call per line on subsequent lines */
	seq_printf(m,
		   "%5hu %5hu %04hx %08x %08x %s %3u%c"
		   " %c %-7.7s %6d %08x %5d\n",
		   call->conn->trans->port,
		   ntohs(call->conn->addr.sin_port),
		   ntohs(call->conn->service_id),
		   ntohl(call->conn->conn_id),
		   ntohl(call->call_id),
		   call->conn->service ? "SVC" : "CLT",
		   atomic_read(&call->usage),
		   waitqueue_active(&call->waitq) ? 'w' : ' ',
		   call->app_last_rcv ? 'Y' : '-',
		   (call->app_call_state!=RXRPC_CSTATE_ERROR ?
		    rxrpc_call_states7[call->app_call_state] :
		    rxrpc_call_error_states7[call->app_err_state]),
		   call->app_opcode,
		   call->app_abort_code,
		   call->app_errno
		   );

	return 0;
} /* end rxrpc_proc_calls_show() */
