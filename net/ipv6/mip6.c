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

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/time.h>
#include <linux/ipv6.h>
#include <linux/icmpv6.h>
#include <net/sock.h>
#include <net/ipv6.h>
#include <net/ip6_checksum.h>
#include <net/rawv6.h>
#include <net/xfrm.h>
#include <net/mip6.h>

static inline unsigned int calc_padlen(unsigned int len, unsigned int n)
{
	return (n - len + 16) & 0x7;
}

static inline void *mip6_padn(__u8 *data, __u8 padlen)
{
	if (!data)
		return NULL;
	if (padlen == 1) {
		data[0] = IPV6_TLV_PAD0;
	} else if (padlen > 1) {
		data[0] = IPV6_TLV_PADN;
		data[1] = padlen - 2;
		if (padlen > 2)
			memset(data+2, 0, data[1]);
	}
	return data + padlen;
}

static inline void mip6_param_prob(struct sk_buff *skb, u8 code, int pos)
{
	icmpv6_send(skb, ICMPV6_PARAMPROB, code, pos);
}

static int mip6_mh_len(int type)
{
	int len = 0;

	switch (type) {
	case IP6_MH_TYPE_BRR:
		len = 0;
		break;
	case IP6_MH_TYPE_HOTI:
	case IP6_MH_TYPE_COTI:
	case IP6_MH_TYPE_BU:
	case IP6_MH_TYPE_BACK:
		len = 1;
		break;
	case IP6_MH_TYPE_HOT:
	case IP6_MH_TYPE_COT:
	case IP6_MH_TYPE_BERROR:
		len = 2;
		break;
	}
	return len;
}

static int mip6_mh_filter(struct sock *sk, struct sk_buff *skb)
{
	struct ip6_mh *mh;

	if (!pskb_may_pull(skb, (skb_transport_offset(skb)) + 8) ||
	    !pskb_may_pull(skb, (skb_transport_offset(skb) +
				 ((skb_transport_header(skb)[1] + 1) << 3))))
		return -1;

	mh = (struct ip6_mh *)skb_transport_header(skb);

	if (mh->ip6mh_hdrlen < mip6_mh_len(mh->ip6mh_type)) {
		LIMIT_NETDEBUG(KERN_DEBUG "mip6: MH message too short: %d vs >=%d\n",
			       mh->ip6mh_hdrlen, mip6_mh_len(mh->ip6mh_type));
		mip6_param_prob(skb, 0, ((&mh->ip6mh_hdrlen) -
					 skb_network_header(skb)));
		return -1;
	}

	if (mh->ip6mh_proto != IPPROTO_NONE) {
		LIMIT_NETDEBUG(KERN_DEBUG "mip6: MH invalid payload proto = %d\n",
			       mh->ip6mh_proto);
		mip6_param_prob(skb, 0, ((&mh->ip6mh_proto) -
					 skb_network_header(skb)));
		return -1;
	}

	return 0;
}

struct mip6_report_rate_limiter {
	spinlock_t lock;
	struct timeval stamp;
	int iif;
	struct in6_addr src;
	struct in6_addr dst;
};

static struct mip6_report_rate_limiter mip6_report_rl = {
	.lock = __SPIN_LOCK_UNLOCKED(mip6_report_rl.lock)
};

static int mip6_destopt_input(struct xfrm_state *x, struct sk_buff *skb)
{
	const struct ipv6hdr *iph = ipv6_hdr(skb);
	struct ipv6_destopt_hdr *destopt = (struct ipv6_destopt_hdr *)skb->data;
	int err = destopt->nexthdr;

	spin_lock(&x->lock);
	if (!ipv6_addr_equal(&iph->saddr, (struct in6_addr *)x->coaddr) &&
	    !ipv6_addr_any((struct in6_addr *)x->coaddr))
		err = -ENOENT;
	spin_unlock(&x->lock);

	return err;
}

/* Destination Option Header is inserted.
 * IP Header's src address is replaced with Home Address Option in
 * Destination Option Header.
 */
