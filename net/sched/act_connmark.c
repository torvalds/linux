// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * net/sched/act_connmark.c  netfilter connmark retriever action
 * skb mark is over-written
 *
 * Copyright (c) 2011 Felix Fietkau <nbd@openwrt.org>
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/pkt_cls.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <net/act_api.h>
#include <net/pkt_cls.h>
#include <uapi/linux/tc_act/tc_connmark.h>
#include <net/tc_act/tc_connmark.h>
#include <net/tc_wrapper.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_zones.h>

static struct tc_action_ops act_connmark_ops;

TC_INDIRECT_SCOPE int tcf_connmark_act(struct sk_buff *skb,
				       const struct tc_action *a,
				       struct tcf_result *res)
{
	const struct nf_conntrack_tuple_hash *thash;
	struct nf_conntrack_tuple tuple;
	enum ip_conntrack_info ctinfo;
	struct tcf_connmark_info *ca = to_connmark(a);
	struct tcf_connmark_parms *parms;
	struct nf_conntrack_zone zone;
	struct nf_conn *c;
	int proto;

	tcf_lastuse_update(&ca->tcf_tm);
	tcf_action_update_bstats(&ca->common, skb);

	parms = rcu_dereference_bh(ca->parms);

	switch (skb_protocol(skb, true)) {
	case htons(ETH_P_IP):
		if (skb->len < sizeof(struct iphdr))
			goto out;

		proto = NFPROTO_IPV4;
		break;
	case htons(ETH_P_IPV6):
		if (skb->len < sizeof(struct ipv6hdr))
			goto out;

		proto = NFPROTO_IPV6;
		break;
	default:
		goto out;
	}

	c = nf_ct_get(skb, &ctinfo);
	if (c) {
		skb->mark = READ_ONCE(c->mark);
		goto count;
	}

	if (!nf_ct_get_tuplepr(skb, skb_network_offset(skb), proto, parms->net,
			       &tuple))
		goto out;

	zone.id = parms->zone;
	zone.dir = NF_CT_DEFAULT_ZONE_DIR;

	thash = nf_conntrack_find_get(parms->net, &zone, &tuple);
	if (!thash)
		goto out;

	c = nf_ct_tuplehash_to_ctrack(thash);
	skb->mark = READ_ONCE(c->mark);
	nf_ct_put(c);

count:
	/* using overlimits stats to count how many packets marked */
	tcf_action_inc_overlimit_qstats(&ca->common);
out:
	return READ_ONCE(ca->tcf_action);
}

static const struct nla_policy connmark_policy[TCA_CONNMARK_MAX + 1] = {
	[TCA_CONNMARK_PARMS] = { .len = sizeof(struct tc_connmark) },
};

static int tcf_connmark_init(struct net *net, struct nlattr *nla,
			     struct nlattr *est, struct tc_action **a,
			     struct tcf_proto *tp, u32 flags,
			     struct netlink_ext_ack *extack)
{
	struct tc_action_net *tn = net_generic(net, act_connmark_ops.net_id);
	struct tcf_connmark_parms *nparms, *oparms;
	struct nlattr *tb[TCA_CONNMARK_MAX + 1];
	bool bind = flags & TCA_ACT_FLAGS_BIND;
	struct tcf_chain *goto_ch = NULL;
	struct tcf_connmark_info *ci;
	struct tc_connmark *parm;
	int ret = 0, err;
	u32 index;

	if (!nla)
		return -EINVAL;

	ret = nla_parse_nested_deprecated(tb, TCA_CONNMARK_MAX, nla,
					  connmark_policy, NULL);
	if (ret < 0)
		return ret;

	if (!tb[TCA_CONNMARK_PARMS])
		return -EINVAL;

	nparms = kzalloc(sizeof(*nparms), GFP_KERNEL);
	if (!nparms)
		return -ENOMEM;

	parm = nla_data(tb[TCA_CONNMARK_PARMS]);
	index = parm->index;
	ret = tcf_idr_check_alloc(tn, &index, a, bind);
	if (!ret) {
		ret = tcf_idr_create_from_flags(tn, index, est, a,
						&act_connmark_ops, bind, flags);
		if (ret) {
			tcf_idr_cleanup(tn, index);
			err = ret;
			goto out_free;
		}

		ci = to_connmark(*a);

		nparms->net = net;
		nparms->zone = parm->zone;

		ret = ACT_P_CREATED;
	} else if (ret > 0) {
		ci = to_connmark(*a);
		if (bind) {
			err = ACT_P_BOUND;
			goto out_free;
		}
		if (!(flags & TCA_ACT_FLAGS_REPLACE)) {
			err = -EEXIST;
			goto release_idr;
		}

		nparms->net = rtnl_dereference(ci->parms)->net;
		nparms->zone = parm->zone;

		ret = 0;
	} else {
		err = ret;
		goto out_free;
	}

