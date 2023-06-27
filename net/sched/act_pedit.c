// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * net/sched/act_pedit.c	Generic packet editor
 *
 * Authors:	Jamal Hadi Salim (2002-4)
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <linux/tc_act/tc_pedit.h>
#include <net/tc_act/tc_pedit.h>
#include <uapi/linux/tc_act/tc_pedit.h>
#include <net/pkt_cls.h>
#include <net/tc_wrapper.h>

static struct tc_action_ops act_pedit_ops;

static const struct nla_policy pedit_policy[TCA_PEDIT_MAX + 1] = {
	[TCA_PEDIT_PARMS]	= { .len = sizeof(struct tc_pedit) },
	[TCA_PEDIT_KEYS_EX]   = { .type = NLA_NESTED },
};

static const struct nla_policy pedit_key_ex_policy[TCA_PEDIT_KEY_EX_MAX + 1] = {
	[TCA_PEDIT_KEY_EX_HTYPE] =
		NLA_POLICY_MAX(NLA_U16, TCA_PEDIT_HDR_TYPE_MAX),
	[TCA_PEDIT_KEY_EX_CMD] = NLA_POLICY_MAX(NLA_U16, TCA_PEDIT_CMD_MAX),
};

static struct tcf_pedit_key_ex *tcf_pedit_keys_ex_parse(struct nlattr *nla,
							u8 n, struct netlink_ext_ack *extack)
{
	struct tcf_pedit_key_ex *keys_ex;
	struct tcf_pedit_key_ex *k;
	const struct nlattr *ka;
	int err = -EINVAL;
	int rem;

	if (!nla)
		return NULL;

	keys_ex = kcalloc(n, sizeof(*k), GFP_KERNEL);
	if (!keys_ex)
		return ERR_PTR(-ENOMEM);

	k = keys_ex;

	nla_for_each_nested(ka, nla, rem) {
		struct nlattr *tb[TCA_PEDIT_KEY_EX_MAX + 1];

		if (!n) {
			NL_SET_ERR_MSG_MOD(extack, "Can't parse more extended keys than requested");
			err = -EINVAL;
			goto err_out;
		}
		n--;

		if (nla_type(ka) != TCA_PEDIT_KEY_EX) {
			NL_SET_ERR_MSG_ATTR(extack, ka, "Unknown attribute, expected extended key");
			err = -EINVAL;
			goto err_out;
		}

		err = nla_parse_nested_deprecated(tb, TCA_PEDIT_KEY_EX_MAX,
						  ka, pedit_key_ex_policy,
						  NULL);
		if (err)
			goto err_out;

		if (NL_REQ_ATTR_CHECK(extack, nla, tb, TCA_PEDIT_KEY_EX_HTYPE)) {
			NL_SET_ERR_MSG(extack, "Missing required attribute");
			err = -EINVAL;
			goto err_out;
		}

		if (NL_REQ_ATTR_CHECK(extack, nla, tb, TCA_PEDIT_KEY_EX_CMD)) {
			NL_SET_ERR_MSG(extack, "Missing required attribute");
			err = -EINVAL;
			goto err_out;
		}

		k->htype = nla_get_u16(tb[TCA_PEDIT_KEY_EX_HTYPE]);
		k->cmd = nla_get_u16(tb[TCA_PEDIT_KEY_EX_CMD]);

		k++;
	}

	if (n) {
		NL_SET_ERR_MSG_MOD(extack, "Not enough extended keys to parse");
		err = -EINVAL;
		goto err_out;
	}

	return keys_ex;

err_out:
	kfree(keys_ex);
	return ERR_PTR(err);
}

static int tcf_pedit_key_ex_dump(struct sk_buff *skb,
				 struct tcf_pedit_key_ex *keys_ex, int n)
{
	struct nlattr *keys_start = nla_nest_start_noflag(skb,
							  TCA_PEDIT_KEYS_EX);

	if (!keys_start)
		goto nla_failure;
	for (; n > 0; n--) {
		struct nlattr *key_start;

		key_start = nla_nest_start_noflag(skb, TCA_PEDIT_KEY_EX);
		if (!key_start)
			goto nla_failure;

		if (nla_put_u16(skb, TCA_PEDIT_KEY_EX_HTYPE, keys_ex->htype) ||
		    nla_put_u16(skb, TCA_PEDIT_KEY_EX_CMD, keys_ex->cmd))
			goto nla_failure;

		nla_nest_end(skb, key_start);

		keys_ex++;
	}

