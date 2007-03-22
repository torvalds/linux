/*
 * net/core/fib_rules.c		Generic Routing Rules
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 * Authors:	Thomas Graf <tgraf@suug.ch>
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <net/fib_rules.h>

static LIST_HEAD(rules_ops);
static DEFINE_SPINLOCK(rules_mod_lock);

static void notify_rule_change(int event, struct fib_rule *rule,
			       struct fib_rules_ops *ops, struct nlmsghdr *nlh,
			       u32 pid);

static struct fib_rules_ops *lookup_rules_ops(int family)
{
	struct fib_rules_ops *ops;

	rcu_read_lock();
	list_for_each_entry_rcu(ops, &rules_ops, list) {
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

int fib_rules_register(struct fib_rules_ops *ops)
{
	int err = -EEXIST;
	struct fib_rules_ops *o;

	if (ops->rule_size < sizeof(struct fib_rule))
		return -EINVAL;

	if (ops->match == NULL || ops->configure == NULL ||
	    ops->compare == NULL || ops->fill == NULL ||
	    ops->action == NULL)
		return -EINVAL;

	spin_lock(&rules_mod_lock);
	list_for_each_entry(o, &rules_ops, list)
		if (ops->family == o->family)
			goto errout;

	list_add_tail_rcu(&ops->list, &rules_ops);
	err = 0;
errout:
	spin_unlock(&rules_mod_lock);

	return err;
}

EXPORT_SYMBOL_GPL(fib_rules_register);

static void cleanup_ops(struct fib_rules_ops *ops)
{
	struct fib_rule *rule, *tmp;

	list_for_each_entry_safe(rule, tmp, ops->rules_list, list) {
		list_del_rcu(&rule->list);
		fib_rule_put(rule);
	}
}

int fib_rules_unregister(struct fib_rules_ops *ops)
{
	int err = 0;
	struct fib_rules_ops *o;

	spin_lock(&rules_mod_lock);
	list_for_each_entry(o, &rules_ops, list) {
		if (o == ops) {
			list_del_rcu(&o->list);
			cleanup_ops(ops);
			goto out;
		}
	}

	err = -ENOENT;
out:
	spin_unlock(&rules_mod_lock);

	synchronize_rcu();

	return err;
}

EXPORT_SYMBOL_GPL(fib_rules_unregister);

static int fib_rule_match(struct fib_rule *rule, struct fib_rules_ops *ops,
			  struct flowi *fl, int flags)
{
	int ret = 0;

	if (rule->ifindex && (rule->ifindex != fl->iif))
		goto out;

	if ((rule->mark ^ fl->mark) & rule->mark_mask)
		goto out;

	ret = ops->match(rule, fl, flags);
out:
	return (rule->flags & FIB_RULE_INVERT) ? !ret : ret;
}

int fib_rules_lookup(struct fib_rules_ops *ops, struct flowi *fl,
		     int flags, struct fib_lookup_arg *arg)
{
	struct fib_rule *rule;
	int err;

	rcu_read_lock();

	list_for_each_entry_rcu(rule, ops->rules_list, list) {
		if (!fib_rule_match(rule, ops, fl, flags))
			continue;

		err = ops->action(rule, fl, flags, arg);
		if (err != -EAGAIN) {
			fib_rule_get(rule);
			arg->rule = rule;
			goto out;
		}
	}

	err = -ENETUNREACH;
out:
	rcu_read_unlock();

	return err;
}

EXPORT_SYMBOL_GPL(fib_rules_lookup);

int fib_nl_newrule(struct sk_buff *skb, struct nlmsghdr* nlh, void *arg)
{
	struct fib_rule_hdr *frh = nlmsg_data(nlh);
	struct fib_rules_ops *ops = NULL;
	struct fib_rule *rule, *r, *last = NULL;
	struct nlattr *tb[FRA_MAX+1];
	int err = -EINVAL;

	if (nlh->nlmsg_len < nlmsg_msg_size(sizeof(*frh)))
		goto errout;

	ops = lookup_rules_ops(frh->family);
	if (ops == NULL) {
		err = EAFNOSUPPORT;
		goto errout;
	}

	err = nlmsg_parse(nlh, sizeof(*frh), tb, FRA_MAX, ops->policy);
	if (err < 0)
		goto errout;

	rule = kzalloc(ops->rule_size, GFP_KERNEL);
	if (rule == NULL) {
		err = -ENOMEM;
		goto errout;
	}

	if (tb[FRA_PRIORITY])
		rule->pref = nla_get_u32(tb[FRA_PRIORITY]);

	if (tb[FRA_IFNAME]) {
		struct net_device *dev;

		rule->ifindex = -1;
		nla_strlcpy(rule->ifname, tb[FRA_IFNAME], IFNAMSIZ);
		dev = __dev_get_by_name(rule->ifname);
		if (dev)
			rule->ifindex = dev->ifindex;
	}

	if (tb[FRA_FWMARK]) {
		rule->mark = nla_get_u32(tb[FRA_FWMARK]);
		if (rule->mark)
			/* compatibility: if the mark value is non-zero all bits
			 * are compared unless a mask is explicitly specified.
			 */
			rule->mark_mask = 0xFFFFFFFF;
	}

	if (tb[FRA_FWMASK])
		rule->mark_mask = nla_get_u32(tb[FRA_FWMASK]);

	rule->action = frh->action;
	rule->flags = frh->flags;
	rule->table = frh_get_table(frh, tb);

	if (!rule->pref && ops->default_pref)
		rule->pref = ops->default_pref();

	err = ops->configure(rule, skb, nlh, frh, tb);
	if (err < 0)
		goto errout_free;

	list_for_each_entry(r, ops->rules_list, list) {
		if (r->pref > rule->pref)
			break;
		last = r;
	}

	fib_rule_get(rule);

	if (last)
		list_add_rcu(&rule->list, &last->list);
	else
		list_add_rcu(&rule->list, ops->rules_list);

	notify_rule_change(RTM_NEWRULE, rule, ops, nlh, NETLINK_CB(skb).pid);
	rules_ops_put(ops);
	return 0;

