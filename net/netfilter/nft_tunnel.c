/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/seqlock.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>
#include <net/dst_metadata.h>
#include <net/ip_tunnels.h>
#include <net/vxlan.h>
#include <net/erspan.h>

struct nft_tunnel_opts {
	union {
		struct vxlan_metadata	vxlan;
		struct erspan_metadata	erspan;
	} u;
	u32	len;
	u32	flags;
};

struct nft_tunnel_obj {
	struct metadata_dst	*md;
	struct nft_tunnel_opts	opts;
};

static const struct nla_policy nft_tunnel_ip_policy[NFTA_TUNNEL_KEY_IP_MAX + 1] = {
	[NFTA_TUNNEL_KEY_IP_SRC]	= { .type = NLA_U32 },
	[NFTA_TUNNEL_KEY_IP_DST]	= { .type = NLA_U32 },
};

static int nft_tunnel_obj_ip_init(const struct nft_ctx *ctx,
				  const struct nlattr *attr,
				  struct ip_tunnel_info *info)
{
	struct nlattr *tb[NFTA_TUNNEL_KEY_IP_MAX + 1];
	int err;

	err = nla_parse_nested(tb, NFTA_TUNNEL_KEY_IP_MAX, attr,
			       nft_tunnel_ip_policy, NULL);
	if (err < 0)
		return err;

	if (!tb[NFTA_TUNNEL_KEY_IP_DST])
		return -EINVAL;

	if (tb[NFTA_TUNNEL_KEY_IP_SRC])
		info->key.u.ipv4.src = nla_get_be32(tb[NFTA_TUNNEL_KEY_IP_SRC]);
	if (tb[NFTA_TUNNEL_KEY_IP_DST])
		info->key.u.ipv4.dst = nla_get_be32(tb[NFTA_TUNNEL_KEY_IP_DST]);

	return 0;
}

static const struct nla_policy nft_tunnel_ip6_policy[NFTA_TUNNEL_KEY_IP6_MAX + 1] = {
	[NFTA_TUNNEL_KEY_IP6_SRC]	= { .len = sizeof(struct in6_addr), },
	[NFTA_TUNNEL_KEY_IP6_DST]	= { .len = sizeof(struct in6_addr), },
	[NFTA_TUNNEL_KEY_IP6_FLOWLABEL]	= { .type = NLA_U32, }
};

static int nft_tunnel_obj_ip6_init(const struct nft_ctx *ctx,
				   const struct nlattr *attr,
				   struct ip_tunnel_info *info)
{
	struct nlattr *tb[NFTA_TUNNEL_KEY_IP6_MAX + 1];
	int err;

	err = nla_parse_nested(tb, NFTA_TUNNEL_KEY_IP6_MAX, attr,
			       nft_tunnel_ip6_policy, NULL);
	if (err < 0)
		return err;

	if (!tb[NFTA_TUNNEL_KEY_IP6_DST])
		return -EINVAL;

	if (tb[NFTA_TUNNEL_KEY_IP6_SRC]) {
		memcpy(&info->key.u.ipv6.src,
		       nla_data(tb[NFTA_TUNNEL_KEY_IP6_SRC]),
		       sizeof(struct in6_addr));
	}
	if (tb[NFTA_TUNNEL_KEY_IP6_DST]) {
		memcpy(&info->key.u.ipv6.dst,
		       nla_data(tb[NFTA_TUNNEL_KEY_IP6_DST]),
		       sizeof(struct in6_addr));
	}
	if (tb[NFTA_TUNNEL_KEY_IP6_FLOWLABEL])
		info->key.label = nla_get_be32(tb[NFTA_TUNNEL_KEY_IP6_FLOWLABEL]);

	info->mode |= IP_TUNNEL_INFO_IPV6;

	return 0;
}

static const struct nla_policy nft_tunnel_opts_vxlan_policy[NFTA_TUNNEL_KEY_VXLAN_MAX + 1] = {
	[NFTA_TUNNEL_KEY_VXLAN_GBP]	= { .type = NLA_U32 },
};

