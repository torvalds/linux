#ifndef _NF_TABLES_IPV4_H_
#define _NF_TABLES_IPV4_H_

#include <net/netfilter/nf_tables.h>
#include <net/ip.h>

static inline void
nft_set_pktinfo_ipv4(struct nft_pktinfo *pkt,
		     const struct nf_hook_ops *ops,
		     struct sk_buff *skb,
		     const struct net_device *in,
		     const struct net_device *out)
{
	struct iphdr *ip;

	nft_set_pktinfo(pkt, ops, skb, in, out);

	pkt->xt.thoff = ip_hdrlen(pkt->skb);
	ip = ip_hdr(pkt->skb);
	pkt->xt.fragoff = ntohs(ip->frag_off) & IP_OFFSET;
}

#endif
