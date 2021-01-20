// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2020, Nikolay Aleksandrov <nikolay@nvidia.com>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/if_ether.h>
#include <linux/igmp.h>
#include <linux/in.h>
#include <linux/jhash.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/netdevice.h>
#include <linux/netfilter_bridge.h>
#include <linux/random.h>
#include <linux/rculist.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/inetdevice.h>
#include <linux/mroute.h>
#include <net/ip.h>
#include <net/switchdev.h>
#if IS_ENABLED(CONFIG_IPV6)
#include <linux/icmpv6.h>
#include <net/ipv6.h>
#include <net/mld.h>
#include <net/ip6_checksum.h>
#include <net/addrconf.h>
#endif

#include "br_private.h"
#include "br_private_mcast_eht.h"

static bool br_multicast_del_eht_set_entry(struct net_bridge_port_group *pg,
					   union net_bridge_eht_addr *src_addr,
					   union net_bridge_eht_addr *h_addr);
static void br_multicast_create_eht_set_entry(struct net_bridge_port_group *pg,
					      union net_bridge_eht_addr *src_addr,
					      union net_bridge_eht_addr *h_addr,
					      int filter_mode,
					      bool allow_zero_src);

static struct net_bridge_group_eht_host *
br_multicast_eht_host_lookup(struct net_bridge_port_group *pg,
			     union net_bridge_eht_addr *h_addr)
{
	struct rb_node *node = pg->eht_host_tree.rb_node;

	while (node) {
		struct net_bridge_group_eht_host *this;
		int result;

		this = rb_entry(node, struct net_bridge_group_eht_host,
				rb_node);
		result = memcmp(h_addr, &this->h_addr, sizeof(*h_addr));
		if (result < 0)
			node = node->rb_left;
		else if (result > 0)
			node = node->rb_right;
		else
			return this;
	}

	return NULL;
}

static int br_multicast_eht_host_filter_mode(struct net_bridge_port_group *pg,
					     union net_bridge_eht_addr *h_addr)
{
	struct net_bridge_group_eht_host *eht_host;

	eht_host = br_multicast_eht_host_lookup(pg, h_addr);
	if (!eht_host)
		return MCAST_INCLUDE;

	return eht_host->filter_mode;
}

static struct net_bridge_group_eht_set_entry *
br_multicast_eht_set_entry_lookup(struct net_bridge_group_eht_set *eht_set,
				  union net_bridge_eht_addr *h_addr)
{
	struct rb_node *node = eht_set->entry_tree.rb_node;

	while (node) {
		struct net_bridge_group_eht_set_entry *this;
		int result;

	this = rb_entry(node, struct net_bridge_group_eht_set_entry,
			rb_node);
	result = memcmp(h_addr, &this->h_addr, sizeof(*h_addr));
	if (result < 0)
		node = node->rb_left;
	else if (result > 0)
		node = node->rb_right;
	else
		return this;
	}

	return NULL;
}

static struct net_bridge_group_eht_set *
br_multicast_eht_set_lookup(struct net_bridge_port_group *pg,
			    union net_bridge_eht_addr *src_addr)
{
	struct rb_node *node = pg->eht_set_tree.rb_node;

	while (node) {
		struct net_bridge_group_eht_set *this;
		int result;

		this = rb_entry(node, struct net_bridge_group_eht_set,
				rb_node);
		result = memcmp(src_addr, &this->src_addr, sizeof(*src_addr));
		if (result < 0)
			node = node->rb_left;
		else if (result > 0)
			node = node->rb_right;
		else
			return this;
	}

	return NULL;
}

static void __eht_destroy_host(struct net_bridge_group_eht_host *eht_host)
{
	WARN_ON(!hlist_empty(&eht_host->set_entries));

	rb_erase(&eht_host->rb_node, &eht_host->pg->eht_host_tree);
	RB_CLEAR_NODE(&eht_host->rb_node);
	kfree(eht_host);
}

static void br_multicast_destroy_eht_set_entry(struct net_bridge_mcast_gc *gc)
{
	struct net_bridge_group_eht_set_entry *set_h;

	set_h = container_of(gc, struct net_bridge_group_eht_set_entry, mcast_gc);
	WARN_ON(!RB_EMPTY_NODE(&set_h->rb_node));

	del_timer_sync(&set_h->timer);
	kfree(set_h);
}

static void br_multicast_destroy_eht_set(struct net_bridge_mcast_gc *gc)
{
	struct net_bridge_group_eht_set *eht_set;

	eht_set = container_of(gc, struct net_bridge_group_eht_set, mcast_gc);
	WARN_ON(!RB_EMPTY_NODE(&eht_set->rb_node));
	WARN_ON(!RB_EMPTY_ROOT(&eht_set->entry_tree));

	del_timer_sync(&eht_set->timer);
	kfree(eht_set);
}

