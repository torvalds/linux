/*
 * net/tipc/name_table.c: TIPC name table code
 *
 * Copyright (c) 2000-2006, 2014-2018, Ericsson AB
 * Copyright (c) 2004-2008, 2010-2014, Wind River Systems
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

#include <net/sock.h>
#include <linux/list_sort.h>
#include <linux/rbtree_augmented.h>
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
 * @max: largest 'upper' in this node subtree
 * @local_publ: list of identical publications made from this node
 *   Used by closest_first lookup and multicast lookup algorithm
 * @all_publ: all publications identical to this one, whatever node and scope
 *   Used by round-robin lookup algorithm
 */
struct service_range {
	u32 lower;
	u32 upper;
	struct rb_node tree_node;
	u32 max;
	struct list_head local_publ;
	struct list_head all_publ;
};

/**
 * struct tipc_service - container for all published instances of a service type
 * @type: 32 bit 'type' value for service
 * @publ_cnt: increasing counter for publications in this service
 * @ranges: rb tree containing all service ranges for this service
 * @service_list: links to adjacent name ranges in hash chain
 * @subscriptions: list of subscriptions for this service type
 * @lock: spinlock controlling access to pertaining service ranges/publications
 * @rcu: RCU callback head used for deferred freeing
 */
struct tipc_service {
	u32 type;
	u32 publ_cnt;
	struct rb_root ranges;
	struct hlist_node service_list;
	struct list_head subscriptions;
	spinlock_t lock; /* Covers service range list */
	struct rcu_head rcu;
};

#define service_range_upper(sr) ((sr)->upper)
RB_DECLARE_CALLBACKS_MAX(static, sr_callbacks,
			 struct service_range, tree_node, u32, max,
			 service_range_upper)

#define service_range_entry(rbtree_node)				\
	(container_of(rbtree_node, struct service_range, tree_node))

#define service_range_overlap(sr, start, end)				\
	((sr)->lower <= (end) && (sr)->upper >= (start))

/**
 * service_range_foreach_match - iterate over tipc service rbtree for each
 *                               range match
 * @sr: the service range pointer as a loop cursor
 * @sc: the pointer to tipc service which holds the service range rbtree
 * @start: beginning of the search range (end >= start) for matching
 * @end: end of the search range (end >= start) for matching
 */
#define service_range_foreach_match(sr, sc, start, end)			\
	for (sr = service_range_match_first((sc)->ranges.rb_node,	\
					    start,			\
					    end);			\
	     sr;							\
	     sr = service_range_match_next(&(sr)->tree_node,		\
					   start,			\
					   end))

/**
 * service_range_match_first - find first service range matching a range
 * @n: the root node of service range rbtree for searching
 * @start: beginning of the search range (end >= start) for matching
 * @end: end of the search range (end >= start) for matching
 *
 * Return: the leftmost service range node in the rbtree that overlaps the
 * specific range if any. Otherwise, returns NULL.
 */
static struct service_range *service_range_match_first(struct rb_node *n,
						       u32 start, u32 end)
{
	struct service_range *sr;
	struct rb_node *l, *r;

	/* Non overlaps in tree at all? */
	if (!n || service_range_entry(n)->max < start)
		return NULL;

	while (n) {
		l = n->rb_left;
		if (l && service_range_entry(l)->max >= start) {
			/* A leftmost overlap range node must be one in the left
			 * subtree. If not, it has lower > end, then nodes on
			 * the right side cannot satisfy the condition either.
			 */
			n = l;
			continue;
		}

		/* No one in the left subtree can match, return if this node is
		 * an overlap i.e. leftmost.
		 */
		sr = service_range_entry(n);
		if (service_range_overlap(sr, start, end))
			return sr;

		/* Ok, try to lookup on the right side */
		r = n->rb_right;
		if (sr->lower <= end &&
		    r && service_range_entry(r)->max >= start) {
			n = r;
			continue;
		}
		break;
	}

	return NULL;
}

/**
 * service_range_match_next - find next service range matching a range
 * @n: a node in service range rbtree from which the searching starts
 * @start: beginning of the search range (end >= start) for matching
 * @end: end of the search range (end >= start) for matching
 *
 * Return: the next service range node to the given node in the rbtree that
 * overlaps the specific range if any. Otherwise, returns NULL.
 */
