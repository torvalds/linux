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
#include <asm/system.h>
#include <asm/bitops.h>
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

   * The stored value for avbps is scaled by 2^5, so that maximal
     rate is ~1Gbit, avpps is scaled by 2^10.

   * Minimal interval is HZ/4=250msec (it is the greatest common divisor
     for HZ=100 and HZ=1024 8)), maximal interval
     is (HZ*2^EST_MAX_INTERVAL)/4 = 8sec. Shorter intervals
     are too expensive, longer ones can be implemented
     at user level painlessly.
 */

#define EST_MAX_INTERVAL	5

struct gen_estimator
{
	struct gen_estimator	*next;
	struct gnet_stats_basic	*bstats;
	struct gnet_stats_rate_est	*rate_est;
	spinlock_t		*stats_lock;
	unsigned		interval;
	int			ewma_log;
	u64			last_bytes;
	u32			last_packets;
	u32			avpps;
	u32			avbps;
};

struct gen_estimator_head
{
	struct timer_list	timer;
	struct gen_estimator	*list;
};

static struct gen_estimator_head elist[EST_MAX_INTERVAL+1];

/* Estimator array lock */
static DEFINE_RWLOCK(est_lock);

static void est_timer(unsigned long arg)
{
	int idx = (int)arg;
	struct gen_estimator *e;

	read_lock(&est_lock);
	for (e = elist[idx].list; e; e = e->next) {
		u64 nbytes;
		u32 npackets;
		u32 rate;

		spin_lock(e->stats_lock);
		nbytes = e->bstats->bytes;
		npackets = e->bstats->packets;
		rate = (nbytes - e->last_bytes)<<(7 - idx);
		e->last_bytes = nbytes;
		e->avbps += ((long)rate - (long)e->avbps) >> e->ewma_log;
		e->rate_est->bps = (e->avbps+0xF)>>5;

		rate = (npackets - e->last_packets)<<(12 - idx);
		e->last_packets = npackets;
		e->avpps += ((long)rate - (long)e->avpps) >> e->ewma_log;
		e->rate_est->pps = (e->avpps+0x1FF)>>10;
		spin_unlock(e->stats_lock);
	}

	mod_timer(&elist[idx].timer, jiffies + ((HZ<<idx)/4));
	read_unlock(&est_lock);
}

/**
 * gen_new_estimator - create a new rate estimator
 * @bstats: basic statistics
 * @rate_est: rate estimator statistics
 * @stats_lock: statistics lock
 * @opt: rate estimator configuration TLV
 *
 * Creates a new rate estimator with &bstats as source and &rate_est
 * as destination. A new timer with the interval specified in the
 * configuration TLV is created. Upon each interval, the latest statistics
 * will be read from &bstats and the estimated rate will be stored in
 * &rate_est with the statistics lock grabed during this period.
 * 
 * Returns 0 on success or a negative error code.
 */
int gen_new_estimator(struct gnet_stats_basic *bstats,
	struct gnet_stats_rate_est *rate_est, spinlock_t *stats_lock, struct rtattr *opt)
{
	struct gen_estimator *est;
	struct gnet_estimator *parm = RTA_DATA(opt);

	if (RTA_PAYLOAD(opt) < sizeof(*parm))
		return -EINVAL;

	if (parm->interval < -2 || parm->interval > 3)
		return -EINVAL;

	est = kzalloc(sizeof(*est), GFP_KERNEL);
	if (est == NULL)
		return -ENOBUFS;

	est->interval = parm->interval + 2;
	est->bstats = bstats;
	est->rate_est = rate_est;
	est->stats_lock = stats_lock;
	est->ewma_log = parm->ewma_log;
	est->last_bytes = bstats->bytes;
	est->avbps = rate_est->bps<<5;
	est->last_packets = bstats->packets;
	est->avpps = rate_est->pps<<10;

	est->next = elist[est->interval].list;
	if (est->next == NULL) {
		init_timer(&elist[est->interval].timer);
		elist[est->interval].timer.data = est->interval;
		elist[est->interval].timer.expires = jiffies + ((HZ<<est->interval)/4);
		elist[est->interval].timer.function = est_timer;
		add_timer(&elist[est->interval].timer);
	}
	write_lock_bh(&est_lock);
	elist[est->interval].list = est;
	write_unlock_bh(&est_lock);
	return 0;
}

/**
 * gen_kill_estimator - remove a rate estimator
 * @bstats: basic statistics
 * @rate_est: rate estimator statistics
 *
 * Removes the rate estimator specified by &bstats and &rate_est
 * and deletes the timer.
 */
void gen_kill_estimator(struct gnet_stats_basic *bstats,
	struct gnet_stats_rate_est *rate_est)
{
	int idx;
	struct gen_estimator *est, **pest;

	for (idx=0; idx <= EST_MAX_INTERVAL; idx++) {
		int killed = 0;
		pest = &elist[idx].list;
		while ((est=*pest) != NULL) {
			if (est->rate_est != rate_est || est->bstats != bstats) {
				pest = &est->next;
				continue;
			}

			write_lock_bh(&est_lock);
			*pest = est->next;
			write_unlock_bh(&est_lock);

			kfree(est);
			killed++;
		}
		if (killed && elist[idx].list == NULL)
			del_timer(&elist[idx].timer);
	}
}

/**
 * gen_replace_estimator - replace rate estimator configruation
 * @bstats: basic statistics
 * @rate_est: rate estimator statistics
 * @stats_lock: statistics lock
 * @opt: rate estimator configuration TLV
 *
 * Replaces the configuration of a rate estimator by calling
 * gen_kill_estimator() and gen_new_estimator().
 * 
 * Returns 0 on success or a negative error code.
 */
int
gen_replace_estimator(struct gnet_stats_basic *bstats,
	struct gnet_stats_rate_est *rate_est, spinlock_t *stats_lock,
	struct rtattr *opt)
{
    gen_kill_estimator(bstats, rate_est);
    return gen_new_estimator(bstats, rate_est, stats_lock, opt);
}
    

EXPORT_SYMBOL(gen_kill_estimator);
EXPORT_SYMBOL(gen_new_estimator);
EXPORT_SYMBOL(gen_replace_estimator);
