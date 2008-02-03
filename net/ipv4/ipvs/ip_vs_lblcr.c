/*
 * IPVS:        Locality-Based Least-Connection with Replication scheduler
 *
 * Version:     $Id: ip_vs_lblcr.c,v 1.11 2002/09/15 08:14:08 wensong Exp $
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

#include <linux/ip.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/jiffies.h>

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

/*
 *    It is for full expiration check.
 *    When there is no partial expiration check (garbage collection)
 *    in a half hour, do a full expiration check to collect stale
 *    entries that haven't been touched for a day.
 */
#define COUNT_FOR_FULL_EXPIRATION   30
static int sysctl_ip_vs_lblcr_expiration = 24*60*60*HZ;


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
struct ip_vs_dest_list {
	struct ip_vs_dest_list  *next;          /* list link */
	struct ip_vs_dest       *dest;          /* destination server */
};

struct ip_vs_dest_set {
	atomic_t                size;           /* set size */
	unsigned long           lastmod;        /* last modified time */
	struct ip_vs_dest_list  *list;          /* destination list */
	rwlock_t	        lock;           /* lock for this list */
};


static struct ip_vs_dest_list *
ip_vs_dest_set_insert(struct ip_vs_dest_set *set, struct ip_vs_dest *dest)
{
	struct ip_vs_dest_list *e;

	for (e=set->list; e!=NULL; e=e->next) {
		if (e->dest == dest)
			/* already existed */
			return NULL;
	}

	e = kmalloc(sizeof(struct ip_vs_dest_list), GFP_ATOMIC);
	if (e == NULL) {
		IP_VS_ERR("ip_vs_dest_set_insert(): no memory\n");
		return NULL;
	}

	atomic_inc(&dest->refcnt);
	e->dest = dest;

	/* link it to the list */
	write_lock(&set->lock);
	e->next = set->list;
	set->list = e;
	atomic_inc(&set->size);
	write_unlock(&set->lock);

	set->lastmod = jiffies;
	return e;
}

static void
ip_vs_dest_set_erase(struct ip_vs_dest_set *set, struct ip_vs_dest *dest)
{
	struct ip_vs_dest_list *e, **ep;

	write_lock(&set->lock);
	for (ep=&set->list, e=*ep; e!=NULL; e=*ep) {
		if (e->dest == dest) {
			/* HIT */
			*ep = e->next;
			atomic_dec(&set->size);
			set->lastmod = jiffies;
			atomic_dec(&e->dest->refcnt);
			kfree(e);
			break;
		}
		ep = &e->next;
	}
	write_unlock(&set->lock);
}

static void ip_vs_dest_set_eraseall(struct ip_vs_dest_set *set)
{
	struct ip_vs_dest_list *e, **ep;

	write_lock(&set->lock);
	for (ep=&set->list, e=*ep; e!=NULL; e=*ep) {
		*ep = e->next;
		/*
		 * We don't kfree dest because it is refered either
		 * by its service or by the trash dest list.
		 */
		atomic_dec(&e->dest->refcnt);
		kfree(e);
	}
	write_unlock(&set->lock);
}