static int mip6_destopt_output(struct xfrm_state *x, struct sk_buff *skb)
{
	struct ipv6hdr *iph;
	struct ipv6_destopt_hdr *dstopt;
	struct ipv6_destopt_hao *hao;
	u8 nexthdr;
	int len;

	skb_push(skb, -skb_network_offset(skb));
	iph = ipv6_hdr(skb);

	nexthdr = *skb_mac_header(skb);
	*skb_mac_header(skb) = IPPROTO_DSTOPTS;

	dstopt = (struct ipv6_destopt_hdr *)skb_transport_header(skb);
	dstopt->nexthdr = nexthdr;

	hao = mip6_padn((char *)(dstopt + 1),
			calc_padlen(sizeof(*dstopt), 6));

	hao->type = IPV6_TLV_HAO;
	BUILD_BUG_ON(sizeof(*hao) != 18);
	hao->length = sizeof(*hao) - 2;

	len = ((char *)hao - (char *)dstopt) + sizeof(*hao);

	memcpy(&hao->addr, &iph->saddr, sizeof(hao->addr));
	spin_lock_bh(&x->lock);
	memcpy(&iph->saddr, x->coaddr, sizeof(iph->saddr));
	spin_unlock_bh(&x->lock);

	WARN_ON(len != x->props.header_len);
	dstopt->hdrlen = (x->props.header_len >> 3) - 1;

	return 0;
}

static inline int mip6_report_rl_allow(struct timeval *stamp,
				       const struct in6_addr *dst,
				       const struct in6_addr *src, int iif)
{
	int allow = 0;

	spin_lock_bh(&mip6_report_rl.lock);
	if (mip6_report_rl.stamp.tv_sec != stamp->tv_sec ||
	    mip6_report_rl.stamp.tv_usec != stamp->tv_usec ||
	    mip6_report_rl.iif != iif ||
	    !ipv6_addr_equal(&mip6_report_rl.src, src) ||
	    !ipv6_addr_equal(&mip6_report_rl.dst, dst)) {
		mip6_report_rl.stamp.tv_sec = stamp->tv_sec;
		mip6_report_rl.stamp.tv_usec = stamp->tv_usec;
		mip6_report_rl.iif = iif;
		ipv6_addr_copy(&mip6_report_rl.src, src);
		ipv6_addr_copy(&mip6_report_rl.dst, dst);
		allow = 1;
	}
	spin_unlock_bh(&mip6_report_rl.lock);
	return allow;
}

static int mip6_destopt_reject(struct xfrm_state *x, struct sk_buff *skb,
			       const struct flowi *fl)
{
	struct net *net = xs_net(x);
	struct inet6_skb_parm *opt = (struct inet6_skb_parm *)skb->cb;
	const struct flowi6 *fl6 = &fl->u.ip6;
	struct ipv6_destopt_hao *hao = NULL;
	struct xfrm_selector sel;
	int offset;
	struct timeval stamp;
	int err = 0;

	if (unlikely(fl6->flowi6_proto == IPPROTO_MH &&
		     fl6->fl6_mh_type <= IP6_MH_TYPE_MAX))
		goto out;

	if (likely(opt->dsthao)) {
		offset = ipv6_find_tlv(skb, opt->dsthao, IPV6_TLV_HAO);
		if (likely(offset >= 0))
			hao = (struct ipv6_destopt_hao *)
					(skb_network_header(skb) + offset);
	}

	skb_get_timestamp(skb, &stamp);

	if (!mip6_report_rl_allow(&stamp, &ipv6_hdr(skb)->daddr,
				  hao ? &hao->addr : &ipv6_hdr(skb)->saddr,
				  opt->iif))
		goto out;

	memset(&sel, 0, sizeof(sel));
	memcpy(&sel.daddr, (xfrm_address_t *)&ipv6_hdr(skb)->daddr,
	       sizeof(sel.daddr));
	sel.prefixlen_d = 128;
	memcpy(&sel.saddr, (xfrm_address_t *)&ipv6_hdr(skb)->saddr,
	       sizeof(sel.saddr));
	sel.prefixlen_s = 128;
	sel.family = AF_INET6;
	sel.proto = fl6->flowi6_proto;
	sel.dport = xfrm_flowi_dport(fl, &fl6->uli);
	if (sel.dport)
		sel.dport_mask = htons(~0);
	sel.sport = xfrm_flowi_sport(fl, &fl6->uli);
	if (sel.sport)
		sel.sport_mask = htons(~0);
	sel.ifindex = fl6->flowi6_oif;

	err = km_report(net, IPPROTO_DSTOPTS, &sel,
			(hao ? (xfrm_address_t *)&hao->addr : NULL));

 out:
	return err;
}

