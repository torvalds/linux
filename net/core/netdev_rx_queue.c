// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/ethtool_netlink.h>
#include <linux/netdevice.h>
#include <net/netdev_lock.h>
#include <net/netdev_queues.h>
#include <net/netdev_rx_queue.h>
#include <net/page_pool/memory_provider.h>

#include "page_pool_priv.h"

void netdev_rx_queue_lease(struct netdev_rx_queue *rxq_dst,
			   struct netdev_rx_queue *rxq_src)
{
	netdev_assert_locked(rxq_src->dev);
	netdev_assert_locked(rxq_dst->dev);

	netdev_hold(rxq_src->dev, &rxq_src->lease_tracker, GFP_KERNEL);

	WRITE_ONCE(rxq_src->lease, rxq_dst);
	WRITE_ONCE(rxq_dst->lease, rxq_src);
}

void netdev_rx_queue_unlease(struct netdev_rx_queue *rxq_dst,
			     struct netdev_rx_queue *rxq_src)
{
	netdev_assert_locked(rxq_dst->dev);
	netdev_assert_locked(rxq_src->dev);

	WRITE_ONCE(rxq_src->lease, NULL);
	WRITE_ONCE(rxq_dst->lease, NULL);

	netdev_put(rxq_src->dev, &rxq_src->lease_tracker);
}

bool netif_rxq_is_leased(struct net_device *dev, unsigned int rxq_idx)
{
	if (rxq_idx < dev->real_num_rx_queues)
		return READ_ONCE(__netif_get_rx_queue(dev, rxq_idx)->lease);
	return false;
}

static bool netif_lease_dir_ok(const struct net_device *dev,
			       enum netif_lease_dir dir)
{
	if (dir == NETIF_VIRT_TO_PHYS && !dev->dev.parent)
		return true;
	if (dir == NETIF_PHYS_TO_VIRT && dev->dev.parent)
		return true;
	return false;
}

struct netdev_rx_queue *
__netif_get_rx_queue_lease(struct net_device **dev, unsigned int *rxq_idx,
			   enum netif_lease_dir dir)
{
	struct net_device *orig_dev = *dev;
	struct netdev_rx_queue *rxq = __netif_get_rx_queue(orig_dev, *rxq_idx);

	if (rxq->lease) {
		if (!netif_lease_dir_ok(orig_dev, dir))
			return NULL;
		rxq = rxq->lease;
		*rxq_idx = get_netdev_rx_queue_index(rxq);
		*dev = rxq->dev;
	}
	return rxq;
}

struct netdev_rx_queue *
netif_get_rx_queue_lease_locked(struct net_device **dev, unsigned int *rxq_idx)
{
	struct net_device *orig_dev = *dev;
	struct netdev_rx_queue *rxq;

	/* Locking order is always from the virtual to the physical device
	 * see netdev_nl_queue_create_doit().
	 */
	netdev_ops_assert_locked(orig_dev);
	rxq = __netif_get_rx_queue_lease(dev, rxq_idx, NETIF_VIRT_TO_PHYS);
	if (rxq && orig_dev != *dev)
		netdev_lock(*dev);
	return rxq;
}

void netif_put_rx_queue_lease_locked(struct net_device *orig_dev,
				     struct net_device *dev)
{
	if (orig_dev != dev)
		netdev_unlock(dev);
}

bool netif_rx_queue_lease_get_owner(struct net_device **dev,
				    unsigned int *rxq_idx)
{
	struct net_device *orig_dev = *dev;
	struct netdev_rx_queue *rxq;

	/* The physical device needs to be locked. If there is indeed a lease,
	 * then the virtual device holds a reference on the physical device
	 * and the lease stays active until the virtual device is torn down.
	 * When queues get {un,}leased both devices are always locked.
	 */
	netdev_ops_assert_locked(orig_dev);
	rxq = __netif_get_rx_queue_lease(dev, rxq_idx, NETIF_PHYS_TO_VIRT);
	if (rxq && orig_dev != *dev)
		return true;
	return false;
}

/* See also page_pool_is_unreadable() */
bool netif_rxq_has_unreadable_mp(struct net_device *dev, unsigned int rxq_idx)
{
	if (rxq_idx < dev->real_num_rx_queues)
		return __netif_get_rx_queue(dev, rxq_idx)->mp_params.mp_ops;
	return false;
}
EXPORT_SYMBOL(netif_rxq_has_unreadable_mp);

bool netif_rxq_has_mp(struct net_device *dev, unsigned int rxq_idx)
{
	if (rxq_idx < dev->real_num_rx_queues)
		return __netif_get_rx_queue(dev, rxq_idx)->mp_params.mp_priv;
	return false;
}

