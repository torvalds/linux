/* krxsecd.c: Rx security daemon
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * This daemon deals with:
 * - consulting the application as to whether inbound peers and calls should be authorised
 * - generating security challenges for inbound connections
 * - responding to security challenges on outbound connections
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/completion.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <rxrpc/krxsecd.h>
#include <rxrpc/transport.h>
#include <rxrpc/connection.h>
#include <rxrpc/message.h>
#include <rxrpc/peer.h>
#include <rxrpc/call.h>
#include <linux/udp.h>
#include <linux/ip.h>
#include <linux/freezer.h>
#include <net/sock.h>
#include "internal.h"

static DECLARE_WAIT_QUEUE_HEAD(rxrpc_krxsecd_sleepq);
static DECLARE_COMPLETION(rxrpc_krxsecd_dead);
static volatile int rxrpc_krxsecd_die;

static atomic_t rxrpc_krxsecd_qcount;

/* queue of unprocessed inbound messages with seqno #1 and
 * RXRPC_CLIENT_INITIATED flag set */
static LIST_HEAD(rxrpc_krxsecd_initmsgq);
static DEFINE_SPINLOCK(rxrpc_krxsecd_initmsgq_lock);

static void rxrpc_krxsecd_process_incoming_call(struct rxrpc_message *msg);

/*****************************************************************************/
/*
 * Rx security daemon
 */
static int rxrpc_krxsecd(void *arg)
{
	DECLARE_WAITQUEUE(krxsecd, current);

	int die;

	printk("Started krxsecd %d\n", current->pid);

	daemonize("krxsecd");

	/* loop around waiting for work to do */
	do {
		/* wait for work or to be told to exit */
		_debug("### Begin Wait");
		if (!atomic_read(&rxrpc_krxsecd_qcount)) {
			set_current_state(TASK_INTERRUPTIBLE);

			add_wait_queue(&rxrpc_krxsecd_sleepq, &krxsecd);

			for (;;) {
				set_current_state(TASK_INTERRUPTIBLE);
				if (atomic_read(&rxrpc_krxsecd_qcount) ||
				    rxrpc_krxsecd_die ||
				    signal_pending(current))
					break;

				schedule();
			}

			remove_wait_queue(&rxrpc_krxsecd_sleepq, &krxsecd);
			set_current_state(TASK_RUNNING);
		}
		die = rxrpc_krxsecd_die;
		_debug("### End Wait");

		/* see if there're incoming calls in need of authenticating */
		_debug("### Begin Inbound Calls");

		if (!list_empty(&rxrpc_krxsecd_initmsgq)) {
			struct rxrpc_message *msg = NULL;

			spin_lock(&rxrpc_krxsecd_initmsgq_lock);

			if (!list_empty(&rxrpc_krxsecd_initmsgq)) {
				msg = list_entry(rxrpc_krxsecd_initmsgq.next,
						 struct rxrpc_message, link);
				list_del_init(&msg->link);
				atomic_dec(&rxrpc_krxsecd_qcount);
			}

			spin_unlock(&rxrpc_krxsecd_initmsgq_lock);

			if (msg) {
				rxrpc_krxsecd_process_incoming_call(msg);
				rxrpc_put_message(msg);
			}
		}

		_debug("### End Inbound Calls");

		try_to_freeze();

		/* discard pending signals */
		rxrpc_discard_my_signals();

	} while (!die);

	/* and that's all */
	complete_and_exit(&rxrpc_krxsecd_dead, 0);

} /* end rxrpc_krxsecd() */

/*****************************************************************************/
/*
 * start up a krxsecd daemon
 */
int __init rxrpc_krxsecd_init(void)
{
	return kernel_thread(rxrpc_krxsecd, NULL, 0);

} /* end rxrpc_krxsecd_init() */

/*****************************************************************************/
/*
 * kill the krxsecd daemon and wait for it to complete
 */
void rxrpc_krxsecd_kill(void)
{
	rxrpc_krxsecd_die = 1;
	wake_up_all(&rxrpc_krxsecd_sleepq);
	wait_for_completion(&rxrpc_krxsecd_dead);

} /* end rxrpc_krxsecd_kill() */

