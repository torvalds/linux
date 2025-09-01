/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_ACT_API_H
#define __NET_ACT_API_H

/*
 * Public action API for classifiers/qdiscs
*/

#include <linux/refcount.h>
#include <net/flow_offload.h>
#include <net/sch_generic.h>
#include <net/pkt_sched.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>

struct tcf_idrinfo {
	struct mutex	lock;
	struct idr	action_idr;
	struct net	*net;
};

struct tc_action_ops;

struct tc_action {
	const struct tc_action_ops	*ops;
	__u32				type; /* for backward compat(TCA_OLD_COMPAT) */
	struct tcf_idrinfo		*idrinfo;

	u32				tcfa_index;
	refcount_t			tcfa_refcnt;
	atomic_t			tcfa_bindcnt;
	int				tcfa_action;
	struct tcf_t			tcfa_tm;
	struct gnet_stats_basic_sync	tcfa_bstats;
	struct gnet_stats_basic_sync	tcfa_bstats_hw;

	atomic_t			tcfa_drops;
	atomic_t			tcfa_overlimits;

	struct net_rate_estimator __rcu *tcfa_rate_est;
	spinlock_t			tcfa_lock;
	struct gnet_stats_basic_sync __percpu *cpu_bstats;
	struct gnet_stats_basic_sync __percpu *cpu_bstats_hw;
	struct gnet_stats_queue __percpu *cpu_qstats;
	struct tc_cookie	__rcu *user_cookie;
	struct tcf_chain	__rcu *goto_chain;
	u32			tcfa_flags;
	u8			hw_stats;
	u8			used_hw_stats;
	bool			used_hw_stats_valid;
	u32			in_hw_count;
};
#define tcf_index	common.tcfa_index
#define tcf_refcnt	common.tcfa_refcnt
#define tcf_bindcnt	common.tcfa_bindcnt
#define tcf_action	common.tcfa_action
#define tcf_tm		common.tcfa_tm
#define tcf_bstats	common.tcfa_bstats
#define tcf_rate_est	common.tcfa_rate_est
#define tcf_lock	common.tcfa_lock

#define TCA_ACT_HW_STATS_ANY (TCA_ACT_HW_STATS_IMMEDIATE | \
			      TCA_ACT_HW_STATS_DELAYED)

/* Reserve 16 bits for user-space. See TCA_ACT_FLAGS_NO_PERCPU_STATS. */
#define TCA_ACT_FLAGS_USER_BITS 16
#define TCA_ACT_FLAGS_USER_MASK 0xffff
#define TCA_ACT_FLAGS_POLICE	(1U << TCA_ACT_FLAGS_USER_BITS)
#define TCA_ACT_FLAGS_BIND	(1U << (TCA_ACT_FLAGS_USER_BITS + 1))
#define TCA_ACT_FLAGS_REPLACE	(1U << (TCA_ACT_FLAGS_USER_BITS + 2))
#define TCA_ACT_FLAGS_NO_RTNL	(1U << (TCA_ACT_FLAGS_USER_BITS + 3))
#define TCA_ACT_FLAGS_AT_INGRESS	(1U << (TCA_ACT_FLAGS_USER_BITS + 4))

/* Update lastuse only if needed, to avoid dirtying a cache line.
 * We use a temp variable to avoid fetching jiffies twice.
 */
static inline void tcf_lastuse_update(struct tcf_t *tm)
{
	unsigned long now = jiffies;

	if (READ_ONCE(tm->lastuse) != now)
		WRITE_ONCE(tm->lastuse, now);
	if (unlikely(!READ_ONCE(tm->firstuse)))
		WRITE_ONCE(tm->firstuse, now);
}

static inline void tcf_tm_dump(struct tcf_t *dtm, const struct tcf_t *stm)
{
	unsigned long firstuse, now = jiffies;

	dtm->install = jiffies_to_clock_t(now - READ_ONCE(stm->install));
	dtm->lastuse = jiffies_to_clock_t(now - READ_ONCE(stm->lastuse));

	firstuse = READ_ONCE(stm->firstuse);
	dtm->firstuse = firstuse ?
		jiffies_to_clock_t(now - firstuse) : 0;

	dtm->expires = jiffies_to_clock_t(READ_ONCE(stm->expires));
}