	nla_nest_end(skb, keys_start);

	return 0;
nla_failure:
	nla_nest_cancel(skb, keys_start);
	return -EINVAL;
}

static void tcf_pedit_cleanup_rcu(struct rcu_head *head)
{
	struct tcf_pedit_parms *parms =
		container_of(head, struct tcf_pedit_parms, rcu);

	kfree(parms->tcfp_keys_ex);
	kfree(parms->tcfp_keys);

	kfree(parms);
}

static int tcf_pedit_init(struct net *net, struct nlattr *nla,
			  struct nlattr *est, struct tc_action **a,
			  struct tcf_proto *tp, u32 flags,
			  struct netlink_ext_ack *extack)
{
	struct tc_action_net *tn = net_generic(net, act_pedit_ops.net_id);
	bool bind = flags & TCA_ACT_FLAGS_BIND;
	struct tcf_chain *goto_ch = NULL;
	struct tcf_pedit_parms *oparms, *nparms;
	struct nlattr *tb[TCA_PEDIT_MAX + 1];
	struct tc_pedit *parm;
	struct nlattr *pattr;
	struct tcf_pedit *p;
	int ret = 0, err;
	int i, ksize;
	u32 index;

	if (!nla) {
		NL_SET_ERR_MSG_MOD(extack, "Pedit requires attributes to be passed");
		return -EINVAL;
	}

	err = nla_parse_nested_deprecated(tb, TCA_PEDIT_MAX, nla,
					  pedit_policy, NULL);
	if (err < 0)
		return err;

	pattr = tb[TCA_PEDIT_PARMS];
	if (!pattr)
		pattr = tb[TCA_PEDIT_PARMS_EX];
	if (!pattr) {
		NL_SET_ERR_MSG_MOD(extack, "Missing required TCA_PEDIT_PARMS or TCA_PEDIT_PARMS_EX pedit attribute");
		return -EINVAL;
	}

	parm = nla_data(pattr);

	index = parm->index;
	err = tcf_idr_check_alloc(tn, &index, a, bind);
	if (!err) {
		ret = tcf_idr_create_from_flags(tn, index, est, a,
						&act_pedit_ops, bind, flags);
		if (ret) {
			tcf_idr_cleanup(tn, index);
			return ret;
		}
		ret = ACT_P_CREATED;
	} else if (err > 0) {
		if (bind)
			return 0;
		if (!(flags & TCA_ACT_FLAGS_REPLACE)) {
			ret = -EEXIST;
			goto out_release;
		}
	} else {
		return err;
	}

	if (!parm->nkeys) {
		NL_SET_ERR_MSG_MOD(extack, "Pedit requires keys to be passed");
		ret = -EINVAL;
		goto out_release;
	}
	ksize = parm->nkeys * sizeof(struct tc_pedit_key);
	if (nla_len(pattr) < sizeof(*parm) + ksize) {
		NL_SET_ERR_MSG_ATTR(extack, pattr, "Length of TCA_PEDIT_PARMS or TCA_PEDIT_PARMS_EX pedit attribute is invalid");
		ret = -EINVAL;
		goto out_release;
	}

	nparms = kzalloc(sizeof(*nparms), GFP_KERNEL);
	if (!nparms) {
		ret = -ENOMEM;
		goto out_release;
	}

	nparms->tcfp_keys_ex =
		tcf_pedit_keys_ex_parse(tb[TCA_PEDIT_KEYS_EX], parm->nkeys, extack);
	if (IS_ERR(nparms->tcfp_keys_ex)) {
		ret = PTR_ERR(nparms->tcfp_keys_ex);
		goto out_free;
	}

	err = tcf_action_check_ctrlact(parm->action, tp, &goto_ch, extack);
	if (err < 0) {
		ret = err;
		goto out_free_ex;
	}

	nparms->tcfp_off_max_hint = 0;
	nparms->tcfp_flags = parm->flags;
	nparms->tcfp_nkeys = parm->nkeys;

	nparms->tcfp_keys = kmalloc(ksize, GFP_KERNEL);
	if (!nparms->tcfp_keys) {
		ret = -ENOMEM;
		goto put_chain;
	}

	memcpy(nparms->tcfp_keys, parm->keys, ksize);

