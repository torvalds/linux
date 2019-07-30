/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * net/dsa/dsa_priv.h - Hardware switch handling
 * Copyright (c) 2008-2009 Marvell Semiconductor
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

#ifdef CONFIG_NET_POLL_CONTROLLER
	struct netpoll		*netpoll;
#endif

	/* TC context */
	struct list_head	mall_tc_list;
};

/* dsa.c */
const struct dsa_device_ops *dsa_tag_driver_get(int tag_protocol);
void dsa_tag_driver_put(const struct dsa_device_ops *ops);

bool dsa_schedule_work(struct work_struct *work);
const char *dsa_tag_protocol_to_str(const struct dsa_device_ops *ops);

int dsa_legacy_fdb_add(struct ndmsg *ndm, struct nlattr *tb[],
		       struct net_device *dev,
		       const unsigned char *addr, u16 vid,
		       u16 flags,
		       struct netlink_ext_ack *extack);
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
	struct dsa_port *slave_port;

	if (device < 0 || device >= DSA_MAX_SWITCHES)
		return NULL;

	ds = dst->ds[device];
	if (!ds)
		return NULL;

	if (port < 0 || port >= ds->num_ports)
		return NULL;

	slave_port = &ds->ports[port];

	if (unlikely(slave_port->type != DSA_PORT_TYPE_USER))
		return NULL;

	return slave_port->slave;
}

/* port.c */
int dsa_port_set_state(struct dsa_port *dp, u8 state,
		       struct switchdev_trans *trans);
int dsa_port_enable(struct dsa_port *dp, struct phy_device *phy);
void dsa_port_disable(struct dsa_port *dp);
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
int dsa_port_pre_bridge_flags(const struct dsa_port *dp, unsigned long flags,
			      struct switchdev_trans *trans);
int dsa_port_bridge_flags(const struct dsa_port *dp, unsigned long flags,
			  struct switchdev_trans *trans);
int dsa_port_vlan_add(struct dsa_port *dp,
		      const struct switchdev_obj_port_vlan *vlan,
		      struct switchdev_trans *trans);
int dsa_port_vlan_del(struct dsa_port *dp,
		      const struct switchdev_obj_port_vlan *vlan);
int dsa_port_vid_add(struct dsa_port *dp, u16 vid, u16 flags);
int dsa_port_vid_del(struct dsa_port *dp, u16 vid);
int dsa_port_link_register_of(struct dsa_port *dp);
void dsa_port_link_unregister_of(struct dsa_port *dp);

/* slave.c */
extern const struct dsa_device_ops notag_netdev_ops;
void dsa_slave_mii_bus_init(struct dsa_switch *ds);
int dsa_slave_create(struct dsa_port *dp);
void dsa_slave_destroy(struct net_device *slave_dev);
int dsa_slave_suspend(struct net_device *slave_dev);
int dsa_slave_resume(struct net_device *slave_dev);
int dsa_slave_register_notifier(void);
void dsa_slave_unregister_notifier(void);

void *dsa_defer_xmit(struct sk_buff *skb, struct net_device *dev);

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
#endif
