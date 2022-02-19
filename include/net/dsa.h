/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * include/net/dsa.h - Driver for Distributed Switch Architecture switch chips
 * Copyright (c) 2008-2009 Marvell Semiconductor
 */

#ifndef __LINUX_NET_DSA_H
#define __LINUX_NET_DSA_H

#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/list.h>
#include <linux/notifier.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/of.h>
#include <linux/ethtool.h>
#include <linux/net_tstamp.h>
#include <linux/phy.h>
#include <linux/platform_data/dsa.h>
#include <linux/phylink.h>
#include <net/devlink.h>
#include <net/switchdev.h>

struct tc_action;
struct phy_device;
struct fixed_phy_status;
struct phylink_link_state;

#define DSA_TAG_PROTO_NONE_VALUE		0
#define DSA_TAG_PROTO_BRCM_VALUE		1
#define DSA_TAG_PROTO_BRCM_PREPEND_VALUE	2
#define DSA_TAG_PROTO_DSA_VALUE			3
#define DSA_TAG_PROTO_EDSA_VALUE		4
#define DSA_TAG_PROTO_GSWIP_VALUE		5
#define DSA_TAG_PROTO_KSZ9477_VALUE		6
#define DSA_TAG_PROTO_KSZ9893_VALUE		7
#define DSA_TAG_PROTO_LAN9303_VALUE		8
#define DSA_TAG_PROTO_MTK_VALUE			9
#define DSA_TAG_PROTO_QCA_VALUE			10
#define DSA_TAG_PROTO_TRAILER_VALUE		11
#define DSA_TAG_PROTO_8021Q_VALUE		12
#define DSA_TAG_PROTO_SJA1105_VALUE		13
#define DSA_TAG_PROTO_KSZ8795_VALUE		14
#define DSA_TAG_PROTO_OCELOT_VALUE		15
#define DSA_TAG_PROTO_AR9331_VALUE		16
#define DSA_TAG_PROTO_RTL4_A_VALUE		17
#define DSA_TAG_PROTO_HELLCREEK_VALUE		18
#define DSA_TAG_PROTO_XRS700X_VALUE		19
#define DSA_TAG_PROTO_OCELOT_8021Q_VALUE	20
#define DSA_TAG_PROTO_SEVILLE_VALUE		21
#define DSA_TAG_PROTO_BRCM_LEGACY_VALUE		22
#define DSA_TAG_PROTO_SJA1110_VALUE		23
#define DSA_TAG_PROTO_RTL8_4_VALUE		24

enum dsa_tag_protocol {
	DSA_TAG_PROTO_NONE		= DSA_TAG_PROTO_NONE_VALUE,
	DSA_TAG_PROTO_BRCM		= DSA_TAG_PROTO_BRCM_VALUE,
	DSA_TAG_PROTO_BRCM_LEGACY	= DSA_TAG_PROTO_BRCM_LEGACY_VALUE,
	DSA_TAG_PROTO_BRCM_PREPEND	= DSA_TAG_PROTO_BRCM_PREPEND_VALUE,
	DSA_TAG_PROTO_DSA		= DSA_TAG_PROTO_DSA_VALUE,
	DSA_TAG_PROTO_EDSA		= DSA_TAG_PROTO_EDSA_VALUE,
	DSA_TAG_PROTO_GSWIP		= DSA_TAG_PROTO_GSWIP_VALUE,
	DSA_TAG_PROTO_KSZ9477		= DSA_TAG_PROTO_KSZ9477_VALUE,
	DSA_TAG_PROTO_KSZ9893		= DSA_TAG_PROTO_KSZ9893_VALUE,
	DSA_TAG_PROTO_LAN9303		= DSA_TAG_PROTO_LAN9303_VALUE,
	DSA_TAG_PROTO_MTK		= DSA_TAG_PROTO_MTK_VALUE,
	DSA_TAG_PROTO_QCA		= DSA_TAG_PROTO_QCA_VALUE,
	DSA_TAG_PROTO_TRAILER		= DSA_TAG_PROTO_TRAILER_VALUE,
	DSA_TAG_PROTO_8021Q		= DSA_TAG_PROTO_8021Q_VALUE,
	DSA_TAG_PROTO_SJA1105		= DSA_TAG_PROTO_SJA1105_VALUE,
	DSA_TAG_PROTO_KSZ8795		= DSA_TAG_PROTO_KSZ8795_VALUE,
	DSA_TAG_PROTO_OCELOT		= DSA_TAG_PROTO_OCELOT_VALUE,
	DSA_TAG_PROTO_AR9331		= DSA_TAG_PROTO_AR9331_VALUE,
	DSA_TAG_PROTO_RTL4_A		= DSA_TAG_PROTO_RTL4_A_VALUE,
	DSA_TAG_PROTO_HELLCREEK		= DSA_TAG_PROTO_HELLCREEK_VALUE,
	DSA_TAG_PROTO_XRS700X		= DSA_TAG_PROTO_XRS700X_VALUE,
	DSA_TAG_PROTO_OCELOT_8021Q	= DSA_TAG_PROTO_OCELOT_8021Q_VALUE,
	DSA_TAG_PROTO_SEVILLE		= DSA_TAG_PROTO_SEVILLE_VALUE,
	DSA_TAG_PROTO_SJA1110		= DSA_TAG_PROTO_SJA1110_VALUE,
	DSA_TAG_PROTO_RTL8_4		= DSA_TAG_PROTO_RTL8_4_VALUE,
};

struct dsa_switch;

struct dsa_device_ops {
	struct sk_buff *(*xmit)(struct sk_buff *skb, struct net_device *dev);
	struct sk_buff *(*rcv)(struct sk_buff *skb, struct net_device *dev);
	void (*flow_dissect)(const struct sk_buff *skb, __be16 *proto,
			     int *offset);
	int (*connect)(struct dsa_switch *ds);
	void (*disconnect)(struct dsa_switch *ds);
	unsigned int needed_headroom;
	unsigned int needed_tailroom;
	const char *name;
	enum dsa_tag_protocol proto;
	/* Some tagging protocols either mangle or shift the destination MAC
	 * address, in which case the DSA master would drop packets on ingress
	 * if what it understands out of the destination MAC address is not in
	 * its RX filter.
	 */
	bool promisc_on_master;
};

