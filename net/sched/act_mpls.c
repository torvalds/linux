// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2019 Netronome Systems, Inc. */

#include <linux/if_arp.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mpls.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <linux/tc_act/tc_mpls.h>
#include <net/mpls.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <net/pkt_cls.h>
#include <net/tc_act/tc_mpls.h>

static unsigned int mpls_net_id;
static struct tc_action_ops act_mpls_ops;

#define ACT_MPLS_TTL_DEFAULT	255

static __be32 tcf_mpls_get_lse(struct mpls_shim_hdr *lse,
			       struct tcf_mpls_params *p, bool set_bos)
{
	u32 new_lse = 0;

	if (lse)
		new_lse = be32_to_cpu(lse->label_stack_entry);

	if (p->tcfm_label != ACT_MPLS_LABEL_NOT_SET) {
		new_lse &= ~MPLS_LS_LABEL_MASK;
		new_lse |= p->tcfm_label << MPLS_LS_LABEL_SHIFT;
	}
	if (p->tcfm_ttl) {
		new_lse &= ~MPLS_LS_TTL_MASK;
		new_lse |= p->tcfm_ttl << MPLS_LS_TTL_SHIFT;
	}
	if (p->tcfm_tc != ACT_MPLS_TC_NOT_SET) {
		new_lse &= ~MPLS_LS_TC_MASK;
		new_lse |= p->tcfm_tc << MPLS_LS_TC_SHIFT;
	}
	if (p->tcfm_bos != ACT_MPLS_BOS_NOT_SET) {
		new_lse &= ~MPLS_LS_S_MASK;
		new_lse |= p->tcfm_bos << MPLS_LS_S_SHIFT;
	} else if (set_bos) {
		new_lse |= 1 << MPLS_LS_S_SHIFT;
	}

	return cpu_to_be32(new_lse);
}

static int tcf_mpls_act(struct sk_buff *skb, const struct tc_action *a,
			struct tcf_result *res)
{
	struct tcf_mpls *m = to_mpls(a);
	struct tcf_mpls_params *p;
	__be32 new_lse;
	int ret, mac_len;

	tcf_lastuse_update(&m->tcf_tm);
	bstats_cpu_update(this_cpu_ptr(m->common.cpu_bstats), skb);

	/* Ensure 'data' points at mac_header prior calling mpls manipulating
	 * functions.
	 */
	if (skb_at_tc_ingress(skb)) {
		skb_push_rcsum(skb, skb->mac_len);
		mac_len = skb->mac_len;
	} else {
		mac_len = skb_network_header(skb) - skb_mac_header(skb);
	}

	ret = READ_ONCE(m->tcf_action);

	p = rcu_dereference_bh(m->mpls_p);

	switch (p->tcfm_action) {
	case TCA_MPLS_ACT_POP:
		if (skb_mpls_pop(skb, p->tcfm_proto, mac_len,
				 skb->dev && skb->dev->type == ARPHRD_ETHER))
			goto drop;
		break;
	case TCA_MPLS_ACT_PUSH:
		new_lse = tcf_mpls_get_lse(NULL, p, !eth_p_mpls(skb_protocol(skb, true)));
		if (skb_mpls_push(skb, new_lse, p->tcfm_proto, mac_len,
				  skb->dev && skb->dev->type == ARPHRD_ETHER))
			goto drop;
		break;
	case TCA_MPLS_ACT_MAC_PUSH:
		if (skb_vlan_tag_present(skb)) {
			if (__vlan_insert_inner_tag(skb, skb->vlan_proto,
						    skb_vlan_tag_get(skb),
						    ETH_HLEN) < 0)
				goto drop;

			skb->protocol = skb->vlan_proto;
			__vlan_hwaccel_clear_tag(skb);
		}

		new_lse = tcf_mpls_get_lse(NULL, p, mac_len ||
					   !eth_p_mpls(skb->protocol));

		if (skb_mpls_push(skb, new_lse, p->tcfm_proto, 0, false))
			goto drop;
		break;
	case TCA_MPLS_ACT_MODIFY:
		if (!pskb_may_pull(skb,
				   skb_network_offset(skb) + MPLS_HLEN))
			goto drop;
		new_lse = tcf_mpls_get_lse(mpls_hdr(skb), p, false);
		if (skb_mpls_update_lse(skb, new_lse))
			goto drop;
		break;
	case TCA_MPLS_ACT_DEC_TTL:
		if (skb_mpls_dec_ttl(skb))
			goto drop;
		break;
	}

