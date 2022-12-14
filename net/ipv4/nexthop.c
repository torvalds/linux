// SPDX-License-Identifier: GPL-2.0
/* Generic nexthop implementation
 *
 * Copyright (c) 2017-19 Cumulus Networks
 * Copyright (c) 2017-19 David Ahern <dsa@cumulusnetworks.com>
 */

#include <linux/nexthop.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <net/arp.h>
#include <net/ipv6_stubs.h>
#include <net/lwtunnel.h>
#include <net/ndisc.h>
#include <net/nexthop.h>
#include <net/route.h>
#include <net/sock.h>

#define NH_RES_DEFAULT_IDLE_TIMER	(120 * HZ)
#define NH_RES_DEFAULT_UNBALANCED_TIMER	0	/* No forced rebalancing. */

static void remove_nexthop(struct net *net, struct nexthop *nh,
			   struct nl_info *nlinfo);

#define NH_DEV_HASHBITS  8
#define NH_DEV_HASHSIZE (1U << NH_DEV_HASHBITS)

static const struct nla_policy rtm_nh_policy_new[] = {
	[NHA_ID]		= { .type = NLA_U32 },
	[NHA_GROUP]		= { .type = NLA_BINARY },
	[NHA_GROUP_TYPE]	= { .type = NLA_U16 },
	[NHA_BLACKHOLE]		= { .type = NLA_FLAG },
	[NHA_OIF]		= { .type = NLA_U32 },
	[NHA_GATEWAY]		= { .type = NLA_BINARY },
	[NHA_ENCAP_TYPE]	= { .type = NLA_U16 },
	[NHA_ENCAP]		= { .type = NLA_NESTED },
	[NHA_FDB]		= { .type = NLA_FLAG },
	[NHA_RES_GROUP]		= { .type = NLA_NESTED },
};

static const struct nla_policy rtm_nh_policy_get[] = {
	[NHA_ID]		= { .type = NLA_U32 },
};

static const struct nla_policy rtm_nh_policy_dump[] = {
	[NHA_OIF]		= { .type = NLA_U32 },
	[NHA_GROUPS]		= { .type = NLA_FLAG },
	[NHA_MASTER]		= { .type = NLA_U32 },
	[NHA_FDB]		= { .type = NLA_FLAG },
};

static const struct nla_policy rtm_nh_res_policy_new[] = {
	[NHA_RES_GROUP_BUCKETS]			= { .type = NLA_U16 },
	[NHA_RES_GROUP_IDLE_TIMER]		= { .type = NLA_U32 },
	[NHA_RES_GROUP_UNBALANCED_TIMER]	= { .type = NLA_U32 },
};

static const struct nla_policy rtm_nh_policy_dump_bucket[] = {
	[NHA_ID]		= { .type = NLA_U32 },
	[NHA_OIF]		= { .type = NLA_U32 },
	[NHA_MASTER]		= { .type = NLA_U32 },
	[NHA_RES_BUCKET]	= { .type = NLA_NESTED },
};

static const struct nla_policy rtm_nh_res_bucket_policy_dump[] = {
	[NHA_RES_BUCKET_NH_ID]	= { .type = NLA_U32 },
};

static const struct nla_policy rtm_nh_policy_get_bucket[] = {
	[NHA_ID]		= { .type = NLA_U32 },
	[NHA_RES_BUCKET]	= { .type = NLA_NESTED },
};

static const struct nla_policy rtm_nh_res_bucket_policy_get[] = {
	[NHA_RES_BUCKET_INDEX]	= { .type = NLA_U16 },
};

static bool nexthop_notifiers_is_empty(struct net *net)
{
	return !net->nexthop.notifier_chain.head;
}

static void
__nh_notifier_single_info_init(struct nh_notifier_single_info *nh_info,
			       const struct nh_info *nhi)
{
	nh_info->dev = nhi->fib_nhc.nhc_dev;
	nh_info->gw_family = nhi->fib_nhc.nhc_gw_family;
	if (nh_info->gw_family == AF_INET)
		nh_info->ipv4 = nhi->fib_nhc.nhc_gw.ipv4;
	else if (nh_info->gw_family == AF_INET6)
		nh_info->ipv6 = nhi->fib_nhc.nhc_gw.ipv6;

	nh_info->is_reject = nhi->reject_nh;
	nh_info->is_fdb = nhi->fdb_nh;
	nh_info->has_encap = !!nhi->fib_nhc.nhc_lwtstate;
}

static int nh_notifier_single_info_init(struct nh_notifier_info *info,
					const struct nexthop *nh)
{
	struct nh_info *nhi = rtnl_dereference(nh->nh_info);

	info->type = NH_NOTIFIER_INFO_TYPE_SINGLE;
	info->nh = kzalloc(sizeof(*info->nh), GFP_KERNEL);
	if (!info->nh)
		return -ENOMEM;

	__nh_notifier_single_info_init(info->nh, nhi);

	return 0;
}

static void nh_notifier_single_info_fini(struct nh_notifier_info *info)
{
	kfree(info->nh);
}

static int nh_notifier_mpath_info_init(struct nh_notifier_info *info,
				       struct nh_group *nhg)
{
	u16 num_nh = nhg->num_nh;
	int i;

	info->type = NH_NOTIFIER_INFO_TYPE_GRP;
	info->nh_grp = kzalloc(struct_size(info->nh_grp, nh_entries, num_nh),
			       GFP_KERNEL);
	if (!info->nh_grp)
		return -ENOMEM;

	info->nh_grp->num_nh = num_nh;
	info->nh_grp->is_fdb = nhg->fdb_nh;

	for (i = 0; i < num_nh; i++) {
		struct nh_grp_entry *nhge = &nhg->nh_entries[i];
		struct nh_info *nhi;

		nhi = rtnl_dereference(nhge->nh->nh_info);
		info->nh_grp->nh_entries[i].id = nhge->nh->id;
		info->nh_grp->nh_entries[i].weight = nhge->weight;
		__nh_notifier_single_info_init(&info->nh_grp->nh_entries[i].nh,
					       nhi);
	}

	return 0;
}

static int nh_notifier_res_table_info_init(struct nh_notifier_info *info,
					   struct nh_group *nhg)
{
	struct nh_res_table *res_table = rtnl_dereference(nhg->res_table);
	u16 num_nh_buckets = res_table->num_nh_buckets;
	unsigned long size;
	u16 i;

	info->type = NH_NOTIFIER_INFO_TYPE_RES_TABLE;
	size = struct_size(info->nh_res_table, nhs, num_nh_buckets);
	info->nh_res_table = __vmalloc(size, GFP_KERNEL | __GFP_ZERO |
				       __GFP_NOWARN);
	if (!info->nh_res_table)
		return -ENOMEM;

	info->nh_res_table->num_nh_buckets = num_nh_buckets;

	for (i = 0; i < num_nh_buckets; i++) {
		struct nh_res_bucket *bucket = &res_table->nh_buckets[i];
		struct nh_grp_entry *nhge;
		struct nh_info *nhi;

		nhge = rtnl_dereference(bucket->nh_entry);
		nhi = rtnl_dereference(nhge->nh->nh_info);
		__nh_notifier_single_info_init(&info->nh_res_table->nhs[i],
					       nhi);
	}

	return 0;
}

static int nh_notifier_grp_info_init(struct nh_notifier_info *info,
				     const struct nexthop *nh)
{
	struct nh_group *nhg = rtnl_dereference(nh->nh_grp);

	if (nhg->hash_threshold)
		return nh_notifier_mpath_info_init(info, nhg);
	else if (nhg->resilient)
		return nh_notifier_res_table_info_init(info, nhg);
	return -EINVAL;
}

static void nh_notifier_grp_info_fini(struct nh_notifier_info *info,
				      const struct nexthop *nh)
{
	struct nh_group *nhg = rtnl_dereference(nh->nh_grp);

	if (nhg->hash_threshold)
		kfree(info->nh_grp);
	else if (nhg->resilient)
		vfree(info->nh_res_table);
}

static int nh_notifier_info_init(struct nh_notifier_info *info,
				 const struct nexthop *nh)
{
	info->id = nh->id;

	if (nh->is_group)
		return nh_notifier_grp_info_init(info, nh);
	else
		return nh_notifier_single_info_init(info, nh);
}

static void nh_notifier_info_fini(struct nh_notifier_info *info,
				  const struct nexthop *nh)
{
	if (nh->is_group)
		nh_notifier_grp_info_fini(info, nh);
	else
		nh_notifier_single_info_fini(info);
}

static int call_nexthop_notifiers(struct net *net,
				  enum nexthop_event_type event_type,
				  struct nexthop *nh,
				  struct netlink_ext_ack *extack)
{
	struct nh_notifier_info info = {
		.net = net,
		.extack = extack,
	};
	int err;

	ASSERT_RTNL();

	if (nexthop_notifiers_is_empty(net))
		return 0;

	err = nh_notifier_info_init(&info, nh);
	if (err) {
		NL_SET_ERR_MSG(extack, "Failed to initialize nexthop notifier info");
		return err;
	}

	err = blocking_notifier_call_chain(&net->nexthop.notifier_chain,
					   event_type, &info);
	nh_notifier_info_fini(&info, nh);

	return notifier_to_errno(err);
}

static int
nh_notifier_res_bucket_idle_timer_get(const struct nh_notifier_info *info,
				      bool force, unsigned int *p_idle_timer_ms)
{
	struct nh_res_table *res_table;
	struct nh_group *nhg;
	struct nexthop *nh;
	int err = 0;

	/* When 'force' is false, nexthop bucket replacement is performed
	 * because the bucket was deemed to be idle. In this case, capable
	 * listeners can choose to perform an atomic replacement: The bucket is
	 * only replaced if it is inactive. However, if the idle timer interval
	 * is smaller than the interval in which a listener is querying
	 * buckets' activity from the device, then atomic replacement should
	 * not be tried. Pass the idle timer value to listeners, so that they
	 * could determine which type of replacement to perform.
	 */
	if (force) {
		*p_idle_timer_ms = 0;
		return 0;
	}

	rcu_read_lock();

	nh = nexthop_find_by_id(info->net, info->id);
	if (!nh) {
		err = -EINVAL;
		goto out;
	}

	nhg = rcu_dereference(nh->nh_grp);
	res_table = rcu_dereference(nhg->res_table);
	*p_idle_timer_ms = jiffies_to_msecs(res_table->idle_timer);

out:
	rcu_read_unlock();

	return err;
}

static int nh_notifier_res_bucket_info_init(struct nh_notifier_info *info,
					    u16 bucket_index, bool force,
					    struct nh_info *oldi,
					    struct nh_info *newi)
{
	unsigned int idle_timer_ms;
	int err;

	err = nh_notifier_res_bucket_idle_timer_get(info, force,
						    &idle_timer_ms);
	if (err)
		return err;

	info->type = NH_NOTIFIER_INFO_TYPE_RES_BUCKET;
	info->nh_res_bucket = kzalloc(sizeof(*info->nh_res_bucket),
				      GFP_KERNEL);
	if (!info->nh_res_bucket)
		return -ENOMEM;

	info->nh_res_bucket->bucket_index = bucket_index;
	info->nh_res_bucket->idle_timer_ms = idle_timer_ms;
	info->nh_res_bucket->force = force;
	__nh_notifier_single_info_init(&info->nh_res_bucket->old_nh, oldi);
	__nh_notifier_single_info_init(&info->nh_res_bucket->new_nh, newi);
	return 0;
}

static void nh_notifier_res_bucket_info_fini(struct nh_notifier_info *info)
{
	kfree(info->nh_res_bucket);
}

static int __call_nexthop_res_bucket_notifiers(struct net *net, u32 nhg_id,
					       u16 bucket_index, bool force,
					       struct nh_info *oldi,
					       struct nh_info *newi,
					       struct netlink_ext_ack *extack)
{
	struct nh_notifier_info info = {
		.net = net,
		.extack = extack,
		.id = nhg_id,
	};
	int err;

	if (nexthop_notifiers_is_empty(net))
		return 0;

	err = nh_notifier_res_bucket_info_init(&info, bucket_index, force,
					       oldi, newi);
	if (err)
		return err;

	err = blocking_notifier_call_chain(&net->nexthop.notifier_chain,
					   NEXTHOP_EVENT_BUCKET_REPLACE, &info);
	nh_notifier_res_bucket_info_fini(&info);

	return notifier_to_errno(err);
}

/* There are three users of RES_TABLE, and NHs etc. referenced from there:
 *
 * 1) a collection of callbacks for NH maintenance. This operates under
 *    RTNL,
 * 2) the delayed work that gradually balances the resilient table,
 * 3) and nexthop_select_path(), operating under RCU.
 *
 * Both the delayed work and the RTNL block are writers, and need to
 * maintain mutual exclusion. Since there are only two and well-known
 * writers for each table, the RTNL code can make sure it has exclusive
 * access thus:
 *
 * - Have the DW operate without locking;
 * - synchronously cancel the DW;
 * - do the writing;
 * - if the write was not actually a delete, call upkeep, which schedules
 *   DW again if necessary.
 *
 * The functions that are always called from the RTNL context use
 * rtnl_dereference(). The functions that can also be called from the DW do
 * a raw dereference and rely on the above mutual exclusion scheme.
 */
#define nh_res_dereference(p) (rcu_dereference_raw(p))

static int call_nexthop_res_bucket_notifiers(struct net *net, u32 nhg_id,
					     u16 bucket_index, bool force,
					     struct nexthop *old_nh,
					     struct nexthop *new_nh,
					     struct netlink_ext_ack *extack)
{
	struct nh_info *oldi = nh_res_dereference(old_nh->nh_info);
	struct nh_info *newi = nh_res_dereference(new_nh->nh_info);

	return __call_nexthop_res_bucket_notifiers(net, nhg_id, bucket_index,
						   force, oldi, newi, extack);
}

static int call_nexthop_res_table_notifiers(struct net *net, struct nexthop *nh,
					    struct netlink_ext_ack *extack)
{
	struct nh_notifier_info info = {
		.net = net,
		.extack = extack,
	};
	struct nh_group *nhg;
	int err;

	ASSERT_RTNL();

	if (nexthop_notifiers_is_empty(net))
		return 0;

	/* At this point, the nexthop buckets are still not populated. Only
	 * emit a notification with the logical nexthops, so that a listener
	 * could potentially veto it in case of unsupported configuration.
	 */
	nhg = rtnl_dereference(nh->nh_grp);
	err = nh_notifier_mpath_info_init(&info, nhg);
	if (err) {
		NL_SET_ERR_MSG(extack, "Failed to initialize nexthop notifier info");
		return err;
	}

	err = blocking_notifier_call_chain(&net->nexthop.notifier_chain,
					   NEXTHOP_EVENT_RES_TABLE_PRE_REPLACE,
					   &info);
	kfree(info.nh_grp);

	return notifier_to_errno(err);
}

static int call_nexthop_notifier(struct notifier_block *nb, struct net *net,
				 enum nexthop_event_type event_type,
				 struct nexthop *nh,
				 struct netlink_ext_ack *extack)
{
	struct nh_notifier_info info = {
		.net = net,
		.extack = extack,
	};
	int err;

	err = nh_notifier_info_init(&info, nh);
	if (err)
		return err;

	err = nb->notifier_call(nb, event_type, &info);
	nh_notifier_info_fini(&info, nh);

