/*
 * IPVS:        Locality-Based Least-Connection with Replication scheduler
 *
 * Authors:     Wensong Zhang <wensong@gnuchina.org>
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Changes:
 *     Julian Anastasov        :    Added the missing (dest->weight>0)
 *                                  condition in the ip_vs_dest_set_max.
 *
 */

/*
 * The lblc/r algorithm is as follows (pseudo code):
 *
 *       if serverSet[dest_ip] is null then
 *               n, serverSet[dest_ip] <- {weighted least-conn node};
 *       else
 *               n <- {least-conn (alive) node in serverSet[dest_ip]};
 *               if (n is null) OR
 *                  (n.conns>n.weight AND
 *                   there is a node m with m.conns<m.weight/2) then
 *                   n <- {weighted least-conn node};
 *                   add n to serverSet[dest_ip];
 *               if |serverSet[dest_ip]| > 1 AND
 *                   now - serverSet[dest_ip].lastMod > T then
 *                   m <- {most conn node in serverSet[dest_ip]};
 *                   remove m from serverSet[dest_ip];
 *       if serverSet[dest_ip] changed then
 *               serverSet[dest_ip].lastMod <- now;
 *
 *       return n;
 *
 */

#define KMSG_COMPONENT "IPVS"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/ip.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/jiffies.h>
#include <linux/list.h>
#include <linux/slab.h>

/* for sysctl */
#include <linux/fs.h>
#include <linux/sysctl.h>
#include <net/net_namespace.h>

#include <net/ip_vs.h>


/*
 *    It is for garbage collection of stale IPVS lblcr entries,
 *    when the table is full.
 */
#define CHECK_EXPIRE_INTERVAL   (60*HZ)
#define ENTRY_TIMEOUT           (6*60*HZ)

#define DEFAULT_EXPIRATION	(24*60*60*HZ)

/*
 *    It is for full expiration check.
 *    When there is no partial expiration check (garbage collection)
 *    in a half hour, do a full expiration check to collect stale
 *    entries that haven't been touched for a day.
 */
#define COUNT_FOR_FULL_EXPIRATION   30

/*
 *     for IPVS lblcr entry hash table
 */
#ifndef CONFIG_IP_VS_LBLCR_TAB_BITS
#define CONFIG_IP_VS_LBLCR_TAB_BITS      10
#endif
#define IP_VS_LBLCR_TAB_BITS     CONFIG_IP_VS_LBLCR_TAB_BITS
#define IP_VS_LBLCR_TAB_SIZE     (1 << IP_VS_LBLCR_TAB_BITS)
#define IP_VS_LBLCR_TAB_MASK     (IP_VS_LBLCR_TAB_SIZE - 1)


/*
 *      IPVS destination set structure and operations
 */
struct ip_vs_dest_set_elem {
	struct list_head	list;          /* list link */
	struct ip_vs_dest	*dest;		/* destination server */
	struct rcu_head		rcu_head;
};

struct ip_vs_dest_set {
	atomic_t                size;           /* set size */
	unsigned long           lastmod;        /* last modified time */
	struct list_head	list;           /* destination list */
};


static void ip_vs_dest_set_insert(struct ip_vs_dest_set *set,
				  struct ip_vs_dest *dest, bool check)
{
	struct ip_vs_dest_set_elem *e;

	if (check) {
		list_for_each_entry(e, &set->list, list) {
			if (e->dest == dest)
				return;
		}
	}

	e = kmalloc(sizeof(*e), GFP_ATOMIC);
	if (e == NULL)
		return;

	ip_vs_dest_hold(dest);
	e->dest = dest;

	list_add_rcu(&e->list, &set->list);
	atomic_inc(&set->size);

	set->lastmod = jiffies;
}

static void ip_vs_lblcr_elem_rcu_free(struct rcu_head *head)
{
	struct ip_vs_dest_set_elem *e;

	e = container_of(head, struct ip_vs_dest_set_elem, rcu_head);
	ip_vs_dest_put_and_free(e->dest);
	kfree(e);
}

static void
ip_vs_dest_set_erase(struct ip_vs_dest_set *set, struct ip_vs_dest *dest)
{
	struct ip_vs_dest_set_elem *e;

	list_for_each_entry(e, &set->list, list) {
		if (e->dest == dest) {
			/* HIT */
			atomic_dec(&set->size);
			set->lastmod = jiffies;
			list_del_rcu(&e->list);
			call_rcu(&e->rcu_head, ip_vs_lblcr_elem_rcu_free);
			break;
		}
	}
}

