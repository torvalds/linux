// SPDX-License-Identifier: GPL-2.0-only
/*
 * net/core/fib_rules.c		Generic Routing Rules
 *
 * Authors:	Thomas Graf <tgraf@suug.ch>
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/module.h>
#include <net/net_namespace.h>
#include <net/inet_dscp.h>
#include <net/sock.h>
#include <net/fib_rules.h>
#include <net/ip_tunnels.h>
#include <linux/indirect_call_wrapper.h>

#if defined(CONFIG_IPV6) && defined(CONFIG_IPV6_MULTIPLE_TABLES)
#ifdef CONFIG_IP_MULTIPLE_TABLES
#define INDIRECT_CALL_MT(f, f2, f1, ...) \
	INDIRECT_CALL_INET(f, f2, f1, __VA_ARGS__)
#else
#define INDIRECT_CALL_MT(f, f2, f1, ...) INDIRECT_CALL_1(f, f2, __VA_ARGS__)
#endif
#elif defined(CONFIG_IP_MULTIPLE_TABLES)
#define INDIRECT_CALL_MT(f, f2, f1, ...) INDIRECT_CALL_1(f, f1, __VA_ARGS__)
#else
#define INDIRECT_CALL_MT(f, f2, f1, ...) f(__VA_ARGS__)
#endif

static const struct fib_kuid_range fib_kuid_range_unset = {
	KUIDT_INIT(0),
	KUIDT_INIT(~0),
};

bool fib_rule_matchall(const struct fib_rule *rule)
{
	if (rule->iifindex || rule->oifindex || rule->mark || rule->tun_id ||
	    rule->flags)
		return false;
	if (rule->suppress_ifgroup != -1 || rule->suppress_prefixlen != -1)
		return false;
	if (!uid_eq(rule->uid_range.start, fib_kuid_range_unset.start) ||
	    !uid_eq(rule->uid_range.end, fib_kuid_range_unset.end))
		return false;
	if (fib_rule_port_range_set(&rule->sport_range))
		return false;
	if (fib_rule_port_range_set(&rule->dport_range))
		return false;
	return true;
}
EXPORT_SYMBOL_GPL(fib_rule_matchall);

int fib_default_rule_add(struct fib_rules_ops *ops,
			 u32 pref, u32 table)
{
	struct fib_rule *r;

	r = kzalloc(ops->rule_size, GFP_KERNEL_ACCOUNT);
	if (r == NULL)
		return -ENOMEM;

	refcount_set(&r->refcnt, 1);
	r->action = FR_ACT_TO_TBL;
	r->pref = pref;
	r->table = table;
	r->proto = RTPROT_KERNEL;
	r->fr_net = ops->fro_net;
	r->uid_range = fib_kuid_range_unset;

	r->suppress_prefixlen = -1;
	r->suppress_ifgroup = -1;

	/* The lock is not required here, the list in unreachable
	 * at the moment this function is called */
	list_add_tail(&r->list, &ops->rules_list);
	return 0;
}
EXPORT_SYMBOL(fib_default_rule_add);

static u32 fib_default_rule_pref(struct fib_rules_ops *ops)
{
	struct list_head *pos;
	struct fib_rule *rule;

	if (!list_empty(&ops->rules_list)) {
		pos = ops->rules_list.next;
		if (pos->next != &ops->rules_list) {
			rule = list_entry(pos->next, struct fib_rule, list);
			if (rule->pref)
				return rule->pref - 1;
		}
	}

	return 0;
}

static void notify_rule_change(int event, struct fib_rule *rule,
			       struct fib_rules_ops *ops, struct nlmsghdr *nlh,
			       u32 pid);

static struct fib_rules_ops *lookup_rules_ops(const struct net *net,
					      int family)
{
	struct fib_rules_ops *ops;

	rcu_read_lock();
	list_for_each_entry_rcu(ops, &net->rules_ops, list) {
		if (ops->family == family) {
			if (!try_module_get(ops->owner))
				ops = NULL;
			rcu_read_unlock();
			return ops;
		}
	}
	rcu_read_unlock();

	return NULL;
}

static void rules_ops_put(struct fib_rules_ops *ops)
{
	if (ops)
		module_put(ops->owner);
}

static void flush_route_cache(struct fib_rules_ops *ops)
{
	if (ops->flush_cache)
		ops->flush_cache(ops);
}

static int __fib_rules_register(struct fib_rules_ops *ops)
{
	int err = -EEXIST;
	struct fib_rules_ops *o;
	struct net *net;

	net = ops->fro_net;

	if (ops->rule_size < sizeof(struct fib_rule))
		return -EINVAL;

	if (ops->match == NULL || ops->configure == NULL ||
	    ops->compare == NULL || ops->fill == NULL ||
	    ops->action == NULL)
		return -EINVAL;

	spin_lock(&net->rules_mod_lock);
	list_for_each_entry(o, &net->rules_ops, list)
		if (ops->family == o->family)
			goto errout;

	list_add_tail_rcu(&ops->list, &net->rules_ops);
	err = 0;
errout:
	spin_unlock(&net->rules_mod_lock);

	return err;
}

struct fib_rules_ops *
fib_rules_register(const struct fib_rules_ops *tmpl, struct net *net)
{
	struct fib_rules_ops *ops;
	int err;

	ops = kmemdup(tmpl, sizeof(*ops), GFP_KERNEL);
	if (ops == NULL)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&ops->rules_list);
	ops->fro_net = net;

	err = __fib_rules_register(ops);
	if (err) {
		kfree(ops);
		ops = ERR_PTR(err);
	}

	return ops;
}
EXPORT_SYMBOL_GPL(fib_rules_register);

