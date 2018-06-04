/*
 * net/tipc/name_table.c: TIPC name table code
 *
 * Copyright (c) 2000-2006, 2014-2018, Ericsson AB
 * Copyright (c) 2004-2008, 2010-2014, Wind River Systems
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

#include <net/sock.h>
#include "core.h"
#include "netlink.h"
#include "name_table.h"
#include "name_distr.h"
#include "subscr.h"
#include "bcast.h"
#include "addr.h"
#include "node.h"
#include "group.h"

/**
 * struct service_range - container for all bindings of a service range
 * @lower: service range lower bound
 * @upper: service range upper bound
 * @tree_node: member of service range RB tree
 * @local_publ: list of identical publications made from this node
 *   Used by closest_first lookup and multicast lookup algorithm
 * @all_publ: all publications identical to this one, whatever node and scope
 *   Used by round-robin lookup algorithm
 */
struct service_range {
	u32 lower;
	u32 upper;
	struct rb_node tree_node;
	struct list_head local_publ;
	struct list_head all_publ;
};

/**
 * struct tipc_service - container for all published instances of a service type
 * @type: 32 bit 'type' value for service
 * @ranges: rb tree containing all service ranges for this service
 * @service_list: links to adjacent name ranges in hash chain
 * @subscriptions: list of subscriptions for this service type
 * @lock: spinlock controlling access to pertaining service ranges/publications
 * @rcu: RCU callback head used for deferred freeing
 */
struct tipc_service {
	u32 type;
	struct rb_root ranges;
	struct hlist_node service_list;
	struct list_head subscriptions;
	spinlock_t lock; /* Covers service range list */
	struct rcu_head rcu;
};

static int hash(int x)
{
	return x & (TIPC_NAMETBL_SIZE - 1);
}

/**
 * tipc_publ_create - create a publication structure
 */
static struct publication *tipc_publ_create(u32 type, u32 lower, u32 upper,
					    u32 scope, u32 node, u32 port,
					    u32 key)
{
	struct publication *publ = kzalloc(sizeof(*publ), GFP_ATOMIC);

	if (!publ)
		return NULL;

	publ->type = type;
	publ->lower = lower;
	publ->upper = upper;
	publ->scope = scope;
	publ->node = node;
	publ->port = port;
	publ->key = key;
	INIT_LIST_HEAD(&publ->binding_sock);
	INIT_LIST_HEAD(&publ->binding_node);
	INIT_LIST_HEAD(&publ->local_publ);
	INIT_LIST_HEAD(&publ->all_publ);
	return publ;
}

/**
 * tipc_service_create - create a service structure for the specified 'type'
 *
 * Allocates a single range structure and sets it to all 0's.
 */
static struct tipc_service *tipc_service_create(u32 type, struct hlist_head *hd)
{
	struct tipc_service *service = kzalloc(sizeof(*service), GFP_ATOMIC);

	if (!service) {
		pr_warn("Service creation failed, no memory\n");
		return NULL;
	}

	spin_lock_init(&service->lock);
	service->type = type;
	service->ranges = RB_ROOT;
	INIT_HLIST_NODE(&service->service_list);
	INIT_LIST_HEAD(&service->subscriptions);
	hlist_add_head_rcu(&service->service_list, hd);
	return service;
}

/**
 * tipc_service_find_range - find service range matching a service instance
 *
 * Very time-critical, so binary search through range rb tree
 */
static struct service_range *tipc_service_find_range(struct tipc_service *sc,
						     u32 instance)
{
	struct rb_node *n = sc->ranges.rb_node;
	struct service_range *sr;

	while (n) {
		sr = container_of(n, struct service_range, tree_node);
		if (sr->lower > instance)
			n = n->rb_left;
		else if (sr->upper < instance)
			n = n->rb_right;
		else
			return sr;
	}
	return NULL;
}

