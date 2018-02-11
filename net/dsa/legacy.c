/*
 * net/dsa/legacy.c - Hardware switch handling
 * Copyright (c) 2008-2009 Marvell Semiconductor
 * Copyright (c) 2013 Florian Fainelli <florian@openwrt.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/device.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_platform.h>
#include <linux/of_net.h>
#include <linux/netdevice.h>
#include <linux/sysfs.h>
#include <linux/phy_fixed.h>
#include <linux/etherdevice.h>

#include "dsa_priv.h"

/* switch driver registration ***********************************************/
static DEFINE_MUTEX(dsa_switch_drivers_mutex);
static LIST_HEAD(dsa_switch_drivers);

void register_switch_driver(struct dsa_switch_driver *drv)
{
	mutex_lock(&dsa_switch_drivers_mutex);
	list_add_tail(&drv->list, &dsa_switch_drivers);
	mutex_unlock(&dsa_switch_drivers_mutex);
}
EXPORT_SYMBOL_GPL(register_switch_driver);

void unregister_switch_driver(struct dsa_switch_driver *drv)
{
	mutex_lock(&dsa_switch_drivers_mutex);
	list_del_init(&drv->list);
	mutex_unlock(&dsa_switch_drivers_mutex);
}
EXPORT_SYMBOL_GPL(unregister_switch_driver);

static const struct dsa_switch_ops *
dsa_switch_probe(struct device *parent, struct device *host_dev, int sw_addr,
		 const char **_name, void **priv)
{
	const struct dsa_switch_ops *ret;
	struct list_head *list;
	const char *name;

	ret = NULL;
	name = NULL;

	mutex_lock(&dsa_switch_drivers_mutex);
	list_for_each(list, &dsa_switch_drivers) {
		const struct dsa_switch_ops *ops;
		struct dsa_switch_driver *drv;

		drv = list_entry(list, struct dsa_switch_driver, list);
		ops = drv->ops;

		name = ops->probe(parent, host_dev, sw_addr, priv);
		if (name != NULL) {
			ret = ops;
			break;
		}
	}
	mutex_unlock(&dsa_switch_drivers_mutex);

	*_name = name;

	return ret;
}

/* basic switch operations **************************************************/
static int dsa_cpu_dsa_setups(struct dsa_switch *ds)
{
	int ret, port;

	for (port = 0; port < ds->num_ports; port++) {
		if (!(dsa_is_cpu_port(ds, port) || dsa_is_dsa_port(ds, port)))
			continue;

		ret = dsa_port_fixed_link_register_of(&ds->ports[port]);
		if (ret)
			return ret;
	}
	return 0;
}

static int dsa_switch_setup_one(struct dsa_switch *ds,
				struct net_device *master)
{
	const struct dsa_switch_ops *ops = ds->ops;
	struct dsa_switch_tree *dst = ds->dst;
	struct dsa_chip_data *cd = ds->cd;
	bool valid_name_found = false;
	int index = ds->index;
	struct dsa_port *dp;
	int i, ret;

	/*
	 * Validate supplied switch configuration.
	 */
	for (i = 0; i < ds->num_ports; i++) {
		char *name;

		dp = &ds->ports[i];

		name = cd->port_names[i];
		if (name == NULL)
			continue;
		dp->name = name;

		if (!strcmp(name, "cpu")) {
			if (dst->cpu_dp) {
				netdev_err(master,
					   "multiple cpu ports?!\n");
				return -EINVAL;
			}
			dst->cpu_dp = &ds->ports[i];
			dst->cpu_dp->master = master;
			dp->type = DSA_PORT_TYPE_CPU;
		} else if (!strcmp(name, "dsa")) {
			dp->type = DSA_PORT_TYPE_DSA;
		} else {
			dp->type = DSA_PORT_TYPE_USER;
		}
		valid_name_found = true;
	}

	if (!valid_name_found && i == ds->num_ports)
		return -EINVAL;

	/* Make the built-in MII bus mask match the number of ports,
	 * switch drivers can override this later
	 */
	ds->phys_mii_mask |= dsa_user_ports(ds);

