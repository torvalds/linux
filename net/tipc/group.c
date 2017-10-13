/*
 * net/tipc/group.c: TIPC group messaging code
 *
 * Copyright (c) 2017, Ericsson AB
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
#include "addr.h"
#include "group.h"
#include "bcast.h"
#include "server.h"
#include "msg.h"
#include "socket.h"
#include "node.h"
#include "name_table.h"
#include "subscr.h"

#define ADV_UNIT (((MAX_MSG_SIZE + MAX_H_SIZE) / FLOWCTL_BLK_SZ) + 1)
#define ADV_IDLE ADV_UNIT
#define ADV_ACTIVE (ADV_UNIT * 12)

enum mbr_state {
	MBR_QUARANTINED,
	MBR_DISCOVERED,
	MBR_JOINING,
	MBR_PUBLISHED,
	MBR_JOINED,
	MBR_LEAVING
};

struct tipc_member {
	struct rb_node tree_node;
	struct list_head list;
	struct list_head congested;
	struct sk_buff *event_msg;
	struct sk_buff_head deferredq;
	struct tipc_group *group;
	u32 node;
	u32 port;
	u32 instance;
	enum mbr_state state;
	u16 advertised;
	u16 window;
	u16 bc_rcv_nxt;
	bool usr_pending;
};

struct tipc_group {
	struct rb_root members;
	struct list_head congested;
	struct tipc_nlist dests;
	struct net *net;
	int subid;
	u32 type;
	u32 instance;
	u32 domain;
	u32 scope;
	u32 portid;
	u16 member_cnt;
	u16 bc_snd_nxt;
	bool loopback;
	bool events;
};

static void tipc_group_proto_xmit(struct tipc_group *grp, struct tipc_member *m,
				  int mtyp, struct sk_buff_head *xmitq);

static int tipc_group_rcvbuf_limit(struct tipc_group *grp)
{
	int mcnt = grp->member_cnt + 1;

	/* Scale to bytes, considering worst-case truesize/msgsize ratio */
	return mcnt * ADV_ACTIVE * FLOWCTL_BLK_SZ * 4;
}

u16 tipc_group_bc_snd_nxt(struct tipc_group *grp)
{
	return grp->bc_snd_nxt;
}

static bool tipc_group_is_enabled(struct tipc_member *m)
{
	return m->state != MBR_QUARANTINED && m->state != MBR_LEAVING;
}

static bool tipc_group_is_receiver(struct tipc_member *m)
{
	return m && m->state >= MBR_JOINED;
}

u32 tipc_group_exclude(struct tipc_group *grp)
{
	if (!grp->loopback)
		return grp->portid;
	return 0;
}

int tipc_group_size(struct tipc_group *grp)
{
	return grp->member_cnt;
}

struct tipc_group *tipc_group_create(struct net *net, u32 portid,
				     struct tipc_group_req *mreq)
{
	struct tipc_group *grp;
	u32 type = mreq->type;

	grp = kzalloc(sizeof(*grp), GFP_ATOMIC);
	if (!grp)
		return NULL;
	tipc_nlist_init(&grp->dests, tipc_own_addr(net));
	INIT_LIST_HEAD(&grp->congested);
	grp->members = RB_ROOT;
	grp->net = net;
	grp->portid = portid;
	grp->domain = addr_domain(net, mreq->scope);
	grp->type = type;
	grp->instance = mreq->instance;
	grp->scope = mreq->scope;
	grp->loopback = mreq->flags & TIPC_GROUP_LOOPBACK;
	grp->events = mreq->flags & TIPC_GROUP_MEMBER_EVTS;
	if (tipc_topsrv_kern_subscr(net, portid, type, 0, ~0, &grp->subid))
		return grp;
	kfree(grp);
	return NULL;
}

void tipc_group_delete(struct net *net, struct tipc_group *grp)
{
	struct rb_root *tree = &grp->members;
	struct tipc_member *m, *tmp;
	struct sk_buff_head xmitq;

	__skb_queue_head_init(&xmitq);

	rbtree_postorder_for_each_entry_safe(m, tmp, tree, tree_node) {
		tipc_group_proto_xmit(grp, m, GRP_LEAVE_MSG, &xmitq);
		list_del(&m->list);
		kfree(m);
	}
	tipc_node_distr_xmit(net, &xmitq);
	tipc_nlist_purge(&grp->dests);
	tipc_topsrv_kern_unsubscr(net, grp->subid);
	kfree(grp);
}

