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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Written by:
 * Pavel Smolenskiy <pavel.smolenskiy@gmail.com>
 * Maxim Gorbachyov <maxim.gorbachev@siemens.com>
 * Dmitry Eremin-Solenikov <dbaryshkov@gmail.com>
 * Alexander Smirnov <alex.bluesman.smirnov@gmail.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/netdevice.h>
#include <linux/crc-ccitt.h>

#include <net/mac802154.h>
#include <net/ieee802154_netdev.h>

#include "mac802154.h"

/* The IEEE 802.15.4 standard defines 4 MAC packet types:
 * - beacon frame
 * - MAC command frame
 * - acknowledgement frame
 * - data frame
 *
 * and only the data frame should be pushed to the upper layers, other types
 * are just internal MAC layer management information. So only data packets
 * are going to be sent to the networking queue, all other will be processed
 * right here by using the device workqueue.
 */
struct rx_work {
	struct sk_buff *skb;
	struct work_struct work;
	struct ieee802154_dev *dev;
	u8 lqi;
};

static void
mac802154_subif_rx(struct ieee802154_dev *hw, struct sk_buff *skb, u8 lqi)
{
	struct mac802154_priv *priv = mac802154_to_priv(hw);

	mac_cb(skb)->lqi = lqi;
	skb->protocol = htons(ETH_P_IEEE802154);
	skb_reset_mac_header(skb);

	BUILD_BUG_ON(sizeof(struct ieee802154_mac_cb) > sizeof(skb->cb));

	if (!(priv->hw.flags & IEEE802154_HW_OMIT_CKSUM)) {
		u16 crc;

		if (skb->len < 2) {
			pr_debug("got invalid frame\n");
			goto out;
		}
		crc = crc_ccitt(0, skb->data, skb->len);
		if (crc) {
			pr_debug("CRC mismatch\n");
			goto out;
		}
		skb_trim(skb, skb->len - 2); /* CRC */
	}

	mac802154_monitors_rx(priv, skb);
	mac802154_wpans_rx(priv, skb);
out:
	dev_kfree_skb(skb);
	return;
}

static void mac802154_rx_worker(struct work_struct *work)
{
	struct rx_work *rw = container_of(work, struct rx_work, work);
	struct sk_buff *skb = rw->skb;

	mac802154_subif_rx(rw->dev, skb, rw->lqi);
	kfree(rw);
}

void
ieee802154_rx_irqsafe(struct ieee802154_dev *dev, struct sk_buff *skb, u8 lqi)
{
	struct mac802154_priv *priv = mac802154_to_priv(dev);
	struct rx_work *work;

	if (!skb)
		return;

	work = kzalloc(sizeof(struct rx_work), GFP_ATOMIC);
	if (!work)
		return;

	INIT_WORK(&work->work, mac802154_rx_worker);
	work->skb = skb;
	work->dev = dev;
	work->lqi = lqi;

	queue_work(priv->dev_workqueue, &work->work);
}
EXPORT_SYMBOL(ieee802154_rx_irqsafe);