static void __eht_del_set_entry(struct net_bridge_group_eht_set_entry *set_h)
{
	struct net_bridge_group_eht_host *eht_host = set_h->h_parent;
	union net_bridge_eht_addr zero_addr;

	rb_erase(&set_h->rb_node, &set_h->eht_set->entry_tree);
	RB_CLEAR_NODE(&set_h->rb_node);
	hlist_del_init(&set_h->host_list);
	memset(&zero_addr, 0, sizeof(zero_addr));
	if (memcmp(&set_h->h_addr, &zero_addr, sizeof(zero_addr)))
		eht_host->num_entries--;
	hlist_add_head(&set_h->mcast_gc.gc_node, &set_h->br->mcast_gc_list);
	queue_work(system_long_wq, &set_h->br->mcast_gc_work);

	if (hlist_empty(&eht_host->set_entries))
		__eht_destroy_host(eht_host);
}

static void br_multicast_del_eht_set(struct net_bridge_group_eht_set *eht_set)
{
	struct net_bridge_group_eht_set_entry *set_h;
	struct rb_node *node;

	while ((node = rb_first(&eht_set->entry_tree))) {
		set_h = rb_entry(node, struct net_bridge_group_eht_set_entry,
				 rb_node);
		__eht_del_set_entry(set_h);
	}

	rb_erase(&eht_set->rb_node, &eht_set->pg->eht_set_tree);
	RB_CLEAR_NODE(&eht_set->rb_node);
	hlist_add_head(&eht_set->mcast_gc.gc_node, &eht_set->br->mcast_gc_list);
	queue_work(system_long_wq, &eht_set->br->mcast_gc_work);
}

void br_multicast_eht_clean_sets(struct net_bridge_port_group *pg)
{
	struct net_bridge_group_eht_set *eht_set;
	struct rb_node *node;

	while ((node = rb_first(&pg->eht_set_tree))) {
		eht_set = rb_entry(node, struct net_bridge_group_eht_set,
				   rb_node);
		br_multicast_del_eht_set(eht_set);
	}
}

static void br_multicast_eht_set_entry_expired(struct timer_list *t)
{
	struct net_bridge_group_eht_set_entry *set_h = from_timer(set_h, t, timer);
	struct net_bridge *br = set_h->br;

	spin_lock(&br->multicast_lock);
	if (RB_EMPTY_NODE(&set_h->rb_node) || timer_pending(&set_h->timer))
		goto out;

	br_multicast_del_eht_set_entry(set_h->eht_set->pg,
				       &set_h->eht_set->src_addr,
				       &set_h->h_addr);
out:
	spin_unlock(&br->multicast_lock);
}

static void br_multicast_eht_set_expired(struct timer_list *t)
{
	struct net_bridge_group_eht_set *eht_set = from_timer(eht_set, t,
							      timer);
	struct net_bridge *br = eht_set->br;

	spin_lock(&br->multicast_lock);
	if (RB_EMPTY_NODE(&eht_set->rb_node) || timer_pending(&eht_set->timer))
		goto out;

	br_multicast_del_eht_set(eht_set);
out:
	spin_unlock(&br->multicast_lock);
}

static struct net_bridge_group_eht_host *
__eht_lookup_create_host(struct net_bridge_port_group *pg,
			 union net_bridge_eht_addr *h_addr,
			 unsigned char filter_mode)
{
	struct rb_node **link = &pg->eht_host_tree.rb_node, *parent = NULL;
	struct net_bridge_group_eht_host *eht_host;

	while (*link) {
		struct net_bridge_group_eht_host *this;
		int result;

		this = rb_entry(*link, struct net_bridge_group_eht_host,
				rb_node);
		result = memcmp(h_addr, &this->h_addr, sizeof(*h_addr));
		parent = *link;
		if (result < 0)
			link = &((*link)->rb_left);
		else if (result > 0)
			link = &((*link)->rb_right);
		else
			return this;
	}

	eht_host = kzalloc(sizeof(*eht_host), GFP_ATOMIC);
	if (!eht_host)
		return NULL;

	memcpy(&eht_host->h_addr, h_addr, sizeof(*h_addr));
	INIT_HLIST_HEAD(&eht_host->set_entries);
	eht_host->pg = pg;
	eht_host->filter_mode = filter_mode;

	rb_link_node(&eht_host->rb_node, parent, link);
	rb_insert_color(&eht_host->rb_node, &pg->eht_host_tree);

	return eht_host;
}

