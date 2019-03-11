/*
 * Handling of a master device, switching frames via its switch fabric CPU port
 *
 * Copyright (c) 2017 Savoir-faire Linux Inc.
 *	Vivien Didelot <vivien.didelot@savoirfairelinux.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "dsa_priv.h"

static void dsa_master_get_ethtool_stats(struct net_device *dev,
					 struct ethtool_stats *stats,
					 uint64_t *data)
{
	struct dsa_port *cpu_dp = dev->dsa_ptr;
	const struct ethtool_ops *ops = cpu_dp->orig_ethtool_ops;
	struct dsa_switch *ds = cpu_dp->ds;
	int port = cpu_dp->index;
	int count = 0;

	if (ops->get_sset_count && ops->get_ethtool_stats) {
		count = ops->get_sset_count(dev, ETH_SS_STATS);
		ops->get_ethtool_stats(dev, stats, data);
	}

	if (ds->ops->get_ethtool_stats)
		ds->ops->get_ethtool_stats(ds, port, data + count);
}

static void dsa_master_get_ethtool_phy_stats(struct net_device *dev,
					     struct ethtool_stats *stats,
					     uint64_t *data)
{
	struct dsa_port *cpu_dp = dev->dsa_ptr;
	const struct ethtool_ops *ops = cpu_dp->orig_ethtool_ops;
	struct dsa_switch *ds = cpu_dp->ds;
	int port = cpu_dp->index;
	int count = 0;

	if (dev->phydev && !ops->get_ethtool_phy_stats) {
		count = phy_ethtool_get_sset_count(dev->phydev);
		if (count >= 0)
			phy_ethtool_get_stats(dev->phydev, stats, data);
	} else if (ops->get_sset_count && ops->get_ethtool_phy_stats) {
		count = ops->get_sset_count(dev, ETH_SS_PHY_STATS);
		ops->get_ethtool_phy_stats(dev, stats, data);
	}

	if (count < 0)
		count = 0;

	if (ds->ops->get_ethtool_phy_stats)
		ds->ops->get_ethtool_phy_stats(ds, port, data + count);
}

static int dsa_master_get_sset_count(struct net_device *dev, int sset)
{
	struct dsa_port *cpu_dp = dev->dsa_ptr;
	const struct ethtool_ops *ops = cpu_dp->orig_ethtool_ops;
	struct dsa_switch *ds = cpu_dp->ds;
	int count = 0;

	if (sset == ETH_SS_PHY_STATS && dev->phydev &&
	    !ops->get_ethtool_phy_stats)
		count = phy_ethtool_get_sset_count(dev->phydev);
	else if (ops->get_sset_count)
		count = ops->get_sset_count(dev, sset);

	if (count < 0)
		count = 0;

	if (ds->ops->get_sset_count)
		count += ds->ops->get_sset_count(ds, cpu_dp->index, sset);

	return count;
}

static void dsa_master_get_strings(struct net_device *dev, uint32_t stringset,
				   uint8_t *data)
{
	struct dsa_port *cpu_dp = dev->dsa_ptr;
	const struct ethtool_ops *ops = cpu_dp->orig_ethtool_ops;
	struct dsa_switch *ds = cpu_dp->ds;
	int port = cpu_dp->index;
	int len = ETH_GSTRING_LEN;
	int mcount = 0, count;
	unsigned int i;
	uint8_t pfx[4];
	uint8_t *ndata;

	snprintf(pfx, sizeof(pfx), "p%.2d", port);
	/* We do not want to be NULL-terminated, since this is a prefix */
	pfx[sizeof(pfx) - 1] = '_';

	if (stringset == ETH_SS_PHY_STATS && dev->phydev &&
	    !ops->get_ethtool_phy_stats) {
		mcount = phy_ethtool_get_sset_count(dev->phydev);
		if (mcount < 0)
			mcount = 0;
		else
			phy_ethtool_get_strings(dev->phydev, data);
	} else if (ops->get_sset_count && ops->get_strings) {
		mcount = ops->get_sset_count(dev, stringset);
		if (mcount < 0)
			mcount = 0;
		ops->get_strings(dev, stringset, data);
	}

	if (ds->ops->get_strings) {
		ndata = data + mcount * len;
		/* This function copies ETH_GSTRINGS_LEN bytes, we will mangle
		 * the output after to prepend our CPU port prefix we
		 * constructed earlier
		 */
		ds->ops->get_strings(ds, port, stringset, ndata);
		count = ds->ops->get_sset_count(ds, port, stringset);
		for (i = 0; i < count; i++) {
			memmove(ndata + (i * len + sizeof(pfx)),
				ndata + i * len, len - sizeof(pfx));
			memcpy(ndata + i * len, pfx, sizeof(pfx));
		}
	}
}

