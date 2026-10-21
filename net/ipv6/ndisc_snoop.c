// SPDX-License-Identifier: GPL-2.0-only

#include <linux/skbuff.h>
#include <net/addrconf.h>
#include <net/ip6_checksum.h>
#include <net/ipv6.h>
#include <net/ndisc.h>

static int ndisc_check_ip6hdr(struct sk_buff *skb)
{
	const struct ipv6hdr *ip6h;
	unsigned int offset, len;

	offset = skb_network_offset(skb) + sizeof(*ip6h);
	if (!pskb_may_pull(skb, offset))
		return -EINVAL;

	ip6h = ipv6_hdr(skb);

	if (ip6h->version != 6)
		return -EINVAL;

	if (ip6h->nexthdr != IPPROTO_ICMPV6)
		return -ENOMSG;

	/* RFC 4861 7.1.1 / 7.1.2: must not have been forwarded by a router */
	if (ip6h->hop_limit != 255)
		return -EINVAL;

	len = offset + ntohs(ip6h->payload_len);
	if (skb->len < len || len <= offset)
		return -EINVAL;

	skb_set_transport_header(skb, offset);

	return 0;
}

static __sum16 ndisc_validate_checksum(struct sk_buff *skb)
{
	return skb_checksum_validate(skb, IPPROTO_ICMPV6, ip6_compute_pseudo);
}

static int ndisc_check_icmpv6(struct sk_buff *skb)
{
	unsigned int len = skb_transport_offset(skb) + sizeof(struct icmp6hdr);
	unsigned int transport_len = ipv6_transport_len(skb);
	struct sk_buff *skb_chk;
	struct icmp6hdr *hdr;

	if (!pskb_may_pull(skb, len))
		return -EINVAL;

	/* RFC 4861 7.1.1 / 7.1.2: the ICMPv6 checksum must be valid */
	skb_chk = skb_checksum_trimmed(skb, transport_len,
				       ndisc_validate_checksum);
	if (!skb_chk)
		return -EINVAL;

	if (skb_chk != skb)
		kfree_skb(skb_chk);

	/* RFC 4861 7.1.1 / 7.1.2: Code must be 0 */
	hdr = (struct icmp6hdr *)skb_transport_header(skb);
	if (hdr->icmp6_code != 0)
		return -EINVAL;

	return 0;
}

static int ndisc_check_options(struct sk_buff *skb, unsigned int opts_len,
			       bool reject_slla)
{
	unsigned int offset = skb_transport_offset(skb) + sizeof(struct nd_msg);
	struct nd_opt_hdr *opt, _opt;

	while (opts_len > 0) {
		if (opts_len < sizeof(*opt))
			return -EINVAL;

		opt = skb_header_pointer(skb, offset, sizeof(_opt), &_opt);
		if (!opt)
			return -EINVAL;

		/* RFC 4861 7.1.1 / 7.1.2: all option lengths must be > 0 */
		if (!opt->nd_opt_len)
			return -EINVAL;

		/* RFC 4861 7.1.1: DAD NS must not contain a source link-layer
		 * address option
		 */
		if (reject_slla && opt->nd_opt_type == ND_OPT_SOURCE_LL_ADDR)
			return -EINVAL;

		if (opt->nd_opt_len * 8 > opts_len)
			return -EINVAL;

		offset += opt->nd_opt_len * 8;
		opts_len -= opt->nd_opt_len * 8;
	}

	return 0;
}

static int ndisc_check_nd_msg(struct sk_buff *skb)
{
	unsigned int len = skb_transport_offset(skb) + sizeof(struct nd_msg);
	unsigned int transport_len = ipv6_transport_len(skb);
	bool reject_slla = false;
	const struct nd_msg *msg;

	if (!pskb_may_pull(skb, len))
		return -EINVAL;

	/* RFC 4861 7.1.1 / 7.1.2: ICMP length is at least sizeof(nd_msg) */
	if (transport_len < sizeof(struct nd_msg))
		return -EINVAL;

	msg = (struct nd_msg *)skb_transport_header(skb);

	/* RFC 4861 7.1.1 / 7.1.2: Target Address must not be a
	 * multicast address
	 */
	if (ipv6_addr_is_multicast(&msg->target))
		return -EINVAL;

	switch (msg->icmph.icmp6_type) {
	case NDISC_NEIGHBOUR_SOLICITATION:
		if (ipv6_addr_any(&ipv6_hdr(skb)->saddr)) {
			/* RFC 4861 7.1.1: DAD NS destination must be a
			 * solicited-node multicast address
			 */
			if (!ipv6_addr_is_solict_mult(&ipv6_hdr(skb)->daddr))
				return -EINVAL;
			/* RFC 4861 7.1.1: DAD NS must not contain a source
			 * link-layer address option
			 */
			reject_slla = true;
		}
		break;
	case NDISC_NEIGHBOUR_ADVERTISEMENT:
		/* RFC 4861 7.1.2: Solicited flag must be 0 for
		 * multicast destinations
		 */
		if (ipv6_addr_is_multicast(&ipv6_hdr(skb)->daddr) &&
		    msg->icmph.icmp6_solicited)
			return -EINVAL;
		break;
	default:
		return -ENODATA;
	}

	return ndisc_check_options(skb, transport_len - sizeof(struct nd_msg),
				   reject_slla);
}

/**
 * ndisc_check_ns_na - validate an NS/NA packet and set its transport header
 * @skb: the skb to validate
 *
 * Validates an IPv6 packet for compliance with RFC 4861 sections 7.1.1
 * (Neighbor Solicitation) and 7.1.2 (Neighbor Advertisement). If valid,
 * sets the skb transport header.
 *
 * Caller needs to set the skb network header.
 *
 * Return:
 * * 0        - valid NS/NA; the skb transport header has been set.
 * * -EINVAL  - a broken packet was detected, i.e. it violates some
 *              internet standard.
 * * -ENOMSG  - IP header validation succeeded but it is not an ICMPv6
 *              packet.
 * * -ENODATA - IP+ICMPv6 header validation succeeded but it is not a
 *              Neighbor Solicitation or Neighbor Advertisement.
 */
int ndisc_check_ns_na(struct sk_buff *skb)
{
	int ret;

	ret = ndisc_check_ip6hdr(skb);
	if (ret < 0)
		return ret;

	ret = ndisc_check_icmpv6(skb);
	if (ret < 0)
		return ret;

	return ndisc_check_nd_msg(skb);
}
EXPORT_SYMBOL_GPL(ndisc_check_ns_na);