	err = tcf_action_check_ctrlact(parm->action, tp, &goto_ch, extack);
	if (err < 0)
		goto release_idr;

	spin_lock_bh(&ci->tcf_lock);
	goto_ch = tcf_action_set_ctrlact(*a, parm->action, goto_ch);
	oparms = rcu_replace_pointer(ci->parms, nparms, lockdep_is_held(&ci->tcf_lock));
	spin_unlock_bh(&ci->tcf_lock);

	if (goto_ch)
		tcf_chain_put_by_act(goto_ch);

	if (oparms)
		kfree_rcu(oparms, rcu);

	return ret;

release_idr:
	tcf_idr_release(*a, bind);
out_free:
	kfree(nparms);
	return err;
}

static inline int tcf_connmark_dump(struct sk_buff *skb, struct tc_action *a,
				    int bind, int ref)
{
	unsigned char *b = skb_tail_pointer(skb);
	struct tcf_connmark_info *ci = to_connmark(a);
	struct tc_connmark opt = {
		.index   = ci->tcf_index,
		.refcnt  = refcount_read(&ci->tcf_refcnt) - ref,
		.bindcnt = atomic_read(&ci->tcf_bindcnt) - bind,
	};
	struct tcf_connmark_parms *parms;
	struct tcf_t t;

	spin_lock_bh(&ci->tcf_lock);
	parms = rcu_dereference_protected(ci->parms, lockdep_is_held(&ci->tcf_lock));

	opt.action = ci->tcf_action;
	opt.zone = parms->zone;
	if (nla_put(skb, TCA_CONNMARK_PARMS, sizeof(opt), &opt))
		goto nla_put_failure;

	tcf_tm_dump(&t, &ci->tcf_tm);
	if (nla_put_64bit(skb, TCA_CONNMARK_TM, sizeof(t), &t,
			  TCA_CONNMARK_PAD))
		goto nla_put_failure;
	spin_unlock_bh(&ci->tcf_lock);

	return skb->len;

nla_put_failure:
	spin_unlock_bh(&ci->tcf_lock);
	nlmsg_trim(skb, b);
	return -1;
}

static void tcf_connmark_cleanup(struct tc_action *a)
{
	struct tcf_connmark_info *ci = to_connmark(a);
	struct tcf_connmark_parms *parms;

	parms = rcu_dereference_protected(ci->parms, 1);
	if (parms)
		kfree_rcu(parms, rcu);
}

static struct tc_action_ops act_connmark_ops = {
	.kind		=	"connmark",
	.id		=	TCA_ID_CONNMARK,
	.owner		=	THIS_MODULE,
	.act		=	tcf_connmark_act,
	.dump		=	tcf_connmark_dump,
	.init		=	tcf_connmark_init,
	.cleanup	=	tcf_connmark_cleanup,
	.size		=	sizeof(struct tcf_connmark_info),
};
MODULE_ALIAS_NET_ACT("connmark");

static __net_init int connmark_init_net(struct net *net)
{
	struct tc_action_net *tn = net_generic(net, act_connmark_ops.net_id);

	return tc_action_net_init(net, tn, &act_connmark_ops);
}

static void __net_exit connmark_exit_net(struct list_head *net_list)
{
	tc_action_net_exit(net_list, act_connmark_ops.net_id);
}

static struct pernet_operations connmark_net_ops = {
	.init = connmark_init_net,
	.exit_batch = connmark_exit_net,
	.id   = &act_connmark_ops.net_id,
	.size = sizeof(struct tc_action_net),
};

static int __init connmark_init_module(void)
{
	return tcf_register_action(&act_connmark_ops, &connmark_net_ops);
}

static void __exit connmark_cleanup_module(void)
{
	tcf_unregister_action(&act_connmark_ops, &connmark_net_ops);
}

module_init(connmark_init_module);
module_exit(connmark_cleanup_module);
MODULE_AUTHOR("Felix Fietkau <nbd@openwrt.org>");
MODULE_DESCRIPTION("Connection tracking mark restoring");
MODULE_LICENSE("GPL");
