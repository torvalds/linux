// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Handling of a single switch port
 *
 * Copyright (c) 2017 Savoir-faire Linux Inc.
 *	Vivien Didelot <vivien.didelot@savoirfairelinux.com>
 */

#include <linux/if_bridge.h>
#include <linux/notifier.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>

#include "dsa_priv.h"

/**
 * dsa_port_notify - Notify the switching fabric of changes to a port
 * @dp: port on which change occurred
 * @e: event, must be of type DSA_NOTIFIER_*
 * @v: event-specific value.
 *
 * Notify all switches in the DSA tree that this port's switch belongs to,
 * including this switch itself, of an event. Allows the other switches to
 * reconfigure themselves for cross-chip operations. Can also be used to
 * reconfigure ports without net_devices (CPU ports, DSA links) whenever
 * a user port's state changes.
 */
static int dsa_port_notify(const struct dsa_port *dp, unsigned long e, void *v)
{
	return dsa_tree_notify(dp->ds->dst, e, v);
}

static void dsa_port_notify_bridge_fdb_flush(const struct dsa_port *dp, u16 vid)
{
	struct net_device *brport_dev = dsa_port_to_bridge_port(dp);
	struct switchdev_notifier_fdb_info info = {
		.vid = vid,
	};

	/* When the port becomes standalone it has already left the bridge.
	 * Don't notify the bridge in that case.
	 */
	if (!brport_dev)
		return;

	call_switchdev_notifiers(SWITCHDEV_FDB_FLUSH_TO_BRIDGE,
				 brport_dev, &info.info, NULL);
}

static void dsa_port_fast_age(const struct dsa_port *dp)
{
	struct dsa_switch *ds = dp->ds;

	if (!ds->ops->port_fast_age)
		return;

	ds->ops->port_fast_age(ds, dp->index);

	/* flush all VLANs */
	dsa_port_notify_bridge_fdb_flush(dp, 0);
}

static int dsa_port_vlan_fast_age(const struct dsa_port *dp, u16 vid)
{
	struct dsa_switch *ds = dp->ds;
	int err;

	if (!ds->ops->port_vlan_fast_age)
		return -EOPNOTSUPP;

	err = ds->ops->port_vlan_fast_age(ds, dp->index, vid);

	if (!err)
		dsa_port_notify_bridge_fdb_flush(dp, vid);

	return err;
}

static int dsa_port_msti_fast_age(const struct dsa_port *dp, u16 msti)
{
	DECLARE_BITMAP(vids, VLAN_N_VID) = { 0 };
	int err, vid;

	err = br_mst_get_info(dsa_port_bridge_dev_get(dp), msti, vids);
	if (err)
		return err;

	for_each_set_bit(vid, vids, VLAN_N_VID) {
		err = dsa_port_vlan_fast_age(dp, vid);
		if (err)
			return err;
	}

	return 0;
}

static bool dsa_port_can_configure_learning(struct dsa_port *dp)
{
	struct switchdev_brport_flags flags = {
		.mask = BR_LEARNING,
	};
	struct dsa_switch *ds = dp->ds;
	int err;

	if (!ds->ops->port_bridge_flags || !ds->ops->port_pre_bridge_flags)
		return false;

	err = ds->ops->port_pre_bridge_flags(ds, dp->index, flags, NULL);
	return !err;
}

int dsa_port_set_state(struct dsa_port *dp, u8 state, bool do_fast_age)
{
	struct dsa_switch *ds = dp->ds;
	int port = dp->index;

	if (!ds->ops->port_stp_state_set)
		return -EOPNOTSUPP;

	ds->ops->port_stp_state_set(ds, port, state);

	if (!dsa_port_can_configure_learning(dp) ||
	    (do_fast_age && dp->learning)) {
		/* Fast age FDB entries or flush appropriate forwarding database
		 * for the given port, if we are moving it from Learning or
		 * Forwarding state, to Disabled or Blocking or Listening state.
		 * Ports that were standalone before the STP state change don't
		 * need to fast age the FDB, since address learning is off in
		 * standalone mode.
		 */

		if ((dp->stp_state == BR_STATE_LEARNING ||
		     dp->stp_state == BR_STATE_FORWARDING) &&
		    (state == BR_STATE_DISABLED ||
		     state == BR_STATE_BLOCKING ||
		     state == BR_STATE_LISTENING))
			dsa_port_fast_age(dp);
	}

	dp->stp_state = state;

	return 0;
}

static void dsa_port_set_state_now(struct dsa_port *dp, u8 state,
				   bool do_fast_age)
{
	struct dsa_switch *ds = dp->ds;
	int err;

	err = dsa_port_set_state(dp, state, do_fast_age);
	if (err && err != -EOPNOTSUPP) {
		dev_err(ds->dev, "port %d failed to set STP state %u: %pe\n",
			dp->index, state, ERR_PTR(err));
	}
}

int dsa_port_set_mst_state(struct dsa_port *dp,
			   const struct switchdev_mst_state *state,
			   struct netlink_ext_ack *extack)
{
	struct dsa_switch *ds = dp->ds;
	u8 prev_state;
	int err;

	if (!ds->ops->port_mst_state_set)
		return -EOPNOTSUPP;

	err = br_mst_get_state(dsa_port_to_bridge_port(dp), state->msti,
			       &prev_state);
	if (err)
		return err;

	err = ds->ops->port_mst_state_set(ds, dp->index, state);
	if (err)
		return err;

	if (!(dp->learning &&
	      (prev_state == BR_STATE_LEARNING ||
	       prev_state == BR_STATE_FORWARDING) &&
	      (state->state == BR_STATE_DISABLED ||
	       state->state == BR_STATE_BLOCKING ||
	       state->state == BR_STATE_LISTENING)))
		return 0;

	err = dsa_port_msti_fast_age(dp, state->msti);
	if (err)
		NL_SET_ERR_MSG_MOD(extack,
				   "Unable to flush associated VLANs");

	return 0;
}

int dsa_port_enable_rt(struct dsa_port *dp, struct phy_device *phy)
{
	struct dsa_switch *ds = dp->ds;
	int port = dp->index;
	int err;

	if (ds->ops->port_enable) {
		err = ds->ops->port_enable(ds, port, phy);
		if (err)
			return err;
	}

	if (!dp->bridge)
		dsa_port_set_state_now(dp, BR_STATE_FORWARDING, false);

	if (dp->pl)
		phylink_start(dp->pl);

	return 0;
}

int dsa_port_enable(struct dsa_port *dp, struct phy_device *phy)
{
	int err;

	rtnl_lock();
	err = dsa_port_enable_rt(dp, phy);
	rtnl_unlock();

	return err;
}

void dsa_port_disable_rt(struct dsa_port *dp)
{
	struct dsa_switch *ds = dp->ds;
	int port = dp->index;

	if (dp->pl)
		phylink_stop(dp->pl);

	if (!dp->bridge)
		dsa_port_set_state_now(dp, BR_STATE_DISABLED, false);

	if (ds->ops->port_disable)
		ds->ops->port_disable(ds, port);
}

