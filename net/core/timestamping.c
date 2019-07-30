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
		   skb->dev->phydev->drv))
		return ptp_classify_raw(skb);
	else
		return PTP_CLASS_NONE;
}

void skb_clone_tx_timestamp(struct sk_buff *skb)
{
	struct phy_device *phydev;
	struct sk_buff *clone;
	unsigned int type;

	if (!skb->sk)
		return;

	type = classify(skb);
	if (type == PTP_CLASS_NONE)
		return;

	phydev = skb->dev->phydev;
	if (likely(phydev->drv->txtstamp)) {
		clone = skb_clone_sk(skb);
		if (!clone)
			return;
		phydev->drv->txtstamp(phydev, clone, type);
	}
}
EXPORT_SYMBOL_GPL(skb_clone_tx_timestamp);

bool skb_defer_rx_timestamp(struct sk_buff *skb)
{
	struct phy_device *phydev;
	unsigned int type;

	if (!skb->dev || !skb->dev->phydev || !skb->dev->phydev->drv)
		return false;

	if (skb_headroom(skb) < ETH_HLEN)
		return false;

	__skb_push(skb, ETH_HLEN);

	type = ptp_classify_raw(skb);

	__skb_pull(skb, ETH_HLEN);

	if (type == PTP_CLASS_NONE)
		return false;

	phydev = skb->dev->phydev;
	if (likely(phydev->drv->rxtstamp))
		return phydev->drv->rxtstamp(phydev, skb, type);

	return false;
}
EXPORT_SYMBOL_GPL(skb_defer_rx_timestamp);
