// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	Extension Header handling for IPv6
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *	Andi Kleen		<ak@muc.de>
 *	Alexey Kuznetsov	<kuznet@ms2.inr.ac.ru>
 */

/* Changes:
 *	yoshfuji		: ensure not to overrun while parsing
 *				  tlv options.
 *	Mitsuru KANDA @USAGI and: Remove ipv6_parse_exthdrs().
 *	YOSHIFUJI Hideaki @USAGI  Register inbound extension header
 *				  handlers as inet6_protocol{}.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/in6.h>
#include <linux/icmpv6.h>
#include <linux/slab.h>
#include <linux/export.h>

#include <net/dst.h>
#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/transp_v6.h>
#include <net/rawv6.h>
#include <net/ndisc.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#include <net/calipso.h>
#if IS_ENABLED(CONFIG_IPV6_MIP6)
#include <net/xfrm.h>
#endif
#include <linux/seg6.h>
#include <net/seg6.h>
#ifdef CONFIG_IPV6_SEG6_HMAC
#include <net/seg6_hmac.h>
#endif
#include <net/rpl.h>
#include <linux/ioam6.h>
#include <net/ioam6.h>
#include <net/dst_metadata.h>

#include <linux/uaccess.h>

/*********************
  Generic functions
 *********************/

/* An unknown option is detected, decide what to do */

static bool ip6_tlvopt_unknown(struct sk_buff *skb, int optoff,
			       bool disallow_unknowns)
{
	if (disallow_unknowns) {
		/* If unknown TLVs are disallowed by configuration
		 * then always silently drop packet. Note this also
		 * means no ICMP parameter problem is sent which
		 * could be a good property to mitigate a reflection DOS
		 * attack.
		 */

		goto drop;
	}

	switch ((skb_network_header(skb)[optoff] & 0xC0) >> 6) {
	case 0: /* ignore */
		return true;

	case 1: /* drop packet */
		break;

	case 3: /* Send ICMP if not a multicast address and drop packet */
		/* Actually, it is redundant check. icmp_send
		   will recheck in any case.
		 */
		if (ipv6_addr_is_multicast(&ipv6_hdr(skb)->daddr))
			break;
		fallthrough;
	case 2: /* send ICMP PARM PROB regardless and drop packet */
		icmpv6_param_prob(skb, ICMPV6_UNK_OPTION, optoff);
		return false;
	}

drop:
	kfree_skb(skb);
	return false;
}

static bool ipv6_hop_ra(struct sk_buff *skb, int optoff);
static bool ipv6_hop_ioam(struct sk_buff *skb, int optoff);
static bool ipv6_hop_jumbo(struct sk_buff *skb, int optoff);
static bool ipv6_hop_calipso(struct sk_buff *skb, int optoff);
#if IS_ENABLED(CONFIG_IPV6_MIP6)
static bool ipv6_dest_hao(struct sk_buff *skb, int optoff);
#endif

/* Parse tlv encoded option header (hop-by-hop or destination) */

static bool ip6_parse_tlv(bool hopbyhop,
			  struct sk_buff *skb,
			  int max_count)
{
	int len = (skb_transport_header(skb)[1] + 1) << 3;
	const unsigned char *nh = skb_network_header(skb);
	int off = skb_network_header_len(skb);
	bool disallow_unknowns = false;
	int tlv_count = 0;
	int padlen = 0;

	if (unlikely(max_count < 0)) {
		disallow_unknowns = true;
		max_count = -max_count;
	}

	if (skb_transport_offset(skb) + len > skb_headlen(skb))
		goto bad;

	off += 2;
	len -= 2;

	while (len > 0) {
		int optlen, i;

		if (nh[off] == IPV6_TLV_PAD1) {
			padlen++;
			if (padlen > 7)
				goto bad;
			off++;
			len--;
			continue;
		}
		if (len < 2)
			goto bad;
		optlen = nh[off + 1] + 2;
		if (optlen > len)
			goto bad;

		if (nh[off] == IPV6_TLV_PADN) {
			/* RFC 2460 states that the purpose of PadN is
			 * to align the containing header to multiples
			 * of 8. 7 is therefore the highest valid value.
			 * See also RFC 4942, Section 2.1.9.5.
			 */
			padlen += optlen;
			if (padlen > 7)
				goto bad;
			/* RFC 4942 recommends receiving hosts to
			 * actively check PadN payload to contain
			 * only zeroes.
			 */
			for (i = 2; i < optlen; i++) {
				if (nh[off + i] != 0)
					goto bad;
			}
		} else {
			tlv_count++;
			if (tlv_count > max_count)
				goto bad;

			if (hopbyhop) {
				switch (nh[off]) {
				case IPV6_TLV_ROUTERALERT:
					if (!ipv6_hop_ra(skb, off))
						return false;
					break;
				case IPV6_TLV_IOAM:
					if (!ipv6_hop_ioam(skb, off))
						return false;
					break;
				case IPV6_TLV_JUMBO:
					if (!ipv6_hop_jumbo(skb, off))
						return false;
					break;
				case IPV6_TLV_CALIPSO:
					if (!ipv6_hop_calipso(skb, off))
						return false;
					break;
				default:
					if (!ip6_tlvopt_unknown(skb, off,
								disallow_unknowns))
						return false;
					break;
				}
			} else {
				switch (nh[off]) {
#if IS_ENABLED(CONFIG_IPV6_MIP6)
				case IPV6_TLV_HAO:
					if (!ipv6_dest_hao(skb, off))
						return false;
					break;
#endif
				default:
					if (!ip6_tlvopt_unknown(skb, off,
								disallow_unknowns))
						return false;
					break;
				}
			}
			padlen = 0;
		}
		off += optlen;
		len -= optlen;
	}

	if (len == 0)
		return true;
bad:
	kfree_skb(skb);
	return false;
}

/*****************************
  Destination options header.
 *****************************/

