/*
 * net/dsa/dsa2.c - Hardware switch handling, binding version 2
 * Copyright (c) 2008-2009 Marvell Semiconductor
 * Copyright (c) 2013 Florian Fainelli <florian@openwrt.org>
 * Copyright (c) 2016 Andrew Lunn <andrew@lunn.ch>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/rtnetlink.h>
#include <linux/of.h>
#include <linux/of_net.h>

#include "dsa_priv.h"

static LIST_HEAD(dsa_tree_list);
static DEFINE_MUTEX(dsa2_mutex);

static const struct devlink_ops dsa_devlink_ops = {
};

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

	INIT_LIST_HEAD(&dst->list);
	list_add_tail(&dsa_tree_list, &dst->list);

	/* Initialize the reference counter to the number of switches, not 1 */
	kref_init(&dst->refcount);
	refcount_set(&dst->refcount.refcount, 0);

	return dst;
}

static void dsa_tree_free(struct dsa_switch_tree *dst)
{
	list_del(&dst->list);
	kfree(dst);
}

static struct dsa_switch_tree *dsa_tree_touch(int index)
{
	struct dsa_switch_tree *dst;

	dst = dsa_tree_find(index);
	if (!dst)
		dst = dsa_tree_alloc(index);

	return dst;
}

static void dsa_tree_get(struct dsa_switch_tree *dst)
{
	kref_get(&dst->refcount);
}

static void dsa_tree_release(struct kref *ref)
{
	struct dsa_switch_tree *dst;

	dst = container_of(ref, struct dsa_switch_tree, refcount);

	dsa_tree_free(dst);
}

static void dsa_tree_put(struct dsa_switch_tree *dst)
{
	kref_put(&dst->refcount, dsa_tree_release);
}

/* For platform data configurations, we need to have a valid name argument to
 * differentiate a disabled port from an enabled one
 */
static bool dsa_port_is_valid(struct dsa_port *port)
{
	return port->type != DSA_PORT_TYPE_UNUSED;
}

static bool dsa_port_is_dsa(struct dsa_port *port)
{
	return port->type == DSA_PORT_TYPE_DSA;
}

static bool dsa_port_is_cpu(struct dsa_port *port)
{
	return port->type == DSA_PORT_TYPE_CPU;
}

static bool dsa_ds_find_port_dn(struct dsa_switch *ds,
				struct device_node *port)
{
	u32 index;

	for (index = 0; index < ds->num_ports; index++)
		if (ds->ports[index].dn == port)
			return true;
	return false;
}

static struct dsa_switch *dsa_dst_find_port_dn(struct dsa_switch_tree *dst,
					       struct device_node *port)
{
	struct dsa_switch *ds;
	u32 index;

	for (index = 0; index < DSA_MAX_SWITCHES; index++) {
		ds = dst->ds[index];
		if (!ds)
			continue;

		if (dsa_ds_find_port_dn(ds, port))
			return ds;
	}

	return NULL;
}

static int dsa_port_complete(struct dsa_switch_tree *dst,
			     struct dsa_switch *src_ds,
			     struct dsa_port *port,
			     u32 src_port)
{
	struct device_node *link;
	int index;
	struct dsa_switch *dst_ds;

	for (index = 0;; index++) {
		link = of_parse_phandle(port->dn, "link", index);
		if (!link)
			break;

		dst_ds = dsa_dst_find_port_dn(dst, link);
		of_node_put(link);

		if (!dst_ds)
			return 1;

		src_ds->rtable[dst_ds->index] = src_port;
	}

	return 0;
}

/* A switch is complete if all the DSA ports phandles point to ports
 * known in the tree. A return value of 1 means the tree is not
 * complete. This is not an error condition. A value of 0 is
 * success.
 */
static int dsa_ds_complete(struct dsa_switch_tree *dst, struct dsa_switch *ds)
{
	struct dsa_port *port;
	u32 index;
	int err;

	for (index = 0; index < ds->num_ports; index++) {
		port = &ds->ports[index];
		if (!dsa_port_is_valid(port))
			continue;

		if (!dsa_port_is_dsa(port))
			continue;

		err = dsa_port_complete(dst, ds, port, index);
		if (err != 0)
			return err;
	}

	return 0;
}

