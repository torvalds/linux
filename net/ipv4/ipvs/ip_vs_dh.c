/*
 * IPVS:        Destination Hashing scheduling module
 *
 * Version:     $Id: ip_vs_dh.c,v 1.5 2002/09/15 08:14:08 wensong Exp $
 *
 * Authors:     Wensong Zhang <wensong@gnuchina.org>
 *
 *              Inspired by the consistent hashing scheduler patch from
 *              Thomas Proell <proellt@gmx.de>
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
 * The dh algorithm is to select server by the hash key of destination IP
 * address. The pseudo code is as follows:
 *
 *       n <- servernode[dest_ip];
 *       if (n is dead) OR
 *          (n is overloaded) OR (n.weight <= 0) then
 *                 return NULL;
 *
 *       return n;
 *
 * Notes that servernode is a 256-bucket hash table that maps the hash
 * index derived from packet destination IP address to the current server
 * array. If the dh scheduler is used in cache cluster, it is good to
 * combine it with cache_bypass feature. When the statically assigned
 * server is dead or overloaded, the load balancer can bypass the cache
 * server and send requests to the original server directly.
 *
 */

#include <linux/ip.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>

#include <net/ip_vs.h>


/*
 *      IPVS DH bucket
 */
struct ip_vs_dh_bucket {
	struct ip_vs_dest       *dest;          /* real server (cache) */
};

/*
 *     for IPVS DH entry hash table
 */
#ifndef CONFIG_IP_VS_DH_TAB_BITS
#define CONFIG_IP_VS_DH_TAB_BITS        8
#endif
#define IP_VS_DH_TAB_BITS               CONFIG_IP_VS_DH_TAB_BITS
#define IP_VS_DH_TAB_SIZE               (1 << IP_VS_DH_TAB_BITS)
#define IP_VS_DH_TAB_MASK               (IP_VS_DH_TAB_SIZE - 1)


/*
 *	Returns hash value for IPVS DH entry
 */
static inline unsigned ip_vs_dh_hashkey(__be32 addr)
{
	return (ntohl(addr)*2654435761UL) & IP_VS_DH_TAB_MASK;
}


/*
 *      Get ip_vs_dest associated with supplied parameters.
 */
static inline struct ip_vs_dest *
ip_vs_dh_get(struct ip_vs_dh_bucket *tbl, __be32 addr)
{
	return (tbl[ip_vs_dh_hashkey(addr)]).dest;
}


/*
 *      Assign all the hash buckets of the specified table with the service.
 */
static int
ip_vs_dh_assign(struct ip_vs_dh_bucket *tbl, struct ip_vs_service *svc)
{
	int i;
	struct ip_vs_dh_bucket *b;
	struct list_head *p;
	struct ip_vs_dest *dest;

	b = tbl;
	p = &svc->destinations;
	for (i=0; i<IP_VS_DH_TAB_SIZE; i++) {
		if (list_empty(p)) {
			b->dest = NULL;
		} else {
			if (p == &svc->destinations)
				p = p->next;

			dest = list_entry(p, struct ip_vs_dest, n_list);
			atomic_inc(&dest->refcnt);
			b->dest = dest;

			p = p->next;
		}
		b++;
	}
	return 0;
}


/*
 *      Flush all the hash buckets of the specified table.
 */
static void ip_vs_dh_flush(struct ip_vs_dh_bucket *tbl)
{
	int i;
	struct ip_vs_dh_bucket *b;

	b = tbl;
	for (i=0; i<IP_VS_DH_TAB_SIZE; i++) {
		if (b->dest) {
			atomic_dec(&b->dest->refcnt);
			b->dest = NULL;
		}
		b++;
	}
}


