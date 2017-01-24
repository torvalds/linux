#ifndef __NET_ACT_API_H
#define __NET_ACT_API_H

/*
 * Public action API for classifiers/qdiscs
*/

#include <net/sch_generic.h>
#include <net/pkt_sched.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>


struct tcf_hashinfo {
	struct hlist_head	*htab;
	unsigned int		hmask;
	spinlock_t		lock;
	u32			index;
};

struct tc_action_ops;

struct tc_action {
	const struct tc_action_ops	*ops;
	__u32				type; /* for backward compat(TCA_OLD_COMPAT) */
	__u32				order;
	struct list_head		list;
	struct tcf_hashinfo		*hinfo;

	struct hlist_node		tcfa_head;
	u32				tcfa_index;
	int				tcfa_refcnt;
	int				tcfa_bindcnt;
	u32				tcfa_capab;
	int				tcfa_action;
	struct tcf_t			tcfa_tm;
	struct gnet_stats_basic_packed	tcfa_bstats;
	struct gnet_stats_queue		tcfa_qstats;
	struct net_rate_estimator __rcu *tcfa_rate_est;
	spinlock_t			tcfa_lock;
	struct rcu_head			tcfa_rcu;
	struct gnet_stats_basic_cpu __percpu *cpu_bstats;
	struct gnet_stats_queue __percpu *cpu_qstats;
	struct tc_cookie	*act_cookie;
};
#define tcf_head	common.tcfa_head
#define tcf_index	common.tcfa_index
#define tcf_refcnt	common.tcfa_refcnt
#define tcf_bindcnt	common.tcfa_bindcnt
#define tcf_capab	common.tcfa_capab
#define tcf_action	common.tcfa_action
#define tcf_tm		common.tcfa_tm
#define tcf_bstats	common.tcfa_bstats
#define tcf_qstats	common.tcfa_qstats
#define tcf_rate_est	common.tcfa_rate_est
#define tcf_lock	common.tcfa_lock
#define tcf_rcu		common.tcfa_rcu

static inline unsigned int tcf_hash(u32 index, unsigned int hmask)
{
	return index & hmask;
}

static inline int tcf_hashinfo_init(struct tcf_hashinfo *hf, unsigned int mask)
{
	int i;

	spin_lock_init(&hf->lock);
	hf->index = 0;
	hf->hmask = mask;
	hf->htab = kzalloc((mask + 1) * sizeof(struct hlist_head),
			   GFP_KERNEL);
	if (!hf->htab)
		return -ENOMEM;
	for (i = 0; i < mask + 1; i++)
		INIT_HLIST_HEAD(&hf->htab[i]);
	return 0;
}

/* Update lastuse only if needed, to avoid dirtying a cache line.
 * We use a temp variable to avoid fetching jiffies twice.
 */
static inline void tcf_lastuse_update(struct tcf_t *tm)
{
	unsigned long now = jiffies;

	if (tm->lastuse != now)
		tm->lastuse = now;
	if (unlikely(!tm->firstuse))
		tm->firstuse = now;
}

static inline void tcf_tm_dump(struct tcf_t *dtm, const struct tcf_t *stm)
{
	dtm->install = jiffies_to_clock_t(jiffies - stm->install);
	dtm->lastuse = jiffies_to_clock_t(jiffies - stm->lastuse);
	dtm->firstuse = jiffies_to_clock_t(jiffies - stm->firstuse);
	dtm->expires = jiffies_to_clock_t(stm->expires);
}

#ifdef CONFIG_NET_CLS_ACT

#define ACT_P_CREATED 1
#define ACT_P_DELETED 1

