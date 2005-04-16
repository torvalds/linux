#ifndef __NET_SCHED_GENERIC_H
#define __NET_SCHED_GENERIC_H

#include <linux/config.h>
#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/rcupdate.h>
#include <linux/module.h>
#include <linux/rtnetlink.h>
#include <linux/pkt_sched.h>
#include <linux/pkt_cls.h>
#include <net/gen_stats.h>

struct Qdisc_ops;
struct qdisc_walker;
struct tcf_walker;
struct module;

struct qdisc_rate_table
{
	struct tc_ratespec rate;
	u32		data[256];
	struct qdisc_rate_table *next;
	int		refcnt;
};

struct Qdisc
{
	int 			(*enqueue)(struct sk_buff *skb, struct Qdisc *dev);
	struct sk_buff *	(*dequeue)(struct Qdisc *dev);
	unsigned		flags;
#define TCQ_F_BUILTIN	1
#define TCQ_F_THROTTLED	2
#define TCQ_F_INGRESS	4
	int			padded;
	struct Qdisc_ops	*ops;
	u32			handle;
	u32			parent;
	atomic_t		refcnt;
	struct sk_buff_head	q;
	struct net_device	*dev;
	struct list_head	list;

	struct gnet_stats_basic	bstats;
	struct gnet_stats_queue	qstats;
	struct gnet_stats_rate_est	rate_est;
	spinlock_t		*stats_lock;
	struct rcu_head 	q_rcu;
	int			(*reshape_fail)(struct sk_buff *skb,
					struct Qdisc *q);

	/* This field is deprecated, but it is still used by CBQ
	 * and it will live until better solution will be invented.
	 */
	struct Qdisc		*__parent;
};

struct Qdisc_class_ops
{
	/* Child qdisc manipulation */
	int			(*graft)(struct Qdisc *, unsigned long cl,
					struct Qdisc *, struct Qdisc **);
	struct Qdisc *		(*leaf)(struct Qdisc *, unsigned long cl);

	/* Class manipulation routines */
	unsigned long		(*get)(struct Qdisc *, u32 classid);
	void			(*put)(struct Qdisc *, unsigned long);
	int			(*change)(struct Qdisc *, u32, u32,
					struct rtattr **, unsigned long *);
	int			(*delete)(struct Qdisc *, unsigned long);
	void			(*walk)(struct Qdisc *, struct qdisc_walker * arg);

	/* Filter manipulation */
	struct tcf_proto **	(*tcf_chain)(struct Qdisc *, unsigned long);
	unsigned long		(*bind_tcf)(struct Qdisc *, unsigned long,
					u32 classid);
	void			(*unbind_tcf)(struct Qdisc *, unsigned long);

	/* rtnetlink specific */
	int			(*dump)(struct Qdisc *, unsigned long,
					struct sk_buff *skb, struct tcmsg*);
	int			(*dump_stats)(struct Qdisc *, unsigned long,
					struct gnet_dump *);
};

struct Qdisc_ops
{
	struct Qdisc_ops	*next;
	struct Qdisc_class_ops	*cl_ops;
	char			id[IFNAMSIZ];
	int			priv_size;

	int 			(*enqueue)(struct sk_buff *, struct Qdisc *);
	struct sk_buff *	(*dequeue)(struct Qdisc *);
	int 			(*requeue)(struct sk_buff *, struct Qdisc *);
	unsigned int		(*drop)(struct Qdisc *);

	int			(*init)(struct Qdisc *, struct rtattr *arg);
	void			(*reset)(struct Qdisc *);
	void			(*destroy)(struct Qdisc *);
	int			(*change)(struct Qdisc *, struct rtattr *arg);

	int			(*dump)(struct Qdisc *, struct sk_buff *);
	int			(*dump_stats)(struct Qdisc *, struct gnet_dump *);

	struct module		*owner;
};


struct tcf_result
{
	unsigned long	class;
	u32		classid;
};

struct tcf_proto_ops
{
	struct tcf_proto_ops	*next;
	char			kind[IFNAMSIZ];

	int			(*classify)(struct sk_buff*, struct tcf_proto*,
					struct tcf_result *);
	int			(*init)(struct tcf_proto*);
	void			(*destroy)(struct tcf_proto*);

	unsigned long		(*get)(struct tcf_proto*, u32 handle);
	void			(*put)(struct tcf_proto*, unsigned long);
	int			(*change)(struct tcf_proto*, unsigned long,
					u32 handle, struct rtattr **,
					unsigned long *);
	int			(*delete)(struct tcf_proto*, unsigned long);
	void			(*walk)(struct tcf_proto*, struct tcf_walker *arg);

	/* rtnetlink specific */
	int			(*dump)(struct tcf_proto*, unsigned long,
					struct sk_buff *skb, struct tcmsg*);

	struct module		*owner;
};

struct tcf_proto
{
	/* Fast access part */
	struct tcf_proto	*next;
	void			*root;
	int			(*classify)(struct sk_buff*, struct tcf_proto*,
					struct tcf_result *);
	u32			protocol;

	/* All the rest */
	u32			prio;
	u32			classid;
	struct Qdisc		*q;
	void			*data;
	struct tcf_proto_ops	*ops;
};


extern void qdisc_lock_tree(struct net_device *dev);
extern void qdisc_unlock_tree(struct net_device *dev);

#define sch_tree_lock(q)	qdisc_lock_tree((q)->dev)
#define sch_tree_unlock(q)	qdisc_unlock_tree((q)->dev)
#define tcf_tree_lock(tp)	qdisc_lock_tree((tp)->q->dev)
#define tcf_tree_unlock(tp)	qdisc_unlock_tree((tp)->q->dev)

static inline void
tcf_destroy(struct tcf_proto *tp)
{
	tp->ops->destroy(tp);
	module_put(tp->ops->owner);
	kfree(tp);
}

#endif