void dsa_port_disable(struct dsa_port *dp)
{
	rtnl_lock();
	dsa_port_disable_rt(dp);
	rtnl_unlock();
}

static void dsa_port_reset_vlan_filtering(struct dsa_port *dp,
					  struct dsa_bridge bridge)
{
	struct netlink_ext_ack extack = {0};
	bool change_vlan_filtering = false;
	struct dsa_switch *ds = dp->ds;
	struct dsa_port *other_dp;
	bool vlan_filtering;
	int err;

	if (ds->needs_standalone_vlan_filtering &&
	    !br_vlan_enabled(bridge.dev)) {
		change_vlan_filtering = true;
		vlan_filtering = true;
	} else if (!ds->needs_standalone_vlan_filtering &&
		   br_vlan_enabled(bridge.dev)) {
		change_vlan_filtering = true;
		vlan_filtering = false;
	}

	/* If the bridge was vlan_filtering, the bridge core doesn't trigger an
	 * event for changing vlan_filtering setting upon slave ports leaving
	 * it. That is a good thing, because that lets us handle it and also
	 * handle the case where the switch's vlan_filtering setting is global
	 * (not per port). When that happens, the correct moment to trigger the
	 * vlan_filtering callback is only when the last port leaves the last
	 * VLAN-aware bridge.
	 */
	if (change_vlan_filtering && ds->vlan_filtering_is_global) {
		dsa_switch_for_each_port(other_dp, ds) {
			struct net_device *br = dsa_port_bridge_dev_get(other_dp);

			if (br && br_vlan_enabled(br)) {
				change_vlan_filtering = false;
				break;
			}
		}
	}

	if (!change_vlan_filtering)
		return;

	err = dsa_port_vlan_filtering(dp, vlan_filtering, &extack);
	if (extack._msg) {
		dev_err(ds->dev, "port %d: %s\n", dp->index,
			extack._msg);
	}
	if (err && err != -EOPNOTSUPP) {
		dev_err(ds->dev,
			"port %d failed to reset VLAN filtering to %d: %pe\n",
		       dp->index, vlan_filtering, ERR_PTR(err));
	}
}

static int dsa_port_inherit_brport_flags(struct dsa_port *dp,
					 struct netlink_ext_ack *extack)
{
	const unsigned long mask = BR_LEARNING | BR_FLOOD | BR_MCAST_FLOOD |
				   BR_BCAST_FLOOD | BR_PORT_LOCKED;
	struct net_device *brport_dev = dsa_port_to_bridge_port(dp);
	int flag, err;

	for_each_set_bit(flag, &mask, 32) {
		struct switchdev_brport_flags flags = {0};

		flags.mask = BIT(flag);

		if (br_port_flag_is_set(brport_dev, BIT(flag)))
			flags.val = BIT(flag);

		err = dsa_port_bridge_flags(dp, flags, extack);
		if (err && err != -EOPNOTSUPP)
			return err;
	}

	return 0;
}

static void dsa_port_clear_brport_flags(struct dsa_port *dp)
{
	const unsigned long val = BR_FLOOD | BR_MCAST_FLOOD | BR_BCAST_FLOOD;
	const unsigned long mask = BR_LEARNING | BR_FLOOD | BR_MCAST_FLOOD |
				   BR_BCAST_FLOOD | BR_PORT_LOCKED;
	int flag, err;

	for_each_set_bit(flag, &mask, 32) {
		struct switchdev_brport_flags flags = {0};

		flags.mask = BIT(flag);
		flags.val = val & BIT(flag);

		err = dsa_port_bridge_flags(dp, flags, NULL);
		if (err && err != -EOPNOTSUPP)
			dev_err(dp->ds->dev,
				"failed to clear bridge port flag %lu: %pe\n",
				flags.val, ERR_PTR(err));
	}
}

static int dsa_port_switchdev_sync_attrs(struct dsa_port *dp,
					 struct netlink_ext_ack *extack)
{
	struct net_device *brport_dev = dsa_port_to_bridge_port(dp);
	struct net_device *br = dsa_port_bridge_dev_get(dp);
	int err;

	err = dsa_port_inherit_brport_flags(dp, extack);
	if (err)
		return err;

	err = dsa_port_set_state(dp, br_port_get_stp_state(brport_dev), false);
	if (err && err != -EOPNOTSUPP)
		return err;

	err = dsa_port_vlan_filtering(dp, br_vlan_enabled(br), extack);
	if (err && err != -EOPNOTSUPP)
		return err;

	err = dsa_port_ageing_time(dp, br_get_ageing_time(br));
	if (err && err != -EOPNOTSUPP)
		return err;

	return 0;
}

static void dsa_port_switchdev_unsync_attrs(struct dsa_port *dp,
					    struct dsa_bridge bridge)
{
	/* Configure the port for standalone mode (no address learning,
	 * flood everything).
	 * The bridge only emits SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS events
	 * when the user requests it through netlink or sysfs, but not
	 * automatically at port join or leave, so we need to handle resetting
	 * the brport flags ourselves. But we even prefer it that way, because
	 * otherwise, some setups might never get the notification they need,
	 * for example, when a port leaves a LAG that offloads the bridge,
	 * it becomes standalone, but as far as the bridge is concerned, no
	 * port ever left.
	 */
	dsa_port_clear_brport_flags(dp);

	/* Port left the bridge, put in BR_STATE_DISABLED by the bridge layer,
	 * so allow it to be in BR_STATE_FORWARDING to be kept functional
	 */
	dsa_port_set_state_now(dp, BR_STATE_FORWARDING, true);

	dsa_port_reset_vlan_filtering(dp, bridge);

	/* Ageing time may be global to the switch chip, so don't change it
	 * here because we have no good reason (or value) to change it to.
	 */
}

static int dsa_port_bridge_create(struct dsa_port *dp,
				  struct net_device *br,
				  struct netlink_ext_ack *extack)
{
	struct dsa_switch *ds = dp->ds;
	struct dsa_bridge *bridge;

	bridge = dsa_tree_bridge_find(ds->dst, br);
	if (bridge) {
		refcount_inc(&bridge->refcount);
		dp->bridge = bridge;
		return 0;
	}

	bridge = kzalloc(sizeof(*bridge), GFP_KERNEL);
	if (!bridge)
		return -ENOMEM;

	refcount_set(&bridge->refcount, 1);

	bridge->dev = br;

	bridge->num = dsa_bridge_num_get(br, ds->max_num_bridges);
	if (ds->max_num_bridges && !bridge->num) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Range of offloadable bridges exceeded");
		kfree(bridge);
		return -EOPNOTSUPP;
	}

	dp->bridge = bridge;

	return 0;
}

