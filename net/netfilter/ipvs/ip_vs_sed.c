/*
 * IPVS:        Shortest Expected Delay scheduling module
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

/*
 * The SED algorithm attempts to minimize each job's expected delay until
 * completion. The expected delay that the job will experience is
 * (Ci + 1) / Ui if sent to the ith server, in which Ci is the number of
 * jobs on the ith server and Ui is the fixed service rate (weight) of
 * the ith server. The SED algorithm adopts a greedy policy that each does
 * what is in its own best interest, i.e. to join the queue which would
 * minimize its expected delay of completion.
 *
 * See the following paper for more information:
 * A. Weinrib and S. Shenker, Greed is not enough: Adaptive load sharing
 * in large heterogeneous systems. In Proceedings IEEE INFOCOM'88,
 * pages 986-994, 1988.
 *
 * Thanks must go to Marko Buuri <marko@buuri.name> for talking SED to me.
 *
 * The difference between SED and WLC is that SED includes the incoming
 * job in the cost function (the increment of 1). SED may outperform
 * WLC, while scheduling big jobs under larger heterogeneous systems
 * (the server weight varies a lot).
 *
 */

#define KMSG_COMPONENT "IPVS"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>

#include <net/ip_vs.h>


static inline unsigned int
ip_vs_sed_dest_overhead(struct ip_vs_dest *dest)
{
	/*
	 * We only use the active connection number in the cost
	 * calculation here.
	 */
	return atomic_read(&dest->activeconns) + 1;
}


/*
 *	Weighted Least Connection scheduling
 */
static struct ip_vs_dest *
ip_vs_sed_schedule(struct ip_vs_service *svc, const struct sk_buff *skb)
{
	struct ip_vs_dest *dest, *least;
	unsigned int loh, doh;

	IP_VS_DBG(6, "%s(): Scheduling...\n", __func__);

	/*
	 * We calculate the load of each dest server as follows:
	 *	(server expected overhead) / dest->weight
	 *
	 * Remember -- no floats in kernel mode!!!
	 * The comparison of h1*w2 > h2*w1 is equivalent to that of
	 *		  h1/w1 > h2/w2
	 * if every weight is larger than zero.
	 *
	 * The server with weight=0 is quiesced and will not receive any
	 * new connections.
	 */

	list_for_each_entry(dest, &svc->destinations, n_list) {
		if (!(dest->flags & IP_VS_DEST_F_OVERLOAD) &&
		    atomic_read(&dest->weight) > 0) {
			least = dest;
			loh = ip_vs_sed_dest_overhead(least);
			goto nextstage;
		}
	}
	IP_VS_ERR_RL("SED: no destination available\n");
	return NULL;

	/*
	 *    Find the destination with the least load.
	 */
  nextstage:
	list_for_each_entry_continue(dest, &svc->destinations, n_list) {
		if (dest->flags & IP_VS_DEST_F_OVERLOAD)
			continue;
		doh = ip_vs_sed_dest_overhead(dest);
		if (loh * atomic_read(&dest->weight) >
		    doh * atomic_read(&least->weight)) {
			least = dest;
			loh = doh;
		}
	}

	IP_VS_DBG_BUF(6, "SED: server %s:%u "
		      "activeconns %d refcnt %d weight %d overhead %d\n",
		      IP_VS_DBG_ADDR(svc->af, &least->addr), ntohs(least->port),
		      atomic_read(&least->activeconns),
		      atomic_read(&least->refcnt),
		      atomic_read(&least->weight), loh);

	return least;
}


static struct ip_vs_scheduler ip_vs_sed_scheduler =
{
	.name =			"sed",
	.refcnt =		ATOMIC_INIT(0),
	.module =		THIS_MODULE,
	.n_list =		LIST_HEAD_INIT(ip_vs_sed_scheduler.n_list),
	.schedule =		ip_vs_sed_schedule,
};


static int __init ip_vs_sed_init(void)
{
	return register_ip_vs_scheduler(&ip_vs_sed_scheduler);
}

static void __exit ip_vs_sed_cleanup(void)
{
	unregister_ip_vs_scheduler(&ip_vs_sed_scheduler);
}

module_init(ip_vs_sed_init);
module_exit(ip_vs_sed_cleanup);
MODULE_LICENSE("GPL");
