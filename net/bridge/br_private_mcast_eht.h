/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2020, Nikolay Aleksandrov <nikolay@nvidia.com>
 */
#ifndef _BR_PRIVATE_MCAST_EHT_H_
#define _BR_PRIVATE_MCAST_EHT_H_

#define BR_MCAST_DEFAULT_EHT_HOSTS_LIMIT 512

union net_bridge_eht_addr {
	__be32				ip4;
#if IS_ENABLED(CONFIG_IPV6)
	struct in6_addr			ip6;
#endif
};

/* single host's list of set entries and filter_mode */
struct net_bridge_group_eht_host {
	struct rb_node			rb_node;

	union net_bridge_eht_addr	h_addr;
	struct hlist_head		set_entries;
	unsigned int			num_entries;
	unsigned char			filter_mode;
	struct net_bridge_port_group	*pg;
};

/* (host, src entry) added to a per-src set and host's list */
struct net_bridge_group_eht_set_entry {
	struct rb_node			rb_node;
	struct hlist_node		host_list;

	union net_bridge_eht_addr	h_addr;
	struct timer_list		timer;
	struct net_bridge		*br;
	struct net_bridge_group_eht_set	*eht_set;
	struct net_bridge_group_eht_host *h_parent;
	struct net_bridge_mcast_gc	mcast_gc;
};

/* per-src set */
struct net_bridge_group_eht_set {
	struct rb_node			rb_node;

	union net_bridge_eht_addr	src_addr;
	struct rb_root			entry_tree;
	struct timer_list		timer;
	struct net_bridge_port_group	*pg;
	struct net_bridge		*br;
	struct net_bridge_mcast_gc	mcast_gc;
};

#ifdef CONFIG_BRIDGE_IGMP_SNOOPING
void br_multicast_eht_clean_sets(struct net_bridge_port_group *pg);
bool br_multicast_eht_handle(struct net_bridge_port_group *pg,
			     void *h_addr,
			     void *srcs,
			     u32 nsrcs,
			     size_t addr_size,
			     int grec_type);
int br_multicast_eht_set_hosts_limit(struct net_bridge_port *p,
				     u32 eht_hosts_limit);

static inline bool
br_multicast_eht_should_del_pg(const struct net_bridge_port_group *pg)
{
	return !!((pg->key.port->flags & BR_MULTICAST_FAST_LEAVE) &&
		  RB_EMPTY_ROOT(&pg->eht_host_tree));
}

static inline bool
br_multicast_eht_hosts_over_limit(const struct net_bridge_port_group *pg)
{
	const struct net_bridge_port *p = pg->key.port;

	return !!(p->multicast_eht_hosts_cnt >= p->multicast_eht_hosts_limit);
}

static inline void br_multicast_eht_hosts_inc(struct net_bridge_port_group *pg)
{
	struct net_bridge_port *p = pg->key.port;

	p->multicast_eht_hosts_cnt++;
}

static inline void br_multicast_eht_hosts_dec(struct net_bridge_port_group *pg)
{
	struct net_bridge_port *p = pg->key.port;

	p->multicast_eht_hosts_cnt--;
}
#endif /* CONFIG_BRIDGE_IGMP_SNOOPING */

#endif /* _BR_PRIVATE_MCAST_EHT_H_ */
