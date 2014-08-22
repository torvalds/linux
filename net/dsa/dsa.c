/*
 * net/dsa/dsa.c - Hardware switch handling
 * Copyright (c) 2008-2009 Marvell Semiconductor
 * Copyright (c) 2013 Florian Fainelli <florian@openwrt.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <net/dsa.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_platform.h>
#include "dsa_priv.h"

char dsa_driver_version[] = "0.1";


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

static struct dsa_switch_driver *
dsa_switch_probe(struct mii_bus *bus, int sw_addr, char **_name)
{
	struct dsa_switch_driver *ret;
	struct list_head *list;
	char *name;

	ret = NULL;
	name = NULL;

	mutex_lock(&dsa_switch_drivers_mutex);
	list_for_each(list, &dsa_switch_drivers) {
		struct dsa_switch_driver *drv;

		drv = list_entry(list, struct dsa_switch_driver, list);

		name = drv->probe(bus, sw_addr);
		if (name != NULL) {
			ret = drv;
			break;
		}
	}
	mutex_unlock(&dsa_switch_drivers_mutex);

	*_name = name;

	return ret;
}


/* basic switch operations **************************************************/
static struct dsa_switch *
dsa_switch_setup(struct dsa_switch_tree *dst, int index,
		 struct device *parent, struct mii_bus *bus)
{
	struct dsa_chip_data *pd = dst->pd->chip + index;
	struct dsa_switch_driver *drv;
	struct dsa_switch *ds;
	int ret;
	char *name;
	int i;
	bool valid_name_found = false;

	/*
	 * Probe for switch model.
	 */
	drv = dsa_switch_probe(bus, pd->sw_addr, &name);
	if (drv == NULL) {
		printk(KERN_ERR "%s[%d]: could not detect attached switch\n",
		       dst->master_netdev->name, index);
		return ERR_PTR(-EINVAL);
	}
	printk(KERN_INFO "%s[%d]: detected a %s switch\n",
		dst->master_netdev->name, index, name);


	/*
	 * Allocate and initialise switch state.
	 */
	ds = kzalloc(sizeof(*ds) + drv->priv_size, GFP_KERNEL);
	if (ds == NULL)
		return ERR_PTR(-ENOMEM);

	ds->dst = dst;
	ds->index = index;
	ds->pd = dst->pd->chip + index;
	ds->drv = drv;
	ds->master_mii_bus = bus;


	/*
	 * Validate supplied switch configuration.
	 */
	for (i = 0; i < DSA_MAX_PORTS; i++) {
		char *name;

		name = pd->port_names[i];
		if (name == NULL)
			continue;

		if (!strcmp(name, "cpu")) {
			if (dst->cpu_switch != -1) {
				printk(KERN_ERR "multiple cpu ports?!\n");
				ret = -EINVAL;
				goto out;
			}
			dst->cpu_switch = index;
			dst->cpu_port = i;
		} else if (!strcmp(name, "dsa")) {
			ds->dsa_port_mask |= 1 << i;
		} else {
			ds->phys_port_mask |= 1 << i;
		}
		valid_name_found = true;
	}

	if (!valid_name_found && i == DSA_MAX_PORTS) {
		ret = -EINVAL;
		goto out;
	}

	/*
	 * If the CPU connects to this switch, set the switch tree
	 * tagging protocol to the preferred tagging format of this
	 * switch.
	 */
	if (ds->dst->cpu_switch == index)
		ds->dst->tag_protocol = drv->tag_protocol;


	/*
	 * Do basic register setup.
	 */
	ret = drv->setup(ds);
	if (ret < 0)
		goto out;

	ret = drv->set_addr(ds, dst->master_netdev->dev_addr);
	if (ret < 0)
		goto out;

