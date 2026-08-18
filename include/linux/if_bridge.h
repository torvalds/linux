/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
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
	} src;
	union {
		__be32	ip4;
#if IS_ENABLED(CONFIG_IPV6)
		struct in6_addr ip6;
#endif
		unsigned char	mac_addr[ETH_ALEN];
	} dst;
	__be16		proto;
	__u16           vid;
};

struct br_ip_list {
	struct list_head list;
	struct br_ip addr;
};

enum bridge_flags_bit {
	BR_HAIRPIN_MODE_BIT,
	BR_BPDU_GUARD_BIT,
	BR_ROOT_BLOCK_BIT,
	BR_MULTICAST_FAST_LEAVE_BIT,
	BR_ADMIN_COST_BIT,
	BR_LEARNING_BIT,
	BR_FLOOD_BIT,
	BR_PROMISC_BIT,
	BR_PROXYARP_BIT,
	BR_LEARNING_SYNC_BIT,
	BR_PROXYARP_WIFI_BIT,
	BR_MCAST_FLOOD_BIT,
	BR_MULTICAST_TO_UNICAST_BIT,
	BR_VLAN_TUNNEL_BIT,
	BR_BCAST_FLOOD_BIT,
	BR_NEIGH_SUPPRESS_BIT,
	BR_ISOLATED_BIT,
	BR_MRP_AWARE_BIT,
	BR_MRP_LOST_CONT_BIT,
	BR_MRP_LOST_IN_CONT_BIT,
	BR_TX_FWD_OFFLOAD_BIT,
	BR_PORT_LOCKED_BIT,
	BR_PORT_MAB_BIT,
	BR_NEIGH_VLAN_SUPPRESS_BIT,
	BR_NEIGH_FORWARD_GRAT_BIT,
};

#define BR_HAIRPIN_MODE		BIT(BR_HAIRPIN_MODE_BIT)
#define BR_BPDU_GUARD		BIT(BR_BPDU_GUARD_BIT)
#define BR_ROOT_BLOCK		BIT(BR_ROOT_BLOCK_BIT)
#define BR_MULTICAST_FAST_LEAVE	BIT(BR_MULTICAST_FAST_LEAVE_BIT)
#define BR_ADMIN_COST		BIT(BR_ADMIN_COST_BIT)
#define BR_LEARNING		BIT(BR_LEARNING_BIT)
#define BR_FLOOD		BIT(BR_FLOOD_BIT)
#define BR_AUTO_MASK		(BR_FLOOD | BR_LEARNING)
#define BR_PROMISC		BIT(BR_PROMISC_BIT)
#define BR_PROXYARP		BIT(BR_PROXYARP_BIT)
#define BR_LEARNING_SYNC	BIT(BR_LEARNING_SYNC_BIT)
#define BR_PROXYARP_WIFI	BIT(BR_PROXYARP_WIFI_BIT)
#define BR_MCAST_FLOOD		BIT(BR_MCAST_FLOOD_BIT)
#define BR_MULTICAST_TO_UNICAST	BIT(BR_MULTICAST_TO_UNICAST_BIT)
#define BR_VLAN_TUNNEL		BIT(BR_VLAN_TUNNEL_BIT)
#define BR_BCAST_FLOOD		BIT(BR_BCAST_FLOOD_BIT)
#define BR_NEIGH_SUPPRESS	BIT(BR_NEIGH_SUPPRESS_BIT)
#define BR_ISOLATED		BIT(BR_ISOLATED_BIT)
#define BR_MRP_AWARE		BIT(BR_MRP_AWARE_BIT)
#define BR_MRP_LOST_CONT	BIT(BR_MRP_LOST_CONT_BIT)
#define BR_MRP_LOST_IN_CONT	BIT(BR_MRP_LOST_IN_CONT_BIT)
#define BR_TX_FWD_OFFLOAD	BIT(BR_TX_FWD_OFFLOAD_BIT)
#define BR_PORT_LOCKED		BIT(BR_PORT_LOCKED_BIT)
#define BR_PORT_MAB		BIT(BR_PORT_MAB_BIT)
#define BR_NEIGH_VLAN_SUPPRESS	BIT(BR_NEIGH_VLAN_SUPPRESS_BIT)
#define BR_NEIGH_FORWARD_GRAT	BIT(BR_NEIGH_FORWARD_GRAT_BIT)