static int ip_vs_dh_init_svc(struct ip_vs_service *svc)
{
	struct ip_vs_dh_bucket *tbl;

	/* allocate the DH table for this service */
	tbl = kmalloc(sizeof(struct ip_vs_dh_bucket)*IP_VS_DH_TAB_SIZE,
		      GFP_ATOMIC);
	if (tbl == NULL) {
		IP_VS_ERR("ip_vs_dh_init_svc(): no memory\n");
		return -ENOMEM;
	}
	svc->sched_data = tbl;
	IP_VS_DBG(6, "DH hash table (memory=%Zdbytes) allocated for "
		  "current service\n",
		  sizeof(struct ip_vs_dh_bucket)*IP_VS_DH_TAB_SIZE);

	/* assign the hash buckets with the updated service */
	ip_vs_dh_assign(tbl, svc);

	return 0;
}


static int ip_vs_dh_done_svc(struct ip_vs_service *svc)
{
	struct ip_vs_dh_bucket *tbl = svc->sched_data;

	/* got to clean up hash buckets here */
	ip_vs_dh_flush(tbl);

	/* release the table itself */
	kfree(svc->sched_data);
	IP_VS_DBG(6, "DH hash table (memory=%Zdbytes) released\n",
		  sizeof(struct ip_vs_dh_bucket)*IP_VS_DH_TAB_SIZE);

	return 0;
}


static int ip_vs_dh_update_svc(struct ip_vs_service *svc)
{
	struct ip_vs_dh_bucket *tbl = svc->sched_data;

	/* got to clean up hash buckets here */
	ip_vs_dh_flush(tbl);

	/* assign the hash buckets with the updated service */
	ip_vs_dh_assign(tbl, svc);

	return 0;
}


/*
 *      If the dest flags is set with IP_VS_DEST_F_OVERLOAD,
 *      consider that the server is overloaded here.
 */
static inline int is_overloaded(struct ip_vs_dest *dest)
{
	return dest->flags & IP_VS_DEST_F_OVERLOAD;
}


/*
 *      Destination hashing scheduling
 */
static struct ip_vs_dest *
ip_vs_dh_schedule(struct ip_vs_service *svc, const struct sk_buff *skb)
{
	struct ip_vs_dest *dest;
	struct ip_vs_dh_bucket *tbl;
	struct iphdr *iph = skb->nh.iph;

	IP_VS_DBG(6, "ip_vs_dh_schedule(): Scheduling...\n");

	tbl = (struct ip_vs_dh_bucket *)svc->sched_data;
	dest = ip_vs_dh_get(tbl, iph->daddr);
	if (!dest
	    || !(dest->flags & IP_VS_DEST_F_AVAILABLE)
	    || atomic_read(&dest->weight) <= 0
	    || is_overloaded(dest)) {
		return NULL;
	}

	IP_VS_DBG(6, "DH: destination IP address %u.%u.%u.%u "
		  "--> server %u.%u.%u.%u:%d\n",
		  NIPQUAD(iph->daddr),
		  NIPQUAD(dest->addr),
		  ntohs(dest->port));

	return dest;
}


/*
 *      IPVS DH Scheduler structure
 */
static struct ip_vs_scheduler ip_vs_dh_scheduler =
{
	.name =			"dh",
	.refcnt =		ATOMIC_INIT(0),
	.module =		THIS_MODULE,
	.init_service =		ip_vs_dh_init_svc,
	.done_service =		ip_vs_dh_done_svc,
	.update_service =	ip_vs_dh_update_svc,
	.schedule =		ip_vs_dh_schedule,
};


static int __init ip_vs_dh_init(void)
{
	INIT_LIST_HEAD(&ip_vs_dh_scheduler.n_list);
	return register_ip_vs_scheduler(&ip_vs_dh_scheduler);
}


static void __exit ip_vs_dh_cleanup(void)
{
	unregister_ip_vs_scheduler(&ip_vs_dh_scheduler);
}


module_init(ip_vs_dh_init);
module_exit(ip_vs_dh_cleanup);
MODULE_LICENSE("GPL");
