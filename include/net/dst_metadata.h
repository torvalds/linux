#ifndef __NET_DST_METADATA_H
#define __NET_DST_METADATA_H 1

#include <linux/skbuff.h>
#include <net/ip_tunnels.h>
#include <net/dst.h>

struct metadata_dst {
	struct dst_entry		dst;
	size_t				opts_len;
	union {
		struct ip_tunnel_info	tun_info;
	} u;
};

static inline struct metadata_dst *skb_metadata_dst(struct sk_buff *skb)
{
	struct metadata_dst *md_dst = (struct metadata_dst *) skb_dst(skb);

	if (md_dst && md_dst->dst.flags & DST_METADATA)
		return md_dst;

	return NULL;
}

static inline struct ip_tunnel_info *skb_tunnel_info(struct sk_buff *skb,
						     int family)
{
	struct metadata_dst *md_dst = skb_metadata_dst(skb);
	struct rtable *rt;

	if (md_dst)
		return &md_dst->u.tun_info;

	switch (family) {
	case AF_INET:
		rt = (struct rtable *)skb_dst(skb);
		if (rt && rt->rt_lwtstate)
			return lwt_tun_info(rt->rt_lwtstate);
		break;
	}

	return NULL;
}

static inline bool skb_valid_dst(const struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);

	return dst && !(dst->flags & DST_METADATA);
}

struct metadata_dst *metadata_dst_alloc(u8 optslen, gfp_t flags);

#endif /* __NET_DST_METADATA_H */
