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

static LIST_HEAD(dsa_switch_trees);
static DEFINE_MUTEX(dsa2_mutex);

static const struct devlink_ops dsa_devlink_ops = {
};

static struct dsa_switch_tree *dsa_get_dst(u32 tree)
{
	struct dsa_switch_tree *dst;

	list_for_each_entry(dst, &dsa_switch_trees, list)
		if (dst->tree == tree) {
			kref_get(&dst->refcount);
			return dst;
		}
	return NULL;
}

static void dsa_free_dst(struct kref *ref)
{
	struct dsa_switch_tree *dst = container_of(ref, struct dsa_switch_tree,
						   refcount);

	list_del(&dst->list);
	kfree(dst);
}

static void dsa_put_dst(struct dsa_switch_tree *dst)
{
	kref_put(&dst->refcount, dsa_free_dst);
}

static struct dsa_switch_tree *dsa_add_dst(u32 tree)
{
	struct dsa_switch_tree *dst;

	dst = kzalloc(sizeof(*dst), GFP_KERNEL);
	if (!dst)
		return NULL;
	dst->tree = tree;
	INIT_LIST_HEAD(&dst->list);
	list_add_tail(&dsa_switch_trees, &dst->list);
	kref_init(&dst->refcount);

	return dst;
}

static void dsa_dst_add_ds(struct dsa_switch_tree *dst,
			   struct dsa_switch *ds, u32 index)
{
	kref_get(&dst->refcount);
	dst->ds[index] = ds;
}

static void dsa_dst_del_ds(struct dsa_switch_tree *dst,
			   struct dsa_switch *ds, u32 index)
{
	dst->ds[index] = NULL;
	kref_put(&dst->refcount, dsa_free_dst);
}

/* For platform data configurations, we need to have a valid name argument to
 * differentiate a disabled port from an enabled one
 */
static bool dsa_port_is_valid(struct dsa_port *port)
{
	return !!(port->dn || port->name);
}

static bool dsa_port_is_dsa(struct dsa_port *port)
{
	if (port->name && !strcmp(port->name, "dsa"))
		return true;
	else
		return !!of_parse_phandle(port->dn, "link", 0);
}

static bool dsa_port_is_cpu(struct dsa_port *port)
{
	if (port->name && !strcmp(port->name, "cpu"))
		return true;
	else
		return !!of_parse_phandle(port->dn, "ethernet", 0);
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

		ds->dsa_port_mask |= BIT(index);
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

	err = dsa_cpu_dsa_setup(ds, ds->dev, port, port->index);
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
	dsa_cpu_dsa_destroy(port);
}

static int dsa_cpu_port_apply(struct dsa_port *port)
{
	struct dsa_switch *ds = port->ds;
	int err;

	err = dsa_cpu_dsa_setup(ds, ds->dev, port, port->index);
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
	dsa_cpu_dsa_destroy(port);
	port->ds->cpu_port_mask &= ~BIT(port->index);

}

static int dsa_user_port_apply(struct dsa_port *port)
{
	struct dsa_switch *ds = port->ds;
	const char *name = port->name;
	int err;

	if (port->dn)
		name = of_get_property(port->dn, "label", NULL);
	if (!name)
		name = "eth%d";

	err = dsa_slave_create(ds, ds->dev, port->index, name);
	if (err) {
		dev_warn(ds->dev, "Failed to create slave %d: %d\n",
			 port->index, err);
		port->netdev = NULL;
		return err;
	}

	memset(&port->devlink_port, 0, sizeof(port->devlink_port));
	err = devlink_port_register(ds->devlink, &port->devlink_port,
				    port->index);
	if (err)
		return err;

	devlink_port_type_eth_set(&port->devlink_port, port->netdev);

	return 0;
}

