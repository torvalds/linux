// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Handling of a single switch chip, part of a switch fabric
 *
 * Copyright (c) 2017 Savoir-faire Linux Inc.
 *	Vivien Didelot <vivien.didelot@savoirfairelinux.com>
 */

#include <linux/if_bridge.h>
#include <linux/netdevice.h>
#include <linux/notifier.h>
#include <linux/if_vlan.h>
#include <net/switchdev.h>

#include "dsa.h"
#include "netlink.h"
#include "port.h"
#include "slave.h"
#include "switch.h"
#include "tag_8021q.h"

static unsigned int dsa_switch_fastest_ageing_time(struct dsa_switch *ds,
						   unsigned int ageing_time)
{
	struct dsa_port *dp;

	dsa_switch_for_each_port(dp, ds)
		if (dp->ageing_time && dp->ageing_time < ageing_time)
			ageing_time = dp->ageing_time;

	return ageing_time;
}

static int dsa_switch_ageing_time(struct dsa_switch *ds,
				  struct dsa_notifier_ageing_time_info *info)
{
	unsigned int ageing_time = info->ageing_time;

	if (ds->ageing_time_min && ageing_time < ds->ageing_time_min)
		return -ERANGE;

	if (ds->ageing_time_max && ageing_time > ds->ageing_time_max)
		return -ERANGE;

	/* Program the fastest ageing time in case of multiple bridges */
	ageing_time = dsa_switch_fastest_ageing_time(ds, ageing_time);

	if (ds->ops->set_ageing_time)
		return ds->ops->set_ageing_time(ds, ageing_time);

	return 0;
}

static bool dsa_port_mtu_match(struct dsa_port *dp,
			       struct dsa_notifier_mtu_info *info)
{
	return dp == info->dp || dsa_port_is_dsa(dp) || dsa_port_is_cpu(dp);
}

static int dsa_switch_mtu(struct dsa_switch *ds,
			  struct dsa_notifier_mtu_info *info)
{
	struct dsa_port *dp;
	int ret;

	if (!ds->ops->port_change_mtu)
		return -EOPNOTSUPP;

