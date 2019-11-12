/*
 * net/tipc/monitor.c
 *
 * Copyright (c) 2016, Ericsson AB
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

#include <net/genetlink.h>
#include "core.h"
#include "addr.h"
#include "monitor.h"
#include "bearer.h"

#define MAX_MON_DOMAIN       64
#define MON_TIMEOUT          120000
#define MAX_PEER_DOWN_EVENTS 4

/* struct tipc_mon_domain: domain record to be transferred between peers
 * @len: actual size of domain record
 * @gen: current generation of sender's domain
 * @ack_gen: most recent generation of self's domain acked by peer
 * @member_cnt: number of domain member nodes described in this record
 * @up_map: bit map indicating which of the members the sender considers up
 * @members: identity of the domain members
 */
struct tipc_mon_domain {
	u16 len;
	u16 gen;
	u16 ack_gen;
	u16 member_cnt;
	u64 up_map;
	u32 members[MAX_MON_DOMAIN];
};

/* struct tipc_peer: state of a peer node and its domain
 * @addr: tipc node identity of peer
 * @head_map: shows which other nodes currently consider peer 'up'
 * @domain: most recent domain record from peer
 * @hash: position in hashed lookup list
 * @list: position in linked list, in circular ascending order by 'addr'
 * @applied: number of reported domain members applied on this monitor list
 * @is_up: peer is up as seen from this node
 * @is_head: peer is assigned domain head as seen from this node
 * @is_local: peer is in local domain and should be continuously monitored
 * @down_cnt: - numbers of other peers which have reported this on lost
 */
struct tipc_peer {
	u32 addr;
	struct tipc_mon_domain *domain;
	struct hlist_node hash;
	struct list_head list;
	u8 applied;
	u8 down_cnt;
	bool is_up;
	bool is_head;
	bool is_local;
};

struct tipc_monitor {
	struct hlist_head peers[NODE_HTABLE_SIZE];
	int peer_cnt;
	struct tipc_peer *self;
	rwlock_t lock;
	struct tipc_mon_domain cache;
	u16 list_gen;
	u16 dom_gen;
	struct net *net;
	struct timer_list timer;
	unsigned long timer_intv;
};

static struct tipc_monitor *tipc_monitor(struct net *net, int bearer_id)
{
	return tipc_net(net)->monitors[bearer_id];
}

const int tipc_max_domain_size = sizeof(struct tipc_mon_domain);

/* dom_rec_len(): actual length of domain record for transport
 */
static int dom_rec_len(struct tipc_mon_domain *dom, u16 mcnt)
{
	return ((void *)&dom->members - (void *)dom) + (mcnt * sizeof(u32));
}

/* dom_size() : calculate size of own domain based on number of peers
 */
static int dom_size(int peers)
{
	int i = 0;

	while ((i * i) < peers)
		i++;
	return i < MAX_MON_DOMAIN ? i : MAX_MON_DOMAIN;
}

static void map_set(u64 *up_map, int i, unsigned int v)
{
	*up_map &= ~(1ULL << i);
	*up_map |= ((u64)v << i);
}

static int map_get(u64 up_map, int i)
{
	return (up_map & (1 << i)) >> i;
}

static struct tipc_peer *peer_prev(struct tipc_peer *peer)
{
	return list_last_entry(&peer->list, struct tipc_peer, list);
}

static struct tipc_peer *peer_nxt(struct tipc_peer *peer)
{
	return list_first_entry(&peer->list, struct tipc_peer, list);
}

static struct tipc_peer *peer_head(struct tipc_peer *peer)
{
	while (!peer->is_head)
		peer = peer_prev(peer);
	return peer;
}

static struct tipc_peer *get_peer(struct tipc_monitor *mon, u32 addr)
{
	struct tipc_peer *peer;
	unsigned int thash = tipc_hashfn(addr);

	hlist_for_each_entry(peer, &mon->peers[thash], hash) {
		if (peer->addr == addr)
			return peer;
	}
	return NULL;
}

static struct tipc_peer *get_self(struct net *net, int bearer_id)
{
	struct tipc_monitor *mon = tipc_monitor(net, bearer_id);

	return mon->self;
}

static inline bool tipc_mon_is_active(struct net *net, struct tipc_monitor *mon)
{
	struct tipc_net *tn = tipc_net(net);

	return mon->peer_cnt > tn->mon_threshold;
}

/* mon_identify_lost_members() : - identify amd mark potentially lost members
 */
