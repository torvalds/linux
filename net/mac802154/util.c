/* This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Authors:
 * Alexander Aring <aar@pengutronix.de>
 *
 * Based on: net/mac80211/util.c
 */

#include "ieee802154_i.h"

void ieee802154_wake_queue(struct ieee802154_hw *hw)
{
	struct ieee802154_local *local = hw_to_local(hw);
	struct ieee802154_sub_if_data *sdata;

	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		if (!sdata->dev)
			continue;

		netif_wake_queue(sdata->dev);
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL(ieee802154_wake_queue);

void ieee802154_stop_queue(struct ieee802154_hw *hw)
{
	struct ieee802154_local *local = hw_to_local(hw);
	struct ieee802154_sub_if_data *sdata;

	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		if (!sdata->dev)
			continue;

		netif_stop_queue(sdata->dev);
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL(ieee802154_stop_queue);

void ieee802154_xmit_complete(struct ieee802154_hw *hw, struct sk_buff *skb)
{
	ieee802154_wake_queue(hw);
	consume_skb(skb);
}
EXPORT_SYMBOL(ieee802154_xmit_complete);