	dsa_switch_for_each_port(dp, ds) {
		if (dsa_port_mtu_match(dp, info)) {
			ret = ds->ops->port_change_mtu(ds, dp->index,
						       info->mtu);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int dsa_switch_bridge_join(struct dsa_switch *ds,
				  struct dsa_notifier_bridge_info *info)
{
	int err;

	if (info->dp->ds == ds) {
		if (!ds->ops->port_bridge_join)
			return -EOPNOTSUPP;

		err = ds->ops->port_bridge_join(ds, info->dp->index,
						info->bridge,
						&info->tx_fwd_offload,
						info->extack);
		if (err)
			return err;
	}

	if (info->dp->ds != ds && ds->ops->crosschip_bridge_join) {
		err = ds->ops->crosschip_bridge_join(ds,
						     info->dp->ds->dst->index,
						     info->dp->ds->index,
						     info->dp->index,
						     info->bridge,
						     info->extack);
		if (err)
			return err;
	}

	return 0;
}

static int dsa_switch_bridge_leave(struct dsa_switch *ds,
				   struct dsa_notifier_bridge_info *info)
{
	if (info->dp->ds == ds && ds->ops->port_bridge_leave)
		ds->ops->port_bridge_leave(ds, info->dp->index, info->bridge);

	if (info->dp->ds != ds && ds->ops->crosschip_bridge_leave)
		ds->ops->crosschip_bridge_leave(ds, info->dp->ds->dst->index,
						info->dp->ds->index,
						info->dp->index,
						info->bridge);

	return 0;
}

/* Matches for all upstream-facing ports (the CPU port and all upstream-facing
 * DSA links) that sit between the targeted port on which the notifier was
 * emitted and its dedicated CPU port.
 */
static bool dsa_port_host_address_match(struct dsa_port *dp,
					const struct dsa_port *targeted_dp)
{
	struct dsa_port *cpu_dp = targeted_dp->cpu_dp;

	if (dsa_switch_is_upstream_of(dp->ds, targeted_dp->ds))
		return dp->index == dsa_towards_port(dp->ds, cpu_dp->ds->index,
						     cpu_dp->index);

	return false;
}

static struct dsa_mac_addr *dsa_mac_addr_find(struct list_head *addr_list,
					      const unsigned char *addr, u16 vid,
					      struct dsa_db db)
{
	struct dsa_mac_addr *a;

	list_for_each_entry(a, addr_list, list)
		if (ether_addr_equal(a->addr, addr) && a->vid == vid &&
		    dsa_db_equal(&a->db, &db))
			return a;

	return NULL;
}

static int dsa_port_do_mdb_add(struct dsa_port *dp,
			       const struct switchdev_obj_port_mdb *mdb,
			       struct dsa_db db)
{
	struct dsa_switch *ds = dp->ds;
	struct dsa_mac_addr *a;
	int port = dp->index;
	int err = 0;

	/* No need to bother with refcounting for user ports */
	if (!(dsa_port_is_cpu(dp) || dsa_port_is_dsa(dp)))
		return ds->ops->port_mdb_add(ds, port, mdb, db);

	mutex_lock(&dp->addr_lists_lock);

	a = dsa_mac_addr_find(&dp->mdbs, mdb->addr, mdb->vid, db);
	if (a) {
		refcount_inc(&a->refcount);
		goto out;
	}

	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a) {
		err = -ENOMEM;
		goto out;
	}

	err = ds->ops->port_mdb_add(ds, port, mdb, db);
	if (err) {
		kfree(a);
		goto out;
	}

	ether_addr_copy(a->addr, mdb->addr);
	a->vid = mdb->vid;
	a->db = db;
	refcount_set(&a->refcount, 1);
	list_add_tail(&a->list, &dp->mdbs);

out:
	mutex_unlock(&dp->addr_lists_lock);

	return err;
}

static int dsa_port_do_mdb_del(struct dsa_port *dp,
			       const struct switchdev_obj_port_mdb *mdb,
			       struct dsa_db db)
{
	struct dsa_switch *ds = dp->ds;
	struct dsa_mac_addr *a;
	int port = dp->index;
	int err = 0;

	/* No need to bother with refcounting for user ports */
	if (!(dsa_port_is_cpu(dp) || dsa_port_is_dsa(dp)))
		return ds->ops->port_mdb_del(ds, port, mdb, db);

	mutex_lock(&dp->addr_lists_lock);

	a = dsa_mac_addr_find(&dp->mdbs, mdb->addr, mdb->vid, db);
	if (!a) {
		err = -ENOENT;
		goto out;
	}

	if (!refcount_dec_and_test(&a->refcount))
		goto out;

	err = ds->ops->port_mdb_del(ds, port, mdb, db);
	if (err) {
		refcount_set(&a->refcount, 1);
		goto out;
	}

	list_del(&a->list);
	kfree(a);

out:
	mutex_unlock(&dp->addr_lists_lock);

	return err;
}

static int dsa_port_do_fdb_add(struct dsa_port *dp, const unsigned char *addr,
			       u16 vid, struct dsa_db db)
{
	struct dsa_switch *ds = dp->ds;
	struct dsa_mac_addr *a;
	int port = dp->index;
	int err = 0;

	/* No need to bother with refcounting for user ports */
	if (!(dsa_port_is_cpu(dp) || dsa_port_is_dsa(dp)))
		return ds->ops->port_fdb_add(ds, port, addr, vid, db);

	mutex_lock(&dp->addr_lists_lock);

	a = dsa_mac_addr_find(&dp->fdbs, addr, vid, db);
	if (a) {
		refcount_inc(&a->refcount);
		goto out;
	}

	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a) {
		err = -ENOMEM;
		goto out;
	}

	err = ds->ops->port_fdb_add(ds, port, addr, vid, db);
	if (err) {
		kfree(a);
		goto out;
	}

	ether_addr_copy(a->addr, addr);
	a->vid = vid;
	a->db = db;
	refcount_set(&a->refcount, 1);
	list_add_tail(&a->list, &dp->fdbs);

out:
	mutex_unlock(&dp->addr_lists_lock);

	return err;
}

static int dsa_port_do_fdb_del(struct dsa_port *dp, const unsigned char *addr,
			       u16 vid, struct dsa_db db)
{
	struct dsa_switch *ds = dp->ds;
	struct dsa_mac_addr *a;
	int port = dp->index;
	int err = 0;

	/* No need to bother with refcounting for user ports */
	if (!(dsa_port_is_cpu(dp) || dsa_port_is_dsa(dp)))
		return ds->ops->port_fdb_del(ds, port, addr, vid, db);

	mutex_lock(&dp->addr_lists_lock);

	a = dsa_mac_addr_find(&dp->fdbs, addr, vid, db);
	if (!a) {
		err = -ENOENT;
		goto out;
	}

	if (!refcount_dec_and_test(&a->refcount))
		goto out;

	err = ds->ops->port_fdb_del(ds, port, addr, vid, db);
	if (err) {
		refcount_set(&a->refcount, 1);
		goto out;
	}

	list_del(&a->list);
	kfree(a);

out:
	mutex_unlock(&dp->addr_lists_lock);

	return err;
}

static int dsa_switch_do_lag_fdb_add(struct dsa_switch *ds, struct dsa_lag *lag,
				     const unsigned char *addr, u16 vid,
				     struct dsa_db db)
{
	struct dsa_mac_addr *a;
	int err = 0;

