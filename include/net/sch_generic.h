#ifndef __NET_SCHED_GENERIC_H
#define __NET_SCHED_GENERIC_H

#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/rcupdate.h>
#include <linux/pkt_sched.h>
#include <linux/pkt_cls.h>
#include <linux/percpu.h>
#include <linux/dynamic_queue_limits.h>
#include <net/gen_stats.h>
#include <net/rtnetlink.h>

struct Qdisc_ops;
struct qdisc_walker;
struct tcf_walker;
struct module;

struct qdisc_rate_table {
	struct tc_ratespec rate;
	u32		data[256];
	struct qdisc_rate_table *next;
	int		refcnt;
};

enum qdisc_state_t {
	__QDISC_STATE_SCHED,
	__QDISC_STATE_DEACTIVATED,
	__QDISC_STATE_THROTTLED,
};

/*
 * following bits are only changed while qdisc lock is held
 */
enum qdisc___state_t {
	__QDISC___STATE_RUNNING = 1,
};

struct qdisc_size_table {
	struct rcu_head		rcu;
	struct list_head	list;
	struct tc_sizespec	szopts;
	int			refcnt;
	u16			data[];
};

struct Qdisc {
	int 			(*enqueue)(struct sk_buff *skb, struct Qdisc *dev);
	struct sk_buff *	(*dequeue)(struct Qdisc *dev);
	unsigned int		flags;
#define TCQ_F_BUILTIN		1
#define TCQ_F_INGRESS		2
#define TCQ_F_CAN_BYPASS	4
#define TCQ_F_MQROOT		8
#define TCQ_F_ONETXQUEUE	0x10 /* dequeue_skb() can assume all skbs are for
				      * q->dev_queue : It can test
				      * netif_xmit_frozen_or_stopped() before
				      * dequeueing next packet.
				      * Its true for MQ/MQPRIO slaves, or non
				      * multiqueue device.
				      */
#define TCQ_F_WARN_NONWC	(1 << 16)
#define TCQ_F_CPUSTATS		0x20 /* run using percpu statistics */
#define TCQ_F_NOPARENT		0x40 /* root of its hierarchy :
				      * qdisc_tree_decrease_qlen() should stop.
				      */
	u32			limit;
	const struct Qdisc_ops	*ops;
	struct qdisc_size_table	__rcu *stab;
	struct list_head	list;
	u32			handle;
	u32			parent;
	int			(*reshape_fail)(struct sk_buff *skb,
					struct Qdisc *q);

	void			*u32_node;

	/* This field is deprecated, but it is still used by CBQ
	 * and it will live until better solution will be invented.
	 */
	struct Qdisc		*__parent;
	struct netdev_queue	*dev_queue;

	struct gnet_stats_rate_est64	rate_est;
	struct gnet_stats_basic_cpu __percpu *cpu_bstats;
	struct gnet_stats_queue	__percpu *cpu_qstats;

	struct Qdisc		*next_sched;
	struct sk_buff		*gso_skb;
	/*
	 * For performance sake on SMP, we put highly modified fields at the end
	 */
	unsigned long		state;
	struct sk_buff_head	q;
	struct gnet_stats_basic_packed bstats;
	unsigned int		__state;
	struct gnet_stats_queue	qstats;
	struct rcu_head		rcu_head;
	int			padded;
	atomic_t		refcnt;

	spinlock_t		busylock ____cacheline_aligned_in_smp;
};

static inline bool qdisc_is_running(const struct Qdisc *qdisc)
{
	return (qdisc->__state & __QDISC___STATE_RUNNING) ? true : false;
}

static inline bool qdisc_run_begin(struct Qdisc *qdisc)
{
	if (qdisc_is_running(qdisc))
		return false;
	qdisc->__state |= __QDISC___STATE_RUNNING;
	return true;
}

static inline void qdisc_run_end(struct Qdisc *qdisc)
{
	qdisc->__state &= ~__QDISC___STATE_RUNNING;
}

static inline bool qdisc_may_bulk(const struct Qdisc *qdisc)
{
	return qdisc->flags & TCQ_F_ONETXQUEUE;
}

static inline int qdisc_avail_bulklimit(const struct netdev_queue *txq)
{
#ifdef CONFIG_BQL
	/* Non-BQL migrated drivers will return 0, too. */
	return dql_avail(&txq->dql);
#else
	return 0;
#endif
}