static void fib_rules_cleanup_ops(struct fib_rules_ops *ops)
{
	struct fib_rule *rule, *tmp;

	list_for_each_entry_safe(rule, tmp, &ops->rules_list, list) {
		list_del_rcu(&rule->list);
		if (ops->delete)
			ops->delete(rule);
		fib_rule_put(rule);
	}
}

void fib_rules_unregister(struct fib_rules_ops *ops)
{
	struct net *net = ops->fro_net;

	spin_lock(&net->rules_mod_lock);
	list_del_rcu(&ops->list);
	spin_unlock(&net->rules_mod_lock);

	fib_rules_cleanup_ops(ops);
	kfree_rcu(ops, rcu);
}
EXPORT_SYMBOL_GPL(fib_rules_unregister);

static int uid_range_set(struct fib_kuid_range *range)
{
	return uid_valid(range->start) && uid_valid(range->end);
}

static struct fib_kuid_range nla_get_kuid_range(struct nlattr **tb)
{
	struct fib_rule_uid_range *in;
	struct fib_kuid_range out;

	in = (struct fib_rule_uid_range *)nla_data(tb[FRA_UID_RANGE]);

	out.start = make_kuid(current_user_ns(), in->start);
	out.end = make_kuid(current_user_ns(), in->end);

	return out;
}

static int nla_put_uid_range(struct sk_buff *skb, struct fib_kuid_range *range)
{
	struct fib_rule_uid_range out = {
		from_kuid_munged(current_user_ns(), range->start),
		from_kuid_munged(current_user_ns(), range->end)
	};

	return nla_put(skb, FRA_UID_RANGE, sizeof(out), &out);
}

static int nla_get_port_range(struct nlattr *pattr,
			      struct fib_rule_port_range *port_range)
{
	const struct fib_rule_port_range *pr = nla_data(pattr);

	if (!fib_rule_port_range_valid(pr))
		return -EINVAL;

	port_range->start = pr->start;
	port_range->end = pr->end;

	return 0;
}

static int nla_put_port_range(struct sk_buff *skb, int attrtype,
			      struct fib_rule_port_range *range)
{
	return nla_put(skb, attrtype, sizeof(*range), range);
}

static int fib_rule_match(struct fib_rule *rule, struct fib_rules_ops *ops,
			  struct flowi *fl, int flags,
			  struct fib_lookup_arg *arg)
{
	int ret = 0;

	if (rule->iifindex && (rule->iifindex != fl->flowi_iif))
		goto out;

	if (rule->oifindex && (rule->oifindex != fl->flowi_oif))
		goto out;

	if ((rule->mark ^ fl->flowi_mark) & rule->mark_mask)
		goto out;

	if (rule->tun_id && (rule->tun_id != fl->flowi_tun_key.tun_id))
		goto out;

	if (rule->l3mdev && !l3mdev_fib_rule_match(rule->fr_net, fl, arg))
		goto out;

	if (uid_lt(fl->flowi_uid, rule->uid_range.start) ||
	    uid_gt(fl->flowi_uid, rule->uid_range.end))
		goto out;

	ret = INDIRECT_CALL_MT(ops->match,
			       fib6_rule_match,
			       fib4_rule_match,
			       rule, fl, flags);
out:
	return (rule->flags & FIB_RULE_INVERT) ? !ret : ret;
}

int fib_rules_lookup(struct fib_rules_ops *ops, struct flowi *fl,
		     int flags, struct fib_lookup_arg *arg)
{
	struct fib_rule *rule;
	int err;

	rcu_read_lock();

	list_for_each_entry_rcu(rule, &ops->rules_list, list) {
jumped:
		if (!fib_rule_match(rule, ops, fl, flags, arg))
			continue;

		if (rule->action == FR_ACT_GOTO) {
			struct fib_rule *target;

			target = rcu_dereference(rule->ctarget);
			if (target == NULL) {
				continue;
			} else {
				rule = target;
				goto jumped;
			}
		} else if (rule->action == FR_ACT_NOP)
			continue;
		else
			err = INDIRECT_CALL_MT(ops->action,
					       fib6_rule_action,
					       fib4_rule_action,
					       rule, fl, flags, arg);

		if (!err && ops->suppress && INDIRECT_CALL_MT(ops->suppress,
							      fib6_rule_suppress,
							      fib4_rule_suppress,
							      rule, flags, arg))
			continue;

		if (err != -EAGAIN) {
			if ((arg->flags & FIB_LOOKUP_NOREF) ||
			    likely(refcount_inc_not_zero(&rule->refcnt))) {
				arg->rule = rule;
				goto out;
			}
			break;
		}
	}

	err = -ESRCH;
out:
	rcu_read_unlock();

	return err;
}
EXPORT_SYMBOL_GPL(fib_rules_lookup);

static int call_fib_rule_notifier(struct notifier_block *nb,
				  enum fib_event_type event_type,
				  struct fib_rule *rule, int family,
				  struct netlink_ext_ack *extack)
{
	struct fib_rule_notifier_info info = {
		.info.family = family,
		.info.extack = extack,
		.rule = rule,
	};

	return call_fib_notifier(nb, event_type, &info.info);
}

