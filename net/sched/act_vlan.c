/*
 * Copyright (c) 2014 Jiri Pirko <jiri@resnulli.us>
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
#include <linux/if_vlan.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>

#include <linux/tc_act/tc_vlan.h>
#include <net/tc_act/tc_vlan.h>

static unsigned int vlan_net_id;
static struct tc_action_ops act_vlan_ops;

static int tcf_vlan(struct sk_buff *skb, const struct tc_action *a,
		    struct tcf_result *res)
{
	struct tcf_vlan *v = to_vlan(a);
	int action;
	int err;
	u16 tci;

	spin_lock(&v->tcf_lock);
	tcf_lastuse_update(&v->tcf_tm);
	bstats_update(&v->tcf_bstats, skb);
	action = v->tcf_action;

	/* Ensure 'data' points at mac_header prior calling vlan manipulating
	 * functions.
	 */
	if (skb_at_tc_ingress(skb))
		skb_push_rcsum(skb, skb->mac_len);

	switch (v->tcfv_action) {
	case TCA_VLAN_ACT_POP:
		err = skb_vlan_pop(skb);
		if (err)
			goto drop;
		break;
	case TCA_VLAN_ACT_PUSH:
		err = skb_vlan_push(skb, v->tcfv_push_proto, v->tcfv_push_vid |
				    (v->tcfv_push_prio << VLAN_PRIO_SHIFT));
		if (err)
			goto drop;
		break;
	case TCA_VLAN_ACT_MODIFY:
		/* No-op if no vlan tag (either hw-accel or in-payload) */
		if (!skb_vlan_tagged(skb))
			goto unlock;
		/* extract existing tag (and guarantee no hw-accel tag) */
		if (skb_vlan_tag_present(skb)) {
			tci = skb_vlan_tag_get(skb);
			skb->vlan_tci = 0;
		} else {
			/* in-payload vlan tag, pop it */
			err = __skb_vlan_pop(skb, &tci);
			if (err)
				goto drop;
		}
		/* replace the vid */
		tci = (tci & ~VLAN_VID_MASK) | v->tcfv_push_vid;
		/* replace prio bits, if tcfv_push_prio specified */
		if (v->tcfv_push_prio) {
			tci &= ~VLAN_PRIO_MASK;
			tci |= v->tcfv_push_prio << VLAN_PRIO_SHIFT;
		}
		/* put updated tci as hwaccel tag */
		__vlan_hwaccel_put_tag(skb, v->tcfv_push_proto, tci);
		break;
	default:
		BUG();
	}

	goto unlock;

drop:
	action = TC_ACT_SHOT;
	v->tcf_qstats.drops++;
unlock:
	if (skb_at_tc_ingress(skb))
		skb_pull_rcsum(skb, skb->mac_len);

	spin_unlock(&v->tcf_lock);
	return action;
}

static const struct nla_policy vlan_policy[TCA_VLAN_MAX + 1] = {
	[TCA_VLAN_PARMS]		= { .len = sizeof(struct tc_vlan) },
	[TCA_VLAN_PUSH_VLAN_ID]		= { .type = NLA_U16 },
	[TCA_VLAN_PUSH_VLAN_PROTOCOL]	= { .type = NLA_U16 },
	[TCA_VLAN_PUSH_VLAN_PRIORITY]	= { .type = NLA_U8 },
};

static int tcf_vlan_init(struct net *net, struct nlattr *nla,
			 struct nlattr *est, struct tc_action **a,
			 int ovr, int bind)
{
	struct tc_action_net *tn = net_generic(net, vlan_net_id);
	struct nlattr *tb[TCA_VLAN_MAX + 1];
	struct tc_vlan *parm;
	struct tcf_vlan *v;
	int action;
	__be16 push_vid = 0;
	__be16 push_proto = 0;
	u8 push_prio = 0;
	bool exists = false;
	int ret = 0, err;

	if (!nla)
		return -EINVAL;

	err = nla_parse_nested(tb, TCA_VLAN_MAX, nla, vlan_policy, NULL);
	if (err < 0)
		return err;

	if (!tb[TCA_VLAN_PARMS])
		return -EINVAL;
	parm = nla_data(tb[TCA_VLAN_PARMS]);
	exists = tcf_idr_check(tn, parm->index, a, bind);
	if (exists && bind)
		return 0;

	switch (parm->v_action) {
	case TCA_VLAN_ACT_POP:
		break;
	case TCA_VLAN_ACT_PUSH:
	case TCA_VLAN_ACT_MODIFY:
		if (!tb[TCA_VLAN_PUSH_VLAN_ID]) {
			if (exists)
				tcf_idr_release(*a, bind);
			return -EINVAL;
		}
		push_vid = nla_get_u16(tb[TCA_VLAN_PUSH_VLAN_ID]);
		if (push_vid >= VLAN_VID_MASK) {
			if (exists)
				tcf_idr_release(*a, bind);
			return -ERANGE;
		}

		if (tb[TCA_VLAN_PUSH_VLAN_PROTOCOL]) {
			push_proto = nla_get_be16(tb[TCA_VLAN_PUSH_VLAN_PROTOCOL]);
			switch (push_proto) {
			case htons(ETH_P_8021Q):
			case htons(ETH_P_8021AD):
				break;
			default:
				return -EPROTONOSUPPORT;
			}
		} else {
			push_proto = htons(ETH_P_8021Q);
		}

		if (tb[TCA_VLAN_PUSH_VLAN_PRIORITY])
			push_prio = nla_get_u8(tb[TCA_VLAN_PUSH_VLAN_PRIORITY]);
		break;
	default:
		if (exists)
			tcf_idr_release(*a, bind);
		return -EINVAL;
	}
	action = parm->v_action;

	if (!exists) {
		ret = tcf_idr_create(tn, parm->index, est, a,
				     &act_vlan_ops, bind, false);
		if (ret)
			return ret;

		ret = ACT_P_CREATED;
	} else {
		tcf_idr_release(*a, bind);
		if (!ovr)
			return -EEXIST;
	}

	v = to_vlan(*a);

	spin_lock_bh(&v->tcf_lock);

	v->tcfv_action = action;
	v->tcfv_push_vid = push_vid;
	v->tcfv_push_prio = push_prio;
	v->tcfv_push_proto = push_proto;

	v->tcf_action = parm->action;

	spin_unlock_bh(&v->tcf_lock);

	if (ret == ACT_P_CREATED)
		tcf_idr_insert(tn, *a);
	return ret;
}