static void dsa_port_bridge_destroy(struct dsa_port *dp,
				    const struct net_device *br)
{
	struct dsa_bridge *bridge = dp->bridge;

	dp->bridge = NULL;

	if (!refcount_dec_and_test(&bridge->refcount))
		return;

	if (bridge->num)
		dsa_bridge_num_put(br, bridge->num);

	kfree(bridge);
}

static bool dsa_port_supports_mst(struct dsa_port *dp)
{
	struct dsa_switch *ds = dp->ds;

	return ds->ops->vlan_msti_set &&
		ds->ops->port_mst_state_set &&
		ds->ops->port_vlan_fast_age &&
		dsa_port_can_configure_learning(dp);
}

int dsa_port_bridge_join(struct dsa_port *dp, struct net_device *br,
			 struct netlink_ext_ack *extack)
{
	struct dsa_notifier_bridge_info info = {
		.dp = dp,
		.extack = extack,
	};
	struct net_device *dev = dp->slave;
	struct net_device *brport_dev;
	int err;

	if (br_mst_enabled(br) && !dsa_port_supports_mst(dp))
		return -EOPNOTSUPP;

	/* Here the interface is already bridged. Reflect the current
	 * configuration so that drivers can program their chips accordingly.
	 */
	err = dsa_port_bridge_create(dp, br, extack);
	if (err)
		return err;

	brport_dev = dsa_port_to_bridge_port(dp);

	info.bridge = *dp->bridge;
	err = dsa_broadcast(DSA_NOTIFIER_BRIDGE_JOIN, &info);
	if (err)
		goto out_rollback;

	/* Drivers which support bridge TX forwarding should set this */
	dp->bridge->tx_fwd_offload = info.tx_fwd_offload;

	err = switchdev_bridge_port_offload(brport_dev, dev, dp,
					    &dsa_slave_switchdev_notifier,
					    &dsa_slave_switchdev_blocking_notifier,
					    dp->bridge->tx_fwd_offload, extack);
	if (err)
		goto out_rollback_unbridge;

	err = dsa_port_switchdev_sync_attrs(dp, extack);
	if (err)
		goto out_rollback_unoffload;

	return 0;

out_rollback_unoffload:
	switchdev_bridge_port_unoffload(brport_dev, dp,
					&dsa_slave_switchdev_notifier,
					&dsa_slave_switchdev_blocking_notifier);
	dsa_flush_workqueue();
out_rollback_unbridge:
	dsa_broadcast(DSA_NOTIFIER_BRIDGE_LEAVE, &info);
out_rollback:
	dsa_port_bridge_destroy(dp, br);
	return err;
}

void dsa_port_pre_bridge_leave(struct dsa_port *dp, struct net_device *br)
{
	struct net_device *brport_dev = dsa_port_to_bridge_port(dp);

	/* Don't try to unoffload something that is not offloaded */
	if (!brport_dev)
		return;

	switchdev_bridge_port_unoffload(brport_dev, dp,
					&dsa_slave_switchdev_notifier,
					&dsa_slave_switchdev_blocking_notifier);

	dsa_flush_workqueue();
}

void dsa_port_bridge_leave(struct dsa_port *dp, struct net_device *br)
{
	struct dsa_notifier_bridge_info info = {
		.dp = dp,
	};
	int err;

	/* If the port could not be offloaded to begin with, then
	 * there is nothing to do.
	 */
	if (!dp->bridge)
		return;

	info.bridge = *dp->bridge;

	/* Here the port is already unbridged. Reflect the current configuration
	 * so that drivers can program their chips accordingly.
	 */
	dsa_port_bridge_destroy(dp, br);

	err = dsa_broadcast(DSA_NOTIFIER_BRIDGE_LEAVE, &info);
	if (err)
		dev_err(dp->ds->dev,
			"port %d failed to notify DSA_NOTIFIER_BRIDGE_LEAVE: %pe\n",
			dp->index, ERR_PTR(err));

	dsa_port_switchdev_unsync_attrs(dp, info.bridge);
}

int dsa_port_lag_change(struct dsa_port *dp,
			struct netdev_lag_lower_state_info *linfo)
{
	struct dsa_notifier_lag_info info = {
		.dp = dp,
	};
	bool tx_enabled;

	if (!dp->lag)
		return 0;

	/* On statically configured aggregates (e.g. loadbalance
	 * without LACP) ports will always be tx_enabled, even if the
	 * link is down. Thus we require both link_up and tx_enabled
	 * in order to include it in the tx set.
	 */
	tx_enabled = linfo->link_up && linfo->tx_enabled;

	if (tx_enabled == dp->lag_tx_enabled)
		return 0;

	dp->lag_tx_enabled = tx_enabled;

	return dsa_port_notify(dp, DSA_NOTIFIER_LAG_CHANGE, &info);
}

static int dsa_port_lag_create(struct dsa_port *dp,
			       struct net_device *lag_dev)
{
	struct dsa_switch *ds = dp->ds;
	struct dsa_lag *lag;

	lag = dsa_tree_lag_find(ds->dst, lag_dev);
	if (lag) {
		refcount_inc(&lag->refcount);
		dp->lag = lag;
		return 0;
	}

	lag = kzalloc(sizeof(*lag), GFP_KERNEL);
	if (!lag)
		return -ENOMEM;

	refcount_set(&lag->refcount, 1);
	mutex_init(&lag->fdb_lock);
	INIT_LIST_HEAD(&lag->fdbs);
	lag->dev = lag_dev;
	dsa_lag_map(ds->dst, lag);
	dp->lag = lag;

	return 0;
}

static void dsa_port_lag_destroy(struct dsa_port *dp)
{
	struct dsa_lag *lag = dp->lag;

	dp->lag = NULL;
	dp->lag_tx_enabled = false;

	if (!refcount_dec_and_test(&lag->refcount))
		return;

	WARN_ON(!list_empty(&lag->fdbs));
	dsa_lag_unmap(dp->ds->dst, lag);
	kfree(lag);
}

int dsa_port_lag_join(struct dsa_port *dp, struct net_device *lag_dev,
		      struct netdev_lag_upper_info *uinfo,
		      struct netlink_ext_ack *extack)
{
	struct dsa_notifier_lag_info info = {
		.dp = dp,
		.info = uinfo,
	};
	struct net_device *bridge_dev;
	int err;

	err = dsa_port_lag_create(dp, lag_dev);
	if (err)
		goto err_lag_create;

	info.lag = *dp->lag;
	err = dsa_port_notify(dp, DSA_NOTIFIER_LAG_JOIN, &info);
	if (err)
		goto err_lag_join;

	bridge_dev = netdev_master_upper_dev_get(lag_dev);
	if (!bridge_dev || !netif_is_bridge_master(bridge_dev))
		return 0;

	err = dsa_port_bridge_join(dp, bridge_dev, extack);
	if (err)
		goto err_bridge_join;