	mutex_lock(&lag->fdb_lock);

	a = dsa_mac_addr_find(&lag->fdbs, addr, vid, db);
	if (a) {
		refcount_inc(&a->refcount);
		goto out;
	}

	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a) {
		err = -ENOMEM;
		goto out;
	}

	err = ds->ops->lag_fdb_add(ds, *lag, addr, vid, db);
	if (err) {
		kfree(a);
		goto out;
	}

	ether_addr_copy(a->addr, addr);
	a->vid = vid;
	a->db = db;
	refcount_set(&a->refcount, 1);
	list_add_tail(&a->list, &lag->fdbs);

out:
	mutex_unlock(&lag->fdb_lock);

	return err;
}

static int dsa_switch_do_lag_fdb_del(struct dsa_switch *ds, struct dsa_lag *lag,
				     const unsigned char *addr, u16 vid,
				     struct dsa_db db)
{
	struct dsa_mac_addr *a;
	int err = 0;

	mutex_lock(&lag->fdb_lock);

	a = dsa_mac_addr_find(&lag->fdbs, addr, vid, db);
	if (!a) {
		err = -ENOENT;
		goto out;
	}

	if (!refcount_dec_and_test(&a->refcount))
		goto out;

	err = ds->ops->lag_fdb_del(ds, *lag, addr, vid, db);
	if (err) {
		refcount_set(&a->refcount, 1);
		goto out;
	}

	list_del(&a->list);
	kfree(a);

out:
	mutex_unlock(&lag->fdb_lock);

	return err;
}

static int dsa_switch_host_fdb_add(struct dsa_switch *ds,
				   struct dsa_notifier_fdb_info *info)
{
	struct dsa_port *dp;
	int err = 0;

	if (!ds->ops->port_fdb_add)
		return -EOPNOTSUPP;

	dsa_switch_for_each_port(dp, ds) {
		if (dsa_port_host_address_match(dp, info->dp)) {
			if (dsa_port_is_cpu(dp) && info->dp->cpu_port_in_lag) {
				err = dsa_switch_do_lag_fdb_add(ds, dp->lag,
								info->addr,
								info->vid,
								info->db);
			} else {
				err = dsa_port_do_fdb_add(dp, info->addr,
							  info->vid, info->db);
			}
			if (err)
				break;
		}
	}

	return err;
}

static int dsa_switch_host_fdb_del(struct dsa_switch *ds,
				   struct dsa_notifier_fdb_info *info)
{
	struct dsa_port *dp;
	int err = 0;

	if (!ds->ops->port_fdb_del)
		return -EOPNOTSUPP;

	dsa_switch_for_each_port(dp, ds) {
		if (dsa_port_host_address_match(dp, info->dp)) {
			if (dsa_port_is_cpu(dp) && info->dp->cpu_port_in_lag) {
				err = dsa_switch_do_lag_fdb_del(ds, dp->lag,
								info->addr,
								info->vid,
								info->db);
			} else {
				err = dsa_port_do_fdb_del(dp, info->addr,
							  info->vid, info->db);
			}
			if (err)
				break;
		}
	}

