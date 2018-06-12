/*
 *  SR-IPv6 implementation
 *
 *  Author:
 *  David Lebrun <david.lebrun@uclouvain.be>
 *
 *
 *  This program is free software; you can redistribute it and/or
 *        modify it under the terms of the GNU General Public License
 *        as published by the Free Software Foundation; either version
 *        2 of the License, or (at your option) any later version.
 */

#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/net.h>
#include <linux/module.h>
#include <net/ip.h>
#include <net/ip_tunnels.h>
#include <net/lwtunnel.h>
#include <net/netevent.h>
#include <net/netns/generic.h>
#include <net/ip6_fib.h>
#include <net/route.h>
#include <net/seg6.h>
#include <linux/seg6.h>
#include <linux/seg6_iptunnel.h>
#include <net/addrconf.h>
#include <net/ip6_route.h>
#include <net/dst_cache.h>
#ifdef CONFIG_IPV6_SEG6_HMAC
#include <net/seg6_hmac.h>
#endif

struct seg6_lwt {
	struct dst_cache cache;
	struct seg6_iptunnel_encap tuninfo[0];
};

static inline struct seg6_lwt *seg6_lwt_lwtunnel(struct lwtunnel_state *lwt)
{
	return (struct seg6_lwt *)lwt->data;
}

static inline struct seg6_iptunnel_encap *
seg6_encap_lwtunnel(struct lwtunnel_state *lwt)
{
	return seg6_lwt_lwtunnel(lwt)->tuninfo;
}

static const struct nla_policy seg6_iptunnel_policy[SEG6_IPTUNNEL_MAX + 1] = {
	[SEG6_IPTUNNEL_SRH]	= { .type = NLA_BINARY },
};

static int nla_put_srh(struct sk_buff *skb, int attrtype,
		       struct seg6_iptunnel_encap *tuninfo)
{
	struct seg6_iptunnel_encap *data;
	struct nlattr *nla;
	int len;

	len = SEG6_IPTUN_ENCAP_SIZE(tuninfo);

	nla = nla_reserve(skb, attrtype, len);
	if (!nla)
		return -EMSGSIZE;

	data = nla_data(nla);
	memcpy(data, tuninfo, len);

	return 0;
}

static void set_tun_src(struct net *net, struct net_device *dev,
			struct in6_addr *daddr, struct in6_addr *saddr)
{
	struct seg6_pernet_data *sdata = seg6_pernet(net);
	struct in6_addr *tun_src;

	rcu_read_lock();

	tun_src = rcu_dereference(sdata->tun_src);

	if (!ipv6_addr_any(tun_src)) {
		memcpy(saddr, tun_src, sizeof(struct in6_addr));
	} else {
		ipv6_dev_get_saddr(net, dev, daddr, IPV6_PREFER_SRC_PUBLIC,
				   saddr);
	}

	rcu_read_unlock();
}

/* encapsulate an IPv6 packet within an outer IPv6 header with a given SRH */
int seg6_do_srh_encap(struct sk_buff *skb, struct ipv6_sr_hdr *osrh, int proto)
{
	struct dst_entry *dst = skb_dst(skb);
	struct net *net = dev_net(dst->dev);
	struct ipv6hdr *hdr, *inner_hdr;
	struct ipv6_sr_hdr *isrh;
	int hdrlen, tot_len, err;

	hdrlen = (osrh->hdrlen + 1) << 3;
	tot_len = hdrlen + sizeof(*hdr);

	err = skb_cow_head(skb, tot_len + skb->mac_len);
	if (unlikely(err))
		return err;

	inner_hdr = ipv6_hdr(skb);

	skb_push(skb, tot_len);
	skb_reset_network_header(skb);
	skb_mac_header_rebuild(skb);
	hdr = ipv6_hdr(skb);

	/* inherit tc, flowlabel and hlim
	 * hlim will be decremented in ip6_forward() afterwards and
	 * decapsulation will overwrite inner hlim with outer hlim
	 */

	if (skb->protocol == htons(ETH_P_IPV6)) {
		ip6_flow_hdr(hdr, ip6_tclass(ip6_flowinfo(inner_hdr)),
			     ip6_flowlabel(inner_hdr));
		hdr->hop_limit = inner_hdr->hop_limit;
	} else {
		ip6_flow_hdr(hdr, 0, 0);
		hdr->hop_limit = ip6_dst_hoplimit(skb_dst(skb));
	}

	hdr->nexthdr = NEXTHDR_ROUTING;

	isrh = (void *)hdr + sizeof(*hdr);
	memcpy(isrh, osrh, hdrlen);

	isrh->nexthdr = proto;

	hdr->daddr = isrh->segments[isrh->first_segment];
	set_tun_src(net, dst->dev, &hdr->daddr, &hdr->saddr);

#ifdef CONFIG_IPV6_SEG6_HMAC
	if (sr_has_hmac(isrh)) {
		err = seg6_push_hmac(net, &hdr->saddr, isrh);
		if (unlikely(err))
			return err;
	}
#endif

	skb_postpush_rcsum(skb, hdr, tot_len);

	return 0;
}
EXPORT_SYMBOL_GPL(seg6_do_srh_encap);

