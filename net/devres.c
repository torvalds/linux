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

static void devm_unregister_netdev(struct device *dev, void *this)
{
	struct net_device_devres *res = this;

	unregister_netdev(res->ndev);
}

static int netdev_devres_match(struct device *dev, void *this, void *match_data)
{
	struct net_device_devres *res = this;
	struct net_device *ndev = match_data;

	return ndev == res->ndev;
}

/**
 *	devm_register_netdev - resource managed variant of register_netdev()
 *	@dev: managing device for this netdev - usually the parent device
 *	@ndev: device to register
 *
 *	This is a devres variant of register_netdev() for which the unregister
 *	function will be called automatically when the managing device is
 *	detached. Note: the net_device used must also be resource managed by
 *	the same struct device.
 */
int devm_register_netdev(struct device *dev, struct net_device *ndev)
{
	struct net_device_devres *dr;
	int ret;

	/* struct net_device must itself be managed. For now a managed netdev
	 * can only be allocated by devm_alloc_etherdev_mqs() so the check is
	 * straightforward.
	 */
	if (WARN_ON(!devres_find(dev, devm_free_netdev,
				 netdev_devres_match, ndev)))
		return -EINVAL;

	dr = devres_alloc(devm_unregister_netdev, sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	ret = register_netdev(ndev);
	if (ret) {
		devres_free(dr);
		return ret;
	}

	dr->ndev = ndev;
	devres_add(ndev->dev.parent, dr);

	return 0;
}
EXPORT_SYMBOL(devm_register_netdev);