	return notifier_to_errno(err);
}

static unsigned int nh_dev_hashfn(unsigned int val)
{
	unsigned int mask = NH_DEV_HASHSIZE - 1;

	return (val ^
		(val >> NH_DEV_HASHBITS) ^
		(val >> (NH_DEV_HASHBITS * 2))) & mask;
}

static void nexthop_devhash_add(struct net *net, struct nh_info *nhi)
{
	struct net_device *dev = nhi->fib_nhc.nhc_dev;
	struct hlist_head *head;
	unsigned int hash;

	WARN_ON(!dev);

	hash = nh_dev_hashfn(dev->ifindex);
	head = &net->nexthop.devhash[hash];
	hlist_add_head(&nhi->dev_hash, head);
}

static void nexthop_free_group(struct nexthop *nh)
{
	struct nh_group *nhg;
	int i;

	nhg = rcu_dereference_raw(nh->nh_grp);
	for (i = 0; i < nhg->num_nh; ++i) {
		struct nh_grp_entry *nhge = &nhg->nh_entries[i];

		WARN_ON(!list_empty(&nhge->nh_list));
		nexthop_put(nhge->nh);
	}

	WARN_ON(nhg->spare == nhg);

	if (nhg->resilient)
		vfree(rcu_dereference_raw(nhg->res_table));

	kfree(nhg->spare);
	kfree(nhg);
}

static void nexthop_free_single(struct nexthop *nh)
{
	struct nh_info *nhi;

	nhi = rcu_dereference_raw(nh->nh_info);
	switch (nhi->family) {
	case AF_INET:
		fib_nh_release(nh->net, &nhi->fib_nh);
		break;
	case AF_INET6:
		ipv6_stub->fib6_nh_release(&nhi->fib6_nh);
		break;
	}
	kfree(nhi);
}

void nexthop_free_rcu(struct rcu_head *head)
{
	struct nexthop *nh = container_of(head, struct nexthop, rcu);

	if (nh->is_group)
		nexthop_free_group(nh);
	else
		nexthop_free_single(nh);

	kfree(nh);
}
EXPORT_SYMBOL_GPL(nexthop_free_rcu);

static struct nexthop *nexthop_alloc(void)
{
	struct nexthop *nh;

	nh = kzalloc(sizeof(struct nexthop), GFP_KERNEL);
	if (nh) {
		INIT_LIST_HEAD(&nh->fi_list);
		INIT_LIST_HEAD(&nh->f6i_list);
		INIT_LIST_HEAD(&nh->grp_list);
		INIT_LIST_HEAD(&nh->fdb_list);
	}
	return nh;
}

static struct nh_group *nexthop_grp_alloc(u16 num_nh)
{
	struct nh_group *nhg;

	nhg = kzalloc(struct_size(nhg, nh_entries, num_nh), GFP_KERNEL);
	if (nhg)
		nhg->num_nh = num_nh;

	return nhg;
}

static void nh_res_table_upkeep_dw(struct work_struct *work);

static struct nh_res_table *
nexthop_res_table_alloc(struct net *net, u32 nhg_id, struct nh_config *cfg)
{
	const u16 num_nh_buckets = cfg->nh_grp_res_num_buckets;
	struct nh_res_table *res_table;
	unsigned long size;

	size = struct_size(res_table, nh_buckets, num_nh_buckets);
	res_table = __vmalloc(size, GFP_KERNEL | __GFP_ZERO | __GFP_NOWARN);
	if (!res_table)
		return NULL;

	res_table->net = net;
	res_table->nhg_id = nhg_id;
	INIT_DELAYED_WORK(&res_table->upkeep_dw, &nh_res_table_upkeep_dw);
	INIT_LIST_HEAD(&res_table->uw_nh_entries);
	res_table->idle_timer = cfg->nh_grp_res_idle_timer;
	res_table->unbalanced_timer = cfg->nh_grp_res_unbalanced_timer;
	res_table->num_nh_buckets = num_nh_buckets;
	return res_table;
}

static void nh_base_seq_inc(struct net *net)
{
	while (++net->nexthop.seq == 0)
		;
}

/* no reference taken; rcu lock or rtnl must be held */
struct nexthop *nexthop_find_by_id(struct net *net, u32 id)
{
	struct rb_node **pp, *parent = NULL, *next;

