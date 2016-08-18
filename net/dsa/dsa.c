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

#include <linux/ctype.h>
#include <linux/device.h>
#include <linux/hwmon.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <net/dsa.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_platform.h>
#include <linux/of_net.h>
#include <linux/of_gpio.h>
#include <linux/sysfs.h>
#include <linux/phy_fixed.h>
#include <linux/gpio/consumer.h>
#include "dsa_priv.h"

char dsa_driver_version[] = "0.1";

static struct sk_buff *dsa_slave_notag_xmit(struct sk_buff *skb,
					    struct net_device *dev)
{
	/* Just return the original SKB */
	return skb;
}

static const struct dsa_device_ops none_ops = {
	.xmit	= dsa_slave_notag_xmit,
	.rcv	= NULL,
};

const struct dsa_device_ops *dsa_device_ops[DSA_TAG_LAST] = {
#ifdef CONFIG_NET_DSA_TAG_DSA
	[DSA_TAG_PROTO_DSA] = &dsa_netdev_ops,
#endif
#ifdef CONFIG_NET_DSA_TAG_EDSA
	[DSA_TAG_PROTO_EDSA] = &edsa_netdev_ops,
#endif
#ifdef CONFIG_NET_DSA_TAG_TRAILER
	[DSA_TAG_PROTO_TRAILER] = &trailer_netdev_ops,
#endif
#ifdef CONFIG_NET_DSA_TAG_BRCM
	[DSA_TAG_PROTO_BRCM] = &brcm_netdev_ops,
#endif
	[DSA_TAG_PROTO_NONE] = &none_ops,
};

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
dsa_switch_probe(struct device *parent, struct device *host_dev, int sw_addr,
		 const char **_name, void **priv)
{
	struct dsa_switch_driver *ret;
	struct list_head *list;
	const char *name;

	ret = NULL;
	name = NULL;

	mutex_lock(&dsa_switch_drivers_mutex);
	list_for_each(list, &dsa_switch_drivers) {
		struct dsa_switch_driver *drv;

		drv = list_entry(list, struct dsa_switch_driver, list);

		name = drv->probe(parent, host_dev, sw_addr, priv);
		if (name != NULL) {
			ret = drv;
			break;
		}
	}
	mutex_unlock(&dsa_switch_drivers_mutex);

	*_name = name;

	return ret;
}

/* hwmon support ************************************************************/

#ifdef CONFIG_NET_DSA_HWMON

static ssize_t temp1_input_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct dsa_switch *ds = dev_get_drvdata(dev);
	int temp, ret;

	ret = ds->drv->get_temp(ds, &temp);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", temp * 1000);
}
static DEVICE_ATTR_RO(temp1_input);

static ssize_t temp1_max_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct dsa_switch *ds = dev_get_drvdata(dev);
	int temp, ret;

	ret = ds->drv->get_temp_limit(ds, &temp);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", temp * 1000);
}

static ssize_t temp1_max_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct dsa_switch *ds = dev_get_drvdata(dev);
	int temp, ret;

	ret = kstrtoint(buf, 0, &temp);
	if (ret < 0)
		return ret;

	ret = ds->drv->set_temp_limit(ds, DIV_ROUND_CLOSEST(temp, 1000));
	if (ret < 0)
		return ret;

	return count;
}
static DEVICE_ATTR_RW(temp1_max);

static ssize_t temp1_max_alarm_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct dsa_switch *ds = dev_get_drvdata(dev);
	bool alarm;
	int ret;

	ret = ds->drv->get_temp_alarm(ds, &alarm);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", alarm);
}
static DEVICE_ATTR_RO(temp1_max_alarm);

static struct attribute *dsa_hwmon_attrs[] = {
	&dev_attr_temp1_input.attr,	/* 0 */
	&dev_attr_temp1_max.attr,	/* 1 */
	&dev_attr_temp1_max_alarm.attr,	/* 2 */
	NULL
};

static umode_t dsa_hwmon_attrs_visible(struct kobject *kobj,
				       struct attribute *attr, int index)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct dsa_switch *ds = dev_get_drvdata(dev);
	struct dsa_switch_driver *drv = ds->drv;
	umode_t mode = attr->mode;

	if (index == 1) {
		if (!drv->get_temp_limit)
			mode = 0;
		else if (!drv->set_temp_limit)
			mode &= ~S_IWUSR;
	} else if (index == 2 && !drv->get_temp_alarm) {
		mode = 0;
	}
	return mode;
}

static const struct attribute_group dsa_hwmon_group = {
	.attrs = dsa_hwmon_attrs,
	.is_visible = dsa_hwmon_attrs_visible,
};
__ATTRIBUTE_GROUPS(dsa_hwmon);

#endif /* CONFIG_NET_DSA_HWMON */

