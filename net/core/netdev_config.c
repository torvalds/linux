// SPDX-License-Identifier: GPL-2.0-only

#include <linux/netdevice.h>
#include <net/netdev_queues.h>
#include <net/netdev_rx_queue.h>

#include "dev.h"

int netdev_alloc_config(struct net_device *dev)
{
	struct netdev_config *cfg;

	cfg = kzalloc_obj(*dev->cfg, GFP_KERNEL_ACCOUNT);
	if (!cfg)
		return -ENOMEM;

	dev->cfg = cfg;
	dev->cfg_pending = cfg;
	return 0;
}

void __netdev_free_config(struct netdev_config *cfg)
{
	kfree(cfg);
}

void netdev_free_config(struct net_device *dev)
{
	WARN_ON(dev->cfg != dev->cfg_pending);
	__netdev_free_config(dev->cfg);
}

int netdev_reconfig_start(struct net_device *dev)
{
	struct netdev_config *cfg;

	WARN_ON(dev->cfg != dev->cfg_pending);
	cfg = kmemdup(dev->cfg, sizeof(*dev->cfg), GFP_KERNEL_ACCOUNT);
	if (!cfg)
		return -ENOMEM;

	dev->cfg_pending = cfg;
	return 0;
}

static int netdev_nop_validate_qcfg(struct net_device *dev,
				    struct netdev_queue_config *qcfg,
				    struct netlink_ext_ack *extack)
{
	return 0;
}

static void netdev_qcfg_apply_dev(struct netdev_queue_config *qcfg,
				  const struct netdev_config *cfg)
{
	/* Device config overrides callback-provided fallbacks. */
	qcfg->rx_ring_size = cfg->rings.rx_pending;
	qcfg->rx_mini_ring_size = cfg->rings.rx_mini_pending;
	qcfg->rx_jumbo_ring_size = cfg->rings.rx_jumbo_pending;
}

static int __netdev_queue_config(struct net_device *dev, int rxq_idx,
				 struct netdev_queue_config *qcfg,
				 struct netlink_ext_ack *extack,
				 bool validate)
{
	int (*validate_cb)(struct net_device *dev,
			   struct netdev_queue_config *qcfg,
			   struct netlink_ext_ack *extack);
	struct pp_memory_provider_params *mpp;
	int err;

	validate_cb = netdev_nop_validate_qcfg;
	if (validate && dev->queue_mgmt_ops->ndo_validate_qcfg)
		validate_cb = dev->queue_mgmt_ops->ndo_validate_qcfg;

	memset(qcfg, 0, sizeof(*qcfg));

	/* Get defaults from the driver, in case user config not set */
	if (dev->queue_mgmt_ops->ndo_default_qcfg)
		dev->queue_mgmt_ops->ndo_default_qcfg(dev, qcfg);
	netdev_qcfg_apply_dev(qcfg, dev->cfg_pending);
	err = validate_cb(dev, qcfg, extack);
	if (err)
		return err;

	/* Apply MP overrides */
	mpp = &__netif_get_rx_queue(dev, rxq_idx)->mp_params;
	if (mpp->rx_page_size)
		qcfg->rx_page_size = mpp->rx_page_size;
	err = validate_cb(dev, qcfg, extack);
	if (err)
		return err;

	return 0;
}

/**
 * netdev_queue_config() - get configuration for a given queue
 * @dev:      net_device instance
 * @rxq_idx:  index of the queue of interest
 * @qcfg: queue configuration struct (output)
 *
 * Render the configuration for a given queue. During a configuration
 * transaction this includes the proposed device-wide values in
 * @dev->cfg_pending; otherwise @dev->cfg_pending points to the accepted
 * configuration. This helper should be used by drivers which support queue
 * configuration to retrieve config for a particular queue.
 *
 * @qcfg is an output parameter and is always fully initialized by this
 * function. Some values may not be set by the user, drivers may either
 * deal with the "unset" values in @qcfg, or provide the callback
 * to populate defaults in queue_management_ops.
 */
void netdev_queue_config(struct net_device *dev, int rxq_idx,
			 struct netdev_queue_config *qcfg)
{
	__netdev_queue_config(dev, rxq_idx, qcfg, NULL, false);
}
EXPORT_SYMBOL(netdev_queue_config);

int netdev_queue_config_validate(struct net_device *dev, int rxq_idx,
				 struct netdev_queue_config *qcfg,
				 struct netlink_ext_ack *extack)
{
	return __netdev_queue_config(dev, rxq_idx, qcfg, extack, true);
}

int netdev_queue_config_revalidate(struct net_device *dev,
				   struct netlink_ext_ack *extack)
{
	const struct netdev_queue_mgmt_ops *qops = dev->queue_mgmt_ops;
	struct netdev_queue_config qcfg;
	unsigned int i;
	int err;

	if (!qops || !qops->ndo_validate_qcfg)
		return 0;

	for (i = 0; i < dev->real_num_rx_queues; i++) {
		err = netdev_queue_config_validate(dev, i, &qcfg, extack);
		if (err)
			return err;
	}

	return 0;
}
