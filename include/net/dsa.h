/*
 * include/net/dsa.h - Driver for Distributed Switch Architecture switch chips
 * Copyright (c) 2008-2009 Marvell Semiconductor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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
#include <net/devlink.h>
#include <net/switchdev.h>

struct tc_action;
struct phy_device;
struct fixed_phy_status;

enum dsa_tag_protocol {
	DSA_TAG_PROTO_NONE = 0,
	DSA_TAG_PROTO_BRCM,
	DSA_TAG_PROTO_DSA,
	DSA_TAG_PROTO_EDSA,
	DSA_TAG_PROTO_KSZ,
	DSA_TAG_PROTO_LAN9303,
	DSA_TAG_PROTO_MTK,
	DSA_TAG_PROTO_QCA,
	DSA_TAG_PROTO_TRAILER,
	DSA_TAG_LAST,		/* MUST BE LAST */
};

#define DSA_MAX_SWITCHES	4
#define DSA_MAX_PORTS		12

#define DSA_RTABLE_NONE		-1

struct dsa_chip_data {
	/*
	 * How to access the switch configuration registers.
	 */
	struct device	*host_dev;
	int		sw_addr;

	/*
	 * Reference to network devices
	 */
	struct device	*netdev[DSA_MAX_PORTS];

	/* set to size of eeprom if supported by the switch */
	int		eeprom_len;

	/* Device tree node pointer for this specific switch chip
	 * used during switch setup in case additional properties
	 * and resources needs to be used
	 */
	struct device_node *of_node;

	/*
	 * The names of the switch's ports.  Use "cpu" to
	 * designate the switch port that the cpu is connected to,
	 * "dsa" to indicate that this port is a DSA link to
	 * another switch, NULL to indicate the port is unused,
	 * or any other string to indicate this is a physical port.
	 */
	char		*port_names[DSA_MAX_PORTS];
	struct device_node *port_dn[DSA_MAX_PORTS];

	/*
	 * An array of which element [a] indicates which port on this
	 * switch should be used to send packets to that are destined
	 * for switch a. Can be NULL if there is only one switch chip.
	 */
	s8		rtable[DSA_MAX_SWITCHES];
};

struct dsa_platform_data {
	/*
	 * Reference to a Linux network interface that connects
	 * to the root switch chip of the tree.
	 */
	struct device	*netdev;
	struct net_device *of_netdev;

	/*
	 * Info structs describing each of the switch chips
	 * connected via this network interface.
	 */
	int		nr_chips;
	struct dsa_chip_data	*chip;
};

struct packet_type;

struct dsa_switch_tree {
	struct list_head	list;

	/* Notifier chain for switch-wide events */
	struct raw_notifier_head	nh;

	/* Tree identifier */
	u32 tree;

	/* Number of switches attached to this tree */
	struct kref refcount;

	/* Has this tree been applied to the hardware? */
	bool applied;

	/*
	 * Configuration data for the platform device that owns
	 * this dsa switch tree instance.
	 */
	struct dsa_platform_data	*pd;

	/*
	 * Reference to network device to use, and which tagging
	 * protocol to use.
	 */
	struct net_device	*master_netdev;

	/* Copy of tag_ops->rcv for faster access in hot path */
	struct sk_buff *	(*rcv)(struct sk_buff *skb,
				       struct net_device *dev,
				       struct packet_type *pt,
				       struct net_device *orig_dev);

	/*
	 * Original copy of the master netdev ethtool_ops
	 */
	struct ethtool_ops	master_ethtool_ops;
	const struct ethtool_ops *master_orig_ethtool_ops;

	/*
	 * The switch port to which the CPU is attached.
	 */
	struct dsa_port		*cpu_dp;

	/*
	 * Data for the individual switch chips.
	 */
	struct dsa_switch	*ds[DSA_MAX_SWITCHES];

	/*
	 * Tagging protocol operations for adding and removing an
	 * encapsulation tag.
	 */
	const struct dsa_device_ops *tag_ops;
};

/* TC matchall action types, only mirroring for now */
enum dsa_port_mall_action_type {
	DSA_PORT_MALL_MIRROR,
};

/* TC mirroring entry */
struct dsa_mall_mirror_tc_entry {
	u8 to_local_port;
	bool ingress;
};

/* TC matchall entry */
struct dsa_mall_tc_entry {
	struct list_head list;
	unsigned long cookie;
	enum dsa_port_mall_action_type type;
	union {
		struct dsa_mall_mirror_tc_entry mirror;
	};
};


