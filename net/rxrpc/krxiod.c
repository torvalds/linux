/* krxiod.c: Rx I/O daemon
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
#include <linux/completion.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/freezer.h>
#include <rxrpc/krxiod.h>
#include <rxrpc/transport.h>
#include <rxrpc/peer.h>
#include <rxrpc/call.h>
#include "internal.h"

static DECLARE_WAIT_QUEUE_HEAD(rxrpc_krxiod_sleepq);
static DECLARE_COMPLETION(rxrpc_krxiod_dead);

static atomic_t rxrpc_krxiod_qcount = ATOMIC_INIT(0);

static LIST_HEAD(rxrpc_krxiod_transportq);
static DEFINE_SPINLOCK(rxrpc_krxiod_transportq_lock);

static LIST_HEAD(rxrpc_krxiod_callq);
static DEFINE_SPINLOCK(rxrpc_krxiod_callq_lock);

static volatile int rxrpc_krxiod_die;

/*****************************************************************************/
/*
 * Rx I/O daemon
 */
static int rxrpc_krxiod(void *arg)
{
	DECLARE_WAITQUEUE(krxiod,current);

	printk("Started krxiod %d\n",current->pid);

	daemonize("krxiod");

	/* loop around waiting for work to do */
	do {
		/* wait for work or to be told to exit */
		_debug("### Begin Wait");
		if (!atomic_read(&rxrpc_krxiod_qcount)) {
			set_current_state(TASK_INTERRUPTIBLE);

			add_wait_queue(&rxrpc_krxiod_sleepq, &krxiod);

			for (;;) {
				set_current_state(TASK_INTERRUPTIBLE);
				if (atomic_read(&rxrpc_krxiod_qcount) ||
				    rxrpc_krxiod_die ||
				    signal_pending(current))
					break;

				schedule();
			}

			remove_wait_queue(&rxrpc_krxiod_sleepq, &krxiod);
			set_current_state(TASK_RUNNING);
		}
		_debug("### End Wait");

		/* do work if been given some to do */
		_debug("### Begin Work");

		/* see if there's a transport in need of attention */
		if (!list_empty(&rxrpc_krxiod_transportq)) {
			struct rxrpc_transport *trans = NULL;

			spin_lock_irq(&rxrpc_krxiod_transportq_lock);

			if (!list_empty(&rxrpc_krxiod_transportq)) {
				trans = list_entry(
					rxrpc_krxiod_transportq.next,
					struct rxrpc_transport,
					krxiodq_link);

				list_del_init(&trans->krxiodq_link);
				atomic_dec(&rxrpc_krxiod_qcount);

				/* make sure it hasn't gone away and doesn't go
				 * away */
				if (atomic_read(&trans->usage)>0)
					rxrpc_get_transport(trans);
				else
					trans = NULL;
			}

			spin_unlock_irq(&rxrpc_krxiod_transportq_lock);

			if (trans) {
				rxrpc_trans_receive_packet(trans);
				rxrpc_put_transport(trans);
			}
		}

		/* see if there's a call in need of attention */
		if (!list_empty(&rxrpc_krxiod_callq)) {
			struct rxrpc_call *call = NULL;

			spin_lock_irq(&rxrpc_krxiod_callq_lock);

			if (!list_empty(&rxrpc_krxiod_callq)) {
				call = list_entry(rxrpc_krxiod_callq.next,
						  struct rxrpc_call,
						  rcv_krxiodq_lk);
				list_del_init(&call->rcv_krxiodq_lk);
				atomic_dec(&rxrpc_krxiod_qcount);

				/* make sure it hasn't gone away and doesn't go
				 * away */
				if (atomic_read(&call->usage) > 0) {
					_debug("@@@ KRXIOD"
					       " Begin Attend Call %p", call);
					rxrpc_get_call(call);
				}
				else {
					call = NULL;
				}
			}

			spin_unlock_irq(&rxrpc_krxiod_callq_lock);

			if (call) {
				rxrpc_call_do_stuff(call);
				rxrpc_put_call(call);
				_debug("@@@ KRXIOD End Attend Call %p", call);
			}
		}

		_debug("### End Work");

		try_to_freeze();

		/* discard pending signals */
		rxrpc_discard_my_signals();

	} while (!rxrpc_krxiod_die);

	/* and that's all */
	complete_and_exit(&rxrpc_krxiod_dead, 0);

} /* end rxrpc_krxiod() */

