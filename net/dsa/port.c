/*
 * Handling of a single switch port
 *
 * Copyright (c) 2017 Savoir-faire Linux Inc.
 *	Vivien Didelot <vivien.didelot@savoirfairelinux.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/if_bridge.h>
#include <linux/notifier.h>

#include "dsa_priv.h"

static int dsa_port_notify(struct dsa_port *dp, unsigned long e, void *v)
{
	struct raw_notifier_head *nh = &dp->ds->dst->nh;
	int err;

	err = raw_notifier_call_chain(nh, e, v);

	return notifier_to_errno(err);
}

int dsa_port_set_state(struct dsa_port *dp, u8 state,
		       struct switchdev_trans *trans)
{
	struct dsa_switch *ds = dp->ds;
	int port = dp->index;

	if (switchdev_trans_ph_prepare(trans))
		return ds->ops->port_stp_state_set ? 0 : -EOPNOTSUPP;

	if (ds->ops->port_stp_state_set)
		ds->ops->port_stp_state_set(ds, port, state);

	if (ds->ops->port_fast_age) {
		/* Fast age FDB entries or flush appropriate forwarding database
		 * for the given port, if we are moving it from Learning or
		 * Forwarding state, to Disabled or Blocking or Listening state.
		 */

		if ((dp->stp_state == BR_STATE_LEARNING ||
		     dp->stp_state == BR_STATE_FORWARDING) &&
		    (state == BR_STATE_DISABLED ||
		     state == BR_STATE_BLOCKING ||
		     state == BR_STATE_LISTENING))
			ds->ops->port_fast_age(ds, port);
	}

	dp->stp_state = state;

	return 0;
}

void dsa_port_set_state_now(struct dsa_port *dp, u8 state)
{
	int err;

	err = dsa_port_set_state(dp, state, NULL);
	if (err)
		pr_err("DSA: failed to set STP state %u (%d)\n", state, err);
}

int dsa_port_bridge_join(struct dsa_port *dp, struct net_device *br)
{
	struct dsa_notifier_bridge_info info = {
		.sw_index = dp->ds->index,
		.port = dp->index,
		.br = br,
	};
	int err;

	/* Here the port is already bridged. Reflect the current configuration
	 * so that drivers can program their chips accordingly.
	 */
	dp->bridge_dev = br;

	err = dsa_port_notify(dp, DSA_NOTIFIER_BRIDGE_JOIN, &info);

	/* The bridging is rolled back on error */
	if (err)
		dp->bridge_dev = NULL;

	return err;
}

void dsa_port_bridge_leave(struct dsa_port *dp, struct net_device *br)
{
	struct dsa_notifier_bridge_info info = {
		.sw_index = dp->ds->index,
		.port = dp->index,
		.br = br,
	};
	int err;

	/* Here the port is already unbridged. Reflect the current configuration
	 * so that drivers can program their chips accordingly.
	 */
	dp->bridge_dev = NULL;

	err = dsa_port_notify(dp, DSA_NOTIFIER_BRIDGE_LEAVE, &info);
	if (err)
		pr_err("DSA: failed to notify DSA_NOTIFIER_BRIDGE_LEAVE\n");

	/* Port left the bridge, put in BR_STATE_DISABLED by the bridge layer,
	 * so allow it to be in BR_STATE_FORWARDING to be kept functional
	 */
	dsa_port_set_state_now(dp, BR_STATE_FORWARDING);
}

int dsa_port_vlan_filtering(struct dsa_port *dp, bool vlan_filtering,
			    struct switchdev_trans *trans)
{
	struct dsa_switch *ds = dp->ds;

	/* bridge skips -EOPNOTSUPP, so skip the prepare phase */
	if (switchdev_trans_ph_prepare(trans))
		return 0;

	if (ds->ops->port_vlan_filtering)
		return ds->ops->port_vlan_filtering(ds, dp->index,
						    vlan_filtering);

	return 0;
}

int dsa_port_ageing_time(struct dsa_port *dp, clock_t ageing_clock,
			 struct switchdev_trans *trans)
{
	unsigned long ageing_jiffies = clock_t_to_jiffies(ageing_clock);
	unsigned int ageing_time = jiffies_to_msecs(ageing_jiffies);
	struct dsa_notifier_ageing_time_info info = {
		.ageing_time = ageing_time,
		.trans = trans,
	};

