/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __DSA_SWITCH_H
#define __DSA_SWITCH_H

#include <net/dsa.h>

struct netlink_ext_ack;

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
	DSA_NOTIFIER_CONDUIT_STATE_CHANGE,
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

/* DSA_NOTIFIER_CONDUIT_STATE_CHANGE */
struct dsa_notifier_conduit_state_info {
	const struct net_device *conduit;
	bool operational;
};

struct dsa_vlan *dsa_vlan_find(struct list_head *vlan_list,
			       const struct switchdev_obj_port_vlan *vlan);

int dsa_tree_notify(struct dsa_switch_tree *dst, unsigned long e, void *v);
int dsa_broadcast(unsigned long e, void *v);

int dsa_switch_register_notifier(struct dsa_switch *ds);
void dsa_switch_unregister_notifier(struct dsa_switch *ds);

#endif
