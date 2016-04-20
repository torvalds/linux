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
#include "subscr.h"

/**
 * struct tipc_subscriber - TIPC network topology subscriber
 * @kref: reference counter to tipc_subscription object
 * @conid: connection identifier to server connecting to subscriber
 * @lock: control access to subscriber
 * @subscrp_list: list of subscription objects for this subscriber
 */
struct tipc_subscriber {
	struct kref kref;
	int conid;
	spinlock_t lock;
	struct list_head subscrp_list;
};

static void tipc_subscrp_delete(struct tipc_subscription *sub);
static void tipc_subscrb_put(struct tipc_subscriber *subscriber);

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

static void tipc_subscrp_send_event(struct tipc_subscription *sub,
				    u32 found_lower, u32 found_upper,
				    u32 event, u32 port_ref, u32 node)
{
	struct tipc_net *tn = net_generic(sub->net, tipc_net_id);
	struct tipc_subscriber *subscriber = sub->subscriber;
	struct kvec msg_sect;

	msg_sect.iov_base = (void *)&sub->evt;
	msg_sect.iov_len = sizeof(struct tipc_event);
	sub->evt.event = htohl(event, sub->swap);
	sub->evt.found_lower = htohl(found_lower, sub->swap);
	sub->evt.found_upper = htohl(found_upper, sub->swap);
	sub->evt.port.ref = htohl(port_ref, sub->swap);
	sub->evt.port.node = htohl(node, sub->swap);
	tipc_conn_sendmsg(tn->topsrv, subscriber->conid, NULL,
			  msg_sect.iov_base, msg_sect.iov_len);
}

/**
 * tipc_subscrp_check_overlap - test for subscription overlap with the
 * given values
 *
 * Returns 1 if there is overlap, otherwise 0.
 */
int tipc_subscrp_check_overlap(struct tipc_name_seq *seq, u32 found_lower,
			       u32 found_upper)
{
	if (found_lower < seq->lower)
		found_lower = seq->lower;
	if (found_upper > seq->upper)
		found_upper = seq->upper;
	if (found_lower > found_upper)
		return 0;
	return 1;
}

u32 tipc_subscrp_convert_seq_type(u32 type, int swap)
{
	return htohl(type, swap);
}

void tipc_subscrp_convert_seq(struct tipc_name_seq *in, int swap,
			      struct tipc_name_seq *out)
{
	out->type = htohl(in->type, swap);
	out->lower = htohl(in->lower, swap);
	out->upper = htohl(in->upper, swap);
}

void tipc_subscrp_report_overlap(struct tipc_subscription *sub, u32 found_lower,
				 u32 found_upper, u32 event, u32 port_ref,
				 u32 node, int must)
{
	struct tipc_name_seq seq;

	tipc_subscrp_convert_seq(&sub->evt.s.seq, sub->swap, &seq);
	if (!tipc_subscrp_check_overlap(&seq, found_lower, found_upper))
		return;
	if (!must &&
	    !(htohl(sub->evt.s.filter, sub->swap) & TIPC_SUB_PORTS))
		return;

	tipc_subscrp_send_event(sub, found_lower, found_upper, event, port_ref,
				node);
}

static void tipc_subscrp_timeout(unsigned long data)
{
	struct tipc_subscription *sub = (struct tipc_subscription *)data;
	struct tipc_subscriber *subscriber = sub->subscriber;

	/* Notify subscriber of timeout */
	tipc_subscrp_send_event(sub, sub->evt.s.seq.lower, sub->evt.s.seq.upper,
				TIPC_SUBSCR_TIMEOUT, 0, 0);

	spin_lock_bh(&subscriber->lock);
	tipc_subscrp_delete(sub);
	spin_unlock_bh(&subscriber->lock);

	tipc_subscrb_put(subscriber);
}

static void tipc_subscrb_kref_release(struct kref *kref)
{
	struct tipc_subscriber *subcriber = container_of(kref,
					    struct tipc_subscriber, kref);

	kfree(subcriber);
}

