/*
 * Handling of a single switch chip, part of a switch fabric
 *
 * Copyright (c) 2017 Savoir-faire Linux Inc.
 *	Vivien Didelot <vivien.didelot@savoirfairelinux.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/netdevice.h>
#include <linux/notifier.h>
#include <net/switchdev.h>

#include "dsa_priv.h"

static unsigned int dsa_switch_fastest_ageing_time(struct dsa_switch *ds,
						   unsigned int ageing_time)
{
	int i;

	for (i = 0; i < ds->num_ports; ++i) {
		struct dsa_port *dp = &ds->ports[i];

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

static int dsa_switch_bridge_join(struct dsa_switch *ds,
				  struct dsa_notifier_bridge_info *info)
{
	if (ds->index == info->sw_index && ds->ops->port_bridge_join)
		return ds->ops->port_bridge_join(ds, info->port, info->br);

	if (ds->index != info->sw_index && ds->ops->crosschip_bridge_join)
		return ds->ops->crosschip_bridge_join(ds, info->sw_index,
						      info->port, info->br);

	return 0;
}

static int dsa_switch_bridge_leave(struct dsa_switch *ds,
				   struct dsa_notifier_bridge_info *info)
{
	if (ds->index == info->sw_index && ds->ops->port_bridge_leave)
		ds->ops->port_bridge_leave(ds, info->port, info->br);

	if (ds->index != info->sw_index && ds->ops->crosschip_bridge_leave)
		ds->ops->crosschip_bridge_leave(ds, info->sw_index, info->port,
						info->br);

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

static int
dsa_switch_mdb_prepare_bitmap(struct dsa_switch *ds,
			      const struct switchdev_obj_port_mdb *mdb,
			      const unsigned long *bitmap)
{
	int port, err;

	if (!ds->ops->port_mdb_prepare || !ds->ops->port_mdb_add)
		return -EOPNOTSUPP;

	for_each_set_bit(port, bitmap, ds->num_ports) {
		err = ds->ops->port_mdb_prepare(ds, port, mdb);
		if (err)
			return err;
	}

	return 0;
}

static void dsa_switch_mdb_add_bitmap(struct dsa_switch *ds,
				      const struct switchdev_obj_port_mdb *mdb,
				      const unsigned long *bitmap)
{
	int port;

	for_each_set_bit(port, bitmap, ds->num_ports)
		ds->ops->port_mdb_add(ds, port, mdb);
}

static int dsa_switch_mdb_add(struct dsa_switch *ds,
			      struct dsa_notifier_mdb_info *info)
{
	const struct switchdev_obj_port_mdb *mdb = info->mdb;
	struct switchdev_trans *trans = info->trans;
	DECLARE_BITMAP(group, ds->num_ports);
	int port;

	/* Build a mask of Multicast group members */
	bitmap_zero(group, ds->num_ports);
	if (ds->index == info->sw_index)
		set_bit(info->port, group);
	for (port = 0; port < ds->num_ports; port++)
		if (dsa_is_dsa_port(ds, port))
			set_bit(port, group);

	if (switchdev_trans_ph_prepare(trans))
		return dsa_switch_mdb_prepare_bitmap(ds, mdb, group);

	dsa_switch_mdb_add_bitmap(ds, mdb, group);

	return 0;
}

static int dsa_switch_mdb_del(struct dsa_switch *ds,
			      struct dsa_notifier_mdb_info *info)
{
	const struct switchdev_obj_port_mdb *mdb = info->mdb;

	if (!ds->ops->port_mdb_del)
		return -EOPNOTSUPP;

	if (ds->index == info->sw_index)
		return ds->ops->port_mdb_del(ds, info->port, mdb);

	return 0;
}

static int
dsa_switch_vlan_prepare_bitmap(struct dsa_switch *ds,
			       const struct switchdev_obj_port_vlan *vlan,
			       const unsigned long *bitmap)
{
	int port, err;

	if (!ds->ops->port_vlan_prepare || !ds->ops->port_vlan_add)
		return -EOPNOTSUPP;

	for_each_set_bit(port, bitmap, ds->num_ports) {
		err = ds->ops->port_vlan_prepare(ds, port, vlan);
		if (err)
			return err;
	}

	return 0;
}

static void
dsa_switch_vlan_add_bitmap(struct dsa_switch *ds,
			   const struct switchdev_obj_port_vlan *vlan,
			   const unsigned long *bitmap)
{
	int port;

	for_each_set_bit(port, bitmap, ds->num_ports)
		ds->ops->port_vlan_add(ds, port, vlan);
}

static int dsa_switch_vlan_add(struct dsa_switch *ds,
			       struct dsa_notifier_vlan_info *info)
{
	const struct switchdev_obj_port_vlan *vlan = info->vlan;
	struct switchdev_trans *trans = info->trans;
	DECLARE_BITMAP(members, ds->num_ports);
	int port;

	/* Build a mask of VLAN members */
	bitmap_zero(members, ds->num_ports);
	if (ds->index == info->sw_index)
		set_bit(info->port, members);
	for (port = 0; port < ds->num_ports; port++)
		if (dsa_is_cpu_port(ds, port) || dsa_is_dsa_port(ds, port))
			set_bit(port, members);

	if (switchdev_trans_ph_prepare(trans))
		return dsa_switch_vlan_prepare_bitmap(ds, vlan, members);

	dsa_switch_vlan_add_bitmap(ds, vlan, members);

	return 0;
}

static int dsa_switch_vlan_del(struct dsa_switch *ds,
			       struct dsa_notifier_vlan_info *info)
{
	const struct switchdev_obj_port_vlan *vlan = info->vlan;

	if (!ds->ops->port_vlan_del)
		return -EOPNOTSUPP;

	if (ds->index == info->sw_index)
		return ds->ops->port_vlan_del(ds, info->port, vlan);

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
