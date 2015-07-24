/* Copyright (C) 2010: YOSHIFUJI Hideaki <yoshfuji@linux-ipv6.org>
 * Copyright (C) 2015: Linus Lüssing <linus.luessing@c0d3.blue>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Based on the MLD support added to br_multicast.c by YOSHIFUJI Hideaki.
 */

#include <linux/skbuff.h>
#include <net/ipv6.h>
#include <net/mld.h>
#include <net/addrconf.h>
#include <net/ip6_checksum.h>

static int ipv6_mc_check_ip6hdr(struct sk_buff *skb)
{
	const struct ipv6hdr *ip6h;
	unsigned int len;
	unsigned int offset = skb_network_offset(skb) + sizeof(*ip6h);

	if (!pskb_may_pull(skb, offset))
		return -EINVAL;

	ip6h = ipv6_hdr(skb);

	if (ip6h->version != 6)
		return -EINVAL;

	len = offset + ntohs(ip6h->payload_len);
	if (skb->len < len || len <= offset)
		return -EINVAL;

	return 0;
}

static int ipv6_mc_check_exthdrs(struct sk_buff *skb)
{
	const struct ipv6hdr *ip6h;
	int offset;
	u8 nexthdr;
	__be16 frag_off;

	ip6h = ipv6_hdr(skb);

	if (ip6h->nexthdr != IPPROTO_HOPOPTS)
		return -ENOMSG;

	nexthdr = ip6h->nexthdr;
	offset = skb_network_offset(skb) + sizeof(*ip6h);
	offset = ipv6_skip_exthdr(skb, offset, &nexthdr, &frag_off);

	if (offset < 0)
		return -EINVAL;

	if (nexthdr != IPPROTO_ICMPV6)
		return -ENOMSG;

	skb_set_transport_header(skb, offset);

	return 0;
}

static int ipv6_mc_check_mld_reportv2(struct sk_buff *skb)
{
	unsigned int len = skb_transport_offset(skb);

	len += sizeof(struct mld2_report);

	return pskb_may_pull(skb, len) ? 0 : -EINVAL;
}

static int ipv6_mc_check_mld_query(struct sk_buff *skb)
{
	struct mld_msg *mld;
	unsigned int len = skb_transport_offset(skb);

	/* RFC2710+RFC3810 (MLDv1+MLDv2) require link-local source addresses */
	if (!(ipv6_addr_type(&ipv6_hdr(skb)->saddr) & IPV6_ADDR_LINKLOCAL))
		return -EINVAL;

	len += sizeof(struct mld_msg);
	if (skb->len < len)
		return -EINVAL;

	/* MLDv1? */
	if (skb->len != len) {
		/* or MLDv2? */
		len += sizeof(struct mld2_query) - sizeof(struct mld_msg);
		if (skb->len < len || !pskb_may_pull(skb, len))
			return -EINVAL;
	}

	mld = (struct mld_msg *)skb_transport_header(skb);

	/* RFC2710+RFC3810 (MLDv1+MLDv2) require the multicast link layer
	 * all-nodes destination address (ff02::1) for general queries
	 */
	if (ipv6_addr_any(&mld->mld_mca) &&
	    !ipv6_addr_is_ll_all_nodes(&ipv6_hdr(skb)->daddr))
		return -EINVAL;

	return 0;
}

static int ipv6_mc_check_mld_msg(struct sk_buff *skb)
{
	struct mld_msg *mld = (struct mld_msg *)skb_transport_header(skb);

	switch (mld->mld_type) {
	case ICMPV6_MGM_REDUCTION:
	case ICMPV6_MGM_REPORT:
		/* fall through */
		return 0;
	case ICMPV6_MLD2_REPORT:
		return ipv6_mc_check_mld_reportv2(skb);
	case ICMPV6_MGM_QUERY:
		return ipv6_mc_check_mld_query(skb);
	default:
		return -ENOMSG;
	}
}

static inline __sum16 ipv6_mc_validate_checksum(struct sk_buff *skb)
{
	return skb_checksum_validate(skb, IPPROTO_ICMPV6, ip6_compute_pseudo);
}

static int __ipv6_mc_check_mld(struct sk_buff *skb,
			       struct sk_buff **skb_trimmed)

{
	struct sk_buff *skb_chk = NULL;
	unsigned int transport_len;
	unsigned int len = skb_transport_offset(skb) + sizeof(struct mld_msg);
	int ret;

	transport_len = ntohs(ipv6_hdr(skb)->payload_len);
	transport_len -= skb_transport_offset(skb) - sizeof(struct ipv6hdr);

	skb_get(skb);
	skb_chk = skb_checksum_trimmed(skb, transport_len,
				       ipv6_mc_validate_checksum);
	if (!skb_chk)
		return -EINVAL;

	if (!pskb_may_pull(skb_chk, len)) {
		kfree_skb(skb_chk);
		return -EINVAL;
	}

	ret = ipv6_mc_check_mld_msg(skb_chk);
	if (ret) {
		kfree_skb(skb_chk);
		return ret;
	}

	if (skb_trimmed)
		*skb_trimmed = skb_chk;
	else
		kfree_skb(skb_chk);

	return 0;
}

/**
 * ipv6_mc_check_mld - checks whether this is a sane MLD packet
 * @skb: the skb to validate
 * @skb_trimmed: to store an skb pointer trimmed to IPv6 packet tail (optional)
 *
 * Checks whether an IPv6 packet is a valid MLD packet. If so sets
 * skb network and transport headers accordingly and returns zero.
 *
 * -EINVAL: A broken packet was detected, i.e. it violates some internet
 *  standard
 * -ENOMSG: IP header validation succeeded but it is not an MLD packet.
 * -ENOMEM: A memory allocation failure happened.
 *
 * Optionally, an skb pointer might be provided via skb_trimmed (or set it
 * to NULL): After parsing an MLD packet successfully it will point to
 * an skb which has its tail aligned to the IP packet end. This might
 * either be the originally provided skb or a trimmed, cloned version if
 * the skb frame had data beyond the IP packet. A cloned skb allows us
 * to leave the original skb and its full frame unchanged (which might be
 * desirable for layer 2 frame jugglers).
 *
 * The caller needs to release a reference count from any returned skb_trimmed.
 */
int ipv6_mc_check_mld(struct sk_buff *skb, struct sk_buff **skb_trimmed)
{
	int ret;

	ret = ipv6_mc_check_ip6hdr(skb);
	if (ret < 0)
		return ret;

	ret = ipv6_mc_check_exthdrs(skb);
	if (ret < 0)
		return ret;

	return __ipv6_mc_check_mld(skb, skb_trimmed);
}
EXPORT_SYMBOL(ipv6_mc_check_mld);