static void mon_identify_lost_members(struct tipc_peer *peer,
				      struct tipc_mon_domain *dom_bef,
				      int applied_bef)
{
	struct tipc_peer *member = peer;
	struct tipc_mon_domain *dom_aft = peer->domain;
	int applied_aft = peer->applied;
	int i;

	for (i = 0; i < applied_bef; i++) {
		member = peer_nxt(member);

		/* Do nothing if self or peer already see member as down */
		if (!member->is_up || !map_get(dom_bef->up_map, i))
			continue;

		/* Loss of local node must be detected by active probing */
		if (member->is_local)
			continue;

		/* Start probing if member was removed from applied domain */
		if (!applied_aft || (applied_aft < i)) {
			member->down_cnt = 1;
			continue;
		}

		/* Member loss is confirmed if it is still in applied domain */
		if (!map_get(dom_aft->up_map, i))
			member->down_cnt++;
	}
}

/* mon_apply_domain() : match a peer's domain record against monitor list
 */
static void mon_apply_domain(struct tipc_monitor *mon,
			     struct tipc_peer *peer)
{
	struct tipc_mon_domain *dom = peer->domain;
	struct tipc_peer *member;
	u32 addr;
	int i;

	if (!dom || !peer->is_up)
		return;

	/* Scan across domain members and match against monitor list */
	peer->applied = 0;
	member = peer_nxt(peer);
	for (i = 0; i < dom->member_cnt; i++) {
		addr = dom->members[i];
		if (addr != member->addr)
			return;
		peer->applied++;
		member = peer_nxt(member);
	}
}

/* mon_update_local_domain() : update after peer addition/removal/up/down
 */
static void mon_update_local_domain(struct tipc_monitor *mon)
{
	struct tipc_peer *self = mon->self;
	struct tipc_mon_domain *cache = &mon->cache;
	struct tipc_mon_domain *dom = self->domain;
	struct tipc_peer *peer = self;
	u64 prev_up_map = dom->up_map;
	u16 member_cnt, i;
	bool diff;

	/* Update local domain size based on current size of cluster */
	member_cnt = dom_size(mon->peer_cnt) - 1;
	self->applied = member_cnt;

	/* Update native and cached outgoing local domain records */
	dom->len = dom_rec_len(dom, member_cnt);
	diff = dom->member_cnt != member_cnt;
	dom->member_cnt = member_cnt;
	for (i = 0; i < member_cnt; i++) {
		peer = peer_nxt(peer);
		diff |= dom->members[i] != peer->addr;
		dom->members[i] = peer->addr;
		map_set(&dom->up_map, i, peer->is_up);
		cache->members[i] = htonl(peer->addr);
	}
	diff |= dom->up_map != prev_up_map;
	if (!diff)
		return;
	dom->gen = ++mon->dom_gen;
	cache->len = htons(dom->len);
	cache->gen = htons(dom->gen);
	cache->member_cnt = htons(member_cnt);
	cache->up_map = cpu_to_be64(dom->up_map);
	mon_apply_domain(mon, self);
}

/* mon_update_neighbors() : update preceding neighbors of added/removed peer
 */
static void mon_update_neighbors(struct tipc_monitor *mon,
				 struct tipc_peer *peer)
{
	int dz, i;

	dz = dom_size(mon->peer_cnt);
	for (i = 0; i < dz; i++) {
		mon_apply_domain(mon, peer);
		peer = peer_prev(peer);
	}
}

/* mon_assign_roles() : reassign peer roles after a network change
 * The monitor list is consistent at this stage; i.e., each peer is monitoring
 * a set of domain members as matched between domain record and the monitor list
 */
static void mon_assign_roles(struct tipc_monitor *mon, struct tipc_peer *head)
{
	struct tipc_peer *peer = peer_nxt(head);
	struct tipc_peer *self = mon->self;
	int i = 0;

	for (; peer != self; peer = peer_nxt(peer)) {
		peer->is_local = false;

		/* Update domain member */
		if (i++ < head->applied) {
			peer->is_head = false;
			if (head == self)
				peer->is_local = true;
			continue;
		}
		/* Assign next domain head */
		if (!peer->is_up)
			continue;
		if (peer->is_head)
			break;
		head = peer;
		head->is_head = true;
		i = 0;
	}
	mon->list_gen++;
}

