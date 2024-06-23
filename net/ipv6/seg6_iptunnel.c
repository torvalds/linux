// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  SR-IPv6 implementation
 *
 *  Author:
 *  David Lebrun <david.lebrun@uclouvain.be>
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
#include <linux/netfilter.h>

static size_t seg6_lwt_headroom(struct seg6_iptunnel_encap *tuninfo)
{
	int head = 0;

	switch (tuninfo->mode) {
	case SEG6_IPTUN_MODE_INLINE:
		break;
	case SEG6_IPTUN_MODE_ENCAP:
	case SEG6_IPTUN_MODE_ENCAP_RED:
		head = sizeof(struct ipv6hdr);
		break;
	case SEG6_IPTUN_MODE_L2ENCAP:
	case SEG6_IPTUN_MODE_L2ENCAP_RED:
		return 0;
	}

	return ((tuninfo->srh->hdrlen + 1) << 3) + head;
}

struct seg6_lwt {
	struct dst_cache cache;
	struct seg6_iptunnel_encap tuninfo[];
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

/* Compute flowlabel for outer IPv6 header */
static __be32 seg6_make_flowlabel(struct net *net, struct sk_buff *skb,
				  struct ipv6hdr *inner_hdr)
{
	int do_flowlabel = net->ipv6.sysctl.seg6_flowlabel;
	__be32 flowlabel = 0;
	u32 hash;

	if (do_flowlabel > 0) {
		hash = skb_get_hash(skb);
		hash = rol32(hash, 16);
		flowlabel = (__force __be32)hash & IPV6_FLOWLABEL_MASK;
	} else if (!do_flowlabel && skb->protocol == htons(ETH_P_IPV6)) {
		flowlabel = ip6_flowlabel(inner_hdr);
	}
	return flowlabel;
}

/* encapsulate an IPv6 packet within an outer IPv6 header with a given SRH */
int seg6_do_srh_encap(struct sk_buff *skb, struct ipv6_sr_hdr *osrh, int proto)
{
	struct dst_entry *dst = skb_dst(skb);
	struct net *net = dev_net(dst->dev);
	struct ipv6hdr *hdr, *inner_hdr;
	struct ipv6_sr_hdr *isrh;
	int hdrlen, tot_len, err;
	__be32 flowlabel;

	hdrlen = (osrh->hdrlen + 1) << 3;
	tot_len = hdrlen + sizeof(*hdr);

	err = skb_cow_head(skb, tot_len + skb->mac_len);
	if (unlikely(err))
		return err;

	inner_hdr = ipv6_hdr(skb);
	flowlabel = seg6_make_flowlabel(net, skb, inner_hdr);

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
			     flowlabel);
		hdr->hop_limit = inner_hdr->hop_limit;
	} else {
		ip6_flow_hdr(hdr, 0, flowlabel);
		hdr->hop_limit = ip6_dst_hoplimit(skb_dst(skb));

		memset(IP6CB(skb), 0, sizeof(*IP6CB(skb)));

		/* the control block has been erased, so we have to set the
		 * iif once again.
		 * We read the receiving interface index directly from the
		 * skb->skb_iif as it is done in the IPv4 receiving path (i.e.:
		 * ip_rcv_core(...)).
		 */
		IP6CB(skb)->iif = skb->skb_iif;
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

	hdr->payload_len = htons(skb->len - sizeof(struct ipv6hdr));

	skb_postpush_rcsum(skb, hdr, tot_len);

	return 0;
}
EXPORT_SYMBOL_GPL(seg6_do_srh_encap);

/* encapsulate an IPv6 packet within an outer IPv6 header with reduced SRH */
static int seg6_do_srh_encap_red(struct sk_buff *skb,
				 struct ipv6_sr_hdr *osrh, int proto)
{
	__u8 first_seg = osrh->first_segment;
	struct dst_entry *dst = skb_dst(skb);
	struct net *net = dev_net(dst->dev);
	struct ipv6hdr *hdr, *inner_hdr;
	int hdrlen = ipv6_optlen(osrh);
	int red_tlv_offset, tlv_offset;
	struct ipv6_sr_hdr *isrh;
	bool skip_srh = false;
	__be32 flowlabel;
	int tot_len, err;
	int red_hdrlen;
	int tlvs_len;

	if (first_seg > 0) {
		red_hdrlen = hdrlen - sizeof(struct in6_addr);
	} else {
		/* NOTE: if tag/flags and/or other TLVs are introduced in the
		 * seg6_iptunnel infrastructure, they should be considered when
		 * deciding to skip the SRH.
		 */
		skip_srh = !sr_has_hmac(osrh);

		red_hdrlen = skip_srh ? 0 : hdrlen;
	}

	tot_len = red_hdrlen + sizeof(struct ipv6hdr);

	err = skb_cow_head(skb, tot_len + skb->mac_len);
	if (unlikely(err))
		return err;

	inner_hdr = ipv6_hdr(skb);
	flowlabel = seg6_make_flowlabel(net, skb, inner_hdr);

