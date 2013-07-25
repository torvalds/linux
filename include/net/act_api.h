#ifndef __NET_ACT_API_H
#define __NET_ACT_API_H

/*
 * Public police action API for classifiers/qdiscs
 */

#include <net/sch_generic.h>
#include <net/pkt_sched.h>

struct tcf_common {
	struct tcf_common		*tcfc_next;
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
};
#define tcf_next	common.tcfc_next
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
	struct tcf_common	**htab;
	unsigned int		hmask;
	rwlock_t		*lock;
};

static inline unsigned int tcf_hash(u32 index, unsigned int hmask)
{
	return index & hmask;
}

#ifdef CONFIG_NET_CLS_ACT

#define ACT_P_CREATED 1
#define ACT_P_DELETED 1

struct tcf_act_hdr {
	struct tcf_common	common;
};

struct tc_action {
	void			*priv;
	const struct tc_action_ops	*ops;
	__u32			type; /* for backward compat(TCA_OLD_COMPAT) */
	__u32			order;
	struct tc_action	*next;
};

#define TCA_CAP_NONE 0
struct tc_action_ops {
	struct tc_action_ops *next;
	struct tcf_hashinfo *hinfo;
	char    kind[IFNAMSIZ];
	__u32   type; /* TBD to match kind */
	__u32 	capab;  /* capabilities includes 4 bit version */
	struct module		*owner;
	int     (*act)(struct sk_buff *, const struct tc_action *, struct tcf_result *);
	int     (*get_stats)(struct sk_buff *, struct tc_action *);
	int     (*dump)(struct sk_buff *, struct tc_action *, int, int);
	int     (*cleanup)(struct tc_action *, int bind);
	int     (*lookup)(struct tc_action *, u32);
	int     (*init)(struct net *net, struct nlattr *nla,
			struct nlattr *est, struct tc_action *act, int ovr,
			int bind);
	int     (*walk)(struct sk_buff *, struct netlink_callback *, int, struct tc_action *);
};

extern struct tcf_common *tcf_hash_lookup(u32 index,
					  struct tcf_hashinfo *hinfo);
extern void tcf_hash_destroy(struct tcf_common *p, struct tcf_hashinfo *hinfo);
extern int tcf_hash_release(struct tcf_common *p, int bind,
			    struct tcf_hashinfo *hinfo);
extern int tcf_generic_walker(struct sk_buff *skb, struct netlink_callback *cb,
			      int type, struct tc_action *a);
extern u32 tcf_hash_new_index(u32 *idx_gen, struct tcf_hashinfo *hinfo);
extern int tcf_hash_search(struct tc_action *a, u32 index);
extern struct tcf_common *tcf_hash_check(u32 index, struct tc_action *a,
					 int bind, struct tcf_hashinfo *hinfo);
extern struct tcf_common *tcf_hash_create(u32 index, struct nlattr *est,
					  struct tc_action *a, int size,
					  int bind, u32 *idx_gen,
					  struct tcf_hashinfo *hinfo);
extern void tcf_hash_insert(struct tcf_common *p, struct tcf_hashinfo *hinfo);

extern int tcf_register_action(struct tc_action_ops *a);
extern int tcf_unregister_action(struct tc_action_ops *a);
extern void tcf_action_destroy(struct tc_action *a, int bind);
extern int tcf_action_exec(struct sk_buff *skb, const struct tc_action *a, struct tcf_result *res);
extern struct tc_action *tcf_action_init(struct net *net, struct nlattr *nla,
					 struct nlattr *est, char *n, int ovr,
					 int bind);
extern struct tc_action *tcf_action_init_1(struct net *net, struct nlattr *nla,
					   struct nlattr *est, char *n, int ovr,
					   int bind);
extern int tcf_action_dump(struct sk_buff *skb, struct tc_action *a, int, int);
extern int tcf_action_dump_old(struct sk_buff *skb, struct tc_action *a, int, int);
extern int tcf_action_dump_1(struct sk_buff *skb, struct tc_action *a, int, int);
extern int tcf_action_copy_stats (struct sk_buff *,struct tc_action *, int);
#endif /* CONFIG_NET_CLS_ACT */
#endif
