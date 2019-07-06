// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2007-2012 Siemens AG
 *
 * Written by:
 * Alexander Smirnov <alex.bluesman.smirnov@gmail.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>

#include <net/netlink.h>
#include <net/nl802154.h>
#include <net/mac802154.h>
#include <net/ieee802154_netdev.h>
#include <net/route.h>
#include <net/cfg802154.h>

#include "ieee802154_i.h"
#include "cfg.h"

static void ieee802154_tasklet_handler(unsigned long data)
{
	struct ieee802154_local *local = (struct ieee802154_local *)data;
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&local->skb_queue))) {
		switch (skb->pkt_type) {
		case IEEE802154_RX_MSG:
			/* Clear skb->pkt_type in order to not confuse kernel
			 * netstack.
			 */
			skb->pkt_type = 0;
			ieee802154_rx(local, skb);
			break;
		default:
			WARN(1, "mac802154: Packet is of unknown type %d\n",
			     skb->pkt_type);
			kfree_skb(skb);
			break;
		}
	}
}

struct ieee802154_hw *
ieee802154_alloc_hw(size_t priv_data_len, const struct ieee802154_ops *ops)
{
	struct wpan_phy *phy;
	struct ieee802154_local *local;
	size_t priv_size;

	if (WARN_ON(!ops || !(ops->xmit_async || ops->xmit_sync) || !ops->ed ||
		    !ops->start || !ops->stop || !ops->set_channel))
		return NULL;

	/* Ensure 32-byte alignment of our private data and hw private data.
	 * We use the wpan_phy priv data for both our ieee802154_local and for
	 * the driver's private data
	 *
	 * in memory it'll be like this:
	 *
	 * +-------------------------+
	 * | struct wpan_phy         |
	 * +-------------------------+
	 * | struct ieee802154_local |
	 * +-------------------------+
	 * | driver's private data   |
	 * +-------------------------+
	 *
	 * Due to ieee802154 layer isn't aware of driver and MAC structures,
	 * so lets align them here.
	 */

	priv_size = ALIGN(sizeof(*local), NETDEV_ALIGN) + priv_data_len;

	phy = wpan_phy_new(&mac802154_config_ops, priv_size);
	if (!phy) {
		pr_err("failure to allocate master IEEE802.15.4 device\n");
		return NULL;
	}

	phy->privid = mac802154_wpan_phy_privid;

	local = wpan_phy_priv(phy);
	local->phy = phy;
	local->hw.phy = local->phy;
	local->hw.priv = (char *)local + ALIGN(sizeof(*local), NETDEV_ALIGN);
	local->ops = ops;

	INIT_LIST_HEAD(&local->interfaces);
	mutex_init(&local->iflist_mtx);

	tasklet_init(&local->tasklet,
		     ieee802154_tasklet_handler,
		     (unsigned long)local);

	skb_queue_head_init(&local->skb_queue);

	INIT_WORK(&local->tx_work, ieee802154_xmit_worker);

	/* init supported flags with 802.15.4 default ranges */
	phy->supported.max_minbe = 8;
	phy->supported.min_maxbe = 3;
	phy->supported.max_maxbe = 8;
	phy->supported.min_frame_retries = 0;
	phy->supported.max_frame_retries = 7;
	phy->supported.max_csma_backoffs = 5;
	phy->supported.lbt = NL802154_SUPPORTED_BOOL_FALSE;

	/* always supported */
	phy->supported.iftypes = BIT(NL802154_IFTYPE_NODE);

	return &local->hw;
}
EXPORT_SYMBOL(ieee802154_alloc_hw);

void ieee802154_free_hw(struct ieee802154_hw *hw)
{
	struct ieee802154_local *local = hw_to_local(hw);

	BUG_ON(!list_empty(&local->interfaces));

	mutex_destroy(&local->iflist_mtx);

	wpan_phy_free(local->phy);
}
EXPORT_SYMBOL(ieee802154_free_hw);

static void ieee802154_setup_wpan_phy_pib(struct wpan_phy *wpan_phy)
{
	/* TODO warn on empty symbol_duration
	 * Should be done when all drivers sets this value.
	 */

	wpan_phy->lifs_period = IEEE802154_LIFS_PERIOD *
				wpan_phy->symbol_duration;
	wpan_phy->sifs_period = IEEE802154_SIFS_PERIOD *
				wpan_phy->symbol_duration;
}

int ieee802154_register_hw(struct ieee802154_hw *hw)
{
	struct ieee802154_local *local = hw_to_local(hw);
	struct net_device *dev;
	int rc = -ENOSYS;

	local->workqueue =
		create_singlethread_workqueue(wpan_phy_name(local->phy));
	if (!local->workqueue) {
		rc = -ENOMEM;
		goto out;
	}

	hrtimer_init(&local->ifs_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	local->ifs_timer.function = ieee802154_xmit_ifs_timer;

	wpan_phy_set_dev(local->phy, local->hw.parent);

	ieee802154_setup_wpan_phy_pib(local->phy);

	if (!(hw->flags & IEEE802154_HW_CSMA_PARAMS)) {
		local->phy->supported.min_csma_backoffs = 4;
		local->phy->supported.max_csma_backoffs = 4;
		local->phy->supported.min_maxbe = 5;
		local->phy->supported.max_maxbe = 5;
		local->phy->supported.min_minbe = 3;
		local->phy->supported.max_minbe = 3;
	}

	if (!(hw->flags & IEEE802154_HW_FRAME_RETRIES)) {
		local->phy->supported.min_frame_retries = 3;
		local->phy->supported.max_frame_retries = 3;
	}

	if (hw->flags & IEEE802154_HW_PROMISCUOUS)
		local->phy->supported.iftypes |= BIT(NL802154_IFTYPE_MONITOR);

	rc = wpan_phy_register(local->phy);
	if (rc < 0)
		goto out_wq;

	rtnl_lock();

	dev = ieee802154_if_add(local, "wpan%d", NET_NAME_ENUM,
				NL802154_IFTYPE_NODE,
				cpu_to_le64(0x0000000000000000ULL));
	if (IS_ERR(dev)) {
		rtnl_unlock();
		rc = PTR_ERR(dev);
		goto out_phy;
	}

	rtnl_unlock();

	return 0;

out_phy:
	wpan_phy_unregister(local->phy);
out_wq:
	destroy_workqueue(local->workqueue);
out:
	return rc;
}
EXPORT_SYMBOL(ieee802154_register_hw);

void ieee802154_unregister_hw(struct ieee802154_hw *hw)
{
	struct ieee802154_local *local = hw_to_local(hw);

	tasklet_kill(&local->tasklet);
	flush_workqueue(local->workqueue);

	rtnl_lock();

	ieee802154_remove_interfaces(local);

	rtnl_unlock();

	destroy_workqueue(local->workqueue);
	wpan_phy_unregister(local->phy);
}
EXPORT_SYMBOL(ieee802154_unregister_hw);

static int __init ieee802154_init(void)
{
	return ieee802154_iface_init();
}

static void __exit ieee802154_exit(void)
{
	ieee802154_iface_exit();

	rcu_barrier();
}

subsys_initcall(ieee802154_init);
module_exit(ieee802154_exit);

MODULE_DESCRIPTION("IEEE 802.15.4 subsystem");
MODULE_LICENSE("GPL v2");
