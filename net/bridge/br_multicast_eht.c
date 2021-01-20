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

static void __eht_destroy_host(struct net_bridge_group_eht_host *eht_host)
{
	WARN_ON(!hlist_empty(&eht_host->set_entries));

	rb_erase(&eht_host->rb_node, &eht_host->pg->eht_host_tree);
	RB_CLEAR_NODE(&eht_host->rb_node);
	kfree(eht_host);
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
