/*
 * IPVS:        Least-Connection Scheduling module
 *
 * Version:     $Id: ip_vs_lc.c,v 1.10 2003/04/18 09:03:16 wensong Exp $
 *
 * Authors:     Wensong Zhang <wensong@linuxvirtualserver.org>
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Changes:
 *     Wensong Zhang            :     added the ip_vs_lc_update_svc
 *     Wensong Zhang            :     added any dest with weight=0 is quiesced
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>

#include <net/ip_vs.h>


static int ip_vs_lc_init_svc(struct ip_vs_service *svc)
{
	return 0;
}


static int ip_vs_lc_done_svc(struct ip_vs_service *svc)
{
	return 0;
}


static int ip_vs_lc_update_svc(struct ip_vs_service *svc)
{
	return 0;
}


static inline unsigned int
ip_vs_lc_dest_overhead(struct ip_vs_dest *dest)
{
	/*
	 * We think the overhead of processing active connections is 256
	 * times higher than that of inactive connections in average. (This
	 * 256 times might not be accurate, we will change it later) We
	 * use the following formula to estimate the overhead now:
	 *		  dest->activeconns*256 + dest->inactconns
	 */
	return (atomic_read(&dest->activeconns) << 8) +
		atomic_read(&dest->inactconns);
}


/*
 *	Least Connection scheduling
 */
static struct ip_vs_dest *
ip_vs_lc_schedule(struct ip_vs_service *svc, const struct sk_buff *skb)
{
	struct ip_vs_dest *dest, *least = NULL;
	unsigned int loh = 0, doh;

	IP_VS_DBG(6, "ip_vs_lc_schedule(): Scheduling...\n");

	/*
	 * Simply select the server with the least number of
	 *        (activeconns<<5) + inactconns
	 * Except whose weight is equal to zero.
	 * If the weight is equal to zero, it means that the server is
	 * quiesced, the existing connections to the server still get
	 * served, but no new connection is assigned to the server.
	 */

	list_for_each_entry(dest, &svc->destinations, n_list) {
		if ((dest->flags & IP_VS_DEST_F_OVERLOAD) ||
		    atomic_read(&dest->weight) == 0)
			continue;
		doh = ip_vs_lc_dest_overhead(dest);
		if (!least || doh < loh) {
			least = dest;
			loh = doh;
		}
	}

	if (least)
	IP_VS_DBG(6, "LC: server %u.%u.%u.%u:%u activeconns %d inactconns %d\n",
		  NIPQUAD(least->addr), ntohs(least->port),
		  atomic_read(&least->activeconns),
		  atomic_read(&least->inactconns));

	return least;
}


static struct ip_vs_scheduler ip_vs_lc_scheduler = {
	.name =			"lc",
	.refcnt =		ATOMIC_INIT(0),
	.module =		THIS_MODULE,
	.init_service =		ip_vs_lc_init_svc,
	.done_service =		ip_vs_lc_done_svc,
	.update_service =	ip_vs_lc_update_svc,
	.schedule =		ip_vs_lc_schedule,
};


static int __init ip_vs_lc_init(void)
{
	INIT_LIST_HEAD(&ip_vs_lc_scheduler.n_list);
	return register_ip_vs_scheduler(&ip_vs_lc_scheduler) ;
}

static void __exit ip_vs_lc_cleanup(void)
{
	unregister_ip_vs_scheduler(&ip_vs_lc_scheduler);
}

module_init(ip_vs_lc_init);
module_exit(ip_vs_lc_cleanup);
MODULE_LICENSE("GPL");
