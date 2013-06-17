/*
 * net/tipc/subscr.c: TIPC network topology service
 *
 * Copyright (c) 2000-2006, Ericsson AB
 * Copyright (c) 2005-2007, 2010-2013, Wind River Systems
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "core.h"
#include "name_table.h"
#include "port.h"
#include "subscr.h"

/**
 * struct tipc_subscriber - TIPC network topology subscriber
 * @conid: connection identifier to server connecting to subscriber
 * @lock: controll access to subscriber
 * @subscription_list: list of subscription objects for this subscriber
 */
struct tipc_subscriber {
	int conid;
	spinlock_t lock;
	struct list_head subscription_list;
};

static void subscr_conn_msg_event(int conid, struct sockaddr_tipc *addr,
				  void *usr_data, void *buf, size_t len);
static void *subscr_named_msg_event(int conid);
static void subscr_conn_shutdown_event(int conid, void *usr_data);

static atomic_t subscription_count = ATOMIC_INIT(0);

static struct sockaddr_tipc topsrv_addr __read_mostly = {
	.family			= AF_TIPC,
	.addrtype		= TIPC_ADDR_NAMESEQ,
	.addr.nameseq.type	= TIPC_TOP_SRV,
	.addr.nameseq.lower	= TIPC_TOP_SRV,
	.addr.nameseq.upper	= TIPC_TOP_SRV,
	.scope			= TIPC_NODE_SCOPE
};

static struct tipc_server topsrv __read_mostly = {
	.saddr			= &topsrv_addr,
	.imp			= TIPC_CRITICAL_IMPORTANCE,
	.type			= SOCK_SEQPACKET,
	.max_rcvbuf_size	= sizeof(struct tipc_subscr),
	.name			= "topology_server",
	.tipc_conn_recvmsg	= subscr_conn_msg_event,
	.tipc_conn_new		= subscr_named_msg_event,
	.tipc_conn_shutdown	= subscr_conn_shutdown_event,
};

/**
 * htohl - convert value to endianness used by destination
 * @in: value to convert
 * @swap: non-zero if endianness must be reversed
 *
 * Returns converted value
 */
static u32 htohl(u32 in, int swap)
{
	return swap ? swab32(in) : in;
}

static void subscr_send_event(struct tipc_subscription *sub, u32 found_lower,
			      u32 found_upper, u32 event, u32 port_ref,
			      u32 node)
{
	struct tipc_subscriber *subscriber = sub->subscriber;
	struct kvec msg_sect;
	int ret;

	msg_sect.iov_base = (void *)&sub->evt;
	msg_sect.iov_len = sizeof(struct tipc_event);

	sub->evt.event = htohl(event, sub->swap);
	sub->evt.found_lower = htohl(found_lower, sub->swap);
	sub->evt.found_upper = htohl(found_upper, sub->swap);
	sub->evt.port.ref = htohl(port_ref, sub->swap);
	sub->evt.port.node = htohl(node, sub->swap);
	ret = tipc_conn_sendmsg(&topsrv, subscriber->conid, NULL,
				msg_sect.iov_base, msg_sect.iov_len);
	if (ret < 0)
		pr_err("Sending subscription event failed, no memory\n");
}

/**
 * tipc_subscr_overlap - test for subscription overlap with the given values
 *
 * Returns 1 if there is overlap, otherwise 0.
 */
int tipc_subscr_overlap(struct tipc_subscription *sub,
			u32 found_lower,
			u32 found_upper)

{
	if (found_lower < sub->seq.lower)
		found_lower = sub->seq.lower;
	if (found_upper > sub->seq.upper)
		found_upper = sub->seq.upper;
	if (found_lower > found_upper)
		return 0;
	return 1;
}

/**
 * tipc_subscr_report_overlap - issue event if there is subscription overlap
 *
 * Protected by nameseq.lock in name_table.c
 */
void tipc_subscr_report_overlap(struct tipc_subscription *sub,
				u32 found_lower,
				u32 found_upper,
				u32 event,
				u32 port_ref,
				u32 node,
				int must)
{
	if (!tipc_subscr_overlap(sub, found_lower, found_upper))
		return;
	if (!must && !(sub->filter & TIPC_SUB_PORTS))
		return;

	subscr_send_event(sub, found_lower, found_upper, event, port_ref, node);
}

