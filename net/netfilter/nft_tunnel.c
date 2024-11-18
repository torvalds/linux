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
#include <net/geneve.h>

struct nft_tunnel {
	enum nft_tunnel_keys	key:8;
	u8			dreg;
	enum nft_tunnel_mode	mode:8;
	u8			len;
};

static void nft_tunnel_get_eval(const struct nft_expr *expr,
				struct nft_regs *regs,
				const struct nft_pktinfo *pkt)
{
	const struct nft_tunnel *priv = nft_expr_priv(expr);
	u32 *dest = &regs->data[priv->dreg];
	struct ip_tunnel_info *tun_info;

	tun_info = skb_tunnel_info(pkt->skb);

	switch (priv->key) {
	case NFT_TUNNEL_PATH:
		if (!tun_info) {
			nft_reg_store8(dest, false);
			return;
		}
		if (priv->mode == NFT_TUNNEL_MODE_NONE ||
		    (priv->mode == NFT_TUNNEL_MODE_RX &&
		     !(tun_info->mode & IP_TUNNEL_INFO_TX)) ||
		    (priv->mode == NFT_TUNNEL_MODE_TX &&
		     (tun_info->mode & IP_TUNNEL_INFO_TX)))
			nft_reg_store8(dest, true);
		else
			nft_reg_store8(dest, false);
		break;
	case NFT_TUNNEL_ID:
		if (!tun_info) {
			regs->verdict.code = NFT_BREAK;
			return;
		}
		if (priv->mode == NFT_TUNNEL_MODE_NONE ||
		    (priv->mode == NFT_TUNNEL_MODE_RX &&
		     !(tun_info->mode & IP_TUNNEL_INFO_TX)) ||
		    (priv->mode == NFT_TUNNEL_MODE_TX &&
		     (tun_info->mode & IP_TUNNEL_INFO_TX)))
			*dest = ntohl(tunnel_id_to_key32(tun_info->key.tun_id));
		else
			regs->verdict.code = NFT_BREAK;
		break;
	default:
		WARN_ON(1);
		regs->verdict.code = NFT_BREAK;
	}
}

static const struct nla_policy nft_tunnel_policy[NFTA_TUNNEL_MAX + 1] = {
	[NFTA_TUNNEL_KEY]	= NLA_POLICY_MAX(NLA_BE32, 255),
	[NFTA_TUNNEL_DREG]	= { .type = NLA_U32 },
	[NFTA_TUNNEL_MODE]	= NLA_POLICY_MAX(NLA_BE32, 255),
};