static struct net_bridge_group_eht_set_entry *
__eht_lookup_create_set_entry(struct net_bridge *br,
			      struct net_bridge_group_eht_set *eht_set,
			      struct net_bridge_group_eht_host *eht_host,
			      bool allow_zero_src)
{
	struct rb_node **link = &eht_set->entry_tree.rb_node, *parent = NULL;
	struct net_bridge_group_eht_set_entry *set_h;

	while (*link) {
		struct net_bridge_group_eht_set_entry *this;
		int result;

		this = rb_entry(*link, struct net_bridge_group_eht_set_entry,
				rb_node);
		result = memcmp(&eht_host->h_addr, &this->h_addr,
				sizeof(union net_bridge_eht_addr));
		parent = *link;
		if (result < 0)
			link = &((*link)->rb_left);
		else if (result > 0)
			link = &((*link)->rb_right);
		else
			return this;
	}

	/* always allow auto-created zero entry */
	if (!allow_zero_src && eht_host->num_entries >= PG_SRC_ENT_LIMIT)
		return NULL;

	set_h = kzalloc(sizeof(*set_h), GFP_ATOMIC);
	if (!set_h)
		return NULL;

	memcpy(&set_h->h_addr, &eht_host->h_addr,
	       sizeof(union net_bridge_eht_addr));
	set_h->mcast_gc.destroy = br_multicast_destroy_eht_set_entry;
	set_h->eht_set = eht_set;
	set_h->h_parent = eht_host;
	set_h->br = br;
	timer_setup(&set_h->timer, br_multicast_eht_set_entry_expired, 0);

	hlist_add_head(&set_h->host_list, &eht_host->set_entries);
	rb_link_node(&set_h->rb_node, parent, link);
	rb_insert_color(&set_h->rb_node, &eht_set->entry_tree);
	/* we must not count the auto-created zero entry otherwise we won't be
	 * able to track the full list of PG_SRC_ENT_LIMIT entries
	 */
	if (!allow_zero_src)
		eht_host->num_entries++;

	return set_h;
}

static struct net_bridge_group_eht_set *
__eht_lookup_create_set(struct net_bridge_port_group *pg,
			union net_bridge_eht_addr *src_addr)
{
	struct rb_node **link = &pg->eht_set_tree.rb_node, *parent = NULL;
	struct net_bridge_group_eht_set *eht_set;

	while (*link) {
		struct net_bridge_group_eht_set *this;
		int result;

		this = rb_entry(*link, struct net_bridge_group_eht_set,
				rb_node);
		result = memcmp(src_addr, &this->src_addr, sizeof(*src_addr));
		parent = *link;
		if (result < 0)
			link = &((*link)->rb_left);
		else if (result > 0)
			link = &((*link)->rb_right);
		else
			return this;
	}

	eht_set = kzalloc(sizeof(*eht_set), GFP_ATOMIC);
	if (!eht_set)
		return NULL;

	memcpy(&eht_set->src_addr, src_addr, sizeof(*src_addr));
	eht_set->mcast_gc.destroy = br_multicast_destroy_eht_set;
	eht_set->pg = pg;
	eht_set->br = pg->key.port->br;
	eht_set->entry_tree = RB_ROOT;
	timer_setup(&eht_set->timer, br_multicast_eht_set_expired, 0);

	rb_link_node(&eht_set->rb_node, parent, link);
	rb_insert_color(&eht_set->rb_node, &pg->eht_set_tree);

	return eht_set;
}

static void br_multicast_create_eht_set_entry(struct net_bridge_port_group *pg,
					      union net_bridge_eht_addr *src_addr,
					      union net_bridge_eht_addr *h_addr,
					      int filter_mode,
					      bool allow_zero_src)
{
	struct net_bridge_group_eht_set_entry *set_h;
	struct net_bridge_group_eht_host *eht_host;
	struct net_bridge *br = pg->key.port->br;
	struct net_bridge_group_eht_set *eht_set;
	union net_bridge_eht_addr zero_addr;

	memset(&zero_addr, 0, sizeof(zero_addr));
	if (!allow_zero_src && !memcmp(src_addr, &zero_addr, sizeof(zero_addr)))
		return;

	eht_set = __eht_lookup_create_set(pg, src_addr);
	if (!eht_set)
		return;

	eht_host = __eht_lookup_create_host(pg, h_addr, filter_mode);
	if (!eht_host)
		goto fail_host;

	set_h = __eht_lookup_create_set_entry(br, eht_set, eht_host,
					      allow_zero_src);
	if (!set_h)
		goto fail_set_entry;

	mod_timer(&set_h->timer, jiffies + br_multicast_gmi(br));
	mod_timer(&eht_set->timer, jiffies + br_multicast_gmi(br));

	return;

fail_set_entry:
	if (hlist_empty(&eht_host->set_entries))
		__eht_destroy_host(eht_host);
fail_host:
	if (RB_EMPTY_ROOT(&eht_set->entry_tree))
		br_multicast_del_eht_set(eht_set);
}

static bool br_multicast_del_eht_set_entry(struct net_bridge_port_group *pg,
					   union net_bridge_eht_addr *src_addr,
					   union net_bridge_eht_addr *h_addr)
{
	struct net_bridge_group_eht_set_entry *set_h;
	struct net_bridge_group_eht_set *eht_set;
	bool set_deleted = false;

	eht_set = br_multicast_eht_set_lookup(pg, src_addr);
	if (!eht_set)
		goto out;

	set_h = br_multicast_eht_set_entry_lookup(eht_set, h_addr);
	if (!set_h)
		goto out;

	__eht_del_set_entry(set_h);

	if (RB_EMPTY_ROOT(&eht_set->entry_tree)) {
		br_multicast_del_eht_set(eht_set);
		set_deleted = true;
	}

out:
	return set_deleted;
}