static void subscr_timeout(struct tipc_subscription *sub)
{
	struct tipc_subscriber *subscriber = sub->subscriber;

	/* The spin lock per subscriber is used to protect its members */
	spin_lock_bh(&subscriber->lock);

	/* Validate if the connection related to the subscriber is
	 * closed (in case subscriber is terminating)
	 */
	if (subscriber->conid == 0) {
		spin_unlock_bh(&subscriber->lock);
		return;
	}

	/* Validate timeout (in case subscription is being cancelled) */
	if (sub->timeout == TIPC_WAIT_FOREVER) {
		spin_unlock_bh(&subscriber->lock);
		return;
	}

	/* Unlink subscription from name table */
	tipc_nametbl_unsubscribe(sub);

	/* Unlink subscription from subscriber */
	list_del(&sub->subscription_list);

	spin_unlock_bh(&subscriber->lock);

	/* Notify subscriber of timeout */
	subscr_send_event(sub, sub->evt.s.seq.lower, sub->evt.s.seq.upper,
			  TIPC_SUBSCR_TIMEOUT, 0, 0);

	/* Now destroy subscription */
	k_term_timer(&sub->timer);
	kfree(sub);
	atomic_dec(&subscription_count);
}

/**
 * subscr_del - delete a subscription within a subscription list
 *
 * Called with subscriber lock held.
 */
static void subscr_del(struct tipc_subscription *sub)
{
	tipc_nametbl_unsubscribe(sub);
	list_del(&sub->subscription_list);
	kfree(sub);
	atomic_dec(&subscription_count);
}

/**
 * subscr_terminate - terminate communication with a subscriber
 *
 * Note: Must call it in process context since it might sleep.
 */
static void subscr_terminate(struct tipc_subscriber *subscriber)
{
	tipc_conn_terminate(&topsrv, subscriber->conid);
}

static void subscr_release(struct tipc_subscriber *subscriber)
{
	struct tipc_subscription *sub;
	struct tipc_subscription *sub_temp;

	spin_lock_bh(&subscriber->lock);

	/* Invalidate subscriber reference */
	subscriber->conid = 0;

	/* Destroy any existing subscriptions for subscriber */
	list_for_each_entry_safe(sub, sub_temp, &subscriber->subscription_list,
				 subscription_list) {
		if (sub->timeout != TIPC_WAIT_FOREVER) {
			spin_unlock_bh(&subscriber->lock);
			k_cancel_timer(&sub->timer);
			k_term_timer(&sub->timer);
			spin_lock_bh(&subscriber->lock);
		}
		subscr_del(sub);
	}
	spin_unlock_bh(&subscriber->lock);

	/* Now destroy subscriber */
	kfree(subscriber);
}

/**
 * subscr_cancel - handle subscription cancellation request
 *
 * Called with subscriber lock held. Routine must temporarily release lock
 * to enable the subscription timeout routine to finish without deadlocking;
 * the lock is then reclaimed to allow caller to release it upon return.
 *
 * Note that fields of 's' use subscriber's endianness!
 */
static void subscr_cancel(struct tipc_subscr *s,
			  struct tipc_subscriber *subscriber)
{
	struct tipc_subscription *sub;
	struct tipc_subscription *sub_temp;
	int found = 0;

	/* Find first matching subscription, exit if not found */
	list_for_each_entry_safe(sub, sub_temp, &subscriber->subscription_list,
				 subscription_list) {
		if (!memcmp(s, &sub->evt.s, sizeof(struct tipc_subscr))) {
			found = 1;
			break;
		}
	}
	if (!found)
		return;

	/* Cancel subscription timer (if used), then delete subscription */
	if (sub->timeout != TIPC_WAIT_FOREVER) {
		sub->timeout = TIPC_WAIT_FOREVER;
		spin_unlock_bh(&subscriber->lock);
		k_cancel_timer(&sub->timer);
		k_term_timer(&sub->timer);
		spin_lock_bh(&subscriber->lock);
	}
	subscr_del(sub);
}

