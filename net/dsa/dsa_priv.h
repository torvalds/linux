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

/* tag_8021q.c */
int dsa_switch_tag_8021q_vlan_add(struct dsa_switch *ds,
				  struct dsa_notifier_tag_8021q_vlan_info *info);
int dsa_switch_tag_8021q_vlan_del(struct dsa_switch *ds,
				  struct dsa_notifier_tag_8021q_vlan_info *info);

#endif