static inline bool qdisc_is_throttled(const struct Qdisc *qdisc)
{
	return test_bit(__QDISC_STATE_THROTTLED, &qdisc->state) ? true : false;
}

static inline void qdisc_throttled(struct Qdisc *qdisc)
{
	set_bit(__QDISC_STATE_THROTTLED, &qdisc->state);
}

static inline void qdisc_unthrottled(struct Qdisc *qdisc)
{
	clear_bit(__QDISC_STATE_THROTTLED, &qdisc->state);
}

struct Qdisc_class_ops {
	/* Child qdisc manipulation */
	struct netdev_queue *	(*select_queue)(struct Qdisc *, struct tcmsg *);
	int			(*graft)(struct Qdisc *, unsigned long cl,
					struct Qdisc *, struct Qdisc **);
	struct Qdisc *		(*leaf)(struct Qdisc *, unsigned long cl);
	void			(*qlen_notify)(struct Qdisc *, unsigned long);

	/* Class manipulation routines */
	unsigned long		(*get)(struct Qdisc *, u32 classid);
	void			(*put)(struct Qdisc *, unsigned long);
	int			(*change)(struct Qdisc *, u32, u32,
					struct nlattr **, unsigned long *);
	int			(*delete)(struct Qdisc *, unsigned long);
	void			(*walk)(struct Qdisc *, struct qdisc_walker * arg);

	/* Filter manipulation */
	struct tcf_proto __rcu ** (*tcf_chain)(struct Qdisc *, unsigned long);
	unsigned long		(*bind_tcf)(struct Qdisc *, unsigned long,
					u32 classid);
	void			(*unbind_tcf)(struct Qdisc *, unsigned long);

	/* rtnetlink specific */
	int			(*dump)(struct Qdisc *, unsigned long,
					struct sk_buff *skb, struct tcmsg*);
	int			(*dump_stats)(struct Qdisc *, unsigned long,
					struct gnet_dump *);
};

struct Qdisc_ops {
	struct Qdisc_ops	*next;
	const struct Qdisc_class_ops	*cl_ops;
	char			id[IFNAMSIZ];
	int			priv_size;

	int 			(*enqueue)(struct sk_buff *, struct Qdisc *);
	struct sk_buff *	(*dequeue)(struct Qdisc *);
	struct sk_buff *	(*peek)(struct Qdisc *);
	unsigned int		(*drop)(struct Qdisc *);

	int			(*init)(struct Qdisc *, struct nlattr *arg);
	void			(*reset)(struct Qdisc *);
	void			(*destroy)(struct Qdisc *);
	int			(*change)(struct Qdisc *, struct nlattr *arg);
	void			(*attach)(struct Qdisc *);

	int			(*dump)(struct Qdisc *, struct sk_buff *);
	int			(*dump_stats)(struct Qdisc *, struct gnet_dump *);

	struct module		*owner;
};


struct tcf_result {
	unsigned long	class;
	u32		classid;
};

struct tcf_proto_ops {
	struct list_head	head;
	char			kind[IFNAMSIZ];

	int			(*classify)(struct sk_buff *,
					    const struct tcf_proto *,
					    struct tcf_result *);
	int			(*init)(struct tcf_proto*);
	bool			(*destroy)(struct tcf_proto*, bool);

	unsigned long		(*get)(struct tcf_proto*, u32 handle);
	int			(*change)(struct net *net, struct sk_buff *,
					struct tcf_proto*, unsigned long,
					u32 handle, struct nlattr **,
					unsigned long *, bool);
	int			(*delete)(struct tcf_proto*, unsigned long);
	void			(*walk)(struct tcf_proto*, struct tcf_walker *arg);

	/* rtnetlink specific */
	int			(*dump)(struct net*, struct tcf_proto*, unsigned long,
					struct sk_buff *skb, struct tcmsg*);

	struct module		*owner;
};

struct tcf_proto {
	/* Fast access part */
	struct tcf_proto __rcu	*next;
	void __rcu		*root;
	int			(*classify)(struct sk_buff *,
					    const struct tcf_proto *,
					    struct tcf_result *);
	__be16			protocol;

	/* All the rest */
	u32			prio;
	u32			classid;
	struct Qdisc		*q;
	void			*data;
	const struct tcf_proto_ops	*ops;
	struct rcu_head		rcu;
};

struct qdisc_skb_cb {
	unsigned int		pkt_len;
	u16			slave_dev_queue_mapping;
	u16			tc_classid;
#define QDISC_CB_PRIV_LEN 20
	unsigned char		data[QDISC_CB_PRIV_LEN];
};