static struct service_range *tipc_service_create_range(struct tipc_service *sc,
						       u32 lower, u32 upper)
{
	struct rb_node **n, *parent = NULL;
	struct service_range *sr, *tmp;

	n = &sc->ranges.rb_node;
	while (*n) {
		tmp = container_of(*n, struct service_range, tree_node);
		parent = *n;
		tmp = container_of(parent, struct service_range, tree_node);
		if (lower < tmp->lower)
			n = &(*n)->rb_left;
		else if (lower > tmp->lower)
			n = &(*n)->rb_right;
		else if (upper < tmp->upper)
			n = &(*n)->rb_left;
		else if (upper > tmp->upper)
			n = &(*n)->rb_right;
		else
			return tmp;
	}
	sr = kzalloc(sizeof(*sr), GFP_ATOMIC);
	if (!sr)
		return NULL;
	sr->lower = lower;
	sr->upper = upper;
	INIT_LIST_HEAD(&sr->local_publ);
	INIT_LIST_HEAD(&sr->all_publ);
	rb_link_node(&sr->tree_node, parent, n);
	rb_insert_color(&sr->tree_node, &sc->ranges);
	return sr;
}

static struct publication *tipc_service_insert_publ(struct net *net,
						    struct tipc_service *sc,
						    u32 type, u32 lower,
						    u32 upper, u32 scope,
						    u32 node, u32 port,
						    u32 key)
{
	struct tipc_subscription *sub, *tmp;
	struct service_range *sr;
	struct publication *p;
	bool first = false;

	sr = tipc_service_create_range(sc, lower, upper);
	if (!sr)
		goto  err;

	first = list_empty(&sr->all_publ);

	/* Return if the publication already exists */
	list_for_each_entry(p, &sr->all_publ, all_publ) {
		if (p->key == key && (!p->node || p->node == node))
			return NULL;
	}

	/* Create and insert publication */
	p = tipc_publ_create(type, lower, upper, scope, node, port, key);
	if (!p)
		goto err;
	if (in_own_node(net, node))
		list_add(&p->local_publ, &sr->local_publ);
	list_add(&p->all_publ, &sr->all_publ);

	/* Any subscriptions waiting for notification?  */
	list_for_each_entry_safe(sub, tmp, &sc->subscriptions, service_list) {
		tipc_sub_report_overlap(sub, p->lower, p->upper, TIPC_PUBLISHED,
					p->port, p->node, p->scope, first);
	}
	return p;
err:
	pr_warn("Failed to bind to %u,%u,%u, no memory\n", type, lower, upper);
	return NULL;
}

/**
 * tipc_service_remove_publ - remove a publication from a service
 */
static struct publication *tipc_service_remove_publ(struct net *net,
						    struct tipc_service *sc,
						    u32 lower, u32 upper,
						    u32 node, u32 key)
{
	struct tipc_subscription *sub, *tmp;
	struct service_range *sr;
	struct publication *p;
	bool found = false;
	bool last = false;
	struct rb_node *n;

	sr = tipc_service_find_range(sc, lower);
	if (!sr)
		return NULL;

	/* Find exact matching service range */
	for (n = &sr->tree_node; n; n = rb_next(n)) {
		sr = container_of(n, struct service_range, tree_node);
		if (sr->upper == upper)
			break;
	}
	if (!n || sr->lower != lower || sr->upper != upper)
		return NULL;

	/* Find publication, if it exists */
	list_for_each_entry(p, &sr->all_publ, all_publ) {
		if (p->key != key || (node && node != p->node))
			continue;
		found = true;
		break;
	}
	if (!found)
		return NULL;

	list_del(&p->all_publ);
	list_del(&p->local_publ);

	/* Remove service range item if this was its last publication */
	if (list_empty(&sr->all_publ)) {
		last = true;
		rb_erase(&sr->tree_node, &sc->ranges);
		kfree(sr);
	}

	/* Notify any waiting subscriptions */
	list_for_each_entry_safe(sub, tmp, &sc->subscriptions, service_list) {
		tipc_sub_report_overlap(sub, p->lower, p->upper, TIPC_WITHDRAWN,
					p->port, p->node, p->scope, last);
	}
	return p;
}

