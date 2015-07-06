#ifndef __NET_ACT_API_H
#define __NET_ACT_API_H

/*
 * Public police action API for classifiers/qdiscs
 */

#include <net/sch_generic.h>
#include <net/pkt_sched.h>

struct tcf_common {
	struct hlist_node		tcfc_head;
	u32				tcfc_index;
	int				tcfc_refcnt;
	int				tcfc_bindcnt;
	u32				tcfc_capab;
	int				tcfc_action;
	struct tcf_t			tcfc_tm;
	struct gnet_stats_basic_packed	tcfc_bstats;
	struct gnet_stats_queue		tcfc_qstats;
	struct gnet_stats_rate_est64	tcfc_rate_est;
	spinlock_t			tcfc_lock;
	struct rcu_head			tcfc_rcu;
	struct gnet_stats_basic_cpu __percpu *cpu_bstats;
	struct gnet_stats_queue __percpu *cpu_qstats;
};
#define tcf_head	common.tcfc_head
#define tcf_index	common.tcfc_index
#define tcf_refcnt	common.tcfc_refcnt
#define tcf_bindcnt	common.tcfc_bindcnt
#define tcf_capab	common.tcfc_capab
#define tcf_action	common.tcfc_action
#define tcf_tm		common.tcfc_tm
#define tcf_bstats	common.tcfc_bstats
#define tcf_qstats	common.tcfc_qstats
#define tcf_rate_est	common.tcfc_rate_est
#define tcf_lock	common.tcfc_lock
#define tcf_rcu		common.tcfc_rcu

struct tcf_hashinfo {
	struct hlist_head	*htab;
	unsigned int		hmask;
	spinlock_t		lock;
	u32			index;
};

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

static inline void tcf_hashinfo_destroy(struct tcf_hashinfo *hf)
{
	kfree(hf->htab);
}

/* Update lastuse only if needed, to avoid dirtying a cache line.
 * We use a temp variable to avoid fetching jiffies twice.
 */
static inline void tcf_lastuse_update(struct tcf_t *tm)
{
	unsigned long now = jiffies;

	if (tm->lastuse != now)
		tm->lastuse = now;
}

#ifdef CONFIG_NET_CLS_ACT

#define ACT_P_CREATED 1
#define ACT_P_DELETED 1

struct tc_action {
	void			*priv;
	const struct tc_action_ops	*ops;
	__u32			type; /* for backward compat(TCA_OLD_COMPAT) */
	__u32			order;
	struct list_head	list;
};

struct tc_action_ops {
	struct list_head head;
	struct tcf_hashinfo *hinfo;
	char    kind[IFNAMSIZ];
	__u32   type; /* TBD to match kind */
	struct module		*owner;
	int     (*act)(struct sk_buff *, const struct tc_action *, struct tcf_result *);
	int     (*dump)(struct sk_buff *, struct tc_action *, int, int);
	void	(*cleanup)(struct tc_action *, int bind);
	int     (*lookup)(struct tc_action *, u32);
	int     (*init)(struct net *net, struct nlattr *nla,
			struct nlattr *est, struct tc_action *act, int ovr,
			int bind);
	int     (*walk)(struct sk_buff *, struct netlink_callback *, int, struct tc_action *);
};

int tcf_hash_search(struct tc_action *a, u32 index);
void tcf_hash_destroy(struct tc_action *a);
int tcf_hash_release(struct tc_action *a, int bind);
u32 tcf_hash_new_index(struct tcf_hashinfo *hinfo);
int tcf_hash_check(u32 index, struct tc_action *a, int bind);
int tcf_hash_create(u32 index, struct nlattr *est, struct tc_action *a,
		    int size, int bind, bool cpustats);
void tcf_hash_cleanup(struct tc_action *a, struct nlattr *est);
void tcf_hash_insert(struct tc_action *a);

int tcf_register_action(struct tc_action_ops *a, unsigned int mask);
int tcf_unregister_action(struct tc_action_ops *a);
int tcf_action_destroy(struct list_head *actions, int bind);
int tcf_action_exec(struct sk_buff *skb, const struct list_head *actions,
		    struct tcf_result *res);
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
#endif