/* insert an SRH within an IPv6 packet, just after the IPv6 header */
int seg6_do_srh_inline(struct sk_buff *skb, struct ipv6_sr_hdr *osrh)
{
	struct ipv6hdr *hdr, *oldhdr;
	struct ipv6_sr_hdr *isrh;
	int hdrlen, err;

	hdrlen = (osrh->hdrlen + 1) << 3;

	err = skb_cow_head(skb, hdrlen + skb->mac_len);
	if (unlikely(err))
		return err;

	oldhdr = ipv6_hdr(skb);

	skb_pull(skb, sizeof(struct ipv6hdr));
	skb_postpull_rcsum(skb, skb_network_header(skb),
			   sizeof(struct ipv6hdr));

	skb_push(skb, sizeof(struct ipv6hdr) + hdrlen);
	skb_reset_network_header(skb);
	skb_mac_header_rebuild(skb);

	hdr = ipv6_hdr(skb);

	memmove(hdr, oldhdr, sizeof(*hdr));

	isrh = (void *)hdr + sizeof(*hdr);
	memcpy(isrh, osrh, hdrlen);

	isrh->nexthdr = hdr->nexthdr;
	hdr->nexthdr = NEXTHDR_ROUTING;

	isrh->segments[0] = hdr->daddr;
	hdr->daddr = isrh->segments[isrh->first_segment];

#ifdef CONFIG_IPV6_SEG6_HMAC
	if (sr_has_hmac(isrh)) {
		struct net *net = dev_net(skb_dst(skb)->dev);

		err = seg6_push_hmac(net, &hdr->saddr, isrh);
		if (unlikely(err))
			return err;
	}
#endif

	skb_postpush_rcsum(skb, hdr, sizeof(struct ipv6hdr) + hdrlen);

	return 0;
}
EXPORT_SYMBOL_GPL(seg6_do_srh_inline);

static int seg6_do_srh(struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);
	struct seg6_iptunnel_encap *tinfo;
	int proto, err = 0;

	tinfo = seg6_encap_lwtunnel(dst->lwtstate);

	switch (tinfo->mode) {
	case SEG6_IPTUN_MODE_INLINE:
		if (skb->protocol != htons(ETH_P_IPV6))
			return -EINVAL;

		err = seg6_do_srh_inline(skb, tinfo->srh);
		if (err)
			return err;
		break;
	case SEG6_IPTUN_MODE_ENCAP:
		err = iptunnel_handle_offloads(skb, SKB_GSO_IPXIP6);
		if (err)
			return err;

		if (skb->protocol == htons(ETH_P_IPV6))
			proto = IPPROTO_IPV6;
		else if (skb->protocol == htons(ETH_P_IP))
			proto = IPPROTO_IPIP;
		else
			return -EINVAL;

		err = seg6_do_srh_encap(skb, tinfo->srh, proto);
		if (err)
			return err;

		skb_set_inner_transport_header(skb, skb_transport_offset(skb));
		skb_set_inner_protocol(skb, skb->protocol);
		skb->protocol = htons(ETH_P_IPV6);
		break;
	case SEG6_IPTUN_MODE_L2ENCAP:
		if (!skb_mac_header_was_set(skb))
			return -EINVAL;

		if (pskb_expand_head(skb, skb->mac_len, 0, GFP_ATOMIC) < 0)
			return -ENOMEM;

		skb_mac_header_rebuild(skb);
		skb_push(skb, skb->mac_len);

		err = seg6_do_srh_encap(skb, tinfo->srh, NEXTHDR_NONE);
		if (err)
			return err;

		skb->protocol = htons(ETH_P_IPV6);
		break;
	}

	ipv6_hdr(skb)->payload_len = htons(skb->len - sizeof(struct ipv6hdr));
	skb_set_transport_header(skb, sizeof(struct ipv6hdr));

	return 0;
}

