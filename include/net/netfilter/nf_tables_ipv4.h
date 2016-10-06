#ifndef _NF_TABLES_IPV4_H_
#define _NF_TABLES_IPV4_H_

#include <net/netfilter/nf_tables.h>
#include <net/ip.h>

static inline void
nft_set_pktinfo_ipv4(struct nft_pktinfo *pkt,
		     struct sk_buff *skb,
		     const struct nf_hook_state *state)
{
	struct iphdr *ip;

	nft_set_pktinfo(pkt, skb, state);

	ip = ip_hdr(pkt->skb);
	pkt->tprot_set = true;
	pkt->tprot = ip->protocol;
	pkt->xt.thoff = ip_hdrlen(pkt->skb);
	pkt->xt.fragoff = ntohs(ip->frag_off) & IP_OFFSET;
}

static inline int
__nft_set_pktinfo_ipv4_validate(struct nft_pktinfo *pkt,
				struct sk_buff *skb,
				const struct nf_hook_state *state)
{
	struct iphdr *iph, _iph;
	u32 len, thoff;

	iph = skb_header_pointer(skb, skb_network_offset(skb), sizeof(*iph),
				 &_iph);
	if (!iph)
		return -1;

	if (iph->ihl < 5 || iph->version != 4)
		return -1;

	len = ntohs(iph->tot_len);
	thoff = iph->ihl * 4;
	if (skb->len < len)
		return -1;
	else if (len < thoff)
		return -1;

	pkt->tprot_set = true;
	pkt->tprot = iph->protocol;
	pkt->xt.thoff = thoff;
	pkt->xt.fragoff = ntohs(iph->frag_off) & IP_OFFSET;

	return 0;
}

static inline void
nft_set_pktinfo_ipv4_validate(struct nft_pktinfo *pkt,
			      struct sk_buff *skb,
			      const struct nf_hook_state *state)
{
	nft_set_pktinfo(pkt, skb, state);
	if (__nft_set_pktinfo_ipv4_validate(pkt, skb, state) < 0)
		nft_set_pktinfo_proto_unspec(pkt, skb);
}

extern struct nft_af_info nft_af_ipv4;

#endif