struct tipc_member *tipc_group_find_member(struct tipc_group *grp,
					   u32 node, u32 port)
{
	struct rb_node *n = grp->members.rb_node;
	u64 nkey, key = (u64)node << 32 | port;
	struct tipc_member *m;

	while (n) {
		m = container_of(n, struct tipc_member, tree_node);
		nkey = (u64)m->node << 32 | m->port;
		if (key < nkey)
			n = n->rb_left;
		else if (key > nkey)
			n = n->rb_right;
		else
			return m;
	}
	return NULL;
}

static struct tipc_member *tipc_group_find_dest(struct tipc_group *grp,
						u32 node, u32 port)
{
	struct tipc_member *m;

	m = tipc_group_find_member(grp, node, port);
	if (m && tipc_group_is_enabled(m))
		return m;
	return NULL;
}

static struct tipc_member *tipc_group_find_node(struct tipc_group *grp,
						u32 node)
{
	struct tipc_member *m;
	struct rb_node *n;

	for (n = rb_first(&grp->members); n; n = rb_next(n)) {
		m = container_of(n, struct tipc_member, tree_node);
		if (m->node == node)
			return m;
	}
	return NULL;
}

static void tipc_group_add_to_tree(struct tipc_group *grp,
				   struct tipc_member *m)
{
	u64 nkey, key = (u64)m->node << 32 | m->port;
	struct rb_node **n, *parent = NULL;
	struct tipc_member *tmp;

	n = &grp->members.rb_node;
	while (*n) {
		tmp = container_of(*n, struct tipc_member, tree_node);
		parent = *n;
		tmp = container_of(parent, struct tipc_member, tree_node);
		nkey = (u64)tmp->node << 32 | tmp->port;
		if (key < nkey)
			n = &(*n)->rb_left;
		else if (key > nkey)
			n = &(*n)->rb_right;
		else
			return;
	}
	rb_link_node(&m->tree_node, parent, n);
	rb_insert_color(&m->tree_node, &grp->members);
}

static struct tipc_member *tipc_group_create_member(struct tipc_group *grp,
						    u32 node, u32 port,
						    int state)
{
	struct tipc_member *m;

	m = kzalloc(sizeof(*m), GFP_ATOMIC);
	if (!m)
		return NULL;
	INIT_LIST_HEAD(&m->list);
	INIT_LIST_HEAD(&m->congested);
	__skb_queue_head_init(&m->deferredq);
	m->group = grp;
	m->node = node;
	m->port = port;
	grp->member_cnt++;
	tipc_group_add_to_tree(grp, m);
	tipc_nlist_add(&grp->dests, m->node);
	m->state = state;
	return m;
}

void tipc_group_add_member(struct tipc_group *grp, u32 node, u32 port)
{
	tipc_group_create_member(grp, node, port, MBR_DISCOVERED);
}

static void tipc_group_delete_member(struct tipc_group *grp,
				     struct tipc_member *m)
{
	rb_erase(&m->tree_node, &grp->members);
	grp->member_cnt--;
	list_del_init(&m->list);
	list_del_init(&m->congested);

	/* If last member on a node, remove node from dest list */
	if (!tipc_group_find_node(grp, m->node))
		tipc_nlist_del(&grp->dests, m->node);

	kfree(m);
}

struct tipc_nlist *tipc_group_dests(struct tipc_group *grp)
{
	return &grp->dests;
}

void tipc_group_self(struct tipc_group *grp, struct tipc_name_seq *seq,
		     int *scope)
{
	seq->type = grp->type;
	seq->lower = grp->instance;
	seq->upper = grp->instance;
	*scope = grp->scope;
}

