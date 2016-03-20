/*
 * IPVS:        Source Hashing scheduling module
 *
 * Authors:     Wensong Zhang <wensong@gnuchina.org>
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
 * The sh algorithm is to select server by the hash key of source IP
 * address. The pseudo code is as follows:
 *
 *       n <- servernode[src_ip];
 *       if (n is dead) OR
 *          (n is overloaded) or (n.weight <= 0) then
 *                 return NULL;
 *
 *       return n;
 *
 * Notes that servernode is a 256-bucket hash table that maps the hash
 * index derived from packet source IP address to the current server
 * array. If the sh scheduler is used in cache cluster, it is good to
 * combine it with cache_bypass feature. When the statically assigned
 * server is dead or overloaded, the load balancer can bypass the cache
 * server and send requests to the original server directly.
 *
 * The weight destination attribute can be used to control the
 * distribution of connections to the destinations in servernode. The
 * greater the weight, the more connections the destination
 * will receive.
 *
 */

#define KMSG_COMPONENT "IPVS"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/ip.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>

#include <net/ip_vs.h>

#include <net/tcp.h>
#include <linux/udp.h>
#include <linux/sctp.h>


/*
 *      IPVS SH bucket
 */
struct ip_vs_sh_bucket {
	struct ip_vs_dest __rcu	*dest;	/* real server (cache) */
};

/*
 *     for IPVS SH entry hash table
 */
#ifndef CONFIG_IP_VS_SH_TAB_BITS
#define CONFIG_IP_VS_SH_TAB_BITS        8
#endif
#define IP_VS_SH_TAB_BITS               CONFIG_IP_VS_SH_TAB_BITS
#define IP_VS_SH_TAB_SIZE               (1 << IP_VS_SH_TAB_BITS)
#define IP_VS_SH_TAB_MASK               (IP_VS_SH_TAB_SIZE - 1)

struct ip_vs_sh_state {
	struct rcu_head			rcu_head;
	struct ip_vs_sh_bucket		buckets[IP_VS_SH_TAB_SIZE];
};

/* Helper function to determine if server is unavailable */
static inline bool is_unavailable(struct ip_vs_dest *dest)
{
	return atomic_read(&dest->weight) <= 0 ||
	       dest->flags & IP_VS_DEST_F_OVERLOAD;
}

/*
 *	Returns hash value for IPVS SH entry
 */
static inline unsigned int
ip_vs_sh_hashkey(int af, const union nf_inet_addr *addr,
		 __be16 port, unsigned int offset)
{
	__be32 addr_fold = addr->ip;

#ifdef CONFIG_IP_VS_IPV6
	if (af == AF_INET6)
		addr_fold = addr->ip6[0]^addr->ip6[1]^
			    addr->ip6[2]^addr->ip6[3];
#endif
	return (offset + (ntohs(port) + ntohl(addr_fold))*2654435761UL) &
		IP_VS_SH_TAB_MASK;
}


/*
 *      Get ip_vs_dest associated with supplied parameters.
 */
static inline struct ip_vs_dest *
ip_vs_sh_get(struct ip_vs_service *svc, struct ip_vs_sh_state *s,
	     const union nf_inet_addr *addr, __be16 port)
{
	unsigned int hash = ip_vs_sh_hashkey(svc->af, addr, port, 0);
	struct ip_vs_dest *dest = rcu_dereference(s->buckets[hash].dest);

	return (!dest || is_unavailable(dest)) ? NULL : dest;
}


/* As ip_vs_sh_get, but with fallback if selected server is unavailable
 *
 * The fallback strategy loops around the table starting from a "random"
 * point (in fact, it is chosen to be the original hash value to make the
 * algorithm deterministic) to find a new server.
 */