void tipc_mon_remove_peer(struct net *net, u32 addr, int bearer_id)
{
	struct tipc_monitor *mon = tipc_monitor(net, bearer_id);
	struct tipc_peer *self = get_self(net, bearer_id);
	struct tipc_peer *peer, *prev, *head;

	write_lock_bh(&mon->lock);
	peer = get_peer(mon, addr);
	if (!peer)
		goto exit;
	prev = peer_prev(peer);
	list_del(&peer->list);
	hlist_del(&peer->hash);
	kfree(peer->domain);
	kfree(peer);
	mon->peer_cnt--;
	head = peer_head(prev);
	if (head == self)
		mon_update_local_domain(mon);
	mon_update_neighbors(mon, prev);

	/* Revert to full-mesh monitoring if we reach threshold */
	if (!tipc_mon_is_active(net, mon)) {
		list_for_each_entry(peer, &self->list, list) {
			kfree(peer->domain);
			peer->domain = NULL;
			peer->applied = 0;
		}
	}
	mon_assign_roles(mon, head);
exit:
	write_unlock_bh(&mon->lock);
}

static bool tipc_mon_add_peer(struct tipc_monitor *mon, u32 addr,
			      struct tipc_peer **peer)
{
	struct tipc_peer *self = mon->self;
	struct tipc_peer *cur, *prev, *p;

	p = kzalloc(sizeof(*p), GFP_ATOMIC);
	*peer = p;
	if (!p)
		return false;
	p->addr = addr;

	/* Add new peer to lookup list */
	INIT_LIST_HEAD(&p->list);
	hlist_add_head(&p->hash, &mon->peers[tipc_hashfn(addr)]);

	/* Sort new peer into iterator list, in ascending circular order */
	prev = self;
	list_for_each_entry(cur, &self->list, list) {
		if ((addr > prev->addr) && (addr < cur->addr))
			break;
		if (((addr < cur->addr) || (addr > prev->addr)) &&
		    (prev->addr > cur->addr))
			break;
		prev = cur;
	}
	list_add_tail(&p->list, &cur->list);
	mon->peer_cnt++;
	mon_update_neighbors(mon, p);
	return true;
}

void tipc_mon_peer_up(struct net *net, u32 addr, int bearer_id)
{
	struct tipc_monitor *mon = tipc_monitor(net, bearer_id);
	struct tipc_peer *self = get_self(net, bearer_id);
	struct tipc_peer *peer, *head;

	write_lock_bh(&mon->lock);
	peer = get_peer(mon, addr);
	if (!peer && !tipc_mon_add_peer(mon, addr, &peer))
		goto exit;
	peer->is_up = true;
	head = peer_head(peer);
	if (head == self)
		mon_update_local_domain(mon);
	mon_assign_roles(mon, head);
exit:
	write_unlock_bh(&mon->lock);
}

void tipc_mon_peer_down(struct net *net, u32 addr, int bearer_id)
{
	struct tipc_monitor *mon = tipc_monitor(net, bearer_id);
	struct tipc_peer *self = get_self(net, bearer_id);
	struct tipc_peer *peer, *head;
	struct tipc_mon_domain *dom;
	int applied;

	write_lock_bh(&mon->lock);
	peer = get_peer(mon, addr);
	if (!peer) {
		pr_warn("Mon: unknown link %x/%u DOWN\n", addr, bearer_id);
		goto exit;
	}
	applied = peer->applied;
	peer->applied = 0;
	dom = peer->domain;
	peer->domain = NULL;
	if (peer->is_head)
		mon_identify_lost_members(peer, dom, applied);
	kfree(dom);
	peer->is_up = false;
	peer->is_head = false;
	peer->is_local = false;
	peer->down_cnt = 0;
	head = peer_head(peer);
	if (head == self)
		mon_update_local_domain(mon);
	mon_assign_roles(mon, head);
exit:
	write_unlock_bh(&mon->lock);
}

/* tipc_mon_rcv - process monitor domain event message
 */
void tipc_mon_rcv(struct net *net, void *data, u16 dlen, u32 addr,
		  struct tipc_mon_state *state, int bearer_id)
{
	struct tipc_monitor *mon = tipc_monitor(net, bearer_id);
	struct tipc_mon_domain *arrv_dom = data;
	struct tipc_mon_domain dom_bef;
	struct tipc_mon_domain *dom;
	struct tipc_peer *peer;
	u16 new_member_cnt = ntohs(arrv_dom->member_cnt);
	int new_dlen = dom_rec_len(arrv_dom, new_member_cnt);
	u16 new_gen = ntohs(arrv_dom->gen);
	u16 acked_gen = ntohs(arrv_dom->ack_gen);
	bool probing = state->probing;
	int i, applied_bef;