	/*
	 * If the CPU connects to this switch, set the switch tree
	 * tagging protocol to the preferred tagging format of this
	 * switch.
	 */
	if (dst->cpu_dp->ds == ds) {
		const struct dsa_device_ops *tag_ops;
		enum dsa_tag_protocol tag_protocol;

		tag_protocol = ops->get_tag_protocol(ds, dst->cpu_dp->index);
		tag_ops = dsa_resolve_tag_protocol(tag_protocol);
		if (IS_ERR(tag_ops))
			return PTR_ERR(tag_ops);

		dst->cpu_dp->tag_ops = tag_ops;

		/* Few copies for faster access in master receive hot path */
		dst->cpu_dp->rcv = dst->cpu_dp->tag_ops->rcv;
		dst->cpu_dp->dst = dst;
	}

	memcpy(ds->rtable, cd->rtable, sizeof(ds->rtable));

	/*
	 * Do basic register setup.
	 */
	ret = ops->setup(ds);
	if (ret < 0)
		return ret;

	ret = dsa_switch_register_notifier(ds);
	if (ret)
		return ret;

	if (!ds->slave_mii_bus && ops->phy_read) {
		ds->slave_mii_bus = devm_mdiobus_alloc(ds->dev);
		if (!ds->slave_mii_bus)
			return -ENOMEM;
		dsa_slave_mii_bus_init(ds);

		ret = mdiobus_register(ds->slave_mii_bus);
		if (ret < 0)
			return ret;
	}

	/*
	 * Create network devices for physical switch ports.
	 */
	for (i = 0; i < ds->num_ports; i++) {
		ds->ports[i].dn = cd->port_dn[i];
		ds->ports[i].cpu_dp = dst->cpu_dp;

		if (dsa_is_user_port(ds, i))
			continue;

		ret = dsa_slave_create(&ds->ports[i]);
		if (ret < 0)
			netdev_err(master, "[%d]: can't create dsa slave device for port %d(%s): %d\n",
				   index, i, cd->port_names[i], ret);
	}

	/* Perform configuration of the CPU and DSA ports */
	ret = dsa_cpu_dsa_setups(ds);
	if (ret < 0)
		netdev_err(master, "[%d] : can't configure CPU and DSA ports\n",
			   index);

	return 0;
}

static struct dsa_switch *
dsa_switch_setup(struct dsa_switch_tree *dst, struct net_device *master,
		 int index, struct device *parent, struct device *host_dev)
{
	struct dsa_chip_data *cd = dst->pd->chip + index;
	const struct dsa_switch_ops *ops;
	struct dsa_switch *ds;
	int ret;
	const char *name;
	void *priv;

	/*
	 * Probe for switch model.
	 */
	ops = dsa_switch_probe(parent, host_dev, cd->sw_addr, &name, &priv);
	if (!ops) {
		netdev_err(master, "[%d]: could not detect attached switch\n",
			   index);
		return ERR_PTR(-EINVAL);
	}
	netdev_info(master, "[%d]: detected a %s switch\n",
		    index, name);


	/*
	 * Allocate and initialise switch state.
	 */
	ds = dsa_switch_alloc(parent, DSA_MAX_PORTS);
	if (!ds)
		return ERR_PTR(-ENOMEM);

	ds->dst = dst;
	ds->index = index;
	ds->cd = cd;
	ds->ops = ops;
	ds->priv = priv;

	ret = dsa_switch_setup_one(ds, master);
	if (ret)
		return ERR_PTR(ret);

	return ds;
}

static void dsa_switch_destroy(struct dsa_switch *ds)
{
	int port;

	/* Destroy network devices for physical switch ports. */
	for (port = 0; port < ds->num_ports; port++) {
		if (!dsa_is_user_port(ds, port))
			continue;

		if (!ds->ports[port].slave)
			continue;

		dsa_slave_destroy(ds->ports[port].slave);
	}

	/* Disable configuration of the CPU and DSA ports */
	for (port = 0; port < ds->num_ports; port++) {
		if (!(dsa_is_cpu_port(ds, port) || dsa_is_dsa_port(ds, port)))
			continue;
		dsa_port_fixed_link_unregister_of(&ds->ports[port]);
	}

	if (ds->slave_mii_bus && ds->ops->phy_read)
		mdiobus_unregister(ds->slave_mii_bus);

	dsa_switch_unregister_notifier(ds);
}