#if IS_ENABLED(CONFIG_IPV6_MIP6)
static bool ipv6_dest_hao(struct sk_buff *skb, int optoff)
{
	struct ipv6_destopt_hao *hao;
	struct inet6_skb_parm *opt = IP6CB(skb);
	struct ipv6hdr *ipv6h = ipv6_hdr(skb);
	int ret;

	if (opt->dsthao) {
		net_dbg_ratelimited("hao duplicated\n");
		goto discard;
	}
	opt->dsthao = opt->dst1;
	opt->dst1 = 0;

	hao = (struct ipv6_destopt_hao *)(skb_network_header(skb) + optoff);

	if (hao->length != 16) {
		net_dbg_ratelimited("hao invalid option length = %d\n",
				    hao->length);
		goto discard;
	}

	if (!(ipv6_addr_type(&hao->addr) & IPV6_ADDR_UNICAST)) {
		net_dbg_ratelimited("hao is not an unicast addr: %pI6\n",
				    &hao->addr);
		goto discard;
	}

	ret = xfrm6_input_addr(skb, (xfrm_address_t *)&ipv6h->daddr,
			       (xfrm_address_t *)&hao->addr, IPPROTO_DSTOPTS);
	if (unlikely(ret < 0))
		goto discard;

	if (skb_cloned(skb)) {
		if (pskb_expand_head(skb, 0, 0, GFP_ATOMIC))
			goto discard;

		/* update all variable using below by copied skbuff */
		hao = (struct ipv6_destopt_hao *)(skb_network_header(skb) +
						  optoff);
		ipv6h = ipv6_hdr(skb);
	}

	if (skb->ip_summed == CHECKSUM_COMPLETE)
		skb->ip_summed = CHECKSUM_NONE;

	swap(ipv6h->saddr, hao->addr);

	if (skb->tstamp == 0)
		__net_timestamp(skb);

	return true;

 discard:
	kfree_skb(skb);
	return false;
}
#endif

static int ipv6_destopt_rcv(struct sk_buff *skb)
{
	struct inet6_dev *idev = __in6_dev_get(skb->dev);
	struct inet6_skb_parm *opt = IP6CB(skb);
#if IS_ENABLED(CONFIG_IPV6_MIP6)
	__u16 dstbuf;
#endif
	struct dst_entry *dst = skb_dst(skb);
	struct net *net = dev_net(skb->dev);
	int extlen;

	if (!pskb_may_pull(skb, skb_transport_offset(skb) + 8) ||
	    !pskb_may_pull(skb, (skb_transport_offset(skb) +
				 ((skb_transport_header(skb)[1] + 1) << 3)))) {
		__IP6_INC_STATS(dev_net(dst->dev), idev,
				IPSTATS_MIB_INHDRERRORS);
fail_and_free:
		kfree_skb(skb);
		return -1;
	}

	extlen = (skb_transport_header(skb)[1] + 1) << 3;
	if (extlen > net->ipv6.sysctl.max_dst_opts_len)
		goto fail_and_free;

	opt->lastopt = opt->dst1 = skb_network_header_len(skb);
#if IS_ENABLED(CONFIG_IPV6_MIP6)
	dstbuf = opt->dst1;
#endif

	if (ip6_parse_tlv(false, skb, net->ipv6.sysctl.max_dst_opts_cnt)) {
		skb->transport_header += extlen;
		opt = IP6CB(skb);
#if IS_ENABLED(CONFIG_IPV6_MIP6)
		opt->nhoff = dstbuf;
#else
		opt->nhoff = opt->dst1;
#endif
		return 1;
	}

	__IP6_INC_STATS(net, idev, IPSTATS_MIB_INHDRERRORS);
	return -1;
}

static void seg6_update_csum(struct sk_buff *skb)
{
	struct ipv6_sr_hdr *hdr;
	struct in6_addr *addr;
	__be32 from, to;

	/* srh is at transport offset and seg_left is already decremented
	 * but daddr is not yet updated with next segment
	 */

	hdr = (struct ipv6_sr_hdr *)skb_transport_header(skb);
	addr = hdr->segments + hdr->segments_left;

	hdr->segments_left++;
	from = *(__be32 *)hdr;

	hdr->segments_left--;
	to = *(__be32 *)hdr;

	/* update skb csum with diff resulting from seg_left decrement */

	update_csum_diff4(skb, from, to);

	/* compute csum diff between current and next segment and update */

	update_csum_diff16(skb, (__be32 *)(&ipv6_hdr(skb)->daddr),
			   (__be32 *)addr);
}

