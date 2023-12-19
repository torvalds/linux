/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/types.h>
#include <linux/ip.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter_bridge.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/icmp.h>
#include <linux/sysctl.h>
#include <net/route.h>
#include <net/ip.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_bridge.h>

#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>

#include "../br_private.h"

/* Best effort variant of ip_do_fragment which preserves geometry, unless skbuff
 * has been linearized or cloned.
 */
static int nf_br_ip_fragment(struct net *net, struct sock *sk,
			     struct sk_buff *skb,
			     struct nf_bridge_frag_data *data,
			     int (*output)(struct net *, struct sock *sk,
					   const struct nf_bridge_frag_data *data,
					   struct sk_buff *))
{
	int frag_max_size = BR_INPUT_SKB_CB(skb)->frag_max_size;
	bool mono_delivery_time = skb->mono_delivery_time;
	unsigned int hlen, ll_rs, mtu;
	ktime_t tstamp = skb->tstamp;
	struct ip_frag_state state;
	struct iphdr *iph;
	int err = 0;

	/* for offloaded checksums cleanup checksum before fragmentation */
	if (skb->ip_summed == CHECKSUM_PARTIAL &&
	    (err = skb_checksum_help(skb)))
		goto blackhole;

	iph = ip_hdr(skb);

	/*
	 *	Setup starting values
	 */

	hlen = iph->ihl * 4;
	frag_max_size -= hlen;
	ll_rs = LL_RESERVED_SPACE(skb->dev);
	mtu = skb->dev->mtu;

	if (skb_has_frag_list(skb)) {
		unsigned int first_len = skb_pagelen(skb);
		struct ip_fraglist_iter iter;
		struct sk_buff *frag;

		if (first_len - hlen > mtu ||
		    skb_headroom(skb) < ll_rs)
			goto blackhole;

		if (skb_cloned(skb))
			goto slow_path;

		skb_walk_frags(skb, frag) {
			if (frag->len > mtu ||
			    skb_headroom(frag) < hlen + ll_rs)
				goto blackhole;

			if (skb_shared(frag))
				goto slow_path;
		}

		ip_fraglist_init(skb, iph, hlen, &iter);

		for (;;) {
			if (iter.frag)
				ip_fraglist_prepare(skb, &iter);

			skb_set_delivery_time(skb, tstamp, mono_delivery_time);
			err = output(net, sk, data, skb);
			if (err || !iter.frag)
				break;

			skb = ip_fraglist_next(&iter);
		}

		if (!err)
			return 0;

		kfree_skb_list(iter.frag);

		return err;
	}
slow_path:
	/* This is a linearized skbuff, the original geometry is lost for us.
	 * This may also be a clone skbuff, we could preserve the geometry for
	 * the copies but probably not worth the effort.
	 */
	ip_frag_init(skb, hlen, ll_rs, frag_max_size, false, &state);

	while (state.left > 0) {
		struct sk_buff *skb2;

		skb2 = ip_frag_next(skb, &state);
		if (IS_ERR(skb2)) {
			err = PTR_ERR(skb2);
			goto blackhole;
		}

		skb_set_delivery_time(skb2, tstamp, mono_delivery_time);
		err = output(net, sk, data, skb2);
		if (err)
			goto blackhole;
	}
	consume_skb(skb);
	return err;

blackhole:
	kfree_skb(skb);
	return 0;
}

/* ip_defrag() expects IPCB() in place. */
static void br_skb_cb_save(struct sk_buff *skb, struct br_input_skb_cb *cb,
			   size_t inet_skb_parm_size)
{
	memcpy(cb, skb->cb, sizeof(*cb));
	memset(skb->cb, 0, inet_skb_parm_size);
}

static void br_skb_cb_restore(struct sk_buff *skb,
			      const struct br_input_skb_cb *cb,
			      u16 fragsz)
{
	memcpy(skb->cb, cb, sizeof(*cb));
	BR_INPUT_SKB_CB(skb)->frag_max_size = fragsz;
}