static struct service_range *service_range_match_next(struct rb_node *n,
						      u32 start, u32 end)
{
	struct service_range *sr;
	struct rb_node *p, *r;

	while (n) {
		r = n->rb_right;
		if (r && service_range_entry(r)->max >= start)
			/* A next overlap range node must be one in the right
			 * subtree. If not, it has lower > end, then any next
			 * successor (- an ancestor) of this node cannot
			 * satisfy the condition either.
			 */
			return service_range_match_first(r, start, end);

		/* No one in the right subtree can match, go up to find an
		 * ancestor of this node which is parent of a left-hand child.
		 */
		while ((p = rb_parent(n)) && n == p->rb_right)
			n = p;
		if (!p)
			break;

		/* Return if this ancestor is an overlap */
		sr = service_range_entry(p);
		if (service_range_overlap(sr, start, end))
			return sr;

		/* Ok, try to lookup more from this ancestor */
		if (sr->lower <= end) {
			n = p;
			continue;
		}
		break;
	}

	return NULL;
}

static int hash(int x)
{
	return x & (TIPC_NAMETBL_SIZE - 1);
}

/**
 * tipc_publ_create - create a publication structure
 * @ua: the service range the user is binding to
 * @sk: the address of the socket that is bound
 * @key: publication key
 */
static struct publication *tipc_publ_create(struct tipc_uaddr *ua,
					    struct tipc_socket_addr *sk,
					    u32 key)
{
	struct publication *p = kzalloc(sizeof(*p), GFP_ATOMIC);

	if (!p)
		return NULL;

	p->sr = ua->sr;
	p->sk = *sk;
	p->scope = ua->scope;
	p->key = key;
	INIT_LIST_HEAD(&p->binding_sock);
	INIT_LIST_HEAD(&p->binding_node);
	INIT_LIST_HEAD(&p->local_publ);
	INIT_LIST_HEAD(&p->all_publ);
	INIT_LIST_HEAD(&p->list);
	return p;
}

/**
 * tipc_service_create - create a service structure for the specified 'type'
 * @net: network namespace
 * @ua: address representing the service to be bound
 *
 * Allocates a single range structure and sets it to all 0's.
 */
static struct tipc_service *tipc_service_create(struct net *net,
						struct tipc_uaddr *ua)
{
	struct name_table *nt = tipc_name_table(net);
	struct tipc_service *service;
	struct hlist_head *hd;

	service = kzalloc(sizeof(*service), GFP_ATOMIC);
	if (!service) {
		pr_warn("Service creation failed, no memory\n");
		return NULL;
	}

	spin_lock_init(&service->lock);
	service->type = ua->sr.type;
	service->ranges = RB_ROOT;
	INIT_HLIST_NODE(&service->service_list);
	INIT_LIST_HEAD(&service->subscriptions);
	hd = &nt->services[hash(ua->sr.type)];
	hlist_add_head_rcu(&service->service_list, hd);
	return service;
}

/*  tipc_service_find_range - find service range matching publication parameters
 */
static struct service_range *tipc_service_find_range(struct tipc_service *sc,
						     struct tipc_uaddr *ua)
{
	struct service_range *sr;

	service_range_foreach_match(sr, sc, ua->sr.lower, ua->sr.upper) {
		/* Look for exact match */
		if (sr->lower == ua->sr.lower && sr->upper == ua->sr.upper)
			return sr;
	}

	return NULL;
}

static struct service_range *tipc_service_create_range(struct tipc_service *sc,
						       struct publication *p)
{
	struct rb_node **n, *parent = NULL;
	struct service_range *sr;
	u32 lower = p->sr.lower;
	u32 upper = p->sr.upper;

	n = &sc->ranges.rb_node;
	while (*n) {
		parent = *n;
		sr = service_range_entry(parent);
		if (lower == sr->lower && upper == sr->upper)
			return sr;
		if (sr->max < upper)
			sr->max = upper;
		if (lower <= sr->lower)
			n = &parent->rb_left;
		else
			n = &parent->rb_right;
	}
	sr = kzalloc(sizeof(*sr), GFP_ATOMIC);
	if (!sr)
		return NULL;
	sr->lower = lower;
	sr->upper = upper;
	sr->max = upper;
	INIT_LIST_HEAD(&sr->local_publ);
	INIT_LIST_HEAD(&sr->all_publ);
	rb_link_node(&sr->tree_node, parent, n);
	rb_insert_augmented(&sr->tree_node, &sc->ranges, &sr_callbacks);
	return sr;
}