static int ipv6_srh_rcv(struct sk_buff *skb)
{
	struct inet6_skb_parm *opt = IP6CB(skb);
	struct net *net = dev_net(skb->dev);
	struct ipv6_sr_hdr *hdr;
	struct inet6_dev *idev;
	struct in6_addr *addr;
	int accept_seg6;

	hdr = (struct ipv6_sr_hdr *)skb_transport_header(skb);

	idev = __in6_dev_get(skb->dev);

	accept_seg6 = net->ipv6.devconf_all->seg6_enabled;
	if (accept_seg6 > idev->cnf.seg6_enabled)
		accept_seg6 = idev->cnf.seg6_enabled;

	if (!accept_seg6) {
		kfree_skb(skb);
		return -1;
	}

#ifdef CONFIG_IPV6_SEG6_HMAC
	if (!seg6_hmac_validate_skb(skb)) {
		kfree_skb(skb);
		return -1;
	}
#endif

looped_back:
	if (hdr->segments_left == 0) {
		if (hdr->nexthdr == NEXTHDR_IPV6 || hdr->nexthdr == NEXTHDR_IPV4) {
			int offset = (hdr->hdrlen + 1) << 3;

			skb_postpull_rcsum(skb, skb_network_header(skb),
					   skb_network_header_len(skb));

			if (!pskb_pull(skb, offset)) {
				kfree_skb(skb);
				return -1;
			}
			skb_postpull_rcsum(skb, skb_transport_header(skb),
					   offset);

			skb_reset_network_header(skb);
			skb_reset_transport_header(skb);
			skb->encapsulation = 0;
			if (hdr->nexthdr == NEXTHDR_IPV4)
				skb->protocol = htons(ETH_P_IP);
			__skb_tunnel_rx(skb, skb->dev, net);

			netif_rx(skb);
			return -1;
		}

		opt->srcrt = skb_network_header_len(skb);
		opt->lastopt = opt->srcrt;
		skb->transport_header += (hdr->hdrlen + 1) << 3;
		opt->nhoff = (&hdr->nexthdr) - skb_network_header(skb);

		return 1;
	}

	if (hdr->segments_left >= (hdr->hdrlen >> 1)) {
		__IP6_INC_STATS(net, idev, IPSTATS_MIB_INHDRERRORS);
		icmpv6_param_prob(skb, ICMPV6_HDR_FIELD,
				  ((&hdr->segments_left) -
				   skb_network_header(skb)));
		return -1;
	}

	if (skb_cloned(skb)) {
		if (pskb_expand_head(skb, 0, 0, GFP_ATOMIC)) {
			__IP6_INC_STATS(net, ip6_dst_idev(skb_dst(skb)),
					IPSTATS_MIB_OUTDISCARDS);
			kfree_skb(skb);
			return -1;
		}
	}

	hdr = (struct ipv6_sr_hdr *)skb_transport_header(skb);

	hdr->segments_left--;
	addr = hdr->segments + hdr->segments_left;

	skb_push(skb, sizeof(struct ipv6hdr));

	if (skb->ip_summed == CHECKSUM_COMPLETE)
		seg6_update_csum(skb);

	ipv6_hdr(skb)->daddr = *addr;

	skb_dst_drop(skb);

	ip6_route_input(skb);

	if (skb_dst(skb)->error) {
		dst_input(skb);
		return -1;
	}

	if (skb_dst(skb)->dev->flags & IFF_LOOPBACK) {
		if (ipv6_hdr(skb)->hop_limit <= 1) {
			__IP6_INC_STATS(net, idev, IPSTATS_MIB_INHDRERRORS);
			icmpv6_send(skb, ICMPV6_TIME_EXCEED,
				    ICMPV6_EXC_HOPLIMIT, 0);
			kfree_skb(skb);
			return -1;
		}
		ipv6_hdr(skb)->hop_limit--;

		skb_pull(skb, sizeof(struct ipv6hdr));
		goto looped_back;
	}

	dst_input(skb);

	return -1;
}

static int ipv6_rpl_srh_rcv(struct sk_buff *skb)
{
	struct ipv6_rpl_sr_hdr *hdr, *ohdr, *chdr;
	struct inet6_skb_parm *opt = IP6CB(skb);
	struct net *net = dev_net(skb->dev);
	struct inet6_dev *idev;
	struct ipv6hdr *oldhdr;
	struct in6_addr addr;
	unsigned char *buf;
	int accept_rpl_seg;
	int i, err;
	u64 n = 0;
	u32 r;

	idev = __in6_dev_get(skb->dev);

	accept_rpl_seg = net->ipv6.devconf_all->rpl_seg_enabled;
	if (accept_rpl_seg > idev->cnf.rpl_seg_enabled)
		accept_rpl_seg = idev->cnf.rpl_seg_enabled;

	if (!accept_rpl_seg) {
		kfree_skb(skb);
		return -1;
	}

looped_back:
	hdr = (struct ipv6_rpl_sr_hdr *)skb_transport_header(skb);

	if (hdr->segments_left == 0) {
		if (hdr->nexthdr == NEXTHDR_IPV6) {
			int offset = (hdr->hdrlen + 1) << 3;

			skb_postpull_rcsum(skb, skb_network_header(skb),
					   skb_network_header_len(skb));

			if (!pskb_pull(skb, offset)) {
				kfree_skb(skb);
				return -1;
			}
			skb_postpull_rcsum(skb, skb_transport_header(skb),
					   offset);

			skb_reset_network_header(skb);
			skb_reset_transport_header(skb);
			skb->encapsulation = 0;

			__skb_tunnel_rx(skb, skb->dev, net);

			netif_rx(skb);
			return -1;
		}

		opt->srcrt = skb_network_header_len(skb);
		opt->lastopt = opt->srcrt;
		skb->transport_header += (hdr->hdrlen + 1) << 3;
		opt->nhoff = (&hdr->nexthdr) - skb_network_header(skb);

		return 1;
	}

	if (!pskb_may_pull(skb, sizeof(*hdr))) {
		kfree_skb(skb);
		return -1;
	}

	n = (hdr->hdrlen << 3) - hdr->pad - (16 - hdr->cmpre);
	r = do_div(n, (16 - hdr->cmpri));
	/* checks if calculation was without remainder and n fits into
	 * unsigned char which is segments_left field. Should not be
	 * higher than that.
	 */
	if (r || (n + 1) > 255) {
		kfree_skb(skb);
		return -1;
	}

	if (hdr->segments_left > n + 1) {
		__IP6_INC_STATS(net, idev, IPSTATS_MIB_INHDRERRORS);
		icmpv6_param_prob(skb, ICMPV6_HDR_FIELD,
				  ((&hdr->segments_left) -
				   skb_network_header(skb)));
		return -1;
	}

	if (skb_cloned(skb)) {
		if (pskb_expand_head(skb, IPV6_RPL_SRH_WORST_SWAP_SIZE, 0,
				     GFP_ATOMIC)) {
			__IP6_INC_STATS(net, ip6_dst_idev(skb_dst(skb)),
					IPSTATS_MIB_OUTDISCARDS);
			kfree_skb(skb);
			return -1;
		}
	} else {
		err = skb_cow_head(skb, IPV6_RPL_SRH_WORST_SWAP_SIZE);
		if (unlikely(err)) {
			kfree_skb(skb);
			return -1;
		}
	}

	hdr = (struct ipv6_rpl_sr_hdr *)skb_transport_header(skb);

	if (!pskb_may_pull(skb, ipv6_rpl_srh_size(n, hdr->cmpri,
						  hdr->cmpre))) {
		kfree_skb(skb);
		return -1;
	}

	hdr->segments_left--;
	i = n - hdr->segments_left;

	buf = kcalloc(struct_size(hdr, segments.addr, n + 2), 2, GFP_ATOMIC);
	if (unlikely(!buf)) {
		kfree_skb(skb);
		return -1;
	}

	ohdr = (struct ipv6_rpl_sr_hdr *)buf;
	ipv6_rpl_srh_decompress(ohdr, hdr, &ipv6_hdr(skb)->daddr, n);
	chdr = (struct ipv6_rpl_sr_hdr *)(buf + ((ohdr->hdrlen + 1) << 3));

	if ((ipv6_addr_type(&ipv6_hdr(skb)->daddr) & IPV6_ADDR_MULTICAST) ||
	    (ipv6_addr_type(&ohdr->rpl_segaddr[i]) & IPV6_ADDR_MULTICAST)) {
		kfree_skb(skb);
		kfree(buf);
		return -1;
	}

	err = ipv6_chk_rpl_srh_loop(net, ohdr->rpl_segaddr, n + 1);
	if (err) {
		icmpv6_send(skb, ICMPV6_PARAMPROB, 0, 0);
		kfree_skb(skb);
		kfree(buf);
		return -1;
	}

	addr = ipv6_hdr(skb)->daddr;
	ipv6_hdr(skb)->daddr = ohdr->rpl_segaddr[i];
	ohdr->rpl_segaddr[i] = addr;

	ipv6_rpl_srh_compress(chdr, ohdr, &ipv6_hdr(skb)->daddr, n);

	oldhdr = ipv6_hdr(skb);

	skb_pull(skb, ((hdr->hdrlen + 1) << 3));
	skb_postpull_rcsum(skb, oldhdr,
			   sizeof(struct ipv6hdr) + ((hdr->hdrlen + 1) << 3));
	skb_push(skb, ((chdr->hdrlen + 1) << 3) + sizeof(struct ipv6hdr));
	skb_reset_network_header(skb);
	skb_mac_header_rebuild(skb);
	skb_set_transport_header(skb, sizeof(struct ipv6hdr));

	memmove(ipv6_hdr(skb), oldhdr, sizeof(struct ipv6hdr));
	memcpy(skb_transport_header(skb), chdr, (chdr->hdrlen + 1) << 3);

	ipv6_hdr(skb)->payload_len = htons(skb->len - sizeof(struct ipv6hdr));
	skb_postpush_rcsum(skb, ipv6_hdr(skb),
			   sizeof(struct ipv6hdr) + ((chdr->hdrlen + 1) << 3));

	kfree(buf);

	skb_dst_drop(skb);

	ip6_route_input(skb);

	if (skb_dst(skb)->error) {
		dst_input(skb);
		return -1;
	}

	if (skb_dst(skb)->dev->flags & IFF_LOOPBACK) {
		if (ipv6_hdr(skb)->hop_limit <= 1) {
			__IP6_INC_STATS(net, idev, IPSTATS_MIB_INHDRERRORS);
			icmpv6_send(skb, ICMPV6_TIME_EXCEED,
				    ICMPV6_EXC_HOPLIMIT, 0);
			kfree_skb(skb);
			return -1;
		}
		ipv6_hdr(skb)->hop_limit--;

		skb_pull(skb, sizeof(struct ipv6hdr));
		goto looped_back;
	}

	dst_input(skb);

	return -1;
}