	return err;
}

static int dsa_switch_fdb_add(struct dsa_switch *ds,
			      struct dsa_notifier_fdb_info *info)
{
	int port = dsa_towards_port(ds, info->dp->ds->index, info->dp->index);
	struct dsa_port *dp = dsa_to_port(ds, port);

	if (!ds->ops->port_fdb_add)
		return -EOPNOTSUPP;

	return dsa_port_do_fdb_add(dp, info->addr, info->vid, info->db);
}

static int dsa_switch_fdb_del(struct dsa_switch *ds,
			      struct dsa_notifier_fdb_info *info)
{
	int port = dsa_towards_port(ds, info->dp->ds->index, info->dp->index);
	struct dsa_port *dp = dsa_to_port(ds, port);

	if (!ds->ops->port_fdb_del)
		return -EOPNOTSUPP;

	return dsa_port_do_fdb_del(dp, info->addr, info->vid, info->db);
}

static int dsa_switch_lag_fdb_add(struct dsa_switch *ds,
				  struct dsa_notifier_lag_fdb_info *info)
{
	struct dsa_port *dp;

	if (!ds->ops->lag_fdb_add)
		return -EOPNOTSUPP;

	/* Notify switch only if it has a port in this LAG */
	dsa_switch_for_each_port(dp, ds)
		if (dsa_port_offloads_lag(dp, info->lag))
			return dsa_switch_do_lag_fdb_add(ds, info->lag,
							 info->addr, info->vid,
							 info->db);

	return 0;
}

static int dsa_switch_lag_fdb_del(struct dsa_switch *ds,
				  struct dsa_notifier_lag_fdb_info *info)
{
	struct dsa_port *dp;

	if (!ds->ops->lag_fdb_del)
		return -EOPNOTSUPP;

	/* Notify switch only if it has a port in this LAG */
	dsa_switch_for_each_port(dp, ds)
		if (dsa_port_offloads_lag(dp, info->lag))
			return dsa_switch_do_lag_fdb_del(ds, info->lag,
							 info->addr, info->vid,
							 info->db);

	return 0;
}

static int dsa_switch_lag_change(struct dsa_switch *ds,
				 struct dsa_notifier_lag_info *info)
{
	if (info->dp->ds == ds && ds->ops->port_lag_change)
		return ds->ops->port_lag_change(ds, info->dp->index);

	if (info->dp->ds != ds && ds->ops->crosschip_lag_change)
		return ds->ops->crosschip_lag_change(ds, info->dp->ds->index,
						     info->dp->index);

	return 0;
}

static int dsa_switch_lag_join(struct dsa_switch *ds,
			       struct dsa_notifier_lag_info *info)
{
	if (info->dp->ds == ds && ds->ops->port_lag_join)
		return ds->ops->port_lag_join(ds, info->dp->index, info->lag,
					      info->info, info->extack);

	if (info->dp->ds != ds && ds->ops->crosschip_lag_join)
		return ds->ops->crosschip_lag_join(ds, info->dp->ds->index,
						   info->dp->index, info->lag,
						   info->info, info->extack);

	return -EOPNOTSUPP;
}

static int dsa_switch_lag_leave(struct dsa_switch *ds,
				struct dsa_notifier_lag_info *info)
{
	if (info->dp->ds == ds && ds->ops->port_lag_leave)
		return ds->ops->port_lag_leave(ds, info->dp->index, info->lag);

	if (info->dp->ds != ds && ds->ops->crosschip_lag_leave)
		return ds->ops->crosschip_lag_leave(ds, info->dp->ds->index,
						    info->dp->index, info->lag);

	return -EOPNOTSUPP;
}

static int dsa_switch_mdb_add(struct dsa_switch *ds,
			      struct dsa_notifier_mdb_info *info)
{
	int port = dsa_towards_port(ds, info->dp->ds->index, info->dp->index);
	struct dsa_port *dp = dsa_to_port(ds, port);