	ds->slave_mii_bus = mdiobus_alloc();
	if (ds->slave_mii_bus == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	dsa_slave_mii_bus_init(ds);

	ret = mdiobus_register(ds->slave_mii_bus);
	if (ret < 0)
		goto out_free;


	/*
	 * Create network devices for physical switch ports.
	 */
	for (i = 0; i < DSA_MAX_PORTS; i++) {
		struct net_device *slave_dev;

		if (!(ds->phys_port_mask & (1 << i)))
			continue;

		slave_dev = dsa_slave_create(ds, parent, i, pd->port_names[i]);
		if (slave_dev == NULL) {
			printk(KERN_ERR "%s[%d]: can't create dsa "
			       "slave device for port %d(%s)\n",
			       dst->master_netdev->name,
			       index, i, pd->port_names[i]);
			continue;
		}

		ds->ports[i] = slave_dev;
	}

	return ds;

out_free:
	mdiobus_free(ds->slave_mii_bus);
out:
	kfree(ds);
	return ERR_PTR(ret);
}

static void dsa_switch_destroy(struct dsa_switch *ds)
{
}


/* link polling *************************************************************/
static void dsa_link_poll_work(struct work_struct *ugly)
{
	struct dsa_switch_tree *dst;
	int i;

	dst = container_of(ugly, struct dsa_switch_tree, link_poll_work);

	for (i = 0; i < dst->pd->nr_chips; i++) {
		struct dsa_switch *ds = dst->ds[i];

		if (ds != NULL && ds->drv->poll_link != NULL)
			ds->drv->poll_link(ds);
	}

	mod_timer(&dst->link_poll_timer, round_jiffies(jiffies + HZ));
}

static void dsa_link_poll_timer(unsigned long _dst)
{
	struct dsa_switch_tree *dst = (void *)_dst;

	schedule_work(&dst->link_poll_work);
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

static struct mii_bus *dev_to_mii_bus(struct device *dev)
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

static struct net_device *dev_to_net_device(struct device *dev)
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

#ifdef CONFIG_OF
static int dsa_of_setup_routing_table(struct dsa_platform_data *pd,
					struct dsa_chip_data *cd,
					int chip_index,
					struct device_node *link)
{
	int ret;
	const __be32 *reg;
	int link_port_addr;
	int link_sw_addr;
	struct device_node *parent_sw;
	int len;

	parent_sw = of_get_parent(link);
	if (!parent_sw)
		return -EINVAL;

	reg = of_get_property(parent_sw, "reg", &len);
	if (!reg || (len != sizeof(*reg) * 2))
		return -EINVAL;

	link_sw_addr = be32_to_cpup(reg + 1);

	if (link_sw_addr >= pd->nr_chips)
		return -EINVAL;

	/* First time routing table allocation */
	if (!cd->rtable) {
		cd->rtable = kmalloc(pd->nr_chips * sizeof(s8), GFP_KERNEL);
		if (!cd->rtable)
			return -ENOMEM;

		/* default to no valid uplink/downlink */
		memset(cd->rtable, -1, pd->nr_chips * sizeof(s8));
	}

	reg = of_get_property(link, "reg", NULL);
	if (!reg) {
		ret = -EINVAL;
		goto out;
	}

	link_port_addr = be32_to_cpup(reg);

	cd->rtable[link_sw_addr] = link_port_addr;

	return 0;
out:
	kfree(cd->rtable);
	return ret;
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
		kfree(pd->chip[i].rtable);
	}
	kfree(pd->chip);
}

static int dsa_of_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *child, *mdio, *ethernet, *port, *link;
	struct mii_bus *mdio_bus;
	struct platform_device *ethernet_dev;
	struct dsa_platform_data *pd;
	struct dsa_chip_data *cd;
	const char *port_name;
	int chip_index, port_index;
	const unsigned int *sw_addr, *port_reg;
	int ret;

	mdio = of_parse_phandle(np, "dsa,mii-bus", 0);
	if (!mdio)
		return -EINVAL;

	mdio_bus = of_mdio_find_bus(mdio);
	if (!mdio_bus)
		return -EINVAL;

	ethernet = of_parse_phandle(np, "dsa,ethernet", 0);
	if (!ethernet)
		return -EINVAL;

	ethernet_dev = of_find_device_by_node(ethernet);
	if (!ethernet_dev)
		return -ENODEV;

	pd = kzalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;

	pdev->dev.platform_data = pd;
	pd->netdev = &ethernet_dev->dev;
	pd->nr_chips = of_get_child_count(np);
	if (pd->nr_chips > DSA_MAX_SWITCHES)
		pd->nr_chips = DSA_MAX_SWITCHES;

	pd->chip = kzalloc(pd->nr_chips * sizeof(struct dsa_chip_data),
			GFP_KERNEL);
	if (!pd->chip) {
		ret = -ENOMEM;
		goto out_free;
	}

	chip_index = -1;
	for_each_available_child_of_node(np, child) {
		chip_index++;
		cd = &pd->chip[chip_index];

		cd->mii_bus = &mdio_bus->dev;

		sw_addr = of_get_property(child, "reg", NULL);
		if (!sw_addr)
			continue;

		cd->sw_addr = be32_to_cpup(sw_addr);
		if (cd->sw_addr > PHY_MAX_ADDR)
			continue;

		for_each_available_child_of_node(child, port) {
			port_reg = of_get_property(port, "reg", NULL);
			if (!port_reg)
				continue;

			port_index = be32_to_cpup(port_reg);

			port_name = of_get_property(port, "label", NULL);
			if (!port_name)
				continue;

			cd->port_names[port_index] = kstrdup(port_name,
					GFP_KERNEL);
			if (!cd->port_names[port_index]) {
				ret = -ENOMEM;
				goto out_free_chip;
			}

			link = of_parse_phandle(port, "link", 0);

			if (!strcmp(port_name, "dsa") && link &&
					pd->nr_chips > 1) {
				ret = dsa_of_setup_routing_table(pd, cd,
						chip_index, link);
				if (ret)
					goto out_free_chip;
			}

			if (port_index == DSA_MAX_PORTS)
				break;
		}
	}

	return 0;

out_free_chip:
	dsa_of_free_platform_data(pd);
out_free:
	kfree(pd);
	pdev->dev.platform_data = NULL;
	return ret;
}

static void dsa_of_remove(struct platform_device *pdev)
{
	struct dsa_platform_data *pd = pdev->dev.platform_data;

	if (!pdev->dev.of_node)
		return;

	dsa_of_free_platform_data(pd);
	kfree(pd);
}
#else
static inline int dsa_of_probe(struct platform_device *pdev)
{
	return 0;
}

