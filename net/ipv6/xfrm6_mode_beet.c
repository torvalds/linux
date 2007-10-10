/*
 * xfrm6_mode_beet.c - BEET mode encapsulation for IPv6.
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
#include <net/dsfield.h>
#include <net/dst.h>
#include <net/inet_ecn.h>
#include <net/ipv6.h>
#include <net/xfrm.h>

/* Add encapsulation header.
 *
 * The top IP header will be constructed per draft-nikander-esp-beet-mode-06.txt.
 */
static int xfrm6_beet_output(struct xfrm_state *x, struct sk_buff *skb)
{
	struct ipv6hdr *iph, *top_iph;
	u8 *prevhdr;
	int hdr_len;

	iph = ipv6_hdr(skb);

	hdr_len = ip6_find_1stfragopt(skb, &prevhdr);

	skb_set_mac_header(skb, (prevhdr - x->props.header_len) - skb->data);
	skb_set_network_header(skb, -x->props.header_len);
	skb->transport_header = skb->network_header + hdr_len;
	__skb_pull(skb, hdr_len);

	top_iph = ipv6_hdr(skb);
	memmove(top_iph, iph, hdr_len);

	ipv6_addr_copy(&top_iph->saddr, (struct in6_addr *)&x->props.saddr);
	ipv6_addr_copy(&top_iph->daddr, (struct in6_addr *)&x->id.daddr);

	return 0;
}

static int xfrm6_beet_input(struct xfrm_state *x, struct sk_buff *skb)
{
	struct ipv6hdr *ip6h;
	const unsigned char *old_mac;
	int size = sizeof(struct ipv6hdr);
	int err = -EINVAL;

	if (!pskb_may_pull(skb, sizeof(struct ipv6hdr)))
		goto out;

	skb_push(skb, size);
	memmove(skb->data, skb_network_header(skb), size);
	skb_reset_network_header(skb);

	old_mac = skb_mac_header(skb);
	skb_set_mac_header(skb, -skb->mac_len);
	memmove(skb_mac_header(skb), old_mac, skb->mac_len);

	ip6h = ipv6_hdr(skb);
	ip6h->payload_len = htons(skb->len - size);
	ipv6_addr_copy(&ip6h->daddr, (struct in6_addr *) &x->sel.daddr.a6);
	ipv6_addr_copy(&ip6h->saddr, (struct in6_addr *) &x->sel.saddr.a6);
	err = 0;
out:
	return err;
}

static struct xfrm_mode xfrm6_beet_mode = {
	.input = xfrm6_beet_input,
	.output = xfrm6_beet_output,
	.owner = THIS_MODULE,
	.encap = XFRM_MODE_BEET,
};

static int __init xfrm6_beet_init(void)
{
	return xfrm_register_mode(&xfrm6_beet_mode, AF_INET6);
}

static void __exit xfrm6_beet_exit(void)
{
	int err;

	err = xfrm_unregister_mode(&xfrm6_beet_mode, AF_INET6);
	BUG_ON(err);
}

module_init(xfrm6_beet_init);
module_exit(xfrm6_beet_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_XFRM_MODE(AF_INET6, XFRM_MODE_BEET);