	for (i = 0; i < nparms->tcfp_nkeys; ++i) {
		u32 offmask = nparms->tcfp_keys[i].offmask;
		u32 cur = nparms->tcfp_keys[i].off;

		/* The AT option can be added to static offsets in the datapath */
		if (!offmask && cur % 4) {
			NL_SET_ERR_MSG_MOD(extack, "Offsets must be on 32bit boundaries");
			ret = -EINVAL;
			goto out_free_keys;
		}

		/* sanitize the shift value for any later use */
		nparms->tcfp_keys[i].shift = min_t(size_t,
						   BITS_PER_TYPE(int) - 1,
						   nparms->tcfp_keys[i].shift);

		/* The AT option can read a single byte, we can bound the actual
		 * value with uchar max.
		 */
		cur += (0xff & offmask) >> nparms->tcfp_keys[i].shift;

		/* Each key touches 4 bytes starting from the computed offset */
		nparms->tcfp_off_max_hint =
			max(nparms->tcfp_off_max_hint, cur + 4);
	}

	p = to_pedit(*a);

	spin_lock_bh(&p->tcf_lock);
	goto_ch = tcf_action_set_ctrlact(*a, parm->action, goto_ch);
	oparms = rcu_replace_pointer(p->parms, nparms, 1);
	spin_unlock_bh(&p->tcf_lock);

	if (oparms)
		call_rcu(&oparms->rcu, tcf_pedit_cleanup_rcu);

	if (goto_ch)
		tcf_chain_put_by_act(goto_ch);

	return ret;

out_free_keys:
	kfree(nparms->tcfp_keys);
put_chain:
	if (goto_ch)
		tcf_chain_put_by_act(goto_ch);
out_free_ex:
	kfree(nparms->tcfp_keys_ex);
out_free:
	kfree(nparms);
out_release:
	tcf_idr_release(*a, bind);
	return ret;
}

static void tcf_pedit_cleanup(struct tc_action *a)
{
	struct tcf_pedit *p = to_pedit(a);
	struct tcf_pedit_parms *parms;

	parms = rcu_dereference_protected(p->parms, 1);

	if (parms)
		call_rcu(&parms->rcu, tcf_pedit_cleanup_rcu);
}

static bool offset_valid(struct sk_buff *skb, int offset)
{
	if (offset > 0 && offset > skb->len)
		return false;

	if  (offset < 0 && -offset > skb_headroom(skb))
		return false;

	return true;
}

static void pedit_skb_hdr_offset(struct sk_buff *skb,
				 enum pedit_header_type htype, int *hoffset)
{
	/* 'htype' is validated in the netlink parsing */
	switch (htype) {
	case TCA_PEDIT_KEY_EX_HDR_TYPE_ETH:
		if (skb_mac_header_was_set(skb))
			*hoffset = skb_mac_offset(skb);
		break;
	case TCA_PEDIT_KEY_EX_HDR_TYPE_NETWORK:
	case TCA_PEDIT_KEY_EX_HDR_TYPE_IP4:
	case TCA_PEDIT_KEY_EX_HDR_TYPE_IP6:
		*hoffset = skb_network_offset(skb);
		break;
	case TCA_PEDIT_KEY_EX_HDR_TYPE_TCP:
	case TCA_PEDIT_KEY_EX_HDR_TYPE_UDP:
		if (skb_transport_header_was_set(skb))
			*hoffset = skb_transport_offset(skb);
		break;
	default:
		break;
	}
}

TC_INDIRECT_SCOPE int tcf_pedit_act(struct sk_buff *skb,
				    const struct tc_action *a,
				    struct tcf_result *res)
{
	enum pedit_header_type htype = TCA_PEDIT_KEY_EX_HDR_TYPE_NETWORK;
	enum pedit_cmd cmd = TCA_PEDIT_KEY_EX_CMD_SET;
	struct tcf_pedit *p = to_pedit(a);
	struct tcf_pedit_key_ex *tkey_ex;
	struct tcf_pedit_parms *parms;
	struct tc_pedit_key *tkey;
	u32 max_offset;
	int i;

	parms = rcu_dereference_bh(p->parms);

	max_offset = (skb_transport_header_was_set(skb) ?
		      skb_transport_offset(skb) :
		      skb_network_offset(skb)) +
		     parms->tcfp_off_max_hint;
	if (skb_ensure_writable(skb, min(skb->len, max_offset)))
		goto done;