static int mip6_destopt_offset(struct xfrm_state *x, struct sk_buff *skb,
			       u8 **nexthdr)
{
	u16 offset = sizeof(struct ipv6hdr);
	struct ipv6_opt_hdr *exthdr =
				   (struct ipv6_opt_hdr *)(ipv6_hdr(skb) + 1);
	const unsigned char *nh = skb_network_header(skb);
	unsigned int packet_len = skb->tail - skb->network_header;
	int found_rhdr = 0;

	*nexthdr = &ipv6_hdr(skb)->nexthdr;

	while (offset + 1 <= packet_len) {

		switch (**nexthdr) {
		case NEXTHDR_HOP:
			break;
		case NEXTHDR_ROUTING:
			found_rhdr = 1;
			break;
		case NEXTHDR_DEST:
			/*
			 * HAO MUST NOT appear more than once.
			 * XXX: It is better to try to find by the end of
			 * XXX: packet if HAO exists.
			 */
			if (ipv6_find_tlv(skb, offset, IPV6_TLV_HAO) >= 0) {
				LIMIT_NETDEBUG(KERN_WARNING "mip6: hao exists already, override\n");
				return offset;
			}

			if (found_rhdr)
				return offset;

			break;
		default:
			return offset;
		}

		offset += ipv6_optlen(exthdr);
		*nexthdr = &exthdr->nexthdr;
		exthdr = (struct ipv6_opt_hdr *)(nh + offset);
	}

	return offset;
}

static int mip6_destopt_init_state(struct xfrm_state *x)
{
	if (x->id.spi) {
		printk(KERN_INFO "%s: spi is not 0: %u\n", __func__,
		       x->id.spi);
		return -EINVAL;
	}
	if (x->props.mode != XFRM_MODE_ROUTEOPTIMIZATION) {
		printk(KERN_INFO "%s: state's mode is not %u: %u\n",
		       __func__, XFRM_MODE_ROUTEOPTIMIZATION, x->props.mode);
		return -EINVAL;
	}

	x->props.header_len = sizeof(struct ipv6_destopt_hdr) +
		calc_padlen(sizeof(struct ipv6_destopt_hdr), 6) +
		sizeof(struct ipv6_destopt_hao);
	WARN_ON(x->props.header_len != 24);

	return 0;
}

/*
 * Do nothing about destroying since it has no specific operation for
 * destination options header unlike IPsec protocols.
 */
static void mip6_destopt_destroy(struct xfrm_state *x)
{
}

static const struct xfrm_type mip6_destopt_type =
{
	.description	= "MIP6DESTOPT",
	.owner		= THIS_MODULE,
	.proto	     	= IPPROTO_DSTOPTS,
	.flags		= XFRM_TYPE_NON_FRAGMENT | XFRM_TYPE_LOCAL_COADDR,
	.init_state	= mip6_destopt_init_state,
	.destructor	= mip6_destopt_destroy,
	.input		= mip6_destopt_input,
	.output		= mip6_destopt_output,
	.reject		= mip6_destopt_reject,
	.hdr_offset	= mip6_destopt_offset,
};

static int mip6_rthdr_input(struct xfrm_state *x, struct sk_buff *skb)
{
	const struct ipv6hdr *iph = ipv6_hdr(skb);
	struct rt2_hdr *rt2 = (struct rt2_hdr *)skb->data;
	int err = rt2->rt_hdr.nexthdr;

	spin_lock(&x->lock);
	if (!ipv6_addr_equal(&iph->daddr, (struct in6_addr *)x->coaddr) &&
	    !ipv6_addr_any((struct in6_addr *)x->coaddr))
		err = -ENOENT;
	spin_unlock(&x->lock);

	return err;
}

/* Routing Header type 2 is inserted.
 * IP Header's dst address is replaced with Routing Header's Home Address.
 */
static int mip6_rthdr_output(struct xfrm_state *x, struct sk_buff *skb)
{
	struct ipv6hdr *iph;
	struct rt2_hdr *rt2;
	u8 nexthdr;

	skb_push(skb, -skb_network_offset(skb));
	iph = ipv6_hdr(skb);

	nexthdr = *skb_mac_header(skb);
	*skb_mac_header(skb) = IPPROTO_ROUTING;

	rt2 = (struct rt2_hdr *)skb_transport_header(skb);
	rt2->rt_hdr.nexthdr = nexthdr;
	rt2->rt_hdr.hdrlen = (x->props.header_len >> 3) - 1;
	rt2->rt_hdr.type = IPV6_SRCRT_TYPE_2;
	rt2->rt_hdr.segments_left = 1;
	memset(&rt2->reserved, 0, sizeof(rt2->reserved));

	WARN_ON(rt2->rt_hdr.hdrlen != 2);

	memcpy(&rt2->addr, &iph->daddr, sizeof(rt2->addr));
	spin_lock_bh(&x->lock);
	memcpy(&iph->daddr, x->coaddr, sizeof(iph->daddr));
	spin_unlock_bh(&x->lock);

	return 0;
}