	return 0;

err_bridge_join:
	dsa_port_notify(dp, DSA_NOTIFIER_LAG_LEAVE, &info);
err_lag_join:
	dsa_port_lag_destroy(dp);
err_lag_create:
	return err;
}

void dsa_port_pre_lag_leave(struct dsa_port *dp, struct net_device *lag_dev)
{
	struct net_device *br = dsa_port_bridge_dev_get(dp);

	if (br)
		dsa_port_pre_bridge_leave(dp, br);
}

void dsa_port_lag_leave(struct dsa_port *dp, struct net_device *lag_dev)
{
	struct net_device *br = dsa_port_bridge_dev_get(dp);
	struct dsa_notifier_lag_info info = {
		.dp = dp,
	};
	int err;

	if (!dp->lag)
		return;

	/* Port might have been part of a LAG that in turn was
	 * attached to a bridge.
	 */
	if (br)
		dsa_port_bridge_leave(dp, br);

	info.lag = *dp->lag;

	dsa_port_lag_destroy(dp);

	err = dsa_port_notify(dp, DSA_NOTIFIER_LAG_LEAVE, &info);
	if (err)
		dev_err(dp->ds->dev,
			"port %d failed to notify DSA_NOTIFIER_LAG_LEAVE: %pe\n",
			dp->index, ERR_PTR(err));
}

/* Must be called under rcu_read_lock() */
static bool dsa_port_can_apply_vlan_filtering(struct dsa_port *dp,
					      bool vlan_filtering,
					      struct netlink_ext_ack *extack)
{
	struct dsa_switch *ds = dp->ds;
	struct dsa_port *other_dp;
	int err;

	/* VLAN awareness was off, so the question is "can we turn it on".
	 * We may have had 8021q uppers, those need to go. Make sure we don't
	 * enter an inconsistent state: deny changing the VLAN awareness state
	 * as long as we have 8021q uppers.
	 */
	if (vlan_filtering && dsa_port_is_user(dp)) {
		struct net_device *br = dsa_port_bridge_dev_get(dp);
		struct net_device *upper_dev, *slave = dp->slave;
		struct list_head *iter;

		netdev_for_each_upper_dev_rcu(slave, upper_dev, iter) {
			struct bridge_vlan_info br_info;
			u16 vid;

			if (!is_vlan_dev(upper_dev))
				continue;

			vid = vlan_dev_vlan_id(upper_dev);

			/* br_vlan_get_info() returns -EINVAL or -ENOENT if the
			 * device, respectively the VID is not found, returning
			 * 0 means success, which is a failure for us here.
			 */
			err = br_vlan_get_info(br, vid, &br_info);
			if (err == 0) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Must first remove VLAN uppers having VIDs also present in bridge");
				return false;
			}
		}
	}

	if (!ds->vlan_filtering_is_global)
		return true;

	/* For cases where enabling/disabling VLAN awareness is global to the
	 * switch, we need to handle the case where multiple bridges span
	 * different ports of the same switch device and one of them has a
	 * different setting than what is being requested.
	 */
	dsa_switch_for_each_port(other_dp, ds) {
		struct net_device *other_br = dsa_port_bridge_dev_get(other_dp);

		/* If it's the same bridge, it also has same
		 * vlan_filtering setting => no need to check
		 */
		if (!other_br || other_br == dsa_port_bridge_dev_get(dp))
			continue;

		if (br_vlan_enabled(other_br) != vlan_filtering) {
			NL_SET_ERR_MSG_MOD(extack,
					   "VLAN filtering is a global setting");
			return false;
		}
	}
	return true;
}

int dsa_port_vlan_filtering(struct dsa_port *dp, bool vlan_filtering,
			    struct netlink_ext_ack *extack)
{
	bool old_vlan_filtering = dsa_port_is_vlan_filtering(dp);
	struct dsa_switch *ds = dp->ds;
	bool apply;
	int err;

	if (!ds->ops->port_vlan_filtering)
		return -EOPNOTSUPP;

	/* We are called from dsa_slave_switchdev_blocking_event(),
	 * which is not under rcu_read_lock(), unlike
	 * dsa_slave_switchdev_event().
	 */
	rcu_read_lock();
	apply = dsa_port_can_apply_vlan_filtering(dp, vlan_filtering, extack);
	rcu_read_unlock();
	if (!apply)
		return -EINVAL;

	if (dsa_port_is_vlan_filtering(dp) == vlan_filtering)
		return 0;

	err = ds->ops->port_vlan_filtering(ds, dp->index, vlan_filtering,
					   extack);
	if (err)
		return err;

	if (ds->vlan_filtering_is_global) {
		struct dsa_port *other_dp;

		ds->vlan_filtering = vlan_filtering;

		dsa_switch_for_each_user_port(other_dp, ds) {
			struct net_device *slave = other_dp->slave;

			/* We might be called in the unbind path, so not
			 * all slave devices might still be registered.
			 */
			if (!slave)
				continue;

			err = dsa_slave_manage_vlan_filtering(slave,
							      vlan_filtering);
			if (err)
				goto restore;
		}
	} else {
		dp->vlan_filtering = vlan_filtering;

		err = dsa_slave_manage_vlan_filtering(dp->slave,
						      vlan_filtering);
		if (err)
			goto restore;
	}

	return 0;

restore:
	ds->ops->port_vlan_filtering(ds, dp->index, old_vlan_filtering, NULL);

	if (ds->vlan_filtering_is_global)
		ds->vlan_filtering = old_vlan_filtering;
	else
		dp->vlan_filtering = old_vlan_filtering;

	return err;
}

/* This enforces legacy behavior for switch drivers which assume they can't
 * receive VLAN configuration when enslaved to a bridge with vlan_filtering=0
 */
bool dsa_port_skip_vlan_configuration(struct dsa_port *dp)
{
	struct net_device *br = dsa_port_bridge_dev_get(dp);
	struct dsa_switch *ds = dp->ds;

	if (!br)
		return false;

	return !ds->configure_vlan_while_not_filtering && !br_vlan_enabled(br);
}

int dsa_port_ageing_time(struct dsa_port *dp, clock_t ageing_clock)
{
	unsigned long ageing_jiffies = clock_t_to_jiffies(ageing_clock);
	unsigned int ageing_time = jiffies_to_msecs(ageing_jiffies);
	struct dsa_notifier_ageing_time_info info;
	int err;

	info.ageing_time = ageing_time;

	err = dsa_port_notify(dp, DSA_NOTIFIER_AGEING_TIME, &info);
	if (err)
		return err;

	dp->ageing_time = ageing_time;

	return 0;
}

int dsa_port_mst_enable(struct dsa_port *dp, bool on,
			struct netlink_ext_ack *extack)
{
	if (on && !dsa_port_supports_mst(dp)) {
		NL_SET_ERR_MSG_MOD(extack, "Hardware does not support MST");
		return -EINVAL;
	}