	if (skb_at_tc_ingress(skb))
		skb_pull_rcsum(skb, skb->mac_len);

	return ret;

drop:
	qstats_drop_inc(this_cpu_ptr(m->common.cpu_qstats));
	return TC_ACT_SHOT;
}

static int valid_label(const struct nlattr *attr,
		       struct netlink_ext_ack *extack)
{
	const u32 *label = nla_data(attr);

	if (nla_len(attr) != sizeof(*label)) {
		NL_SET_ERR_MSG_MOD(extack, "Invalid MPLS label length");
		return -EINVAL;
	}

	if (*label & ~MPLS_LABEL_MASK || *label == MPLS_LABEL_IMPLNULL) {
		NL_SET_ERR_MSG_MOD(extack, "MPLS label out of range");
		return -EINVAL;
	}

	return 0;
}

static const struct nla_policy mpls_policy[TCA_MPLS_MAX + 1] = {
	[TCA_MPLS_PARMS]	= NLA_POLICY_EXACT_LEN(sizeof(struct tc_mpls)),
	[TCA_MPLS_PROTO]	= { .type = NLA_U16 },
	[TCA_MPLS_LABEL]	= NLA_POLICY_VALIDATE_FN(NLA_BINARY,
							 valid_label),
	[TCA_MPLS_TC]		= NLA_POLICY_RANGE(NLA_U8, 0, 7),
	[TCA_MPLS_TTL]		= NLA_POLICY_MIN(NLA_U8, 1),
	[TCA_MPLS_BOS]		= NLA_POLICY_RANGE(NLA_U8, 0, 1),
};

static int tcf_mpls_init(struct net *net, struct nlattr *nla,
			 struct nlattr *est, struct tc_action **a,
			 int ovr, int bind, bool rtnl_held,
			 struct tcf_proto *tp, u32 flags,
			 struct netlink_ext_ack *extack)
{
	struct tc_action_net *tn = net_generic(net, mpls_net_id);
	struct nlattr *tb[TCA_MPLS_MAX + 1];
	struct tcf_chain *goto_ch = NULL;
	struct tcf_mpls_params *p;
	struct tc_mpls *parm;
	bool exists = false;
	struct tcf_mpls *m;
	int ret = 0, err;
	u8 mpls_ttl = 0;
	u32 index;

	if (!nla) {
		NL_SET_ERR_MSG_MOD(extack, "Missing netlink attributes");
		return -EINVAL;
	}

	err = nla_parse_nested(tb, TCA_MPLS_MAX, nla, mpls_policy, extack);
	if (err < 0)
		return err;

	if (!tb[TCA_MPLS_PARMS]) {
		NL_SET_ERR_MSG_MOD(extack, "No MPLS params");
		return -EINVAL;
	}
	parm = nla_data(tb[TCA_MPLS_PARMS]);
	index = parm->index;