static inline struct ip_vs_dest *
ip_vs_sh_get_fallback(struct ip_vs_service *svc, struct ip_vs_sh_state *s,
		      const union nf_inet_addr *addr, __be16 port)
{
	unsigned int offset, roffset;
	unsigned int hash, ihash;
	struct ip_vs_dest *dest;

	/* first try the dest it's supposed to go to */
	ihash = ip_vs_sh_hashkey(svc->af, addr, port, 0);
	dest = rcu_dereference(s->buckets[ihash].dest);
	if (!dest)
		return NULL;
	if (!is_unavailable(dest))
		return dest;

	IP_VS_DBG_BUF(6, "SH: selected unavailable server %s:%d, reselecting",
		      IP_VS_DBG_ADDR(dest->af, &dest->addr), ntohs(dest->port));

	/* if the original dest is unavailable, loop around the table
	 * starting from ihash to find a new dest
	 */
	for (offset = 0; offset < IP_VS_SH_TAB_SIZE; offset++) {
		roffset = (offset + ihash) % IP_VS_SH_TAB_SIZE;
		hash = ip_vs_sh_hashkey(svc->af, addr, port, roffset);
		dest = rcu_dereference(s->buckets[hash].dest);
		if (!dest)
			break;
		if (!is_unavailable(dest))
			return dest;
		IP_VS_DBG_BUF(6, "SH: selected unavailable "
			      "server %s:%d (offset %d), reselecting",
			      IP_VS_DBG_ADDR(dest->af, &dest->addr),
			      ntohs(dest->port), roffset);
	}

	return NULL;
}

/*
 *      Assign all the hash buckets of the specified table with the service.
 */
static int
ip_vs_sh_reassign(struct ip_vs_sh_state *s, struct ip_vs_service *svc)
{
	int i;
	struct ip_vs_sh_bucket *b;
	struct list_head *p;
	struct ip_vs_dest *dest;
	int d_count;
	bool empty;

	b = &s->buckets[0];
	p = &svc->destinations;
	empty = list_empty(p);
	d_count = 0;
	for (i=0; i<IP_VS_SH_TAB_SIZE; i++) {
		dest = rcu_dereference_protected(b->dest, 1);
		if (dest)
			ip_vs_dest_put(dest);
		if (empty)
			RCU_INIT_POINTER(b->dest, NULL);
		else {
			if (p == &svc->destinations)
				p = p->next;

			dest = list_entry(p, struct ip_vs_dest, n_list);
			ip_vs_dest_hold(dest);
			RCU_INIT_POINTER(b->dest, dest);

			IP_VS_DBG_BUF(6, "assigned i: %d dest: %s weight: %d\n",
				      i, IP_VS_DBG_ADDR(dest->af, &dest->addr),
				      atomic_read(&dest->weight));

			/* Don't move to next dest until filling weight */
			if (++d_count >= atomic_read(&dest->weight)) {
				p = p->next;
				d_count = 0;
			}

		}
		b++;
	}
	return 0;
}


/*
 *      Flush all the hash buckets of the specified table.
 */
static void ip_vs_sh_flush(struct ip_vs_sh_state *s)
{
	int i;
	struct ip_vs_sh_bucket *b;
	struct ip_vs_dest *dest;

	b = &s->buckets[0];
	for (i=0; i<IP_VS_SH_TAB_SIZE; i++) {
		dest = rcu_dereference_protected(b->dest, 1);
		if (dest) {
			ip_vs_dest_put(dest);
			RCU_INIT_POINTER(b->dest, NULL);
		}
		b++;
	}
}


static int ip_vs_sh_init_svc(struct ip_vs_service *svc)
{
	struct ip_vs_sh_state *s;

	/* allocate the SH table for this service */
	s = kzalloc(sizeof(struct ip_vs_sh_state), GFP_KERNEL);
	if (s == NULL)
		return -ENOMEM;

	svc->sched_data = s;
	IP_VS_DBG(6, "SH hash table (memory=%Zdbytes) allocated for "
		  "current service\n",
		  sizeof(struct ip_vs_sh_bucket)*IP_VS_SH_TAB_SIZE);

	/* assign the hash buckets with current dests */
	ip_vs_sh_reassign(s, svc);

	return 0;
}