/* A tree is complete if all the DSA ports phandles point to ports
 * known in the tree. A return value of 1 means the tree is not
 * complete. This is not an error condition. A value of 0 is
 * success.
 */
static int dsa_dst_complete(struct dsa_switch_tree *dst)
{
	struct dsa_switch *ds;
	u32 index;
	int err;

	for (index = 0; index < DSA_MAX_SWITCHES; index++) {
		ds = dst->ds[index];
		if (!ds)
			continue;

		err = dsa_ds_complete(dst, ds);
		if (err != 0)
			return err;
	}

	return 0;
}

static int dsa_dsa_port_apply(struct dsa_port *port)
{
	struct dsa_switch *ds = port->ds;
	int err;

	err = dsa_port_fixed_link_register_of(port);
	if (err) {
		dev_warn(ds->dev, "Failed to setup dsa port %d: %d\n",
			 port->index, err);
		return err;
	}

	memset(&port->devlink_port, 0, sizeof(port->devlink_port));

	return devlink_port_register(ds->devlink, &port->devlink_port,
				     port->index);
}

static void dsa_dsa_port_unapply(struct dsa_port *port)
{
	devlink_port_unregister(&port->devlink_port);
	dsa_port_fixed_link_unregister_of(port);
}

static int dsa_cpu_port_apply(struct dsa_port *port)
{
	struct dsa_switch *ds = port->ds;
	int err;

	err = dsa_port_fixed_link_register_of(port);
	if (err) {
		dev_warn(ds->dev, "Failed to setup cpu port %d: %d\n",
			 port->index, err);
		return err;
	}

	memset(&port->devlink_port, 0, sizeof(port->devlink_port));
	err = devlink_port_register(ds->devlink, &port->devlink_port,
				    port->index);
	return err;
}

static void dsa_cpu_port_unapply(struct dsa_port *port)
{
	devlink_port_unregister(&port->devlink_port);
	dsa_port_fixed_link_unregister_of(port);
}

static int dsa_user_port_apply(struct dsa_port *port)
{
	struct dsa_switch *ds = port->ds;
	int err;

	err = dsa_slave_create(port);
	if (err) {
		dev_warn(ds->dev, "Failed to create slave %d: %d\n",
			 port->index, err);
		port->slave = NULL;
		return err;
	}

	memset(&port->devlink_port, 0, sizeof(port->devlink_port));
	err = devlink_port_register(ds->devlink, &port->devlink_port,
				    port->index);
	if (err)
		return err;

	devlink_port_type_eth_set(&port->devlink_port, port->slave);

	return 0;
}

static void dsa_user_port_unapply(struct dsa_port *port)
{
	devlink_port_unregister(&port->devlink_port);
	if (port->slave) {
		dsa_slave_destroy(port->slave);
		port->slave = NULL;
	}
}

static int dsa_ds_apply(struct dsa_switch_tree *dst, struct dsa_switch *ds)
{
	struct dsa_port *port;
	u32 index;
	int err;

	/* Initialize ds->phys_mii_mask before registering the slave MDIO bus
	 * driver and before ops->setup() has run, since the switch drivers and
	 * the slave MDIO bus driver rely on these values for probing PHY
	 * devices or not
	 */
	ds->phys_mii_mask |= dsa_user_ports(ds);

	/* Add the switch to devlink before calling setup, so that setup can
	 * add dpipe tables
	 */
	ds->devlink = devlink_alloc(&dsa_devlink_ops, 0);
	if (!ds->devlink)
		return -ENOMEM;

	err = devlink_register(ds->devlink, ds->dev);
	if (err)
		return err;

	err = ds->ops->setup(ds);
	if (err < 0)
		return err;

	err = dsa_switch_register_notifier(ds);
	if (err)
		return err;

	if (!ds->slave_mii_bus && ds->ops->phy_read) {
		ds->slave_mii_bus = devm_mdiobus_alloc(ds->dev);
		if (!ds->slave_mii_bus)
			return -ENOMEM;

		dsa_slave_mii_bus_init(ds);

		err = mdiobus_register(ds->slave_mii_bus);
		if (err < 0)
			return err;
	}

	for (index = 0; index < ds->num_ports; index++) {
		port = &ds->ports[index];
		if (!dsa_port_is_valid(port))
			continue;

		if (dsa_port_is_dsa(port)) {
			err = dsa_dsa_port_apply(port);
			if (err)
				return err;
			continue;
		}

		if (dsa_port_is_cpu(port)) {
			err = dsa_cpu_port_apply(port);
			if (err)
				return err;
			continue;
		}

		err = dsa_user_port_apply(port);
		if (err)
			continue;
	}

	return 0;
}

