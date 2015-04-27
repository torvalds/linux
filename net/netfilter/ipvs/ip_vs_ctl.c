/*
 * IPVS         An implementation of the IP virtual server support for the
 *              LINUX operating system.  IPVS is now implemented as a module
 *              over the NetFilter framework. IPVS can be used to build a
 *              high-performance and highly available server based on a
 *              cluster of servers.
 *
 * Authors:     Wensong Zhang <wensong@linuxvirtualserver.org>
 *              Peter Kese <peter.kese@ijs.si>
 *              Julian Anastasov <ja@ssi.bg>
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Changes:
 *
 */

#define KMSG_COMPONENT "IPVS"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/workqueue.h>
#include <linux/swap.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/mutex.h>

#include <net/net_namespace.h>
#include <linux/nsproxy.h>
#include <net/ip.h>
#ifdef CONFIG_IP_VS_IPV6
#include <net/ipv6.h>
#include <net/ip6_route.h>
#endif
#include <net/route.h>
#include <net/sock.h>
#include <net/genetlink.h>

#include <asm/uaccess.h>

#include <net/ip_vs.h>

/* semaphore for IPVS sockopts. And, [gs]etsockopt may sleep. */
static DEFINE_MUTEX(__ip_vs_mutex);

/* sysctl variables */

#ifdef CONFIG_IP_VS_DEBUG
static int sysctl_ip_vs_debug_level = 0;

int ip_vs_get_debug_level(void)
{
	return sysctl_ip_vs_debug_level;
}
#endif


/*  Protos */
static void __ip_vs_del_service(struct ip_vs_service *svc, bool cleanup);


#ifdef CONFIG_IP_VS_IPV6
/* Taken from rt6_fill_node() in net/ipv6/route.c, is there a better way? */
static bool __ip_vs_addr_is_local_v6(struct net *net,
				     const struct in6_addr *addr)
{
	struct flowi6 fl6 = {
		.daddr = *addr,
	};
	struct dst_entry *dst = ip6_route_output(net, NULL, &fl6);
	bool is_local;

	is_local = !dst->error && dst->dev && (dst->dev->flags & IFF_LOOPBACK);

	dst_release(dst);
	return is_local;
}
#endif

#ifdef CONFIG_SYSCTL
/*
 *	update_defense_level is called from keventd and from sysctl,
 *	so it needs to protect itself from softirqs
 */
static void update_defense_level(struct netns_ipvs *ipvs)
{
	struct sysinfo i;
	static int old_secure_tcp = 0;
	int availmem;
	int nomem;
	int to_change = -1;

	/* we only count free and buffered memory (in pages) */
	si_meminfo(&i);
	availmem = i.freeram + i.bufferram;
	/* however in linux 2.5 the i.bufferram is total page cache size,
	   we need adjust it */
	/* si_swapinfo(&i); */
	/* availmem = availmem - (i.totalswap - i.freeswap); */

	nomem = (availmem < ipvs->sysctl_amemthresh);

	local_bh_disable();

	/* drop_entry */
	spin_lock(&ipvs->dropentry_lock);
	switch (ipvs->sysctl_drop_entry) {
	case 0:
		atomic_set(&ipvs->dropentry, 0);
		break;
	case 1:
		if (nomem) {
			atomic_set(&ipvs->dropentry, 1);
			ipvs->sysctl_drop_entry = 2;
		} else {
			atomic_set(&ipvs->dropentry, 0);
		}
		break;
	case 2:
		if (nomem) {
			atomic_set(&ipvs->dropentry, 1);
		} else {
			atomic_set(&ipvs->dropentry, 0);
			ipvs->sysctl_drop_entry = 1;
		};
		break;
	case 3:
		atomic_set(&ipvs->dropentry, 1);
		break;
	}
	spin_unlock(&ipvs->dropentry_lock);

	/* drop_packet */
	spin_lock(&ipvs->droppacket_lock);
	switch (ipvs->sysctl_drop_packet) {
	case 0:
		ipvs->drop_rate = 0;
		break;
	case 1:
		if (nomem) {
			ipvs->drop_rate = ipvs->drop_counter
				= ipvs->sysctl_amemthresh /
				(ipvs->sysctl_amemthresh-availmem);
			ipvs->sysctl_drop_packet = 2;
		} else {
			ipvs->drop_rate = 0;
		}
		break;
	case 2:
		if (nomem) {
			ipvs->drop_rate = ipvs->drop_counter
				= ipvs->sysctl_amemthresh /
				(ipvs->sysctl_amemthresh-availmem);
		} else {
			ipvs->drop_rate = 0;
			ipvs->sysctl_drop_packet = 1;
		}
		break;
	case 3:
		ipvs->drop_rate = ipvs->sysctl_am_droprate;
		break;
	}
	spin_unlock(&ipvs->droppacket_lock);

	/* secure_tcp */
	spin_lock(&ipvs->securetcp_lock);
	switch (ipvs->sysctl_secure_tcp) {
	case 0:
		if (old_secure_tcp >= 2)
			to_change = 0;
		break;
	case 1:
		if (nomem) {
			if (old_secure_tcp < 2)
				to_change = 1;
			ipvs->sysctl_secure_tcp = 2;
		} else {
			if (old_secure_tcp >= 2)
				to_change = 0;
		}
		break;
	case 2:
		if (nomem) {
			if (old_secure_tcp < 2)
				to_change = 1;
		} else {
			if (old_secure_tcp >= 2)
				to_change = 0;
			ipvs->sysctl_secure_tcp = 1;
		}
		break;
	case 3:
		if (old_secure_tcp < 2)
			to_change = 1;
		break;
	}
	old_secure_tcp = ipvs->sysctl_secure_tcp;
	if (to_change >= 0)
		ip_vs_protocol_timeout_change(ipvs,
					      ipvs->sysctl_secure_tcp > 1);
	spin_unlock(&ipvs->securetcp_lock);

	local_bh_enable();
}


/*
 *	Timer for checking the defense
 */
#define DEFENSE_TIMER_PERIOD	1*HZ

static void defense_work_handler(struct work_struct *work)
{
	struct netns_ipvs *ipvs =
		container_of(work, struct netns_ipvs, defense_work.work);

	update_defense_level(ipvs);
	if (atomic_read(&ipvs->dropentry))
		ip_vs_random_dropentry(ipvs->net);
	schedule_delayed_work(&ipvs->defense_work, DEFENSE_TIMER_PERIOD);
}
#endif

int
ip_vs_use_count_inc(void)
{
	return try_module_get(THIS_MODULE);
}

void
ip_vs_use_count_dec(void)
{
	module_put(THIS_MODULE);
}


/*
 *	Hash table: for virtual service lookups
 */
#define IP_VS_SVC_TAB_BITS 8
#define IP_VS_SVC_TAB_SIZE (1 << IP_VS_SVC_TAB_BITS)
#define IP_VS_SVC_TAB_MASK (IP_VS_SVC_TAB_SIZE - 1)

/* the service table hashed by <protocol, addr, port> */
static struct hlist_head ip_vs_svc_table[IP_VS_SVC_TAB_SIZE];
/* the service table hashed by fwmark */
static struct hlist_head ip_vs_svc_fwm_table[IP_VS_SVC_TAB_SIZE];


/*
 *	Returns hash value for virtual service
 */
static inline unsigned int
ip_vs_svc_hashkey(struct net *net, int af, unsigned int proto,
		  const union nf_inet_addr *addr, __be16 port)
{
	register unsigned int porth = ntohs(port);
	__be32 addr_fold = addr->ip;
	__u32 ahash;

#ifdef CONFIG_IP_VS_IPV6
	if (af == AF_INET6)
		addr_fold = addr->ip6[0]^addr->ip6[1]^
			    addr->ip6[2]^addr->ip6[3];
#endif
	ahash = ntohl(addr_fold);
	ahash ^= ((size_t) net >> 8);

	return (proto ^ ahash ^ (porth >> IP_VS_SVC_TAB_BITS) ^ porth) &
	       IP_VS_SVC_TAB_MASK;
}

/*
 *	Returns hash value of fwmark for virtual service lookup
 */
static inline unsigned int ip_vs_svc_fwm_hashkey(struct net *net, __u32 fwmark)
{
	return (((size_t)net>>8) ^ fwmark) & IP_VS_SVC_TAB_MASK;
}

/*
 *	Hashes a service in the ip_vs_svc_table by <netns,proto,addr,port>
 *	or in the ip_vs_svc_fwm_table by fwmark.
 *	Should be called with locked tables.
 */
static int ip_vs_svc_hash(struct ip_vs_service *svc)
{
	unsigned int hash;

	if (svc->flags & IP_VS_SVC_F_HASHED) {
		pr_err("%s(): request for already hashed, called from %pF\n",
		       __func__, __builtin_return_address(0));
		return 0;
	}

	if (svc->fwmark == 0) {
		/*
		 *  Hash it by <netns,protocol,addr,port> in ip_vs_svc_table
		 */
		hash = ip_vs_svc_hashkey(svc->net, svc->af, svc->protocol,
					 &svc->addr, svc->port);
		hlist_add_head_rcu(&svc->s_list, &ip_vs_svc_table[hash]);
	} else {
		/*
		 *  Hash it by fwmark in svc_fwm_table
		 */
		hash = ip_vs_svc_fwm_hashkey(svc->net, svc->fwmark);
		hlist_add_head_rcu(&svc->f_list, &ip_vs_svc_fwm_table[hash]);
	}

	svc->flags |= IP_VS_SVC_F_HASHED;
	/* increase its refcnt because it is referenced by the svc table */
	atomic_inc(&svc->refcnt);
	return 1;
}


/*
 *	Unhashes a service from svc_table / svc_fwm_table.
 *	Should be called with locked tables.
 */
static int ip_vs_svc_unhash(struct ip_vs_service *svc)
{
	if (!(svc->flags & IP_VS_SVC_F_HASHED)) {
		pr_err("%s(): request for unhash flagged, called from %pF\n",
		       __func__, __builtin_return_address(0));
		return 0;
	}

	if (svc->fwmark == 0) {
		/* Remove it from the svc_table table */
		hlist_del_rcu(&svc->s_list);
	} else {
		/* Remove it from the svc_fwm_table table */
		hlist_del_rcu(&svc->f_list);
	}

	svc->flags &= ~IP_VS_SVC_F_HASHED;
	atomic_dec(&svc->refcnt);
	return 1;
}


/*
 *	Get service by {netns, proto,addr,port} in the service table.
 */
static inline struct ip_vs_service *
__ip_vs_service_find(struct net *net, int af, __u16 protocol,
		     const union nf_inet_addr *vaddr, __be16 vport)
{
	unsigned int hash;
	struct ip_vs_service *svc;

	/* Check for "full" addressed entries */
	hash = ip_vs_svc_hashkey(net, af, protocol, vaddr, vport);

	hlist_for_each_entry_rcu(svc, &ip_vs_svc_table[hash], s_list) {
		if ((svc->af == af)
		    && ip_vs_addr_equal(af, &svc->addr, vaddr)
		    && (svc->port == vport)
		    && (svc->protocol == protocol)
		    && net_eq(svc->net, net)) {
			/* HIT */
			return svc;
		}
	}

	return NULL;
}


/*
 *	Get service by {fwmark} in the service table.
 */
static inline struct ip_vs_service *
__ip_vs_svc_fwm_find(struct net *net, int af, __u32 fwmark)
{
	unsigned int hash;
	struct ip_vs_service *svc;

	/* Check for fwmark addressed entries */
	hash = ip_vs_svc_fwm_hashkey(net, fwmark);

	hlist_for_each_entry_rcu(svc, &ip_vs_svc_fwm_table[hash], f_list) {
		if (svc->fwmark == fwmark && svc->af == af
		    && net_eq(svc->net, net)) {
			/* HIT */
			return svc;
		}
	}

	return NULL;
}

/* Find service, called under RCU lock */
struct ip_vs_service *
ip_vs_service_find(struct net *net, int af, __u32 fwmark, __u16 protocol,
		   const union nf_inet_addr *vaddr, __be16 vport)
{
	struct ip_vs_service *svc;
	struct netns_ipvs *ipvs = net_ipvs(net);

	/*
	 *	Check the table hashed by fwmark first
	 */
	if (fwmark) {
		svc = __ip_vs_svc_fwm_find(net, af, fwmark);
		if (svc)
			goto out;
	}

	/*
	 *	Check the table hashed by <protocol,addr,port>
	 *	for "full" addressed entries
	 */
	svc = __ip_vs_service_find(net, af, protocol, vaddr, vport);

	if (svc == NULL
	    && protocol == IPPROTO_TCP
	    && atomic_read(&ipvs->ftpsvc_counter)
	    && (vport == FTPDATA || ntohs(vport) >= PROT_SOCK)) {
		/*
		 * Check if ftp service entry exists, the packet
		 * might belong to FTP data connections.
		 */
		svc = __ip_vs_service_find(net, af, protocol, vaddr, FTPPORT);
	}

	if (svc == NULL
	    && atomic_read(&ipvs->nullsvc_counter)) {
		/*
		 * Check if the catch-all port (port zero) exists
		 */
		svc = __ip_vs_service_find(net, af, protocol, vaddr, 0);
	}

  out:
	IP_VS_DBG_BUF(9, "lookup service: fwm %u %s %s:%u %s\n",
		      fwmark, ip_vs_proto_name(protocol),
		      IP_VS_DBG_ADDR(af, vaddr), ntohs(vport),
		      svc ? "hit" : "not hit");

	return svc;
}


static inline void
__ip_vs_bind_svc(struct ip_vs_dest *dest, struct ip_vs_service *svc)
{
	atomic_inc(&svc->refcnt);
	rcu_assign_pointer(dest->svc, svc);
}

static void ip_vs_service_free(struct ip_vs_service *svc)
{
	free_percpu(svc->stats.cpustats);
	kfree(svc);
}

static void ip_vs_service_rcu_free(struct rcu_head *head)
{
	struct ip_vs_service *svc;

	svc = container_of(head, struct ip_vs_service, rcu_head);
	ip_vs_service_free(svc);
}

static void __ip_vs_svc_put(struct ip_vs_service *svc, bool do_delay)
{
	if (atomic_dec_and_test(&svc->refcnt)) {
		IP_VS_DBG_BUF(3, "Removing service %u/%s:%u\n",
			      svc->fwmark,
			      IP_VS_DBG_ADDR(svc->af, &svc->addr),
			      ntohs(svc->port));
		if (do_delay)
			call_rcu(&svc->rcu_head, ip_vs_service_rcu_free);
		else
			ip_vs_service_free(svc);
	}
}


/*
 *	Returns hash value for real service
 */
static inline unsigned int ip_vs_rs_hashkey(int af,
					    const union nf_inet_addr *addr,
					    __be16 port)
{
	register unsigned int porth = ntohs(port);
	__be32 addr_fold = addr->ip;

#ifdef CONFIG_IP_VS_IPV6
	if (af == AF_INET6)
		addr_fold = addr->ip6[0]^addr->ip6[1]^
			    addr->ip6[2]^addr->ip6[3];
#endif

	return (ntohl(addr_fold)^(porth>>IP_VS_RTAB_BITS)^porth)
		& IP_VS_RTAB_MASK;
}

/* Hash ip_vs_dest in rs_table by <proto,addr,port>. */
static void ip_vs_rs_hash(struct netns_ipvs *ipvs, struct ip_vs_dest *dest)
{
	unsigned int hash;

	if (dest->in_rs_table)
		return;

	/*
	 *	Hash by proto,addr,port,
	 *	which are the parameters of the real service.
	 */
	hash = ip_vs_rs_hashkey(dest->af, &dest->addr, dest->port);

	hlist_add_head_rcu(&dest->d_list, &ipvs->rs_table[hash]);
	dest->in_rs_table = 1;
}

/* Unhash ip_vs_dest from rs_table. */
static void ip_vs_rs_unhash(struct ip_vs_dest *dest)
{
	/*
	 * Remove it from the rs_table table.
	 */
	if (dest->in_rs_table) {
		hlist_del_rcu(&dest->d_list);
		dest->in_rs_table = 0;
	}
}

/* Check if real service by <proto,addr,port> is present */
bool ip_vs_has_real_service(struct net *net, int af, __u16 protocol,
			    const union nf_inet_addr *daddr, __be16 dport)
{
	struct netns_ipvs *ipvs = net_ipvs(net);
	unsigned int hash;
	struct ip_vs_dest *dest;

	/* Check for "full" addressed entries */
	hash = ip_vs_rs_hashkey(af, daddr, dport);

	rcu_read_lock();
	hlist_for_each_entry_rcu(dest, &ipvs->rs_table[hash], d_list) {
		if (dest->port == dport &&
		    dest->af == af &&
		    ip_vs_addr_equal(af, &dest->addr, daddr) &&
		    (dest->protocol == protocol || dest->vfwmark)) {
			/* HIT */
			rcu_read_unlock();
			return true;
		}
	}
	rcu_read_unlock();

	return false;
}