static inline void qdisc_cb_private_validate(const struct sk_buff *skb, int sz)
{
	struct qdisc_skb_cb *qcb;

	BUILD_BUG_ON(sizeof(skb->cb) < offsetof(struct qdisc_skb_cb, data) + sz);
	BUILD_BUG_ON(sizeof(qcb->data) < sz);
}

static inline int qdisc_qlen(const struct Qdisc *q)
{
	return q->q.qlen;
}

static inline struct qdisc_skb_cb *qdisc_skb_cb(const struct sk_buff *skb)
{
	return (struct qdisc_skb_cb *)skb->cb;
}

static inline spinlock_t *qdisc_lock(struct Qdisc *qdisc)
{
	return &qdisc->q.lock;
}

static inline struct Qdisc *qdisc_root(const struct Qdisc *qdisc)
{
	struct Qdisc *q = rcu_dereference_rtnl(qdisc->dev_queue->qdisc);

	return q;
}

static inline struct Qdisc *qdisc_root_sleeping(const struct Qdisc *qdisc)
{
	return qdisc->dev_queue->qdisc_sleeping;
}

/* The qdisc root lock is a mechanism by which to top level
 * of a qdisc tree can be locked from any qdisc node in the
 * forest.  This allows changing the configuration of some
 * aspect of the qdisc tree while blocking out asynchronous
 * qdisc access in the packet processing paths.
 *
 * It is only legal to do this when the root will not change
 * on us.  Otherwise we'll potentially lock the wrong qdisc
 * root.  This is enforced by holding the RTNL semaphore, which
 * all users of this lock accessor must do.
 */
static inline spinlock_t *qdisc_root_lock(const struct Qdisc *qdisc)
{
	struct Qdisc *root = qdisc_root(qdisc);

	ASSERT_RTNL();
	return qdisc_lock(root);
}

static inline spinlock_t *qdisc_root_sleeping_lock(const struct Qdisc *qdisc)
{
	struct Qdisc *root = qdisc_root_sleeping(qdisc);

	ASSERT_RTNL();
	return qdisc_lock(root);
}

static inline struct net_device *qdisc_dev(const struct Qdisc *qdisc)
{
	return qdisc->dev_queue->dev;
}

static inline void sch_tree_lock(const struct Qdisc *q)
{
	spin_lock_bh(qdisc_root_sleeping_lock(q));
}

static inline void sch_tree_unlock(const struct Qdisc *q)
{
	spin_unlock_bh(qdisc_root_sleeping_lock(q));
}

#define tcf_tree_lock(tp)	sch_tree_lock((tp)->q)
#define tcf_tree_unlock(tp)	sch_tree_unlock((tp)->q)

extern struct Qdisc noop_qdisc;
extern struct Qdisc_ops noop_qdisc_ops;
extern struct Qdisc_ops pfifo_fast_ops;
extern struct Qdisc_ops mq_qdisc_ops;
extern struct Qdisc_ops noqueue_qdisc_ops;
extern const struct Qdisc_ops *default_qdisc_ops;

struct Qdisc_class_common {
	u32			classid;
	struct hlist_node	hnode;
};

struct Qdisc_class_hash {
	struct hlist_head	*hash;
	unsigned int		hashsize;
	unsigned int		hashmask;
	unsigned int		hashelems;
};

static inline unsigned int qdisc_class_hash(u32 id, u32 mask)
{
	id ^= id >> 8;
	id ^= id >> 4;
	return id & mask;
}

static inline struct Qdisc_class_common *
qdisc_class_find(const struct Qdisc_class_hash *hash, u32 id)
{
	struct Qdisc_class_common *cl;
	unsigned int h;

	h = qdisc_class_hash(id, hash->hashmask);
	hlist_for_each_entry(cl, &hash->hash[h], hnode) {
		if (cl->classid == id)
			return cl;
	}
	return NULL;
}

int qdisc_class_hash_init(struct Qdisc_class_hash *);
void qdisc_class_hash_insert(struct Qdisc_class_hash *,
			     struct Qdisc_class_common *);
void qdisc_class_hash_remove(struct Qdisc_class_hash *,
			     struct Qdisc_class_common *);
void qdisc_class_hash_grow(struct Qdisc *, struct Qdisc_class_hash *);
void qdisc_class_hash_destroy(struct Qdisc_class_hash *);