/* get weighted least-connection node in the destination set */
static inline struct ip_vs_dest *ip_vs_dest_set_min(struct ip_vs_dest_set *set)
{
	register struct ip_vs_dest_list *e;
	struct ip_vs_dest *dest, *least;
	int loh, doh;

	if (set == NULL)
		return NULL;

	read_lock(&set->lock);
	/* select the first destination server, whose weight > 0 */
	for (e=set->list; e!=NULL; e=e->next) {
		least = e->dest;
		if (least->flags & IP_VS_DEST_F_OVERLOAD)
			continue;

		if ((atomic_read(&least->weight) > 0)
		    && (least->flags & IP_VS_DEST_F_AVAILABLE)) {
			loh = atomic_read(&least->activeconns) * 50
				+ atomic_read(&least->inactconns);
			goto nextstage;
		}
	}
	read_unlock(&set->lock);
	return NULL;

	/* find the destination with the weighted least load */
  nextstage:
	for (e=e->next; e!=NULL; e=e->next) {
		dest = e->dest;
		if (dest->flags & IP_VS_DEST_F_OVERLOAD)
			continue;

		doh = atomic_read(&dest->activeconns) * 50
			+ atomic_read(&dest->inactconns);
		if ((loh * atomic_read(&dest->weight) >
		     doh * atomic_read(&least->weight))
		    && (dest->flags & IP_VS_DEST_F_AVAILABLE)) {
			least = dest;
			loh = doh;
		}
	}
	read_unlock(&set->lock);

	IP_VS_DBG(6, "ip_vs_dest_set_min: server %d.%d.%d.%d:%d "
		  "activeconns %d refcnt %d weight %d overhead %d\n",
		  NIPQUAD(least->addr), ntohs(least->port),
		  atomic_read(&least->activeconns),
		  atomic_read(&least->refcnt),
		  atomic_read(&least->weight), loh);
	return least;
}


/* get weighted most-connection node in the destination set */
static inline struct ip_vs_dest *ip_vs_dest_set_max(struct ip_vs_dest_set *set)
{
	register struct ip_vs_dest_list *e;
	struct ip_vs_dest *dest, *most;
	int moh, doh;

	if (set == NULL)
		return NULL;

	read_lock(&set->lock);
	/* select the first destination server, whose weight > 0 */
	for (e=set->list; e!=NULL; e=e->next) {
		most = e->dest;
		if (atomic_read(&most->weight) > 0) {
			moh = atomic_read(&most->activeconns) * 50
				+ atomic_read(&most->inactconns);
			goto nextstage;
		}
	}
	read_unlock(&set->lock);
	return NULL;

	/* find the destination with the weighted most load */
  nextstage:
	for (e=e->next; e!=NULL; e=e->next) {
		dest = e->dest;
		doh = atomic_read(&dest->activeconns) * 50
			+ atomic_read(&dest->inactconns);
		/* moh/mw < doh/dw ==> moh*dw < doh*mw, where mw,dw>0 */
		if ((moh * atomic_read(&dest->weight) <
		     doh * atomic_read(&most->weight))
		    && (atomic_read(&dest->weight) > 0)) {
			most = dest;
			moh = doh;
		}
	}
	read_unlock(&set->lock);

	IP_VS_DBG(6, "ip_vs_dest_set_max: server %d.%d.%d.%d:%d "
		  "activeconns %d refcnt %d weight %d overhead %d\n",
		  NIPQUAD(most->addr), ntohs(most->port),
		  atomic_read(&most->activeconns),
		  atomic_read(&most->refcnt),
		  atomic_read(&most->weight), moh);
	return most;
}


/*
 *      IPVS lblcr entry represents an association between destination
 *      IP address and its destination server set
 */
struct ip_vs_lblcr_entry {
	struct list_head        list;
	__be32                   addr;           /* destination IP address */
	struct ip_vs_dest_set   set;            /* destination server set */
	unsigned long           lastuse;        /* last used time */
};


/*
 *      IPVS lblcr hash table
 */
struct ip_vs_lblcr_table {
	rwlock_t	        lock;           /* lock for this table */
	struct list_head        bucket[IP_VS_LBLCR_TAB_SIZE];  /* hash bucket */
	atomic_t                entries;        /* number of entries */
	int                     max_size;       /* maximum size of entries */
	struct timer_list       periodic_timer; /* collect stale entries */
	int                     rover;          /* rover for expire check */
	int                     counter;        /* counter for no expire */
};


/*
 *      IPVS LBLCR sysctl table
 */