	tcf_lastuse_update(&p->tcf_tm);
	tcf_action_update_bstats(&p->common, skb);

	tkey = parms->tcfp_keys;
	tkey_ex = parms->tcfp_keys_ex;

	for (i = parms->tcfp_nkeys; i > 0; i--, tkey++) {
		int offset = tkey->off;
		int hoffset = 0;
		u32 *ptr, hdata;
		u32 val;

		if (tkey_ex) {
			htype = tkey_ex->htype;
			cmd = tkey_ex->cmd;

			tkey_ex++;
		}

		pedit_skb_hdr_offset(skb, htype, &hoffset);

		if (tkey->offmask) {
			u8 *d, _d;

			if (!offset_valid(skb, hoffset + tkey->at)) {
				pr_info_ratelimited("tc action pedit 'at' offset %d out of bounds\n",
						    hoffset + tkey->at);
				goto bad;
			}
			d = skb_header_pointer(skb, hoffset + tkey->at,
					       sizeof(_d), &_d);
			if (!d)
				goto bad;

			offset += (*d & tkey->offmask) >> tkey->shift;
			if (offset % 4) {
				pr_info_ratelimited("tc action pedit offset must be on 32 bit boundaries\n");
				goto bad;
			}
		}

		if (!offset_valid(skb, hoffset + offset)) {
			pr_info_ratelimited("tc action pedit offset %d out of bounds\n", hoffset + offset);
			goto bad;
		}

		ptr = skb_header_pointer(skb, hoffset + offset,
					 sizeof(hdata), &hdata);
		if (!ptr)
			goto bad;
		/* just do it, baby */
		switch (cmd) {
		case TCA_PEDIT_KEY_EX_CMD_SET:
			val = tkey->val;
			break;
		case TCA_PEDIT_KEY_EX_CMD_ADD:
			val = (*ptr + tkey->val) & ~tkey->mask;
			break;
		default:
			pr_info_ratelimited("tc action pedit bad command (%d)\n", cmd);
			goto bad;
		}

		*ptr = ((*ptr & tkey->mask) ^ val);
		if (ptr == &hdata)
			skb_store_bits(skb, hoffset + offset, ptr, 4);
	}

	goto done;

bad:
	tcf_action_inc_overlimit_qstats(&p->common);
done:
	return p->tcf_action;
}

static void tcf_pedit_stats_update(struct tc_action *a, u64 bytes, u64 packets,
				   u64 drops, u64 lastuse, bool hw)
{
	struct tcf_pedit *d = to_pedit(a);
	struct tcf_t *tm = &d->tcf_tm;

	tcf_action_update_stats(a, bytes, packets, drops, hw);
	tm->lastuse = max_t(u64, tm->lastuse, lastuse);
}

static int tcf_pedit_dump(struct sk_buff *skb, struct tc_action *a,
			  int bind, int ref)
{
	unsigned char *b = skb_tail_pointer(skb);
	struct tcf_pedit *p = to_pedit(a);
	struct tcf_pedit_parms *parms;
	struct tc_pedit *opt;
	struct tcf_t t;
	int s;

	spin_lock_bh(&p->tcf_lock);
	parms = rcu_dereference_protected(p->parms, 1);
	s = struct_size(opt, keys, parms->tcfp_nkeys);

	opt = kzalloc(s, GFP_ATOMIC);
	if (unlikely(!opt)) {
		spin_unlock_bh(&p->tcf_lock);
		return -ENOBUFS;
	}

	memcpy(opt->keys, parms->tcfp_keys,
	       flex_array_size(opt, keys, parms->tcfp_nkeys));
	opt->index = p->tcf_index;
	opt->nkeys = parms->tcfp_nkeys;
	opt->flags = parms->tcfp_flags;
	opt->action = p->tcf_action;
	opt->refcnt = refcount_read(&p->tcf_refcnt) - ref;
	opt->bindcnt = atomic_read(&p->tcf_bindcnt) - bind;

	if (parms->tcfp_keys_ex) {
		if (tcf_pedit_key_ex_dump(skb, parms->tcfp_keys_ex,
					  parms->tcfp_nkeys))
			goto nla_put_failure;

		if (nla_put(skb, TCA_PEDIT_PARMS_EX, s, opt))
			goto nla_put_failure;
	} else {
		if (nla_put(skb, TCA_PEDIT_PARMS, s, opt))
			goto nla_put_failure;
	}

	tcf_tm_dump(&t, &p->tcf_tm);
	if (nla_put_64bit(skb, TCA_PEDIT_TM, sizeof(t), &t, TCA_PEDIT_PAD))
		goto nla_put_failure;
	spin_unlock_bh(&p->tcf_lock);

	kfree(opt);
	return skb->len;

nla_put_failure:
	spin_unlock_bh(&p->tcf_lock);
	nlmsg_trim(skb, b);
	kfree(opt);
	return -1;
}

