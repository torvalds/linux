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
#include <linux/sysfs.h>
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
dsa_switch_probe(struct device *host_dev, int sw_addr, char **_name)
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

		name = drv->probe(host_dev, sw_addr);
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
static int dsa_switch_setup_one(struct dsa_switch *ds, struct device *parent)
{
	struct dsa_switch_driver *drv = ds->drv;
	struct dsa_switch_tree *dst = ds->dst;
	struct dsa_chip_data *pd = ds->pd;
	bool valid_name_found = false;
	int index = ds->index;
	int i, ret;

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
				netdev_err(dst->master_netdev,
					   "multiple cpu ports?!\n");
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

	/* Make the built-in MII bus mask match the number of ports,
	 * switch drivers can override this later
	 */
	ds->phys_mii_mask = ds->phys_port_mask;

	/*
	 * If the CPU connects to this switch, set the switch tree
	 * tagging protocol to the preferred tagging format of this
	 * switch.
	 */
	if (dst->cpu_switch == index) {
		switch (ds->tag_protocol) {
#ifdef CONFIG_NET_DSA_TAG_DSA
		case DSA_TAG_PROTO_DSA:
			dst->rcv = dsa_netdev_ops.rcv;
			break;
#endif
#ifdef CONFIG_NET_DSA_TAG_EDSA
		case DSA_TAG_PROTO_EDSA:
			dst->rcv = edsa_netdev_ops.rcv;
			break;
#endif
#ifdef CONFIG_NET_DSA_TAG_TRAILER
		case DSA_TAG_PROTO_TRAILER:
			dst->rcv = trailer_netdev_ops.rcv;
			break;
#endif
#ifdef CONFIG_NET_DSA_TAG_BRCM
		case DSA_TAG_PROTO_BRCM:
			dst->rcv = brcm_netdev_ops.rcv;
			break;
#endif
		case DSA_TAG_PROTO_NONE:
			break;
		default:
			ret = -ENOPROTOOPT;
			goto out;
		}

		dst->tag_protocol = ds->tag_protocol;
	}

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
		if (!(ds->phys_port_mask & (1 << i)))
			continue;

		ret = dsa_slave_create(ds, parent, i, pd->port_names[i]);
		if (ret < 0) {
			netdev_err(dst->master_netdev, "[%d]: can't create dsa slave device for port %d(%s)\n",
				   index, i, pd->port_names[i]);
			ret = 0;
		}
	}

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

out_free:
	mdiobus_free(ds->slave_mii_bus);
out:
	kfree(ds);
	return ret;
}

static struct dsa_switch *
dsa_switch_setup(struct dsa_switch_tree *dst, int index,
		 struct device *parent, struct device *host_dev)
{
	struct dsa_chip_data *pd = dst->pd->chip + index;
	struct dsa_switch_driver *drv;
	struct dsa_switch *ds;
	int ret;
	char *name;

	/*
	 * Probe for switch model.
	 */
	drv = dsa_switch_probe(host_dev, pd->sw_addr, &name);
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
	ds = kzalloc(sizeof(*ds) + drv->priv_size, GFP_KERNEL);
	if (ds == NULL)
		return NULL;

	ds->dst = dst;
	ds->index = index;
	ds->pd = pd;
	ds->drv = drv;
	ds->tag_protocol = drv->tag_protocol;
	ds->master_dev = host_dev;

	ret = dsa_switch_setup_one(ds, parent);
	if (ret)
		return NULL;

	return ds;
}

static void dsa_switch_destroy(struct dsa_switch *ds)
{
#ifdef CONFIG_NET_DSA_HWMON
	if (ds->hwmon_dev)
		hwmon_device_unregister(ds->hwmon_dev);
#endif
}

#ifdef CONFIG_PM_SLEEP
static int dsa_switch_suspend(struct dsa_switch *ds)
{
	int i, ret = 0;

	/* Suspend slave network devices */
	for (i = 0; i < DSA_MAX_PORTS; i++) {
		if (!dsa_is_port_initialized(ds, i))
			continue;

		ret = dsa_slave_suspend(ds->ports[i]);
		if (ret)
			return ret;
	}

	if (ds->drv->suspend)
		ret = ds->drv->suspend(ds);

	return ret;
}

static int dsa_switch_resume(struct dsa_switch *ds)
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

		ret = dsa_slave_resume(ds->ports[i]);
		if (ret)
			return ret;
	}

	return 0;
}
#endif


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

	/* First time routing table allocation */
	if (!cd->rtable) {
		cd->rtable = kmalloc_array(pd->nr_chips, sizeof(s8),
					   GFP_KERNEL);
		if (!cd->rtable)
			return -ENOMEM;

		/* default to no valid uplink/downlink */
		memset(cd->rtable, -1, pd->nr_chips * sizeof(s8));
	}

	cd->rtable[link_sw_addr] = port_index;

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
		kfree(pd->chip[i].rtable);
	}
	kfree(pd->chip);
}

static int dsa_of_probe(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct device_node *child, *mdio, *ethernet, *port, *link;
	struct mii_bus *mdio_bus;
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
	if (!ethernet)
		return -EINVAL;

	ethernet_dev = of_find_net_device_by_node(ethernet);
	if (!ethernet_dev)
		return -EPROBE_DEFER;

	pd = kzalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;

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
		chip_index++;
		cd = &pd->chip[chip_index];

		cd->of_node = child;
		cd->host_dev = &mdio_bus->dev;

		sw_addr = of_get_property(child, "reg", NULL);
		if (!sw_addr)
			continue;

		cd->sw_addr = be32_to_cpup(sw_addr);
		if (cd->sw_addr > PHY_MAX_ADDR)
			continue;

		if (!of_property_read_u32(child, "eeprom-length", &eeprom_len))
			cd->eeprom_len = eeprom_len;

		for_each_available_child_of_node(child, port) {
			port_reg = of_get_property(port, "reg", NULL);
			if (!port_reg)
				continue;

			port_index = be32_to_cpup(port_reg);

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

			link = of_parse_phandle(port, "link", 0);

			if (!strcmp(port_name, "dsa") && link &&
					pd->nr_chips > 1) {
				ret = dsa_of_setup_routing_table(pd, cd,
						chip_index, port_index, link);
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
	dev->platform_data = NULL;
	return ret;
}

static void dsa_of_remove(struct device *dev)
{
	struct dsa_platform_data *pd = dev->platform_data;

	if (!dev->of_node)
		return;

	dsa_of_free_platform_data(pd);
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

static void dsa_setup_dst(struct dsa_switch_tree *dst, struct net_device *dev,
			  struct device *parent, struct dsa_platform_data *pd)
{
	int i;

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

	dst = kzalloc(sizeof(*dst), GFP_KERNEL);
	if (dst == NULL) {
		dev_put(dev);
		ret = -ENOMEM;
		goto out;
	}

	platform_set_drvdata(pdev, dst);

	dsa_setup_dst(dst, dev, &pdev->dev, pd);

	return 0;

out:
	dsa_of_remove(&pdev->dev);

	return ret;
}

static void dsa_remove_dst(struct dsa_switch_tree *dst)
{
	int i;

	if (dst->link_poll_needed)
		del_timer_sync(&dst->link_poll_timer);

	flush_work(&dst->link_poll_work);

	for (i = 0; i < dst->pd->nr_chips; i++) {
		struct dsa_switch *ds = dst->ds[i];

		if (ds != NULL)
			dsa_switch_destroy(ds);
	}
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
	{ .compatible = "brcm,bcm7445-switch-v4.0" },
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