/* basic switch operations **************************************************/
int dsa_cpu_dsa_setup(struct dsa_switch *ds, struct device *dev,
		      struct device_node *port_dn, int port)
{
	struct phy_device *phydev;
	int ret, mode;

	if (of_phy_is_fixed_link(port_dn)) {
		ret = of_phy_register_fixed_link(port_dn);
		if (ret) {
			dev_err(dev, "failed to register fixed PHY\n");
			return ret;
		}
		phydev = of_phy_find_device(port_dn);

		mode = of_get_phy_mode(port_dn);
		if (mode < 0)
			mode = PHY_INTERFACE_MODE_NA;
		phydev->interface = mode;

		genphy_config_init(phydev);
		genphy_read_status(phydev);
		if (ds->drv->adjust_link)
			ds->drv->adjust_link(ds, port, phydev);
	}

	return 0;
}

static int dsa_cpu_dsa_setups(struct dsa_switch *ds, struct device *dev)
{
	struct device_node *port_dn;
	int ret, port;

	for (port = 0; port < DSA_MAX_PORTS; port++) {
		if (!(dsa_is_cpu_port(ds, port) || dsa_is_dsa_port(ds, port)))
			continue;

		port_dn = ds->ports[port].dn;
		ret = dsa_cpu_dsa_setup(ds, dev, port_dn, port);
		if (ret)
			return ret;
	}
	return 0;
}

const struct dsa_device_ops *dsa_resolve_tag_protocol(int tag_protocol)
{
	const struct dsa_device_ops *ops;

	if (tag_protocol >= DSA_TAG_LAST)
		return ERR_PTR(-EINVAL);
	ops = dsa_device_ops[tag_protocol];

	if (!ops)
		return ERR_PTR(-ENOPROTOOPT);

	return ops;
}

int dsa_cpu_port_ethtool_setup(struct dsa_switch *ds)
{
	struct net_device *master;
	struct ethtool_ops *cpu_ops;

	master = ds->dst->master_netdev;
	if (ds->master_netdev)
		master = ds->master_netdev;

	cpu_ops = devm_kzalloc(ds->dev, sizeof(*cpu_ops), GFP_KERNEL);
	if (!cpu_ops)
		return -ENOMEM;

	memcpy(&ds->dst->master_ethtool_ops, master->ethtool_ops,
	       sizeof(struct ethtool_ops));
	ds->dst->master_orig_ethtool_ops = master->ethtool_ops;
	memcpy(cpu_ops, &ds->dst->master_ethtool_ops,
	       sizeof(struct ethtool_ops));
	dsa_cpu_port_ethtool_init(cpu_ops);
	master->ethtool_ops = cpu_ops;

	return 0;
}

void dsa_cpu_port_ethtool_restore(struct dsa_switch *ds)
{
	struct net_device *master;

	master = ds->dst->master_netdev;
	if (ds->master_netdev)
		master = ds->master_netdev;

	master->ethtool_ops = ds->dst->master_orig_ethtool_ops;
}

