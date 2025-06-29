/*
 * net/tipc/subscr.c: TIPC network topology service
 *
 * Copyright (c) 2000-2017, Ericsson AB
 * Copyright (c) 2005-2007, 2010-2013, Wind River Systems
 * Copyright (c) 2020-2021, Red Hat Inc
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

static void tipc_sub_send_event(struct tipc_subscription *sub,
				struct publication *p,
				u32 event)
{
	struct tipc_subscr *s = &sub->evt.s;
	struct tipc_event *evt = &sub->evt;

	if (sub->inactive)
		return;
	tipc_evt_write(evt, event, event);
	if (p) {
		tipc_evt_write(evt, found_lower, p->sr.lower);
		tipc_evt_write(evt, found_upper, p->sr.upper);
		tipc_evt_write(evt, port.ref, p->sk.ref);
		tipc_evt_write(evt, port.node, p->sk.node);
	} else {
		tipc_evt_write(evt, found_lower, s->seq.lower);
		tipc_evt_write(evt, found_upper, s->seq.upper);
		tipc_evt_write(evt, port.ref, 0);
		tipc_evt_write(evt, port.node, 0);
	}
	tipc_topsrv_queue_evt(sub->net, sub->conid, event, evt);
}

/**
 * tipc_sub_check_overlap - test for subscription overlap with the given values
 * @subscribed: the service range subscribed for
 * @found: the service range we are checking for match
 *
 * Returns true if there is overlap, otherwise false.
 */
static bool tipc_sub_check_overlap(struct tipc_service_range *subscribed,
				   struct tipc_service_range *found)
{
	u32 found_lower = found->lower;
	u32 found_upper = found->upper;

	if (found_lower < subscribed->lower)
		found_lower = subscribed->lower;
	if (found_upper > subscribed->upper)
		found_upper = subscribed->upper;
	return found_lower <= found_upper;
}

void tipc_sub_report_overlap(struct tipc_subscription *sub,
			     struct publication *p,
			     u32 event, bool must)
{
	struct tipc_service_range *sr = &sub->s.seq;
	u32 filter = sub->s.filter;

	if (!tipc_sub_check_overlap(sr, &p->sr))
		return;
	if (!must && !(filter & TIPC_SUB_PORTS))
		return;
	if (filter & TIPC_SUB_CLUSTER_SCOPE && p->scope == TIPC_NODE_SCOPE)
		return;
	if (filter & TIPC_SUB_NODE_SCOPE && p->scope != TIPC_NODE_SCOPE)
		return;
	spin_lock(&sub->lock);
	tipc_sub_send_event(sub, p, event);
	spin_unlock(&sub->lock);
}

static void tipc_sub_timeout(struct timer_list *t)
{
	struct tipc_subscription *sub = timer_container_of(sub, t, timer);

	spin_lock(&sub->lock);
	tipc_sub_send_event(sub, NULL, TIPC_SUBSCR_TIMEOUT);
	sub->inactive = true;
	spin_unlock(&sub->lock);
}

static void tipc_sub_kref_release(struct kref *kref)
{
	kfree(container_of(kref, struct tipc_subscription, kref));
}

void tipc_sub_put(struct tipc_subscription *subscription)
{
	kref_put(&subscription->kref, tipc_sub_kref_release);
}

void tipc_sub_get(struct tipc_subscription *subscription)
{
	kref_get(&subscription->kref);
}

struct tipc_subscription *tipc_sub_subscribe(struct net *net,
					     struct tipc_subscr *s,
					     int conid)
{
	u32 lower = tipc_sub_read(s, seq.lower);
	u32 upper = tipc_sub_read(s, seq.upper);
	u32 filter = tipc_sub_read(s, filter);
	struct tipc_subscription *sub;
	u32 timeout;

	if ((filter & TIPC_SUB_PORTS && filter & TIPC_SUB_SERVICE) ||
	    lower > upper) {
		pr_warn("Subscription rejected, illegal request\n");
		return NULL;
	}
	sub = kmalloc(sizeof(*sub), GFP_ATOMIC);
	if (!sub) {
		pr_warn("Subscription rejected, no memory\n");
		return NULL;
	}
	INIT_LIST_HEAD(&sub->service_list);
	INIT_LIST_HEAD(&sub->sub_list);
	sub->net = net;
	sub->conid = conid;
	sub->inactive = false;
	memcpy(&sub->evt.s, s, sizeof(*s));
	sub->s.seq.type = tipc_sub_read(s, seq.type);
	sub->s.seq.lower = lower;
	sub->s.seq.upper = upper;
	sub->s.filter = filter;
	sub->s.timeout = tipc_sub_read(s, timeout);
	memcpy(sub->s.usr_handle, s->usr_handle, 8);
	spin_lock_init(&sub->lock);
	kref_init(&sub->kref);
	if (!tipc_nametbl_subscribe(sub)) {
		kfree(sub);
		return NULL;
	}
	timer_setup(&sub->timer, tipc_sub_timeout, 0);
	timeout = tipc_sub_read(&sub->evt.s, timeout);
	if (timeout != TIPC_WAIT_FOREVER)
		mod_timer(&sub->timer, jiffies + msecs_to_jiffies(timeout));
	return sub;
}

void tipc_sub_unsubscribe(struct tipc_subscription *sub)
{
	tipc_nametbl_unsubscribe(sub);
	if (sub->evt.s.timeout != TIPC_WAIT_FOREVER)
		timer_delete_sync(&sub->timer);
	list_del(&sub->sub_list);
	tipc_sub_put(sub);
}
