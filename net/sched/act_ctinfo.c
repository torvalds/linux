// SPDX-License-Identifier: GPL-2.0+
/* net/sched/act_ctinfo.c  netfilter ctinfo connmark actions
 *
 * Copyright (c) 2019 Kevin Darbyshire-Bryant <ldir@darbyshire-bryant.me.uk>
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
#include <uapi/linux/tc_act/tc_ctinfo.h>
#include <net/tc_act/tc_ctinfo.h>
#include <net/tc_wrapper.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_ecache.h>
#include <net/netfilter/nf_conntrack_zones.h>

static struct tc_action_ops act_ctinfo_ops;

static void tcf_ctinfo_dscp_set(struct nf_conn *ct, struct tcf_ctinfo *ca,
				struct tcf_ctinfo_params *cp,
				struct sk_buff *skb, int wlen, int proto)
{
	u8 dscp, newdscp;

	newdscp = (((READ_ONCE(ct->mark) & cp->dscpmask) >> cp->dscpmaskshift) << 2) &
		     ~INET_ECN_MASK;

	switch (proto) {
	case NFPROTO_IPV4:
		dscp = ipv4_get_dsfield(ip_hdr(skb)) & ~INET_ECN_MASK;
		if (dscp != newdscp) {
			if (likely(!skb_try_make_writable(skb, wlen))) {
				ipv4_change_dsfield(ip_hdr(skb),
						    INET_ECN_MASK,
						    newdscp);
				atomic64_inc(&ca->stats_dscp_set);
			} else {
				atomic64_inc(&ca->stats_dscp_error);
			}
		}
		break;
	case NFPROTO_IPV6:
		dscp = ipv6_get_dsfield(ipv6_hdr(skb)) & ~INET_ECN_MASK;
		if (dscp != newdscp) {
			if (likely(!skb_try_make_writable(skb, wlen))) {
				ipv6_change_dsfield(ipv6_hdr(skb),
						    INET_ECN_MASK,
						    newdscp);
				atomic64_inc(&ca->stats_dscp_set);
			} else {
				atomic64_inc(&ca->stats_dscp_error);
			}
		}
		break;
	default:
		break;
	}
}

static void tcf_ctinfo_cpmark_set(struct nf_conn *ct, struct tcf_ctinfo *ca,
				  struct tcf_ctinfo_params *cp,
				  struct sk_buff *skb)
{
	atomic64_inc(&ca->stats_cpmark_set);
	skb->mark = READ_ONCE(ct->mark) & cp->cpmarkmask;
}

TC_INDIRECT_SCOPE int tcf_ctinfo_act(struct sk_buff *skb,
				     const struct tc_action *a,
				     struct tcf_result *res)
{
	const struct nf_conntrack_tuple_hash *thash = NULL;
	struct tcf_ctinfo *ca = to_ctinfo(a);
	struct nf_conntrack_tuple tuple;
	struct nf_conntrack_zone zone;
	enum ip_conntrack_info ctinfo;
	struct tcf_ctinfo_params *cp;
	struct nf_conn *ct;
	int proto, wlen;
	int action;

	cp = rcu_dereference_bh(ca->params);

	tcf_lastuse_update(&ca->tcf_tm);
	tcf_action_update_bstats(&ca->common, skb);
	action = READ_ONCE(ca->tcf_action);

	wlen = skb_network_offset(skb);
	switch (skb_protocol(skb, true)) {
	case htons(ETH_P_IP):
		wlen += sizeof(struct iphdr);
		if (!pskb_may_pull(skb, wlen))
			goto out;

		proto = NFPROTO_IPV4;
		break;
	case htons(ETH_P_IPV6):
		wlen += sizeof(struct ipv6hdr);
		if (!pskb_may_pull(skb, wlen))
			goto out;

		proto = NFPROTO_IPV6;
		break;
	default:
		goto out;
	}

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct) { /* look harder, usually ingress */
		if (!nf_ct_get_tuplepr(skb, skb_network_offset(skb),
				       proto, cp->net, &tuple))
			goto out;
		zone.id = cp->zone;
		zone.dir = NF_CT_DEFAULT_ZONE_DIR;

		thash = nf_conntrack_find_get(cp->net, &zone, &tuple);
		if (!thash)
			goto out;

		ct = nf_ct_tuplehash_to_ctrack(thash);
	}

	if (cp->mode & CTINFO_MODE_DSCP)
		if (!cp->dscpstatemask || (READ_ONCE(ct->mark) & cp->dscpstatemask))
			tcf_ctinfo_dscp_set(ct, ca, cp, skb, wlen, proto);

	if (cp->mode & CTINFO_MODE_CPMARK)
		tcf_ctinfo_cpmark_set(ct, ca, cp, skb);

	if (thash)
		nf_ct_put(ct);
out:
	return action;
}

