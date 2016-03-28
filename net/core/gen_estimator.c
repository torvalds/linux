/*
 * net/sched/gen_estimator.c	Simple rate estimator.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 * Changes:
 *              Jamal Hadi Salim - moved it to net/core and reshulfed
 *              names to make it usable in general net subsystem.
 */

#include <asm/uaccess.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/init.h>
#include <linux/rbtree.h>
#include <linux/slab.h>
#include <net/sock.h>
#include <net/gen_stats.h>

/*
   This code is NOT intended to be used for statistics collection,
   its purpose is to provide a base for statistical multiplexing
   for controlled load service.
   If you need only statistics, run a user level daemon which
   periodically reads byte counters.

   Unfortunately, rate estimation is not a very easy task.
   F.e. I did not find a simple way to estimate the current peak rate
   and even failed to formulate the problem 8)8)

   So I preferred not to built an estimator into the scheduler,
   but run this task separately.
   Ideally, it should be kernel thread(s), but for now it runs
   from timers, which puts apparent top bounds on the number of rated
   flows, has minimal overhead on small, but is enough
   to handle controlled load service, sets of aggregates.

   We measure rate over A=(1<<interval) seconds and evaluate EWMA:

   avrate = avrate*(1-W) + rate*W

   where W is chosen as negative power of 2: W = 2^(-ewma_log)

   The resulting time constant is:

   T = A/(-ln(1-W))


   NOTES.

   * avbps and avpps are scaled by 2^5.
   * both values are reported as 32 bit unsigned values. bps can
     overflow for fast links : max speed being 34360Mbit/sec
   * Minimal interval is HZ/4=250msec (it is the greatest common divisor
     for HZ=100 and HZ=1024 8)), maximal interval
     is (HZ*2^EST_MAX_INTERVAL)/4 = 8sec. Shorter intervals
     are too expensive, longer ones can be implemented
     at user level painlessly.
 */

#define EST_MAX_INTERVAL	5

struct gen_estimator
{
	struct list_head	list;
	struct gnet_stats_basic_packed	*bstats;
	struct gnet_stats_rate_est64	*rate_est;
	spinlock_t		*stats_lock;
	int			ewma_log;
	u32			last_packets;
	unsigned long		avpps;
	u64			last_bytes;
	u64			avbps;
	struct rcu_head		e_rcu;
	struct rb_node		node;
	struct gnet_stats_basic_cpu __percpu *cpu_bstats;
	struct rcu_head		head;
};

struct gen_estimator_head
{
	struct timer_list	timer;
	struct list_head	list;
};

static struct gen_estimator_head elist[EST_MAX_INTERVAL+1];

/* Protects against NULL dereference */
static DEFINE_RWLOCK(est_lock);

/* Protects against soft lockup during large deletion */
static struct rb_root est_root = RB_ROOT;
static DEFINE_SPINLOCK(est_tree_lock);

static void est_timer(unsigned long arg)
{
	int idx = (int)arg;
	struct gen_estimator *e;

	rcu_read_lock();
	list_for_each_entry_rcu(e, &elist[idx].list, list) {
		struct gnet_stats_basic_packed b = {0};
		unsigned long rate;
		u64 brate;

		spin_lock(e->stats_lock);
		read_lock(&est_lock);
		if (e->bstats == NULL)
			goto skip;

		__gnet_stats_copy_basic(&b, e->cpu_bstats, e->bstats);

		brate = (b.bytes - e->last_bytes)<<(7 - idx);
		e->last_bytes = b.bytes;
		e->avbps += (brate >> e->ewma_log) - (e->avbps >> e->ewma_log);
		e->rate_est->bps = (e->avbps+0xF)>>5;

		rate = b.packets - e->last_packets;
		rate <<= (7 - idx);
		e->last_packets = b.packets;
		e->avpps += (rate >> e->ewma_log) - (e->avpps >> e->ewma_log);
		e->rate_est->pps = (e->avpps + 0xF) >> 5;
skip:
		read_unlock(&est_lock);
		spin_unlock(e->stats_lock);
	}

	if (!list_empty(&elist[idx].list))
		mod_timer(&elist[idx].timer, jiffies + ((HZ/4) << idx));
	rcu_read_unlock();
}

static void gen_add_node(struct gen_estimator *est)
{
	struct rb_node **p = &est_root.rb_node, *parent = NULL;

	while (*p) {
		struct gen_estimator *e;

		parent = *p;
		e = rb_entry(parent, struct gen_estimator, node);

		if (est->bstats > e->bstats)
			p = &parent->rb_right;
		else
			p = &parent->rb_left;
	}
	rb_link_node(&est->node, parent, p);
	rb_insert_color(&est->node, &est_root);
}

static
struct gen_estimator *gen_find_node(const struct gnet_stats_basic_packed *bstats,
				    const struct gnet_stats_rate_est64 *rate_est)
{
	struct rb_node *p = est_root.rb_node;

	while (p) {
		struct gen_estimator *e;

		e = rb_entry(p, struct gen_estimator, node);

		if (bstats > e->bstats)
			p = p->rb_right;
		else if (bstats < e->bstats || rate_est != e->rate_est)
			p = p->rb_left;
		else
			return e;
	}
	return NULL;
}