static bool tipc_service_insert_publ(struct net *net,
				     struct tipc_service *sc,
				     struct publication *p)
{
	struct tipc_subscription *sub, *tmp;
	struct service_range *sr;
	struct publication *_p;
	u32 node = p->sk.node;
	bool first = false;
	bool res = false;
	u32 key = p->key;

	spin_lock_bh(&sc->lock);
	sr = tipc_service_create_range(sc, p);
	if (!sr)
		goto  exit;

	first = list_empty(&sr->all_publ);

	/* Return if the publication already exists */
	list_for_each_entry(_p, &sr->all_publ, all_publ) {
		if (_p->key == key && (!_p->sk.node || _p->sk.node == node)) {
			pr_debug("Failed to bind duplicate %u,%u,%u/%u:%u/%u\n",
				 p->sr.type, p->sr.lower, p->sr.upper,
				 node, p->sk.ref, key);
			goto exit;
		}
	}

	if (in_own_node(net, p->sk.node))
		list_add(&p->local_publ, &sr->local_publ);
	list_add(&p->all_publ, &sr->all_publ);
	p->id = sc->publ_cnt++;

	/* Any subscriptions waiting for notification?  */
	list_for_each_entry_safe(sub, tmp, &sc->subscriptions, service_list) {
		tipc_sub_report_overlap(sub, p, TIPC_PUBLISHED, first);
	}
	res = true;
exit:
	if (!res)
		pr_warn("Failed to bind to %u,%u,%u\n",
			p->sr.type, p->sr.lower, p->sr.upper);
	spin_unlock_bh(&sc->lock);
	return res;
}

/**
 * tipc_service_remove_publ - remove a publication from a service
 * @r: service_range to remove publication from
 * @sk: address publishing socket
 * @key: target publication key
 */
static struct publication *tipc_service_remove_publ(struct service_range *r,
						    struct tipc_socket_addr *sk,
						    u32 key)
{
	struct publication *p;
	u32 node = sk->node;

	list_for_each_entry(p, &r->all_publ, all_publ) {
		if (p->key != key || (node && node != p->sk.node))
			continue;
		list_del(&p->all_publ);
		list_del(&p->local_publ);
		return p;
	}
	return NULL;
}

/*
 * Code reused: time_after32() for the same purpose
 */
#define publication_after(pa, pb) time_after32((pa)->id, (pb)->id)
static int tipc_publ_sort(void *priv, struct list_head *a,
			  struct list_head *b)
{
	struct publication *pa, *pb;

	pa = container_of(a, struct publication, list);
	pb = container_of(b, struct publication, list);
	return publication_after(pa, pb);
}

/**
 * tipc_service_subscribe - attach a subscription, and optionally
 * issue the prescribed number of events if there is any service
 * range overlapping with the requested range
 * @service: the tipc_service to attach the @sub to
 * @sub: the subscription to attach
 */
static void tipc_service_subscribe(struct tipc_service *service,
				   struct tipc_subscription *sub)
{
	struct publication *p, *first, *tmp;
	struct list_head publ_list;
	struct service_range *sr;
	u32 filter, lower, upper;

	filter = sub->s.filter;
	lower = sub->s.seq.lower;
	upper = sub->s.seq.upper;

	tipc_sub_get(sub);
	list_add(&sub->service_list, &service->subscriptions);

	if (filter & TIPC_SUB_NO_STATUS)
		return;

	INIT_LIST_HEAD(&publ_list);
	service_range_foreach_match(sr, service, lower, upper) {
		first = NULL;
		list_for_each_entry(p, &sr->all_publ, all_publ) {
			if (filter & TIPC_SUB_PORTS)
				list_add_tail(&p->list, &publ_list);
			else if (!first || publication_after(first, p))
				/* Pick this range's *first* publication */
				first = p;
		}
		if (first)
			list_add_tail(&first->list, &publ_list);
	}

