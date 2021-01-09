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

#include "dsa_priv.h"

static unsigned int dsa_switch_fastest_ageing_time(struct dsa_switch *ds,
						   unsigned int ageing_time)
{
	int i;

	for (i = 0; i < ds->num_ports; ++i) {
		struct dsa_port *dp = dsa_to_port(ds, i);

		if (dp->ageing_time && dp->ageing_time < ageing_time)
			ageing_time = dp->ageing_time;
	}

	return ageing_time;
}

static int dsa_switch_ageing_time(struct dsa_switch *ds,
				  struct dsa_notifier_ageing_time_info *info)
{
	unsigned int ageing_time = info->ageing_time;
	struct switchdev_trans *trans = info->trans;

	if (switchdev_trans_ph_prepare(trans)) {
		if (ds->ageing_time_min && ageing_time < ds->ageing_time_min)
			return -ERANGE;
		if (ds->ageing_time_max && ageing_time > ds->ageing_time_max)
			return -ERANGE;
		return 0;
	}

	/* Program the fastest ageing time in case of multiple bridges */
	ageing_time = dsa_switch_fastest_ageing_time(ds, ageing_time);

	if (ds->ops->set_ageing_time)
		return ds->ops->set_ageing_time(ds, ageing_time);

	return 0;
}

static bool dsa_switch_mtu_match(struct dsa_switch *ds, int port,
				 struct dsa_notifier_mtu_info *info)
{
	if (ds->index == info->sw_index)
		return (port == info->port) || dsa_is_dsa_port(ds, port);

	if (!info->propagate_upstream)
		return false;

	if (dsa_is_dsa_port(ds, port) || dsa_is_cpu_port(ds, port))
		return true;

	return false;
}

static int dsa_switch_mtu(struct dsa_switch *ds,
			  struct dsa_notifier_mtu_info *info)
{
	int port, ret;

	if (!ds->ops->port_change_mtu)
		return -EOPNOTSUPP;

