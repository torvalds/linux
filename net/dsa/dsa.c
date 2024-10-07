// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DSA topology and switch handling
 *
 * Copyright (c) 2008-2009 Marvell Semiconductor
 * Copyright (c) 2013 Florian Fainelli <florian@openwrt.org>
 * Copyright (c) 2016 Andrew Lunn <andrew@lunn.ch>
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/rtnetlink.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <net/dsa_stubs.h>
#include <net/sch_generic.h>

#include "conduit.h"
#include "devlink.h"
#include "dsa.h"
#include "netlink.h"
#include "port.h"
#include "switch.h"
#include "tag.h"
#include "user.h"

#define DSA_MAX_NUM_OFFLOADING_BRIDGES		BITS_PER_LONG

static DEFINE_MUTEX(dsa2_mutex);
LIST_HEAD(dsa_tree_list);

static struct workqueue_struct *dsa_owq;

/* Track the bridges with forwarding offload enabled */
static unsigned long dsa_fwd_offloading_bridges;

bool dsa_schedule_work(struct work_struct *work)
{
	return queue_work(dsa_owq, work);
}

void dsa_flush_workqueue(void)
{
	flush_workqueue(dsa_owq);
}
EXPORT_SYMBOL_GPL(dsa_flush_workqueue);

/**
 * dsa_lag_map() - Map LAG structure to a linear LAG array
 * @dst: Tree in which to record the mapping.
 * @lag: LAG structure that is to be mapped to the tree's array.
 *
 * dsa_lag_id/dsa_lag_by_id can then be used to translate between the
 * two spaces. The size of the mapping space is determined by the
 * driver by setting ds->num_lag_ids. It is perfectly legal to leave
 * it unset if it is not needed, in which case these functions become
 * no-ops.
 */
void dsa_lag_map(struct dsa_switch_tree *dst, struct dsa_lag *lag)
{
	unsigned int id;

	for (id = 1; id <= dst->lags_len; id++) {
		if (!dsa_lag_by_id(dst, id)) {
			dst->lags[id - 1] = lag;
			lag->id = id;
			return;
		}
	}

	/* No IDs left, which is OK. Some drivers do not need it. The
	 * ones that do, e.g. mv88e6xxx, will discover that dsa_lag_id
	 * returns an error for this device when joining the LAG. The
	 * driver can then return -EOPNOTSUPP back to DSA, which will
	 * fall back to a software LAG.
	 */
}

/**
 * dsa_lag_unmap() - Remove a LAG ID mapping
 * @dst: Tree in which the mapping is recorded.
 * @lag: LAG structure that was mapped.
 *
 * As there may be multiple users of the mapping, it is only removed
 * if there are no other references to it.
 */
void dsa_lag_unmap(struct dsa_switch_tree *dst, struct dsa_lag *lag)
{
	unsigned int id;

	dsa_lags_foreach_id(id, dst) {
		if (dsa_lag_by_id(dst, id) == lag) {
			dst->lags[id - 1] = NULL;
			lag->id = 0;
			break;
		}
	}
}

struct dsa_lag *dsa_tree_lag_find(struct dsa_switch_tree *dst,
				  const struct net_device *lag_dev)
{
	struct dsa_port *dp;

	list_for_each_entry(dp, &dst->ports, list)
		if (dsa_port_lag_dev_get(dp) == lag_dev)
			return dp->lag;

	return NULL;
}

struct dsa_bridge *dsa_tree_bridge_find(struct dsa_switch_tree *dst,
					const struct net_device *br)
{
	struct dsa_port *dp;

	list_for_each_entry(dp, &dst->ports, list)
		if (dsa_port_bridge_dev_get(dp) == br)
			return dp->bridge;

	return NULL;
}

static int dsa_bridge_num_find(const struct net_device *bridge_dev)
{
	struct dsa_switch_tree *dst;

	list_for_each_entry(dst, &dsa_tree_list, list) {
		struct dsa_bridge *bridge;

		bridge = dsa_tree_bridge_find(dst, bridge_dev);
		if (bridge)
			return bridge->num;
	}

	return 0;
}

unsigned int dsa_bridge_num_get(const struct net_device *bridge_dev, int max)
{
	unsigned int bridge_num = dsa_bridge_num_find(bridge_dev);

	/* Switches without FDB isolation support don't get unique
	 * bridge numbering
	 */
	if (!max)
		return 0;

	if (!bridge_num) {
		/* First port that requests FDB isolation or TX forwarding
		 * offload for this bridge
		 */
		bridge_num = find_next_zero_bit(&dsa_fwd_offloading_bridges,
						DSA_MAX_NUM_OFFLOADING_BRIDGES,
						1);
		if (bridge_num >= max)
			return 0;

		set_bit(bridge_num, &dsa_fwd_offloading_bridges);
	}

	return bridge_num;
}

void dsa_bridge_num_put(const struct net_device *bridge_dev,
			unsigned int bridge_num)
{
	/* Since we refcount bridges, we know that when we call this function
	 * it is no longer in use, so we can just go ahead and remove it from
	 * the bit mask.
	 */
	clear_bit(bridge_num, &dsa_fwd_offloading_bridges);
}

struct dsa_switch *dsa_switch_find(int tree_index, int sw_index)
{
	struct dsa_switch_tree *dst;
	struct dsa_port *dp;

