// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Handling of a master device, switching frames via its switch fabric CPU port
 *
 * Copyright (c) 2017 Savoir-faire Linux Inc.
 *	Vivien Didelot <vivien.didelot@savoirfairelinux.com>
 */

#include "dsa_priv.h"

static int dsa_master_get_regs_len(struct net_device *dev)
{
	struct dsa_port *cpu_dp = dev->dsa_ptr;
	const struct ethtool_ops *ops = cpu_dp->orig_ethtool_ops;
	struct dsa_switch *ds = cpu_dp->ds;
	int port = cpu_dp->index;
	int ret = 0;
	int len;

	if (ops->get_regs_len) {
		len = ops->get_regs_len(dev);
		if (len < 0)
			return len;
		ret += len;
	}

	ret += sizeof(struct ethtool_drvinfo);
	ret += sizeof(struct ethtool_regs);

	if (ds->ops->get_regs_len) {
		len = ds->ops->get_regs_len(ds, port);
		if (len < 0)
			return len;
		ret += len;
	}

	return ret;
}

static void dsa_master_get_regs(struct net_device *dev,
				struct ethtool_regs *regs, void *data)
{
	struct dsa_port *cpu_dp = dev->dsa_ptr;
	const struct ethtool_ops *ops = cpu_dp->orig_ethtool_ops;
	struct dsa_switch *ds = cpu_dp->ds;
	struct ethtool_drvinfo *cpu_info;
	struct ethtool_regs *cpu_regs;
	int port = cpu_dp->index;
	int len;

	if (ops->get_regs_len && ops->get_regs) {
		len = ops->get_regs_len(dev);
		if (len < 0)
			return;
		regs->len = len;
		ops->get_regs(dev, regs, data);
		data += regs->len;
	}

	cpu_info = (struct ethtool_drvinfo *)data;
	strlcpy(cpu_info->driver, "dsa", sizeof(cpu_info->driver));
	data += sizeof(*cpu_info);
	cpu_regs = (struct ethtool_regs *)data;
	data += sizeof(*cpu_regs);

	if (ds->ops->get_regs_len && ds->ops->get_regs) {
		len = ds->ops->get_regs_len(ds, port);
		if (len < 0)
			return;
		cpu_regs->len = len;
		ds->ops->get_regs(ds, port, cpu_regs, data);
	}
}

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
	int mcount = 0, count, i;
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
		if (count < 0)
			return;
		for (i = 0; i < count; i++) {
			memmove(ndata + (i * len + sizeof(pfx)),
				ndata + i * len, len - sizeof(pfx));
			memcpy(ndata + i * len, pfx, sizeof(pfx));
		}
	}
}

static int dsa_master_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct dsa_port *cpu_dp = dev->dsa_ptr;
	struct dsa_switch *ds = cpu_dp->ds;
	struct dsa_switch_tree *dst;
	int err = -EOPNOTSUPP;
	struct dsa_port *dp;

	dst = ds->dst;

	switch (cmd) {
	case SIOCGHWTSTAMP:
	case SIOCSHWTSTAMP:
		/* Deny PTP operations on master if there is at least one
		 * switch in the tree that is PTP capable.
		 */
		list_for_each_entry(dp, &dst->ports, list)
			if (dp->ds->ops->port_hwtstamp_get ||
			    dp->ds->ops->port_hwtstamp_set)
				return -EBUSY;
		break;
	}

	if (dev->netdev_ops->ndo_do_ioctl)
		err = dev->netdev_ops->ndo_do_ioctl(dev, ifr, cmd);

	return err;
}

static const struct dsa_netdevice_ops dsa_netdev_ops = {
	.ndo_do_ioctl = dsa_master_ioctl,
};

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

	ops->get_regs_len = dsa_master_get_regs_len;
	ops->get_regs = dsa_master_get_regs;
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

static void dsa_netdev_ops_set(struct net_device *dev,
			       const struct dsa_netdevice_ops *ops)
{
	dev->dsa_ptr->netdev_ops = ops;
}

static void dsa_master_set_promiscuity(struct net_device *dev, int inc)
{
	const struct dsa_device_ops *ops = dev->dsa_ptr->tag_ops;

	if (!ops->promisc_on_master)
		return;

	rtnl_lock();
	dev_set_promiscuity(dev, inc);
	rtnl_unlock();
}

