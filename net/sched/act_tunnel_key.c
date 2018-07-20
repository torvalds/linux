/*
 * Copyright (c) 2016, Amir Vadai <amir@vadai.me>
 * Copyright (c) 2016, Mellanox Technologies. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <net/geneve.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <net/dst.h>

#include <linux/tc_act/tc_tunnel_key.h>
#include <net/tc_act/tc_tunnel_key.h>

static unsigned int tunnel_key_net_id;
static struct tc_action_ops act_tunnel_key_ops;

static int tunnel_key_act(struct sk_buff *skb, const struct tc_action *a,
			  struct tcf_result *res)
{
	struct tcf_tunnel_key *t = to_tunnel_key(a);
	struct tcf_tunnel_key_params *params;
	int action;

	rcu_read_lock();

	params = rcu_dereference(t->params);

	tcf_lastuse_update(&t->tcf_tm);
	bstats_cpu_update(this_cpu_ptr(t->common.cpu_bstats), skb);
	action = READ_ONCE(t->tcf_action);

	switch (params->tcft_action) {
	case TCA_TUNNEL_KEY_ACT_RELEASE:
		skb_dst_drop(skb);
		break;
	case TCA_TUNNEL_KEY_ACT_SET:
		skb_dst_drop(skb);
		skb_dst_set(skb, dst_clone(&params->tcft_enc_metadata->dst));
		break;
	default:
		WARN_ONCE(1, "Bad tunnel_key action %d.\n",
			  params->tcft_action);
		break;
	}

	rcu_read_unlock();

	return action;
}

static const struct nla_policy
enc_opts_policy[TCA_TUNNEL_KEY_ENC_OPTS_MAX + 1] = {
	[TCA_TUNNEL_KEY_ENC_OPTS_GENEVE]	= { .type = NLA_NESTED },
};

static const struct nla_policy
geneve_opt_policy[TCA_TUNNEL_KEY_ENC_OPT_GENEVE_MAX + 1] = {
	[TCA_TUNNEL_KEY_ENC_OPT_GENEVE_CLASS]	   = { .type = NLA_U16 },
	[TCA_TUNNEL_KEY_ENC_OPT_GENEVE_TYPE]	   = { .type = NLA_U8 },
	[TCA_TUNNEL_KEY_ENC_OPT_GENEVE_DATA]	   = { .type = NLA_BINARY,
						       .len = 128 },
};

static int
tunnel_key_copy_geneve_opt(const struct nlattr *nla, void *dst, int dst_len,
			   struct netlink_ext_ack *extack)
{
	struct nlattr *tb[TCA_TUNNEL_KEY_ENC_OPT_GENEVE_MAX + 1];
	int err, data_len, opt_len;
	u8 *data;

	err = nla_parse_nested(tb, TCA_TUNNEL_KEY_ENC_OPT_GENEVE_MAX,
			       nla, geneve_opt_policy, extack);
	if (err < 0)
		return err;

	if (!tb[TCA_TUNNEL_KEY_ENC_OPT_GENEVE_CLASS] ||
	    !tb[TCA_TUNNEL_KEY_ENC_OPT_GENEVE_TYPE] ||
	    !tb[TCA_TUNNEL_KEY_ENC_OPT_GENEVE_DATA]) {
		NL_SET_ERR_MSG(extack, "Missing tunnel key geneve option class, type or data");
		return -EINVAL;
	}

	data = nla_data(tb[TCA_TUNNEL_KEY_ENC_OPT_GENEVE_DATA]);
	data_len = nla_len(tb[TCA_TUNNEL_KEY_ENC_OPT_GENEVE_DATA]);
	if (data_len < 4) {
		NL_SET_ERR_MSG(extack, "Tunnel key geneve option data is less than 4 bytes long");
		return -ERANGE;
	}
	if (data_len % 4) {
		NL_SET_ERR_MSG(extack, "Tunnel key geneve option data is not a multiple of 4 bytes long");
		return -ERANGE;
	}

	opt_len = sizeof(struct geneve_opt) + data_len;
	if (dst) {
		struct geneve_opt *opt = dst;

		WARN_ON(dst_len < opt_len);

		opt->opt_class =
			nla_get_be16(tb[TCA_TUNNEL_KEY_ENC_OPT_GENEVE_CLASS]);
		opt->type = nla_get_u8(tb[TCA_TUNNEL_KEY_ENC_OPT_GENEVE_TYPE]);
		opt->length = data_len / 4; /* length is in units of 4 bytes */
		opt->r1 = 0;
		opt->r2 = 0;
		opt->r3 = 0;

		memcpy(opt + 1, data, data_len);
	}

	return opt_len;
}