static int call_fib_rule_notifiers(struct net *net,
				   enum fib_event_type event_type,
				   struct fib_rule *rule,
				   struct fib_rules_ops *ops,
				   struct netlink_ext_ack *extack)
{
	struct fib_rule_notifier_info info = {
		.info.family = ops->family,
		.info.extack = extack,
		.rule = rule,
	};

	ASSERT_RTNL();
	/* Paired with READ_ONCE() in fib_rules_seq() */
	WRITE_ONCE(ops->fib_rules_seq, ops->fib_rules_seq + 1);
	return call_fib_notifiers(net, event_type, &info.info);
}

/* Called with rcu_read_lock() */
int fib_rules_dump(struct net *net, struct notifier_block *nb, int family,
		   struct netlink_ext_ack *extack)
{
	struct fib_rules_ops *ops;
	struct fib_rule *rule;
	int err = 0;

	ops = lookup_rules_ops(net, family);
	if (!ops)
		return -EAFNOSUPPORT;
	list_for_each_entry_rcu(rule, &ops->rules_list, list) {
		err = call_fib_rule_notifier(nb, FIB_EVENT_RULE_ADD,
					     rule, family, extack);
		if (err)
			break;
	}
	rules_ops_put(ops);

	return err;
}
EXPORT_SYMBOL_GPL(fib_rules_dump);

unsigned int fib_rules_seq_read(const struct net *net, int family)
{
	unsigned int fib_rules_seq;
	struct fib_rules_ops *ops;

	ops = lookup_rules_ops(net, family);
	if (!ops)
		return 0;
	/* Paired with WRITE_ONCE() in call_fib_rule_notifiers() */
	fib_rules_seq = READ_ONCE(ops->fib_rules_seq);
	rules_ops_put(ops);

	return fib_rules_seq;
}
EXPORT_SYMBOL_GPL(fib_rules_seq_read);

static struct fib_rule *rule_find(struct fib_rules_ops *ops,
				  struct fib_rule_hdr *frh,
				  struct nlattr **tb,
				  struct fib_rule *rule,
				  bool user_priority)
{
	struct fib_rule *r;

	list_for_each_entry(r, &ops->rules_list, list) {
		if (rule->action && r->action != rule->action)
			continue;

		if (rule->table && r->table != rule->table)
			continue;

		if (user_priority && r->pref != rule->pref)
			continue;

		if (rule->iifname[0] &&
		    memcmp(r->iifname, rule->iifname, IFNAMSIZ))
			continue;

		if (rule->oifname[0] &&
		    memcmp(r->oifname, rule->oifname, IFNAMSIZ))
			continue;

		if (rule->mark && r->mark != rule->mark)
			continue;

		if (rule->suppress_ifgroup != -1 &&
		    r->suppress_ifgroup != rule->suppress_ifgroup)
			continue;

		if (rule->suppress_prefixlen != -1 &&
		    r->suppress_prefixlen != rule->suppress_prefixlen)
			continue;

		if (rule->mark_mask && r->mark_mask != rule->mark_mask)
			continue;

		if (rule->tun_id && r->tun_id != rule->tun_id)
			continue;

		if (r->fr_net != rule->fr_net)
			continue;

		if (rule->l3mdev && r->l3mdev != rule->l3mdev)
			continue;

		if (uid_range_set(&rule->uid_range) &&
		    (!uid_eq(r->uid_range.start, rule->uid_range.start) ||
		    !uid_eq(r->uid_range.end, rule->uid_range.end)))
			continue;

		if (rule->ip_proto && r->ip_proto != rule->ip_proto)
			continue;

		if (rule->proto && r->proto != rule->proto)
			continue;

		if (fib_rule_port_range_set(&rule->sport_range) &&
		    !fib_rule_port_range_compare(&r->sport_range,
						 &rule->sport_range))
			continue;

		if (fib_rule_port_range_set(&rule->dport_range) &&
		    !fib_rule_port_range_compare(&r->dport_range,
						 &rule->dport_range))
			continue;

		if (!ops->compare(r, frh, tb))
			continue;
		return r;
	}

	return NULL;
}

#ifdef CONFIG_NET_L3_MASTER_DEV
static int fib_nl2rule_l3mdev(struct nlattr *nla, struct fib_rule *nlrule,
			      struct netlink_ext_ack *extack)
{
	nlrule->l3mdev = nla_get_u8(nla);
	if (nlrule->l3mdev != 1) {
		NL_SET_ERR_MSG(extack, "Invalid l3mdev attribute");
		return -1;
	}

	return 0;
}
#else
static int fib_nl2rule_l3mdev(struct nlattr *nla, struct fib_rule *nlrule,
			      struct netlink_ext_ack *extack)
{
	NL_SET_ERR_MSG(extack, "l3mdev support is not enabled in kernel");
	return -1;
}
#endif