static void dsa_ds_unapply(struct dsa_switch_tree *dst, struct dsa_switch *ds)
{
	struct dsa_port *port;
	u32 index;

	for (index = 0; index < ds->num_ports; index++) {
		port = &ds->ports[index];
		if (!dsa_port_is_valid(port))
			continue;

		if (dsa_port_is_dsa(port)) {
			dsa_dsa_port_unapply(port);
			continue;
		}

		if (dsa_port_is_cpu(port)) {
			dsa_cpu_port_unapply(port);
			continue;
		}

		dsa_user_port_unapply(port);
	}

	if (ds->slave_mii_bus && ds->ops->phy_read)
		mdiobus_unregister(ds->slave_mii_bus);

	dsa_switch_unregister_notifier(ds);

	if (ds->devlink) {
		devlink_unregister(ds->devlink);
		devlink_free(ds->devlink);
		ds->devlink = NULL;
	}

}

static int dsa_dst_apply(struct dsa_switch_tree *dst)
{
	struct dsa_switch *ds;
	u32 index;
	int err;

	for (index = 0; index < DSA_MAX_SWITCHES; index++) {
		ds = dst->ds[index];
		if (!ds)
			continue;

		err = dsa_ds_apply(dst, ds);
		if (err)
			return err;
	}

	/* If we use a tagging format that doesn't have an ethertype
	 * field, make sure that all packets from this point on get
	 * sent to the tag format's receive function.
	 */
	wmb();
	dst->cpu_dp->master->dsa_ptr = dst->cpu_dp;

	err = dsa_master_ethtool_setup(dst->cpu_dp->master);
	if (err)
		return err;

	dst->applied = true;

	return 0;
}

static void dsa_dst_unapply(struct dsa_switch_tree *dst)
{
	struct dsa_switch *ds;
	u32 index;

	if (!dst->applied)
		return;

	dsa_master_ethtool_restore(dst->cpu_dp->master);

	dst->cpu_dp->master->dsa_ptr = NULL;

	/* If we used a tagging format that doesn't have an ethertype
	 * field, make sure that all packets from this point get sent
	 * without the tag and go through the regular receive path.
	 */
	wmb();

	for (index = 0; index < DSA_MAX_SWITCHES; index++) {
		ds = dst->ds[index];
		if (!ds)
			continue;

		dsa_ds_unapply(dst, ds);
	}

	dst->cpu_dp = NULL;

	pr_info("DSA: tree %d unapplied\n", dst->index);
	dst->applied = false;
}

static void dsa_tree_remove_switch(struct dsa_switch_tree *dst,
				   unsigned int index)
{
	dst->ds[index] = NULL;
	dsa_tree_put(dst);
}

static int dsa_tree_add_switch(struct dsa_switch_tree *dst,
			       struct dsa_switch *ds)
{
	unsigned int index = ds->index;

	if (dst->ds[index])
		return -EBUSY;

	dsa_tree_get(dst);
	dst->ds[index] = ds;

	return 0;
}

static int dsa_port_parse_user(struct dsa_port *dp, const char *name)
{
	if (!name)
		name = "eth%d";

	dp->type = DSA_PORT_TYPE_USER;
	dp->name = name;

	return 0;
}