static const struct nla_policy ctinfo_policy[TCA_CTINFO_MAX + 1] = {
	[TCA_CTINFO_ACT]		  =
		NLA_POLICY_EXACT_LEN(sizeof(struct tc_ctinfo)),
	[TCA_CTINFO_ZONE]		  = { .type = NLA_U16 },
	[TCA_CTINFO_PARMS_DSCP_MASK]	  = { .type = NLA_U32 },
	[TCA_CTINFO_PARMS_DSCP_STATEMASK] = { .type = NLA_U32 },
	[TCA_CTINFO_PARMS_CPMARK_MASK]	  = { .type = NLA_U32 },
};

static int tcf_ctinfo_init(struct net *net, struct nlattr *nla,
			   struct nlattr *est, struct tc_action **a,
			   struct tcf_proto *tp, u32 flags,
			   struct netlink_ext_ack *extack)
{
	struct tc_action_net *tn = net_generic(net, act_ctinfo_ops.net_id);
	bool bind = flags & TCA_ACT_FLAGS_BIND;
	u32 dscpmask = 0, dscpstatemask, index;
	struct nlattr *tb[TCA_CTINFO_MAX + 1];
	struct tcf_ctinfo_params *cp_new;
	struct tcf_chain *goto_ch = NULL;
	struct tc_ctinfo *actparm;
	struct tcf_ctinfo *ci;
	u8 dscpmaskshift;
	int ret = 0, err;

	if (!nla) {
		NL_SET_ERR_MSG_MOD(extack, "ctinfo requires attributes to be passed");
		return -EINVAL;
	}

	err = nla_parse_nested(tb, TCA_CTINFO_MAX, nla, ctinfo_policy, extack);
	if (err < 0)
		return err;

	if (!tb[TCA_CTINFO_ACT]) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Missing required TCA_CTINFO_ACT attribute");
		return -EINVAL;
	}
	actparm = nla_data(tb[TCA_CTINFO_ACT]);

	/* do some basic validation here before dynamically allocating things */
	/* that we would otherwise have to clean up.			      */
	if (tb[TCA_CTINFO_PARMS_DSCP_MASK]) {
		dscpmask = nla_get_u32(tb[TCA_CTINFO_PARMS_DSCP_MASK]);
		/* need contiguous 6 bit mask */
		dscpmaskshift = dscpmask ? __ffs(dscpmask) : 0;
		if ((~0 & (dscpmask >> dscpmaskshift)) != 0x3f) {
			NL_SET_ERR_MSG_ATTR(extack,
					    tb[TCA_CTINFO_PARMS_DSCP_MASK],
					    "dscp mask must be 6 contiguous bits");
			return -EINVAL;
		}
		dscpstatemask =
			nla_get_u32_default(tb[TCA_CTINFO_PARMS_DSCP_STATEMASK],
					    0);
		/* mask & statemask must not overlap */
		if (dscpmask & dscpstatemask) {
			NL_SET_ERR_MSG_ATTR(extack,
					    tb[TCA_CTINFO_PARMS_DSCP_STATEMASK],
					    "dscp statemask must not overlap dscp mask");
			return -EINVAL;
		}
	}

	/* done the validation:now to the actual action allocation */
	index = actparm->index;
	err = tcf_idr_check_alloc(tn, &index, a, bind);
	if (!err) {
		ret = tcf_idr_create_from_flags(tn, index, est, a,
						&act_ctinfo_ops, bind, flags);
		if (ret) {
			tcf_idr_cleanup(tn, index);
			return ret;
		}
		ret = ACT_P_CREATED;
	} else if (err > 0) {
		if (bind) /* don't override defaults */
			return ACT_P_BOUND;
		if (!(flags & TCA_ACT_FLAGS_REPLACE)) {
			tcf_idr_release(*a, bind);
			return -EEXIST;
		}
	} else {
		return err;
	}

	err = tcf_action_check_ctrlact(actparm->action, tp, &goto_ch, extack);
	if (err < 0)
		goto release_idr;

	ci = to_ctinfo(*a);

	cp_new = kzalloc(sizeof(*cp_new), GFP_KERNEL);
	if (unlikely(!cp_new)) {
		err = -ENOMEM;
		goto put_chain;
	}

	cp_new->net = net;
	cp_new->zone = nla_get_u16_default(tb[TCA_CTINFO_ZONE], 0);
	if (dscpmask) {
		cp_new->dscpmask = dscpmask;
		cp_new->dscpmaskshift = dscpmaskshift;
		cp_new->dscpstatemask = dscpstatemask;
		cp_new->mode |= CTINFO_MODE_DSCP;
	}

	if (tb[TCA_CTINFO_PARMS_CPMARK_MASK]) {
		cp_new->cpmarkmask =
				nla_get_u32(tb[TCA_CTINFO_PARMS_CPMARK_MASK]);
		cp_new->mode |= CTINFO_MODE_CPMARK;
	}

	spin_lock_bh(&ci->tcf_lock);
	goto_ch = tcf_action_set_ctrlact(*a, actparm->action, goto_ch);
	cp_new = rcu_replace_pointer(ci->params, cp_new,
				     lockdep_is_held(&ci->tcf_lock));
	spin_unlock_bh(&ci->tcf_lock);

	if (goto_ch)
		tcf_chain_put_by_act(goto_ch);
	if (cp_new)
		kfree_rcu(cp_new, rcu);

	return ret;

