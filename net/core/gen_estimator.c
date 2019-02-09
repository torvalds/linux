/*
 * net/sched/gen_estimator.c	Simple rate estimator.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *		Eric Dumazet <edumazet@google.com>
 *
 * Changes:
 *              Jamal Hadi Salim - moved it to net/core and reshulfed
 *              names to make it usable in general net subsystem.
 */

#include <linux/uaccess.h>
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
#include <linux/slab.h>
#include <linux/seqlock.h>
#include <net/sock.h>
#include <net/gen_stats.h>

/* This code is NOT intended to be used for statistics collection,
 * its purpose is to provide a base for statistical multiplexing
 * for controlled load service.
 * If you need only statistics, run a user level daemon which
 * periodically reads byte counters.
 */

struct net_rate_estimator {
	struct gnet_stats_basic_packed	*bstats;
	spinlock_t		*stats_lock;
	seqcount_t		*running;
	struct gnet_stats_basic_cpu __percpu *cpu_bstats;
	u8			ewma_log;
	u8			intvl_log; /* period : (250ms << intvl_log) */

	seqcount_t		seq;
	u32			last_packets;
	u64			last_bytes;

	u64			avpps;
	u64			avbps;

	unsigned long           next_jiffies;
	struct timer_list       timer;
	struct rcu_head		rcu;
};

static void est_fetch_counters(struct net_rate_estimator *e,
			       struct gnet_stats_basic_packed *b)
{
	memset(b, 0, sizeof(*b));
	if (e->stats_lock)
		spin_lock(e->stats_lock);

	__gnet_stats_copy_basic(e->running, b, e->cpu_bstats, e->bstats);

	if (e->stats_lock)
		spin_unlock(e->stats_lock);

}

static void est_timer(struct timer_list *t)
{
	struct net_rate_estimator *est = from_timer(est, t, timer);
	struct gnet_stats_basic_packed b;
	u64 rate, brate;

	est_fetch_counters(est, &b);
	brate = (b.bytes - est->last_bytes) << (10 - est->ewma_log - est->intvl_log);
	brate -= (est->avbps >> est->ewma_log);

	rate = (u64)(b.packets - est->last_packets) << (10 - est->ewma_log - est->intvl_log);
	rate -= (est->avpps >> est->ewma_log);

	write_seqcount_begin(&est->seq);
	est->avbps += brate;
	est->avpps += rate;
	write_seqcount_end(&est->seq);

	est->last_bytes = b.bytes;
	est->last_packets = b.packets;

	est->next_jiffies += ((HZ/4) << est->intvl_log);

	if (unlikely(time_after_eq(jiffies, est->next_jiffies))) {
		/* Ouch... timer was delayed. */
		est->next_jiffies = jiffies + 1;
	}
	mod_timer(&est->timer, est->next_jiffies);
}

/**
 * gen_new_estimator - create a new rate estimator
 * @bstats: basic statistics
 * @cpu_bstats: bstats per cpu
 * @rate_est: rate estimator statistics
 * @lock: lock for statistics and control path
 * @running: qdisc running seqcount
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
		      struct net_rate_estimator __rcu **rate_est,
		      spinlock_t *lock,
		      seqcount_t *running,
		      struct nlattr *opt)
{
	struct gnet_estimator *parm = nla_data(opt);
	struct net_rate_estimator *old, *est;
	struct gnet_stats_basic_packed b;
	int intvl_log;

	if (nla_len(opt) < sizeof(*parm))
		return -EINVAL;

	/* allowed timer periods are :
	 * -2 : 250ms,   -1 : 500ms,    0 : 1 sec
	 *  1 : 2 sec,    2 : 4 sec,    3 : 8 sec
	 */
	if (parm->interval < -2 || parm->interval > 3)
		return -EINVAL;

	est = kzalloc(sizeof(*est), GFP_KERNEL);
	if (!est)
		return -ENOBUFS;

	seqcount_init(&est->seq);
	intvl_log = parm->interval + 2;
	est->bstats = bstats;
	est->stats_lock = lock;
	est->running  = running;
	est->ewma_log = parm->ewma_log;
	est->intvl_log = intvl_log;
	est->cpu_bstats = cpu_bstats;

	if (lock)
		local_bh_disable();
	est_fetch_counters(est, &b);
	if (lock)
		local_bh_enable();
	est->last_bytes = b.bytes;
	est->last_packets = b.packets;

	if (lock)
		spin_lock_bh(lock);
	old = rcu_dereference_protected(*rate_est, 1);
	if (old) {
		del_timer_sync(&old->timer);
		est->avbps = old->avbps;
		est->avpps = old->avpps;
	}

	est->next_jiffies = jiffies + ((HZ/4) << intvl_log);
	timer_setup(&est->timer, est_timer, 0);
	mod_timer(&est->timer, est->next_jiffies);

	rcu_assign_pointer(*rate_est, est);
	if (lock)
		spin_unlock_bh(lock);
	if (old)
		kfree_rcu(old, rcu);
	return 0;
}
EXPORT_SYMBOL(gen_new_estimator);

/**
 * gen_kill_estimator - remove a rate estimator
 * @rate_est: rate estimator
 *
 * Removes the rate estimator.
 *
 */
void gen_kill_estimator(struct net_rate_estimator __rcu **rate_est)
{
	struct net_rate_estimator *est;

	est = xchg((__force struct net_rate_estimator **)rate_est, NULL);
	if (est) {
		del_timer_sync(&est->timer);
		kfree_rcu(est, rcu);
	}
}
EXPORT_SYMBOL(gen_kill_estimator);

/**
 * gen_replace_estimator - replace rate estimator configuration
 * @bstats: basic statistics
 * @cpu_bstats: bstats per cpu
 * @rate_est: rate estimator statistics
 * @lock: lock for statistics and control path
 * @running: qdisc running seqcount (might be NULL)
 * @opt: rate estimator configuration TLV
 *
 * Replaces the configuration of a rate estimator by calling
 * gen_kill_estimator() and gen_new_estimator().
 *
 * Returns 0 on success or a negative error code.
 */
int gen_replace_estimator(struct gnet_stats_basic_packed *bstats,
			  struct gnet_stats_basic_cpu __percpu *cpu_bstats,
			  struct net_rate_estimator __rcu **rate_est,
			  spinlock_t *lock,
			  seqcount_t *running, struct nlattr *opt)
{
	return gen_new_estimator(bstats, cpu_bstats, rate_est,
				 lock, running, opt);
}
EXPORT_SYMBOL(gen_replace_estimator);

/**
 * gen_estimator_active - test if estimator is currently in use
 * @rate_est: rate estimator
 *
 * Returns true if estimator is active, and false if not.
 */
bool gen_estimator_active(struct net_rate_estimator __rcu **rate_est)
{
	return !!rcu_access_pointer(*rate_est);
}
EXPORT_SYMBOL(gen_estimator_active);

bool gen_estimator_read(struct net_rate_estimator __rcu **rate_est,
			struct gnet_stats_rate_est64 *sample)
{
	struct net_rate_estimator *est;
	unsigned seq;

	rcu_read_lock();
	est = rcu_dereference(*rate_est);
	if (!est) {
		rcu_read_unlock();
		return false;
	}

	do {
		seq = read_seqcount_begin(&est->seq);
		sample->bps = est->avbps >> 8;
		sample->pps = est->avpps >> 8;
	} while (read_seqcount_retry(&est->seq, seq));

	rcu_read_unlock();
	return true;
}
EXPORT_SYMBOL(gen_estimator_read);
