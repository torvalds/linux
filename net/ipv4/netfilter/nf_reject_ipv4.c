// SPDX-License-Identifier: GPL-2.0-only
/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 */

#include <linux/module.h>
#include <net/ip.h>
#include <net/tcp.h>
#include <net/route.h>
#include <net/dst.h>
#include <net/netfilter/ipv4/nf_reject.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_bridge.h>

const struct tcphdr *nf_reject_ip_tcphdr_get(struct sk_buff *oldskb,
					     struct tcphdr *_oth, int hook)
{
	const struct tcphdr *oth;

	/* IP header checks: fragment. */
	if (ip_hdr(oldskb)->frag_off & htons(IP_OFFSET))
		return NULL;

	if (ip_hdr(oldskb)->protocol != IPPROTO_TCP)
		return NULL;

	oth = skb_header_pointer(oldskb, ip_hdrlen(oldskb),
				 sizeof(struct tcphdr), _oth);
	if (oth == NULL)
		return NULL;

	/* No RST for RST. */
	if (oth->rst)
		return NULL;

	/* Check checksum */
	if (nf_ip_checksum(oldskb, hook, ip_hdrlen(oldskb), IPPROTO_TCP))
		return NULL;

	return oth;
}
EXPORT_SYMBOL_GPL(nf_reject_ip_tcphdr_get);

struct iphdr *nf_reject_iphdr_put(struct sk_buff *nskb,
				  const struct sk_buff *oldskb,
				  __u8 protocol, int ttl)
{
	struct iphdr *niph, *oiph = ip_hdr(oldskb);

	skb_reset_network_header(nskb);
	niph = skb_put(nskb, sizeof(struct iphdr));
	niph->version	= 4;
	niph->ihl	= sizeof(struct iphdr) / 4;
	niph->tos	= 0;
	niph->id	= 0;
	niph->frag_off	= htons(IP_DF);
	niph->protocol	= protocol;
	niph->check	= 0;
	niph->saddr	= oiph->daddr;
	niph->daddr	= oiph->saddr;
	niph->ttl	= ttl;

	nskb->protocol = htons(ETH_P_IP);

	return niph;
}
EXPORT_SYMBOL_GPL(nf_reject_iphdr_put);

void nf_reject_ip_tcphdr_put(struct sk_buff *nskb, const struct sk_buff *oldskb,
			  const struct tcphdr *oth)
{
	struct iphdr *niph = ip_hdr(nskb);
	struct tcphdr *tcph;

	skb_reset_transport_header(nskb);
	tcph = skb_put_zero(nskb, sizeof(struct tcphdr));
	tcph->source	= oth->dest;
	tcph->dest	= oth->source;
	tcph->doff	= sizeof(struct tcphdr) / 4;

	if (oth->ack) {
		tcph->seq = oth->ack_seq;
	} else {
		tcph->ack_seq = htonl(ntohl(oth->seq) + oth->syn + oth->fin +
				      oldskb->len - ip_hdrlen(oldskb) -
				      (oth->doff << 2));
		tcph->ack = 1;
	}

	tcph->rst	= 1;
	tcph->check = ~tcp_v4_check(sizeof(struct tcphdr), niph->saddr,
				    niph->daddr, 0);
	nskb->ip_summed = CHECKSUM_PARTIAL;
	nskb->csum_start = (unsigned char *)tcph - nskb->head;
	nskb->csum_offset = offsetof(struct tcphdr, check);
}
EXPORT_SYMBOL_GPL(nf_reject_ip_tcphdr_put);

static int nf_reject_fill_skb_dst(struct sk_buff *skb_in)
{
	struct dst_entry *dst = NULL;
	struct flowi fl;

	memset(&fl, 0, sizeof(struct flowi));
	fl.u.ip4.daddr = ip_hdr(skb_in)->saddr;
	nf_ip_route(dev_net(skb_in->dev), &dst, &fl, false);
	if (!dst)
		return -1;

	skb_dst_set(skb_in, dst);
	return 0;
}

/* Send RST reply */
void nf_send_reset(struct net *net, struct sock *sk, struct sk_buff *oldskb,
		   int hook)
{
	struct net_device *br_indev __maybe_unused;
	struct sk_buff *nskb;
	struct iphdr *niph;
	const struct tcphdr *oth;
	struct tcphdr _oth;

	oth = nf_reject_ip_tcphdr_get(oldskb, &_oth, hook);
	if (!oth)
		return;

	if (hook == NF_INET_PRE_ROUTING && nf_reject_fill_skb_dst(oldskb))
		return;

	if (skb_rtable(oldskb)->rt_flags & (RTCF_BROADCAST | RTCF_MULTICAST))
		return;

	nskb = alloc_skb(sizeof(struct iphdr) + sizeof(struct tcphdr) +
			 LL_MAX_HEADER, GFP_ATOMIC);
	if (!nskb)
		return;

	/* ip_route_me_harder expects skb->dst to be set */
	skb_dst_set_noref(nskb, skb_dst(oldskb));

	nskb->mark = IP4_REPLY_MARK(net, oldskb->mark);

	skb_reserve(nskb, LL_MAX_HEADER);
	niph = nf_reject_iphdr_put(nskb, oldskb, IPPROTO_TCP,
				   ip4_dst_hoplimit(skb_dst(nskb)));
	nf_reject_ip_tcphdr_put(nskb, oldskb, oth);
	if (ip_route_me_harder(net, sk, nskb, RTN_UNSPEC))
		goto free_nskb;

	niph = ip_hdr(nskb);

	/* "Never happens" */
	if (nskb->len > dst_mtu(skb_dst(nskb)))
		goto free_nskb;

	nf_ct_attach(nskb, oldskb);

#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
	/* If we use ip_local_out for bridged traffic, the MAC source on
	 * the RST will be ours, instead of the destination's.  This confuses
	 * some routers/firewalls, and they drop the packet.  So we need to
	 * build the eth header using the original destination's MAC as the
	 * source, and send the RST packet directly.
	 */
	br_indev = nf_bridge_get_physindev(oldskb);
	if (br_indev) {
		struct ethhdr *oeth = eth_hdr(oldskb);

		nskb->dev = br_indev;
		niph->tot_len = htons(nskb->len);
		ip_send_check(niph);
		if (dev_hard_header(nskb, nskb->dev, ntohs(nskb->protocol),
				    oeth->h_source, oeth->h_dest, nskb->len) < 0)
			goto free_nskb;
		dev_queue_xmit(nskb);
	} else
#endif
		ip_local_out(net, nskb->sk, nskb);

	return;

 free_nskb:
	kfree_skb(nskb);
}
EXPORT_SYMBOL_GPL(nf_send_reset);

void nf_send_unreach(struct sk_buff *skb_in, int code, int hook)
{
	struct iphdr *iph = ip_hdr(skb_in);
	u8 proto = iph->protocol;

	if (iph->frag_off & htons(IP_OFFSET))
		return;

	if (hook == NF_INET_PRE_ROUTING && nf_reject_fill_skb_dst(skb_in))
		return;

	if (skb_csum_unnecessary(skb_in) || !nf_reject_verify_csum(proto)) {
		icmp_send(skb_in, ICMP_DEST_UNREACH, code, 0);
		return;
	}

	if (nf_ip_checksum(skb_in, hook, ip_hdrlen(skb_in), proto) == 0)
		icmp_send(skb_in, ICMP_DEST_UNREACH, code, 0);
}
EXPORT_SYMBOL_GPL(nf_send_unreach);

MODULE_LICENSE("GPL");
