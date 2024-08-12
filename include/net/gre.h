/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_GRE_H
#define __LINUX_GRE_H

#include <linux/skbuff.h>
#include <net/ip_tunnels.h>

struct gre_base_hdr {
	__be16 flags;
	__be16 protocol;
} __packed;

struct gre_full_hdr {
	struct gre_base_hdr fixed_header;
	__be16 csum;
	__be16 reserved1;
	__be32 key;
	__be32 seq;
} __packed;
#define GRE_HEADER_SECTION 4

#define GREPROTO_CISCO		0
#define GREPROTO_PPTP		1
#define GREPROTO_MAX		2
#define GRE_IP_PROTO_MAX	2

struct gre_protocol {
	int  (*handler)(struct sk_buff *skb);
	void (*err_handler)(struct sk_buff *skb, u32 info);
};

int gre_add_protocol(const struct gre_protocol *proto, u8 version);
int gre_del_protocol(const struct gre_protocol *proto, u8 version);

struct net_device *gretap_fb_dev_create(struct net *net, const char *name,
				       u8 name_assign_type);
int gre_parse_header(struct sk_buff *skb, struct tnl_ptk_info *tpi,
		     bool *csum_err, __be16 proto, int nhs);

static inline bool netif_is_gretap(const struct net_device *dev)
{
	return dev->rtnl_link_ops &&
	       !strcmp(dev->rtnl_link_ops->kind, "gretap");
}

static inline bool netif_is_ip6gretap(const struct net_device *dev)
{
	return dev->rtnl_link_ops &&
	       !strcmp(dev->rtnl_link_ops->kind, "ip6gretap");
}

static inline int gre_calc_hlen(const unsigned long *o_flags)
{
	int addend = 4;

	if (test_bit(IP_TUNNEL_CSUM_BIT, o_flags))
		addend += 4;
	if (test_bit(IP_TUNNEL_KEY_BIT, o_flags))
		addend += 4;
	if (test_bit(IP_TUNNEL_SEQ_BIT, o_flags))
		addend += 4;
	return addend;
}

static inline void gre_flags_to_tnl_flags(unsigned long *dst, __be16 flags)
{
	IP_TUNNEL_DECLARE_FLAGS(res) = { };

	__assign_bit(IP_TUNNEL_CSUM_BIT, res, flags & GRE_CSUM);
	__assign_bit(IP_TUNNEL_ROUTING_BIT, res, flags & GRE_ROUTING);
	__assign_bit(IP_TUNNEL_KEY_BIT, res, flags & GRE_KEY);
	__assign_bit(IP_TUNNEL_SEQ_BIT, res, flags & GRE_SEQ);
	__assign_bit(IP_TUNNEL_STRICT_BIT, res, flags & GRE_STRICT);
	__assign_bit(IP_TUNNEL_REC_BIT, res, flags & GRE_REC);
	__assign_bit(IP_TUNNEL_VERSION_BIT, res, flags & GRE_VERSION);

	ip_tunnel_flags_copy(dst, res);
}

static inline __be16 gre_tnl_flags_to_gre_flags(const unsigned long *tflags)
{
	__be16 flags = 0;

	if (test_bit(IP_TUNNEL_CSUM_BIT, tflags))
		flags |= GRE_CSUM;
	if (test_bit(IP_TUNNEL_ROUTING_BIT, tflags))
		flags |= GRE_ROUTING;
	if (test_bit(IP_TUNNEL_KEY_BIT, tflags))
		flags |= GRE_KEY;
	if (test_bit(IP_TUNNEL_SEQ_BIT, tflags))
		flags |= GRE_SEQ;
	if (test_bit(IP_TUNNEL_STRICT_BIT, tflags))
		flags |= GRE_STRICT;
	if (test_bit(IP_TUNNEL_REC_BIT, tflags))
		flags |= GRE_REC;
	if (test_bit(IP_TUNNEL_VERSION_BIT, tflags))
		flags |= GRE_VERSION;

	return flags;
}

static inline void gre_build_header(struct sk_buff *skb, int hdr_len,
				    const unsigned long *flags, __be16 proto,
				    __be32 key, __be32 seq)
{
	IP_TUNNEL_DECLARE_FLAGS(cond) = { };
	struct gre_base_hdr *greh;

	skb_push(skb, hdr_len);

	skb_set_inner_protocol(skb, proto);
	skb_reset_transport_header(skb);
	greh = (struct gre_base_hdr *)skb->data;
	greh->flags = gre_tnl_flags_to_gre_flags(flags);
	greh->protocol = proto;

	__set_bit(IP_TUNNEL_KEY_BIT, cond);
	__set_bit(IP_TUNNEL_CSUM_BIT, cond);
	__set_bit(IP_TUNNEL_SEQ_BIT, cond);

	if (ip_tunnel_flags_intersect(flags, cond)) {
		__be32 *ptr = (__be32 *)(((u8 *)greh) + hdr_len - 4);

		if (test_bit(IP_TUNNEL_SEQ_BIT, flags)) {
			*ptr = seq;
			ptr--;
		}
		if (test_bit(IP_TUNNEL_KEY_BIT, flags)) {
			*ptr = key;
			ptr--;
		}
		if (test_bit(IP_TUNNEL_CSUM_BIT, flags) &&
		    !(skb_shinfo(skb)->gso_type &
		      (SKB_GSO_GRE | SKB_GSO_GRE_CSUM))) {
			*ptr = 0;
			if (skb->ip_summed == CHECKSUM_PARTIAL) {
				*(__sum16 *)ptr = csum_fold(lco_csum(skb));
			} else {
				skb->ip_summed = CHECKSUM_PARTIAL;
				skb->csum_start = skb_transport_header(skb) - skb->head;
				skb->csum_offset = sizeof(*greh);
			}
		}
	}
}

#endif
