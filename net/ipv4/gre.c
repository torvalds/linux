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
#include <linux/if.h>
#include <linux/icmp.h>
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

#include <net/icmp.h>
#include <net/route.h>
#include <net/xfrm.h>

static const struct gre_protocol __rcu *gre_proto[GREPROTO_MAX] __read_mostly;
static struct gre_cisco_protocol __rcu *gre_cisco_proto_list[GRE_IP_PROTO_MAX];

int gre_add_protocol(const struct gre_protocol *proto, u8 version)
{
	if (version >= GREPROTO_MAX)
		return -EINVAL;

	return (cmpxchg((const struct gre_protocol **)&gre_proto[version], NULL, proto) == NULL) ?
		0 : -EBUSY;
}
EXPORT_SYMBOL_GPL(gre_add_protocol);

int gre_del_protocol(const struct gre_protocol *proto, u8 version)
{
	int ret;

	if (version >= GREPROTO_MAX)
		return -EINVAL;

	ret = (cmpxchg((const struct gre_protocol **)&gre_proto[version], proto, NULL) == proto) ?
		0 : -EBUSY;

	if (ret)
		return ret;

	synchronize_rcu();
	return 0;
}
EXPORT_SYMBOL_GPL(gre_del_protocol);

void gre_build_header(struct sk_buff *skb, const struct tnl_ptk_info *tpi,
		      int hdr_len)
{
	struct gre_base_hdr *greh;

	skb_push(skb, hdr_len);

	greh = (struct gre_base_hdr *)skb->data;
	greh->flags = tnl_flags_to_gre_flags(tpi->flags);
	greh->protocol = tpi->proto;

	if (tpi->flags&(TUNNEL_KEY|TUNNEL_CSUM|TUNNEL_SEQ)) {
		__be32 *ptr = (__be32 *)(((u8 *)greh) + hdr_len - 4);

		if (tpi->flags&TUNNEL_SEQ) {
			*ptr = tpi->seq;
			ptr--;
		}
		if (tpi->flags&TUNNEL_KEY) {
			*ptr = tpi->key;
			ptr--;
		}
		if (tpi->flags&TUNNEL_CSUM &&
		    !(skb_shinfo(skb)->gso_type & SKB_GSO_GRE)) {
			*ptr = 0;
			*(__sum16 *)ptr = csum_fold(skb_checksum(skb, 0,
								 skb->len, 0));
		}
	}
}
EXPORT_SYMBOL_GPL(gre_build_header);

struct sk_buff *gre_handle_offloads(struct sk_buff *skb, bool gre_csum)
{
	int err;

	if (likely(!skb->encapsulation)) {
		skb_reset_inner_headers(skb);
		skb->encapsulation = 1;
	}

	if (skb_is_gso(skb)) {
		err = skb_unclone(skb, GFP_ATOMIC);
		if (unlikely(err))
			goto error;
		skb_shinfo(skb)->gso_type |= SKB_GSO_GRE;
		return skb;
	} else if (skb->ip_summed == CHECKSUM_PARTIAL && gre_csum) {
		err = skb_checksum_help(skb);
		if (unlikely(err))
			goto error;
	} else if (skb->ip_summed != CHECKSUM_PARTIAL)
		skb->ip_summed = CHECKSUM_NONE;