	skb_push(skb, tot_len);
	skb_reset_network_header(skb);
	skb_mac_header_rebuild(skb);
	hdr = ipv6_hdr(skb);

	/* based on seg6_do_srh_encap() */
	if (skb->protocol == htons(ETH_P_IPV6)) {
		ip6_flow_hdr(hdr, ip6_tclass(ip6_flowinfo(inner_hdr)),
			     flowlabel);
		hdr->hop_limit = inner_hdr->hop_limit;
	} else {
		ip6_flow_hdr(hdr, 0, flowlabel);
		hdr->hop_limit = ip6_dst_hoplimit(skb_dst(skb));

		memset(IP6CB(skb), 0, sizeof(*IP6CB(skb)));
		IP6CB(skb)->iif = skb->skb_iif;
	}

	/* no matter if we have to skip the SRH or not, the first segment
	 * always comes in the pushed IPv6 header.
	 */
	hdr->daddr = osrh->segments[first_seg];

	if (skip_srh) {
		hdr->nexthdr = proto;

		set_tun_src(net, dst->dev, &hdr->daddr, &hdr->saddr);
		goto out;
	}

	/* we cannot skip the SRH, slow path */

	hdr->nexthdr = NEXTHDR_ROUTING;
	isrh = (void *)hdr + sizeof(struct ipv6hdr);

	if (unlikely(!first_seg)) {
		/* this is a very rare case; we have only one SID but
		 * we cannot skip the SRH since we are carrying some
		 * other info.
		 */
		memcpy(isrh, osrh, hdrlen);
		goto srcaddr;
	}

	tlv_offset = sizeof(*osrh) + (first_seg + 1) * sizeof(struct in6_addr);
	red_tlv_offset = tlv_offset - sizeof(struct in6_addr);

	memcpy(isrh, osrh, red_tlv_offset);

	tlvs_len = hdrlen - tlv_offset;
	if (unlikely(tlvs_len > 0)) {
		const void *s = (const void *)osrh + tlv_offset;
		void *d = (void *)isrh + red_tlv_offset;

		memcpy(d, s, tlvs_len);
	}

	--isrh->first_segment;
	isrh->hdrlen -= 2;

srcaddr:
	isrh->nexthdr = proto;
	set_tun_src(net, dst->dev, &hdr->daddr, &hdr->saddr);

#ifdef CONFIG_IPV6_SEG6_HMAC
	if (unlikely(!skip_srh && sr_has_hmac(isrh))) {
		err = seg6_push_hmac(net, &hdr->saddr, isrh);
		if (unlikely(err))
			return err;
	}
#endif

out:
	hdr->payload_len = htons(skb->len - sizeof(struct ipv6hdr));

	skb_postpush_rcsum(skb, hdr, tot_len);

	return 0;
}

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

	hdr->payload_len = htons(skb->len - sizeof(struct ipv6hdr));

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
	case SEG6_IPTUN_MODE_ENCAP_RED:
		err = iptunnel_handle_offloads(skb, SKB_GSO_IPXIP6);
		if (err)
			return err;

		if (skb->protocol == htons(ETH_P_IPV6))
			proto = IPPROTO_IPV6;
		else if (skb->protocol == htons(ETH_P_IP))
			proto = IPPROTO_IPIP;
		else
			return -EINVAL;

		if (tinfo->mode == SEG6_IPTUN_MODE_ENCAP)
			err = seg6_do_srh_encap(skb, tinfo->srh, proto);
		else
			err = seg6_do_srh_encap_red(skb, tinfo->srh, proto);

		if (err)
			return err;

		skb_set_inner_transport_header(skb, skb_transport_offset(skb));
		skb_set_inner_protocol(skb, skb->protocol);
		skb->protocol = htons(ETH_P_IPV6);
		break;
	case SEG6_IPTUN_MODE_L2ENCAP:
	case SEG6_IPTUN_MODE_L2ENCAP_RED:
		if (!skb_mac_header_was_set(skb))
			return -EINVAL;

		if (pskb_expand_head(skb, skb->mac_len, 0, GFP_ATOMIC) < 0)
			return -ENOMEM;

		skb_mac_header_rebuild(skb);
		skb_push(skb, skb->mac_len);

		if (tinfo->mode == SEG6_IPTUN_MODE_L2ENCAP)
			err = seg6_do_srh_encap(skb, tinfo->srh,
						IPPROTO_ETHERNET);
		else
			err = seg6_do_srh_encap_red(skb, tinfo->srh,
						    IPPROTO_ETHERNET);

		if (err)
			return err;

		skb->protocol = htons(ETH_P_IPV6);
		break;
	}

	skb_set_transport_header(skb, sizeof(struct ipv6hdr));
	nf_reset_ct(skb);

	return 0;
}

static int seg6_input_finish(struct net *net, struct sock *sk,
			     struct sk_buff *skb)
{
	return dst_input(skb);
}

