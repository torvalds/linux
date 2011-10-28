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

  * The stored value for average bps is scaled by 2^5, so that maximal
    rate is ~2.15Gbits/s, average pps and cps are scaled by 2^10.

  * A lot code is taken from net/sched/estimator.c
 */


/*
 * Make a summary from each cpu
 */
static void ip_vs_read_cpu_stats(struct ip_vs_stats_user *sum,
				 struct ip_vs_cpu_stats *stats)
{
	int i;

	for_each_possible_cpu(i) {
		struct ip_vs_cpu_stats *s = per_cpu_ptr(stats, i);
		unsigned int start;
		__u64 inbytes, outbytes;
		if (i) {
			sum->conns += s->ustats.conns;
			sum->inpkts += s->ustats.inpkts;
			sum->outpkts += s->ustats.outpkts;
			do {
				start = u64_stats_fetch_begin(&s->syncp);
				inbytes = s->ustats.inbytes;
				outbytes = s->ustats.outbytes;
			} while (u64_stats_fetch_retry(&s->syncp, start));
			sum->inbytes += inbytes;
			sum->outbytes += outbytes;
		} else {
			sum->conns = s->ustats.conns;
			sum->inpkts = s->ustats.inpkts;
			sum->outpkts = s->ustats.outpkts;
			do {
				start = u64_stats_fetch_begin(&s->syncp);
				sum->inbytes = s->ustats.inbytes;
				sum->outbytes = s->ustats.outbytes;
			} while (u64_stats_fetch_retry(&s->syncp, start));
		}
	}
}


static void estimation_timer(unsigned long arg)
{
	struct ip_vs_estimator *e;
	struct ip_vs_stats *s;
	u32 n_conns;
	u32 n_inpkts, n_outpkts;
	u64 n_inbytes, n_outbytes;
	u32 rate;
	struct net *net = (struct net *)arg;
	struct netns_ipvs *ipvs;

	ipvs = net_ipvs(net);
	spin_lock(&ipvs->est_lock);
	list_for_each_entry(e, &ipvs->est_list, list) {
		s = container_of(e, struct ip_vs_stats, est);

		spin_lock(&s->lock);
		ip_vs_read_cpu_stats(&s->ustats, s->cpustats);
		n_conns = s->ustats.conns;
		n_inpkts = s->ustats.inpkts;
		n_outpkts = s->ustats.outpkts;
		n_inbytes = s->ustats.inbytes;
		n_outbytes = s->ustats.outbytes;

		/* scaled by 2^10, but divided 2 seconds */
		rate = (n_conns - e->last_conns) << 9;
		e->last_conns = n_conns;
		e->cps += ((long)rate - (long)e->cps) >> 2;

		rate = (n_inpkts - e->last_inpkts) << 9;
		e->last_inpkts = n_inpkts;
		e->inpps += ((long)rate - (long)e->inpps) >> 2;

		rate = (n_outpkts - e->last_outpkts) << 9;
		e->last_outpkts = n_outpkts;
		e->outpps += ((long)rate - (long)e->outpps) >> 2;

		rate = (n_inbytes - e->last_inbytes) << 4;
		e->last_inbytes = n_inbytes;
		e->inbps += ((long)rate - (long)e->inbps) >> 2;

		rate = (n_outbytes - e->last_outbytes) << 4;
		e->last_outbytes = n_outbytes;
		e->outbps += ((long)rate - (long)e->outbps) >> 2;
		spin_unlock(&s->lock);
	}
	spin_unlock(&ipvs->est_lock);
	mod_timer(&ipvs->est_timer, jiffies + 2*HZ);
}

void ip_vs_start_estimator(struct net *net, struct ip_vs_stats *stats)
{
	struct netns_ipvs *ipvs = net_ipvs(net);
	struct ip_vs_estimator *est = &stats->est;

	INIT_LIST_HEAD(&est->list);

	spin_lock_bh(&ipvs->est_lock);
	list_add(&est->list, &ipvs->est_list);
	spin_unlock_bh(&ipvs->est_lock);
}

void ip_vs_stop_estimator(struct net *net, struct ip_vs_stats *stats)
{
	struct netns_ipvs *ipvs = net_ipvs(net);
	struct ip_vs_estimator *est = &stats->est;

	spin_lock_bh(&ipvs->est_lock);
	list_del(&est->list);
	spin_unlock_bh(&ipvs->est_lock);
}

void ip_vs_zero_estimator(struct ip_vs_stats *stats)
{
	struct ip_vs_estimator *est = &stats->est;
	struct ip_vs_stats_user *u = &stats->ustats;

	/* reset counters, caller must hold the stats->lock lock */
	est->last_inbytes = u->inbytes;
	est->last_outbytes = u->outbytes;
	est->last_conns = u->conns;
	est->last_inpkts = u->inpkts;
	est->last_outpkts = u->outpkts;
	est->cps = 0;
	est->inpps = 0;
	est->outpps = 0;
	est->inbps = 0;
	est->outbps = 0;
}

/* Get decoded rates */
void ip_vs_read_estimator(struct ip_vs_stats_user *dst,
			  struct ip_vs_stats *stats)
{
	struct ip_vs_estimator *e = &stats->est;

	dst->cps = (e->cps + 0x1FF) >> 10;
	dst->inpps = (e->inpps + 0x1FF) >> 10;
	dst->outpps = (e->outpps + 0x1FF) >> 10;
	dst->inbps = (e->inbps + 0xF) >> 5;
	dst->outbps = (e->outbps + 0xF) >> 5;
}

int __net_init __ip_vs_estimator_init(struct net *net)
{
	struct netns_ipvs *ipvs = net_ipvs(net);

	INIT_LIST_HEAD(&ipvs->est_list);
	spin_lock_init(&ipvs->est_lock);
	setup_timer(&ipvs->est_timer, estimation_timer, (unsigned long)net);
	mod_timer(&ipvs->est_timer, jiffies + 2 * HZ);
	return 0;
}

void __net_exit __ip_vs_estimator_cleanup(struct net *net)
{
	del_timer_sync(&net_ipvs(net)->est_timer);
}

int __init ip_vs_estimator_init(void)
{
	return 0;
}

void ip_vs_estimator_cleanup(void)
{
}