#define BR_DEFAULT_AGEING_TIME	(300 * HZ)

struct net_bridge;
void brioctl_set(int (*hook)(struct net *net, unsigned int cmd,
			     void __user *uarg));
int br_ioctl_call(struct net *net, unsigned int cmd, void __user *uarg);

#if IS_ENABLED(CONFIG_BRIDGE) && IS_ENABLED(CONFIG_BRIDGE_IGMP_SNOOPING)
int br_multicast_list_adjacent(struct net_device *dev,
			       struct list_head *br_ip_list);
bool br_multicast_has_querier_anywhere(struct net_device *dev, int proto);
bool br_multicast_has_querier_adjacent(struct net_device *dev, int proto);
bool br_multicast_has_router_adjacent(struct net_device *dev, int proto);
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

static inline bool br_multicast_has_router_adjacent(struct net_device *dev,
						    int proto)
{
	return true;
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
int br_vlan_get_pvid_rcu(const struct net_device *dev, u16 *p_pvid);
int br_vlan_get_proto(const struct net_device *dev, u16 *p_proto);
int br_vlan_get_info(const struct net_device *dev, u16 vid,
		     struct bridge_vlan_info *p_vinfo);
int br_vlan_get_info_rcu(const struct net_device *dev, u16 vid,
			 struct bridge_vlan_info *p_vinfo);
bool br_mst_enabled(const struct net_device *dev);
int br_mst_get_info(const struct net_device *dev, u16 msti, unsigned long *vids);
int br_mst_get_state(const struct net_device *dev, u16 msti, u8 *state);
#else
static inline bool br_vlan_enabled(const struct net_device *dev)
{
	return false;
}

static inline int br_vlan_get_pvid(const struct net_device *dev, u16 *p_pvid)
{
	return -EINVAL;
}

static inline int br_vlan_get_proto(const struct net_device *dev, u16 *p_proto)
{
	return -EINVAL;
}

static inline int br_vlan_get_pvid_rcu(const struct net_device *dev, u16 *p_pvid)
{
	return -EINVAL;
}

static inline int br_vlan_get_info(const struct net_device *dev, u16 vid,
				   struct bridge_vlan_info *p_vinfo)
{
	return -EINVAL;
}

static inline int br_vlan_get_info_rcu(const struct net_device *dev, u16 vid,
				       struct bridge_vlan_info *p_vinfo)
{
	return -EINVAL;
}

static inline bool br_mst_enabled(const struct net_device *dev)
{
	return false;
}

static inline int br_mst_get_info(const struct net_device *dev, u16 msti,
				  unsigned long *vids)
{
	return -EINVAL;
}
static inline int br_mst_get_state(const struct net_device *dev, u16 msti,
				   u8 *state)
{
	return -EINVAL;
}
#endif

#if IS_ENABLED(CONFIG_BRIDGE)
struct net_device *br_fdb_find_port(const struct net_device *br_dev,
				    const unsigned char *addr,
				    __u16 vid);
void br_fdb_clear_offload(const struct net_device *dev, u16 vid);
bool br_port_flag_is_set(const struct net_device *dev, unsigned long flag);
u8 br_port_get_stp_state(const struct net_device *dev);
clock_t br_get_ageing_time(const struct net_device *br_dev);
#else
static inline struct net_device *
br_fdb_find_port(const struct net_device *br_dev,
		 const unsigned char *addr,
		 __u16 vid)
{
	return NULL;
}

static inline void br_fdb_clear_offload(const struct net_device *dev, u16 vid)
{
}

static inline bool
br_port_flag_is_set(const struct net_device *dev, unsigned long flag)
{
	return false;
}

static inline u8 br_port_get_stp_state(const struct net_device *dev)
{
	return BR_STATE_DISABLED;
}

static inline clock_t br_get_ageing_time(const struct net_device *br_dev)
{
	return 0;
}
#endif

#endif
