/*
 * xfrm6_mode_ro.c - Route optimization mode for IPv6.
 *
 * Copyright (C)2003-2006 Helsinki University of Technology
 * Copyright (C)2003-2006 USAGI/WIDE Project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
/*
 * Authors:
 *	Noriaki TAKAMIYA @USAGI
 *	Masahide NAKAMURA @USAGI
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/stringify.h>
#include <net/ipv6.h>
#include <net/xfrm.h>

/* Add route optimization header space.
 *
 * The IP header and mutable extension headers will be moved forward to make
 * space for the route optimization header.
 *
 * On exit, skb->h will be set to the start of the encapsulation header to be
 * filled in by x->type->output and skb->nh will be set to the nextheader field
 * of the extension header directly preceding the encapsulation header, or in
 * its absence, that of the top IP header.  The value of skb->data will always
 * point to the top IP header.
 */
static int xfrm6_ro_output(struct xfrm_state *x, struct sk_buff *skb)
{
	struct ipv6hdr *iph;
	u8 *prevhdr;
	int hdr_len;

	skb_push(skb, x->props.header_len);
	iph = skb->nh.ipv6h;

	hdr_len = x->type->hdr_offset(x, skb, &prevhdr);
	skb->nh.raw = prevhdr - x->props.header_len;
	skb->h.raw = skb->data + hdr_len;
	memmove(skb->data, iph, hdr_len);
	return 0;
}

/*
 * Do nothing about routing optimization header unlike IPsec.
 */
static int xfrm6_ro_input(struct xfrm_state *x, struct sk_buff *skb)
{
	return 0;
}

static struct xfrm_mode xfrm6_ro_mode = {
	.input = xfrm6_ro_input,
	.output = xfrm6_ro_output,
	.owner = THIS_MODULE,
	.encap = XFRM_MODE_ROUTEOPTIMIZATION,
};

static int __init xfrm6_ro_init(void)
{
	return xfrm_register_mode(&xfrm6_ro_mode, AF_INET6);
}

static void __exit xfrm6_ro_exit(void)
{
	int err;

	err = xfrm_unregister_mode(&xfrm6_ro_mode, AF_INET6);
	BUG_ON(err);
}

module_init(xfrm6_ro_init);
module_exit(xfrm6_ro_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_XFRM_MODE(AF_INET6, XFRM_MODE_ROUTEOPTIMIZATION);