static int seg6_input(struct sk_buff *skb)
{
	struct dst_entry *orig_dst = skb_dst(skb);
	struct dst_entry *dst = NULL;
	struct seg6_lwt *slwt;
	int err;

	err = seg6_do_srh(skb);
	if (unlikely(err)) {
		kfree_skb(skb);
		return err;
	}

	slwt = seg6_lwt_lwtunnel(orig_dst->lwtstate);

	preempt_disable();
	dst = dst_cache_get(&slwt->cache);
	preempt_enable();

	skb_dst_drop(skb);

	if (!dst) {
		ip6_route_input(skb);
		dst = skb_dst(skb);
		if (!dst->error) {
			preempt_disable();
			dst_cache_set_ip6(&slwt->cache, dst,
					  &ipv6_hdr(skb)->saddr);
			preempt_enable();
		}
	} else {
		skb_dst_set(skb, dst);
	}

	err = skb_cow_head(skb, LL_RESERVED_SPACE(dst->dev));
	if (unlikely(err))
		return err;

	return dst_input(skb);
}

static int seg6_output(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	struct dst_entry *orig_dst = skb_dst(skb);
	struct dst_entry *dst = NULL;
	struct seg6_lwt *slwt;
	int err = -EINVAL;

	err = seg6_do_srh(skb);
	if (unlikely(err))
		goto drop;

	slwt = seg6_lwt_lwtunnel(orig_dst->lwtstate);

	preempt_disable();
	dst = dst_cache_get(&slwt->cache);
	preempt_enable();

	if (unlikely(!dst)) {
		struct ipv6hdr *hdr = ipv6_hdr(skb);
		struct flowi6 fl6;

		fl6.daddr = hdr->daddr;
		fl6.saddr = hdr->saddr;
		fl6.flowlabel = ip6_flowinfo(hdr);
		fl6.flowi6_mark = skb->mark;
		fl6.flowi6_proto = hdr->nexthdr;

		dst = ip6_route_output(net, NULL, &fl6);
		if (dst->error) {
			err = dst->error;
			dst_release(dst);
			goto drop;
		}

		preempt_disable();
		dst_cache_set_ip6(&slwt->cache, dst, &fl6.saddr);
		preempt_enable();
	}

	skb_dst_drop(skb);
	skb_dst_set(skb, dst);

	err = skb_cow_head(skb, LL_RESERVED_SPACE(dst->dev));
	if (unlikely(err))
		goto drop;

	return dst_output(net, sk, skb);
drop:
	kfree_skb(skb);
	return err;
}

static int seg6_build_state(struct nlattr *nla,
			    unsigned int family, const void *cfg,
			    struct lwtunnel_state **ts,
			    struct netlink_ext_ack *extack)
{
	struct nlattr *tb[SEG6_IPTUNNEL_MAX + 1];
	struct seg6_iptunnel_encap *tuninfo;
	struct lwtunnel_state *newts;
	int tuninfo_len, min_size;
	struct seg6_lwt *slwt;
	int err;

	if (family != AF_INET && family != AF_INET6)
		return -EINVAL;

	err = nla_parse_nested(tb, SEG6_IPTUNNEL_MAX, nla,
			       seg6_iptunnel_policy, extack);

	if (err < 0)
		return err;