/* Lookup destination by {addr,port} in the given service
 * Called under RCU lock.
 */
static struct ip_vs_dest *
ip_vs_lookup_dest(struct ip_vs_service *svc, int dest_af,
		  const union nf_inet_addr *daddr, __be16 dport)
{
	struct ip_vs_dest *dest;

	/*
	 * Find the destination for the given service
	 */
	list_for_each_entry_rcu(dest, &svc->destinations, n_list) {
		if ((dest->af == dest_af) &&
		    ip_vs_addr_equal(dest_af, &dest->addr, daddr) &&
		    (dest->port == dport)) {
			/* HIT */
			return dest;
		}
	}

	return NULL;
}

/*
 * Find destination by {daddr,dport,vaddr,protocol}
 * Created to be used in ip_vs_process_message() in
 * the backup synchronization daemon. It finds the
 * destination to be bound to the received connection
 * on the backup.
 * Called under RCU lock, no refcnt is returned.
 */
struct ip_vs_dest *ip_vs_find_dest(struct net  *net, int svc_af, int dest_af,
				   const union nf_inet_addr *daddr,
				   __be16 dport,
				   const union nf_inet_addr *vaddr,
				   __be16 vport, __u16 protocol, __u32 fwmark,
				   __u32 flags)
{
	struct ip_vs_dest *dest;
	struct ip_vs_service *svc;
	__be16 port = dport;

	svc = ip_vs_service_find(net, svc_af, fwmark, protocol, vaddr, vport);
	if (!svc)
		return NULL;
	if (fwmark && (flags & IP_VS_CONN_F_FWD_MASK) != IP_VS_CONN_F_MASQ)
		port = 0;
	dest = ip_vs_lookup_dest(svc, dest_af, daddr, port);
	if (!dest)
		dest = ip_vs_lookup_dest(svc, dest_af, daddr, port ^ dport);
	return dest;
}

void ip_vs_dest_dst_rcu_free(struct rcu_head *head)
{
	struct ip_vs_dest_dst *dest_dst = container_of(head,
						       struct ip_vs_dest_dst,
						       rcu_head);

	dst_release(dest_dst->dst_cache);
	kfree(dest_dst);
}

/* Release dest_dst and dst_cache for dest in user context */
static void __ip_vs_dst_cache_reset(struct ip_vs_dest *dest)
{
	struct ip_vs_dest_dst *old;

	old = rcu_dereference_protected(dest->dest_dst, 1);
	if (old) {
		RCU_INIT_POINTER(dest->dest_dst, NULL);
		call_rcu(&old->rcu_head, ip_vs_dest_dst_rcu_free);
	}
}

/*
 *  Lookup dest by {svc,addr,port} in the destination trash.
 *  The destination trash is used to hold the destinations that are removed
 *  from the service table but are still referenced by some conn entries.
 *  The reason to add the destination trash is when the dest is temporary
 *  down (either by administrator or by monitor program), the dest can be
 *  picked back from the trash, the remaining connections to the dest can
 *  continue, and the counting information of the dest is also useful for
 *  scheduling.
 */
static struct ip_vs_dest *
ip_vs_trash_get_dest(struct ip_vs_service *svc, int dest_af,
		     const union nf_inet_addr *daddr, __be16 dport)
{
	struct ip_vs_dest *dest;
	struct netns_ipvs *ipvs = net_ipvs(svc->net);

	/*
	 * Find the destination in trash
	 */
	spin_lock_bh(&ipvs->dest_trash_lock);
	list_for_each_entry(dest, &ipvs->dest_trash, t_list) {
		IP_VS_DBG_BUF(3, "Destination %u/%s:%u still in trash, "
			      "dest->refcnt=%d\n",
			      dest->vfwmark,
			      IP_VS_DBG_ADDR(dest->af, &dest->addr),
			      ntohs(dest->port),
			      atomic_read(&dest->refcnt));
		if (dest->af == dest_af &&
		    ip_vs_addr_equal(dest_af, &dest->addr, daddr) &&
		    dest->port == dport &&
		    dest->vfwmark == svc->fwmark &&
		    dest->protocol == svc->protocol &&
		    (svc->fwmark ||
		     (ip_vs_addr_equal(svc->af, &dest->vaddr, &svc->addr) &&
		      dest->vport == svc->port))) {
			/* HIT */
			list_del(&dest->t_list);
			ip_vs_dest_hold(dest);
			goto out;
		}
	}

	dest = NULL;

out:
	spin_unlock_bh(&ipvs->dest_trash_lock);

	return dest;
}

static void ip_vs_dest_free(struct ip_vs_dest *dest)
{
	struct ip_vs_service *svc = rcu_dereference_protected(dest->svc, 1);

	__ip_vs_dst_cache_reset(dest);
	__ip_vs_svc_put(svc, false);
	free_percpu(dest->stats.cpustats);
	ip_vs_dest_put_and_free(dest);
}

/*
 *  Clean up all the destinations in the trash
 *  Called by the ip_vs_control_cleanup()
 *
 *  When the ip_vs_control_clearup is activated by ipvs module exit,
 *  the service tables must have been flushed and all the connections
 *  are expired, and the refcnt of each destination in the trash must
 *  be 0, so we simply release them here.
 */
static void ip_vs_trash_cleanup(struct net *net)
{
	struct ip_vs_dest *dest, *nxt;
	struct netns_ipvs *ipvs = net_ipvs(net);

	del_timer_sync(&ipvs->dest_trash_timer);
	/* No need to use dest_trash_lock */
	list_for_each_entry_safe(dest, nxt, &ipvs->dest_trash, t_list) {
		list_del(&dest->t_list);
		ip_vs_dest_free(dest);
	}
}

static void
ip_vs_copy_stats(struct ip_vs_kstats *dst, struct ip_vs_stats *src)
{
#define IP_VS_SHOW_STATS_COUNTER(c) dst->c = src->kstats.c - src->kstats0.c

	spin_lock_bh(&src->lock);

	IP_VS_SHOW_STATS_COUNTER(conns);
	IP_VS_SHOW_STATS_COUNTER(inpkts);
	IP_VS_SHOW_STATS_COUNTER(outpkts);
	IP_VS_SHOW_STATS_COUNTER(inbytes);
	IP_VS_SHOW_STATS_COUNTER(outbytes);

	ip_vs_read_estimator(dst, src);

	spin_unlock_bh(&src->lock);
}

static void
ip_vs_export_stats_user(struct ip_vs_stats_user *dst, struct ip_vs_kstats *src)
{
	dst->conns = (u32)src->conns;
	dst->inpkts = (u32)src->inpkts;
	dst->outpkts = (u32)src->outpkts;
	dst->inbytes = src->inbytes;
	dst->outbytes = src->outbytes;
	dst->cps = (u32)src->cps;
	dst->inpps = (u32)src->inpps;
	dst->outpps = (u32)src->outpps;
	dst->inbps = (u32)src->inbps;
	dst->outbps = (u32)src->outbps;
}

static void
ip_vs_zero_stats(struct ip_vs_stats *stats)
{
	spin_lock_bh(&stats->lock);

	/* get current counters as zero point, rates are zeroed */

#define IP_VS_ZERO_STATS_COUNTER(c) stats->kstats0.c = stats->kstats.c

	IP_VS_ZERO_STATS_COUNTER(conns);
	IP_VS_ZERO_STATS_COUNTER(inpkts);
	IP_VS_ZERO_STATS_COUNTER(outpkts);
	IP_VS_ZERO_STATS_COUNTER(inbytes);
	IP_VS_ZERO_STATS_COUNTER(outbytes);

	ip_vs_zero_estimator(stats);

	spin_unlock_bh(&stats->lock);
}

/*
 *	Update a destination in the given service
 */
static void
__ip_vs_update_dest(struct ip_vs_service *svc, struct ip_vs_dest *dest,
		    struct ip_vs_dest_user_kern *udest, int add)
{
	struct netns_ipvs *ipvs = net_ipvs(svc->net);
	struct ip_vs_service *old_svc;
	struct ip_vs_scheduler *sched;
	int conn_flags;

	/* We cannot modify an address and change the address family */
	BUG_ON(!add && udest->af != dest->af);

	if (add && udest->af != svc->af)
		ipvs->mixed_address_family_dests++;

	/* set the weight and the flags */
	atomic_set(&dest->weight, udest->weight);
	conn_flags = udest->conn_flags & IP_VS_CONN_F_DEST_MASK;
	conn_flags |= IP_VS_CONN_F_INACTIVE;

	/* set the IP_VS_CONN_F_NOOUTPUT flag if not masquerading/NAT */
	if ((conn_flags & IP_VS_CONN_F_FWD_MASK) != IP_VS_CONN_F_MASQ) {
		conn_flags |= IP_VS_CONN_F_NOOUTPUT;
	} else {
		/*
		 *    Put the real service in rs_table if not present.
		 *    For now only for NAT!
		 */
		ip_vs_rs_hash(ipvs, dest);
	}
	atomic_set(&dest->conn_flags, conn_flags);

	/* bind the service */
	old_svc = rcu_dereference_protected(dest->svc, 1);
	if (!old_svc) {
		__ip_vs_bind_svc(dest, svc);
	} else {
		if (old_svc != svc) {
			ip_vs_zero_stats(&dest->stats);
			__ip_vs_bind_svc(dest, svc);
			__ip_vs_svc_put(old_svc, true);
		}
	}

	/* set the dest status flags */
	dest->flags |= IP_VS_DEST_F_AVAILABLE;

	if (udest->u_threshold == 0 || udest->u_threshold > dest->u_threshold)
		dest->flags &= ~IP_VS_DEST_F_OVERLOAD;
	dest->u_threshold = udest->u_threshold;
	dest->l_threshold = udest->l_threshold;

	dest->af = udest->af;

	spin_lock_bh(&dest->dst_lock);
	__ip_vs_dst_cache_reset(dest);
	spin_unlock_bh(&dest->dst_lock);

	sched = rcu_dereference_protected(svc->scheduler, 1);
	if (add) {
		ip_vs_start_estimator(svc->net, &dest->stats);
		list_add_rcu(&dest->n_list, &svc->destinations);
		svc->num_dests++;
		if (sched->add_dest)
			sched->add_dest(svc, dest);
	} else {
		if (sched->upd_dest)
			sched->upd_dest(svc, dest);
	}
}


/*
 *	Create a destination for the given service
 */
static int
ip_vs_new_dest(struct ip_vs_service *svc, struct ip_vs_dest_user_kern *udest,
	       struct ip_vs_dest **dest_p)
{
	struct ip_vs_dest *dest;
	unsigned int atype, i;

	EnterFunction(2);

#ifdef CONFIG_IP_VS_IPV6
	if (udest->af == AF_INET6) {
		atype = ipv6_addr_type(&udest->addr.in6);
		if ((!(atype & IPV6_ADDR_UNICAST) ||
			atype & IPV6_ADDR_LINKLOCAL) &&
			!__ip_vs_addr_is_local_v6(svc->net, &udest->addr.in6))
			return -EINVAL;
	} else
#endif
	{
		atype = inet_addr_type(svc->net, udest->addr.ip);
		if (atype != RTN_LOCAL && atype != RTN_UNICAST)
			return -EINVAL;
	}

	dest = kzalloc(sizeof(struct ip_vs_dest), GFP_KERNEL);
	if (dest == NULL)
		return -ENOMEM;

	dest->stats.cpustats = alloc_percpu(struct ip_vs_cpu_stats);
	if (!dest->stats.cpustats)
		goto err_alloc;

	for_each_possible_cpu(i) {
		struct ip_vs_cpu_stats *ip_vs_dest_stats;
		ip_vs_dest_stats = per_cpu_ptr(dest->stats.cpustats, i);
		u64_stats_init(&ip_vs_dest_stats->syncp);
	}

	dest->af = udest->af;
	dest->protocol = svc->protocol;
	dest->vaddr = svc->addr;
	dest->vport = svc->port;
	dest->vfwmark = svc->fwmark;
	ip_vs_addr_copy(udest->af, &dest->addr, &udest->addr);
	dest->port = udest->port;

	atomic_set(&dest->activeconns, 0);
	atomic_set(&dest->inactconns, 0);
	atomic_set(&dest->persistconns, 0);
	atomic_set(&dest->refcnt, 1);

	INIT_HLIST_NODE(&dest->d_list);
	spin_lock_init(&dest->dst_lock);
	spin_lock_init(&dest->stats.lock);
	__ip_vs_update_dest(svc, dest, udest, 1);

	*dest_p = dest;

	LeaveFunction(2);
	return 0;

err_alloc:
	kfree(dest);
	return -ENOMEM;
}


/*
 *	Add a destination into an existing service
 */
static int
ip_vs_add_dest(struct ip_vs_service *svc, struct ip_vs_dest_user_kern *udest)
{
	struct ip_vs_dest *dest;
	union nf_inet_addr daddr;
	__be16 dport = udest->port;
	int ret;

	EnterFunction(2);

	if (udest->weight < 0) {
		pr_err("%s(): server weight less than zero\n", __func__);
		return -ERANGE;
	}

	if (udest->l_threshold > udest->u_threshold) {
		pr_err("%s(): lower threshold is higher than upper threshold\n",
			__func__);
		return -ERANGE;
	}

	ip_vs_addr_copy(udest->af, &daddr, &udest->addr);

	/* We use function that requires RCU lock */
	rcu_read_lock();
	dest = ip_vs_lookup_dest(svc, udest->af, &daddr, dport);
	rcu_read_unlock();

	if (dest != NULL) {
		IP_VS_DBG(1, "%s(): dest already exists\n", __func__);
		return -EEXIST;
	}

	/*
	 * Check if the dest already exists in the trash and
	 * is from the same service
	 */
	dest = ip_vs_trash_get_dest(svc, udest->af, &daddr, dport);

	if (dest != NULL) {
		IP_VS_DBG_BUF(3, "Get destination %s:%u from trash, "
			      "dest->refcnt=%d, service %u/%s:%u\n",
			      IP_VS_DBG_ADDR(udest->af, &daddr), ntohs(dport),
			      atomic_read(&dest->refcnt),
			      dest->vfwmark,
			      IP_VS_DBG_ADDR(svc->af, &dest->vaddr),
			      ntohs(dest->vport));

		__ip_vs_update_dest(svc, dest, udest, 1);
		ret = 0;
	} else {
		/*
		 * Allocate and initialize the dest structure
		 */
		ret = ip_vs_new_dest(svc, udest, &dest);
	}
	LeaveFunction(2);

	return ret;
}


/*
 *	Edit a destination in the given service
 */
static int
ip_vs_edit_dest(struct ip_vs_service *svc, struct ip_vs_dest_user_kern *udest)
{
	struct ip_vs_dest *dest;
	union nf_inet_addr daddr;
	__be16 dport = udest->port;

	EnterFunction(2);

	if (udest->weight < 0) {
		pr_err("%s(): server weight less than zero\n", __func__);
		return -ERANGE;
	}

	if (udest->l_threshold > udest->u_threshold) {
		pr_err("%s(): lower threshold is higher than upper threshold\n",
			__func__);
		return -ERANGE;
	}

	ip_vs_addr_copy(udest->af, &daddr, &udest->addr);

	/* We use function that requires RCU lock */
	rcu_read_lock();
	dest = ip_vs_lookup_dest(svc, udest->af, &daddr, dport);
	rcu_read_unlock();

	if (dest == NULL) {
		IP_VS_DBG(1, "%s(): dest doesn't exist\n", __func__);
		return -ENOENT;
	}

	__ip_vs_update_dest(svc, dest, udest, 0);
	LeaveFunction(2);

	return 0;
}

/*
 *	Delete a destination (must be already unlinked from the service)
 */
static void __ip_vs_del_dest(struct net *net, struct ip_vs_dest *dest,
			     bool cleanup)
{
	struct netns_ipvs *ipvs = net_ipvs(net);

	ip_vs_stop_estimator(net, &dest->stats);

	/*
	 *  Remove it from the d-linked list with the real services.
	 */
	ip_vs_rs_unhash(dest);

	spin_lock_bh(&ipvs->dest_trash_lock);
	IP_VS_DBG_BUF(3, "Moving dest %s:%u into trash, dest->refcnt=%d\n",
		      IP_VS_DBG_ADDR(dest->af, &dest->addr), ntohs(dest->port),
		      atomic_read(&dest->refcnt));
	if (list_empty(&ipvs->dest_trash) && !cleanup)
		mod_timer(&ipvs->dest_trash_timer,
			  jiffies + (IP_VS_DEST_TRASH_PERIOD >> 1));
	/* dest lives in trash without reference */
	list_add(&dest->t_list, &ipvs->dest_trash);
	dest->idle_start = 0;
	spin_unlock_bh(&ipvs->dest_trash_lock);
	ip_vs_dest_put(dest);
}