static int nft_tunnel_get_init(const struct nft_ctx *ctx,
			       const struct nft_expr *expr,
			       const struct nlattr * const tb[])
{
	struct nft_tunnel *priv = nft_expr_priv(expr);
	u32 len;

	if (!tb[NFTA_TUNNEL_KEY] ||
	    !tb[NFTA_TUNNEL_DREG])
		return -EINVAL;

	priv->key = ntohl(nla_get_be32(tb[NFTA_TUNNEL_KEY]));
	switch (priv->key) {
	case NFT_TUNNEL_PATH:
		len = sizeof(u8);
		break;
	case NFT_TUNNEL_ID:
		len = sizeof(u32);
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (tb[NFTA_TUNNEL_MODE]) {
		priv->mode = ntohl(nla_get_be32(tb[NFTA_TUNNEL_MODE]));
		if (priv->mode > NFT_TUNNEL_MODE_MAX)
			return -EOPNOTSUPP;
	} else {
		priv->mode = NFT_TUNNEL_MODE_NONE;
	}

	priv->len = len;
	return nft_parse_register_store(ctx, tb[NFTA_TUNNEL_DREG], &priv->dreg,
					NULL, NFT_DATA_VALUE, len);
}

static int nft_tunnel_get_dump(struct sk_buff *skb,
			       const struct nft_expr *expr, bool reset)
{
	const struct nft_tunnel *priv = nft_expr_priv(expr);

	if (nla_put_be32(skb, NFTA_TUNNEL_KEY, htonl(priv->key)))
		goto nla_put_failure;
	if (nft_dump_register(skb, NFTA_TUNNEL_DREG, priv->dreg))
		goto nla_put_failure;
	if (nla_put_be32(skb, NFTA_TUNNEL_MODE, htonl(priv->mode)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static bool nft_tunnel_get_reduce(struct nft_regs_track *track,
				  const struct nft_expr *expr)
{
	const struct nft_tunnel *priv = nft_expr_priv(expr);
	const struct nft_tunnel *tunnel;

	if (!nft_reg_track_cmp(track, expr, priv->dreg)) {
		nft_reg_track_update(track, expr, priv->dreg, priv->len);
		return false;
	}

	tunnel = nft_expr_priv(track->regs[priv->dreg].selector);
	if (priv->key != tunnel->key ||
	    priv->dreg != tunnel->dreg ||
	    priv->mode != tunnel->mode) {
		nft_reg_track_update(track, expr, priv->dreg, priv->len);
		return false;
	}

	if (!track->regs[priv->dreg].bitwise)
		return true;

	return false;
}

static struct nft_expr_type nft_tunnel_type;
static const struct nft_expr_ops nft_tunnel_get_ops = {
	.type		= &nft_tunnel_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_tunnel)),
	.eval		= nft_tunnel_get_eval,
	.init		= nft_tunnel_get_init,
	.dump		= nft_tunnel_get_dump,
	.reduce		= nft_tunnel_get_reduce,
};

static struct nft_expr_type nft_tunnel_type __read_mostly = {
	.name		= "tunnel",
	.family		= NFPROTO_NETDEV,
	.ops		= &nft_tunnel_get_ops,
	.policy		= nft_tunnel_policy,
	.maxattr	= NFTA_TUNNEL_MAX,
	.owner		= THIS_MODULE,
};

struct nft_tunnel_opts {
	union {
		struct vxlan_metadata	vxlan;
		struct erspan_metadata	erspan;
		u8	data[IP_TUNNEL_OPTS_MAX];
	} u;
	IP_TUNNEL_DECLARE_FLAGS(flags);
	u32	len;
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

	err = nla_parse_nested_deprecated(tb, NFTA_TUNNEL_KEY_IP_MAX, attr,
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

	err = nla_parse_nested_deprecated(tb, NFTA_TUNNEL_KEY_IP6_MAX, attr,
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

	err = nla_parse_nested_deprecated(tb, NFTA_TUNNEL_KEY_VXLAN_MAX, attr,
					  nft_tunnel_opts_vxlan_policy, NULL);
	if (err < 0)
		return err;

	if (!tb[NFTA_TUNNEL_KEY_VXLAN_GBP])
		return -EINVAL;

	opts->u.vxlan.gbp = ntohl(nla_get_be32(tb[NFTA_TUNNEL_KEY_VXLAN_GBP]));

	opts->len	= sizeof(struct vxlan_metadata);
	ip_tunnel_flags_zero(opts->flags);
	__set_bit(IP_TUNNEL_VXLAN_OPT_BIT, opts->flags);

	return 0;
}

static const struct nla_policy nft_tunnel_opts_erspan_policy[NFTA_TUNNEL_KEY_ERSPAN_MAX + 1] = {
	[NFTA_TUNNEL_KEY_ERSPAN_VERSION]	= { .type = NLA_U32 },
	[NFTA_TUNNEL_KEY_ERSPAN_V1_INDEX]	= { .type = NLA_U32 },
	[NFTA_TUNNEL_KEY_ERSPAN_V2_DIR]		= { .type = NLA_U8 },
	[NFTA_TUNNEL_KEY_ERSPAN_V2_HWID]	= { .type = NLA_U8 },
};

static int nft_tunnel_obj_erspan_init(const struct nlattr *attr,
				      struct nft_tunnel_opts *opts)
{
	struct nlattr *tb[NFTA_TUNNEL_KEY_ERSPAN_MAX + 1];
	uint8_t hwid, dir;
	int err, version;

	err = nla_parse_nested_deprecated(tb, NFTA_TUNNEL_KEY_ERSPAN_MAX,
					  attr, nft_tunnel_opts_erspan_policy,
					  NULL);
	if (err < 0)
		return err;

	if (!tb[NFTA_TUNNEL_KEY_ERSPAN_VERSION])
		 return -EINVAL;

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
	ip_tunnel_flags_zero(opts->flags);
	__set_bit(IP_TUNNEL_ERSPAN_OPT_BIT, opts->flags);

	return 0;
}

static const struct nla_policy nft_tunnel_opts_geneve_policy[NFTA_TUNNEL_KEY_GENEVE_MAX + 1] = {
	[NFTA_TUNNEL_KEY_GENEVE_CLASS]	= { .type = NLA_U16 },
	[NFTA_TUNNEL_KEY_GENEVE_TYPE]	= { .type = NLA_U8 },
	[NFTA_TUNNEL_KEY_GENEVE_DATA]	= { .type = NLA_BINARY, .len = 128 },
};

static int nft_tunnel_obj_geneve_init(const struct nlattr *attr,
				      struct nft_tunnel_opts *opts)
{
	struct geneve_opt *opt = (struct geneve_opt *)opts->u.data + opts->len;
	struct nlattr *tb[NFTA_TUNNEL_KEY_GENEVE_MAX + 1];
	int err, data_len;

	err = nla_parse_nested(tb, NFTA_TUNNEL_KEY_GENEVE_MAX, attr,
			       nft_tunnel_opts_geneve_policy, NULL);
	if (err < 0)
		return err;

	if (!tb[NFTA_TUNNEL_KEY_GENEVE_CLASS] ||
	    !tb[NFTA_TUNNEL_KEY_GENEVE_TYPE] ||
	    !tb[NFTA_TUNNEL_KEY_GENEVE_DATA])
		return -EINVAL;

	attr = tb[NFTA_TUNNEL_KEY_GENEVE_DATA];
	data_len = nla_len(attr);
	if (data_len % 4)
		return -EINVAL;

	opts->len += sizeof(*opt) + data_len;
	if (opts->len > IP_TUNNEL_OPTS_MAX)
		return -EINVAL;

	memcpy(opt->opt_data, nla_data(attr), data_len);
	opt->length = data_len / 4;
	opt->opt_class = nla_get_be16(tb[NFTA_TUNNEL_KEY_GENEVE_CLASS]);
	opt->type = nla_get_u8(tb[NFTA_TUNNEL_KEY_GENEVE_TYPE]);
	ip_tunnel_flags_zero(opts->flags);
	__set_bit(IP_TUNNEL_GENEVE_OPT_BIT, opts->flags);

	return 0;
}

static const struct nla_policy nft_tunnel_opts_policy[NFTA_TUNNEL_KEY_OPTS_MAX + 1] = {
	[NFTA_TUNNEL_KEY_OPTS_UNSPEC]	= {
		.strict_start_type = NFTA_TUNNEL_KEY_OPTS_GENEVE },
	[NFTA_TUNNEL_KEY_OPTS_VXLAN]	= { .type = NLA_NESTED, },
	[NFTA_TUNNEL_KEY_OPTS_ERSPAN]	= { .type = NLA_NESTED, },
	[NFTA_TUNNEL_KEY_OPTS_GENEVE]	= { .type = NLA_NESTED, },
};

static int nft_tunnel_obj_opts_init(const struct nft_ctx *ctx,
				    const struct nlattr *attr,
				    struct ip_tunnel_info *info,
				    struct nft_tunnel_opts *opts)
{
	struct nlattr *nla;
	int err, rem;
	u32 type = 0;

	err = nla_validate_nested_deprecated(attr, NFTA_TUNNEL_KEY_OPTS_MAX,
					     nft_tunnel_opts_policy, NULL);
	if (err < 0)
		return err;

	nla_for_each_attr(nla, nla_data(attr), nla_len(attr), rem) {
		switch (nla_type(nla)) {
		case NFTA_TUNNEL_KEY_OPTS_VXLAN:
			if (type)
				return -EINVAL;
			err = nft_tunnel_obj_vxlan_init(nla, opts);
			if (err)
				return err;
			type = IP_TUNNEL_VXLAN_OPT_BIT;
			break;
		case NFTA_TUNNEL_KEY_OPTS_ERSPAN:
			if (type)
				return -EINVAL;
			err = nft_tunnel_obj_erspan_init(nla, opts);
			if (err)
				return err;
			type = IP_TUNNEL_ERSPAN_OPT_BIT;
			break;
		case NFTA_TUNNEL_KEY_OPTS_GENEVE:
			if (type && type != IP_TUNNEL_GENEVE_OPT_BIT)
				return -EINVAL;
			err = nft_tunnel_obj_geneve_init(nla, opts);
			if (err)
				return err;
			type = IP_TUNNEL_GENEVE_OPT_BIT;
			break;
		default:
			return -EOPNOTSUPP;
		}
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
	[NFTA_TUNNEL_KEY_SPORT]	= { .type = NLA_U16, },
	[NFTA_TUNNEL_KEY_DPORT]	= { .type = NLA_U16, },
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
	__set_bit(IP_TUNNEL_KEY_BIT, info.key.tun_flags);
	__set_bit(IP_TUNNEL_CSUM_BIT, info.key.tun_flags);
	__set_bit(IP_TUNNEL_NOCACHE_BIT, info.key.tun_flags);

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
		info.key.tp_src = nla_get_be16(tb[NFTA_TUNNEL_KEY_SPORT]);
	}
	if (tb[NFTA_TUNNEL_KEY_DPORT]) {
		info.key.tp_dst = nla_get_be16(tb[NFTA_TUNNEL_KEY_DPORT]);
	}

	if (tb[NFTA_TUNNEL_KEY_FLAGS]) {
		u32 tun_flags;

		tun_flags = ntohl(nla_get_be32(tb[NFTA_TUNNEL_KEY_FLAGS]));
		if (tun_flags & ~NFT_TUNNEL_F_MASK)
			return -EOPNOTSUPP;

		if (tun_flags & NFT_TUNNEL_F_ZERO_CSUM_TX)
			__clear_bit(IP_TUNNEL_CSUM_BIT, info.key.tun_flags);
		if (tun_flags & NFT_TUNNEL_F_DONT_FRAGMENT)
			__set_bit(IP_TUNNEL_DONT_FRAGMENT_BIT,
				  info.key.tun_flags);
		if (tun_flags & NFT_TUNNEL_F_SEQ_NUMBER)
			__set_bit(IP_TUNNEL_SEQ_BIT, info.key.tun_flags);
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

	md = metadata_dst_alloc(priv->opts.len, METADATA_IP_TUNNEL,
				GFP_KERNEL_ACCOUNT);
	if (!md)
		return -ENOMEM;

	memcpy(&md->u.tun_info, &info, sizeof(info));
#ifdef CONFIG_DST_CACHE
	err = dst_cache_init(&md->u.tun_info.dst_cache, GFP_KERNEL_ACCOUNT);
	if (err < 0) {
		metadata_dst_free(md);
		return err;
	}
#endif
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
		nest = nla_nest_start_noflag(skb, NFTA_TUNNEL_KEY_IP6);
		if (!nest)
			return -1;

		if (nla_put_in6_addr(skb, NFTA_TUNNEL_KEY_IP6_SRC,
				     &info->key.u.ipv6.src) < 0 ||
		    nla_put_in6_addr(skb, NFTA_TUNNEL_KEY_IP6_DST,
				     &info->key.u.ipv6.dst) < 0 ||
		    nla_put_be32(skb, NFTA_TUNNEL_KEY_IP6_FLOWLABEL,
				 info->key.label)) {
			nla_nest_cancel(skb, nest);
			return -1;
		}

		nla_nest_end(skb, nest);
	} else {
		nest = nla_nest_start_noflag(skb, NFTA_TUNNEL_KEY_IP);
		if (!nest)
			return -1;

		if (nla_put_in_addr(skb, NFTA_TUNNEL_KEY_IP_SRC,
				    info->key.u.ipv4.src) < 0 ||
		    nla_put_in_addr(skb, NFTA_TUNNEL_KEY_IP_DST,
				    info->key.u.ipv4.dst) < 0) {
			nla_nest_cancel(skb, nest);
			return -1;
		}

		nla_nest_end(skb, nest);
	}

	return 0;
}

static int nft_tunnel_opts_dump(struct sk_buff *skb,
				struct nft_tunnel_obj *priv)
{
	struct nft_tunnel_opts *opts = &priv->opts;
	struct nlattr *nest, *inner;

	nest = nla_nest_start_noflag(skb, NFTA_TUNNEL_KEY_OPTS);
	if (!nest)
		return -1;

	if (test_bit(IP_TUNNEL_VXLAN_OPT_BIT, opts->flags)) {
		inner = nla_nest_start_noflag(skb, NFTA_TUNNEL_KEY_OPTS_VXLAN);
		if (!inner)
			goto failure;
		if (nla_put_be32(skb, NFTA_TUNNEL_KEY_VXLAN_GBP,
				 htonl(opts->u.vxlan.gbp)))
			goto inner_failure;
		nla_nest_end(skb, inner);
	} else if (test_bit(IP_TUNNEL_ERSPAN_OPT_BIT, opts->flags)) {
		inner = nla_nest_start_noflag(skb, NFTA_TUNNEL_KEY_OPTS_ERSPAN);
		if (!inner)
			goto failure;
		if (nla_put_be32(skb, NFTA_TUNNEL_KEY_ERSPAN_VERSION,
				 htonl(opts->u.erspan.version)))
			goto inner_failure;
		switch (opts->u.erspan.version) {
		case ERSPAN_VERSION:
			if (nla_put_be32(skb, NFTA_TUNNEL_KEY_ERSPAN_V1_INDEX,
					 opts->u.erspan.u.index))
				goto inner_failure;
			break;
		case ERSPAN_VERSION2:
			if (nla_put_u8(skb, NFTA_TUNNEL_KEY_ERSPAN_V2_HWID,
				       get_hwid(&opts->u.erspan.u.md2)) ||
			    nla_put_u8(skb, NFTA_TUNNEL_KEY_ERSPAN_V2_DIR,
				       opts->u.erspan.u.md2.dir))
				goto inner_failure;
			break;
		}
		nla_nest_end(skb, inner);
	} else if (test_bit(IP_TUNNEL_GENEVE_OPT_BIT, opts->flags)) {
		struct geneve_opt *opt;
		int offset = 0;

		inner = nla_nest_start_noflag(skb, NFTA_TUNNEL_KEY_OPTS_GENEVE);
		if (!inner)
			goto failure;
		while (opts->len > offset) {
			opt = (struct geneve_opt *)opts->u.data + offset;
			if (nla_put_be16(skb, NFTA_TUNNEL_KEY_GENEVE_CLASS,
					 opt->opt_class) ||
			    nla_put_u8(skb, NFTA_TUNNEL_KEY_GENEVE_TYPE,
				       opt->type) ||
			    nla_put(skb, NFTA_TUNNEL_KEY_GENEVE_DATA,
				    opt->length * 4, opt->opt_data))
				goto inner_failure;
			offset += sizeof(*opt) + opt->length * 4;
		}
		nla_nest_end(skb, inner);
	}
	nla_nest_end(skb, nest);
	return 0;

inner_failure:
	nla_nest_cancel(skb, inner);
failure:
	nla_nest_cancel(skb, nest);
	return -1;
}

static int nft_tunnel_ports_dump(struct sk_buff *skb,
				 struct ip_tunnel_info *info)
{
	if (nla_put_be16(skb, NFTA_TUNNEL_KEY_SPORT, info->key.tp_src) < 0 ||
	    nla_put_be16(skb, NFTA_TUNNEL_KEY_DPORT, info->key.tp_dst) < 0)
		return -1;

	return 0;
}

static int nft_tunnel_flags_dump(struct sk_buff *skb,
				 struct ip_tunnel_info *info)
{
	u32 flags = 0;

	if (test_bit(IP_TUNNEL_DONT_FRAGMENT_BIT, info->key.tun_flags))
		flags |= NFT_TUNNEL_F_DONT_FRAGMENT;
	if (!test_bit(IP_TUNNEL_CSUM_BIT, info->key.tun_flags))
		flags |= NFT_TUNNEL_F_ZERO_CSUM_TX;
	if (test_bit(IP_TUNNEL_SEQ_BIT, info->key.tun_flags))
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
	.family		= NFPROTO_NETDEV,
	.ops		= &nft_tunnel_obj_ops,
	.maxattr	= NFTA_TUNNEL_KEY_MAX,
	.policy		= nft_tunnel_key_policy,
	.owner		= THIS_MODULE,
};

static int __init nft_tunnel_module_init(void)
{
	int err;

	err = nft_register_expr(&nft_tunnel_type);
	if (err < 0)
		return err;

	err = nft_register_obj(&nft_tunnel_obj_type);
	if (err < 0)
		nft_unregister_expr(&nft_tunnel_type);

	return err;
}

static void __exit nft_tunnel_module_exit(void)
{
	nft_unregister_obj(&nft_tunnel_obj_type);
	nft_unregister_expr(&nft_tunnel_type);
}

module_init(nft_tunnel_module_init);
module_exit(nft_tunnel_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pablo Neira Ayuso <pablo@netfilter.org>");
MODULE_ALIAS_NFT_EXPR("tunnel");
MODULE_ALIAS_NFT_OBJ(NFT_OBJECT_TUNNEL);
MODULE_DESCRIPTION("nftables tunnel expression support");
