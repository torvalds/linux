/*
 * mpls tunnels	An implementation mpls tunnels using the light weight tunnel
 *		infrastructure
 *
 * Authors:	Roopa Prabhu, <roopa@cumulusnetworks.com>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 */
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/net.h>
#include <linux/module.h>
#include <linux/mpls.h>
#include <linux/vmalloc.h>
#include <net/ip.h>
#include <net/dst.h>
#include <net/lwtunnel.h>
#include <net/netevent.h>
#include <net/netns/generic.h>
#include <net/ip6_fib.h>
#include <net/route.h>
#include <net/mpls_iptunnel.h>
#include <linux/mpls_iptunnel.h>
#include "internal.h"

static const struct nla_policy mpls_iptunnel_policy[MPLS_IPTUNNEL_MAX + 1] = {
	[MPLS_IPTUNNEL_DST]	= { .len = sizeof(u32) },
	[MPLS_IPTUNNEL_TTL]	= { .type = NLA_U8 },
};

static unsigned int mpls_encap_size(struct mpls_iptunnel_encap *en)
{
	/* The size of the layer 2.5 labels to be added for this route */
	return en->labels * sizeof(struct mpls_shim_hdr);
}

static int mpls_xmit(struct sk_buff *skb)
{
	struct mpls_iptunnel_encap *tun_encap_info;
	struct mpls_shim_hdr *hdr;
	struct net_device *out_dev;
	unsigned int hh_len;
	unsigned int new_header_size;
	unsigned int mtu;
	struct dst_entry *dst = skb_dst(skb);
	struct rtable *rt = NULL;
	struct rt6_info *rt6 = NULL;
	struct mpls_dev *out_mdev;
	struct net *net;
	int err = 0;
	bool bos;
	int i;
	unsigned int ttl;

	/* Find the output device */
	out_dev = dst->dev;
	net = dev_net(out_dev);

	skb_orphan(skb);

	if (!mpls_output_possible(out_dev) ||
	    !dst->lwtstate || skb_warn_if_lro(skb))
		goto drop;

	skb_forward_csum(skb);

	tun_encap_info = mpls_lwtunnel_encap(dst->lwtstate);

	/* Obtain the ttl using the following set of rules.
	 *
	 * LWT ttl propagation setting:
	 *  - disabled => use default TTL value from LWT
	 *  - enabled  => use TTL value from IPv4/IPv6 header
	 *  - default  =>
	 *   Global ttl propagation setting:
	 *    - disabled => use default TTL value from global setting
	 *    - enabled => use TTL value from IPv4/IPv6 header
	 */
	if (dst->ops->family == AF_INET) {
		if (tun_encap_info->ttl_propagate == MPLS_TTL_PROP_DISABLED)
			ttl = tun_encap_info->default_ttl;
		else if (tun_encap_info->ttl_propagate == MPLS_TTL_PROP_DEFAULT &&
			 !net->mpls.ip_ttl_propagate)
			ttl = net->mpls.default_ttl;
		else
			ttl = ip_hdr(skb)->ttl;
		rt = (struct rtable *)dst;
	} else if (dst->ops->family == AF_INET6) {
		if (tun_encap_info->ttl_propagate == MPLS_TTL_PROP_DISABLED)
			ttl = tun_encap_info->default_ttl;
		else if (tun_encap_info->ttl_propagate == MPLS_TTL_PROP_DEFAULT &&
			 !net->mpls.ip_ttl_propagate)
			ttl = net->mpls.default_ttl;
		else
			ttl = ipv6_hdr(skb)->hop_limit;
		rt6 = (struct rt6_info *)dst;
	} else {
		goto drop;
	}

	/* Verify the destination can hold the packet */
	new_header_size = mpls_encap_size(tun_encap_info);
	mtu = mpls_dev_mtu(out_dev);
	if (mpls_pkt_too_big(skb, mtu - new_header_size))
		goto drop;

	hh_len = LL_RESERVED_SPACE(out_dev);
	if (!out_dev->header_ops)
		hh_len = 0;

	/* Ensure there is enough space for the headers in the skb */
	if (skb_cow(skb, hh_len + new_header_size))
		goto drop;

	skb_set_inner_protocol(skb, skb->protocol);
	skb_reset_inner_network_header(skb);

	skb_push(skb, new_header_size);

	skb_reset_network_header(skb);

	skb->dev = out_dev;
	skb->protocol = htons(ETH_P_MPLS_UC);

	/* Push the new labels */
	hdr = mpls_hdr(skb);
	bos = true;
	for (i = tun_encap_info->labels - 1; i >= 0; i--) {
		hdr[i] = mpls_entry_encode(tun_encap_info->label[i],
					   ttl, 0, bos);
		bos = false;
	}

	mpls_stats_inc_outucastpkts(out_dev, skb);

	if (rt)
		err = neigh_xmit(NEIGH_ARP_TABLE, out_dev, &rt->rt_gateway,
				 skb);
	else if (rt6)
		err = neigh_xmit(NEIGH_ND_TABLE, out_dev, &rt6->rt6i_gateway,
				 skb);
	if (err)
		net_dbg_ratelimited("%s: packet transmission failed: %d\n",
				    __func__, err);

	return LWTUNNEL_XMIT_DONE;

drop:
	out_mdev = out_dev ? mpls_dev_get(out_dev) : NULL;
	if (out_mdev)
		MPLS_INC_STATS(out_mdev, tx_errors);
	kfree_skb(skb);
	return -EINVAL;
}

