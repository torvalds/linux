// SPDX-License-Identifier: GPL-2.0-only
/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 */

#include <linux/module.h>
#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <net/ip6_fib.h>
#include <net/ip6_checksum.h>
#include <net/netfilter/ipv6/nf_reject.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter_bridge.h>

static bool nf_reject_v6_csum_ok(struct sk_buff *skb, int hook)
{
	const struct ipv6hdr *ip6h = ipv6_hdr(skb);
	int thoff;
	__be16 fo;
	u8 proto = ip6h->nexthdr;

	if (skb_csum_unnecessary(skb))
		return true;

	if (ip6h->payload_len &&
	    pskb_trim_rcsum(skb, ntohs(ip6h->payload_len) + sizeof(*ip6h)))
		return false;

	ip6h = ipv6_hdr(skb);
	thoff = ipv6_skip_exthdr(skb, ((u8*)(ip6h+1) - skb->data), &proto, &fo);
	if (thoff < 0 || thoff >= skb->len || (fo & htons(~0x7)) != 0)
		return false;

	if (!nf_reject_verify_csum(proto))
		return true;

	return nf_ip6_checksum(skb, hook, thoff, proto) == 0;
}

static int nf_reject_ip6hdr_validate(struct sk_buff *skb)
{
	struct ipv6hdr *hdr;
	u32 pkt_len;

	if (!pskb_may_pull(skb, sizeof(struct ipv6hdr)))
		return 0;

	hdr = ipv6_hdr(skb);
	if (hdr->version != 6)
		return 0;

	pkt_len = ntohs(hdr->payload_len);
	if (pkt_len + sizeof(struct ipv6hdr) > skb->len)
		return 0;

	return 1;
}

struct sk_buff *nf_reject_skb_v6_tcp_reset(struct net *net,
					   struct sk_buff *oldskb,
					   const struct net_device *dev,
					   int hook)
{
	struct sk_buff *nskb;
	const struct tcphdr *oth;
	struct tcphdr _oth;
	unsigned int otcplen;
	struct ipv6hdr *nip6h;

	if (!nf_reject_ip6hdr_validate(oldskb))
		return NULL;

	oth = nf_reject_ip6_tcphdr_get(oldskb, &_oth, &otcplen, hook);
	if (!oth)
		return NULL;

	nskb = alloc_skb(sizeof(struct ipv6hdr) + sizeof(struct tcphdr) +
			 LL_MAX_HEADER, GFP_ATOMIC);
	if (!nskb)
		return NULL;

	nskb->dev = (struct net_device *)dev;

	skb_reserve(nskb, LL_MAX_HEADER);
	nip6h = nf_reject_ip6hdr_put(nskb, oldskb, IPPROTO_TCP,
				     net->ipv6.devconf_all->hop_limit);
	nf_reject_ip6_tcphdr_put(nskb, oldskb, oth, otcplen);
	nip6h->payload_len = htons(nskb->len - sizeof(struct ipv6hdr));

	return nskb;
}
EXPORT_SYMBOL_GPL(nf_reject_skb_v6_tcp_reset);

struct sk_buff *nf_reject_skb_v6_unreach(struct net *net,
					 struct sk_buff *oldskb,
					 const struct net_device *dev,
					 int hook, u8 code)
{
	struct sk_buff *nskb;
	struct ipv6hdr *nip6h;
	struct icmp6hdr *icmp6h;
	unsigned int len;

	if (!nf_reject_ip6hdr_validate(oldskb))
		return NULL;

	/* Include "As much of invoking packet as possible without the ICMPv6
	 * packet exceeding the minimum IPv6 MTU" in the ICMP payload.
	 */
	len = min_t(unsigned int, 1220, oldskb->len);

	if (!pskb_may_pull(oldskb, len))
		return NULL;

	if (!nf_reject_v6_csum_ok(oldskb, hook))
		return NULL;

	nskb = alloc_skb(sizeof(struct ipv6hdr) + sizeof(struct icmp6hdr) +
			 LL_MAX_HEADER + len, GFP_ATOMIC);
	if (!nskb)
		return NULL;

	nskb->dev = (struct net_device *)dev;

	skb_reserve(nskb, LL_MAX_HEADER);
	nip6h = nf_reject_ip6hdr_put(nskb, oldskb, IPPROTO_ICMPV6,
				     net->ipv6.devconf_all->hop_limit);

	skb_reset_transport_header(nskb);
	icmp6h = skb_put_zero(nskb, sizeof(struct icmp6hdr));
	icmp6h->icmp6_type = ICMPV6_DEST_UNREACH;
	icmp6h->icmp6_code = code;

	skb_put_data(nskb, skb_network_header(oldskb), len);
	nip6h->payload_len = htons(nskb->len - sizeof(struct ipv6hdr));

	icmp6h->icmp6_cksum =
		csum_ipv6_magic(&nip6h->saddr, &nip6h->daddr,
				nskb->len - sizeof(struct ipv6hdr),
				IPPROTO_ICMPV6,
				csum_partial(icmp6h,
					     nskb->len - sizeof(struct ipv6hdr),
					     0));

	return nskb;
}
EXPORT_SYMBOL_GPL(nf_reject_skb_v6_unreach);

