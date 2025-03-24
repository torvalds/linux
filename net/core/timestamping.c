// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PTP 1588 clock support - support for timestamping in PHY devices
 *
 * Copyright (C) 2010 OMICRON electronics GmbH
 */
#include <linux/errqueue.h>
#include <linux/phy.h>
#include <linux/ptp_classify.h>
#include <linux/skbuff.h>
#include <linux/export.h>
#include <linux/ptp_clock_kernel.h>

static unsigned int classify(const struct sk_buff *skb)
{
	if (likely(skb->dev && skb->dev->phydev &&
		   skb->dev->phydev->mii_ts))
		return ptp_classify_raw(skb);
	else
		return PTP_CLASS_NONE;
}

void skb_clone_tx_timestamp(struct sk_buff *skb)
{
	struct hwtstamp_provider *hwprov;
	struct mii_timestamper *mii_ts;
	struct phy_device *phydev;
	struct sk_buff *clone;
	unsigned int type;

	if (!skb->sk || !skb->dev)
		return;

	rcu_read_lock();
	hwprov = rcu_dereference(skb->dev->hwprov);
	if (hwprov) {
		if (hwprov->source != HWTSTAMP_SOURCE_PHYLIB ||
		    !hwprov->phydev) {
			rcu_read_unlock();
			return;
		}

		phydev = hwprov->phydev;
	} else {
		phydev = skb->dev->phydev;
		if (!phy_is_default_hwtstamp(phydev)) {
			rcu_read_unlock();
			return;
		}
	}
	rcu_read_unlock();

	type = classify(skb);
	if (type == PTP_CLASS_NONE)
		return;

	mii_ts = phydev->mii_ts;
	if (likely(mii_ts->txtstamp)) {
		clone = skb_clone_sk(skb);
		if (!clone)
			return;
		mii_ts->txtstamp(mii_ts, clone, type);
	}
}
EXPORT_SYMBOL_GPL(skb_clone_tx_timestamp);

bool skb_defer_rx_timestamp(struct sk_buff *skb)
{
	struct hwtstamp_provider *hwprov;
	struct mii_timestamper *mii_ts;
	struct phy_device *phydev;
	unsigned int type;

	if (!skb->dev)
		return false;

	rcu_read_lock();
	hwprov = rcu_dereference(skb->dev->hwprov);
	if (hwprov) {
		if (hwprov->source != HWTSTAMP_SOURCE_PHYLIB ||
		    !hwprov->phydev) {
			rcu_read_unlock();
			return false;
		}

		phydev = hwprov->phydev;
	} else {
		phydev = skb->dev->phydev;
		if (!phy_is_default_hwtstamp(phydev)) {
			rcu_read_unlock();
			return false;
		}
	}
	rcu_read_unlock();

	if (skb_headroom(skb) < ETH_HLEN)
		return false;

	__skb_push(skb, ETH_HLEN);

	type = ptp_classify_raw(skb);

	__skb_pull(skb, ETH_HLEN);

	if (type == PTP_CLASS_NONE)
		return false;

	mii_ts = phydev->mii_ts;
	if (likely(mii_ts->rxtstamp))
		return mii_ts->rxtstamp(mii_ts, skb, type);

	return false;
}
EXPORT_SYMBOL_GPL(skb_defer_rx_timestamp);
