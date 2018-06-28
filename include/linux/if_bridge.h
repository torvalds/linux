/*
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */
#ifndef _LINUX_IF_BRIDGE_H
#define _LINUX_IF_BRIDGE_H


#include <linux/netdevice.h>
#include <uapi/linux/if_bridge.h>
#include <linux/bitops.h>

struct br_ip {
	union {
		__be32	ip4;
#if IS_ENABLED(CONFIG_IPV6)
		struct in6_addr ip6;
#endif
	} u;
	__be16		proto;
	__u16           vid;
};

struct br_ip_list {
	struct list_head list;
	struct br_ip addr;
};

#define BR_HAIRPIN_MODE		BIT(0)
#define BR_BPDU_GUARD		BIT(1)
#define BR_ROOT_BLOCK		BIT(2)
#define BR_MULTICAST_FAST_LEAVE	BIT(3)
#define BR_ADMIN_COST		BIT(4)
#define BR_LEARNING		BIT(5)
#define BR_FLOOD		BIT(6)
#define BR_AUTO_MASK		(BR_FLOOD | BR_LEARNING)
#define BR_PROMISC		BIT(7)
#define BR_PROXYARP		BIT(8)
#define BR_LEARNING_SYNC	BIT(9)
#define BR_PROXYARP_WIFI	BIT(10)
#define BR_MCAST_FLOOD		BIT(11)
#define BR_MULTICAST_TO_UNICAST	BIT(12)
#define BR_VLAN_TUNNEL		BIT(13)
#define BR_BCAST_FLOOD		BIT(14)
#define BR_NEIGH_SUPPRESS	BIT(15)
#define BR_ISOLATED		BIT(16)

#define BR_DEFAULT_AGEING_TIME	(300 * HZ)

extern void brioctl_set(int (*ioctl_hook)(struct net *, unsigned int, void __user *));

typedef int br_should_route_hook_t(struct sk_buff *skb);
extern br_should_route_hook_t __rcu *br_should_route_hook;

#if IS_ENABLED(CONFIG_BRIDGE) && IS_ENABLED(CONFIG_BRIDGE_IGMP_SNOOPING)
int br_multicast_list_adjacent(struct net_device *dev,
			       struct list_head *br_ip_list);
bool br_multicast_has_querier_anywhere(struct net_device *dev, int proto);
bool br_multicast_has_querier_adjacent(struct net_device *dev, int proto);
bool br_multicast_enabled(const struct net_device *dev);
bool br_multicast_router(const struct net_device *dev);
#else
static inline int br_multicast_list_adjacent(struct net_device *dev,
					     struct list_head *br_ip_list)
{
	return 0;
}
static inline bool br_multicast_has_querier_anywhere(struct net_device *dev,
						     int proto)
{
	return false;
}
static inline bool br_multicast_has_querier_adjacent(struct net_device *dev,
						     int proto)
{
	return false;
}
static inline bool br_multicast_enabled(const struct net_device *dev)
{
	return false;
}
static inline bool br_multicast_router(const struct net_device *dev)
{
	return false;
}
#endif

#if IS_ENABLED(CONFIG_BRIDGE) && IS_ENABLED(CONFIG_BRIDGE_VLAN_FILTERING)
bool br_vlan_enabled(const struct net_device *dev);
int br_vlan_get_pvid(const struct net_device *dev, u16 *p_pvid);
int br_vlan_get_info(const struct net_device *dev, u16 vid,
		     struct bridge_vlan_info *p_vinfo);
#else
static inline bool br_vlan_enabled(const struct net_device *dev)
{
	return false;
}

static inline int br_vlan_get_pvid(const struct net_device *dev, u16 *p_pvid)
{
	return -1;
}

static inline int br_vlan_get_info(const struct net_device *dev, u16 vid,
				   struct bridge_vlan_info *p_vinfo)
{
	return -1;
}
#endif

#if IS_ENABLED(CONFIG_BRIDGE)
struct net_device *br_fdb_find_port(const struct net_device *br_dev,
				    const unsigned char *addr,
				    __u16 vid);
#else
static inline struct net_device *
br_fdb_find_port(const struct net_device *br_dev,
		 const unsigned char *addr,
		 __u16 vid)
{
	return NULL;
}
#endif

#endif