static int fib_nl2rule(struct sk_buff *skb, struct nlmsghdr *nlh,
		       struct netlink_ext_ack *extack,
		       struct fib_rules_ops *ops,
		       struct nlattr *tb[],
		       struct fib_rule **rule,
		       bool *user_priority)
{
	struct net *net = sock_net(skb->sk);
	struct fib_rule_hdr *frh = nlmsg_data(nlh);
	struct fib_rule *nlrule = NULL;
	int err = -EINVAL;

	if (frh->src_len)
		if (!tb[FRA_SRC] ||
		    frh->src_len > (ops->addr_size * 8) ||
		    nla_len(tb[FRA_SRC]) != ops->addr_size) {
			NL_SET_ERR_MSG(extack, "Invalid source address");
			goto errout;
	}

	if (frh->dst_len)
		if (!tb[FRA_DST] ||
		    frh->dst_len > (ops->addr_size * 8) ||
		    nla_len(tb[FRA_DST]) != ops->addr_size) {
			NL_SET_ERR_MSG(extack, "Invalid dst address");
			goto errout;
	}

	nlrule = kzalloc(ops->rule_size, GFP_KERNEL_ACCOUNT);
	if (!nlrule) {
		err = -ENOMEM;
		goto errout;
	}
	refcount_set(&nlrule->refcnt, 1);
	nlrule->fr_net = net;

	if (tb[FRA_PRIORITY]) {
		nlrule->pref = nla_get_u32(tb[FRA_PRIORITY]);
		*user_priority = true;
	} else {
		nlrule->pref = fib_default_rule_pref(ops);
	}

	nlrule->proto = nla_get_u8_default(tb[FRA_PROTOCOL], RTPROT_UNSPEC);

	if (tb[FRA_IIFNAME]) {
		struct net_device *dev;

		nlrule->iifindex = -1;
		nla_strscpy(nlrule->iifname, tb[FRA_IIFNAME], IFNAMSIZ);
		dev = __dev_get_by_name(net, nlrule->iifname);
		if (dev)
			nlrule->iifindex = dev->ifindex;
	}

	if (tb[FRA_OIFNAME]) {
		struct net_device *dev;

		nlrule->oifindex = -1;
		nla_strscpy(nlrule->oifname, tb[FRA_OIFNAME], IFNAMSIZ);
		dev = __dev_get_by_name(net, nlrule->oifname);
		if (dev)
			nlrule->oifindex = dev->ifindex;
	}

	if (tb[FRA_FWMARK]) {
		nlrule->mark = nla_get_u32(tb[FRA_FWMARK]);
		if (nlrule->mark)
			/* compatibility: if the mark value is non-zero all bits
			 * are compared unless a mask is explicitly specified.
			 */
			nlrule->mark_mask = 0xFFFFFFFF;
	}

	if (tb[FRA_FWMASK])
		nlrule->mark_mask = nla_get_u32(tb[FRA_FWMASK]);

	if (tb[FRA_TUN_ID])
		nlrule->tun_id = nla_get_be64(tb[FRA_TUN_ID]);

	if (tb[FRA_L3MDEV] &&
	    fib_nl2rule_l3mdev(tb[FRA_L3MDEV], nlrule, extack) < 0)
		goto errout_free;

	nlrule->action = frh->action;
	nlrule->flags = frh->flags;
	nlrule->table = frh_get_table(frh, tb);
	if (tb[FRA_SUPPRESS_PREFIXLEN])
		nlrule->suppress_prefixlen = nla_get_u32(tb[FRA_SUPPRESS_PREFIXLEN]);
	else
		nlrule->suppress_prefixlen = -1;

	if (tb[FRA_SUPPRESS_IFGROUP])
		nlrule->suppress_ifgroup = nla_get_u32(tb[FRA_SUPPRESS_IFGROUP]);
	else
		nlrule->suppress_ifgroup = -1;

	if (tb[FRA_GOTO]) {
		if (nlrule->action != FR_ACT_GOTO) {
			NL_SET_ERR_MSG(extack, "Unexpected goto");
			goto errout_free;
		}

		nlrule->target = nla_get_u32(tb[FRA_GOTO]);
		/* Backward jumps are prohibited to avoid endless loops */
		if (nlrule->target <= nlrule->pref) {
			NL_SET_ERR_MSG(extack, "Backward goto not supported");
			goto errout_free;
		}
	} else if (nlrule->action == FR_ACT_GOTO) {
		NL_SET_ERR_MSG(extack, "Missing goto target for action goto");
		goto errout_free;
	}

	if (nlrule->l3mdev && nlrule->table) {
		NL_SET_ERR_MSG(extack, "l3mdev and table are mutually exclusive");
		goto errout_free;
	}

	if (tb[FRA_UID_RANGE]) {
		if (current_user_ns() != net->user_ns) {
			err = -EPERM;
			NL_SET_ERR_MSG(extack, "No permission to set uid");
			goto errout_free;
		}

		nlrule->uid_range = nla_get_kuid_range(tb);

		if (!uid_range_set(&nlrule->uid_range) ||
		    !uid_lte(nlrule->uid_range.start, nlrule->uid_range.end)) {
			NL_SET_ERR_MSG(extack, "Invalid uid range");
			goto errout_free;
		}
	} else {
		nlrule->uid_range = fib_kuid_range_unset;
	}

	if (tb[FRA_IP_PROTO])
		nlrule->ip_proto = nla_get_u8(tb[FRA_IP_PROTO]);

	if (tb[FRA_SPORT_RANGE]) {
		err = nla_get_port_range(tb[FRA_SPORT_RANGE],
					 &nlrule->sport_range);
		if (err) {
			NL_SET_ERR_MSG(extack, "Invalid sport range");
			goto errout_free;
		}
	}

	if (tb[FRA_DPORT_RANGE]) {
		err = nla_get_port_range(tb[FRA_DPORT_RANGE],
					 &nlrule->dport_range);
		if (err) {
			NL_SET_ERR_MSG(extack, "Invalid dport range");
			goto errout_free;
		}
	}

	*rule = nlrule;

	return 0;

errout_free:
	kfree(nlrule);
errout:
	return err;
}