	if (!ds->ops->port_mdb_add)
		return -EOPNOTSUPP;

	return dsa_port_do_mdb_add(dp, info->mdb, info->db);
}

static int dsa_switch_mdb_del(struct dsa_switch *ds,
			      struct dsa_notifier_mdb_info *info)
{
	int port = dsa_towards_port(ds, info->dp->ds->index, info->dp->index);
	struct dsa_port *dp = dsa_to_port(ds, port);

	if (!ds->ops->port_mdb_del)
		return -EOPNOTSUPP;

	return dsa_port_do_mdb_del(dp, info->mdb, info->db);
}

static int dsa_switch_host_mdb_add(struct dsa_switch *ds,
				   struct dsa_notifier_mdb_info *info)
{
	struct dsa_port *dp;
	int err = 0;

	if (!ds->ops->port_mdb_add)
		return -EOPNOTSUPP;

	dsa_switch_for_each_port(dp, ds) {
		if (dsa_port_host_address_match(dp, info->dp)) {
			err = dsa_port_do_mdb_add(dp, info->mdb, info->db);
			if (err)
				break;
		}
	}

	return err;
}

static int dsa_switch_host_mdb_del(struct dsa_switch *ds,
				   struct dsa_notifier_mdb_info *info)
{
	struct dsa_port *dp;
	int err = 0;

	if (!ds->ops->port_mdb_del)
		return -EOPNOTSUPP;

	dsa_switch_for_each_port(dp, ds) {
		if (dsa_port_host_address_match(dp, info->dp)) {
			err = dsa_port_do_mdb_del(dp, info->mdb, info->db);
			if (err)
				break;
		}
	}

	return err;
}

/* Port VLANs match on the targeted port and on all DSA ports */
static bool dsa_port_vlan_match(struct dsa_port *dp,
				struct dsa_notifier_vlan_info *info)
{
	return dsa_port_is_dsa(dp) || dp == info->dp;
}

/* Host VLANs match on the targeted port's CPU port, and on all DSA ports
 * (upstream and downstream) of that switch and its upstream switches.
 */
static bool dsa_port_host_vlan_match(struct dsa_port *dp,
				     const struct dsa_port *targeted_dp)
{
	struct dsa_port *cpu_dp = targeted_dp->cpu_dp;

	if (dsa_switch_is_upstream_of(dp->ds, targeted_dp->ds))
		return dsa_port_is_dsa(dp) || dp == cpu_dp;

	return false;
}

static struct dsa_vlan *dsa_vlan_find(struct list_head *vlan_list,
				      const struct switchdev_obj_port_vlan *vlan)
{
	struct dsa_vlan *v;

	list_for_each_entry(v, vlan_list, list)
		if (v->vid == vlan->vid)
			return v;

	return NULL;
}

static int dsa_port_do_vlan_add(struct dsa_port *dp,
				const struct switchdev_obj_port_vlan *vlan,
				struct netlink_ext_ack *extack)
{
	struct dsa_switch *ds = dp->ds;
	int port = dp->index;
	struct dsa_vlan *v;
	int err = 0;

	/* No need to bother with refcounting for user ports. */
	if (!(dsa_port_is_cpu(dp) || dsa_port_is_dsa(dp)))
		return ds->ops->port_vlan_add(ds, port, vlan, extack);

	/* No need to propagate on shared ports the existing VLANs that were
	 * re-notified after just the flags have changed. This would cause a
	 * refcount bump which we need to avoid, since it unbalances the
	 * additions with the deletions.
	 */
	if (vlan->changed)
		return 0;

	mutex_lock(&dp->vlans_lock);

	v = dsa_vlan_find(&dp->vlans, vlan);
	if (v) {
		refcount_inc(&v->refcount);
		goto out;
	}

	v = kzalloc(sizeof(*v), GFP_KERNEL);
	if (!v) {
		err = -ENOMEM;
		goto out;
	}

	err = ds->ops->port_vlan_add(ds, port, vlan, extack);
	if (err) {
		kfree(v);
		goto out;
	}

	v->vid = vlan->vid;
	refcount_set(&v->refcount, 1);
	list_add_tail(&v->list, &dp->vlans);

out:
	mutex_unlock(&dp->vlans_lock);

