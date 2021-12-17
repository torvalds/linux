// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright 2020 NXP */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <net/act_api.h>
#include <net/netlink.h>
#include <net/pkt_cls.h>
#include <net/tc_act/tc_gate.h>

static unsigned int gate_net_id;
static struct tc_action_ops act_gate_ops;

static ktime_t gate_get_time(struct tcf_gate *gact)
{
	ktime_t mono = ktime_get();

	switch (gact->tk_offset) {
	case TK_OFFS_MAX:
		return mono;
	default:
		return ktime_mono_to_any(mono, gact->tk_offset);
	}

	return KTIME_MAX;
}

static void gate_get_start_time(struct tcf_gate *gact, ktime_t *start)
{
	struct tcf_gate_params *param = &gact->param;
	ktime_t now, base, cycle;
	u64 n;

	base = ns_to_ktime(param->tcfg_basetime);
	now = gate_get_time(gact);

	if (ktime_after(base, now)) {
		*start = base;
		return;
	}

	cycle = param->tcfg_cycletime;

	n = div64_u64(ktime_sub_ns(now, base), cycle);
	*start = ktime_add_ns(base, (n + 1) * cycle);
}

static void gate_start_timer(struct tcf_gate *gact, ktime_t start)
{
	ktime_t expires;

	expires = hrtimer_get_expires(&gact->hitimer);
	if (expires == 0)
		expires = KTIME_MAX;

	start = min_t(ktime_t, start, expires);

	hrtimer_start(&gact->hitimer, start, HRTIMER_MODE_ABS_SOFT);
}

static enum hrtimer_restart gate_timer_func(struct hrtimer *timer)
{
	struct tcf_gate *gact = container_of(timer, struct tcf_gate,
					     hitimer);
	struct tcf_gate_params *p = &gact->param;
	struct tcfg_gate_entry *next;
	ktime_t close_time, now;

	spin_lock(&gact->tcf_lock);

	next = gact->next_entry;

	/* cycle start, clear pending bit, clear total octets */
	gact->current_gate_status = next->gate_state ? GATE_ACT_GATE_OPEN : 0;
	gact->current_entry_octets = 0;
	gact->current_max_octets = next->maxoctets;

	gact->current_close_time = ktime_add_ns(gact->current_close_time,
						next->interval);

	close_time = gact->current_close_time;

	if (list_is_last(&next->list, &p->entries))
		next = list_first_entry(&p->entries,
					struct tcfg_gate_entry, list);
	else
		next = list_next_entry(next, list);

	now = gate_get_time(gact);

	if (ktime_after(now, close_time)) {
		ktime_t cycle, base;
		u64 n;

		cycle = p->tcfg_cycletime;
		base = ns_to_ktime(p->tcfg_basetime);
		n = div64_u64(ktime_sub_ns(now, base), cycle);
		close_time = ktime_add_ns(base, (n + 1) * cycle);
	}

	gact->next_entry = next;

	hrtimer_set_expires(&gact->hitimer, close_time);

	spin_unlock(&gact->tcf_lock);

	return HRTIMER_RESTART;
}

static int tcf_gate_act(struct sk_buff *skb, const struct tc_action *a,
			struct tcf_result *res)
{
	struct tcf_gate *gact = to_gate(a);

	spin_lock(&gact->tcf_lock);

	tcf_lastuse_update(&gact->tcf_tm);
	bstats_update(&gact->tcf_bstats, skb);

	if (unlikely(gact->current_gate_status & GATE_ACT_PENDING)) {
		spin_unlock(&gact->tcf_lock);
		return gact->tcf_action;
	}

	if (!(gact->current_gate_status & GATE_ACT_GATE_OPEN))
		goto drop;

	if (gact->current_max_octets >= 0) {
		gact->current_entry_octets += qdisc_pkt_len(skb);
		if (gact->current_entry_octets > gact->current_max_octets) {
			gact->tcf_qstats.overlimits++;
			goto drop;
		}
	}

	spin_unlock(&gact->tcf_lock);

	return gact->tcf_action;
drop:
	gact->tcf_qstats.drops++;
	spin_unlock(&gact->tcf_lock);

	return TC_ACT_SHOT;
}