static int rule_exists(struct fib_rules_ops *ops, struct fib_rule_hdr *frh,
		       struct nlattr **tb, struct fib_rule *rule)
{
	struct fib_rule *r;

	list_for_each_entry(r, &ops->rules_list, list) {
		if (r->action != rule->action)
			continue;

		if (r->table != rule->table)
			continue;

		if (r->pref != rule->pref)
			continue;

		if (memcmp(r->iifname, rule->iifname, IFNAMSIZ))
			continue;

		if (memcmp(r->oifname, rule->oifname, IFNAMSIZ))
			continue;

		if (r->mark != rule->mark)
			continue;

		if (r->suppress_ifgroup != rule->suppress_ifgroup)
			continue;

		if (r->suppress_prefixlen != rule->suppress_prefixlen)
			continue;

		if (r->mark_mask != rule->mark_mask)
			continue;

		if (r->tun_id != rule->tun_id)
			continue;

		if (r->fr_net != rule->fr_net)
			continue;

		if (r->l3mdev != rule->l3mdev)
			continue;

		if (!uid_eq(r->uid_range.start, rule->uid_range.start) ||
		    !uid_eq(r->uid_range.end, rule->uid_range.end))
			continue;

		if (r->ip_proto != rule->ip_proto)
			continue;

		if (r->proto != rule->proto)
			continue;

		if (!fib_rule_port_range_compare(&r->sport_range,
						 &rule->sport_range))
			continue;

		if (!fib_rule_port_range_compare(&r->dport_range,
						 &rule->dport_range))
			continue;

		if (!ops->compare(r, frh, tb))
			continue;
		return 1;
	}
	return 0;
}

static const struct nla_policy fib_rule_policy[FRA_MAX + 1] = {
	[FRA_UNSPEC]	= { .strict_start_type = FRA_DPORT_RANGE + 1 },
	[FRA_IIFNAME]	= { .type = NLA_STRING, .len = IFNAMSIZ - 1 },
	[FRA_OIFNAME]	= { .type = NLA_STRING, .len = IFNAMSIZ - 1 },
	[FRA_PRIORITY]	= { .type = NLA_U32 },
	[FRA_FWMARK]	= { .type = NLA_U32 },
	[FRA_FLOW]	= { .type = NLA_U32 },
	[FRA_TUN_ID]	= { .type = NLA_U64 },
	[FRA_FWMASK]	= { .type = NLA_U32 },
	[FRA_TABLE]     = { .type = NLA_U32 },
	[FRA_SUPPRESS_PREFIXLEN] = { .type = NLA_U32 },
	[FRA_SUPPRESS_IFGROUP] = { .type = NLA_U32 },
	[FRA_GOTO]	= { .type = NLA_U32 },
	[FRA_L3MDEV]	= { .type = NLA_U8 },
	[FRA_UID_RANGE]	= { .len = sizeof(struct fib_rule_uid_range) },
	[FRA_PROTOCOL]  = { .type = NLA_U8 },
	[FRA_IP_PROTO]  = { .type = NLA_U8 },
	[FRA_SPORT_RANGE] = { .len = sizeof(struct fib_rule_port_range) },
	[FRA_DPORT_RANGE] = { .len = sizeof(struct fib_rule_port_range) },
	[FRA_DSCP]	= NLA_POLICY_MAX(NLA_U8, INET_DSCP_MASK >> 2),
};

int fib_nl_newrule(struct sk_buff *skb, struct nlmsghdr *nlh,
		   struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct fib_rule_hdr *frh = nlmsg_data(nlh);
	struct fib_rules_ops *ops = NULL;
	struct fib_rule *rule = NULL, *r, *last = NULL;
	struct nlattr *tb[FRA_MAX + 1];
	int err = -EINVAL, unresolved = 0;
	bool user_priority = false;

	if (nlh->nlmsg_len < nlmsg_msg_size(sizeof(*frh))) {
		NL_SET_ERR_MSG(extack, "Invalid msg length");
		goto errout;
	}

	ops = lookup_rules_ops(net, frh->family);
	if (!ops) {
		err = -EAFNOSUPPORT;
		NL_SET_ERR_MSG(extack, "Rule family not supported");
		goto errout;
	}

	err = nlmsg_parse_deprecated(nlh, sizeof(*frh), tb, FRA_MAX,
				     fib_rule_policy, extack);
	if (err < 0) {
		NL_SET_ERR_MSG(extack, "Error parsing msg");
		goto errout;
	}

	err = fib_nl2rule(skb, nlh, extack, ops, tb, &rule, &user_priority);
	if (err)
		goto errout;

	if ((nlh->nlmsg_flags & NLM_F_EXCL) &&
	    rule_exists(ops, frh, tb, rule)) {
		err = -EEXIST;
		goto errout_free;
	}

	err = ops->configure(rule, skb, frh, tb, extack);
	if (err < 0)
		goto errout_free;

	err = call_fib_rule_notifiers(net, FIB_EVENT_RULE_ADD, rule, ops,
				      extack);
	if (err < 0)
		goto errout_free;

	list_for_each_entry(r, &ops->rules_list, list) {
		if (r->pref == rule->target) {
			RCU_INIT_POINTER(rule->ctarget, r);
			break;
		}
	}

	if (rcu_dereference_protected(rule->ctarget, 1) == NULL)
		unresolved = 1;

	list_for_each_entry(r, &ops->rules_list, list) {
		if (r->pref > rule->pref)
			break;
		last = r;
	}

	if (last)
		list_add_rcu(&rule->list, &last->list);
	else
		list_add_rcu(&rule->list, &ops->rules_list);

	if (ops->unresolved_rules) {
		/*
		 * There are unresolved goto rules in the list, check if
		 * any of them are pointing to this new rule.
		 */
		list_for_each_entry(r, &ops->rules_list, list) {
			if (r->action == FR_ACT_GOTO &&
			    r->target == rule->pref &&
			    rtnl_dereference(r->ctarget) == NULL) {
				rcu_assign_pointer(r->ctarget, rule);
				if (--ops->unresolved_rules == 0)
					break;
			}
		}
	}

	if (rule->action == FR_ACT_GOTO)
		ops->nr_goto_rules++;

	if (unresolved)
		ops->unresolved_rules++;

	if (rule->tun_id)
		ip_tunnel_need_metadata();

	notify_rule_change(RTM_NEWRULE, rule, ops, nlh, NETLINK_CB(skb).portid);
	flush_route_cache(ops);
	rules_ops_put(ops);
	return 0;

errout_free:
	kfree(rule);
errout:
	rules_ops_put(ops);
	return err;
}
EXPORT_SYMBOL_GPL(fib_nl_newrule);