struct dsa_port {
	struct dsa_switch	*ds;
	unsigned int		index;
	const char		*name;
	struct net_device	*netdev;
	struct device_node	*dn;
	unsigned int		ageing_time;
	u8			stp_state;
	struct net_device	*bridge_dev;
	struct devlink_port	devlink_port;
};

struct dsa_switch {
	struct device *dev;

	/*
	 * Parent switch tree, and switch index.
	 */
	struct dsa_switch_tree	*dst;
	int			index;

	/* Listener for switch fabric events */
	struct notifier_block	nb;

	/*
	 * Give the switch driver somewhere to hang its private data
	 * structure.
	 */
	void *priv;

	/*
	 * Configuration data for this switch.
	 */
	struct dsa_chip_data	*cd;

	/*
	 * The switch operations.
	 */
	const struct dsa_switch_ops	*ops;

	/*
	 * An array of which element [a] indicates which port on this
	 * switch should be used to send packets to that are destined
	 * for switch a. Can be NULL if there is only one switch chip.
	 */
	s8		rtable[DSA_MAX_SWITCHES];

	/*
	 * The lower device this switch uses to talk to the host
	 */
	struct net_device *master_netdev;

	/*
	 * Slave mii_bus and devices for the individual ports.
	 */
	u32			dsa_port_mask;
	u32			cpu_port_mask;
	u32			enabled_port_mask;
	u32			phys_mii_mask;
	struct mii_bus		*slave_mii_bus;

	/* Ageing Time limits in msecs */
	unsigned int ageing_time_min;
	unsigned int ageing_time_max;

	/* devlink used to represent this switch device */
	struct devlink		*devlink;

	/* Dynamically allocated ports, keep last */
	size_t num_ports;
	struct dsa_port ports[];
};

static inline bool dsa_is_cpu_port(struct dsa_switch *ds, int p)
{
	return !!(ds->cpu_port_mask & (1 << p));
}

static inline bool dsa_is_dsa_port(struct dsa_switch *ds, int p)
{
	return !!((ds->dsa_port_mask) & (1 << p));
}

static inline bool dsa_is_normal_port(struct dsa_switch *ds, int p)
{
	return !dsa_is_cpu_port(ds, p) && !dsa_is_dsa_port(ds, p);
}

static inline bool dsa_is_port_initialized(struct dsa_switch *ds, int p)
{
	return ds->enabled_port_mask & (1 << p) && ds->ports[p].netdev;
}

static inline u8 dsa_upstream_port(struct dsa_switch *ds)
{
	struct dsa_switch_tree *dst = ds->dst;

	/*
	 * If this is the root switch (i.e. the switch that connects
	 * to the CPU), return the cpu port number on this switch.
	 * Else return the (DSA) port number that connects to the
	 * switch that is one hop closer to the cpu.
	 */
	if (dst->cpu_dp->ds == ds)
		return dst->cpu_dp->index;
	else
		return ds->rtable[dst->cpu_dp->ds->index];
}

struct dsa_switch_ops {
	/*
	 * Legacy probing.
	 */
	const char	*(*probe)(struct device *dsa_dev,
				  struct device *host_dev, int sw_addr,
				  void **priv);

	enum dsa_tag_protocol (*get_tag_protocol)(struct dsa_switch *ds);

	int	(*setup)(struct dsa_switch *ds);
	int	(*set_addr)(struct dsa_switch *ds, u8 *addr);
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
	 * ethtool hardware statistics.
	 */
	void	(*get_strings)(struct dsa_switch *ds, int port, uint8_t *data);
	void	(*get_ethtool_stats)(struct dsa_switch *ds,
				     int port, uint64_t *data);
	int	(*get_sset_count)(struct dsa_switch *ds);

	/*
	 * ethtool Wake-on-LAN
	 */
	void	(*get_wol)(struct dsa_switch *ds, int port,
			   struct ethtool_wolinfo *w);
	int	(*set_wol)(struct dsa_switch *ds, int port,
			   struct ethtool_wolinfo *w);

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
	void	(*port_disable)(struct dsa_switch *ds, int port,
				struct phy_device *phy);