	return err;
}

static int dsa_port_do_vlan_del(struct dsa_port *dp,
				const struct switchdev_obj_port_vlan *vlan)
{
	struct dsa_switch *ds = dp->ds;
	int port = dp->index;
	struct dsa_vlan *v;
	int err = 0;

	/* No need to bother with refcounting for user ports */
	if (!(dsa_port_is_cpu(dp) || dsa_port_is_dsa(dp)))
		return ds->ops->port_vlan_del(ds, port, vlan);

	mutex_lock(&dp->vlans_lock);

	v = dsa_vlan_find(&dp->vlans, vlan);
	if (!v) {
		err = -ENOENT;
		goto out;
	}

	if (!refcount_dec_and_test(&v->refcount))
		goto out;

	err = ds->ops->port_vlan_del(ds, port, vlan);
	if (err) {
		refcount_set(&v->refcount, 1);
		goto out;
	}

	list_del(&v->list);
	kfree(v);

out:
	mutex_unlock(&dp->vlans_lock);

	return err;
}

static int dsa_switch_vlan_add(struct dsa_switch *ds,
			       struct dsa_notifier_vlan_info *info)
{
	struct dsa_port *dp;
	int err;

	if (!ds->ops->port_vlan_add)
		return -EOPNOTSUPP;

	dsa_switch_for_each_port(dp, ds) {
		if (dsa_port_vlan_match(dp, info)) {
			err = dsa_port_do_vlan_add(dp, info->vlan,
						   info->extack);
			if (err)
				return err;
		}
	}

	return 0;
}

static int dsa_switch_vlan_del(struct dsa_switch *ds,
			       struct dsa_notifier_vlan_info *info)
{
	struct dsa_port *dp;
	int err;

	if (!ds->ops->port_vlan_del)
		return -EOPNOTSUPP;

	dsa_switch_for_each_port(dp, ds) {
		if (dsa_port_vlan_match(dp, info)) {
			err = dsa_port_do_vlan_del(dp, info->vlan);
			if (err)
				return err;
		}
	}

	return 0;
}

static int dsa_switch_host_vlan_add(struct dsa_switch *ds,
				    struct dsa_notifier_vlan_info *info)
{
	struct dsa_port *dp;
	int err;

	if (!ds->ops->port_vlan_add)
		return -EOPNOTSUPP;

	dsa_switch_for_each_port(dp, ds) {
		if (dsa_port_host_vlan_match(dp, info->dp)) {
			err = dsa_port_do_vlan_add(dp, info->vlan,
						   info->extack);
			if (err)
				return err;
		}
	}

	return 0;
}

static int dsa_switch_host_vlan_del(struct dsa_switch *ds,
				    struct dsa_notifier_vlan_info *info)
{
	struct dsa_port *dp;
	int err;

	if (!ds->ops->port_vlan_del)
		return -EOPNOTSUPP;

	dsa_switch_for_each_port(dp, ds) {
		if (dsa_port_host_vlan_match(dp, info->dp)) {
			err = dsa_port_do_vlan_del(dp, info->vlan);
			if (err)
				return err;
		}
	}

	return 0;
}

static int dsa_switch_change_tag_proto(struct dsa_switch *ds,
				       struct dsa_notifier_tag_proto_info *info)
{
	const struct dsa_device_ops *tag_ops = info->tag_ops;
	struct dsa_port *dp, *cpu_dp;
	int err;

	if (!ds->ops->change_tag_protocol)
		return -EOPNOTSUPP;

	ASSERT_RTNL();

	err = ds->ops->change_tag_protocol(ds, tag_ops->proto);
	if (err)
		return err;

	dsa_switch_for_each_cpu_port(cpu_dp, ds)
		dsa_port_set_tag_protocol(cpu_dp, tag_ops);

	/* Now that changing the tag protocol can no longer fail, let's update
	 * the remaining bits which are "duplicated for faster access", and the
	 * bits that depend on the tagger, such as the MTU.
	 */
	dsa_switch_for_each_user_port(dp, ds) {
		struct net_device *slave = dp->slave;

		dsa_slave_setup_tagger(slave);

		/* rtnl_mutex is held in dsa_tree_change_tag_proto */
		dsa_slave_change_mtu(slave, slave->mtu);
	}