static int tunnel_key_copy_opts(const struct nlattr *nla, u8 *dst,
				int dst_len, struct netlink_ext_ack *extack)
{
	int err, rem, opt_len, len = nla_len(nla), opts_len = 0;
	const struct nlattr *attr, *head = nla_data(nla);

	err = nla_validate(head, len, TCA_TUNNEL_KEY_ENC_OPTS_MAX,
			   enc_opts_policy, extack);
	if (err)
		return err;

	nla_for_each_attr(attr, head, len, rem) {
		switch (nla_type(attr)) {
		case TCA_TUNNEL_KEY_ENC_OPTS_GENEVE:
			opt_len = tunnel_key_copy_geneve_opt(attr, dst,
							     dst_len, extack);
			if (opt_len < 0)
				return opt_len;
			opts_len += opt_len;
			if (dst) {
				dst_len -= opt_len;
				dst += opt_len;
			}
			break;
		}
	}

	if (!opts_len) {
		NL_SET_ERR_MSG(extack, "Empty list of tunnel options");
		return -EINVAL;
	}

	if (rem > 0) {
		NL_SET_ERR_MSG(extack, "Trailing data after parsing tunnel key options attributes");
		return -EINVAL;
	}

	return opts_len;
}

static int tunnel_key_get_opts_len(struct nlattr *nla,
				   struct netlink_ext_ack *extack)
{
	return tunnel_key_copy_opts(nla, NULL, 0, extack);
}

static int tunnel_key_opts_set(struct nlattr *nla, struct ip_tunnel_info *info,
			       int opts_len, struct netlink_ext_ack *extack)
{
	info->options_len = opts_len;
	switch (nla_type(nla_data(nla))) {
	case TCA_TUNNEL_KEY_ENC_OPTS_GENEVE:
#if IS_ENABLED(CONFIG_INET)
		info->key.tun_flags |= TUNNEL_GENEVE_OPT;
		return tunnel_key_copy_opts(nla, ip_tunnel_info_opts(info),
					    opts_len, extack);
#else
		return -EAFNOSUPPORT;
#endif
	default:
		NL_SET_ERR_MSG(extack, "Cannot set tunnel options for unknown tunnel type");
		return -EINVAL;
	}
}

static const struct nla_policy tunnel_key_policy[TCA_TUNNEL_KEY_MAX + 1] = {
	[TCA_TUNNEL_KEY_PARMS]	    = { .len = sizeof(struct tc_tunnel_key) },
	[TCA_TUNNEL_KEY_ENC_IPV4_SRC] = { .type = NLA_U32 },
	[TCA_TUNNEL_KEY_ENC_IPV4_DST] = { .type = NLA_U32 },
	[TCA_TUNNEL_KEY_ENC_IPV6_SRC] = { .len = sizeof(struct in6_addr) },
	[TCA_TUNNEL_KEY_ENC_IPV6_DST] = { .len = sizeof(struct in6_addr) },
	[TCA_TUNNEL_KEY_ENC_KEY_ID]   = { .type = NLA_U32 },
	[TCA_TUNNEL_KEY_ENC_DST_PORT] = {.type = NLA_U16},
	[TCA_TUNNEL_KEY_NO_CSUM]      = { .type = NLA_U8 },
	[TCA_TUNNEL_KEY_ENC_OPTS]     = { .type = NLA_NESTED },
	[TCA_TUNNEL_KEY_ENC_TOS]      = { .type = NLA_U8 },
	[TCA_TUNNEL_KEY_ENC_TTL]      = { .type = NLA_U8 },
};