	state->probing = false;

	/* Sanity check received domain record */
	if (dlen < dom_rec_len(arrv_dom, 0))
		return;
	if (dlen != dom_rec_len(arrv_dom, new_member_cnt))
		return;
	if ((dlen < new_dlen) || ntohs(arrv_dom->len) != new_dlen)
		return;

	/* Synch generation numbers with peer if link just came up */
	if (!state->synched) {
		state->peer_gen = new_gen - 1;
		state->acked_gen = acked_gen;
		state->synched = true;
	}

	if (more(acked_gen, state->acked_gen))
		state->acked_gen = acked_gen;

	/* Drop duplicate unless we are waiting for a probe response */
	if (!more(new_gen, state->peer_gen) && !probing)
		return;

	write_lock_bh(&mon->lock);
	peer = get_peer(mon, addr);
	if (!peer || !peer->is_up)
		goto exit;

	/* Peer is confirmed, stop any ongoing probing */
	peer->down_cnt = 0;

	/* Task is done for duplicate record */
	if (!more(new_gen, state->peer_gen))
		goto exit;

	state->peer_gen = new_gen;

	/* Cache current domain record for later use */
	dom_bef.member_cnt = 0;
	dom = peer->domain;
	if (dom)
		memcpy(&dom_bef, dom, dom->len);

	/* Transform and store received domain record */
	if (!dom || (dom->len < new_dlen)) {
		kfree(dom);
		dom = kmalloc(new_dlen, GFP_ATOMIC);
		peer->domain = dom;
		if (!dom)
			goto exit;
	}
	dom->len = new_dlen;
	dom->gen = new_gen;
	dom->member_cnt = new_member_cnt;
	dom->up_map = be64_to_cpu(arrv_dom->up_map);
	for (i = 0; i < new_member_cnt; i++)
		dom->members[i] = ntohl(arrv_dom->members[i]);

	/* Update peers affected by this domain record */
	applied_bef = peer->applied;
	mon_apply_domain(mon, peer);
	mon_identify_lost_members(peer, &dom_bef, applied_bef);
	mon_assign_roles(mon, peer_head(peer));
exit:
	write_unlock_bh(&mon->lock);
}

void tipc_mon_prep(struct net *net, void *data, int *dlen,
		   struct tipc_mon_state *state, int bearer_id)
{
	struct tipc_monitor *mon = tipc_monitor(net, bearer_id);
	struct tipc_mon_domain *dom = data;
	u16 gen = mon->dom_gen;
	u16 len;

	/* Send invalid record if not active */
	if (!tipc_mon_is_active(net, mon)) {
		dom->len = 0;
		return;
	}

	/* Send only a dummy record with ack if peer has acked our last sent */
	if (likely(state->acked_gen == gen)) {
		len = dom_rec_len(dom, 0);
		*dlen = len;
		dom->len = htons(len);
		dom->gen = htons(gen);
		dom->ack_gen = htons(state->peer_gen);
		dom->member_cnt = 0;
		return;
	}
	/* Send the full record */
	read_lock_bh(&mon->lock);
	len = ntohs(mon->cache.len);
	*dlen = len;
	memcpy(data, &mon->cache, len);
	read_unlock_bh(&mon->lock);
	dom->ack_gen = htons(state->peer_gen);
}

void tipc_mon_get_state(struct net *net, u32 addr,
			struct tipc_mon_state *state,
			int bearer_id)
{
	struct tipc_monitor *mon = tipc_monitor(net, bearer_id);
	struct tipc_peer *peer;

	if (!tipc_mon_is_active(net, mon)) {
		state->probing = false;
		state->monitoring = true;
		return;
	}

	/* Used cached state if table has not changed */
	if (!state->probing &&
	    (state->list_gen == mon->list_gen) &&
	    (state->acked_gen == mon->dom_gen))
		return;

	read_lock_bh(&mon->lock);
	peer = get_peer(mon, addr);
	if (peer) {
		state->probing = state->acked_gen != mon->dom_gen;
		state->probing |= peer->down_cnt;
		state->reset |= peer->down_cnt >= MAX_PEER_DOWN_EVENTS;
		state->monitoring = peer->is_local;
		state->monitoring |= peer->is_head;
		state->list_gen = mon->list_gen;
	}
	read_unlock_bh(&mon->lock);
}