const struct tcphdr *nf_reject_ip6_tcphdr_get(struct sk_buff *oldskb,
					      struct tcphdr *otcph,
					      unsigned int *otcplen, int hook)
{
	const struct ipv6hdr *oip6h = ipv6_hdr(oldskb);
	u8 proto;
	__be16 frag_off;
	int tcphoff;

	proto = oip6h->nexthdr;
	tcphoff = ipv6_skip_exthdr(oldskb, ((u8 *)(oip6h + 1) - oldskb->data),
				   &proto, &frag_off);

	if ((tcphoff < 0) || (tcphoff > oldskb->len)) {
		pr_debug("Cannot get TCP header.\n");
		return NULL;
	}

	*otcplen = oldskb->len - tcphoff;

	/* IP header checks: fragment, too short. */
	if (proto != IPPROTO_TCP || *otcplen < sizeof(struct tcphdr)) {
		pr_debug("proto(%d) != IPPROTO_TCP or too short (len = %d)\n",
			 proto, *otcplen);
		return NULL;
	}

	otcph = skb_header_pointer(oldskb, tcphoff, sizeof(struct tcphdr),
				   otcph);
	if (otcph == NULL)
		return NULL;

	/* No RST for RST. */
	if (otcph->rst) {
		pr_debug("RST is set\n");
		return NULL;
	}

	/* Check checksum. */
	if (nf_ip6_checksum(oldskb, hook, tcphoff, IPPROTO_TCP)) {
		pr_debug("TCP checksum is invalid\n");
		return NULL;
	}

	return otcph;
}
EXPORT_SYMBOL_GPL(nf_reject_ip6_tcphdr_get);

struct ipv6hdr *nf_reject_ip6hdr_put(struct sk_buff *nskb,
				     const struct sk_buff *oldskb,
				     __u8 protocol, int hoplimit)
{
	struct ipv6hdr *ip6h;
	const struct ipv6hdr *oip6h = ipv6_hdr(oldskb);
#define DEFAULT_TOS_VALUE	0x0U
	const __u8 tclass = DEFAULT_TOS_VALUE;

	skb_put(nskb, sizeof(struct ipv6hdr));
	skb_reset_network_header(nskb);
	ip6h = ipv6_hdr(nskb);
	ip6_flow_hdr(ip6h, tclass, 0);
	ip6h->hop_limit = hoplimit;
	ip6h->nexthdr = protocol;
	ip6h->saddr = oip6h->daddr;
	ip6h->daddr = oip6h->saddr;

	nskb->protocol = htons(ETH_P_IPV6);

	return ip6h;
}
EXPORT_SYMBOL_GPL(nf_reject_ip6hdr_put);

void nf_reject_ip6_tcphdr_put(struct sk_buff *nskb,
			      const struct sk_buff *oldskb,
			      const struct tcphdr *oth, unsigned int otcplen)
{
	struct tcphdr *tcph;
	int needs_ack;

	skb_reset_transport_header(nskb);
	tcph = skb_put(nskb, sizeof(struct tcphdr));
	/* Truncate to length (no data) */
	tcph->doff = sizeof(struct tcphdr)/4;
	tcph->source = oth->dest;
	tcph->dest = oth->source;

	if (oth->ack) {
		needs_ack = 0;
		tcph->seq = oth->ack_seq;
		tcph->ack_seq = 0;
	} else {
		needs_ack = 1;
		tcph->ack_seq = htonl(ntohl(oth->seq) + oth->syn + oth->fin +
				      otcplen - (oth->doff<<2));
		tcph->seq = 0;
	}

	/* Reset flags */
	((u_int8_t *)tcph)[13] = 0;
	tcph->rst = 1;
	tcph->ack = needs_ack;
	tcph->window = 0;
	tcph->urg_ptr = 0;
	tcph->check = 0;

	/* Adjust TCP checksum */
	tcph->check = csum_ipv6_magic(&ipv6_hdr(nskb)->saddr,
				      &ipv6_hdr(nskb)->daddr,
				      sizeof(struct tcphdr), IPPROTO_TCP,
				      csum_partial(tcph,
						   sizeof(struct tcphdr), 0));
}
EXPORT_SYMBOL_GPL(nf_reject_ip6_tcphdr_put);

static int nf_reject6_fill_skb_dst(struct sk_buff *skb_in)
{
	struct dst_entry *dst = NULL;
	struct flowi fl;

	memset(&fl, 0, sizeof(struct flowi));
	fl.u.ip6.daddr = ipv6_hdr(skb_in)->saddr;
	nf_ip6_route(dev_net(skb_in->dev), &dst, &fl, false);
	if (!dst)
		return -1;

	skb_dst_set(skb_in, dst);
	return 0;
}