static inline void dsa_of_remove(struct platform_device *pdev)
{
}
#endif

static int dsa_probe(struct platform_device *pdev)
{
	static int dsa_version_printed;
	struct dsa_platform_data *pd = pdev->dev.platform_data;
	struct net_device *dev;
	struct dsa_switch_tree *dst;
	int i, ret;

	if (!dsa_version_printed++)
		printk(KERN_NOTICE "Distributed Switch Architecture "
			"driver version %s\n", dsa_driver_version);

	if (pdev->dev.of_node) {
		ret = dsa_of_probe(pdev);
		if (ret)
			return ret;

		pd = pdev->dev.platform_data;
	}

	if (pd == NULL || pd->netdev == NULL)
		return -EINVAL;

	dev = dev_to_net_device(pd->netdev);
	if (dev == NULL) {
		ret = -EINVAL;
		goto out;
	}

	if (dev->dsa_ptr != NULL) {
		dev_put(dev);
		ret = -EEXIST;
		goto out;
	}

	dst = kzalloc(sizeof(*dst), GFP_KERNEL);
	if (dst == NULL) {
		dev_put(dev);
		ret = -ENOMEM;
		goto out;
	}

	platform_set_drvdata(pdev, dst);

	dst->pd = pd;
	dst->master_netdev = dev;
	dst->cpu_switch = -1;
	dst->cpu_port = -1;

	for (i = 0; i < pd->nr_chips; i++) {
		struct mii_bus *bus;
		struct dsa_switch *ds;

		bus = dev_to_mii_bus(pd->chip[i].mii_bus);
		if (bus == NULL) {
			printk(KERN_ERR "%s[%d]: no mii bus found for "
				"dsa switch\n", dev->name, i);
			continue;
		}

		ds = dsa_switch_setup(dst, i, &pdev->dev, bus);
		if (IS_ERR(ds)) {
			printk(KERN_ERR "%s[%d]: couldn't create dsa switch "
				"instance (error %ld)\n", dev->name, i,
				PTR_ERR(ds));
			continue;
		}

		dst->ds[i] = ds;
		if (ds->drv->poll_link != NULL)
			dst->link_poll_needed = 1;
	}

	/*
	 * If we use a tagging format that doesn't have an ethertype
	 * field, make sure that all packets from this point on get
	 * sent to the tag format's receive function.
	 */
	wmb();
	dev->dsa_ptr = (void *)dst;

	if (dst->link_poll_needed) {
		INIT_WORK(&dst->link_poll_work, dsa_link_poll_work);
		init_timer(&dst->link_poll_timer);
		dst->link_poll_timer.data = (unsigned long)dst;
		dst->link_poll_timer.function = dsa_link_poll_timer;
		dst->link_poll_timer.expires = round_jiffies(jiffies + HZ);
		add_timer(&dst->link_poll_timer);
	}

	return 0;

out:
	dsa_of_remove(pdev);

	return ret;
}

static int dsa_remove(struct platform_device *pdev)
{
	struct dsa_switch_tree *dst = platform_get_drvdata(pdev);
	int i;

	if (dst->link_poll_needed)
		del_timer_sync(&dst->link_poll_timer);

	flush_work(&dst->link_poll_work);

	for (i = 0; i < dst->pd->nr_chips; i++) {
		struct dsa_switch *ds = dst->ds[i];

		if (ds != NULL)
			dsa_switch_destroy(ds);
	}

	dsa_of_remove(pdev);

	return 0;
}

static void dsa_shutdown(struct platform_device *pdev)
{
}

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
		.owner	= THIS_MODULE,
		.of_match_table = dsa_of_match_table,
	},
};

static int __init dsa_init_module(void)
{
	int rc;

	rc = platform_driver_register(&dsa_driver);
	if (rc)
		return rc;

#ifdef CONFIG_NET_DSA_TAG_DSA
	dev_add_pack(&dsa_packet_type);
#endif
#ifdef CONFIG_NET_DSA_TAG_EDSA
	dev_add_pack(&edsa_packet_type);
#endif
#ifdef CONFIG_NET_DSA_TAG_TRAILER
	dev_add_pack(&trailer_packet_type);
#endif
	return 0;
}
module_init(dsa_init_module);

static void __exit dsa_cleanup_module(void)
{
#ifdef CONFIG_NET_DSA_TAG_TRAILER
	dev_remove_pack(&trailer_packet_type);
#endif
#ifdef CONFIG_NET_DSA_TAG_EDSA
	dev_remove_pack(&edsa_packet_type);
#endif
#ifdef CONFIG_NET_DSA_TAG_DSA
	dev_remove_pack(&dsa_packet_type);
#endif
	platform_driver_unregister(&dsa_driver);
}
module_exit(dsa_cleanup_module);

MODULE_AUTHOR("Lennert Buytenhek <buytenh@wantstofly.org>");
MODULE_DESCRIPTION("Driver for Distributed Switch Architecture switch chips");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:dsa");