put_chain:
	if (goto_ch)
		tcf_chain_put_by_act(goto_ch);
release_idr:
	tcf_idr_release(*a, bind);
	return err;
}

static int tcf_ctinfo_dump(struct sk_buff *skb, struct tc_action *a,
			   int bind, int ref)
{
	struct tcf_ctinfo *ci = to_ctinfo(a);
	struct tc_ctinfo opt = {
		.index   = ci->tcf_index,
		.refcnt  = refcount_read(&ci->tcf_refcnt) - ref,
		.bindcnt = atomic_read(&ci->tcf_bindcnt) - bind,
	};
	unsigned char *b = skb_tail_pointer(skb);
	struct tcf_ctinfo_params *cp;
	struct tcf_t t;

	spin_lock_bh(&ci->tcf_lock);
	cp = rcu_dereference_protected(ci->params,
				       lockdep_is_held(&ci->tcf_lock));

	tcf_tm_dump(&t, &ci->tcf_tm);
	if (nla_put_64bit(skb, TCA_CTINFO_TM, sizeof(t), &t, TCA_CTINFO_PAD))
		goto nla_put_failure;

	opt.action = ci->tcf_action;
	if (nla_put(skb, TCA_CTINFO_ACT, sizeof(opt), &opt))
		goto nla_put_failure;

	if (nla_put_u16(skb, TCA_CTINFO_ZONE, cp->zone))
		goto nla_put_failure;

	if (cp->mode & CTINFO_MODE_DSCP) {
		if (nla_put_u32(skb, TCA_CTINFO_PARMS_DSCP_MASK,
				cp->dscpmask))
			goto nla_put_failure;
		if (nla_put_u32(skb, TCA_CTINFO_PARMS_DSCP_STATEMASK,
				cp->dscpstatemask))
			goto nla_put_failure;
	}

	if (cp->mode & CTINFO_MODE_CPMARK) {
		if (nla_put_u32(skb, TCA_CTINFO_PARMS_CPMARK_MASK,
				cp->cpmarkmask))
			goto nla_put_failure;
	}

	if (nla_put_u64_64bit(skb, TCA_CTINFO_STATS_DSCP_SET,
			      atomic64_read(&ci->stats_dscp_set),
			      TCA_CTINFO_PAD))
		goto nla_put_failure;

	if (nla_put_u64_64bit(skb, TCA_CTINFO_STATS_DSCP_ERROR,
			      atomic64_read(&ci->stats_dscp_error),
			      TCA_CTINFO_PAD))
		goto nla_put_failure;

	if (nla_put_u64_64bit(skb, TCA_CTINFO_STATS_CPMARK_SET,
			      atomic64_read(&ci->stats_cpmark_set),
			      TCA_CTINFO_PAD))
		goto nla_put_failure;

	spin_unlock_bh(&ci->tcf_lock);
	return skb->len;

nla_put_failure:
	spin_unlock_bh(&ci->tcf_lock);
	nlmsg_trim(skb, b);
	return -1;
}

static void tcf_ctinfo_cleanup(struct tc_action *a)
{
	struct tcf_ctinfo *ci = to_ctinfo(a);
	struct tcf_ctinfo_params *cp;

	cp = rcu_dereference_protected(ci->params, 1);
	if (cp)
		kfree_rcu(cp, rcu);
}

static struct tc_action_ops act_ctinfo_ops = {
	.kind	= "ctinfo",
	.id	= TCA_ID_CTINFO,
	.owner	= THIS_MODULE,
	.act	= tcf_ctinfo_act,
	.dump	= tcf_ctinfo_dump,
	.init	= tcf_ctinfo_init,
	.cleanup= tcf_ctinfo_cleanup,
	.size	= sizeof(struct tcf_ctinfo),
};
MODULE_ALIAS_NET_ACT("ctinfo");

static __net_init int ctinfo_init_net(struct net *net)
{
	struct tc_action_net *tn = net_generic(net, act_ctinfo_ops.net_id);

	return tc_action_net_init(net, tn, &act_ctinfo_ops);
}

static void __net_exit ctinfo_exit_net(struct list_head *net_list)
{
	tc_action_net_exit(net_list, act_ctinfo_ops.net_id);
}

static struct pernet_operations ctinfo_net_ops = {
	.init		= ctinfo_init_net,
	.exit_batch	= ctinfo_exit_net,
	.id		= &act_ctinfo_ops.net_id,
	.size		= sizeof(struct tc_action_net),
};

static int __init ctinfo_init_module(void)
{
	return tcf_register_action(&act_ctinfo_ops, &ctinfo_net_ops);
}

static void __exit ctinfo_cleanup_module(void)
{
	tcf_unregister_action(&act_ctinfo_ops, &ctinfo_net_ops);
}

module_init(ctinfo_init_module);
module_exit(ctinfo_cleanup_module);
MODULE_AUTHOR("Kevin Darbyshire-Bryant <ldir@darbyshire-bryant.me.uk>");
MODULE_DESCRIPTION("Connection tracking mark actions");
MODULE_LICENSE("GPL");