void tipc_group_update_member(struct tipc_member *m, int len)
{
	struct tipc_group *grp = m->group;
	struct tipc_member *_m, *tmp;

	if (!tipc_group_is_enabled(m))
		return;

	m->window -= len;

	if (m->window >= ADV_IDLE)
		return;

	if (!list_empty(&m->congested))
		return;

	/* Sort member into congested members' list */
	list_for_each_entry_safe(_m, tmp, &grp->congested, congested) {
		if (m->window > _m->window)
			continue;
		list_add_tail(&m->congested, &_m->congested);
		return;
	}
	list_add_tail(&m->congested, &grp->congested);
}

void tipc_group_update_bc_members(struct tipc_group *grp, int len)
{
	struct tipc_member *m;
	struct rb_node *n;

	for (n = rb_first(&grp->members); n; n = rb_next(n)) {
		m = container_of(n, struct tipc_member, tree_node);
		if (tipc_group_is_enabled(m))
			tipc_group_update_member(m, len);
	}
	grp->bc_snd_nxt++;
}

bool tipc_group_cong(struct tipc_group *grp, u32 dnode, u32 dport,
		     int len, struct tipc_member **mbr)
{
	struct sk_buff_head xmitq;
	struct tipc_member *m;
	int adv, state;

	m = tipc_group_find_dest(grp, dnode, dport);
	*mbr = m;
	if (!m)
		return false;
	if (m->usr_pending)
		return true;
	if (m->window >= len)
		return false;
	m->usr_pending = true;

	/* If not fully advertised, do it now to prevent mutual blocking */
	adv = m->advertised;
	state = m->state;
	if (state < MBR_JOINED)
		return true;
	if (state == MBR_JOINED && adv == ADV_IDLE)
		return true;
	skb_queue_head_init(&xmitq);
	tipc_group_proto_xmit(grp, m, GRP_ADV_MSG, &xmitq);
	tipc_node_distr_xmit(grp->net, &xmitq);
	return true;
}

bool tipc_group_bc_cong(struct tipc_group *grp, int len)
{
	struct tipc_member *m = NULL;

	if (list_empty(&grp->congested))
		return false;

	m = list_first_entry(&grp->congested, struct tipc_member, congested);
	if (m->window >= len)
		return false;

	return tipc_group_cong(grp, m->node, m->port, len, &m);
}

/* tipc_group_sort_msg() - sort msg into queue by bcast sequence number
 */
static void tipc_group_sort_msg(struct sk_buff *skb, struct sk_buff_head *defq)
{
	struct tipc_msg *_hdr, *hdr = buf_msg(skb);
	u16 bc_seqno = msg_grp_bc_seqno(hdr);
	struct sk_buff *_skb, *tmp;
	int mtyp = msg_type(hdr);

	/* Bcast may be bypassed by unicast, - sort it in */
	if (mtyp == TIPC_GRP_BCAST_MSG || mtyp == TIPC_GRP_MCAST_MSG) {
		skb_queue_walk_safe(defq, _skb, tmp) {
			_hdr = buf_msg(_skb);
			if (!less(bc_seqno, msg_grp_bc_seqno(_hdr)))
				continue;
			__skb_queue_before(defq, _skb, skb);
			return;
		}
		/* Bcast was not bypassed, - add to tail */
	}
	/* Unicasts are never bypassed, - always add to tail */
	__skb_queue_tail(defq, skb);
}

/* tipc_group_filter_msg() - determine if we should accept arriving message
 */
void tipc_group_filter_msg(struct tipc_group *grp, struct sk_buff_head *inputq,
			   struct sk_buff_head *xmitq)
{
	struct sk_buff *skb = __skb_dequeue(inputq);
	struct sk_buff_head *defq;
	struct tipc_member *m;
	struct tipc_msg *hdr;
	bool deliver, update;
	u32 node, port;
	int mtyp, blks;

	if (!skb)
		return;

	hdr = buf_msg(skb);
	node =  msg_orignode(hdr);
	port = msg_origport(hdr);

	if (!msg_in_group(hdr))
		goto drop;

	if (msg_is_grp_evt(hdr)) {
		if (!grp->events)
			goto drop;
		__skb_queue_tail(inputq, skb);
		return;
	}