/*
 *	Unlink a destination from the given service
 */
static void __ip_vs_unlink_dest(struct ip_vs_service *svc,
				struct ip_vs_dest *dest,
				int svcupd)
{
	dest->flags &= ~IP_VS_DEST_F_AVAILABLE;

	/*
	 *  Remove it from the d-linked destination list.
	 */
	list_del_rcu(&dest->n_list);
	svc->num_dests--;

	if (dest->af != svc->af)
		net_ipvs(svc->net)->mixed_address_family_dests--;

	if (svcupd) {
		struct ip_vs_scheduler *sched;

		sched = rcu_dereference_protected(svc->scheduler, 1);
		if (sched->del_dest)
			sched->del_dest(svc, dest);
	}
}


/*
 *	Delete a destination server in the given service
 */
static int
ip_vs_del_dest(struct ip_vs_service *svc, struct ip_vs_dest_user_kern *udest)
{
	struct ip_vs_dest *dest;
	__be16 dport = udest->port;

	EnterFunction(2);

	/* We use function that requires RCU lock */
	rcu_read_lock();
	dest = ip_vs_lookup_dest(svc, udest->af, &udest->addr, dport);
	rcu_read_unlock();

	if (dest == NULL) {
		IP_VS_DBG(1, "%s(): destination not found!\n", __func__);
		return -ENOENT;
	}

	/*
	 *	Unlink dest from the service
	 */
	__ip_vs_unlink_dest(svc, dest, 1);

	/*
	 *	Delete the destination
	 */
	__ip_vs_del_dest(svc->net, dest, false);

	LeaveFunction(2);

	return 0;
}

static void ip_vs_dest_trash_expire(unsigned long data)
{
	struct net *net = (struct net *) data;
	struct netns_ipvs *ipvs = net_ipvs(net);
	struct ip_vs_dest *dest, *next;
	unsigned long now = jiffies;

	spin_lock(&ipvs->dest_trash_lock);
	list_for_each_entry_safe(dest, next, &ipvs->dest_trash, t_list) {
		if (atomic_read(&dest->refcnt) > 0)
			continue;
		if (dest->idle_start) {
			if (time_before(now, dest->idle_start +
					     IP_VS_DEST_TRASH_PERIOD))
				continue;
		} else {
			dest->idle_start = max(1UL, now);
			continue;
		}
		IP_VS_DBG_BUF(3, "Removing destination %u/%s:%u from trash\n",
			      dest->vfwmark,
			      IP_VS_DBG_ADDR(dest->af, &dest->addr),
			      ntohs(dest->port));
		list_del(&dest->t_list);
		ip_vs_dest_free(dest);
	}
	if (!list_empty(&ipvs->dest_trash))
		mod_timer(&ipvs->dest_trash_timer,
			  jiffies + (IP_VS_DEST_TRASH_PERIOD >> 1));
	spin_unlock(&ipvs->dest_trash_lock);
}

/*
 *	Add a service into the service hash table
 */
static int
ip_vs_add_service(struct net *net, struct ip_vs_service_user_kern *u,
		  struct ip_vs_service **svc_p)
{
	int ret = 0, i;
	struct ip_vs_scheduler *sched = NULL;
	struct ip_vs_pe *pe = NULL;
	struct ip_vs_service *svc = NULL;
	struct netns_ipvs *ipvs = net_ipvs(net);

	/* increase the module use count */
	ip_vs_use_count_inc();

	/* Lookup the scheduler by 'u->sched_name' */
	sched = ip_vs_scheduler_get(u->sched_name);
	if (sched == NULL) {
		pr_info("Scheduler module ip_vs_%s not found\n", u->sched_name);
		ret = -ENOENT;
		goto out_err;
	}

	if (u->pe_name && *u->pe_name) {
		pe = ip_vs_pe_getbyname(u->pe_name);
		if (pe == NULL) {
			pr_info("persistence engine module ip_vs_pe_%s "
				"not found\n", u->pe_name);
			ret = -ENOENT;
			goto out_err;
		}
	}

#ifdef CONFIG_IP_VS_IPV6
	if (u->af == AF_INET6) {
		__u32 plen = (__force __u32) u->netmask;

		if (plen < 1 || plen > 128) {
			ret = -EINVAL;
			goto out_err;
		}
	}
#endif

	svc = kzalloc(sizeof(struct ip_vs_service), GFP_KERNEL);
	if (svc == NULL) {
		IP_VS_DBG(1, "%s(): no memory\n", __func__);
		ret = -ENOMEM;
		goto out_err;
	}
	svc->stats.cpustats = alloc_percpu(struct ip_vs_cpu_stats);
	if (!svc->stats.cpustats) {
		ret = -ENOMEM;
		goto out_err;
	}

	for_each_possible_cpu(i) {
		struct ip_vs_cpu_stats *ip_vs_stats;
		ip_vs_stats = per_cpu_ptr(svc->stats.cpustats, i);
		u64_stats_init(&ip_vs_stats->syncp);
	}


	/* I'm the first user of the service */
	atomic_set(&svc->refcnt, 0);

	svc->af = u->af;
	svc->protocol = u->protocol;
	ip_vs_addr_copy(svc->af, &svc->addr, &u->addr);
	svc->port = u->port;
	svc->fwmark = u->fwmark;
	svc->flags = u->flags;
	svc->timeout = u->timeout * HZ;
	svc->netmask = u->netmask;
	svc->net = net;

	INIT_LIST_HEAD(&svc->destinations);
	spin_lock_init(&svc->sched_lock);
	spin_lock_init(&svc->stats.lock);

	/* Bind the scheduler */
	ret = ip_vs_bind_scheduler(svc, sched);
	if (ret)
		goto out_err;
	sched = NULL;

	/* Bind the ct retriever */
	RCU_INIT_POINTER(svc->pe, pe);
	pe = NULL;

	/* Update the virtual service counters */
	if (svc->port == FTPPORT)
		atomic_inc(&ipvs->ftpsvc_counter);
	else if (svc->port == 0)
		atomic_inc(&ipvs->nullsvc_counter);

	ip_vs_start_estimator(net, &svc->stats);

	/* Count only IPv4 services for old get/setsockopt interface */
	if (svc->af == AF_INET)
		ipvs->num_services++;

	/* Hash the service into the service table */
	ip_vs_svc_hash(svc);

	*svc_p = svc;
	/* Now there is a service - full throttle */
	ipvs->enable = 1;
	return 0;


 out_err:
	if (svc != NULL) {
		ip_vs_unbind_scheduler(svc, sched);
		ip_vs_service_free(svc);
	}
	ip_vs_scheduler_put(sched);
	ip_vs_pe_put(pe);

	/* decrease the module use count */
	ip_vs_use_count_dec();

	return ret;
}


/*
 *	Edit a service and bind it with a new scheduler
 */
static int
ip_vs_edit_service(struct ip_vs_service *svc, struct ip_vs_service_user_kern *u)
{
	struct ip_vs_scheduler *sched, *old_sched;
	struct ip_vs_pe *pe = NULL, *old_pe = NULL;
	int ret = 0;

	/*
	 * Lookup the scheduler, by 'u->sched_name'
	 */
	sched = ip_vs_scheduler_get(u->sched_name);
	if (sched == NULL) {
		pr_info("Scheduler module ip_vs_%s not found\n", u->sched_name);
		return -ENOENT;
	}
	old_sched = sched;

	if (u->pe_name && *u->pe_name) {
		pe = ip_vs_pe_getbyname(u->pe_name);
		if (pe == NULL) {
			pr_info("persistence engine module ip_vs_pe_%s "
				"not found\n", u->pe_name);
			ret = -ENOENT;
			goto out;
		}
		old_pe = pe;
	}

#ifdef CONFIG_IP_VS_IPV6
	if (u->af == AF_INET6) {
		__u32 plen = (__force __u32) u->netmask;

		if (plen < 1 || plen > 128) {
			ret = -EINVAL;
			goto out;
		}
	}
#endif

	old_sched = rcu_dereference_protected(svc->scheduler, 1);
	if (sched != old_sched) {
		/* Bind the new scheduler */
		ret = ip_vs_bind_scheduler(svc, sched);
		if (ret) {
			old_sched = sched;
			goto out;
		}
		/* Unbind the old scheduler on success */
		ip_vs_unbind_scheduler(svc, old_sched);
	}

	/*
	 * Set the flags and timeout value
	 */
	svc->flags = u->flags | IP_VS_SVC_F_HASHED;
	svc->timeout = u->timeout * HZ;
	svc->netmask = u->netmask;

	old_pe = rcu_dereference_protected(svc->pe, 1);
	if (pe != old_pe)
		rcu_assign_pointer(svc->pe, pe);

out:
	ip_vs_scheduler_put(old_sched);
	ip_vs_pe_put(old_pe);
	return ret;
}

/*
 *	Delete a service from the service list
 *	- The service must be unlinked, unlocked and not referenced!
 *	- We are called under _bh lock
 */
static void __ip_vs_del_service(struct ip_vs_service *svc, bool cleanup)
{
	struct ip_vs_dest *dest, *nxt;
	struct ip_vs_scheduler *old_sched;
	struct ip_vs_pe *old_pe;
	struct netns_ipvs *ipvs = net_ipvs(svc->net);

	pr_info("%s: enter\n", __func__);

	/* Count only IPv4 services for old get/setsockopt interface */
	if (svc->af == AF_INET)
		ipvs->num_services--;

	ip_vs_stop_estimator(svc->net, &svc->stats);

	/* Unbind scheduler */
	old_sched = rcu_dereference_protected(svc->scheduler, 1);
	ip_vs_unbind_scheduler(svc, old_sched);
	ip_vs_scheduler_put(old_sched);

	/* Unbind persistence engine, keep svc->pe */
	old_pe = rcu_dereference_protected(svc->pe, 1);
	ip_vs_pe_put(old_pe);

	/*
	 *    Unlink the whole destination list
	 */
	list_for_each_entry_safe(dest, nxt, &svc->destinations, n_list) {
		__ip_vs_unlink_dest(svc, dest, 0);
		__ip_vs_del_dest(svc->net, dest, cleanup);
	}

	/*
	 *    Update the virtual service counters
	 */
	if (svc->port == FTPPORT)
		atomic_dec(&ipvs->ftpsvc_counter);
	else if (svc->port == 0)
		atomic_dec(&ipvs->nullsvc_counter);

	/*
	 *    Free the service if nobody refers to it
	 */
	__ip_vs_svc_put(svc, true);

	/* decrease the module use count */
	ip_vs_use_count_dec();
}

/*
 * Unlink a service from list and try to delete it if its refcnt reached 0
 */
static void ip_vs_unlink_service(struct ip_vs_service *svc, bool cleanup)
{
	/* Hold svc to avoid double release from dest_trash */
	atomic_inc(&svc->refcnt);
	/*
	 * Unhash it from the service table
	 */
	ip_vs_svc_unhash(svc);

	__ip_vs_del_service(svc, cleanup);
}

/*
 *	Delete a service from the service list
 */
static int ip_vs_del_service(struct ip_vs_service *svc)
{
	if (svc == NULL)
		return -EEXIST;
	ip_vs_unlink_service(svc, false);

	return 0;
}


/*
 *	Flush all the virtual services
 */
static int ip_vs_flush(struct net *net, bool cleanup)
{
	int idx;
	struct ip_vs_service *svc;
	struct hlist_node *n;

	/*
	 * Flush the service table hashed by <netns,protocol,addr,port>
	 */
	for(idx = 0; idx < IP_VS_SVC_TAB_SIZE; idx++) {
		hlist_for_each_entry_safe(svc, n, &ip_vs_svc_table[idx],
					  s_list) {
			if (net_eq(svc->net, net))
				ip_vs_unlink_service(svc, cleanup);
		}
	}

	/*
	 * Flush the service table hashed by fwmark
	 */
	for(idx = 0; idx < IP_VS_SVC_TAB_SIZE; idx++) {
		hlist_for_each_entry_safe(svc, n, &ip_vs_svc_fwm_table[idx],
					  f_list) {
			if (net_eq(svc->net, net))
				ip_vs_unlink_service(svc, cleanup);
		}
	}

	return 0;
}

/*
 *	Delete service by {netns} in the service table.
 *	Called by __ip_vs_cleanup()
 */
void ip_vs_service_net_cleanup(struct net *net)
{
	EnterFunction(2);
	/* Check for "full" addressed entries */
	mutex_lock(&__ip_vs_mutex);
	ip_vs_flush(net, true);
	mutex_unlock(&__ip_vs_mutex);
	LeaveFunction(2);
}

/* Put all references for device (dst_cache) */
static inline void
ip_vs_forget_dev(struct ip_vs_dest *dest, struct net_device *dev)
{
	struct ip_vs_dest_dst *dest_dst;

	spin_lock_bh(&dest->dst_lock);
	dest_dst = rcu_dereference_protected(dest->dest_dst, 1);
	if (dest_dst && dest_dst->dst_cache->dev == dev) {
		IP_VS_DBG_BUF(3, "Reset dev:%s dest %s:%u ,dest->refcnt=%d\n",
			      dev->name,
			      IP_VS_DBG_ADDR(dest->af, &dest->addr),
			      ntohs(dest->port),
			      atomic_read(&dest->refcnt));
		__ip_vs_dst_cache_reset(dest);
	}
	spin_unlock_bh(&dest->dst_lock);

}
/* Netdev event receiver
 * Currently only NETDEV_DOWN is handled to release refs to cached dsts
 */
static int ip_vs_dst_event(struct notifier_block *this, unsigned long event,
			   void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct net *net = dev_net(dev);
	struct netns_ipvs *ipvs = net_ipvs(net);
	struct ip_vs_service *svc;
	struct ip_vs_dest *dest;
	unsigned int idx;

	if (event != NETDEV_DOWN || !ipvs)
		return NOTIFY_DONE;
	IP_VS_DBG(3, "%s() dev=%s\n", __func__, dev->name);
	EnterFunction(2);
	mutex_lock(&__ip_vs_mutex);
	for (idx = 0; idx < IP_VS_SVC_TAB_SIZE; idx++) {
		hlist_for_each_entry(svc, &ip_vs_svc_table[idx], s_list) {
			if (net_eq(svc->net, net)) {
				list_for_each_entry(dest, &svc->destinations,
						    n_list) {
					ip_vs_forget_dev(dest, dev);
				}
			}
		}

		hlist_for_each_entry(svc, &ip_vs_svc_fwm_table[idx], f_list) {
			if (net_eq(svc->net, net)) {
				list_for_each_entry(dest, &svc->destinations,
						    n_list) {
					ip_vs_forget_dev(dest, dev);
				}
			}

		}
	}

	spin_lock_bh(&ipvs->dest_trash_lock);
	list_for_each_entry(dest, &ipvs->dest_trash, t_list) {
		ip_vs_forget_dev(dest, dev);
	}
	spin_unlock_bh(&ipvs->dest_trash_lock);
	mutex_unlock(&__ip_vs_mutex);
	LeaveFunction(2);
	return NOTIFY_DONE;
}

/*
 *	Zero counters in a service or all services
 */
static int ip_vs_zero_service(struct ip_vs_service *svc)
{
	struct ip_vs_dest *dest;

	list_for_each_entry(dest, &svc->destinations, n_list) {
		ip_vs_zero_stats(&dest->stats);
	}
	ip_vs_zero_stats(&svc->stats);
	return 0;
}

static int ip_vs_zero_all(struct net *net)
{
	int idx;
	struct ip_vs_service *svc;

	for(idx = 0; idx < IP_VS_SVC_TAB_SIZE; idx++) {
		hlist_for_each_entry(svc, &ip_vs_svc_table[idx], s_list) {
			if (net_eq(svc->net, net))
				ip_vs_zero_service(svc);
		}
	}

	for(idx = 0; idx < IP_VS_SVC_TAB_SIZE; idx++) {
		hlist_for_each_entry(svc, &ip_vs_svc_fwm_table[idx], f_list) {
			if (net_eq(svc->net, net))
				ip_vs_zero_service(svc);
		}
	}

	ip_vs_zero_stats(&net_ipvs(net)->tot_stats);
	return 0;
}

#ifdef CONFIG_SYSCTL

static int zero;
static int three = 3;

static int
proc_do_defense_mode(struct ctl_table *table, int write,
		     void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct net *net = current->nsproxy->net_ns;
	int *valp = table->data;
	int val = *valp;
	int rc;

	rc = proc_dointvec(table, write, buffer, lenp, ppos);
	if (write && (*valp != val)) {
		if ((*valp < 0) || (*valp > 3)) {
			/* Restore the correct value */
			*valp = val;
		} else {
			update_defense_level(net_ipvs(net));
		}
	}
	return rc;
}