/**
 * tipc_service_subscribe - attach a subscription, and optionally
 * issue the prescribed number of events if there is any service
 * range overlapping with the requested range
 */
static void tipc_service_subscribe(struct tipc_service *service,
				   struct tipc_subscription *sub)
{
	struct tipc_subscr *sb = &sub->evt.s;
	struct service_range *sr;
	struct tipc_name_seq ns;
	struct publication *p;
	struct rb_node *n;
	bool first;

	ns.type = tipc_sub_read(sb, seq.type);
	ns.lower = tipc_sub_read(sb, seq.lower);
	ns.upper = tipc_sub_read(sb, seq.upper);

	tipc_sub_get(sub);
	list_add(&sub->service_list, &service->subscriptions);

	if (tipc_sub_read(sb, filter) & TIPC_SUB_NO_STATUS)
		return;

	for (n = rb_first(&service->ranges); n; n = rb_next(n)) {
		sr = container_of(n, struct service_range, tree_node);
		if (sr->lower > ns.upper)
			break;
		if (!tipc_sub_check_overlap(&ns, sr->lower, sr->upper))
			continue;
		first = true;

		list_for_each_entry(p, &sr->all_publ, all_publ) {
			tipc_sub_report_overlap(sub, sr->lower, sr->upper,
						TIPC_PUBLISHED,	p->port,
						p->node, p->scope, first);
			first = false;
		}
	}
}

static struct tipc_service *tipc_service_find(struct net *net, u32 type)
{
	struct name_table *nt = tipc_name_table(net);
	struct hlist_head *service_head;
	struct tipc_service *service;

	service_head = &nt->services[hash(type)];
	hlist_for_each_entry_rcu(service, service_head, service_list) {
		if (service->type == type)
			return service;
	}
	return NULL;
};

struct publication *tipc_nametbl_insert_publ(struct net *net, u32 type,
					     u32 lower, u32 upper,
					     u32 scope, u32 node,
					     u32 port, u32 key)
{
	struct name_table *nt = tipc_name_table(net);
	struct tipc_service *sc;
	struct publication *p;

	if (scope > TIPC_NODE_SCOPE || lower > upper) {
		pr_debug("Failed to bind illegal {%u,%u,%u} with scope %u\n",
			 type, lower, upper, scope);
		return NULL;
	}
	sc = tipc_service_find(net, type);
	if (!sc)
		sc = tipc_service_create(type, &nt->services[hash(type)]);
	if (!sc)
		return NULL;

	spin_lock_bh(&sc->lock);
	p = tipc_service_insert_publ(net, sc, type, lower, upper,
				     scope, node, port, key);
	spin_unlock_bh(&sc->lock);
	return p;
}

struct publication *tipc_nametbl_remove_publ(struct net *net, u32 type,
					     u32 lower, u32 upper,
					     u32 node, u32 key)
{
	struct tipc_service *sc = tipc_service_find(net, type);
	struct publication *p = NULL;

	if (!sc)
		return NULL;

	spin_lock_bh(&sc->lock);
	p = tipc_service_remove_publ(net, sc, lower, upper, node, key);

	/* Delete service item if this no more publications and subscriptions */
	if (RB_EMPTY_ROOT(&sc->ranges) && list_empty(&sc->subscriptions)) {
		hlist_del_init_rcu(&sc->service_list);
		kfree_rcu(sc, rcu);
	}
	spin_unlock_bh(&sc->lock);
	return p;
}

/**
 * tipc_nametbl_translate - perform service instance to socket translation
 *
 * On entry, 'dnode' is the search domain used during translation.
 *
 * On exit:
 * - if translation is deferred to another node, leave 'dnode' unchanged and
 *   return 0
 * - if translation is attempted and succeeds, set 'dnode' to the publishing
 *   node and return the published (non-zero) port number
 * - if translation is attempted and fails, set 'dnode' to 0 and return 0
 *
 * Note that for legacy users (node configured with Z.C.N address format) the
 * 'closest-first' lookup algorithm must be maintained, i.e., if dnode is 0
 * we must look in the local binding list first
 */