	return skb;
error:
	kfree_skb(skb);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(gre_handle_offloads);

static __sum16 check_checksum(struct sk_buff *skb)
{
	__sum16 csum = 0;

	switch (skb->ip_summed) {
	case CHECKSUM_COMPLETE:
		csum = csum_fold(skb->csum);

		if (!csum)
			break;
		/* Fall through. */

	case CHECKSUM_NONE:
		skb->csum = 0;
		csum = __skb_checksum_complete(skb);
		skb->ip_summed = CHECKSUM_COMPLETE;
		break;
	}

	return csum;
}

static int parse_gre_header(struct sk_buff *skb, struct tnl_ptk_info *tpi,
			    bool *csum_err)
{
	unsigned int ip_hlen = ip_hdrlen(skb);
	const struct gre_base_hdr *greh;
	__be32 *options;
	int hdr_len;

	if (unlikely(!pskb_may_pull(skb, sizeof(struct gre_base_hdr))))
		return -EINVAL;

	greh = (struct gre_base_hdr *)(skb_network_header(skb) + ip_hlen);
	if (unlikely(greh->flags & (GRE_VERSION | GRE_ROUTING)))
		return -EINVAL;

	tpi->flags = gre_flags_to_tnl_flags(greh->flags);
	hdr_len = ip_gre_calc_hlen(tpi->flags);

	if (!pskb_may_pull(skb, hdr_len))
		return -EINVAL;

	greh = (struct gre_base_hdr *)(skb_network_header(skb) + ip_hlen);
	tpi->proto = greh->protocol;

	options = (__be32 *)(greh + 1);
	if (greh->flags & GRE_CSUM) {
		if (check_checksum(skb)) {
			*csum_err = true;
			return -EINVAL;
		}
		options++;
	}

	if (greh->flags & GRE_KEY) {
		tpi->key = *options;
		options++;
	} else
		tpi->key = 0;

	if (unlikely(greh->flags & GRE_SEQ)) {
		tpi->seq = *options;
		options++;
	} else
		tpi->seq = 0;

	/* WCCP version 1 and 2 protocol decoding.
	 * - Change protocol to IP
	 * - When dealing with WCCPv2, Skip extra 4 bytes in GRE header
	 */
	if (greh->flags == 0 && tpi->proto == htons(ETH_P_WCCP)) {
		tpi->proto = htons(ETH_P_IP);
		if ((*(u8 *)options & 0xF0) != 0x40) {
			hdr_len += 4;
			if (!pskb_may_pull(skb, hdr_len))
				return -EINVAL;
		}
	}

	return iptunnel_pull_header(skb, hdr_len, tpi->proto);
}

static int gre_cisco_rcv(struct sk_buff *skb)
{
	struct tnl_ptk_info tpi;
	int i;
	bool csum_err = false;

	if (parse_gre_header(skb, &tpi, &csum_err) < 0)
		goto drop;

	rcu_read_lock();
	for (i = 0; i < GRE_IP_PROTO_MAX; i++) {
		struct gre_cisco_protocol *proto;
		int ret;

		proto = rcu_dereference(gre_cisco_proto_list[i]);
		if (!proto)
			continue;
		ret = proto->handler(skb, &tpi);
		if (ret == PACKET_RCVD) {
			rcu_read_unlock();
			return 0;
		}
	}
	rcu_read_unlock();

	icmp_send(skb, ICMP_DEST_UNREACH, ICMP_PORT_UNREACH, 0);
drop:
	kfree_skb(skb);
	return 0;
}

static void gre_cisco_err(struct sk_buff *skb, u32 info)
{
	/* All the routers (except for Linux) return only
	 * 8 bytes of packet payload. It means, that precise relaying of
	 * ICMP in the real Internet is absolutely infeasible.
	 *
	 * Moreover, Cisco "wise men" put GRE key to the third word
	 * in GRE header. It makes impossible maintaining even soft
	 * state for keyed
	 * GRE tunnels with enabled checksum. Tell them "thank you".
	 *
	 * Well, I wonder, rfc1812 was written by Cisco employee,
	 * what the hell these idiots break standards established
	 * by themselves???
	 */

	const int type = icmp_hdr(skb)->type;
	const int code = icmp_hdr(skb)->code;
	struct tnl_ptk_info tpi;
	bool csum_err = false;
	int i;

	if (parse_gre_header(skb, &tpi, &csum_err)) {
		if (!csum_err)		/* ignore csum errors. */
			return;
	}

	if (type == ICMP_DEST_UNREACH && code == ICMP_FRAG_NEEDED) {
		ipv4_update_pmtu(skb, dev_net(skb->dev), info,
				skb->dev->ifindex, 0, IPPROTO_GRE, 0);
		return;
	}
	if (type == ICMP_REDIRECT) {
		ipv4_redirect(skb, dev_net(skb->dev), skb->dev->ifindex, 0,
				IPPROTO_GRE, 0);
		return;
	}

	rcu_read_lock();
	for (i = 0; i < GRE_IP_PROTO_MAX; i++) {
		struct gre_cisco_protocol *proto;

		proto = rcu_dereference(gre_cisco_proto_list[i]);
		if (!proto)
			continue;

		if (proto->err_handler(skb, info, &tpi) == PACKET_RCVD)
			goto out;

	}
out:
	rcu_read_unlock();
}

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
	__be16 protocol = skb->protocol;
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
	skb->protocol = greh->protocol;
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
		skb->protocol = protocol;
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

static const struct gre_protocol ipgre_protocol = {
	.handler     = gre_cisco_rcv,
	.err_handler = gre_cisco_err,
};

int gre_cisco_register(struct gre_cisco_protocol *newp)
{
	struct gre_cisco_protocol **proto = (struct gre_cisco_protocol **)
					    &gre_cisco_proto_list[newp->priority];

	return (cmpxchg(proto, NULL, newp) == NULL) ? 0 : -EBUSY;
}
EXPORT_SYMBOL_GPL(gre_cisco_register);

int gre_cisco_unregister(struct gre_cisco_protocol *del_proto)
{
	struct gre_cisco_protocol **proto = (struct gre_cisco_protocol **)
					    &gre_cisco_proto_list[del_proto->priority];
	int ret;

	ret = (cmpxchg(proto, del_proto, NULL) == del_proto) ? 0 : -EINVAL;

	if (ret)
		return ret;

	synchronize_net();
	return 0;
}
EXPORT_SYMBOL_GPL(gre_cisco_unregister);

static int __init gre_init(void)
{
	pr_info("GRE over IPv4 demultiplexor driver\n");

	if (inet_add_protocol(&net_gre_protocol, IPPROTO_GRE) < 0) {
		pr_err("can't add protocol\n");
		goto err;
	}

	if (gre_add_protocol(&ipgre_protocol, GREPROTO_CISCO) < 0) {
		pr_info("%s: can't add ipgre handler\n", __func__);
		goto err_gre;
	}

	if (inet_add_offload(&gre_offload, IPPROTO_GRE)) {
		pr_err("can't add protocol offload\n");
		goto err_gso;
	}

	return 0;
err_gso:
	gre_del_protocol(&ipgre_protocol, GREPROTO_CISCO);
err_gre:
	inet_del_protocol(&net_gre_protocol, IPPROTO_GRE);
err:
	return -EAGAIN;
}

static void __exit gre_exit(void)
{
	inet_del_offload(&gre_offload, IPPROTO_GRE);
	gre_del_protocol(&ipgre_protocol, GREPROTO_CISCO);
	inet_del_protocol(&net_gre_protocol, IPPROTO_GRE);
}

module_init(gre_init);
module_exit(gre_exit);

MODULE_DESCRIPTION("GRE over IPv4 demultiplexer driver");
MODULE_AUTHOR("D. Kozlov (xeb@mail.ru)");
MODULE_LICENSE("GPL");