static int tunnel_key_init(struct net *net, struct nlattr *nla,
			   struct nlattr *est, struct tc_action **a,
			   int ovr, int bind, bool rtnl_held,
			   struct netlink_ext_ack *extack)
{
	struct tc_action_net *tn = net_generic(net, tunnel_key_net_id);
	struct nlattr *tb[TCA_TUNNEL_KEY_MAX + 1];
	struct tcf_tunnel_key_params *params_old;
	struct tcf_tunnel_key_params *params_new;
	struct metadata_dst *metadata = NULL;
	struct tc_tunnel_key *parm;
	struct tcf_tunnel_key *t;
	bool exists = false;
	__be16 dst_port = 0;
	int opts_len = 0;
	__be64 key_id;
	__be16 flags;
	u8 tos, ttl;
	int ret = 0;
	int err;

	if (!nla) {
		NL_SET_ERR_MSG(extack, "Tunnel requires attributes to be passed");
		return -EINVAL;
	}

	err = nla_parse_nested(tb, TCA_TUNNEL_KEY_MAX, nla, tunnel_key_policy,
			       extack);
	if (err < 0) {
		NL_SET_ERR_MSG(extack, "Failed to parse nested tunnel key attributes");
		return err;
	}

	if (!tb[TCA_TUNNEL_KEY_PARMS]) {
		NL_SET_ERR_MSG(extack, "Missing tunnel key parameters");
		return -EINVAL;
	}

	parm = nla_data(tb[TCA_TUNNEL_KEY_PARMS]);
	err = tcf_idr_check_alloc(tn, &parm->index, a, bind);
	if (err < 0)
		return err;
	exists = err;
	if (exists && bind)
		return 0;