static const struct nla_policy entry_policy[TCA_GATE_ENTRY_MAX + 1] = {
	[TCA_GATE_ENTRY_INDEX]		= { .type = NLA_U32 },
	[TCA_GATE_ENTRY_GATE]		= { .type = NLA_FLAG },
	[TCA_GATE_ENTRY_INTERVAL]	= { .type = NLA_U32 },
	[TCA_GATE_ENTRY_IPV]		= { .type = NLA_S32 },
	[TCA_GATE_ENTRY_MAX_OCTETS]	= { .type = NLA_S32 },
};

static const struct nla_policy gate_policy[TCA_GATE_MAX + 1] = {
	[TCA_GATE_PARMS]		=
		NLA_POLICY_EXACT_LEN(sizeof(struct tc_gate)),
	[TCA_GATE_PRIORITY]		= { .type = NLA_S32 },
	[TCA_GATE_ENTRY_LIST]		= { .type = NLA_NESTED },
	[TCA_GATE_BASE_TIME]		= { .type = NLA_U64 },
	[TCA_GATE_CYCLE_TIME]		= { .type = NLA_U64 },
	[TCA_GATE_CYCLE_TIME_EXT]	= { .type = NLA_U64 },
	[TCA_GATE_FLAGS]		= { .type = NLA_U32 },
	[TCA_GATE_CLOCKID]		= { .type = NLA_S32 },
};

static int fill_gate_entry(struct nlattr **tb, struct tcfg_gate_entry *entry,
			   struct netlink_ext_ack *extack)
{
	u32 interval = 0;

	entry->gate_state = nla_get_flag(tb[TCA_GATE_ENTRY_GATE]);

	if (tb[TCA_GATE_ENTRY_INTERVAL])
		interval = nla_get_u32(tb[TCA_GATE_ENTRY_INTERVAL]);

	if (interval == 0) {
		NL_SET_ERR_MSG(extack, "Invalid interval for schedule entry");
		return -EINVAL;
	}

	entry->interval = interval;

	if (tb[TCA_GATE_ENTRY_IPV])
		entry->ipv = nla_get_s32(tb[TCA_GATE_ENTRY_IPV]);
	else
		entry->ipv = -1;

	if (tb[TCA_GATE_ENTRY_MAX_OCTETS])
		entry->maxoctets = nla_get_s32(tb[TCA_GATE_ENTRY_MAX_OCTETS]);
	else
		entry->maxoctets = -1;

	return 0;
}

static int parse_gate_entry(struct nlattr *n, struct  tcfg_gate_entry *entry,
			    int index, struct netlink_ext_ack *extack)
{
	struct nlattr *tb[TCA_GATE_ENTRY_MAX + 1] = { };
	int err;

	err = nla_parse_nested(tb, TCA_GATE_ENTRY_MAX, n, entry_policy, extack);
	if (err < 0) {
		NL_SET_ERR_MSG(extack, "Could not parse nested entry");
		return -EINVAL;
	}

	entry->index = index;

	return fill_gate_entry(tb, entry, extack);
}

static void release_entry_list(struct list_head *entries)
{
	struct tcfg_gate_entry *entry, *e;

	list_for_each_entry_safe(entry, e, entries, list) {
		list_del(&entry->list);
		kfree(entry);
	}
}

static int parse_gate_list(struct nlattr *list_attr,
			   struct tcf_gate_params *sched,
			   struct netlink_ext_ack *extack)
{
	struct tcfg_gate_entry *entry;
	struct nlattr *n;
	int err, rem;
	int i = 0;

	if (!list_attr)
		return -EINVAL;

	nla_for_each_nested(n, list_attr, rem) {
		if (nla_type(n) != TCA_GATE_ONE_ENTRY) {
			NL_SET_ERR_MSG(extack, "Attribute isn't type 'entry'");
			continue;
		}

		entry = kzalloc(sizeof(*entry), GFP_ATOMIC);
		if (!entry) {
			NL_SET_ERR_MSG(extack, "Not enough memory for entry");
			err = -ENOMEM;
			goto release_list;
		}

		err = parse_gate_entry(n, entry, i, extack);
		if (err < 0) {
			kfree(entry);
			goto release_list;
		}

		list_add_tail(&entry->list, &sched->entries);
		i++;
	}

	sched->num_entries = i;

	return i;

release_list:
	release_entry_list(&sched->entries);

	return err;
}

