/*
 * PTP 1588 clock support - support for timestamping in PHY devices
 *
 * Copyright (C) 2010 OMICRON electronics GmbH
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/errqueue.h>
#include <linux/phy.h>
#include <linux/ptp_classify.h>
#include <linux/skbuff.h>

static struct sock_filter ptp_filter[] = {
	PTP_FILTER
};

static unsigned int classify(const struct sk_buff *skb)
{
	if (likely(skb->dev &&
		   skb->dev->phydev &&
		   skb->dev->phydev->drv))
		return sk_run_filter(skb, ptp_filter);
	else
		return PTP_CLASS_NONE;
}

void skb_clone_tx_timestamp(struct sk_buff *skb)
{
	struct phy_device *phydev;
	struct sk_buff *clone;
	struct sock *sk = skb->sk;
	unsigned int type;

	if (!sk)
		return;

	type = classify(skb);

	switch (type) {
	case PTP_CLASS_V1_IPV4:
	case PTP_CLASS_V1_IPV6:
	case PTP_CLASS_V2_IPV4:
	case PTP_CLASS_V2_IPV6:
	case PTP_CLASS_V2_L2:
	case PTP_CLASS_V2_VLAN:
		phydev = skb->dev->phydev;
		if (likely(phydev->drv->txtstamp)) {
			clone = skb_clone(skb, GFP_ATOMIC);
			if (!clone)
				return;
			clone->sk = sk;
			phydev->drv->txtstamp(phydev, clone, type);
		}
		break;
	default:
		break;
	}
}

void skb_complete_tx_timestamp(struct sk_buff *skb,
			       struct skb_shared_hwtstamps *hwtstamps)
{
	struct sock *sk = skb->sk;
	struct sock_exterr_skb *serr;
	int err;

	if (!hwtstamps)
		return;

	*skb_hwtstamps(skb) = *hwtstamps;
	serr = SKB_EXT_ERR(skb);
	memset(serr, 0, sizeof(*serr));
	serr->ee.ee_errno = ENOMSG;
	serr->ee.ee_origin = SO_EE_ORIGIN_TIMESTAMPING;
	skb->sk = NULL;
	err = sock_queue_err_skb(sk, skb);
	if (err)
		kfree_skb(skb);
}
EXPORT_SYMBOL_GPL(skb_complete_tx_timestamp);

bool skb_defer_rx_timestamp(struct sk_buff *skb)
{
	struct phy_device *phydev;
	unsigned int type;

	skb_push(skb, ETH_HLEN);

	type = classify(skb);

	skb_pull(skb, ETH_HLEN);

	switch (type) {
	case PTP_CLASS_V1_IPV4:
	case PTP_CLASS_V1_IPV6:
	case PTP_CLASS_V2_IPV4:
	case PTP_CLASS_V2_IPV6:
	case PTP_CLASS_V2_L2:
	case PTP_CLASS_V2_VLAN:
		phydev = skb->dev->phydev;
		if (likely(phydev->drv->rxtstamp))
			return phydev->drv->rxtstamp(phydev, skb, type);
		break;
	default:
		break;
	}

	return false;
}

void __init skb_timestamping_init(void)
{
	BUG_ON(sk_chk_filter(ptp_filter, ARRAY_SIZE(ptp_filter)));
}