/*****************************************************************************/
/*
 * start up a krxiod daemon
 */
int __init rxrpc_krxiod_init(void)
{
	return kernel_thread(rxrpc_krxiod, NULL, 0);

} /* end rxrpc_krxiod_init() */

/*****************************************************************************/
/*
 * kill the krxiod daemon and wait for it to complete
 */
void rxrpc_krxiod_kill(void)
{
	rxrpc_krxiod_die = 1;
	wake_up_all(&rxrpc_krxiod_sleepq);
	wait_for_completion(&rxrpc_krxiod_dead);

} /* end rxrpc_krxiod_kill() */

/*****************************************************************************/
/*
 * queue a transport for attention by krxiod
 */
void rxrpc_krxiod_queue_transport(struct rxrpc_transport *trans)
{
	unsigned long flags;

	_enter("");

	if (list_empty(&trans->krxiodq_link)) {
		spin_lock_irqsave(&rxrpc_krxiod_transportq_lock, flags);

		if (list_empty(&trans->krxiodq_link)) {
			if (atomic_read(&trans->usage) > 0) {
				list_add_tail(&trans->krxiodq_link,
					      &rxrpc_krxiod_transportq);
				atomic_inc(&rxrpc_krxiod_qcount);
			}
		}

		spin_unlock_irqrestore(&rxrpc_krxiod_transportq_lock, flags);
		wake_up_all(&rxrpc_krxiod_sleepq);
	}

	_leave("");

} /* end rxrpc_krxiod_queue_transport() */

/*****************************************************************************/
/*
 * dequeue a transport from krxiod's attention queue
 */
void rxrpc_krxiod_dequeue_transport(struct rxrpc_transport *trans)
{
	unsigned long flags;

	_enter("");

	spin_lock_irqsave(&rxrpc_krxiod_transportq_lock, flags);
	if (!list_empty(&trans->krxiodq_link)) {
		list_del_init(&trans->krxiodq_link);
		atomic_dec(&rxrpc_krxiod_qcount);
	}
	spin_unlock_irqrestore(&rxrpc_krxiod_transportq_lock, flags);

	_leave("");

} /* end rxrpc_krxiod_dequeue_transport() */

/*****************************************************************************/
/*
 * queue a call for attention by krxiod
 */
void rxrpc_krxiod_queue_call(struct rxrpc_call *call)
{
	unsigned long flags;

	if (list_empty(&call->rcv_krxiodq_lk)) {
		spin_lock_irqsave(&rxrpc_krxiod_callq_lock, flags);
		if (atomic_read(&call->usage) > 0) {
			list_add_tail(&call->rcv_krxiodq_lk,
				      &rxrpc_krxiod_callq);
			atomic_inc(&rxrpc_krxiod_qcount);
		}
		spin_unlock_irqrestore(&rxrpc_krxiod_callq_lock, flags);
	}
	wake_up_all(&rxrpc_krxiod_sleepq);

} /* end rxrpc_krxiod_queue_call() */

/*****************************************************************************/
/*
 * dequeue a call from krxiod's attention queue
 */
void rxrpc_krxiod_dequeue_call(struct rxrpc_call *call)
{
	unsigned long flags;

	spin_lock_irqsave(&rxrpc_krxiod_callq_lock, flags);
	if (!list_empty(&call->rcv_krxiodq_lk)) {
		list_del_init(&call->rcv_krxiodq_lk);
		atomic_dec(&rxrpc_krxiod_qcount);
	}
	spin_unlock_irqrestore(&rxrpc_krxiod_callq_lock, flags);

} /* end rxrpc_krxiod_dequeue_call() */
