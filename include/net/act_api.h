#ifndef __NET_ACT_API_H
#define __NET_ACT_API_H

/*
 * Public police action API for classifiers/qdiscs
 */

#include <net/sch_generic.h>
#include <net/pkt_sched.h>

#define tca_gen(name) \
struct tcf_##name *next; \
	u32 index; \
	int refcnt; \
	int bindcnt; \
	u32 capab; \
	int action; \
	struct tcf_t tm; \
	struct gnet_stats_basic bstats; \
	struct gnet_stats_queue qstats; \
	struct gnet_stats_rate_est rate_est; \
	spinlock_t *stats_lock; \
	spinlock_t lock

struct tcf_police
{
	tca_gen(police);
	int		result;
	u32		ewma_rate;
	u32		burst;
	u32		mtu;
	u32		toks;
	u32		ptoks;
	psched_time_t	t_c;
	struct qdisc_rate_table *R_tab;
	struct qdisc_rate_table *P_tab;
};

#ifdef CONFIG_NET_CLS_ACT

#define ACT_P_CREATED 1
#define ACT_P_DELETED 1

struct tcf_act_hdr
{
	tca_gen(act_hdr);
};

struct tc_action
{
	void *priv;
	struct tc_action_ops *ops;
	__u32   type;   /* for backward compat(TCA_OLD_COMPAT) */
	__u32   order; 
	struct tc_action *next;
};

#define TCA_CAP_NONE 0
struct tc_action_ops
{
	struct tc_action_ops *next;
	char    kind[IFNAMSIZ];
	__u32   type; /* TBD to match kind */
	__u32 	capab;  /* capabilities includes 4 bit version */
	struct module		*owner;
	int     (*act)(struct sk_buff *, struct tc_action *, struct tcf_result *);
	int     (*get_stats)(struct sk_buff *, struct tc_action *);
	int     (*dump)(struct sk_buff *, struct tc_action *,int , int);
	int     (*cleanup)(struct tc_action *, int bind);
	int     (*lookup)(struct tc_action *, u32 );
	int     (*init)(struct rtattr *,struct rtattr *,struct tc_action *, int , int );
	int     (*walk)(struct sk_buff *, struct netlink_callback *, int , struct tc_action *);
};

extern int tcf_register_action(struct tc_action_ops *a);
extern int tcf_unregister_action(struct tc_action_ops *a);
extern void tcf_action_destroy(struct tc_action *a, int bind);
extern int tcf_action_exec(struct sk_buff *skb, struct tc_action *a, struct tcf_result *res);
extern struct tc_action *tcf_action_init(struct rtattr *rta, struct rtattr *est, char *n, int ovr, int bind, int *err);
extern struct tc_action *tcf_action_init_1(struct rtattr *rta, struct rtattr *est, char *n, int ovr, int bind, int *err);
extern int tcf_action_dump(struct sk_buff *skb, struct tc_action *a, int, int);
extern int tcf_action_dump_old(struct sk_buff *skb, struct tc_action *a, int, int);
extern int tcf_action_dump_1(struct sk_buff *skb, struct tc_action *a, int, int);
extern int tcf_action_copy_stats (struct sk_buff *,struct tc_action *, int);
#endif /* CONFIG_NET_CLS_ACT */

extern int tcf_police(struct sk_buff *skb, struct tcf_police *p);
extern void tcf_police_destroy(struct tcf_police *p);
extern struct tcf_police * tcf_police_locate(struct rtattr *rta, struct rtattr *est);
extern int tcf_police_dump(struct sk_buff *skb, struct tcf_police *p);
extern int tcf_police_dump_stats(struct sk_buff *skb, struct tcf_police *p);

static inline int
tcf_police_release(struct tcf_police *p, int bind)
{
	int ret = 0;
#ifdef CONFIG_NET_CLS_ACT
	if (p) {
		if (bind) {
			 p->bindcnt--;
		}
		p->refcnt--;
		if (p->refcnt <= 0 && !p->bindcnt) {
			tcf_police_destroy(p);
			ret = 1;
		}
	}
#else
	if (p && --p->refcnt == 0)
		tcf_police_destroy(p);

#endif /* CONFIG_NET_CLS_ACT */
	return ret;
}

#endif
