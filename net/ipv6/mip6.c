/*
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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ipv6.h>
#include <net/ipv6.h>
#include <net/xfrm.h>
#include <net/mip6.h>

static xfrm_address_t *mip6_xfrm_addr(struct xfrm_state *x, xfrm_address_t *addr)
{
	return x->coaddr;
}

static int mip6_rthdr_input(struct xfrm_state *x, struct sk_buff *skb)
{
	struct rt2_hdr *rt2 = (struct rt2_hdr *)skb->data;

	if (!ipv6_addr_equal(&rt2->addr, (struct in6_addr *)x->coaddr) &&
	    !ipv6_addr_any((struct in6_addr *)x->coaddr))
		return -ENOENT;

	return rt2->rt_hdr.nexthdr;
}

/* Routing Header type 2 is inserted.
 * IP Header's dst address is replaced with Routing Header's Home Address.
 */
static int mip6_rthdr_output(struct xfrm_state *x, struct sk_buff *skb)
{
	struct ipv6hdr *iph;
	struct rt2_hdr *rt2;
	u8 nexthdr;

	iph = (struct ipv6hdr *)skb->data;
	iph->payload_len = htons(skb->len - sizeof(*iph));

	nexthdr = *skb->nh.raw;
	*skb->nh.raw = IPPROTO_ROUTING;

	rt2 = (struct rt2_hdr *)skb->h.raw;
	rt2->rt_hdr.nexthdr = nexthdr;
	rt2->rt_hdr.hdrlen = (x->props.header_len >> 3) - 1;
	rt2->rt_hdr.type = IPV6_SRCRT_TYPE_2;
	rt2->rt_hdr.segments_left = 1;
	memset(&rt2->reserved, 0, sizeof(rt2->reserved));

	BUG_TRAP(rt2->rt_hdr.hdrlen == 2);

	memcpy(&rt2->addr, &iph->daddr, sizeof(rt2->addr));
	memcpy(&iph->daddr, x->coaddr, sizeof(iph->daddr));

	return 0;
}

static int mip6_rthdr_offset(struct xfrm_state *x, struct sk_buff *skb,
			     u8 **nexthdr)
{
	u16 offset = sizeof(struct ipv6hdr);
	struct ipv6_opt_hdr *exthdr = (struct ipv6_opt_hdr*)(skb->nh.ipv6h + 1);
	unsigned int packet_len = skb->tail - skb->nh.raw;
	int found_rhdr = 0;

	*nexthdr = &skb->nh.ipv6h->nexthdr;

	while (offset + 1 <= packet_len) {

		switch (**nexthdr) {
		case NEXTHDR_HOP:
			break;
		case NEXTHDR_ROUTING:
			if (offset + 3 <= packet_len) {
				struct ipv6_rt_hdr *rt;
				rt = (struct ipv6_rt_hdr *)(skb->nh.raw + offset);
				if (rt->type != 0)
					return offset;
			}
			found_rhdr = 1;
			break;
		case NEXTHDR_DEST:
			if (ipv6_find_tlv(skb, offset, IPV6_TLV_HAO) >= 0)
				return offset;

			if (found_rhdr)
				return offset;

			break;
		default:
			return offset;
		}

		offset += ipv6_optlen(exthdr);
		*nexthdr = &exthdr->nexthdr;
		exthdr = (struct ipv6_opt_hdr*)(skb->nh.raw + offset);
	}

	return offset;
}

static int mip6_rthdr_init_state(struct xfrm_state *x)
{
	if (x->id.spi) {
		printk(KERN_INFO "%s: spi is not 0: %u\n", __FUNCTION__,
		       x->id.spi);
		return -EINVAL;
	}
	if (x->props.mode != XFRM_MODE_ROUTEOPTIMIZATION) {
		printk(KERN_INFO "%s: state's mode is not %u: %u\n",
		       __FUNCTION__, XFRM_MODE_ROUTEOPTIMIZATION, x->props.mode);
		return -EINVAL;
	}

	x->props.header_len = sizeof(struct rt2_hdr);

	return 0;
}

/*
 * Do nothing about destroying since it has no specific operation for routing
 * header type 2 unlike IPsec protocols.
 */
static void mip6_rthdr_destroy(struct xfrm_state *x)
{
}

static struct xfrm_type mip6_rthdr_type =
{
	.description	= "MIP6RT",
	.owner		= THIS_MODULE,
	.proto	     	= IPPROTO_ROUTING,
	.flags		= XFRM_TYPE_NON_FRAGMENT,
	.init_state	= mip6_rthdr_init_state,
	.destructor	= mip6_rthdr_destroy,
	.input		= mip6_rthdr_input,
	.output		= mip6_rthdr_output,
	.hdr_offset	= mip6_rthdr_offset,
	.remote_addr	= mip6_xfrm_addr,
};

int __init mip6_init(void)
{
	printk(KERN_INFO "Mobile IPv6\n");

	if (xfrm_register_type(&mip6_rthdr_type, AF_INET6) < 0) {
		printk(KERN_INFO "%s: can't add xfrm type(rthdr)\n", __FUNCTION__);
		goto mip6_rthdr_xfrm_fail;
	}
	return 0;

 mip6_rthdr_xfrm_fail:
	return -EAGAIN;
}

void __exit mip6_fini(void)
{
	if (xfrm_unregister_type(&mip6_rthdr_type, AF_INET6) < 0)
		printk(KERN_INFO "%s: can't remove xfrm type(rthdr)\n", __FUNCTION__);
}