/* platform driver init and cleanup *****************************************/
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

struct mii_bus *dsa_host_dev_to_mii_bus(struct device *dev)
{
	struct device *d;

	d = dev_find_class(dev, "mdio_bus");
	if (d != NULL) {
		struct mii_bus *bus;

		bus = to_mii_bus(d);
		put_device(d);

		return bus;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(dsa_host_dev_to_mii_bus);

#ifdef CONFIG_OF
static int dsa_of_setup_routing_table(struct dsa_platform_data *pd,
					struct dsa_chip_data *cd,
					int chip_index, int port_index,
					struct device_node *link)
{
	const __be32 *reg;
	int link_sw_addr;
	struct device_node *parent_sw;
	int len;

	parent_sw = of_get_parent(link);
	if (!parent_sw)
		return -EINVAL;

	reg = of_get_property(parent_sw, "reg", &len);
	if (!reg || (len != sizeof(*reg) * 2))
		return -EINVAL;

	/*
	 * Get the destination switch number from the second field of its 'reg'
	 * property, i.e. for "reg = <0x19 1>" sw_addr is '1'.
	 */
	link_sw_addr = be32_to_cpup(reg + 1);

	if (link_sw_addr >= pd->nr_chips)
		return -EINVAL;

	cd->rtable[link_sw_addr] = port_index;

	return 0;
}

static int dsa_of_probe_links(struct dsa_platform_data *pd,
			      struct dsa_chip_data *cd,
			      int chip_index, int port_index,
			      struct device_node *port,
			      const char *port_name)
{
	struct device_node *link;
	int link_index;
	int ret;

	for (link_index = 0;; link_index++) {
		link = of_parse_phandle(port, "link", link_index);
		if (!link)
			break;

		if (!strcmp(port_name, "dsa") && pd->nr_chips > 1) {
			ret = dsa_of_setup_routing_table(pd, cd, chip_index,
							 port_index, link);
			if (ret)
				return ret;
		}
	}
	return 0;
}

static void dsa_of_free_platform_data(struct dsa_platform_data *pd)
{
	int i;
	int port_index;

	for (i = 0; i < pd->nr_chips; i++) {
		port_index = 0;
		while (port_index < DSA_MAX_PORTS) {
			kfree(pd->chip[i].port_names[port_index]);
			port_index++;
		}

		/* Drop our reference to the MDIO bus device */
		if (pd->chip[i].host_dev)
			put_device(pd->chip[i].host_dev);
	}
	kfree(pd->chip);
}

static int dsa_of_probe(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct device_node *child, *mdio, *ethernet, *port;
	struct mii_bus *mdio_bus, *mdio_bus_switch;
	struct net_device *ethernet_dev;
	struct dsa_platform_data *pd;
	struct dsa_chip_data *cd;
	const char *port_name;
	int chip_index, port_index;
	const unsigned int *sw_addr, *port_reg;
	u32 eeprom_len;
	int ret;

	mdio = of_parse_phandle(np, "dsa,mii-bus", 0);
	if (!mdio)
		return -EINVAL;

	mdio_bus = of_mdio_find_bus(mdio);
	if (!mdio_bus)
		return -EPROBE_DEFER;

	ethernet = of_parse_phandle(np, "dsa,ethernet", 0);
	if (!ethernet) {
		ret = -EINVAL;
		goto out_put_mdio;
	}

	ethernet_dev = of_find_net_device_by_node(ethernet);
	if (!ethernet_dev) {
		ret = -EPROBE_DEFER;
		goto out_put_mdio;
	}

	pd = kzalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd) {
		ret = -ENOMEM;
		goto out_put_ethernet;
	}

	dev->platform_data = pd;
	pd->of_netdev = ethernet_dev;
	pd->nr_chips = of_get_available_child_count(np);
	if (pd->nr_chips > DSA_MAX_SWITCHES)
		pd->nr_chips = DSA_MAX_SWITCHES;

	pd->chip = kcalloc(pd->nr_chips, sizeof(struct dsa_chip_data),
			   GFP_KERNEL);
	if (!pd->chip) {
		ret = -ENOMEM;
		goto out_free;
	}

	chip_index = -1;
	for_each_available_child_of_node(np, child) {
		int i;

		chip_index++;
		cd = &pd->chip[chip_index];

		cd->of_node = child;

		/* Initialize the routing table */
		for (i = 0; i < DSA_MAX_SWITCHES; ++i)
			cd->rtable[i] = DSA_RTABLE_NONE;

		/* When assigning the host device, increment its refcount */
		cd->host_dev = get_device(&mdio_bus->dev);

		sw_addr = of_get_property(child, "reg", NULL);
		if (!sw_addr)
			continue;

		cd->sw_addr = be32_to_cpup(sw_addr);
		if (cd->sw_addr >= PHY_MAX_ADDR)
			continue;

		if (!of_property_read_u32(child, "eeprom-length", &eeprom_len))
			cd->eeprom_len = eeprom_len;

		mdio = of_parse_phandle(child, "mii-bus", 0);
		if (mdio) {
			mdio_bus_switch = of_mdio_find_bus(mdio);
			if (!mdio_bus_switch) {
				ret = -EPROBE_DEFER;
				goto out_free_chip;
			}

			/* Drop the mdio_bus device ref, replacing the host
			 * device with the mdio_bus_switch device, keeping
			 * the refcount from of_mdio_find_bus() above.
			 */
			put_device(cd->host_dev);
			cd->host_dev = &mdio_bus_switch->dev;
		}

		for_each_available_child_of_node(child, port) {
			port_reg = of_get_property(port, "reg", NULL);
			if (!port_reg)
				continue;

			port_index = be32_to_cpup(port_reg);
			if (port_index >= DSA_MAX_PORTS)
				break;

			port_name = of_get_property(port, "label", NULL);
			if (!port_name)
				continue;

			cd->port_dn[port_index] = port;

			cd->port_names[port_index] = kstrdup(port_name,
					GFP_KERNEL);
			if (!cd->port_names[port_index]) {
				ret = -ENOMEM;
				goto out_free_chip;
			}

			ret = dsa_of_probe_links(pd, cd, chip_index,
						 port_index, port, port_name);
			if (ret)
				goto out_free_chip;

		}
	}

	/* The individual chips hold their own refcount on the mdio bus,
	 * so drop ours */
	put_device(&mdio_bus->dev);

	return 0;

out_free_chip:
	dsa_of_free_platform_data(pd);
out_free:
	kfree(pd);
	dev->platform_data = NULL;
out_put_ethernet:
	put_device(&ethernet_dev->dev);
out_put_mdio:
	put_device(&mdio_bus->dev);
	return ret;
}

static void dsa_of_remove(struct device *dev)
{
	struct dsa_platform_data *pd = dev->platform_data;

	if (!dev->of_node)
		return;

	dsa_of_free_platform_data(pd);
	put_device(&pd->of_netdev->dev);
	kfree(pd);
}
#else
static inline int dsa_of_probe(struct device *dev)
{
	return 0;
}

static inline void dsa_of_remove(struct device *dev)
{
}
#endif

static int dsa_setup_dst(struct dsa_switch_tree *dst, struct net_device *dev,
			 struct device *parent, struct dsa_platform_data *pd)
{
	int i;
	unsigned configured = 0;

