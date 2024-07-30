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
	struct mii_timestamper *mii_ts;
	struct sk_buff *clone;
	unsigned int type;

	if (!skb->sk || !skb->dev ||
	    !phy_is_default_hwtstamp(skb->dev->phydev))
		return;

	type = classify(skb);
	if (type == PTP_CLASS_NONE)
		return;

	mii_ts = skb->dev->phydev->mii_ts;
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
	struct mii_timestamper *mii_ts;
	unsigned int type;

	if (!skb->dev || !phy_is_default_hwtstamp(skb->dev->phydev))
		return false;

	if (skb_headroom(skb) < ETH_HLEN)
		return false;

	__skb_push(skb, ETH_HLEN);

	type = ptp_classify_raw(skb);

	__skb_pull(skb, ETH_HLEN);

	if (type == PTP_CLASS_NONE)
		return false;

	mii_ts = skb->dev->phydev->mii_ts;
	if (likely(mii_ts->rxtstamp))
		return mii_ts->rxtstamp(mii_ts, skb, type);

	return false;
}
EXPORT_SYMBOL_GPL(skb_defer_rx_timestamp);
