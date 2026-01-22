// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/ethtool_netlink.h>
#include <linux/netdevice.h>
#include <net/netdev_lock.h>
#include <net/netdev_queues.h>
#include <net/netdev_rx_queue.h>
#include <net/page_pool/memory_provider.h>

#include "dev.h"
#include "page_pool_priv.h"

/* See also page_pool_is_unreadable() */
bool netif_rxq_has_unreadable_mp(struct net_device *dev, int idx)
{
	struct netdev_rx_queue *rxq = __netif_get_rx_queue(dev, idx);

	return !!rxq->mp_params.mp_ops;
}
EXPORT_SYMBOL(netif_rxq_has_unreadable_mp);

static int netdev_rx_queue_reconfig(struct net_device *dev,
				    unsigned int rxq_idx,
				    struct netdev_queue_config *qcfg_old,
				    struct netdev_queue_config *qcfg_new)
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

	err = qops->ndo_queue_mem_alloc(dev, qcfg_new, new_mem, rxq_idx);
	if (err)
		goto err_free_old_mem;

	err = page_pool_check_memory_provider(dev, rxq);
	if (err)
		goto err_free_new_queue_mem;

	if (netif_running(dev)) {
		err = qops->ndo_queue_stop(dev, old_mem, rxq_idx);
		if (err)
			goto err_free_new_queue_mem;

		err = qops->ndo_queue_start(dev, qcfg_new, new_mem, rxq_idx);
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
	if (qops->ndo_queue_start(dev, qcfg_old, old_mem, rxq_idx)) {
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

int netdev_rx_queue_restart(struct net_device *dev, unsigned int rxq_idx)
{
	struct netdev_queue_config qcfg;

	netdev_queue_config(dev, rxq_idx, &qcfg);
	return netdev_rx_queue_reconfig(dev, rxq_idx, &qcfg, &qcfg);
}
EXPORT_SYMBOL_NS_GPL(netdev_rx_queue_restart, "NETDEV_INTERNAL");

int __net_mp_open_rxq(struct net_device *dev, unsigned int rxq_idx,
		      const struct pp_memory_provider_params *p,
		      struct netlink_ext_ack *extack)
{
	const struct netdev_queue_mgmt_ops *qops = dev->queue_mgmt_ops;
	struct netdev_queue_config qcfg[2];
	struct netdev_rx_queue *rxq;
	int ret;

	if (!netdev_need_ops_lock(dev))
		return -EOPNOTSUPP;

	if (rxq_idx >= dev->real_num_rx_queues) {
		NL_SET_ERR_MSG(extack, "rx queue index out of range");
		return -ERANGE;
	}
	rxq_idx = array_index_nospec(rxq_idx, dev->real_num_rx_queues);

	if (dev->cfg->hds_config != ETHTOOL_TCP_DATA_SPLIT_ENABLED) {
		NL_SET_ERR_MSG(extack, "tcp-data-split is disabled");
		return -EINVAL;
	}
	if (dev->cfg->hds_thresh) {
		NL_SET_ERR_MSG(extack, "hds-thresh is not zero");
		return -EINVAL;
	}
	if (dev_xdp_prog_count(dev)) {
		NL_SET_ERR_MSG(extack, "unable to custom memory provider to device with XDP program attached");
		return -EEXIST;
	}
	if (p->rx_page_size && !(qops->supported_params & QCFG_RX_PAGE_SIZE)) {
		NL_SET_ERR_MSG(extack, "device does not support: rx_page_size");
		return -EOPNOTSUPP;
	}

	rxq = __netif_get_rx_queue(dev, rxq_idx);
	if (rxq->mp_params.mp_ops) {
		NL_SET_ERR_MSG(extack, "designated queue already memory provider bound");
		return -EEXIST;
	}
#ifdef CONFIG_XDP_SOCKETS
	if (rxq->pool) {
		NL_SET_ERR_MSG(extack, "designated queue already in use by AF_XDP");
		return -EBUSY;
	}
#endif

	netdev_queue_config(dev, rxq_idx, &qcfg[0]);
	rxq->mp_params = *p;
	ret = netdev_queue_config_validate(dev, rxq_idx, &qcfg[1], extack);
	if (ret)
		goto err_clear_mp;

	ret = netdev_rx_queue_reconfig(dev, rxq_idx, &qcfg[0], &qcfg[1]);
	if (ret)
		goto err_clear_mp;

	return 0;

err_clear_mp:
	memset(&rxq->mp_params, 0, sizeof(rxq->mp_params));
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

void __net_mp_close_rxq(struct net_device *dev, unsigned int ifq_idx,
			const struct pp_memory_provider_params *old_p)
{
	struct netdev_queue_config qcfg[2];
	struct netdev_rx_queue *rxq;
	int err;

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

	netdev_queue_config(dev, ifq_idx, &qcfg[0]);
	memset(&rxq->mp_params, 0, sizeof(rxq->mp_params));
	netdev_queue_config(dev, ifq_idx, &qcfg[1]);

	err = netdev_rx_queue_reconfig(dev, ifq_idx, &qcfg[0], &qcfg[1]);
	WARN_ON(err && err != -ENETDOWN);
}

void net_mp_close_rxq(struct net_device *dev, unsigned ifq_idx,
		      struct pp_memory_provider_params *old_p)
{
	netdev_lock(dev);
	__net_mp_close_rxq(dev, ifq_idx, old_p);
	netdev_unlock(dev);
}