	switch (parm->t_action) {
	case TCA_TUNNEL_KEY_ACT_RELEASE:
		break;
	case TCA_TUNNEL_KEY_ACT_SET:
		if (!tb[TCA_TUNNEL_KEY_ENC_KEY_ID]) {
			NL_SET_ERR_MSG(extack, "Missing tunnel key id");
			ret = -EINVAL;
			goto err_out;
		}

		key_id = key32_to_tunnel_id(nla_get_be32(tb[TCA_TUNNEL_KEY_ENC_KEY_ID]));

		flags = TUNNEL_KEY | TUNNEL_CSUM;
		if (tb[TCA_TUNNEL_KEY_NO_CSUM] &&
		    nla_get_u8(tb[TCA_TUNNEL_KEY_NO_CSUM]))
			flags &= ~TUNNEL_CSUM;

		if (tb[TCA_TUNNEL_KEY_ENC_DST_PORT])
			dst_port = nla_get_be16(tb[TCA_TUNNEL_KEY_ENC_DST_PORT]);

		if (tb[TCA_TUNNEL_KEY_ENC_OPTS]) {
			opts_len = tunnel_key_get_opts_len(tb[TCA_TUNNEL_KEY_ENC_OPTS],
							   extack);
			if (opts_len < 0) {
				ret = opts_len;
				goto err_out;
			}
		}

		tos = 0;
		if (tb[TCA_TUNNEL_KEY_ENC_TOS])
			tos = nla_get_u8(tb[TCA_TUNNEL_KEY_ENC_TOS]);
		ttl = 0;
		if (tb[TCA_TUNNEL_KEY_ENC_TTL])
			ttl = nla_get_u8(tb[TCA_TUNNEL_KEY_ENC_TTL]);

		if (tb[TCA_TUNNEL_KEY_ENC_IPV4_SRC] &&
		    tb[TCA_TUNNEL_KEY_ENC_IPV4_DST]) {
			__be32 saddr;
			__be32 daddr;

			saddr = nla_get_in_addr(tb[TCA_TUNNEL_KEY_ENC_IPV4_SRC]);
			daddr = nla_get_in_addr(tb[TCA_TUNNEL_KEY_ENC_IPV4_DST]);

			metadata = __ip_tun_set_dst(saddr, daddr, tos, ttl,
						    dst_port, flags,
						    key_id, opts_len);
		} else if (tb[TCA_TUNNEL_KEY_ENC_IPV6_SRC] &&
			   tb[TCA_TUNNEL_KEY_ENC_IPV6_DST]) {
			struct in6_addr saddr;
			struct in6_addr daddr;

			saddr = nla_get_in6_addr(tb[TCA_TUNNEL_KEY_ENC_IPV6_SRC]);
			daddr = nla_get_in6_addr(tb[TCA_TUNNEL_KEY_ENC_IPV6_DST]);

			metadata = __ipv6_tun_set_dst(&saddr, &daddr, tos, ttl, dst_port,
						      0, flags,
						      key_id, 0);
		} else {
			NL_SET_ERR_MSG(extack, "Missing either ipv4 or ipv6 src and dst");
			ret = -EINVAL;
			goto err_out;
		}

		if (!metadata) {
			NL_SET_ERR_MSG(extack, "Cannot allocate tunnel metadata dst");
			ret = -ENOMEM;
			goto err_out;
		}

		if (opts_len) {
			ret = tunnel_key_opts_set(tb[TCA_TUNNEL_KEY_ENC_OPTS],
						  &metadata->u.tun_info,
						  opts_len, extack);
			if (ret < 0)
				goto err_out;
		}

		metadata->u.tun_info.mode |= IP_TUNNEL_INFO_TX;
		break;
	default:
		NL_SET_ERR_MSG(extack, "Unknown tunnel key action");
		ret = -EINVAL;
		goto err_out;
	}

	if (!exists) {
		ret = tcf_idr_create(tn, parm->index, est, a,
				     &act_tunnel_key_ops, bind, true);
		if (ret) {
			NL_SET_ERR_MSG(extack, "Cannot create TC IDR");
			goto err_out;
		}

		ret = ACT_P_CREATED;
	} else if (!ovr) {
		tcf_idr_release(*a, bind);
		NL_SET_ERR_MSG(extack, "TC IDR already exists");
		return -EEXIST;
	}

	t = to_tunnel_key(*a);

	ASSERT_RTNL();
	params_new = kzalloc(sizeof(*params_new), GFP_KERNEL);
	if (unlikely(!params_new)) {
		tcf_idr_release(*a, bind);
		NL_SET_ERR_MSG(extack, "Cannot allocate tunnel key parameters");
		return -ENOMEM;
	}

	params_old = rtnl_dereference(t->params);

	t->tcf_action = parm->action;
	params_new->tcft_action = parm->t_action;
	params_new->tcft_enc_metadata = metadata;

	rcu_assign_pointer(t->params, params_new);

	if (params_old)
		kfree_rcu(params_old, rcu);

	if (ret == ACT_P_CREATED)
		tcf_idr_insert(tn, *a);

	return ret;

err_out:
	if (exists)
		tcf_idr_release(*a, bind);
	else
		tcf_idr_cleanup(tn, parm->index);
	return ret;
}

static void tunnel_key_release(struct tc_action *a)
{
	struct tcf_tunnel_key *t = to_tunnel_key(a);
	struct tcf_tunnel_key_params *params;

	params = rcu_dereference_protected(t->params, 1);
	if (params) {
		if (params->tcft_action == TCA_TUNNEL_KEY_ACT_SET)
			dst_release(&params->tcft_enc_metadata->dst);

		kfree_rcu(params, rcu);
	}
}

