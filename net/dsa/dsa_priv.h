/*
 * net/dsa/dsa_priv.h - Hardware switch handling
 * Copyright (c) 2008-2009 Marvell Semiconductor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __DSA_PRIV_H
#define __DSA_PRIV_H

#include <linux/phy.h>
#include <linux/netdevice.h>
#include <linux/netpoll.h>
#include <net/dsa.h>

enum {
	DSA_NOTIFIER_AGEING_TIME,
	DSA_NOTIFIER_BRIDGE_JOIN,
	DSA_NOTIFIER_BRIDGE_LEAVE,
	DSA_NOTIFIER_FDB_ADD,
	DSA_NOTIFIER_FDB_DEL,
	DSA_NOTIFIER_MDB_ADD,
	DSA_NOTIFIER_MDB_DEL,
	DSA_NOTIFIER_VLAN_ADD,
	DSA_NOTIFIER_VLAN_DEL,
};

/* DSA_NOTIFIER_AGEING_TIME */
struct dsa_notifier_ageing_time_info {
	struct switchdev_trans *trans;
	unsigned int ageing_time;
};

/* DSA_NOTIFIER_BRIDGE_* */
struct dsa_notifier_bridge_info {
	struct net_device *br;
	int sw_index;
	int port;
};

/* DSA_NOTIFIER_FDB_* */
struct dsa_notifier_fdb_info {
	int sw_index;
	int port;
	const unsigned char *addr;
	u16 vid;
};

/* DSA_NOTIFIER_MDB_* */
struct dsa_notifier_mdb_info {
	const struct switchdev_obj_port_mdb *mdb;
	struct switchdev_trans *trans;
	int sw_index;
	int port;
};

/* DSA_NOTIFIER_VLAN_* */
struct dsa_notifier_vlan_info {
	const struct switchdev_obj_port_vlan *vlan;
	struct switchdev_trans *trans;
	int sw_index;
	int port;
};

struct dsa_slave_priv {
	/* Copy of CPU port xmit for faster access in slave transmit hot path */
	struct sk_buff *	(*xmit)(struct sk_buff *skb,
					struct net_device *dev);

	struct pcpu_sw_netstats	*stats64;

	/* DSA port data, such as switch, port index, etc. */
	struct dsa_port		*dp;

	/*
	 * The phylib phy_device pointer for the PHY connected
	 * to this port.
	 */
	phy_interface_t		phy_interface;
	int			old_link;
	int			old_pause;
	int			old_duplex;

#ifdef CONFIG_NET_POLL_CONTROLLER
	struct netpoll		*netpoll;
#endif

	/* TC context */
	struct list_head	mall_tc_list;
};

/* dsa.c */
const struct dsa_device_ops *dsa_resolve_tag_protocol(int tag_protocol);
bool dsa_schedule_work(struct work_struct *work);

/* legacy.c */
int dsa_legacy_register(void);
void dsa_legacy_unregister(void);
int dsa_legacy_fdb_add(struct ndmsg *ndm, struct nlattr *tb[],
		       struct net_device *dev,
		       const unsigned char *addr, u16 vid,
		       u16 flags);
int dsa_legacy_fdb_del(struct ndmsg *ndm, struct nlattr *tb[],
		       struct net_device *dev,
		       const unsigned char *addr, u16 vid);

/* master.c */
int dsa_master_setup(struct net_device *dev, struct dsa_port *cpu_dp);
void dsa_master_teardown(struct net_device *dev);

static inline struct net_device *dsa_master_find_slave(struct net_device *dev,
						       int device, int port)
{
	struct dsa_port *cpu_dp = dev->dsa_ptr;
	struct dsa_switch_tree *dst = cpu_dp->dst;
	struct dsa_switch *ds;

	if (device < 0 || device >= DSA_MAX_SWITCHES)
		return NULL;

	ds = dst->ds[device];
	if (!ds)
		return NULL;

	if (port < 0 || port >= ds->num_ports)
		return NULL;

	return ds->ports[port].slave;
}

/* port.c */
int dsa_port_set_state(struct dsa_port *dp, u8 state,
		       struct switchdev_trans *trans);
int dsa_port_enable(struct dsa_port *dp, struct phy_device *phy);
void dsa_port_disable(struct dsa_port *dp, struct phy_device *phy);
int dsa_port_bridge_join(struct dsa_port *dp, struct net_device *br);
void dsa_port_bridge_leave(struct dsa_port *dp, struct net_device *br);
int dsa_port_vlan_filtering(struct dsa_port *dp, bool vlan_filtering,
			    struct switchdev_trans *trans);
int dsa_port_ageing_time(struct dsa_port *dp, clock_t ageing_clock,
			 struct switchdev_trans *trans);
int dsa_port_fdb_add(struct dsa_port *dp, const unsigned char *addr,
		     u16 vid);
int dsa_port_fdb_del(struct dsa_port *dp, const unsigned char *addr,
		     u16 vid);
int dsa_port_fdb_dump(struct dsa_port *dp, dsa_fdb_dump_cb_t *cb, void *data);
int dsa_port_mdb_add(const struct dsa_port *dp,
		     const struct switchdev_obj_port_mdb *mdb,
		     struct switchdev_trans *trans);
int dsa_port_mdb_del(const struct dsa_port *dp,
		     const struct switchdev_obj_port_mdb *mdb);
int dsa_port_vlan_add(struct dsa_port *dp,
		      const struct switchdev_obj_port_vlan *vlan,
		      struct switchdev_trans *trans);
int dsa_port_vlan_del(struct dsa_port *dp,
		      const struct switchdev_obj_port_vlan *vlan);
int dsa_port_fixed_link_register_of(struct dsa_port *dp);
void dsa_port_fixed_link_unregister_of(struct dsa_port *dp);

/* slave.c */
extern const struct dsa_device_ops notag_netdev_ops;
void dsa_slave_mii_bus_init(struct dsa_switch *ds);
int dsa_slave_create(struct dsa_port *dp);
void dsa_slave_destroy(struct net_device *slave_dev);
int dsa_slave_suspend(struct net_device *slave_dev);
int dsa_slave_resume(struct net_device *slave_dev);
int dsa_slave_register_notifier(void);
void dsa_slave_unregister_notifier(void);

static inline struct dsa_port *dsa_slave_to_port(const struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);

	return p->dp;
}

static inline struct net_device *
dsa_slave_to_master(const struct net_device *dev)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);

	return dp->cpu_dp->master;
}

/* switch.c */
int dsa_switch_register_notifier(struct dsa_switch *ds);
void dsa_switch_unregister_notifier(struct dsa_switch *ds);

/* tag_brcm.c */
extern const struct dsa_device_ops brcm_netdev_ops;
extern const struct dsa_device_ops brcm_prepend_netdev_ops;

/* tag_dsa.c */
extern const struct dsa_device_ops dsa_netdev_ops;

/* tag_edsa.c */
extern const struct dsa_device_ops edsa_netdev_ops;

/* tag_ksz.c */
extern const struct dsa_device_ops ksz_netdev_ops;

/* tag_lan9303.c */
extern const struct dsa_device_ops lan9303_netdev_ops;

/* tag_mtk.c */
extern const struct dsa_device_ops mtk_netdev_ops;

/* tag_qca.c */
extern const struct dsa_device_ops qca_netdev_ops;

/* tag_trailer.c */
extern const struct dsa_device_ops trailer_netdev_ops;

#endif