static inline enum flow_action_hw_stats tc_act_hw_stats(u8 hw_stats)
{
	if (WARN_ON_ONCE(hw_stats > TCA_ACT_HW_STATS_ANY))
		return FLOW_ACTION_HW_STATS_DONT_CARE;
	else if (!hw_stats)
		return FLOW_ACTION_HW_STATS_DISABLED;

	return hw_stats;
}

typedef void (*tc_action_priv_destructor)(void *priv);

struct tc_action_ops {
	struct list_head head;
	char    kind[IFNAMSIZ];
	enum tca_id  id; /* identifier should match kind */
	unsigned int	net_id;
	size_t	size;
	struct module		*owner;
	int     (*act)(struct sk_buff *, const struct tc_action *,
		       struct tcf_result *); /* called under RCU BH lock*/
	int     (*dump)(struct sk_buff *, struct tc_action *, int, int);
	void	(*cleanup)(struct tc_action *);
	int     (*lookup)(struct net *net, struct tc_action **a, u32 index);
	int     (*init)(struct net *net, struct nlattr *nla,
			struct nlattr *est, struct tc_action **act,
			struct tcf_proto *tp,
			u32 flags, struct netlink_ext_ack *extack);
	int     (*walk)(struct net *, struct sk_buff *,
			struct netlink_callback *, int,
			const struct tc_action_ops *,
			struct netlink_ext_ack *);
	void	(*stats_update)(struct tc_action *, u64, u64, u64, u64, bool);
	size_t  (*get_fill_size)(const struct tc_action *act);
	struct net_device *(*get_dev)(const struct tc_action *a,
				      tc_action_priv_destructor *destructor);
	struct psample_group *
	(*get_psample_group)(const struct tc_action *a,
			     tc_action_priv_destructor *destructor);
	int     (*offload_act_setup)(struct tc_action *act, void *entry_data,
				     u32 *index_inc, bool bind,
				     struct netlink_ext_ack *extack);
};

#ifdef CONFIG_NET_CLS_ACT

#define ACT_P_BOUND 0
#define ACT_P_CREATED 1
#define ACT_P_DELETED 1

struct tc_action_net {
	struct tcf_idrinfo *idrinfo;
	const struct tc_action_ops *ops;
};

static inline
int tc_action_net_init(struct net *net, struct tc_action_net *tn,
		       const struct tc_action_ops *ops)
{
	int err = 0;

	tn->idrinfo = kmalloc(sizeof(*tn->idrinfo), GFP_KERNEL);
	if (!tn->idrinfo)
		return -ENOMEM;
	tn->ops = ops;
	tn->idrinfo->net = net;
	mutex_init(&tn->idrinfo->lock);
	idr_init(&tn->idrinfo->action_idr);
	return err;
}

void tcf_idrinfo_destroy(const struct tc_action_ops *ops,
			 struct tcf_idrinfo *idrinfo);

static inline void tc_action_net_exit(struct list_head *net_list,
				      unsigned int id)
{
	struct net *net;

	list_for_each_entry(net, net_list, exit_list) {
		struct tc_action_net *tn = net_generic(net, id);

		tcf_idrinfo_destroy(tn->ops, tn->idrinfo);
		kfree(tn->idrinfo);
	}
}

int tcf_generic_walker(struct tc_action_net *tn, struct sk_buff *skb,
		       struct netlink_callback *cb, int type,
		       const struct tc_action_ops *ops,
		       struct netlink_ext_ack *extack);
int tcf_idr_search(struct tc_action_net *tn, struct tc_action **a, u32 index);
int tcf_idr_create(struct tc_action_net *tn, u32 index, struct nlattr *est,
		   struct tc_action **a, const struct tc_action_ops *ops,
		   int bind, bool cpustats, u32 flags);
int tcf_idr_create_from_flags(struct tc_action_net *tn, u32 index,
			      struct nlattr *est, struct tc_action **a,
			      const struct tc_action_ops *ops, int bind,
			      u32 flags);
void tcf_idr_insert_many(struct tc_action *actions[], int init_res[]);
void tcf_idr_cleanup(struct tc_action_net *tn, u32 index);
int tcf_idr_check_alloc(struct tc_action_net *tn, u32 *index,
			struct tc_action **a, int bind);
