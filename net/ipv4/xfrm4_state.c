// SPDX-License-Identifier: GPL-2.0
/*
 * xfrm4_state.c
 *
 * Changes:
 * 	YOSHIFUJI Hideaki @USAGI
 * 		Split up af-specific portion
 *
 */

#include <net/ip.h>
#include <net/xfrm.h>
#include <linux/pfkeyv2.h>
#include <linux/ipsec.h>
#include <linux/netfilter_ipv4.h>
#include <linux/export.h>

int xfrm4_extract_header(struct sk_buff *skb)
{
	const struct iphdr *iph = ip_hdr(skb);

	XFRM_MODE_SKB_CB(skb)->ihl = sizeof(*iph);
	XFRM_MODE_SKB_CB(skb)->id = iph->id;
	XFRM_MODE_SKB_CB(skb)->frag_off = iph->frag_off;
	XFRM_MODE_SKB_CB(skb)->tos = iph->tos;
	XFRM_MODE_SKB_CB(skb)->ttl = iph->ttl;
	XFRM_MODE_SKB_CB(skb)->optlen = iph->ihl * 4 - sizeof(*iph);
	memset(XFRM_MODE_SKB_CB(skb)->flow_lbl, 0,
	       sizeof(XFRM_MODE_SKB_CB(skb)->flow_lbl));

	return 0;
}

static struct xfrm_state_afinfo xfrm4_state_afinfo = {
	.family			= AF_INET,
	.proto			= IPPROTO_IPIP,
	.output			= xfrm4_output,
	.output_finish		= xfrm4_output_finish,
	.extract_input		= xfrm4_extract_input,
	.extract_output		= xfrm4_extract_output,
	.transport_finish	= xfrm4_transport_finish,
	.local_error		= xfrm4_local_error,
};

void __init xfrm4_state_init(void)
{
	xfrm_state_register_afinfo(&xfrm4_state_afinfo);
}