static void gate_setup_timer(struct tcf_gate *gact, u64 basetime,
			     enum tk_offsets tko, s32 clockid,
			     bool do_init)
{
	if (!do_init) {
		if (basetime == gact->param.tcfg_basetime &&
		    tko == gact->tk_offset &&
		    clockid == gact->param.tcfg_clockid)
			return;

		spin_unlock_bh(&gact->tcf_lock);
		hrtimer_cancel(&gact->hitimer);
		spin_lock_bh(&gact->tcf_lock);
	}
	gact->param.tcfg_basetime = basetime;
	gact->param.tcfg_clockid = clockid;
	gact->tk_offset = tko;
	hrtimer_init(&gact->hitimer, clockid, HRTIMER_MODE_ABS_SOFT);
	gact->hitimer.function = gate_timer_func;
}

static int tcf_gate_init(struct net *net, struct nlattr *nla,
			 struct nlattr *est, struct tc_action **a,
			 struct tcf_proto *tp, u32 flags,
			 struct netlink_ext_ack *extack)
{
	struct tc_action_net *tn = net_generic(net, gate_net_id);
	enum tk_offsets tk_offset = TK_OFFS_TAI;
	bool bind = flags & TCA_ACT_FLAGS_BIND;
	struct nlattr *tb[TCA_GATE_MAX + 1];
	struct tcf_chain *goto_ch = NULL;
	u64 cycletime = 0, basetime = 0;
	struct tcf_gate_params *p;
	s32 clockid = CLOCK_TAI;
	struct tcf_gate *gact;
	struct tc_gate *parm;
	int ret = 0, err;
	u32 gflags = 0;
	s32 prio = -1;
	ktime_t start;
	u32 index;

	if (!nla)
		return -EINVAL;

	err = nla_parse_nested(tb, TCA_GATE_MAX, nla, gate_policy, extack);
	if (err < 0)
		return err;

	if (!tb[TCA_GATE_PARMS])
		return -EINVAL;

	if (tb[TCA_GATE_CLOCKID]) {
		clockid = nla_get_s32(tb[TCA_GATE_CLOCKID]);
		switch (clockid) {
		case CLOCK_REALTIME:
			tk_offset = TK_OFFS_REAL;
			break;
		case CLOCK_MONOTONIC:
			tk_offset = TK_OFFS_MAX;
			break;
		case CLOCK_BOOTTIME:
			tk_offset = TK_OFFS_BOOT;
			break;
		case CLOCK_TAI:
			tk_offset = TK_OFFS_TAI;
			break;
		default:
			NL_SET_ERR_MSG(extack, "Invalid 'clockid'");
			return -EINVAL;
		}
	}

	parm = nla_data(tb[TCA_GATE_PARMS]);
	index = parm->index;

	err = tcf_idr_check_alloc(tn, &index, a, bind);
	if (err < 0)
		return err;

	if (err && bind)
		return 0;

	if (!err) {
		ret = tcf_idr_create(tn, index, est, a,
				     &act_gate_ops, bind, false, flags);
		if (ret) {
			tcf_idr_cleanup(tn, index);
			return ret;
		}

		ret = ACT_P_CREATED;
	} else if (!(flags & TCA_ACT_FLAGS_REPLACE)) {
		tcf_idr_release(*a, bind);
		return -EEXIST;
	}

	if (tb[TCA_GATE_PRIORITY])
		prio = nla_get_s32(tb[TCA_GATE_PRIORITY]);

	if (tb[TCA_GATE_BASE_TIME])
		basetime = nla_get_u64(tb[TCA_GATE_BASE_TIME]);

	if (tb[TCA_GATE_FLAGS])
		gflags = nla_get_u32(tb[TCA_GATE_FLAGS]);

	gact = to_gate(*a);
	if (ret == ACT_P_CREATED)
		INIT_LIST_HEAD(&gact->param.entries);

	err = tcf_action_check_ctrlact(parm->action, tp, &goto_ch, extack);
	if (err < 0)
		goto release_idr;

	spin_lock_bh(&gact->tcf_lock);
	p = &gact->param;

	if (tb[TCA_GATE_CYCLE_TIME])
		cycletime = nla_get_u64(tb[TCA_GATE_CYCLE_TIME]);

	if (tb[TCA_GATE_ENTRY_LIST]) {
		err = parse_gate_list(tb[TCA_GATE_ENTRY_LIST], p, extack);
		if (err < 0)
			goto chain_put;
	}