int tcf_idr_release(struct tc_action *a, bool bind);

int tcf_register_action(struct tc_action_ops *a, struct pernet_operations *ops);
int tcf_unregister_action(struct tc_action_ops *a,
			  struct pernet_operations *ops);
#define NET_ACT_ALIAS_PREFIX "net-act-"
#define MODULE_ALIAS_NET_ACT(kind)	MODULE_ALIAS(NET_ACT_ALIAS_PREFIX kind)
int tcf_action_destroy(struct tc_action *actions[], int bind);
int tcf_action_exec(struct sk_buff *skb, struct tc_action **actions,
		    int nr_actions, struct tcf_result *res);
int tcf_action_init(struct net *net, struct tcf_proto *tp, struct nlattr *nla,
		    struct nlattr *est,
		    struct tc_action *actions[], int init_res[], size_t *attr_size,
		    u32 flags, u32 fl_flags, struct netlink_ext_ack *extack);
struct tc_action_ops *tc_action_load_ops(struct nlattr *nla, u32 flags,
					 struct netlink_ext_ack *extack);
struct tc_action *tcf_action_init_1(struct net *net, struct tcf_proto *tp,
				    struct nlattr *nla, struct nlattr *est,
				    struct tc_action_ops *a_o, int *init_res,
				    u32 flags, struct netlink_ext_ack *extack);
int tcf_action_dump(struct sk_buff *skb, struct tc_action *actions[], int bind,
		    int ref, bool terse);
int tcf_action_dump_old(struct sk_buff *skb, struct tc_action *a, int, int);

static inline void tcf_action_update_bstats(struct tc_action *a,
					    struct sk_buff *skb)
{
	if (likely(a->cpu_bstats)) {
		bstats_update(this_cpu_ptr(a->cpu_bstats), skb);
		return;
	}
	spin_lock(&a->tcfa_lock);
	bstats_update(&a->tcfa_bstats, skb);
	spin_unlock(&a->tcfa_lock);
}

static inline void tcf_action_inc_drop_qstats(struct tc_action *a)
{
	if (likely(a->cpu_qstats)) {
		qstats_drop_inc(this_cpu_ptr(a->cpu_qstats));
		return;
	}
	atomic_inc(&a->tcfa_drops);
}

static inline void tcf_action_inc_overlimit_qstats(struct tc_action *a)
{
	if (likely(a->cpu_qstats)) {
		qstats_overlimit_inc(this_cpu_ptr(a->cpu_qstats));
		return;
	}
	atomic_inc(&a->tcfa_overlimits);
}

void tcf_action_update_stats(struct tc_action *a, u64 bytes, u64 packets,
			     u64 drops, bool hw);
int tcf_action_copy_stats(struct sk_buff *, struct tc_action *, int);

int tcf_action_update_hw_stats(struct tc_action *action);
int tcf_action_reoffload_cb(flow_indr_block_bind_cb_t *cb,
			    void *cb_priv, bool add);
int tcf_action_check_ctrlact(int action, struct tcf_proto *tp,
			     struct tcf_chain **handle,
			     struct netlink_ext_ack *newchain);
struct tcf_chain *tcf_action_set_ctrlact(struct tc_action *a, int action,
					 struct tcf_chain *newchain);

#ifdef CONFIG_INET
DECLARE_STATIC_KEY_FALSE(tcf_frag_xmit_count);
#endif

int tcf_dev_queue_xmit(struct sk_buff *skb, int (*xmit)(struct sk_buff *skb));

#else /* !CONFIG_NET_CLS_ACT */

static inline int tcf_action_reoffload_cb(flow_indr_block_bind_cb_t *cb,
					  void *cb_priv, bool add) {
	return 0;
}

#endif /* CONFIG_NET_CLS_ACT */

static inline void tcf_action_stats_update(struct tc_action *a, u64 bytes,
					   u64 packets, u64 drops,
					   u64 lastuse, bool hw)
{
#ifdef CONFIG_NET_CLS_ACT
	if (!a->ops->stats_update)
		return;

	a->ops->stats_update(a, bytes, packets, drops, lastuse, hw);
#endif
}


#endif