static void mon_timeout(struct timer_list *t)
{
	struct tipc_monitor *mon = from_timer(mon, t, timer);
	struct tipc_peer *self;
	int best_member_cnt = dom_size(mon->peer_cnt) - 1;

	write_lock_bh(&mon->lock);
	self = mon->self;
	if (self && (best_member_cnt != self->applied)) {
		mon_update_local_domain(mon);
		mon_assign_roles(mon, self);
	}
	write_unlock_bh(&mon->lock);
	mod_timer(&mon->timer, jiffies + mon->timer_intv);
}

int tipc_mon_create(struct net *net, int bearer_id)
{
	struct tipc_net *tn = tipc_net(net);
	struct tipc_monitor *mon;
	struct tipc_peer *self;
	struct tipc_mon_domain *dom;

	if (tn->monitors[bearer_id])
		return 0;

	mon = kzalloc(sizeof(*mon), GFP_ATOMIC);
	self = kzalloc(sizeof(*self), GFP_ATOMIC);
	dom = kzalloc(sizeof(*dom), GFP_ATOMIC);
	if (!mon || !self || !dom) {
		kfree(mon);
		kfree(self);
		kfree(dom);
		return -ENOMEM;
	}
	tn->monitors[bearer_id] = mon;
	rwlock_init(&mon->lock);
	mon->net = net;
	mon->peer_cnt = 1;
	mon->self = self;
	self->domain = dom;
	self->addr = tipc_own_addr(net);
	self->is_up = true;
	self->is_head = true;
	INIT_LIST_HEAD(&self->list);
	timer_setup(&mon->timer, mon_timeout, 0);
	mon->timer_intv = msecs_to_jiffies(MON_TIMEOUT + (tn->random & 0xffff));
	mod_timer(&mon->timer, jiffies + mon->timer_intv);
	return 0;
}

void tipc_mon_delete(struct net *net, int bearer_id)
{
	struct tipc_net *tn = tipc_net(net);
	struct tipc_monitor *mon = tipc_monitor(net, bearer_id);
	struct tipc_peer *self;
	struct tipc_peer *peer, *tmp;

	if (!mon)
		return;

	self = get_self(net, bearer_id);
	write_lock_bh(&mon->lock);
	tn->monitors[bearer_id] = NULL;
	list_for_each_entry_safe(peer, tmp, &self->list, list) {
		list_del(&peer->list);
		hlist_del(&peer->hash);
		kfree(peer->domain);
		kfree(peer);
	}
	mon->self = NULL;
	write_unlock_bh(&mon->lock);
	del_timer_sync(&mon->timer);
	kfree(self->domain);
	kfree(self);
	kfree(mon);
}

void tipc_mon_reinit_self(struct net *net)
{
	struct tipc_monitor *mon;
	int bearer_id;

	for (bearer_id = 0; bearer_id < MAX_BEARERS; bearer_id++) {
		mon = tipc_monitor(net, bearer_id);
		if (!mon)
			continue;
		write_lock_bh(&mon->lock);
		mon->self->addr = tipc_own_addr(net);
		write_unlock_bh(&mon->lock);
	}
}

int tipc_nl_monitor_set_threshold(struct net *net, u32 cluster_size)
{
	struct tipc_net *tn = tipc_net(net);

	if (cluster_size > TIPC_CLUSTER_SIZE)
		return -EINVAL;

	tn->mon_threshold = cluster_size;

	return 0;
}

int tipc_nl_monitor_get_threshold(struct net *net)
{
	struct tipc_net *tn = tipc_net(net);

	return tn->mon_threshold;
}

static int __tipc_nl_add_monitor_peer(struct tipc_peer *peer,
				      struct tipc_nl_msg *msg)
{
	struct tipc_mon_domain *dom = peer->domain;
	struct nlattr *attrs;
	void *hdr;

	hdr = genlmsg_put(msg->skb, msg->portid, msg->seq, &tipc_genl_family,
			  NLM_F_MULTI, TIPC_NL_MON_PEER_GET);
	if (!hdr)
		return -EMSGSIZE;

	attrs = nla_nest_start(msg->skb, TIPC_NLA_MON_PEER);
	if (!attrs)
		goto msg_full;