	/*
	 * EEE setttings
	 */
	int	(*set_eee)(struct dsa_switch *ds, int port,
			   struct phy_device *phydev,
			   struct ethtool_eee *e);
	int	(*get_eee)(struct dsa_switch *ds, int port,
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
	 * Bridge integration
	 */
	int	(*set_ageing_time)(struct dsa_switch *ds, unsigned int msecs);
	int	(*port_bridge_join)(struct dsa_switch *ds, int port,
				    struct net_device *bridge);
	void	(*port_bridge_leave)(struct dsa_switch *ds, int port,
				     struct net_device *bridge);
	void	(*port_stp_state_set)(struct dsa_switch *ds, int port,
				      u8 state);
	void	(*port_fast_age)(struct dsa_switch *ds, int port);

	/*
	 * VLAN support
	 */
	int	(*port_vlan_filtering)(struct dsa_switch *ds, int port,
				       bool vlan_filtering);
	int	(*port_vlan_prepare)(struct dsa_switch *ds, int port,
				     const struct switchdev_obj_port_vlan *vlan,
				     struct switchdev_trans *trans);
	void	(*port_vlan_add)(struct dsa_switch *ds, int port,
				 const struct switchdev_obj_port_vlan *vlan,
				 struct switchdev_trans *trans);
	int	(*port_vlan_del)(struct dsa_switch *ds, int port,
				 const struct switchdev_obj_port_vlan *vlan);
	int	(*port_vlan_dump)(struct dsa_switch *ds, int port,
				  struct switchdev_obj_port_vlan *vlan,
				  switchdev_obj_dump_cb_t *cb);

	/*
	 * Forwarding database
	 */
	int	(*port_fdb_prepare)(struct dsa_switch *ds, int port,
				    const struct switchdev_obj_port_fdb *fdb,
				    struct switchdev_trans *trans);
	void	(*port_fdb_add)(struct dsa_switch *ds, int port,
				const struct switchdev_obj_port_fdb *fdb,
				struct switchdev_trans *trans);
	int	(*port_fdb_del)(struct dsa_switch *ds, int port,
				const struct switchdev_obj_port_fdb *fdb);
	int	(*port_fdb_dump)(struct dsa_switch *ds, int port,
				 struct switchdev_obj_port_fdb *fdb,
				  switchdev_obj_dump_cb_t *cb);

	/*
	 * Multicast database
	 */
	int	(*port_mdb_prepare)(struct dsa_switch *ds, int port,
				    const struct switchdev_obj_port_mdb *mdb,
				    struct switchdev_trans *trans);
	void	(*port_mdb_add)(struct dsa_switch *ds, int port,
				const struct switchdev_obj_port_mdb *mdb,
				struct switchdev_trans *trans);
	int	(*port_mdb_del)(struct dsa_switch *ds, int port,
				const struct switchdev_obj_port_mdb *mdb);
	int	(*port_mdb_dump)(struct dsa_switch *ds, int port,
				 struct switchdev_obj_port_mdb *mdb,
				  switchdev_obj_dump_cb_t *cb);

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
	int	(*port_mirror_add)(struct dsa_switch *ds, int port,
				   struct dsa_mall_mirror_tc_entry *mirror,
				   bool ingress);
	void	(*port_mirror_del)(struct dsa_switch *ds, int port,
				   struct dsa_mall_mirror_tc_entry *mirror);

	/*
	 * Cross-chip operations
	 */
	int	(*crosschip_bridge_join)(struct dsa_switch *ds, int sw_index,
					 int port, struct net_device *br);
	void	(*crosschip_bridge_leave)(struct dsa_switch *ds, int sw_index,
					  int port, struct net_device *br);
};

struct dsa_switch_driver {
	struct list_head	list;
	const struct dsa_switch_ops *ops;
};

/* Legacy driver registration */
void register_switch_driver(struct dsa_switch_driver *type);
void unregister_switch_driver(struct dsa_switch_driver *type);
struct mii_bus *dsa_host_dev_to_mii_bus(struct device *dev);

struct net_device *dsa_dev_to_net_device(struct device *dev);

/* Keep inline for faster access in hot path */
static inline bool netdev_uses_dsa(struct net_device *dev)
{
#if IS_ENABLED(CONFIG_NET_DSA)
	return dev->dsa_ptr && dev->dsa_ptr->rcv;
#endif
	return false;
}

struct dsa_switch *dsa_switch_alloc(struct device *dev, size_t n);
void dsa_unregister_switch(struct dsa_switch *ds);
int dsa_register_switch(struct dsa_switch *ds);
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

#endif