	/* Verify parameters against action type. */
	switch (parm->m_action) {
	case TCA_MPLS_ACT_POP:
		if (!tb[TCA_MPLS_PROTO]) {
			NL_SET_ERR_MSG_MOD(extack, "Protocol must be set for MPLS pop");
			return -EINVAL;
		}
		if (!eth_proto_is_802_3(nla_get_be16(tb[TCA_MPLS_PROTO]))) {
			NL_SET_ERR_MSG_MOD(extack, "Invalid protocol type for MPLS pop");
			return -EINVAL;
		}
		if (tb[TCA_MPLS_LABEL] || tb[TCA_MPLS_TTL] || tb[TCA_MPLS_TC] ||
		    tb[TCA_MPLS_BOS]) {
			NL_SET_ERR_MSG_MOD(extack, "Label, TTL, TC or BOS cannot be used with MPLS pop");
			return -EINVAL;
		}
		break;
	case TCA_MPLS_ACT_DEC_TTL:
		if (tb[TCA_MPLS_PROTO] || tb[TCA_MPLS_LABEL] ||
		    tb[TCA_MPLS_TTL] || tb[TCA_MPLS_TC] || tb[TCA_MPLS_BOS]) {
			NL_SET_ERR_MSG_MOD(extack, "Label, TTL, TC, BOS or protocol cannot be used with MPLS dec_ttl");
			return -EINVAL;
		}
		break;
	case TCA_MPLS_ACT_PUSH:
	case TCA_MPLS_ACT_MAC_PUSH:
		if (!tb[TCA_MPLS_LABEL]) {
			NL_SET_ERR_MSG_MOD(extack, "Label is required for MPLS push");
			return -EINVAL;
		}
		if (tb[TCA_MPLS_PROTO] &&
		    !eth_p_mpls(nla_get_be16(tb[TCA_MPLS_PROTO]))) {
			NL_SET_ERR_MSG_MOD(extack, "Protocol must be an MPLS type for MPLS push");
			return -EPROTONOSUPPORT;
		}
		/* Push needs a TTL - if not specified, set a default value. */
		if (!tb[TCA_MPLS_TTL]) {
#if IS_ENABLED(CONFIG_MPLS)
			mpls_ttl = net->mpls.default_ttl ?
				   net->mpls.default_ttl : ACT_MPLS_TTL_DEFAULT;
#else
			mpls_ttl = ACT_MPLS_TTL_DEFAULT;
#endif
		}
		break;
	case TCA_MPLS_ACT_MODIFY:
		if (tb[TCA_MPLS_PROTO]) {
			NL_SET_ERR_MSG_MOD(extack, "Protocol cannot be used with MPLS modify");
			return -EINVAL;
		}
		break;
	default:
		NL_SET_ERR_MSG_MOD(extack, "Unknown MPLS action");
		return -EINVAL;
	}

	err = tcf_idr_check_alloc(tn, &index, a, bind);
	if (err < 0)
		return err;
	exists = err;
	if (exists && bind)
		return 0;

	if (!exists) {
		ret = tcf_idr_create(tn, index, est, a,
				     &act_mpls_ops, bind, true, flags);
		if (ret) {
			tcf_idr_cleanup(tn, index);
			return ret;
		}

		ret = ACT_P_CREATED;
	} else if (!ovr) {
		tcf_idr_release(*a, bind);
		return -EEXIST;
	}

	err = tcf_action_check_ctrlact(parm->action, tp, &goto_ch, extack);
	if (err < 0)
		goto release_idr;

	m = to_mpls(*a);

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p) {
		err = -ENOMEM;
		goto put_chain;
	}

	p->tcfm_action = parm->m_action;
	p->tcfm_label = tb[TCA_MPLS_LABEL] ? nla_get_u32(tb[TCA_MPLS_LABEL]) :
					     ACT_MPLS_LABEL_NOT_SET;
	p->tcfm_tc = tb[TCA_MPLS_TC] ? nla_get_u8(tb[TCA_MPLS_TC]) :
				       ACT_MPLS_TC_NOT_SET;
	p->tcfm_ttl = tb[TCA_MPLS_TTL] ? nla_get_u8(tb[TCA_MPLS_TTL]) :
					 mpls_ttl;
	p->tcfm_bos = tb[TCA_MPLS_BOS] ? nla_get_u8(tb[TCA_MPLS_BOS]) :
					 ACT_MPLS_BOS_NOT_SET;
	p->tcfm_proto = tb[TCA_MPLS_PROTO] ? nla_get_be16(tb[TCA_MPLS_PROTO]) :
					     htons(ETH_P_MPLS_UC);

	spin_lock_bh(&m->tcf_lock);
	goto_ch = tcf_action_set_ctrlact(*a, parm->action, goto_ch);
	p = rcu_replace_pointer(m->mpls_p, p, lockdep_is_held(&m->tcf_lock));
	spin_unlock_bh(&m->tcf_lock);

	if (goto_ch)
		tcf_chain_put_by_act(goto_ch);
	if (p)
		kfree_rcu(p, rcu);

	return ret;
put_chain:
	if (goto_ch)
		tcf_chain_put_by_act(goto_ch);
release_idr:
	tcf_idr_release(*a, bind);
	return err;
}

static void tcf_mpls_cleanup(struct tc_action *a)
{
	struct tcf_mpls *m = to_mpls(a);
	struct tcf_mpls_params *p;

	p = rcu_dereference_protected(m->mpls_p, 1);
	if (p)
		kfree_rcu(p, rcu);
}

