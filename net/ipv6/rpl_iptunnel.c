// SPDX-License-Identifier: GPL-2.0-only
/**
 * Authors:
 * (C) 2020 Alexander Aring <alex.aring@gmail.com>
 */

#include <linux/rpl_iptunnel.h>

#include <net/dst_cache.h>
#include <net/ip6_route.h>
#include <net/lwtunnel.h>
#include <net/ipv6.h>
#include <net/rpl.h>

struct rpl_iptunnel_encap {
	struct ipv6_rpl_sr_hdr srh[0];
};

struct rpl_lwt {
	struct dst_cache cache;
	struct rpl_iptunnel_encap tuninfo;
};

static inline struct rpl_lwt *rpl_lwt_lwtunnel(struct lwtunnel_state *lwt)
{
	return (struct rpl_lwt *)lwt->data;
}

static inline struct rpl_iptunnel_encap *
rpl_encap_lwtunnel(struct lwtunnel_state *lwt)
{
	return &rpl_lwt_lwtunnel(lwt)->tuninfo;
}

static const struct nla_policy rpl_iptunnel_policy[RPL_IPTUNNEL_MAX + 1] = {
	[RPL_IPTUNNEL_SRH]	= { .type = NLA_BINARY },
};

static bool rpl_validate_srh(struct net *net, struct ipv6_rpl_sr_hdr *srh,
			     size_t seglen)
{
	int err;

	if ((srh->hdrlen << 3) != seglen)
		return false;

	/* check at least one segment and seglen fit with segments_left */
	if (!srh->segments_left ||
	    (srh->segments_left * sizeof(struct in6_addr)) != seglen)
		return false;

	if (srh->cmpri || srh->cmpre)
		return false;

	err = ipv6_chk_rpl_srh_loop(net, srh->rpl_segaddr,
				    srh->segments_left);
	if (err)
		return false;

	if (ipv6_addr_type(&srh->rpl_segaddr[srh->segments_left - 1]) &
	    IPV6_ADDR_MULTICAST)
		return false;

	return true;
}

static int rpl_build_state(struct net *net, struct nlattr *nla,
			   unsigned int family, const void *cfg,
			   struct lwtunnel_state **ts,
			   struct netlink_ext_ack *extack)
{
	struct nlattr *tb[RPL_IPTUNNEL_MAX + 1];
	struct lwtunnel_state *newts;
	struct ipv6_rpl_sr_hdr *srh;
	struct rpl_lwt *rlwt;
	int err, srh_len;

	if (family != AF_INET6)
		return -EINVAL;

	err = nla_parse_nested(tb, RPL_IPTUNNEL_MAX, nla,
			       rpl_iptunnel_policy, extack);
	if (err < 0)
		return err;

	if (!tb[RPL_IPTUNNEL_SRH])
		return -EINVAL;

	srh = nla_data(tb[RPL_IPTUNNEL_SRH]);
	srh_len = nla_len(tb[RPL_IPTUNNEL_SRH]);

	if (srh_len < sizeof(*srh))
		return -EINVAL;

	/* verify that SRH is consistent */
	if (!rpl_validate_srh(net, srh, srh_len - sizeof(*srh)))
		return -EINVAL;

	newts = lwtunnel_state_alloc(srh_len + sizeof(*rlwt));
	if (!newts)
		return -ENOMEM;

	rlwt = rpl_lwt_lwtunnel(newts);

	err = dst_cache_init(&rlwt->cache, GFP_ATOMIC);
	if (err) {
		kfree(newts);
		return err;
	}

	memcpy(&rlwt->tuninfo.srh, srh, srh_len);

	newts->type = LWTUNNEL_ENCAP_RPL;
	newts->flags |= LWTUNNEL_STATE_INPUT_REDIRECT;
	newts->flags |= LWTUNNEL_STATE_OUTPUT_REDIRECT;

	*ts = newts;

	return 0;
}

static void rpl_destroy_state(struct lwtunnel_state *lwt)
{
	dst_cache_destroy(&rpl_lwt_lwtunnel(lwt)->cache);
}

static int rpl_do_srh_inline(struct sk_buff *skb, const struct rpl_lwt *rlwt,
			     const struct ipv6_rpl_sr_hdr *srh)
{
	struct ipv6_rpl_sr_hdr *isrh, *csrh;
	const struct ipv6hdr *oldhdr;
	struct ipv6hdr *hdr;
	unsigned char *buf;
	size_t hdrlen;
	int err;

	oldhdr = ipv6_hdr(skb);

	buf = kzalloc(ipv6_rpl_srh_alloc_size(srh->segments_left - 1) * 2,
		      GFP_ATOMIC);
	if (!buf)
		return -ENOMEM;

	isrh = (struct ipv6_rpl_sr_hdr *)buf;
	csrh = (struct ipv6_rpl_sr_hdr *)(buf + ((srh->hdrlen + 1) << 3));

	memcpy(isrh, srh, sizeof(*isrh));
	memcpy(isrh->rpl_segaddr, &srh->rpl_segaddr[1],
	       (srh->segments_left - 1) * 16);
	isrh->rpl_segaddr[srh->segments_left - 1] = oldhdr->daddr;

	ipv6_rpl_srh_compress(csrh, isrh, &srh->rpl_segaddr[0],
			      isrh->segments_left - 1);

	hdrlen = ((csrh->hdrlen + 1) << 3);

	err = skb_cow_head(skb, hdrlen + skb->mac_len);
	if (unlikely(err)) {
		kfree(buf);
		return err;
	}

	skb_pull(skb, sizeof(struct ipv6hdr));
	skb_postpull_rcsum(skb, skb_network_header(skb),
			   sizeof(struct ipv6hdr));

	skb_push(skb, sizeof(struct ipv6hdr) + hdrlen);
	skb_reset_network_header(skb);
	skb_mac_header_rebuild(skb);

	hdr = ipv6_hdr(skb);
	memmove(hdr, oldhdr, sizeof(*hdr));
	isrh = (void *)hdr + sizeof(*hdr);
	memcpy(isrh, csrh, hdrlen);

	isrh->nexthdr = hdr->nexthdr;
	hdr->nexthdr = NEXTHDR_ROUTING;
	hdr->daddr = srh->rpl_segaddr[0];

	ipv6_hdr(skb)->payload_len = htons(skb->len - sizeof(struct ipv6hdr));
	skb_set_transport_header(skb, sizeof(struct ipv6hdr));

	skb_postpush_rcsum(skb, hdr, sizeof(struct ipv6hdr) + hdrlen);

	kfree(buf);

	return 0;
}