static int nft_tunnel_obj_vxlan_init(const struct nlattr *attr,
				     struct nft_tunnel_opts *opts)
{
	struct nlattr *tb[NFTA_TUNNEL_KEY_VXLAN_MAX + 1];
	int err;

	err = nla_parse_nested(tb, NFTA_TUNNEL_KEY_VXLAN_MAX, attr,
			       nft_tunnel_opts_vxlan_policy, NULL);
	if (err < 0)
		return err;

	if (!tb[NFTA_TUNNEL_KEY_VXLAN_GBP])
		return -EINVAL;

	opts->u.vxlan.gbp = ntohl(nla_get_be32(tb[NFTA_TUNNEL_KEY_VXLAN_GBP]));

	opts->len	= sizeof(struct vxlan_metadata);
	opts->flags	= TUNNEL_VXLAN_OPT;

	return 0;
}

static const struct nla_policy nft_tunnel_opts_erspan_policy[NFTA_TUNNEL_KEY_ERSPAN_MAX + 1] = {
	[NFTA_TUNNEL_KEY_ERSPAN_V1_INDEX]	= { .type = NLA_U32 },
	[NFTA_TUNNEL_KEY_ERSPAN_V2_DIR]	= { .type = NLA_U8 },
	[NFTA_TUNNEL_KEY_ERSPAN_V2_HWID]	= { .type = NLA_U8 },
};

static int nft_tunnel_obj_erspan_init(const struct nlattr *attr,
				      struct nft_tunnel_opts *opts)
{
	struct nlattr *tb[NFTA_TUNNEL_KEY_ERSPAN_MAX + 1];
	uint8_t hwid, dir;
	int err, version;

	err = nla_parse_nested(tb, NFTA_TUNNEL_KEY_ERSPAN_MAX, attr,
			       nft_tunnel_opts_erspan_policy, NULL);
	if (err < 0)
		return err;

	version = ntohl(nla_get_be32(tb[NFTA_TUNNEL_KEY_ERSPAN_VERSION]));
	switch (version) {
	case ERSPAN_VERSION:
		if (!tb[NFTA_TUNNEL_KEY_ERSPAN_V1_INDEX])
			return -EINVAL;

		opts->u.erspan.u.index =
			nla_get_be32(tb[NFTA_TUNNEL_KEY_ERSPAN_V1_INDEX]);
		break;
	case ERSPAN_VERSION2:
		if (!tb[NFTA_TUNNEL_KEY_ERSPAN_V2_DIR] ||
		    !tb[NFTA_TUNNEL_KEY_ERSPAN_V2_HWID])
			return -EINVAL;

		hwid = nla_get_u8(tb[NFTA_TUNNEL_KEY_ERSPAN_V2_HWID]);
		dir = nla_get_u8(tb[NFTA_TUNNEL_KEY_ERSPAN_V2_DIR]);

		set_hwid(&opts->u.erspan.u.md2, hwid);
		opts->u.erspan.u.md2.dir = dir;
		break;
	default:
		return -EOPNOTSUPP;
	}
	opts->u.erspan.version = version;

	opts->len	= sizeof(struct erspan_metadata);
	opts->flags	= TUNNEL_ERSPAN_OPT;

	return 0;
}

static const struct nla_policy nft_tunnel_opts_policy[NFTA_TUNNEL_KEY_OPTS_MAX + 1] = {
	[NFTA_TUNNEL_KEY_OPTS_VXLAN]	= { .type = NLA_NESTED, },
	[NFTA_TUNNEL_KEY_OPTS_ERSPAN]	= { .type = NLA_NESTED, },
};

static int nft_tunnel_obj_opts_init(const struct nft_ctx *ctx,
				    const struct nlattr *attr,
				    struct ip_tunnel_info *info,
				    struct nft_tunnel_opts *opts)
{
	struct nlattr *tb[NFTA_TUNNEL_KEY_OPTS_MAX + 1];
	int err;

