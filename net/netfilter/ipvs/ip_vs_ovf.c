// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * IPVS:        Overflow-Connection Scheduling module
 *
 * Authors:     Raducu Deaconu <rhadoo_io@yahoo.com>
 *
 * Scheduler implements "overflow" loadbalancing according to number of active
 * connections , will keep all conections to the node with the highest weight
 * and overflow to the next node if the number of connections exceeds the node's
 * weight.
 * Note that this scheduler might not be suitable for UDP because it only uses
 * active connections
 */

#define KMSG_COMPONENT "IPVS"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>

#include <net/ip_vs.h>

/* OVF Connection scheduling  */
static struct ip_vs_dest *
ip_vs_ovf_schedule(struct ip_vs_service *svc, const struct sk_buff *skb,
		   struct ip_vs_iphdr *iph)
{
	struct ip_vs_dest *dest, *h = NULL;
	int hw = 0, w;

	IP_VS_DBG(6, "ip_vs_ovf_schedule(): Scheduling...\n");
	/* select the node with highest weight, go to next in line if active
	* connections exceed weight
	*/
	list_for_each_entry_rcu(dest, &svc->destinations, n_list) {
		w = atomic_read(&dest->weight);
		if ((dest->flags & IP_VS_DEST_F_OVERLOAD) ||
		    atomic_read(&dest->activeconns) > w ||
		    w == 0)
			continue;
		if (!h || w > hw) {
			h = dest;
			hw = w;
		}
	}

	if (h) {
		IP_VS_DBG_BUF(6, "OVF: server %s:%u active %d w %d\n",
			      IP_VS_DBG_ADDR(h->af, &h->addr),
			      ntohs(h->port),
			      atomic_read(&h->activeconns),
			      atomic_read(&h->weight));
		return h;
	}

	ip_vs_scheduler_err(svc, "no destination available");
	return NULL;
}

static struct ip_vs_scheduler ip_vs_ovf_scheduler = {
	.name =			"ovf",
	.refcnt =		ATOMIC_INIT(0),
	.module =		THIS_MODULE,
	.n_list =		LIST_HEAD_INIT(ip_vs_ovf_scheduler.n_list),
	.schedule =		ip_vs_ovf_schedule,
};

static int __init ip_vs_ovf_init(void)
{
	return register_ip_vs_scheduler(&ip_vs_ovf_scheduler);
}

static void __exit ip_vs_ovf_cleanup(void)
{
	unregister_ip_vs_scheduler(&ip_vs_ovf_scheduler);
	synchronize_rcu();
}

module_init(ip_vs_ovf_init);
module_exit(ip_vs_ovf_cleanup);
MODULE_LICENSE("GPL");