static void dsa_user_port_unapply(struct dsa_port *port)
{
	devlink_port_unregister(&port->devlink_port);
	if (port->netdev) {
		dsa_slave_destroy(port->netdev);
		port->netdev = NULL;
		port->ds->enabled_port_mask &= ~(1 << port->index);
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
	ds->phys_mii_mask = ds->enabled_port_mask;

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

	if (ds->ops->set_addr) {
		err = ds->ops->set_addr(ds, dst->master_netdev->dev_addr);
		if (err < 0)
			return err;
	}

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

	if (dst->cpu_dp) {
		err = dsa_cpu_port_ethtool_setup(dst->cpu_dp);
		if (err)
			return err;
	}

	/* If we use a tagging format that doesn't have an ethertype
	 * field, make sure that all packets from this point on get
	 * sent to the tag format's receive function.
	 */
	wmb();
	dst->master_netdev->dsa_ptr = dst;
	dst->applied = true;

	return 0;
}

static void dsa_dst_unapply(struct dsa_switch_tree *dst)
{
	struct dsa_switch *ds;
	u32 index;

	if (!dst->applied)
		return;

	dst->master_netdev->dsa_ptr = NULL;

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

	if (dst->cpu_dp) {
		dsa_cpu_port_ethtool_restore(dst->cpu_dp);
		dst->cpu_dp = NULL;
	}

	pr_info("DSA: tree %d unapplied\n", dst->tree);
	dst->applied = false;
}

static int dsa_cpu_parse(struct dsa_port *port, u32 index,
			 struct dsa_switch_tree *dst,
			 struct dsa_switch *ds)
{
	enum dsa_tag_protocol tag_protocol;
	struct net_device *ethernet_dev;
	struct device_node *ethernet;

	if (port->dn) {
		ethernet = of_parse_phandle(port->dn, "ethernet", 0);
		if (!ethernet)
			return -EINVAL;
		ethernet_dev = of_find_net_device_by_node(ethernet);
	} else {
		ethernet_dev = dsa_dev_to_net_device(ds->cd->netdev[index]);
		dev_put(ethernet_dev);
	}

	if (!ethernet_dev)
		return -EPROBE_DEFER;

	if (!ds->master_netdev)
		ds->master_netdev = ethernet_dev;

	if (!dst->master_netdev)
		dst->master_netdev = ethernet_dev;

	if (!dst->cpu_dp)
		dst->cpu_dp = port;

	tag_protocol = ds->ops->get_tag_protocol(ds);
	dst->tag_ops = dsa_resolve_tag_protocol(tag_protocol);
	if (IS_ERR(dst->tag_ops)) {
		dev_warn(ds->dev, "No tagger for this switch\n");
		return PTR_ERR(dst->tag_ops);
	}

	dst->rcv = dst->tag_ops->rcv;

	/* Initialize cpu_port_mask now for drv->setup()
	 * to have access to a correct value, just like what
	 * net/dsa/dsa.c::dsa_switch_setup_one does.
	 */
	ds->cpu_port_mask |= BIT(index);

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
		} else {
			/* Initialize enabled_port_mask now for drv->setup()
			 * to have access to a correct value, just like what
			 * net/dsa/dsa.c::dsa_switch_setup_one does.
			 */
			ds->enabled_port_mask |= BIT(index);
		}

	}

	pr_info("DSA: switch %d %d parsed\n", dst->tree, ds->index);

	return 0;
}

static int dsa_dst_parse(struct dsa_switch_tree *dst)
{
	struct dsa_switch *ds;
	u32 index;
	int err;

	for (index = 0; index < DSA_MAX_SWITCHES; index++) {
		ds = dst->ds[index];
		if (!ds)
			continue;

		err = dsa_ds_parse(dst, ds);
		if (err)
			return err;
	}

	if (!dst->master_netdev) {
		pr_warn("Tree has no master device\n");
		return -EINVAL;
	}

	pr_info("DSA: tree %d parsed\n", dst->tree);

	return 0;
}

static int dsa_parse_ports_dn(struct device_node *ports, struct dsa_switch *ds)
{
	struct device_node *port;
	int err;
	u32 reg;

	for_each_available_child_of_node(ports, port) {
		err = of_property_read_u32(port, "reg", &reg);
		if (err)
			return err;

		if (reg >= ds->num_ports)
			return -EINVAL;

		ds->ports[reg].dn = port;
	}

	return 0;
}