/********************************
  Routing header.
 ********************************/

/* called with rcu_read_lock() */
static int ipv6_rthdr_rcv(struct sk_buff *skb)
{
	struct inet6_dev *idev = __in6_dev_get(skb->dev);
	struct inet6_skb_parm *opt = IP6CB(skb);
	struct in6_addr *addr = NULL;
	struct in6_addr daddr;
	int n, i;
	struct ipv6_rt_hdr *hdr;
	struct rt0_hdr *rthdr;
	struct net *net = dev_net(skb->dev);
	int accept_source_route = net->ipv6.devconf_all->accept_source_route;

	idev = __in6_dev_get(skb->dev);
	if (idev && accept_source_route > idev->cnf.accept_source_route)
		accept_source_route = idev->cnf.accept_source_route;

	if (!pskb_may_pull(skb, skb_transport_offset(skb) + 8) ||
	    !pskb_may_pull(skb, (skb_transport_offset(skb) +
				 ((skb_transport_header(skb)[1] + 1) << 3)))) {
		__IP6_INC_STATS(net, idev, IPSTATS_MIB_INHDRERRORS);
		kfree_skb(skb);
		return -1;
	}

	hdr = (struct ipv6_rt_hdr *)skb_transport_header(skb);

	if (ipv6_addr_is_multicast(&ipv6_hdr(skb)->daddr) ||
	    skb->pkt_type != PACKET_HOST) {
		__IP6_INC_STATS(net, idev, IPSTATS_MIB_INADDRERRORS);
		kfree_skb(skb);
		return -1;
	}

	switch (hdr->type) {
	case IPV6_SRCRT_TYPE_4:
		/* segment routing */
		return ipv6_srh_rcv(skb);
	case IPV6_SRCRT_TYPE_3:
		/* rpl segment routing */
		return ipv6_rpl_srh_rcv(skb);
	default:
		break;
	}

looped_back:
	if (hdr->segments_left == 0) {
		switch (hdr->type) {
#if IS_ENABLED(CONFIG_IPV6_MIP6)
		case IPV6_SRCRT_TYPE_2:
			/* Silently discard type 2 header unless it was
			 * processed by own
			 */
			if (!addr) {
				__IP6_INC_STATS(net, idev,
						IPSTATS_MIB_INADDRERRORS);
				kfree_skb(skb);
				return -1;
			}
			break;
#endif
		default:
			break;
		}

		opt->lastopt = opt->srcrt = skb_network_header_len(skb);
		skb->transport_header += (hdr->hdrlen + 1) << 3;
		opt->dst0 = opt->dst1;
		opt->dst1 = 0;
		opt->nhoff = (&hdr->nexthdr) - skb_network_header(skb);
		return 1;
	}

	switch (hdr->type) {
#if IS_ENABLED(CONFIG_IPV6_MIP6)
	case IPV6_SRCRT_TYPE_2:
		if (accept_source_route < 0)
			goto unknown_rh;
		/* Silently discard invalid RTH type 2 */
		if (hdr->hdrlen != 2 || hdr->segments_left != 1) {
			__IP6_INC_STATS(net, idev, IPSTATS_MIB_INHDRERRORS);
			kfree_skb(skb);
			return -1;
		}
		break;
#endif
	default:
		goto unknown_rh;
	}

	/*
	 *	This is the routing header forwarding algorithm from
	 *	RFC 2460, page 16.
	 */

	n = hdr->hdrlen >> 1;

	if (hdr->segments_left > n) {
		__IP6_INC_STATS(net, idev, IPSTATS_MIB_INHDRERRORS);
		icmpv6_param_prob(skb, ICMPV6_HDR_FIELD,
				  ((&hdr->segments_left) -
				   skb_network_header(skb)));
		return -1;
	}

	/* We are about to mangle packet header. Be careful!
	   Do not damage packets queued somewhere.
	 */
	if (skb_cloned(skb)) {
		/* the copy is a forwarded packet */
		if (pskb_expand_head(skb, 0, 0, GFP_ATOMIC)) {
			__IP6_INC_STATS(net, ip6_dst_idev(skb_dst(skb)),
					IPSTATS_MIB_OUTDISCARDS);
			kfree_skb(skb);
			return -1;
		}
		hdr = (struct ipv6_rt_hdr *)skb_transport_header(skb);
	}

	if (skb->ip_summed == CHECKSUM_COMPLETE)
		skb->ip_summed = CHECKSUM_NONE;

	i = n - --hdr->segments_left;

	rthdr = (struct rt0_hdr *) hdr;
	addr = rthdr->addr;
	addr += i - 1;

	switch (hdr->type) {
#if IS_ENABLED(CONFIG_IPV6_MIP6)
	case IPV6_SRCRT_TYPE_2:
		if (xfrm6_input_addr(skb, (xfrm_address_t *)addr,
				     (xfrm_address_t *)&ipv6_hdr(skb)->saddr,
				     IPPROTO_ROUTING) < 0) {
			__IP6_INC_STATS(net, idev, IPSTATS_MIB_INADDRERRORS);
			kfree_skb(skb);
			return -1;
		}
		if (!ipv6_chk_home_addr(dev_net(skb_dst(skb)->dev), addr)) {
			__IP6_INC_STATS(net, idev, IPSTATS_MIB_INADDRERRORS);
			kfree_skb(skb);
			return -1;
		}
		break;
#endif
	default:
		break;
	}

	if (ipv6_addr_is_multicast(addr)) {
		__IP6_INC_STATS(net, idev, IPSTATS_MIB_INADDRERRORS);
		kfree_skb(skb);
		return -1;
	}

	daddr = *addr;
	*addr = ipv6_hdr(skb)->daddr;
	ipv6_hdr(skb)->daddr = daddr;

	skb_dst_drop(skb);
	ip6_route_input(skb);
	if (skb_dst(skb)->error) {
		skb_push(skb, skb->data - skb_network_header(skb));
		dst_input(skb);
		return -1;
	}

	if (skb_dst(skb)->dev->flags&IFF_LOOPBACK) {
		if (ipv6_hdr(skb)->hop_limit <= 1) {
			__IP6_INC_STATS(net, idev, IPSTATS_MIB_INHDRERRORS);
			icmpv6_send(skb, ICMPV6_TIME_EXCEED, ICMPV6_EXC_HOPLIMIT,
				    0);
			kfree_skb(skb);
			return -1;
		}
		ipv6_hdr(skb)->hop_limit--;
		goto looped_back;
	}

	skb_push(skb, skb->data - skb_network_header(skb));
	dst_input(skb);
	return -1;

unknown_rh:
	__IP6_INC_STATS(net, idev, IPSTATS_MIB_INHDRERRORS);
	icmpv6_param_prob(skb, ICMPV6_HDR_FIELD,
			  (&hdr->type) - skb_network_header(skb));
	return -1;
}