static void ip_vs_dest_set_eraseall(struct ip_vs_dest_set *set)
{
	struct ip_vs_dest_set_elem *e, *ep;

	list_for_each_entry_safe(e, ep, &set->list, list) {
		list_del_rcu(&e->list);
		call_rcu(&e->rcu_head, ip_vs_lblcr_elem_rcu_free);
	}
}

/* get weighted least-connection node in the destination set */
static inline struct ip_vs_dest *ip_vs_dest_set_min(struct ip_vs_dest_set *set)
{
	register struct ip_vs_dest_set_elem *e;
	struct ip_vs_dest *dest, *least;
	int loh, doh;

	/* select the first destination server, whose weight > 0 */
	list_for_each_entry_rcu(e, &set->list, list) {
		least = e->dest;
		if (least->flags & IP_VS_DEST_F_OVERLOAD)
			continue;

		if ((atomic_read(&least->weight) > 0)
		    && (least->flags & IP_VS_DEST_F_AVAILABLE)) {
			loh = ip_vs_dest_conn_overhead(least);
			goto nextstage;
		}
	}
	return NULL;

	/* find the destination with the weighted least load */
  nextstage:
	list_for_each_entry_continue_rcu(e, &set->list, list) {
		dest = e->dest;
		if (dest->flags & IP_VS_DEST_F_OVERLOAD)
			continue;

		doh = ip_vs_dest_conn_overhead(dest);
		if (((__s64)loh * atomic_read(&dest->weight) >
		     (__s64)doh * atomic_read(&least->weight))
		    && (dest->flags & IP_VS_DEST_F_AVAILABLE)) {
			least = dest;
			loh = doh;
		}
	}

	IP_VS_DBG_BUF(6, "%s(): server %s:%d "
		      "activeconns %d refcnt %d weight %d overhead %d\n",
		      __func__,
		      IP_VS_DBG_ADDR(least->af, &least->addr),
		      ntohs(least->port),
		      atomic_read(&least->activeconns),
		      refcount_read(&least->refcnt),
		      atomic_read(&least->weight), loh);
	return least;
}


/* get weighted most-connection node in the destination set */
static inline struct ip_vs_dest *ip_vs_dest_set_max(struct ip_vs_dest_set *set)
{
	register struct ip_vs_dest_set_elem *e;
	struct ip_vs_dest *dest, *most;
	int moh, doh;

	if (set == NULL)
		return NULL;

	/* select the first destination server, whose weight > 0 */
	list_for_each_entry(e, &set->list, list) {
		most = e->dest;
		if (atomic_read(&most->weight) > 0) {
			moh = ip_vs_dest_conn_overhead(most);
			goto nextstage;
		}
	}
	return NULL;

	/* find the destination with the weighted most load */
  nextstage:
	list_for_each_entry_continue(e, &set->list, list) {
		dest = e->dest;
		doh = ip_vs_dest_conn_overhead(dest);
		/* moh/mw < doh/dw ==> moh*dw < doh*mw, where mw,dw>0 */
		if (((__s64)moh * atomic_read(&dest->weight) <
		     (__s64)doh * atomic_read(&most->weight))
		    && (atomic_read(&dest->weight) > 0)) {
			most = dest;
			moh = doh;
		}
	}

	IP_VS_DBG_BUF(6, "%s(): server %s:%d "
		      "activeconns %d refcnt %d weight %d overhead %d\n",
		      __func__,
		      IP_VS_DBG_ADDR(most->af, &most->addr), ntohs(most->port),
		      atomic_read(&most->activeconns),
		      refcount_read(&most->refcnt),
		      atomic_read(&most->weight), moh);
	return most;
}


/*
 *      IPVS lblcr entry represents an association between destination
 *      IP address and its destination server set
 */
struct ip_vs_lblcr_entry {
	struct hlist_node       list;
	int			af;		/* address family */
	union nf_inet_addr      addr;           /* destination IP address */
	struct ip_vs_dest_set   set;            /* destination server set */
	unsigned long           lastuse;        /* last used time */
	struct rcu_head		rcu_head;
};


/*
 *      IPVS lblcr hash table
 */