static int mpls_build_state(struct nlattr *nla,
			    unsigned int family, const void *cfg,
			    struct lwtunnel_state **ts,
			    struct netlink_ext_ack *extack)
{
	struct mpls_iptunnel_encap *tun_encap_info;
	struct nlattr *tb[MPLS_IPTUNNEL_MAX + 1];
	struct lwtunnel_state *newts;
	u8 n_labels;
	int ret;

	ret = nla_parse_nested(tb, MPLS_IPTUNNEL_MAX, nla,
			       mpls_iptunnel_policy, extack);
	if (ret < 0)
		return ret;

	if (!tb[MPLS_IPTUNNEL_DST]) {
		NL_SET_ERR_MSG(extack, "MPLS_IPTUNNEL_DST attribute is missing");
		return -EINVAL;
	}

	/* determine number of labels */
	if (nla_get_labels(tb[MPLS_IPTUNNEL_DST], MAX_NEW_LABELS,
			   &n_labels, NULL, extack))
		return -EINVAL;

	newts = lwtunnel_state_alloc(sizeof(*tun_encap_info) +
				     n_labels * sizeof(u32));
	if (!newts)
		return -ENOMEM;

	tun_encap_info = mpls_lwtunnel_encap(newts);
	ret = nla_get_labels(tb[MPLS_IPTUNNEL_DST], n_labels,
			     &tun_encap_info->labels, tun_encap_info->label,
			     extack);
	if (ret)
		goto errout;

	tun_encap_info->ttl_propagate = MPLS_TTL_PROP_DEFAULT;

	if (tb[MPLS_IPTUNNEL_TTL]) {
		tun_encap_info->default_ttl = nla_get_u8(tb[MPLS_IPTUNNEL_TTL]);
		/* TTL 0 implies propagate from IP header */
		tun_encap_info->ttl_propagate = tun_encap_info->default_ttl ?
			MPLS_TTL_PROP_DISABLED :
			MPLS_TTL_PROP_ENABLED;
	}

	newts->type = LWTUNNEL_ENCAP_MPLS;
	newts->flags |= LWTUNNEL_STATE_XMIT_REDIRECT;
	newts->headroom = mpls_encap_size(tun_encap_info);

	*ts = newts;

	return 0;

errout:
	kfree(newts);
	*ts = NULL;

	return ret;
}

static int mpls_fill_encap_info(struct sk_buff *skb,
				struct lwtunnel_state *lwtstate)
{
	struct mpls_iptunnel_encap *tun_encap_info;

	tun_encap_info = mpls_lwtunnel_encap(lwtstate);

	if (nla_put_labels(skb, MPLS_IPTUNNEL_DST, tun_encap_info->labels,
			   tun_encap_info->label))
		goto nla_put_failure;

	if (tun_encap_info->ttl_propagate != MPLS_TTL_PROP_DEFAULT &&
	    nla_put_u8(skb, MPLS_IPTUNNEL_TTL, tun_encap_info->default_ttl))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static int mpls_encap_nlsize(struct lwtunnel_state *lwtstate)
{
	struct mpls_iptunnel_encap *tun_encap_info;
	int nlsize;

	tun_encap_info = mpls_lwtunnel_encap(lwtstate);

	nlsize = nla_total_size(tun_encap_info->labels * 4);

	if (tun_encap_info->ttl_propagate != MPLS_TTL_PROP_DEFAULT)
		nlsize += nla_total_size(1);

	return nlsize;
}

static int mpls_encap_cmp(struct lwtunnel_state *a, struct lwtunnel_state *b)
{
	struct mpls_iptunnel_encap *a_hdr = mpls_lwtunnel_encap(a);
	struct mpls_iptunnel_encap *b_hdr = mpls_lwtunnel_encap(b);
	int l;

	if (a_hdr->labels != b_hdr->labels ||
	    a_hdr->ttl_propagate != b_hdr->ttl_propagate ||
	    a_hdr->default_ttl != b_hdr->default_ttl)
		return 1;

	for (l = 0; l < a_hdr->labels; l++)
		if (a_hdr->label[l] != b_hdr->label[l])
			return 1;
	return 0;
}

static const struct lwtunnel_encap_ops mpls_iptun_ops = {
	.build_state = mpls_build_state,
	.xmit = mpls_xmit,
	.fill_encap = mpls_fill_encap_info,
	.get_encap_size = mpls_encap_nlsize,
	.cmp_encap = mpls_encap_cmp,
	.owner = THIS_MODULE,
};

static int __init mpls_iptunnel_init(void)
{
	return lwtunnel_encap_add_ops(&mpls_iptun_ops, LWTUNNEL_ENCAP_MPLS);
}
module_init(mpls_iptunnel_init);

static void __exit mpls_iptunnel_exit(void)
{
	lwtunnel_encap_del_ops(&mpls_iptun_ops, LWTUNNEL_ENCAP_MPLS);
}
module_exit(mpls_iptunnel_exit);

MODULE_ALIAS_RTNL_LWT(MPLS);
MODULE_DESCRIPTION("MultiProtocol Label Switching IP Tunnels");
MODULE_LICENSE("GPL v2");