	if (!cycletime) {
		struct tcfg_gate_entry *entry;
		ktime_t cycle = 0;

		list_for_each_entry(entry, &p->entries, list)
			cycle = ktime_add_ns(cycle, entry->interval);
		cycletime = cycle;
		if (!cycletime) {
			err = -EINVAL;
			goto chain_put;
		}
	}
	p->tcfg_cycletime = cycletime;

	if (tb[TCA_GATE_CYCLE_TIME_EXT])
		p->tcfg_cycletime_ext =
			nla_get_u64(tb[TCA_GATE_CYCLE_TIME_EXT]);

	gate_setup_timer(gact, basetime, tk_offset, clockid,
			 ret == ACT_P_CREATED);
	p->tcfg_priority = prio;
	p->tcfg_flags = gflags;
	gate_get_start_time(gact, &start);

	gact->current_close_time = start;
	gact->current_gate_status = GATE_ACT_GATE_OPEN | GATE_ACT_PENDING;

	gact->next_entry = list_first_entry(&p->entries,
					    struct tcfg_gate_entry, list);

	goto_ch = tcf_action_set_ctrlact(*a, parm->action, goto_ch);

	gate_start_timer(gact, start);

	spin_unlock_bh(&gact->tcf_lock);

	if (goto_ch)
		tcf_chain_put_by_act(goto_ch);

	return ret;

chain_put:
	spin_unlock_bh(&gact->tcf_lock);

	if (goto_ch)
		tcf_chain_put_by_act(goto_ch);
release_idr:
	/* action is not inserted in any list: it's safe to init hitimer
	 * without taking tcf_lock.
	 */
	if (ret == ACT_P_CREATED)
		gate_setup_timer(gact, gact->param.tcfg_basetime,
				 gact->tk_offset, gact->param.tcfg_clockid,
				 true);
	tcf_idr_release(*a, bind);
	return err;
}

static void tcf_gate_cleanup(struct tc_action *a)
{
	struct tcf_gate *gact = to_gate(a);
	struct tcf_gate_params *p;

	p = &gact->param;
	hrtimer_cancel(&gact->hitimer);
	release_entry_list(&p->entries);
}

static int dumping_entry(struct sk_buff *skb,
			 struct tcfg_gate_entry *entry)
{
	struct nlattr *item;

	item = nla_nest_start_noflag(skb, TCA_GATE_ONE_ENTRY);
	if (!item)
		return -ENOSPC;

	if (nla_put_u32(skb, TCA_GATE_ENTRY_INDEX, entry->index))
		goto nla_put_failure;

	if (entry->gate_state && nla_put_flag(skb, TCA_GATE_ENTRY_GATE))
		goto nla_put_failure;

	if (nla_put_u32(skb, TCA_GATE_ENTRY_INTERVAL, entry->interval))
		goto nla_put_failure;

	if (nla_put_s32(skb, TCA_GATE_ENTRY_MAX_OCTETS, entry->maxoctets))
		goto nla_put_failure;

	if (nla_put_s32(skb, TCA_GATE_ENTRY_IPV, entry->ipv))
		goto nla_put_failure;

	return nla_nest_end(skb, item);

nla_put_failure:
	nla_nest_cancel(skb, item);
	return -1;
}

