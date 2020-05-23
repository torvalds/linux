// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * This file contains all networking devres helpers.
 */

#include <linux/device.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>

struct net_device_devres {
	struct net_device *ndev;
};

static void devm_free_netdev(struct device *dev, void *this)
{
	struct net_device_devres *res = this;

	free_netdev(res->ndev);
}

struct net_device *devm_alloc_etherdev_mqs(struct device *dev, int sizeof_priv,
					   unsigned int txqs, unsigned int rxqs)
{
	struct net_device_devres *dr;

	dr = devres_alloc(devm_free_netdev, sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return NULL;

	dr->ndev = alloc_etherdev_mqs(sizeof_priv, txqs, rxqs);
	if (!dr->ndev) {
		devres_free(dr);
		return NULL;
	}

	devres_add(dev, dr);

	return dr->ndev;
}
EXPORT_SYMBOL(devm_alloc_etherdev_mqs);