	pp = &net->nexthop.rb_root.rb_node;
	while (1) {
		struct nexthop *nh;

		next = rcu_dereference_raw(*pp);
		if (!next)
			break;
		parent = next;

		nh = rb_entry(parent, struct nexthop, rb_node);
		if (id < nh->id)
			pp = &next->rb_left;
		else if (id > nh->id)
			pp = &next->rb_right;
		else
			return nh;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(nexthop_find_by_id);

/* used for auto id allocation; called with rtnl held */
static u32 nh_find_unused_id(struct net *net)
{
	u32 id_start = net->nexthop.last_id_allocated;

	while (1) {
		net->nexthop.last_id_allocated++;
		if (net->nexthop.last_id_allocated == id_start)
			break;

		if (!nexthop_find_by_id(net, net->nexthop.last_id_allocated))
			return net->nexthop.last_id_allocated;
	}
	return 0;
}

static void nh_res_time_set_deadline(unsigned long next_time,
				     unsigned long *deadline)
{
	if (time_before(next_time, *deadline))
		*deadline = next_time;
}

static clock_t nh_res_table_unbalanced_time(struct nh_res_table *res_table)
{
	if (list_empty(&res_table->uw_nh_entries))
		return 0;
	return jiffies_delta_to_clock_t(jiffies - res_table->unbalanced_since);
}

static int nla_put_nh_group_res(struct sk_buff *skb, struct nh_group *nhg)
{
	struct nh_res_table *res_table = rtnl_dereference(nhg->res_table);
	struct nlattr *nest;

	nest = nla_nest_start(skb, NHA_RES_GROUP);
	if (!nest)
		return -EMSGSIZE;

	if (nla_put_u16(skb, NHA_RES_GROUP_BUCKETS,
			res_table->num_nh_buckets) ||
	    nla_put_u32(skb, NHA_RES_GROUP_IDLE_TIMER,
			jiffies_to_clock_t(res_table->idle_timer)) ||
	    nla_put_u32(skb, NHA_RES_GROUP_UNBALANCED_TIMER,
			jiffies_to_clock_t(res_table->unbalanced_timer)) ||
	    nla_put_u64_64bit(skb, NHA_RES_GROUP_UNBALANCED_TIME,
			      nh_res_table_unbalanced_time(res_table),
			      NHA_RES_GROUP_PAD))
		goto nla_put_failure;

	nla_nest_end(skb, nest);
	return 0;

nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -EMSGSIZE;
}

static int nla_put_nh_group(struct sk_buff *skb, struct nh_group *nhg)
{
	struct nexthop_grp *p;
	size_t len = nhg->num_nh * sizeof(*p);
	struct nlattr *nla;
	u16 group_type = 0;
	int i;

	if (nhg->hash_threshold)
		group_type = NEXTHOP_GRP_TYPE_MPATH;
	else if (nhg->resilient)
		group_type = NEXTHOP_GRP_TYPE_RES;

	if (nla_put_u16(skb, NHA_GROUP_TYPE, group_type))
		goto nla_put_failure;

	nla = nla_reserve(skb, NHA_GROUP, len);
	if (!nla)
		goto nla_put_failure;

	p = nla_data(nla);
	for (i = 0; i < nhg->num_nh; ++i) {
		p->id = nhg->nh_entries[i].nh->id;
		p->weight = nhg->nh_entries[i].weight - 1;
		p += 1;
	}

	if (nhg->resilient && nla_put_nh_group_res(skb, nhg))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static int nh_fill_node(struct sk_buff *skb, struct nexthop *nh,
			int event, u32 portid, u32 seq, unsigned int nlflags)
{
	struct fib6_nh *fib6_nh;
	struct fib_nh *fib_nh;
	struct nlmsghdr *nlh;
	struct nh_info *nhi;
	struct nhmsg *nhm;

	nlh = nlmsg_put(skb, portid, seq, event, sizeof(*nhm), nlflags);
	if (!nlh)
		return -EMSGSIZE;

	nhm = nlmsg_data(nlh);
	nhm->nh_family = AF_UNSPEC;
	nhm->nh_flags = nh->nh_flags;
	nhm->nh_protocol = nh->protocol;
	nhm->nh_scope = 0;
	nhm->resvd = 0;

	if (nla_put_u32(skb, NHA_ID, nh->id))
		goto nla_put_failure;

	if (nh->is_group) {
		struct nh_group *nhg = rtnl_dereference(nh->nh_grp);

		if (nhg->fdb_nh && nla_put_flag(skb, NHA_FDB))
			goto nla_put_failure;
		if (nla_put_nh_group(skb, nhg))
			goto nla_put_failure;
		goto out;
	}

	nhi = rtnl_dereference(nh->nh_info);
	nhm->nh_family = nhi->family;
	if (nhi->reject_nh) {
		if (nla_put_flag(skb, NHA_BLACKHOLE))
			goto nla_put_failure;
		goto out;
	} else if (nhi->fdb_nh) {
		if (nla_put_flag(skb, NHA_FDB))
			goto nla_put_failure;
	} else {
		const struct net_device *dev;

		dev = nhi->fib_nhc.nhc_dev;
		if (dev && nla_put_u32(skb, NHA_OIF, dev->ifindex))
			goto nla_put_failure;
	}

	nhm->nh_scope = nhi->fib_nhc.nhc_scope;
	switch (nhi->family) {
	case AF_INET:
		fib_nh = &nhi->fib_nh;
		if (fib_nh->fib_nh_gw_family &&
		    nla_put_be32(skb, NHA_GATEWAY, fib_nh->fib_nh_gw4))
			goto nla_put_failure;
		break;

	case AF_INET6:
		fib6_nh = &nhi->fib6_nh;
		if (fib6_nh->fib_nh_gw_family &&
		    nla_put_in6_addr(skb, NHA_GATEWAY, &fib6_nh->fib_nh_gw6))
			goto nla_put_failure;
		break;
	}

	if (nhi->fib_nhc.nhc_lwtstate &&
	    lwtunnel_fill_encap(skb, nhi->fib_nhc.nhc_lwtstate,
				NHA_ENCAP, NHA_ENCAP_TYPE) < 0)
		goto nla_put_failure;

out:
	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static size_t nh_nlmsg_size_grp_res(struct nh_group *nhg)
{
	return nla_total_size(0) +	/* NHA_RES_GROUP */
		nla_total_size(2) +	/* NHA_RES_GROUP_BUCKETS */
		nla_total_size(4) +	/* NHA_RES_GROUP_IDLE_TIMER */
		nla_total_size(4) +	/* NHA_RES_GROUP_UNBALANCED_TIMER */
		nla_total_size_64bit(8);/* NHA_RES_GROUP_UNBALANCED_TIME */
}

static size_t nh_nlmsg_size_grp(struct nexthop *nh)
{
	struct nh_group *nhg = rtnl_dereference(nh->nh_grp);
	size_t sz = sizeof(struct nexthop_grp) * nhg->num_nh;
	size_t tot = nla_total_size(sz) +
		nla_total_size(2); /* NHA_GROUP_TYPE */

	if (nhg->resilient)
		tot += nh_nlmsg_size_grp_res(nhg);

	return tot;
}

static size_t nh_nlmsg_size_single(struct nexthop *nh)
{
	struct nh_info *nhi = rtnl_dereference(nh->nh_info);
	size_t sz;

	/* covers NHA_BLACKHOLE since NHA_OIF and BLACKHOLE
	 * are mutually exclusive
	 */
	sz = nla_total_size(4);  /* NHA_OIF */

	switch (nhi->family) {
	case AF_INET:
		if (nhi->fib_nh.fib_nh_gw_family)
			sz += nla_total_size(4);  /* NHA_GATEWAY */
		break;

	case AF_INET6:
		/* NHA_GATEWAY */
		if (nhi->fib6_nh.fib_nh_gw_family)
			sz += nla_total_size(sizeof(const struct in6_addr));
		break;
	}

	if (nhi->fib_nhc.nhc_lwtstate) {
		sz += lwtunnel_get_encap_size(nhi->fib_nhc.nhc_lwtstate);
		sz += nla_total_size(2);  /* NHA_ENCAP_TYPE */
	}

	return sz;
}

static size_t nh_nlmsg_size(struct nexthop *nh)
{
	size_t sz = NLMSG_ALIGN(sizeof(struct nhmsg));

	sz += nla_total_size(4); /* NHA_ID */

	if (nh->is_group)
		sz += nh_nlmsg_size_grp(nh);
	else
		sz += nh_nlmsg_size_single(nh);

	return sz;
}

static void nexthop_notify(int event, struct nexthop *nh, struct nl_info *info)
{
	unsigned int nlflags = info->nlh ? info->nlh->nlmsg_flags : 0;
	u32 seq = info->nlh ? info->nlh->nlmsg_seq : 0;
	struct sk_buff *skb;
	int err = -ENOBUFS;

	skb = nlmsg_new(nh_nlmsg_size(nh), gfp_any());
	if (!skb)
		goto errout;

	err = nh_fill_node(skb, nh, event, info->portid, seq, nlflags);
	if (err < 0) {
		/* -EMSGSIZE implies BUG in nh_nlmsg_size() */
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(skb);
		goto errout;
	}

	rtnl_notify(skb, info->nl_net, info->portid, RTNLGRP_NEXTHOP,
		    info->nlh, gfp_any());
	return;
errout:
	if (err < 0)
		rtnl_set_sk_err(info->nl_net, RTNLGRP_NEXTHOP, err);
}

static unsigned long nh_res_bucket_used_time(const struct nh_res_bucket *bucket)
{
	return (unsigned long)atomic_long_read(&bucket->used_time);
}

static unsigned long
nh_res_bucket_idle_point(const struct nh_res_table *res_table,
			 const struct nh_res_bucket *bucket,
			 unsigned long now)
{
	unsigned long time = nh_res_bucket_used_time(bucket);

	/* Bucket was not used since it was migrated. The idle time is now. */
	if (time == bucket->migrated_time)
		return now;

	return time + res_table->idle_timer;
}

static unsigned long
nh_res_table_unb_point(const struct nh_res_table *res_table)
{
	return res_table->unbalanced_since + res_table->unbalanced_timer;
}

static void nh_res_bucket_set_idle(const struct nh_res_table *res_table,
				   struct nh_res_bucket *bucket)
{
	unsigned long now = jiffies;

	atomic_long_set(&bucket->used_time, (long)now);
	bucket->migrated_time = now;
}

static void nh_res_bucket_set_busy(struct nh_res_bucket *bucket)
{
	atomic_long_set(&bucket->used_time, (long)jiffies);
}

static clock_t nh_res_bucket_idle_time(const struct nh_res_bucket *bucket)
{
	unsigned long used_time = nh_res_bucket_used_time(bucket);

	return jiffies_delta_to_clock_t(jiffies - used_time);
}

static int nh_fill_res_bucket(struct sk_buff *skb, struct nexthop *nh,
			      struct nh_res_bucket *bucket, u16 bucket_index,
			      int event, u32 portid, u32 seq,
			      unsigned int nlflags,
			      struct netlink_ext_ack *extack)
{
	struct nh_grp_entry *nhge = nh_res_dereference(bucket->nh_entry);
	struct nlmsghdr *nlh;
	struct nlattr *nest;
	struct nhmsg *nhm;

	nlh = nlmsg_put(skb, portid, seq, event, sizeof(*nhm), nlflags);
	if (!nlh)
		return -EMSGSIZE;

	nhm = nlmsg_data(nlh);
	nhm->nh_family = AF_UNSPEC;
	nhm->nh_flags = bucket->nh_flags;
	nhm->nh_protocol = nh->protocol;
	nhm->nh_scope = 0;
	nhm->resvd = 0;

	if (nla_put_u32(skb, NHA_ID, nh->id))
		goto nla_put_failure;

	nest = nla_nest_start(skb, NHA_RES_BUCKET);
	if (!nest)
		goto nla_put_failure;

	if (nla_put_u16(skb, NHA_RES_BUCKET_INDEX, bucket_index) ||
	    nla_put_u32(skb, NHA_RES_BUCKET_NH_ID, nhge->nh->id) ||
	    nla_put_u64_64bit(skb, NHA_RES_BUCKET_IDLE_TIME,
			      nh_res_bucket_idle_time(bucket),
			      NHA_RES_BUCKET_PAD))
		goto nla_put_failure_nest;

	nla_nest_end(skb, nest);
	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure_nest:
	nla_nest_cancel(skb, nest);
nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static void nexthop_bucket_notify(struct nh_res_table *res_table,
				  u16 bucket_index)
{
	struct nh_res_bucket *bucket = &res_table->nh_buckets[bucket_index];
	struct nh_grp_entry *nhge = nh_res_dereference(bucket->nh_entry);
	struct nexthop *nh = nhge->nh_parent;
	struct sk_buff *skb;
	int err = -ENOBUFS;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		goto errout;

	err = nh_fill_res_bucket(skb, nh, bucket, bucket_index,
				 RTM_NEWNEXTHOPBUCKET, 0, 0, NLM_F_REPLACE,
				 NULL);
	if (err < 0) {
		kfree_skb(skb);
		goto errout;
	}

	rtnl_notify(skb, nh->net, 0, RTNLGRP_NEXTHOP, NULL, GFP_KERNEL);
	return;
errout:
	if (err < 0)
		rtnl_set_sk_err(nh->net, RTNLGRP_NEXTHOP, err);
}

static bool valid_group_nh(struct nexthop *nh, unsigned int npaths,
			   bool *is_fdb, struct netlink_ext_ack *extack)
{
	if (nh->is_group) {
		struct nh_group *nhg = rtnl_dereference(nh->nh_grp);

		/* Nesting groups within groups is not supported. */
		if (nhg->hash_threshold) {
			NL_SET_ERR_MSG(extack,
				       "Hash-threshold group can not be a nexthop within a group");
			return false;
		}
		if (nhg->resilient) {
			NL_SET_ERR_MSG(extack,
				       "Resilient group can not be a nexthop within a group");
			return false;
		}
		*is_fdb = nhg->fdb_nh;
	} else {
		struct nh_info *nhi = rtnl_dereference(nh->nh_info);

		if (nhi->reject_nh && npaths > 1) {
			NL_SET_ERR_MSG(extack,
				       "Blackhole nexthop can not be used in a group with more than 1 path");
			return false;
		}
		*is_fdb = nhi->fdb_nh;
	}

	return true;
}

static int nh_check_attr_fdb_group(struct nexthop *nh, u8 *nh_family,
				   struct netlink_ext_ack *extack)
{
	struct nh_info *nhi;

	nhi = rtnl_dereference(nh->nh_info);

	if (!nhi->fdb_nh) {
		NL_SET_ERR_MSG(extack, "FDB nexthop group can only have fdb nexthops");
		return -EINVAL;
	}

	if (*nh_family == AF_UNSPEC) {
		*nh_family = nhi->family;
	} else if (*nh_family != nhi->family) {
		NL_SET_ERR_MSG(extack, "FDB nexthop group cannot have mixed family nexthops");
		return -EINVAL;
	}

	return 0;
}

static int nh_check_attr_group(struct net *net,
			       struct nlattr *tb[], size_t tb_size,
			       u16 nh_grp_type, struct netlink_ext_ack *extack)
{
	unsigned int len = nla_len(tb[NHA_GROUP]);
	u8 nh_family = AF_UNSPEC;
	struct nexthop_grp *nhg;
	unsigned int i, j;
	u8 nhg_fdb = 0;

	if (!len || len & (sizeof(struct nexthop_grp) - 1)) {
		NL_SET_ERR_MSG(extack,
			       "Invalid length for nexthop group attribute");
		return -EINVAL;
	}

	/* convert len to number of nexthop ids */
	len /= sizeof(*nhg);

	nhg = nla_data(tb[NHA_GROUP]);
	for (i = 0; i < len; ++i) {
		if (nhg[i].resvd1 || nhg[i].resvd2) {
			NL_SET_ERR_MSG(extack, "Reserved fields in nexthop_grp must be 0");
			return -EINVAL;
		}
		if (nhg[i].weight > 254) {
			NL_SET_ERR_MSG(extack, "Invalid value for weight");
			return -EINVAL;
		}
		for (j = i + 1; j < len; ++j) {
			if (nhg[i].id == nhg[j].id) {
				NL_SET_ERR_MSG(extack, "Nexthop id can not be used twice in a group");
				return -EINVAL;
			}
		}
	}

	if (tb[NHA_FDB])
		nhg_fdb = 1;
	nhg = nla_data(tb[NHA_GROUP]);
	for (i = 0; i < len; ++i) {
		struct nexthop *nh;
		bool is_fdb_nh;

		nh = nexthop_find_by_id(net, nhg[i].id);
		if (!nh) {
			NL_SET_ERR_MSG(extack, "Invalid nexthop id");
			return -EINVAL;
		}
		if (!valid_group_nh(nh, len, &is_fdb_nh, extack))
			return -EINVAL;

		if (nhg_fdb && nh_check_attr_fdb_group(nh, &nh_family, extack))
			return -EINVAL;

		if (!nhg_fdb && is_fdb_nh) {
			NL_SET_ERR_MSG(extack, "Non FDB nexthop group cannot have fdb nexthops");
			return -EINVAL;
		}
	}
	for (i = NHA_GROUP_TYPE + 1; i < tb_size; ++i) {
		if (!tb[i])
			continue;
		switch (i) {
		case NHA_FDB:
			continue;
		case NHA_RES_GROUP:
			if (nh_grp_type == NEXTHOP_GRP_TYPE_RES)
				continue;
			break;
		}
		NL_SET_ERR_MSG(extack,
			       "No other attributes can be set in nexthop groups");
		return -EINVAL;
	}

	return 0;
}

static bool ipv6_good_nh(const struct fib6_nh *nh)
{
	int state = NUD_REACHABLE;
	struct neighbour *n;

	rcu_read_lock_bh();

	n = __ipv6_neigh_lookup_noref_stub(nh->fib_nh_dev, &nh->fib_nh_gw6);
	if (n)
		state = n->nud_state;

	rcu_read_unlock_bh();

	return !!(state & NUD_VALID);
}

static bool ipv4_good_nh(const struct fib_nh *nh)
{
	int state = NUD_REACHABLE;
	struct neighbour *n;

	rcu_read_lock_bh();

	n = __ipv4_neigh_lookup_noref(nh->fib_nh_dev,
				      (__force u32)nh->fib_nh_gw4);
	if (n)
		state = n->nud_state;

	rcu_read_unlock_bh();

	return !!(state & NUD_VALID);
}

static struct nexthop *nexthop_select_path_hthr(struct nh_group *nhg, int hash)
{
	struct nexthop *rc = NULL;
	int i;

	for (i = 0; i < nhg->num_nh; ++i) {
		struct nh_grp_entry *nhge = &nhg->nh_entries[i];
		struct nh_info *nhi;

		if (hash > atomic_read(&nhge->hthr.upper_bound))
			continue;

		nhi = rcu_dereference(nhge->nh->nh_info);
		if (nhi->fdb_nh)
			return nhge->nh;

		/* nexthops always check if it is good and does
		 * not rely on a sysctl for this behavior
		 */
		switch (nhi->family) {
		case AF_INET:
			if (ipv4_good_nh(&nhi->fib_nh))
				return nhge->nh;
			break;
		case AF_INET6:
			if (ipv6_good_nh(&nhi->fib6_nh))
				return nhge->nh;
			break;
		}

		if (!rc)
			rc = nhge->nh;
	}

	return rc;
}

static struct nexthop *nexthop_select_path_res(struct nh_group *nhg, int hash)
{
	struct nh_res_table *res_table = rcu_dereference(nhg->res_table);
	u16 bucket_index = hash % res_table->num_nh_buckets;
	struct nh_res_bucket *bucket;
	struct nh_grp_entry *nhge;

	/* nexthop_select_path() is expected to return a non-NULL value, so
	 * skip protocol validation and just hand out whatever there is.
	 */
	bucket = &res_table->nh_buckets[bucket_index];
	nh_res_bucket_set_busy(bucket);
	nhge = rcu_dereference(bucket->nh_entry);
	return nhge->nh;
}

struct nexthop *nexthop_select_path(struct nexthop *nh, int hash)
{
	struct nh_group *nhg;

	if (!nh->is_group)
		return nh;

	nhg = rcu_dereference(nh->nh_grp);
	if (nhg->hash_threshold)
		return nexthop_select_path_hthr(nhg, hash);
	else if (nhg->resilient)
		return nexthop_select_path_res(nhg, hash);

	/* Unreachable. */
	return NULL;
}
EXPORT_SYMBOL_GPL(nexthop_select_path);

int nexthop_for_each_fib6_nh(struct nexthop *nh,
			     int (*cb)(struct fib6_nh *nh, void *arg),
			     void *arg)
{
	struct nh_info *nhi;
	int err;

	if (nh->is_group) {
		struct nh_group *nhg;
		int i;

		nhg = rcu_dereference_rtnl(nh->nh_grp);
		for (i = 0; i < nhg->num_nh; i++) {
			struct nh_grp_entry *nhge = &nhg->nh_entries[i];

			nhi = rcu_dereference_rtnl(nhge->nh->nh_info);
			err = cb(&nhi->fib6_nh, arg);
			if (err)
				return err;
		}
	} else {
		nhi = rcu_dereference_rtnl(nh->nh_info);
		err = cb(&nhi->fib6_nh, arg);
		if (err)
			return err;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(nexthop_for_each_fib6_nh);

static int check_src_addr(const struct in6_addr *saddr,
			  struct netlink_ext_ack *extack)
{
	if (!ipv6_addr_any(saddr)) {
		NL_SET_ERR_MSG(extack, "IPv6 routes using source address can not use nexthop objects");
		return -EINVAL;
	}
	return 0;
}

int fib6_check_nexthop(struct nexthop *nh, struct fib6_config *cfg,
		       struct netlink_ext_ack *extack)
{
	struct nh_info *nhi;
	bool is_fdb_nh;

	/* fib6_src is unique to a fib6_info and limits the ability to cache
	 * routes in fib6_nh within a nexthop that is potentially shared
	 * across multiple fib entries. If the config wants to use source
	 * routing it can not use nexthop objects. mlxsw also does not allow
	 * fib6_src on routes.
	 */
	if (cfg && check_src_addr(&cfg->fc_src, extack) < 0)
		return -EINVAL;

	if (nh->is_group) {
		struct nh_group *nhg;

		nhg = rtnl_dereference(nh->nh_grp);
		if (nhg->has_v4)
			goto no_v4_nh;
		is_fdb_nh = nhg->fdb_nh;
	} else {
		nhi = rtnl_dereference(nh->nh_info);
		if (nhi->family == AF_INET)
			goto no_v4_nh;
		is_fdb_nh = nhi->fdb_nh;
	}

	if (is_fdb_nh) {
		NL_SET_ERR_MSG(extack, "Route cannot point to a fdb nexthop");
		return -EINVAL;
	}

	return 0;
no_v4_nh:
	NL_SET_ERR_MSG(extack, "IPv6 routes can not use an IPv4 nexthop");
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(fib6_check_nexthop);

/* if existing nexthop has ipv6 routes linked to it, need
 * to verify this new spec works with ipv6
 */
static int fib6_check_nh_list(struct nexthop *old, struct nexthop *new,
			      struct netlink_ext_ack *extack)
{
	struct fib6_info *f6i;

	if (list_empty(&old->f6i_list))
		return 0;

	list_for_each_entry(f6i, &old->f6i_list, nh_list) {
		if (check_src_addr(&f6i->fib6_src.addr, extack) < 0)
			return -EINVAL;
	}

	return fib6_check_nexthop(new, NULL, extack);
}

static int nexthop_check_scope(struct nh_info *nhi, u8 scope,
			       struct netlink_ext_ack *extack)
{
	if (scope == RT_SCOPE_HOST && nhi->fib_nhc.nhc_gw_family) {
		NL_SET_ERR_MSG(extack,
			       "Route with host scope can not have a gateway");
		return -EINVAL;
	}

	if (nhi->fib_nhc.nhc_flags & RTNH_F_ONLINK && scope >= RT_SCOPE_LINK) {
		NL_SET_ERR_MSG(extack, "Scope mismatch with nexthop");
		return -EINVAL;
	}

	return 0;
}

/* Invoked by fib add code to verify nexthop by id is ok with
 * config for prefix; parts of fib_check_nh not done when nexthop
 * object is used.
 */
int fib_check_nexthop(struct nexthop *nh, u8 scope,
		      struct netlink_ext_ack *extack)
{
	struct nh_info *nhi;
	int err = 0;

	if (nh->is_group) {
		struct nh_group *nhg;

		nhg = rtnl_dereference(nh->nh_grp);
		if (nhg->fdb_nh) {
			NL_SET_ERR_MSG(extack, "Route cannot point to a fdb nexthop");
			err = -EINVAL;
			goto out;
		}

		if (scope == RT_SCOPE_HOST) {
			NL_SET_ERR_MSG(extack, "Route with host scope can not have multiple nexthops");
			err = -EINVAL;
			goto out;
		}

		/* all nexthops in a group have the same scope */
		nhi = rtnl_dereference(nhg->nh_entries[0].nh->nh_info);
		err = nexthop_check_scope(nhi, scope, extack);
	} else {
		nhi = rtnl_dereference(nh->nh_info);
		if (nhi->fdb_nh) {
			NL_SET_ERR_MSG(extack, "Route cannot point to a fdb nexthop");
			err = -EINVAL;
			goto out;
		}
		err = nexthop_check_scope(nhi, scope, extack);
	}

out:
	return err;
}

static int fib_check_nh_list(struct nexthop *old, struct nexthop *new,
			     struct netlink_ext_ack *extack)
{
	struct fib_info *fi;

	list_for_each_entry(fi, &old->fi_list, nh_list) {
		int err;

		err = fib_check_nexthop(new, fi->fib_scope, extack);
		if (err)
			return err;
	}
	return 0;
}

static bool nh_res_nhge_is_balanced(const struct nh_grp_entry *nhge)
{
	return nhge->res.count_buckets == nhge->res.wants_buckets;
}

static bool nh_res_nhge_is_ow(const struct nh_grp_entry *nhge)
{
	return nhge->res.count_buckets > nhge->res.wants_buckets;
}

static bool nh_res_nhge_is_uw(const struct nh_grp_entry *nhge)
{
	return nhge->res.count_buckets < nhge->res.wants_buckets;
}

static bool nh_res_table_is_balanced(const struct nh_res_table *res_table)
{
	return list_empty(&res_table->uw_nh_entries);
}

static void nh_res_bucket_unset_nh(struct nh_res_bucket *bucket)
{
	struct nh_grp_entry *nhge;

	if (bucket->occupied) {
		nhge = nh_res_dereference(bucket->nh_entry);
		nhge->res.count_buckets--;
		bucket->occupied = false;
	}
}

static void nh_res_bucket_set_nh(struct nh_res_bucket *bucket,
				 struct nh_grp_entry *nhge)
{
	nh_res_bucket_unset_nh(bucket);

	bucket->occupied = true;
	rcu_assign_pointer(bucket->nh_entry, nhge);
	nhge->res.count_buckets++;
}

static bool nh_res_bucket_should_migrate(struct nh_res_table *res_table,
					 struct nh_res_bucket *bucket,
					 unsigned long *deadline, bool *force)
{
	unsigned long now = jiffies;
	struct nh_grp_entry *nhge;
	unsigned long idle_point;

	if (!bucket->occupied) {
		/* The bucket is not occupied, its NHGE pointer is either
		 * NULL or obsolete. We _have to_ migrate: set force.
		 */
		*force = true;
		return true;
	}

	nhge = nh_res_dereference(bucket->nh_entry);

	/* If the bucket is populated by an underweight or balanced
	 * nexthop, do not migrate.
	 */
	if (!nh_res_nhge_is_ow(nhge))
		return false;

	/* At this point we know that the bucket is populated with an
	 * overweight nexthop. It needs to be migrated to a new nexthop if
	 * the idle timer of unbalanced timer expired.
	 */

	idle_point = nh_res_bucket_idle_point(res_table, bucket, now);
	if (time_after_eq(now, idle_point)) {
		/* The bucket is idle. We _can_ migrate: unset force. */
		*force = false;
		return true;
	}

	/* Unbalanced timer of 0 means "never force". */
	if (res_table->unbalanced_timer) {
		unsigned long unb_point;

		unb_point = nh_res_table_unb_point(res_table);
		if (time_after(now, unb_point)) {
			/* The bucket is not idle, but the unbalanced timer
			 * expired. We _can_ migrate, but set force anyway,
			 * so that drivers know to ignore activity reports
			 * from the HW.
			 */
			*force = true;
			return true;
		}

		nh_res_time_set_deadline(unb_point, deadline);
	}

	nh_res_time_set_deadline(idle_point, deadline);
	return false;
}

static bool nh_res_bucket_migrate(struct nh_res_table *res_table,
				  u16 bucket_index, bool notify,
				  bool notify_nl, bool force)
{
	struct nh_res_bucket *bucket = &res_table->nh_buckets[bucket_index];
	struct nh_grp_entry *new_nhge;
	struct netlink_ext_ack extack;
	int err;

	new_nhge = list_first_entry_or_null(&res_table->uw_nh_entries,
					    struct nh_grp_entry,
					    res.uw_nh_entry);
	if (WARN_ON_ONCE(!new_nhge))
		/* If this function is called, "bucket" is either not
		 * occupied, or it belongs to a next hop that is
		 * overweight. In either case, there ought to be a
		 * corresponding underweight next hop.
		 */
		return false;

	if (notify) {
		struct nh_grp_entry *old_nhge;

		old_nhge = nh_res_dereference(bucket->nh_entry);
		err = call_nexthop_res_bucket_notifiers(res_table->net,
							res_table->nhg_id,
							bucket_index, force,
							old_nhge->nh,
							new_nhge->nh, &extack);
		if (err) {
			pr_err_ratelimited("%s\n", extack._msg);
			if (!force)
				return false;
			/* It is not possible to veto a forced replacement, so
			 * just clear the hardware flags from the nexthop
			 * bucket to indicate to user space that this bucket is
			 * not correctly populated in hardware.
			 */
			bucket->nh_flags &= ~(RTNH_F_OFFLOAD | RTNH_F_TRAP);
		}
	}

	nh_res_bucket_set_nh(bucket, new_nhge);
	nh_res_bucket_set_idle(res_table, bucket);

	if (notify_nl)
		nexthop_bucket_notify(res_table, bucket_index);

	if (nh_res_nhge_is_balanced(new_nhge))
		list_del(&new_nhge->res.uw_nh_entry);
	return true;
}

#define NH_RES_UPKEEP_DW_MINIMUM_INTERVAL (HZ / 2)

static void nh_res_table_upkeep(struct nh_res_table *res_table,
				bool notify, bool notify_nl)
{
	unsigned long now = jiffies;
	unsigned long deadline;
	u16 i;

	/* Deadline is the next time that upkeep should be run. It is the
	 * earliest time at which one of the buckets might be migrated.
	 * Start at the most pessimistic estimate: either unbalanced_timer
	 * from now, or if there is none, idle_timer from now. For each
	 * encountered time point, call nh_res_time_set_deadline() to
	 * refine the estimate.
	 */
	if (res_table->unbalanced_timer)
		deadline = now + res_table->unbalanced_timer;
	else
		deadline = now + res_table->idle_timer;

	for (i = 0; i < res_table->num_nh_buckets; i++) {
		struct nh_res_bucket *bucket = &res_table->nh_buckets[i];
		bool force;

		if (nh_res_bucket_should_migrate(res_table, bucket,
						 &deadline, &force)) {
			if (!nh_res_bucket_migrate(res_table, i, notify,
						   notify_nl, force)) {
				unsigned long idle_point;

				/* A driver can override the migration
				 * decision if the HW reports that the
				 * bucket is actually not idle. Therefore
				 * remark the bucket as busy again and
				 * update the deadline.
				 */
				nh_res_bucket_set_busy(bucket);
				idle_point = nh_res_bucket_idle_point(res_table,
								      bucket,
								      now);
				nh_res_time_set_deadline(idle_point, &deadline);
			}
		}
	}

	/* If the group is still unbalanced, schedule the next upkeep to
	 * either the deadline computed above, or the minimum deadline,
	 * whichever comes later.
	 */
	if (!nh_res_table_is_balanced(res_table)) {
		unsigned long now = jiffies;
		unsigned long min_deadline;

		min_deadline = now + NH_RES_UPKEEP_DW_MINIMUM_INTERVAL;
		if (time_before(deadline, min_deadline))
			deadline = min_deadline;

		queue_delayed_work(system_power_efficient_wq,
				   &res_table->upkeep_dw, deadline - now);
	}
}

static void nh_res_table_upkeep_dw(struct work_struct *work)
{
	struct delayed_work *dw = to_delayed_work(work);
	struct nh_res_table *res_table;

	res_table = container_of(dw, struct nh_res_table, upkeep_dw);
	nh_res_table_upkeep(res_table, true, true);
}

static void nh_res_table_cancel_upkeep(struct nh_res_table *res_table)
{
	cancel_delayed_work_sync(&res_table->upkeep_dw);
}

static void nh_res_group_rebalance(struct nh_group *nhg,
				   struct nh_res_table *res_table)
{
	int prev_upper_bound = 0;
	int total = 0;
	int w = 0;
	int i;

	INIT_LIST_HEAD(&res_table->uw_nh_entries);

	for (i = 0; i < nhg->num_nh; ++i)
		total += nhg->nh_entries[i].weight;

	for (i = 0; i < nhg->num_nh; ++i) {
		struct nh_grp_entry *nhge = &nhg->nh_entries[i];
		int upper_bound;

		w += nhge->weight;
		upper_bound = DIV_ROUND_CLOSEST(res_table->num_nh_buckets * w,
						total);
		nhge->res.wants_buckets = upper_bound - prev_upper_bound;
		prev_upper_bound = upper_bound;

		if (nh_res_nhge_is_uw(nhge)) {
			if (list_empty(&res_table->uw_nh_entries))
				res_table->unbalanced_since = jiffies;
			list_add(&nhge->res.uw_nh_entry,
				 &res_table->uw_nh_entries);
		}
	}
}

/* Migrate buckets in res_table so that they reference NHGE's from NHG with
 * the right NH ID. Set those buckets that do not have a corresponding NHGE
 * entry in NHG as not occupied.
 */
static void nh_res_table_migrate_buckets(struct nh_res_table *res_table,
					 struct nh_group *nhg)
{
	u16 i;

	for (i = 0; i < res_table->num_nh_buckets; i++) {
		struct nh_res_bucket *bucket = &res_table->nh_buckets[i];
		u32 id = rtnl_dereference(bucket->nh_entry)->nh->id;
		bool found = false;
		int j;

		for (j = 0; j < nhg->num_nh; j++) {
			struct nh_grp_entry *nhge = &nhg->nh_entries[j];

			if (nhge->nh->id == id) {
				nh_res_bucket_set_nh(bucket, nhge);
				found = true;
				break;
			}
		}

		if (!found)
			nh_res_bucket_unset_nh(bucket);
	}
}

static void replace_nexthop_grp_res(struct nh_group *oldg,
				    struct nh_group *newg)
{
	/* For NH group replacement, the new NHG might only have a stub
	 * hash table with 0 buckets, because the number of buckets was not
	 * specified. For NH removal, oldg and newg both reference the same
	 * res_table. So in any case, in the following, we want to work
	 * with oldg->res_table.
	 */
	struct nh_res_table *old_res_table = rtnl_dereference(oldg->res_table);
	unsigned long prev_unbalanced_since = old_res_table->unbalanced_since;
	bool prev_has_uw = !list_empty(&old_res_table->uw_nh_entries);

	nh_res_table_cancel_upkeep(old_res_table);
	nh_res_table_migrate_buckets(old_res_table, newg);
	nh_res_group_rebalance(newg, old_res_table);
	if (prev_has_uw && !list_empty(&old_res_table->uw_nh_entries))
		old_res_table->unbalanced_since = prev_unbalanced_since;
	nh_res_table_upkeep(old_res_table, true, false);
}

static void nh_hthr_group_rebalance(struct nh_group *nhg)
{
	int total = 0;
	int w = 0;
	int i;

	for (i = 0; i < nhg->num_nh; ++i)
		total += nhg->nh_entries[i].weight;

	for (i = 0; i < nhg->num_nh; ++i) {
		struct nh_grp_entry *nhge = &nhg->nh_entries[i];
		int upper_bound;

		w += nhge->weight;
		upper_bound = DIV_ROUND_CLOSEST_ULL((u64)w << 31, total) - 1;
		atomic_set(&nhge->hthr.upper_bound, upper_bound);
	}
}

static void remove_nh_grp_entry(struct net *net, struct nh_grp_entry *nhge,
				struct nl_info *nlinfo)
{
	struct nh_grp_entry *nhges, *new_nhges;
	struct nexthop *nhp = nhge->nh_parent;
	struct netlink_ext_ack extack;
	struct nexthop *nh = nhge->nh;
	struct nh_group *nhg, *newg;
	int i, j, err;

	WARN_ON(!nh);

	nhg = rtnl_dereference(nhp->nh_grp);
	newg = nhg->spare;

	/* last entry, keep it visible and remove the parent */
	if (nhg->num_nh == 1) {
		remove_nexthop(net, nhp, nlinfo);
		return;
	}

	newg->has_v4 = false;
	newg->is_multipath = nhg->is_multipath;
	newg->hash_threshold = nhg->hash_threshold;
	newg->resilient = nhg->resilient;
	newg->fdb_nh = nhg->fdb_nh;
	newg->num_nh = nhg->num_nh;

	/* copy old entries to new except the one getting removed */
	nhges = nhg->nh_entries;
	new_nhges = newg->nh_entries;
	for (i = 0, j = 0; i < nhg->num_nh; ++i) {
		struct nh_info *nhi;

		/* current nexthop getting removed */
		if (nhg->nh_entries[i].nh == nh) {
			newg->num_nh--;
			continue;
		}

		nhi = rtnl_dereference(nhges[i].nh->nh_info);
		if (nhi->family == AF_INET)
			newg->has_v4 = true;

		list_del(&nhges[i].nh_list);
		new_nhges[j].nh_parent = nhges[i].nh_parent;
		new_nhges[j].nh = nhges[i].nh;
		new_nhges[j].weight = nhges[i].weight;
		list_add(&new_nhges[j].nh_list, &new_nhges[j].nh->grp_list);
		j++;
	}

	if (newg->hash_threshold)
		nh_hthr_group_rebalance(newg);
	else if (newg->resilient)
		replace_nexthop_grp_res(nhg, newg);

	rcu_assign_pointer(nhp->nh_grp, newg);

	list_del(&nhge->nh_list);
	nexthop_put(nhge->nh);

	/* Removal of a NH from a resilient group is notified through
	 * bucket notifications.
	 */
	if (newg->hash_threshold) {
		err = call_nexthop_notifiers(net, NEXTHOP_EVENT_REPLACE, nhp,
					     &extack);
		if (err)
			pr_err("%s\n", extack._msg);
	}

	if (nlinfo)
		nexthop_notify(RTM_NEWNEXTHOP, nhp, nlinfo);
}

static void remove_nexthop_from_groups(struct net *net, struct nexthop *nh,
				       struct nl_info *nlinfo)
{
	struct nh_grp_entry *nhge, *tmp;

	list_for_each_entry_safe(nhge, tmp, &nh->grp_list, nh_list)
		remove_nh_grp_entry(net, nhge, nlinfo);

	/* make sure all see the newly published array before releasing rtnl */
	synchronize_net();
}

static void remove_nexthop_group(struct nexthop *nh, struct nl_info *nlinfo)
{
	struct nh_group *nhg = rcu_dereference_rtnl(nh->nh_grp);
	struct nh_res_table *res_table;
	int i, num_nh = nhg->num_nh;

	for (i = 0; i < num_nh; ++i) {
		struct nh_grp_entry *nhge = &nhg->nh_entries[i];

		if (WARN_ON(!nhge->nh))
			continue;

		list_del_init(&nhge->nh_list);
	}

	if (nhg->resilient) {
		res_table = rtnl_dereference(nhg->res_table);
		nh_res_table_cancel_upkeep(res_table);
	}
}

/* not called for nexthop replace */
static void __remove_nexthop_fib(struct net *net, struct nexthop *nh)
{
	struct fib6_info *f6i, *tmp;
	bool do_flush = false;
	struct fib_info *fi;

	list_for_each_entry(fi, &nh->fi_list, nh_list) {
		fi->fib_flags |= RTNH_F_DEAD;
		do_flush = true;
	}
	if (do_flush)
		fib_flush(net);

	/* ip6_del_rt removes the entry from this list hence the _safe */
	list_for_each_entry_safe(f6i, tmp, &nh->f6i_list, nh_list) {
		/* __ip6_del_rt does a release, so do a hold here */
		fib6_info_hold(f6i);
		ipv6_stub->ip6_del_rt(net, f6i,
				      !READ_ONCE(net->ipv4.sysctl_nexthop_compat_mode));
	}
}

static void __remove_nexthop(struct net *net, struct nexthop *nh,
			     struct nl_info *nlinfo)
{
	__remove_nexthop_fib(net, nh);

	if (nh->is_group) {
		remove_nexthop_group(nh, nlinfo);
	} else {
		struct nh_info *nhi;

		nhi = rtnl_dereference(nh->nh_info);
		if (nhi->fib_nhc.nhc_dev)
			hlist_del(&nhi->dev_hash);

		remove_nexthop_from_groups(net, nh, nlinfo);
	}
}

static void remove_nexthop(struct net *net, struct nexthop *nh,
			   struct nl_info *nlinfo)
{
	call_nexthop_notifiers(net, NEXTHOP_EVENT_DEL, nh, NULL);

	/* remove from the tree */
	rb_erase(&nh->rb_node, &net->nexthop.rb_root);

	if (nlinfo)
		nexthop_notify(RTM_DELNEXTHOP, nh, nlinfo);

	__remove_nexthop(net, nh, nlinfo);
	nh_base_seq_inc(net);

	nexthop_put(nh);
}

/* if any FIB entries reference this nexthop, any dst entries
 * need to be regenerated
 */
static void nh_rt_cache_flush(struct net *net, struct nexthop *nh,
			      struct nexthop *replaced_nh)
{
	struct fib6_info *f6i;
	struct nh_group *nhg;
	int i;

	if (!list_empty(&nh->fi_list))
		rt_cache_flush(net);

	list_for_each_entry(f6i, &nh->f6i_list, nh_list)
		ipv6_stub->fib6_update_sernum(net, f6i);

	/* if an IPv6 group was replaced, we have to release all old
	 * dsts to make sure all refcounts are released
	 */
	if (!replaced_nh->is_group)
		return;

	nhg = rtnl_dereference(replaced_nh->nh_grp);
	for (i = 0; i < nhg->num_nh; i++) {
		struct nh_grp_entry *nhge = &nhg->nh_entries[i];
		struct nh_info *nhi = rtnl_dereference(nhge->nh->nh_info);

		if (nhi->family == AF_INET6)
			ipv6_stub->fib6_nh_release_dsts(&nhi->fib6_nh);
	}
}

static int replace_nexthop_grp(struct net *net, struct nexthop *old,
			       struct nexthop *new, const struct nh_config *cfg,
			       struct netlink_ext_ack *extack)
{
	struct nh_res_table *tmp_table = NULL;
	struct nh_res_table *new_res_table;
	struct nh_res_table *old_res_table;
	struct nh_group *oldg, *newg;
	int i, err;

	if (!new->is_group) {
		NL_SET_ERR_MSG(extack, "Can not replace a nexthop group with a nexthop.");
		return -EINVAL;
	}

	oldg = rtnl_dereference(old->nh_grp);
	newg = rtnl_dereference(new->nh_grp);

	if (newg->hash_threshold != oldg->hash_threshold) {
		NL_SET_ERR_MSG(extack, "Can not replace a nexthop group with one of a different type.");
		return -EINVAL;
	}

	if (newg->hash_threshold) {
		err = call_nexthop_notifiers(net, NEXTHOP_EVENT_REPLACE, new,
					     extack);
		if (err)
			return err;
	} else if (newg->resilient) {
		new_res_table = rtnl_dereference(newg->res_table);
		old_res_table = rtnl_dereference(oldg->res_table);

		/* Accept if num_nh_buckets was not given, but if it was
		 * given, demand that the value be correct.
		 */
		if (cfg->nh_grp_res_has_num_buckets &&
		    cfg->nh_grp_res_num_buckets !=
		    old_res_table->num_nh_buckets) {
			NL_SET_ERR_MSG(extack, "Can not change number of buckets of a resilient nexthop group.");
			return -EINVAL;
		}

		/* Emit a pre-replace notification so that listeners could veto
		 * a potentially unsupported configuration. Otherwise,
		 * individual bucket replacement notifications would need to be
		 * vetoed, which is something that should only happen if the
		 * bucket is currently active.
		 */
		err = call_nexthop_res_table_notifiers(net, new, extack);
		if (err)
			return err;

		if (cfg->nh_grp_res_has_idle_timer)
			old_res_table->idle_timer = cfg->nh_grp_res_idle_timer;
		if (cfg->nh_grp_res_has_unbalanced_timer)
			old_res_table->unbalanced_timer =
				cfg->nh_grp_res_unbalanced_timer;

		replace_nexthop_grp_res(oldg, newg);

		tmp_table = new_res_table;
		rcu_assign_pointer(newg->res_table, old_res_table);
		rcu_assign_pointer(newg->spare->res_table, old_res_table);
	}

	/* update parents - used by nexthop code for cleanup */
	for (i = 0; i < newg->num_nh; i++)
		newg->nh_entries[i].nh_parent = old;

	rcu_assign_pointer(old->nh_grp, newg);

	/* Make sure concurrent readers are not using 'oldg' anymore. */
	synchronize_net();

	if (newg->resilient) {
		rcu_assign_pointer(oldg->res_table, tmp_table);
		rcu_assign_pointer(oldg->spare->res_table, tmp_table);
	}

	for (i = 0; i < oldg->num_nh; i++)
		oldg->nh_entries[i].nh_parent = new;

	rcu_assign_pointer(new->nh_grp, oldg);

	return 0;
}

static void nh_group_v4_update(struct nh_group *nhg)
{
	struct nh_grp_entry *nhges;
	bool has_v4 = false;
	int i;

	nhges = nhg->nh_entries;
	for (i = 0; i < nhg->num_nh; i++) {
		struct nh_info *nhi;

		nhi = rtnl_dereference(nhges[i].nh->nh_info);
		if (nhi->family == AF_INET)
			has_v4 = true;
	}
	nhg->has_v4 = has_v4;
}

static int replace_nexthop_single_notify_res(struct net *net,
					     struct nh_res_table *res_table,
					     struct nexthop *old,
					     struct nh_info *oldi,
					     struct nh_info *newi,
					     struct netlink_ext_ack *extack)
{
	u32 nhg_id = res_table->nhg_id;
	int err;
	u16 i;

	for (i = 0; i < res_table->num_nh_buckets; i++) {
		struct nh_res_bucket *bucket = &res_table->nh_buckets[i];
		struct nh_grp_entry *nhge;

		nhge = rtnl_dereference(bucket->nh_entry);
		if (nhge->nh == old) {
			err = __call_nexthop_res_bucket_notifiers(net, nhg_id,
								  i, true,
								  oldi, newi,
								  extack);
			if (err)
				goto err_notify;
		}
	}

	return 0;

err_notify:
	while (i-- > 0) {
		struct nh_res_bucket *bucket = &res_table->nh_buckets[i];
		struct nh_grp_entry *nhge;

		nhge = rtnl_dereference(bucket->nh_entry);
		if (nhge->nh == old)
			__call_nexthop_res_bucket_notifiers(net, nhg_id, i,
							    true, newi, oldi,
							    extack);
	}
	return err;
}

static int replace_nexthop_single_notify(struct net *net,
					 struct nexthop *group_nh,
					 struct nexthop *old,
					 struct nh_info *oldi,
					 struct nh_info *newi,
					 struct netlink_ext_ack *extack)
{
	struct nh_group *nhg = rtnl_dereference(group_nh->nh_grp);
	struct nh_res_table *res_table;

	if (nhg->hash_threshold) {
		return call_nexthop_notifiers(net, NEXTHOP_EVENT_REPLACE,
					      group_nh, extack);
	} else if (nhg->resilient) {
		res_table = rtnl_dereference(nhg->res_table);
		return replace_nexthop_single_notify_res(net, res_table,
							 old, oldi, newi,
							 extack);
	}

	return -EINVAL;
}

static int replace_nexthop_single(struct net *net, struct nexthop *old,
				  struct nexthop *new,
				  struct netlink_ext_ack *extack)
{
	u8 old_protocol, old_nh_flags;
	struct nh_info *oldi, *newi;
	struct nh_grp_entry *nhge;
	int err;

	if (new->is_group) {
		NL_SET_ERR_MSG(extack, "Can not replace a nexthop with a nexthop group.");
		return -EINVAL;
	}

	err = call_nexthop_notifiers(net, NEXTHOP_EVENT_REPLACE, new, extack);
	if (err)
		return err;

	/* Hardware flags were set on 'old' as 'new' is not in the red-black
	 * tree. Therefore, inherit the flags from 'old' to 'new'.
	 */
	new->nh_flags |= old->nh_flags & (RTNH_F_OFFLOAD | RTNH_F_TRAP);

	oldi = rtnl_dereference(old->nh_info);
	newi = rtnl_dereference(new->nh_info);

	newi->nh_parent = old;
	oldi->nh_parent = new;

	old_protocol = old->protocol;
	old_nh_flags = old->nh_flags;

	old->protocol = new->protocol;
	old->nh_flags = new->nh_flags;

	rcu_assign_pointer(old->nh_info, newi);
	rcu_assign_pointer(new->nh_info, oldi);

	/* Send a replace notification for all the groups using the nexthop. */
	list_for_each_entry(nhge, &old->grp_list, nh_list) {
		struct nexthop *nhp = nhge->nh_parent;

		err = replace_nexthop_single_notify(net, nhp, old, oldi, newi,
						    extack);
		if (err)
			goto err_notify;
	}

	/* When replacing an IPv4 nexthop with an IPv6 nexthop, potentially
	 * update IPv4 indication in all the groups using the nexthop.
	 */
	if (oldi->family == AF_INET && newi->family == AF_INET6) {
		list_for_each_entry(nhge, &old->grp_list, nh_list) {
			struct nexthop *nhp = nhge->nh_parent;
			struct nh_group *nhg;

			nhg = rtnl_dereference(nhp->nh_grp);
			nh_group_v4_update(nhg);
		}
	}

	return 0;

err_notify:
	rcu_assign_pointer(new->nh_info, newi);
	rcu_assign_pointer(old->nh_info, oldi);
	old->nh_flags = old_nh_flags;
	old->protocol = old_protocol;
	oldi->nh_parent = old;
	newi->nh_parent = new;
	list_for_each_entry_continue_reverse(nhge, &old->grp_list, nh_list) {
		struct nexthop *nhp = nhge->nh_parent;

		replace_nexthop_single_notify(net, nhp, old, newi, oldi, NULL);
	}
	call_nexthop_notifiers(net, NEXTHOP_EVENT_REPLACE, old, extack);
	return err;
}

static void __nexthop_replace_notify(struct net *net, struct nexthop *nh,
				     struct nl_info *info)
{
	struct fib6_info *f6i;

	if (!list_empty(&nh->fi_list)) {
		struct fib_info *fi;

		/* expectation is a few fib_info per nexthop and then
		 * a lot of routes per fib_info. So mark the fib_info
		 * and then walk the fib tables once
		 */
		list_for_each_entry(fi, &nh->fi_list, nh_list)
			fi->nh_updated = true;

		fib_info_notify_update(net, info);

		list_for_each_entry(fi, &nh->fi_list, nh_list)
			fi->nh_updated = false;
	}

	list_for_each_entry(f6i, &nh->f6i_list, nh_list)
		ipv6_stub->fib6_rt_update(net, f6i, info);
}

/* send RTM_NEWROUTE with REPLACE flag set for all FIB entries
 * linked to this nexthop and for all groups that the nexthop
 * is a member of
 */
static void nexthop_replace_notify(struct net *net, struct nexthop *nh,
				   struct nl_info *info)
{
	struct nh_grp_entry *nhge;

	__nexthop_replace_notify(net, nh, info);

	list_for_each_entry(nhge, &nh->grp_list, nh_list)
		__nexthop_replace_notify(net, nhge->nh_parent, info);
}

static int replace_nexthop(struct net *net, struct nexthop *old,
			   struct nexthop *new, const struct nh_config *cfg,
			   struct netlink_ext_ack *extack)
{
	bool new_is_reject = false;
	struct nh_grp_entry *nhge;
	int err;

	/* check that existing FIB entries are ok with the
	 * new nexthop definition
	 */
	err = fib_check_nh_list(old, new, extack);
	if (err)
		return err;

	err = fib6_check_nh_list(old, new, extack);
	if (err)
		return err;

	if (!new->is_group) {
		struct nh_info *nhi = rtnl_dereference(new->nh_info);

		new_is_reject = nhi->reject_nh;
	}

	list_for_each_entry(nhge, &old->grp_list, nh_list) {
		/* if new nexthop is a blackhole, any groups using this
		 * nexthop cannot have more than 1 path
		 */
		if (new_is_reject &&
		    nexthop_num_path(nhge->nh_parent) > 1) {
			NL_SET_ERR_MSG(extack, "Blackhole nexthop can not be a member of a group with more than one path");
			return -EINVAL;
		}

		err = fib_check_nh_list(nhge->nh_parent, new, extack);
		if (err)
			return err;

		err = fib6_check_nh_list(nhge->nh_parent, new, extack);
		if (err)
			return err;
	}

	if (old->is_group)
		err = replace_nexthop_grp(net, old, new, cfg, extack);
	else
		err = replace_nexthop_single(net, old, new, extack);

	if (!err) {
		nh_rt_cache_flush(net, old, new);

		__remove_nexthop(net, new, NULL);
		nexthop_put(new);
	}

	return err;
}

/* called with rtnl_lock held */
static int insert_nexthop(struct net *net, struct nexthop *new_nh,
			  struct nh_config *cfg, struct netlink_ext_ack *extack)
{
	struct rb_node **pp, *parent = NULL, *next;
	struct rb_root *root = &net->nexthop.rb_root;
	bool replace = !!(cfg->nlflags & NLM_F_REPLACE);
	bool create = !!(cfg->nlflags & NLM_F_CREATE);
	u32 new_id = new_nh->id;
	int replace_notify = 0;
	int rc = -EEXIST;

	pp = &root->rb_node;
	while (1) {
		struct nexthop *nh;

		next = *pp;
		if (!next)
			break;

		parent = next;

		nh = rb_entry(parent, struct nexthop, rb_node);
		if (new_id < nh->id) {
			pp = &next->rb_left;
		} else if (new_id > nh->id) {
			pp = &next->rb_right;
		} else if (replace) {
			rc = replace_nexthop(net, nh, new_nh, cfg, extack);
			if (!rc) {
				new_nh = nh; /* send notification with old nh */
				replace_notify = 1;
			}
			goto out;
		} else {
			/* id already exists and not a replace */
			goto out;
		}
	}

	if (replace && !create) {
		NL_SET_ERR_MSG(extack, "Replace specified without create and no entry exists");
		rc = -ENOENT;
		goto out;
	}

	if (new_nh->is_group) {
		struct nh_group *nhg = rtnl_dereference(new_nh->nh_grp);
		struct nh_res_table *res_table;

		if (nhg->resilient) {
			res_table = rtnl_dereference(nhg->res_table);

			/* Not passing the number of buckets is OK when
			 * replacing, but not when creating a new group.
			 */
			if (!cfg->nh_grp_res_has_num_buckets) {
				NL_SET_ERR_MSG(extack, "Number of buckets not specified for nexthop group insertion");
				rc = -EINVAL;
				goto out;
			}

			nh_res_group_rebalance(nhg, res_table);

			/* Do not send bucket notifications, we do full
			 * notification below.
			 */
			nh_res_table_upkeep(res_table, false, false);
		}
	}

	rb_link_node_rcu(&new_nh->rb_node, parent, pp);
	rb_insert_color(&new_nh->rb_node, root);

	/* The initial insertion is a full notification for hash-threshold as
	 * well as resilient groups.
	 */
	rc = call_nexthop_notifiers(net, NEXTHOP_EVENT_REPLACE, new_nh, extack);
	if (rc)
		rb_erase(&new_nh->rb_node, &net->nexthop.rb_root);

out:
	if (!rc) {
		nh_base_seq_inc(net);
		nexthop_notify(RTM_NEWNEXTHOP, new_nh, &cfg->nlinfo);
		if (replace_notify &&
		    READ_ONCE(net->ipv4.sysctl_nexthop_compat_mode))
			nexthop_replace_notify(net, new_nh, &cfg->nlinfo);
	}

	return rc;
}

/* rtnl */
/* remove all nexthops tied to a device being deleted */
static void nexthop_flush_dev(struct net_device *dev, unsigned long event)
{
	unsigned int hash = nh_dev_hashfn(dev->ifindex);
	struct net *net = dev_net(dev);
	struct hlist_head *head = &net->nexthop.devhash[hash];
	struct hlist_node *n;
	struct nh_info *nhi;

	hlist_for_each_entry_safe(nhi, n, head, dev_hash) {
		if (nhi->fib_nhc.nhc_dev != dev)
			continue;

		if (nhi->reject_nh &&
		    (event == NETDEV_DOWN || event == NETDEV_CHANGE))
			continue;

		remove_nexthop(net, nhi->nh_parent, NULL);
	}
}

/* rtnl; called when net namespace is deleted */
static void flush_all_nexthops(struct net *net)
{
	struct rb_root *root = &net->nexthop.rb_root;
	struct rb_node *node;
	struct nexthop *nh;

	while ((node = rb_first(root))) {
		nh = rb_entry(node, struct nexthop, rb_node);
		remove_nexthop(net, nh, NULL);
		cond_resched();
	}
}

static struct nexthop *nexthop_create_group(struct net *net,
					    struct nh_config *cfg)
{
	struct nlattr *grps_attr = cfg->nh_grp;
	struct nexthop_grp *entry = nla_data(grps_attr);
	u16 num_nh = nla_len(grps_attr) / sizeof(*entry);
	struct nh_group *nhg;
	struct nexthop *nh;
	int err;
	int i;

	if (WARN_ON(!num_nh))
		return ERR_PTR(-EINVAL);

	nh = nexthop_alloc();
	if (!nh)
		return ERR_PTR(-ENOMEM);

	nh->is_group = 1;

	nhg = nexthop_grp_alloc(num_nh);
	if (!nhg) {
		kfree(nh);
		return ERR_PTR(-ENOMEM);
	}

	/* spare group used for removals */
	nhg->spare = nexthop_grp_alloc(num_nh);
	if (!nhg->spare) {
		kfree(nhg);
		kfree(nh);
		return ERR_PTR(-ENOMEM);
	}
	nhg->spare->spare = nhg;

	for (i = 0; i < nhg->num_nh; ++i) {
		struct nexthop *nhe;
		struct nh_info *nhi;

		nhe = nexthop_find_by_id(net, entry[i].id);
		if (!nexthop_get(nhe)) {
			err = -ENOENT;
			goto out_no_nh;
		}

		nhi = rtnl_dereference(nhe->nh_info);
		if (nhi->family == AF_INET)
			nhg->has_v4 = true;

		nhg->nh_entries[i].nh = nhe;
		nhg->nh_entries[i].weight = entry[i].weight + 1;
		list_add(&nhg->nh_entries[i].nh_list, &nhe->grp_list);
		nhg->nh_entries[i].nh_parent = nh;
	}

	if (cfg->nh_grp_type == NEXTHOP_GRP_TYPE_MPATH) {
		nhg->hash_threshold = 1;
		nhg->is_multipath = true;
	} else if (cfg->nh_grp_type == NEXTHOP_GRP_TYPE_RES) {
		struct nh_res_table *res_table;

		res_table = nexthop_res_table_alloc(net, cfg->nh_id, cfg);
		if (!res_table) {
			err = -ENOMEM;
			goto out_no_nh;
		}

		rcu_assign_pointer(nhg->spare->res_table, res_table);
		rcu_assign_pointer(nhg->res_table, res_table);
		nhg->resilient = true;
		nhg->is_multipath = true;
	}

	WARN_ON_ONCE(nhg->hash_threshold + nhg->resilient != 1);

	if (nhg->hash_threshold)
		nh_hthr_group_rebalance(nhg);

	if (cfg->nh_fdb)
		nhg->fdb_nh = 1;

	rcu_assign_pointer(nh->nh_grp, nhg);

	return nh;

out_no_nh:
	for (i--; i >= 0; --i) {
		list_del(&nhg->nh_entries[i].nh_list);
		nexthop_put(nhg->nh_entries[i].nh);
	}

	kfree(nhg->spare);
	kfree(nhg);
	kfree(nh);

	return ERR_PTR(err);
}

static int nh_create_ipv4(struct net *net, struct nexthop *nh,
			  struct nh_info *nhi, struct nh_config *cfg,
			  struct netlink_ext_ack *extack)
{
	struct fib_nh *fib_nh = &nhi->fib_nh;
	struct fib_config fib_cfg = {
		.fc_oif   = cfg->nh_ifindex,
		.fc_gw4   = cfg->gw.ipv4,
		.fc_gw_family = cfg->gw.ipv4 ? AF_INET : 0,
		.fc_flags = cfg->nh_flags,
		.fc_nlinfo = cfg->nlinfo,
		.fc_encap = cfg->nh_encap,
		.fc_encap_type = cfg->nh_encap_type,
	};
	u32 tb_id = (cfg->dev ? l3mdev_fib_table(cfg->dev) : RT_TABLE_MAIN);
	int err;

	err = fib_nh_init(net, fib_nh, &fib_cfg, 1, extack);
	if (err) {
		fib_nh_release(net, fib_nh);
		goto out;
	}

	if (nhi->fdb_nh)
		goto out;

	/* sets nh_dev if successful */
	err = fib_check_nh(net, fib_nh, tb_id, 0, extack);
	if (!err) {
		nh->nh_flags = fib_nh->fib_nh_flags;
		fib_info_update_nhc_saddr(net, &fib_nh->nh_common,
					  !fib_nh->fib_nh_scope ? 0 : fib_nh->fib_nh_scope - 1);
	} else {
		fib_nh_release(net, fib_nh);
	}
out:
	return err;
}

static int nh_create_ipv6(struct net *net,  struct nexthop *nh,
			  struct nh_info *nhi, struct nh_config *cfg,
			  struct netlink_ext_ack *extack)
{
	struct fib6_nh *fib6_nh = &nhi->fib6_nh;
	struct fib6_config fib6_cfg = {
		.fc_table = l3mdev_fib_table(cfg->dev),
		.fc_ifindex = cfg->nh_ifindex,
		.fc_gateway = cfg->gw.ipv6,
		.fc_flags = cfg->nh_flags,
		.fc_nlinfo = cfg->nlinfo,
		.fc_encap = cfg->nh_encap,
		.fc_encap_type = cfg->nh_encap_type,
		.fc_is_fdb = cfg->nh_fdb,
	};
	int err;

	if (!ipv6_addr_any(&cfg->gw.ipv6))
		fib6_cfg.fc_flags |= RTF_GATEWAY;

	/* sets nh_dev if successful */
	err = ipv6_stub->fib6_nh_init(net, fib6_nh, &fib6_cfg, GFP_KERNEL,
				      extack);
	if (err) {
		/* IPv6 is not enabled, don't call fib6_nh_release */
		if (err == -EAFNOSUPPORT)
			goto out;
		ipv6_stub->fib6_nh_release(fib6_nh);
	} else {
		nh->nh_flags = fib6_nh->fib_nh_flags;
	}
out:
	return err;
}

static struct nexthop *nexthop_create(struct net *net, struct nh_config *cfg,
				      struct netlink_ext_ack *extack)
{
	struct nh_info *nhi;
	struct nexthop *nh;
	int err = 0;

	nh = nexthop_alloc();
	if (!nh)
		return ERR_PTR(-ENOMEM);

	nhi = kzalloc(sizeof(*nhi), GFP_KERNEL);
	if (!nhi) {
		kfree(nh);
		return ERR_PTR(-ENOMEM);
	}

	nh->nh_flags = cfg->nh_flags;
	nh->net = net;

	nhi->nh_parent = nh;
	nhi->family = cfg->nh_family;
	nhi->fib_nhc.nhc_scope = RT_SCOPE_LINK;

	if (cfg->nh_fdb)
		nhi->fdb_nh = 1;

	if (cfg->nh_blackhole) {
		nhi->reject_nh = 1;
		cfg->nh_ifindex = net->loopback_dev->ifindex;
	}

	switch (cfg->nh_family) {
	case AF_INET:
		err = nh_create_ipv4(net, nh, nhi, cfg, extack);
		break;
	case AF_INET6:
		err = nh_create_ipv6(net, nh, nhi, cfg, extack);
		break;
	}

	if (err) {
		kfree(nhi);
		kfree(nh);
		return ERR_PTR(err);
	}

	/* add the entry to the device based hash */
	if (!nhi->fdb_nh)
		nexthop_devhash_add(net, nhi);

	rcu_assign_pointer(nh->nh_info, nhi);

	return nh;
}

/* called with rtnl lock held */
static struct nexthop *nexthop_add(struct net *net, struct nh_config *cfg,
				   struct netlink_ext_ack *extack)
{
	struct nexthop *nh;
	int err;

	if (cfg->nlflags & NLM_F_REPLACE && !cfg->nh_id) {
		NL_SET_ERR_MSG(extack, "Replace requires nexthop id");
		return ERR_PTR(-EINVAL);
	}

	if (!cfg->nh_id) {
		cfg->nh_id = nh_find_unused_id(net);
		if (!cfg->nh_id) {
			NL_SET_ERR_MSG(extack, "No unused id");
			return ERR_PTR(-EINVAL);
		}
	}

	if (cfg->nh_grp)
		nh = nexthop_create_group(net, cfg);
	else
		nh = nexthop_create(net, cfg, extack);

	if (IS_ERR(nh))
		return nh;

	refcount_set(&nh->refcnt, 1);
	nh->id = cfg->nh_id;
	nh->protocol = cfg->nh_protocol;
	nh->net = net;

	err = insert_nexthop(net, nh, cfg, extack);
	if (err) {
		__remove_nexthop(net, nh, NULL);
		nexthop_put(nh);
		nh = ERR_PTR(err);
	}

	return nh;
}

static int rtm_nh_get_timer(struct nlattr *attr, unsigned long fallback,
			    unsigned long *timer_p, bool *has_p,
			    struct netlink_ext_ack *extack)
{
	unsigned long timer;
	u32 value;

	if (!attr) {
		*timer_p = fallback;
		*has_p = false;
		return 0;
	}

	value = nla_get_u32(attr);
	timer = clock_t_to_jiffies(value);
	if (timer == ~0UL) {
		NL_SET_ERR_MSG(extack, "Timer value too large");
		return -EINVAL;
	}

	*timer_p = timer;
	*has_p = true;
	return 0;
}

static int rtm_to_nh_config_grp_res(struct nlattr *res, struct nh_config *cfg,
				    struct netlink_ext_ack *extack)
{
	struct nlattr *tb[ARRAY_SIZE(rtm_nh_res_policy_new)] = {};
	int err;

	if (res) {
		err = nla_parse_nested(tb,
				       ARRAY_SIZE(rtm_nh_res_policy_new) - 1,
				       res, rtm_nh_res_policy_new, extack);
		if (err < 0)
			return err;
	}

	if (tb[NHA_RES_GROUP_BUCKETS]) {
		cfg->nh_grp_res_num_buckets =
			nla_get_u16(tb[NHA_RES_GROUP_BUCKETS]);
		cfg->nh_grp_res_has_num_buckets = true;
		if (!cfg->nh_grp_res_num_buckets) {
			NL_SET_ERR_MSG(extack, "Number of buckets needs to be non-0");
			return -EINVAL;
		}
	}

	err = rtm_nh_get_timer(tb[NHA_RES_GROUP_IDLE_TIMER],
			       NH_RES_DEFAULT_IDLE_TIMER,
			       &cfg->nh_grp_res_idle_timer,
			       &cfg->nh_grp_res_has_idle_timer,
			       extack);
	if (err)
		return err;

	return rtm_nh_get_timer(tb[NHA_RES_GROUP_UNBALANCED_TIMER],
				NH_RES_DEFAULT_UNBALANCED_TIMER,
				&cfg->nh_grp_res_unbalanced_timer,
				&cfg->nh_grp_res_has_unbalanced_timer,
				extack);
}

static int rtm_to_nh_config(struct net *net, struct sk_buff *skb,
			    struct nlmsghdr *nlh, struct nh_config *cfg,
			    struct netlink_ext_ack *extack)
{
	struct nhmsg *nhm = nlmsg_data(nlh);
	struct nlattr *tb[ARRAY_SIZE(rtm_nh_policy_new)];
	int err;

	err = nlmsg_parse(nlh, sizeof(*nhm), tb,
			  ARRAY_SIZE(rtm_nh_policy_new) - 1,
			  rtm_nh_policy_new, extack);
	if (err < 0)
		return err;

	err = -EINVAL;
	if (nhm->resvd || nhm->nh_scope) {
		NL_SET_ERR_MSG(extack, "Invalid values in ancillary header");
		goto out;
	}
	if (nhm->nh_flags & ~NEXTHOP_VALID_USER_FLAGS) {
		NL_SET_ERR_MSG(extack, "Invalid nexthop flags in ancillary header");
		goto out;
	}

	switch (nhm->nh_family) {
	case AF_INET:
	case AF_INET6:
		break;
	case AF_UNSPEC:
		if (tb[NHA_GROUP])
			break;
		fallthrough;
	default:
		NL_SET_ERR_MSG(extack, "Invalid address family");
		goto out;
	}

	memset(cfg, 0, sizeof(*cfg));
	cfg->nlflags = nlh->nlmsg_flags;
	cfg->nlinfo.portid = NETLINK_CB(skb).portid;
	cfg->nlinfo.nlh = nlh;
	cfg->nlinfo.nl_net = net;

	cfg->nh_family = nhm->nh_family;
	cfg->nh_protocol = nhm->nh_protocol;
	cfg->nh_flags = nhm->nh_flags;

	if (tb[NHA_ID])
		cfg->nh_id = nla_get_u32(tb[NHA_ID]);

	if (tb[NHA_FDB]) {
		if (tb[NHA_OIF] || tb[NHA_BLACKHOLE] ||
		    tb[NHA_ENCAP]   || tb[NHA_ENCAP_TYPE]) {
			NL_SET_ERR_MSG(extack, "Fdb attribute can not be used with encap, oif or blackhole");
			goto out;
		}
		if (nhm->nh_flags) {
			NL_SET_ERR_MSG(extack, "Unsupported nexthop flags in ancillary header");
			goto out;
		}
		cfg->nh_fdb = nla_get_flag(tb[NHA_FDB]);
	}

	if (tb[NHA_GROUP]) {
		if (nhm->nh_family != AF_UNSPEC) {
			NL_SET_ERR_MSG(extack, "Invalid family for group");
			goto out;
		}
		cfg->nh_grp = tb[NHA_GROUP];

		cfg->nh_grp_type = NEXTHOP_GRP_TYPE_MPATH;
		if (tb[NHA_GROUP_TYPE])
			cfg->nh_grp_type = nla_get_u16(tb[NHA_GROUP_TYPE]);

		if (cfg->nh_grp_type > NEXTHOP_GRP_TYPE_MAX) {
			NL_SET_ERR_MSG(extack, "Invalid group type");
			goto out;
		}
		err = nh_check_attr_group(net, tb, ARRAY_SIZE(tb),
					  cfg->nh_grp_type, extack);
		if (err)
			goto out;

		if (cfg->nh_grp_type == NEXTHOP_GRP_TYPE_RES)
			err = rtm_to_nh_config_grp_res(tb[NHA_RES_GROUP],
						       cfg, extack);

		/* no other attributes should be set */
		goto out;
	}

	if (tb[NHA_BLACKHOLE]) {
		if (tb[NHA_GATEWAY] || tb[NHA_OIF] ||
		    tb[NHA_ENCAP]   || tb[NHA_ENCAP_TYPE] || tb[NHA_FDB]) {
			NL_SET_ERR_MSG(extack, "Blackhole attribute can not be used with gateway, oif, encap or fdb");
			goto out;
		}

		cfg->nh_blackhole = 1;
		err = 0;
		goto out;
	}

	if (!cfg->nh_fdb && !tb[NHA_OIF]) {
		NL_SET_ERR_MSG(extack, "Device attribute required for non-blackhole and non-fdb nexthops");
		goto out;
	}

	if (!cfg->nh_fdb && tb[NHA_OIF]) {
		cfg->nh_ifindex = nla_get_u32(tb[NHA_OIF]);
		if (cfg->nh_ifindex)
			cfg->dev = __dev_get_by_index(net, cfg->nh_ifindex);

		if (!cfg->dev) {
			NL_SET_ERR_MSG(extack, "Invalid device index");
			goto out;
		} else if (!(cfg->dev->flags & IFF_UP)) {
			NL_SET_ERR_MSG(extack, "Nexthop device is not up");
			err = -ENETDOWN;
			goto out;
		} else if (!netif_carrier_ok(cfg->dev)) {
			NL_SET_ERR_MSG(extack, "Carrier for nexthop device is down");
			err = -ENETDOWN;
			goto out;
		}
	}

	err = -EINVAL;
	if (tb[NHA_GATEWAY]) {
		struct nlattr *gwa = tb[NHA_GATEWAY];

		switch (cfg->nh_family) {
		case AF_INET:
			if (nla_len(gwa) != sizeof(u32)) {
				NL_SET_ERR_MSG(extack, "Invalid gateway");
				goto out;
			}
			cfg->gw.ipv4 = nla_get_be32(gwa);
			break;
		case AF_INET6:
			if (nla_len(gwa) != sizeof(struct in6_addr)) {
				NL_SET_ERR_MSG(extack, "Invalid gateway");
				goto out;
			}
			cfg->gw.ipv6 = nla_get_in6_addr(gwa);
			break;
		default:
			NL_SET_ERR_MSG(extack,
				       "Unknown address family for gateway");
			goto out;
		}
	} else {
		/* device only nexthop (no gateway) */
		if (cfg->nh_flags & RTNH_F_ONLINK) {
			NL_SET_ERR_MSG(extack,
				       "ONLINK flag can not be set for nexthop without a gateway");
			goto out;
		}
	}

	if (tb[NHA_ENCAP]) {
		cfg->nh_encap = tb[NHA_ENCAP];

		if (!tb[NHA_ENCAP_TYPE]) {
			NL_SET_ERR_MSG(extack, "LWT encapsulation type is missing");
			goto out;
		}

		cfg->nh_encap_type = nla_get_u16(tb[NHA_ENCAP_TYPE]);
		err = lwtunnel_valid_encap_type(cfg->nh_encap_type, extack);
		if (err < 0)
			goto out;

	} else if (tb[NHA_ENCAP_TYPE]) {
		NL_SET_ERR_MSG(extack, "LWT encapsulation attribute is missing");
		goto out;
	}


	err = 0;
out:
	return err;
}

/* rtnl */
static int rtm_new_nexthop(struct sk_buff *skb, struct nlmsghdr *nlh,
			   struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct nh_config cfg;
	struct nexthop *nh;
	int err;

	err = rtm_to_nh_config(net, skb, nlh, &cfg, extack);
	if (!err) {
		nh = nexthop_add(net, &cfg, extack);
		if (IS_ERR(nh))
			err = PTR_ERR(nh);
	}

	return err;
}

static int __nh_valid_get_del_req(const struct nlmsghdr *nlh,
				  struct nlattr **tb, u32 *id,
				  struct netlink_ext_ack *extack)
{
	struct nhmsg *nhm = nlmsg_data(nlh);

	if (nhm->nh_protocol || nhm->resvd || nhm->nh_scope || nhm->nh_flags) {
		NL_SET_ERR_MSG(extack, "Invalid values in header");
		return -EINVAL;
	}

	if (!tb[NHA_ID]) {
		NL_SET_ERR_MSG(extack, "Nexthop id is missing");
		return -EINVAL;
	}

	*id = nla_get_u32(tb[NHA_ID]);
	if (!(*id)) {
		NL_SET_ERR_MSG(extack, "Invalid nexthop id");
		return -EINVAL;
	}

	return 0;
}

static int nh_valid_get_del_req(const struct nlmsghdr *nlh, u32 *id,
				struct netlink_ext_ack *extack)
{
	struct nlattr *tb[ARRAY_SIZE(rtm_nh_policy_get)];
	int err;

	err = nlmsg_parse(nlh, sizeof(struct nhmsg), tb,
			  ARRAY_SIZE(rtm_nh_policy_get) - 1,
			  rtm_nh_policy_get, extack);
	if (err < 0)
		return err;

	return __nh_valid_get_del_req(nlh, tb, id, extack);
}

/* rtnl */
static int rtm_del_nexthop(struct sk_buff *skb, struct nlmsghdr *nlh,
			   struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct nl_info nlinfo = {
		.nlh = nlh,
		.nl_net = net,
		.portid = NETLINK_CB(skb).portid,
	};
	struct nexthop *nh;
	int err;
	u32 id;

	err = nh_valid_get_del_req(nlh, &id, extack);
	if (err)
		return err;

	nh = nexthop_find_by_id(net, id);
	if (!nh)
		return -ENOENT;

	remove_nexthop(net, nh, &nlinfo);

	return 0;
}

/* rtnl */
static int rtm_get_nexthop(struct sk_buff *in_skb, struct nlmsghdr *nlh,
			   struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(in_skb->sk);
	struct sk_buff *skb = NULL;
	struct nexthop *nh;
	int err;
	u32 id;

	err = nh_valid_get_del_req(nlh, &id, extack);
	if (err)
		return err;

	err = -ENOBUFS;
	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		goto out;

	err = -ENOENT;
	nh = nexthop_find_by_id(net, id);
	if (!nh)
		goto errout_free;

	err = nh_fill_node(skb, nh, RTM_NEWNEXTHOP, NETLINK_CB(in_skb).portid,
			   nlh->nlmsg_seq, 0);
	if (err < 0) {
		WARN_ON(err == -EMSGSIZE);
		goto errout_free;
	}

	err = rtnl_unicast(skb, net, NETLINK_CB(in_skb).portid);
out:
	return err;
errout_free:
	kfree_skb(skb);
	goto out;
}

struct nh_dump_filter {
	u32 nh_id;
	int dev_idx;
	int master_idx;
	bool group_filter;
	bool fdb_filter;
	u32 res_bucket_nh_id;
};

static bool nh_dump_filtered(struct nexthop *nh,
			     struct nh_dump_filter *filter, u8 family)
{
	const struct net_device *dev;
	const struct nh_info *nhi;

	if (filter->group_filter && !nh->is_group)
		return true;

	if (!filter->dev_idx && !filter->master_idx && !family)
		return false;

	if (nh->is_group)
		return true;

	nhi = rtnl_dereference(nh->nh_info);
	if (family && nhi->family != family)
		return true;

	dev = nhi->fib_nhc.nhc_dev;
	if (filter->dev_idx && (!dev || dev->ifindex != filter->dev_idx))
		return true;

	if (filter->master_idx) {
		struct net_device *master;

		if (!dev)
			return true;

		master = netdev_master_upper_dev_get((struct net_device *)dev);
		if (!master || master->ifindex != filter->master_idx)
			return true;
	}

	return false;
}

static int __nh_valid_dump_req(const struct nlmsghdr *nlh, struct nlattr **tb,
			       struct nh_dump_filter *filter,
			       struct netlink_ext_ack *extack)
{
	struct nhmsg *nhm;
	u32 idx;

	if (tb[NHA_OIF]) {
		idx = nla_get_u32(tb[NHA_OIF]);
		if (idx > INT_MAX) {
			NL_SET_ERR_MSG(extack, "Invalid device index");
			return -EINVAL;
		}
		filter->dev_idx = idx;
	}
	if (tb[NHA_MASTER]) {
		idx = nla_get_u32(tb[NHA_MASTER]);
		if (idx > INT_MAX) {
			NL_SET_ERR_MSG(extack, "Invalid master device index");
			return -EINVAL;
		}
		filter->master_idx = idx;
	}
	filter->group_filter = nla_get_flag(tb[NHA_GROUPS]);
	filter->fdb_filter = nla_get_flag(tb[NHA_FDB]);

	nhm = nlmsg_data(nlh);
	if (nhm->nh_protocol || nhm->resvd || nhm->nh_scope || nhm->nh_flags) {
		NL_SET_ERR_MSG(extack, "Invalid values in header for nexthop dump request");
		return -EINVAL;
	}

	return 0;
}

static int nh_valid_dump_req(const struct nlmsghdr *nlh,
			     struct nh_dump_filter *filter,
			     struct netlink_callback *cb)
{
	struct nlattr *tb[ARRAY_SIZE(rtm_nh_policy_dump)];
	int err;

	err = nlmsg_parse(nlh, sizeof(struct nhmsg), tb,
			  ARRAY_SIZE(rtm_nh_policy_dump) - 1,
			  rtm_nh_policy_dump, cb->extack);
	if (err < 0)
		return err;

	return __nh_valid_dump_req(nlh, tb, filter, cb->extack);
}

struct rtm_dump_nh_ctx {
	u32 idx;
};

static struct rtm_dump_nh_ctx *
rtm_dump_nh_ctx(struct netlink_callback *cb)
{
	struct rtm_dump_nh_ctx *ctx = (void *)cb->ctx;

	BUILD_BUG_ON(sizeof(*ctx) > sizeof(cb->ctx));
	return ctx;
}

static int rtm_dump_walk_nexthops(struct sk_buff *skb,
				  struct netlink_callback *cb,
				  struct rb_root *root,
				  struct rtm_dump_nh_ctx *ctx,
				  int (*nh_cb)(struct sk_buff *skb,
					       struct netlink_callback *cb,
					       struct nexthop *nh, void *data),
				  void *data)
{
	struct rb_node *node;
	int s_idx;
	int err;

	s_idx = ctx->idx;
	for (node = rb_first(root); node; node = rb_next(node)) {
		struct nexthop *nh;

		nh = rb_entry(node, struct nexthop, rb_node);
		if (nh->id < s_idx)
			continue;

		ctx->idx = nh->id;
		err = nh_cb(skb, cb, nh, data);
		if (err)
			return err;
	}

	ctx->idx++;
	return 0;
}

static int rtm_dump_nexthop_cb(struct sk_buff *skb, struct netlink_callback *cb,
			       struct nexthop *nh, void *data)
{
	struct nhmsg *nhm = nlmsg_data(cb->nlh);
	struct nh_dump_filter *filter = data;

	if (nh_dump_filtered(nh, filter, nhm->nh_family))
		return 0;

	return nh_fill_node(skb, nh, RTM_NEWNEXTHOP,
			    NETLINK_CB(cb->skb).portid,
			    cb->nlh->nlmsg_seq, NLM_F_MULTI);
}

/* rtnl */
static int rtm_dump_nexthop(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct rtm_dump_nh_ctx *ctx = rtm_dump_nh_ctx(cb);
	struct net *net = sock_net(skb->sk);
	struct rb_root *root = &net->nexthop.rb_root;
	struct nh_dump_filter filter = {};
	int err;

	err = nh_valid_dump_req(cb->nlh, &filter, cb);
	if (err < 0)
		return err;

	err = rtm_dump_walk_nexthops(skb, cb, root, ctx,
				     &rtm_dump_nexthop_cb, &filter);
	if (err < 0) {
		if (likely(skb->len))
			goto out;
		goto out_err;
	}

out:
	err = skb->len;
out_err:
	cb->seq = net->nexthop.seq;
	nl_dump_check_consistent(cb, nlmsg_hdr(skb));
	return err;
}

static struct nexthop *
nexthop_find_group_resilient(struct net *net, u32 id,
			     struct netlink_ext_ack *extack)
{
	struct nh_group *nhg;
	struct nexthop *nh;

	nh = nexthop_find_by_id(net, id);
	if (!nh)
		return ERR_PTR(-ENOENT);

	if (!nh->is_group) {
		NL_SET_ERR_MSG(extack, "Not a nexthop group");
		return ERR_PTR(-EINVAL);
	}

	nhg = rtnl_dereference(nh->nh_grp);
	if (!nhg->resilient) {
		NL_SET_ERR_MSG(extack, "Nexthop group not of type resilient");
		return ERR_PTR(-EINVAL);
	}

	return nh;
}

static int nh_valid_dump_nhid(struct nlattr *attr, u32 *nh_id_p,
			      struct netlink_ext_ack *extack)
{
	u32 idx;

	if (attr) {
		idx = nla_get_u32(attr);
		if (!idx) {
			NL_SET_ERR_MSG(extack, "Invalid nexthop id");
			return -EINVAL;
		}
		*nh_id_p = idx;
	} else {
		*nh_id_p = 0;
	}

	return 0;
}

static int nh_valid_dump_bucket_req(const struct nlmsghdr *nlh,
				    struct nh_dump_filter *filter,
				    struct netlink_callback *cb)
{
	struct nlattr *res_tb[ARRAY_SIZE(rtm_nh_res_bucket_policy_dump)];
	struct nlattr *tb[ARRAY_SIZE(rtm_nh_policy_dump_bucket)];
	int err;

	err = nlmsg_parse(nlh, sizeof(struct nhmsg), tb,
			  ARRAY_SIZE(rtm_nh_policy_dump_bucket) - 1,
			  rtm_nh_policy_dump_bucket, NULL);
	if (err < 0)
		return err;

	err = nh_valid_dump_nhid(tb[NHA_ID], &filter->nh_id, cb->extack);
	if (err)
		return err;

	if (tb[NHA_RES_BUCKET]) {
		size_t max = ARRAY_SIZE(rtm_nh_res_bucket_policy_dump) - 1;

		err = nla_parse_nested(res_tb, max,
				       tb[NHA_RES_BUCKET],
				       rtm_nh_res_bucket_policy_dump,
				       cb->extack);
		if (err < 0)
			return err;

		err = nh_valid_dump_nhid(res_tb[NHA_RES_BUCKET_NH_ID],
					 &filter->res_bucket_nh_id,
					 cb->extack);
		if (err)
			return err;
	}

	return __nh_valid_dump_req(nlh, tb, filter, cb->extack);
}

struct rtm_dump_res_bucket_ctx {
	struct rtm_dump_nh_ctx nh;
	u16 bucket_index;
	u32 done_nh_idx; /* 1 + the index of the last fully processed NH. */
};

static struct rtm_dump_res_bucket_ctx *
rtm_dump_res_bucket_ctx(struct netlink_callback *cb)
{
	struct rtm_dump_res_bucket_ctx *ctx = (void *)cb->ctx;

	BUILD_BUG_ON(sizeof(*ctx) > sizeof(cb->ctx));
	return ctx;
}

struct rtm_dump_nexthop_bucket_data {
	struct rtm_dump_res_bucket_ctx *ctx;
	struct nh_dump_filter filter;
};

static int rtm_dump_nexthop_bucket_nh(struct sk_buff *skb,
				      struct netlink_callback *cb,
				      struct nexthop *nh,
				      struct rtm_dump_nexthop_bucket_data *dd)
{
	u32 portid = NETLINK_CB(cb->skb).portid;
	struct nhmsg *nhm = nlmsg_data(cb->nlh);
	struct nh_res_table *res_table;
	struct nh_group *nhg;
	u16 bucket_index;
	int err;

	if (dd->ctx->nh.idx < dd->ctx->done_nh_idx)
		return 0;

	nhg = rtnl_dereference(nh->nh_grp);
	res_table = rtnl_dereference(nhg->res_table);
	for (bucket_index = dd->ctx->bucket_index;
	     bucket_index < res_table->num_nh_buckets;
	     bucket_index++) {
		struct nh_res_bucket *bucket;
		struct nh_grp_entry *nhge;

		bucket = &res_table->nh_buckets[bucket_index];
		nhge = rtnl_dereference(bucket->nh_entry);
		if (nh_dump_filtered(nhge->nh, &dd->filter, nhm->nh_family))
			continue;

		if (dd->filter.res_bucket_nh_id &&
		    dd->filter.res_bucket_nh_id != nhge->nh->id)
			continue;

		err = nh_fill_res_bucket(skb, nh, bucket, bucket_index,
					 RTM_NEWNEXTHOPBUCKET, portid,
					 cb->nlh->nlmsg_seq, NLM_F_MULTI,
					 cb->extack);
		if (err < 0) {
			if (likely(skb->len))
				goto out;
			goto out_err;
		}
	}

	dd->ctx->done_nh_idx = dd->ctx->nh.idx + 1;
	bucket_index = 0;

out:
	err = skb->len;
out_err:
	dd->ctx->bucket_index = bucket_index;
	return err;
}

static int rtm_dump_nexthop_bucket_cb(struct sk_buff *skb,
				      struct netlink_callback *cb,
				      struct nexthop *nh, void *data)
{
	struct rtm_dump_nexthop_bucket_data *dd = data;
	struct nh_group *nhg;

	if (!nh->is_group)
		return 0;

	nhg = rtnl_dereference(nh->nh_grp);
	if (!nhg->resilient)
		return 0;

	return rtm_dump_nexthop_bucket_nh(skb, cb, nh, dd);
}

/* rtnl */
static int rtm_dump_nexthop_bucket(struct sk_buff *skb,
				   struct netlink_callback *cb)
{
	struct rtm_dump_res_bucket_ctx *ctx = rtm_dump_res_bucket_ctx(cb);
	struct rtm_dump_nexthop_bucket_data dd = { .ctx = ctx };
	struct net *net = sock_net(skb->sk);
	struct nexthop *nh;
	int err;

	err = nh_valid_dump_bucket_req(cb->nlh, &dd.filter, cb);
	if (err)
		return err;

	if (dd.filter.nh_id) {
		nh = nexthop_find_group_resilient(net, dd.filter.nh_id,
						  cb->extack);
		if (IS_ERR(nh))
			return PTR_ERR(nh);
		err = rtm_dump_nexthop_bucket_nh(skb, cb, nh, &dd);
	} else {
		struct rb_root *root = &net->nexthop.rb_root;

		err = rtm_dump_walk_nexthops(skb, cb, root, &ctx->nh,
					     &rtm_dump_nexthop_bucket_cb, &dd);
	}

	if (err < 0) {
		if (likely(skb->len))
			goto out;
		goto out_err;
	}

out:
	err = skb->len;
out_err:
	cb->seq = net->nexthop.seq;
	nl_dump_check_consistent(cb, nlmsg_hdr(skb));
	return err;
}

static int nh_valid_get_bucket_req_res_bucket(struct nlattr *res,
					      u16 *bucket_index,
					      struct netlink_ext_ack *extack)
{
	struct nlattr *tb[ARRAY_SIZE(rtm_nh_res_bucket_policy_get)];
	int err;

	err = nla_parse_nested(tb, ARRAY_SIZE(rtm_nh_res_bucket_policy_get) - 1,
			       res, rtm_nh_res_bucket_policy_get, extack);
	if (err < 0)
		return err;

	if (!tb[NHA_RES_BUCKET_INDEX]) {
		NL_SET_ERR_MSG(extack, "Bucket index is missing");
		return -EINVAL;
	}

	*bucket_index = nla_get_u16(tb[NHA_RES_BUCKET_INDEX]);
	return 0;
}

static int nh_valid_get_bucket_req(const struct nlmsghdr *nlh,
				   u32 *id, u16 *bucket_index,
				   struct netlink_ext_ack *extack)
{
	struct nlattr *tb[ARRAY_SIZE(rtm_nh_policy_get_bucket)];
	int err;

	err = nlmsg_parse(nlh, sizeof(struct nhmsg), tb,
			  ARRAY_SIZE(rtm_nh_policy_get_bucket) - 1,
			  rtm_nh_policy_get_bucket, extack);
	if (err < 0)
		return err;

	err = __nh_valid_get_del_req(nlh, tb, id, extack);
	if (err)
		return err;

	if (!tb[NHA_RES_BUCKET]) {
		NL_SET_ERR_MSG(extack, "Bucket information is missing");
		return -EINVAL;
	}

	err = nh_valid_get_bucket_req_res_bucket(tb[NHA_RES_BUCKET],
						 bucket_index, extack);
	if (err)
		return err;

	return 0;
}

/* rtnl */
static int rtm_get_nexthop_bucket(struct sk_buff *in_skb, struct nlmsghdr *nlh,
				  struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(in_skb->sk);
	struct nh_res_table *res_table;
	struct sk_buff *skb = NULL;
	struct nh_group *nhg;
	struct nexthop *nh;
	u16 bucket_index;
	int err;
	u32 id;

	err = nh_valid_get_bucket_req(nlh, &id, &bucket_index, extack);
	if (err)
		return err;

	nh = nexthop_find_group_resilient(net, id, extack);
	if (IS_ERR(nh))
		return PTR_ERR(nh);

	nhg = rtnl_dereference(nh->nh_grp);
	res_table = rtnl_dereference(nhg->res_table);
	if (bucket_index >= res_table->num_nh_buckets) {
		NL_SET_ERR_MSG(extack, "Bucket index out of bounds");
		return -ENOENT;
	}

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	err = nh_fill_res_bucket(skb, nh, &res_table->nh_buckets[bucket_index],
				 bucket_index, RTM_NEWNEXTHOPBUCKET,
				 NETLINK_CB(in_skb).portid, nlh->nlmsg_seq,
				 0, extack);
	if (err < 0) {
		WARN_ON(err == -EMSGSIZE);
		goto errout_free;
	}

	return rtnl_unicast(skb, net, NETLINK_CB(in_skb).portid);

errout_free:
	kfree_skb(skb);
	return err;
}

static void nexthop_sync_mtu(struct net_device *dev, u32 orig_mtu)
{
	unsigned int hash = nh_dev_hashfn(dev->ifindex);
	struct net *net = dev_net(dev);
	struct hlist_head *head = &net->nexthop.devhash[hash];
	struct hlist_node *n;
	struct nh_info *nhi;

	hlist_for_each_entry_safe(nhi, n, head, dev_hash) {
		if (nhi->fib_nhc.nhc_dev == dev) {
			if (nhi->family == AF_INET)
				fib_nhc_update_mtu(&nhi->fib_nhc, dev->mtu,
						   orig_mtu);
		}
	}
}

/* rtnl */
static int nh_netdev_event(struct notifier_block *this,
			   unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct netdev_notifier_info_ext *info_ext;

	switch (event) {
	case NETDEV_DOWN:
	case NETDEV_UNREGISTER:
		nexthop_flush_dev(dev, event);
		break;
	case NETDEV_CHANGE:
		if (!(dev_get_flags(dev) & (IFF_RUNNING | IFF_LOWER_UP)))
			nexthop_flush_dev(dev, event);
		break;
	case NETDEV_CHANGEMTU:
		info_ext = ptr;
		nexthop_sync_mtu(dev, info_ext->ext.mtu);
		rt_cache_flush(dev_net(dev));
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block nh_netdev_notifier = {
	.notifier_call = nh_netdev_event,
};

static int nexthops_dump(struct net *net, struct notifier_block *nb,
			 enum nexthop_event_type event_type,
			 struct netlink_ext_ack *extack)
{
	struct rb_root *root = &net->nexthop.rb_root;
	struct rb_node *node;
	int err = 0;

	for (node = rb_first(root); node; node = rb_next(node)) {
		struct nexthop *nh;

		nh = rb_entry(node, struct nexthop, rb_node);
		err = call_nexthop_notifier(nb, net, event_type, nh, extack);
		if (err)
			break;
	}

	return err;
}

int register_nexthop_notifier(struct net *net, struct notifier_block *nb,
			      struct netlink_ext_ack *extack)
{
	int err;

	rtnl_lock();
	err = nexthops_dump(net, nb, NEXTHOP_EVENT_REPLACE, extack);
	if (err)
		goto unlock;
	err = blocking_notifier_chain_register(&net->nexthop.notifier_chain,
					       nb);
unlock:
	rtnl_unlock();
	return err;
}
EXPORT_SYMBOL(register_nexthop_notifier);

int unregister_nexthop_notifier(struct net *net, struct notifier_block *nb)
{
	int err;

	rtnl_lock();
	err = blocking_notifier_chain_unregister(&net->nexthop.notifier_chain,
						 nb);
	if (err)
		goto unlock;
	nexthops_dump(net, nb, NEXTHOP_EVENT_DEL, NULL);
unlock:
	rtnl_unlock();
	return err;
}
EXPORT_SYMBOL(unregister_nexthop_notifier);

void nexthop_set_hw_flags(struct net *net, u32 id, bool offload, bool trap)
{
	struct nexthop *nexthop;

	rcu_read_lock();

	nexthop = nexthop_find_by_id(net, id);
	if (!nexthop)
		goto out;

	nexthop->nh_flags &= ~(RTNH_F_OFFLOAD | RTNH_F_TRAP);
	if (offload)
		nexthop->nh_flags |= RTNH_F_OFFLOAD;
	if (trap)
		nexthop->nh_flags |= RTNH_F_TRAP;

out:
	rcu_read_unlock();
}
EXPORT_SYMBOL(nexthop_set_hw_flags);

void nexthop_bucket_set_hw_flags(struct net *net, u32 id, u16 bucket_index,
				 bool offload, bool trap)
{
	struct nh_res_table *res_table;
	struct nh_res_bucket *bucket;
	struct nexthop *nexthop;
	struct nh_group *nhg;

	rcu_read_lock();

	nexthop = nexthop_find_by_id(net, id);
	if (!nexthop || !nexthop->is_group)
		goto out;

	nhg = rcu_dereference(nexthop->nh_grp);
	if (!nhg->resilient)
		goto out;

	if (bucket_index >= nhg->res_table->num_nh_buckets)
		goto out;

	res_table = rcu_dereference(nhg->res_table);
	bucket = &res_table->nh_buckets[bucket_index];
	bucket->nh_flags &= ~(RTNH_F_OFFLOAD | RTNH_F_TRAP);
	if (offload)
		bucket->nh_flags |= RTNH_F_OFFLOAD;
	if (trap)
		bucket->nh_flags |= RTNH_F_TRAP;

out:
	rcu_read_unlock();
}
EXPORT_SYMBOL(nexthop_bucket_set_hw_flags);

void nexthop_res_grp_activity_update(struct net *net, u32 id, u16 num_buckets,
				     unsigned long *activity)
{
	struct nh_res_table *res_table;
	struct nexthop *nexthop;
	struct nh_group *nhg;
	u16 i;

	rcu_read_lock();

	nexthop = nexthop_find_by_id(net, id);
	if (!nexthop || !nexthop->is_group)
		goto out;

	nhg = rcu_dereference(nexthop->nh_grp);
	if (!nhg->resilient)
		goto out;

	/* Instead of silently ignoring some buckets, demand that the sizes
	 * be the same.
	 */
	res_table = rcu_dereference(nhg->res_table);
	if (num_buckets != res_table->num_nh_buckets)
		goto out;

	for (i = 0; i < num_buckets; i++) {
		if (test_bit(i, activity))
			nh_res_bucket_set_busy(&res_table->nh_buckets[i]);
	}

out:
	rcu_read_unlock();
}
EXPORT_SYMBOL(nexthop_res_grp_activity_update);

static void __net_exit nexthop_net_exit_batch(struct list_head *net_list)
{
	struct net *net;

	rtnl_lock();
	list_for_each_entry(net, net_list, exit_list) {
		flush_all_nexthops(net);
		kfree(net->nexthop.devhash);
	}
	rtnl_unlock();
}

static int __net_init nexthop_net_init(struct net *net)
{
	size_t sz = sizeof(struct hlist_head) * NH_DEV_HASHSIZE;

	net->nexthop.rb_root = RB_ROOT;
	net->nexthop.devhash = kzalloc(sz, GFP_KERNEL);
	if (!net->nexthop.devhash)
		return -ENOMEM;
	BLOCKING_INIT_NOTIFIER_HEAD(&net->nexthop.notifier_chain);

	return 0;
}

static struct pernet_operations nexthop_net_ops = {
	.init = nexthop_net_init,
	.exit_batch = nexthop_net_exit_batch,
};

static int __init nexthop_init(void)
{
	register_pernet_subsys(&nexthop_net_ops);

	register_netdevice_notifier(&nh_netdev_notifier);

	rtnl_register(PF_UNSPEC, RTM_NEWNEXTHOP, rtm_new_nexthop, NULL, 0);
	rtnl_register(PF_UNSPEC, RTM_DELNEXTHOP, rtm_del_nexthop, NULL, 0);
	rtnl_register(PF_UNSPEC, RTM_GETNEXTHOP, rtm_get_nexthop,
		      rtm_dump_nexthop, 0);

	rtnl_register(PF_INET, RTM_NEWNEXTHOP, rtm_new_nexthop, NULL, 0);
	rtnl_register(PF_INET, RTM_GETNEXTHOP, NULL, rtm_dump_nexthop, 0);

	rtnl_register(PF_INET6, RTM_NEWNEXTHOP, rtm_new_nexthop, NULL, 0);
	rtnl_register(PF_INET6, RTM_GETNEXTHOP, NULL, rtm_dump_nexthop, 0);

	rtnl_register(PF_UNSPEC, RTM_GETNEXTHOPBUCKET, rtm_get_nexthop_bucket,
		      rtm_dump_nexthop_bucket, 0);

	return 0;
}
subsys_initcall(nexthop_init);
