// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * IPVS:        Weighted Fail Over module
 *
 * Authors:     Kenny Mathis <kmathis@chokepoint.net>
 *
 * Changes:
 *     Kenny Mathis            :     added initial functionality based on weight
 */

#define KMSG_COMPONENT "IPVS"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>

#include <net/ip_vs.h>

/* Weighted Fail Over Module */
static struct ip_vs_dest *
ip_vs_fo_schedule(struct ip_vs_service *svc, const struct sk_buff *skb,
		  struct ip_vs_iphdr *iph)
{
	struct ip_vs_dest *dest, *hweight = NULL;
	int hw = 0; /* Track highest weight */

	IP_VS_DBG(6, "ip_vs_fo_schedule(): Scheduling...\n");

	/* Basic failover functionality
	 * Find virtual server with highest weight and send it traffic
	 */
	list_for_each_entry_rcu(dest, &svc->destinations, n_list) {
		if (!(dest->flags & IP_VS_DEST_F_OVERLOAD) &&
		    atomic_read(&dest->weight) > hw) {
			hweight = dest;
			hw = atomic_read(&dest->weight);
		}
	}

	if (hweight) {
		IP_VS_DBG_BUF(6, "FO: server %s:%u activeconns %d weight %d\n",
			      IP_VS_DBG_ADDR(hweight->af, &hweight->addr),
			      ntohs(hweight->port),
			      atomic_read(&hweight->activeconns),
			      atomic_read(&hweight->weight));
		return hweight;
	}

	ip_vs_scheduler_err(svc, "no destination available");
	return NULL;
}

static struct ip_vs_scheduler ip_vs_fo_scheduler = {
	.name =			"fo",
	.refcnt =		ATOMIC_INIT(0),
	.module =		THIS_MODULE,
	.n_list =		LIST_HEAD_INIT(ip_vs_fo_scheduler.n_list),
	.schedule =		ip_vs_fo_schedule,
};

static int __init ip_vs_fo_init(void)
{
	return register_ip_vs_scheduler(&ip_vs_fo_scheduler);
}

static void __exit ip_vs_fo_cleanup(void)
{
	unregister_ip_vs_scheduler(&ip_vs_fo_scheduler);
	synchronize_rcu();
}

module_init(ip_vs_fo_init);
module_exit(ip_vs_fo_cleanup);
MODULE_LICENSE("GPL");