int fib_nl_delrule(struct sk_buff *skb, struct nlmsghdr *nlh,
		   struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct fib_rule_hdr *frh = nlmsg_data(nlh);
	struct fib_rules_ops *ops = NULL;
	struct fib_rule *rule = NULL, *r, *nlrule = NULL;
	struct nlattr *tb[FRA_MAX+1];
	int err = -EINVAL;
	bool user_priority = false;

	if (nlh->nlmsg_len < nlmsg_msg_size(sizeof(*frh))) {
		NL_SET_ERR_MSG(extack, "Invalid msg length");
		goto errout;
	}

	ops = lookup_rules_ops(net, frh->family);
	if (ops == NULL) {
		err = -EAFNOSUPPORT;
		NL_SET_ERR_MSG(extack, "Rule family not supported");
		goto errout;
	}

	err = nlmsg_parse_deprecated(nlh, sizeof(*frh), tb, FRA_MAX,
				     fib_rule_policy, extack);
	if (err < 0) {
		NL_SET_ERR_MSG(extack, "Error parsing msg");
		goto errout;
	}

	err = fib_nl2rule(skb, nlh, extack, ops, tb, &nlrule, &user_priority);
	if (err)
		goto errout;

	rule = rule_find(ops, frh, tb, nlrule, user_priority);
	if (!rule) {
		err = -ENOENT;
		goto errout;
	}

	if (rule->flags & FIB_RULE_PERMANENT) {
		err = -EPERM;
		goto errout;
	}

	if (ops->delete) {
		err = ops->delete(rule);
		if (err)
			goto errout;
	}

	if (rule->tun_id)
		ip_tunnel_unneed_metadata();

	list_del_rcu(&rule->list);

	if (rule->action == FR_ACT_GOTO) {
		ops->nr_goto_rules--;
		if (rtnl_dereference(rule->ctarget) == NULL)
			ops->unresolved_rules--;
	}

	/*
	 * Check if this rule is a target to any of them. If so,
	 * adjust to the next one with the same preference or
	 * disable them. As this operation is eventually very
	 * expensive, it is only performed if goto rules, except
	 * current if it is goto rule, have actually been added.
	 */
	if (ops->nr_goto_rules > 0) {
		struct fib_rule *n;

		n = list_next_entry(rule, list);
		if (&n->list == &ops->rules_list || n->pref != rule->pref)
			n = NULL;
		list_for_each_entry(r, &ops->rules_list, list) {
			if (rtnl_dereference(r->ctarget) != rule)
				continue;
			rcu_assign_pointer(r->ctarget, n);
			if (!n)
				ops->unresolved_rules++;
		}
	}

	call_fib_rule_notifiers(net, FIB_EVENT_RULE_DEL, rule, ops,
				NULL);
	notify_rule_change(RTM_DELRULE, rule, ops, nlh,
			   NETLINK_CB(skb).portid);
	fib_rule_put(rule);
	flush_route_cache(ops);
	rules_ops_put(ops);
	kfree(nlrule);
	return 0;

errout:
	kfree(nlrule);
	rules_ops_put(ops);
	return err;
}
EXPORT_SYMBOL_GPL(fib_nl_delrule);

static inline size_t fib_rule_nlmsg_size(struct fib_rules_ops *ops,
					 struct fib_rule *rule)
{
	size_t payload = NLMSG_ALIGN(sizeof(struct fib_rule_hdr))
			 + nla_total_size(IFNAMSIZ) /* FRA_IIFNAME */
			 + nla_total_size(IFNAMSIZ) /* FRA_OIFNAME */
			 + nla_total_size(4) /* FRA_PRIORITY */
			 + nla_total_size(4) /* FRA_TABLE */
			 + nla_total_size(4) /* FRA_SUPPRESS_PREFIXLEN */
			 + nla_total_size(4) /* FRA_SUPPRESS_IFGROUP */
			 + nla_total_size(4) /* FRA_FWMARK */
			 + nla_total_size(4) /* FRA_FWMASK */
			 + nla_total_size_64bit(8) /* FRA_TUN_ID */
			 + nla_total_size(sizeof(struct fib_kuid_range))
			 + nla_total_size(1) /* FRA_PROTOCOL */
			 + nla_total_size(1) /* FRA_IP_PROTO */
			 + nla_total_size(sizeof(struct fib_rule_port_range)) /* FRA_SPORT_RANGE */
			 + nla_total_size(sizeof(struct fib_rule_port_range)); /* FRA_DPORT_RANGE */

