/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_FIB_RULES_H
#define __NET_FIB_RULES_H

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/fib_rules.h>
#include <linux/refcount.h>
#include <net/flow.h>
#include <net/rtnetlink.h>
#include <net/fib_notifier.h>
#include <linux/indirect_call_wrapper.h>

struct fib_kuid_range {
	kuid_t start;
	kuid_t end;
};

struct fib_rule {
	struct list_head	list;
	int			iifindex;
	int			oifindex;
	u32			mark;
	u32			mark_mask;
	u32			flags;
	u32			table;
	u8			action;
	u8			l3mdev;
	u8                      proto;
	u8			ip_proto;
	u32			target;
	__be64			tun_id;
	struct fib_rule __rcu	*ctarget;
	struct net		*fr_net;

	refcount_t		refcnt;
	u32			pref;
	int			suppress_ifgroup;
	int			suppress_prefixlen;
	char			iifname[IFNAMSIZ];
	char			oifname[IFNAMSIZ];
	struct fib_kuid_range	uid_range;
	struct fib_rule_port_range	sport_range;
	struct fib_rule_port_range	dport_range;
	u16			sport_mask;
	u16			dport_mask;
	u8                      iif_is_l3_master;
	u8                      oif_is_l3_master;
	struct rcu_head		rcu;
};

struct fib_lookup_arg {
	void			*lookup_ptr;
	const void		*lookup_data;
	void			*result;
	struct fib_rule		*rule;
	u32			table;
	int			flags;
#define FIB_LOOKUP_NOREF		1
#define FIB_LOOKUP_IGNORE_LINKSTATE	2
};

struct fib_rules_ops {
	int			family;
	struct list_head	list;
	int			rule_size;
	int			addr_size;
	int			unresolved_rules;
	int			nr_goto_rules;
	unsigned int		fib_rules_seq;

	int			(*action)(struct fib_rule *,
					  struct flowi *, int,
					  struct fib_lookup_arg *);
	bool			(*suppress)(struct fib_rule *, int,
					    struct fib_lookup_arg *);
	int			(*match)(struct fib_rule *,
					 struct flowi *, int);
	int			(*configure)(struct fib_rule *,
					     struct sk_buff *,
					     struct fib_rule_hdr *,
					     struct nlattr **,
					     struct netlink_ext_ack *);
	int			(*delete)(struct fib_rule *);
	int			(*compare)(struct fib_rule *,
					   struct fib_rule_hdr *,
					   struct nlattr **);
	int			(*fill)(struct fib_rule *, struct sk_buff *,
					struct fib_rule_hdr *);
	size_t			(*nlmsg_payload)(struct fib_rule *);

	/* Called after modifications to the rules set, must flush
	 * the route cache if one exists. */
	void			(*flush_cache)(struct fib_rules_ops *ops);

	int			nlgroup;
	struct list_head	rules_list;
	struct module		*owner;
	struct net		*fro_net;
	struct rcu_head		rcu;
};

struct fib_rule_notifier_info {
	struct fib_notifier_info info; /* must be first */
	struct fib_rule *rule;
};

static inline void fib_rule_get(struct fib_rule *rule)
{
	refcount_inc(&rule->refcnt);
}

static inline void fib_rule_put(struct fib_rule *rule)
{
	if (refcount_dec_and_test(&rule->refcnt))
		kfree_rcu(rule, rcu);
}

#ifdef CONFIG_NET_L3_MASTER_DEV
static inline u32 fib_rule_get_table(struct fib_rule *rule,
				     struct fib_lookup_arg *arg)
{
	return rule->l3mdev ? arg->table : rule->table;
}
#else
static inline u32 fib_rule_get_table(struct fib_rule *rule,
				     struct fib_lookup_arg *arg)
{
	return rule->table;
}
#endif

static inline u32 frh_get_table(struct fib_rule_hdr *frh, struct nlattr **nla)
{
	if (nla[FRA_TABLE])
		return nla_get_u32(nla[FRA_TABLE]);
	return frh->table;
}

static inline bool fib_rule_port_range_set(const struct fib_rule_port_range *range)
{
	return range->start != 0 && range->end != 0;
}

static inline bool fib_rule_port_inrange(const struct fib_rule_port_range *a,
					 __be16 port)
{
	return ntohs(port) >= a->start &&
		ntohs(port) <= a->end;
}

static inline bool fib_rule_port_match(const struct fib_rule_port_range *range,
				       u16 port_mask, __be16 port)
{
	if ((range->start ^ ntohs(port)) & port_mask)
		return false;
	if (!port_mask && fib_rule_port_range_set(range) &&
	    !fib_rule_port_inrange(range, port))
		return false;
	return true;
}

static inline bool fib_rule_port_range_valid(const struct fib_rule_port_range *a)
{
	return a->start != 0 && a->end != 0 && a->end < 0xffff &&
		a->start <= a->end;
}

static inline bool fib_rule_port_range_compare(struct fib_rule_port_range *a,
					       struct fib_rule_port_range *b)
{
	return a->start == b->start &&
		a->end == b->end;
}

static inline bool
fib_rule_port_is_range(const struct fib_rule_port_range *range)
{
	return range->start != range->end;
}

static inline bool fib_rule_requires_fldissect(struct fib_rule *rule)
{
	return rule->iifindex != LOOPBACK_IFINDEX && (rule->ip_proto ||
		fib_rule_port_range_set(&rule->sport_range) ||
		fib_rule_port_range_set(&rule->dport_range));
}

struct fib_rules_ops *fib_rules_register(const struct fib_rules_ops *,
					 struct net *);
void fib_rules_unregister(struct fib_rules_ops *);

int fib_rules_lookup(struct fib_rules_ops *, struct flowi *, int flags,
		     struct fib_lookup_arg *);
int fib_default_rule_add(struct fib_rules_ops *, u32 pref, u32 table);
bool fib_rule_matchall(const struct fib_rule *rule);
int fib_rules_dump(struct net *net, struct notifier_block *nb, int family,
		   struct netlink_ext_ack *extack);
unsigned int fib_rules_seq_read(const struct net *net, int family);

int fib_newrule(struct net *net, struct sk_buff *skb, struct nlmsghdr *nlh,
		struct netlink_ext_ack *extack, bool rtnl_held);
int fib_delrule(struct net *net, struct sk_buff *skb, struct nlmsghdr *nlh,
		struct netlink_ext_ack *extack, bool rtnl_held);

INDIRECT_CALLABLE_DECLARE(int fib6_rule_match(struct fib_rule *rule,
					    struct flowi *fl, int flags));
INDIRECT_CALLABLE_DECLARE(int fib4_rule_match(struct fib_rule *rule,
					    struct flowi *fl, int flags));

INDIRECT_CALLABLE_DECLARE(int fib6_rule_action(struct fib_rule *rule,
			    struct flowi *flp, int flags,
			    struct fib_lookup_arg *arg));
INDIRECT_CALLABLE_DECLARE(int fib4_rule_action(struct fib_rule *rule,
			    struct flowi *flp, int flags,
			    struct fib_lookup_arg *arg));

INDIRECT_CALLABLE_DECLARE(bool fib6_rule_suppress(struct fib_rule *rule,
						int flags,
						struct fib_lookup_arg *arg));
INDIRECT_CALLABLE_DECLARE(bool fib4_rule_suppress(struct fib_rule *rule,
						int flags,
						struct fib_lookup_arg *arg));
#endif
