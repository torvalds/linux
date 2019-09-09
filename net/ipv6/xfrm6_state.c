// SPDX-License-Identifier: GPL-2.0
/*
 * xfrm6_state.c: based on xfrm4_state.c
 *
 * Authors:
 *	Mitsuru KANDA @USAGI
 *	Kazunori MIYAZAWA @USAGI
 *	Kunihiro Ishiguro <kunihiro@ipinfusion.com>
 *		IPv6 support
 *	YOSHIFUJI Hideaki @USAGI
 *		Split up af-specific portion
 *
 */

#include <net/xfrm.h>
#include <linux/pfkeyv2.h>
#include <linux/ipsec.h>
#include <linux/netfilter_ipv6.h>
#include <linux/export.h>
#include <net/dsfield.h>
#include <net/ipv6.h>
#include <net/addrconf.h>

int xfrm6_extract_header(struct sk_buff *skb)
{
	struct ipv6hdr *iph = ipv6_hdr(skb);

	XFRM_MODE_SKB_CB(skb)->ihl = sizeof(*iph);
	XFRM_MODE_SKB_CB(skb)->id = 0;
	XFRM_MODE_SKB_CB(skb)->frag_off = htons(IP_DF);
	XFRM_MODE_SKB_CB(skb)->tos = ipv6_get_dsfield(iph);
	XFRM_MODE_SKB_CB(skb)->ttl = iph->hop_limit;
	XFRM_MODE_SKB_CB(skb)->optlen = 0;
	memcpy(XFRM_MODE_SKB_CB(skb)->flow_lbl, iph->flow_lbl,
	       sizeof(XFRM_MODE_SKB_CB(skb)->flow_lbl));

	return 0;
}

static struct xfrm_state_afinfo xfrm6_state_afinfo = {
	.family			= AF_INET6,
	.proto			= IPPROTO_IPV6,
	.output			= xfrm6_output,
	.output_finish		= xfrm6_output_finish,
	.extract_input		= xfrm6_extract_input,
	.extract_output		= xfrm6_extract_output,
	.transport_finish	= xfrm6_transport_finish,
	.local_error		= xfrm6_local_error,
};

int __init xfrm6_state_init(void)
{
	return xfrm_state_register_afinfo(&xfrm6_state_afinfo);
}

void xfrm6_state_fini(void)
{
	xfrm_state_unregister_afinfo(&xfrm6_state_afinfo);
}