static int dsa_switch_setup_one(struct dsa_switch *ds, struct device *parent)
{
	struct dsa_switch_driver *drv = ds->drv;
	struct dsa_switch_tree *dst = ds->dst;
	struct dsa_chip_data *cd = ds->cd;
	bool valid_name_found = false;
	int index = ds->index;
	int i, ret;

	/*
	 * Validate supplied switch configuration.
	 */
	for (i = 0; i < DSA_MAX_PORTS; i++) {
		char *name;

		name = cd->port_names[i];
		if (name == NULL)
			continue;

		if (!strcmp(name, "cpu")) {
			if (dst->cpu_switch != -1) {
				netdev_err(dst->master_netdev,
					   "multiple cpu ports?!\n");
				ret = -EINVAL;
				goto out;
			}
			dst->cpu_switch = index;
			dst->cpu_port = i;
			ds->cpu_port_mask |= 1 << i;
		} else if (!strcmp(name, "dsa")) {
			ds->dsa_port_mask |= 1 << i;
		} else {
			ds->enabled_port_mask |= 1 << i;
		}
		valid_name_found = true;
	}

	if (!valid_name_found && i == DSA_MAX_PORTS) {
		ret = -EINVAL;
		goto out;
	}

	/* Make the built-in MII bus mask match the number of ports,
	 * switch drivers can override this later
	 */
	ds->phys_mii_mask = ds->enabled_port_mask;

	/*
	 * If the CPU connects to this switch, set the switch tree
	 * tagging protocol to the preferred tagging format of this
	 * switch.
	 */
	if (dst->cpu_switch == index) {
		dst->tag_ops = dsa_resolve_tag_protocol(drv->tag_protocol);
		if (IS_ERR(dst->tag_ops)) {
			ret = PTR_ERR(dst->tag_ops);
			goto out;
		}

		dst->rcv = dst->tag_ops->rcv;
	}

	memcpy(ds->rtable, cd->rtable, sizeof(ds->rtable));

	/*
	 * Do basic register setup.
	 */
	ret = drv->setup(ds);
	if (ret < 0)
		goto out;

	ret = drv->set_addr(ds, dst->master_netdev->dev_addr);
	if (ret < 0)
		goto out;

	if (!ds->slave_mii_bus && drv->phy_read) {
		ds->slave_mii_bus = devm_mdiobus_alloc(parent);
		if (!ds->slave_mii_bus) {
			ret = -ENOMEM;
			goto out;
		}
		dsa_slave_mii_bus_init(ds);

		ret = mdiobus_register(ds->slave_mii_bus);
		if (ret < 0)
			goto out;
	}

	/*
	 * Create network devices for physical switch ports.
	 */
	for (i = 0; i < DSA_MAX_PORTS; i++) {
		ds->ports[i].dn = cd->port_dn[i];

		if (!(ds->enabled_port_mask & (1 << i)))
			continue;

		ret = dsa_slave_create(ds, parent, i, cd->port_names[i]);
		if (ret < 0) {
			netdev_err(dst->master_netdev, "[%d]: can't create dsa slave device for port %d(%s): %d\n",
				   index, i, cd->port_names[i], ret);
			ret = 0;
		}
	}

	/* Perform configuration of the CPU and DSA ports */
	ret = dsa_cpu_dsa_setups(ds, parent);
	if (ret < 0) {
		netdev_err(dst->master_netdev, "[%d] : can't configure CPU and DSA ports\n",
			   index);
		ret = 0;
	}

	ret = dsa_cpu_port_ethtool_setup(ds);
	if (ret)
		return ret;

#ifdef CONFIG_NET_DSA_HWMON
	/* If the switch provides a temperature sensor,
	 * register with hardware monitoring subsystem.
	 * Treat registration error as non-fatal and ignore it.
	 */
	if (drv->get_temp) {
		const char *netname = netdev_name(dst->master_netdev);
		char hname[IFNAMSIZ + 1];
		int i, j;

		/* Create valid hwmon 'name' attribute */
		for (i = j = 0; i < IFNAMSIZ && netname[i]; i++) {
			if (isalnum(netname[i]))
				hname[j++] = netname[i];
		}
		hname[j] = '\0';
		scnprintf(ds->hwmon_name, sizeof(ds->hwmon_name), "%s_dsa%d",
			  hname, index);
		ds->hwmon_dev = hwmon_device_register_with_groups(NULL,
					ds->hwmon_name, ds, dsa_hwmon_groups);
		if (IS_ERR(ds->hwmon_dev))
			ds->hwmon_dev = NULL;
	}
#endif /* CONFIG_NET_DSA_HWMON */

	return ret;

out:
	return ret;
}

static struct dsa_switch *
dsa_switch_setup(struct dsa_switch_tree *dst, int index,
		 struct device *parent, struct device *host_dev)
{
	struct dsa_chip_data *cd = dst->pd->chip + index;
	struct dsa_switch_driver *drv;
	struct dsa_switch *ds;
	int ret;
	const char *name;
	void *priv;

	/*
	 * Probe for switch model.
	 */
	drv = dsa_switch_probe(parent, host_dev, cd->sw_addr, &name, &priv);
	if (drv == NULL) {
		netdev_err(dst->master_netdev, "[%d]: could not detect attached switch\n",
			   index);
		return ERR_PTR(-EINVAL);
	}
	netdev_info(dst->master_netdev, "[%d]: detected a %s switch\n",
		    index, name);


	/*
	 * Allocate and initialise switch state.
	 */
	ds = devm_kzalloc(parent, sizeof(*ds), GFP_KERNEL);
	if (ds == NULL)
		return ERR_PTR(-ENOMEM);

	ds->dst = dst;
	ds->index = index;
	ds->cd = cd;
	ds->drv = drv;
	ds->priv = priv;
	ds->dev = parent;

	ret = dsa_switch_setup_one(ds, parent);
	if (ret)
		return ERR_PTR(ret);

	return ds;
}

void dsa_cpu_dsa_destroy(struct device_node *port_dn)
{
	struct phy_device *phydev;

	if (of_phy_is_fixed_link(port_dn)) {
		phydev = of_phy_find_device(port_dn);
		if (phydev) {
			phy_device_free(phydev);
			fixed_phy_unregister(phydev);
		}
	}
}

