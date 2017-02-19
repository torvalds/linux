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
	[MPLS_IPTUNNEL_DST]	= { .type = NLA_U32 },
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
	int err = 0;
	bool bos;
	int i;
	unsigned int ttl;

	/* Obtain the ttl */
	if (dst->ops->family == AF_INET) {
		ttl = ip_hdr(skb)->ttl;
		rt = (struct rtable *)dst;
	} else if (dst->ops->family == AF_INET6) {
		ttl = ipv6_hdr(skb)->hop_limit;
		rt6 = (struct rt6_info *)dst;
	} else {
		goto drop;
	}

	skb_orphan(skb);

	/* Find the output device */
	out_dev = dst->dev;
	if (!mpls_output_possible(out_dev) ||
	    !dst->lwtstate || skb_warn_if_lro(skb))
		goto drop;

	skb_forward_csum(skb);

	tun_encap_info = mpls_lwtunnel_encap(dst->lwtstate);

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
	kfree_skb(skb);
	return -EINVAL;
}

static int mpls_build_state(struct net_device *dev, struct nlattr *nla,
			    unsigned int family, const void *cfg,
			    struct lwtunnel_state **ts)
{
	struct mpls_iptunnel_encap *tun_encap_info;
	struct nlattr *tb[MPLS_IPTUNNEL_MAX + 1];
	struct lwtunnel_state *newts;
	int ret;

	ret = nla_parse_nested(tb, MPLS_IPTUNNEL_MAX, nla,
			       mpls_iptunnel_policy);
	if (ret < 0)
		return ret;

	if (!tb[MPLS_IPTUNNEL_DST])
		return -EINVAL;


	newts = lwtunnel_state_alloc(sizeof(*tun_encap_info));
	if (!newts)
		return -ENOMEM;

	tun_encap_info = mpls_lwtunnel_encap(newts);
	ret = nla_get_labels(tb[MPLS_IPTUNNEL_DST], MAX_NEW_LABELS,
			     &tun_encap_info->labels, tun_encap_info->label);
	if (ret)
		goto errout;
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

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static int mpls_encap_nlsize(struct lwtunnel_state *lwtstate)
{
	struct mpls_iptunnel_encap *tun_encap_info;

	tun_encap_info = mpls_lwtunnel_encap(lwtstate);

	return nla_total_size(tun_encap_info->labels * 4);
}

static int mpls_encap_cmp(struct lwtunnel_state *a, struct lwtunnel_state *b)
{
	struct mpls_iptunnel_encap *a_hdr = mpls_lwtunnel_encap(a);
	struct mpls_iptunnel_encap *b_hdr = mpls_lwtunnel_encap(b);
	int l;

	if (a_hdr->labels != b_hdr->labels)
		return 1;

	for (l = 0; l < MAX_NEW_LABELS; l++)
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
