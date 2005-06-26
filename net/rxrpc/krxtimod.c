/* krxtimod.c: RXRPC timeout daemon
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/completion.h>
#include <rxrpc/rxrpc.h>
#include <rxrpc/krxtimod.h>
#include <asm/errno.h>
#include "internal.h"

static DECLARE_COMPLETION(krxtimod_alive);
static DECLARE_COMPLETION(krxtimod_dead);
static DECLARE_WAIT_QUEUE_HEAD(krxtimod_sleepq);
static int krxtimod_die;

static LIST_HEAD(krxtimod_list);
static DEFINE_SPINLOCK(krxtimod_lock);

static int krxtimod(void *arg);

/*****************************************************************************/
/*
 * start the timeout daemon
 */
int rxrpc_krxtimod_start(void)
{
	int ret;

	ret = kernel_thread(krxtimod, NULL, 0);
	if (ret < 0)
		return ret;

	wait_for_completion(&krxtimod_alive);

	return ret;
} /* end rxrpc_krxtimod_start() */

/*****************************************************************************/
/*
 * stop the timeout daemon
 */
void rxrpc_krxtimod_kill(void)
{
	/* get rid of my daemon */
	krxtimod_die = 1;
	wake_up(&krxtimod_sleepq);
	wait_for_completion(&krxtimod_dead);

} /* end rxrpc_krxtimod_kill() */

/*****************************************************************************/
/*
 * timeout processing daemon
 */
static int krxtimod(void *arg)
{
	DECLARE_WAITQUEUE(myself, current);

	rxrpc_timer_t *timer;

	printk("Started krxtimod %d\n", current->pid);

	daemonize("krxtimod");

	complete(&krxtimod_alive);

	/* loop around looking for things to attend to */
 loop:
	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&krxtimod_sleepq, &myself);

	for (;;) {
		unsigned long jif;
		signed long timeout;

		/* deal with the server being asked to die */
		if (krxtimod_die) {
			remove_wait_queue(&krxtimod_sleepq, &myself);
			_leave("");
			complete_and_exit(&krxtimod_dead, 0);
		}

		try_to_freeze();

		/* discard pending signals */
		rxrpc_discard_my_signals();

		/* work out the time to elapse before the next event */
		spin_lock(&krxtimod_lock);
		if (list_empty(&krxtimod_list)) {
			timeout = MAX_SCHEDULE_TIMEOUT;
		}
		else {
			timer = list_entry(krxtimod_list.next,
					   rxrpc_timer_t, link);
			timeout = timer->timo_jif;
			jif = jiffies;

			if (time_before_eq((unsigned long) timeout, jif))
				goto immediate;

			else {
				timeout = (long) timeout - (long) jiffies;
			}
		}
		spin_unlock(&krxtimod_lock);

		schedule_timeout(timeout);

		set_current_state(TASK_INTERRUPTIBLE);
	}

	/* the thing on the front of the queue needs processing
	 * - we come here with the lock held and timer pointing to the expired
	 *   entry
	 */
 immediate:
	remove_wait_queue(&krxtimod_sleepq, &myself);
	set_current_state(TASK_RUNNING);

	_debug("@@@ Begin Timeout of %p", timer);

	/* dequeue the timer */
	list_del_init(&timer->link);
	spin_unlock(&krxtimod_lock);

	/* call the timeout function */
	timer->ops->timed_out(timer);

	_debug("@@@ End Timeout");
	goto loop;

} /* end krxtimod() */

/*****************************************************************************/
/*
 * (re-)queue a timer
 */
void rxrpc_krxtimod_add_timer(rxrpc_timer_t *timer, unsigned long timeout)
{
	struct list_head *_p;
	rxrpc_timer_t *ptimer;

	_enter("%p,%lu", timer, timeout);

	spin_lock(&krxtimod_lock);

	list_del(&timer->link);

	/* the timer was deferred or reset - put it back in the queue at the
	 * right place */
	timer->timo_jif = jiffies + timeout;

	list_for_each(_p, &krxtimod_list) {
		ptimer = list_entry(_p, rxrpc_timer_t, link);
		if (time_before(timer->timo_jif, ptimer->timo_jif))
			break;
	}

	list_add_tail(&timer->link, _p); /* insert before stopping point */

	spin_unlock(&krxtimod_lock);

	wake_up(&krxtimod_sleepq);

	_leave("");
} /* end rxrpc_krxtimod_add_timer() */

/*****************************************************************************/
/*
 * dequeue a timer
 * - returns 0 if the timer was deleted or -ENOENT if it wasn't queued
 */
int rxrpc_krxtimod_del_timer(rxrpc_timer_t *timer)
{
	int ret = 0;

	_enter("%p", timer);

	spin_lock(&krxtimod_lock);

	if (list_empty(&timer->link))
		ret = -ENOENT;
	else
		list_del_init(&timer->link);

	spin_unlock(&krxtimod_lock);

	wake_up(&krxtimod_sleepq);

	_leave(" = %d", ret);
	return ret;
} /* end rxrpc_krxtimod_del_timer() */
