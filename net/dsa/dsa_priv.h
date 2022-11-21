/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * net/dsa/dsa_priv.h - Hardware switch handling
 * Copyright (c) 2008-2009 Marvell Semiconductor
 */

#ifndef __DSA_PRIV_H
#define __DSA_PRIV_H

#include <linux/phy.h>
#include <linux/netdevice.h>
#include <net/dsa.h>

#define DSA_MAX_NUM_OFFLOADING_BRIDGES		BITS_PER_LONG

enum {
	DSA_NOTIFIER_AGEING_TIME,
	DSA_NOTIFIER_BRIDGE_JOIN,
	DSA_NOTIFIER_BRIDGE_LEAVE,
	DSA_NOTIFIER_FDB_ADD,
	DSA_NOTIFIER_FDB_DEL,
	DSA_NOTIFIER_HOST_FDB_ADD,
	DSA_NOTIFIER_HOST_FDB_DEL,
	DSA_NOTIFIER_LAG_FDB_ADD,
	DSA_NOTIFIER_LAG_FDB_DEL,
	DSA_NOTIFIER_LAG_CHANGE,
	DSA_NOTIFIER_LAG_JOIN,
	DSA_NOTIFIER_LAG_LEAVE,
	DSA_NOTIFIER_MDB_ADD,
	DSA_NOTIFIER_MDB_DEL,
	DSA_NOTIFIER_HOST_MDB_ADD,
	DSA_NOTIFIER_HOST_MDB_DEL,
	DSA_NOTIFIER_VLAN_ADD,
	DSA_NOTIFIER_VLAN_DEL,
	DSA_NOTIFIER_HOST_VLAN_ADD,
	DSA_NOTIFIER_HOST_VLAN_DEL,
	DSA_NOTIFIER_MTU,
	DSA_NOTIFIER_TAG_PROTO,
	DSA_NOTIFIER_TAG_PROTO_CONNECT,
	DSA_NOTIFIER_TAG_PROTO_DISCONNECT,
	DSA_NOTIFIER_TAG_8021Q_VLAN_ADD,
	DSA_NOTIFIER_TAG_8021Q_VLAN_DEL,
	DSA_NOTIFIER_MASTER_STATE_CHANGE,
};

/* DSA_NOTIFIER_AGEING_TIME */
struct dsa_notifier_ageing_time_info {
	unsigned int ageing_time;
};

/* DSA_NOTIFIER_BRIDGE_* */
struct dsa_notifier_bridge_info {
	const struct dsa_port *dp;
	struct dsa_bridge bridge;
	bool tx_fwd_offload;
	struct netlink_ext_ack *extack;
};

/* DSA_NOTIFIER_FDB_* */
struct dsa_notifier_fdb_info {
	const struct dsa_port *dp;
	const unsigned char *addr;
	u16 vid;
	struct dsa_db db;
};

/* DSA_NOTIFIER_LAG_FDB_* */
struct dsa_notifier_lag_fdb_info {
	struct dsa_lag *lag;
	const unsigned char *addr;
	u16 vid;
	struct dsa_db db;
};

/* DSA_NOTIFIER_MDB_* */
struct dsa_notifier_mdb_info {
	const struct dsa_port *dp;
	const struct switchdev_obj_port_mdb *mdb;
	struct dsa_db db;
};

/* DSA_NOTIFIER_LAG_* */
struct dsa_notifier_lag_info {
	const struct dsa_port *dp;
	struct dsa_lag lag;
	struct netdev_lag_upper_info *info;
	struct netlink_ext_ack *extack;
};

/* DSA_NOTIFIER_VLAN_* */
struct dsa_notifier_vlan_info {
	const struct dsa_port *dp;
	const struct switchdev_obj_port_vlan *vlan;
	struct netlink_ext_ack *extack;
};

/* DSA_NOTIFIER_MTU */
struct dsa_notifier_mtu_info {
	const struct dsa_port *dp;
	int mtu;
};