	/* Sort the publications before reporting */
	list_sort(NULL, &publ_list, tipc_publ_sort);
	list_for_each_entry_safe(p, tmp, &publ_list, list) {
		tipc_sub_report_overlap(sub, p, TIPC_PUBLISHED, true);
		list_del_init(&p->list);
	}
}

static struct tipc_service *tipc_service_find(struct net *net,
					      struct tipc_uaddr *ua)
{
	struct name_table *nt = tipc_name_table(net);
	struct hlist_head *service_head;
	struct tipc_service *service;

	service_head = &nt->services[hash(ua->sr.type)];
	hlist_for_each_entry_rcu(service, service_head, service_list) {
		if (service->type == ua->sr.type)
			return service;
	}
	return NULL;
};

struct publication *tipc_nametbl_insert_publ(struct net *net,
					     struct tipc_uaddr *ua,
					     struct tipc_socket_addr *sk,
					     u32 key)
{
	struct tipc_service *sc;
	struct publication *p;

	p = tipc_publ_create(ua, sk, key);
	if (!p)
		return NULL;

	sc = tipc_service_find(net, ua);
	if (!sc)
		sc = tipc_service_create(net, ua);
	if (sc && tipc_service_insert_publ(net, sc, p))
		return p;
	kfree(p);
	return NULL;
}

struct publication *tipc_nametbl_remove_publ(struct net *net,
					     struct tipc_uaddr *ua,
					     struct tipc_socket_addr *sk,
					     u32 key)
{
	struct tipc_subscription *sub, *tmp;
	struct publication *p = NULL;
	struct service_range *sr;
	struct tipc_service *sc;
	bool last;

	sc = tipc_service_find(net, ua);
	if (!sc)
		goto exit;

	spin_lock_bh(&sc->lock);
	sr = tipc_service_find_range(sc, ua);
	if (!sr)
		goto unlock;
	p = tipc_service_remove_publ(sr, sk, key);
	if (!p)
		goto unlock;

	/* Notify any waiting subscriptions */
	last = list_empty(&sr->all_publ);
	list_for_each_entry_safe(sub, tmp, &sc->subscriptions, service_list) {
		tipc_sub_report_overlap(sub, p, TIPC_WITHDRAWN, last);
	}

	/* Remove service range item if this was its last publication */
	if (list_empty(&sr->all_publ)) {
		rb_erase_augmented(&sr->tree_node, &sc->ranges, &sr_callbacks);
		kfree(sr);
	}

	/* Delete service item if no more publications and subscriptions */
	if (RB_EMPTY_ROOT(&sc->ranges) && list_empty(&sc->subscriptions)) {
		hlist_del_init_rcu(&sc->service_list);
		kfree_rcu(sc, rcu);
	}
unlock:
	spin_unlock_bh(&sc->lock);
exit:
	if (!p) {
		pr_err("Failed to remove unknown binding: %u,%u,%u/%u:%u/%u\n",
		       ua->sr.type, ua->sr.lower, ua->sr.upper,
		       sk->node, sk->ref, key);
	}
	return p;
}

/**
 * tipc_nametbl_lookup_anycast - perform service instance to socket translation
 * @net: network namespace
 * @ua: service address to look up
 * @sk: address to socket we want to find
 *
 * On entry, a non-zero 'sk->node' indicates the node where we want lookup to be
 * performed, which may not be this one.
 * On exit:
 * - If lookup is deferred to another node, leave 'sk->node' unchanged and
 *   return 'true'.
 * - If lookup is successful, set the 'sk->node' and 'sk->ref' (== portid) which
 *   represent the bound socket and return 'true'.
 * - If lookup fails, return 'false'
 *
 * Note that for legacy users (node configured with Z.C.N address format) the
 * 'closest-first' lookup algorithm must be maintained, i.e., if sk.node is 0
 * we must look in the local binding list first
 */
bool tipc_nametbl_lookup_anycast(struct net *net,
				 struct tipc_uaddr *ua,
				 struct tipc_socket_addr *sk)
{
	struct tipc_net *tn = tipc_net(net);
	bool legacy = tn->legacy_addr_format;
	u32 self = tipc_own_addr(net);
	u32 inst = ua->sa.instance;
	struct service_range *r;
	struct tipc_service *sc;
	struct publication *p;
	struct list_head *l;
	bool res = false;

