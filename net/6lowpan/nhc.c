/*
 *	6LoWPAN next header compression
 *
 *
 *	Authors:
 *	Alexander Aring		<aar@pengutronix.de>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/netdevice.h>

#include <net/ipv6.h>

#include "nhc.h"

static struct rb_root rb_root = RB_ROOT;
static struct lowpan_nhc *lowpan_nexthdr_nhcs[NEXTHDR_MAX];
static DEFINE_SPINLOCK(lowpan_nhc_lock);

static int lowpan_nhc_insert(struct lowpan_nhc *nhc)
{
	struct rb_node **new = &rb_root.rb_node, *parent = NULL;

	/* Figure out where to put new node */
	while (*new) {
		struct lowpan_nhc *this = container_of(*new, struct lowpan_nhc,
						       node);
		int result, len_dif, len;

		len_dif = nhc->idlen - this->idlen;

		if (nhc->idlen < this->idlen)
			len = nhc->idlen;
		else
			len = this->idlen;

		result = memcmp(nhc->id, this->id, len);
		if (!result)
			result = len_dif;

		parent = *new;
		if (result < 0)
			new = &((*new)->rb_left);
		else if (result > 0)
			new = &((*new)->rb_right);
		else
			return -EEXIST;
	}

	/* Add new node and rebalance tree. */
	rb_link_node(&nhc->node, parent, new);
	rb_insert_color(&nhc->node, &rb_root);

	return 0;
}

static void lowpan_nhc_remove(struct lowpan_nhc *nhc)
{
	rb_erase(&nhc->node, &rb_root);
}

static struct lowpan_nhc *lowpan_nhc_by_nhcid(const struct sk_buff *skb)
{
	struct rb_node *node = rb_root.rb_node;
	const u8 *nhcid_skb_ptr = skb->data;

	while (node) {
		struct lowpan_nhc *nhc = container_of(node, struct lowpan_nhc,
						      node);
		u8 nhcid_skb_ptr_masked[LOWPAN_NHC_MAX_ID_LEN];
		int result, i;

		if (nhcid_skb_ptr + nhc->idlen > skb->data + skb->len)
			return NULL;

		/* copy and mask afterwards the nhid value from skb */
		memcpy(nhcid_skb_ptr_masked, nhcid_skb_ptr, nhc->idlen);
		for (i = 0; i < nhc->idlen; i++)
			nhcid_skb_ptr_masked[i] &= nhc->idmask[i];

		result = memcmp(nhcid_skb_ptr_masked, nhc->id, nhc->idlen);
		if (result < 0)
			node = node->rb_left;
		else if (result > 0)
			node = node->rb_right;
		else
			return nhc;
	}

	return NULL;
}

int lowpan_nhc_check_compression(struct sk_buff *skb,
				 const struct ipv6hdr *hdr, u8 **hc_ptr,
				 u8 *iphc0)
{
	struct lowpan_nhc *nhc;

	spin_lock_bh(&lowpan_nhc_lock);

	nhc = lowpan_nexthdr_nhcs[hdr->nexthdr];
	if (nhc && nhc->compress)
		*iphc0 |= LOWPAN_IPHC_NH_C;
	else
		lowpan_push_hc_data(hc_ptr, &hdr->nexthdr,
				    sizeof(hdr->nexthdr));

	spin_unlock_bh(&lowpan_nhc_lock);

	return 0;
}

int lowpan_nhc_do_compression(struct sk_buff *skb, const struct ipv6hdr *hdr,
			      u8 **hc_ptr)
{
	int ret;
	struct lowpan_nhc *nhc;

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

int lowpan_nhc_do_uncompression(struct sk_buff *skb, struct net_device *dev,
				struct ipv6hdr *hdr)
{
	struct lowpan_nhc *nhc;
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

int lowpan_nhc_add(struct lowpan_nhc *nhc)
{
	int ret;

	if (!nhc->idlen || !nhc->idsetup)
		return -EINVAL;

	WARN_ONCE(nhc->idlen > LOWPAN_NHC_MAX_ID_LEN,
		  "LOWPAN_NHC_MAX_ID_LEN should be updated to %zd.\n",
		  nhc->idlen);

	nhc->idsetup(nhc);

	spin_lock_bh(&lowpan_nhc_lock);

	if (lowpan_nexthdr_nhcs[nhc->nexthdr]) {
		ret = -EEXIST;
		goto out;
	}

	ret = lowpan_nhc_insert(nhc);
	if (ret < 0)
		goto out;

	lowpan_nexthdr_nhcs[nhc->nexthdr] = nhc;
out:
	spin_unlock_bh(&lowpan_nhc_lock);
	return ret;
}
EXPORT_SYMBOL(lowpan_nhc_add);

void lowpan_nhc_del(struct lowpan_nhc *nhc)
{
	spin_lock_bh(&lowpan_nhc_lock);

	lowpan_nhc_remove(nhc);
	lowpan_nexthdr_nhcs[nhc->nexthdr] = NULL;

	spin_unlock_bh(&lowpan_nhc_lock);

	synchronize_net();
}
EXPORT_SYMBOL(lowpan_nhc_del);