static int
proc_do_sync_threshold(struct ctl_table *table, int write,
		       void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int *valp = table->data;
	int val[2];
	int rc;

	/* backup the value first */
	memcpy(val, valp, sizeof(val));

	rc = proc_dointvec(table, write, buffer, lenp, ppos);
	if (write && (valp[0] < 0 || valp[1] < 0 ||
	    (valp[0] >= valp[1] && valp[1]))) {
		/* Restore the correct value */
		memcpy(valp, val, sizeof(val));
	}
	return rc;
}

static int
proc_do_sync_mode(struct ctl_table *table, int write,
		     void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int *valp = table->data;
	int val = *valp;
	int rc;

	rc = proc_dointvec(table, write, buffer, lenp, ppos);
	if (write && (*valp != val)) {
		if ((*valp < 0) || (*valp > 1)) {
			/* Restore the correct value */
			*valp = val;
		}
	}
	return rc;
}

static int
proc_do_sync_ports(struct ctl_table *table, int write,
		   void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int *valp = table->data;
	int val = *valp;
	int rc;

	rc = proc_dointvec(table, write, buffer, lenp, ppos);
	if (write && (*valp != val)) {
		if (*valp < 1 || !is_power_of_2(*valp)) {
			/* Restore the correct value */
			*valp = val;
		}
	}
	return rc;
}

/*
 *	IPVS sysctl table (under the /proc/sys/net/ipv4/vs/)
 *	Do not change order or insert new entries without
 *	align with netns init in ip_vs_control_net_init()
 */

static struct ctl_table vs_vars[] = {
	{
		.procname	= "amemthresh",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "am_droprate",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "drop_entry",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_do_defense_mode,
	},
	{
		.procname	= "drop_packet",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_do_defense_mode,
	},
#ifdef CONFIG_IP_VS_NFCT
	{
		.procname	= "conntrack",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
	{
		.procname	= "secure_tcp",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_do_defense_mode,
	},
	{
		.procname	= "snat_reroute",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.procname	= "sync_version",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_do_sync_mode,
	},
	{
		.procname	= "sync_ports",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_do_sync_ports,
	},
	{
		.procname	= "sync_persist_mode",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "sync_qlen_max",
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
	},
	{
		.procname	= "sync_sock_size",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "cache_bypass",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "expire_nodest_conn",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "sloppy_tcp",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "sloppy_sctp",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "expire_quiescent_template",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "sync_threshold",
		.maxlen		=
			sizeof(((struct netns_ipvs *)0)->sysctl_sync_threshold),
		.mode		= 0644,
		.proc_handler	= proc_do_sync_threshold,
	},
	{
		.procname	= "sync_refresh_period",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{
		.procname	= "sync_retries",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &three,
	},
	{
		.procname	= "nat_icmp_send",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "pmtu_disc",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "backup_only",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "conn_reuse_mode",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#ifdef CONFIG_IP_VS_DEBUG
	{
		.procname	= "debug_level",
		.data		= &sysctl_ip_vs_debug_level,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
	{ }
};

#endif

#ifdef CONFIG_PROC_FS

struct ip_vs_iter {
	struct seq_net_private p;  /* Do not move this, netns depends upon it*/
	struct hlist_head *table;
	int bucket;
};

/*
 *	Write the contents of the VS rule table to a PROCfs file.
 *	(It is kept just for backward compatibility)
 */
static inline const char *ip_vs_fwd_name(unsigned int flags)
{
	switch (flags & IP_VS_CONN_F_FWD_MASK) {
	case IP_VS_CONN_F_LOCALNODE:
		return "Local";
	case IP_VS_CONN_F_TUNNEL:
		return "Tunnel";
	case IP_VS_CONN_F_DROUTE:
		return "Route";
	default:
		return "Masq";
	}
}


/* Get the Nth entry in the two lists */
static struct ip_vs_service *ip_vs_info_array(struct seq_file *seq, loff_t pos)
{
	struct net *net = seq_file_net(seq);
	struct ip_vs_iter *iter = seq->private;
	int idx;
	struct ip_vs_service *svc;

	/* look in hash by protocol */
	for (idx = 0; idx < IP_VS_SVC_TAB_SIZE; idx++) {
		hlist_for_each_entry_rcu(svc, &ip_vs_svc_table[idx], s_list) {
			if (net_eq(svc->net, net) && pos-- == 0) {
				iter->table = ip_vs_svc_table;
				iter->bucket = idx;
				return svc;
			}
		}
	}

	/* keep looking in fwmark */
	for (idx = 0; idx < IP_VS_SVC_TAB_SIZE; idx++) {
		hlist_for_each_entry_rcu(svc, &ip_vs_svc_fwm_table[idx],
					 f_list) {
			if (net_eq(svc->net, net) && pos-- == 0) {
				iter->table = ip_vs_svc_fwm_table;
				iter->bucket = idx;
				return svc;
			}
		}
	}

	return NULL;
}

static void *ip_vs_info_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(RCU)
{
	rcu_read_lock();
	return *pos ? ip_vs_info_array(seq, *pos - 1) : SEQ_START_TOKEN;
}


static void *ip_vs_info_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct hlist_node *e;
	struct ip_vs_iter *iter;
	struct ip_vs_service *svc;

	++*pos;
	if (v == SEQ_START_TOKEN)
		return ip_vs_info_array(seq,0);

	svc = v;
	iter = seq->private;

	if (iter->table == ip_vs_svc_table) {
		/* next service in table hashed by protocol */
		e = rcu_dereference(hlist_next_rcu(&svc->s_list));
		if (e)
			return hlist_entry(e, struct ip_vs_service, s_list);

		while (++iter->bucket < IP_VS_SVC_TAB_SIZE) {
			hlist_for_each_entry_rcu(svc,
						 &ip_vs_svc_table[iter->bucket],
						 s_list) {
				return svc;
			}
		}

		iter->table = ip_vs_svc_fwm_table;
		iter->bucket = -1;
		goto scan_fwmark;
	}

	/* next service in hashed by fwmark */
	e = rcu_dereference(hlist_next_rcu(&svc->f_list));
	if (e)
		return hlist_entry(e, struct ip_vs_service, f_list);

 scan_fwmark:
	while (++iter->bucket < IP_VS_SVC_TAB_SIZE) {
		hlist_for_each_entry_rcu(svc,
					 &ip_vs_svc_fwm_table[iter->bucket],
					 f_list)
			return svc;
	}

	return NULL;
}

static void ip_vs_info_seq_stop(struct seq_file *seq, void *v)
	__releases(RCU)
{
	rcu_read_unlock();
}


static int ip_vs_info_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN) {
		seq_printf(seq,
			"IP Virtual Server version %d.%d.%d (size=%d)\n",
			NVERSION(IP_VS_VERSION_CODE), ip_vs_conn_tab_size);
		seq_puts(seq,
			 "Prot LocalAddress:Port Scheduler Flags\n");
		seq_puts(seq,
			 "  -> RemoteAddress:Port Forward Weight ActiveConn InActConn\n");
	} else {
		const struct ip_vs_service *svc = v;
		const struct ip_vs_iter *iter = seq->private;
		const struct ip_vs_dest *dest;
		struct ip_vs_scheduler *sched = rcu_dereference(svc->scheduler);

		if (iter->table == ip_vs_svc_table) {
#ifdef CONFIG_IP_VS_IPV6
			if (svc->af == AF_INET6)
				seq_printf(seq, "%s  [%pI6]:%04X %s ",
					   ip_vs_proto_name(svc->protocol),
					   &svc->addr.in6,
					   ntohs(svc->port),
					   sched->name);
			else
#endif
				seq_printf(seq, "%s  %08X:%04X %s %s ",
					   ip_vs_proto_name(svc->protocol),
					   ntohl(svc->addr.ip),
					   ntohs(svc->port),
					   sched->name,
					   (svc->flags & IP_VS_SVC_F_ONEPACKET)?"ops ":"");
		} else {
			seq_printf(seq, "FWM  %08X %s %s",
				   svc->fwmark, sched->name,
				   (svc->flags & IP_VS_SVC_F_ONEPACKET)?"ops ":"");
		}

		if (svc->flags & IP_VS_SVC_F_PERSISTENT)
			seq_printf(seq, "persistent %d %08X\n",
				svc->timeout,
				ntohl(svc->netmask));
		else
			seq_putc(seq, '\n');

		list_for_each_entry_rcu(dest, &svc->destinations, n_list) {
#ifdef CONFIG_IP_VS_IPV6
			if (dest->af == AF_INET6)
				seq_printf(seq,
					   "  -> [%pI6]:%04X"
					   "      %-7s %-6d %-10d %-10d\n",
					   &dest->addr.in6,
					   ntohs(dest->port),
					   ip_vs_fwd_name(atomic_read(&dest->conn_flags)),
					   atomic_read(&dest->weight),
					   atomic_read(&dest->activeconns),
					   atomic_read(&dest->inactconns));
			else
#endif
				seq_printf(seq,
					   "  -> %08X:%04X      "
					   "%-7s %-6d %-10d %-10d\n",
					   ntohl(dest->addr.ip),
					   ntohs(dest->port),
					   ip_vs_fwd_name(atomic_read(&dest->conn_flags)),
					   atomic_read(&dest->weight),
					   atomic_read(&dest->activeconns),
					   atomic_read(&dest->inactconns));

		}
	}
	return 0;
}

static const struct seq_operations ip_vs_info_seq_ops = {
	.start = ip_vs_info_seq_start,
	.next  = ip_vs_info_seq_next,
	.stop  = ip_vs_info_seq_stop,
	.show  = ip_vs_info_seq_show,
};

static int ip_vs_info_open(struct inode *inode, struct file *file)
{
	return seq_open_net(inode, file, &ip_vs_info_seq_ops,
			sizeof(struct ip_vs_iter));
}

static const struct file_operations ip_vs_info_fops = {
	.owner	 = THIS_MODULE,
	.open    = ip_vs_info_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release_net,
};

static int ip_vs_stats_show(struct seq_file *seq, void *v)
{
	struct net *net = seq_file_single_net(seq);
	struct ip_vs_kstats show;

/*               01234567 01234567 01234567 0123456701234567 0123456701234567 */
	seq_puts(seq,
		 "   Total Incoming Outgoing         Incoming         Outgoing\n");
	seq_printf(seq,
		   "   Conns  Packets  Packets            Bytes            Bytes\n");

	ip_vs_copy_stats(&show, &net_ipvs(net)->tot_stats);
	seq_printf(seq, "%8LX %8LX %8LX %16LX %16LX\n\n",
		   (unsigned long long)show.conns,
		   (unsigned long long)show.inpkts,
		   (unsigned long long)show.outpkts,
		   (unsigned long long)show.inbytes,
		   (unsigned long long)show.outbytes);

/*                01234567 01234567 01234567 0123456701234567 0123456701234567*/
	seq_puts(seq,
		 " Conns/s   Pkts/s   Pkts/s          Bytes/s          Bytes/s\n");
	seq_printf(seq, "%8LX %8LX %8LX %16LX %16LX\n",
		   (unsigned long long)show.cps,
		   (unsigned long long)show.inpps,
		   (unsigned long long)show.outpps,
		   (unsigned long long)show.inbps,
		   (unsigned long long)show.outbps);

	return 0;
}

static int ip_vs_stats_seq_open(struct inode *inode, struct file *file)
{
	return single_open_net(inode, file, ip_vs_stats_show);
}

static const struct file_operations ip_vs_stats_fops = {
	.owner = THIS_MODULE,
	.open = ip_vs_stats_seq_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release_net,
};

static int ip_vs_stats_percpu_show(struct seq_file *seq, void *v)
{
	struct net *net = seq_file_single_net(seq);
	struct ip_vs_stats *tot_stats = &net_ipvs(net)->tot_stats;
	struct ip_vs_cpu_stats __percpu *cpustats = tot_stats->cpustats;
	struct ip_vs_kstats kstats;
	int i;

/*               01234567 01234567 01234567 0123456701234567 0123456701234567 */
	seq_puts(seq,
		 "       Total Incoming Outgoing         Incoming         Outgoing\n");
	seq_printf(seq,
		   "CPU    Conns  Packets  Packets            Bytes            Bytes\n");

	for_each_possible_cpu(i) {
		struct ip_vs_cpu_stats *u = per_cpu_ptr(cpustats, i);
		unsigned int start;
		u64 conns, inpkts, outpkts, inbytes, outbytes;

		do {
			start = u64_stats_fetch_begin_irq(&u->syncp);
			conns = u->cnt.conns;
			inpkts = u->cnt.inpkts;
			outpkts = u->cnt.outpkts;
			inbytes = u->cnt.inbytes;
			outbytes = u->cnt.outbytes;
		} while (u64_stats_fetch_retry_irq(&u->syncp, start));

		seq_printf(seq, "%3X %8LX %8LX %8LX %16LX %16LX\n",
			   i, (u64)conns, (u64)inpkts,
			   (u64)outpkts, (u64)inbytes,
			   (u64)outbytes);
	}

	ip_vs_copy_stats(&kstats, tot_stats);

	seq_printf(seq, "  ~ %8LX %8LX %8LX %16LX %16LX\n\n",
		   (unsigned long long)kstats.conns,
		   (unsigned long long)kstats.inpkts,
		   (unsigned long long)kstats.outpkts,
		   (unsigned long long)kstats.inbytes,
		   (unsigned long long)kstats.outbytes);

/*                ... 01234567 01234567 01234567 0123456701234567 0123456701234567 */
	seq_puts(seq,
		 "     Conns/s   Pkts/s   Pkts/s          Bytes/s          Bytes/s\n");
	seq_printf(seq, "    %8LX %8LX %8LX %16LX %16LX\n",
		   kstats.cps,
		   kstats.inpps,
		   kstats.outpps,
		   kstats.inbps,
		   kstats.outbps);

	return 0;
}

static int ip_vs_stats_percpu_seq_open(struct inode *inode, struct file *file)
{
	return single_open_net(inode, file, ip_vs_stats_percpu_show);
}

static const struct file_operations ip_vs_stats_percpu_fops = {
	.owner = THIS_MODULE,
	.open = ip_vs_stats_percpu_seq_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release_net,
};
#endif

/*
 *	Set timeout values for tcp tcpfin udp in the timeout_table.
 */
static int ip_vs_set_timeout(struct net *net, struct ip_vs_timeout_user *u)
{
#if defined(CONFIG_IP_VS_PROTO_TCP) || defined(CONFIG_IP_VS_PROTO_UDP)
	struct ip_vs_proto_data *pd;
#endif

	IP_VS_DBG(2, "Setting timeout tcp:%d tcpfin:%d udp:%d\n",
		  u->tcp_timeout,
		  u->tcp_fin_timeout,
		  u->udp_timeout);

#ifdef CONFIG_IP_VS_PROTO_TCP
	if (u->tcp_timeout) {
		pd = ip_vs_proto_data_get(net, IPPROTO_TCP);
		pd->timeout_table[IP_VS_TCP_S_ESTABLISHED]
			= u->tcp_timeout * HZ;
	}

	if (u->tcp_fin_timeout) {
		pd = ip_vs_proto_data_get(net, IPPROTO_TCP);
		pd->timeout_table[IP_VS_TCP_S_FIN_WAIT]
			= u->tcp_fin_timeout * HZ;
	}
#endif

#ifdef CONFIG_IP_VS_PROTO_UDP
	if (u->udp_timeout) {
		pd = ip_vs_proto_data_get(net, IPPROTO_UDP);
		pd->timeout_table[IP_VS_UDP_S_NORMAL]
			= u->udp_timeout * HZ;
	}
#endif
	return 0;
}

#define CMDID(cmd)		(cmd - IP_VS_BASE_CTL)

struct ip_vs_svcdest_user {
	struct ip_vs_service_user	s;
	struct ip_vs_dest_user		d;
};

static const unsigned char set_arglen[CMDID(IP_VS_SO_SET_MAX) + 1] = {
	[CMDID(IP_VS_SO_SET_ADD)]         = sizeof(struct ip_vs_service_user),
	[CMDID(IP_VS_SO_SET_EDIT)]        = sizeof(struct ip_vs_service_user),
	[CMDID(IP_VS_SO_SET_DEL)]         = sizeof(struct ip_vs_service_user),
	[CMDID(IP_VS_SO_SET_ADDDEST)]     = sizeof(struct ip_vs_svcdest_user),
	[CMDID(IP_VS_SO_SET_DELDEST)]     = sizeof(struct ip_vs_svcdest_user),
	[CMDID(IP_VS_SO_SET_EDITDEST)]    = sizeof(struct ip_vs_svcdest_user),
	[CMDID(IP_VS_SO_SET_TIMEOUT)]     = sizeof(struct ip_vs_timeout_user),
	[CMDID(IP_VS_SO_SET_STARTDAEMON)] = sizeof(struct ip_vs_daemon_user),
	[CMDID(IP_VS_SO_SET_STOPDAEMON)]  = sizeof(struct ip_vs_daemon_user),
	[CMDID(IP_VS_SO_SET_ZERO)]        = sizeof(struct ip_vs_service_user),
};

union ip_vs_set_arglen {
	struct ip_vs_service_user	field_IP_VS_SO_SET_ADD;
	struct ip_vs_service_user	field_IP_VS_SO_SET_EDIT;
	struct ip_vs_service_user	field_IP_VS_SO_SET_DEL;
	struct ip_vs_svcdest_user	field_IP_VS_SO_SET_ADDDEST;
	struct ip_vs_svcdest_user	field_IP_VS_SO_SET_DELDEST;
	struct ip_vs_svcdest_user	field_IP_VS_SO_SET_EDITDEST;
	struct ip_vs_timeout_user	field_IP_VS_SO_SET_TIMEOUT;
	struct ip_vs_daemon_user	field_IP_VS_SO_SET_STARTDAEMON;
	struct ip_vs_daemon_user	field_IP_VS_SO_SET_STOPDAEMON;
	struct ip_vs_service_user	field_IP_VS_SO_SET_ZERO;
};

#define MAX_SET_ARGLEN	sizeof(union ip_vs_set_arglen)

static void ip_vs_copy_usvc_compat(struct ip_vs_service_user_kern *usvc,
				  struct ip_vs_service_user *usvc_compat)
{
	memset(usvc, 0, sizeof(*usvc));

