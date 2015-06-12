#ifndef _NF_TABLES_IPV4_H_
#define _NF_TABLES_IPV4_H_

#include <net/netfilter/nf_tables.h>
#include <net/ip.h>

static inline void
nft_set_pktinfo_ipv4(struct nft_pktinfo *pkt,
		     const struct nf_hook_ops *ops,
		     struct sk_buff *skb,
		     const struct nf_hook_state *state)
{
	struct iphdr *ip;

	nft_set_pktinfo(pkt, ops, skb, state);

	ip = ip_hdr(pkt->skb);
	pkt->tprot = ip->protocol;
	pkt->xt.thoff = ip_hdrlen(pkt->skb);
	pkt->xt.fragoff = ntohs(ip->frag_off) & IP_OFFSET;
}

extern struct nft_af_info nft_af_ipv4;

#endif