static unsigned int nf_ct_br_defrag4(struct sk_buff *skb,
				     const struct nf_hook_state *state)
{
	u16 zone_id = NF_CT_DEFAULT_ZONE_ID;
	enum ip_conntrack_info ctinfo;
	struct br_input_skb_cb cb;
	const struct nf_conn *ct;
	int err;

	if (!ip_is_fragment(ip_hdr(skb)))
		return NF_ACCEPT;

	ct = nf_ct_get(skb, &ctinfo);
	if (ct)
		zone_id = nf_ct_zone_id(nf_ct_zone(ct), CTINFO2DIR(ctinfo));

	br_skb_cb_save(skb, &cb, sizeof(struct inet_skb_parm));
	local_bh_disable();
	err = ip_defrag(state->net, skb,
			IP_DEFRAG_CONNTRACK_BRIDGE_IN + zone_id);
	local_bh_enable();
	if (!err) {
		br_skb_cb_restore(skb, &cb, IPCB(skb)->frag_max_size);
		skb->ignore_df = 1;
		return NF_ACCEPT;
	}

	return NF_STOLEN;
}

static unsigned int nf_ct_br_defrag6(struct sk_buff *skb,
				     const struct nf_hook_state *state)
{
#if IS_ENABLED(CONFIG_NF_DEFRAG_IPV6)
	u16 zone_id = NF_CT_DEFAULT_ZONE_ID;
	enum ip_conntrack_info ctinfo;
	struct br_input_skb_cb cb;
	const struct nf_conn *ct;
	int err;

	ct = nf_ct_get(skb, &ctinfo);
	if (ct)
		zone_id = nf_ct_zone_id(nf_ct_zone(ct), CTINFO2DIR(ctinfo));

	br_skb_cb_save(skb, &cb, sizeof(struct inet6_skb_parm));

	err = nf_ct_frag6_gather(state->net, skb,
				 IP_DEFRAG_CONNTRACK_BRIDGE_IN + zone_id);
	/* queued */
	if (err == -EINPROGRESS)
		return NF_STOLEN;

	br_skb_cb_restore(skb, &cb, IP6CB(skb)->frag_max_size);
	return err == 0 ? NF_ACCEPT : NF_DROP;
#else
	return NF_ACCEPT;
#endif
}

static int nf_ct_br_ip_check(const struct sk_buff *skb)
{
	const struct iphdr *iph;
	int nhoff, len;

	nhoff = skb_network_offset(skb);
	iph = ip_hdr(skb);
	if (iph->ihl < 5 ||
	    iph->version != 4)
		return -1;

	len = skb_ip_totlen(skb);
	if (skb->len < nhoff + len ||
	    len < (iph->ihl * 4))
                return -1;

	return 0;
}

static int nf_ct_br_ipv6_check(const struct sk_buff *skb)
{
	const struct ipv6hdr *hdr;
	int nhoff, len;

	nhoff = skb_network_offset(skb);
	hdr = ipv6_hdr(skb);
	if (hdr->version != 6)
		return -1;

	len = ntohs(hdr->payload_len) + sizeof(struct ipv6hdr) + nhoff;
	if (skb->len < len)
		return -1;

	return 0;
}

static unsigned int nf_ct_bridge_pre(void *priv, struct sk_buff *skb,
				     const struct nf_hook_state *state)
{
	struct nf_hook_state bridge_state = *state;
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct;
	u32 len;
	int ret;

	ct = nf_ct_get(skb, &ctinfo);
	if ((ct && !nf_ct_is_template(ct)) ||
	    ctinfo == IP_CT_UNTRACKED)
		return NF_ACCEPT;

	switch (skb->protocol) {
	case htons(ETH_P_IP):
		if (!pskb_may_pull(skb, sizeof(struct iphdr)))
			return NF_ACCEPT;

		len = skb_ip_totlen(skb);
		if (pskb_trim_rcsum(skb, len))
			return NF_ACCEPT;

		if (nf_ct_br_ip_check(skb))
			return NF_ACCEPT;

		bridge_state.pf = NFPROTO_IPV4;
		ret = nf_ct_br_defrag4(skb, &bridge_state);
		break;
	case htons(ETH_P_IPV6):
		if (!pskb_may_pull(skb, sizeof(struct ipv6hdr)))
			return NF_ACCEPT;

		len = sizeof(struct ipv6hdr) + ntohs(ipv6_hdr(skb)->payload_len);
		if (pskb_trim_rcsum(skb, len))
			return NF_ACCEPT;

		if (nf_ct_br_ipv6_check(skb))
			return NF_ACCEPT;

		bridge_state.pf = NFPROTO_IPV6;
		ret = nf_ct_br_defrag6(skb, &bridge_state);
		break;
	default:
		nf_ct_set(skb, NULL, IP_CT_UNTRACKED);
		return NF_ACCEPT;
	}

	if (ret != NF_ACCEPT)
		return ret;

	return nf_conntrack_in(skb, &bridge_state);
}