/**
 * subscr_subscribe - create subscription for subscriber
 *
 * Called with subscriber lock held.
 */
static struct tipc_subscription *subscr_subscribe(struct tipc_subscr *s,
					     struct tipc_subscriber *subscriber)
{
	struct tipc_subscription *sub;
	int swap;

	/* Determine subscriber's endianness */
	swap = !(s->filter & (TIPC_SUB_PORTS | TIPC_SUB_SERVICE));

	/* Detect & process a subscription cancellation request */
	if (s->filter & htohl(TIPC_SUB_CANCEL, swap)) {
		s->filter &= ~htohl(TIPC_SUB_CANCEL, swap);
		subscr_cancel(s, subscriber);
		return NULL;
	}

	/* Refuse subscription if global limit exceeded */
	if (atomic_read(&subscription_count) >= TIPC_MAX_SUBSCRIPTIONS) {
		pr_warn("Subscription rejected, limit reached (%u)\n",
			TIPC_MAX_SUBSCRIPTIONS);
		subscr_terminate(subscriber);
		return NULL;
	}

	/* Allocate subscription object */
	sub = kmalloc(sizeof(*sub), GFP_ATOMIC);
	if (!sub) {
		pr_warn("Subscription rejected, no memory\n");
		subscr_terminate(subscriber);
		return NULL;
	}

	/* Initialize subscription object */
	sub->seq.type = htohl(s->seq.type, swap);
	sub->seq.lower = htohl(s->seq.lower, swap);
	sub->seq.upper = htohl(s->seq.upper, swap);
	sub->timeout = htohl(s->timeout, swap);
	sub->filter = htohl(s->filter, swap);
	if ((!(sub->filter & TIPC_SUB_PORTS) ==
	     !(sub->filter & TIPC_SUB_SERVICE)) ||
	    (sub->seq.lower > sub->seq.upper)) {
		pr_warn("Subscription rejected, illegal request\n");
		kfree(sub);
		subscr_terminate(subscriber);
		return NULL;
	}
	INIT_LIST_HEAD(&sub->nameseq_list);
	list_add(&sub->subscription_list, &subscriber->subscription_list);
	sub->subscriber = subscriber;
	sub->swap = swap;
	memcpy(&sub->evt.s, s, sizeof(struct tipc_subscr));
	atomic_inc(&subscription_count);
	if (sub->timeout != TIPC_WAIT_FOREVER) {
		k_init_timer(&sub->timer,
			     (Handler)subscr_timeout, (unsigned long)sub);
		k_start_timer(&sub->timer, sub->timeout);
	}

	return sub;
}

/* Handle one termination request for the subscriber */
static void subscr_conn_shutdown_event(int conid, void *usr_data)
{
	subscr_release((struct tipc_subscriber *)usr_data);
}

/* Handle one request to create a new subscription for the subscriber */
static void subscr_conn_msg_event(int conid, struct sockaddr_tipc *addr,
				  void *usr_data, void *buf, size_t len)
{
	struct tipc_subscriber *subscriber = usr_data;
	struct tipc_subscription *sub;

	spin_lock_bh(&subscriber->lock);
	sub = subscr_subscribe((struct tipc_subscr *)buf, subscriber);
	if (sub)
		tipc_nametbl_subscribe(sub);
	spin_unlock_bh(&subscriber->lock);
}


/* Handle one request to establish a new subscriber */
static void *subscr_named_msg_event(int conid)
{
	struct tipc_subscriber *subscriber;

	/* Create subscriber object */
	subscriber = kzalloc(sizeof(struct tipc_subscriber), GFP_ATOMIC);
	if (subscriber == NULL) {
		pr_warn("Subscriber rejected, no memory\n");
		return NULL;
	}
	INIT_LIST_HEAD(&subscriber->subscription_list);
	subscriber->conid = conid;
	spin_lock_init(&subscriber->lock);

	return (void *)subscriber;
}

int tipc_subscr_start(void)
{
	return tipc_server_start(&topsrv);
}

void tipc_subscr_stop(void)
{
	tipc_server_stop(&topsrv);
}