	list_for_each_entry(dst, &dsa_tree_list, list) {
		if (dst->index != tree_index)
			continue;

		list_for_each_entry(dp, &dst->ports, list) {
			if (dp->ds->index != sw_index)
				continue;

			return dp->ds;
		}
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(dsa_switch_find);

static struct dsa_switch_tree *dsa_tree_find(int index)
{
	struct dsa_switch_tree *dst;

	list_for_each_entry(dst, &dsa_tree_list, list)
		if (dst->index == index)
			return dst;

	return NULL;
}

static struct dsa_switch_tree *dsa_tree_alloc(int index)
{
	struct dsa_switch_tree *dst;

	dst = kzalloc(sizeof(*dst), GFP_KERNEL);
	if (!dst)
		return NULL;

	dst->index = index;

	INIT_LIST_HEAD(&dst->rtable);

	INIT_LIST_HEAD(&dst->ports);

	INIT_LIST_HEAD(&dst->list);
	list_add_tail(&dst->list, &dsa_tree_list);

	kref_init(&dst->refcount);

	return dst;
}

static void dsa_tree_free(struct dsa_switch_tree *dst)
{
	if (dst->tag_ops)
		dsa_tag_driver_put(dst->tag_ops);
	list_del(&dst->list);
	kfree(dst);
}

static struct dsa_switch_tree *dsa_tree_get(struct dsa_switch_tree *dst)
{
	if (dst)
		kref_get(&dst->refcount);

	return dst;
}

static struct dsa_switch_tree *dsa_tree_touch(int index)
{
	struct dsa_switch_tree *dst;

	dst = dsa_tree_find(index);
	if (dst)
		return dsa_tree_get(dst);
	else
		return dsa_tree_alloc(index);
}

static void dsa_tree_release(struct kref *ref)
{
	struct dsa_switch_tree *dst;

	dst = container_of(ref, struct dsa_switch_tree, refcount);

	dsa_tree_free(dst);
}

static void dsa_tree_put(struct dsa_switch_tree *dst)
{
	if (dst)
		kref_put(&dst->refcount, dsa_tree_release);
}

static struct dsa_port *dsa_tree_find_port_by_node(struct dsa_switch_tree *dst,
						   struct device_node *dn)
{
	struct dsa_port *dp;

	list_for_each_entry(dp, &dst->ports, list)
		if (dp->dn == dn)
			return dp;

	return NULL;
}

static struct dsa_link *dsa_link_touch(struct dsa_port *dp,
				       struct dsa_port *link_dp)
{
	struct dsa_switch *ds = dp->ds;
	struct dsa_switch_tree *dst;
	struct dsa_link *dl;

	dst = ds->dst;

	list_for_each_entry(dl, &dst->rtable, list)
		if (dl->dp == dp && dl->link_dp == link_dp)
			return dl;

	dl = kzalloc(sizeof(*dl), GFP_KERNEL);
	if (!dl)
		return NULL;

	dl->dp = dp;
	dl->link_dp = link_dp;

	INIT_LIST_HEAD(&dl->list);
	list_add_tail(&dl->list, &dst->rtable);

	return dl;
}

static bool dsa_port_setup_routing_table(struct dsa_port *dp)
{
	struct dsa_switch *ds = dp->ds;
	struct dsa_switch_tree *dst = ds->dst;
	struct device_node *dn = dp->dn;
	struct of_phandle_iterator it;
	struct dsa_port *link_dp;
	struct dsa_link *dl;
	int err;

	of_for_each_phandle(&it, err, dn, "link", NULL, 0) {
		link_dp = dsa_tree_find_port_by_node(dst, it.node);
		if (!link_dp) {
			of_node_put(it.node);
			return false;
		}

		dl = dsa_link_touch(dp, link_dp);
		if (!dl) {
			of_node_put(it.node);
			return false;
		}
	}

	return true;
}

static bool dsa_tree_setup_routing_table(struct dsa_switch_tree *dst)
{
	bool complete = true;
	struct dsa_port *dp;

	list_for_each_entry(dp, &dst->ports, list) {
		if (dsa_port_is_dsa(dp)) {
			complete = dsa_port_setup_routing_table(dp);
			if (!complete)
				break;
		}
	}

	return complete;
}

static struct dsa_port *dsa_tree_find_first_cpu(struct dsa_switch_tree *dst)
{
	struct dsa_port *dp;

	list_for_each_entry(dp, &dst->ports, list)
		if (dsa_port_is_cpu(dp))
			return dp;

	return NULL;
}

struct net_device *dsa_tree_find_first_conduit(struct dsa_switch_tree *dst)
{
	struct device_node *ethernet;
	struct net_device *conduit;
	struct dsa_port *cpu_dp;

	cpu_dp = dsa_tree_find_first_cpu(dst);
	ethernet = of_parse_phandle(cpu_dp->dn, "ethernet", 0);
	conduit = of_find_net_device_by_node(ethernet);
	of_node_put(ethernet);

	return conduit;
}

/* Assign the default CPU port (the first one in the tree) to all ports of the
 * fabric which don't already have one as part of their own switch.
 */
static int dsa_tree_setup_default_cpu(struct dsa_switch_tree *dst)
{
	struct dsa_port *cpu_dp, *dp;

	cpu_dp = dsa_tree_find_first_cpu(dst);
	if (!cpu_dp) {
		pr_err("DSA: tree %d has no CPU port\n", dst->index);
		return -EINVAL;
	}

	list_for_each_entry(dp, &dst->ports, list) {
		if (dp->cpu_dp)
			continue;

		if (dsa_port_is_user(dp) || dsa_port_is_dsa(dp))
			dp->cpu_dp = cpu_dp;
	}

	return 0;
}

static struct dsa_port *
dsa_switch_preferred_default_local_cpu_port(struct dsa_switch *ds)
{
	struct dsa_port *cpu_dp;

	if (!ds->ops->preferred_default_local_cpu_port)
		return NULL;

	cpu_dp = ds->ops->preferred_default_local_cpu_port(ds);
	if (!cpu_dp)
		return NULL;

	if (WARN_ON(!dsa_port_is_cpu(cpu_dp) || cpu_dp->ds != ds))
		return NULL;

	return cpu_dp;
}

/* Perform initial assignment of CPU ports to user ports and DSA links in the
 * fabric, giving preference to CPU ports local to each switch. Default to
 * using the first CPU port in the switch tree if the port does not have a CPU
 * port local to this switch.
 */
static int dsa_tree_setup_cpu_ports(struct dsa_switch_tree *dst)
{
	struct dsa_port *preferred_cpu_dp, *cpu_dp, *dp;

	list_for_each_entry(cpu_dp, &dst->ports, list) {
		if (!dsa_port_is_cpu(cpu_dp))
			continue;

		preferred_cpu_dp = dsa_switch_preferred_default_local_cpu_port(cpu_dp->ds);
		if (preferred_cpu_dp && preferred_cpu_dp != cpu_dp)
			continue;

		/* Prefer a local CPU port */
		dsa_switch_for_each_port(dp, cpu_dp->ds) {
			/* Prefer the first local CPU port found */
			if (dp->cpu_dp)
				continue;

			if (dsa_port_is_user(dp) || dsa_port_is_dsa(dp))
				dp->cpu_dp = cpu_dp;
		}
	}

	return dsa_tree_setup_default_cpu(dst);
}

static void dsa_tree_teardown_cpu_ports(struct dsa_switch_tree *dst)
{
	struct dsa_port *dp;

	list_for_each_entry(dp, &dst->ports, list)
		if (dsa_port_is_user(dp) || dsa_port_is_dsa(dp))
			dp->cpu_dp = NULL;
}

static int dsa_port_setup(struct dsa_port *dp)
{
	bool dsa_port_link_registered = false;
	struct dsa_switch *ds = dp->ds;
	bool dsa_port_enabled = false;
	int err = 0;

	if (dp->setup)
		return 0;

	err = dsa_port_devlink_setup(dp);
	if (err)
		return err;

	switch (dp->type) {
	case DSA_PORT_TYPE_UNUSED:
		dsa_port_disable(dp);
		break;
	case DSA_PORT_TYPE_CPU:
		if (dp->dn) {
			err = dsa_shared_port_link_register_of(dp);
			if (err)
				break;
			dsa_port_link_registered = true;
		} else {
			dev_warn(ds->dev,
				 "skipping link registration for CPU port %d\n",
				 dp->index);
		}

		err = dsa_port_enable(dp, NULL);
		if (err)
			break;
		dsa_port_enabled = true;

		break;
	case DSA_PORT_TYPE_DSA:
		if (dp->dn) {
			err = dsa_shared_port_link_register_of(dp);
			if (err)
				break;
			dsa_port_link_registered = true;
		} else {
			dev_warn(ds->dev,
				 "skipping link registration for DSA port %d\n",
				 dp->index);
		}

		err = dsa_port_enable(dp, NULL);
		if (err)
			break;
		dsa_port_enabled = true;

		break;
	case DSA_PORT_TYPE_USER:
		of_get_mac_address(dp->dn, dp->mac);
		err = dsa_user_create(dp);
		break;
	}

	if (err && dsa_port_enabled)
		dsa_port_disable(dp);
	if (err && dsa_port_link_registered)
		dsa_shared_port_link_unregister_of(dp);
	if (err) {
		dsa_port_devlink_teardown(dp);
		return err;
	}

	dp->setup = true;

	return 0;
}

static void dsa_port_teardown(struct dsa_port *dp)
{
	if (!dp->setup)
		return;

	switch (dp->type) {
	case DSA_PORT_TYPE_UNUSED:
		break;
	case DSA_PORT_TYPE_CPU:
		dsa_port_disable(dp);
		if (dp->dn)
			dsa_shared_port_link_unregister_of(dp);
		break;
	case DSA_PORT_TYPE_DSA:
		dsa_port_disable(dp);
		if (dp->dn)
			dsa_shared_port_link_unregister_of(dp);
		break;
	case DSA_PORT_TYPE_USER:
		if (dp->user) {
			dsa_user_destroy(dp->user);
			dp->user = NULL;
		}
		break;
	}

	dsa_port_devlink_teardown(dp);

	dp->setup = false;
}

static int dsa_port_setup_as_unused(struct dsa_port *dp)
{
	dp->type = DSA_PORT_TYPE_UNUSED;
	return dsa_port_setup(dp);
}

static int dsa_switch_setup_tag_protocol(struct dsa_switch *ds)
{
	const struct dsa_device_ops *tag_ops = ds->dst->tag_ops;
	struct dsa_switch_tree *dst = ds->dst;
	int err;

	if (tag_ops->proto == dst->default_proto)
		goto connect;

	rtnl_lock();
	err = ds->ops->change_tag_protocol(ds, tag_ops->proto);
	rtnl_unlock();
	if (err) {
		dev_err(ds->dev, "Unable to use tag protocol \"%s\": %pe\n",
			tag_ops->name, ERR_PTR(err));
		return err;
	}

connect:
	if (tag_ops->connect) {
		err = tag_ops->connect(ds);
		if (err)
			return err;
	}

	if (ds->ops->connect_tag_protocol) {
		err = ds->ops->connect_tag_protocol(ds, tag_ops->proto);
		if (err) {
			dev_err(ds->dev,
				"Unable to connect to tag protocol \"%s\": %pe\n",
				tag_ops->name, ERR_PTR(err));
			goto disconnect;
		}
	}

	return 0;

disconnect:
	if (tag_ops->disconnect)
		tag_ops->disconnect(ds);

	return err;
}

static void dsa_switch_teardown_tag_protocol(struct dsa_switch *ds)
{
	const struct dsa_device_ops *tag_ops = ds->dst->tag_ops;

	if (tag_ops->disconnect)
		tag_ops->disconnect(ds);
}

static int dsa_switch_setup(struct dsa_switch *ds)
{
	int err;

	if (ds->setup)
		return 0;

	/* Initialize ds->phys_mii_mask before registering the user MDIO bus
	 * driver and before ops->setup() has run, since the switch drivers and
	 * the user MDIO bus driver rely on these values for probing PHY
	 * devices or not
	 */
	ds->phys_mii_mask |= dsa_user_ports(ds);

	err = dsa_switch_devlink_alloc(ds);
	if (err)
		return err;

	err = dsa_switch_register_notifier(ds);
	if (err)
		goto devlink_free;

	ds->configure_vlan_while_not_filtering = true;

	err = ds->ops->setup(ds);
	if (err < 0)
		goto unregister_notifier;

	err = dsa_switch_setup_tag_protocol(ds);
	if (err)
		goto teardown;

	if (!ds->user_mii_bus && ds->ops->phy_read) {
		ds->user_mii_bus = mdiobus_alloc();
		if (!ds->user_mii_bus) {
			err = -ENOMEM;
			goto teardown;
		}

		dsa_user_mii_bus_init(ds);

		err = mdiobus_register(ds->user_mii_bus);
		if (err < 0)
			goto free_user_mii_bus;
	}

	dsa_switch_devlink_register(ds);

	ds->setup = true;
	return 0;

free_user_mii_bus:
	if (ds->user_mii_bus && ds->ops->phy_read)
		mdiobus_free(ds->user_mii_bus);
teardown:
	if (ds->ops->teardown)
		ds->ops->teardown(ds);
unregister_notifier:
	dsa_switch_unregister_notifier(ds);
devlink_free:
	dsa_switch_devlink_free(ds);
	return err;
}

static void dsa_switch_teardown(struct dsa_switch *ds)
{
	if (!ds->setup)
		return;

	dsa_switch_devlink_unregister(ds);

	if (ds->user_mii_bus && ds->ops->phy_read) {
		mdiobus_unregister(ds->user_mii_bus);
		mdiobus_free(ds->user_mii_bus);
		ds->user_mii_bus = NULL;
	}

	dsa_switch_teardown_tag_protocol(ds);

	if (ds->ops->teardown)
		ds->ops->teardown(ds);

	dsa_switch_unregister_notifier(ds);

	dsa_switch_devlink_free(ds);

	ds->setup = false;
}

/* First tear down the non-shared, then the shared ports. This ensures that
 * all work items scheduled by our switchdev handlers for user ports have
 * completed before we destroy the refcounting kept on the shared ports.
 */
static void dsa_tree_teardown_ports(struct dsa_switch_tree *dst)
{
	struct dsa_port *dp;

	list_for_each_entry(dp, &dst->ports, list)
		if (dsa_port_is_user(dp) || dsa_port_is_unused(dp))
			dsa_port_teardown(dp);

	dsa_flush_workqueue();

	list_for_each_entry(dp, &dst->ports, list)
		if (dsa_port_is_dsa(dp) || dsa_port_is_cpu(dp))
			dsa_port_teardown(dp);
}

static void dsa_tree_teardown_switches(struct dsa_switch_tree *dst)
{
	struct dsa_port *dp;

	list_for_each_entry(dp, &dst->ports, list)
		dsa_switch_teardown(dp->ds);
}

/* Bring shared ports up first, then non-shared ports */
static int dsa_tree_setup_ports(struct dsa_switch_tree *dst)
{
	struct dsa_port *dp;
	int err = 0;

	list_for_each_entry(dp, &dst->ports, list) {
		if (dsa_port_is_dsa(dp) || dsa_port_is_cpu(dp)) {
			err = dsa_port_setup(dp);
			if (err)
				goto teardown;
		}
	}

	list_for_each_entry(dp, &dst->ports, list) {
		if (dsa_port_is_user(dp) || dsa_port_is_unused(dp)) {
			err = dsa_port_setup(dp);
			if (err) {
				err = dsa_port_setup_as_unused(dp);
				if (err)
					goto teardown;
			}
		}
	}

	return 0;

teardown:
	dsa_tree_teardown_ports(dst);

	return err;
}

static int dsa_tree_setup_switches(struct dsa_switch_tree *dst)
{
	struct dsa_port *dp;
	int err = 0;

	list_for_each_entry(dp, &dst->ports, list) {
		err = dsa_switch_setup(dp->ds);
		if (err) {
			dsa_tree_teardown_switches(dst);
			break;
		}
	}

	return err;
}

static int dsa_tree_setup_conduit(struct dsa_switch_tree *dst)
{
	struct dsa_port *cpu_dp;
	int err = 0;

	rtnl_lock();

	dsa_tree_for_each_cpu_port(cpu_dp, dst) {
		struct net_device *conduit = cpu_dp->conduit;
		bool admin_up = (conduit->flags & IFF_UP) &&
				!qdisc_tx_is_noop(conduit);

		err = dsa_conduit_setup(conduit, cpu_dp);
		if (err)
			break;

		/* Replay conduit state event */
		dsa_tree_conduit_admin_state_change(dst, conduit, admin_up);
		dsa_tree_conduit_oper_state_change(dst, conduit,
						   netif_oper_up(conduit));
	}

	rtnl_unlock();

	return err;
}

static void dsa_tree_teardown_conduit(struct dsa_switch_tree *dst)
{
	struct dsa_port *cpu_dp;

	rtnl_lock();

	dsa_tree_for_each_cpu_port(cpu_dp, dst) {
		struct net_device *conduit = cpu_dp->conduit;

		/* Synthesizing an "admin down" state is sufficient for
		 * the switches to get a notification if the conduit is
		 * currently up and running.
		 */
		dsa_tree_conduit_admin_state_change(dst, conduit, false);

		dsa_conduit_teardown(conduit);
	}

	rtnl_unlock();
}

static int dsa_tree_setup_lags(struct dsa_switch_tree *dst)
{
	unsigned int len = 0;
	struct dsa_port *dp;

	list_for_each_entry(dp, &dst->ports, list) {
		if (dp->ds->num_lag_ids > len)
			len = dp->ds->num_lag_ids;
	}

	if (!len)
		return 0;

	dst->lags = kcalloc(len, sizeof(*dst->lags), GFP_KERNEL);
	if (!dst->lags)
		return -ENOMEM;

	dst->lags_len = len;
	return 0;
}

static void dsa_tree_teardown_lags(struct dsa_switch_tree *dst)
{
	kfree(dst->lags);
}

static int dsa_tree_setup(struct dsa_switch_tree *dst)
{
	bool complete;
	int err;

	if (dst->setup) {
		pr_err("DSA: tree %d already setup! Disjoint trees?\n",
		       dst->index);
		return -EEXIST;
	}

	complete = dsa_tree_setup_routing_table(dst);
	if (!complete)
		return 0;

	err = dsa_tree_setup_cpu_ports(dst);
	if (err)
		return err;

	err = dsa_tree_setup_switches(dst);
	if (err)
		goto teardown_cpu_ports;

	err = dsa_tree_setup_ports(dst);
	if (err)
		goto teardown_switches;

	err = dsa_tree_setup_conduit(dst);
	if (err)
		goto teardown_ports;

	err = dsa_tree_setup_lags(dst);
	if (err)
		goto teardown_conduit;

	dst->setup = true;

	pr_info("DSA: tree %d setup\n", dst->index);

	return 0;

teardown_conduit:
	dsa_tree_teardown_conduit(dst);
teardown_ports:
	dsa_tree_teardown_ports(dst);
teardown_switches:
	dsa_tree_teardown_switches(dst);
teardown_cpu_ports:
	dsa_tree_teardown_cpu_ports(dst);

	return err;
}

static void dsa_tree_teardown(struct dsa_switch_tree *dst)
{
	struct dsa_link *dl, *next;

	if (!dst->setup)
		return;

	dsa_tree_teardown_lags(dst);

	dsa_tree_teardown_conduit(dst);

	dsa_tree_teardown_ports(dst);

	dsa_tree_teardown_switches(dst);

	dsa_tree_teardown_cpu_ports(dst);

	list_for_each_entry_safe(dl, next, &dst->rtable, list) {
		list_del(&dl->list);
		kfree(dl);
	}

	pr_info("DSA: tree %d torn down\n", dst->index);

	dst->setup = false;
}

static int dsa_tree_bind_tag_proto(struct dsa_switch_tree *dst,
				   const struct dsa_device_ops *tag_ops)
{
	const struct dsa_device_ops *old_tag_ops = dst->tag_ops;
	struct dsa_notifier_tag_proto_info info;
	int err;

	dst->tag_ops = tag_ops;

	/* Notify the switches from this tree about the connection
	 * to the new tagger
	 */
	info.tag_ops = tag_ops;
	err = dsa_tree_notify(dst, DSA_NOTIFIER_TAG_PROTO_CONNECT, &info);
	if (err && err != -EOPNOTSUPP)
		goto out_disconnect;

	/* Notify the old tagger about the disconnection from this tree */
	info.tag_ops = old_tag_ops;
	dsa_tree_notify(dst, DSA_NOTIFIER_TAG_PROTO_DISCONNECT, &info);

	return 0;

out_disconnect:
	info.tag_ops = tag_ops;
	dsa_tree_notify(dst, DSA_NOTIFIER_TAG_PROTO_DISCONNECT, &info);
	dst->tag_ops = old_tag_ops;

	return err;
}

/* Since the dsa/tagging sysfs device attribute is per conduit, the assumption
 * is that all DSA switches within a tree share the same tagger, otherwise
 * they would have formed disjoint trees (different "dsa,member" values).
 */
int dsa_tree_change_tag_proto(struct dsa_switch_tree *dst,
			      const struct dsa_device_ops *tag_ops,
			      const struct dsa_device_ops *old_tag_ops)
{
	struct dsa_notifier_tag_proto_info info;
	struct dsa_port *dp;
	int err = -EBUSY;

	if (!rtnl_trylock())
		return restart_syscall();

	/* At the moment we don't allow changing the tag protocol under
	 * traffic. The rtnl_mutex also happens to serialize concurrent
	 * attempts to change the tagging protocol. If we ever lift the IFF_UP
	 * restriction, there needs to be another mutex which serializes this.
	 */
	dsa_tree_for_each_user_port(dp, dst) {
		if (dsa_port_to_conduit(dp)->flags & IFF_UP)
			goto out_unlock;

		if (dp->user->flags & IFF_UP)
			goto out_unlock;
	}

	/* Notify the tag protocol change */
	info.tag_ops = tag_ops;
	err = dsa_tree_notify(dst, DSA_NOTIFIER_TAG_PROTO, &info);
	if (err)
		goto out_unwind_tagger;

	err = dsa_tree_bind_tag_proto(dst, tag_ops);
	if (err)
		goto out_unwind_tagger;

	rtnl_unlock();

	return 0;

out_unwind_tagger:
	info.tag_ops = old_tag_ops;
	dsa_tree_notify(dst, DSA_NOTIFIER_TAG_PROTO, &info);
out_unlock:
	rtnl_unlock();
	return err;
}

static void dsa_tree_conduit_state_change(struct dsa_switch_tree *dst,
					  struct net_device *conduit)
{
	struct dsa_notifier_conduit_state_info info;
	struct dsa_port *cpu_dp = conduit->dsa_ptr;

	info.conduit = conduit;
	info.operational = dsa_port_conduit_is_operational(cpu_dp);

	dsa_tree_notify(dst, DSA_NOTIFIER_CONDUIT_STATE_CHANGE, &info);
}

void dsa_tree_conduit_admin_state_change(struct dsa_switch_tree *dst,
					 struct net_device *conduit,
					 bool up)
{
	struct dsa_port *cpu_dp = conduit->dsa_ptr;
	bool notify = false;

	/* Don't keep track of admin state on LAG DSA conduits,
	 * but rather just of physical DSA conduits
	 */
	if (netif_is_lag_master(conduit))
		return;

	if ((dsa_port_conduit_is_operational(cpu_dp)) !=
	    (up && cpu_dp->conduit_oper_up))
		notify = true;

	cpu_dp->conduit_admin_up = up;

	if (notify)
		dsa_tree_conduit_state_change(dst, conduit);
}

void dsa_tree_conduit_oper_state_change(struct dsa_switch_tree *dst,
					struct net_device *conduit,
					bool up)
{
	struct dsa_port *cpu_dp = conduit->dsa_ptr;
	bool notify = false;

	/* Don't keep track of oper state on LAG DSA conduits,
	 * but rather just of physical DSA conduits
	 */
	if (netif_is_lag_master(conduit))
		return;

	if ((dsa_port_conduit_is_operational(cpu_dp)) !=
	    (cpu_dp->conduit_admin_up && up))
		notify = true;

	cpu_dp->conduit_oper_up = up;

	if (notify)
		dsa_tree_conduit_state_change(dst, conduit);
}

static struct dsa_port *dsa_port_touch(struct dsa_switch *ds, int index)
{
	struct dsa_switch_tree *dst = ds->dst;
	struct dsa_port *dp;

	dsa_switch_for_each_port(dp, ds)
		if (dp->index == index)
			return dp;

	dp = kzalloc(sizeof(*dp), GFP_KERNEL);
	if (!dp)
		return NULL;

	dp->ds = ds;
	dp->index = index;

	mutex_init(&dp->addr_lists_lock);
	mutex_init(&dp->vlans_lock);
	INIT_LIST_HEAD(&dp->fdbs);
	INIT_LIST_HEAD(&dp->mdbs);
	INIT_LIST_HEAD(&dp->vlans); /* also initializes &dp->user_vlans */
	INIT_LIST_HEAD(&dp->list);
	list_add_tail(&dp->list, &dst->ports);

	return dp;
}

static int dsa_port_parse_user(struct dsa_port *dp, const char *name)
{
	dp->type = DSA_PORT_TYPE_USER;
	dp->name = name;

	return 0;
}

static int dsa_port_parse_dsa(struct dsa_port *dp)
{
	dp->type = DSA_PORT_TYPE_DSA;

	return 0;
}

static enum dsa_tag_protocol dsa_get_tag_protocol(struct dsa_port *dp,
						  struct net_device *conduit)
{
	enum dsa_tag_protocol tag_protocol = DSA_TAG_PROTO_NONE;
	struct dsa_switch *mds, *ds = dp->ds;
	unsigned int mdp_upstream;
	struct dsa_port *mdp;

	/* It is possible to stack DSA switches onto one another when that
	 * happens the switch driver may want to know if its tagging protocol
	 * is going to work in such a configuration.
	 */
	if (dsa_user_dev_check(conduit)) {
		mdp = dsa_user_to_port(conduit);
		mds = mdp->ds;
		mdp_upstream = dsa_upstream_port(mds, mdp->index);
		tag_protocol = mds->ops->get_tag_protocol(mds, mdp_upstream,
							  DSA_TAG_PROTO_NONE);
	}

	/* If the conduit device is not itself a DSA user in a disjoint DSA
	 * tree, then return immediately.
	 */
	return ds->ops->get_tag_protocol(ds, dp->index, tag_protocol);
}

static int dsa_port_parse_cpu(struct dsa_port *dp, struct net_device *conduit,
			      const char *user_protocol)
{
	const struct dsa_device_ops *tag_ops = NULL;
	struct dsa_switch *ds = dp->ds;
	struct dsa_switch_tree *dst = ds->dst;
	enum dsa_tag_protocol default_proto;

	/* Find out which protocol the switch would prefer. */
	default_proto = dsa_get_tag_protocol(dp, conduit);
	if (dst->default_proto) {
		if (dst->default_proto != default_proto) {
			dev_err(ds->dev,
				"A DSA switch tree can have only one tagging protocol\n");
			return -EINVAL;
		}
	} else {
		dst->default_proto = default_proto;
	}

	/* See if the user wants to override that preference. */
	if (user_protocol) {
		if (!ds->ops->change_tag_protocol) {
			dev_err(ds->dev, "Tag protocol cannot be modified\n");
			return -EINVAL;
		}

		tag_ops = dsa_tag_driver_get_by_name(user_protocol);
		if (IS_ERR(tag_ops)) {
			dev_warn(ds->dev,
				 "Failed to find a tagging driver for protocol %s, using default\n",
				 user_protocol);
			tag_ops = NULL;
		}
	}

	if (!tag_ops)
		tag_ops = dsa_tag_driver_get_by_id(default_proto);

	if (IS_ERR(tag_ops)) {
		if (PTR_ERR(tag_ops) == -ENOPROTOOPT)
			return -EPROBE_DEFER;

		dev_warn(ds->dev, "No tagger for this switch\n");
		return PTR_ERR(tag_ops);
	}

	if (dst->tag_ops) {
		if (dst->tag_ops != tag_ops) {
			dev_err(ds->dev,
				"A DSA switch tree can have only one tagging protocol\n");

			dsa_tag_driver_put(tag_ops);
			return -EINVAL;
		}

		/* In the case of multiple CPU ports per switch, the tagging
		 * protocol is still reference-counted only per switch tree.
		 */
		dsa_tag_driver_put(tag_ops);
	} else {
		dst->tag_ops = tag_ops;
	}

	dp->conduit = conduit;
	dp->type = DSA_PORT_TYPE_CPU;
	dsa_port_set_tag_protocol(dp, dst->tag_ops);
	dp->dst = dst;

	/* At this point, the tree may be configured to use a different
	 * tagger than the one chosen by the switch driver during
	 * .setup, in the case when a user selects a custom protocol
	 * through the DT.
	 *
	 * This is resolved by syncing the driver with the tree in
	 * dsa_switch_setup_tag_protocol once .setup has run and the
	 * driver is ready to accept calls to .change_tag_protocol. If
	 * the driver does not support the custom protocol at that
	 * point, the tree is wholly rejected, thereby ensuring that the
	 * tree and driver are always in agreement on the protocol to
	 * use.
	 */
	return 0;
}

static int dsa_port_parse_of(struct dsa_port *dp, struct device_node *dn)
{
	struct device_node *ethernet = of_parse_phandle(dn, "ethernet", 0);
	const char *name = of_get_property(dn, "label", NULL);
	bool link = of_property_read_bool(dn, "link");

	dp->dn = dn;

	if (ethernet) {
		struct net_device *conduit;
		const char *user_protocol;

		conduit = of_find_net_device_by_node(ethernet);
		of_node_put(ethernet);
		if (!conduit)
			return -EPROBE_DEFER;

		user_protocol = of_get_property(dn, "dsa-tag-protocol", NULL);
		return dsa_port_parse_cpu(dp, conduit, user_protocol);
	}

	if (link)
		return dsa_port_parse_dsa(dp);

	return dsa_port_parse_user(dp, name);
}

static int dsa_switch_parse_ports_of(struct dsa_switch *ds,
				     struct device_node *dn)
{
	struct device_node *ports, *port;
	struct dsa_port *dp;
	int err = 0;
	u32 reg;

	ports = of_get_child_by_name(dn, "ports");
	if (!ports) {
		/* The second possibility is "ethernet-ports" */
		ports = of_get_child_by_name(dn, "ethernet-ports");
		if (!ports) {
			dev_err(ds->dev, "no ports child node found\n");
			return -EINVAL;
		}
	}

	for_each_available_child_of_node(ports, port) {
		err = of_property_read_u32(port, "reg", &reg);
		if (err) {
			of_node_put(port);
			goto out_put_node;
		}

		if (reg >= ds->num_ports) {
			dev_err(ds->dev, "port %pOF index %u exceeds num_ports (%u)\n",
				port, reg, ds->num_ports);
			of_node_put(port);
			err = -EINVAL;
			goto out_put_node;
		}

		dp = dsa_to_port(ds, reg);

		err = dsa_port_parse_of(dp, port);
		if (err) {
			of_node_put(port);
			goto out_put_node;
		}
	}

out_put_node:
	of_node_put(ports);
	return err;
}

static int dsa_switch_parse_member_of(struct dsa_switch *ds,
				      struct device_node *dn)
{
	u32 m[2] = { 0, 0 };
	int sz;

	/* Don't error out if this optional property isn't found */
	sz = of_property_read_variable_u32_array(dn, "dsa,member", m, 2, 2);
	if (sz < 0 && sz != -EINVAL)
		return sz;

	ds->index = m[1];

	ds->dst = dsa_tree_touch(m[0]);
	if (!ds->dst)
		return -ENOMEM;

	if (dsa_switch_find(ds->dst->index, ds->index)) {
		dev_err(ds->dev,
			"A DSA switch with index %d already exists in tree %d\n",
			ds->index, ds->dst->index);
		return -EEXIST;
	}

	if (ds->dst->last_switch < ds->index)
		ds->dst->last_switch = ds->index;

	return 0;
}

static int dsa_switch_touch_ports(struct dsa_switch *ds)
{
	struct dsa_port *dp;
	int port;

	for (port = 0; port < ds->num_ports; port++) {
		dp = dsa_port_touch(ds, port);
		if (!dp)
			return -ENOMEM;
	}

	return 0;
}

static int dsa_switch_parse_of(struct dsa_switch *ds, struct device_node *dn)
{
	int err;

	err = dsa_switch_parse_member_of(ds, dn);
	if (err)
		return err;

	err = dsa_switch_touch_ports(ds);
	if (err)
		return err;

	return dsa_switch_parse_ports_of(ds, dn);
}

static int dev_is_class(struct device *dev, void *class)
{
	if (dev->class != NULL && !strcmp(dev->class->name, class))
		return 1;

	return 0;
}

static struct device *dev_find_class(struct device *parent, char *class)
{
	if (dev_is_class(parent, class)) {
		get_device(parent);
		return parent;
	}

	return device_find_child(parent, class, dev_is_class);
}

static struct net_device *dsa_dev_to_net_device(struct device *dev)
{
	struct device *d;

	d = dev_find_class(dev, "net");
	if (d != NULL) {
		struct net_device *nd;

		nd = to_net_dev(d);
		dev_hold(nd);
		put_device(d);

		return nd;
	}

	return NULL;
}

static int dsa_port_parse(struct dsa_port *dp, const char *name,
			  struct device *dev)
{
	if (!strcmp(name, "cpu")) {
		struct net_device *conduit;

		conduit = dsa_dev_to_net_device(dev);
		if (!conduit)
			return -EPROBE_DEFER;

		dev_put(conduit);

		return dsa_port_parse_cpu(dp, conduit, NULL);
	}

	if (!strcmp(name, "dsa"))
		return dsa_port_parse_dsa(dp);

	return dsa_port_parse_user(dp, name);
}

static int dsa_switch_parse_ports(struct dsa_switch *ds,
				  struct dsa_chip_data *cd)
{
	bool valid_name_found = false;
	struct dsa_port *dp;
	struct device *dev;
	const char *name;
	unsigned int i;
	int err;

	for (i = 0; i < DSA_MAX_PORTS; i++) {
		name = cd->port_names[i];
		dev = cd->netdev[i];
		dp = dsa_to_port(ds, i);

		if (!name)
			continue;

		err = dsa_port_parse(dp, name, dev);
		if (err)
			return err;

		valid_name_found = true;
	}

	if (!valid_name_found && i == DSA_MAX_PORTS)
		return -EINVAL;

	return 0;
}

static int dsa_switch_parse(struct dsa_switch *ds, struct dsa_chip_data *cd)
{
	int err;

	ds->cd = cd;

	/* We don't support interconnected switches nor multiple trees via
	 * platform data, so this is the unique switch of the tree.
	 */
	ds->index = 0;
	ds->dst = dsa_tree_touch(0);
	if (!ds->dst)
		return -ENOMEM;

	err = dsa_switch_touch_ports(ds);
	if (err)
		return err;

	return dsa_switch_parse_ports(ds, cd);
}

static void dsa_switch_release_ports(struct dsa_switch *ds)
{
	struct dsa_port *dp, *next;

	dsa_switch_for_each_port_safe(dp, next, ds) {
		WARN_ON(!list_empty(&dp->fdbs));
		WARN_ON(!list_empty(&dp->mdbs));
		WARN_ON(!list_empty(&dp->vlans));
		list_del(&dp->list);
		kfree(dp);
	}
}

static int dsa_switch_probe(struct dsa_switch *ds)
{
	struct dsa_switch_tree *dst;
	struct dsa_chip_data *pdata;
	struct device_node *np;
	int err;

	if (!ds->dev)
		return -ENODEV;

	pdata = ds->dev->platform_data;
	np = ds->dev->of_node;

	if (!ds->num_ports)
		return -EINVAL;

	if (ds->phylink_mac_ops) {
		if (ds->ops->phylink_mac_select_pcs ||
		    ds->ops->phylink_mac_config ||
		    ds->ops->phylink_mac_link_down ||
		    ds->ops->phylink_mac_link_up)
			return -EINVAL;
	}

	if (np) {
		err = dsa_switch_parse_of(ds, np);
		if (err)
			dsa_switch_release_ports(ds);
	} else if (pdata) {
		err = dsa_switch_parse(ds, pdata);
		if (err)
			dsa_switch_release_ports(ds);
	} else {
		err = -ENODEV;
	}

	if (err)
		return err;

	dst = ds->dst;
	dsa_tree_get(dst);
	err = dsa_tree_setup(dst);
	if (err) {
		dsa_switch_release_ports(ds);
		dsa_tree_put(dst);
	}

	return err;
}

int dsa_register_switch(struct dsa_switch *ds)
{
	int err;

	mutex_lock(&dsa2_mutex);
	err = dsa_switch_probe(ds);
	dsa_tree_put(ds->dst);
	mutex_unlock(&dsa2_mutex);

	return err;
}
EXPORT_SYMBOL_GPL(dsa_register_switch);

static void dsa_switch_remove(struct dsa_switch *ds)
{
	struct dsa_switch_tree *dst = ds->dst;

	dsa_tree_teardown(dst);
	dsa_switch_release_ports(ds);
	dsa_tree_put(dst);
}

void dsa_unregister_switch(struct dsa_switch *ds)
{
	mutex_lock(&dsa2_mutex);
	dsa_switch_remove(ds);
	mutex_unlock(&dsa2_mutex);
}
EXPORT_SYMBOL_GPL(dsa_unregister_switch);

/* If the DSA conduit chooses to unregister its net_device on .shutdown, DSA is
 * blocking that operation from completion, due to the dev_hold taken inside
 * netdev_upper_dev_link. Unlink the DSA user interfaces from being uppers of
 * the DSA conduit, so that the system can reboot successfully.
 */
void dsa_switch_shutdown(struct dsa_switch *ds)
{
	struct net_device *conduit, *user_dev;
	LIST_HEAD(close_list);
	struct dsa_port *dp;

	mutex_lock(&dsa2_mutex);

	if (!ds->setup)
		goto out;

	rtnl_lock();

	dsa_switch_for_each_cpu_port(dp, ds)
		list_add(&dp->conduit->close_list, &close_list);

	dev_close_many(&close_list, true);

	dsa_switch_for_each_user_port(dp, ds) {
		conduit = dsa_port_to_conduit(dp);
		user_dev = dp->user;

		netif_device_detach(user_dev);
		netdev_upper_dev_unlink(conduit, user_dev);
	}

	/* Disconnect from further netdevice notifiers on the conduit,
	 * since netdev_uses_dsa() will now return false.
	 */
	dsa_switch_for_each_cpu_port(dp, ds)
		dp->conduit->dsa_ptr = NULL;

	rtnl_unlock();
out:
	mutex_unlock(&dsa2_mutex);
}
EXPORT_SYMBOL_GPL(dsa_switch_shutdown);

#ifdef CONFIG_PM_SLEEP
static bool dsa_port_is_initialized(const struct dsa_port *dp)
{
	return dp->type == DSA_PORT_TYPE_USER && dp->user;
}

int dsa_switch_suspend(struct dsa_switch *ds)
{
	struct dsa_port *dp;
	int ret = 0;

	/* Suspend user network devices */
	dsa_switch_for_each_port(dp, ds) {
		if (!dsa_port_is_initialized(dp))
			continue;

		ret = dsa_user_suspend(dp->user);
		if (ret)
			return ret;
	}

	if (ds->ops->suspend)
		ret = ds->ops->suspend(ds);

	return ret;
}
EXPORT_SYMBOL_GPL(dsa_switch_suspend);

int dsa_switch_resume(struct dsa_switch *ds)
{
	struct dsa_port *dp;
	int ret = 0;

	if (ds->ops->resume)
		ret = ds->ops->resume(ds);

	if (ret)
		return ret;

	/* Resume user network devices */
	dsa_switch_for_each_port(dp, ds) {
		if (!dsa_port_is_initialized(dp))
			continue;

		ret = dsa_user_resume(dp->user);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(dsa_switch_resume);
#endif

struct dsa_port *dsa_port_from_netdev(struct net_device *netdev)
{
	if (!netdev || !dsa_user_dev_check(netdev))
		return ERR_PTR(-ENODEV);

	return dsa_user_to_port(netdev);
}
EXPORT_SYMBOL_GPL(dsa_port_from_netdev);

bool dsa_db_equal(const struct dsa_db *a, const struct dsa_db *b)
{
	if (a->type != b->type)
		return false;

	switch (a->type) {
	case DSA_DB_PORT:
		return a->dp == b->dp;
	case DSA_DB_LAG:
		return a->lag.dev == b->lag.dev;
	case DSA_DB_BRIDGE:
		return a->bridge.num == b->bridge.num;
	default:
		WARN_ON(1);
		return false;
	}
}

bool dsa_fdb_present_in_other_db(struct dsa_switch *ds, int port,
				 const unsigned char *addr, u16 vid,
				 struct dsa_db db)
{
	struct dsa_port *dp = dsa_to_port(ds, port);
	struct dsa_mac_addr *a;

	lockdep_assert_held(&dp->addr_lists_lock);

	list_for_each_entry(a, &dp->fdbs, list) {
		if (!ether_addr_equal(a->addr, addr) || a->vid != vid)
			continue;

		if (a->db.type == db.type && !dsa_db_equal(&a->db, &db))
			return true;
	}

	return false;
}
EXPORT_SYMBOL_GPL(dsa_fdb_present_in_other_db);

bool dsa_mdb_present_in_other_db(struct dsa_switch *ds, int port,
				 const struct switchdev_obj_port_mdb *mdb,
				 struct dsa_db db)
{
	struct dsa_port *dp = dsa_to_port(ds, port);
	struct dsa_mac_addr *a;

	lockdep_assert_held(&dp->addr_lists_lock);

	list_for_each_entry(a, &dp->mdbs, list) {
		if (!ether_addr_equal(a->addr, mdb->addr) || a->vid != mdb->vid)
			continue;

		if (a->db.type == db.type && !dsa_db_equal(&a->db, &db))
			return true;
	}

	return false;
}
EXPORT_SYMBOL_GPL(dsa_mdb_present_in_other_db);

static const struct dsa_stubs __dsa_stubs = {
	.conduit_hwtstamp_validate = __dsa_conduit_hwtstamp_validate,
};

static void dsa_register_stubs(void)
{
	dsa_stubs = &__dsa_stubs;
}

static void dsa_unregister_stubs(void)
{
	dsa_stubs = NULL;
}

static int __init dsa_init_module(void)
{
	int rc;

	dsa_owq = alloc_ordered_workqueue("dsa_ordered",
					  WQ_MEM_RECLAIM);
	if (!dsa_owq)
		return -ENOMEM;

	rc = dsa_user_register_notifier();
	if (rc)
		goto register_notifier_fail;

	dev_add_pack(&dsa_pack_type);

	rc = rtnl_link_register(&dsa_link_ops);
	if (rc)
		goto netlink_register_fail;

	dsa_register_stubs();

	return 0;

netlink_register_fail:
	dsa_user_unregister_notifier();
	dev_remove_pack(&dsa_pack_type);
register_notifier_fail:
	destroy_workqueue(dsa_owq);

	return rc;
}
module_init(dsa_init_module);

static void __exit dsa_cleanup_module(void)
{
	dsa_unregister_stubs();

	rtnl_link_unregister(&dsa_link_ops);

	dsa_user_unregister_notifier();
	dev_remove_pack(&dsa_pack_type);
	destroy_workqueue(dsa_owq);
}
module_exit(dsa_cleanup_module);

MODULE_AUTHOR("Lennert Buytenhek <buytenh@wantstofly.org>");
MODULE_DESCRIPTION("Driver for Distributed Switch Architecture switch chips");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:dsa");