	return 0;
}

int dsa_port_pre_bridge_flags(const struct dsa_port *dp,
			      struct switchdev_brport_flags flags,
			      struct netlink_ext_ack *extack)
{
	struct dsa_switch *ds = dp->ds;

	if (!ds->ops->port_pre_bridge_flags)
		return -EINVAL;

	return ds->ops->port_pre_bridge_flags(ds, dp->index, flags, extack);
}

int dsa_port_bridge_flags(struct dsa_port *dp,
			  struct switchdev_brport_flags flags,
			  struct netlink_ext_ack *extack)
{
	struct dsa_switch *ds = dp->ds;
	int err;

	if (!ds->ops->port_bridge_flags)
		return -EOPNOTSUPP;

	err = ds->ops->port_bridge_flags(ds, dp->index, flags, extack);
	if (err)
		return err;

	if (flags.mask & BR_LEARNING) {
		bool learning = flags.val & BR_LEARNING;

		if (learning == dp->learning)
			return 0;

		if ((dp->learning && !learning) &&
		    (dp->stp_state == BR_STATE_LEARNING ||
		     dp->stp_state == BR_STATE_FORWARDING))
			dsa_port_fast_age(dp);

		dp->learning = learning;
	}

	return 0;
}

void dsa_port_set_host_flood(struct dsa_port *dp, bool uc, bool mc)
{
	struct dsa_switch *ds = dp->ds;

	if (ds->ops->port_set_host_flood)
		ds->ops->port_set_host_flood(ds, dp->index, uc, mc);
}

int dsa_port_vlan_msti(struct dsa_port *dp,
		       const struct switchdev_vlan_msti *msti)
{
	struct dsa_switch *ds = dp->ds;

	if (!ds->ops->vlan_msti_set)
		return -EOPNOTSUPP;

	return ds->ops->vlan_msti_set(ds, *dp->bridge, msti);
}

int dsa_port_mtu_change(struct dsa_port *dp, int new_mtu)
{
	struct dsa_notifier_mtu_info info = {
		.dp = dp,
		.mtu = new_mtu,
	};

	return dsa_port_notify(dp, DSA_NOTIFIER_MTU, &info);
}

int dsa_port_fdb_add(struct dsa_port *dp, const unsigned char *addr,
		     u16 vid)
{
	struct dsa_notifier_fdb_info info = {
		.dp = dp,
		.addr = addr,
		.vid = vid,
		.db = {
			.type = DSA_DB_BRIDGE,
			.bridge = *dp->bridge,
		},
	};

	/* Refcounting takes bridge.num as a key, and should be global for all
	 * bridges in the absence of FDB isolation, and per bridge otherwise.
	 * Force the bridge.num to zero here in the absence of FDB isolation.
	 */
	if (!dp->ds->fdb_isolation)
		info.db.bridge.num = 0;

	return dsa_port_notify(dp, DSA_NOTIFIER_FDB_ADD, &info);
}

int dsa_port_fdb_del(struct dsa_port *dp, const unsigned char *addr,
		     u16 vid)
{
	struct dsa_notifier_fdb_info info = {
		.dp = dp,
		.addr = addr,
		.vid = vid,
		.db = {
			.type = DSA_DB_BRIDGE,
			.bridge = *dp->bridge,
		},
	};

	if (!dp->ds->fdb_isolation)
		info.db.bridge.num = 0;

	return dsa_port_notify(dp, DSA_NOTIFIER_FDB_DEL, &info);
}

static int dsa_port_host_fdb_add(struct dsa_port *dp,
				 const unsigned char *addr, u16 vid,
				 struct dsa_db db)
{
	struct dsa_notifier_fdb_info info = {
		.dp = dp,
		.addr = addr,
		.vid = vid,
		.db = db,
	};

	if (!dp->ds->fdb_isolation)
		info.db.bridge.num = 0;

	return dsa_port_notify(dp, DSA_NOTIFIER_HOST_FDB_ADD, &info);
}

int dsa_port_standalone_host_fdb_add(struct dsa_port *dp,
				     const unsigned char *addr, u16 vid)
{
	struct dsa_db db = {
		.type = DSA_DB_PORT,
		.dp = dp,
	};

	return dsa_port_host_fdb_add(dp, addr, vid, db);
}

int dsa_port_bridge_host_fdb_add(struct dsa_port *dp,
				 const unsigned char *addr, u16 vid)
{
	struct dsa_port *cpu_dp = dp->cpu_dp;
	struct dsa_db db = {
		.type = DSA_DB_BRIDGE,
		.bridge = *dp->bridge,
	};
	int err;

	/* Avoid a call to __dev_set_promiscuity() on the master, which
	 * requires rtnl_lock(), since we can't guarantee that is held here,
	 * and we can't take it either.
	 */
	if (cpu_dp->master->priv_flags & IFF_UNICAST_FLT) {
		err = dev_uc_add(cpu_dp->master, addr);
		if (err)
			return err;
	}

	return dsa_port_host_fdb_add(dp, addr, vid, db);
}

static int dsa_port_host_fdb_del(struct dsa_port *dp,
				 const unsigned char *addr, u16 vid,
				 struct dsa_db db)
{
	struct dsa_notifier_fdb_info info = {
		.dp = dp,
		.addr = addr,
		.vid = vid,
		.db = db,
	};

	if (!dp->ds->fdb_isolation)
		info.db.bridge.num = 0;

	return dsa_port_notify(dp, DSA_NOTIFIER_HOST_FDB_DEL, &info);
}

int dsa_port_standalone_host_fdb_del(struct dsa_port *dp,
				     const unsigned char *addr, u16 vid)
{
	struct dsa_db db = {
		.type = DSA_DB_PORT,
		.dp = dp,
	};

	return dsa_port_host_fdb_del(dp, addr, vid, db);
}

int dsa_port_bridge_host_fdb_del(struct dsa_port *dp,
				 const unsigned char *addr, u16 vid)
{
	struct dsa_port *cpu_dp = dp->cpu_dp;
	struct dsa_db db = {
		.type = DSA_DB_BRIDGE,
		.bridge = *dp->bridge,
	};
	int err;

	if (cpu_dp->master->priv_flags & IFF_UNICAST_FLT) {
		err = dev_uc_del(cpu_dp->master, addr);
		if (err)
			return err;
	}

	return dsa_port_host_fdb_del(dp, addr, vid, db);
}

int dsa_port_lag_fdb_add(struct dsa_port *dp, const unsigned char *addr,
			 u16 vid)
{
	struct dsa_notifier_lag_fdb_info info = {
		.lag = dp->lag,
		.addr = addr,
		.vid = vid,
		.db = {
			.type = DSA_DB_BRIDGE,
			.bridge = *dp->bridge,
		},
	};

	if (!dp->ds->fdb_isolation)
		info.db.bridge.num = 0;

	return dsa_port_notify(dp, DSA_NOTIFIER_LAG_FDB_ADD, &info);
}