static int mip6_rthdr_offset(struct xfrm_state *x, struct sk_buff *skb,
			     u8 **nexthdr)
{
	u16 offset = sizeof(struct ipv6hdr);
	struct ipv6_opt_hdr *exthdr =
				   (struct ipv6_opt_hdr *)(ipv6_hdr(skb) + 1);
	const unsigned char *nh = skb_network_header(skb);
	unsigned int packet_len = skb->tail - skb->network_header;
	int found_rhdr = 0;

	*nexthdr = &ipv6_hdr(skb)->nexthdr;

	while (offset + 1 <= packet_len) {

		switch (**nexthdr) {
		case NEXTHDR_HOP:
			break;
		case NEXTHDR_ROUTING:
			if (offset + 3 <= packet_len) {
				struct ipv6_rt_hdr *rt;
				rt = (struct ipv6_rt_hdr *)(nh + offset);
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
		exthdr = (struct ipv6_opt_hdr *)(nh + offset);
	}

	return offset;
}

static int mip6_rthdr_init_state(struct xfrm_state *x)
{
	if (x->id.spi) {
		printk(KERN_INFO "%s: spi is not 0: %u\n", __func__,
		       x->id.spi);
		return -EINVAL;
	}
	if (x->props.mode != XFRM_MODE_ROUTEOPTIMIZATION) {
		printk(KERN_INFO "%s: state's mode is not %u: %u\n",
		       __func__, XFRM_MODE_ROUTEOPTIMIZATION, x->props.mode);
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

static const struct xfrm_type mip6_rthdr_type =
{
	.description	= "MIP6RT",
	.owner		= THIS_MODULE,
	.proto	     	= IPPROTO_ROUTING,
	.flags		= XFRM_TYPE_NON_FRAGMENT | XFRM_TYPE_REMOTE_COADDR,
	.init_state	= mip6_rthdr_init_state,
	.destructor	= mip6_rthdr_destroy,
	.input		= mip6_rthdr_input,
	.output		= mip6_rthdr_output,
	.hdr_offset	= mip6_rthdr_offset,
};

static int __init mip6_init(void)
{
	printk(KERN_INFO "Mobile IPv6\n");

	if (xfrm_register_type(&mip6_destopt_type, AF_INET6) < 0) {
		printk(KERN_INFO "%s: can't add xfrm type(destopt)\n", __func__);
		goto mip6_destopt_xfrm_fail;
	}
	if (xfrm_register_type(&mip6_rthdr_type, AF_INET6) < 0) {
		printk(KERN_INFO "%s: can't add xfrm type(rthdr)\n", __func__);
		goto mip6_rthdr_xfrm_fail;
	}
	if (rawv6_mh_filter_register(mip6_mh_filter) < 0) {
		printk(KERN_INFO "%s: can't add rawv6 mh filter\n", __func__);
		goto mip6_rawv6_mh_fail;
	}


	return 0;

 mip6_rawv6_mh_fail:
	xfrm_unregister_type(&mip6_rthdr_type, AF_INET6);
 mip6_rthdr_xfrm_fail:
	xfrm_unregister_type(&mip6_destopt_type, AF_INET6);
 mip6_destopt_xfrm_fail:
	return -EAGAIN;
}

static void __exit mip6_fini(void)
{
	if (rawv6_mh_filter_unregister(mip6_mh_filter) < 0)
		printk(KERN_INFO "%s: can't remove rawv6 mh filter\n", __func__);
	if (xfrm_unregister_type(&mip6_rthdr_type, AF_INET6) < 0)
		printk(KERN_INFO "%s: can't remove xfrm type(rthdr)\n", __func__);
	if (xfrm_unregister_type(&mip6_destopt_type, AF_INET6) < 0)
		printk(KERN_INFO "%s: can't remove xfrm type(destopt)\n", __func__);
}

module_init(mip6_init);
module_exit(mip6_fini);

MODULE_LICENSE("GPL");
MODULE_ALIAS_XFRM_TYPE(AF_INET6, XFRM_PROTO_DSTOPTS);
MODULE_ALIAS_XFRM_TYPE(AF_INET6, XFRM_PROTO_ROUTING);