void dev_init_scheduler(struct net_device *dev);
void dev_shutdown(struct net_device *dev);
void dev_activate(struct net_device *dev);
void dev_deactivate(struct net_device *dev);
void dev_deactivate_many(struct list_head *head);
struct Qdisc *dev_graft_qdisc(struct netdev_queue *dev_queue,
			      struct Qdisc *qdisc);
void qdisc_reset(struct Qdisc *qdisc);
void qdisc_destroy(struct Qdisc *qdisc);
void qdisc_tree_decrease_qlen(struct Qdisc *qdisc, unsigned int n);
struct Qdisc *qdisc_alloc(struct netdev_queue *dev_queue,
			  const struct Qdisc_ops *ops);
struct Qdisc *qdisc_create_dflt(struct netdev_queue *dev_queue,
				const struct Qdisc_ops *ops, u32 parentid);
void __qdisc_calculate_pkt_len(struct sk_buff *skb,
			       const struct qdisc_size_table *stab);
bool tcf_destroy(struct tcf_proto *tp, bool force);
void tcf_destroy_chain(struct tcf_proto __rcu **fl);
int skb_do_redirect(struct sk_buff *);

static inline bool skb_at_tc_ingress(const struct sk_buff *skb)
{
#ifdef CONFIG_NET_CLS_ACT
	return G_TC_AT(skb->tc_verd) & AT_INGRESS;
#else
	return false;
#endif
}

/* Reset all TX qdiscs greater then index of a device.  */
static inline void qdisc_reset_all_tx_gt(struct net_device *dev, unsigned int i)
{
	struct Qdisc *qdisc;

	for (; i < dev->num_tx_queues; i++) {
		qdisc = rtnl_dereference(netdev_get_tx_queue(dev, i)->qdisc);
		if (qdisc) {
			spin_lock_bh(qdisc_lock(qdisc));
			qdisc_reset(qdisc);
			spin_unlock_bh(qdisc_lock(qdisc));
		}
	}
}

static inline void qdisc_reset_all_tx(struct net_device *dev)
{
	qdisc_reset_all_tx_gt(dev, 0);
}

/* Are all TX queues of the device empty?  */
static inline bool qdisc_all_tx_empty(const struct net_device *dev)
{
	unsigned int i;

	rcu_read_lock();
	for (i = 0; i < dev->num_tx_queues; i++) {
		struct netdev_queue *txq = netdev_get_tx_queue(dev, i);
		const struct Qdisc *q = rcu_dereference(txq->qdisc);

		if (q->q.qlen) {
			rcu_read_unlock();
			return false;
		}
	}
	rcu_read_unlock();
	return true;
}

/* Are any of the TX qdiscs changing?  */
static inline bool qdisc_tx_changing(const struct net_device *dev)
{
	unsigned int i;

	for (i = 0; i < dev->num_tx_queues; i++) {
		struct netdev_queue *txq = netdev_get_tx_queue(dev, i);
		if (rcu_access_pointer(txq->qdisc) != txq->qdisc_sleeping)
			return true;
	}
	return false;
}

/* Is the device using the noop qdisc on all queues?  */
static inline bool qdisc_tx_is_noop(const struct net_device *dev)
{
	unsigned int i;

	for (i = 0; i < dev->num_tx_queues; i++) {
		struct netdev_queue *txq = netdev_get_tx_queue(dev, i);
		if (rcu_access_pointer(txq->qdisc) != &noop_qdisc)
			return false;
	}
	return true;
}

static inline unsigned int qdisc_pkt_len(const struct sk_buff *skb)
{
	return qdisc_skb_cb(skb)->pkt_len;
}

/* additional qdisc xmit flags (NET_XMIT_MASK in linux/netdevice.h) */
enum net_xmit_qdisc_t {
	__NET_XMIT_STOLEN = 0x00010000,
	__NET_XMIT_BYPASS = 0x00020000,
};

#ifdef CONFIG_NET_CLS_ACT
#define net_xmit_drop_count(e)	((e) & __NET_XMIT_STOLEN ? 0 : 1)
#else
#define net_xmit_drop_count(e)	(1)
#endif

static inline void qdisc_calculate_pkt_len(struct sk_buff *skb,
					   const struct Qdisc *sch)
{
#ifdef CONFIG_NET_SCHED
	struct qdisc_size_table *stab = rcu_dereference_bh(sch->stab);

	if (stab)
		__qdisc_calculate_pkt_len(skb, stab);
#endif
}