struct ip_vs_lblcr_table {
	struct rcu_head		rcu_head;
	struct hlist_head	bucket[IP_VS_LBLCR_TAB_SIZE];  /* hash bucket */
	atomic_t                entries;        /* number of entries */
	int                     max_size;       /* maximum size of entries */
	struct timer_list       periodic_timer; /* collect stale entries */
	int                     rover;          /* rover for expire check */
	int                     counter;        /* counter for no expire */
	bool			dead;
};


#ifdef CONFIG_SYSCTL
/*
 *      IPVS LBLCR sysctl table
 */

static struct ctl_table vs_vars_table[] = {
	{
		.procname	= "lblcr_expiration",
		.data		= NULL,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{ }
};
#endif

static inline void ip_vs_lblcr_free(struct ip_vs_lblcr_entry *en)
{
	hlist_del_rcu(&en->list);
	ip_vs_dest_set_eraseall(&en->set);
	kfree_rcu(en, rcu_head);
}


/*
 *	Returns hash value for IPVS LBLCR entry
 */
static inline unsigned int
ip_vs_lblcr_hashkey(int af, const union nf_inet_addr *addr)
{
	__be32 addr_fold = addr->ip;

#ifdef CONFIG_IP_VS_IPV6
	if (af == AF_INET6)
		addr_fold = addr->ip6[0]^addr->ip6[1]^
			    addr->ip6[2]^addr->ip6[3];
#endif
	return (ntohl(addr_fold)*2654435761UL) & IP_VS_LBLCR_TAB_MASK;
}


/*
 *	Hash an entry in the ip_vs_lblcr_table.
 *	returns bool success.
 */
static void
ip_vs_lblcr_hash(struct ip_vs_lblcr_table *tbl, struct ip_vs_lblcr_entry *en)
{
	unsigned int hash = ip_vs_lblcr_hashkey(en->af, &en->addr);

	hlist_add_head_rcu(&en->list, &tbl->bucket[hash]);
	atomic_inc(&tbl->entries);
}


/* Get ip_vs_lblcr_entry associated with supplied parameters. */
static inline struct ip_vs_lblcr_entry *
ip_vs_lblcr_get(int af, struct ip_vs_lblcr_table *tbl,
		const union nf_inet_addr *addr)
{
	unsigned int hash = ip_vs_lblcr_hashkey(af, addr);
	struct ip_vs_lblcr_entry *en;

	hlist_for_each_entry_rcu(en, &tbl->bucket[hash], list)
		if (ip_vs_addr_equal(af, &en->addr, addr))
			return en;

	return NULL;
}


/*
 * Create or update an ip_vs_lblcr_entry, which is a mapping of a destination
 * IP address to a server. Called under spin lock.
 */
static inline struct ip_vs_lblcr_entry *
ip_vs_lblcr_new(struct ip_vs_lblcr_table *tbl, const union nf_inet_addr *daddr,
		u16 af, struct ip_vs_dest *dest)
{
	struct ip_vs_lblcr_entry *en;

	en = ip_vs_lblcr_get(af, tbl, daddr);
	if (!en) {
		en = kmalloc(sizeof(*en), GFP_ATOMIC);
		if (!en)
			return NULL;

		en->af = af;
		ip_vs_addr_copy(af, &en->addr, daddr);
		en->lastuse = jiffies;

		/* initialize its dest set */
		atomic_set(&(en->set.size), 0);
		INIT_LIST_HEAD(&en->set.list);

		ip_vs_dest_set_insert(&en->set, dest, false);

		ip_vs_lblcr_hash(tbl, en);
		return en;
	}

	ip_vs_dest_set_insert(&en->set, dest, true);

	return en;
}


/*
 *      Flush all the entries of the specified table.
 */
static void ip_vs_lblcr_flush(struct ip_vs_service *svc)
{
	struct ip_vs_lblcr_table *tbl = svc->sched_data;
	int i;
	struct ip_vs_lblcr_entry *en;
	struct hlist_node *next;

	spin_lock_bh(&svc->sched_lock);
	tbl->dead = 1;
	for (i = 0; i < IP_VS_LBLCR_TAB_SIZE; i++) {
		hlist_for_each_entry_safe(en, next, &tbl->bucket[i], list) {
			ip_vs_lblcr_free(en);
		}
	}
	spin_unlock_bh(&svc->sched_lock);
}

static int sysctl_lblcr_expiration(struct ip_vs_service *svc)
{
#ifdef CONFIG_SYSCTL
	return svc->ipvs->sysctl_lblcr_expiration;
#else
	return DEFAULT_EXPIRATION;
#endif
}

static inline void ip_vs_lblcr_full_check(struct ip_vs_service *svc)
{
	struct ip_vs_lblcr_table *tbl = svc->sched_data;
	unsigned long now = jiffies;
	int i, j;
	struct ip_vs_lblcr_entry *en;
	struct hlist_node *next;

	for (i = 0, j = tbl->rover; i < IP_VS_LBLCR_TAB_SIZE; i++) {
		j = (j + 1) & IP_VS_LBLCR_TAB_MASK;

		spin_lock(&svc->sched_lock);
		hlist_for_each_entry_safe(en, next, &tbl->bucket[j], list) {
			if (time_after(en->lastuse +
				       sysctl_lblcr_expiration(svc), now))
				continue;

			ip_vs_lblcr_free(en);
			atomic_dec(&tbl->entries);
		}
		spin_unlock(&svc->sched_lock);
	}
	tbl->rover = j;
}


/*
 *      Periodical timer handler for IPVS lblcr table
 *      It is used to collect stale entries when the number of entries
 *      exceeds the maximum size of the table.
 *
 *      Fixme: we probably need more complicated algorithm to collect
 *             entries that have not been used for a long time even
 *             if the number of entries doesn't exceed the maximum size
 *             of the table.
 *      The full expiration check is for this purpose now.
 */
static void ip_vs_lblcr_check_expire(unsigned long data)
{
	struct ip_vs_service *svc = (struct ip_vs_service *) data;
	struct ip_vs_lblcr_table *tbl = svc->sched_data;
	unsigned long now = jiffies;
	int goal;
	int i, j;
	struct ip_vs_lblcr_entry *en;
	struct hlist_node *next;

	if ((tbl->counter % COUNT_FOR_FULL_EXPIRATION) == 0) {
		/* do full expiration check */
		ip_vs_lblcr_full_check(svc);
		tbl->counter = 1;
		goto out;
	}

	if (atomic_read(&tbl->entries) <= tbl->max_size) {
		tbl->counter++;
		goto out;
	}

	goal = (atomic_read(&tbl->entries) - tbl->max_size)*4/3;
	if (goal > tbl->max_size/2)
		goal = tbl->max_size/2;

	for (i = 0, j = tbl->rover; i < IP_VS_LBLCR_TAB_SIZE; i++) {
		j = (j + 1) & IP_VS_LBLCR_TAB_MASK;

		spin_lock(&svc->sched_lock);
		hlist_for_each_entry_safe(en, next, &tbl->bucket[j], list) {
			if (time_before(now, en->lastuse+ENTRY_TIMEOUT))
				continue;

			ip_vs_lblcr_free(en);
			atomic_dec(&tbl->entries);
			goal--;
		}
		spin_unlock(&svc->sched_lock);
		if (goal <= 0)
			break;
	}
	tbl->rover = j;

  out:
	mod_timer(&tbl->periodic_timer, jiffies+CHECK_EXPIRE_INTERVAL);
}

static int ip_vs_lblcr_init_svc(struct ip_vs_service *svc)
{
	int i;
	struct ip_vs_lblcr_table *tbl;

	/*
	 *    Allocate the ip_vs_lblcr_table for this service
	 */
	tbl = kmalloc(sizeof(*tbl), GFP_KERNEL);
	if (tbl == NULL)
		return -ENOMEM;

	svc->sched_data = tbl;
	IP_VS_DBG(6, "LBLCR hash table (memory=%zdbytes) allocated for "
		  "current service\n", sizeof(*tbl));

	/*
	 *    Initialize the hash buckets
	 */
	for (i = 0; i < IP_VS_LBLCR_TAB_SIZE; i++) {
		INIT_HLIST_HEAD(&tbl->bucket[i]);
	}
	tbl->max_size = IP_VS_LBLCR_TAB_SIZE*16;
	tbl->rover = 0;
	tbl->counter = 1;
	tbl->dead = 0;

	/*
	 *    Hook periodic timer for garbage collection
	 */
	setup_timer(&tbl->periodic_timer, ip_vs_lblcr_check_expire,
			(unsigned long)svc);
	mod_timer(&tbl->periodic_timer, jiffies + CHECK_EXPIRE_INTERVAL);

	return 0;
}


static void ip_vs_lblcr_done_svc(struct ip_vs_service *svc)
{
	struct ip_vs_lblcr_table *tbl = svc->sched_data;

	/* remove periodic timer */
	del_timer_sync(&tbl->periodic_timer);

	/* got to clean up table entries here */
	ip_vs_lblcr_flush(svc);

	/* release the table itself */
	kfree_rcu(tbl, rcu_head);
	IP_VS_DBG(6, "LBLCR hash table (memory=%zdbytes) released\n",
		  sizeof(*tbl));
}


static inline struct ip_vs_dest *
__ip_vs_lblcr_schedule(struct ip_vs_service *svc)
{
	struct ip_vs_dest *dest, *least;
	int loh, doh;

	/*
	 * We use the following formula to estimate the load:
	 *                (dest overhead) / dest->weight
	 *
	 * Remember -- no floats in kernel mode!!!
	 * The comparison of h1*w2 > h2*w1 is equivalent to that of
	 *                h1/w1 > h2/w2
	 * if every weight is larger than zero.
	 *
	 * The server with weight=0 is quiesced and will not receive any
	 * new connection.
	 */
	list_for_each_entry_rcu(dest, &svc->destinations, n_list) {
		if (dest->flags & IP_VS_DEST_F_OVERLOAD)
			continue;

		if (atomic_read(&dest->weight) > 0) {
			least = dest;
			loh = ip_vs_dest_conn_overhead(least);
			goto nextstage;
		}
	}
	return NULL;

	/*
	 *    Find the destination with the least load.
	 */
  nextstage:
	list_for_each_entry_continue_rcu(dest, &svc->destinations, n_list) {
		if (dest->flags & IP_VS_DEST_F_OVERLOAD)
			continue;

		doh = ip_vs_dest_conn_overhead(dest);
		if ((__s64)loh * atomic_read(&dest->weight) >
		    (__s64)doh * atomic_read(&least->weight)) {
			least = dest;
			loh = doh;
		}
	}

	IP_VS_DBG_BUF(6, "LBLCR: server %s:%d "
		      "activeconns %d refcnt %d weight %d overhead %d\n",
		      IP_VS_DBG_ADDR(least->af, &least->addr),
		      ntohs(least->port),
		      atomic_read(&least->activeconns),
		      refcount_read(&least->refcnt),
		      atomic_read(&least->weight), loh);

	return least;
}


/*
 *   If this destination server is overloaded and there is a less loaded
 *   server, then return true.
 */
static inline int
is_overloaded(struct ip_vs_dest *dest, struct ip_vs_service *svc)
{
	if (atomic_read(&dest->activeconns) > atomic_read(&dest->weight)) {
		struct ip_vs_dest *d;

		list_for_each_entry_rcu(d, &svc->destinations, n_list) {
			if (atomic_read(&d->activeconns)*2
			    < atomic_read(&d->weight)) {
				return 1;
			}
		}
	}
	return 0;
}


/*
 *    Locality-Based (weighted) Least-Connection scheduling
 */
static struct ip_vs_dest *
ip_vs_lblcr_schedule(struct ip_vs_service *svc, const struct sk_buff *skb,
		     struct ip_vs_iphdr *iph)
{
	struct ip_vs_lblcr_table *tbl = svc->sched_data;
	struct ip_vs_dest *dest;
	struct ip_vs_lblcr_entry *en;