static void tipc_subscrb_put(struct tipc_subscriber *subscriber)
{
	kref_put(&subscriber->kref, tipc_subscrb_kref_release);
}

static void tipc_subscrb_get(struct tipc_subscriber *subscriber)
{
	kref_get(&subscriber->kref);
}

static struct tipc_subscriber *tipc_subscrb_create(int conid)
{
	struct tipc_subscriber *subscriber;

	subscriber = kzalloc(sizeof(*subscriber), GFP_ATOMIC);
	if (!subscriber) {
		pr_warn("Subscriber rejected, no memory\n");
		return NULL;
	}
	kref_init(&subscriber->kref);
	INIT_LIST_HEAD(&subscriber->subscrp_list);
	subscriber->conid = conid;
	spin_lock_init(&subscriber->lock);

	return subscriber;
}

static void tipc_subscrb_delete(struct tipc_subscriber *subscriber)
{
	struct tipc_subscription *sub, *temp;
	u32 timeout;

	spin_lock_bh(&subscriber->lock);
	/* Destroy any existing subscriptions for subscriber */
	list_for_each_entry_safe(sub, temp, &subscriber->subscrp_list,
				 subscrp_list) {
		timeout = htohl(sub->evt.s.timeout, sub->swap);
		if ((timeout == TIPC_WAIT_FOREVER) || del_timer(&sub->timer)) {
			tipc_subscrp_delete(sub);
			tipc_subscrb_put(subscriber);
		}
	}
	spin_unlock_bh(&subscriber->lock);

	tipc_subscrb_put(subscriber);
}

static void tipc_subscrp_delete(struct tipc_subscription *sub)
{
	struct tipc_net *tn = net_generic(sub->net, tipc_net_id);

	tipc_nametbl_unsubscribe(sub);
	list_del(&sub->subscrp_list);
	kfree(sub);
	atomic_dec(&tn->subscription_count);
}

static void tipc_subscrp_cancel(struct tipc_subscr *s,
				struct tipc_subscriber *subscriber)
{
	struct tipc_subscription *sub, *temp;
	u32 timeout;

	spin_lock_bh(&subscriber->lock);
	/* Find first matching subscription, exit if not found */
	list_for_each_entry_safe(sub, temp, &subscriber->subscrp_list,
				 subscrp_list) {
		if (!memcmp(s, &sub->evt.s, sizeof(struct tipc_subscr))) {
			timeout = htohl(sub->evt.s.timeout, sub->swap);
			if ((timeout == TIPC_WAIT_FOREVER) ||
			    del_timer(&sub->timer)) {
				tipc_subscrp_delete(sub);
				tipc_subscrb_put(subscriber);
			}
			break;
		}
	}
	spin_unlock_bh(&subscriber->lock);
}

static struct tipc_subscription *tipc_subscrp_create(struct net *net,
						     struct tipc_subscr *s,
						     int swap)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_subscription *sub;
	u32 filter = htohl(s->filter, swap);

	/* Refuse subscription if global limit exceeded */
	if (atomic_read(&tn->subscription_count) >= TIPC_MAX_SUBSCRIPTIONS) {
		pr_warn("Subscription rejected, limit reached (%u)\n",
			TIPC_MAX_SUBSCRIPTIONS);
		return NULL;
	}

	/* Allocate subscription object */
	sub = kmalloc(sizeof(*sub), GFP_ATOMIC);
	if (!sub) {
		pr_warn("Subscription rejected, no memory\n");
		return NULL;
	}

	/* Initialize subscription object */
	sub->net = net;
	if (((filter & TIPC_SUB_PORTS) && (filter & TIPC_SUB_SERVICE)) ||
	    (htohl(s->seq.lower, swap) > htohl(s->seq.upper, swap))) {
		pr_warn("Subscription rejected, illegal request\n");
		kfree(sub);
		return NULL;
	}

	sub->swap = swap;
	memcpy(&sub->evt.s, s, sizeof(*s));
	atomic_inc(&tn->subscription_count);
	return sub;
}

