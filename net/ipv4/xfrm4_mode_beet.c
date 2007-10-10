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

/* Add encapsulation header.
 *
 * The top IP header will be constructed per draft-nikander-esp-beet-mode-06.txt.
 */
static int xfrm4_beet_output(struct xfrm_state *x, struct sk_buff *skb)
{
	struct ip_beet_phdr *ph;
	struct iphdr *iph, *top_iph;
	int hdrlen, optlen;

	iph = ip_hdr(skb);

	hdrlen = 0;
	optlen = iph->ihl * 4 - sizeof(*iph);
	if (unlikely(optlen))
		hdrlen += IPV4_BEET_PHMAXLEN - (optlen & 4);

	skb_set_network_header(skb, IPV4_BEET_PHMAXLEN - x->props.header_len -
				    hdrlen);
	skb->mac_header = skb->network_header +
			  offsetof(struct iphdr, protocol);
	skb->transport_header = skb->network_header + sizeof(*iph);

	ph = (struct ip_beet_phdr *)__skb_pull(skb, sizeof(*iph) - hdrlen);

	top_iph = ip_hdr(skb);
	memmove(top_iph, iph, sizeof(*iph));
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
	struct iphdr *iph = ip_hdr(skb);
	int phlen = 0;
	int optlen = 0;
	u8 ph_nexthdr = 0;
	int err = -EINVAL;

	if (unlikely(iph->protocol == IPPROTO_BEETPH)) {
		struct ip_beet_phdr *ph;

		if (!pskb_may_pull(skb, sizeof(*ph)))
			goto out;
		ph = (struct ip_beet_phdr *)(ipip_hdr(skb) + 1);

		phlen = sizeof(*ph) + ph->padlen;
		optlen = ph->hdrlen * 8 + (IPV4_BEET_PHMAXLEN - phlen);
		if (optlen < 0 || optlen & 3 || optlen > 250)
			goto out;

		if (!pskb_may_pull(skb, phlen + optlen))
			goto out;
		skb->len -= phlen + optlen;

		ph_nexthdr = ph->nexthdr;
	}

	skb_set_network_header(skb, phlen - sizeof(*iph));
	memmove(skb_network_header(skb), iph, sizeof(*iph));
	skb_set_transport_header(skb, phlen + optlen);
	skb->data = skb_transport_header(skb);

	iph = ip_hdr(skb);
	iph->ihl = (sizeof(*iph) + optlen) / 4;
	iph->tot_len = htons(skb->len + iph->ihl * 4);
	iph->daddr = x->sel.daddr.a4;
	iph->saddr = x->sel.saddr.a4;
	if (ph_nexthdr)
		iph->protocol = ph_nexthdr;
	iph->check = 0;
	iph->check = ip_fast_csum(skb_network_header(skb), iph->ihl);
	err = 0;
out:
	return err;
}

static struct xfrm_mode xfrm4_beet_mode = {
	.input = xfrm4_beet_input,
	.output = xfrm4_beet_output,
	.owner = THIS_MODULE,
	.encap = XFRM_MODE_BEET,
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