/* This structure defines the control interfaces that are overlayed by the
 * DSA layer on top of the DSA CPU/management net_device instance. This is
 * used by the core net_device layer while calling various net_device_ops
 * function pointers.
 */
struct dsa_netdevice_ops {
	int (*ndo_eth_ioctl)(struct net_device *dev, struct ifreq *ifr,
			     int cmd);
};

#define DSA_TAG_DRIVER_ALIAS "dsa_tag-"
#define MODULE_ALIAS_DSA_TAG_DRIVER(__proto)				\
	MODULE_ALIAS(DSA_TAG_DRIVER_ALIAS __stringify(__proto##_VALUE))

struct dsa_switch_tree {
	struct list_head	list;

	/* List of switch ports */
	struct list_head ports;

	/* Notifier chain for switch-wide events */
	struct raw_notifier_head	nh;

	/* Tree identifier */
	unsigned int index;

	/* Number of switches attached to this tree */
	struct kref refcount;

	/* Maps offloaded LAG netdevs to a zero-based linear ID for
	 * drivers that need it.
	 */
	struct net_device **lags;

	/* Tagging protocol operations */
	const struct dsa_device_ops *tag_ops;

	/* Default tagging protocol preferred by the switches in this
	 * tree.
	 */
	enum dsa_tag_protocol default_proto;

	/* Has this tree been applied to the hardware? */
	bool setup;

	/*
	 * Configuration data for the platform device that owns
	 * this dsa switch tree instance.
	 */
	struct dsa_platform_data	*pd;

	/* List of DSA links composing the routing table */
	struct list_head rtable;

	/* Length of "lags" array */
	unsigned int lags_len;

	/* Track the largest switch index within a tree */
	unsigned int last_switch;
};

#define dsa_lags_foreach_id(_id, _dst)				\
	for ((_id) = 0; (_id) < (_dst)->lags_len; (_id)++)	\
		if ((_dst)->lags[(_id)])

#define dsa_lag_foreach_port(_dp, _dst, _lag)			\
	list_for_each_entry((_dp), &(_dst)->ports, list)	\
		if ((_dp)->lag_dev == (_lag))

#define dsa_hsr_foreach_port(_dp, _ds, _hsr)			\
	list_for_each_entry((_dp), &(_ds)->dst->ports, list)	\
		if ((_dp)->ds == (_ds) && (_dp)->hsr_dev == (_hsr))

static inline struct net_device *dsa_lag_dev(struct dsa_switch_tree *dst,
					     unsigned int id)
{
	return dst->lags[id];
}

static inline int dsa_lag_id(struct dsa_switch_tree *dst,
			     struct net_device *lag)
{
	unsigned int id;

	dsa_lags_foreach_id(id, dst) {
		if (dsa_lag_dev(dst, id) == lag)
			return id;
	}

	return -ENODEV;
}

/* TC matchall action types */
enum dsa_port_mall_action_type {
	DSA_PORT_MALL_MIRROR,
	DSA_PORT_MALL_POLICER,
};

/* TC mirroring entry */
struct dsa_mall_mirror_tc_entry {
	u8 to_local_port;
	bool ingress;
};

/* TC port policer entry */
struct dsa_mall_policer_tc_entry {
	u32 burst;
	u64 rate_bytes_per_sec;
};

/* TC matchall entry */
struct dsa_mall_tc_entry {
	struct list_head list;
	unsigned long cookie;
	enum dsa_port_mall_action_type type;
	union {
		struct dsa_mall_mirror_tc_entry mirror;
		struct dsa_mall_policer_tc_entry policer;
	};
};

struct dsa_bridge {
	struct net_device *dev;
	unsigned int num;
	bool tx_fwd_offload;
	refcount_t refcount;
};

struct dsa_port {
	/* A CPU port is physically connected to a master device.
	 * A user port exposed to userspace has a slave device.
	 */
	union {
		struct net_device *master;
		struct net_device *slave;
	};

	/* Copy of the tagging protocol operations, for quicker access
	 * in the data path. Valid only for the CPU ports.
	 */
	const struct dsa_device_ops *tag_ops;

	/* Copies for faster access in master receive hot path */
	struct dsa_switch_tree *dst;
	struct sk_buff *(*rcv)(struct sk_buff *skb, struct net_device *dev);

	struct dsa_switch	*ds;

	unsigned int		index;

	enum {
		DSA_PORT_TYPE_UNUSED = 0,
		DSA_PORT_TYPE_CPU,
		DSA_PORT_TYPE_DSA,
		DSA_PORT_TYPE_USER,
	} type;

	const char		*name;
	struct dsa_port		*cpu_dp;
	u8			mac[ETH_ALEN];

	u8			stp_state;

	/* Warning: the following bit fields are not atomic, and updating them
	 * can only be done from code paths where concurrency is not possible
	 * (probe time or under rtnl_lock).
	 */
	u8			vlan_filtering:1;

	/* Managed by DSA on user ports and by drivers on CPU and DSA ports */
	u8			learning:1;

	u8			lag_tx_enabled:1;

	u8			devlink_port_setup:1;

	/* Master state bits, valid only on CPU ports */
	u8			master_admin_up:1;
	u8			master_oper_up:1;

	u8			setup:1;

	struct device_node	*dn;
	unsigned int		ageing_time;

	struct dsa_bridge	*bridge;
	struct devlink_port	devlink_port;
	struct phylink		*pl;
	struct phylink_config	pl_config;
	struct net_device	*lag_dev;
	struct net_device	*hsr_dev;

	struct list_head list;

	/*
	 * Original copy of the master netdev ethtool_ops
	 */
	const struct ethtool_ops *orig_ethtool_ops;

	/*
	 * Original copy of the master netdev net_device_ops
	 */
	const struct dsa_netdevice_ops *netdev_ops;

	/* List of MAC addresses that must be forwarded on this port.
	 * These are only valid on CPU ports and DSA links.
	 */
	struct mutex		addr_lists_lock;
	struct list_head	fdbs;
	struct list_head	mdbs;

	/* List of VLANs that CPU and DSA ports are members of. */
	struct mutex		vlans_lock;
	struct list_head	vlans;
};

/* TODO: ideally DSA ports would have a single dp->link_dp member,
 * and no dst->rtable nor this struct dsa_link would be needed,
 * but this would require some more complex tree walking,
 * so keep it stupid at the moment and list them all.
 */
struct dsa_link {
	struct dsa_port *dp;
	struct dsa_port *link_dp;
	struct list_head list;
};

struct dsa_mac_addr {
	unsigned char addr[ETH_ALEN];
	u16 vid;
	refcount_t refcount;
	struct list_head list;
};

struct dsa_vlan {
	u16 vid;
	refcount_t refcount;
	struct list_head list;
};

struct dsa_switch {
	struct device *dev;

	/*
	 * Parent switch tree, and switch index.
	 */
	struct dsa_switch_tree	*dst;
	unsigned int		index;

	/* Warning: the following bit fields are not atomic, and updating them
	 * can only be done from code paths where concurrency is not possible
	 * (probe time or under rtnl_lock).
	 */
	u32			setup:1;

	/* Disallow bridge core from requesting different VLAN awareness
	 * settings on ports if not hardware-supported
	 */
	u32			vlan_filtering_is_global:1;

	/* Keep VLAN filtering enabled on ports not offloading any upper */
	u32			needs_standalone_vlan_filtering:1;

	/* Pass .port_vlan_add and .port_vlan_del to drivers even for bridges
	 * that have vlan_filtering=0. All drivers should ideally set this (and
	 * then the option would get removed), but it is unknown whether this
	 * would break things or not.
	 */
	u32			configure_vlan_while_not_filtering:1;

	/* If the switch driver always programs the CPU port as egress tagged
	 * despite the VLAN configuration indicating otherwise, then setting
	 * @untag_bridge_pvid will force the DSA receive path to pop the
	 * bridge's default_pvid VLAN tagged frames to offer a consistent
	 * behavior between a vlan_filtering=0 and vlan_filtering=1 bridge
	 * device.
	 */
	u32			untag_bridge_pvid:1;

	/* Let DSA manage the FDB entries towards the
	 * CPU, based on the software bridge database.
	 */
	u32			assisted_learning_on_cpu_port:1;

	/* In case vlan_filtering_is_global is set, the VLAN awareness state
	 * should be retrieved from here and not from the per-port settings.
	 */
	u32			vlan_filtering:1;

	/* For switches that only have the MRU configurable. To ensure the
	 * configured MTU is not exceeded, normalization of MRU on all bridged
	 * interfaces is needed.
	 */
	u32			mtu_enforcement_ingress:1;

	/* Listener for switch fabric events */
	struct notifier_block	nb;

	/*
	 * Give the switch driver somewhere to hang its private data
	 * structure.
	 */
	void *priv;

	void *tagger_data;

	/*
	 * Configuration data for this switch.
	 */
	struct dsa_chip_data	*cd;

	/*
	 * The switch operations.
	 */
	const struct dsa_switch_ops	*ops;

	/*
	 * Slave mii_bus and devices for the individual ports.
	 */
	u32			phys_mii_mask;
	struct mii_bus		*slave_mii_bus;

	/* Ageing Time limits in msecs */
	unsigned int ageing_time_min;
	unsigned int ageing_time_max;

	/* Storage for drivers using tag_8021q */
	struct dsa_8021q_context *tag_8021q_ctx;

	/* devlink used to represent this switch device */
	struct devlink		*devlink;

	/* Number of switch port queues */
	unsigned int		num_tx_queues;

	/* Drivers that benefit from having an ID associated with each
	 * offloaded LAG should set this to the maximum number of
	 * supported IDs. DSA will then maintain a mapping of _at
	 * least_ these many IDs, accessible to drivers via
	 * dsa_lag_id().
	 */
	unsigned int		num_lag_ids;

	/* Drivers that support bridge forwarding offload or FDB isolation
	 * should set this to the maximum number of bridges spanning the same
	 * switch tree (or all trees, in the case of cross-tree bridging
	 * support) that can be offloaded.
	 */
	unsigned int		max_num_bridges;

	unsigned int		num_ports;
};

static inline struct dsa_port *dsa_to_port(struct dsa_switch *ds, int p)
{
	struct dsa_switch_tree *dst = ds->dst;
	struct dsa_port *dp;

	list_for_each_entry(dp, &dst->ports, list)
		if (dp->ds == ds && dp->index == p)
			return dp;

	return NULL;
}

static inline bool dsa_port_is_dsa(struct dsa_port *port)
{
	return port->type == DSA_PORT_TYPE_DSA;
}

static inline bool dsa_port_is_cpu(struct dsa_port *port)
{
	return port->type == DSA_PORT_TYPE_CPU;
}

static inline bool dsa_port_is_user(struct dsa_port *dp)
{
	return dp->type == DSA_PORT_TYPE_USER;
}

static inline bool dsa_port_is_unused(struct dsa_port *dp)
{
	return dp->type == DSA_PORT_TYPE_UNUSED;
}

static inline bool dsa_port_master_is_operational(struct dsa_port *dp)
{
	return dsa_port_is_cpu(dp) && dp->master_admin_up &&
	       dp->master_oper_up;
}

static inline bool dsa_is_unused_port(struct dsa_switch *ds, int p)
{
	return dsa_to_port(ds, p)->type == DSA_PORT_TYPE_UNUSED;
}

static inline bool dsa_is_cpu_port(struct dsa_switch *ds, int p)
{
	return dsa_to_port(ds, p)->type == DSA_PORT_TYPE_CPU;
}

static inline bool dsa_is_dsa_port(struct dsa_switch *ds, int p)
{
	return dsa_to_port(ds, p)->type == DSA_PORT_TYPE_DSA;
}

static inline bool dsa_is_user_port(struct dsa_switch *ds, int p)
{
	return dsa_to_port(ds, p)->type == DSA_PORT_TYPE_USER;
}

#define dsa_tree_for_each_user_port(_dp, _dst) \
	list_for_each_entry((_dp), &(_dst)->ports, list) \
		if (dsa_port_is_user((_dp)))

#define dsa_switch_for_each_port(_dp, _ds) \
	list_for_each_entry((_dp), &(_ds)->dst->ports, list) \
		if ((_dp)->ds == (_ds))

#define dsa_switch_for_each_port_safe(_dp, _next, _ds) \
	list_for_each_entry_safe((_dp), (_next), &(_ds)->dst->ports, list) \
		if ((_dp)->ds == (_ds))

#define dsa_switch_for_each_port_continue_reverse(_dp, _ds) \
	list_for_each_entry_continue_reverse((_dp), &(_ds)->dst->ports, list) \
		if ((_dp)->ds == (_ds))

#define dsa_switch_for_each_available_port(_dp, _ds) \
	dsa_switch_for_each_port((_dp), (_ds)) \
		if (!dsa_port_is_unused((_dp)))

#define dsa_switch_for_each_user_port(_dp, _ds) \
	dsa_switch_for_each_port((_dp), (_ds)) \
		if (dsa_port_is_user((_dp)))

#define dsa_switch_for_each_cpu_port(_dp, _ds) \
	dsa_switch_for_each_port((_dp), (_ds)) \
		if (dsa_port_is_cpu((_dp)))

static inline u32 dsa_user_ports(struct dsa_switch *ds)
{
	struct dsa_port *dp;
	u32 mask = 0;

	dsa_switch_for_each_user_port(dp, ds)
		mask |= BIT(dp->index);

	return mask;
}

/* Return the local port used to reach an arbitrary switch device */
static inline unsigned int dsa_routing_port(struct dsa_switch *ds, int device)
{
	struct dsa_switch_tree *dst = ds->dst;
	struct dsa_link *dl;

	list_for_each_entry(dl, &dst->rtable, list)
		if (dl->dp->ds == ds && dl->link_dp->ds->index == device)
			return dl->dp->index;

	return ds->num_ports;
}

/* Return the local port used to reach an arbitrary switch port */
static inline unsigned int dsa_towards_port(struct dsa_switch *ds, int device,
					    int port)
{
	if (device == ds->index)
		return port;
	else
		return dsa_routing_port(ds, device);
}

/* Return the local port used to reach the dedicated CPU port */
static inline unsigned int dsa_upstream_port(struct dsa_switch *ds, int port)
{
	const struct dsa_port *dp = dsa_to_port(ds, port);
	const struct dsa_port *cpu_dp = dp->cpu_dp;

	if (!cpu_dp)
		return port;

	return dsa_towards_port(ds, cpu_dp->ds->index, cpu_dp->index);
}

/* Return true if this is the local port used to reach the CPU port */
static inline bool dsa_is_upstream_port(struct dsa_switch *ds, int port)
{
	if (dsa_is_unused_port(ds, port))
		return false;

	return port == dsa_upstream_port(ds, port);
}

/* Return true if this is a DSA port leading away from the CPU */
static inline bool dsa_is_downstream_port(struct dsa_switch *ds, int port)
{
	return dsa_is_dsa_port(ds, port) && !dsa_is_upstream_port(ds, port);
}

/* Return the local port used to reach the CPU port */
static inline unsigned int dsa_switch_upstream_port(struct dsa_switch *ds)
{
	struct dsa_port *dp;

	dsa_switch_for_each_available_port(dp, ds) {
		return dsa_upstream_port(ds, dp->index);
	}

	return ds->num_ports;
}

/* Return true if @upstream_ds is an upstream switch of @downstream_ds, meaning
 * that the routing port from @downstream_ds to @upstream_ds is also the port
 * which @downstream_ds uses to reach its dedicated CPU.
 */
static inline bool dsa_switch_is_upstream_of(struct dsa_switch *upstream_ds,
					     struct dsa_switch *downstream_ds)
{
	int routing_port;

	if (upstream_ds == downstream_ds)
		return true;

	routing_port = dsa_routing_port(downstream_ds, upstream_ds->index);

	return dsa_is_upstream_port(downstream_ds, routing_port);
}

static inline bool dsa_port_is_vlan_filtering(const struct dsa_port *dp)
{
	const struct dsa_switch *ds = dp->ds;

	if (ds->vlan_filtering_is_global)
		return ds->vlan_filtering;
	else
		return dp->vlan_filtering;
}

static inline
struct net_device *dsa_port_to_bridge_port(const struct dsa_port *dp)
{
	if (!dp->bridge)
		return NULL;

	if (dp->lag_dev)
		return dp->lag_dev;
	else if (dp->hsr_dev)
		return dp->hsr_dev;

	return dp->slave;
}

static inline struct net_device *
dsa_port_bridge_dev_get(const struct dsa_port *dp)
{
	return dp->bridge ? dp->bridge->dev : NULL;
}

static inline unsigned int dsa_port_bridge_num_get(struct dsa_port *dp)
{
	return dp->bridge ? dp->bridge->num : 0;
}

static inline bool dsa_port_bridge_same(const struct dsa_port *a,
					const struct dsa_port *b)
{
	struct net_device *br_a = dsa_port_bridge_dev_get(a);
	struct net_device *br_b = dsa_port_bridge_dev_get(b);

	/* Standalone ports are not in the same bridge with one another */
	return (!br_a || !br_b) ? false : (br_a == br_b);
}

static inline bool dsa_port_offloads_bridge_port(struct dsa_port *dp,
						 const struct net_device *dev)
{
	return dsa_port_to_bridge_port(dp) == dev;
}

static inline bool
dsa_port_offloads_bridge_dev(struct dsa_port *dp,
			     const struct net_device *bridge_dev)
{
	/* DSA ports connected to a bridge, and event was emitted
	 * for the bridge.
	 */
	return dsa_port_bridge_dev_get(dp) == bridge_dev;
}

static inline bool dsa_port_offloads_bridge(struct dsa_port *dp,
					    const struct dsa_bridge *bridge)
{
	return dsa_port_bridge_dev_get(dp) == bridge->dev;
}

/* Returns true if any port of this tree offloads the given net_device */
static inline bool dsa_tree_offloads_bridge_port(struct dsa_switch_tree *dst,
						 const struct net_device *dev)
{
	struct dsa_port *dp;

	list_for_each_entry(dp, &dst->ports, list)
		if (dsa_port_offloads_bridge_port(dp, dev))
			return true;

	return false;
}

/* Returns true if any port of this tree offloads the given bridge */
static inline bool
dsa_tree_offloads_bridge_dev(struct dsa_switch_tree *dst,
			     const struct net_device *bridge_dev)
{
	struct dsa_port *dp;

	list_for_each_entry(dp, &dst->ports, list)
		if (dsa_port_offloads_bridge_dev(dp, bridge_dev))
			return true;

	return false;
}

typedef int dsa_fdb_dump_cb_t(const unsigned char *addr, u16 vid,
			      bool is_static, void *data);
struct dsa_switch_ops {
	/*
	 * Tagging protocol helpers called for the CPU ports and DSA links.
	 * @get_tag_protocol retrieves the initial tagging protocol and is
	 * mandatory. Switches which can operate using multiple tagging
	 * protocols should implement @change_tag_protocol and report in
	 * @get_tag_protocol the tagger in current use.
	 */
	enum dsa_tag_protocol (*get_tag_protocol)(struct dsa_switch *ds,
						  int port,
						  enum dsa_tag_protocol mprot);
	int	(*change_tag_protocol)(struct dsa_switch *ds, int port,
				       enum dsa_tag_protocol proto);
	/*
	 * Method for switch drivers to connect to the tagging protocol driver
	 * in current use. The switch driver can provide handlers for certain
	 * types of packets for switch management.
	 */
	int	(*connect_tag_protocol)(struct dsa_switch *ds,
					enum dsa_tag_protocol proto);

	/* Optional switch-wide initialization and destruction methods */
	int	(*setup)(struct dsa_switch *ds);
	void	(*teardown)(struct dsa_switch *ds);

	/* Per-port initialization and destruction methods. Mandatory if the
	 * driver registers devlink port regions, optional otherwise.
	 */
	int	(*port_setup)(struct dsa_switch *ds, int port);
	void	(*port_teardown)(struct dsa_switch *ds, int port);

	u32	(*get_phy_flags)(struct dsa_switch *ds, int port);

	/*
	 * Access to the switch's PHY registers.
	 */
	int	(*phy_read)(struct dsa_switch *ds, int port, int regnum);
	int	(*phy_write)(struct dsa_switch *ds, int port,
			     int regnum, u16 val);

	/*
	 * Link state adjustment (called from libphy)
	 */
	void	(*adjust_link)(struct dsa_switch *ds, int port,
				struct phy_device *phydev);
	void	(*fixed_link_update)(struct dsa_switch *ds, int port,
				struct fixed_phy_status *st);

	/*
	 * PHYLINK integration
	 */
	void	(*phylink_get_caps)(struct dsa_switch *ds, int port,
				    struct phylink_config *config);
	void	(*phylink_validate)(struct dsa_switch *ds, int port,
				    unsigned long *supported,
				    struct phylink_link_state *state);
	struct phylink_pcs *(*phylink_mac_select_pcs)(struct dsa_switch *ds,
						      int port,
						      phy_interface_t iface);
	int	(*phylink_mac_link_state)(struct dsa_switch *ds, int port,
					  struct phylink_link_state *state);
	void	(*phylink_mac_config)(struct dsa_switch *ds, int port,
				      unsigned int mode,
				      const struct phylink_link_state *state);
	void	(*phylink_mac_an_restart)(struct dsa_switch *ds, int port);
	void	(*phylink_mac_link_down)(struct dsa_switch *ds, int port,
					 unsigned int mode,
					 phy_interface_t interface);
	void	(*phylink_mac_link_up)(struct dsa_switch *ds, int port,
				       unsigned int mode,
				       phy_interface_t interface,
				       struct phy_device *phydev,
				       int speed, int duplex,
				       bool tx_pause, bool rx_pause);
	void	(*phylink_fixed_state)(struct dsa_switch *ds, int port,
				       struct phylink_link_state *state);
	/*
	 * Port statistics counters.
	 */
	void	(*get_strings)(struct dsa_switch *ds, int port,
			       u32 stringset, uint8_t *data);
	void	(*get_ethtool_stats)(struct dsa_switch *ds,
				     int port, uint64_t *data);
	int	(*get_sset_count)(struct dsa_switch *ds, int port, int sset);
	void	(*get_ethtool_phy_stats)(struct dsa_switch *ds,
					 int port, uint64_t *data);
	void	(*get_eth_phy_stats)(struct dsa_switch *ds, int port,
				     struct ethtool_eth_phy_stats *phy_stats);
	void	(*get_eth_mac_stats)(struct dsa_switch *ds, int port,
				     struct ethtool_eth_mac_stats *mac_stats);
	void	(*get_eth_ctrl_stats)(struct dsa_switch *ds, int port,
				      struct ethtool_eth_ctrl_stats *ctrl_stats);
	void	(*get_stats64)(struct dsa_switch *ds, int port,
				   struct rtnl_link_stats64 *s);
	void	(*self_test)(struct dsa_switch *ds, int port,
			     struct ethtool_test *etest, u64 *data);

	/*
	 * ethtool Wake-on-LAN
	 */
	void	(*get_wol)(struct dsa_switch *ds, int port,
			   struct ethtool_wolinfo *w);
	int	(*set_wol)(struct dsa_switch *ds, int port,
			   struct ethtool_wolinfo *w);

	/*
	 * ethtool timestamp info
	 */
	int	(*get_ts_info)(struct dsa_switch *ds, int port,
			       struct ethtool_ts_info *ts);

	/*
	 * Suspend and resume
	 */
	int	(*suspend)(struct dsa_switch *ds);
	int	(*resume)(struct dsa_switch *ds);

	/*
	 * Port enable/disable
	 */
	int	(*port_enable)(struct dsa_switch *ds, int port,
			       struct phy_device *phy);
	void	(*port_disable)(struct dsa_switch *ds, int port);

	/*
	 * Port's MAC EEE settings
	 */
	int	(*set_mac_eee)(struct dsa_switch *ds, int port,
			       struct ethtool_eee *e);
	int	(*get_mac_eee)(struct dsa_switch *ds, int port,
			       struct ethtool_eee *e);

	/* EEPROM access */
	int	(*get_eeprom_len)(struct dsa_switch *ds);
	int	(*get_eeprom)(struct dsa_switch *ds,
			      struct ethtool_eeprom *eeprom, u8 *data);
	int	(*set_eeprom)(struct dsa_switch *ds,
			      struct ethtool_eeprom *eeprom, u8 *data);

	/*
	 * Register access.
	 */
	int	(*get_regs_len)(struct dsa_switch *ds, int port);
	void	(*get_regs)(struct dsa_switch *ds, int port,
			    struct ethtool_regs *regs, void *p);

	/*
	 * Upper device tracking.
	 */
	int	(*port_prechangeupper)(struct dsa_switch *ds, int port,
				       struct netdev_notifier_changeupper_info *info);

	/*
	 * Bridge integration
	 */
	int	(*set_ageing_time)(struct dsa_switch *ds, unsigned int msecs);
	int	(*port_bridge_join)(struct dsa_switch *ds, int port,
				    struct dsa_bridge bridge,
				    bool *tx_fwd_offload);
	void	(*port_bridge_leave)(struct dsa_switch *ds, int port,
				     struct dsa_bridge bridge);
	void	(*port_stp_state_set)(struct dsa_switch *ds, int port,
				      u8 state);
	void	(*port_fast_age)(struct dsa_switch *ds, int port);
	int	(*port_pre_bridge_flags)(struct dsa_switch *ds, int port,
					 struct switchdev_brport_flags flags,
					 struct netlink_ext_ack *extack);
	int	(*port_bridge_flags)(struct dsa_switch *ds, int port,
				     struct switchdev_brport_flags flags,
				     struct netlink_ext_ack *extack);

	/*
	 * VLAN support
	 */
	int	(*port_vlan_filtering)(struct dsa_switch *ds, int port,
				       bool vlan_filtering,
				       struct netlink_ext_ack *extack);
	int	(*port_vlan_add)(struct dsa_switch *ds, int port,
				 const struct switchdev_obj_port_vlan *vlan,
				 struct netlink_ext_ack *extack);
	int	(*port_vlan_del)(struct dsa_switch *ds, int port,
				 const struct switchdev_obj_port_vlan *vlan);
	/*
	 * Forwarding database
	 */
	int	(*port_fdb_add)(struct dsa_switch *ds, int port,
				const unsigned char *addr, u16 vid);
	int	(*port_fdb_del)(struct dsa_switch *ds, int port,
				const unsigned char *addr, u16 vid);
	int	(*port_fdb_dump)(struct dsa_switch *ds, int port,
				 dsa_fdb_dump_cb_t *cb, void *data);

	/*
	 * Multicast database
	 */
	int	(*port_mdb_add)(struct dsa_switch *ds, int port,
				const struct switchdev_obj_port_mdb *mdb);
	int	(*port_mdb_del)(struct dsa_switch *ds, int port,
				const struct switchdev_obj_port_mdb *mdb);
	/*
	 * RXNFC
	 */
	int	(*get_rxnfc)(struct dsa_switch *ds, int port,
			     struct ethtool_rxnfc *nfc, u32 *rule_locs);
	int	(*set_rxnfc)(struct dsa_switch *ds, int port,
			     struct ethtool_rxnfc *nfc);

	/*
	 * TC integration
	 */
	int	(*cls_flower_add)(struct dsa_switch *ds, int port,
				  struct flow_cls_offload *cls, bool ingress);
	int	(*cls_flower_del)(struct dsa_switch *ds, int port,
				  struct flow_cls_offload *cls, bool ingress);
	int	(*cls_flower_stats)(struct dsa_switch *ds, int port,
				    struct flow_cls_offload *cls, bool ingress);
	int	(*port_mirror_add)(struct dsa_switch *ds, int port,
				   struct dsa_mall_mirror_tc_entry *mirror,
				   bool ingress);
	void	(*port_mirror_del)(struct dsa_switch *ds, int port,
				   struct dsa_mall_mirror_tc_entry *mirror);
	int	(*port_policer_add)(struct dsa_switch *ds, int port,
				    struct dsa_mall_policer_tc_entry *policer);
	void	(*port_policer_del)(struct dsa_switch *ds, int port);
	int	(*port_setup_tc)(struct dsa_switch *ds, int port,
				 enum tc_setup_type type, void *type_data);

	/*
	 * Cross-chip operations
	 */
	int	(*crosschip_bridge_join)(struct dsa_switch *ds, int tree_index,
					 int sw_index, int port,
					 struct dsa_bridge bridge);
	void	(*crosschip_bridge_leave)(struct dsa_switch *ds, int tree_index,
					  int sw_index, int port,
					  struct dsa_bridge bridge);
	int	(*crosschip_lag_change)(struct dsa_switch *ds, int sw_index,
					int port);
	int	(*crosschip_lag_join)(struct dsa_switch *ds, int sw_index,
				      int port, struct net_device *lag,
				      struct netdev_lag_upper_info *info);
	int	(*crosschip_lag_leave)(struct dsa_switch *ds, int sw_index,
				       int port, struct net_device *lag);

	/*
	 * PTP functionality
	 */
	int	(*port_hwtstamp_get)(struct dsa_switch *ds, int port,
				     struct ifreq *ifr);
	int	(*port_hwtstamp_set)(struct dsa_switch *ds, int port,
				     struct ifreq *ifr);
	void	(*port_txtstamp)(struct dsa_switch *ds, int port,
				 struct sk_buff *skb);
	bool	(*port_rxtstamp)(struct dsa_switch *ds, int port,
				 struct sk_buff *skb, unsigned int type);

	/* Devlink parameters, etc */
	int	(*devlink_param_get)(struct dsa_switch *ds, u32 id,
				     struct devlink_param_gset_ctx *ctx);
	int	(*devlink_param_set)(struct dsa_switch *ds, u32 id,
				     struct devlink_param_gset_ctx *ctx);
	int	(*devlink_info_get)(struct dsa_switch *ds,
				    struct devlink_info_req *req,
				    struct netlink_ext_ack *extack);
	int	(*devlink_sb_pool_get)(struct dsa_switch *ds,
				       unsigned int sb_index, u16 pool_index,
				       struct devlink_sb_pool_info *pool_info);
	int	(*devlink_sb_pool_set)(struct dsa_switch *ds, unsigned int sb_index,
				       u16 pool_index, u32 size,
				       enum devlink_sb_threshold_type threshold_type,
				       struct netlink_ext_ack *extack);
	int	(*devlink_sb_port_pool_get)(struct dsa_switch *ds, int port,
					    unsigned int sb_index, u16 pool_index,
					    u32 *p_threshold);
	int	(*devlink_sb_port_pool_set)(struct dsa_switch *ds, int port,
					    unsigned int sb_index, u16 pool_index,
					    u32 threshold,
					    struct netlink_ext_ack *extack);
	int	(*devlink_sb_tc_pool_bind_get)(struct dsa_switch *ds, int port,
					       unsigned int sb_index, u16 tc_index,
					       enum devlink_sb_pool_type pool_type,
					       u16 *p_pool_index, u32 *p_threshold);
	int	(*devlink_sb_tc_pool_bind_set)(struct dsa_switch *ds, int port,
					       unsigned int sb_index, u16 tc_index,
					       enum devlink_sb_pool_type pool_type,
					       u16 pool_index, u32 threshold,
					       struct netlink_ext_ack *extack);
	int	(*devlink_sb_occ_snapshot)(struct dsa_switch *ds,
					   unsigned int sb_index);
	int	(*devlink_sb_occ_max_clear)(struct dsa_switch *ds,
					    unsigned int sb_index);
	int	(*devlink_sb_occ_port_pool_get)(struct dsa_switch *ds, int port,
						unsigned int sb_index, u16 pool_index,
						u32 *p_cur, u32 *p_max);
	int	(*devlink_sb_occ_tc_port_bind_get)(struct dsa_switch *ds, int port,
						   unsigned int sb_index, u16 tc_index,
						   enum devlink_sb_pool_type pool_type,
						   u32 *p_cur, u32 *p_max);

	/*
	 * MTU change functionality. Switches can also adjust their MRU through
	 * this method. By MTU, one understands the SDU (L2 payload) length.
	 * If the switch needs to account for the DSA tag on the CPU port, this
	 * method needs to do so privately.
	 */
	int	(*port_change_mtu)(struct dsa_switch *ds, int port,
				   int new_mtu);
	int	(*port_max_mtu)(struct dsa_switch *ds, int port);

	/*
	 * LAG integration
	 */
	int	(*port_lag_change)(struct dsa_switch *ds, int port);
	int	(*port_lag_join)(struct dsa_switch *ds, int port,
				 struct net_device *lag,
				 struct netdev_lag_upper_info *info);
	int	(*port_lag_leave)(struct dsa_switch *ds, int port,
				  struct net_device *lag);

	/*
	 * HSR integration
	 */
	int	(*port_hsr_join)(struct dsa_switch *ds, int port,
				 struct net_device *hsr);
	int	(*port_hsr_leave)(struct dsa_switch *ds, int port,
				  struct net_device *hsr);

	/*
	 * MRP integration
	 */
	int	(*port_mrp_add)(struct dsa_switch *ds, int port,
				const struct switchdev_obj_mrp *mrp);
	int	(*port_mrp_del)(struct dsa_switch *ds, int port,
				const struct switchdev_obj_mrp *mrp);
	int	(*port_mrp_add_ring_role)(struct dsa_switch *ds, int port,
					  const struct switchdev_obj_ring_role_mrp *mrp);
	int	(*port_mrp_del_ring_role)(struct dsa_switch *ds, int port,
					  const struct switchdev_obj_ring_role_mrp *mrp);

	/*
	 * tag_8021q operations
	 */
	int	(*tag_8021q_vlan_add)(struct dsa_switch *ds, int port, u16 vid,
				      u16 flags);
	int	(*tag_8021q_vlan_del)(struct dsa_switch *ds, int port, u16 vid);

	/*
	 * DSA master tracking operations
	 */
	void	(*master_state_change)(struct dsa_switch *ds,
				       const struct net_device *master,
				       bool operational);
};

#define DSA_DEVLINK_PARAM_DRIVER(_id, _name, _type, _cmodes)		\
	DEVLINK_PARAM_DRIVER(_id, _name, _type, _cmodes,		\
			     dsa_devlink_param_get, dsa_devlink_param_set, NULL)

int dsa_devlink_param_get(struct devlink *dl, u32 id,
			  struct devlink_param_gset_ctx *ctx);
int dsa_devlink_param_set(struct devlink *dl, u32 id,
			  struct devlink_param_gset_ctx *ctx);
int dsa_devlink_params_register(struct dsa_switch *ds,
				const struct devlink_param *params,
				size_t params_count);
void dsa_devlink_params_unregister(struct dsa_switch *ds,
				   const struct devlink_param *params,
				   size_t params_count);
int dsa_devlink_resource_register(struct dsa_switch *ds,
				  const char *resource_name,
				  u64 resource_size,
				  u64 resource_id,
				  u64 parent_resource_id,
				  const struct devlink_resource_size_params *size_params);

void dsa_devlink_resources_unregister(struct dsa_switch *ds);

void dsa_devlink_resource_occ_get_register(struct dsa_switch *ds,
					   u64 resource_id,
					   devlink_resource_occ_get_t *occ_get,
					   void *occ_get_priv);
void dsa_devlink_resource_occ_get_unregister(struct dsa_switch *ds,
					     u64 resource_id);
struct devlink_region *
dsa_devlink_region_create(struct dsa_switch *ds,
			  const struct devlink_region_ops *ops,
			  u32 region_max_snapshots, u64 region_size);
struct devlink_region *
dsa_devlink_port_region_create(struct dsa_switch *ds,
			       int port,
			       const struct devlink_port_region_ops *ops,
			       u32 region_max_snapshots, u64 region_size);
void dsa_devlink_region_destroy(struct devlink_region *region);

struct dsa_port *dsa_port_from_netdev(struct net_device *netdev);

struct dsa_devlink_priv {
	struct dsa_switch *ds;
};

static inline struct dsa_switch *dsa_devlink_to_ds(struct devlink *dl)
{
	struct dsa_devlink_priv *dl_priv = devlink_priv(dl);

	return dl_priv->ds;
}

static inline
struct dsa_switch *dsa_devlink_port_to_ds(struct devlink_port *port)
{
	struct devlink *dl = port->devlink;
	struct dsa_devlink_priv *dl_priv = devlink_priv(dl);

	return dl_priv->ds;
}

static inline int dsa_devlink_port_to_port(struct devlink_port *port)
{
	return port->index;
}

struct dsa_switch_driver {
	struct list_head	list;
	const struct dsa_switch_ops *ops;
};

struct net_device *dsa_dev_to_net_device(struct device *dev);

/* Keep inline for faster access in hot path */
static inline bool netdev_uses_dsa(const struct net_device *dev)
{
#if IS_ENABLED(CONFIG_NET_DSA)
	return dev->dsa_ptr && dev->dsa_ptr->rcv;
#endif
	return false;
}

/* All DSA tags that push the EtherType to the right (basically all except tail
 * tags, which don't break dissection) can be treated the same from the
 * perspective of the flow dissector.
 *
 * We need to return:
 *  - offset: the (B - A) difference between:
 *    A. the position of the real EtherType and
 *    B. the current skb->data (aka ETH_HLEN bytes into the frame, aka 2 bytes
 *       after the normal EtherType was supposed to be)
 *    The offset in bytes is exactly equal to the tagger overhead (and half of
 *    that, in __be16 shorts).
 *
 *  - proto: the value of the real EtherType.
 */
static inline void dsa_tag_generic_flow_dissect(const struct sk_buff *skb,
						__be16 *proto, int *offset)
{
#if IS_ENABLED(CONFIG_NET_DSA)
	const struct dsa_device_ops *ops = skb->dev->dsa_ptr->tag_ops;
	int tag_len = ops->needed_headroom;

	*offset = tag_len;
	*proto = ((__be16 *)skb->data)[(tag_len / 2) - 1];
#endif
}

#if IS_ENABLED(CONFIG_NET_DSA)
static inline int __dsa_netdevice_ops_check(struct net_device *dev)
{
	int err = -EOPNOTSUPP;

	if (!dev->dsa_ptr)
		return err;

	if (!dev->dsa_ptr->netdev_ops)
		return err;

	return 0;
}

static inline int dsa_ndo_eth_ioctl(struct net_device *dev, struct ifreq *ifr,
				    int cmd)
{
	const struct dsa_netdevice_ops *ops;
	int err;

	err = __dsa_netdevice_ops_check(dev);
	if (err)
		return err;

	ops = dev->dsa_ptr->netdev_ops;

	return ops->ndo_eth_ioctl(dev, ifr, cmd);
}
#else
static inline int dsa_ndo_eth_ioctl(struct net_device *dev, struct ifreq *ifr,
				    int cmd)
{
	return -EOPNOTSUPP;
}
#endif

void dsa_unregister_switch(struct dsa_switch *ds);
int dsa_register_switch(struct dsa_switch *ds);
void dsa_switch_shutdown(struct dsa_switch *ds);
struct dsa_switch *dsa_switch_find(int tree_index, int sw_index);
void dsa_flush_workqueue(void);
#ifdef CONFIG_PM_SLEEP
int dsa_switch_suspend(struct dsa_switch *ds);
int dsa_switch_resume(struct dsa_switch *ds);
#else
static inline int dsa_switch_suspend(struct dsa_switch *ds)
{
	return 0;
}
static inline int dsa_switch_resume(struct dsa_switch *ds)
{
	return 0;
}
#endif /* CONFIG_PM_SLEEP */

#if IS_ENABLED(CONFIG_NET_DSA)
bool dsa_slave_dev_check(const struct net_device *dev);
#else
static inline bool dsa_slave_dev_check(const struct net_device *dev)
{
	return false;
}
#endif

netdev_tx_t dsa_enqueue_skb(struct sk_buff *skb, struct net_device *dev);
void dsa_port_phylink_mac_change(struct dsa_switch *ds, int port, bool up);

struct dsa_tag_driver {
	const struct dsa_device_ops *ops;
	struct list_head list;
	struct module *owner;
};

void dsa_tag_drivers_register(struct dsa_tag_driver *dsa_tag_driver_array[],
			      unsigned int count,
			      struct module *owner);
void dsa_tag_drivers_unregister(struct dsa_tag_driver *dsa_tag_driver_array[],
				unsigned int count);

#define dsa_tag_driver_module_drivers(__dsa_tag_drivers_array, __count)	\
static int __init dsa_tag_driver_module_init(void)			\
{									\
	dsa_tag_drivers_register(__dsa_tag_drivers_array, __count,	\
				 THIS_MODULE);				\
	return 0;							\
}									\
module_init(dsa_tag_driver_module_init);				\
									\
static void __exit dsa_tag_driver_module_exit(void)			\
{									\
	dsa_tag_drivers_unregister(__dsa_tag_drivers_array, __count);	\
}									\
module_exit(dsa_tag_driver_module_exit)

/**
 * module_dsa_tag_drivers() - Helper macro for registering DSA tag
 * drivers
 * @__ops_array: Array of tag driver structures
 *
 * Helper macro for DSA tag drivers which do not do anything special
 * in module init/exit. Each module may only use this macro once, and
 * calling it replaces module_init() and module_exit().
 */
#define module_dsa_tag_drivers(__ops_array)				\
dsa_tag_driver_module_drivers(__ops_array, ARRAY_SIZE(__ops_array))

#define DSA_TAG_DRIVER_NAME(__ops) dsa_tag_driver ## _ ## __ops

/* Create a static structure we can build a linked list of dsa_tag
 * drivers
 */
#define DSA_TAG_DRIVER(__ops)						\
static struct dsa_tag_driver DSA_TAG_DRIVER_NAME(__ops) = {		\
	.ops = &__ops,							\
}

/**
 * module_dsa_tag_driver() - Helper macro for registering a single DSA tag
 * driver
 * @__ops: Single tag driver structures
 *
 * Helper macro for DSA tag drivers which do not do anything special
 * in module init/exit. Each module may only use this macro once, and
 * calling it replaces module_init() and module_exit().
 */
#define module_dsa_tag_driver(__ops)					\
DSA_TAG_DRIVER(__ops);							\
									\
static struct dsa_tag_driver *dsa_tag_driver_array[] =	{		\
	&DSA_TAG_DRIVER_NAME(__ops)					\
};									\
module_dsa_tag_drivers(dsa_tag_driver_array)
#endif