void nf_send_reset6(struct net *net, struct sock *sk, struct sk_buff *oldskb,
		    int hook)
{
	struct net_device *br_indev __maybe_unused;
	struct sk_buff *nskb;
	struct tcphdr _otcph;
	const struct tcphdr *otcph;
	unsigned int otcplen, hh_len;
	const struct ipv6hdr *oip6h = ipv6_hdr(oldskb);
	struct ipv6hdr *ip6h;
	struct dst_entry *dst = NULL;
	struct flowi6 fl6;

	if ((!(ipv6_addr_type(&oip6h->saddr) & IPV6_ADDR_UNICAST)) ||
	    (!(ipv6_addr_type(&oip6h->daddr) & IPV6_ADDR_UNICAST))) {
		pr_debug("addr is not unicast.\n");
		return;
	}

	otcph = nf_reject_ip6_tcphdr_get(oldskb, &_otcph, &otcplen, hook);
	if (!otcph)
		return;

	memset(&fl6, 0, sizeof(fl6));
	fl6.flowi6_proto = IPPROTO_TCP;
	fl6.saddr = oip6h->daddr;
	fl6.daddr = oip6h->saddr;
	fl6.fl6_sport = otcph->dest;
	fl6.fl6_dport = otcph->source;

	if (hook == NF_INET_PRE_ROUTING || hook == NF_INET_INGRESS) {
		nf_ip6_route(net, &dst, flowi6_to_flowi(&fl6), false);
		if (!dst)
			return;
		skb_dst_set(oldskb, dst);
	}

	fl6.flowi6_oif = l3mdev_master_ifindex(skb_dst(oldskb)->dev);
	fl6.flowi6_mark = IP6_REPLY_MARK(net, oldskb->mark);
	security_skb_classify_flow(oldskb, flowi6_to_flowi_common(&fl6));
	dst = ip6_route_output(net, NULL, &fl6);
	if (dst->error) {
		dst_release(dst);
		return;
	}
	dst = xfrm_lookup(net, dst, flowi6_to_flowi(&fl6), NULL, 0);
	if (IS_ERR(dst))
		return;

	hh_len = (dst->dev->hard_header_len + 15)&~15;
	nskb = alloc_skb(hh_len + 15 + dst->header_len + sizeof(struct ipv6hdr)
			 + sizeof(struct tcphdr) + dst->trailer_len,
			 GFP_ATOMIC);

	if (!nskb) {
		net_dbg_ratelimited("cannot alloc skb\n");
		dst_release(dst);
		return;
	}

	skb_dst_set(nskb, dst);

	nskb->mark = fl6.flowi6_mark;

	skb_reserve(nskb, hh_len + dst->header_len);
	ip6h = nf_reject_ip6hdr_put(nskb, oldskb, IPPROTO_TCP,
				    ip6_dst_hoplimit(dst));
	nf_reject_ip6_tcphdr_put(nskb, oldskb, otcph, otcplen);

	nf_ct_attach(nskb, oldskb);

#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
	/* If we use ip6_local_out for bridged traffic, the MAC source on
	 * the RST will be ours, instead of the destination's.  This confuses
	 * some routers/firewalls, and they drop the packet.  So we need to
	 * build the eth header using the original destination's MAC as the
	 * source, and send the RST packet directly.
	 */
	br_indev = nf_bridge_get_physindev(oldskb);
	if (br_indev) {
		struct ethhdr *oeth = eth_hdr(oldskb);

		nskb->dev = br_indev;
		nskb->protocol = htons(ETH_P_IPV6);
		ip6h->payload_len = htons(sizeof(struct tcphdr));
		if (dev_hard_header(nskb, nskb->dev, ntohs(nskb->protocol),
				    oeth->h_source, oeth->h_dest, nskb->len) < 0) {
			kfree_skb(nskb);
			return;
		}
		dev_queue_xmit(nskb);
	} else
#endif
		ip6_local_out(net, sk, nskb);
}
EXPORT_SYMBOL_GPL(nf_send_reset6);

static bool reject6_csum_ok(struct sk_buff *skb, int hook)
{
	const struct ipv6hdr *ip6h = ipv6_hdr(skb);
	int thoff;
	__be16 fo;
	u8 proto;

	if (skb_csum_unnecessary(skb))
		return true;

	proto = ip6h->nexthdr;
	thoff = ipv6_skip_exthdr(skb, ((u8 *)(ip6h + 1) - skb->data), &proto, &fo);

	if (thoff < 0 || thoff >= skb->len || (fo & htons(~0x7)) != 0)
		return false;

	if (!nf_reject_verify_csum(proto))
		return true;

	return nf_ip6_checksum(skb, hook, thoff, proto) == 0;
}

void nf_send_unreach6(struct net *net, struct sk_buff *skb_in,
		      unsigned char code, unsigned int hooknum)
{
	if (!reject6_csum_ok(skb_in, hooknum))
		return;

	if (hooknum == NF_INET_LOCAL_OUT && skb_in->dev == NULL)
		skb_in->dev = net->loopback_dev;

	if ((hooknum == NF_INET_PRE_ROUTING || hooknum == NF_INET_INGRESS) &&
	    nf_reject6_fill_skb_dst(skb_in) < 0)
		return;

	icmpv6_send(skb_in, ICMPV6_DEST_UNREACH, code, 0);
}
EXPORT_SYMBOL_GPL(nf_send_unreach6);

MODULE_LICENSE("GPL");