static ssize_t tagging_show(struct device *d, struct device_attribute *attr,
			    char *buf)
{
	struct net_device *dev = to_net_dev(d);
	struct dsa_port *cpu_dp = dev->dsa_ptr;

	return sprintf(buf, "%s\n",
		       dsa_tag_protocol_to_str(cpu_dp->tag_ops));
}

static ssize_t tagging_store(struct device *d, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	const struct dsa_device_ops *new_tag_ops, *old_tag_ops;
	struct net_device *dev = to_net_dev(d);
	struct dsa_port *cpu_dp = dev->dsa_ptr;
	int err;

	old_tag_ops = cpu_dp->tag_ops;
	new_tag_ops = dsa_find_tagger_by_name(buf);
	/* Bad tagger name, or module is not loaded? */
	if (IS_ERR(new_tag_ops))
		return PTR_ERR(new_tag_ops);

	if (new_tag_ops == old_tag_ops)
		/* Drop the temporarily held duplicate reference, since
		 * the DSA switch tree uses this tagger.
		 */
		goto out;

	err = dsa_tree_change_tag_proto(cpu_dp->ds->dst, dev, new_tag_ops,
					old_tag_ops);
	if (err) {
		/* On failure the old tagger is restored, so we don't need the
		 * driver for the new one.
		 */
		dsa_tag_driver_put(new_tag_ops);
		return err;
	}

	/* On success we no longer need the module for the old tagging protocol
	 */
out:
	dsa_tag_driver_put(old_tag_ops);
	return count;
}
static DEVICE_ATTR_RW(tagging);

static struct attribute *dsa_slave_attrs[] = {
	&dev_attr_tagging.attr,
	NULL
};

static const struct attribute_group dsa_group = {
	.name	= "dsa",
	.attrs	= dsa_slave_attrs,
};

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
	const struct dsa_device_ops *tag_ops = cpu_dp->tag_ops;
	struct dsa_switch *ds = cpu_dp->ds;
	struct device_link *consumer_link;
	int mtu, ret;

	mtu = ETH_DATA_LEN + dsa_tag_protocol_overhead(tag_ops);

	/* The DSA master must use SET_NETDEV_DEV for this to work. */
	consumer_link = device_link_add(ds->dev, dev->dev.parent,
					DL_FLAG_AUTOREMOVE_CONSUMER);
	if (!consumer_link)
		netdev_err(dev,
			   "Failed to create a device link to DSA switch %s\n",
			   dev_name(ds->dev));

	rtnl_lock();
	ret = dev_set_mtu(dev, mtu);
	rtnl_unlock();
	if (ret)
		netdev_warn(dev, "error %d setting MTU to %d to include DSA overhead\n",
			    ret, mtu);

	/* If we use a tagging format that doesn't have an ethertype
	 * field, make sure that all packets from this point on get
	 * sent to the tag format's receive function.
	 */
	wmb();

	dev->dsa_ptr = cpu_dp;
	lockdep_set_class(&dev->addr_list_lock,
			  &dsa_master_addr_list_lock_key);

	dsa_master_set_promiscuity(dev, 1);

	ret = dsa_master_ethtool_setup(dev);
	if (ret)
		goto out_err_reset_promisc;

	dsa_netdev_ops_set(dev, &dsa_netdev_ops);

	ret = sysfs_create_group(&dev->dev.kobj, &dsa_group);
	if (ret)
		goto out_err_ndo_teardown;

	return ret;

out_err_ndo_teardown:
	dsa_netdev_ops_set(dev, NULL);
	dsa_master_ethtool_teardown(dev);
out_err_reset_promisc:
	dsa_master_set_promiscuity(dev, -1);
	return ret;
}

void dsa_master_teardown(struct net_device *dev)
{
	sysfs_remove_group(&dev->dev.kobj, &dsa_group);
	dsa_netdev_ops_set(dev, NULL);
	dsa_master_ethtool_teardown(dev);
	dsa_master_reset_mtu(dev);
	dsa_master_set_promiscuity(dev, -1);

	dev->dsa_ptr = NULL;

	/* If we used a tagging format that doesn't have an ethertype
	 * field, make sure that all packets from this point get sent
	 * without the tag and go through the regular receive path.
	 */
	wmb();
}
