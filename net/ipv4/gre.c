/*
 *	GRE over IPv4 demultiplexer driver
 *
 *	Authors: Dmitry Kozlov (xeb@mail.ru)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/netdevice.h>
#include <linux/if_tunnel.h>
#include <linux/spinlock.h>
#include <net/protocol.h>
#include <net/gre.h>


static const struct gre_protocol __rcu *gre_proto[GREPROTO_MAX] __read_mostly;
static DEFINE_SPINLOCK(gre_proto_lock);
struct gre_base_hdr {
	__be16 flags;
	__be16 protocol;
};
#define GRE_HEADER_SECTION 4

int gre_add_protocol(const struct gre_protocol *proto, u8 version)
{
	if (version >= GREPROTO_MAX)
		goto err_out;

	spin_lock(&gre_proto_lock);
	if (gre_proto[version])
		goto err_out_unlock;

	RCU_INIT_POINTER(gre_proto[version], proto);
	spin_unlock(&gre_proto_lock);
	return 0;

err_out_unlock:
	spin_unlock(&gre_proto_lock);
err_out:
	return -1;
}
EXPORT_SYMBOL_GPL(gre_add_protocol);

int gre_del_protocol(const struct gre_protocol *proto, u8 version)
{
	if (version >= GREPROTO_MAX)
		goto err_out;

	spin_lock(&gre_proto_lock);
	if (rcu_dereference_protected(gre_proto[version],
			lockdep_is_held(&gre_proto_lock)) != proto)
		goto err_out_unlock;
	RCU_INIT_POINTER(gre_proto[version], NULL);
	spin_unlock(&gre_proto_lock);
	synchronize_rcu();
	return 0;

err_out_unlock:
	spin_unlock(&gre_proto_lock);
err_out:
	return -1;
}
EXPORT_SYMBOL_GPL(gre_del_protocol);

static int gre_rcv(struct sk_buff *skb)
{
	const struct gre_protocol *proto;
	u8 ver;
	int ret;

	if (!pskb_may_pull(skb, 12))
		goto drop;

	ver = skb->data[1]&0x7f;
	if (ver >= GREPROTO_MAX)
		goto drop;

	rcu_read_lock();
	proto = rcu_dereference(gre_proto[ver]);
	if (!proto || !proto->handler)
		goto drop_unlock;
	ret = proto->handler(skb);
	rcu_read_unlock();
	return ret;

drop_unlock:
	rcu_read_unlock();
drop:
	kfree_skb(skb);
	return NET_RX_DROP;
}

static void gre_err(struct sk_buff *skb, u32 info)
{
	const struct gre_protocol *proto;
	const struct iphdr *iph = (const struct iphdr *)skb->data;
	u8 ver = skb->data[(iph->ihl<<2) + 1]&0x7f;

	if (ver >= GREPROTO_MAX)
		return;

	rcu_read_lock();
	proto = rcu_dereference(gre_proto[ver]);
	if (proto && proto->err_handler)
		proto->err_handler(skb, info);
	rcu_read_unlock();
}

static struct sk_buff *gre_gso_segment(struct sk_buff *skb,
				       netdev_features_t features)
{
	struct sk_buff *segs = ERR_PTR(-EINVAL);
	netdev_features_t enc_features;
	int ghl = GRE_HEADER_SECTION;
	struct gre_base_hdr *greh;
	int mac_len = skb->mac_len;
	int tnl_hlen;
	bool csum;

	if (unlikely(skb_shinfo(skb)->gso_type &
				~(SKB_GSO_TCPV4 |
				  SKB_GSO_TCPV6 |
				  SKB_GSO_UDP |
				  SKB_GSO_DODGY |
				  SKB_GSO_TCP_ECN |
				  SKB_GSO_GRE)))
		goto out;

	if (unlikely(!pskb_may_pull(skb, sizeof(*greh))))
		goto out;

	greh = (struct gre_base_hdr *)skb_transport_header(skb);

	if (greh->flags & GRE_KEY)
		ghl += GRE_HEADER_SECTION;
	if (greh->flags & GRE_SEQ)
		ghl += GRE_HEADER_SECTION;
	if (greh->flags & GRE_CSUM) {
		ghl += GRE_HEADER_SECTION;
		csum = true;
	} else
		csum = false;

	/* setup inner skb. */
	if (greh->protocol == htons(ETH_P_TEB)) {
		struct ethhdr *eth = eth_hdr(skb);
		skb->protocol = eth->h_proto;
	} else {
		skb->protocol = greh->protocol;
	}

	skb->encapsulation = 0;

	if (unlikely(!pskb_may_pull(skb, ghl)))
		goto out;
	__skb_pull(skb, ghl);
	skb_reset_mac_header(skb);
	skb_set_network_header(skb, skb_inner_network_offset(skb));
	skb->mac_len = skb_inner_network_offset(skb);

	/* segment inner packet. */
	enc_features = skb->dev->hw_enc_features & netif_skb_features(skb);
	segs = skb_mac_gso_segment(skb, enc_features);
	if (!segs || IS_ERR(segs))
		goto out;

	skb = segs;
	tnl_hlen = skb_tnl_header_len(skb);
	do {
		__skb_push(skb, ghl);
		if (csum) {
			__be32 *pcsum;

			if (skb_has_shared_frag(skb)) {
				int err;

				err = __skb_linearize(skb);
				if (err) {
					kfree_skb(segs);
					segs = ERR_PTR(err);
					goto out;
				}
			}

			greh = (struct gre_base_hdr *)(skb->data);
			pcsum = (__be32 *)(greh + 1);
			*pcsum = 0;
			*(__sum16 *)pcsum = csum_fold(skb_checksum(skb, 0, skb->len, 0));
		}
		__skb_push(skb, tnl_hlen - ghl);

		skb_reset_mac_header(skb);
		skb_set_network_header(skb, mac_len);
		skb->mac_len = mac_len;
	} while ((skb = skb->next));
out:
	return segs;
}

static int gre_gso_send_check(struct sk_buff *skb)
{
	if (!skb->encapsulation)
		return -EINVAL;
	return 0;
}

static const struct net_protocol net_gre_protocol = {
	.handler     = gre_rcv,
	.err_handler = gre_err,
	.netns_ok    = 1,
};

static const struct net_offload gre_offload = {
	.callbacks = {
		.gso_send_check =	gre_gso_send_check,
		.gso_segment    =	gre_gso_segment,
	},
};

static int __init gre_init(void)
{
	pr_info("GRE over IPv4 demultiplexor driver\n");

	if (inet_add_protocol(&net_gre_protocol, IPPROTO_GRE) < 0) {
		pr_err("can't add protocol\n");
		return -EAGAIN;
	}

	if (inet_add_offload(&gre_offload, IPPROTO_GRE)) {
		pr_err("can't add protocol offload\n");
		inet_del_protocol(&net_gre_protocol, IPPROTO_GRE);
		return -EAGAIN;
	}

	return 0;
}

static void __exit gre_exit(void)
{
	inet_del_offload(&gre_offload, IPPROTO_GRE);
	inet_del_protocol(&net_gre_protocol, IPPROTO_GRE);
}

module_init(gre_init);
module_exit(gre_exit);

MODULE_DESCRIPTION("GRE over IPv4 demultiplexer driver");
MODULE_AUTHOR("D. Kozlov (xeb@mail.ru)");
MODULE_LICENSE("GPL");