	m = tipc_group_find_member(grp, node, port);
	if (!tipc_group_is_receiver(m))
		goto drop;

	if (less(msg_grp_bc_seqno(hdr), m->bc_rcv_nxt))
		goto drop;

	TIPC_SKB_CB(skb)->orig_member = m->instance;
	defq = &m->deferredq;
	tipc_group_sort_msg(skb, defq);

	while ((skb = skb_peek(defq))) {
		hdr = buf_msg(skb);
		mtyp = msg_type(hdr);
		deliver = true;
		update = false;

		if (more(msg_grp_bc_seqno(hdr), m->bc_rcv_nxt))
			break;

		/* Decide what to do with message */
		switch (mtyp) {
		case TIPC_GRP_MCAST_MSG:
			if (msg_nameinst(hdr) != grp->instance) {
				update = true;
				deliver = false;
			}
			/* Fall thru */
		case TIPC_GRP_BCAST_MSG:
			m->bc_rcv_nxt++;
			break;
		case TIPC_GRP_UCAST_MSG:
			break;
		default:
			break;
		}

		/* Execute decisions */
		__skb_dequeue(defq);
		if (deliver)
			__skb_queue_tail(inputq, skb);
		else
			kfree_skb(skb);

		if (!update)
			continue;

		blks = msg_blocks(hdr);
		tipc_group_update_rcv_win(grp, blks, node, port, xmitq);
	}
	return;
drop:
	kfree_skb(skb);
}

void tipc_group_update_rcv_win(struct tipc_group *grp, int blks, u32 node,
			       u32 port, struct sk_buff_head *xmitq)
{
	struct tipc_member *m;

	m = tipc_group_find_member(grp, node, port);
	if (!m)
		return;

	m->advertised -= blks;

	switch (m->state) {
	case MBR_JOINED:
		if (m->advertised <= (ADV_ACTIVE - ADV_UNIT))
			tipc_group_proto_xmit(grp, m, GRP_ADV_MSG, xmitq);
		break;
	case MBR_DISCOVERED:
	case MBR_JOINING:
	case MBR_LEAVING:
	default:
		break;
	}
}

static void tipc_group_proto_xmit(struct tipc_group *grp, struct tipc_member *m,
				  int mtyp, struct sk_buff_head *xmitq)
{
	struct tipc_msg *hdr;
	struct sk_buff *skb;
	int adv = 0;

	skb = tipc_msg_create(GROUP_PROTOCOL, mtyp, INT_H_SIZE, 0,
			      m->node, tipc_own_addr(grp->net),
			      m->port, grp->portid, 0);
	if (!skb)
		return;

	if (m->state == MBR_JOINED)
		adv = ADV_ACTIVE - m->advertised;

	hdr = buf_msg(skb);

	if (mtyp == GRP_JOIN_MSG) {
		msg_set_grp_bc_syncpt(hdr, grp->bc_snd_nxt);
		msg_set_adv_win(hdr, adv);
		m->advertised += adv;
	} else if (mtyp == GRP_ADV_MSG) {
		msg_set_adv_win(hdr, adv);
		m->advertised += adv;
	}
	__skb_queue_tail(xmitq, skb);
}

void tipc_group_proto_rcv(struct tipc_group *grp, bool *usr_wakeup,
			  struct tipc_msg *hdr, struct sk_buff_head *inputq,
			  struct sk_buff_head *xmitq)
{
	u32 node = msg_orignode(hdr);
	u32 port = msg_origport(hdr);
	struct tipc_member *m;

	if (!grp)
		return;

	m = tipc_group_find_member(grp, node, port);