	if (switchdev_trans_ph_prepare(trans))
		return dsa_port_notify(dp, DSA_NOTIFIER_AGEING_TIME, &info);

	dp->ageing_time = ageing_time;

	return dsa_port_notify(dp, DSA_NOTIFIER_AGEING_TIME, &info);
}

int dsa_port_fdb_add(struct dsa_port *dp, const unsigned char *addr,
		     u16 vid)
{
	struct dsa_notifier_fdb_info info = {
		.sw_index = dp->ds->index,
		.port = dp->index,
		.addr = addr,
		.vid = vid,
	};

	return dsa_port_notify(dp, DSA_NOTIFIER_FDB_ADD, &info);
}

int dsa_port_fdb_del(struct dsa_port *dp, const unsigned char *addr,
		     u16 vid)
{
	struct dsa_notifier_fdb_info info = {
		.sw_index = dp->ds->index,
		.port = dp->index,
		.addr = addr,
		.vid = vid,

	};

	return dsa_port_notify(dp, DSA_NOTIFIER_FDB_DEL, &info);
}

int dsa_port_fdb_dump(struct dsa_port *dp, struct switchdev_obj_port_fdb *fdb,
		      switchdev_obj_dump_cb_t *cb)
{
	struct dsa_switch *ds = dp->ds;

	if (ds->ops->port_fdb_dump)
		return ds->ops->port_fdb_dump(ds, dp->index, fdb, cb);

	return -EOPNOTSUPP;
}

int dsa_port_mdb_add(struct dsa_port *dp,
		     const struct switchdev_obj_port_mdb *mdb,
		     struct switchdev_trans *trans)
{
	struct dsa_notifier_mdb_info info = {
		.sw_index = dp->ds->index,
		.port = dp->index,
		.trans = trans,
		.mdb = mdb,
	};

	return dsa_port_notify(dp, DSA_NOTIFIER_MDB_ADD, &info);
}

int dsa_port_mdb_del(struct dsa_port *dp,
		     const struct switchdev_obj_port_mdb *mdb)
{
	struct dsa_notifier_mdb_info info = {
		.sw_index = dp->ds->index,
		.port = dp->index,
		.mdb = mdb,
	};

	return dsa_port_notify(dp, DSA_NOTIFIER_MDB_DEL, &info);
}

int dsa_port_mdb_dump(struct dsa_port *dp, struct switchdev_obj_port_mdb *mdb,
		      switchdev_obj_dump_cb_t *cb)
{
	struct dsa_switch *ds = dp->ds;

	if (ds->ops->port_mdb_dump)
		return ds->ops->port_mdb_dump(ds, dp->index, mdb, cb);

	return -EOPNOTSUPP;
}

int dsa_port_vlan_add(struct dsa_port *dp,
		      const struct switchdev_obj_port_vlan *vlan,
		      struct switchdev_trans *trans)
{
	struct dsa_notifier_vlan_info info = {
		.sw_index = dp->ds->index,
		.port = dp->index,
		.trans = trans,
		.vlan = vlan,
	};

	return dsa_port_notify(dp, DSA_NOTIFIER_VLAN_ADD, &info);
}

int dsa_port_vlan_del(struct dsa_port *dp,
		      const struct switchdev_obj_port_vlan *vlan)
{
	struct dsa_notifier_vlan_info info = {
		.sw_index = dp->ds->index,
		.port = dp->index,
		.vlan = vlan,
	};

	return dsa_port_notify(dp, DSA_NOTIFIER_VLAN_DEL, &info);
}

int dsa_port_vlan_dump(struct dsa_port *dp,
		       struct switchdev_obj_port_vlan *vlan,
		       switchdev_obj_dump_cb_t *cb)
{
	struct dsa_switch *ds = dp->ds;

	if (ds->ops->port_vlan_dump)
		return ds->ops->port_vlan_dump(ds, dp->index, vlan, cb);

	return -EOPNOTSUPP;
}
