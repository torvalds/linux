/*
 * Copyright (C) 2007-2012 Siemens AG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Written by:
 * Pavel Smolenskiy <pavel.smolenskiy@gmail.com>
 * Maxim Gorbachyov <maxim.gorbachev@siemens.com>
 * Dmitry Eremin-Solenikov <dbaryshkov@gmail.com>
 * Alexander Smirnov <alex.bluesman.smirnov@gmail.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/crc-ccitt.h>

#include <net/mac802154.h>
#include <net/ieee802154_netdev.h>

#include "ieee802154_i.h"

static void
mac802154_subif_rx(struct ieee802154_hw *hw, struct sk_buff *skb)
{
	struct ieee802154_local *local = hw_to_local(hw);

	skb->protocol = htons(ETH_P_IEEE802154);
	skb_reset_mac_header(skb);

	if (!(local->hw.flags & IEEE802154_HW_OMIT_CKSUM)) {
		u16 crc;

		if (skb->len < 2) {
			pr_debug("got invalid frame\n");
			goto fail;
		}
		crc = crc_ccitt(0, skb->data, skb->len);
		if (crc) {
			pr_debug("CRC mismatch\n");
			goto fail;
		}
		skb_trim(skb, skb->len - 2); /* CRC */
	}

	mac802154_monitors_rx(local, skb);
	mac802154_wpans_rx(local, skb);

	return;

fail:
	kfree_skb(skb);
}

void ieee802154_rx(struct ieee802154_hw *hw, struct sk_buff *skb)
{
	WARN_ON_ONCE(softirq_count() == 0);

	mac802154_subif_rx(hw, skb);
}
EXPORT_SYMBOL(ieee802154_rx);

void
ieee802154_rx_irqsafe(struct ieee802154_hw *hw, struct sk_buff *skb, u8 lqi)
{
	struct ieee802154_local *local = hw_to_local(hw);

	mac_cb(skb)->lqi = lqi;
	skb->pkt_type = IEEE802154_RX_MSG;
	skb_queue_tail(&local->skb_queue, skb);
	tasklet_schedule(&local->tasklet);
}
EXPORT_SYMBOL(ieee802154_rx_irqsafe);