static int rpl_do_srh(struct sk_buff *skb, const struct rpl_lwt *rlwt)
{
	struct dst_entry *dst = skb_dst(skb);
	struct rpl_iptunnel_encap *tinfo;
	int err = 0;

	if (skb->protocol != htons(ETH_P_IPV6))
		return -EINVAL;

	tinfo = rpl_encap_lwtunnel(dst->lwtstate);

	err = rpl_do_srh_inline(skb, rlwt, tinfo->srh);
	if (err)
		return err;

	return 0;
}

static int rpl_output(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	struct dst_entry *orig_dst = skb_dst(skb);
	struct dst_entry *dst = NULL;
	struct rpl_lwt *rlwt;
	int err;

	rlwt = rpl_lwt_lwtunnel(orig_dst->lwtstate);

	err = rpl_do_srh(skb, rlwt);
	if (unlikely(err))
		goto drop;

	preempt_disable();
	dst = dst_cache_get(&rlwt->cache);
	preempt_enable();

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

		preempt_disable();
		dst_cache_set_ip6(&rlwt->cache, dst, &fl6.saddr);
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

static int rpl_input(struct sk_buff *skb)
{
	struct dst_entry *orig_dst = skb_dst(skb);
	struct dst_entry *dst = NULL;
	struct rpl_lwt *rlwt;
	int err;

	rlwt = rpl_lwt_lwtunnel(orig_dst->lwtstate);

	err = rpl_do_srh(skb, rlwt);
	if (unlikely(err)) {
		kfree_skb(skb);
		return err;
	}

	preempt_disable();
	dst = dst_cache_get(&rlwt->cache);
	preempt_enable();

	skb_dst_drop(skb);

	if (!dst) {
		ip6_route_input(skb);
		dst = skb_dst(skb);
		if (!dst->error) {
			preempt_disable();
			dst_cache_set_ip6(&rlwt->cache, dst,
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

static int nla_put_rpl_srh(struct sk_buff *skb, int attrtype,
			   struct rpl_iptunnel_encap *tuninfo)
{
	struct rpl_iptunnel_encap *data;
	struct nlattr *nla;
	int len;

	len = RPL_IPTUNNEL_SRH_SIZE(tuninfo->srh);

	nla = nla_reserve(skb, attrtype, len);
	if (!nla)
		return -EMSGSIZE;

	data = nla_data(nla);
	memcpy(data, tuninfo->srh, len);

	return 0;
}

static int rpl_fill_encap_info(struct sk_buff *skb,
			       struct lwtunnel_state *lwtstate)
{
	struct rpl_iptunnel_encap *tuninfo = rpl_encap_lwtunnel(lwtstate);

	if (nla_put_rpl_srh(skb, RPL_IPTUNNEL_SRH, tuninfo))
		return -EMSGSIZE;

	return 0;
}

static int rpl_encap_nlsize(struct lwtunnel_state *lwtstate)
{
	struct rpl_iptunnel_encap *tuninfo = rpl_encap_lwtunnel(lwtstate);

	return nla_total_size(RPL_IPTUNNEL_SRH_SIZE(tuninfo->srh));
}

static int rpl_encap_cmp(struct lwtunnel_state *a, struct lwtunnel_state *b)
{
	struct rpl_iptunnel_encap *a_hdr = rpl_encap_lwtunnel(a);
	struct rpl_iptunnel_encap *b_hdr = rpl_encap_lwtunnel(b);
	int len = RPL_IPTUNNEL_SRH_SIZE(a_hdr->srh);

	if (len != RPL_IPTUNNEL_SRH_SIZE(b_hdr->srh))
		return 1;

	return memcmp(a_hdr, b_hdr, len);
}

static const struct lwtunnel_encap_ops rpl_ops = {
	.build_state	= rpl_build_state,
	.destroy_state	= rpl_destroy_state,
	.output		= rpl_output,
	.input		= rpl_input,
	.fill_encap	= rpl_fill_encap_info,
	.get_encap_size	= rpl_encap_nlsize,
	.cmp_encap	= rpl_encap_cmp,
	.owner		= THIS_MODULE,
};

int __init rpl_init(void)
{
	int err;

	err = lwtunnel_encap_add_ops(&rpl_ops, LWTUNNEL_ENCAP_RPL);
	if (err)
		goto out;

	pr_info("RPL Segment Routing with IPv6\n");

	return 0;

out:
	return err;
}

void rpl_exit(void)
{
	lwtunnel_encap_del_ops(&rpl_ops, LWTUNNEL_ENCAP_RPL);
}