	usvc->af		= AF_INET;
	usvc->protocol		= usvc_compat->protocol;
	usvc->addr.ip		= usvc_compat->addr;
	usvc->port		= usvc_compat->port;
	usvc->fwmark		= usvc_compat->fwmark;

	/* Deep copy of sched_name is not needed here */
	usvc->sched_name	= usvc_compat->sched_name;

	usvc->flags		= usvc_compat->flags;
	usvc->timeout		= usvc_compat->timeout;
	usvc->netmask		= usvc_compat->netmask;
}

static void ip_vs_copy_udest_compat(struct ip_vs_dest_user_kern *udest,
				   struct ip_vs_dest_user *udest_compat)
{
	memset(udest, 0, sizeof(*udest));

	udest->addr.ip		= udest_compat->addr;
	udest->port		= udest_compat->port;
	udest->conn_flags	= udest_compat->conn_flags;
	udest->weight		= udest_compat->weight;
	udest->u_threshold	= udest_compat->u_threshold;
	udest->l_threshold	= udest_compat->l_threshold;
	udest->af		= AF_INET;
}

static int
do_ip_vs_set_ctl(struct sock *sk, int cmd, void __user *user, unsigned int len)
{
	struct net *net = sock_net(sk);
	int ret;
	unsigned char arg[MAX_SET_ARGLEN];
	struct ip_vs_service_user *usvc_compat;
	struct ip_vs_service_user_kern usvc;
	struct ip_vs_service *svc;
	struct ip_vs_dest_user *udest_compat;
	struct ip_vs_dest_user_kern udest;
	struct netns_ipvs *ipvs = net_ipvs(net);

	BUILD_BUG_ON(sizeof(arg) > 255);
	if (!ns_capable(sock_net(sk)->user_ns, CAP_NET_ADMIN))
		return -EPERM;

	if (cmd < IP_VS_BASE_CTL || cmd > IP_VS_SO_SET_MAX)
		return -EINVAL;
	if (len != set_arglen[CMDID(cmd)]) {
		IP_VS_DBG(1, "set_ctl: len %u != %u\n",
			  len, set_arglen[CMDID(cmd)]);
		return -EINVAL;
	}

	if (copy_from_user(arg, user, len) != 0)
		return -EFAULT;

	/* increase the module use count */
	ip_vs_use_count_inc();

	/* Handle daemons since they have another lock */
	if (cmd == IP_VS_SO_SET_STARTDAEMON ||
	    cmd == IP_VS_SO_SET_STOPDAEMON) {
		struct ip_vs_daemon_user *dm = (struct ip_vs_daemon_user *)arg;

		mutex_lock(&ipvs->sync_mutex);
		if (cmd == IP_VS_SO_SET_STARTDAEMON)
			ret = start_sync_thread(net, dm->state, dm->mcast_ifn,
						dm->syncid);
		else
			ret = stop_sync_thread(net, dm->state);
		mutex_unlock(&ipvs->sync_mutex);
		goto out_dec;
	}

	mutex_lock(&__ip_vs_mutex);
	if (cmd == IP_VS_SO_SET_FLUSH) {
		/* Flush the virtual service */
		ret = ip_vs_flush(net, false);
		goto out_unlock;
	} else if (cmd == IP_VS_SO_SET_TIMEOUT) {
		/* Set timeout values for (tcp tcpfin udp) */
		ret = ip_vs_set_timeout(net, (struct ip_vs_timeout_user *)arg);
		goto out_unlock;
	}

	usvc_compat = (struct ip_vs_service_user *)arg;
	udest_compat = (struct ip_vs_dest_user *)(usvc_compat + 1);

	/* We only use the new structs internally, so copy userspace compat
	 * structs to extended internal versions */
	ip_vs_copy_usvc_compat(&usvc, usvc_compat);
	ip_vs_copy_udest_compat(&udest, udest_compat);

	if (cmd == IP_VS_SO_SET_ZERO) {
		/* if no service address is set, zero counters in all */
		if (!usvc.fwmark && !usvc.addr.ip && !usvc.port) {
			ret = ip_vs_zero_all(net);
			goto out_unlock;
		}
	}

	/* Check for valid protocol: TCP or UDP or SCTP, even for fwmark!=0 */
	if (usvc.protocol != IPPROTO_TCP && usvc.protocol != IPPROTO_UDP &&
	    usvc.protocol != IPPROTO_SCTP) {
		pr_err("set_ctl: invalid protocol: %d %pI4:%d %s\n",
		       usvc.protocol, &usvc.addr.ip,
		       ntohs(usvc.port), usvc.sched_name);
		ret = -EFAULT;
		goto out_unlock;
	}

	/* Lookup the exact service by <protocol, addr, port> or fwmark */
	rcu_read_lock();
	if (usvc.fwmark == 0)
		svc = __ip_vs_service_find(net, usvc.af, usvc.protocol,
					   &usvc.addr, usvc.port);
	else
		svc = __ip_vs_svc_fwm_find(net, usvc.af, usvc.fwmark);
	rcu_read_unlock();

	if (cmd != IP_VS_SO_SET_ADD
	    && (svc == NULL || svc->protocol != usvc.protocol)) {
		ret = -ESRCH;
		goto out_unlock;
	}

	switch (cmd) {
	case IP_VS_SO_SET_ADD:
		if (svc != NULL)
			ret = -EEXIST;
		else
			ret = ip_vs_add_service(net, &usvc, &svc);
		break;
	case IP_VS_SO_SET_EDIT:
		ret = ip_vs_edit_service(svc, &usvc);
		break;
	case IP_VS_SO_SET_DEL:
		ret = ip_vs_del_service(svc);
		if (!ret)
			goto out_unlock;
		break;
	case IP_VS_SO_SET_ZERO:
		ret = ip_vs_zero_service(svc);
		break;
	case IP_VS_SO_SET_ADDDEST:
		ret = ip_vs_add_dest(svc, &udest);
		break;
	case IP_VS_SO_SET_EDITDEST:
		ret = ip_vs_edit_dest(svc, &udest);
		break;
	case IP_VS_SO_SET_DELDEST:
		ret = ip_vs_del_dest(svc, &udest);
		break;
	default:
		ret = -EINVAL;
	}

  out_unlock:
	mutex_unlock(&__ip_vs_mutex);
  out_dec:
	/* decrease the module use count */
	ip_vs_use_count_dec();

	return ret;
}


static void
ip_vs_copy_service(struct ip_vs_service_entry *dst, struct ip_vs_service *src)
{
	struct ip_vs_scheduler *sched;
	struct ip_vs_kstats kstats;

	sched = rcu_dereference_protected(src->scheduler, 1);
	dst->protocol = src->protocol;
	dst->addr = src->addr.ip;
	dst->port = src->port;
	dst->fwmark = src->fwmark;
	strlcpy(dst->sched_name, sched->name, sizeof(dst->sched_name));
	dst->flags = src->flags;
	dst->timeout = src->timeout / HZ;
	dst->netmask = src->netmask;
	dst->num_dests = src->num_dests;
	ip_vs_copy_stats(&kstats, &src->stats);
	ip_vs_export_stats_user(&dst->stats, &kstats);
}

static inline int
__ip_vs_get_service_entries(struct net *net,
			    const struct ip_vs_get_services *get,
			    struct ip_vs_get_services __user *uptr)
{
	int idx, count=0;
	struct ip_vs_service *svc;
	struct ip_vs_service_entry entry;
	int ret = 0;

	for (idx = 0; idx < IP_VS_SVC_TAB_SIZE; idx++) {
		hlist_for_each_entry(svc, &ip_vs_svc_table[idx], s_list) {
			/* Only expose IPv4 entries to old interface */
			if (svc->af != AF_INET || !net_eq(svc->net, net))
				continue;

			if (count >= get->num_services)
				goto out;
			memset(&entry, 0, sizeof(entry));
			ip_vs_copy_service(&entry, svc);
			if (copy_to_user(&uptr->entrytable[count],
					 &entry, sizeof(entry))) {
				ret = -EFAULT;
				goto out;
			}
			count++;
		}
	}

	for (idx = 0; idx < IP_VS_SVC_TAB_SIZE; idx++) {
		hlist_for_each_entry(svc, &ip_vs_svc_fwm_table[idx], f_list) {
			/* Only expose IPv4 entries to old interface */
			if (svc->af != AF_INET || !net_eq(svc->net, net))
				continue;

			if (count >= get->num_services)
				goto out;
			memset(&entry, 0, sizeof(entry));
			ip_vs_copy_service(&entry, svc);
			if (copy_to_user(&uptr->entrytable[count],
					 &entry, sizeof(entry))) {
				ret = -EFAULT;
				goto out;
			}
			count++;
		}
	}
out:
	return ret;
}

static inline int
__ip_vs_get_dest_entries(struct net *net, const struct ip_vs_get_dests *get,
			 struct ip_vs_get_dests __user *uptr)
{
	struct ip_vs_service *svc;
	union nf_inet_addr addr = { .ip = get->addr };
	int ret = 0;

	rcu_read_lock();
	if (get->fwmark)
		svc = __ip_vs_svc_fwm_find(net, AF_INET, get->fwmark);
	else
		svc = __ip_vs_service_find(net, AF_INET, get->protocol, &addr,
					   get->port);
	rcu_read_unlock();