	return 0;
}

/* We use the same cross-chip notifiers to inform both the tagger side, as well
 * as the switch side, of connection and disconnection events.
 * Since ds->tagger_data is owned by the tagger, it isn't a hard error if the
 * switch side doesn't support connecting to this tagger, and therefore, the
 * fact that we don't disconnect the tagger side doesn't constitute a memory
 * leak: the tagger will still operate with persistent per-switch memory, just
 * with the switch side unconnected to it. What does constitute a hard error is
 * when the switch side supports connecting but fails.
 */
static int
dsa_switch_connect_tag_proto(struct dsa_switch *ds,
			     struct dsa_notifier_tag_proto_info *info)
{
	const struct dsa_device_ops *tag_ops = info->tag_ops;
	int err;

	/* Notify the new tagger about the connection to this switch */
	if (tag_ops->connect) {
		err = tag_ops->connect(ds);
		if (err)
			return err;
	}

	if (!ds->ops->connect_tag_protocol)
		return -EOPNOTSUPP;

	/* Notify the switch about the connection to the new tagger */
	err = ds->ops->connect_tag_protocol(ds, tag_ops->proto);
	if (err) {
		/* Revert the new tagger's connection to this tree */
		if (tag_ops->disconnect)
			tag_ops->disconnect(ds);
		return err;
	}

	return 0;
}

static int
dsa_switch_disconnect_tag_proto(struct dsa_switch *ds,
				struct dsa_notifier_tag_proto_info *info)
{
	const struct dsa_device_ops *tag_ops = info->tag_ops;

	/* Notify the tagger about the disconnection from this switch */
	if (tag_ops->disconnect && ds->tagger_data)
		tag_ops->disconnect(ds);

	/* No need to notify the switch, since it shouldn't have any
	 * resources to tear down
	 */
	return 0;
}

static int
dsa_switch_master_state_change(struct dsa_switch *ds,
			       struct dsa_notifier_master_state_info *info)
{
	if (!ds->ops->master_state_change)
		return 0;

	ds->ops->master_state_change(ds, info->master, info->operational);

	return 0;
}

