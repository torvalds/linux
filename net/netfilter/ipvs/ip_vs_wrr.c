/*
 * IPVS:        Weighted Round-Robin Scheduling module
 *
 * Authors:     Wensong Zhang <wensong@linuxvirtualserver.org>
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Changes:
 *     Wensong Zhang            :     changed the ip_vs_wrr_schedule to return dest
 *     Wensong Zhang            :     changed some comestics things for debugging
 *     Wensong Zhang            :     changed for the d-linked destination list
 *     Wensong Zhang            :     added the ip_vs_wrr_update_svc
 *     Julian Anastasov         :     fixed the bug of returning destination
 *                                    with weight 0 when all weights are zero
 *
 */

#define KMSG_COMPONENT "IPVS"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/net.h>
#include <linux/gcd.h>

#include <net/ip_vs.h>

/*
 * current destination pointer for weighted round-robin scheduling
 */
struct ip_vs_wrr_mark {
	struct list_head *cl;	/* current list head */
	int cw;			/* current weight */
	int mw;			/* maximum weight */
	int di;			/* decreasing interval */
};


static int ip_vs_wrr_gcd_weight(struct ip_vs_service *svc)
{
	struct ip_vs_dest *dest;
	int weight;
	int g = 0;

	list_for_each_entry(dest, &svc->destinations, n_list) {
		weight = atomic_read(&dest->weight);
		if (weight > 0) {
			if (g > 0)
				g = gcd(weight, g);
			else
				g = weight;
		}
	}
	return g ? g : 1;
}


/*
 *    Get the maximum weight of the service destinations.
 */
static int ip_vs_wrr_max_weight(struct ip_vs_service *svc)
{
	struct ip_vs_dest *dest;
	int new_weight, weight = 0;

	list_for_each_entry(dest, &svc->destinations, n_list) {
		new_weight = atomic_read(&dest->weight);
		if (new_weight > weight)
			weight = new_weight;
	}

	return weight;
}


static int ip_vs_wrr_init_svc(struct ip_vs_service *svc)
{
	struct ip_vs_wrr_mark *mark;

	/*
	 *    Allocate the mark variable for WRR scheduling
	 */
	mark = kmalloc(sizeof(struct ip_vs_wrr_mark), GFP_ATOMIC);
	if (mark == NULL) {
		pr_err("%s(): no memory\n", __func__);
		return -ENOMEM;
	}
	mark->cl = &svc->destinations;
	mark->cw = 0;
	mark->mw = ip_vs_wrr_max_weight(svc);
	mark->di = ip_vs_wrr_gcd_weight(svc);
	svc->sched_data = mark;

	return 0;
}


static int ip_vs_wrr_done_svc(struct ip_vs_service *svc)
{
	/*
	 *    Release the mark variable
	 */
	kfree(svc->sched_data);

	return 0;
}


static int ip_vs_wrr_update_svc(struct ip_vs_service *svc)
{
	struct ip_vs_wrr_mark *mark = svc->sched_data;

	mark->cl = &svc->destinations;
	mark->mw = ip_vs_wrr_max_weight(svc);
	mark->di = ip_vs_wrr_gcd_weight(svc);
	if (mark->cw > mark->mw)
		mark->cw = 0;
	return 0;
}


/*
 *    Weighted Round-Robin Scheduling
 */
static struct ip_vs_dest *
ip_vs_wrr_schedule(struct ip_vs_service *svc, const struct sk_buff *skb)
{
	struct ip_vs_dest *dest;
	struct ip_vs_wrr_mark *mark = svc->sched_data;
	struct list_head *p;

	IP_VS_DBG(6, "%s(): Scheduling...\n", __func__);

	/*
	 * This loop will always terminate, because mark->cw in (0, max_weight]
	 * and at least one server has its weight equal to max_weight.
	 */
	write_lock(&svc->sched_lock);
	p = mark->cl;
	while (1) {
		if (mark->cl == &svc->destinations) {
			/* it is at the head of the destination list */

			if (mark->cl == mark->cl->next) {
				/* no dest entry */
				ip_vs_scheduler_err(svc,
					"no destination available: "
					"no destinations present");
				dest = NULL;
				goto out;
			}

			mark->cl = svc->destinations.next;
			mark->cw -= mark->di;
			if (mark->cw <= 0) {
				mark->cw = mark->mw;
				/*
				 * Still zero, which means no available servers.
				 */
				if (mark->cw == 0) {
					mark->cl = &svc->destinations;
					ip_vs_scheduler_err(svc,
						"no destination available");
					dest = NULL;
					goto out;
				}
			}
		} else
			mark->cl = mark->cl->next;

		if (mark->cl != &svc->destinations) {
			/* not at the head of the list */
			dest = list_entry(mark->cl, struct ip_vs_dest, n_list);
			if (!(dest->flags & IP_VS_DEST_F_OVERLOAD) &&
			    atomic_read(&dest->weight) >= mark->cw) {
				/* got it */
				break;
			}
		}

		if (mark->cl == p && mark->cw == mark->di) {
			/* back to the start, and no dest is found.
			   It is only possible when all dests are OVERLOADED */
			dest = NULL;
			ip_vs_scheduler_err(svc,
				"no destination available: "
				"all destinations are overloaded");
			goto out;
		}
	}

	IP_VS_DBG_BUF(6, "WRR: server %s:%u "
		      "activeconns %d refcnt %d weight %d\n",
		      IP_VS_DBG_ADDR(svc->af, &dest->addr), ntohs(dest->port),
		      atomic_read(&dest->activeconns),
		      atomic_read(&dest->refcnt),
		      atomic_read(&dest->weight));

  out:
	write_unlock(&svc->sched_lock);
	return dest;
}


static struct ip_vs_scheduler ip_vs_wrr_scheduler = {
	.name =			"wrr",
	.refcnt =		ATOMIC_INIT(0),
	.module =		THIS_MODULE,
	.n_list =		LIST_HEAD_INIT(ip_vs_wrr_scheduler.n_list),
	.init_service =		ip_vs_wrr_init_svc,
	.done_service =		ip_vs_wrr_done_svc,
	.update_service =	ip_vs_wrr_update_svc,
	.schedule =		ip_vs_wrr_schedule,
};

static int __init ip_vs_wrr_init(void)
{
	return register_ip_vs_scheduler(&ip_vs_wrr_scheduler) ;
}

static void __exit ip_vs_wrr_cleanup(void)
{
	unregister_ip_vs_scheduler(&ip_vs_wrr_scheduler);
}

module_init(ip_vs_wrr_init);
module_exit(ip_vs_wrr_cleanup);
MODULE_LICENSE("GPL");
