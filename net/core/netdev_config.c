// SPDX-License-Identifier: GPL-2.0-only

#include <linux/netdevice.h>
#include <net/netdev_queues.h>
#include <net/netdev_rx_queue.h>

/**
 * netdev_queue_config() - get configuration for a given queue
 * @dev:      net_device instance
 * @rxq_idx:  index of the queue of interest
 * @qcfg: queue configuration struct (output)
 *
 * Render the configuration for a given queue. This helper should be used
 * by drivers which support queue configuration to retrieve config for
 * a particular queue.
 *
 * @qcfg is an output parameter and is always fully initialized by this
 * function. Some values may not be set by the user, drivers may either
 * deal with the "unset" values in @qcfg, or provide the callback
 * to populate defaults in queue_management_ops.
 */
void netdev_queue_config(struct net_device *dev, int rxq_idx,
			 struct netdev_queue_config *qcfg)
{
	struct netdev_queue_config *stored;

	memset(qcfg, 0, sizeof(*qcfg));

	stored = &__netif_get_rx_queue(dev, rxq_idx)->qcfg;
	qcfg->rx_page_size = stored->rx_page_size;
}
EXPORT_SYMBOL(netdev_queue_config);