static inline int qdisc_enqueue(struct sk_buff *skb, struct Qdisc *sch)
{
	qdisc_calculate_pkt_len(skb, sch);
	return sch->enqueue(skb, sch);
}

static inline bool qdisc_is_percpu_stats(const struct Qdisc *q)
{
	return q->flags & TCQ_F_CPUSTATS;
}

static inline void bstats_update(struct gnet_stats_basic_packed *bstats,
				 const struct sk_buff *skb)
{
	bstats->bytes += qdisc_pkt_len(skb);
	bstats->packets += skb_is_gso(skb) ? skb_shinfo(skb)->gso_segs : 1;
}

static inline void bstats_cpu_update(struct gnet_stats_basic_cpu *bstats,
				     const struct sk_buff *skb)
{
	u64_stats_update_begin(&bstats->syncp);
	bstats_update(&bstats->bstats, skb);
	u64_stats_update_end(&bstats->syncp);
}

static inline void qdisc_bstats_cpu_update(struct Qdisc *sch,
					   const struct sk_buff *skb)
{
	bstats_cpu_update(this_cpu_ptr(sch->cpu_bstats), skb);
}

static inline void qdisc_bstats_update(struct Qdisc *sch,
				       const struct sk_buff *skb)
{
	bstats_update(&sch->bstats, skb);
}

static inline void qdisc_qstats_backlog_dec(struct Qdisc *sch,
					    const struct sk_buff *skb)
{
	sch->qstats.backlog -= qdisc_pkt_len(skb);
}

static inline void qdisc_qstats_backlog_inc(struct Qdisc *sch,
					    const struct sk_buff *skb)
{
	sch->qstats.backlog += qdisc_pkt_len(skb);
}

static inline void __qdisc_qstats_drop(struct Qdisc *sch, int count)
{
	sch->qstats.drops += count;
}

static inline void qstats_drop_inc(struct gnet_stats_queue *qstats)
{
	qstats->drops++;
}

static inline void qstats_overlimit_inc(struct gnet_stats_queue *qstats)
{
	qstats->overlimits++;
}

static inline void qdisc_qstats_drop(struct Qdisc *sch)
{
	qstats_drop_inc(&sch->qstats);
}

static inline void qdisc_qstats_cpu_drop(struct Qdisc *sch)
{
	qstats_drop_inc(this_cpu_ptr(sch->cpu_qstats));
}

static inline void qdisc_qstats_overlimit(struct Qdisc *sch)
{
	sch->qstats.overlimits++;
}

static inline int __qdisc_enqueue_tail(struct sk_buff *skb, struct Qdisc *sch,
				       struct sk_buff_head *list)
{
	__skb_queue_tail(list, skb);
	qdisc_qstats_backlog_inc(sch, skb);

	return NET_XMIT_SUCCESS;
}

static inline int qdisc_enqueue_tail(struct sk_buff *skb, struct Qdisc *sch)
{
	return __qdisc_enqueue_tail(skb, sch, &sch->q);
}

static inline struct sk_buff *__qdisc_dequeue_head(struct Qdisc *sch,
						   struct sk_buff_head *list)
{
	struct sk_buff *skb = __skb_dequeue(list);

	if (likely(skb != NULL)) {
		qdisc_qstats_backlog_dec(sch, skb);
		qdisc_bstats_update(sch, skb);
	}

	return skb;
}

static inline struct sk_buff *qdisc_dequeue_head(struct Qdisc *sch)
{
	return __qdisc_dequeue_head(sch, &sch->q);
}

static inline unsigned int __qdisc_queue_drop_head(struct Qdisc *sch,
					      struct sk_buff_head *list)
{
	struct sk_buff *skb = __skb_dequeue(list);

	if (likely(skb != NULL)) {
		unsigned int len = qdisc_pkt_len(skb);
		qdisc_qstats_backlog_dec(sch, skb);
		kfree_skb(skb);
		return len;
	}

	return 0;
}

static inline unsigned int qdisc_queue_drop_head(struct Qdisc *sch)
{
	return __qdisc_queue_drop_head(sch, &sch->q);
}

static inline struct sk_buff *__qdisc_dequeue_tail(struct Qdisc *sch,
						   struct sk_buff_head *list)
{
	struct sk_buff *skb = __skb_dequeue_tail(list);

	if (likely(skb != NULL))
		qdisc_qstats_backlog_dec(sch, skb);

	return skb;
}

static inline struct sk_buff *qdisc_dequeue_tail(struct Qdisc *sch)
{
	return __qdisc_dequeue_tail(sch, &sch->q);
}

