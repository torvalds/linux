// SPDX-License-Identifier: GPL-2.0-only
/*
 * Network Service Header
 *
 * Copyright (c) 2017 Red Hat, Inc. -- Jiri Benc <jbenc@redhat.com>
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/gso.h>
#include <net/nsh.h>
#include <net/tun_proto.h>

int nsh_push(struct sk_buff *skb, const struct nshhdr *pushed_nh)
{
	struct nshhdr *nh;
	size_t length = nsh_hdr_len(pushed_nh);
	u8 next_proto;

	if (skb->mac_len) {
		next_proto = TUN_P_ETHERNET;
	} else {
		next_proto = tun_p_from_eth_p(skb->protocol);
		if (!next_proto)
			return -EAFNOSUPPORT;
	}

	/* Add the NSH header */
	if (skb_cow_head(skb, length) < 0)
		return -ENOMEM;

	skb_push(skb, length);
	nh = (struct nshhdr *)(skb->data);
	memcpy(nh, pushed_nh, length);
	nh->np = next_proto;
	skb_postpush_rcsum(skb, nh, length);

	skb->protocol = htons(ETH_P_NSH);
	skb_reset_mac_header(skb);
	skb_reset_network_header(skb);
	skb_reset_mac_len(skb);

	return 0;
}
EXPORT_SYMBOL_GPL(nsh_push);

int nsh_pop(struct sk_buff *skb)
{
	struct nshhdr *nh;
	size_t length;
	__be16 inner_proto;

	if (!pskb_may_pull(skb, NSH_BASE_HDR_LEN))
		return -ENOMEM;
	nh = (struct nshhdr *)(skb->data);
	length = nsh_hdr_len(nh);
	if (length < NSH_BASE_HDR_LEN)
		return -EINVAL;
	inner_proto = tun_p_to_eth_p(nh->np);
	if (!pskb_may_pull(skb, length))
		return -ENOMEM;

	if (!inner_proto)
		return -EAFNOSUPPORT;

	skb_pull_rcsum(skb, length);
	skb_reset_mac_header(skb);
	skb_reset_network_header(skb);
	skb_reset_mac_len(skb);
	skb->protocol = inner_proto;

	return 0;
}
EXPORT_SYMBOL_GPL(nsh_pop);

static struct sk_buff *nsh_gso_segment(struct sk_buff *skb,
				       netdev_features_t features)
{
	struct sk_buff *segs = ERR_PTR(-EINVAL);
	u16 mac_offset = skb->mac_header;
	unsigned int nsh_len, mac_len;
	__be16 proto;

	skb_reset_network_header(skb);

	mac_len = skb->mac_len;

	if (unlikely(!pskb_may_pull(skb, NSH_BASE_HDR_LEN)))
		goto out;
	nsh_len = nsh_hdr_len(nsh_hdr(skb));
	if (nsh_len < NSH_BASE_HDR_LEN)
		goto out;
	if (unlikely(!pskb_may_pull(skb, nsh_len)))
		goto out;

	proto = tun_p_to_eth_p(nsh_hdr(skb)->np);
	if (!proto)
		goto out;

	__skb_pull(skb, nsh_len);

	skb_reset_mac_header(skb);
	skb->mac_len = proto == htons(ETH_P_TEB) ? ETH_HLEN : 0;
	skb->protocol = proto;

	features &= NETIF_F_SG;
	segs = skb_mac_gso_segment(skb, features);
	if (IS_ERR_OR_NULL(segs)) {
		skb_gso_error_unwind(skb, htons(ETH_P_NSH), nsh_len,
				     mac_offset, mac_len);
		goto out;
	}

	for (skb = segs; skb; skb = skb->next) {
		skb->protocol = htons(ETH_P_NSH);
		__skb_push(skb, nsh_len);
		skb->mac_header = mac_offset;
		skb->network_header = skb->mac_header + mac_len;
		skb->mac_len = mac_len;
	}

out:
	return segs;
}

static struct packet_offload nsh_packet_offload __read_mostly = {
	.type = htons(ETH_P_NSH),
	.priority = 15,
	.callbacks = {
		.gso_segment = nsh_gso_segment,
	},
};

static int __init nsh_init_module(void)
{
	dev_add_offload(&nsh_packet_offload);
	return 0;
}

static void __exit nsh_cleanup_module(void)
{
	dev_remove_offload(&nsh_packet_offload);
}

module_init(nsh_init_module);
module_exit(nsh_cleanup_module);

MODULE_AUTHOR("Jiri Benc <jbenc@redhat.com>");
MODULE_DESCRIPTION("NSH protocol");
MODULE_LICENSE("GPL v2");
