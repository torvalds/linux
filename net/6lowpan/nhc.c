// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	6LoWPAN next header compression
 *
 *	Authors:
 *	Alexander Aring		<aar@pengutronix.de>
 */

#include <linux/netdevice.h>

#include <net/ipv6.h>

#include "nhc.h"

static const struct lowpan_nhc *lowpan_nexthdr_nhcs[NEXTHDR_MAX + 1];
static DEFINE_SPINLOCK(lowpan_nhc_lock);

static const struct lowpan_nhc *lowpan_nhc_by_nhcid(struct sk_buff *skb)
{
	const struct lowpan_nhc *nhc;
	int i;
	u8 id;

	if (!pskb_may_pull(skb, 1))
		return NULL;

	id = *skb->data;

	for (i = 0; i < NEXTHDR_MAX + 1; i++) {
		nhc = lowpan_nexthdr_nhcs[i];
		if (!nhc)
			continue;

		if ((id & nhc->idmask) == nhc->id)
			return nhc;
	}

	return NULL;
}

int lowpan_nhc_check_compression(struct sk_buff *skb,
				 const struct ipv6hdr *hdr, u8 **hc_ptr)
{
	const struct lowpan_nhc *nhc;
	int ret = 0;

	spin_lock_bh(&lowpan_nhc_lock);

	nhc = lowpan_nexthdr_nhcs[hdr->nexthdr];
	if (!(nhc && nhc->compress))
		ret = -ENOENT;

	spin_unlock_bh(&lowpan_nhc_lock);

	return ret;
}

int lowpan_nhc_do_compression(struct sk_buff *skb, const struct ipv6hdr *hdr,
			      u8 **hc_ptr)
{
	int ret;
	const struct lowpan_nhc *nhc;

	spin_lock_bh(&lowpan_nhc_lock);

	nhc = lowpan_nexthdr_nhcs[hdr->nexthdr];
	/* check if the nhc module was removed in unlocked part.
	 * TODO: this is a workaround we should prevent unloading
	 * of nhc modules while unlocked part, this will always drop
	 * the lowpan packet but it's very unlikely.
	 *
	 * Solution isn't easy because we need to decide at
	 * lowpan_nhc_check_compression if we do a compression or not.
	 * Because the inline data which is added to skb, we can't move this
	 * handling.
	 */
	if (unlikely(!nhc || !nhc->compress)) {
		ret = -EINVAL;
		goto out;
	}

	/* In the case of RAW sockets the transport header is not set by
	 * the ip6 stack so we must set it ourselves
	 */
	if (skb->transport_header == skb->network_header)
		skb_set_transport_header(skb, sizeof(struct ipv6hdr));

	ret = nhc->compress(skb, hc_ptr);
	if (ret < 0)
		goto out;

	/* skip the transport header */
	skb_pull(skb, nhc->nexthdrlen);

out:
	spin_unlock_bh(&lowpan_nhc_lock);

	return ret;
}

int lowpan_nhc_do_uncompression(struct sk_buff *skb,
				const struct net_device *dev,
				struct ipv6hdr *hdr)
{
	const struct lowpan_nhc *nhc;
	int ret;

	spin_lock_bh(&lowpan_nhc_lock);

	nhc = lowpan_nhc_by_nhcid(skb);
	if (nhc) {
		if (nhc->uncompress) {
			ret = nhc->uncompress(skb, sizeof(struct ipv6hdr) +
					      nhc->nexthdrlen);
			if (ret < 0) {
				spin_unlock_bh(&lowpan_nhc_lock);
				return ret;
			}
		} else {
			spin_unlock_bh(&lowpan_nhc_lock);
			netdev_warn(dev, "received nhc id for %s which is not implemented.\n",
				    nhc->name);
			return -ENOTSUPP;
		}
	} else {
		spin_unlock_bh(&lowpan_nhc_lock);
		netdev_warn(dev, "received unknown nhc id which was not found.\n");
		return -ENOENT;
	}

	hdr->nexthdr = nhc->nexthdr;
	skb_reset_transport_header(skb);
	raw_dump_table(__func__, "raw transport header dump",
		       skb_transport_header(skb), nhc->nexthdrlen);

	spin_unlock_bh(&lowpan_nhc_lock);

	return 0;
}

int lowpan_nhc_add(const struct lowpan_nhc *nhc)
{
	int ret = 0;

	spin_lock_bh(&lowpan_nhc_lock);

	if (lowpan_nexthdr_nhcs[nhc->nexthdr]) {
		ret = -EEXIST;
		goto out;
	}

	lowpan_nexthdr_nhcs[nhc->nexthdr] = nhc;
out:
	spin_unlock_bh(&lowpan_nhc_lock);
	return ret;
}
EXPORT_SYMBOL(lowpan_nhc_add);

void lowpan_nhc_del(const struct lowpan_nhc *nhc)
{
	spin_lock_bh(&lowpan_nhc_lock);

	lowpan_nexthdr_nhcs[nhc->nexthdr] = NULL;

	spin_unlock_bh(&lowpan_nhc_lock);

	synchronize_net();
}
EXPORT_SYMBOL(lowpan_nhc_del);