int netdev_rx_queue_restart(struct net_device *dev, unsigned int rxq_idx)
{
	struct netdev_rx_queue *rxq = __netif_get_rx_queue(dev, rxq_idx);
	const struct netdev_queue_mgmt_ops *qops = dev->queue_mgmt_ops;
	void *new_mem, *old_mem;
	int err;

	if (!qops || !qops->ndo_queue_stop || !qops->ndo_queue_mem_free ||
	    !qops->ndo_queue_mem_alloc || !qops->ndo_queue_start)
		return -EOPNOTSUPP;

	netdev_assert_locked(dev);

	new_mem = kvzalloc(qops->ndo_queue_mem_size, GFP_KERNEL);
	if (!new_mem)
		return -ENOMEM;

	old_mem = kvzalloc(qops->ndo_queue_mem_size, GFP_KERNEL);
	if (!old_mem) {
		err = -ENOMEM;
		goto err_free_new_mem;
	}

	err = qops->ndo_queue_mem_alloc(dev, new_mem, rxq_idx);
	if (err)
		goto err_free_old_mem;

	err = page_pool_check_memory_provider(dev, rxq);
	if (err)
		goto err_free_new_queue_mem;

	if (netif_running(dev)) {
		err = qops->ndo_queue_stop(dev, old_mem, rxq_idx);
		if (err)
			goto err_free_new_queue_mem;

		err = qops->ndo_queue_start(dev, new_mem, rxq_idx);
		if (err)
			goto err_start_queue;
	} else {
		swap(new_mem, old_mem);
	}

	qops->ndo_queue_mem_free(dev, old_mem);

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
	if (qops->ndo_queue_start(dev, old_mem, rxq_idx)) {
		WARN(1,
		     "Failed to restart old queue in error path. RX queue %d may be unhealthy.",
		     rxq_idx);
		qops->ndo_queue_mem_free(dev, old_mem);
	}

err_free_new_queue_mem:
	qops->ndo_queue_mem_free(dev, new_mem);

err_free_old_mem:
	kvfree(old_mem);

err_free_new_mem:
	kvfree(new_mem);

	return err;
}
EXPORT_SYMBOL_NS_GPL(netdev_rx_queue_restart, "NETDEV_INTERNAL");

int __net_mp_open_rxq(struct net_device *dev, unsigned int rxq_idx,
		      const struct pp_memory_provider_params *p,
		      struct netlink_ext_ack *extack)
{
	struct net_device *orig_dev = dev;
	struct netdev_rx_queue *rxq;
	int ret;

	if (!netdev_need_ops_lock(dev))
		return -EOPNOTSUPP;
	if (rxq_idx >= dev->real_num_rx_queues) {
		NL_SET_ERR_MSG(extack, "rx queue index out of range");
		return -ERANGE;
	}

	rxq_idx = array_index_nospec(rxq_idx, dev->real_num_rx_queues);
	rxq = netif_get_rx_queue_lease_locked(&dev, &rxq_idx);
	if (!rxq) {
		NL_SET_ERR_MSG(extack, "rx queue peered to a virtual netdev");
		return -EBUSY;
	}
	if (!dev->dev.parent) {
		NL_SET_ERR_MSG(extack, "rx queue is mapped to a virtual netdev");
		ret = -EBUSY;
		goto out;
	}
	if (dev->cfg->hds_config != ETHTOOL_TCP_DATA_SPLIT_ENABLED) {
		NL_SET_ERR_MSG(extack, "tcp-data-split is disabled");
		ret = -EINVAL;
		goto out;
	}
	if (dev->cfg->hds_thresh) {
		NL_SET_ERR_MSG(extack, "hds-thresh is not zero");
		ret = -EINVAL;
		goto out;
	}
	if (dev_xdp_prog_count(dev)) {
		NL_SET_ERR_MSG(extack, "unable to custom memory provider to device with XDP program attached");
		ret = -EEXIST;
		goto out;
	}
	if (rxq->mp_params.mp_ops) {
		NL_SET_ERR_MSG(extack, "designated queue already memory provider bound");
		ret = -EEXIST;
		goto out;
	}
#ifdef CONFIG_XDP_SOCKETS
	if (rxq->pool) {
		NL_SET_ERR_MSG(extack, "designated queue already in use by AF_XDP");
		ret = -EBUSY;
		goto out;
	}
#endif
	rxq->mp_params = *p;
	ret = netdev_rx_queue_restart(dev, rxq_idx);
	if (ret) {
		rxq->mp_params.mp_ops = NULL;
		rxq->mp_params.mp_priv = NULL;
	}
out:
	netif_put_rx_queue_lease_locked(orig_dev, dev);
	return ret;
}

int net_mp_open_rxq(struct net_device *dev, unsigned int rxq_idx,
		    struct pp_memory_provider_params *p)
{
	int ret;

	netdev_lock(dev);
	ret = __net_mp_open_rxq(dev, rxq_idx, p, NULL);
	netdev_unlock(dev);
	return ret;
}

void __net_mp_close_rxq(struct net_device *dev, unsigned int rxq_idx,
			const struct pp_memory_provider_params *old_p)
{
	struct net_device *orig_dev = dev;
	struct netdev_rx_queue *rxq;
	int err;

	if (WARN_ON_ONCE(rxq_idx >= dev->real_num_rx_queues))
		return;

	rxq = netif_get_rx_queue_lease_locked(&dev, &rxq_idx);
	if (WARN_ON_ONCE(!rxq))
		return;

	/* Callers holding a netdev ref may get here after we already
	 * went thru shutdown via dev_memory_provider_uninstall().
	 */
	if (dev->reg_state > NETREG_REGISTERED &&
	    !rxq->mp_params.mp_ops)
		goto out;

	if (WARN_ON_ONCE(rxq->mp_params.mp_ops != old_p->mp_ops ||
			 rxq->mp_params.mp_priv != old_p->mp_priv))
		goto out;

	rxq->mp_params.mp_ops = NULL;
	rxq->mp_params.mp_priv = NULL;
	err = netdev_rx_queue_restart(dev, rxq_idx);
	WARN_ON(err && err != -ENETDOWN);
out:
	netif_put_rx_queue_lease_locked(orig_dev, dev);
}

void net_mp_close_rxq(struct net_device *dev, unsigned int rxq_idx,
		      struct pp_memory_provider_params *old_p)
{
	netdev_lock(dev);
	__net_mp_close_rxq(dev, rxq_idx, old_p);
	netdev_unlock(dev);
}
