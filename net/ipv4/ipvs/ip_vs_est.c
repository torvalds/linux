/*
 * ip_vs_est.c: simple rate estimator for IPVS
 *
 * Version:     $Id: ip_vs_est.c,v 1.4 2002/11/30 01:50:35 wensong Exp $
 *
 * Authors:     Wensong Zhang <wensong@linuxvirtualserver.org>
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Changes:
 *
 */
#include <linux/kernel.h>
#include <linux/types.h>

#include <net/ip_vs.h>

/*
  This code is to estimate rate in a shorter interval (such as 8
  seconds) for virtual services and real servers. For measure rate in a
  long interval, it is easy to implement a user level daemon which
  periodically reads those statistical counters and measure rate.

  Currently, the measurement is activated by slow timer handler. Hope
  this measurement will not introduce too much load.

  We measure rate during the last 8 seconds every 2 seconds:

    avgrate = avgrate*(1-W) + rate*W

    where W = 2^(-2)

  NOTES.

  * The stored value for average bps is scaled by 2^5, so that maximal
    rate is ~2.15Gbits/s, average pps and cps are scaled by 2^10.

  * A lot code is taken from net/sched/estimator.c
 */


struct ip_vs_estimator
{
	struct ip_vs_estimator	*next;
	struct ip_vs_stats	*stats;

	u32			last_conns;
	u32			last_inpkts;
	u32			last_outpkts;
	u64			last_inbytes;
	u64			last_outbytes;

	u32			cps;
	u32			inpps;
	u32			outpps;
	u32			inbps;
	u32			outbps;
};


static struct ip_vs_estimator *est_list = NULL;
static DEFINE_RWLOCK(est_lock);
static struct timer_list est_timer;

static void estimation_timer(unsigned long arg)
{
	struct ip_vs_estimator *e;
	struct ip_vs_stats *s;
	u32 n_conns;
	u32 n_inpkts, n_outpkts;
	u64 n_inbytes, n_outbytes;
	u32 rate;

	read_lock(&est_lock);
	for (e = est_list; e; e = e->next) {
		s = e->stats;

		spin_lock(&s->lock);
		n_conns = s->conns;
		n_inpkts = s->inpkts;
		n_outpkts = s->outpkts;
		n_inbytes = s->inbytes;
		n_outbytes = s->outbytes;

		/* scaled by 2^10, but divided 2 seconds */
		rate = (n_conns - e->last_conns)<<9;
		e->last_conns = n_conns;
		e->cps += ((long)rate - (long)e->cps)>>2;
		s->cps = (e->cps+0x1FF)>>10;

		rate = (n_inpkts - e->last_inpkts)<<9;
		e->last_inpkts = n_inpkts;
		e->inpps += ((long)rate - (long)e->inpps)>>2;
		s->inpps = (e->inpps+0x1FF)>>10;

		rate = (n_outpkts - e->last_outpkts)<<9;
		e->last_outpkts = n_outpkts;
		e->outpps += ((long)rate - (long)e->outpps)>>2;
		s->outpps = (e->outpps+0x1FF)>>10;

		rate = (n_inbytes - e->last_inbytes)<<4;
		e->last_inbytes = n_inbytes;
		e->inbps += ((long)rate - (long)e->inbps)>>2;
		s->inbps = (e->inbps+0xF)>>5;

		rate = (n_outbytes - e->last_outbytes)<<4;
		e->last_outbytes = n_outbytes;
		e->outbps += ((long)rate - (long)e->outbps)>>2;
		s->outbps = (e->outbps+0xF)>>5;
		spin_unlock(&s->lock);
	}
	read_unlock(&est_lock);
	mod_timer(&est_timer, jiffies + 2*HZ);
}

int ip_vs_new_estimator(struct ip_vs_stats *stats)
{
	struct ip_vs_estimator *est;

	est = kmalloc(sizeof(*est), GFP_KERNEL);
	if (est == NULL)
		return -ENOMEM;

	memset(est, 0, sizeof(*est));
	est->stats = stats;
	est->last_conns = stats->conns;
	est->cps = stats->cps<<10;

	est->last_inpkts = stats->inpkts;
	est->inpps = stats->inpps<<10;

	est->last_outpkts = stats->outpkts;
	est->outpps = stats->outpps<<10;

	est->last_inbytes = stats->inbytes;
	est->inbps = stats->inbps<<5;

	est->last_outbytes = stats->outbytes;
	est->outbps = stats->outbps<<5;

	write_lock_bh(&est_lock);
	est->next = est_list;
	if (est->next == NULL) {
		init_timer(&est_timer);
		est_timer.expires = jiffies + 2*HZ;
		est_timer.function = estimation_timer;
		add_timer(&est_timer);
	}
	est_list = est;
	write_unlock_bh(&est_lock);
	return 0;
}

void ip_vs_kill_estimator(struct ip_vs_stats *stats)
{
	struct ip_vs_estimator *est, **pest;
	int killed = 0;

	write_lock_bh(&est_lock);
	pest = &est_list;
	while ((est=*pest) != NULL) {
		if (est->stats != stats) {
			pest = &est->next;
			continue;
		}
		*pest = est->next;
		kfree(est);
		killed++;
	}
	if (killed && est_list == NULL)
		del_timer_sync(&est_timer);
	write_unlock_bh(&est_lock);
}

void ip_vs_zero_estimator(struct ip_vs_stats *stats)
{
	struct ip_vs_estimator *e;

	write_lock_bh(&est_lock);
	for (e = est_list; e; e = e->next) {
		if (e->stats != stats)
			continue;

		/* set counters zero */
		e->last_conns = 0;
		e->last_inpkts = 0;
		e->last_outpkts = 0;
		e->last_inbytes = 0;
		e->last_outbytes = 0;
		e->cps = 0;
		e->inpps = 0;
		e->outpps = 0;
		e->inbps = 0;
		e->outbps = 0;
	}
	write_unlock_bh(&est_lock);
}
