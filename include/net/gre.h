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

static inline int gre_calc_hlen(__be16 o_flags)
{
	int addend = 4;

	if (o_flags & TUNNEL_CSUM)
		addend += 4;
	if (o_flags & TUNNEL_KEY)
		addend += 4;
	if (o_flags & TUNNEL_SEQ)
		addend += 4;
	return addend;
}

static inline __be16 gre_flags_to_tnl_flags(__be16 flags)
{
	__be16 tflags = 0;

	if (flags & GRE_CSUM)
		tflags |= TUNNEL_CSUM;
	if (flags & GRE_ROUTING)
		tflags |= TUNNEL_ROUTING;
	if (flags & GRE_KEY)
		tflags |= TUNNEL_KEY;
	if (flags & GRE_SEQ)
		tflags |= TUNNEL_SEQ;
	if (flags & GRE_STRICT)
		tflags |= TUNNEL_STRICT;
	if (flags & GRE_REC)
		tflags |= TUNNEL_REC;
	if (flags & GRE_VERSION)
		tflags |= TUNNEL_VERSION;

	return tflags;
}

static inline __be16 gre_tnl_flags_to_gre_flags(__be16 tflags)
{
	__be16 flags = 0;

	if (tflags & TUNNEL_CSUM)
		flags |= GRE_CSUM;
	if (tflags & TUNNEL_ROUTING)
		flags |= GRE_ROUTING;
	if (tflags & TUNNEL_KEY)
		flags |= GRE_KEY;
	if (tflags & TUNNEL_SEQ)
		flags |= GRE_SEQ;
	if (tflags & TUNNEL_STRICT)
		flags |= GRE_STRICT;
	if (tflags & TUNNEL_REC)
		flags |= GRE_REC;
	if (tflags & TUNNEL_VERSION)
		flags |= GRE_VERSION;

	return flags;
}

static inline void gre_build_header(struct sk_buff *skb, int hdr_len,
				    __be16 flags, __be16 proto,
				    __be32 key, __be32 seq)
{
	struct gre_base_hdr *greh;

	skb_push(skb, hdr_len);

	skb_set_inner_protocol(skb, proto);
	skb_reset_transport_header(skb);
	greh = (struct gre_base_hdr *)skb->data;
	greh->flags = gre_tnl_flags_to_gre_flags(flags);
	greh->protocol = proto;

	if (flags & (TUNNEL_KEY | TUNNEL_CSUM | TUNNEL_SEQ)) {
		__be32 *ptr = (__be32 *)(((u8 *)greh) + hdr_len - 4);

		if (flags & TUNNEL_SEQ) {
			*ptr = seq;
			ptr--;
		}
		if (flags & TUNNEL_KEY) {
			*ptr = key;
			ptr--;
		}
		if (flags & TUNNEL_CSUM &&
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
