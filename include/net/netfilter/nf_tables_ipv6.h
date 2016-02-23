#ifndef _NF_TABLES_IPV6_H_
#define _NF_TABLES_IPV6_H_

#include <linux/netfilter_ipv6/ip6_tables.h>
#include <net/ipv6.h>

static inline int
nft_set_pktinfo_ipv6(struct nft_pktinfo *pkt,
		     struct sk_buff *skb,
		     const struct nf_hook_state *state)
{
	int protohdr, thoff = 0;
	unsigned short frag_off;

	nft_set_pktinfo(pkt, skb, state);

	protohdr = ipv6_find_hdr(pkt->skb, &thoff, -1, &frag_off, NULL);
	/* If malformed, drop it */
	if (protohdr < 0)
		return -1;

	pkt->tprot = protohdr;
	pkt->xt.thoff = thoff;
	pkt->xt.fragoff = frag_off;

	return 0;
}

extern struct nft_af_info nft_af_ipv6;

#endif