	err = nla_parse_nested(tb, NFTA_TUNNEL_KEY_OPTS_MAX, attr,
			       nft_tunnel_opts_policy, NULL);
	if (err < 0)
		return err;

	if (tb[NFTA_TUNNEL_KEY_OPTS_VXLAN]) {
		err = nft_tunnel_obj_vxlan_init(tb[NFTA_TUNNEL_KEY_OPTS_VXLAN],
						opts);
	} else if (tb[NFTA_TUNNEL_KEY_OPTS_ERSPAN]) {
		err = nft_tunnel_obj_erspan_init(tb[NFTA_TUNNEL_KEY_OPTS_ERSPAN],
						 opts);
	} else {
		return -EOPNOTSUPP;
	}

	return err;
}

static const struct nla_policy nft_tunnel_key_policy[NFTA_TUNNEL_KEY_MAX + 1] = {
	[NFTA_TUNNEL_KEY_IP]	= { .type = NLA_NESTED, },
	[NFTA_TUNNEL_KEY_IP6]	= { .type = NLA_NESTED, },
	[NFTA_TUNNEL_KEY_ID]	= { .type = NLA_U32, },
	[NFTA_TUNNEL_KEY_FLAGS]	= { .type = NLA_U32, },
	[NFTA_TUNNEL_KEY_TOS]	= { .type = NLA_U8, },
	[NFTA_TUNNEL_KEY_TTL]	= { .type = NLA_U8, },
	[NFTA_TUNNEL_KEY_OPTS]	= { .type = NLA_NESTED, },
};

static int nft_tunnel_obj_init(const struct nft_ctx *ctx,
			       const struct nlattr * const tb[],
			       struct nft_object *obj)
{
	struct nft_tunnel_obj *priv = nft_obj_data(obj);
	struct ip_tunnel_info info;
	struct metadata_dst *md;
	int err;

	if (!tb[NFTA_TUNNEL_KEY_ID])
		return -EINVAL;

	memset(&info, 0, sizeof(info));
	info.mode		= IP_TUNNEL_INFO_TX;
	info.key.tun_id		= key32_to_tunnel_id(nla_get_be32(tb[NFTA_TUNNEL_KEY_ID]));
	info.key.tun_flags	= TUNNEL_KEY | TUNNEL_CSUM | TUNNEL_NOCACHE;

	if (tb[NFTA_TUNNEL_KEY_IP]) {
		err = nft_tunnel_obj_ip_init(ctx, tb[NFTA_TUNNEL_KEY_IP], &info);
		if (err < 0)
			return err;
	} else if (tb[NFTA_TUNNEL_KEY_IP6]) {
		err = nft_tunnel_obj_ip6_init(ctx, tb[NFTA_TUNNEL_KEY_IP6], &info);
		if (err < 0)
			return err;
	} else {
		return -EINVAL;
	}

	if (tb[NFTA_TUNNEL_KEY_SPORT]) {
		info.key.tp_src =
			ntohs(nla_get_be16(tb[NFTA_TUNNEL_KEY_SPORT]));
	}
	if (tb[NFTA_TUNNEL_KEY_DPORT]) {
		info.key.tp_dst =
			ntohs(nla_get_be16(tb[NFTA_TUNNEL_KEY_DPORT]));
	}

	if (tb[NFTA_TUNNEL_KEY_FLAGS]) {
		u32 tun_flags;

		tun_flags = ntohl(nla_get_be32(tb[NFTA_TUNNEL_KEY_FLAGS]));
		if (tun_flags & ~NFT_TUNNEL_F_MASK)
			return -EOPNOTSUPP;

		if (tun_flags & NFT_TUNNEL_F_ZERO_CSUM_TX)
			info.key.tun_flags &= ~TUNNEL_CSUM;
		if (tun_flags & NFT_TUNNEL_F_DONT_FRAGMENT)
			info.key.tun_flags |= TUNNEL_DONT_FRAGMENT;
		if (tun_flags & NFT_TUNNEL_F_SEQ_NUMBER)
			info.key.tun_flags |= TUNNEL_SEQ;
	}
	if (tb[NFTA_TUNNEL_KEY_TOS])
		info.key.tos = nla_get_u8(tb[NFTA_TUNNEL_KEY_TOS]);
	if (tb[NFTA_TUNNEL_KEY_TTL])
		info.key.ttl = nla_get_u8(tb[NFTA_TUNNEL_KEY_TTL]);
	else
		info.key.ttl = U8_MAX;