static int tcf_pedit_offload_act_setup(struct tc_action *act, void *entry_data,
				       u32 *index_inc, bool bind,
				       struct netlink_ext_ack *extack)
{
	if (bind) {
		struct flow_action_entry *entry = entry_data;
		int k;

		for (k = 0; k < tcf_pedit_nkeys(act); k++) {
			switch (tcf_pedit_cmd(act, k)) {
			case TCA_PEDIT_KEY_EX_CMD_SET:
				entry->id = FLOW_ACTION_MANGLE;
				break;
			case TCA_PEDIT_KEY_EX_CMD_ADD:
				entry->id = FLOW_ACTION_ADD;
				break;
			default:
				NL_SET_ERR_MSG_MOD(extack, "Unsupported pedit command offload");
				return -EOPNOTSUPP;
			}
			entry->mangle.htype = tcf_pedit_htype(act, k);
			entry->mangle.mask = tcf_pedit_mask(act, k);
			entry->mangle.val = tcf_pedit_val(act, k);
			entry->mangle.offset = tcf_pedit_offset(act, k);
			entry->hw_stats = tc_act_hw_stats(act->hw_stats);
			entry++;
		}
		*index_inc = k;
	} else {
		struct flow_offload_action *fl_action = entry_data;
		u32 cmd = tcf_pedit_cmd(act, 0);
		int k;

		switch (cmd) {
		case TCA_PEDIT_KEY_EX_CMD_SET:
			fl_action->id = FLOW_ACTION_MANGLE;
			break;
		case TCA_PEDIT_KEY_EX_CMD_ADD:
			fl_action->id = FLOW_ACTION_ADD;
			break;
		default:
			NL_SET_ERR_MSG_MOD(extack, "Unsupported pedit command offload");
			return -EOPNOTSUPP;
		}

		for (k = 1; k < tcf_pedit_nkeys(act); k++) {
			if (cmd != tcf_pedit_cmd(act, k)) {
				NL_SET_ERR_MSG_MOD(extack, "Unsupported pedit command offload");
				return -EOPNOTSUPP;
			}
		}
	}

	return 0;
}

static struct tc_action_ops act_pedit_ops = {
	.kind		=	"pedit",
	.id		=	TCA_ID_PEDIT,
	.owner		=	THIS_MODULE,
	.act		=	tcf_pedit_act,
	.stats_update	=	tcf_pedit_stats_update,
	.dump		=	tcf_pedit_dump,
	.cleanup	=	tcf_pedit_cleanup,
	.init		=	tcf_pedit_init,
	.offload_act_setup =	tcf_pedit_offload_act_setup,
	.size		=	sizeof(struct tcf_pedit),
};

static __net_init int pedit_init_net(struct net *net)
{
	struct tc_action_net *tn = net_generic(net, act_pedit_ops.net_id);

	return tc_action_net_init(net, tn, &act_pedit_ops);
}

static void __net_exit pedit_exit_net(struct list_head *net_list)
{
	tc_action_net_exit(net_list, act_pedit_ops.net_id);
}

static struct pernet_operations pedit_net_ops = {
	.init = pedit_init_net,
	.exit_batch = pedit_exit_net,
	.id   = &act_pedit_ops.net_id,
	.size = sizeof(struct tc_action_net),
};

MODULE_AUTHOR("Jamal Hadi Salim(2002-4)");
MODULE_DESCRIPTION("Generic Packet Editor actions");
MODULE_LICENSE("GPL");

static int __init pedit_init_module(void)
{
	return tcf_register_action(&act_pedit_ops, &pedit_net_ops);
}

static void __exit pedit_cleanup_module(void)
{
	tcf_unregister_action(&act_pedit_ops, &pedit_net_ops);
}

module_init(pedit_init_module);
module_exit(pedit_cleanup_module);