static int tunnel_key_geneve_opts_dump(struct sk_buff *skb,
				       const struct ip_tunnel_info *info)
{
	int len = info->options_len;
	u8 *src = (u8 *)(info + 1);
	struct nlattr *start;

	start = nla_nest_start(skb, TCA_TUNNEL_KEY_ENC_OPTS_GENEVE);
	if (!start)
		return -EMSGSIZE;

	while (len > 0) {
		struct geneve_opt *opt = (struct geneve_opt *)src;

		if (nla_put_be16(skb, TCA_TUNNEL_KEY_ENC_OPT_GENEVE_CLASS,
				 opt->opt_class) ||
		    nla_put_u8(skb, TCA_TUNNEL_KEY_ENC_OPT_GENEVE_TYPE,
			       opt->type) ||
		    nla_put(skb, TCA_TUNNEL_KEY_ENC_OPT_GENEVE_DATA,
			    opt->length * 4, opt + 1))
			return -EMSGSIZE;

		len -= sizeof(struct geneve_opt) + opt->length * 4;
		src += sizeof(struct geneve_opt) + opt->length * 4;
	}

	nla_nest_end(skb, start);
	return 0;
}

static int tunnel_key_opts_dump(struct sk_buff *skb,
				const struct ip_tunnel_info *info)
{
	struct nlattr *start;
	int err;

	if (!info->options_len)
		return 0;

	start = nla_nest_start(skb, TCA_TUNNEL_KEY_ENC_OPTS);
	if (!start)
		return -EMSGSIZE;

	if (info->key.tun_flags & TUNNEL_GENEVE_OPT) {
		err = tunnel_key_geneve_opts_dump(skb, info);
		if (err)
			return err;
	} else {
		return -EINVAL;
	}

	nla_nest_end(skb, start);
	return 0;
}

static int tunnel_key_dump_addresses(struct sk_buff *skb,
				     const struct ip_tunnel_info *info)
{
	unsigned short family = ip_tunnel_info_af(info);

	if (family == AF_INET) {
		__be32 saddr = info->key.u.ipv4.src;
		__be32 daddr = info->key.u.ipv4.dst;

		if (!nla_put_in_addr(skb, TCA_TUNNEL_KEY_ENC_IPV4_SRC, saddr) &&
		    !nla_put_in_addr(skb, TCA_TUNNEL_KEY_ENC_IPV4_DST, daddr))
			return 0;
	}

	if (family == AF_INET6) {
		const struct in6_addr *saddr6 = &info->key.u.ipv6.src;
		const struct in6_addr *daddr6 = &info->key.u.ipv6.dst;

		if (!nla_put_in6_addr(skb,
				      TCA_TUNNEL_KEY_ENC_IPV6_SRC, saddr6) &&
		    !nla_put_in6_addr(skb,
				      TCA_TUNNEL_KEY_ENC_IPV6_DST, daddr6))
			return 0;
	}

	return -EINVAL;
}