	if (ops->nlmsg_payload)
		payload += ops->nlmsg_payload(rule);

	return payload;
}

static int fib_nl_fill_rule(struct sk_buff *skb, struct fib_rule *rule,
			    u32 pid, u32 seq, int type, int flags,
			    struct fib_rules_ops *ops)
{
	struct nlmsghdr *nlh;
	struct fib_rule_hdr *frh;

	nlh = nlmsg_put(skb, pid, seq, type, sizeof(*frh), flags);
	if (nlh == NULL)
		return -EMSGSIZE;

	frh = nlmsg_data(nlh);
	frh->family = ops->family;
	frh->table = rule->table < 256 ? rule->table : RT_TABLE_COMPAT;
	if (nla_put_u32(skb, FRA_TABLE, rule->table))
		goto nla_put_failure;
	if (nla_put_u32(skb, FRA_SUPPRESS_PREFIXLEN, rule->suppress_prefixlen))
		goto nla_put_failure;
	frh->res1 = 0;
	frh->res2 = 0;
	frh->action = rule->action;
	frh->flags = rule->flags;

	if (nla_put_u8(skb, FRA_PROTOCOL, rule->proto))
		goto nla_put_failure;

	if (rule->action == FR_ACT_GOTO &&
	    rcu_access_pointer(rule->ctarget) == NULL)
		frh->flags |= FIB_RULE_UNRESOLVED;

	if (rule->iifname[0]) {
		if (nla_put_string(skb, FRA_IIFNAME, rule->iifname))
			goto nla_put_failure;
		if (rule->iifindex == -1)
			frh->flags |= FIB_RULE_IIF_DETACHED;
	}

	if (rule->oifname[0]) {
		if (nla_put_string(skb, FRA_OIFNAME, rule->oifname))
			goto nla_put_failure;
		if (rule->oifindex == -1)
			frh->flags |= FIB_RULE_OIF_DETACHED;
	}

	if ((rule->pref &&
	     nla_put_u32(skb, FRA_PRIORITY, rule->pref)) ||
	    (rule->mark &&
	     nla_put_u32(skb, FRA_FWMARK, rule->mark)) ||
	    ((rule->mark_mask || rule->mark) &&
	     nla_put_u32(skb, FRA_FWMASK, rule->mark_mask)) ||
	    (rule->target &&
	     nla_put_u32(skb, FRA_GOTO, rule->target)) ||
	    (rule->tun_id &&
	     nla_put_be64(skb, FRA_TUN_ID, rule->tun_id, FRA_PAD)) ||
	    (rule->l3mdev &&
	     nla_put_u8(skb, FRA_L3MDEV, rule->l3mdev)) ||
	    (uid_range_set(&rule->uid_range) &&
	     nla_put_uid_range(skb, &rule->uid_range)) ||
	    (fib_rule_port_range_set(&rule->sport_range) &&
	     nla_put_port_range(skb, FRA_SPORT_RANGE, &rule->sport_range)) ||
	    (fib_rule_port_range_set(&rule->dport_range) &&
	     nla_put_port_range(skb, FRA_DPORT_RANGE, &rule->dport_range)) ||
	    (rule->ip_proto && nla_put_u8(skb, FRA_IP_PROTO, rule->ip_proto)))
		goto nla_put_failure;

	if (rule->suppress_ifgroup != -1) {
		if (nla_put_u32(skb, FRA_SUPPRESS_IFGROUP, rule->suppress_ifgroup))
			goto nla_put_failure;
	}

	if (ops->fill(rule, skb, frh) < 0)
		goto nla_put_failure;

	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static int dump_rules(struct sk_buff *skb, struct netlink_callback *cb,
		      struct fib_rules_ops *ops)
{
	int idx = 0;
	struct fib_rule *rule;
	int err = 0;

	rcu_read_lock();
	list_for_each_entry_rcu(rule, &ops->rules_list, list) {
		if (idx < cb->args[1])
			goto skip;

		err = fib_nl_fill_rule(skb, rule, NETLINK_CB(cb->skb).portid,
				       cb->nlh->nlmsg_seq, RTM_NEWRULE,
				       NLM_F_MULTI, ops);
		if (err)
			break;
skip:
		idx++;
	}
	rcu_read_unlock();
	cb->args[1] = idx;
	rules_ops_put(ops);

	return err;
}

static int fib_valid_dumprule_req(const struct nlmsghdr *nlh,
				   struct netlink_ext_ack *extack)
{
	struct fib_rule_hdr *frh;

	if (nlh->nlmsg_len < nlmsg_msg_size(sizeof(*frh))) {
		NL_SET_ERR_MSG(extack, "Invalid header for fib rule dump request");
		return -EINVAL;
	}

	frh = nlmsg_data(nlh);
	if (frh->dst_len || frh->src_len || frh->tos || frh->table ||
	    frh->res1 || frh->res2 || frh->action || frh->flags) {
		NL_SET_ERR_MSG(extack,
			       "Invalid values in header for fib rule dump request");
		return -EINVAL;
	}