struct tc_action_ops {
	struct list_head head;
	char    kind[IFNAMSIZ];
	__u32   type; /* TBD to match kind */
	size_t	size;
	struct module		*owner;
	int     (*act)(struct sk_buff *, const struct tc_action *,
		       struct tcf_result *);
	int     (*dump)(struct sk_buff *, struct tc_action *, int, int);
	void	(*cleanup)(struct tc_action *, int bind);
	int     (*lookup)(struct net *, struct tc_action **, u32);
	int     (*init)(struct net *net, struct nlattr *nla,
			struct nlattr *est, struct tc_action **act, int ovr,
			int bind);
	int     (*walk)(struct net *, struct sk_buff *,
			struct netlink_callback *, int, const struct tc_action_ops *);
	void	(*stats_update)(struct tc_action *, u64, u32, u64);
	int	(*get_dev)(const struct tc_action *a, struct net *net,
			   struct net_device **mirred_dev);
};

struct tc_action_net {
	struct tcf_hashinfo *hinfo;
	const struct tc_action_ops *ops;
};

static inline
int tc_action_net_init(struct tc_action_net *tn,
		       const struct tc_action_ops *ops, unsigned int mask)
{
	int err = 0;

	tn->hinfo = kmalloc(sizeof(*tn->hinfo), GFP_KERNEL);
	if (!tn->hinfo)
		return -ENOMEM;
	tn->ops = ops;
	err = tcf_hashinfo_init(tn->hinfo, mask);
	if (err)
		kfree(tn->hinfo);
	return err;
}

void tcf_hashinfo_destroy(const struct tc_action_ops *ops,
			  struct tcf_hashinfo *hinfo);

static inline void tc_action_net_exit(struct tc_action_net *tn)
{
	tcf_hashinfo_destroy(tn->ops, tn->hinfo);
	kfree(tn->hinfo);
}

int tcf_generic_walker(struct tc_action_net *tn, struct sk_buff *skb,
		       struct netlink_callback *cb, int type,
		       const struct tc_action_ops *ops);
int tcf_hash_search(struct tc_action_net *tn, struct tc_action **a, u32 index);
u32 tcf_hash_new_index(struct tc_action_net *tn);
bool tcf_hash_check(struct tc_action_net *tn, u32 index, struct tc_action **a,
		    int bind);
int tcf_hash_create(struct tc_action_net *tn, u32 index, struct nlattr *est,
		    struct tc_action **a, const struct tc_action_ops *ops, int bind,
		    bool cpustats);
void tcf_hash_cleanup(struct tc_action *a, struct nlattr *est);
void tcf_hash_insert(struct tc_action_net *tn, struct tc_action *a);

int __tcf_hash_release(struct tc_action *a, bool bind, bool strict);

static inline int tcf_hash_release(struct tc_action *a, bool bind)
{
	return __tcf_hash_release(a, bind, false);
}

int tcf_register_action(struct tc_action_ops *a, struct pernet_operations *ops);
int tcf_unregister_action(struct tc_action_ops *a,
			  struct pernet_operations *ops);
int tcf_action_destroy(struct list_head *actions, int bind);
int tcf_action_exec(struct sk_buff *skb, struct tc_action **actions,
		    int nr_actions, struct tcf_result *res);
int tcf_action_init(struct net *net, struct nlattr *nla,
				  struct nlattr *est, char *n, int ovr,
				  int bind, struct list_head *);
struct tc_action *tcf_action_init_1(struct net *net, struct nlattr *nla,
				    struct nlattr *est, char *n, int ovr,
				    int bind);
int tcf_action_dump(struct sk_buff *skb, struct list_head *, int, int);
int tcf_action_dump_old(struct sk_buff *skb, struct tc_action *a, int, int);
int tcf_action_dump_1(struct sk_buff *skb, struct tc_action *a, int, int);
int tcf_action_copy_stats(struct sk_buff *, struct tc_action *, int);

#endif /* CONFIG_NET_CLS_ACT */

static inline void tcf_action_stats_update(struct tc_action *a, u64 bytes,
					   u64 packets, u64 lastuse)
{
#ifdef CONFIG_NET_CLS_ACT
	if (!a->ops->stats_update)
		return;

	a->ops->stats_update(a, bytes, packets, lastuse);
#endif
}

#endif