u32 tipc_nametbl_translate(struct net *net, u32 type, u32 instance, u32 *dnode)
{
	struct tipc_net *tn = tipc_net(net);
	bool legacy = tn->legacy_addr_format;
	u32 self = tipc_own_addr(net);
	struct service_range *sr;
	struct tipc_service *sc;
	struct list_head *list;
	struct publication *p;
	u32 port = 0;
	u32 node = 0;

	if (!tipc_in_scope(legacy, *dnode, self))
		return 0;

	rcu_read_lock();
	sc = tipc_service_find(net, type);
	if (unlikely(!sc))
		goto not_found;

	spin_lock_bh(&sc->lock);
	sr = tipc_service_find_range(sc, instance);
	if (unlikely(!sr))
		goto no_match;

	/* Select lookup algorithm: local, closest-first or round-robin */
	if (*dnode == self) {
		list = &sr->local_publ;
		if (list_empty(list))
			goto no_match;
		p = list_first_entry(list, struct publication, local_publ);
		list_move_tail(&p->local_publ, &sr->local_publ);
	} else if (legacy && !*dnode && !list_empty(&sr->local_publ)) {
		list = &sr->local_publ;
		p = list_first_entry(list, struct publication, local_publ);
		list_move_tail(&p->local_publ, &sr->local_publ);
	} else {
		list = &sr->all_publ;
		p = list_first_entry(list, struct publication, all_publ);
		list_move_tail(&p->all_publ, &sr->all_publ);
	}
	port = p->port;
	node = p->node;
no_match:
	spin_unlock_bh(&sc->lock);
not_found:
	rcu_read_unlock();
	*dnode = node;
	return port;
}

bool tipc_nametbl_lookup(struct net *net, u32 type, u32 instance, u32 scope,
			 struct list_head *dsts, int *dstcnt, u32 exclude,
			 bool all)
{
	u32 self = tipc_own_addr(net);
	struct service_range *sr;
	struct tipc_service *sc;
	struct publication *p;

	*dstcnt = 0;
	rcu_read_lock();
	sc = tipc_service_find(net, type);
	if (unlikely(!sc))
		goto exit;

	spin_lock_bh(&sc->lock);

	sr = tipc_service_find_range(sc, instance);
	if (!sr)
		goto no_match;

	list_for_each_entry(p, &sr->all_publ, all_publ) {
		if (p->scope != scope)
			continue;
		if (p->port == exclude && p->node == self)
			continue;
		tipc_dest_push(dsts, p->node, p->port);
		(*dstcnt)++;
		if (all)
			continue;
		list_move_tail(&p->all_publ, &sr->all_publ);
		break;
	}
no_match:
	spin_unlock_bh(&sc->lock);
exit:
	rcu_read_unlock();
	return !list_empty(dsts);
}

void tipc_nametbl_mc_lookup(struct net *net, u32 type, u32 lower, u32 upper,
			    u32 scope, bool exact, struct list_head *dports)
{
	struct service_range *sr;
	struct tipc_service *sc;
	struct publication *p;
	struct rb_node *n;

	rcu_read_lock();
	sc = tipc_service_find(net, type);
	if (!sc)
		goto exit;

	spin_lock_bh(&sc->lock);

	for (n = rb_first(&sc->ranges); n; n = rb_next(n)) {
		sr = container_of(n, struct service_range, tree_node);
		if (sr->upper < lower)
			continue;
		if (sr->lower > upper)
			break;
		list_for_each_entry(p, &sr->local_publ, local_publ) {
			if (p->scope == scope || (!exact && p->scope < scope))
				tipc_dest_push(dports, 0, p->port);
		}
	}
	spin_unlock_bh(&sc->lock);
exit:
	rcu_read_unlock();
}