static const struct inet6_protocol rthdr_protocol = {
	.handler	=	ipv6_rthdr_rcv,
	.flags		=	INET6_PROTO_NOPOLICY,
};

static const struct inet6_protocol destopt_protocol = {
	.handler	=	ipv6_destopt_rcv,
	.flags		=	INET6_PROTO_NOPOLICY,
};

static const struct inet6_protocol nodata_protocol = {
	.handler	=	dst_discard,
	.flags		=	INET6_PROTO_NOPOLICY,
};

int __init ipv6_exthdrs_init(void)
{
	int ret;

	ret = inet6_add_protocol(&rthdr_protocol, IPPROTO_ROUTING);
	if (ret)
		goto out;

	ret = inet6_add_protocol(&destopt_protocol, IPPROTO_DSTOPTS);
	if (ret)
		goto out_rthdr;

	ret = inet6_add_protocol(&nodata_protocol, IPPROTO_NONE);
	if (ret)
		goto out_destopt;

out:
	return ret;
out_destopt:
	inet6_del_protocol(&destopt_protocol, IPPROTO_DSTOPTS);
out_rthdr:
	inet6_del_protocol(&rthdr_protocol, IPPROTO_ROUTING);
	goto out;
};

void ipv6_exthdrs_exit(void)
{
	inet6_del_protocol(&nodata_protocol, IPPROTO_NONE);
	inet6_del_protocol(&destopt_protocol, IPPROTO_DSTOPTS);
	inet6_del_protocol(&rthdr_protocol, IPPROTO_ROUTING);
}

/**********************************
  Hop-by-hop options.
 **********************************/

/*
 * Note: we cannot rely on skb_dst(skb) before we assign it in ip6_route_input().
 */
