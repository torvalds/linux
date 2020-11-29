/*
 * net/tipc/subscr.c: TIPC network topology service
 *
 * Copyright (c) 2000-2017, Ericsson AB
 * Copyright (c) 2005-2007, 2010-2013, Wind River Systems
 * Copyright (c) 2020, Red Hat Inc
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
				u32 found_lower, u32 found_upper,
				u32 event, u32 port, u32 node)
{
	struct tipc_event *evt = &sub->evt;

	if (sub->inactive)
		return;
	tipc_evt_write(evt, event, event);
	tipc_evt_write(evt, found_lower, found_lower);
	tipc_evt_write(evt, found_upper, found_upper);
	tipc_evt_write(evt, port.ref, port);
	tipc_evt_write(evt, port.node, node);
	tipc_topsrv_queue_evt(sub->net, sub->conid, event, evt);
}

/**
 * tipc_sub_check_overlap - test for subscription overlap with the given values
 * @seq: tipc_name_seq to check
 * @found_lower: lower value to test
 * @found_upper: upper value to test
 *
 * Return: 1 if there is overlap, otherwise 0.
 */
int tipc_sub_check_overlap(struct tipc_service_range *seq, u32 found_lower,
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

void tipc_sub_report_overlap(struct tipc_subscription *sub,
			     u32 found_lower, u32 found_upper,
			     u32 event, u32 port, u32 node,
			     u32 scope, int must)
{
	struct tipc_subscr *s = &sub->evt.s;
	u32 filter = tipc_sub_read(s, filter);
	struct tipc_service_range seq;

	seq.type = tipc_sub_read(s, seq.type);
	seq.lower = tipc_sub_read(s, seq.lower);
	seq.upper = tipc_sub_read(s, seq.upper);

	if (!tipc_sub_check_overlap(&seq, found_lower, found_upper))
		return;

	if (!must && !(filter & TIPC_SUB_PORTS))
		return;
	if (filter & TIPC_SUB_CLUSTER_SCOPE && scope == TIPC_NODE_SCOPE)
		return;
	if (filter & TIPC_SUB_NODE_SCOPE && scope != TIPC_NODE_SCOPE)
		return;
	spin_lock(&sub->lock);
	tipc_sub_send_event(sub, found_lower, found_upper,
			    event, port, node);
	spin_unlock(&sub->lock);
}

static void tipc_sub_timeout(struct timer_list *t)
{
	struct tipc_subscription *sub = from_timer(sub, t, timer);
	struct tipc_subscr *s = &sub->evt.s;

	spin_lock(&sub->lock);
	tipc_sub_send_event(sub, s->seq.lower, s->seq.upper,
			    TIPC_SUBSCR_TIMEOUT, 0, 0);
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
	u32 filter = tipc_sub_read(s, filter);
	struct tipc_subscription *sub;
	u32 timeout;

	if ((filter & TIPC_SUB_PORTS && filter & TIPC_SUB_SERVICE) ||
	    (tipc_sub_read(s, seq.lower) > tipc_sub_read(s, seq.upper))) {
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
		del_timer_sync(&sub->timer);
	list_del(&sub->sub_list);
	tipc_sub_put(sub);
}