	IP_VS_DBG(6, "%s(): Scheduling...\n", __func__);

	/* First look in our cache */
	en = ip_vs_lblcr_get(svc->af, tbl, &iph->daddr);
	if (en) {
		en->lastuse = jiffies;

		/* Get the least loaded destination */
		dest = ip_vs_dest_set_min(&en->set);

		/* More than one destination + enough time passed by, cleanup */
		if (atomic_read(&en->set.size) > 1 &&
		    time_after(jiffies, en->set.lastmod +
				sysctl_lblcr_expiration(svc))) {
			spin_lock_bh(&svc->sched_lock);
			if (atomic_read(&en->set.size) > 1) {
				struct ip_vs_dest *m;

				m = ip_vs_dest_set_max(&en->set);
				if (m)
					ip_vs_dest_set_erase(&en->set, m);
			}
			spin_unlock_bh(&svc->sched_lock);
		}

		/* If the destination is not overloaded, use it */
		if (dest && !is_overloaded(dest, svc))
			goto out;

		/* The cache entry is invalid, time to schedule */
		dest = __ip_vs_lblcr_schedule(svc);
		if (!dest) {
			ip_vs_scheduler_err(svc, "no destination available");
			return NULL;
		}

		/* Update our cache entry */
		spin_lock_bh(&svc->sched_lock);
		if (!tbl->dead)
			ip_vs_dest_set_insert(&en->set, dest, true);
		spin_unlock_bh(&svc->sched_lock);
		goto out;
	}

