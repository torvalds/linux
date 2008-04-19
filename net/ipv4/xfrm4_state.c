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

static struct xfrm_state_afinfo xfrm4_state_afinfo;

static int xfrm4_init_flags(struct xfrm_state *x)
{
	if (ipv4_config.no_pmtu_disc)
		x->props.flags |= XFRM_STATE_NOPMTUDISC;
	return 0;
}

static void
__xfrm4_init_tempsel(struct xfrm_state *x, struct flowi *fl,
		     struct xfrm_tmpl *tmpl,
		     xfrm_address_t *daddr, xfrm_address_t *saddr)
{
	x->sel.daddr.a4 = fl->fl4_dst;
	x->sel.saddr.a4 = fl->fl4_src;
	x->sel.dport = xfrm_flowi_dport(fl);
	x->sel.dport_mask = htons(0xffff);
	x->sel.sport = xfrm_flowi_sport(fl);
	x->sel.sport_mask = htons(0xffff);
	x->sel.prefixlen_d = 32;
	x->sel.prefixlen_s = 32;
	x->sel.proto = fl->proto;
	x->sel.ifindex = fl->oif;
	x->id = tmpl->id;
	if (x->id.daddr.a4 == 0)
		x->id.daddr.a4 = daddr->a4;
	x->props.saddr = tmpl->saddr;
	if (x->props.saddr.a4 == 0)
		x->props.saddr.a4 = saddr->a4;
	x->props.mode = tmpl->mode;
	x->props.reqid = tmpl->reqid;
	x->props.family = AF_INET;
}

int xfrm4_extract_header(struct sk_buff *skb)
{
	struct iphdr *iph = ip_hdr(skb);

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
	.eth_proto		= htons(ETH_P_IP),
	.owner			= THIS_MODULE,
	.init_flags		= xfrm4_init_flags,
	.init_tempsel		= __xfrm4_init_tempsel,
	.output			= xfrm4_output,
	.extract_input		= xfrm4_extract_input,
	.extract_output		= xfrm4_extract_output,
	.transport_finish	= xfrm4_transport_finish,
};

void __init xfrm4_state_init(void)
{
	xfrm_state_register_afinfo(&xfrm4_state_afinfo);
}

#if 0
void __exit xfrm4_state_fini(void)
{
	xfrm_state_unregister_afinfo(&xfrm4_state_afinfo);
}
#endif  /*  0  */