static void dsa_switch_destroy(struct dsa_switch *ds)
{
	int port;

#ifdef CONFIG_NET_DSA_HWMON
	if (ds->hwmon_dev)
		hwmon_device_unregister(ds->hwmon_dev);
#endif

	/* Destroy network devices for physical switch ports. */
	for (port = 0; port < DSA_MAX_PORTS; port++) {
		if (!(ds->enabled_port_mask & (1 << port)))
			continue;

		if (!ds->ports[port].netdev)
			continue;

		dsa_slave_destroy(ds->ports[port].netdev);
	}

	/* Disable configuration of the CPU and DSA ports */
	for (port = 0; port < DSA_MAX_PORTS; port++) {
		if (!(dsa_is_cpu_port(ds, port) || dsa_is_dsa_port(ds, port)))
			continue;
		dsa_cpu_dsa_destroy(ds->ports[port].dn);

		/* Clearing a bit which is not set does no harm */
		ds->cpu_port_mask |= ~(1 << port);
		ds->dsa_port_mask |= ~(1 << port);
	}

	if (ds->slave_mii_bus && ds->drv->phy_read)
		mdiobus_unregister(ds->slave_mii_bus);
}

#ifdef CONFIG_PM_SLEEP
int dsa_switch_suspend(struct dsa_switch *ds)
{
	int i, ret = 0;

	/* Suspend slave network devices */
	for (i = 0; i < DSA_MAX_PORTS; i++) {
		if (!dsa_is_port_initialized(ds, i))
			continue;

		ret = dsa_slave_suspend(ds->ports[i].netdev);
		if (ret)
			return ret;
	}

	if (ds->drv->suspend)
		ret = ds->drv->suspend(ds);

	return ret;
}
EXPORT_SYMBOL_GPL(dsa_switch_suspend);

int dsa_switch_resume(struct dsa_switch *ds)
{
	int i, ret = 0;

	if (ds->drv->resume)
		ret = ds->drv->resume(ds);

	if (ret)
		return ret;

	/* Resume slave network devices */
	for (i = 0; i < DSA_MAX_PORTS; i++) {
		if (!dsa_is_port_initialized(ds, i))
			continue;

		ret = dsa_slave_resume(ds->ports[i].netdev);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(dsa_switch_resume);
#endif

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
	dst->master_netdev = dev;
	dst->cpu_switch = -1;
	dst->cpu_port = -1;

	for (i = 0; i < pd->nr_chips; i++) {
		struct dsa_switch *ds;

		ds = dsa_switch_setup(dst, i, parent, pd->chip[i].host_dev);
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

	/*
	 * If we use a tagging format that doesn't have an ethertype
	 * field, make sure that all packets from this point on get
	 * sent to the tag format's receive function.
	 */
	wmb();
	dev->dsa_ptr = (void *)dst;

	return 0;
}

static int dsa_probe(struct platform_device *pdev)
{
	struct dsa_platform_data *pd = pdev->dev.platform_data;
	struct net_device *dev;
	struct dsa_switch_tree *dst;
	int ret;

	pr_notice_once("Distributed Switch Architecture driver version %s\n",
		       dsa_driver_version);

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
		dev = dev_to_net_device(pd->netdev);
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

	dst->master_netdev->dsa_ptr = NULL;

	/* If we used a tagging format that doesn't have an ethertype
	 * field, make sure that all packets from this point get sent
	 * without the tag and go through the regular receive path.
	 */
	wmb();

	for (i = 0; i < dst->pd->nr_chips; i++) {
		struct dsa_switch *ds = dst->ds[i];

		if (ds)
			dsa_switch_destroy(ds);
	}

	dsa_cpu_port_ethtool_restore(dst->ds[0]);

	dev_put(dst->master_netdev);
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

static int dsa_switch_rcv(struct sk_buff *skb, struct net_device *dev,
			  struct packet_type *pt, struct net_device *orig_dev)
{
	struct dsa_switch_tree *dst = dev->dsa_ptr;

	if (unlikely(dst == NULL)) {
		kfree_skb(skb);
		return 0;
	}

	return dst->rcv(skb, dev, pt, orig_dev);
}

static struct packet_type dsa_pack_type __read_mostly = {
	.type	= cpu_to_be16(ETH_P_XDSA),
	.func	= dsa_switch_rcv,
};

static struct notifier_block dsa_netdevice_nb __read_mostly = {
	.notifier_call	= dsa_slave_netdevice_event,
};

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

static int __init dsa_init_module(void)
{
	int rc;

	register_netdevice_notifier(&dsa_netdevice_nb);

	rc = platform_driver_register(&dsa_driver);
	if (rc)
		return rc;

	dev_add_pack(&dsa_pack_type);

	return 0;
}
module_init(dsa_init_module);

static void __exit dsa_cleanup_module(void)
{
	unregister_netdevice_notifier(&dsa_netdevice_nb);
	dev_remove_pack(&dsa_pack_type);
	platform_driver_unregister(&dsa_driver);
}
module_exit(dsa_cleanup_module);

MODULE_AUTHOR("Lennert Buytenhek <buytenh@wantstofly.org>");
MODULE_DESCRIPTION("Driver for Distributed Switch Architecture switch chips");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:dsa");