static int seg6_input_core(struct net *net, struct sock *sk,
			   struct sk_buff *skb)
{
	struct dst_entry *orig_dst = skb_dst(skb);
	struct dst_entry *dst = NULL;
	struct seg6_lwt *slwt;
	int err;

	err = seg6_do_srh(skb);
	if (unlikely(err))
		goto drop;

	slwt = seg6_lwt_lwtunnel(orig_dst->lwtstate);

	local_bh_disable();
	dst = dst_cache_get(&slwt->cache);

	if (!dst) {
		ip6_route_input(skb);
		dst = skb_dst(skb);
		if (!dst->error) {
			dst_cache_set_ip6(&slwt->cache, dst,
					  &ipv6_hdr(skb)->saddr);
		}
	} else {
		skb_dst_drop(skb);
		skb_dst_set(skb, dst);
	}
	local_bh_enable();

	err = skb_cow_head(skb, LL_RESERVED_SPACE(dst->dev));
	if (unlikely(err))
		goto drop;

	if (static_branch_unlikely(&nf_hooks_lwtunnel_enabled))
		return NF_HOOK(NFPROTO_IPV6, NF_INET_LOCAL_OUT,
			       dev_net(skb->dev), NULL, skb, NULL,
			       skb_dst(skb)->dev, seg6_input_finish);

	return seg6_input_finish(dev_net(skb->dev), NULL, skb);
drop:
	kfree_skb(skb);
	return err;
}

static int seg6_input_nf(struct sk_buff *skb)
{
	struct net_device *dev = skb_dst(skb)->dev;
	struct net *net = dev_net(skb->dev);

	switch (skb->protocol) {
	case htons(ETH_P_IP):
		return NF_HOOK(NFPROTO_IPV4, NF_INET_POST_ROUTING, net, NULL,
			       skb, NULL, dev, seg6_input_core);
	case htons(ETH_P_IPV6):
		return NF_HOOK(NFPROTO_IPV6, NF_INET_POST_ROUTING, net, NULL,
			       skb, NULL, dev, seg6_input_core);
	}

	return -EINVAL;
}

static int seg6_input(struct sk_buff *skb)
{
	if (static_branch_unlikely(&nf_hooks_lwtunnel_enabled))
		return seg6_input_nf(skb);

	return seg6_input_core(dev_net(skb->dev), NULL, skb);
}

static int seg6_output_core(struct net *net, struct sock *sk,
			    struct sk_buff *skb)
{
	struct dst_entry *orig_dst = skb_dst(skb);
	struct dst_entry *dst = NULL;
	struct seg6_lwt *slwt;
	int err;

	err = seg6_do_srh(skb);
	if (unlikely(err))
		goto drop;

	slwt = seg6_lwt_lwtunnel(orig_dst->lwtstate);

	local_bh_disable();
	dst = dst_cache_get(&slwt->cache);
	local_bh_enable();

	if (unlikely(!dst)) {
		struct ipv6hdr *hdr = ipv6_hdr(skb);
		struct flowi6 fl6;

		memset(&fl6, 0, sizeof(fl6));
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

		local_bh_disable();
		dst_cache_set_ip6(&slwt->cache, dst, &fl6.saddr);
		local_bh_enable();
	}

	skb_dst_drop(skb);
	skb_dst_set(skb, dst);

	err = skb_cow_head(skb, LL_RESERVED_SPACE(dst->dev));
	if (unlikely(err))
		goto drop;

	if (static_branch_unlikely(&nf_hooks_lwtunnel_enabled))
		return NF_HOOK(NFPROTO_IPV6, NF_INET_LOCAL_OUT, net, sk, skb,
			       NULL, skb_dst(skb)->dev, dst_output);

	return dst_output(net, sk, skb);
drop:
	kfree_skb(skb);
	return err;
}

static int seg6_output_nf(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	struct net_device *dev = skb_dst(skb)->dev;

	switch (skb->protocol) {
	case htons(ETH_P_IP):
		return NF_HOOK(NFPROTO_IPV4, NF_INET_POST_ROUTING, net, sk, skb,
			       NULL, dev, seg6_output_core);
	case htons(ETH_P_IPV6):
		return NF_HOOK(NFPROTO_IPV6, NF_INET_POST_ROUTING, net, sk, skb,
			       NULL, dev, seg6_output_core);
	}

	return -EINVAL;
}

static int seg6_output(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	if (static_branch_unlikely(&nf_hooks_lwtunnel_enabled))
		return seg6_output_nf(net, sk, skb);

	return seg6_output_core(net, sk, skb);
}

static int seg6_build_state(struct net *net, struct nlattr *nla,
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

	err = nla_parse_nested_deprecated(tb, SEG6_IPTUNNEL_MAX, nla,
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
	case SEG6_IPTUN_MODE_ENCAP_RED:
		break;
	case SEG6_IPTUN_MODE_L2ENCAP_RED:
		break;
	default:
		return -EINVAL;
	}

	/* verify that SRH is consistent */
	if (!seg6_validate_srh(tuninfo->srh, tuninfo_len - sizeof(*tuninfo), false))
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