static int dsa_port_parse_dsa(struct dsa_port *dp)
{
	dp->type = DSA_PORT_TYPE_DSA;

	return 0;
}

static int dsa_port_parse_cpu(struct dsa_port *dp, struct net_device *master)
{
	struct dsa_switch *ds = dp->ds;
	struct dsa_switch_tree *dst = ds->dst;
	const struct dsa_device_ops *tag_ops;
	enum dsa_tag_protocol tag_protocol;

	tag_protocol = ds->ops->get_tag_protocol(ds);
	tag_ops = dsa_resolve_tag_protocol(tag_protocol);
	if (IS_ERR(tag_ops)) {
		dev_warn(ds->dev, "No tagger for this switch\n");
		return PTR_ERR(tag_ops);
	}

	dp->type = DSA_PORT_TYPE_CPU;
	dp->rcv = tag_ops->rcv;
	dp->tag_ops = tag_ops;
	dp->master = master;
	dp->dst = dst;

	return 0;
}

static int dsa_cpu_parse(struct dsa_port *port, u32 index,
			 struct dsa_switch_tree *dst,
			 struct dsa_switch *ds)
{
	if (!dst->cpu_dp)
		dst->cpu_dp = port;

	return 0;
}

static int dsa_ds_parse(struct dsa_switch_tree *dst, struct dsa_switch *ds)
{
	struct dsa_port *port;
	u32 index;
	int err;

	for (index = 0; index < ds->num_ports; index++) {
		port = &ds->ports[index];
		if (!dsa_port_is_valid(port) ||
		    dsa_port_is_dsa(port))
			continue;

		if (dsa_port_is_cpu(port)) {
			err = dsa_cpu_parse(port, index, dst, ds);
			if (err)
				return err;
		}

	}

	pr_info("DSA: switch %d %d parsed\n", dst->index, ds->index);

	return 0;
}

static int dsa_dst_parse(struct dsa_switch_tree *dst)
{
	struct dsa_switch *ds;
	struct dsa_port *dp;
	u32 index;
	int port;
	int err;

	for (index = 0; index < DSA_MAX_SWITCHES; index++) {
		ds = dst->ds[index];
		if (!ds)
			continue;

		err = dsa_ds_parse(dst, ds);
		if (err)
			return err;
	}

	if (!dst->cpu_dp) {
		pr_warn("Tree has no master device\n");
		return -EINVAL;
	}

	/* Assign the default CPU port to all ports of the fabric */
	for (index = 0; index < DSA_MAX_SWITCHES; index++) {
		ds = dst->ds[index];
		if (!ds)
			continue;

		for (port = 0; port < ds->num_ports; port++) {
			dp = &ds->ports[port];
			if (!dsa_port_is_valid(dp) ||
			    dsa_port_is_dsa(dp) ||
			    dsa_port_is_cpu(dp))
				continue;

			dp->cpu_dp = dst->cpu_dp;
		}
	}

	pr_info("DSA: tree %d parsed\n", dst->index);

	return 0;
}

