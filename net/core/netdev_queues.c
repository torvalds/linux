// SPDX-License-Identifier: GPL-2.0-or-later

#include <net/netdev_queues.h>
#include <net/netdev_rx_queue.h>
#include <net/xdp_sock_drv.h>

/**
 * netdev_queue_get_dma_dev() - get dma device for zero-copy operations
 * @dev:	net_device
 * @idx:	queue index
 *
 * Get dma device for zero-copy operations to be used for this queue. If the
 * queue is leased to a physical queue, we retrieve the latter's dma device.
 * When such device is not available or valid, the function will return NULL.
 *
 * Return: Device or NULL on error
 */
struct device *netdev_queue_get_dma_dev(struct net_device *dev, int idx)
{
	const struct netdev_queue_mgmt_ops *queue_ops;
	struct device *dma_dev;

	if (idx < dev->real_num_rx_queues) {
		struct netdev_rx_queue *rxq = __netif_get_rx_queue(dev, idx);

		if (rxq->lease) {
			rxq = rxq->lease;
			dev = rxq->dev;
			idx = get_netdev_rx_queue_index(rxq);
		}
	}

	queue_ops = dev->queue_mgmt_ops;

	if (queue_ops && queue_ops->ndo_queue_get_dma_dev)
		dma_dev = queue_ops->ndo_queue_get_dma_dev(dev, idx);
	else
		dma_dev = dev->dev.parent;

	return dma_dev && dma_dev->dma_mask ? dma_dev : NULL;
}

bool netdev_can_create_queue(const struct net_device *dev,
			     struct netlink_ext_ack *extack)
{
	if (dev->dev.parent) {
		NL_SET_ERR_MSG(extack, "Device is not a virtual device");
		return false;
	}
	if (!dev->queue_mgmt_ops ||
	    !dev->queue_mgmt_ops->ndo_queue_create) {
		NL_SET_ERR_MSG(extack, "Device does not support queue creation");
		return false;
	}
	if (dev->real_num_rx_queues < 1 ||
	    dev->real_num_tx_queues < 1) {
		NL_SET_ERR_MSG(extack, "Device must have at least one real queue");
		return false;
	}
	return true;
}

bool netdev_can_lease_queue(const struct net_device *dev,
			    struct netlink_ext_ack *extack)
{
	if (!dev->dev.parent) {
		NL_SET_ERR_MSG(extack, "Lease device is a virtual device");
		return false;
	}
	if (!netif_device_present(dev)) {
		NL_SET_ERR_MSG(extack, "Lease device has been removed from the system");
		return false;
	}
	if (!dev->queue_mgmt_ops) {
		NL_SET_ERR_MSG(extack, "Lease device does not support queue management operations");
		return false;
	}
	return true;
}

bool netdev_queue_busy(struct net_device *dev, int idx,
		       struct netlink_ext_ack *extack)
{
	if (netif_rxq_is_leased(dev, idx)) {
		NL_SET_ERR_MSG(extack, "Lease device queue is already leased");
		return true;
	}
	if (xsk_get_pool_from_qid(dev, idx)) {
		NL_SET_ERR_MSG(extack, "Lease device queue in use by AF_XDP");
		return true;
	}
	if (netif_rxq_has_mp(dev, idx)) {
		NL_SET_ERR_MSG(extack, "Lease device queue in use by memory provider");
		return true;
	}
	return false;
}
