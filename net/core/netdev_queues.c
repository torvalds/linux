// SPDX-License-Identifier: GPL-2.0-or-later

#include <net/netdev_queues.h>

/**
 * netdev_queue_get_dma_dev() - get dma device for zero-copy operations
 * @dev:	net_device
 * @idx:	queue index
 *
 * Get dma device for zero-copy operations to be used for this queue.
 * When such device is not available or valid, the function will return NULL.
 *
 * Return: Device or NULL on error
 */
struct device *netdev_queue_get_dma_dev(struct net_device *dev, int idx)
{
	const struct netdev_queue_mgmt_ops *queue_ops = dev->queue_mgmt_ops;
	struct device *dma_dev;

	if (queue_ops && queue_ops->ndo_queue_get_dma_dev)
		dma_dev = queue_ops->ndo_queue_get_dma_dev(dev, idx);
	else
		dma_dev = dev->dev.parent;

	return dma_dev && dma_dev->dma_mask ? dma_dev : NULL;
}