	if (!tipc_in_scope(legacy, sk->node, self))
		return true;

	rcu_read_lock();
	sc = tipc_service_find(net, ua);
	if (unlikely(!sc))
		goto exit;

	spin_lock_bh(&sc->lock);
	service_range_foreach_match(r, sc, inst, inst) {
		/* Select lookup algo: local, closest-first or round-robin */
		if (sk->node == self) {
			l = &r->local_publ;
			if (list_empty(l))
				continue;
			p = list_first_entry(l, struct publication, local_publ);
			list_move_tail(&p->local_publ, &r->local_publ);
		} else if (legacy && !sk->node && !list_empty(&r->local_publ)) {
			l = &r->local_publ;
			p = list_first_entry(l, struct publication, local_publ);
			list_move_tail(&p->local_publ, &r->local_publ);
		} else {
			l = &r->all_publ;
			p = list_first_entry(l, struct publication, all_publ);
			list_move_tail(&p->all_publ, &r->all_publ);
		}
		*sk = p->sk;
		res = true;
		/* Todo: as for legacy, pick the first matching range only, a
		 * "true" round-robin will be performed as needed.
		 */
		break;
	}
	spin_unlock_bh(&sc->lock);

exit:
	rcu_read_unlock();
	return res;
}

/* tipc_nametbl_lookup_group(): lookup destinaton(s) in a communication group
 * Returns a list of one (== group anycast) or more (== group multicast)
 * destination socket/node pairs matching the given address.
 * The requester may or may not want to exclude himself from the list.
 */