static ctl_table vs_vars_table[] = {
	{
		.procname	= "lblcr_expiration",
		.data		= &sysctl_ip_vs_lblcr_expiration,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{ .ctl_name = 0 }
};

static struct ctl_table_header * sysctl_header;

/*
 *      new/free a ip_vs_lblcr_entry, which is a mapping of a destination
 *      IP address to a server.
 */
static inline struct ip_vs_lblcr_entry *ip_vs_lblcr_new(__be32 daddr)
{
	struct ip_vs_lblcr_entry *en;

	en = kmalloc(sizeof(struct ip_vs_lblcr_entry), GFP_ATOMIC);
	if (en == NULL) {
		IP_VS_ERR("ip_vs_lblcr_new(): no memory\n");
		return NULL;
	}

	INIT_LIST_HEAD(&en->list);
	en->addr = daddr;

	/* initilize its dest set */
	atomic_set(&(en->set.size), 0);
	en->set.list = NULL;
	rwlock_init(&en->set.lock);

	return en;
}


static inline void ip_vs_lblcr_free(struct ip_vs_lblcr_entry *en)
{
	list_del(&en->list);
	ip_vs_dest_set_eraseall(&en->set);
	kfree(en);
}


/*
 *	Returns hash value for IPVS LBLCR entry
 */
static inline unsigned ip_vs_lblcr_hashkey(__be32 addr)
{
	return (ntohl(addr)*2654435761UL) & IP_VS_LBLCR_TAB_MASK;
}


/*
 *	Hash an entry in the ip_vs_lblcr_table.
 *	returns bool success.
 */
static int
ip_vs_lblcr_hash(struct ip_vs_lblcr_table *tbl, struct ip_vs_lblcr_entry *en)
{
	unsigned hash;

	if (!list_empty(&en->list)) {
		IP_VS_ERR("ip_vs_lblcr_hash(): request for already hashed, "
			  "called from %p\n", __builtin_return_address(0));
		return 0;
	}

	/*
	 *	Hash by destination IP address
	 */
	hash = ip_vs_lblcr_hashkey(en->addr);

	write_lock(&tbl->lock);
	list_add(&en->list, &tbl->bucket[hash]);
	atomic_inc(&tbl->entries);
	write_unlock(&tbl->lock);

	return 1;
}


/*
 *  Get ip_vs_lblcr_entry associated with supplied parameters.
 */
static inline struct ip_vs_lblcr_entry *
ip_vs_lblcr_get(struct ip_vs_lblcr_table *tbl, __be32 addr)
{
	unsigned hash;
	struct ip_vs_lblcr_entry *en;

	hash = ip_vs_lblcr_hashkey(addr);

	read_lock(&tbl->lock);

	list_for_each_entry(en, &tbl->bucket[hash], list) {
		if (en->addr == addr) {
			/* HIT */
			read_unlock(&tbl->lock);
			return en;
		}
	}

	read_unlock(&tbl->lock);

	return NULL;
}


/*
 *      Flush all the entries of the specified table.
 */
static void ip_vs_lblcr_flush(struct ip_vs_lblcr_table *tbl)
{
	int i;
	struct ip_vs_lblcr_entry *en, *nxt;

	for (i=0; i<IP_VS_LBLCR_TAB_SIZE; i++) {
		write_lock(&tbl->lock);
		list_for_each_entry_safe(en, nxt, &tbl->bucket[i], list) {
			ip_vs_lblcr_free(en);
			atomic_dec(&tbl->entries);
		}
		write_unlock(&tbl->lock);
	}
}


static inline void ip_vs_lblcr_full_check(struct ip_vs_lblcr_table *tbl)
{
	unsigned long now = jiffies;
	int i, j;
	struct ip_vs_lblcr_entry *en, *nxt;

	for (i=0, j=tbl->rover; i<IP_VS_LBLCR_TAB_SIZE; i++) {
		j = (j + 1) & IP_VS_LBLCR_TAB_MASK;

		write_lock(&tbl->lock);
		list_for_each_entry_safe(en, nxt, &tbl->bucket[j], list) {
			if (time_after(en->lastuse+sysctl_ip_vs_lblcr_expiration,
				       now))
				continue;

			ip_vs_lblcr_free(en);
			atomic_dec(&tbl->entries);
		}
		write_unlock(&tbl->lock);
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
	struct ip_vs_lblcr_table *tbl;
	unsigned long now = jiffies;
	int goal;
	int i, j;
	struct ip_vs_lblcr_entry *en, *nxt;

	tbl = (struct ip_vs_lblcr_table *)data;

	if ((tbl->counter % COUNT_FOR_FULL_EXPIRATION) == 0) {
		/* do full expiration check */
		ip_vs_lblcr_full_check(tbl);
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

	for (i=0, j=tbl->rover; i<IP_VS_LBLCR_TAB_SIZE; i++) {
		j = (j + 1) & IP_VS_LBLCR_TAB_MASK;

		write_lock(&tbl->lock);
		list_for_each_entry_safe(en, nxt, &tbl->bucket[j], list) {
			if (time_before(now, en->lastuse+ENTRY_TIMEOUT))
				continue;

			ip_vs_lblcr_free(en);
			atomic_dec(&tbl->entries);
			goal--;
		}
		write_unlock(&tbl->lock);
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
	tbl = kmalloc(sizeof(struct ip_vs_lblcr_table), GFP_ATOMIC);
	if (tbl == NULL) {
		IP_VS_ERR("ip_vs_lblcr_init_svc(): no memory\n");
		return -ENOMEM;
	}
	svc->sched_data = tbl;
	IP_VS_DBG(6, "LBLCR hash table (memory=%Zdbytes) allocated for "
		  "current service\n",
		  sizeof(struct ip_vs_lblcr_table));

	/*
	 *    Initialize the hash buckets
	 */
	for (i=0; i<IP_VS_LBLCR_TAB_SIZE; i++) {
		INIT_LIST_HEAD(&tbl->bucket[i]);
	}
	rwlock_init(&tbl->lock);
	tbl->max_size = IP_VS_LBLCR_TAB_SIZE*16;
	tbl->rover = 0;
	tbl->counter = 1;

	/*
	 *    Hook periodic timer for garbage collection
	 */
	setup_timer(&tbl->periodic_timer, ip_vs_lblcr_check_expire,
			(unsigned long)tbl);
	tbl->periodic_timer.expires = jiffies+CHECK_EXPIRE_INTERVAL;
	add_timer(&tbl->periodic_timer);

	return 0;
}


static int ip_vs_lblcr_done_svc(struct ip_vs_service *svc)
{
	struct ip_vs_lblcr_table *tbl = svc->sched_data;

	/* remove periodic timer */
	del_timer_sync(&tbl->periodic_timer);

	/* got to clean up table entries here */
	ip_vs_lblcr_flush(tbl);

	/* release the table itself */
	kfree(svc->sched_data);
	IP_VS_DBG(6, "LBLCR hash table (memory=%Zdbytes) released\n",
		  sizeof(struct ip_vs_lblcr_table));

	return 0;
}


static int ip_vs_lblcr_update_svc(struct ip_vs_service *svc)
{
	return 0;
}


static inline struct ip_vs_dest *
__ip_vs_wlc_schedule(struct ip_vs_service *svc, struct iphdr *iph)
{
	struct ip_vs_dest *dest, *least;
	int loh, doh;

	/*
	 * We think the overhead of processing active connections is fifty
	 * times higher than that of inactive connections in average. (This
	 * fifty times might not be accurate, we will change it later.) We
	 * use the following formula to estimate the overhead:
	 *                dest->activeconns*50 + dest->inactconns
	 * and the load:
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
	list_for_each_entry(dest, &svc->destinations, n_list) {
		if (dest->flags & IP_VS_DEST_F_OVERLOAD)
			continue;

		if (atomic_read(&dest->weight) > 0) {
			least = dest;
			loh = atomic_read(&least->activeconns) * 50
				+ atomic_read(&least->inactconns);
			goto nextstage;
		}
	}
	return NULL;

	/*
	 *    Find the destination with the least load.
	 */
  nextstage:
	list_for_each_entry_continue(dest, &svc->destinations, n_list) {
		if (dest->flags & IP_VS_DEST_F_OVERLOAD)
			continue;

		doh = atomic_read(&dest->activeconns) * 50
			+ atomic_read(&dest->inactconns);
		if (loh * atomic_read(&dest->weight) >
		    doh * atomic_read(&least->weight)) {
			least = dest;
			loh = doh;
		}
	}

	IP_VS_DBG(6, "LBLCR: server %d.%d.%d.%d:%d "
		  "activeconns %d refcnt %d weight %d overhead %d\n",
		  NIPQUAD(least->addr), ntohs(least->port),
		  atomic_read(&least->activeconns),
		  atomic_read(&least->refcnt),
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

		list_for_each_entry(d, &svc->destinations, n_list) {
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
ip_vs_lblcr_schedule(struct ip_vs_service *svc, const struct sk_buff *skb)
{
	struct ip_vs_dest *dest;
	struct ip_vs_lblcr_table *tbl;
	struct ip_vs_lblcr_entry *en;
	struct iphdr *iph = ip_hdr(skb);

	IP_VS_DBG(6, "ip_vs_lblcr_schedule(): Scheduling...\n");

	tbl = (struct ip_vs_lblcr_table *)svc->sched_data;
	en = ip_vs_lblcr_get(tbl, iph->daddr);
	if (en == NULL) {
		dest = __ip_vs_wlc_schedule(svc, iph);
		if (dest == NULL) {
			IP_VS_DBG(1, "no destination available\n");
			return NULL;
		}
		en = ip_vs_lblcr_new(iph->daddr);
		if (en == NULL) {
			return NULL;
		}
		ip_vs_dest_set_insert(&en->set, dest);
		ip_vs_lblcr_hash(tbl, en);
	} else {
		dest = ip_vs_dest_set_min(&en->set);
		if (!dest || is_overloaded(dest, svc)) {
			dest = __ip_vs_wlc_schedule(svc, iph);
			if (dest == NULL) {
				IP_VS_DBG(1, "no destination available\n");
				return NULL;
			}
			ip_vs_dest_set_insert(&en->set, dest);
		}
		if (atomic_read(&en->set.size) > 1 &&
		    jiffies-en->set.lastmod > sysctl_ip_vs_lblcr_expiration) {
			struct ip_vs_dest *m;
			m = ip_vs_dest_set_max(&en->set);
			if (m)
				ip_vs_dest_set_erase(&en->set, m);
		}
	}
	en->lastuse = jiffies;

	IP_VS_DBG(6, "LBLCR: destination IP address %u.%u.%u.%u "
		  "--> server %u.%u.%u.%u:%d\n",
		  NIPQUAD(en->addr),
		  NIPQUAD(dest->addr),
		  ntohs(dest->port));

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
	.init_service =		ip_vs_lblcr_init_svc,
	.done_service =		ip_vs_lblcr_done_svc,
	.update_service =	ip_vs_lblcr_update_svc,
	.schedule =		ip_vs_lblcr_schedule,
};


static int __init ip_vs_lblcr_init(void)
{
	int ret;

	INIT_LIST_HEAD(&ip_vs_lblcr_scheduler.n_list);
	sysctl_header = register_sysctl_paths(net_vs_ctl_path, vs_vars_table);
	ret = register_ip_vs_scheduler(&ip_vs_lblcr_scheduler);
	if (ret)
		unregister_sysctl_table(sysctl_header);
	return ret;
}


static void __exit ip_vs_lblcr_cleanup(void)
{
	unregister_sysctl_table(sysctl_header);
	unregister_ip_vs_scheduler(&ip_vs_lblcr_scheduler);
}


module_init(ip_vs_lblcr_init);
module_exit(ip_vs_lblcr_cleanup);
MODULE_LICENSE("GPL");