/* tipc_nametbl_lookup_dst_nodes - find broadcast destination nodes
 * - Creates list of nodes that overlap the given multicast address
 * - Determines if any node local destinations overlap
 */
void tipc_nametbl_lookup_dst_nodes(struct net *net, u32 type, u32 lower,
				   u32 upper, struct tipc_nlist *nodes)
{
	struct service_range *sr;
	struct tipc_service *sc;
	struct publication *p;
	struct rb_node *n;

	rcu_read_lock();
	sc = tipc_service_find(net, type);
	if (!sc)
		goto exit;

	spin_lock_bh(&sc->lock);

	for (n = rb_first(&sc->ranges); n; n = rb_next(n)) {
		sr = container_of(n, struct service_range, tree_node);
		if (sr->upper < lower)
			continue;
		if (sr->lower > upper)
			break;
		list_for_each_entry(p, &sr->all_publ, all_publ) {
			tipc_nlist_add(nodes, p->node);
		}
	}
	spin_unlock_bh(&sc->lock);
exit:
	rcu_read_unlock();
}

/* tipc_nametbl_build_group - build list of communication group members
 */
void tipc_nametbl_build_group(struct net *net, struct tipc_group *grp,
			      u32 type, u32 scope)
{
	struct service_range *sr;
	struct tipc_service *sc;
	struct publication *p;
	struct rb_node *n;

	rcu_read_lock();
	sc = tipc_service_find(net, type);
	if (!sc)
		goto exit;

	spin_lock_bh(&sc->lock);
	for (n = rb_first(&sc->ranges); n; n = rb_next(n)) {
		sr = container_of(n, struct service_range, tree_node);
		list_for_each_entry(p, &sr->all_publ, all_publ) {
			if (p->scope != scope)
				continue;
			tipc_group_add_member(grp, p->node, p->port, p->lower);
		}
	}
	spin_unlock_bh(&sc->lock);
exit:
	rcu_read_unlock();
}

/* tipc_nametbl_publish - add service binding to name table
 */
struct publication *tipc_nametbl_publish(struct net *net, u32 type, u32 lower,
					 u32 upper, u32 scope, u32 port,
					 u32 key)
{
	struct name_table *nt = tipc_name_table(net);
	struct tipc_net *tn = tipc_net(net);
	struct publication *p = NULL;
	struct sk_buff *skb = NULL;

	spin_lock_bh(&tn->nametbl_lock);

	if (nt->local_publ_count >= TIPC_MAX_PUBL) {
		pr_warn("Bind failed, max limit %u reached\n", TIPC_MAX_PUBL);
		goto exit;
	}

	p = tipc_nametbl_insert_publ(net, type, lower, upper, scope,
				     tipc_own_addr(net), port, key);
	if (p) {
		nt->local_publ_count++;
		skb = tipc_named_publish(net, p);
	}
exit:
	spin_unlock_bh(&tn->nametbl_lock);

	if (skb)
		tipc_node_broadcast(net, skb);
	return p;
}

/**
 * tipc_nametbl_withdraw - withdraw a service binding
 */
int tipc_nametbl_withdraw(struct net *net, u32 type, u32 lower,
			  u32 upper, u32 key)
{
	struct name_table *nt = tipc_name_table(net);
	struct tipc_net *tn = tipc_net(net);
	u32 self = tipc_own_addr(net);
	struct sk_buff *skb = NULL;
	struct publication *p;

	spin_lock_bh(&tn->nametbl_lock);

	p = tipc_nametbl_remove_publ(net, type, lower, upper, self, key);
	if (p) {
		nt->local_publ_count--;
		skb = tipc_named_withdraw(net, p);
		list_del_init(&p->binding_sock);
		kfree_rcu(p, rcu);
	} else {
		pr_err("Failed to remove local publication {%u,%u,%u}/%u\n",
		       type, lower, upper, key);
	}
	spin_unlock_bh(&tn->nametbl_lock);

	if (skb) {
		tipc_node_broadcast(net, skb);
		return 1;
	}
	return 0;
}