bool tipc_nametbl_lookup_group(struct net *net, struct tipc_uaddr *ua,
			       struct list_head *dsts, int *dstcnt,
			       u32 exclude, bool mcast)
{
	u32 self = tipc_own_addr(net);
	u32 inst = ua->sa.instance;
	struct service_range *sr;
	struct tipc_service *sc;
	struct publication *p;

	*dstcnt = 0;
	rcu_read_lock();
	sc = tipc_service_find(net, ua);
	if (unlikely(!sc))
		goto exit;

	spin_lock_bh(&sc->lock);

	/* Todo: a full search i.e. service_range_foreach_match() instead? */
	sr = service_range_match_first(sc->ranges.rb_node, inst, inst);
	if (!sr)
		goto no_match;

	list_for_each_entry(p, &sr->all_publ, all_publ) {
		if (p->scope != ua->scope)
			continue;
		if (p->sk.ref == exclude && p->sk.node == self)
			continue;
		tipc_dest_push(dsts, p->sk.node, p->sk.ref);
		(*dstcnt)++;
		if (mcast)
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

/* tipc_nametbl_lookup_mcast_sockets(): look up node local destinaton sockets
 *                                      matching the given address
 * Used on nodes which have received a multicast/broadcast message
 * Returns a list of local sockets
 */
void tipc_nametbl_lookup_mcast_sockets(struct net *net, struct tipc_uaddr *ua,
				       bool exact, struct list_head *dports)
{
	struct service_range *sr;
	struct tipc_service *sc;
	struct publication *p;
	u32 scope = ua->scope;

	rcu_read_lock();
	sc = tipc_service_find(net, ua);
	if (!sc)
		goto exit;

	spin_lock_bh(&sc->lock);
	service_range_foreach_match(sr, sc, ua->sr.lower, ua->sr.upper) {
		list_for_each_entry(p, &sr->local_publ, local_publ) {
			if (p->scope == scope || (!exact && p->scope < scope))
				tipc_dest_push(dports, 0, p->sk.ref);
		}
	}
	spin_unlock_bh(&sc->lock);
exit:
	rcu_read_unlock();
}

/* tipc_nametbl_lookup_mcast_nodes(): look up all destination nodes matching
 *                                    the given address. Used in sending node.
 * Used on nodes which are sending out a multicast/broadcast message
 * Returns a list of nodes, including own node if applicable
 */
void tipc_nametbl_lookup_mcast_nodes(struct net *net, struct tipc_uaddr *ua,
				     struct tipc_nlist *nodes)
{
	struct service_range *sr;
	struct tipc_service *sc;
	struct publication *p;

	rcu_read_lock();
	sc = tipc_service_find(net, ua);
	if (!sc)
		goto exit;

	spin_lock_bh(&sc->lock);
	service_range_foreach_match(sr, sc, ua->sr.lower, ua->sr.upper) {
		list_for_each_entry(p, &sr->all_publ, all_publ) {
			tipc_nlist_add(nodes, p->sk.node);
		}
	}
	spin_unlock_bh(&sc->lock);
exit:
	rcu_read_unlock();
}

/* tipc_nametbl_build_group - build list of communication group members
 */
void tipc_nametbl_build_group(struct net *net, struct tipc_group *grp,
			      struct tipc_uaddr *ua)
{
	struct service_range *sr;
	struct tipc_service *sc;
	struct publication *p;
	struct rb_node *n;

	rcu_read_lock();
	sc = tipc_service_find(net, ua);
	if (!sc)
		goto exit;

	spin_lock_bh(&sc->lock);
	for (n = rb_first(&sc->ranges); n; n = rb_next(n)) {
		sr = container_of(n, struct service_range, tree_node);
		list_for_each_entry(p, &sr->all_publ, all_publ) {
			if (p->scope != ua->scope)
				continue;
			tipc_group_add_member(grp, p->sk.node, p->sk.ref,
					      p->sr.lower);
		}
	}
	spin_unlock_bh(&sc->lock);
exit:
	rcu_read_unlock();
}

/* tipc_nametbl_publish - add service binding to name table
 */
struct publication *tipc_nametbl_publish(struct net *net, struct tipc_uaddr *ua,
					 struct tipc_socket_addr *sk, u32 key)
{
	struct name_table *nt = tipc_name_table(net);
	struct tipc_net *tn = tipc_net(net);
	struct publication *p = NULL;
	struct sk_buff *skb = NULL;
	u32 rc_dests;

	spin_lock_bh(&tn->nametbl_lock);

	if (nt->local_publ_count >= TIPC_MAX_PUBL) {
		pr_warn("Bind failed, max limit %u reached\n", TIPC_MAX_PUBL);
		goto exit;
	}

	p = tipc_nametbl_insert_publ(net, ua, sk, key);
	if (p) {
		nt->local_publ_count++;
		skb = tipc_named_publish(net, p);
	}
	rc_dests = nt->rc_dests;
exit:
	spin_unlock_bh(&tn->nametbl_lock);

	if (skb)
		tipc_node_broadcast(net, skb, rc_dests);
	return p;

}

/**
 * tipc_nametbl_withdraw - withdraw a service binding
 * @net: network namespace
 * @ua: service address/range being unbound
 * @sk: address of the socket being unbound from
 * @key: target publication key
 */
void tipc_nametbl_withdraw(struct net *net, struct tipc_uaddr *ua,
			   struct tipc_socket_addr *sk, u32 key)
{
	struct name_table *nt = tipc_name_table(net);
	struct tipc_net *tn = tipc_net(net);
	struct sk_buff *skb = NULL;
	struct publication *p;
	u32 rc_dests;

	spin_lock_bh(&tn->nametbl_lock);

	p = tipc_nametbl_remove_publ(net, ua, sk, key);
	if (p) {
		nt->local_publ_count--;
		skb = tipc_named_withdraw(net, p);
		list_del_init(&p->binding_sock);
		kfree_rcu(p, rcu);
	}
	rc_dests = nt->rc_dests;
	spin_unlock_bh(&tn->nametbl_lock);

	if (skb)
		tipc_node_broadcast(net, skb, rc_dests);
}

/**
 * tipc_nametbl_subscribe - add a subscription object to the name table
 * @sub: subscription to add
 */
bool tipc_nametbl_subscribe(struct tipc_subscription *sub)
{
	struct tipc_net *tn = tipc_net(sub->net);
	u32 type = sub->s.seq.type;
	struct tipc_service *sc;
	struct tipc_uaddr ua;
	bool res = true;

	tipc_uaddr(&ua, TIPC_SERVICE_RANGE, TIPC_NODE_SCOPE, type,
		   sub->s.seq.lower, sub->s.seq.upper);
	spin_lock_bh(&tn->nametbl_lock);
	sc = tipc_service_find(sub->net, &ua);
	if (!sc)
		sc = tipc_service_create(sub->net, &ua);
	if (sc) {
		spin_lock_bh(&sc->lock);
		tipc_service_subscribe(sc, sub);
		spin_unlock_bh(&sc->lock);
	} else {
		pr_warn("Failed to subscribe for {%u,%u,%u}\n",
			type, sub->s.seq.lower, sub->s.seq.upper);
		res = false;
	}
	spin_unlock_bh(&tn->nametbl_lock);
	return res;
}

/**
 * tipc_nametbl_unsubscribe - remove a subscription object from name table
 * @sub: subscription to remove
 */
void tipc_nametbl_unsubscribe(struct tipc_subscription *sub)
{
	struct tipc_net *tn = tipc_net(sub->net);
	struct tipc_service *sc;
	struct tipc_uaddr ua;

	tipc_uaddr(&ua, TIPC_SERVICE_RANGE, TIPC_NODE_SCOPE,
		   sub->s.seq.type, sub->s.seq.lower, sub->s.seq.upper);
	spin_lock_bh(&tn->nametbl_lock);
	sc = tipc_service_find(sub->net, &ua);
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

	nt = kzalloc(sizeof(*nt), GFP_KERNEL);
	if (!nt)
		return -ENOMEM;

	for (i = 0; i < TIPC_NAMETBL_SIZE; i++)
		INIT_HLIST_HEAD(&nt->services[i]);

	INIT_LIST_HEAD(&nt->node_scope);
	INIT_LIST_HEAD(&nt->cluster_scope);
	rwlock_init(&nt->cluster_scope_lock);
	tn->nametbl = nt;
	spin_lock_init(&tn->nametbl_lock);
	return 0;
}

/**
 * tipc_service_delete - purge all publications for a service and delete it
 * @net: the associated network namespace
 * @sc: tipc_service to delete
 */
static void tipc_service_delete(struct net *net, struct tipc_service *sc)
{
	struct service_range *sr, *tmpr;
	struct publication *p, *tmp;

	spin_lock_bh(&sc->lock);
	rbtree_postorder_for_each_entry_safe(sr, tmpr, &sc->ranges, tree_node) {
		list_for_each_entry_safe(p, tmp, &sr->all_publ, all_publ) {
			tipc_service_remove_publ(sr, &p->sk, p->key);
			kfree_rcu(p, rcu);
		}
		rb_erase_augmented(&sr->tree_node, &sc->ranges, &sr_callbacks);
		kfree(sr);
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

		attrs = nla_nest_start_noflag(msg->skb, TIPC_NLA_NAME_TABLE);
		if (!attrs)
			goto msg_full;

		b = nla_nest_start_noflag(msg->skb, TIPC_NLA_NAME_TABLE_PUBL);
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
		if (nla_put_u32(msg->skb, TIPC_NLA_PUBL_NODE, p->sk.node))
			goto publ_msg_full;
		if (nla_put_u32(msg->skb, TIPC_NLA_PUBL_REF, p->sk.ref))
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
	struct tipc_uaddr ua;
	int err;
	int i;

	if (*last_type)
		i = hash(*last_type);
	else
		i = 0;

	for (; i < TIPC_NAMETBL_SIZE; i++) {
		head = &tn->nametbl->services[i];

		if (*last_type ||
		    (!i && *last_key && (*last_lower == *last_key))) {
			tipc_uaddr(&ua, TIPC_SERVICE_RANGE, TIPC_NODE_SCOPE,
				   *last_type, *last_lower, *last_lower);
			service = tipc_service_find(net, &ua);
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
	struct tipc_dest *dst;

	list_for_each_entry(dst, l, list) {
		if (dst->node == node && dst->port == port)
			return dst;
	}
	return NULL;
}

bool tipc_dest_push(struct list_head *l, u32 node, u32 port)
{
	struct tipc_dest *dst;

	if (tipc_dest_find(l, node, port))
		return false;

	dst = kmalloc(sizeof(*dst), GFP_ATOMIC);
	if (unlikely(!dst))
		return false;
	dst->node = node;
	dst->port = port;
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