int dsa_port_lag_fdb_del(struct dsa_port *dp, const unsigned char *addr,
			 u16 vid)
{
	struct dsa_notifier_lag_fdb_info info = {
		.lag = dp->lag,
		.addr = addr,
		.vid = vid,
		.db = {
			.type = DSA_DB_BRIDGE,
			.bridge = *dp->bridge,
		},
	};

	if (!dp->ds->fdb_isolation)
		info.db.bridge.num = 0;

	return dsa_port_notify(dp, DSA_NOTIFIER_LAG_FDB_DEL, &info);
}

int dsa_port_fdb_dump(struct dsa_port *dp, dsa_fdb_dump_cb_t *cb, void *data)
{
	struct dsa_switch *ds = dp->ds;
	int port = dp->index;

	if (!ds->ops->port_fdb_dump)
		return -EOPNOTSUPP;

	return ds->ops->port_fdb_dump(ds, port, cb, data);
}

int dsa_port_mdb_add(const struct dsa_port *dp,
		     const struct switchdev_obj_port_mdb *mdb)
{
	struct dsa_notifier_mdb_info info = {
		.dp = dp,
		.mdb = mdb,
		.db = {
			.type = DSA_DB_BRIDGE,
			.bridge = *dp->bridge,
		},
	};

	if (!dp->ds->fdb_isolation)
		info.db.bridge.num = 0;

	return dsa_port_notify(dp, DSA_NOTIFIER_MDB_ADD, &info);
}

int dsa_port_mdb_del(const struct dsa_port *dp,
		     const struct switchdev_obj_port_mdb *mdb)
{
	struct dsa_notifier_mdb_info info = {
		.dp = dp,
		.mdb = mdb,
		.db = {
			.type = DSA_DB_BRIDGE,
			.bridge = *dp->bridge,
		},
	};

	if (!dp->ds->fdb_isolation)
		info.db.bridge.num = 0;

	return dsa_port_notify(dp, DSA_NOTIFIER_MDB_DEL, &info);
}

static int dsa_port_host_mdb_add(const struct dsa_port *dp,
				 const struct switchdev_obj_port_mdb *mdb,
				 struct dsa_db db)
{
	struct dsa_notifier_mdb_info info = {
		.dp = dp,
		.mdb = mdb,
		.db = db,
	};

	if (!dp->ds->fdb_isolation)
		info.db.bridge.num = 0;

	return dsa_port_notify(dp, DSA_NOTIFIER_HOST_MDB_ADD, &info);
}

int dsa_port_standalone_host_mdb_add(const struct dsa_port *dp,
				     const struct switchdev_obj_port_mdb *mdb)
{
	struct dsa_db db = {
		.type = DSA_DB_PORT,
		.dp = dp,
	};

	return dsa_port_host_mdb_add(dp, mdb, db);
}

int dsa_port_bridge_host_mdb_add(const struct dsa_port *dp,
				 const struct switchdev_obj_port_mdb *mdb)
{
	struct dsa_port *cpu_dp = dp->cpu_dp;
	struct dsa_db db = {
		.type = DSA_DB_BRIDGE,
		.bridge = *dp->bridge,
	};
	int err;

	err = dev_mc_add(cpu_dp->master, mdb->addr);
	if (err)
		return err;

	return dsa_port_host_mdb_add(dp, mdb, db);
}

static int dsa_port_host_mdb_del(const struct dsa_port *dp,
				 const struct switchdev_obj_port_mdb *mdb,
				 struct dsa_db db)
{
	struct dsa_notifier_mdb_info info = {
		.dp = dp,
		.mdb = mdb,
		.db = db,
	};

	if (!dp->ds->fdb_isolation)
		info.db.bridge.num = 0;

	return dsa_port_notify(dp, DSA_NOTIFIER_HOST_MDB_DEL, &info);
}

int dsa_port_standalone_host_mdb_del(const struct dsa_port *dp,
				     const struct switchdev_obj_port_mdb *mdb)
{
	struct dsa_db db = {
		.type = DSA_DB_PORT,
		.dp = dp,
	};

	return dsa_port_host_mdb_del(dp, mdb, db);
}

int dsa_port_bridge_host_mdb_del(const struct dsa_port *dp,
				 const struct switchdev_obj_port_mdb *mdb)
{
	struct dsa_port *cpu_dp = dp->cpu_dp;
	struct dsa_db db = {
		.type = DSA_DB_BRIDGE,
		.bridge = *dp->bridge,
	};
	int err;

	err = dev_mc_del(cpu_dp->master, mdb->addr);
	if (err)
		return err;

	return dsa_port_host_mdb_del(dp, mdb, db);
}

int dsa_port_vlan_add(struct dsa_port *dp,
		      const struct switchdev_obj_port_vlan *vlan,
		      struct netlink_ext_ack *extack)
{
	struct dsa_notifier_vlan_info info = {
		.dp = dp,
		.vlan = vlan,
		.extack = extack,
	};

	return dsa_port_notify(dp, DSA_NOTIFIER_VLAN_ADD, &info);
}

int dsa_port_vlan_del(struct dsa_port *dp,
		      const struct switchdev_obj_port_vlan *vlan)
{
	struct dsa_notifier_vlan_info info = {
		.dp = dp,
		.vlan = vlan,
	};

	return dsa_port_notify(dp, DSA_NOTIFIER_VLAN_DEL, &info);
}

int dsa_port_host_vlan_add(struct dsa_port *dp,
			   const struct switchdev_obj_port_vlan *vlan,
			   struct netlink_ext_ack *extack)
{
	struct dsa_notifier_vlan_info info = {
		.dp = dp,
		.vlan = vlan,
		.extack = extack,
	};
	struct dsa_port *cpu_dp = dp->cpu_dp;
	int err;

	err = dsa_port_notify(dp, DSA_NOTIFIER_HOST_VLAN_ADD, &info);
	if (err && err != -EOPNOTSUPP)
		return err;

	vlan_vid_add(cpu_dp->master, htons(ETH_P_8021Q), vlan->vid);

	return err;
}

int dsa_port_host_vlan_del(struct dsa_port *dp,
			   const struct switchdev_obj_port_vlan *vlan)
{
	struct dsa_notifier_vlan_info info = {
		.dp = dp,
		.vlan = vlan,
	};
	struct dsa_port *cpu_dp = dp->cpu_dp;
	int err;

	err = dsa_port_notify(dp, DSA_NOTIFIER_HOST_VLAN_DEL, &info);
	if (err && err != -EOPNOTSUPP)
		return err;

	vlan_vid_del(cpu_dp->master, htons(ETH_P_8021Q), vlan->vid);

	return err;
}

