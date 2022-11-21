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

struct dsa_notifier_tag_8021q_vlan_info;

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