	switch (msg_type(hdr)) {
	case GRP_JOIN_MSG:
		if (!m)
			m = tipc_group_create_member(grp, node, port,
						     MBR_QUARANTINED);
		if (!m)
			return;
		m->bc_rcv_nxt = msg_grp_bc_syncpt(hdr);
		m->window += msg_adv_win(hdr);

		/* Wait until PUBLISH event is received */
		if (m->state == MBR_DISCOVERED) {
			m->state = MBR_JOINING;
		} else if (m->state == MBR_PUBLISHED) {
			m->state = MBR_JOINED;
			*usr_wakeup = true;
			m->usr_pending = false;
			tipc_group_proto_xmit(grp, m, GRP_ADV_MSG, xmitq);
			__skb_queue_tail(inputq, m->event_msg);
		}
		if (m->window < ADV_IDLE)
			tipc_group_update_member(m, 0);
		else
			list_del_init(&m->congested);
		return;
	case GRP_LEAVE_MSG:
		if (!m)
			return;

		/* Wait until WITHDRAW event is received */
		if (m->state != MBR_LEAVING) {
			m->state = MBR_LEAVING;
			return;
		}
		/* Otherwise deliver already received WITHDRAW event */
		__skb_queue_tail(inputq, m->event_msg);
		*usr_wakeup = m->usr_pending;
		tipc_group_delete_member(grp, m);
		list_del_init(&m->congested);
		return;
	case GRP_ADV_MSG:
		if (!m)
			return;
		m->window += msg_adv_win(hdr);
		*usr_wakeup = m->usr_pending;
		m->usr_pending = false;
		list_del_init(&m->congested);
		return;
	default:
		pr_warn("Received unknown GROUP_PROTO message\n");
	}
}

/* tipc_group_member_evt() - receive and handle a member up/down event
 */
void tipc_group_member_evt(struct tipc_group *grp,
			   bool *usr_wakeup,
			   int *sk_rcvbuf,
			   struct sk_buff *skb,
			   struct sk_buff_head *inputq,
			   struct sk_buff_head *xmitq)
{
	struct tipc_msg *hdr = buf_msg(skb);
	struct tipc_event *evt = (void *)msg_data(hdr);
	u32 instance = evt->found_lower;
	u32 node = evt->port.node;
	u32 port = evt->port.ref;
	int event = evt->event;
	struct tipc_member *m;
	struct net *net;
	u32 self;

	if (!grp)
		goto drop;

	net = grp->net;
	self = tipc_own_addr(net);
	if (!grp->loopback && node == self && port == grp->portid)
		goto drop;

	/* Convert message before delivery to user */
	msg_set_hdr_sz(hdr, GROUP_H_SIZE);
	msg_set_user(hdr, TIPC_CRITICAL_IMPORTANCE);
	msg_set_type(hdr, TIPC_GRP_MEMBER_EVT);
	msg_set_origport(hdr, port);
	msg_set_orignode(hdr, node);
	msg_set_nametype(hdr, grp->type);
	msg_set_grp_evt(hdr, event);

	m = tipc_group_find_member(grp, node, port);

	if (event == TIPC_PUBLISHED) {
		if (!m)
			m = tipc_group_create_member(grp, node, port,
						     MBR_DISCOVERED);
		if (!m)
			goto drop;

		/* Hold back event if JOIN message not yet received */
		if (m->state == MBR_DISCOVERED) {
			m->event_msg = skb;
			m->state = MBR_PUBLISHED;
		} else {
			__skb_queue_tail(inputq, skb);
			m->state = MBR_JOINED;
			*usr_wakeup = true;
			m->usr_pending = false;
		}
		m->instance = instance;
		TIPC_SKB_CB(skb)->orig_member = m->instance;
		tipc_group_proto_xmit(grp, m, GRP_JOIN_MSG, xmitq);
		if (m->window < ADV_IDLE)
			tipc_group_update_member(m, 0);
		else
			list_del_init(&m->congested);
	} else if (event == TIPC_WITHDRAWN) {
		if (!m)
			goto drop;

		TIPC_SKB_CB(skb)->orig_member = m->instance;

		*usr_wakeup = m->usr_pending;
		m->usr_pending = false;

		/* Hold back event if more messages might be expected */
		if (m->state != MBR_LEAVING && tipc_node_is_up(net, node)) {
			m->event_msg = skb;
			m->state = MBR_LEAVING;
		} else {
			__skb_queue_tail(inputq, skb);
			tipc_group_delete_member(grp, m);
		}
		list_del_init(&m->congested);
	}
	*sk_rcvbuf = tipc_group_rcvbuf_limit(grp);
	return;
drop:
	kfree_skb(skb);
}