static void nf_ct_bridge_frag_save(struct sk_buff *skb,
				   struct nf_bridge_frag_data *data)
{
	if (skb_vlan_tag_present(skb)) {
		data->vlan_present = true;
		data->vlan_tci = skb->vlan_tci;
		data->vlan_proto = skb->vlan_proto;
	} else {
		data->vlan_present = false;
	}
	skb_copy_from_linear_data_offset(skb, -ETH_HLEN, data->mac, ETH_HLEN);
}

static unsigned int
nf_ct_bridge_refrag(struct sk_buff *skb, const struct nf_hook_state *state,
		    int (*output)(struct net *, struct sock *sk,
				  const struct nf_bridge_frag_data *data,
				  struct sk_buff *))
{
	struct nf_bridge_frag_data data;

	if (!BR_INPUT_SKB_CB(skb)->frag_max_size)
		return NF_ACCEPT;

	nf_ct_bridge_frag_save(skb, &data);
	switch (skb->protocol) {
	case htons(ETH_P_IP):
		nf_br_ip_fragment(state->net, state->sk, skb, &data, output);
		break;
	case htons(ETH_P_IPV6):
		nf_br_ip6_fragment(state->net, state->sk, skb, &data, output);
		break;
	default:
		WARN_ON_ONCE(1);
		return NF_DROP;
	}

	return NF_STOLEN;
}

/* Actually only slow path refragmentation needs this. */
static int nf_ct_bridge_frag_restore(struct sk_buff *skb,
				     const struct nf_bridge_frag_data *data)
{
	int err;

	err = skb_cow_head(skb, ETH_HLEN);
	if (err) {
		kfree_skb(skb);
		return -ENOMEM;
	}
	if (data->vlan_present)
		__vlan_hwaccel_put_tag(skb, data->vlan_proto, data->vlan_tci);
	else if (skb_vlan_tag_present(skb))
		__vlan_hwaccel_clear_tag(skb);

	skb_copy_to_linear_data_offset(skb, -ETH_HLEN, data->mac, ETH_HLEN);
	skb_reset_mac_header(skb);

	return 0;
}

static int nf_ct_bridge_refrag_post(struct net *net, struct sock *sk,
				    const struct nf_bridge_frag_data *data,
				    struct sk_buff *skb)
{
	int err;

	err = nf_ct_bridge_frag_restore(skb, data);
	if (err < 0)
		return err;

	return br_dev_queue_push_xmit(net, sk, skb);
}

static unsigned int nf_ct_bridge_post(void *priv, struct sk_buff *skb,
				      const struct nf_hook_state *state)
{
	int ret;

	ret = nf_confirm(priv, skb, state);
	if (ret != NF_ACCEPT)
		return ret;

	return nf_ct_bridge_refrag(skb, state, nf_ct_bridge_refrag_post);
}

static struct nf_hook_ops nf_ct_bridge_hook_ops[] __read_mostly = {
	{
		.hook		= nf_ct_bridge_pre,
		.pf		= NFPROTO_BRIDGE,
		.hooknum	= NF_BR_PRE_ROUTING,
		.priority	= NF_IP_PRI_CONNTRACK,
	},
	{
		.hook		= nf_ct_bridge_post,
		.pf		= NFPROTO_BRIDGE,
		.hooknum	= NF_BR_POST_ROUTING,
		.priority	= NF_IP_PRI_CONNTRACK_CONFIRM,
	},
};

static struct nf_ct_bridge_info bridge_info = {
	.ops		= nf_ct_bridge_hook_ops,
	.ops_size	= ARRAY_SIZE(nf_ct_bridge_hook_ops),
	.me		= THIS_MODULE,
};

static int __init nf_conntrack_l3proto_bridge_init(void)
{
	nf_ct_bridge_register(&bridge_info);

	return 0;
}

static void __exit nf_conntrack_l3proto_bridge_fini(void)
{
	nf_ct_bridge_unregister(&bridge_info);
}

module_init(nf_conntrack_l3proto_bridge_init);
module_exit(nf_conntrack_l3proto_bridge_fini);

MODULE_ALIAS("nf_conntrack-" __stringify(AF_BRIDGE));
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Bridge IPv4 and IPv6 connection tracking");