	if (svc) {
		int count = 0;
		struct ip_vs_dest *dest;
		struct ip_vs_dest_entry entry;
		struct ip_vs_kstats kstats;

		memset(&entry, 0, sizeof(entry));
		list_for_each_entry(dest, &svc->destinations, n_list) {
			if (count >= get->num_dests)
				break;

			/* Cannot expose heterogeneous members via sockopt
			 * interface
			 */
			if (dest->af != svc->af)
				continue;

			entry.addr = dest->addr.ip;
			entry.port = dest->port;
			entry.conn_flags = atomic_read(&dest->conn_flags);
			entry.weight = atomic_read(&dest->weight);
			entry.u_threshold = dest->u_threshold;
			entry.l_threshold = dest->l_threshold;
			entry.activeconns = atomic_read(&dest->activeconns);
			entry.inactconns = atomic_read(&dest->inactconns);
			entry.persistconns = atomic_read(&dest->persistconns);
			ip_vs_copy_stats(&kstats, &dest->stats);
			ip_vs_export_stats_user(&entry.stats, &kstats);
			if (copy_to_user(&uptr->entrytable[count],
					 &entry, sizeof(entry))) {
				ret = -EFAULT;
				break;
			}
			count++;
		}
	} else
		ret = -ESRCH;
	return ret;
}

static inline void
__ip_vs_get_timeouts(struct net *net, struct ip_vs_timeout_user *u)
{
#if defined(CONFIG_IP_VS_PROTO_TCP) || defined(CONFIG_IP_VS_PROTO_UDP)
	struct ip_vs_proto_data *pd;
#endif

	memset(u, 0, sizeof (*u));

#ifdef CONFIG_IP_VS_PROTO_TCP
	pd = ip_vs_proto_data_get(net, IPPROTO_TCP);
	u->tcp_timeout = pd->timeout_table[IP_VS_TCP_S_ESTABLISHED] / HZ;
	u->tcp_fin_timeout = pd->timeout_table[IP_VS_TCP_S_FIN_WAIT] / HZ;
#endif
#ifdef CONFIG_IP_VS_PROTO_UDP
	pd = ip_vs_proto_data_get(net, IPPROTO_UDP);
	u->udp_timeout =
			pd->timeout_table[IP_VS_UDP_S_NORMAL] / HZ;
#endif
}

static const unsigned char get_arglen[CMDID(IP_VS_SO_GET_MAX) + 1] = {
	[CMDID(IP_VS_SO_GET_VERSION)]  = 64,
	[CMDID(IP_VS_SO_GET_INFO)]     = sizeof(struct ip_vs_getinfo),
	[CMDID(IP_VS_SO_GET_SERVICES)] = sizeof(struct ip_vs_get_services),
	[CMDID(IP_VS_SO_GET_SERVICE)]  = sizeof(struct ip_vs_service_entry),
	[CMDID(IP_VS_SO_GET_DESTS)]    = sizeof(struct ip_vs_get_dests),
	[CMDID(IP_VS_SO_GET_TIMEOUT)]  = sizeof(struct ip_vs_timeout_user),
	[CMDID(IP_VS_SO_GET_DAEMON)]   = 2 * sizeof(struct ip_vs_daemon_user),
};

union ip_vs_get_arglen {
	char				field_IP_VS_SO_GET_VERSION[64];
	struct ip_vs_getinfo		field_IP_VS_SO_GET_INFO;
	struct ip_vs_get_services	field_IP_VS_SO_GET_SERVICES;
	struct ip_vs_service_entry	field_IP_VS_SO_GET_SERVICE;
	struct ip_vs_get_dests		field_IP_VS_SO_GET_DESTS;
	struct ip_vs_timeout_user	field_IP_VS_SO_GET_TIMEOUT;
	struct ip_vs_daemon_user	field_IP_VS_SO_GET_DAEMON[2];
};

#define MAX_GET_ARGLEN	sizeof(union ip_vs_get_arglen)

static int
do_ip_vs_get_ctl(struct sock *sk, int cmd, void __user *user, int *len)
{
	unsigned char arg[MAX_GET_ARGLEN];
	int ret = 0;
	unsigned int copylen;
	struct net *net = sock_net(sk);
	struct netns_ipvs *ipvs = net_ipvs(net);

	BUG_ON(!net);
	BUILD_BUG_ON(sizeof(arg) > 255);
	if (!ns_capable(sock_net(sk)->user_ns, CAP_NET_ADMIN))
		return -EPERM;

	if (cmd < IP_VS_BASE_CTL || cmd > IP_VS_SO_GET_MAX)
		return -EINVAL;

	copylen = get_arglen[CMDID(cmd)];
	if (*len < (int) copylen) {
		IP_VS_DBG(1, "get_ctl: len %d < %u\n", *len, copylen);
		return -EINVAL;
	}

	if (copy_from_user(arg, user, copylen) != 0)
		return -EFAULT;
	/*
	 * Handle daemons first since it has its own locking
	 */
	if (cmd == IP_VS_SO_GET_DAEMON) {
		struct ip_vs_daemon_user d[2];

		memset(&d, 0, sizeof(d));
		mutex_lock(&ipvs->sync_mutex);
		if (ipvs->sync_state & IP_VS_STATE_MASTER) {
			d[0].state = IP_VS_STATE_MASTER;
			strlcpy(d[0].mcast_ifn, ipvs->master_mcast_ifn,
				sizeof(d[0].mcast_ifn));
			d[0].syncid = ipvs->master_syncid;
		}
		if (ipvs->sync_state & IP_VS_STATE_BACKUP) {
			d[1].state = IP_VS_STATE_BACKUP;
			strlcpy(d[1].mcast_ifn, ipvs->backup_mcast_ifn,
				sizeof(d[1].mcast_ifn));
			d[1].syncid = ipvs->backup_syncid;
		}
		if (copy_to_user(user, &d, sizeof(d)) != 0)
			ret = -EFAULT;
		mutex_unlock(&ipvs->sync_mutex);
		return ret;
	}

	mutex_lock(&__ip_vs_mutex);
	switch (cmd) {
	case IP_VS_SO_GET_VERSION:
	{
		char buf[64];

		sprintf(buf, "IP Virtual Server version %d.%d.%d (size=%d)",
			NVERSION(IP_VS_VERSION_CODE), ip_vs_conn_tab_size);
		if (copy_to_user(user, buf, strlen(buf)+1) != 0) {
			ret = -EFAULT;
			goto out;
		}
		*len = strlen(buf)+1;
	}
	break;

	case IP_VS_SO_GET_INFO:
	{
		struct ip_vs_getinfo info;
		info.version = IP_VS_VERSION_CODE;
		info.size = ip_vs_conn_tab_size;
		info.num_services = ipvs->num_services;
		if (copy_to_user(user, &info, sizeof(info)) != 0)
			ret = -EFAULT;
	}
	break;

	case IP_VS_SO_GET_SERVICES:
	{
		struct ip_vs_get_services *get;
		int size;

		get = (struct ip_vs_get_services *)arg;
		size = sizeof(*get) +
			sizeof(struct ip_vs_service_entry) * get->num_services;
		if (*len != size) {
			pr_err("length: %u != %u\n", *len, size);
			ret = -EINVAL;
			goto out;
		}
		ret = __ip_vs_get_service_entries(net, get, user);
	}
	break;

	case IP_VS_SO_GET_SERVICE:
	{
		struct ip_vs_service_entry *entry;
		struct ip_vs_service *svc;
		union nf_inet_addr addr;

		entry = (struct ip_vs_service_entry *)arg;
		addr.ip = entry->addr;
		rcu_read_lock();
		if (entry->fwmark)
			svc = __ip_vs_svc_fwm_find(net, AF_INET, entry->fwmark);
		else
			svc = __ip_vs_service_find(net, AF_INET,
						   entry->protocol, &addr,
						   entry->port);
		rcu_read_unlock();
		if (svc) {
			ip_vs_copy_service(entry, svc);
			if (copy_to_user(user, entry, sizeof(*entry)) != 0)
				ret = -EFAULT;
		} else
			ret = -ESRCH;
	}
	break;

	case IP_VS_SO_GET_DESTS:
	{
		struct ip_vs_get_dests *get;
		int size;

		get = (struct ip_vs_get_dests *)arg;
		size = sizeof(*get) +
			sizeof(struct ip_vs_dest_entry) * get->num_dests;
		if (*len != size) {
			pr_err("length: %u != %u\n", *len, size);
			ret = -EINVAL;
			goto out;
		}
		ret = __ip_vs_get_dest_entries(net, get, user);
	}
	break;

	case IP_VS_SO_GET_TIMEOUT:
	{
		struct ip_vs_timeout_user t;

		__ip_vs_get_timeouts(net, &t);
		if (copy_to_user(user, &t, sizeof(t)) != 0)
			ret = -EFAULT;
	}
	break;

	default:
		ret = -EINVAL;
	}

out:
	mutex_unlock(&__ip_vs_mutex);
	return ret;
}


static struct nf_sockopt_ops ip_vs_sockopts = {
	.pf		= PF_INET,
	.set_optmin	= IP_VS_BASE_CTL,
	.set_optmax	= IP_VS_SO_SET_MAX+1,
	.set		= do_ip_vs_set_ctl,
	.get_optmin	= IP_VS_BASE_CTL,
	.get_optmax	= IP_VS_SO_GET_MAX+1,
	.get		= do_ip_vs_get_ctl,
	.owner		= THIS_MODULE,
};

/*
 * Generic Netlink interface
 */

/* IPVS genetlink family */
static struct genl_family ip_vs_genl_family = {
	.id		= GENL_ID_GENERATE,
	.hdrsize	= 0,
	.name		= IPVS_GENL_NAME,
	.version	= IPVS_GENL_VERSION,
	.maxattr	= IPVS_CMD_MAX,
	.netnsok        = true,         /* Make ipvsadm to work on netns */
};

/* Policy used for first-level command attributes */
static const struct nla_policy ip_vs_cmd_policy[IPVS_CMD_ATTR_MAX + 1] = {
	[IPVS_CMD_ATTR_SERVICE]		= { .type = NLA_NESTED },
	[IPVS_CMD_ATTR_DEST]		= { .type = NLA_NESTED },
	[IPVS_CMD_ATTR_DAEMON]		= { .type = NLA_NESTED },
	[IPVS_CMD_ATTR_TIMEOUT_TCP]	= { .type = NLA_U32 },
	[IPVS_CMD_ATTR_TIMEOUT_TCP_FIN]	= { .type = NLA_U32 },
	[IPVS_CMD_ATTR_TIMEOUT_UDP]	= { .type = NLA_U32 },
};

/* Policy used for attributes in nested attribute IPVS_CMD_ATTR_DAEMON */
static const struct nla_policy ip_vs_daemon_policy[IPVS_DAEMON_ATTR_MAX + 1] = {
	[IPVS_DAEMON_ATTR_STATE]	= { .type = NLA_U32 },
	[IPVS_DAEMON_ATTR_MCAST_IFN]	= { .type = NLA_NUL_STRING,
					    .len = IP_VS_IFNAME_MAXLEN },
	[IPVS_DAEMON_ATTR_SYNC_ID]	= { .type = NLA_U32 },
};

/* Policy used for attributes in nested attribute IPVS_CMD_ATTR_SERVICE */
static const struct nla_policy ip_vs_svc_policy[IPVS_SVC_ATTR_MAX + 1] = {
	[IPVS_SVC_ATTR_AF]		= { .type = NLA_U16 },
	[IPVS_SVC_ATTR_PROTOCOL]	= { .type = NLA_U16 },
	[IPVS_SVC_ATTR_ADDR]		= { .type = NLA_BINARY,
					    .len = sizeof(union nf_inet_addr) },
	[IPVS_SVC_ATTR_PORT]		= { .type = NLA_U16 },
	[IPVS_SVC_ATTR_FWMARK]		= { .type = NLA_U32 },
	[IPVS_SVC_ATTR_SCHED_NAME]	= { .type = NLA_NUL_STRING,
					    .len = IP_VS_SCHEDNAME_MAXLEN },
	[IPVS_SVC_ATTR_PE_NAME]		= { .type = NLA_NUL_STRING,
					    .len = IP_VS_PENAME_MAXLEN },
	[IPVS_SVC_ATTR_FLAGS]		= { .type = NLA_BINARY,
					    .len = sizeof(struct ip_vs_flags) },
	[IPVS_SVC_ATTR_TIMEOUT]		= { .type = NLA_U32 },
	[IPVS_SVC_ATTR_NETMASK]		= { .type = NLA_U32 },
	[IPVS_SVC_ATTR_STATS]		= { .type = NLA_NESTED },
};

/* Policy used for attributes in nested attribute IPVS_CMD_ATTR_DEST */
static const struct nla_policy ip_vs_dest_policy[IPVS_DEST_ATTR_MAX + 1] = {
	[IPVS_DEST_ATTR_ADDR]		= { .type = NLA_BINARY,
					    .len = sizeof(union nf_inet_addr) },
	[IPVS_DEST_ATTR_PORT]		= { .type = NLA_U16 },
	[IPVS_DEST_ATTR_FWD_METHOD]	= { .type = NLA_U32 },
	[IPVS_DEST_ATTR_WEIGHT]		= { .type = NLA_U32 },
	[IPVS_DEST_ATTR_U_THRESH]	= { .type = NLA_U32 },
	[IPVS_DEST_ATTR_L_THRESH]	= { .type = NLA_U32 },
	[IPVS_DEST_ATTR_ACTIVE_CONNS]	= { .type = NLA_U32 },
	[IPVS_DEST_ATTR_INACT_CONNS]	= { .type = NLA_U32 },
	[IPVS_DEST_ATTR_PERSIST_CONNS]	= { .type = NLA_U32 },
	[IPVS_DEST_ATTR_STATS]		= { .type = NLA_NESTED },
	[IPVS_DEST_ATTR_ADDR_FAMILY]	= { .type = NLA_U16 },
};

static int ip_vs_genl_fill_stats(struct sk_buff *skb, int container_type,
				 struct ip_vs_kstats *kstats)
{
	struct nlattr *nl_stats = nla_nest_start(skb, container_type);

	if (!nl_stats)
		return -EMSGSIZE;

	if (nla_put_u32(skb, IPVS_STATS_ATTR_CONNS, (u32)kstats->conns) ||
	    nla_put_u32(skb, IPVS_STATS_ATTR_INPKTS, (u32)kstats->inpkts) ||
	    nla_put_u32(skb, IPVS_STATS_ATTR_OUTPKTS, (u32)kstats->outpkts) ||
	    nla_put_u64(skb, IPVS_STATS_ATTR_INBYTES, kstats->inbytes) ||
	    nla_put_u64(skb, IPVS_STATS_ATTR_OUTBYTES, kstats->outbytes) ||
	    nla_put_u32(skb, IPVS_STATS_ATTR_CPS, (u32)kstats->cps) ||
	    nla_put_u32(skb, IPVS_STATS_ATTR_INPPS, (u32)kstats->inpps) ||
	    nla_put_u32(skb, IPVS_STATS_ATTR_OUTPPS, (u32)kstats->outpps) ||
	    nla_put_u32(skb, IPVS_STATS_ATTR_INBPS, (u32)kstats->inbps) ||
	    nla_put_u32(skb, IPVS_STATS_ATTR_OUTBPS, (u32)kstats->outbps))
		goto nla_put_failure;
	nla_nest_end(skb, nl_stats);

	return 0;

nla_put_failure:
	nla_nest_cancel(skb, nl_stats);
	return -EMSGSIZE;
}

static int ip_vs_genl_fill_stats64(struct sk_buff *skb, int container_type,
				   struct ip_vs_kstats *kstats)
{
	struct nlattr *nl_stats = nla_nest_start(skb, container_type);

	if (!nl_stats)
		return -EMSGSIZE;

	if (nla_put_u64(skb, IPVS_STATS_ATTR_CONNS, kstats->conns) ||
	    nla_put_u64(skb, IPVS_STATS_ATTR_INPKTS, kstats->inpkts) ||
	    nla_put_u64(skb, IPVS_STATS_ATTR_OUTPKTS, kstats->outpkts) ||
	    nla_put_u64(skb, IPVS_STATS_ATTR_INBYTES, kstats->inbytes) ||
	    nla_put_u64(skb, IPVS_STATS_ATTR_OUTBYTES, kstats->outbytes) ||
	    nla_put_u64(skb, IPVS_STATS_ATTR_CPS, kstats->cps) ||
	    nla_put_u64(skb, IPVS_STATS_ATTR_INPPS, kstats->inpps) ||
	    nla_put_u64(skb, IPVS_STATS_ATTR_OUTPPS, kstats->outpps) ||
	    nla_put_u64(skb, IPVS_STATS_ATTR_INBPS, kstats->inbps) ||
	    nla_put_u64(skb, IPVS_STATS_ATTR_OUTBPS, kstats->outbps))
		goto nla_put_failure;
	nla_nest_end(skb, nl_stats);

	return 0;

nla_put_failure:
	nla_nest_cancel(skb, nl_stats);
	return -EMSGSIZE;
}

static int ip_vs_genl_fill_service(struct sk_buff *skb,
				   struct ip_vs_service *svc)
{
	struct ip_vs_scheduler *sched;
	struct ip_vs_pe *pe;
	struct nlattr *nl_service;
	struct ip_vs_flags flags = { .flags = svc->flags,
				     .mask = ~0 };
	struct ip_vs_kstats kstats;

	nl_service = nla_nest_start(skb, IPVS_CMD_ATTR_SERVICE);
	if (!nl_service)
		return -EMSGSIZE;

	if (nla_put_u16(skb, IPVS_SVC_ATTR_AF, svc->af))
		goto nla_put_failure;
	if (svc->fwmark) {
		if (nla_put_u32(skb, IPVS_SVC_ATTR_FWMARK, svc->fwmark))
			goto nla_put_failure;
	} else {
		if (nla_put_u16(skb, IPVS_SVC_ATTR_PROTOCOL, svc->protocol) ||
		    nla_put(skb, IPVS_SVC_ATTR_ADDR, sizeof(svc->addr), &svc->addr) ||
		    nla_put_be16(skb, IPVS_SVC_ATTR_PORT, svc->port))
			goto nla_put_failure;
	}

	sched = rcu_dereference_protected(svc->scheduler, 1);
	pe = rcu_dereference_protected(svc->pe, 1);
	if (nla_put_string(skb, IPVS_SVC_ATTR_SCHED_NAME, sched->name) ||
	    (pe && nla_put_string(skb, IPVS_SVC_ATTR_PE_NAME, pe->name)) ||
	    nla_put(skb, IPVS_SVC_ATTR_FLAGS, sizeof(flags), &flags) ||
	    nla_put_u32(skb, IPVS_SVC_ATTR_TIMEOUT, svc->timeout / HZ) ||
	    nla_put_be32(skb, IPVS_SVC_ATTR_NETMASK, svc->netmask))
		goto nla_put_failure;
	ip_vs_copy_stats(&kstats, &svc->stats);
	if (ip_vs_genl_fill_stats(skb, IPVS_SVC_ATTR_STATS, &kstats))
		goto nla_put_failure;
	if (ip_vs_genl_fill_stats64(skb, IPVS_SVC_ATTR_STATS64, &kstats))
		goto nla_put_failure;

	nla_nest_end(skb, nl_service);

	return 0;

nla_put_failure:
	nla_nest_cancel(skb, nl_service);
	return -EMSGSIZE;
}

static int ip_vs_genl_dump_service(struct sk_buff *skb,
				   struct ip_vs_service *svc,
				   struct netlink_callback *cb)
{
	void *hdr;

	hdr = genlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			  &ip_vs_genl_family, NLM_F_MULTI,
			  IPVS_CMD_NEW_SERVICE);
	if (!hdr)
		return -EMSGSIZE;

	if (ip_vs_genl_fill_service(skb, svc) < 0)
		goto nla_put_failure;

	genlmsg_end(skb, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(skb, hdr);
	return -EMSGSIZE;
}

static int ip_vs_genl_dump_services(struct sk_buff *skb,
				    struct netlink_callback *cb)
{
	int idx = 0, i;
	int start = cb->args[0];
	struct ip_vs_service *svc;
	struct net *net = skb_sknet(skb);

	mutex_lock(&__ip_vs_mutex);
	for (i = 0; i < IP_VS_SVC_TAB_SIZE; i++) {
		hlist_for_each_entry(svc, &ip_vs_svc_table[i], s_list) {
			if (++idx <= start || !net_eq(svc->net, net))
				continue;
			if (ip_vs_genl_dump_service(skb, svc, cb) < 0) {
				idx--;
				goto nla_put_failure;
			}
		}
	}

	for (i = 0; i < IP_VS_SVC_TAB_SIZE; i++) {
		hlist_for_each_entry(svc, &ip_vs_svc_fwm_table[i], f_list) {
			if (++idx <= start || !net_eq(svc->net, net))
				continue;
			if (ip_vs_genl_dump_service(skb, svc, cb) < 0) {
				idx--;
				goto nla_put_failure;
			}
		}
	}

nla_put_failure:
	mutex_unlock(&__ip_vs_mutex);
	cb->args[0] = idx;

	return skb->len;
}

static int ip_vs_genl_parse_service(struct net *net,
				    struct ip_vs_service_user_kern *usvc,
				    struct nlattr *nla, int full_entry,
				    struct ip_vs_service **ret_svc)
{
	struct nlattr *attrs[IPVS_SVC_ATTR_MAX + 1];
	struct nlattr *nla_af, *nla_port, *nla_fwmark, *nla_protocol, *nla_addr;
	struct ip_vs_service *svc;

	/* Parse mandatory identifying service fields first */
	if (nla == NULL ||
	    nla_parse_nested(attrs, IPVS_SVC_ATTR_MAX, nla, ip_vs_svc_policy))
		return -EINVAL;

	nla_af		= attrs[IPVS_SVC_ATTR_AF];
	nla_protocol	= attrs[IPVS_SVC_ATTR_PROTOCOL];
	nla_addr	= attrs[IPVS_SVC_ATTR_ADDR];
	nla_port	= attrs[IPVS_SVC_ATTR_PORT];
	nla_fwmark	= attrs[IPVS_SVC_ATTR_FWMARK];

	if (!(nla_af && (nla_fwmark || (nla_port && nla_protocol && nla_addr))))
		return -EINVAL;

	memset(usvc, 0, sizeof(*usvc));

	usvc->af = nla_get_u16(nla_af);
#ifdef CONFIG_IP_VS_IPV6
	if (usvc->af != AF_INET && usvc->af != AF_INET6)
#else
	if (usvc->af != AF_INET)
#endif
		return -EAFNOSUPPORT;

	if (nla_fwmark) {
		usvc->protocol = IPPROTO_TCP;
		usvc->fwmark = nla_get_u32(nla_fwmark);
	} else {
		usvc->protocol = nla_get_u16(nla_protocol);
		nla_memcpy(&usvc->addr, nla_addr, sizeof(usvc->addr));
		usvc->port = nla_get_be16(nla_port);
		usvc->fwmark = 0;
	}

	rcu_read_lock();
	if (usvc->fwmark)
		svc = __ip_vs_svc_fwm_find(net, usvc->af, usvc->fwmark);
	else
		svc = __ip_vs_service_find(net, usvc->af, usvc->protocol,
					   &usvc->addr, usvc->port);
	rcu_read_unlock();
	*ret_svc = svc;