static int dsa_port_parse_of(struct dsa_port *dp, struct device_node *dn)
{
	struct device_node *ethernet = of_parse_phandle(dn, "ethernet", 0);
	const char *name = of_get_property(dn, "label", NULL);
	bool link = of_property_read_bool(dn, "link");

	dp->dn = dn;

	if (ethernet) {
		struct net_device *master;

		master = of_find_net_device_by_node(ethernet);
		if (!master)
			return -EPROBE_DEFER;

		return dsa_port_parse_cpu(dp, master);
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
	u32 reg;
	int err;

	ports = of_get_child_by_name(dn, "ports");
	if (!ports) {
		dev_err(ds->dev, "no ports child node found\n");
		return -EINVAL;
	}

	for_each_available_child_of_node(ports, port) {
		err = of_property_read_u32(port, "reg", &reg);
		if (err)
			return err;

		if (reg >= ds->num_ports)
			return -EINVAL;

		dp = &ds->ports[reg];

		err = dsa_port_parse_of(dp, port);
		if (err)
			return err;
	}

	return 0;
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
	if (ds->index >= DSA_MAX_SWITCHES)
		return -EINVAL;

	ds->dst = dsa_tree_touch(m[0]);
	if (!ds->dst)
		return -ENOMEM;

	return 0;
}

static int dsa_switch_parse_of(struct dsa_switch *ds, struct device_node *dn)
{
	int err;

	err = dsa_switch_parse_member_of(ds, dn);
	if (err)
		return err;

	return dsa_switch_parse_ports_of(ds, dn);
}

static int dsa_port_parse(struct dsa_port *dp, const char *name,
			  struct device *dev)
{
	if (!strcmp(name, "cpu")) {
		struct net_device *master;

		master = dsa_dev_to_net_device(dev);
		if (!master)
			return -EPROBE_DEFER;

		dev_put(master);

		return dsa_port_parse_cpu(dp, master);
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
		dp = &ds->ports[i];

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
	ds->cd = cd;

	/* We don't support interconnected switches nor multiple trees via
	 * platform data, so this is the unique switch of the tree.
	 */
	ds->index = 0;
	ds->dst = dsa_tree_touch(0);
	if (!ds->dst)
		return -ENOMEM;

	return dsa_switch_parse_ports(ds, cd);
}

static int _dsa_register_switch(struct dsa_switch *ds)
{
	struct dsa_chip_data *pdata = ds->dev->platform_data;
	struct device_node *np = ds->dev->of_node;
	struct dsa_switch_tree *dst;
	unsigned int index;
	int i, err;

	if (np)
		err = dsa_switch_parse_of(ds, np);
	else if (pdata)
		err = dsa_switch_parse(ds, pdata);
	else
		err = -ENODEV;

	if (err)
		return err;

	index = ds->index;
	dst = ds->dst;

	/* Initialize the routing table */
	for (i = 0; i < DSA_MAX_SWITCHES; ++i)
		ds->rtable[i] = DSA_RTABLE_NONE;

	err = dsa_tree_add_switch(dst, ds);
	if (err)
		return err;

	err = dsa_dst_complete(dst);
	if (err < 0)
		goto out_del_dst;

	/* Not all switches registered yet */
	if (err == 1)
		return 0;

	if (dst->applied) {
		pr_info("DSA: Disjoint trees?\n");
		return -EINVAL;
	}

	err = dsa_dst_parse(dst);
	if (err)
		goto out_del_dst;

	err = dsa_dst_apply(dst);
	if (err) {
		dsa_dst_unapply(dst);
		goto out_del_dst;
	}

	return 0;

out_del_dst:
	dsa_tree_remove_switch(dst, index);

	return err;
}

struct dsa_switch *dsa_switch_alloc(struct device *dev, size_t n)
{
	size_t size = sizeof(struct dsa_switch) + n * sizeof(struct dsa_port);
	struct dsa_switch *ds;
	int i;

	ds = devm_kzalloc(dev, size, GFP_KERNEL);
	if (!ds)
		return NULL;

	ds->dev = dev;
	ds->num_ports = n;

	for (i = 0; i < ds->num_ports; ++i) {
		ds->ports[i].index = i;
		ds->ports[i].ds = ds;
	}

	return ds;
}
EXPORT_SYMBOL_GPL(dsa_switch_alloc);

int dsa_register_switch(struct dsa_switch *ds)
{
	int err;

	mutex_lock(&dsa2_mutex);
	err = _dsa_register_switch(ds);
	mutex_unlock(&dsa2_mutex);

	return err;
}
EXPORT_SYMBOL_GPL(dsa_register_switch);

static void _dsa_unregister_switch(struct dsa_switch *ds)
{
	struct dsa_switch_tree *dst = ds->dst;
	unsigned int index = ds->index;

	dsa_dst_unapply(dst);

	dsa_tree_remove_switch(dst, index);
}

void dsa_unregister_switch(struct dsa_switch *ds)
{
	mutex_lock(&dsa2_mutex);
	_dsa_unregister_switch(ds);
	mutex_unlock(&dsa2_mutex);
}
EXPORT_SYMBOL_GPL(dsa_unregister_switch);
