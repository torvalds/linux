// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/netdevice.h>
#include <net/netdev_queues.h>
#include <net/netdev_rx_queue.h>
#include <net/page_pool/memory_provider.h>

#include "page_pool_priv.h"

int netdev_rx_queue_restart(struct net_device *dev, unsigned int rxq_idx)
{
	struct netdev_rx_queue *rxq = __netif_get_rx_queue(dev, rxq_idx);
	void *new_mem, *old_mem;
	int err;

	if (!dev->queue_mgmt_ops || !dev->queue_mgmt_ops->ndo_queue_stop ||
	    !dev->queue_mgmt_ops->ndo_queue_mem_free ||
	    !dev->queue_mgmt_ops->ndo_queue_mem_alloc ||
	    !dev->queue_mgmt_ops->ndo_queue_start)
		return -EOPNOTSUPP;

	ASSERT_RTNL();

	new_mem = kvzalloc(dev->queue_mgmt_ops->ndo_queue_mem_size, GFP_KERNEL);
	if (!new_mem)
		return -ENOMEM;

	old_mem = kvzalloc(dev->queue_mgmt_ops->ndo_queue_mem_size, GFP_KERNEL);
	if (!old_mem) {
		err = -ENOMEM;
		goto err_free_new_mem;
	}

	err = dev->queue_mgmt_ops->ndo_queue_mem_alloc(dev, new_mem, rxq_idx);
	if (err)
		goto err_free_old_mem;

	err = page_pool_check_memory_provider(dev, rxq);
	if (err)
		goto err_free_new_queue_mem;

	err = dev->queue_mgmt_ops->ndo_queue_stop(dev, old_mem, rxq_idx);
	if (err)
		goto err_free_new_queue_mem;

	err = dev->queue_mgmt_ops->ndo_queue_start(dev, new_mem, rxq_idx);
	if (err)
		goto err_start_queue;

	dev->queue_mgmt_ops->ndo_queue_mem_free(dev, old_mem);

	kvfree(old_mem);
	kvfree(new_mem);

	return 0;

err_start_queue:
	/* Restarting the queue with old_mem should be successful as we haven't
	 * changed any of the queue configuration, and there is not much we can
	 * do to recover from a failure here.
	 *
	 * WARN if we fail to recover the old rx queue, and at least free
	 * old_mem so we don't also leak that.
	 */
	if (dev->queue_mgmt_ops->ndo_queue_start(dev, old_mem, rxq_idx)) {
		WARN(1,
		     "Failed to restart old queue in error path. RX queue %d may be unhealthy.",
		     rxq_idx);
		dev->queue_mgmt_ops->ndo_queue_mem_free(dev, old_mem);
	}

err_free_new_queue_mem:
	dev->queue_mgmt_ops->ndo_queue_mem_free(dev, new_mem);

err_free_old_mem:
	kvfree(old_mem);

err_free_new_mem:
	kvfree(new_mem);

	return err;
}
EXPORT_SYMBOL_NS_GPL(netdev_rx_queue_restart, "NETDEV_INTERNAL");

static int __net_mp_open_rxq(struct net_device *dev, unsigned ifq_idx,
			     struct pp_memory_provider_params *p)
{
	struct netdev_rx_queue *rxq;
	int ret;

	if (ifq_idx >= dev->real_num_rx_queues)
		return -EINVAL;
	ifq_idx = array_index_nospec(ifq_idx, dev->real_num_rx_queues);

	rxq = __netif_get_rx_queue(dev, ifq_idx);
	if (rxq->mp_params.mp_ops)
		return -EEXIST;

	rxq->mp_params = *p;
	ret = netdev_rx_queue_restart(dev, ifq_idx);
	if (ret) {
		rxq->mp_params.mp_ops = NULL;
		rxq->mp_params.mp_priv = NULL;
	}
	return ret;
}

int net_mp_open_rxq(struct net_device *dev, unsigned ifq_idx,
		    struct pp_memory_provider_params *p)
{
	int ret;

	rtnl_lock();
	ret = __net_mp_open_rxq(dev, ifq_idx, p);
	rtnl_unlock();
	return ret;
}

static void __net_mp_close_rxq(struct net_device *dev, unsigned ifq_idx,
			      struct pp_memory_provider_params *old_p)
{
	struct netdev_rx_queue *rxq;

	if (WARN_ON_ONCE(ifq_idx >= dev->real_num_rx_queues))
		return;

	rxq = __netif_get_rx_queue(dev, ifq_idx);

	/* Callers holding a netdev ref may get here after we already
	 * went thru shutdown via dev_memory_provider_uninstall().
	 */
	if (dev->reg_state > NETREG_REGISTERED &&
	    !rxq->mp_params.mp_ops)
		return;

	if (WARN_ON_ONCE(rxq->mp_params.mp_ops != old_p->mp_ops ||
			 rxq->mp_params.mp_priv != old_p->mp_priv))
		return;

	rxq->mp_params.mp_ops = NULL;
	rxq->mp_params.mp_priv = NULL;
	WARN_ON(netdev_rx_queue_restart(dev, ifq_idx));
}

void net_mp_close_rxq(struct net_device *dev, unsigned ifq_idx,
		      struct pp_memory_provider_params *old_p)
{
	rtnl_lock();
	__net_mp_close_rxq(dev, ifq_idx, old_p);
	rtnl_unlock();
}