static int dsa_switch_event(struct notifier_block *nb,
			    unsigned long event, void *info)
{
	struct dsa_switch *ds = container_of(nb, struct dsa_switch, nb);
	int err;

	switch (event) {
	case DSA_NOTIFIER_AGEING_TIME:
		err = dsa_switch_ageing_time(ds, info);
		break;
	case DSA_NOTIFIER_BRIDGE_JOIN:
		err = dsa_switch_bridge_join(ds, info);
		break;
	case DSA_NOTIFIER_BRIDGE_LEAVE:
		err = dsa_switch_bridge_leave(ds, info);
		break;
	case DSA_NOTIFIER_FDB_ADD:
		err = dsa_switch_fdb_add(ds, info);
		break;
	case DSA_NOTIFIER_FDB_DEL:
		err = dsa_switch_fdb_del(ds, info);
		break;
	case DSA_NOTIFIER_HOST_FDB_ADD:
		err = dsa_switch_host_fdb_add(ds, info);
		break;
	case DSA_NOTIFIER_HOST_FDB_DEL:
		err = dsa_switch_host_fdb_del(ds, info);
		break;
	case DSA_NOTIFIER_LAG_FDB_ADD:
		err = dsa_switch_lag_fdb_add(ds, info);
		break;
	case DSA_NOTIFIER_LAG_FDB_DEL:
		err = dsa_switch_lag_fdb_del(ds, info);
		break;
	case DSA_NOTIFIER_LAG_CHANGE:
		err = dsa_switch_lag_change(ds, info);
		break;
	case DSA_NOTIFIER_LAG_JOIN:
		err = dsa_switch_lag_join(ds, info);
		break;
	case DSA_NOTIFIER_LAG_LEAVE:
		err = dsa_switch_lag_leave(ds, info);
		break;
	case DSA_NOTIFIER_MDB_ADD:
		err = dsa_switch_mdb_add(ds, info);
		break;
	case DSA_NOTIFIER_MDB_DEL:
		err = dsa_switch_mdb_del(ds, info);
		break;
	case DSA_NOTIFIER_HOST_MDB_ADD:
		err = dsa_switch_host_mdb_add(ds, info);
		break;
	case DSA_NOTIFIER_HOST_MDB_DEL:
		err = dsa_switch_host_mdb_del(ds, info);
		break;
	case DSA_NOTIFIER_VLAN_ADD:
		err = dsa_switch_vlan_add(ds, info);
		break;
	case DSA_NOTIFIER_VLAN_DEL:
		err = dsa_switch_vlan_del(ds, info);
		break;
	case DSA_NOTIFIER_HOST_VLAN_ADD:
		err = dsa_switch_host_vlan_add(ds, info);
		break;
	case DSA_NOTIFIER_HOST_VLAN_DEL:
		err = dsa_switch_host_vlan_del(ds, info);
		break;
	case DSA_NOTIFIER_MTU:
		err = dsa_switch_mtu(ds, info);
		break;
	case DSA_NOTIFIER_TAG_PROTO:
		err = dsa_switch_change_tag_proto(ds, info);
		break;
	case DSA_NOTIFIER_TAG_PROTO_CONNECT:
		err = dsa_switch_connect_tag_proto(ds, info);
		break;
	case DSA_NOTIFIER_TAG_PROTO_DISCONNECT:
		err = dsa_switch_disconnect_tag_proto(ds, info);
		break;
	case DSA_NOTIFIER_TAG_8021Q_VLAN_ADD:
		err = dsa_switch_tag_8021q_vlan_add(ds, info);
		break;
	case DSA_NOTIFIER_TAG_8021Q_VLAN_DEL:
		err = dsa_switch_tag_8021q_vlan_del(ds, info);
		break;
	case DSA_NOTIFIER_MASTER_STATE_CHANGE:
		err = dsa_switch_master_state_change(ds, info);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	if (err)
		dev_dbg(ds->dev, "breaking chain for DSA event %lu (%d)\n",
			event, err);

	return notifier_from_errno(err);
}

/**
 * dsa_tree_notify - Execute code for all switches in a DSA switch tree.
 * @dst: collection of struct dsa_switch devices to notify.
 * @e: event, must be of type DSA_NOTIFIER_*
 * @v: event-specific value.
 *
 * Given a struct dsa_switch_tree, this can be used to run a function once for
 * each member DSA switch. The other alternative of traversing the tree is only
 * through its ports list, which does not uniquely list the switches.
 */
int dsa_tree_notify(struct dsa_switch_tree *dst, unsigned long e, void *v)
{
	struct raw_notifier_head *nh = &dst->nh;
	int err;

	err = raw_notifier_call_chain(nh, e, v);

	return notifier_to_errno(err);
}

/**
 * dsa_broadcast - Notify all DSA trees in the system.
 * @e: event, must be of type DSA_NOTIFIER_*
 * @v: event-specific value.
 *
 * Can be used to notify the switching fabric of events such as cross-chip
 * bridging between disjoint trees (such as islands of tagger-compatible
 * switches bridged by an incompatible middle switch).
 *
 * WARNING: this function is not reliable during probe time, because probing
 * between trees is asynchronous and not all DSA trees might have probed.
 */
int dsa_broadcast(unsigned long e, void *v)
{
	struct dsa_switch_tree *dst;
	int err = 0;

	list_for_each_entry(dst, &dsa_tree_list, list) {
		err = dsa_tree_notify(dst, e, v);
		if (err)
			break;
	}

	return err;
}

int dsa_switch_register_notifier(struct dsa_switch *ds)
{
	ds->nb.notifier_call = dsa_switch_event;

	return raw_notifier_chain_register(&ds->dst->nh, &ds->nb);
}

void dsa_switch_unregister_notifier(struct dsa_switch *ds)
{
	int err;

	err = raw_notifier_chain_unregister(&ds->dst->nh, &ds->nb);
	if (err)
		dev_err(ds->dev, "failed to unregister notifier (%d)\n", err);
}