	if (!tb[SEG6_IPTUNNEL_SRH])
		return -EINVAL;

	tuninfo = nla_data(tb[SEG6_IPTUNNEL_SRH]);
	tuninfo_len = nla_len(tb[SEG6_IPTUNNEL_SRH]);

	/* tuninfo must contain at least the iptunnel encap structure,
	 * the SRH and one segment
	 */
	min_size = sizeof(*tuninfo) + sizeof(struct ipv6_sr_hdr) +
		   sizeof(struct in6_addr);
	if (tuninfo_len < min_size)
		return -EINVAL;

	switch (tuninfo->mode) {
	case SEG6_IPTUN_MODE_INLINE:
		if (family != AF_INET6)
			return -EINVAL;

		break;
	case SEG6_IPTUN_MODE_ENCAP:
		break;
	case SEG6_IPTUN_MODE_L2ENCAP:
		break;
	default:
		return -EINVAL;
	}

	/* verify that SRH is consistent */
	if (!seg6_validate_srh(tuninfo->srh, tuninfo_len - sizeof(*tuninfo)))
		return -EINVAL;

	newts = lwtunnel_state_alloc(tuninfo_len + sizeof(*slwt));
	if (!newts)
		return -ENOMEM;

	slwt = seg6_lwt_lwtunnel(newts);

	err = dst_cache_init(&slwt->cache, GFP_ATOMIC);
	if (err) {
		kfree(newts);
		return err;
	}

	memcpy(&slwt->tuninfo, tuninfo, tuninfo_len);

	newts->type = LWTUNNEL_ENCAP_SEG6;
	newts->flags |= LWTUNNEL_STATE_INPUT_REDIRECT;

	if (tuninfo->mode != SEG6_IPTUN_MODE_L2ENCAP)
		newts->flags |= LWTUNNEL_STATE_OUTPUT_REDIRECT;

	newts->headroom = seg6_lwt_headroom(tuninfo);

	*ts = newts;

	return 0;
}

static void seg6_destroy_state(struct lwtunnel_state *lwt)
{
	dst_cache_destroy(&seg6_lwt_lwtunnel(lwt)->cache);
}

static int seg6_fill_encap_info(struct sk_buff *skb,
				struct lwtunnel_state *lwtstate)
{
	struct seg6_iptunnel_encap *tuninfo = seg6_encap_lwtunnel(lwtstate);

	if (nla_put_srh(skb, SEG6_IPTUNNEL_SRH, tuninfo))
		return -EMSGSIZE;

	return 0;
}

static int seg6_encap_nlsize(struct lwtunnel_state *lwtstate)
{
	struct seg6_iptunnel_encap *tuninfo = seg6_encap_lwtunnel(lwtstate);

	return nla_total_size(SEG6_IPTUN_ENCAP_SIZE(tuninfo));
}

static int seg6_encap_cmp(struct lwtunnel_state *a, struct lwtunnel_state *b)
{
	struct seg6_iptunnel_encap *a_hdr = seg6_encap_lwtunnel(a);
	struct seg6_iptunnel_encap *b_hdr = seg6_encap_lwtunnel(b);
	int len = SEG6_IPTUN_ENCAP_SIZE(a_hdr);

	if (len != SEG6_IPTUN_ENCAP_SIZE(b_hdr))
		return 1;

	return memcmp(a_hdr, b_hdr, len);
}

static const struct lwtunnel_encap_ops seg6_iptun_ops = {
	.build_state = seg6_build_state,
	.destroy_state = seg6_destroy_state,
	.output = seg6_output,
	.input = seg6_input,
	.fill_encap = seg6_fill_encap_info,
	.get_encap_size = seg6_encap_nlsize,
	.cmp_encap = seg6_encap_cmp,
	.owner = THIS_MODULE,
};

int __init seg6_iptunnel_init(void)
{
	return lwtunnel_encap_add_ops(&seg6_iptun_ops, LWTUNNEL_ENCAP_SEG6);
}

void seg6_iptunnel_exit(void)
{
	lwtunnel_encap_del_ops(&seg6_iptun_ops, LWTUNNEL_ENCAP_SEG6);
}