static inline struct net *ipv6_skb_net(struct sk_buff *skb)
{
	return skb_dst(skb) ? dev_net(skb_dst(skb)->dev) : dev_net(skb->dev);
}

/* Router Alert as of RFC 2711 */

static bool ipv6_hop_ra(struct sk_buff *skb, int optoff)
{
	const unsigned char *nh = skb_network_header(skb);

	if (nh[optoff + 1] == 2) {
		IP6CB(skb)->flags |= IP6SKB_ROUTERALERT;
		memcpy(&IP6CB(skb)->ra, nh + optoff + 2, sizeof(IP6CB(skb)->ra));
		return true;
	}
	net_dbg_ratelimited("ipv6_hop_ra: wrong RA length %d\n",
			    nh[optoff + 1]);
	kfree_skb(skb);
	return false;
}

/* IOAM */

static bool ipv6_hop_ioam(struct sk_buff *skb, int optoff)
{
	struct ioam6_trace_hdr *trace;
	struct ioam6_namespace *ns;
	struct ioam6_hdr *hdr;

	/* Bad alignment (must be 4n-aligned) */
	if (optoff & 3)
		goto drop;

	/* Ignore if IOAM is not enabled on ingress */
	if (!__in6_dev_get(skb->dev)->cnf.ioam6_enabled)
		goto ignore;

	/* Truncated Option header */
	hdr = (struct ioam6_hdr *)(skb_network_header(skb) + optoff);
	if (hdr->opt_len < 2)
		goto drop;

	switch (hdr->type) {
	case IOAM6_TYPE_PREALLOC:
		/* Truncated Pre-allocated Trace header */
		if (hdr->opt_len < 2 + sizeof(*trace))
			goto drop;

		/* Malformed Pre-allocated Trace header */
		trace = (struct ioam6_trace_hdr *)((u8 *)hdr + sizeof(*hdr));
		if (hdr->opt_len < 2 + sizeof(*trace) + trace->remlen * 4)
			goto drop;

		/* Ignore if the IOAM namespace is unknown */
		ns = ioam6_namespace(ipv6_skb_net(skb), trace->namespace_id);
		if (!ns)
			goto ignore;

		if (!skb_valid_dst(skb))
			ip6_route_input(skb);

		ioam6_fill_trace_data(skb, ns, trace, true);
		break;
	default:
		break;
	}

ignore:
	return true;

drop:
	kfree_skb(skb);
	return false;
}

/* Jumbo payload */

static bool ipv6_hop_jumbo(struct sk_buff *skb, int optoff)
{
	const unsigned char *nh = skb_network_header(skb);
	struct inet6_dev *idev = __in6_dev_get_safely(skb->dev);
	struct net *net = ipv6_skb_net(skb);
	u32 pkt_len;

	if (nh[optoff + 1] != 4 || (optoff & 3) != 2) {
		net_dbg_ratelimited("ipv6_hop_jumbo: wrong jumbo opt length/alignment %d\n",
				    nh[optoff+1]);
		__IP6_INC_STATS(net, idev, IPSTATS_MIB_INHDRERRORS);
		goto drop;
	}

	pkt_len = ntohl(*(__be32 *)(nh + optoff + 2));
	if (pkt_len <= IPV6_MAXPLEN) {
		__IP6_INC_STATS(net, idev, IPSTATS_MIB_INHDRERRORS);
		icmpv6_param_prob(skb, ICMPV6_HDR_FIELD, optoff+2);
		return false;
	}
	if (ipv6_hdr(skb)->payload_len) {
		__IP6_INC_STATS(net, idev, IPSTATS_MIB_INHDRERRORS);
		icmpv6_param_prob(skb, ICMPV6_HDR_FIELD, optoff);
		return false;
	}

	if (pkt_len > skb->len - sizeof(struct ipv6hdr)) {
		__IP6_INC_STATS(net, idev, IPSTATS_MIB_INTRUNCATEDPKTS);
		goto drop;
	}

	if (pskb_trim_rcsum(skb, pkt_len + sizeof(struct ipv6hdr)))
		goto drop;

	IP6CB(skb)->flags |= IP6SKB_JUMBOGRAM;
	return true;

drop:
	kfree_skb(skb);
	return false;
}

/* CALIPSO RFC 5570 */

static bool ipv6_hop_calipso(struct sk_buff *skb, int optoff)
{
	const unsigned char *nh = skb_network_header(skb);

	if (nh[optoff + 1] < 8)
		goto drop;

	if (nh[optoff + 6] * 4 + 8 > nh[optoff + 1])
		goto drop;

	if (!calipso_validate(skb, nh + optoff))
		goto drop;

	return true;

drop:
	kfree_skb(skb);
	return false;
}

int ipv6_parse_hopopts(struct sk_buff *skb)
{
	struct inet6_skb_parm *opt = IP6CB(skb);
	struct net *net = dev_net(skb->dev);
	int extlen;

	/*
	 * skb_network_header(skb) is equal to skb->data, and
	 * skb_network_header_len(skb) is always equal to
	 * sizeof(struct ipv6hdr) by definition of
	 * hop-by-hop options.
	 */
	if (!pskb_may_pull(skb, sizeof(struct ipv6hdr) + 8) ||
	    !pskb_may_pull(skb, (sizeof(struct ipv6hdr) +
				 ((skb_transport_header(skb)[1] + 1) << 3)))) {
fail_and_free:
		kfree_skb(skb);
		return -1;
	}

	extlen = (skb_transport_header(skb)[1] + 1) << 3;
	if (extlen > net->ipv6.sysctl.max_hbh_opts_len)
		goto fail_and_free;

	opt->flags |= IP6SKB_HOPBYHOP;
	if (ip6_parse_tlv(true, skb, net->ipv6.sysctl.max_hbh_opts_cnt)) {
		skb->transport_header += extlen;
		opt = IP6CB(skb);
		opt->nhoff = sizeof(struct ipv6hdr);
		return 1;
	}
	return -1;
}