/**
 * tipc_nametbl_subscribe - add a subscription object to the name table
 */
void tipc_nametbl_subscribe(struct tipc_subscription *sub)
{
	struct name_table *nt = tipc_name_table(sub->net);
	struct tipc_net *tn = tipc_net(sub->net);
	struct tipc_subscr *s = &sub->evt.s;
	u32 type = tipc_sub_read(s, seq.type);
	struct tipc_service *sc;

	spin_lock_bh(&tn->nametbl_lock);
	sc = tipc_service_find(sub->net, type);
	if (!sc)
		sc = tipc_service_create(type, &nt->services[hash(type)]);
	if (sc) {
		spin_lock_bh(&sc->lock);
		tipc_service_subscribe(sc, sub);
		spin_unlock_bh(&sc->lock);
	} else {
		pr_warn("Failed to subscribe for {%u,%u,%u}\n", type,
			tipc_sub_read(s, seq.lower),
			tipc_sub_read(s, seq.upper));
	}
	spin_unlock_bh(&tn->nametbl_lock);
}

/**
 * tipc_nametbl_unsubscribe - remove a subscription object from name table
 */
void tipc_nametbl_unsubscribe(struct tipc_subscription *sub)
{
	struct tipc_net *tn = tipc_net(sub->net);
	struct tipc_subscr *s = &sub->evt.s;
	u32 type = tipc_sub_read(s, seq.type);
	struct tipc_service *sc;

	spin_lock_bh(&tn->nametbl_lock);
	sc = tipc_service_find(sub->net, type);
	if (!sc)
		goto exit;

	spin_lock_bh(&sc->lock);
	list_del_init(&sub->service_list);
	tipc_sub_put(sub);

	/* Delete service item if no more publications and subscriptions */
	if (RB_EMPTY_ROOT(&sc->ranges) && list_empty(&sc->subscriptions)) {
		hlist_del_init_rcu(&sc->service_list);
		kfree_rcu(sc, rcu);
	}
	spin_unlock_bh(&sc->lock);
exit:
	spin_unlock_bh(&tn->nametbl_lock);
}

int tipc_nametbl_init(struct net *net)
{
	struct tipc_net *tn = tipc_net(net);
	struct name_table *nt;
	int i;

	nt = kzalloc(sizeof(*nt), GFP_ATOMIC);
	if (!nt)
		return -ENOMEM;

	for (i = 0; i < TIPC_NAMETBL_SIZE; i++)
		INIT_HLIST_HEAD(&nt->services[i]);

	INIT_LIST_HEAD(&nt->node_scope);
	INIT_LIST_HEAD(&nt->cluster_scope);
	tn->nametbl = nt;
	spin_lock_init(&tn->nametbl_lock);
	return 0;
}

/**
 *  tipc_service_delete - purge all publications for a service and delete it
 */
static void tipc_service_delete(struct net *net, struct tipc_service *sc)
{
	struct service_range *sr, *tmpr;
	struct publication *p, *tmpb;

	spin_lock_bh(&sc->lock);
	rbtree_postorder_for_each_entry_safe(sr, tmpr, &sc->ranges, tree_node) {
		list_for_each_entry_safe(p, tmpb,
					 &sr->all_publ, all_publ) {
			tipc_service_remove_publ(net, sc, p->lower, p->upper,
						 p->node, p->key);
			kfree_rcu(p, rcu);
		}
	}
	hlist_del_init_rcu(&sc->service_list);
	spin_unlock_bh(&sc->lock);
	kfree_rcu(sc, rcu);
}