/* DSA_NOTIFIER_TAG_PROTO_* */
struct dsa_notifier_tag_proto_info {
	const struct dsa_device_ops *tag_ops;
};

/* DSA_NOTIFIER_TAG_8021Q_VLAN_* */
struct dsa_notifier_tag_8021q_vlan_info {
	const struct dsa_port *dp;
	u16 vid;
};

/* DSA_NOTIFIER_MASTER_STATE_CHANGE */
struct dsa_notifier_master_state_info {
	const struct net_device *master;
	bool operational;
};

struct dsa_switchdev_event_work {
	struct net_device *dev;
	struct net_device *orig_dev;
	struct work_struct work;
	unsigned long event;
	/* Specific for SWITCHDEV_FDB_ADD_TO_DEVICE and
	 * SWITCHDEV_FDB_DEL_TO_DEVICE
	 */
	unsigned char addr[ETH_ALEN];
	u16 vid;
	bool host_addr;
};

enum dsa_standalone_event {
	DSA_UC_ADD,
	DSA_UC_DEL,
	DSA_MC_ADD,
	DSA_MC_DEL,
};

struct dsa_standalone_event_work {
	struct work_struct work;
	struct net_device *dev;
	enum dsa_standalone_event event;
	unsigned char addr[ETH_ALEN];
	u16 vid;
};

/* dsa.c */
struct net_device *dsa_dev_to_net_device(struct device *dev);

bool dsa_db_equal(const struct dsa_db *a, const struct dsa_db *b);

bool dsa_schedule_work(struct work_struct *work);

/* netlink.c */
extern struct rtnl_link_ops dsa_link_ops __read_mostly;

static inline bool dsa_switch_supports_uc_filtering(struct dsa_switch *ds)
{
	return ds->ops->port_fdb_add && ds->ops->port_fdb_del &&
	       ds->fdb_isolation && !ds->vlan_filtering_is_global &&
	       !ds->needs_standalone_vlan_filtering;
}

static inline bool dsa_switch_supports_mc_filtering(struct dsa_switch *ds)
{
	return ds->ops->port_mdb_add && ds->ops->port_mdb_del &&
	       ds->fdb_isolation && !ds->vlan_filtering_is_global &&
	       !ds->needs_standalone_vlan_filtering;
}

/* dsa2.c */
void dsa_lag_map(struct dsa_switch_tree *dst, struct dsa_lag *lag);
void dsa_lag_unmap(struct dsa_switch_tree *dst, struct dsa_lag *lag);
struct dsa_lag *dsa_tree_lag_find(struct dsa_switch_tree *dst,
				  const struct net_device *lag_dev);
struct net_device *dsa_tree_find_first_master(struct dsa_switch_tree *dst);
int dsa_tree_notify(struct dsa_switch_tree *dst, unsigned long e, void *v);
int dsa_broadcast(unsigned long e, void *v);
int dsa_tree_change_tag_proto(struct dsa_switch_tree *dst,
			      const struct dsa_device_ops *tag_ops,
			      const struct dsa_device_ops *old_tag_ops);
void dsa_tree_master_admin_state_change(struct dsa_switch_tree *dst,
					struct net_device *master,
					bool up);
void dsa_tree_master_oper_state_change(struct dsa_switch_tree *dst,
				       struct net_device *master,
				       bool up);
unsigned int dsa_bridge_num_get(const struct net_device *bridge_dev, int max);
void dsa_bridge_num_put(const struct net_device *bridge_dev,
			unsigned int bridge_num);
struct dsa_bridge *dsa_tree_bridge_find(struct dsa_switch_tree *dst,
					const struct net_device *br);

/* tag_8021q.c */
int dsa_switch_tag_8021q_vlan_add(struct dsa_switch *ds,
				  struct dsa_notifier_tag_8021q_vlan_info *info);
int dsa_switch_tag_8021q_vlan_del(struct dsa_switch *ds,
				  struct dsa_notifier_tag_8021q_vlan_info *info);

extern struct list_head dsa_tree_list;

#endif