static inline struct sk_buff *qdisc_peek_head(struct Qdisc *sch)
{
	return skb_peek(&sch->q);
}

/* generic pseudo peek method for non-work-conserving qdisc */
static inline struct sk_buff *qdisc_peek_dequeued(struct Qdisc *sch)
{
	/* we can reuse ->gso_skb because peek isn't called for root qdiscs */
	if (!sch->gso_skb) {
		sch->gso_skb = sch->dequeue(sch);
		if (sch->gso_skb)
			/* it's still part of the queue */
			sch->q.qlen++;
	}

	return sch->gso_skb;
}

/* use instead of qdisc->dequeue() for all qdiscs queried with ->peek() */
static inline struct sk_buff *qdisc_dequeue_peeked(struct Qdisc *sch)
{
	struct sk_buff *skb = sch->gso_skb;

	if (skb) {
		sch->gso_skb = NULL;
		sch->q.qlen--;
	} else {
		skb = sch->dequeue(sch);
	}

	return skb;
}

static inline void __qdisc_reset_queue(struct Qdisc *sch,
				       struct sk_buff_head *list)
{
	/*
	 * We do not know the backlog in bytes of this list, it
	 * is up to the caller to correct it
	 */
	__skb_queue_purge(list);
}

static inline void qdisc_reset_queue(struct Qdisc *sch)
{
	__qdisc_reset_queue(sch, &sch->q);
	sch->qstats.backlog = 0;
}

static inline unsigned int __qdisc_queue_drop(struct Qdisc *sch,
					      struct sk_buff_head *list)
{
	struct sk_buff *skb = __qdisc_dequeue_tail(sch, list);

	if (likely(skb != NULL)) {
		unsigned int len = qdisc_pkt_len(skb);
		kfree_skb(skb);
		return len;
	}

	return 0;
}

static inline unsigned int qdisc_queue_drop(struct Qdisc *sch)
{
	return __qdisc_queue_drop(sch, &sch->q);
}

static inline int qdisc_drop(struct sk_buff *skb, struct Qdisc *sch)
{
	kfree_skb(skb);
	qdisc_qstats_drop(sch);

	return NET_XMIT_DROP;
}

static inline int qdisc_reshape_fail(struct sk_buff *skb, struct Qdisc *sch)
{
	qdisc_qstats_drop(sch);

#ifdef CONFIG_NET_CLS_ACT
	if (sch->reshape_fail == NULL || sch->reshape_fail(skb, sch))
		goto drop;

	return NET_XMIT_SUCCESS;

drop:
#endif
	kfree_skb(skb);
	return NET_XMIT_DROP;
}

/* Length to Time (L2T) lookup in a qdisc_rate_table, to determine how
   long it will take to send a packet given its size.
 */
static inline u32 qdisc_l2t(struct qdisc_rate_table* rtab, unsigned int pktlen)
{
	int slot = pktlen + rtab->rate.cell_align + rtab->rate.overhead;
	if (slot < 0)
		slot = 0;
	slot >>= rtab->rate.cell_log;
	if (slot > 255)
		return rtab->data[255]*(slot >> 8) + rtab->data[slot & 0xFF];
	return rtab->data[slot];
}

struct psched_ratecfg {
	u64	rate_bytes_ps; /* bytes per second */
	u32	mult;
	u16	overhead;
	u8	linklayer;
	u8	shift;
};

static inline u64 psched_l2t_ns(const struct psched_ratecfg *r,
				unsigned int len)
{
	len += r->overhead;

	if (unlikely(r->linklayer == TC_LINKLAYER_ATM))
		return ((u64)(DIV_ROUND_UP(len,48)*53) * r->mult) >> r->shift;

	return ((u64)len * r->mult) >> r->shift;
}

void psched_ratecfg_precompute(struct psched_ratecfg *r,
			       const struct tc_ratespec *conf,
			       u64 rate64);

static inline void psched_ratecfg_getrate(struct tc_ratespec *res,
					  const struct psched_ratecfg *r)
{
	memset(res, 0, sizeof(*res));

	/* legacy struct tc_ratespec has a 32bit @rate field
	 * Qdisc using 64bit rate should add new attributes
	 * in order to maintain compatibility.
	 */
	res->rate = min_t(u64, r->rate_bytes_ps, ~0U);

	res->overhead = r->overhead;
	res->linklayer = (r->linklayer & TC_LINKLAYER_MASK);
}

#endif