	if (tb[NFTA_TUNNEL_KEY_OPTS]) {
		err = nft_tunnel_obj_opts_init(ctx, tb[NFTA_TUNNEL_KEY_OPTS],
					       &info, &priv->opts);
		if (err < 0)
			return err;
	}

	md = metadata_dst_alloc(priv->opts.len, METADATA_IP_TUNNEL, GFP_KERNEL);
	if (!md)
		return -ENOMEM;

	memcpy(&md->u.tun_info, &info, sizeof(info));
	ip_tunnel_info_opts_set(&md->u.tun_info, &priv->opts.u, priv->opts.len,
				priv->opts.flags);
	priv->md = md;

	return 0;
}

static inline void nft_tunnel_obj_eval(struct nft_object *obj,
				       struct nft_regs *regs,
				       const struct nft_pktinfo *pkt)
{
	struct nft_tunnel_obj *priv = nft_obj_data(obj);
	struct sk_buff *skb = pkt->skb;

	skb_dst_drop(skb);
	dst_hold((struct dst_entry *) priv->md);
	skb_dst_set(skb, (struct dst_entry *) priv->md);
}

static int nft_tunnel_ip_dump(struct sk_buff *skb, struct ip_tunnel_info *info)
{
	struct nlattr *nest;

	if (info->mode & IP_TUNNEL_INFO_IPV6) {
		nest = nla_nest_start(skb, NFTA_TUNNEL_KEY_IP6);
		if (!nest)
			return -1;

		if (nla_put_in6_addr(skb, NFTA_TUNNEL_KEY_IP6_SRC, &info->key.u.ipv6.src) < 0 ||
		    nla_put_in6_addr(skb, NFTA_TUNNEL_KEY_IP6_DST, &info->key.u.ipv6.dst) < 0 ||
		    nla_put_be32(skb, NFTA_TUNNEL_KEY_IP6_FLOWLABEL, info->key.label))
			return -1;

		nla_nest_end(skb, nest);
	} else {
		nest = nla_nest_start(skb, NFTA_TUNNEL_KEY_IP);
		if (!nest)
			return -1;

		if (nla_put_in_addr(skb, NFTA_TUNNEL_KEY_IP_SRC, info->key.u.ipv4.src) < 0 ||
		    nla_put_in_addr(skb, NFTA_TUNNEL_KEY_IP_DST, info->key.u.ipv4.dst) < 0)
			return -1;

		nla_nest_end(skb, nest);
	}

	return 0;
}

static int nft_tunnel_opts_dump(struct sk_buff *skb,
				struct nft_tunnel_obj *priv)
{
	struct nft_tunnel_opts *opts = &priv->opts;
	struct nlattr *nest;

	nest = nla_nest_start(skb, NFTA_TUNNEL_KEY_OPTS);
	if (!nest)
		return -1;

	if (opts->flags & TUNNEL_VXLAN_OPT) {
		if (nla_put_be32(skb, NFTA_TUNNEL_KEY_VXLAN_GBP,
				 htonl(opts->u.vxlan.gbp)))
			return -1;
	} else if (opts->flags & TUNNEL_ERSPAN_OPT) {
		switch (opts->u.erspan.version) {
		case ERSPAN_VERSION:
			if (nla_put_be32(skb, NFTA_TUNNEL_KEY_ERSPAN_V1_INDEX,
					 opts->u.erspan.u.index))
				return -1;
			break;
		case ERSPAN_VERSION2:
			if (nla_put_u8(skb, NFTA_TUNNEL_KEY_ERSPAN_V2_HWID,
				       get_hwid(&opts->u.erspan.u.md2)) ||
			    nla_put_u8(skb, NFTA_TUNNEL_KEY_ERSPAN_V2_DIR,
				       opts->u.erspan.u.md2.dir))
				return -1;
			break;
		}
	}
	nla_nest_end(skb, nest);