errout_free:
	kfree(rule);
errout:
	rules_ops_put(ops);
	return err;
}

int fib_nl_delrule(struct sk_buff *skb, struct nlmsghdr* nlh, void *arg)
{
	struct fib_rule_hdr *frh = nlmsg_data(nlh);
	struct fib_rules_ops *ops = NULL;
	struct fib_rule *rule;
	struct nlattr *tb[FRA_MAX+1];
	int err = -EINVAL;

	if (nlh->nlmsg_len < nlmsg_msg_size(sizeof(*frh)))
		goto errout;

	ops = lookup_rules_ops(frh->family);
	if (ops == NULL) {
		err = EAFNOSUPPORT;
		goto errout;
	}

	err = nlmsg_parse(nlh, sizeof(*frh), tb, FRA_MAX, ops->policy);
	if (err < 0)
		goto errout;

	list_for_each_entry(rule, ops->rules_list, list) {
		if (frh->action && (frh->action != rule->action))
			continue;

		if (frh->table && (frh_get_table(frh, tb) != rule->table))
			continue;

		if (tb[FRA_PRIORITY] &&
		    (rule->pref != nla_get_u32(tb[FRA_PRIORITY])))
			continue;

		if (tb[FRA_IFNAME] &&
		    nla_strcmp(tb[FRA_IFNAME], rule->ifname))
			continue;

		if (tb[FRA_FWMARK] &&
		    (rule->mark != nla_get_u32(tb[FRA_FWMARK])))
			continue;

		if (tb[FRA_FWMASK] &&
		    (rule->mark_mask != nla_get_u32(tb[FRA_FWMASK])))
			continue;

		if (!ops->compare(rule, frh, tb))
			continue;

		if (rule->flags & FIB_RULE_PERMANENT) {
			err = -EPERM;
			goto errout;
		}

		list_del_rcu(&rule->list);
		synchronize_rcu();
		notify_rule_change(RTM_DELRULE, rule, ops, nlh,
				   NETLINK_CB(skb).pid);
		fib_rule_put(rule);
		rules_ops_put(ops);
		return 0;
	}

	err = -ENOENT;
errout:
	rules_ops_put(ops);
	return err;
}