	if (nlmsg_attrlen(nlh, sizeof(*frh))) {
		NL_SET_ERR_MSG(extack, "Invalid data after header in fib rule dump request");
		return -EINVAL;
	}

	return 0;
}

static int fib_nl_dumprule(struct sk_buff *skb, struct netlink_callback *cb)
{
	const struct nlmsghdr *nlh = cb->nlh;
	struct net *net = sock_net(skb->sk);
	struct fib_rules_ops *ops;
	int err, idx = 0, family;

	if (cb->strict_check) {
		err = fib_valid_dumprule_req(nlh, cb->extack);

		if (err < 0)
			return err;
	}

	family = rtnl_msg_family(nlh);
	if (family != AF_UNSPEC) {
		/* Protocol specific dump request */
		ops = lookup_rules_ops(net, family);
		if (ops == NULL)
			return -EAFNOSUPPORT;

		return dump_rules(skb, cb, ops);
	}

	err = 0;
	rcu_read_lock();
	list_for_each_entry_rcu(ops, &net->rules_ops, list) {
		if (idx < cb->args[0] || !try_module_get(ops->owner))
			goto skip;

		err = dump_rules(skb, cb, ops);
		if (err < 0)
			break;

		cb->args[1] = 0;
skip:
		idx++;
	}
	rcu_read_unlock();
	cb->args[0] = idx;

	return err;
}

static void notify_rule_change(int event, struct fib_rule *rule,
			       struct fib_rules_ops *ops, struct nlmsghdr *nlh,
			       u32 pid)
{
	struct net *net;
	struct sk_buff *skb;
	int err = -ENOMEM;

	net = ops->fro_net;
	skb = nlmsg_new(fib_rule_nlmsg_size(ops, rule), GFP_KERNEL);
	if (skb == NULL)
		goto errout;

	err = fib_nl_fill_rule(skb, rule, pid, nlh->nlmsg_seq, event, 0, ops);
	if (err < 0) {
		/* -EMSGSIZE implies BUG in fib_rule_nlmsg_size() */
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(skb);
		goto errout;
	}

	rtnl_notify(skb, net, pid, ops->nlgroup, nlh, GFP_KERNEL);
	return;
errout:
	rtnl_set_sk_err(net, ops->nlgroup, err);
}

static void attach_rules(struct list_head *rules, struct net_device *dev)
{
	struct fib_rule *rule;

	list_for_each_entry(rule, rules, list) {
		if (rule->iifindex == -1 &&
		    strcmp(dev->name, rule->iifname) == 0)
			rule->iifindex = dev->ifindex;
		if (rule->oifindex == -1 &&
		    strcmp(dev->name, rule->oifname) == 0)
			rule->oifindex = dev->ifindex;
	}
}

static void detach_rules(struct list_head *rules, struct net_device *dev)
{
	struct fib_rule *rule;

	list_for_each_entry(rule, rules, list) {
		if (rule->iifindex == dev->ifindex)
			rule->iifindex = -1;
		if (rule->oifindex == dev->ifindex)
			rule->oifindex = -1;
	}
}


static int fib_rules_event(struct notifier_block *this, unsigned long event,
			   void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct net *net = dev_net(dev);
	struct fib_rules_ops *ops;

	ASSERT_RTNL();

	switch (event) {
	case NETDEV_REGISTER:
		list_for_each_entry(ops, &net->rules_ops, list)
			attach_rules(&ops->rules_list, dev);
		break;

	case NETDEV_CHANGENAME:
		list_for_each_entry(ops, &net->rules_ops, list) {
			detach_rules(&ops->rules_list, dev);
			attach_rules(&ops->rules_list, dev);
		}
		break;

	case NETDEV_UNREGISTER:
		list_for_each_entry(ops, &net->rules_ops, list)
			detach_rules(&ops->rules_list, dev);
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block fib_rules_notifier = {
	.notifier_call = fib_rules_event,
};

static int __net_init fib_rules_net_init(struct net *net)
{
	INIT_LIST_HEAD(&net->rules_ops);
	spin_lock_init(&net->rules_mod_lock);
	return 0;
}

static void __net_exit fib_rules_net_exit(struct net *net)
{
	WARN_ON_ONCE(!list_empty(&net->rules_ops));
}

static struct pernet_operations fib_rules_net_ops = {
	.init = fib_rules_net_init,
	.exit = fib_rules_net_exit,
};

static const struct rtnl_msg_handler fib_rules_rtnl_msg_handlers[] __initconst = {
	{.msgtype = RTM_NEWRULE, .doit = fib_nl_newrule},
	{.msgtype = RTM_DELRULE, .doit = fib_nl_delrule},
	{.msgtype = RTM_GETRULE, .dumpit = fib_nl_dumprule,
	 .flags = RTNL_FLAG_DUMP_UNLOCKED},
};

static int __init fib_rules_init(void)
{
	int err;

	rtnl_register_many(fib_rules_rtnl_msg_handlers);

	err = register_pernet_subsys(&fib_rules_net_ops);
	if (err < 0)
		goto fail;

	err = register_netdevice_notifier(&fib_rules_notifier);
	if (err < 0)
		goto fail_unregister;

	return 0;

fail_unregister:
	unregister_pernet_subsys(&fib_rules_net_ops);
fail:
	rtnl_unregister_many(fib_rules_rtnl_msg_handlers);
	return err;
}

subsys_initcall(fib_rules_init);