void tipc_nametbl_stop(struct net *net)
{
	struct name_table *nt = tipc_name_table(net);
	struct tipc_net *tn = tipc_net(net);
	struct hlist_head *service_head;
	struct tipc_service *service;
	u32 i;

	/* Verify name table is empty and purge any lingering
	 * publications, then release the name table
	 */
	spin_lock_bh(&tn->nametbl_lock);
	for (i = 0; i < TIPC_NAMETBL_SIZE; i++) {
		if (hlist_empty(&nt->services[i]))
			continue;
		service_head = &nt->services[i];
		hlist_for_each_entry_rcu(service, service_head, service_list) {
			tipc_service_delete(net, service);
		}
	}
	spin_unlock_bh(&tn->nametbl_lock);

	synchronize_net();
	kfree(nt);
}

static int __tipc_nl_add_nametable_publ(struct tipc_nl_msg *msg,
					struct tipc_service *service,
					struct service_range *sr,
					u32 *last_key)
{
	struct publication *p;
	struct nlattr *attrs;
	struct nlattr *b;
	void *hdr;

	if (*last_key) {
		list_for_each_entry(p, &sr->all_publ, all_publ)
			if (p->key == *last_key)
				break;
		if (p->key != *last_key)
			return -EPIPE;
	} else {
		p = list_first_entry(&sr->all_publ,
				     struct publication,
				     all_publ);
	}

	list_for_each_entry_from(p, &sr->all_publ, all_publ) {
		*last_key = p->key;

		hdr = genlmsg_put(msg->skb, msg->portid, msg->seq,
				  &tipc_genl_family, NLM_F_MULTI,
				  TIPC_NL_NAME_TABLE_GET);
		if (!hdr)
			return -EMSGSIZE;

		attrs = nla_nest_start(msg->skb, TIPC_NLA_NAME_TABLE);
		if (!attrs)
			goto msg_full;

		b = nla_nest_start(msg->skb, TIPC_NLA_NAME_TABLE_PUBL);
		if (!b)
			goto attr_msg_full;

		if (nla_put_u32(msg->skb, TIPC_NLA_PUBL_TYPE, service->type))
			goto publ_msg_full;
		if (nla_put_u32(msg->skb, TIPC_NLA_PUBL_LOWER, sr->lower))
			goto publ_msg_full;
		if (nla_put_u32(msg->skb, TIPC_NLA_PUBL_UPPER, sr->upper))
			goto publ_msg_full;
		if (nla_put_u32(msg->skb, TIPC_NLA_PUBL_SCOPE, p->scope))
			goto publ_msg_full;
		if (nla_put_u32(msg->skb, TIPC_NLA_PUBL_NODE, p->node))
			goto publ_msg_full;
		if (nla_put_u32(msg->skb, TIPC_NLA_PUBL_REF, p->port))
			goto publ_msg_full;
		if (nla_put_u32(msg->skb, TIPC_NLA_PUBL_KEY, p->key))
			goto publ_msg_full;

		nla_nest_end(msg->skb, b);
		nla_nest_end(msg->skb, attrs);
		genlmsg_end(msg->skb, hdr);
	}
	*last_key = 0;

	return 0;

publ_msg_full:
	nla_nest_cancel(msg->skb, b);
attr_msg_full:
	nla_nest_cancel(msg->skb, attrs);
msg_full:
	genlmsg_cancel(msg->skb, hdr);

	return -EMSGSIZE;
}

static int __tipc_nl_service_range_list(struct tipc_nl_msg *msg,
					struct tipc_service *sc,
					u32 *last_lower, u32 *last_key)
{
	struct service_range *sr;
	struct rb_node *n;
	int err;

	for (n = rb_first(&sc->ranges); n; n = rb_next(n)) {
		sr = container_of(n, struct service_range, tree_node);
		if (sr->lower < *last_lower)
			continue;
		err = __tipc_nl_add_nametable_publ(msg, sc, sr, last_key);
		if (err) {
			*last_lower = sr->lower;
			return err;
		}
	}
	*last_lower = 0;
	return 0;
}