/*
 *	Creating outbound headers.
 *
 *	"build" functions work when skb is filled from head to tail (datagram)
 *	"push"	functions work when headers are added from tail to head (tcp)
 *
 *	In both cases we assume, that caller reserved enough room
 *	for headers.
 */

static void ipv6_push_rthdr0(struct sk_buff *skb, u8 *proto,
			     struct ipv6_rt_hdr *opt,
			     struct in6_addr **addr_p, struct in6_addr *saddr)
{
	struct rt0_hdr *phdr, *ihdr;
	int hops;

	ihdr = (struct rt0_hdr *) opt;

	phdr = skb_push(skb, (ihdr->rt_hdr.hdrlen + 1) << 3);
	memcpy(phdr, ihdr, sizeof(struct rt0_hdr));

	hops = ihdr->rt_hdr.hdrlen >> 1;

	if (hops > 1)
		memcpy(phdr->addr, ihdr->addr + 1,
		       (hops - 1) * sizeof(struct in6_addr));

	phdr->addr[hops - 1] = **addr_p;
	*addr_p = ihdr->addr;

	phdr->rt_hdr.nexthdr = *proto;
	*proto = NEXTHDR_ROUTING;
}

static void ipv6_push_rthdr4(struct sk_buff *skb, u8 *proto,
			     struct ipv6_rt_hdr *opt,
			     struct in6_addr **addr_p, struct in6_addr *saddr)
{
	struct ipv6_sr_hdr *sr_phdr, *sr_ihdr;
	int plen, hops;

	sr_ihdr = (struct ipv6_sr_hdr *)opt;
	plen = (sr_ihdr->hdrlen + 1) << 3;

	sr_phdr = skb_push(skb, plen);
	memcpy(sr_phdr, sr_ihdr, sizeof(struct ipv6_sr_hdr));

	hops = sr_ihdr->first_segment + 1;
	memcpy(sr_phdr->segments + 1, sr_ihdr->segments + 1,
	       (hops - 1) * sizeof(struct in6_addr));

	sr_phdr->segments[0] = **addr_p;
	*addr_p = &sr_ihdr->segments[sr_ihdr->segments_left];

	if (sr_ihdr->hdrlen > hops * 2) {
		int tlvs_offset, tlvs_length;

		tlvs_offset = (1 + hops * 2) << 3;
		tlvs_length = (sr_ihdr->hdrlen - hops * 2) << 3;
		memcpy((char *)sr_phdr + tlvs_offset,
		       (char *)sr_ihdr + tlvs_offset, tlvs_length);
	}

#ifdef CONFIG_IPV6_SEG6_HMAC
	if (sr_has_hmac(sr_phdr)) {
		struct net *net = NULL;

		if (skb->dev)
			net = dev_net(skb->dev);
		else if (skb->sk)
			net = sock_net(skb->sk);

		WARN_ON(!net);

		if (net)
			seg6_push_hmac(net, saddr, sr_phdr);
	}
#endif

	sr_phdr->nexthdr = *proto;
	*proto = NEXTHDR_ROUTING;
}

static void ipv6_push_rthdr(struct sk_buff *skb, u8 *proto,
			    struct ipv6_rt_hdr *opt,
			    struct in6_addr **addr_p, struct in6_addr *saddr)
{
	switch (opt->type) {
	case IPV6_SRCRT_TYPE_0:
	case IPV6_SRCRT_STRICT:
	case IPV6_SRCRT_TYPE_2:
		ipv6_push_rthdr0(skb, proto, opt, addr_p, saddr);
		break;
	case IPV6_SRCRT_TYPE_4:
		ipv6_push_rthdr4(skb, proto, opt, addr_p, saddr);
		break;
	default:
		break;
	}
}

static void ipv6_push_exthdr(struct sk_buff *skb, u8 *proto, u8 type, struct ipv6_opt_hdr *opt)
{
	struct ipv6_opt_hdr *h = skb_push(skb, ipv6_optlen(opt));

	memcpy(h, opt, ipv6_optlen(opt));
	h->nexthdr = *proto;
	*proto = type;
}

void ipv6_push_nfrag_opts(struct sk_buff *skb, struct ipv6_txoptions *opt,
			  u8 *proto,
			  struct in6_addr **daddr, struct in6_addr *saddr)
{
	if (opt->srcrt) {
		ipv6_push_rthdr(skb, proto, opt->srcrt, daddr, saddr);
		/*
		 * IPV6_RTHDRDSTOPTS is ignored
		 * unless IPV6_RTHDR is set (RFC3542).
		 */
		if (opt->dst0opt)
			ipv6_push_exthdr(skb, proto, NEXTHDR_DEST, opt->dst0opt);
	}
	if (opt->hopopt)
		ipv6_push_exthdr(skb, proto, NEXTHDR_HOP, opt->hopopt);
}

void ipv6_push_frag_opts(struct sk_buff *skb, struct ipv6_txoptions *opt, u8 *proto)
{
	if (opt->dst1opt)
		ipv6_push_exthdr(skb, proto, NEXTHDR_DEST, opt->dst1opt);
}
EXPORT_SYMBOL(ipv6_push_frag_opts);

struct ipv6_txoptions *
ipv6_dup_options(struct sock *sk, struct ipv6_txoptions *opt)
{
	struct ipv6_txoptions *opt2;

	opt2 = sock_kmalloc(sk, opt->tot_len, GFP_ATOMIC);
	if (opt2) {
		long dif = (char *)opt2 - (char *)opt;
		memcpy(opt2, opt, opt->tot_len);
		if (opt2->hopopt)
			*((char **)&opt2->hopopt) += dif;
		if (opt2->dst0opt)
			*((char **)&opt2->dst0opt) += dif;
		if (opt2->dst1opt)
			*((char **)&opt2->dst1opt) += dif;
		if (opt2->srcrt)
			*((char **)&opt2->srcrt) += dif;
		refcount_set(&opt2->refcnt, 1);
	}
	return opt2;
}
EXPORT_SYMBOL_GPL(ipv6_dup_options);