static int dsa_parse_ports(struct dsa_chip_data *cd, struct dsa_switch *ds)
{
	bool valid_name_found = false;
	unsigned int i;

	for (i = 0; i < DSA_MAX_PORTS; i++) {
		if (!cd->port_names[i])
			continue;

		ds->ports[i].name = cd->port_names[i];
		valid_name_found = true;
	}

	if (!valid_name_found && i == DSA_MAX_PORTS)
		return -EINVAL;

	return 0;
}

static int dsa_parse_member_dn(struct device_node *np, u32 *tree, u32 *index)
{
	int err;

	*tree = *index = 0;

	err = of_property_read_u32_index(np, "dsa,member", 0, tree);
	if (err) {
		/* Does not exist, but it is optional */
		if (err == -EINVAL)
			return 0;
		return err;
	}

	err = of_property_read_u32_index(np, "dsa,member", 1, index);
	if (err)
		return err;

	if (*index >= DSA_MAX_SWITCHES)
		return -EINVAL;

	return 0;
}

static int dsa_parse_member(struct dsa_chip_data *pd, u32 *tree, u32 *index)
{
	if (!pd)
		return -ENODEV;

	/* We do not support complex trees with dsa_chip_data */
	*tree = 0;
	*index = 0;

	return 0;
}

static struct device_node *dsa_get_ports(struct dsa_switch *ds,
					 struct device_node *np)
{
	struct device_node *ports;

	ports = of_get_child_by_name(np, "ports");
	if (!ports) {
		dev_err(ds->dev, "no ports child node found\n");
		return ERR_PTR(-EINVAL);
	}

	return ports;
}

static int _dsa_register_switch(struct dsa_switch *ds)
{
	struct dsa_chip_data *pdata = ds->dev->platform_data;
	struct device_node *np = ds->dev->of_node;
	struct dsa_switch_tree *dst;
	struct device_node *ports;
	u32 tree, index;
	int i, err;

	if (np) {
		err = dsa_parse_member_dn(np, &tree, &index);
		if (err)
			return err;

		ports = dsa_get_ports(ds, np);
		if (IS_ERR(ports))
			return PTR_ERR(ports);

		err = dsa_parse_ports_dn(ports, ds);
		if (err)
			return err;
	} else {
		err = dsa_parse_member(pdata, &tree, &index);
		if (err)
			return err;

		err = dsa_parse_ports(pdata, ds);
		if (err)
			return err;
	}

	dst = dsa_get_dst(tree);
	if (!dst) {
		dst = dsa_add_dst(tree);
		if (!dst)
			return -ENOMEM;
	}

	if (dst->ds[index]) {
		err = -EBUSY;
		goto out;
	}

	ds->dst = dst;
	ds->index = index;
	ds->cd = pdata;

	/* Initialize the routing table */
	for (i = 0; i < DSA_MAX_SWITCHES; ++i)
		ds->rtable[i] = DSA_RTABLE_NONE;

	dsa_dst_add_ds(dst, ds, index);

	err = dsa_dst_complete(dst);
	if (err < 0)
		goto out_del_dst;

	if (err == 1) {
		/* Not all switches registered yet */
		err = 0;
		goto out;
	}

	if (dst->applied) {
		pr_info("DSA: Disjoint trees?\n");
		return -EINVAL;
	}

	err = dsa_dst_parse(dst);
	if (err) {
		if (err == -EPROBE_DEFER) {
			dsa_dst_del_ds(dst, ds, ds->index);
			return err;
		}

		goto out_del_dst;
	}

	err = dsa_dst_apply(dst);
	if (err) {
		dsa_dst_unapply(dst);
		goto out_del_dst;
	}

	dsa_put_dst(dst);
	return 0;

out_del_dst:
	dsa_dst_del_ds(dst, ds, ds->index);
out:
	dsa_put_dst(dst);

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

	dsa_dst_unapply(dst);

	dsa_dst_del_ds(dst, ds, ds->index);
}

void dsa_unregister_switch(struct dsa_switch *ds)
{
	mutex_lock(&dsa2_mutex);
	_dsa_unregister_switch(ds);
	mutex_unlock(&dsa2_mutex);
}
EXPORT_SYMBOL_GPL(dsa_unregister_switch);