static int tunnel_key_dump(struct sk_buff *skb, struct tc_action *a,
			   int bind, int ref)
{
	unsigned char *b = skb_tail_pointer(skb);
	struct tcf_tunnel_key *t = to_tunnel_key(a);
	struct tcf_tunnel_key_params *params;
	struct tc_tunnel_key opt = {
		.index    = t->tcf_index,
		.refcnt   = refcount_read(&t->tcf_refcnt) - ref,
		.bindcnt  = atomic_read(&t->tcf_bindcnt) - bind,
		.action   = t->tcf_action,
	};
	struct tcf_t tm;

	params = rtnl_dereference(t->params);

	opt.t_action = params->tcft_action;

	if (nla_put(skb, TCA_TUNNEL_KEY_PARMS, sizeof(opt), &opt))
		goto nla_put_failure;

	if (params->tcft_action == TCA_TUNNEL_KEY_ACT_SET) {
		struct ip_tunnel_info *info =
			&params->tcft_enc_metadata->u.tun_info;
		struct ip_tunnel_key *key = &info->key;
		__be32 key_id = tunnel_id_to_key32(key->tun_id);

		if (nla_put_be32(skb, TCA_TUNNEL_KEY_ENC_KEY_ID, key_id) ||
		    tunnel_key_dump_addresses(skb,
					      &params->tcft_enc_metadata->u.tun_info) ||
		    nla_put_be16(skb, TCA_TUNNEL_KEY_ENC_DST_PORT, key->tp_dst) ||
		    nla_put_u8(skb, TCA_TUNNEL_KEY_NO_CSUM,
			       !(key->tun_flags & TUNNEL_CSUM)) ||
		    tunnel_key_opts_dump(skb, info))
			goto nla_put_failure;

		if (key->tos && nla_put_u8(skb, TCA_TUNNEL_KEY_ENC_TOS, key->tos))
			goto nla_put_failure;

		if (key->ttl && nla_put_u8(skb, TCA_TUNNEL_KEY_ENC_TTL, key->ttl))
			goto nla_put_failure;
	}

	tcf_tm_dump(&tm, &t->tcf_tm);
	if (nla_put_64bit(skb, TCA_TUNNEL_KEY_TM, sizeof(tm),
			  &tm, TCA_TUNNEL_KEY_PAD))
		goto nla_put_failure;

	return skb->len;

nla_put_failure:
	nlmsg_trim(skb, b);
	return -1;
}

static int tunnel_key_walker(struct net *net, struct sk_buff *skb,
			     struct netlink_callback *cb, int type,
			     const struct tc_action_ops *ops,
			     struct netlink_ext_ack *extack)
{
	struct tc_action_net *tn = net_generic(net, tunnel_key_net_id);

	return tcf_generic_walker(tn, skb, cb, type, ops, extack);
}

static int tunnel_key_search(struct net *net, struct tc_action **a, u32 index,
			     struct netlink_ext_ack *extack)
{
	struct tc_action_net *tn = net_generic(net, tunnel_key_net_id);

	return tcf_idr_search(tn, a, index);
}

static int tunnel_key_delete(struct net *net, u32 index)
{
	struct tc_action_net *tn = net_generic(net, tunnel_key_net_id);

	return tcf_idr_delete_index(tn, index);
}

static struct tc_action_ops act_tunnel_key_ops = {
	.kind		=	"tunnel_key",
	.type		=	TCA_ACT_TUNNEL_KEY,
	.owner		=	THIS_MODULE,
	.act		=	tunnel_key_act,
	.dump		=	tunnel_key_dump,
	.init		=	tunnel_key_init,
	.cleanup	=	tunnel_key_release,
	.walk		=	tunnel_key_walker,
	.lookup		=	tunnel_key_search,
	.delete		=	tunnel_key_delete,
	.size		=	sizeof(struct tcf_tunnel_key),
};

static __net_init int tunnel_key_init_net(struct net *net)
{
	struct tc_action_net *tn = net_generic(net, tunnel_key_net_id);

	return tc_action_net_init(tn, &act_tunnel_key_ops);
}

static void __net_exit tunnel_key_exit_net(struct list_head *net_list)
{
	tc_action_net_exit(net_list, tunnel_key_net_id);
}

static struct pernet_operations tunnel_key_net_ops = {
	.init = tunnel_key_init_net,
	.exit_batch = tunnel_key_exit_net,
	.id   = &tunnel_key_net_id,
	.size = sizeof(struct tc_action_net),
};

static int __init tunnel_key_init_module(void)
{
	return tcf_register_action(&act_tunnel_key_ops, &tunnel_key_net_ops);
}

static void __exit tunnel_key_cleanup_module(void)
{
	tcf_unregister_action(&act_tunnel_key_ops, &tunnel_key_net_ops);
}

module_init(tunnel_key_init_module);
module_exit(tunnel_key_cleanup_module);

MODULE_AUTHOR("Amir Vadai <amir@vadai.me>");
MODULE_DESCRIPTION("ip tunnel manipulation actions");
MODULE_LICENSE("GPL v2");