static int tcf_gate_dump(struct sk_buff *skb, struct tc_action *a,
			 int bind, int ref)
{
	unsigned char *b = skb_tail_pointer(skb);
	struct tcf_gate *gact = to_gate(a);
	struct tc_gate opt = {
		.index    = gact->tcf_index,
		.refcnt   = refcount_read(&gact->tcf_refcnt) - ref,
		.bindcnt  = atomic_read(&gact->tcf_bindcnt) - bind,
	};
	struct tcfg_gate_entry *entry;
	struct tcf_gate_params *p;
	struct nlattr *entry_list;
	struct tcf_t t;

	spin_lock_bh(&gact->tcf_lock);
	opt.action = gact->tcf_action;

	p = &gact->param;

	if (nla_put(skb, TCA_GATE_PARMS, sizeof(opt), &opt))
		goto nla_put_failure;

	if (nla_put_u64_64bit(skb, TCA_GATE_BASE_TIME,
			      p->tcfg_basetime, TCA_GATE_PAD))
		goto nla_put_failure;

	if (nla_put_u64_64bit(skb, TCA_GATE_CYCLE_TIME,
			      p->tcfg_cycletime, TCA_GATE_PAD))
		goto nla_put_failure;

	if (nla_put_u64_64bit(skb, TCA_GATE_CYCLE_TIME_EXT,
			      p->tcfg_cycletime_ext, TCA_GATE_PAD))
		goto nla_put_failure;

	if (nla_put_s32(skb, TCA_GATE_CLOCKID, p->tcfg_clockid))
		goto nla_put_failure;

	if (nla_put_u32(skb, TCA_GATE_FLAGS, p->tcfg_flags))
		goto nla_put_failure;

	if (nla_put_s32(skb, TCA_GATE_PRIORITY, p->tcfg_priority))
		goto nla_put_failure;

	entry_list = nla_nest_start_noflag(skb, TCA_GATE_ENTRY_LIST);
	if (!entry_list)
		goto nla_put_failure;

	list_for_each_entry(entry, &p->entries, list) {
		if (dumping_entry(skb, entry) < 0)
			goto nla_put_failure;
	}

	nla_nest_end(skb, entry_list);

	tcf_tm_dump(&t, &gact->tcf_tm);
	if (nla_put_64bit(skb, TCA_GATE_TM, sizeof(t), &t, TCA_GATE_PAD))
		goto nla_put_failure;
	spin_unlock_bh(&gact->tcf_lock);

	return skb->len;

nla_put_failure:
	spin_unlock_bh(&gact->tcf_lock);
	nlmsg_trim(skb, b);
	return -1;
}

static int tcf_gate_walker(struct net *net, struct sk_buff *skb,
			   struct netlink_callback *cb, int type,
			   const struct tc_action_ops *ops,
			   struct netlink_ext_ack *extack)
{
	struct tc_action_net *tn = net_generic(net, gate_net_id);

	return tcf_generic_walker(tn, skb, cb, type, ops, extack);
}

static void tcf_gate_stats_update(struct tc_action *a, u64 bytes, u64 packets,
				  u64 drops, u64 lastuse, bool hw)
{
	struct tcf_gate *gact = to_gate(a);
	struct tcf_t *tm = &gact->tcf_tm;

	tcf_action_update_stats(a, bytes, packets, drops, hw);
	tm->lastuse = max_t(u64, tm->lastuse, lastuse);
}

static int tcf_gate_search(struct net *net, struct tc_action **a, u32 index)
{
	struct tc_action_net *tn = net_generic(net, gate_net_id);

	return tcf_idr_search(tn, a, index);
}

static size_t tcf_gate_get_fill_size(const struct tc_action *act)
{
	return nla_total_size(sizeof(struct tc_gate));
}

static struct tc_action_ops act_gate_ops = {
	.kind		=	"gate",
	.id		=	TCA_ID_GATE,
	.owner		=	THIS_MODULE,
	.act		=	tcf_gate_act,
	.dump		=	tcf_gate_dump,
	.init		=	tcf_gate_init,
	.cleanup	=	tcf_gate_cleanup,
	.walk		=	tcf_gate_walker,
	.stats_update	=	tcf_gate_stats_update,
	.get_fill_size	=	tcf_gate_get_fill_size,
	.lookup		=	tcf_gate_search,
	.size		=	sizeof(struct tcf_gate),
};

static __net_init int gate_init_net(struct net *net)
{
	struct tc_action_net *tn = net_generic(net, gate_net_id);

	return tc_action_net_init(net, tn, &act_gate_ops);
}

static void __net_exit gate_exit_net(struct list_head *net_list)
{
	tc_action_net_exit(net_list, gate_net_id);
}

static struct pernet_operations gate_net_ops = {
	.init = gate_init_net,
	.exit_batch = gate_exit_net,
	.id   = &gate_net_id,
	.size = sizeof(struct tc_action_net),
};

static int __init gate_init_module(void)
{
	return tcf_register_action(&act_gate_ops, &gate_net_ops);
}

static void __exit gate_cleanup_module(void)
{
	tcf_unregister_action(&act_gate_ops, &gate_net_ops);
}

module_init(gate_init_module);
module_exit(gate_cleanup_module);
MODULE_LICENSE("GPL v2");