static int tcf_mpls_dump(struct sk_buff *skb, struct tc_action *a,
			 int bind, int ref)
{
	unsigned char *b = skb_tail_pointer(skb);
	struct tcf_mpls *m = to_mpls(a);
	struct tcf_mpls_params *p;
	struct tc_mpls opt = {
		.index    = m->tcf_index,
		.refcnt   = refcount_read(&m->tcf_refcnt) - ref,
		.bindcnt  = atomic_read(&m->tcf_bindcnt) - bind,
	};
	struct tcf_t t;

	spin_lock_bh(&m->tcf_lock);
	opt.action = m->tcf_action;
	p = rcu_dereference_protected(m->mpls_p, lockdep_is_held(&m->tcf_lock));
	opt.m_action = p->tcfm_action;

	if (nla_put(skb, TCA_MPLS_PARMS, sizeof(opt), &opt))
		goto nla_put_failure;

	if (p->tcfm_label != ACT_MPLS_LABEL_NOT_SET &&
	    nla_put_u32(skb, TCA_MPLS_LABEL, p->tcfm_label))
		goto nla_put_failure;

	if (p->tcfm_tc != ACT_MPLS_TC_NOT_SET &&
	    nla_put_u8(skb, TCA_MPLS_TC, p->tcfm_tc))
		goto nla_put_failure;

	if (p->tcfm_ttl && nla_put_u8(skb, TCA_MPLS_TTL, p->tcfm_ttl))
		goto nla_put_failure;

	if (p->tcfm_bos != ACT_MPLS_BOS_NOT_SET &&
	    nla_put_u8(skb, TCA_MPLS_BOS, p->tcfm_bos))
		goto nla_put_failure;

	if (nla_put_be16(skb, TCA_MPLS_PROTO, p->tcfm_proto))
		goto nla_put_failure;

	tcf_tm_dump(&t, &m->tcf_tm);

	if (nla_put_64bit(skb, TCA_MPLS_TM, sizeof(t), &t, TCA_MPLS_PAD))
		goto nla_put_failure;

	spin_unlock_bh(&m->tcf_lock);

	return skb->len;

nla_put_failure:
	spin_unlock_bh(&m->tcf_lock);
	nlmsg_trim(skb, b);
	return -EMSGSIZE;
}

static int tcf_mpls_walker(struct net *net, struct sk_buff *skb,
			   struct netlink_callback *cb, int type,
			   const struct tc_action_ops *ops,
			   struct netlink_ext_ack *extack)
{
	struct tc_action_net *tn = net_generic(net, mpls_net_id);

	return tcf_generic_walker(tn, skb, cb, type, ops, extack);
}

static int tcf_mpls_search(struct net *net, struct tc_action **a, u32 index)
{
	struct tc_action_net *tn = net_generic(net, mpls_net_id);

	return tcf_idr_search(tn, a, index);
}

static struct tc_action_ops act_mpls_ops = {
	.kind		=	"mpls",
	.id		=	TCA_ID_MPLS,
	.owner		=	THIS_MODULE,
	.act		=	tcf_mpls_act,
	.dump		=	tcf_mpls_dump,
	.init		=	tcf_mpls_init,
	.cleanup	=	tcf_mpls_cleanup,
	.walk		=	tcf_mpls_walker,
	.lookup		=	tcf_mpls_search,
	.size		=	sizeof(struct tcf_mpls),
};

static __net_init int mpls_init_net(struct net *net)
{
	struct tc_action_net *tn = net_generic(net, mpls_net_id);

	return tc_action_net_init(net, tn, &act_mpls_ops);
}

static void __net_exit mpls_exit_net(struct list_head *net_list)
{
	tc_action_net_exit(net_list, mpls_net_id);
}

static struct pernet_operations mpls_net_ops = {
	.init = mpls_init_net,
	.exit_batch = mpls_exit_net,
	.id   = &mpls_net_id,
	.size = sizeof(struct tc_action_net),
};

static int __init mpls_init_module(void)
{
	return tcf_register_action(&act_mpls_ops, &mpls_net_ops);
}

static void __exit mpls_cleanup_module(void)
{
	tcf_unregister_action(&act_mpls_ops, &mpls_net_ops);
}

module_init(mpls_init_module);
module_exit(mpls_cleanup_module);

MODULE_SOFTDEP("post: mpls_gso");
MODULE_AUTHOR("Netronome Systems <oss-drivers@netronome.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MPLS manipulation actions");
