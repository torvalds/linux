/*
 * xfrm4_mode_beet.c - BEET mode encapsulation for IPv4.
 *
 * Copyright (c) 2006 Diego Beltrami <diego.beltrami@gmail.com>
 *                    Miika Komu     <miika@iki.fi>
 *                    Herbert Xu     <herbert@gondor.apana.org.au>
 *                    Abhinav Pathak <abhinav.pathak@hiit.fi>
 *                    Jeff Ahrenholz <ahrenholz@gmail.com>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/stringify.h>
#include <net/dst.h>
#include <net/ip.h>
#include <net/xfrm.h>

static void xfrm4_beet_make_header(struct sk_buff *skb)
{
	struct iphdr *iph = ip_hdr(skb);

	iph->ihl = 5;
	iph->version = 4;

	iph->protocol = XFRM_MODE_SKB_CB(skb)->protocol;
	iph->tos = XFRM_MODE_SKB_CB(skb)->tos;

	iph->id = XFRM_MODE_SKB_CB(skb)->id;
	iph->frag_off = XFRM_MODE_SKB_CB(skb)->frag_off;
	iph->ttl = XFRM_MODE_SKB_CB(skb)->ttl;
}

/* Add encapsulation header.
 *
 * The top IP header will be constructed per draft-nikander-esp-beet-mode-06.txt.
 */
static int xfrm4_beet_output(struct xfrm_state *x, struct sk_buff *skb)
{
	struct ip_beet_phdr *ph;
	struct iphdr *top_iph;
	int hdrlen, optlen;

	hdrlen = 0;
	optlen = XFRM_MODE_SKB_CB(skb)->optlen;
	if (unlikely(optlen))
		hdrlen += IPV4_BEET_PHMAXLEN - (optlen & 4);

	skb_set_network_header(skb, -x->props.header_len -
				    hdrlen + (XFRM_MODE_SKB_CB(skb)->ihl - sizeof(*top_iph)));
	if (x->sel.family != AF_INET6)
		skb->network_header += IPV4_BEET_PHMAXLEN;
	skb->mac_header = skb->network_header +
			  offsetof(struct iphdr, protocol);
	skb->transport_header = skb->network_header + sizeof(*top_iph);

	xfrm4_beet_make_header(skb);

	ph = __skb_pull(skb, XFRM_MODE_SKB_CB(skb)->ihl - hdrlen);

	top_iph = ip_hdr(skb);

	if (unlikely(optlen)) {
		BUG_ON(optlen < 0);

		ph->padlen = 4 - (optlen & 4);
		ph->hdrlen = optlen / 8;
		ph->nexthdr = top_iph->protocol;
		if (ph->padlen)
			memset(ph + 1, IPOPT_NOP, ph->padlen);

		top_iph->protocol = IPPROTO_BEETPH;
		top_iph->ihl = sizeof(struct iphdr) / 4;
	}

	top_iph->saddr = x->props.saddr.a4;
	top_iph->daddr = x->id.daddr.a4;

	return 0;
}

static int xfrm4_beet_input(struct xfrm_state *x, struct sk_buff *skb)
{
	struct iphdr *iph;
	int optlen = 0;
	int err = -EINVAL;

	if (unlikely(XFRM_MODE_SKB_CB(skb)->protocol == IPPROTO_BEETPH)) {
		struct ip_beet_phdr *ph;
		int phlen;

		if (!pskb_may_pull(skb, sizeof(*ph)))
			goto out;

		ph = (struct ip_beet_phdr *)skb->data;

		phlen = sizeof(*ph) + ph->padlen;
		optlen = ph->hdrlen * 8 + (IPV4_BEET_PHMAXLEN - phlen);
		if (optlen < 0 || optlen & 3 || optlen > 250)
			goto out;

		XFRM_MODE_SKB_CB(skb)->protocol = ph->nexthdr;

		if (!pskb_may_pull(skb, phlen))
			goto out;
		__skb_pull(skb, phlen);
	}

	skb_push(skb, sizeof(*iph));
	skb_reset_network_header(skb);
	skb_mac_header_rebuild(skb);

	xfrm4_beet_make_header(skb);

	iph = ip_hdr(skb);

	iph->ihl += optlen / 4;
	iph->tot_len = htons(skb->len);
	iph->daddr = x->sel.daddr.a4;
	iph->saddr = x->sel.saddr.a4;
	iph->check = 0;
	iph->check = ip_fast_csum(skb_network_header(skb), iph->ihl);
	err = 0;
out:
	return err;
}

static struct xfrm_mode xfrm4_beet_mode = {
	.input2 = xfrm4_beet_input,
	.input = xfrm_prepare_input,
	.output2 = xfrm4_beet_output,
	.output = xfrm4_prepare_output,
	.owner = THIS_MODULE,
	.encap = XFRM_MODE_BEET,
	.flags = XFRM_MODE_FLAG_TUNNEL,
};

static int __init xfrm4_beet_init(void)
{
	return xfrm_register_mode(&xfrm4_beet_mode, AF_INET);
}

static void __exit xfrm4_beet_exit(void)
{
	int err;

	err = xfrm_unregister_mode(&xfrm4_beet_mode, AF_INET);
	BUG_ON(err);
}

module_init(xfrm4_beet_init);
module_exit(xfrm4_beet_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_XFRM_MODE(AF_INET, XFRM_MODE_BEET);