/*****************************************************************************/
/*
 * clear all pending incoming calls for the specified transport
 */
void rxrpc_krxsecd_clear_transport(struct rxrpc_transport *trans)
{
	LIST_HEAD(tmp);

	struct rxrpc_message *msg;
	struct list_head *_p, *_n;

	_enter("%p",trans);

	/* move all the messages for this transport onto a temp list */
	spin_lock(&rxrpc_krxsecd_initmsgq_lock);

	list_for_each_safe(_p, _n, &rxrpc_krxsecd_initmsgq) {
		msg = list_entry(_p, struct rxrpc_message, link);
		if (msg->trans == trans) {
			list_move_tail(&msg->link, &tmp);
			atomic_dec(&rxrpc_krxsecd_qcount);
		}
	}

	spin_unlock(&rxrpc_krxsecd_initmsgq_lock);

	/* zap all messages on the temp list */
	while (!list_empty(&tmp)) {
		msg = list_entry(tmp.next, struct rxrpc_message, link);
		list_del_init(&msg->link);
		rxrpc_put_message(msg);
	}

	_leave("");
} /* end rxrpc_krxsecd_clear_transport() */

/*****************************************************************************/
/*
 * queue a message on the incoming calls list
 */
void rxrpc_krxsecd_queue_incoming_call(struct rxrpc_message *msg)
{
	_enter("%p", msg);

	/* queue for processing by krxsecd */
	spin_lock(&rxrpc_krxsecd_initmsgq_lock);

	if (!rxrpc_krxsecd_die) {
		rxrpc_get_message(msg);
		list_add_tail(&msg->link, &rxrpc_krxsecd_initmsgq);
		atomic_inc(&rxrpc_krxsecd_qcount);
	}

	spin_unlock(&rxrpc_krxsecd_initmsgq_lock);

	wake_up(&rxrpc_krxsecd_sleepq);

	_leave("");
} /* end rxrpc_krxsecd_queue_incoming_call() */

/*****************************************************************************/
/*
 * process the initial message of an incoming call
 */
void rxrpc_krxsecd_process_incoming_call(struct rxrpc_message *msg)
{
	struct rxrpc_transport *trans = msg->trans;
	struct rxrpc_service *srv;
	struct rxrpc_call *call;
	struct list_head *_p;
	unsigned short sid;
	int ret;

	_enter("%p{tr=%p}", msg, trans);

	ret = rxrpc_incoming_call(msg->conn, msg, &call);
	if (ret < 0)
		goto out;

	/* find the matching service on the transport */
	sid = ntohs(msg->hdr.serviceId);
	srv = NULL;

	spin_lock(&trans->lock);
	list_for_each(_p, &trans->services) {
		srv = list_entry(_p, struct rxrpc_service, link);
		if (srv->service_id == sid && try_module_get(srv->owner)) {
			/* found a match (made sure it won't vanish) */
			_debug("found service '%s'", srv->name);
			call->owner = srv->owner;
			break;
		}
	}
	spin_unlock(&trans->lock);

	/* report the new connection
	 * - the func must inc the call's usage count to keep it
	 */
	ret = -ENOENT;
	if (_p != &trans->services) {
		/* attempt to accept the call */
		call->conn->service = srv;
		call->app_attn_func = srv->attn_func;
		call->app_error_func = srv->error_func;
		call->app_aemap_func = srv->aemap_func;

		ret = srv->new_call(call);

		/* send an abort if an error occurred */
		if (ret < 0) {
			rxrpc_call_abort(call, ret);
		}
		else {
			/* formally receive and ACK the new packet */
			ret = rxrpc_conn_receive_call_packet(call->conn,
							     call, msg);
		}
	}

	rxrpc_put_call(call);
 out:
	if (ret < 0)
		rxrpc_trans_immediate_abort(trans, msg, ret);

	_leave(" (%d)", ret);
} /* end rxrpc_krxsecd_process_incoming_call() */