/**
 * gen_new_estimator - create a new rate estimator
 * @bstats: basic statistics
 * @cpu_bstats: bstats per cpu
 * @rate_est: rate estimator statistics
 * @stats_lock: statistics lock
 * @opt: rate estimator configuration TLV
 *
 * Creates a new rate estimator with &bstats as source and &rate_est
 * as destination. A new timer with the interval specified in the
 * configuration TLV is created. Upon each interval, the latest statistics
 * will be read from &bstats and the estimated rate will be stored in
 * &rate_est with the statistics lock grabbed during this period.
 *
 * Returns 0 on success or a negative error code.
 *
 */
int gen_new_estimator(struct gnet_stats_basic_packed *bstats,
		      struct gnet_stats_basic_cpu __percpu *cpu_bstats,
		      struct gnet_stats_rate_est64 *rate_est,
		      spinlock_t *stats_lock,
		      struct nlattr *opt)
{
	struct gen_estimator *est;
	struct gnet_estimator *parm = nla_data(opt);
	struct gnet_stats_basic_packed b = {0};
	int idx;

	if (nla_len(opt) < sizeof(*parm))
		return -EINVAL;

	if (parm->interval < -2 || parm->interval > 3)
		return -EINVAL;

	est = kzalloc(sizeof(*est), GFP_KERNEL);
	if (est == NULL)
		return -ENOBUFS;

	__gnet_stats_copy_basic(&b, cpu_bstats, bstats);

	idx = parm->interval + 2;
	est->bstats = bstats;
	est->rate_est = rate_est;
	est->stats_lock = stats_lock;
	est->ewma_log = parm->ewma_log;
	est->last_bytes = b.bytes;
	est->avbps = rate_est->bps<<5;
	est->last_packets = b.packets;
	est->avpps = rate_est->pps<<10;
	est->cpu_bstats = cpu_bstats;

	spin_lock_bh(&est_tree_lock);
	if (!elist[idx].timer.function) {
		INIT_LIST_HEAD(&elist[idx].list);
		setup_timer(&elist[idx].timer, est_timer, idx);
	}

	if (list_empty(&elist[idx].list))
		mod_timer(&elist[idx].timer, jiffies + ((HZ/4) << idx));

	list_add_rcu(&est->list, &elist[idx].list);
	gen_add_node(est);
	spin_unlock_bh(&est_tree_lock);

	return 0;
}
EXPORT_SYMBOL(gen_new_estimator);

/**
 * gen_kill_estimator - remove a rate estimator
 * @bstats: basic statistics
 * @rate_est: rate estimator statistics
 *
 * Removes the rate estimator specified by &bstats and &rate_est.
 *
 * Note : Caller should respect an RCU grace period before freeing stats_lock
 */
void gen_kill_estimator(struct gnet_stats_basic_packed *bstats,
			struct gnet_stats_rate_est64 *rate_est)
{
	struct gen_estimator *e;

	spin_lock_bh(&est_tree_lock);
	while ((e = gen_find_node(bstats, rate_est))) {
		rb_erase(&e->node, &est_root);

		write_lock(&est_lock);
		e->bstats = NULL;
		write_unlock(&est_lock);

		list_del_rcu(&e->list);
		kfree_rcu(e, e_rcu);
	}
	spin_unlock_bh(&est_tree_lock);
}
EXPORT_SYMBOL(gen_kill_estimator);

/**
 * gen_replace_estimator - replace rate estimator configuration
 * @bstats: basic statistics
 * @cpu_bstats: bstats per cpu
 * @rate_est: rate estimator statistics
 * @stats_lock: statistics lock
 * @opt: rate estimator configuration TLV
 *
 * Replaces the configuration of a rate estimator by calling
 * gen_kill_estimator() and gen_new_estimator().
 *
 * Returns 0 on success or a negative error code.
 */
int gen_replace_estimator(struct gnet_stats_basic_packed *bstats,
			  struct gnet_stats_basic_cpu __percpu *cpu_bstats,
			  struct gnet_stats_rate_est64 *rate_est,
			  spinlock_t *stats_lock, struct nlattr *opt)
{
	gen_kill_estimator(bstats, rate_est);
	return gen_new_estimator(bstats, cpu_bstats, rate_est, stats_lock, opt);
}
EXPORT_SYMBOL(gen_replace_estimator);

/**
 * gen_estimator_active - test if estimator is currently in use
 * @bstats: basic statistics
 * @rate_est: rate estimator statistics
 *
 * Returns true if estimator is active, and false if not.
 */
bool gen_estimator_active(const struct gnet_stats_basic_packed *bstats,
			  const struct gnet_stats_rate_est64 *rate_est)
{
	bool res;

	ASSERT_RTNL();

	spin_lock_bh(&est_tree_lock);
	res = gen_find_node(bstats, rate_est) != NULL;
	spin_unlock_bh(&est_tree_lock);

	return res;
}
EXPORT_SYMBOL(gen_estimator_active);