int dsa_port_mrp_add(const struct dsa_port *dp,
		     const struct switchdev_obj_mrp *mrp)
{
	struct dsa_switch *ds = dp->ds;

	if (!ds->ops->port_mrp_add)
		return -EOPNOTSUPP;

	return ds->ops->port_mrp_add(ds, dp->index, mrp);
}

int dsa_port_mrp_del(const struct dsa_port *dp,
		     const struct switchdev_obj_mrp *mrp)
{
	struct dsa_switch *ds = dp->ds;

	if (!ds->ops->port_mrp_del)
		return -EOPNOTSUPP;

	return ds->ops->port_mrp_del(ds, dp->index, mrp);
}

int dsa_port_mrp_add_ring_role(const struct dsa_port *dp,
			       const struct switchdev_obj_ring_role_mrp *mrp)
{
	struct dsa_switch *ds = dp->ds;

	if (!ds->ops->port_mrp_add_ring_role)
		return -EOPNOTSUPP;

	return ds->ops->port_mrp_add_ring_role(ds, dp->index, mrp);
}

int dsa_port_mrp_del_ring_role(const struct dsa_port *dp,
			       const struct switchdev_obj_ring_role_mrp *mrp)
{
	struct dsa_switch *ds = dp->ds;

	if (!ds->ops->port_mrp_del_ring_role)
		return -EOPNOTSUPP;

	return ds->ops->port_mrp_del_ring_role(ds, dp->index, mrp);
}

void dsa_port_set_tag_protocol(struct dsa_port *cpu_dp,
			       const struct dsa_device_ops *tag_ops)
{
	cpu_dp->rcv = tag_ops->rcv;
	cpu_dp->tag_ops = tag_ops;
}

static struct phy_device *dsa_port_get_phy_device(struct dsa_port *dp)
{
	struct device_node *phy_dn;
	struct phy_device *phydev;

	phy_dn = of_parse_phandle(dp->dn, "phy-handle", 0);
	if (!phy_dn)
		return NULL;

	phydev = of_phy_find_device(phy_dn);
	if (!phydev) {
		of_node_put(phy_dn);
		return ERR_PTR(-EPROBE_DEFER);
	}

	of_node_put(phy_dn);
	return phydev;
}

static void dsa_port_phylink_validate(struct phylink_config *config,
				      unsigned long *supported,
				      struct phylink_link_state *state)
{
	struct dsa_port *dp = container_of(config, struct dsa_port, pl_config);
	struct dsa_switch *ds = dp->ds;

	if (!ds->ops->phylink_validate) {
		if (config->mac_capabilities)
			phylink_generic_validate(config, supported, state);
		return;
	}

	ds->ops->phylink_validate(ds, dp->index, supported, state);
}

static void dsa_port_phylink_mac_pcs_get_state(struct phylink_config *config,
					       struct phylink_link_state *state)
{
	struct dsa_port *dp = container_of(config, struct dsa_port, pl_config);
	struct dsa_switch *ds = dp->ds;
	int err;

	/* Only called for inband modes */
	if (!ds->ops->phylink_mac_link_state) {
		state->link = 0;
		return;
	}

	err = ds->ops->phylink_mac_link_state(ds, dp->index, state);
	if (err < 0) {
		dev_err(ds->dev, "p%d: phylink_mac_link_state() failed: %d\n",
			dp->index, err);
		state->link = 0;
	}
}

static struct phylink_pcs *
dsa_port_phylink_mac_select_pcs(struct phylink_config *config,
				phy_interface_t interface)
{
	struct dsa_port *dp = container_of(config, struct dsa_port, pl_config);
	struct phylink_pcs *pcs = ERR_PTR(-EOPNOTSUPP);
	struct dsa_switch *ds = dp->ds;

	if (ds->ops->phylink_mac_select_pcs)
		pcs = ds->ops->phylink_mac_select_pcs(ds, dp->index, interface);

	return pcs;
}

static void dsa_port_phylink_mac_config(struct phylink_config *config,
					unsigned int mode,
					const struct phylink_link_state *state)
{
	struct dsa_port *dp = container_of(config, struct dsa_port, pl_config);
	struct dsa_switch *ds = dp->ds;

	if (!ds->ops->phylink_mac_config)
		return;

	ds->ops->phylink_mac_config(ds, dp->index, mode, state);
}

static void dsa_port_phylink_mac_an_restart(struct phylink_config *config)
{
	struct dsa_port *dp = container_of(config, struct dsa_port, pl_config);
	struct dsa_switch *ds = dp->ds;

	if (!ds->ops->phylink_mac_an_restart)
		return;

	ds->ops->phylink_mac_an_restart(ds, dp->index);
}

static void dsa_port_phylink_mac_link_down(struct phylink_config *config,
					   unsigned int mode,
					   phy_interface_t interface)
{
	struct dsa_port *dp = container_of(config, struct dsa_port, pl_config);
	struct phy_device *phydev = NULL;
	struct dsa_switch *ds = dp->ds;

	if (dsa_port_is_user(dp))
		phydev = dp->slave->phydev;

	if (!ds->ops->phylink_mac_link_down) {
		if (ds->ops->adjust_link && phydev)
			ds->ops->adjust_link(ds, dp->index, phydev);
		return;
	}

	ds->ops->phylink_mac_link_down(ds, dp->index, mode, interface);
}

static void dsa_port_phylink_mac_link_up(struct phylink_config *config,
					 struct phy_device *phydev,
					 unsigned int mode,
					 phy_interface_t interface,
					 int speed, int duplex,
					 bool tx_pause, bool rx_pause)
{
	struct dsa_port *dp = container_of(config, struct dsa_port, pl_config);
	struct dsa_switch *ds = dp->ds;

	if (!ds->ops->phylink_mac_link_up) {
		if (ds->ops->adjust_link && phydev)
			ds->ops->adjust_link(ds, dp->index, phydev);
		return;
	}

	ds->ops->phylink_mac_link_up(ds, dp->index, mode, interface, phydev,
				     speed, duplex, tx_pause, rx_pause);
}

static const struct phylink_mac_ops dsa_port_phylink_mac_ops = {
	.validate = dsa_port_phylink_validate,
	.mac_select_pcs = dsa_port_phylink_mac_select_pcs,
	.mac_pcs_get_state = dsa_port_phylink_mac_pcs_get_state,
	.mac_config = dsa_port_phylink_mac_config,
	.mac_an_restart = dsa_port_phylink_mac_an_restart,
	.mac_link_down = dsa_port_phylink_mac_link_down,
	.mac_link_up = dsa_port_phylink_mac_link_up,
};