static void ip_vs_sh_done_svc(struct ip_vs_service *svc)
{
	struct ip_vs_sh_state *s = svc->sched_data;

	/* got to clean up hash buckets here */
	ip_vs_sh_flush(s);

	/* release the table itself */
	kfree_rcu(s, rcu_head);
	IP_VS_DBG(6, "SH hash table (memory=%Zdbytes) released\n",
		  sizeof(struct ip_vs_sh_bucket)*IP_VS_SH_TAB_SIZE);
}


static int ip_vs_sh_dest_changed(struct ip_vs_service *svc,
				 struct ip_vs_dest *dest)
{
	struct ip_vs_sh_state *s = svc->sched_data;

	/* assign the hash buckets with the updated service */
	ip_vs_sh_reassign(s, svc);

	return 0;
}


/* Helper function to get port number */
static inline __be16
ip_vs_sh_get_port(const struct sk_buff *skb, struct ip_vs_iphdr *iph)
{
	__be16 _ports[2], *ports;

	/* At this point we know that we have a valid packet of some kind.
	 * Because ICMP packets are only guaranteed to have the first 8
	 * bytes, let's just grab the ports.  Fortunately they're in the
	 * same position for all three of the protocols we care about.
	 */
	switch (iph->protocol) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
	case IPPROTO_SCTP:
		ports = skb_header_pointer(skb, iph->len, sizeof(_ports),
					   &_ports);
		if (unlikely(!ports))
			return 0;

		if (likely(!ip_vs_iph_inverse(iph)))
			return ports[0];
		else
			return ports[1];
	default:
		return 0;
	}
}


/*
 *      Source Hashing scheduling
 */
static struct ip_vs_dest *
ip_vs_sh_schedule(struct ip_vs_service *svc, const struct sk_buff *skb,
		  struct ip_vs_iphdr *iph)
{
	struct ip_vs_dest *dest;
	struct ip_vs_sh_state *s;
	__be16 port = 0;
	const union nf_inet_addr *hash_addr;

	hash_addr = ip_vs_iph_inverse(iph) ? &iph->daddr : &iph->saddr;

	IP_VS_DBG(6, "ip_vs_sh_schedule(): Scheduling...\n");

	if (svc->flags & IP_VS_SVC_F_SCHED_SH_PORT)
		port = ip_vs_sh_get_port(skb, iph);

	s = (struct ip_vs_sh_state *) svc->sched_data;

	if (svc->flags & IP_VS_SVC_F_SCHED_SH_FALLBACK)
		dest = ip_vs_sh_get_fallback(svc, s, hash_addr, port);
	else
		dest = ip_vs_sh_get(svc, s, hash_addr, port);

	if (!dest) {
		ip_vs_scheduler_err(svc, "no destination available");
		return NULL;
	}

	IP_VS_DBG_BUF(6, "SH: source IP address %s --> server %s:%d\n",
		      IP_VS_DBG_ADDR(svc->af, hash_addr),
		      IP_VS_DBG_ADDR(dest->af, &dest->addr),
		      ntohs(dest->port));

	return dest;
}


/*
 *      IPVS SH Scheduler structure
 */
static struct ip_vs_scheduler ip_vs_sh_scheduler =
{
	.name =			"sh",
	.refcnt =		ATOMIC_INIT(0),
	.module =		THIS_MODULE,
	.n_list	 =		LIST_HEAD_INIT(ip_vs_sh_scheduler.n_list),
	.init_service =		ip_vs_sh_init_svc,
	.done_service =		ip_vs_sh_done_svc,
	.add_dest =		ip_vs_sh_dest_changed,
	.del_dest =		ip_vs_sh_dest_changed,
	.upd_dest =		ip_vs_sh_dest_changed,
	.schedule =		ip_vs_sh_schedule,
};


static int __init ip_vs_sh_init(void)
{
	return register_ip_vs_scheduler(&ip_vs_sh_scheduler);
}


static void __exit ip_vs_sh_cleanup(void)
{
	unregister_ip_vs_scheduler(&ip_vs_sh_scheduler);
	synchronize_rcu();
}


module_init(ip_vs_sh_init);
module_exit(ip_vs_sh_cleanup);
MODULE_LICENSE("GPL");
