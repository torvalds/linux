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
	action = params->action;

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

static const struct nla_policy tunnel_key_policy[TCA_TUNNEL_KEY_MAX + 1] = {
	[TCA_TUNNEL_KEY_PARMS]	    = { .len = sizeof(struct tc_tunnel_key) },
	[TCA_TUNNEL_KEY_ENC_IPV4_SRC] = { .type = NLA_U32 },
	[TCA_TUNNEL_KEY_ENC_IPV4_DST] = { .type = NLA_U32 },
	[TCA_TUNNEL_KEY_ENC_IPV6_SRC] = { .len = sizeof(struct in6_addr) },
	[TCA_TUNNEL_KEY_ENC_IPV6_DST] = { .len = sizeof(struct in6_addr) },
	[TCA_TUNNEL_KEY_ENC_KEY_ID]   = { .type = NLA_U32 },
	[TCA_TUNNEL_KEY_ENC_DST_PORT] = {.type = NLA_U16},
	[TCA_TUNNEL_KEY_NO_CSUM]      = { .type = NLA_U8 },
};

static int tunnel_key_init(struct net *net, struct nlattr *nla,
			   struct nlattr *est, struct tc_action **a,
			   int ovr, int bind)
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
	__be64 key_id;
	__be16 flags;
	int ret = 0;
	int err;

	if (!nla)
		return -EINVAL;

	err = nla_parse_nested(tb, TCA_TUNNEL_KEY_MAX, nla, tunnel_key_policy,
			       NULL);
	if (err < 0)
		return err;

	if (!tb[TCA_TUNNEL_KEY_PARMS])
		return -EINVAL;

	parm = nla_data(tb[TCA_TUNNEL_KEY_PARMS]);
	exists = tcf_idr_check(tn, parm->index, a, bind);
	if (exists && bind)
		return 0;

	switch (parm->t_action) {
	case TCA_TUNNEL_KEY_ACT_RELEASE:
		break;
	case TCA_TUNNEL_KEY_ACT_SET:
		if (!tb[TCA_TUNNEL_KEY_ENC_KEY_ID]) {
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

		if (tb[TCA_TUNNEL_KEY_ENC_IPV4_SRC] &&
		    tb[TCA_TUNNEL_KEY_ENC_IPV4_DST]) {
			__be32 saddr;
			__be32 daddr;

			saddr = nla_get_in_addr(tb[TCA_TUNNEL_KEY_ENC_IPV4_SRC]);
			daddr = nla_get_in_addr(tb[TCA_TUNNEL_KEY_ENC_IPV4_DST]);

			metadata = __ip_tun_set_dst(saddr, daddr, 0, 0,
						    dst_port, flags,
						    key_id, 0);
		} else if (tb[TCA_TUNNEL_KEY_ENC_IPV6_SRC] &&
			   tb[TCA_TUNNEL_KEY_ENC_IPV6_DST]) {
			struct in6_addr saddr;
			struct in6_addr daddr;

			saddr = nla_get_in6_addr(tb[TCA_TUNNEL_KEY_ENC_IPV6_SRC]);
			daddr = nla_get_in6_addr(tb[TCA_TUNNEL_KEY_ENC_IPV6_DST]);

			metadata = __ipv6_tun_set_dst(&saddr, &daddr, 0, 0, dst_port,
						      0, flags,
						      key_id, 0);
		}

		if (!metadata) {
			ret = -EINVAL;
			goto err_out;
		}

		metadata->u.tun_info.mode |= IP_TUNNEL_INFO_TX;
		break;
	default:
		goto err_out;
	}

	if (!exists) {
		ret = tcf_idr_create(tn, parm->index, est, a,
				     &act_tunnel_key_ops, bind, true);
		if (ret)
			return ret;

		ret = ACT_P_CREATED;
	} else {
		tcf_idr_release(*a, bind);
		if (!ovr)
			return -EEXIST;
	}

	t = to_tunnel_key(*a);

	ASSERT_RTNL();
	params_new = kzalloc(sizeof(*params_new), GFP_KERNEL);
	if (unlikely(!params_new)) {
		if (ret == ACT_P_CREATED)
			tcf_idr_release(*a, bind);
		return -ENOMEM;
	}

	params_old = rtnl_dereference(t->params);

	params_new->action = parm->action;
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
	return ret;
}

static void tunnel_key_release(struct tc_action *a, int bind)
{
	struct tcf_tunnel_key *t = to_tunnel_key(a);
	struct tcf_tunnel_key_params *params;

	params = rcu_dereference_protected(t->params, 1);

	if (params->tcft_action == TCA_TUNNEL_KEY_ACT_SET)
		dst_release(&params->tcft_enc_metadata->dst);

	kfree_rcu(params, rcu);
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
		.refcnt   = t->tcf_refcnt - ref,
		.bindcnt  = t->tcf_bindcnt - bind,
	};
	struct tcf_t tm;

	params = rtnl_dereference(t->params);

	opt.t_action = params->tcft_action;
	opt.action = params->action;

	if (nla_put(skb, TCA_TUNNEL_KEY_PARMS, sizeof(opt), &opt))
		goto nla_put_failure;

	if (params->tcft_action == TCA_TUNNEL_KEY_ACT_SET) {
		struct ip_tunnel_key *key =
			&params->tcft_enc_metadata->u.tun_info.key;
		__be32 key_id = tunnel_id_to_key32(key->tun_id);

		if (nla_put_be32(skb, TCA_TUNNEL_KEY_ENC_KEY_ID, key_id) ||
		    tunnel_key_dump_addresses(skb,
					      &params->tcft_enc_metadata->u.tun_info) ||
		    nla_put_be16(skb, TCA_TUNNEL_KEY_ENC_DST_PORT, key->tp_dst) ||
		    nla_put_u8(skb, TCA_TUNNEL_KEY_NO_CSUM,
			       !(key->tun_flags & TUNNEL_CSUM)))
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
			     const struct tc_action_ops *ops)
{
	struct tc_action_net *tn = net_generic(net, tunnel_key_net_id);

	return tcf_generic_walker(tn, skb, cb, type, ops);
}

static int tunnel_key_search(struct net *net, struct tc_action **a, u32 index)
{
	struct tc_action_net *tn = net_generic(net, tunnel_key_net_id);

	return tcf_idr_search(tn, a, index);
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
	.size		=	sizeof(struct tcf_tunnel_key),
};

static __net_init int tunnel_key_init_net(struct net *net)
{
	struct tc_action_net *tn = net_generic(net, tunnel_key_net_id);

	return tc_action_net_init(tn, &act_tunnel_key_ops, net);
}

static void __net_exit tunnel_key_exit_net(struct net *net)
{
	struct tc_action_net *tn = net_generic(net, tunnel_key_net_id);

	tc_action_net_exit(tn);
}

static struct pernet_operations tunnel_key_net_ops = {
	.init = tunnel_key_init_net,
	.exit = tunnel_key_exit_net,
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