int dsa_port_phylink_create(struct dsa_port *dp)
{
	struct dsa_switch *ds = dp->ds;
	phy_interface_t mode;
	int err;

	err = of_get_phy_mode(dp->dn, &mode);
	if (err)
		mode = PHY_INTERFACE_MODE_NA;

	/* Presence of phylink_mac_link_state or phylink_mac_an_restart is
	 * an indicator of a legacy phylink driver.
	 */
	if (ds->ops->phylink_mac_link_state ||
	    ds->ops->phylink_mac_an_restart)
		dp->pl_config.legacy_pre_march2020 = true;

	if (ds->ops->phylink_get_caps)
		ds->ops->phylink_get_caps(ds, dp->index, &dp->pl_config);

	dp->pl = phylink_create(&dp->pl_config, of_fwnode_handle(dp->dn),
				mode, &dsa_port_phylink_mac_ops);
	if (IS_ERR(dp->pl)) {
		pr_err("error creating PHYLINK: %ld\n", PTR_ERR(dp->pl));
		return PTR_ERR(dp->pl);
	}

	return 0;
}

static int dsa_port_setup_phy_of(struct dsa_port *dp, bool enable)
{
	struct dsa_switch *ds = dp->ds;
	struct phy_device *phydev;
	int port = dp->index;
	int err = 0;

	phydev = dsa_port_get_phy_device(dp);
	if (!phydev)
		return 0;

	if (IS_ERR(phydev))
		return PTR_ERR(phydev);

	if (enable) {
		err = genphy_resume(phydev);
		if (err < 0)
			goto err_put_dev;

		err = genphy_read_status(phydev);
		if (err < 0)
			goto err_put_dev;
	} else {
		err = genphy_suspend(phydev);
		if (err < 0)
			goto err_put_dev;
	}

	if (ds->ops->adjust_link)
		ds->ops->adjust_link(ds, port, phydev);

	dev_dbg(ds->dev, "enabled port's phy: %s", phydev_name(phydev));

err_put_dev:
	put_device(&phydev->mdio.dev);
	return err;
}

static int dsa_port_fixed_link_register_of(struct dsa_port *dp)
{
	struct device_node *dn = dp->dn;
	struct dsa_switch *ds = dp->ds;
	struct phy_device *phydev;
	int port = dp->index;
	phy_interface_t mode;
	int err;

	err = of_phy_register_fixed_link(dn);
	if (err) {
		dev_err(ds->dev,
			"failed to register the fixed PHY of port %d\n",
			port);
		return err;
	}

	phydev = of_phy_find_device(dn);

	err = of_get_phy_mode(dn, &mode);
	if (err)
		mode = PHY_INTERFACE_MODE_NA;
	phydev->interface = mode;

	genphy_read_status(phydev);

	if (ds->ops->adjust_link)
		ds->ops->adjust_link(ds, port, phydev);

	put_device(&phydev->mdio.dev);

	return 0;
}

static int dsa_port_phylink_register(struct dsa_port *dp)
{
	struct dsa_switch *ds = dp->ds;
	struct device_node *port_dn = dp->dn;
	int err;

	dp->pl_config.dev = ds->dev;
	dp->pl_config.type = PHYLINK_DEV;

	err = dsa_port_phylink_create(dp);
	if (err)
		return err;

	err = phylink_of_phy_connect(dp->pl, port_dn, 0);
	if (err && err != -ENODEV) {
		pr_err("could not attach to PHY: %d\n", err);
		goto err_phy_connect;
	}

	return 0;

err_phy_connect:
	phylink_destroy(dp->pl);
	return err;
}

int dsa_port_link_register_of(struct dsa_port *dp)
{
	struct dsa_switch *ds = dp->ds;
	struct device_node *phy_np;
	int port = dp->index;

	if (!ds->ops->adjust_link) {
		phy_np = of_parse_phandle(dp->dn, "phy-handle", 0);
		if (of_phy_is_fixed_link(dp->dn) || phy_np) {
			if (ds->ops->phylink_mac_link_down)
				ds->ops->phylink_mac_link_down(ds, port,
					MLO_AN_FIXED, PHY_INTERFACE_MODE_NA);
			of_node_put(phy_np);
			return dsa_port_phylink_register(dp);
		}
		of_node_put(phy_np);
		return 0;
	}

	dev_warn(ds->dev,
		 "Using legacy PHYLIB callbacks. Please migrate to PHYLINK!\n");

	if (of_phy_is_fixed_link(dp->dn))
		return dsa_port_fixed_link_register_of(dp);
	else
		return dsa_port_setup_phy_of(dp, true);
}

void dsa_port_link_unregister_of(struct dsa_port *dp)
{
	struct dsa_switch *ds = dp->ds;

	if (!ds->ops->adjust_link && dp->pl) {
		rtnl_lock();
		phylink_disconnect_phy(dp->pl);
		rtnl_unlock();
		phylink_destroy(dp->pl);
		dp->pl = NULL;
		return;
	}

	if (of_phy_is_fixed_link(dp->dn))
		of_phy_deregister_fixed_link(dp->dn);
	else
		dsa_port_setup_phy_of(dp, false);
}

int dsa_port_hsr_join(struct dsa_port *dp, struct net_device *hsr)
{
	struct dsa_switch *ds = dp->ds;
	int err;

	if (!ds->ops->port_hsr_join)
		return -EOPNOTSUPP;

	dp->hsr_dev = hsr;

	err = ds->ops->port_hsr_join(ds, dp->index, hsr);
	if (err)
		dp->hsr_dev = NULL;

	return err;
}

void dsa_port_hsr_leave(struct dsa_port *dp, struct net_device *hsr)
{
	struct dsa_switch *ds = dp->ds;
	int err;

	dp->hsr_dev = NULL;

	if (ds->ops->port_hsr_leave) {
		err = ds->ops->port_hsr_leave(ds, dp->index, hsr);
		if (err)
			dev_err(dp->ds->dev,
				"port %d failed to leave HSR %s: %pe\n",
				dp->index, hsr->name, ERR_PTR(err));
	}
}

int dsa_port_tag_8021q_vlan_add(struct dsa_port *dp, u16 vid, bool broadcast)
{
	struct dsa_notifier_tag_8021q_vlan_info info = {
		.dp = dp,
		.vid = vid,
	};

	if (broadcast)
		return dsa_broadcast(DSA_NOTIFIER_TAG_8021Q_VLAN_ADD, &info);

	return dsa_port_notify(dp, DSA_NOTIFIER_TAG_8021Q_VLAN_ADD, &info);
}

void dsa_port_tag_8021q_vlan_del(struct dsa_port *dp, u16 vid, bool broadcast)
{
	struct dsa_notifier_tag_8021q_vlan_info info = {
		.dp = dp,
		.vid = vid,
	};
	int err;

	if (broadcast)
		err = dsa_broadcast(DSA_NOTIFIER_TAG_8021Q_VLAN_DEL, &info);
	else
		err = dsa_port_notify(dp, DSA_NOTIFIER_TAG_8021Q_VLAN_DEL, &info);
	if (err)
		dev_err(dp->ds->dev,
			"port %d failed to notify tag_8021q VLAN %d deletion: %pe\n",
			dp->index, vid, ERR_PTR(err));
}