	/* No cache entry, time to schedule */
	dest = __ip_vs_lblcr_schedule(svc);
	if (!dest) {
		IP_VS_DBG(1, "no destination available\n");
		return NULL;
	}

	/* If we fail to create a cache entry, we'll just use the valid dest */
	spin_lock_bh(&svc->sched_lock);
	if (!tbl->dead)
		ip_vs_lblcr_new(tbl, &iph->daddr, svc->af, dest);
	spin_unlock_bh(&svc->sched_lock);

out:
	IP_VS_DBG_BUF(6, "LBLCR: destination IP address %s --> server %s:%d\n",
		      IP_VS_DBG_ADDR(svc->af, &iph->daddr),
		      IP_VS_DBG_ADDR(dest->af, &dest->addr), ntohs(dest->port));

	return dest;
}


/*
 *      IPVS LBLCR Scheduler structure
 */
static struct ip_vs_scheduler ip_vs_lblcr_scheduler =
{
	.name =			"lblcr",
	.refcnt =		ATOMIC_INIT(0),
	.module =		THIS_MODULE,
	.n_list =		LIST_HEAD_INIT(ip_vs_lblcr_scheduler.n_list),
	.init_service =		ip_vs_lblcr_init_svc,
	.done_service =		ip_vs_lblcr_done_svc,
	.schedule =		ip_vs_lblcr_schedule,
};

/*
 *  per netns init.
 */
#ifdef CONFIG_SYSCTL
static int __net_init __ip_vs_lblcr_init(struct net *net)
{
	struct netns_ipvs *ipvs = net_ipvs(net);

	if (!ipvs)
		return -ENOENT;

	if (!net_eq(net, &init_net)) {
		ipvs->lblcr_ctl_table = kmemdup(vs_vars_table,
						sizeof(vs_vars_table),
						GFP_KERNEL);
		if (ipvs->lblcr_ctl_table == NULL)
			return -ENOMEM;

		/* Don't export sysctls to unprivileged users */
		if (net->user_ns != &init_user_ns)
			ipvs->lblcr_ctl_table[0].procname = NULL;
	} else
		ipvs->lblcr_ctl_table = vs_vars_table;
	ipvs->sysctl_lblcr_expiration = DEFAULT_EXPIRATION;
	ipvs->lblcr_ctl_table[0].data = &ipvs->sysctl_lblcr_expiration;

	ipvs->lblcr_ctl_header =
		register_net_sysctl(net, "net/ipv4/vs", ipvs->lblcr_ctl_table);
	if (!ipvs->lblcr_ctl_header) {
		if (!net_eq(net, &init_net))
			kfree(ipvs->lblcr_ctl_table);
		return -ENOMEM;
	}

	return 0;
}

static void __net_exit __ip_vs_lblcr_exit(struct net *net)
{
	struct netns_ipvs *ipvs = net_ipvs(net);

	unregister_net_sysctl_table(ipvs->lblcr_ctl_header);

	if (!net_eq(net, &init_net))
		kfree(ipvs->lblcr_ctl_table);
}

#else

static int __net_init __ip_vs_lblcr_init(struct net *net) { return 0; }
static void __net_exit __ip_vs_lblcr_exit(struct net *net) { }

#endif

static struct pernet_operations ip_vs_lblcr_ops = {
	.init = __ip_vs_lblcr_init,
	.exit = __ip_vs_lblcr_exit,
};

static int __init ip_vs_lblcr_init(void)
{
	int ret;

	ret = register_pernet_subsys(&ip_vs_lblcr_ops);
	if (ret)
		return ret;

	ret = register_ip_vs_scheduler(&ip_vs_lblcr_scheduler);
	if (ret)
		unregister_pernet_subsys(&ip_vs_lblcr_ops);
	return ret;
}

static void __exit ip_vs_lblcr_cleanup(void)
{
	unregister_ip_vs_scheduler(&ip_vs_lblcr_scheduler);
	unregister_pernet_subsys(&ip_vs_lblcr_ops);
	rcu_barrier();
}


module_init(ip_vs_lblcr_init);
module_exit(ip_vs_lblcr_cleanup);
MODULE_LICENSE("GPL");