static void tipc_subscrp_subscribe(struct net *net, struct tipc_subscr *s,
				   struct tipc_subscriber *subscriber, int swap)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_subscription *sub = NULL;
	u32 timeout;

	sub = tipc_subscrp_create(net, s, swap);
	if (!sub)
		return tipc_conn_terminate(tn->topsrv, subscriber->conid);

	spin_lock_bh(&subscriber->lock);
	list_add(&sub->subscrp_list, &subscriber->subscrp_list);
	tipc_subscrb_get(subscriber);
	sub->subscriber = subscriber;
	tipc_nametbl_subscribe(sub);
	spin_unlock_bh(&subscriber->lock);

	timeout = htohl(sub->evt.s.timeout, swap);
	if (timeout == TIPC_WAIT_FOREVER)
		return;

	setup_timer(&sub->timer, tipc_subscrp_timeout, (unsigned long)sub);
	mod_timer(&sub->timer, jiffies + msecs_to_jiffies(timeout));
}

/* Handle one termination request for the subscriber */
static void tipc_subscrb_shutdown_cb(int conid, void *usr_data)
{
	tipc_subscrb_delete((struct tipc_subscriber *)usr_data);
}

/* Handle one request to create a new subscription for the subscriber */
static void tipc_subscrb_rcv_cb(struct net *net, int conid,
				struct sockaddr_tipc *addr, void *usr_data,
				void *buf, size_t len)
{
	struct tipc_subscriber *subscriber = usr_data;
	struct tipc_subscr *s = (struct tipc_subscr *)buf;
	int swap;

	/* Determine subscriber's endianness */
	swap = !(s->filter & (TIPC_SUB_PORTS | TIPC_SUB_SERVICE |
			      TIPC_SUB_CANCEL));

	/* Detect & process a subscription cancellation request */
	if (s->filter & htohl(TIPC_SUB_CANCEL, swap)) {
		s->filter &= ~htohl(TIPC_SUB_CANCEL, swap);
		return tipc_subscrp_cancel(s, subscriber);
	}

	if (s)
		tipc_subscrp_subscribe(net, s, subscriber, swap);
}

/* Handle one request to establish a new subscriber */
static void *tipc_subscrb_connect_cb(int conid)
{
	return (void *)tipc_subscrb_create(conid);
}

int tipc_topsrv_start(struct net *net)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	const char name[] = "topology_server";
	struct tipc_server *topsrv;
	struct sockaddr_tipc *saddr;

	saddr = kzalloc(sizeof(*saddr), GFP_ATOMIC);
	if (!saddr)
		return -ENOMEM;
	saddr->family			= AF_TIPC;
	saddr->addrtype			= TIPC_ADDR_NAMESEQ;
	saddr->addr.nameseq.type	= TIPC_TOP_SRV;
	saddr->addr.nameseq.lower	= TIPC_TOP_SRV;
	saddr->addr.nameseq.upper	= TIPC_TOP_SRV;
	saddr->scope			= TIPC_NODE_SCOPE;

	topsrv = kzalloc(sizeof(*topsrv), GFP_ATOMIC);
	if (!topsrv) {
		kfree(saddr);
		return -ENOMEM;
	}
	topsrv->net			= net;
	topsrv->saddr			= saddr;
	topsrv->imp			= TIPC_CRITICAL_IMPORTANCE;
	topsrv->type			= SOCK_SEQPACKET;
	topsrv->max_rcvbuf_size		= sizeof(struct tipc_subscr);
	topsrv->tipc_conn_recvmsg	= tipc_subscrb_rcv_cb;
	topsrv->tipc_conn_new		= tipc_subscrb_connect_cb;
	topsrv->tipc_conn_shutdown	= tipc_subscrb_shutdown_cb;

	strncpy(topsrv->name, name, strlen(name) + 1);
	tn->topsrv = topsrv;
	atomic_set(&tn->subscription_count, 0);

	return tipc_server_start(topsrv);
}

void tipc_topsrv_stop(struct net *net)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	struct tipc_server *topsrv = tn->topsrv;

	tipc_server_stop(topsrv);
	kfree(topsrv->saddr);
	kfree(topsrv);
}
