/*
 * ip_vs_est.c: simple rate estimator for IPVS
 *
 * Authors:     Wensong Zhang <wensong@linuxvirtualserver.org>
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Changes:     Hans Schillstrom <hans.schillstrom@ericsson.com>
 *              Network name space (netns) aware.
 *              Global data moved to netns i.e struct netns_ipvs
 *              Affected data: est_list and est_lock.
 *              estimation_timer() runs with timer per netns.
 *              get_stats()) do the per cpu summing.
 */

#define KMSG_COMPONENT "IPVS"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/sysctl.h>
#include <linux/list.h>

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

  * Average bps is scaled by 2^5, while average pps and cps are scaled by 2^10.

  * Netlink users can see 64-bit values but sockopt users are restricted
    to 32-bit values for conns, packets, bps, cps and pps.

  * A lot of code is taken from net/core/gen_estimator.c
 */


/*
 * Make a summary from each cpu
 */
static void ip_vs_read_cpu_stats(struct ip_vs_kstats *sum,
				 struct ip_vs_cpu_stats __percpu *stats)
{
	int i;
	bool add = false;

	for_each_possible_cpu(i) {
		struct ip_vs_cpu_stats *s = per_cpu_ptr(stats, i);
		unsigned int start;
		u64 conns, inpkts, outpkts, inbytes, outbytes;

		if (add) {
			do {
				start = u64_stats_fetch_begin(&s->syncp);
				conns = s->cnt.conns;
				inpkts = s->cnt.inpkts;
				outpkts = s->cnt.outpkts;
				inbytes = s->cnt.inbytes;
				outbytes = s->cnt.outbytes;
			} while (u64_stats_fetch_retry(&s->syncp, start));
			sum->conns += conns;
			sum->inpkts += inpkts;
			sum->outpkts += outpkts;
			sum->inbytes += inbytes;
			sum->outbytes += outbytes;
		} else {
			add = true;
			do {
				start = u64_stats_fetch_begin(&s->syncp);
				sum->conns = s->cnt.conns;
				sum->inpkts = s->cnt.inpkts;
				sum->outpkts = s->cnt.outpkts;
				sum->inbytes = s->cnt.inbytes;
				sum->outbytes = s->cnt.outbytes;
			} while (u64_stats_fetch_retry(&s->syncp, start));
		}
	}
}


static void estimation_timer(struct timer_list *t)
{
	struct ip_vs_estimator *e;
	struct ip_vs_stats *s;
	u64 rate;
	struct netns_ipvs *ipvs = from_timer(ipvs, t, est_timer);

	spin_lock(&ipvs->est_lock);
	list_for_each_entry(e, &ipvs->est_list, list) {
		s = container_of(e, struct ip_vs_stats, est);

		spin_lock(&s->lock);
		ip_vs_read_cpu_stats(&s->kstats, s->cpustats);

		/* scaled by 2^10, but divided 2 seconds */
		rate = (s->kstats.conns - e->last_conns) << 9;
		e->last_conns = s->kstats.conns;
		e->cps += ((s64)rate - (s64)e->cps) >> 2;

		rate = (s->kstats.inpkts - e->last_inpkts) << 9;
		e->last_inpkts = s->kstats.inpkts;
		e->inpps += ((s64)rate - (s64)e->inpps) >> 2;

		rate = (s->kstats.outpkts - e->last_outpkts) << 9;
		e->last_outpkts = s->kstats.outpkts;
		e->outpps += ((s64)rate - (s64)e->outpps) >> 2;

		/* scaled by 2^5, but divided 2 seconds */
		rate = (s->kstats.inbytes - e->last_inbytes) << 4;
		e->last_inbytes = s->kstats.inbytes;
		e->inbps += ((s64)rate - (s64)e->inbps) >> 2;

		rate = (s->kstats.outbytes - e->last_outbytes) << 4;
		e->last_outbytes = s->kstats.outbytes;
		e->outbps += ((s64)rate - (s64)e->outbps) >> 2;
		spin_unlock(&s->lock);
	}
	spin_unlock(&ipvs->est_lock);
	mod_timer(&ipvs->est_timer, jiffies + 2*HZ);
}

void ip_vs_start_estimator(struct netns_ipvs *ipvs, struct ip_vs_stats *stats)
{
	struct ip_vs_estimator *est = &stats->est;

	INIT_LIST_HEAD(&est->list);

	spin_lock_bh(&ipvs->est_lock);
	list_add(&est->list, &ipvs->est_list);
	spin_unlock_bh(&ipvs->est_lock);
}

void ip_vs_stop_estimator(struct netns_ipvs *ipvs, struct ip_vs_stats *stats)
{
	struct ip_vs_estimator *est = &stats->est;

	spin_lock_bh(&ipvs->est_lock);
	list_del(&est->list);
	spin_unlock_bh(&ipvs->est_lock);
}

void ip_vs_zero_estimator(struct ip_vs_stats *stats)
{
	struct ip_vs_estimator *est = &stats->est;
	struct ip_vs_kstats *k = &stats->kstats;

	/* reset counters, caller must hold the stats->lock lock */
	est->last_inbytes = k->inbytes;
	est->last_outbytes = k->outbytes;
	est->last_conns = k->conns;
	est->last_inpkts = k->inpkts;
	est->last_outpkts = k->outpkts;
	est->cps = 0;
	est->inpps = 0;
	est->outpps = 0;
	est->inbps = 0;
	est->outbps = 0;
}

/* Get decoded rates */
void ip_vs_read_estimator(struct ip_vs_kstats *dst, struct ip_vs_stats *stats)
{
	struct ip_vs_estimator *e = &stats->est;

	dst->cps = (e->cps + 0x1FF) >> 10;
	dst->inpps = (e->inpps + 0x1FF) >> 10;
	dst->outpps = (e->outpps + 0x1FF) >> 10;
	dst->inbps = (e->inbps + 0xF) >> 5;
	dst->outbps = (e->outbps + 0xF) >> 5;
}

int __net_init ip_vs_estimator_net_init(struct netns_ipvs *ipvs)
{
	INIT_LIST_HEAD(&ipvs->est_list);
	spin_lock_init(&ipvs->est_lock);
	timer_setup(&ipvs->est_timer, estimation_timer, 0);
	mod_timer(&ipvs->est_timer, jiffies + 2 * HZ);
	return 0;
}

void __net_exit ip_vs_estimator_net_cleanup(struct netns_ipvs *ipvs)
{
	del_timer_sync(&ipvs->est_timer);
}