static inline size_t fib_rule_nlmsg_size(struct fib_rules_ops *ops,
					 struct fib_rule *rule)
{
	size_t payload = NLMSG_ALIGN(sizeof(struct fib_rule_hdr))
			 + nla_total_size(IFNAMSIZ) /* FRA_IFNAME */
			 + nla_total_size(4) /* FRA_PRIORITY */
			 + nla_total_size(4) /* FRA_TABLE */
			 + nla_total_size(4) /* FRA_FWMARK */
			 + nla_total_size(4); /* FRA_FWMASK */

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
	frh->table = rule->table;
	NLA_PUT_U32(skb, FRA_TABLE, rule->table);
	frh->res1 = 0;
	frh->res2 = 0;
	frh->action = rule->action;
	frh->flags = rule->flags;

	if (rule->ifname[0])
		NLA_PUT_STRING(skb, FRA_IFNAME, rule->ifname);

	if (rule->pref)
		NLA_PUT_U32(skb, FRA_PRIORITY, rule->pref);

	if (rule->mark)
		NLA_PUT_U32(skb, FRA_FWMARK, rule->mark);

	if (rule->mark_mask || rule->mark)
		NLA_PUT_U32(skb, FRA_FWMASK, rule->mark_mask);

	if (ops->fill(rule, skb, nlh, frh) < 0)
		goto nla_put_failure;

	return nlmsg_end(skb, nlh);

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

int fib_rules_dump(struct sk_buff *skb, struct netlink_callback *cb, int family)
{
	int idx = 0;
	struct fib_rule *rule;
	struct fib_rules_ops *ops;

	ops = lookup_rules_ops(family);
	if (ops == NULL)
		return -EAFNOSUPPORT;

	rcu_read_lock();
	list_for_each_entry_rcu(rule, ops->rules_list, list) {
		if (idx < cb->args[0])
			goto skip;

		if (fib_nl_fill_rule(skb, rule, NETLINK_CB(cb->skb).pid,
				     cb->nlh->nlmsg_seq, RTM_NEWRULE,
				     NLM_F_MULTI, ops) < 0)
			break;
skip:
		idx++;
	}
	rcu_read_unlock();
	cb->args[0] = idx;
	rules_ops_put(ops);

	return skb->len;
}

EXPORT_SYMBOL_GPL(fib_rules_dump);

static void notify_rule_change(int event, struct fib_rule *rule,
			       struct fib_rules_ops *ops, struct nlmsghdr *nlh,
			       u32 pid)
{
	struct sk_buff *skb;
	int err = -ENOBUFS;

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
	err = rtnl_notify(skb, pid, ops->nlgroup, nlh, GFP_KERNEL);
errout:
	if (err < 0)
		rtnl_set_sk_err(ops->nlgroup, err);
}

static void attach_rules(struct list_head *rules, struct net_device *dev)
{
	struct fib_rule *rule;

	list_for_each_entry(rule, rules, list) {
		if (rule->ifindex == -1 &&
		    strcmp(dev->name, rule->ifname) == 0)
			rule->ifindex = dev->ifindex;
	}
}

static void detach_rules(struct list_head *rules, struct net_device *dev)
{
	struct fib_rule *rule;

	list_for_each_entry(rule, rules, list)
		if (rule->ifindex == dev->ifindex)
			rule->ifindex = -1;
}


static int fib_rules_event(struct notifier_block *this, unsigned long event,
			    void *ptr)
{
	struct net_device *dev = ptr;
	struct fib_rules_ops *ops;

	ASSERT_RTNL();
	rcu_read_lock();

	switch (event) {
	case NETDEV_REGISTER:
		list_for_each_entry(ops, &rules_ops, list)
			attach_rules(ops->rules_list, dev);
		break;

	case NETDEV_UNREGISTER:
		list_for_each_entry(ops, &rules_ops, list)
			detach_rules(ops->rules_list, dev);
		break;
	}

	rcu_read_unlock();

	return NOTIFY_DONE;
}

static struct notifier_block fib_rules_notifier = {
	.notifier_call = fib_rules_event,
};

static int __init fib_rules_init(void)
{
	return register_netdevice_notifier(&fib_rules_notifier);
}

subsys_initcall(fib_rules_init);
