// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * This file contains all networking devres helpers.
 */

#include <linux/device.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>

static void devm_free_netdev(struct device *dev, void *res)
{
	free_netdev(*(struct net_device **)res);
}

struct net_device *devm_alloc_etherdev_mqs(struct device *dev, int sizeof_priv,
					   unsigned int txqs, unsigned int rxqs)
{
	struct net_device **dr;
	struct net_device *netdev;

	dr = devres_alloc(devm_free_netdev, sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return NULL;

	netdev = alloc_etherdev_mqs(sizeof_priv, txqs, rxqs);
	if (!netdev) {
		devres_free(dr);
		return NULL;
	}

	*dr = netdev;
	devres_add(dev, dr);

	return netdev;
}
EXPORT_SYMBOL(devm_alloc_etherdev_mqs);