static void ipv6_renew_option(int renewtype,
			      struct ipv6_opt_hdr **dest,
			      struct ipv6_opt_hdr *old,
			      struct ipv6_opt_hdr *new,
			      int newtype, char **p)
{
	struct ipv6_opt_hdr *src;

	src = (renewtype == newtype ? new : old);
	if (!src)
		return;

	memcpy(*p, src, ipv6_optlen(src));
	*dest = (struct ipv6_opt_hdr *)*p;
	*p += CMSG_ALIGN(ipv6_optlen(*dest));
}

/**
 * ipv6_renew_options - replace a specific ext hdr with a new one.
 *
 * @sk: sock from which to allocate memory
 * @opt: original options
 * @newtype: option type to replace in @opt
 * @newopt: new option of type @newtype to replace (user-mem)
 *
 * Returns a new set of options which is a copy of @opt with the
 * option type @newtype replaced with @newopt.
 *
 * @opt may be NULL, in which case a new set of options is returned
 * containing just @newopt.
 *
 * @newopt may be NULL, in which case the specified option type is
 * not copied into the new set of options.
 *
 * The new set of options is allocated from the socket option memory
 * buffer of @sk.
 */
struct ipv6_txoptions *
ipv6_renew_options(struct sock *sk, struct ipv6_txoptions *opt,
		   int newtype, struct ipv6_opt_hdr *newopt)
{
	int tot_len = 0;
	char *p;
	struct ipv6_txoptions *opt2;

	if (opt) {
		if (newtype != IPV6_HOPOPTS && opt->hopopt)
			tot_len += CMSG_ALIGN(ipv6_optlen(opt->hopopt));
		if (newtype != IPV6_RTHDRDSTOPTS && opt->dst0opt)
			tot_len += CMSG_ALIGN(ipv6_optlen(opt->dst0opt));
		if (newtype != IPV6_RTHDR && opt->srcrt)
			tot_len += CMSG_ALIGN(ipv6_optlen(opt->srcrt));
		if (newtype != IPV6_DSTOPTS && opt->dst1opt)
			tot_len += CMSG_ALIGN(ipv6_optlen(opt->dst1opt));
	}

	if (newopt)
		tot_len += CMSG_ALIGN(ipv6_optlen(newopt));

	if (!tot_len)
		return NULL;

	tot_len += sizeof(*opt2);
	opt2 = sock_kmalloc(sk, tot_len, GFP_ATOMIC);
	if (!opt2)
		return ERR_PTR(-ENOBUFS);

	memset(opt2, 0, tot_len);
	refcount_set(&opt2->refcnt, 1);
	opt2->tot_len = tot_len;
	p = (char *)(opt2 + 1);

	ipv6_renew_option(IPV6_HOPOPTS, &opt2->hopopt,
			  (opt ? opt->hopopt : NULL),
			  newopt, newtype, &p);
	ipv6_renew_option(IPV6_RTHDRDSTOPTS, &opt2->dst0opt,
			  (opt ? opt->dst0opt : NULL),
			  newopt, newtype, &p);
	ipv6_renew_option(IPV6_RTHDR,
			  (struct ipv6_opt_hdr **)&opt2->srcrt,
			  (opt ? (struct ipv6_opt_hdr *)opt->srcrt : NULL),
			  newopt, newtype, &p);
	ipv6_renew_option(IPV6_DSTOPTS, &opt2->dst1opt,
			  (opt ? opt->dst1opt : NULL),
			  newopt, newtype, &p);

	opt2->opt_nflen = (opt2->hopopt ? ipv6_optlen(opt2->hopopt) : 0) +
			  (opt2->dst0opt ? ipv6_optlen(opt2->dst0opt) : 0) +
			  (opt2->srcrt ? ipv6_optlen(opt2->srcrt) : 0);
	opt2->opt_flen = (opt2->dst1opt ? ipv6_optlen(opt2->dst1opt) : 0);

	return opt2;
}

struct ipv6_txoptions *ipv6_fixup_options(struct ipv6_txoptions *opt_space,
					  struct ipv6_txoptions *opt)
{
	/*
	 * ignore the dest before srcrt unless srcrt is being included.
	 * --yoshfuji
	 */
	if (opt && opt->dst0opt && !opt->srcrt) {
		if (opt_space != opt) {
			memcpy(opt_space, opt, sizeof(*opt_space));
			opt = opt_space;
		}
		opt->opt_nflen -= ipv6_optlen(opt->dst0opt);
		opt->dst0opt = NULL;
	}

	return opt;
}
EXPORT_SYMBOL_GPL(ipv6_fixup_options);

/**
 * fl6_update_dst - update flowi destination address with info given
 *                  by srcrt option, if any.
 *
 * @fl6: flowi6 for which daddr is to be updated
 * @opt: struct ipv6_txoptions in which to look for srcrt opt
 * @orig: copy of original daddr address if modified
 *
 * Returns NULL if no txoptions or no srcrt, otherwise returns orig
 * and initial value of fl6->daddr set in orig
 */
struct in6_addr *fl6_update_dst(struct flowi6 *fl6,
				const struct ipv6_txoptions *opt,
				struct in6_addr *orig)
{
	if (!opt || !opt->srcrt)
		return NULL;

	*orig = fl6->daddr;

	switch (opt->srcrt->type) {
	case IPV6_SRCRT_TYPE_0:
	case IPV6_SRCRT_STRICT:
	case IPV6_SRCRT_TYPE_2:
		fl6->daddr = *((struct rt0_hdr *)opt->srcrt)->addr;
		break;
	case IPV6_SRCRT_TYPE_4:
	{
		struct ipv6_sr_hdr *srh = (struct ipv6_sr_hdr *)opt->srcrt;

		fl6->daddr = srh->segments[srh->segments_left];
		break;
	}
	default:
		return NULL;
	}

	return orig;
}
EXPORT_SYMBOL_GPL(fl6_update_dst);