	for (port = 0; port < ds->num_ports; port++) {
		if (dsa_switch_mtu_match(ds, port, info)) {
			ret = ds->ops->port_change_mtu(ds, port, info->mtu);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int dsa_switch_bridge_join(struct dsa_switch *ds,
				  struct dsa_notifier_bridge_info *info)
{
	struct dsa_switch_tree *dst = ds->dst;

	if (dst->index == info->tree_index && ds->index == info->sw_index &&
	    ds->ops->port_bridge_join)
		return ds->ops->port_bridge_join(ds, info->port, info->br);

	if ((dst->index != info->tree_index || ds->index != info->sw_index) &&
	    ds->ops->crosschip_bridge_join)
		return ds->ops->crosschip_bridge_join(ds, info->tree_index,
						      info->sw_index,
						      info->port, info->br);

	return 0;
}

static int dsa_switch_bridge_leave(struct dsa_switch *ds,
				   struct dsa_notifier_bridge_info *info)
{
	bool unset_vlan_filtering = br_vlan_enabled(info->br);
	struct dsa_switch_tree *dst = ds->dst;
	int err, i;

	if (dst->index == info->tree_index && ds->index == info->sw_index &&
	    ds->ops->port_bridge_join)
		ds->ops->port_bridge_leave(ds, info->port, info->br);

	if ((dst->index != info->tree_index || ds->index != info->sw_index) &&
	    ds->ops->crosschip_bridge_join)
		ds->ops->crosschip_bridge_leave(ds, info->tree_index,
						info->sw_index, info->port,
						info->br);

	/* If the bridge was vlan_filtering, the bridge core doesn't trigger an
	 * event for changing vlan_filtering setting upon slave ports leaving
	 * it. That is a good thing, because that lets us handle it and also
	 * handle the case where the switch's vlan_filtering setting is global
	 * (not per port). When that happens, the correct moment to trigger the
	 * vlan_filtering callback is only when the last port left this bridge.
	 */
	if (unset_vlan_filtering && ds->vlan_filtering_is_global) {
		for (i = 0; i < ds->num_ports; i++) {
			if (i == info->port)
				continue;
			if (dsa_to_port(ds, i)->bridge_dev == info->br) {
				unset_vlan_filtering = false;
				break;
			}
		}
	}
	if (unset_vlan_filtering) {
		struct switchdev_trans trans;

		trans.ph_prepare = true;
		err = dsa_port_vlan_filtering(dsa_to_port(ds, info->port),
					      false, &trans);
		if (err && err != EOPNOTSUPP)
			return err;

		trans.ph_prepare = false;
		err = dsa_port_vlan_filtering(dsa_to_port(ds, info->port),
					      false, &trans);
		if (err && err != EOPNOTSUPP)
			return err;
	}
	return 0;
}

static int dsa_switch_fdb_add(struct dsa_switch *ds,
			      struct dsa_notifier_fdb_info *info)
{
	int port = dsa_towards_port(ds, info->sw_index, info->port);

	if (!ds->ops->port_fdb_add)
		return -EOPNOTSUPP;

	return ds->ops->port_fdb_add(ds, port, info->addr, info->vid);
}

static int dsa_switch_fdb_del(struct dsa_switch *ds,
			      struct dsa_notifier_fdb_info *info)
{
	int port = dsa_towards_port(ds, info->sw_index, info->port);

	if (!ds->ops->port_fdb_del)
		return -EOPNOTSUPP;

	return ds->ops->port_fdb_del(ds, port, info->addr, info->vid);
}

static bool dsa_switch_mdb_match(struct dsa_switch *ds, int port,
				 struct dsa_notifier_mdb_info *info)
{
	if (ds->index == info->sw_index && port == info->port)
		return true;

	if (dsa_is_dsa_port(ds, port))
		return true;

	return false;
}

static int dsa_switch_mdb_add(struct dsa_switch *ds,
			      struct dsa_notifier_mdb_info *info)
{
	int port, err;

	if (!ds->ops->port_mdb_prepare || !ds->ops->port_mdb_add)
		return -EOPNOTSUPP;

	for (port = 0; port < ds->num_ports; port++) {
		if (dsa_switch_mdb_match(ds, port, info)) {
			err = ds->ops->port_mdb_prepare(ds, port, info->mdb);
			if (err)
				return err;
		}
	}

	for (port = 0; port < ds->num_ports; port++)
		if (dsa_switch_mdb_match(ds, port, info))
			ds->ops->port_mdb_add(ds, port, info->mdb);

	return 0;
}

static int dsa_switch_mdb_del(struct dsa_switch *ds,
			      struct dsa_notifier_mdb_info *info)
{
	if (!ds->ops->port_mdb_del)
		return -EOPNOTSUPP;

	if (ds->index == info->sw_index)
		return ds->ops->port_mdb_del(ds, info->port, info->mdb);

	return 0;
}

static bool dsa_switch_vlan_match(struct dsa_switch *ds, int port,
				  struct dsa_notifier_vlan_info *info)
{
	if (ds->index == info->sw_index && port == info->port)
		return true;

	if (dsa_is_dsa_port(ds, port))
		return true;

	return false;
}

static int dsa_switch_vlan_add(struct dsa_switch *ds,
			       struct dsa_notifier_vlan_info *info)
{
	int port, err;

	if (!ds->ops->port_vlan_prepare || !ds->ops->port_vlan_add)
		return -EOPNOTSUPP;

	for (port = 0; port < ds->num_ports; port++) {
		if (dsa_switch_vlan_match(ds, port, info)) {
			err = ds->ops->port_vlan_prepare(ds, port, info->vlan);
			if (err)
				return err;
		}
	}

	for (port = 0; port < ds->num_ports; port++)
		if (dsa_switch_vlan_match(ds, port, info))
			ds->ops->port_vlan_add(ds, port, info->vlan);

	return 0;
}

static int dsa_switch_vlan_del(struct dsa_switch *ds,
			       struct dsa_notifier_vlan_info *info)
{
	if (!ds->ops->port_vlan_del)
		return -EOPNOTSUPP;

	if (ds->index == info->sw_index)
		return ds->ops->port_vlan_del(ds, info->port, info->vlan);

	/* Do not deprogram the DSA links as they may be used as conduit
	 * for other VLAN members in the fabric.
	 */
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
	case DSA_NOTIFIER_MDB_ADD:
		err = dsa_switch_mdb_add(ds, info);
		break;
	case DSA_NOTIFIER_MDB_DEL:
		err = dsa_switch_mdb_del(ds, info);
		break;
	case DSA_NOTIFIER_VLAN_ADD:
		err = dsa_switch_vlan_add(ds, info);
		break;
	case DSA_NOTIFIER_VLAN_DEL:
		err = dsa_switch_vlan_del(ds, info);
		break;
	case DSA_NOTIFIER_MTU:
		err = dsa_switch_mtu(ds, info);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	/* Non-switchdev operations cannot be rolled back. If a DSA driver
	 * returns an error during the chained call, switch chips may be in an
	 * inconsistent state.
	 */
	if (err)
		dev_dbg(ds->dev, "breaking chain for DSA event %lu (%d)\n",
			event, err);

	return notifier_from_errno(err);
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