	/* If a full entry was requested, check for the additional fields */
	if (full_entry) {
		struct nlattr *nla_sched, *nla_flags, *nla_pe, *nla_timeout,
			      *nla_netmask;
		struct ip_vs_flags flags;

		nla_sched = attrs[IPVS_SVC_ATTR_SCHED_NAME];
		nla_pe = attrs[IPVS_SVC_ATTR_PE_NAME];
		nla_flags = attrs[IPVS_SVC_ATTR_FLAGS];
		nla_timeout = attrs[IPVS_SVC_ATTR_TIMEOUT];
		nla_netmask = attrs[IPVS_SVC_ATTR_NETMASK];

		if (!(nla_sched && nla_flags && nla_timeout && nla_netmask))
			return -EINVAL;

		nla_memcpy(&flags, nla_flags, sizeof(flags));

		/* prefill flags from service if it already exists */
		if (svc)
			usvc->flags = svc->flags;

		/* set new flags from userland */
		usvc->flags = (usvc->flags & ~flags.mask) |
			      (flags.flags & flags.mask);
		usvc->sched_name = nla_data(nla_sched);
		usvc->pe_name = nla_pe ? nla_data(nla_pe) : NULL;
		usvc->timeout = nla_get_u32(nla_timeout);
		usvc->netmask = nla_get_be32(nla_netmask);
	}

	return 0;
}

static struct ip_vs_service *ip_vs_genl_find_service(struct net *net,
						     struct nlattr *nla)
{
	struct ip_vs_service_user_kern usvc;
	struct ip_vs_service *svc;
	int ret;

	ret = ip_vs_genl_parse_service(net, &usvc, nla, 0, &svc);
	return ret ? ERR_PTR(ret) : svc;
}

static int ip_vs_genl_fill_dest(struct sk_buff *skb, struct ip_vs_dest *dest)
{
	struct nlattr *nl_dest;
	struct ip_vs_kstats kstats;

	nl_dest = nla_nest_start(skb, IPVS_CMD_ATTR_DEST);
	if (!nl_dest)
		return -EMSGSIZE;

	if (nla_put(skb, IPVS_DEST_ATTR_ADDR, sizeof(dest->addr), &dest->addr) ||
	    nla_put_be16(skb, IPVS_DEST_ATTR_PORT, dest->port) ||
	    nla_put_u32(skb, IPVS_DEST_ATTR_FWD_METHOD,
			(atomic_read(&dest->conn_flags) &
			 IP_VS_CONN_F_FWD_MASK)) ||
	    nla_put_u32(skb, IPVS_DEST_ATTR_WEIGHT,
			atomic_read(&dest->weight)) ||
	    nla_put_u32(skb, IPVS_DEST_ATTR_U_THRESH, dest->u_threshold) ||
	    nla_put_u32(skb, IPVS_DEST_ATTR_L_THRESH, dest->l_threshold) ||
	    nla_put_u32(skb, IPVS_DEST_ATTR_ACTIVE_CONNS,
			atomic_read(&dest->activeconns)) ||
	    nla_put_u32(skb, IPVS_DEST_ATTR_INACT_CONNS,
			atomic_read(&dest->inactconns)) ||
	    nla_put_u32(skb, IPVS_DEST_ATTR_PERSIST_CONNS,
			atomic_read(&dest->persistconns)) ||
	    nla_put_u16(skb, IPVS_DEST_ATTR_ADDR_FAMILY, dest->af))
		goto nla_put_failure;
	ip_vs_copy_stats(&kstats, &dest->stats);
	if (ip_vs_genl_fill_stats(skb, IPVS_DEST_ATTR_STATS, &kstats))
		goto nla_put_failure;
	if (ip_vs_genl_fill_stats64(skb, IPVS_DEST_ATTR_STATS64, &kstats))
		goto nla_put_failure;

	nla_nest_end(skb, nl_dest);

	return 0;

nla_put_failure:
	nla_nest_cancel(skb, nl_dest);
	return -EMSGSIZE;
}

static int ip_vs_genl_dump_dest(struct sk_buff *skb, struct ip_vs_dest *dest,
				struct netlink_callback *cb)
{
	void *hdr;

	hdr = genlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			  &ip_vs_genl_family, NLM_F_MULTI,
			  IPVS_CMD_NEW_DEST);
	if (!hdr)
		return -EMSGSIZE;

	if (ip_vs_genl_fill_dest(skb, dest) < 0)
		goto nla_put_failure;

	genlmsg_end(skb, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(skb, hdr);
	return -EMSGSIZE;
}

static int ip_vs_genl_dump_dests(struct sk_buff *skb,
				 struct netlink_callback *cb)
{
	int idx = 0;
	int start = cb->args[0];
	struct ip_vs_service *svc;
	struct ip_vs_dest *dest;
	struct nlattr *attrs[IPVS_CMD_ATTR_MAX + 1];
	struct net *net = skb_sknet(skb);

	mutex_lock(&__ip_vs_mutex);

	/* Try to find the service for which to dump destinations */
	if (nlmsg_parse(cb->nlh, GENL_HDRLEN, attrs,
			IPVS_CMD_ATTR_MAX, ip_vs_cmd_policy))
		goto out_err;


	svc = ip_vs_genl_find_service(net, attrs[IPVS_CMD_ATTR_SERVICE]);
	if (IS_ERR(svc) || svc == NULL)
		goto out_err;

	/* Dump the destinations */
	list_for_each_entry(dest, &svc->destinations, n_list) {
		if (++idx <= start)
			continue;
		if (ip_vs_genl_dump_dest(skb, dest, cb) < 0) {
			idx--;
			goto nla_put_failure;
		}
	}

nla_put_failure:
	cb->args[0] = idx;

out_err:
	mutex_unlock(&__ip_vs_mutex);

	return skb->len;
}

static int ip_vs_genl_parse_dest(struct ip_vs_dest_user_kern *udest,
				 struct nlattr *nla, int full_entry)
{
	struct nlattr *attrs[IPVS_DEST_ATTR_MAX + 1];
	struct nlattr *nla_addr, *nla_port;
	struct nlattr *nla_addr_family;

	/* Parse mandatory identifying destination fields first */
	if (nla == NULL ||
	    nla_parse_nested(attrs, IPVS_DEST_ATTR_MAX, nla, ip_vs_dest_policy))
		return -EINVAL;

	nla_addr	= attrs[IPVS_DEST_ATTR_ADDR];
	nla_port	= attrs[IPVS_DEST_ATTR_PORT];
	nla_addr_family	= attrs[IPVS_DEST_ATTR_ADDR_FAMILY];

	if (!(nla_addr && nla_port))
		return -EINVAL;

	memset(udest, 0, sizeof(*udest));

	nla_memcpy(&udest->addr, nla_addr, sizeof(udest->addr));
	udest->port = nla_get_be16(nla_port);

	if (nla_addr_family)
		udest->af = nla_get_u16(nla_addr_family);
	else
		udest->af = 0;

	/* If a full entry was requested, check for the additional fields */
	if (full_entry) {
		struct nlattr *nla_fwd, *nla_weight, *nla_u_thresh,
			      *nla_l_thresh;

		nla_fwd		= attrs[IPVS_DEST_ATTR_FWD_METHOD];
		nla_weight	= attrs[IPVS_DEST_ATTR_WEIGHT];
		nla_u_thresh	= attrs[IPVS_DEST_ATTR_U_THRESH];
		nla_l_thresh	= attrs[IPVS_DEST_ATTR_L_THRESH];

		if (!(nla_fwd && nla_weight && nla_u_thresh && nla_l_thresh))
			return -EINVAL;

		udest->conn_flags = nla_get_u32(nla_fwd)
				    & IP_VS_CONN_F_FWD_MASK;
		udest->weight = nla_get_u32(nla_weight);
		udest->u_threshold = nla_get_u32(nla_u_thresh);
		udest->l_threshold = nla_get_u32(nla_l_thresh);
	}

	return 0;
}

static int ip_vs_genl_fill_daemon(struct sk_buff *skb, __u32 state,
				  const char *mcast_ifn, __u32 syncid)
{
	struct nlattr *nl_daemon;

	nl_daemon = nla_nest_start(skb, IPVS_CMD_ATTR_DAEMON);
	if (!nl_daemon)
		return -EMSGSIZE;

	if (nla_put_u32(skb, IPVS_DAEMON_ATTR_STATE, state) ||
	    nla_put_string(skb, IPVS_DAEMON_ATTR_MCAST_IFN, mcast_ifn) ||
	    nla_put_u32(skb, IPVS_DAEMON_ATTR_SYNC_ID, syncid))
		goto nla_put_failure;
	nla_nest_end(skb, nl_daemon);

	return 0;

nla_put_failure:
	nla_nest_cancel(skb, nl_daemon);
	return -EMSGSIZE;
}

static int ip_vs_genl_dump_daemon(struct sk_buff *skb, __u32 state,
				  const char *mcast_ifn, __u32 syncid,
				  struct netlink_callback *cb)
{
	void *hdr;
	hdr = genlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			  &ip_vs_genl_family, NLM_F_MULTI,
			  IPVS_CMD_NEW_DAEMON);
	if (!hdr)
		return -EMSGSIZE;

	if (ip_vs_genl_fill_daemon(skb, state, mcast_ifn, syncid))
		goto nla_put_failure;

	genlmsg_end(skb, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(skb, hdr);
	return -EMSGSIZE;
}

static int ip_vs_genl_dump_daemons(struct sk_buff *skb,
				   struct netlink_callback *cb)
{
	struct net *net = skb_sknet(skb);
	struct netns_ipvs *ipvs = net_ipvs(net);

	mutex_lock(&ipvs->sync_mutex);
	if ((ipvs->sync_state & IP_VS_STATE_MASTER) && !cb->args[0]) {
		if (ip_vs_genl_dump_daemon(skb, IP_VS_STATE_MASTER,
					   ipvs->master_mcast_ifn,
					   ipvs->master_syncid, cb) < 0)
			goto nla_put_failure;

		cb->args[0] = 1;
	}

	if ((ipvs->sync_state & IP_VS_STATE_BACKUP) && !cb->args[1]) {
		if (ip_vs_genl_dump_daemon(skb, IP_VS_STATE_BACKUP,
					   ipvs->backup_mcast_ifn,
					   ipvs->backup_syncid, cb) < 0)
			goto nla_put_failure;

		cb->args[1] = 1;
	}

nla_put_failure:
	mutex_unlock(&ipvs->sync_mutex);