	return 0;
}

static int nft_tunnel_ports_dump(struct sk_buff *skb,
				 struct ip_tunnel_info *info)
{
	if (nla_put_be16(skb, NFTA_TUNNEL_KEY_SPORT, htons(info->key.tp_src)) < 0 ||
	    nla_put_be16(skb, NFTA_TUNNEL_KEY_DPORT, htons(info->key.tp_dst)) < 0)
		return -1;

	return 0;
}

static int nft_tunnel_flags_dump(struct sk_buff *skb,
				 struct ip_tunnel_info *info)
{
	u32 flags = 0;

	if (info->key.tun_flags & TUNNEL_DONT_FRAGMENT)
		flags |= NFT_TUNNEL_F_DONT_FRAGMENT;
	if (!(info->key.tun_flags & TUNNEL_CSUM))
		flags |= NFT_TUNNEL_F_ZERO_CSUM_TX;
	if (info->key.tun_flags & TUNNEL_SEQ)
		flags |= NFT_TUNNEL_F_SEQ_NUMBER;

	if (nla_put_be32(skb, NFTA_TUNNEL_KEY_FLAGS, htonl(flags)) < 0)
		return -1;

	return 0;
}

static int nft_tunnel_obj_dump(struct sk_buff *skb,
			       struct nft_object *obj, bool reset)
{
	struct nft_tunnel_obj *priv = nft_obj_data(obj);
	struct ip_tunnel_info *info = &priv->md->u.tun_info;

	if (nla_put_be32(skb, NFTA_TUNNEL_KEY_ID,
			 tunnel_id_to_key32(info->key.tun_id)) ||
	    nft_tunnel_ip_dump(skb, info) < 0 ||
	    nft_tunnel_ports_dump(skb, info) < 0 ||
	    nft_tunnel_flags_dump(skb, info) < 0 ||
	    nla_put_u8(skb, NFTA_TUNNEL_KEY_TOS, info->key.tos) ||
	    nla_put_u8(skb, NFTA_TUNNEL_KEY_TTL, info->key.ttl) ||
	    nft_tunnel_opts_dump(skb, priv) < 0)
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -1;
}

static void nft_tunnel_obj_destroy(const struct nft_ctx *ctx,
				   struct nft_object *obj)
{
	struct nft_tunnel_obj *priv = nft_obj_data(obj);

	metadata_dst_free(priv->md);
}

static struct nft_object_type nft_tunnel_obj_type;
static const struct nft_object_ops nft_tunnel_obj_ops = {
	.type		= &nft_tunnel_obj_type,
	.size		= sizeof(struct nft_tunnel_obj),
	.eval		= nft_tunnel_obj_eval,
	.init		= nft_tunnel_obj_init,
	.destroy	= nft_tunnel_obj_destroy,
	.dump		= nft_tunnel_obj_dump,
};

static struct nft_object_type nft_tunnel_obj_type __read_mostly = {
	.type		= NFT_OBJECT_TUNNEL,
	.ops		= &nft_tunnel_obj_ops,
	.maxattr	= NFTA_TUNNEL_KEY_MAX,
	.policy		= nft_tunnel_key_policy,
	.owner		= THIS_MODULE,
};

static int __init nft_tunnel_module_init(void)
{
	return nft_register_obj(&nft_tunnel_obj_type);
}

static void __exit nft_tunnel_module_exit(void)
{
	nft_unregister_obj(&nft_tunnel_obj_type);
}

module_init(nft_tunnel_module_init);
module_exit(nft_tunnel_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pablo Neira Ayuso <pablo@netfilter.org>");
MODULE_ALIAS_NFT_OBJ(NFT_OBJECT_TUNNEL);