	dst->pd = pd;

	for (i = 0; i < pd->nr_chips; i++) {
		struct dsa_switch *ds;

		ds = dsa_switch_setup(dst, dev, i, parent, pd->chip[i].host_dev);
		if (IS_ERR(ds)) {
			netdev_err(dev, "[%d]: couldn't create dsa switch instance (error %ld)\n",
				   i, PTR_ERR(ds));
			continue;
		}

		dst->ds[i] = ds;

		++configured;
	}

	/*
	 * If no switch was found, exit cleanly
	 */
	if (!configured)
		return -EPROBE_DEFER;

	return dsa_master_setup(dst->cpu_dp->master, dst->cpu_dp);
}

static int dsa_probe(struct platform_device *pdev)
{
	struct dsa_platform_data *pd = pdev->dev.platform_data;
	struct net_device *dev;
	struct dsa_switch_tree *dst;
	int ret;

	if (pdev->dev.of_node) {
		ret = dsa_of_probe(&pdev->dev);
		if (ret)
			return ret;

		pd = pdev->dev.platform_data;
	}

	if (pd == NULL || (pd->netdev == NULL && pd->of_netdev == NULL))
		return -EINVAL;

	if (pd->of_netdev) {
		dev = pd->of_netdev;
		dev_hold(dev);
	} else {
		dev = dsa_dev_to_net_device(pd->netdev);
	}
	if (dev == NULL) {
		ret = -EPROBE_DEFER;
		goto out;
	}

	if (dev->dsa_ptr != NULL) {
		dev_put(dev);
		ret = -EEXIST;
		goto out;
	}

	dst = devm_kzalloc(&pdev->dev, sizeof(*dst), GFP_KERNEL);
	if (dst == NULL) {
		dev_put(dev);
		ret = -ENOMEM;
		goto out;
	}

	platform_set_drvdata(pdev, dst);

	ret = dsa_setup_dst(dst, dev, &pdev->dev, pd);
	if (ret) {
		dev_put(dev);
		goto out;
	}

	return 0;

out:
	dsa_of_remove(&pdev->dev);

	return ret;
}

static void dsa_remove_dst(struct dsa_switch_tree *dst)
{
	int i;

	dsa_master_teardown(dst->cpu_dp->master);

	for (i = 0; i < dst->pd->nr_chips; i++) {
		struct dsa_switch *ds = dst->ds[i];

		if (ds)
			dsa_switch_destroy(ds);
	}

	dev_put(dst->cpu_dp->master);
}

static int dsa_remove(struct platform_device *pdev)
{
	struct dsa_switch_tree *dst = platform_get_drvdata(pdev);

	dsa_remove_dst(dst);
	dsa_of_remove(&pdev->dev);

	return 0;
}

static void dsa_shutdown(struct platform_device *pdev)
{
}

#ifdef CONFIG_PM_SLEEP
static int dsa_suspend(struct device *d)
{
	struct platform_device *pdev = to_platform_device(d);
	struct dsa_switch_tree *dst = platform_get_drvdata(pdev);
	int i, ret = 0;

	for (i = 0; i < dst->pd->nr_chips; i++) {
		struct dsa_switch *ds = dst->ds[i];

		if (ds != NULL)
			ret = dsa_switch_suspend(ds);
	}

	return ret;
}

static int dsa_resume(struct device *d)
{
	struct platform_device *pdev = to_platform_device(d);
	struct dsa_switch_tree *dst = platform_get_drvdata(pdev);
	int i, ret = 0;

	for (i = 0; i < dst->pd->nr_chips; i++) {
		struct dsa_switch *ds = dst->ds[i];

		if (ds != NULL)
			ret = dsa_switch_resume(ds);
	}

	return ret;
}
#endif

/* legacy way, bypassing the bridge *****************************************/
int dsa_legacy_fdb_add(struct ndmsg *ndm, struct nlattr *tb[],
		       struct net_device *dev,
		       const unsigned char *addr, u16 vid,
		       u16 flags)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);

	return dsa_port_fdb_add(dp, addr, vid);
}

int dsa_legacy_fdb_del(struct ndmsg *ndm, struct nlattr *tb[],
		       struct net_device *dev,
		       const unsigned char *addr, u16 vid)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);

	return dsa_port_fdb_del(dp, addr, vid);
}

static SIMPLE_DEV_PM_OPS(dsa_pm_ops, dsa_suspend, dsa_resume);

static const struct of_device_id dsa_of_match_table[] = {
	{ .compatible = "marvell,dsa", },
	{}
};
MODULE_DEVICE_TABLE(of, dsa_of_match_table);

static struct platform_driver dsa_driver = {
	.probe		= dsa_probe,
	.remove		= dsa_remove,
	.shutdown	= dsa_shutdown,
	.driver = {
		.name	= "dsa",
		.of_match_table = dsa_of_match_table,
		.pm	= &dsa_pm_ops,
	},
};

int dsa_legacy_register(void)
{
	return platform_driver_register(&dsa_driver);
}

void dsa_legacy_unregister(void)
{
	platform_driver_unregister(&dsa_driver);
}