static int tipc_nl_service_list(struct net *net, struct tipc_nl_msg *msg,
				u32 *last_type, u32 *last_lower, u32 *last_key)
{
	struct tipc_net *tn = tipc_net(net);
	struct tipc_service *service = NULL;
	struct hlist_head *head;
	int err;
	int i;

	if (*last_type)
		i = hash(*last_type);
	else
		i = 0;

	for (; i < TIPC_NAMETBL_SIZE; i++) {
		head = &tn->nametbl->services[i];

		if (*last_type) {
			service = tipc_service_find(net, *last_type);
			if (!service)
				return -EPIPE;
		} else {
			hlist_for_each_entry_rcu(service, head, service_list)
				break;
			if (!service)
				continue;
		}

		hlist_for_each_entry_from_rcu(service, service_list) {
			spin_lock_bh(&service->lock);
			err = __tipc_nl_service_range_list(msg, service,
							   last_lower,
							   last_key);

			if (err) {
				*last_type = service->type;
				spin_unlock_bh(&service->lock);
				return err;
			}
			spin_unlock_bh(&service->lock);
		}
		*last_type = 0;
	}
	return 0;
}

int tipc_nl_name_table_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	u32 last_type = cb->args[0];
	u32 last_lower = cb->args[1];
	u32 last_key = cb->args[2];
	int done = cb->args[3];
	struct tipc_nl_msg msg;
	int err;

	if (done)
		return 0;

	msg.skb = skb;
	msg.portid = NETLINK_CB(cb->skb).portid;
	msg.seq = cb->nlh->nlmsg_seq;

	rcu_read_lock();
	err = tipc_nl_service_list(net, &msg, &last_type,
				   &last_lower, &last_key);
	if (!err) {
		done = 1;
	} else if (err != -EMSGSIZE) {
		/* We never set seq or call nl_dump_check_consistent() this
		 * means that setting prev_seq here will cause the consistence
		 * check to fail in the netlink callback handler. Resulting in
		 * the NLMSG_DONE message having the NLM_F_DUMP_INTR flag set if
		 * we got an error.
		 */
		cb->prev_seq = 1;
	}
	rcu_read_unlock();

	cb->args[0] = last_type;
	cb->args[1] = last_lower;
	cb->args[2] = last_key;
	cb->args[3] = done;

	return skb->len;
}

struct tipc_dest *tipc_dest_find(struct list_head *l, u32 node, u32 port)
{
	u64 value = (u64)node << 32 | port;
	struct tipc_dest *dst;

	list_for_each_entry(dst, l, list) {
		if (dst->value != value)
			continue;
		return dst;
	}
	return NULL;
}

bool tipc_dest_push(struct list_head *l, u32 node, u32 port)
{
	u64 value = (u64)node << 32 | port;
	struct tipc_dest *dst;

	if (tipc_dest_find(l, node, port))
		return false;

	dst = kmalloc(sizeof(*dst), GFP_ATOMIC);
	if (unlikely(!dst))
		return false;
	dst->value = value;
	list_add(&dst->list, l);
	return true;
}

bool tipc_dest_pop(struct list_head *l, u32 *node, u32 *port)
{
	struct tipc_dest *dst;

	if (list_empty(l))
		return false;
	dst = list_first_entry(l, typeof(*dst), list);
	if (port)
		*port = dst->port;
	if (node)
		*node = dst->node;
	list_del(&dst->list);
	kfree(dst);
	return true;
}

bool tipc_dest_del(struct list_head *l, u32 node, u32 port)
{
	struct tipc_dest *dst;

	dst = tipc_dest_find(l, node, port);
	if (!dst)
		return false;
	list_del(&dst->list);
	kfree(dst);
	return true;
}

void tipc_dest_list_purge(struct list_head *l)
{
	struct tipc_dest *dst, *tmp;

	list_for_each_entry_safe(dst, tmp, l, list) {
		list_del(&dst->list);
		kfree(dst);
	}
}

int tipc_dest_list_len(struct list_head *l)
{
	struct tipc_dest *dst;
	int i = 0;

	list_for_each_entry(dst, l, list) {
		i++;
	}
	return i;
}