static int dsa_master_ethtool_setup(struct net_device *dev)
{
	struct dsa_port *cpu_dp = dev->dsa_ptr;
	struct dsa_switch *ds = cpu_dp->ds;
	struct ethtool_ops *ops;

	ops = devm_kzalloc(ds->dev, sizeof(*ops), GFP_KERNEL);
	if (!ops)
		return -ENOMEM;

	cpu_dp->orig_ethtool_ops = dev->ethtool_ops;
	if (cpu_dp->orig_ethtool_ops)
		memcpy(ops, cpu_dp->orig_ethtool_ops, sizeof(*ops));

	ops->get_sset_count = dsa_master_get_sset_count;
	ops->get_ethtool_stats = dsa_master_get_ethtool_stats;
	ops->get_strings = dsa_master_get_strings;
	ops->get_ethtool_phy_stats = dsa_master_get_ethtool_phy_stats;

	dev->ethtool_ops = ops;

	return 0;
}

static void dsa_master_ethtool_teardown(struct net_device *dev)
{
	struct dsa_port *cpu_dp = dev->dsa_ptr;

	dev->ethtool_ops = cpu_dp->orig_ethtool_ops;
	cpu_dp->orig_ethtool_ops = NULL;
}

static ssize_t tagging_show(struct device *d, struct device_attribute *attr,
			    char *buf)
{
	struct net_device *dev = to_net_dev(d);
	struct dsa_port *cpu_dp = dev->dsa_ptr;

	return sprintf(buf, "%s\n",
		       dsa_tag_protocol_to_str(cpu_dp->tag_ops));
}
static DEVICE_ATTR_RO(tagging);

static struct attribute *dsa_slave_attrs[] = {
	&dev_attr_tagging.attr,
	NULL
};

static const struct attribute_group dsa_group = {
	.name	= "dsa",
	.attrs	= dsa_slave_attrs,
};

static void dsa_master_set_mtu(struct net_device *dev, struct dsa_port *cpu_dp)
{
	unsigned int mtu = ETH_DATA_LEN + cpu_dp->tag_ops->overhead;
	int err;

	rtnl_lock();
	if (mtu <= dev->max_mtu) {
		err = dev_set_mtu(dev, mtu);
		if (err)
			netdev_dbg(dev, "Unable to set MTU to include for DSA overheads\n");
	}
	rtnl_unlock();
}

static void dsa_master_reset_mtu(struct net_device *dev)
{
	int err;

	rtnl_lock();
	err = dev_set_mtu(dev, ETH_DATA_LEN);
	if (err)
		netdev_dbg(dev,
			   "Unable to reset MTU to exclude DSA overheads\n");
	rtnl_unlock();
}

static struct lock_class_key dsa_master_addr_list_lock_key;

int dsa_master_setup(struct net_device *dev, struct dsa_port *cpu_dp)
{
	int ret;

	dsa_master_set_mtu(dev,  cpu_dp);

	/* If we use a tagging format that doesn't have an ethertype
	 * field, make sure that all packets from this point on get
	 * sent to the tag format's receive function.
	 */
	wmb();

	dev->dsa_ptr = cpu_dp;
	lockdep_set_class(&dev->addr_list_lock,
			  &dsa_master_addr_list_lock_key);

	ret = dsa_master_ethtool_setup(dev);
	if (ret)
		return ret;

	ret = sysfs_create_group(&dev->dev.kobj, &dsa_group);
	if (ret)
		dsa_master_ethtool_teardown(dev);

	return ret;
}

void dsa_master_teardown(struct net_device *dev)
{
	sysfs_remove_group(&dev->dev.kobj, &dsa_group);
	dsa_master_ethtool_teardown(dev);
	dsa_master_reset_mtu(dev);

	dev->dsa_ptr = NULL;

	/* If we used a tagging format that doesn't have an ethertype
	 * field, make sure that all packets from this point get sent
	 * without the tag and go through the regular receive path.
	 */
	wmb();
}