	return skb->len;
}

static int ip_vs_genl_new_daemon(struct net *net, struct nlattr **attrs)
{
	if (!(attrs[IPVS_DAEMON_ATTR_STATE] &&
	      attrs[IPVS_DAEMON_ATTR_MCAST_IFN] &&
	      attrs[IPVS_DAEMON_ATTR_SYNC_ID]))
		return -EINVAL;

	/* The synchronization protocol is incompatible with mixed family
	 * services
	 */
	if (net_ipvs(net)->mixed_address_family_dests > 0)
		return -EINVAL;

	return start_sync_thread(net,
				 nla_get_u32(attrs[IPVS_DAEMON_ATTR_STATE]),
				 nla_data(attrs[IPVS_DAEMON_ATTR_MCAST_IFN]),
				 nla_get_u32(attrs[IPVS_DAEMON_ATTR_SYNC_ID]));
}

static int ip_vs_genl_del_daemon(struct net *net, struct nlattr **attrs)
{
	if (!attrs[IPVS_DAEMON_ATTR_STATE])
		return -EINVAL;

	return stop_sync_thread(net,
				nla_get_u32(attrs[IPVS_DAEMON_ATTR_STATE]));
}

static int ip_vs_genl_set_config(struct net *net, struct nlattr **attrs)
{
	struct ip_vs_timeout_user t;

	__ip_vs_get_timeouts(net, &t);

	if (attrs[IPVS_CMD_ATTR_TIMEOUT_TCP])
		t.tcp_timeout = nla_get_u32(attrs[IPVS_CMD_ATTR_TIMEOUT_TCP]);

	if (attrs[IPVS_CMD_ATTR_TIMEOUT_TCP_FIN])
		t.tcp_fin_timeout =
			nla_get_u32(attrs[IPVS_CMD_ATTR_TIMEOUT_TCP_FIN]);

	if (attrs[IPVS_CMD_ATTR_TIMEOUT_UDP])
		t.udp_timeout = nla_get_u32(attrs[IPVS_CMD_ATTR_TIMEOUT_UDP]);

	return ip_vs_set_timeout(net, &t);
}

static int ip_vs_genl_set_daemon(struct sk_buff *skb, struct genl_info *info)
{
	int ret = 0, cmd;
	struct net *net;
	struct netns_ipvs *ipvs;

	net = skb_sknet(skb);
	ipvs = net_ipvs(net);
	cmd = info->genlhdr->cmd;

	if (cmd == IPVS_CMD_NEW_DAEMON || cmd == IPVS_CMD_DEL_DAEMON) {
		struct nlattr *daemon_attrs[IPVS_DAEMON_ATTR_MAX + 1];

		mutex_lock(&ipvs->sync_mutex);
		if (!info->attrs[IPVS_CMD_ATTR_DAEMON] ||
		    nla_parse_nested(daemon_attrs, IPVS_DAEMON_ATTR_MAX,
				     info->attrs[IPVS_CMD_ATTR_DAEMON],
				     ip_vs_daemon_policy)) {
			ret = -EINVAL;
			goto out;
		}

		if (cmd == IPVS_CMD_NEW_DAEMON)
			ret = ip_vs_genl_new_daemon(net, daemon_attrs);
		else
			ret = ip_vs_genl_del_daemon(net, daemon_attrs);
out:
		mutex_unlock(&ipvs->sync_mutex);
	}
	return ret;
}

static int ip_vs_genl_set_cmd(struct sk_buff *skb, struct genl_info *info)
{
	struct ip_vs_service *svc = NULL;
	struct ip_vs_service_user_kern usvc;
	struct ip_vs_dest_user_kern udest;
	int ret = 0, cmd;
	int need_full_svc = 0, need_full_dest = 0;
	struct net *net;

	net = skb_sknet(skb);
	cmd = info->genlhdr->cmd;

	mutex_lock(&__ip_vs_mutex);

	if (cmd == IPVS_CMD_FLUSH) {
		ret = ip_vs_flush(net, false);
		goto out;
	} else if (cmd == IPVS_CMD_SET_CONFIG) {
		ret = ip_vs_genl_set_config(net, info->attrs);
		goto out;
	} else if (cmd == IPVS_CMD_ZERO &&
		   !info->attrs[IPVS_CMD_ATTR_SERVICE]) {
		ret = ip_vs_zero_all(net);
		goto out;
	}

	/* All following commands require a service argument, so check if we
	 * received a valid one. We need a full service specification when
	 * adding / editing a service. Only identifying members otherwise. */
	if (cmd == IPVS_CMD_NEW_SERVICE || cmd == IPVS_CMD_SET_SERVICE)
		need_full_svc = 1;

	ret = ip_vs_genl_parse_service(net, &usvc,
				       info->attrs[IPVS_CMD_ATTR_SERVICE],
				       need_full_svc, &svc);
	if (ret)
		goto out;

	/* Unless we're adding a new service, the service must already exist */
	if ((cmd != IPVS_CMD_NEW_SERVICE) && (svc == NULL)) {
		ret = -ESRCH;
		goto out;
	}

	/* Destination commands require a valid destination argument. For
	 * adding / editing a destination, we need a full destination
	 * specification. */
	if (cmd == IPVS_CMD_NEW_DEST || cmd == IPVS_CMD_SET_DEST ||
	    cmd == IPVS_CMD_DEL_DEST) {
		if (cmd != IPVS_CMD_DEL_DEST)
			need_full_dest = 1;

		ret = ip_vs_genl_parse_dest(&udest,
					    info->attrs[IPVS_CMD_ATTR_DEST],
					    need_full_dest);
		if (ret)
			goto out;

		/* Old protocols did not allow the user to specify address
		 * family, so we set it to zero instead.  We also didn't
		 * allow heterogeneous pools in the old code, so it's safe
		 * to assume that this will have the same address family as
		 * the service.
		 */
		if (udest.af == 0)
			udest.af = svc->af;

		if (udest.af != svc->af && cmd != IPVS_CMD_DEL_DEST) {
			/* The synchronization protocol is incompatible
			 * with mixed family services
			 */
			if (net_ipvs(net)->sync_state) {
				ret = -EINVAL;
				goto out;
			}

			/* Which connection types do we support? */
			switch (udest.conn_flags) {
			case IP_VS_CONN_F_TUNNEL:
				/* We are able to forward this */
				break;
			default:
				ret = -EINVAL;
				goto out;
			}
		}
	}

	switch (cmd) {
	case IPVS_CMD_NEW_SERVICE:
		if (svc == NULL)
			ret = ip_vs_add_service(net, &usvc, &svc);
		else
			ret = -EEXIST;
		break;
	case IPVS_CMD_SET_SERVICE:
		ret = ip_vs_edit_service(svc, &usvc);
		break;
	case IPVS_CMD_DEL_SERVICE:
		ret = ip_vs_del_service(svc);
		/* do not use svc, it can be freed */
		break;
	case IPVS_CMD_NEW_DEST:
		ret = ip_vs_add_dest(svc, &udest);
		break;
	case IPVS_CMD_SET_DEST:
		ret = ip_vs_edit_dest(svc, &udest);
		break;
	case IPVS_CMD_DEL_DEST:
		ret = ip_vs_del_dest(svc, &udest);
		break;
	case IPVS_CMD_ZERO:
		ret = ip_vs_zero_service(svc);
		break;
	default:
		ret = -EINVAL;
	}

out:
	mutex_unlock(&__ip_vs_mutex);

	return ret;
}

static int ip_vs_genl_get_cmd(struct sk_buff *skb, struct genl_info *info)
{
	struct sk_buff *msg;
	void *reply;
	int ret, cmd, reply_cmd;
	struct net *net;

	net = skb_sknet(skb);
	cmd = info->genlhdr->cmd;

	if (cmd == IPVS_CMD_GET_SERVICE)
		reply_cmd = IPVS_CMD_NEW_SERVICE;
	else if (cmd == IPVS_CMD_GET_INFO)
		reply_cmd = IPVS_CMD_SET_INFO;
	else if (cmd == IPVS_CMD_GET_CONFIG)
		reply_cmd = IPVS_CMD_SET_CONFIG;
	else {
		pr_err("unknown Generic Netlink command\n");
		return -EINVAL;
	}

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	mutex_lock(&__ip_vs_mutex);

	reply = genlmsg_put_reply(msg, info, &ip_vs_genl_family, 0, reply_cmd);
	if (reply == NULL)
		goto nla_put_failure;

	switch (cmd) {
	case IPVS_CMD_GET_SERVICE:
	{
		struct ip_vs_service *svc;

		svc = ip_vs_genl_find_service(net,
					      info->attrs[IPVS_CMD_ATTR_SERVICE]);
		if (IS_ERR(svc)) {
			ret = PTR_ERR(svc);
			goto out_err;
		} else if (svc) {
			ret = ip_vs_genl_fill_service(msg, svc);
			if (ret)
				goto nla_put_failure;
		} else {
			ret = -ESRCH;
			goto out_err;
		}

		break;
	}

	case IPVS_CMD_GET_CONFIG:
	{
		struct ip_vs_timeout_user t;

		__ip_vs_get_timeouts(net, &t);
#ifdef CONFIG_IP_VS_PROTO_TCP
		if (nla_put_u32(msg, IPVS_CMD_ATTR_TIMEOUT_TCP,
				t.tcp_timeout) ||
		    nla_put_u32(msg, IPVS_CMD_ATTR_TIMEOUT_TCP_FIN,
				t.tcp_fin_timeout))
			goto nla_put_failure;
#endif
#ifdef CONFIG_IP_VS_PROTO_UDP
		if (nla_put_u32(msg, IPVS_CMD_ATTR_TIMEOUT_UDP, t.udp_timeout))
			goto nla_put_failure;
#endif

		break;
	}

	case IPVS_CMD_GET_INFO:
		if (nla_put_u32(msg, IPVS_INFO_ATTR_VERSION,
				IP_VS_VERSION_CODE) ||
		    nla_put_u32(msg, IPVS_INFO_ATTR_CONN_TAB_SIZE,
				ip_vs_conn_tab_size))
			goto nla_put_failure;
		break;
	}

	genlmsg_end(msg, reply);
	ret = genlmsg_reply(msg, info);
	goto out;

nla_put_failure:
	pr_err("not enough space in Netlink message\n");
	ret = -EMSGSIZE;

out_err:
	nlmsg_free(msg);
out:
	mutex_unlock(&__ip_vs_mutex);

	return ret;
}


static const struct genl_ops ip_vs_genl_ops[] = {
	{
		.cmd	= IPVS_CMD_NEW_SERVICE,
		.flags	= GENL_ADMIN_PERM,
		.policy	= ip_vs_cmd_policy,
		.doit	= ip_vs_genl_set_cmd,
	},
	{
		.cmd	= IPVS_CMD_SET_SERVICE,
		.flags	= GENL_ADMIN_PERM,
		.policy	= ip_vs_cmd_policy,
		.doit	= ip_vs_genl_set_cmd,
	},
	{
		.cmd	= IPVS_CMD_DEL_SERVICE,
		.flags	= GENL_ADMIN_PERM,
		.policy	= ip_vs_cmd_policy,
		.doit	= ip_vs_genl_set_cmd,
	},
	{
		.cmd	= IPVS_CMD_GET_SERVICE,
		.flags	= GENL_ADMIN_PERM,
		.doit	= ip_vs_genl_get_cmd,
		.dumpit	= ip_vs_genl_dump_services,
		.policy	= ip_vs_cmd_policy,
	},
	{
		.cmd	= IPVS_CMD_NEW_DEST,
		.flags	= GENL_ADMIN_PERM,
		.policy	= ip_vs_cmd_policy,
		.doit	= ip_vs_genl_set_cmd,
	},
	{
		.cmd	= IPVS_CMD_SET_DEST,
		.flags	= GENL_ADMIN_PERM,
		.policy	= ip_vs_cmd_policy,
		.doit	= ip_vs_genl_set_cmd,
	},
	{
		.cmd	= IPVS_CMD_DEL_DEST,
		.flags	= GENL_ADMIN_PERM,
		.policy	= ip_vs_cmd_policy,
		.doit	= ip_vs_genl_set_cmd,
	},
	{
		.cmd	= IPVS_CMD_GET_DEST,
		.flags	= GENL_ADMIN_PERM,
		.policy	= ip_vs_cmd_policy,
		.dumpit	= ip_vs_genl_dump_dests,
	},
	{
		.cmd	= IPVS_CMD_NEW_DAEMON,
		.flags	= GENL_ADMIN_PERM,
		.policy	= ip_vs_cmd_policy,
		.doit	= ip_vs_genl_set_daemon,
	},
	{
		.cmd	= IPVS_CMD_DEL_DAEMON,
		.flags	= GENL_ADMIN_PERM,
		.policy	= ip_vs_cmd_policy,
		.doit	= ip_vs_genl_set_daemon,
	},
	{
		.cmd	= IPVS_CMD_GET_DAEMON,
		.flags	= GENL_ADMIN_PERM,
		.dumpit	= ip_vs_genl_dump_daemons,
	},
	{
		.cmd	= IPVS_CMD_SET_CONFIG,
		.flags	= GENL_ADMIN_PERM,
		.policy	= ip_vs_cmd_policy,
		.doit	= ip_vs_genl_set_cmd,
	},
	{
		.cmd	= IPVS_CMD_GET_CONFIG,
		.flags	= GENL_ADMIN_PERM,
		.doit	= ip_vs_genl_get_cmd,
	},
	{
		.cmd	= IPVS_CMD_GET_INFO,
		.flags	= GENL_ADMIN_PERM,
		.doit	= ip_vs_genl_get_cmd,
	},
	{
		.cmd	= IPVS_CMD_ZERO,
		.flags	= GENL_ADMIN_PERM,
		.policy	= ip_vs_cmd_policy,
		.doit	= ip_vs_genl_set_cmd,
	},
	{
		.cmd	= IPVS_CMD_FLUSH,
		.flags	= GENL_ADMIN_PERM,
		.doit	= ip_vs_genl_set_cmd,
	},
};

static int __init ip_vs_genl_register(void)
{
	return genl_register_family_with_ops(&ip_vs_genl_family,
					     ip_vs_genl_ops);
}

static void ip_vs_genl_unregister(void)
{
	genl_unregister_family(&ip_vs_genl_family);
}

/* End of Generic Netlink interface definitions */

/*
 * per netns intit/exit func.
 */
#ifdef CONFIG_SYSCTL
static int __net_init ip_vs_control_net_init_sysctl(struct net *net)
{
	int idx;
	struct netns_ipvs *ipvs = net_ipvs(net);
	struct ctl_table *tbl;

	atomic_set(&ipvs->dropentry, 0);
	spin_lock_init(&ipvs->dropentry_lock);
	spin_lock_init(&ipvs->droppacket_lock);
	spin_lock_init(&ipvs->securetcp_lock);

	if (!net_eq(net, &init_net)) {
		tbl = kmemdup(vs_vars, sizeof(vs_vars), GFP_KERNEL);
		if (tbl == NULL)
			return -ENOMEM;

		/* Don't export sysctls to unprivileged users */
		if (net->user_ns != &init_user_ns)
			tbl[0].procname = NULL;
	} else
		tbl = vs_vars;
	/* Initialize sysctl defaults */
	idx = 0;
	ipvs->sysctl_amemthresh = 1024;
	tbl[idx++].data = &ipvs->sysctl_amemthresh;
	ipvs->sysctl_am_droprate = 10;
	tbl[idx++].data = &ipvs->sysctl_am_droprate;
	tbl[idx++].data = &ipvs->sysctl_drop_entry;
	tbl[idx++].data = &ipvs->sysctl_drop_packet;
#ifdef CONFIG_IP_VS_NFCT
	tbl[idx++].data = &ipvs->sysctl_conntrack;
#endif
	tbl[idx++].data = &ipvs->sysctl_secure_tcp;
	ipvs->sysctl_snat_reroute = 1;
	tbl[idx++].data = &ipvs->sysctl_snat_reroute;
	ipvs->sysctl_sync_ver = 1;
	tbl[idx++].data = &ipvs->sysctl_sync_ver;
	ipvs->sysctl_sync_ports = 1;
	tbl[idx++].data = &ipvs->sysctl_sync_ports;
	tbl[idx++].data = &ipvs->sysctl_sync_persist_mode;
	ipvs->sysctl_sync_qlen_max = nr_free_buffer_pages() / 32;
	tbl[idx++].data = &ipvs->sysctl_sync_qlen_max;
	ipvs->sysctl_sync_sock_size = 0;
	tbl[idx++].data = &ipvs->sysctl_sync_sock_size;
	tbl[idx++].data = &ipvs->sysctl_cache_bypass;
	tbl[idx++].data = &ipvs->sysctl_expire_nodest_conn;
	tbl[idx++].data = &ipvs->sysctl_sloppy_tcp;
	tbl[idx++].data = &ipvs->sysctl_sloppy_sctp;
	tbl[idx++].data = &ipvs->sysctl_expire_quiescent_template;
	ipvs->sysctl_sync_threshold[0] = DEFAULT_SYNC_THRESHOLD;
	ipvs->sysctl_sync_threshold[1] = DEFAULT_SYNC_PERIOD;
	tbl[idx].data = &ipvs->sysctl_sync_threshold;
	tbl[idx++].maxlen = sizeof(ipvs->sysctl_sync_threshold);
	ipvs->sysctl_sync_refresh_period = DEFAULT_SYNC_REFRESH_PERIOD;
	tbl[idx++].data = &ipvs->sysctl_sync_refresh_period;
	ipvs->sysctl_sync_retries = clamp_t(int, DEFAULT_SYNC_RETRIES, 0, 3);
	tbl[idx++].data = &ipvs->sysctl_sync_retries;
	tbl[idx++].data = &ipvs->sysctl_nat_icmp_send;
	ipvs->sysctl_pmtu_disc = 1;
	tbl[idx++].data = &ipvs->sysctl_pmtu_disc;
	tbl[idx++].data = &ipvs->sysctl_backup_only;
	ipvs->sysctl_conn_reuse_mode = 1;
	tbl[idx++].data = &ipvs->sysctl_conn_reuse_mode;


	ipvs->sysctl_hdr = register_net_sysctl(net, "net/ipv4/vs", tbl);
	if (ipvs->sysctl_hdr == NULL) {
		if (!net_eq(net, &init_net))
			kfree(tbl);
		return -ENOMEM;
	}
	ip_vs_start_estimator(net, &ipvs->tot_stats);
	ipvs->sysctl_tbl = tbl;
	/* Schedule defense work */
	INIT_DELAYED_WORK(&ipvs->defense_work, defense_work_handler);
	schedule_delayed_work(&ipvs->defense_work, DEFENSE_TIMER_PERIOD);

	return 0;
}

static void __net_exit ip_vs_control_net_cleanup_sysctl(struct net *net)
{
	struct netns_ipvs *ipvs = net_ipvs(net);

	cancel_delayed_work_sync(&ipvs->defense_work);
	cancel_work_sync(&ipvs->defense_work.work);
	unregister_net_sysctl_table(ipvs->sysctl_hdr);
	ip_vs_stop_estimator(net, &ipvs->tot_stats);
}

#else

static int __net_init ip_vs_control_net_init_sysctl(struct net *net) { return 0; }
static void __net_exit ip_vs_control_net_cleanup_sysctl(struct net *net) { }

#endif

static struct notifier_block ip_vs_dst_notifier = {
	.notifier_call = ip_vs_dst_event,
};

int __net_init ip_vs_control_net_init(struct net *net)
{
	int i, idx;
	struct netns_ipvs *ipvs = net_ipvs(net);

	/* Initialize rs_table */
	for (idx = 0; idx < IP_VS_RTAB_SIZE; idx++)
		INIT_HLIST_HEAD(&ipvs->rs_table[idx]);

	INIT_LIST_HEAD(&ipvs->dest_trash);
	spin_lock_init(&ipvs->dest_trash_lock);
	setup_timer(&ipvs->dest_trash_timer, ip_vs_dest_trash_expire,
		    (unsigned long) net);
	atomic_set(&ipvs->ftpsvc_counter, 0);
	atomic_set(&ipvs->nullsvc_counter, 0);

	/* procfs stats */
	ipvs->tot_stats.cpustats = alloc_percpu(struct ip_vs_cpu_stats);
	if (!ipvs->tot_stats.cpustats)
		return -ENOMEM;

	for_each_possible_cpu(i) {
		struct ip_vs_cpu_stats *ipvs_tot_stats;
		ipvs_tot_stats = per_cpu_ptr(ipvs->tot_stats.cpustats, i);
		u64_stats_init(&ipvs_tot_stats->syncp);
	}

	spin_lock_init(&ipvs->tot_stats.lock);

	proc_create("ip_vs", 0, net->proc_net, &ip_vs_info_fops);
	proc_create("ip_vs_stats", 0, net->proc_net, &ip_vs_stats_fops);
	proc_create("ip_vs_stats_percpu", 0, net->proc_net,
		    &ip_vs_stats_percpu_fops);

	if (ip_vs_control_net_init_sysctl(net))
		goto err;

	return 0;

err:
	free_percpu(ipvs->tot_stats.cpustats);
	return -ENOMEM;
}

void __net_exit ip_vs_control_net_cleanup(struct net *net)
{
	struct netns_ipvs *ipvs = net_ipvs(net);

	ip_vs_trash_cleanup(net);
	ip_vs_control_net_cleanup_sysctl(net);
	remove_proc_entry("ip_vs_stats_percpu", net->proc_net);
	remove_proc_entry("ip_vs_stats", net->proc_net);
	remove_proc_entry("ip_vs", net->proc_net);
	free_percpu(ipvs->tot_stats.cpustats);
}

int __init ip_vs_register_nl_ioctl(void)
{
	int ret;

	ret = nf_register_sockopt(&ip_vs_sockopts);
	if (ret) {
		pr_err("cannot register sockopt.\n");
		goto err_sock;
	}

	ret = ip_vs_genl_register();
	if (ret) {
		pr_err("cannot register Generic Netlink interface.\n");
		goto err_genl;
	}
	return 0;

err_genl:
	nf_unregister_sockopt(&ip_vs_sockopts);
err_sock:
	return ret;
}

void ip_vs_unregister_nl_ioctl(void)
{
	ip_vs_genl_unregister();
	nf_unregister_sockopt(&ip_vs_sockopts);
}

int __init ip_vs_control_init(void)
{
	int idx;
	int ret;

	EnterFunction(2);

	/* Initialize svc_table, ip_vs_svc_fwm_table */
	for (idx = 0; idx < IP_VS_SVC_TAB_SIZE; idx++) {
		INIT_HLIST_HEAD(&ip_vs_svc_table[idx]);
		INIT_HLIST_HEAD(&ip_vs_svc_fwm_table[idx]);
	}

	smp_wmb();	/* Do we really need it now ? */

	ret = register_netdevice_notifier(&ip_vs_dst_notifier);
	if (ret < 0)
		return ret;

	LeaveFunction(2);
	return 0;
}


void ip_vs_control_cleanup(void)
{
	EnterFunction(2);
	unregister_netdevice_notifier(&ip_vs_dst_notifier);
	LeaveFunction(2);
}