	if (nla_put_u32(msg->skb, TIPC_NLA_MON_PEER_ADDR, peer->addr))
		goto attr_msg_full;
	if (nla_put_u32(msg->skb, TIPC_NLA_MON_PEER_APPLIED, peer->applied))
		goto attr_msg_full;

	if (peer->is_up)
		if (nla_put_flag(msg->skb, TIPC_NLA_MON_PEER_UP))
			goto attr_msg_full;
	if (peer->is_local)
		if (nla_put_flag(msg->skb, TIPC_NLA_MON_PEER_LOCAL))
			goto attr_msg_full;
	if (peer->is_head)
		if (nla_put_flag(msg->skb, TIPC_NLA_MON_PEER_HEAD))
			goto attr_msg_full;

	if (dom) {
		if (nla_put_u32(msg->skb, TIPC_NLA_MON_PEER_DOMGEN, dom->gen))
			goto attr_msg_full;
		if (nla_put_u64_64bit(msg->skb, TIPC_NLA_MON_PEER_UPMAP,
				      dom->up_map, TIPC_NLA_MON_PEER_PAD))
			goto attr_msg_full;
		if (nla_put(msg->skb, TIPC_NLA_MON_PEER_MEMBERS,
			    dom->member_cnt * sizeof(u32), &dom->members))
			goto attr_msg_full;
	}

	nla_nest_end(msg->skb, attrs);
	genlmsg_end(msg->skb, hdr);
	return 0;

attr_msg_full:
	nla_nest_cancel(msg->skb, attrs);
msg_full:
	genlmsg_cancel(msg->skb, hdr);

	return -EMSGSIZE;
}

int tipc_nl_add_monitor_peer(struct net *net, struct tipc_nl_msg *msg,
			     u32 bearer_id, u32 *prev_node)
{
	struct tipc_monitor *mon = tipc_monitor(net, bearer_id);
	struct tipc_peer *peer;

	if (!mon)
		return -EINVAL;

	read_lock_bh(&mon->lock);
	peer = mon->self;
	do {
		if (*prev_node) {
			if (peer->addr == *prev_node)
				*prev_node = 0;
			else
				continue;
		}
		if (__tipc_nl_add_monitor_peer(peer, msg)) {
			*prev_node = peer->addr;
			read_unlock_bh(&mon->lock);
			return -EMSGSIZE;
		}
	} while ((peer = peer_nxt(peer)) != mon->self);
	read_unlock_bh(&mon->lock);

	return 0;
}

int __tipc_nl_add_monitor(struct net *net, struct tipc_nl_msg *msg,
			  u32 bearer_id)
{
	struct tipc_monitor *mon = tipc_monitor(net, bearer_id);
	char bearer_name[TIPC_MAX_BEARER_NAME];
	struct nlattr *attrs;
	void *hdr;
	int ret;

	ret = tipc_bearer_get_name(net, bearer_name, bearer_id);
	if (ret || !mon)
		return 0;

	hdr = genlmsg_put(msg->skb, msg->portid, msg->seq, &tipc_genl_family,
			  NLM_F_MULTI, TIPC_NL_MON_GET);
	if (!hdr)
		return -EMSGSIZE;

	attrs = nla_nest_start(msg->skb, TIPC_NLA_MON);
	if (!attrs)
		goto msg_full;

	read_lock_bh(&mon->lock);
	if (nla_put_u32(msg->skb, TIPC_NLA_MON_REF, bearer_id))
		goto attr_msg_full;
	if (tipc_mon_is_active(net, mon))
		if (nla_put_flag(msg->skb, TIPC_NLA_MON_ACTIVE))
			goto attr_msg_full;
	if (nla_put_string(msg->skb, TIPC_NLA_MON_BEARER_NAME, bearer_name))
		goto attr_msg_full;
	if (nla_put_u32(msg->skb, TIPC_NLA_MON_PEERCNT, mon->peer_cnt))
		goto attr_msg_full;
	if (nla_put_u32(msg->skb, TIPC_NLA_MON_LISTGEN, mon->list_gen))
		goto attr_msg_full;

	read_unlock_bh(&mon->lock);
	nla_nest_end(msg->skb, attrs);
	genlmsg_end(msg->skb, hdr);

	return 0;

attr_msg_full:
	read_unlock_bh(&mon->lock);
	nla_nest_cancel(msg->skb, attrs);
msg_full:
	genlmsg_cancel(msg->skb, hdr);

	return -EMSGSIZE;
}