static int tcf_vlan_dump(struct sk_buff *skb, struct tc_action *a,
			 int bind, int ref)
{
	unsigned char *b = skb_tail_pointer(skb);
	struct tcf_vlan *v = to_vlan(a);
	struct tc_vlan opt = {
		.index    = v->tcf_index,
		.refcnt   = v->tcf_refcnt - ref,
		.bindcnt  = v->tcf_bindcnt - bind,
		.action   = v->tcf_action,
		.v_action = v->tcfv_action,
	};
	struct tcf_t t;

	if (nla_put(skb, TCA_VLAN_PARMS, sizeof(opt), &opt))
		goto nla_put_failure;

	if ((v->tcfv_action == TCA_VLAN_ACT_PUSH ||
	     v->tcfv_action == TCA_VLAN_ACT_MODIFY) &&
	    (nla_put_u16(skb, TCA_VLAN_PUSH_VLAN_ID, v->tcfv_push_vid) ||
	     nla_put_be16(skb, TCA_VLAN_PUSH_VLAN_PROTOCOL,
			  v->tcfv_push_proto) ||
	     (nla_put_u8(skb, TCA_VLAN_PUSH_VLAN_PRIORITY,
					      v->tcfv_push_prio))))
		goto nla_put_failure;

	tcf_tm_dump(&t, &v->tcf_tm);
	if (nla_put_64bit(skb, TCA_VLAN_TM, sizeof(t), &t, TCA_VLAN_PAD))
		goto nla_put_failure;
	return skb->len;

nla_put_failure:
	nlmsg_trim(skb, b);
	return -1;
}

static int tcf_vlan_walker(struct net *net, struct sk_buff *skb,
			   struct netlink_callback *cb, int type,
			   const struct tc_action_ops *ops)
{
	struct tc_action_net *tn = net_generic(net, vlan_net_id);

	return tcf_generic_walker(tn, skb, cb, type, ops);
}

static int tcf_vlan_search(struct net *net, struct tc_action **a, u32 index)
{
	struct tc_action_net *tn = net_generic(net, vlan_net_id);

	return tcf_idr_search(tn, a, index);
}

static struct tc_action_ops act_vlan_ops = {
	.kind		=	"vlan",
	.type		=	TCA_ACT_VLAN,
	.owner		=	THIS_MODULE,
	.act		=	tcf_vlan,
	.dump		=	tcf_vlan_dump,
	.init		=	tcf_vlan_init,
	.walk		=	tcf_vlan_walker,
	.lookup		=	tcf_vlan_search,
	.size		=	sizeof(struct tcf_vlan),
};

static __net_init int vlan_init_net(struct net *net)
{
	struct tc_action_net *tn = net_generic(net, vlan_net_id);

	return tc_action_net_init(tn, &act_vlan_ops, net);
}

static void __net_exit vlan_exit_net(struct net *net)
{
	struct tc_action_net *tn = net_generic(net, vlan_net_id);

	tc_action_net_exit(tn);
}

static struct pernet_operations vlan_net_ops = {
	.init = vlan_init_net,
	.exit = vlan_exit_net,
	.id   = &vlan_net_id,
	.size = sizeof(struct tc_action_net),
};

static int __init vlan_init_module(void)
{
	return tcf_register_action(&act_vlan_ops, &vlan_net_ops);
}

static void __exit vlan_cleanup_module(void)
{
	tcf_unregister_action(&act_vlan_ops, &vlan_net_ops);
}

module_init(vlan_init_module);
module_exit(vlan_cleanup_module);

MODULE_AUTHOR("Jiri Pirko <jiri@resnulli.us>");
MODULE_DESCRIPTION("vlan manipulation actions");
MODULE_LICENSE("GPL v2");
