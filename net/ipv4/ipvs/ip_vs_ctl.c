/*
 * IPVS         An implementation of the IP virtual server support for the
 *              LINUX operating system.  IPVS is now implemented as a module
 *              over the NetFilter framework. IPVS can be used to build a
 *              high-performance and highly available server based on a
 *              cluster of servers.
 *
 * Version:     $Id: ip_vs_ctl.c,v 1.36 2003/06/08 09:31:19 wensong Exp $
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

#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/mutex.h>

#include <net/net_namespace.h>
#include <net/ip.h>
#include <net/route.h>
#include <net/sock.h>

#include <asm/uaccess.h>

#include <net/ip_vs.h>

/* semaphore for IPVS sockopts. And, [gs]etsockopt may sleep. */
static DEFINE_MUTEX(__ip_vs_mutex);

/* lock for service table */
static DEFINE_RWLOCK(__ip_vs_svc_lock);

/* lock for table with the real services */
static DEFINE_RWLOCK(__ip_vs_rs_lock);

/* lock for state and timeout tables */
static DEFINE_RWLOCK(__ip_vs_securetcp_lock);

/* lock for drop entry handling */
static DEFINE_SPINLOCK(__ip_vs_dropentry_lock);

/* lock for drop packet handling */
static DEFINE_SPINLOCK(__ip_vs_droppacket_lock);

/* 1/rate drop and drop-entry variables */
int ip_vs_drop_rate = 0;
int ip_vs_drop_counter = 0;
static atomic_t ip_vs_dropentry = ATOMIC_INIT(0);

/* number of virtual services */
static int ip_vs_num_services = 0;

/* sysctl variables */
static int sysctl_ip_vs_drop_entry = 0;
static int sysctl_ip_vs_drop_packet = 0;
static int sysctl_ip_vs_secure_tcp = 0;
static int sysctl_ip_vs_amemthresh = 1024;
static int sysctl_ip_vs_am_droprate = 10;
int sysctl_ip_vs_cache_bypass = 0;
int sysctl_ip_vs_expire_nodest_conn = 0;
int sysctl_ip_vs_expire_quiescent_template = 0;
int sysctl_ip_vs_sync_threshold[2] = { 3, 50 };
int sysctl_ip_vs_nat_icmp_send = 0;


#ifdef CONFIG_IP_VS_DEBUG
static int sysctl_ip_vs_debug_level = 0;

int ip_vs_get_debug_level(void)
{
	return sysctl_ip_vs_debug_level;
}
#endif

/*
 *	update_defense_level is called from keventd and from sysctl,
 *	so it needs to protect itself from softirqs
 */
static void update_defense_level(void)
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

	nomem = (availmem < sysctl_ip_vs_amemthresh);

	local_bh_disable();

	/* drop_entry */
	spin_lock(&__ip_vs_dropentry_lock);
	switch (sysctl_ip_vs_drop_entry) {
	case 0:
		atomic_set(&ip_vs_dropentry, 0);
		break;
	case 1:
		if (nomem) {
			atomic_set(&ip_vs_dropentry, 1);
			sysctl_ip_vs_drop_entry = 2;
		} else {
			atomic_set(&ip_vs_dropentry, 0);
		}
		break;
	case 2:
		if (nomem) {
			atomic_set(&ip_vs_dropentry, 1);
		} else {
			atomic_set(&ip_vs_dropentry, 0);
			sysctl_ip_vs_drop_entry = 1;
		};
		break;
	case 3:
		atomic_set(&ip_vs_dropentry, 1);
		break;
	}
	spin_unlock(&__ip_vs_dropentry_lock);

	/* drop_packet */
	spin_lock(&__ip_vs_droppacket_lock);
	switch (sysctl_ip_vs_drop_packet) {
	case 0:
		ip_vs_drop_rate = 0;
		break;
	case 1:
		if (nomem) {
			ip_vs_drop_rate = ip_vs_drop_counter
				= sysctl_ip_vs_amemthresh /
				(sysctl_ip_vs_amemthresh-availmem);
			sysctl_ip_vs_drop_packet = 2;
		} else {
			ip_vs_drop_rate = 0;
		}
		break;
	case 2:
		if (nomem) {
			ip_vs_drop_rate = ip_vs_drop_counter
				= sysctl_ip_vs_amemthresh /
				(sysctl_ip_vs_amemthresh-availmem);
		} else {
			ip_vs_drop_rate = 0;
			sysctl_ip_vs_drop_packet = 1;
		}
		break;
	case 3:
		ip_vs_drop_rate = sysctl_ip_vs_am_droprate;
		break;
	}
	spin_unlock(&__ip_vs_droppacket_lock);

	/* secure_tcp */
	write_lock(&__ip_vs_securetcp_lock);
	switch (sysctl_ip_vs_secure_tcp) {
	case 0:
		if (old_secure_tcp >= 2)
			to_change = 0;
		break;
	case 1:
		if (nomem) {
			if (old_secure_tcp < 2)
				to_change = 1;
			sysctl_ip_vs_secure_tcp = 2;
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
			sysctl_ip_vs_secure_tcp = 1;
		}
		break;
	case 3:
		if (old_secure_tcp < 2)
			to_change = 1;
		break;
	}
	old_secure_tcp = sysctl_ip_vs_secure_tcp;
	if (to_change >= 0)
		ip_vs_protocol_timeout_change(sysctl_ip_vs_secure_tcp>1);
	write_unlock(&__ip_vs_securetcp_lock);

	local_bh_enable();
}


/*
 *	Timer for checking the defense
 */
#define DEFENSE_TIMER_PERIOD	1*HZ
static void defense_work_handler(struct work_struct *work);
static DECLARE_DELAYED_WORK(defense_work, defense_work_handler);

static void defense_work_handler(struct work_struct *work)
{
	update_defense_level();
	if (atomic_read(&ip_vs_dropentry))
		ip_vs_random_dropentry();

	schedule_delayed_work(&defense_work, DEFENSE_TIMER_PERIOD);
}

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
static struct list_head ip_vs_svc_table[IP_VS_SVC_TAB_SIZE];
/* the service table hashed by fwmark */
static struct list_head ip_vs_svc_fwm_table[IP_VS_SVC_TAB_SIZE];

/*
 *	Hash table: for real service lookups
 */
#define IP_VS_RTAB_BITS 4
#define IP_VS_RTAB_SIZE (1 << IP_VS_RTAB_BITS)
#define IP_VS_RTAB_MASK (IP_VS_RTAB_SIZE - 1)

static struct list_head ip_vs_rtable[IP_VS_RTAB_SIZE];

/*
 *	Trash for destinations
 */
static LIST_HEAD(ip_vs_dest_trash);

/*
 *	FTP & NULL virtual service counters
 */
static atomic_t ip_vs_ftpsvc_counter = ATOMIC_INIT(0);
static atomic_t ip_vs_nullsvc_counter = ATOMIC_INIT(0);


/*
 *	Returns hash value for virtual service
 */
static __inline__ unsigned
ip_vs_svc_hashkey(unsigned proto, __be32 addr, __be16 port)
{
	register unsigned porth = ntohs(port);

	return (proto^ntohl(addr)^(porth>>IP_VS_SVC_TAB_BITS)^porth)
		& IP_VS_SVC_TAB_MASK;
}

/*
 *	Returns hash value of fwmark for virtual service lookup
 */
static __inline__ unsigned ip_vs_svc_fwm_hashkey(__u32 fwmark)
{
	return fwmark & IP_VS_SVC_TAB_MASK;
}

/*
 *	Hashes a service in the ip_vs_svc_table by <proto,addr,port>
 *	or in the ip_vs_svc_fwm_table by fwmark.
 *	Should be called with locked tables.
 */
static int ip_vs_svc_hash(struct ip_vs_service *svc)
{
	unsigned hash;

	if (svc->flags & IP_VS_SVC_F_HASHED) {
		IP_VS_ERR("ip_vs_svc_hash(): request for already hashed, "
			  "called from %p\n", __builtin_return_address(0));
		return 0;
	}

	if (svc->fwmark == 0) {
		/*
		 *  Hash it by <protocol,addr,port> in ip_vs_svc_table
		 */
		hash = ip_vs_svc_hashkey(svc->protocol, svc->addr, svc->port);
		list_add(&svc->s_list, &ip_vs_svc_table[hash]);
	} else {
		/*
		 *  Hash it by fwmark in ip_vs_svc_fwm_table
		 */
		hash = ip_vs_svc_fwm_hashkey(svc->fwmark);
		list_add(&svc->f_list, &ip_vs_svc_fwm_table[hash]);
	}

	svc->flags |= IP_VS_SVC_F_HASHED;
	/* increase its refcnt because it is referenced by the svc table */
	atomic_inc(&svc->refcnt);
	return 1;
}


/*
 *	Unhashes a service from ip_vs_svc_table/ip_vs_svc_fwm_table.
 *	Should be called with locked tables.
 */
static int ip_vs_svc_unhash(struct ip_vs_service *svc)
{
	if (!(svc->flags & IP_VS_SVC_F_HASHED)) {
		IP_VS_ERR("ip_vs_svc_unhash(): request for unhash flagged, "
			  "called from %p\n", __builtin_return_address(0));
		return 0;
	}

	if (svc->fwmark == 0) {
		/* Remove it from the ip_vs_svc_table table */
		list_del(&svc->s_list);
	} else {
		/* Remove it from the ip_vs_svc_fwm_table table */
		list_del(&svc->f_list);
	}

	svc->flags &= ~IP_VS_SVC_F_HASHED;
	atomic_dec(&svc->refcnt);
	return 1;
}


/*
 *	Get service by {proto,addr,port} in the service table.
 */
static __inline__ struct ip_vs_service *
__ip_vs_service_get(__u16 protocol, __be32 vaddr, __be16 vport)
{
	unsigned hash;
	struct ip_vs_service *svc;

	/* Check for "full" addressed entries */
	hash = ip_vs_svc_hashkey(protocol, vaddr, vport);

	list_for_each_entry(svc, &ip_vs_svc_table[hash], s_list){
		if ((svc->addr == vaddr)
		    && (svc->port == vport)
		    && (svc->protocol == protocol)) {
			/* HIT */
			atomic_inc(&svc->usecnt);
			return svc;
		}
	}

	return NULL;
}


/*
 *	Get service by {fwmark} in the service table.
 */
static __inline__ struct ip_vs_service *__ip_vs_svc_fwm_get(__u32 fwmark)
{
	unsigned hash;
	struct ip_vs_service *svc;

	/* Check for fwmark addressed entries */
	hash = ip_vs_svc_fwm_hashkey(fwmark);

	list_for_each_entry(svc, &ip_vs_svc_fwm_table[hash], f_list) {
		if (svc->fwmark == fwmark) {
			/* HIT */
			atomic_inc(&svc->usecnt);
			return svc;
		}
	}

	return NULL;
}

struct ip_vs_service *
ip_vs_service_get(__u32 fwmark, __u16 protocol, __be32 vaddr, __be16 vport)
{
	struct ip_vs_service *svc;

	read_lock(&__ip_vs_svc_lock);

	/*
	 *	Check the table hashed by fwmark first
	 */
	if (fwmark && (svc = __ip_vs_svc_fwm_get(fwmark)))
		goto out;

	/*
	 *	Check the table hashed by <protocol,addr,port>
	 *	for "full" addressed entries
	 */
	svc = __ip_vs_service_get(protocol, vaddr, vport);

	if (svc == NULL
	    && protocol == IPPROTO_TCP
	    && atomic_read(&ip_vs_ftpsvc_counter)
	    && (vport == FTPDATA || ntohs(vport) >= PROT_SOCK)) {
		/*
		 * Check if ftp service entry exists, the packet
		 * might belong to FTP data connections.
		 */
		svc = __ip_vs_service_get(protocol, vaddr, FTPPORT);
	}

	if (svc == NULL
	    && atomic_read(&ip_vs_nullsvc_counter)) {
		/*
		 * Check if the catch-all port (port zero) exists
		 */
		svc = __ip_vs_service_get(protocol, vaddr, 0);
	}

  out:
	read_unlock(&__ip_vs_svc_lock);

	IP_VS_DBG(9, "lookup service: fwm %u %s %u.%u.%u.%u:%u %s\n",
		  fwmark, ip_vs_proto_name(protocol),
		  NIPQUAD(vaddr), ntohs(vport),
		  svc?"hit":"not hit");

	return svc;
}


static inline void
__ip_vs_bind_svc(struct ip_vs_dest *dest, struct ip_vs_service *svc)
{
	atomic_inc(&svc->refcnt);
	dest->svc = svc;
}

static inline void
__ip_vs_unbind_svc(struct ip_vs_dest *dest)
{
	struct ip_vs_service *svc = dest->svc;

	dest->svc = NULL;
	if (atomic_dec_and_test(&svc->refcnt))
		kfree(svc);
}


/*
 *	Returns hash value for real service
 */
static __inline__ unsigned ip_vs_rs_hashkey(__be32 addr, __be16 port)
{
	register unsigned porth = ntohs(port);

	return (ntohl(addr)^(porth>>IP_VS_RTAB_BITS)^porth)
		& IP_VS_RTAB_MASK;
}

/*
 *	Hashes ip_vs_dest in ip_vs_rtable by <proto,addr,port>.
 *	should be called with locked tables.
 */
static int ip_vs_rs_hash(struct ip_vs_dest *dest)
{
	unsigned hash;

	if (!list_empty(&dest->d_list)) {
		return 0;
	}

	/*
	 *	Hash by proto,addr,port,
	 *	which are the parameters of the real service.
	 */
	hash = ip_vs_rs_hashkey(dest->addr, dest->port);
	list_add(&dest->d_list, &ip_vs_rtable[hash]);

	return 1;
}

/*
 *	UNhashes ip_vs_dest from ip_vs_rtable.
 *	should be called with locked tables.
 */
static int ip_vs_rs_unhash(struct ip_vs_dest *dest)
{
	/*
	 * Remove it from the ip_vs_rtable table.
	 */
	if (!list_empty(&dest->d_list)) {
		list_del(&dest->d_list);
		INIT_LIST_HEAD(&dest->d_list);
	}

	return 1;
}

/*
 *	Lookup real service by <proto,addr,port> in the real service table.
 */
struct ip_vs_dest *
ip_vs_lookup_real_service(__u16 protocol, __be32 daddr, __be16 dport)
{
	unsigned hash;
	struct ip_vs_dest *dest;

	/*
	 *	Check for "full" addressed entries
	 *	Return the first found entry
	 */
	hash = ip_vs_rs_hashkey(daddr, dport);

	read_lock(&__ip_vs_rs_lock);
	list_for_each_entry(dest, &ip_vs_rtable[hash], d_list) {
		if ((dest->addr == daddr)
		    && (dest->port == dport)
		    && ((dest->protocol == protocol) ||
			dest->vfwmark)) {
			/* HIT */
			read_unlock(&__ip_vs_rs_lock);
			return dest;
		}
	}
	read_unlock(&__ip_vs_rs_lock);

	return NULL;
}

/*
 *	Lookup destination by {addr,port} in the given service
 */
static struct ip_vs_dest *
ip_vs_lookup_dest(struct ip_vs_service *svc, __be32 daddr, __be16 dport)
{
	struct ip_vs_dest *dest;

	/*
	 * Find the destination for the given service
	 */
	list_for_each_entry(dest, &svc->destinations, n_list) {
		if ((dest->addr == daddr) && (dest->port == dport)) {
			/* HIT */
			return dest;
		}
	}

	return NULL;
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
ip_vs_trash_get_dest(struct ip_vs_service *svc, __be32 daddr, __be16 dport)
{
	struct ip_vs_dest *dest, *nxt;

	/*
	 * Find the destination in trash
	 */
	list_for_each_entry_safe(dest, nxt, &ip_vs_dest_trash, n_list) {
		IP_VS_DBG(3, "Destination %u/%u.%u.%u.%u:%u still in trash, "
			  "dest->refcnt=%d\n",
			  dest->vfwmark,
			  NIPQUAD(dest->addr), ntohs(dest->port),
			  atomic_read(&dest->refcnt));
		if (dest->addr == daddr &&
		    dest->port == dport &&
		    dest->vfwmark == svc->fwmark &&
		    dest->protocol == svc->protocol &&
		    (svc->fwmark ||
		     (dest->vaddr == svc->addr &&
		      dest->vport == svc->port))) {
			/* HIT */
			return dest;
		}

		/*
		 * Try to purge the destination from trash if not referenced
		 */
		if (atomic_read(&dest->refcnt) == 1) {
			IP_VS_DBG(3, "Removing destination %u/%u.%u.%u.%u:%u "
				  "from trash\n",
				  dest->vfwmark,
				  NIPQUAD(dest->addr), ntohs(dest->port));
			list_del(&dest->n_list);
			ip_vs_dst_reset(dest);
			__ip_vs_unbind_svc(dest);
			kfree(dest);
		}
	}

	return NULL;
}


/*
 *  Clean up all the destinations in the trash
 *  Called by the ip_vs_control_cleanup()
 *
 *  When the ip_vs_control_clearup is activated by ipvs module exit,
 *  the service tables must have been flushed and all the connections
 *  are expired, and the refcnt of each destination in the trash must
 *  be 1, so we simply release them here.
 */
static void ip_vs_trash_cleanup(void)
{
	struct ip_vs_dest *dest, *nxt;

	list_for_each_entry_safe(dest, nxt, &ip_vs_dest_trash, n_list) {
		list_del(&dest->n_list);
		ip_vs_dst_reset(dest);
		__ip_vs_unbind_svc(dest);
		kfree(dest);
	}
}


static void
ip_vs_zero_stats(struct ip_vs_stats *stats)
{
	spin_lock_bh(&stats->lock);
	memset(stats, 0, (char *)&stats->lock - (char *)stats);
	spin_unlock_bh(&stats->lock);
	ip_vs_zero_estimator(stats);
}

/*
 *	Update a destination in the given service
 */
static void
__ip_vs_update_dest(struct ip_vs_service *svc,
		    struct ip_vs_dest *dest, struct ip_vs_dest_user *udest)
{
	int conn_flags;

	/* set the weight and the flags */
	atomic_set(&dest->weight, udest->weight);
	conn_flags = udest->conn_flags | IP_VS_CONN_F_INACTIVE;

	/* check if local node and update the flags */
	if (inet_addr_type(udest->addr) == RTN_LOCAL) {
		conn_flags = (conn_flags & ~IP_VS_CONN_F_FWD_MASK)
			| IP_VS_CONN_F_LOCALNODE;
	}

	/* set the IP_VS_CONN_F_NOOUTPUT flag if not masquerading/NAT */
	if ((conn_flags & IP_VS_CONN_F_FWD_MASK) != 0) {
		conn_flags |= IP_VS_CONN_F_NOOUTPUT;
	} else {
		/*
		 *    Put the real service in ip_vs_rtable if not present.
		 *    For now only for NAT!
		 */
		write_lock_bh(&__ip_vs_rs_lock);
		ip_vs_rs_hash(dest);
		write_unlock_bh(&__ip_vs_rs_lock);
	}
	atomic_set(&dest->conn_flags, conn_flags);

	/* bind the service */
	if (!dest->svc) {
		__ip_vs_bind_svc(dest, svc);
	} else {
		if (dest->svc != svc) {
			__ip_vs_unbind_svc(dest);
			ip_vs_zero_stats(&dest->stats);
			__ip_vs_bind_svc(dest, svc);
		}
	}

	/* set the dest status flags */
	dest->flags |= IP_VS_DEST_F_AVAILABLE;

	if (udest->u_threshold == 0 || udest->u_threshold > dest->u_threshold)
		dest->flags &= ~IP_VS_DEST_F_OVERLOAD;
	dest->u_threshold = udest->u_threshold;
	dest->l_threshold = udest->l_threshold;
}


/*
 *	Create a destination for the given service
 */
static int
ip_vs_new_dest(struct ip_vs_service *svc, struct ip_vs_dest_user *udest,
	       struct ip_vs_dest **dest_p)
{
	struct ip_vs_dest *dest;
	unsigned atype;

	EnterFunction(2);

	atype = inet_addr_type(udest->addr);
	if (atype != RTN_LOCAL && atype != RTN_UNICAST)
		return -EINVAL;

	dest = kzalloc(sizeof(struct ip_vs_dest), GFP_ATOMIC);
	if (dest == NULL) {
		IP_VS_ERR("ip_vs_new_dest: kmalloc failed.\n");
		return -ENOMEM;
	}

	dest->protocol = svc->protocol;
	dest->vaddr = svc->addr;
	dest->vport = svc->port;
	dest->vfwmark = svc->fwmark;
	dest->addr = udest->addr;
	dest->port = udest->port;

	atomic_set(&dest->activeconns, 0);
	atomic_set(&dest->inactconns, 0);
	atomic_set(&dest->persistconns, 0);
	atomic_set(&dest->refcnt, 0);

	INIT_LIST_HEAD(&dest->d_list);
	spin_lock_init(&dest->dst_lock);
	spin_lock_init(&dest->stats.lock);
	__ip_vs_update_dest(svc, dest, udest);
	ip_vs_new_estimator(&dest->stats);

	*dest_p = dest;

	LeaveFunction(2);
	return 0;
}


/*
 *	Add a destination into an existing service
 */
static int
ip_vs_add_dest(struct ip_vs_service *svc, struct ip_vs_dest_user *udest)
{
	struct ip_vs_dest *dest;
	__be32 daddr = udest->addr;
	__be16 dport = udest->port;
	int ret;

	EnterFunction(2);

	if (udest->weight < 0) {
		IP_VS_ERR("ip_vs_add_dest(): server weight less than zero\n");
		return -ERANGE;
	}

	if (udest->l_threshold > udest->u_threshold) {
		IP_VS_ERR("ip_vs_add_dest(): lower threshold is higher than "
			  "upper threshold\n");
		return -ERANGE;
	}

	/*
	 * Check if the dest already exists in the list
	 */
	dest = ip_vs_lookup_dest(svc, daddr, dport);
	if (dest != NULL) {
		IP_VS_DBG(1, "ip_vs_add_dest(): dest already exists\n");
		return -EEXIST;
	}

	/*
	 * Check if the dest already exists in the trash and
	 * is from the same service
	 */
	dest = ip_vs_trash_get_dest(svc, daddr, dport);
	if (dest != NULL) {
		IP_VS_DBG(3, "Get destination %u.%u.%u.%u:%u from trash, "
			  "dest->refcnt=%d, service %u/%u.%u.%u.%u:%u\n",
			  NIPQUAD(daddr), ntohs(dport),
			  atomic_read(&dest->refcnt),
			  dest->vfwmark,
			  NIPQUAD(dest->vaddr),
			  ntohs(dest->vport));
		__ip_vs_update_dest(svc, dest, udest);

		/*
		 * Get the destination from the trash
		 */
		list_del(&dest->n_list);

		ip_vs_new_estimator(&dest->stats);

		write_lock_bh(&__ip_vs_svc_lock);

		/*
		 * Wait until all other svc users go away.
		 */
		IP_VS_WAIT_WHILE(atomic_read(&svc->usecnt) > 1);

		list_add(&dest->n_list, &svc->destinations);
		svc->num_dests++;

		/* call the update_service function of its scheduler */
		svc->scheduler->update_service(svc);

		write_unlock_bh(&__ip_vs_svc_lock);
		return 0;
	}

	/*
	 * Allocate and initialize the dest structure
	 */
	ret = ip_vs_new_dest(svc, udest, &dest);
	if (ret) {
		return ret;
	}

	/*
	 * Add the dest entry into the list
	 */
	atomic_inc(&dest->refcnt);

	write_lock_bh(&__ip_vs_svc_lock);

	/*
	 * Wait until all other svc users go away.
	 */
	IP_VS_WAIT_WHILE(atomic_read(&svc->usecnt) > 1);

	list_add(&dest->n_list, &svc->destinations);
	svc->num_dests++;

	/* call the update_service function of its scheduler */
	svc->scheduler->update_service(svc);

	write_unlock_bh(&__ip_vs_svc_lock);

	LeaveFunction(2);

	return 0;
}


/*
 *	Edit a destination in the given service
 */
static int
ip_vs_edit_dest(struct ip_vs_service *svc, struct ip_vs_dest_user *udest)
{
	struct ip_vs_dest *dest;
	__be32 daddr = udest->addr;
	__be16 dport = udest->port;

	EnterFunction(2);

	if (udest->weight < 0) {
		IP_VS_ERR("ip_vs_edit_dest(): server weight less than zero\n");
		return -ERANGE;
	}

	if (udest->l_threshold > udest->u_threshold) {
		IP_VS_ERR("ip_vs_edit_dest(): lower threshold is higher than "
			  "upper threshold\n");
		return -ERANGE;
	}

	/*
	 *  Lookup the destination list
	 */
	dest = ip_vs_lookup_dest(svc, daddr, dport);
	if (dest == NULL) {
		IP_VS_DBG(1, "ip_vs_edit_dest(): dest doesn't exist\n");
		return -ENOENT;
	}

	__ip_vs_update_dest(svc, dest, udest);

	write_lock_bh(&__ip_vs_svc_lock);

	/* Wait until all other svc users go away */
	IP_VS_WAIT_WHILE(atomic_read(&svc->usecnt) > 1);

	/* call the update_service, because server weight may be changed */
	svc->scheduler->update_service(svc);

	write_unlock_bh(&__ip_vs_svc_lock);

	LeaveFunction(2);

	return 0;
}


/*
 *	Delete a destination (must be already unlinked from the service)
 */
static void __ip_vs_del_dest(struct ip_vs_dest *dest)
{
	ip_vs_kill_estimator(&dest->stats);

	/*
	 *  Remove it from the d-linked list with the real services.
	 */
	write_lock_bh(&__ip_vs_rs_lock);
	ip_vs_rs_unhash(dest);
	write_unlock_bh(&__ip_vs_rs_lock);

	/*
	 *  Decrease the refcnt of the dest, and free the dest
	 *  if nobody refers to it (refcnt=0). Otherwise, throw
	 *  the destination into the trash.
	 */
	if (atomic_dec_and_test(&dest->refcnt)) {
		ip_vs_dst_reset(dest);
		/* simply decrease svc->refcnt here, let the caller check
		   and release the service if nobody refers to it.
		   Only user context can release destination and service,
		   and only one user context can update virtual service at a
		   time, so the operation here is OK */
		atomic_dec(&dest->svc->refcnt);
		kfree(dest);
	} else {
		IP_VS_DBG(3, "Moving dest %u.%u.%u.%u:%u into trash, "
			  "dest->refcnt=%d\n",
			  NIPQUAD(dest->addr), ntohs(dest->port),
			  atomic_read(&dest->refcnt));
		list_add(&dest->n_list, &ip_vs_dest_trash);
		atomic_inc(&dest->refcnt);
	}
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
	list_del(&dest->n_list);
	svc->num_dests--;
	if (svcupd) {
		/*
		 *  Call the update_service function of its scheduler
		 */
		svc->scheduler->update_service(svc);
	}
}


/*
 *	Delete a destination server in the given service
 */
static int
ip_vs_del_dest(struct ip_vs_service *svc,struct ip_vs_dest_user *udest)
{
	struct ip_vs_dest *dest;
	__be32 daddr = udest->addr;
	__be16 dport = udest->port;

	EnterFunction(2);

	dest = ip_vs_lookup_dest(svc, daddr, dport);
	if (dest == NULL) {
		IP_VS_DBG(1, "ip_vs_del_dest(): destination not found!\n");
		return -ENOENT;
	}

	write_lock_bh(&__ip_vs_svc_lock);

	/*
	 *	Wait until all other svc users go away.
	 */
	IP_VS_WAIT_WHILE(atomic_read(&svc->usecnt) > 1);

	/*
	 *	Unlink dest from the service
	 */
	__ip_vs_unlink_dest(svc, dest, 1);

	write_unlock_bh(&__ip_vs_svc_lock);

	/*
	 *	Delete the destination
	 */
	__ip_vs_del_dest(dest);

	LeaveFunction(2);

	return 0;
}


/*
 *	Add a service into the service hash table
 */
static int
ip_vs_add_service(struct ip_vs_service_user *u, struct ip_vs_service **svc_p)
{
	int ret = 0;
	struct ip_vs_scheduler *sched = NULL;
	struct ip_vs_service *svc = NULL;

	/* increase the module use count */
	ip_vs_use_count_inc();

	/* Lookup the scheduler by 'u->sched_name' */
	sched = ip_vs_scheduler_get(u->sched_name);
	if (sched == NULL) {
		IP_VS_INFO("Scheduler module ip_vs_%s not found\n",
			   u->sched_name);
		ret = -ENOENT;
		goto out_mod_dec;
	}

	svc = kzalloc(sizeof(struct ip_vs_service), GFP_ATOMIC);
	if (svc == NULL) {
		IP_VS_DBG(1, "ip_vs_add_service: kmalloc failed.\n");
		ret = -ENOMEM;
		goto out_err;
	}

	/* I'm the first user of the service */
	atomic_set(&svc->usecnt, 1);
	atomic_set(&svc->refcnt, 0);

	svc->protocol = u->protocol;
	svc->addr = u->addr;
	svc->port = u->port;
	svc->fwmark = u->fwmark;
	svc->flags = u->flags;
	svc->timeout = u->timeout * HZ;
	svc->netmask = u->netmask;

	INIT_LIST_HEAD(&svc->destinations);
	rwlock_init(&svc->sched_lock);
	spin_lock_init(&svc->stats.lock);

	/* Bind the scheduler */
	ret = ip_vs_bind_scheduler(svc, sched);
	if (ret)
		goto out_err;
	sched = NULL;

	/* Update the virtual service counters */
	if (svc->port == FTPPORT)
		atomic_inc(&ip_vs_ftpsvc_counter);
	else if (svc->port == 0)
		atomic_inc(&ip_vs_nullsvc_counter);

	ip_vs_new_estimator(&svc->stats);
	ip_vs_num_services++;

	/* Hash the service into the service table */
	write_lock_bh(&__ip_vs_svc_lock);
	ip_vs_svc_hash(svc);
	write_unlock_bh(&__ip_vs_svc_lock);

	*svc_p = svc;
	return 0;

  out_err:
	if (svc != NULL) {
		if (svc->scheduler)
			ip_vs_unbind_scheduler(svc);
		if (svc->inc) {
			local_bh_disable();
			ip_vs_app_inc_put(svc->inc);
			local_bh_enable();
		}
		kfree(svc);
	}
	ip_vs_scheduler_put(sched);

  out_mod_dec:
	/* decrease the module use count */
	ip_vs_use_count_dec();

	return ret;
}


/*
 *	Edit a service and bind it with a new scheduler
 */
static int
ip_vs_edit_service(struct ip_vs_service *svc, struct ip_vs_service_user *u)
{
	struct ip_vs_scheduler *sched, *old_sched;
	int ret = 0;

	/*
	 * Lookup the scheduler, by 'u->sched_name'
	 */
	sched = ip_vs_scheduler_get(u->sched_name);
	if (sched == NULL) {
		IP_VS_INFO("Scheduler module ip_vs_%s not found\n",
			   u->sched_name);
		return -ENOENT;
	}
	old_sched = sched;

	write_lock_bh(&__ip_vs_svc_lock);

	/*
	 * Wait until all other svc users go away.
	 */
	IP_VS_WAIT_WHILE(atomic_read(&svc->usecnt) > 1);

	/*
	 * Set the flags and timeout value
	 */
	svc->flags = u->flags | IP_VS_SVC_F_HASHED;
	svc->timeout = u->timeout * HZ;
	svc->netmask = u->netmask;

	old_sched = svc->scheduler;
	if (sched != old_sched) {
		/*
		 * Unbind the old scheduler
		 */
		if ((ret = ip_vs_unbind_scheduler(svc))) {
			old_sched = sched;
			goto out;
		}

		/*
		 * Bind the new scheduler
		 */
		if ((ret = ip_vs_bind_scheduler(svc, sched))) {
			/*
			 * If ip_vs_bind_scheduler fails, restore the old
			 * scheduler.
			 * The main reason of failure is out of memory.
			 *
			 * The question is if the old scheduler can be
			 * restored all the time. TODO: if it cannot be
			 * restored some time, we must delete the service,
			 * otherwise the system may crash.
			 */
			ip_vs_bind_scheduler(svc, old_sched);
			old_sched = sched;
			goto out;
		}
	}

  out:
	write_unlock_bh(&__ip_vs_svc_lock);

	if (old_sched)
		ip_vs_scheduler_put(old_sched);

	return ret;
}


/*
 *	Delete a service from the service list
 *	- The service must be unlinked, unlocked and not referenced!
 *	- We are called under _bh lock
 */
static void __ip_vs_del_service(struct ip_vs_service *svc)
{
	struct ip_vs_dest *dest, *nxt;
	struct ip_vs_scheduler *old_sched;

	ip_vs_num_services--;
	ip_vs_kill_estimator(&svc->stats);

	/* Unbind scheduler */
	old_sched = svc->scheduler;
	ip_vs_unbind_scheduler(svc);
	if (old_sched)
		ip_vs_scheduler_put(old_sched);

	/* Unbind app inc */
	if (svc->inc) {
		ip_vs_app_inc_put(svc->inc);
		svc->inc = NULL;
	}

	/*
	 *    Unlink the whole destination list
	 */
	list_for_each_entry_safe(dest, nxt, &svc->destinations, n_list) {
		__ip_vs_unlink_dest(svc, dest, 0);
		__ip_vs_del_dest(dest);
	}

	/*
	 *    Update the virtual service counters
	 */
	if (svc->port == FTPPORT)
		atomic_dec(&ip_vs_ftpsvc_counter);
	else if (svc->port == 0)
		atomic_dec(&ip_vs_nullsvc_counter);

	/*
	 *    Free the service if nobody refers to it
	 */
	if (atomic_read(&svc->refcnt) == 0)
		kfree(svc);

	/* decrease the module use count */
	ip_vs_use_count_dec();
}

/*
 *	Delete a service from the service list
 */
static int ip_vs_del_service(struct ip_vs_service *svc)
{
	if (svc == NULL)
		return -EEXIST;

	/*
	 * Unhash it from the service table
	 */
	write_lock_bh(&__ip_vs_svc_lock);

	ip_vs_svc_unhash(svc);

	/*
	 * Wait until all the svc users go away.
	 */
	IP_VS_WAIT_WHILE(atomic_read(&svc->usecnt) > 1);

	__ip_vs_del_service(svc);

	write_unlock_bh(&__ip_vs_svc_lock);

	return 0;
}


/*
 *	Flush all the virtual services
 */
static int ip_vs_flush(void)
{
	int idx;
	struct ip_vs_service *svc, *nxt;

	/*
	 * Flush the service table hashed by <protocol,addr,port>
	 */
	for(idx = 0; idx < IP_VS_SVC_TAB_SIZE; idx++) {
		list_for_each_entry_safe(svc, nxt, &ip_vs_svc_table[idx], s_list) {
			write_lock_bh(&__ip_vs_svc_lock);
			ip_vs_svc_unhash(svc);
			/*
			 * Wait until all the svc users go away.
			 */
			IP_VS_WAIT_WHILE(atomic_read(&svc->usecnt) > 0);
			__ip_vs_del_service(svc);
			write_unlock_bh(&__ip_vs_svc_lock);
		}
	}

	/*
	 * Flush the service table hashed by fwmark
	 */
	for(idx = 0; idx < IP_VS_SVC_TAB_SIZE; idx++) {
		list_for_each_entry_safe(svc, nxt,
					 &ip_vs_svc_fwm_table[idx], f_list) {
			write_lock_bh(&__ip_vs_svc_lock);
			ip_vs_svc_unhash(svc);
			/*
			 * Wait until all the svc users go away.
			 */
			IP_VS_WAIT_WHILE(atomic_read(&svc->usecnt) > 0);
			__ip_vs_del_service(svc);
			write_unlock_bh(&__ip_vs_svc_lock);
		}
	}

	return 0;
}


/*
 *	Zero counters in a service or all services
 */
static int ip_vs_zero_service(struct ip_vs_service *svc)
{
	struct ip_vs_dest *dest;

	write_lock_bh(&__ip_vs_svc_lock);
	list_for_each_entry(dest, &svc->destinations, n_list) {
		ip_vs_zero_stats(&dest->stats);
	}
	ip_vs_zero_stats(&svc->stats);
	write_unlock_bh(&__ip_vs_svc_lock);
	return 0;
}

static int ip_vs_zero_all(void)
{
	int idx;
	struct ip_vs_service *svc;

	for(idx = 0; idx < IP_VS_SVC_TAB_SIZE; idx++) {
		list_for_each_entry(svc, &ip_vs_svc_table[idx], s_list) {
			ip_vs_zero_service(svc);
		}
	}

	for(idx = 0; idx < IP_VS_SVC_TAB_SIZE; idx++) {
		list_for_each_entry(svc, &ip_vs_svc_fwm_table[idx], f_list) {
			ip_vs_zero_service(svc);
		}
	}

	ip_vs_zero_stats(&ip_vs_stats);
	return 0;
}


static int
proc_do_defense_mode(ctl_table *table, int write, struct file * filp,
		     void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int *valp = table->data;
	int val = *valp;
	int rc;

	rc = proc_dointvec(table, write, filp, buffer, lenp, ppos);
	if (write && (*valp != val)) {
		if ((*valp < 0) || (*valp > 3)) {
			/* Restore the correct value */
			*valp = val;
		} else {
			update_defense_level();
		}
	}
	return rc;
}


static int
proc_do_sync_threshold(ctl_table *table, int write, struct file *filp,
		       void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int *valp = table->data;
	int val[2];
	int rc;

	/* backup the value first */
	memcpy(val, valp, sizeof(val));

	rc = proc_dointvec(table, write, filp, buffer, lenp, ppos);
	if (write && (valp[0] < 0 || valp[1] < 0 || valp[0] >= valp[1])) {
		/* Restore the correct value */
		memcpy(valp, val, sizeof(val));
	}
	return rc;
}


/*
 *	IPVS sysctl table (under the /proc/sys/net/ipv4/vs/)
 */

static struct ctl_table vs_vars[] = {
	{
		.ctl_name	= NET_IPV4_VS_AMEMTHRESH,
		.procname	= "amemthresh",
		.data		= &sysctl_ip_vs_amemthresh,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#ifdef CONFIG_IP_VS_DEBUG
	{
		.ctl_name	= NET_IPV4_VS_DEBUG_LEVEL,
		.procname	= "debug_level",
		.data		= &sysctl_ip_vs_debug_level,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
	{
		.ctl_name	= NET_IPV4_VS_AMDROPRATE,
		.procname	= "am_droprate",
		.data		= &sysctl_ip_vs_am_droprate,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_IPV4_VS_DROP_ENTRY,
		.procname	= "drop_entry",
		.data		= &sysctl_ip_vs_drop_entry,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_do_defense_mode,
	},
	{
		.ctl_name	= NET_IPV4_VS_DROP_PACKET,
		.procname	= "drop_packet",
		.data		= &sysctl_ip_vs_drop_packet,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_do_defense_mode,
	},
	{
		.ctl_name	= NET_IPV4_VS_SECURE_TCP,
		.procname	= "secure_tcp",
		.data		= &sysctl_ip_vs_secure_tcp,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_do_defense_mode,
	},
#if 0
	{
		.ctl_name	= NET_IPV4_VS_TO_ES,
		.procname	= "timeout_established",
		.data	= &vs_timeout_table_dos.timeout[IP_VS_S_ESTABLISHED],
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_VS_TO_SS,
		.procname	= "timeout_synsent",
		.data	= &vs_timeout_table_dos.timeout[IP_VS_S_SYN_SENT],
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_VS_TO_SR,
		.procname	= "timeout_synrecv",
		.data	= &vs_timeout_table_dos.timeout[IP_VS_S_SYN_RECV],
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_VS_TO_FW,
		.procname	= "timeout_finwait",
		.data	= &vs_timeout_table_dos.timeout[IP_VS_S_FIN_WAIT],
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_VS_TO_TW,
		.procname	= "timeout_timewait",
		.data	= &vs_timeout_table_dos.timeout[IP_VS_S_TIME_WAIT],
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_VS_TO_CL,
		.procname	= "timeout_close",
		.data	= &vs_timeout_table_dos.timeout[IP_VS_S_CLOSE],
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_VS_TO_CW,
		.procname	= "timeout_closewait",
		.data	= &vs_timeout_table_dos.timeout[IP_VS_S_CLOSE_WAIT],
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_VS_TO_LA,
		.procname	= "timeout_lastack",
		.data	= &vs_timeout_table_dos.timeout[IP_VS_S_LAST_ACK],
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_VS_TO_LI,
		.procname	= "timeout_listen",
		.data	= &vs_timeout_table_dos.timeout[IP_VS_S_LISTEN],
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_VS_TO_SA,
		.procname	= "timeout_synack",
		.data	= &vs_timeout_table_dos.timeout[IP_VS_S_SYNACK],
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_VS_TO_UDP,
		.procname	= "timeout_udp",
		.data	= &vs_timeout_table_dos.timeout[IP_VS_S_UDP],
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_VS_TO_ICMP,
		.procname	= "timeout_icmp",
		.data	= &vs_timeout_table_dos.timeout[IP_VS_S_ICMP],
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
#endif
	{
		.ctl_name	= NET_IPV4_VS_CACHE_BYPASS,
		.procname	= "cache_bypass",
		.data		= &sysctl_ip_vs_cache_bypass,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_IPV4_VS_EXPIRE_NODEST_CONN,
		.procname	= "expire_nodest_conn",
		.data		= &sysctl_ip_vs_expire_nodest_conn,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_IPV4_VS_EXPIRE_QUIESCENT_TEMPLATE,
		.procname	= "expire_quiescent_template",
		.data		= &sysctl_ip_vs_expire_quiescent_template,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_IPV4_VS_SYNC_THRESHOLD,
		.procname	= "sync_threshold",
		.data		= &sysctl_ip_vs_sync_threshold,
		.maxlen		= sizeof(sysctl_ip_vs_sync_threshold),
		.mode		= 0644,
		.proc_handler	= &proc_do_sync_threshold,
	},
	{
		.ctl_name	= NET_IPV4_VS_NAT_ICMP_SEND,
		.procname	= "nat_icmp_send",
		.data		= &sysctl_ip_vs_nat_icmp_send,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{ .ctl_name = 0 }
};

static ctl_table vs_table[] = {
	{
		.ctl_name	= NET_IPV4_VS,
		.procname	= "vs",
		.mode		= 0555,
		.child		= vs_vars
	},
	{ .ctl_name = 0 }
};

static ctl_table ipvs_ipv4_table[] = {
	{
		.ctl_name	= NET_IPV4,
		.procname	= "ipv4",
		.mode		= 0555,
		.child		= vs_table,
	},
	{ .ctl_name = 0 }
};

static ctl_table vs_root_table[] = {
	{
		.ctl_name	= CTL_NET,
		.procname	= "net",
		.mode		= 0555,
		.child		= ipvs_ipv4_table,
	},
	{ .ctl_name = 0 }
};

static struct ctl_table_header * sysctl_header;

#ifdef CONFIG_PROC_FS

struct ip_vs_iter {
	struct list_head *table;
	int bucket;
};

/*
 *	Write the contents of the VS rule table to a PROCfs file.
 *	(It is kept just for backward compatibility)
 */
static inline const char *ip_vs_fwd_name(unsigned flags)
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
	struct ip_vs_iter *iter = seq->private;
	int idx;
	struct ip_vs_service *svc;

	/* look in hash by protocol */
	for (idx = 0; idx < IP_VS_SVC_TAB_SIZE; idx++) {
		list_for_each_entry(svc, &ip_vs_svc_table[idx], s_list) {
			if (pos-- == 0){
				iter->table = ip_vs_svc_table;
				iter->bucket = idx;
				return svc;
			}
		}
	}

	/* keep looking in fwmark */
	for (idx = 0; idx < IP_VS_SVC_TAB_SIZE; idx++) {
		list_for_each_entry(svc, &ip_vs_svc_fwm_table[idx], f_list) {
			if (pos-- == 0) {
				iter->table = ip_vs_svc_fwm_table;
				iter->bucket = idx;
				return svc;
			}
		}
	}

	return NULL;
}

static void *ip_vs_info_seq_start(struct seq_file *seq, loff_t *pos)
{

	read_lock_bh(&__ip_vs_svc_lock);
	return *pos ? ip_vs_info_array(seq, *pos - 1) : SEQ_START_TOKEN;
}


static void *ip_vs_info_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct list_head *e;
	struct ip_vs_iter *iter;
	struct ip_vs_service *svc;

	++*pos;
	if (v == SEQ_START_TOKEN)
		return ip_vs_info_array(seq,0);

	svc = v;
	iter = seq->private;

	if (iter->table == ip_vs_svc_table) {
		/* next service in table hashed by protocol */
		if ((e = svc->s_list.next) != &ip_vs_svc_table[iter->bucket])
			return list_entry(e, struct ip_vs_service, s_list);


		while (++iter->bucket < IP_VS_SVC_TAB_SIZE) {
			list_for_each_entry(svc,&ip_vs_svc_table[iter->bucket],
					    s_list) {
				return svc;
			}
		}

		iter->table = ip_vs_svc_fwm_table;
		iter->bucket = -1;
		goto scan_fwmark;
	}

	/* next service in hashed by fwmark */
	if ((e = svc->f_list.next) != &ip_vs_svc_fwm_table[iter->bucket])
		return list_entry(e, struct ip_vs_service, f_list);

 scan_fwmark:
	while (++iter->bucket < IP_VS_SVC_TAB_SIZE) {
		list_for_each_entry(svc, &ip_vs_svc_fwm_table[iter->bucket],
				    f_list)
			return svc;
	}

	return NULL;
}

static void ip_vs_info_seq_stop(struct seq_file *seq, void *v)
{
	read_unlock_bh(&__ip_vs_svc_lock);
}


static int ip_vs_info_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN) {
		seq_printf(seq,
			"IP Virtual Server version %d.%d.%d (size=%d)\n",
			NVERSION(IP_VS_VERSION_CODE), IP_VS_CONN_TAB_SIZE);
		seq_puts(seq,
			 "Prot LocalAddress:Port Scheduler Flags\n");
		seq_puts(seq,
			 "  -> RemoteAddress:Port Forward Weight ActiveConn InActConn\n");
	} else {
		const struct ip_vs_service *svc = v;
		const struct ip_vs_iter *iter = seq->private;
		const struct ip_vs_dest *dest;

		if (iter->table == ip_vs_svc_table)
			seq_printf(seq, "%s  %08X:%04X %s ",
				   ip_vs_proto_name(svc->protocol),
				   ntohl(svc->addr),
				   ntohs(svc->port),
				   svc->scheduler->name);
		else
			seq_printf(seq, "FWM  %08X %s ",
				   svc->fwmark, svc->scheduler->name);

		if (svc->flags & IP_VS_SVC_F_PERSISTENT)
			seq_printf(seq, "persistent %d %08X\n",
				svc->timeout,
				ntohl(svc->netmask));
		else
			seq_putc(seq, '\n');

		list_for_each_entry(dest, &svc->destinations, n_list) {
			seq_printf(seq,
				   "  -> %08X:%04X      %-7s %-6d %-10d %-10d\n",
				   ntohl(dest->addr), ntohs(dest->port),
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
	return seq_open_private(file, &ip_vs_info_seq_ops,
			sizeof(struct ip_vs_iter));
}

static const struct file_operations ip_vs_info_fops = {
	.owner	 = THIS_MODULE,
	.open    = ip_vs_info_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release_private,
};

#endif

struct ip_vs_stats ip_vs_stats;

#ifdef CONFIG_PROC_FS
static int ip_vs_stats_show(struct seq_file *seq, void *v)
{

/*               01234567 01234567 01234567 0123456701234567 0123456701234567 */
	seq_puts(seq,
		 "   Total Incoming Outgoing         Incoming         Outgoing\n");
	seq_printf(seq,
		   "   Conns  Packets  Packets            Bytes            Bytes\n");

	spin_lock_bh(&ip_vs_stats.lock);
	seq_printf(seq, "%8X %8X %8X %16LX %16LX\n\n", ip_vs_stats.conns,
		   ip_vs_stats.inpkts, ip_vs_stats.outpkts,
		   (unsigned long long) ip_vs_stats.inbytes,
		   (unsigned long long) ip_vs_stats.outbytes);

/*                 01234567 01234567 01234567 0123456701234567 0123456701234567 */
	seq_puts(seq,
		   " Conns/s   Pkts/s   Pkts/s          Bytes/s          Bytes/s\n");
	seq_printf(seq,"%8X %8X %8X %16X %16X\n",
			ip_vs_stats.cps,
			ip_vs_stats.inpps,
			ip_vs_stats.outpps,
			ip_vs_stats.inbps,
			ip_vs_stats.outbps);
	spin_unlock_bh(&ip_vs_stats.lock);

	return 0;
}

static int ip_vs_stats_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, ip_vs_stats_show, NULL);
}

static const struct file_operations ip_vs_stats_fops = {
	.owner = THIS_MODULE,
	.open = ip_vs_stats_seq_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

#endif

/*
 *	Set timeout values for tcp tcpfin udp in the timeout_table.
 */
static int ip_vs_set_timeout(struct ip_vs_timeout_user *u)
{
	IP_VS_DBG(2, "Setting timeout tcp:%d tcpfin:%d udp:%d\n",
		  u->tcp_timeout,
		  u->tcp_fin_timeout,
		  u->udp_timeout);

#ifdef CONFIG_IP_VS_PROTO_TCP
	if (u->tcp_timeout) {
		ip_vs_protocol_tcp.timeout_table[IP_VS_TCP_S_ESTABLISHED]
			= u->tcp_timeout * HZ;
	}

	if (u->tcp_fin_timeout) {
		ip_vs_protocol_tcp.timeout_table[IP_VS_TCP_S_FIN_WAIT]
			= u->tcp_fin_timeout * HZ;
	}
#endif

#ifdef CONFIG_IP_VS_PROTO_UDP
	if (u->udp_timeout) {
		ip_vs_protocol_udp.timeout_table[IP_VS_UDP_S_NORMAL]
			= u->udp_timeout * HZ;
	}
#endif
	return 0;
}


#define SET_CMDID(cmd)		(cmd - IP_VS_BASE_CTL)
#define SERVICE_ARG_LEN		(sizeof(struct ip_vs_service_user))
#define SVCDEST_ARG_LEN		(sizeof(struct ip_vs_service_user) +	\
				 sizeof(struct ip_vs_dest_user))
#define TIMEOUT_ARG_LEN		(sizeof(struct ip_vs_timeout_user))
#define DAEMON_ARG_LEN		(sizeof(struct ip_vs_daemon_user))
#define MAX_ARG_LEN		SVCDEST_ARG_LEN

static const unsigned char set_arglen[SET_CMDID(IP_VS_SO_SET_MAX)+1] = {
	[SET_CMDID(IP_VS_SO_SET_ADD)]		= SERVICE_ARG_LEN,
	[SET_CMDID(IP_VS_SO_SET_EDIT)]		= SERVICE_ARG_LEN,
	[SET_CMDID(IP_VS_SO_SET_DEL)]		= SERVICE_ARG_LEN,
	[SET_CMDID(IP_VS_SO_SET_FLUSH)]		= 0,
	[SET_CMDID(IP_VS_SO_SET_ADDDEST)]	= SVCDEST_ARG_LEN,
	[SET_CMDID(IP_VS_SO_SET_DELDEST)]	= SVCDEST_ARG_LEN,
	[SET_CMDID(IP_VS_SO_SET_EDITDEST)]	= SVCDEST_ARG_LEN,
	[SET_CMDID(IP_VS_SO_SET_TIMEOUT)]	= TIMEOUT_ARG_LEN,
	[SET_CMDID(IP_VS_SO_SET_STARTDAEMON)]	= DAEMON_ARG_LEN,
	[SET_CMDID(IP_VS_SO_SET_STOPDAEMON)]	= DAEMON_ARG_LEN,
	[SET_CMDID(IP_VS_SO_SET_ZERO)]		= SERVICE_ARG_LEN,
};

static int
do_ip_vs_set_ctl(struct sock *sk, int cmd, void __user *user, unsigned int len)
{
	int ret;
	unsigned char arg[MAX_ARG_LEN];
	struct ip_vs_service_user *usvc;
	struct ip_vs_service *svc;
	struct ip_vs_dest_user *udest;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (len != set_arglen[SET_CMDID(cmd)]) {
		IP_VS_ERR("set_ctl: len %u != %u\n",
			  len, set_arglen[SET_CMDID(cmd)]);
		return -EINVAL;
	}

	if (copy_from_user(arg, user, len) != 0)
		return -EFAULT;

	/* increase the module use count */
	ip_vs_use_count_inc();

	if (mutex_lock_interruptible(&__ip_vs_mutex)) {
		ret = -ERESTARTSYS;
		goto out_dec;
	}

	if (cmd == IP_VS_SO_SET_FLUSH) {
		/* Flush the virtual service */
		ret = ip_vs_flush();
		goto out_unlock;
	} else if (cmd == IP_VS_SO_SET_TIMEOUT) {
		/* Set timeout values for (tcp tcpfin udp) */
		ret = ip_vs_set_timeout((struct ip_vs_timeout_user *)arg);
		goto out_unlock;
	} else if (cmd == IP_VS_SO_SET_STARTDAEMON) {
		struct ip_vs_daemon_user *dm = (struct ip_vs_daemon_user *)arg;
		ret = start_sync_thread(dm->state, dm->mcast_ifn, dm->syncid);
		goto out_unlock;
	} else if (cmd == IP_VS_SO_SET_STOPDAEMON) {
		struct ip_vs_daemon_user *dm = (struct ip_vs_daemon_user *)arg;
		ret = stop_sync_thread(dm->state);
		goto out_unlock;
	}

	usvc = (struct ip_vs_service_user *)arg;
	udest = (struct ip_vs_dest_user *)(usvc + 1);

	if (cmd == IP_VS_SO_SET_ZERO) {
		/* if no service address is set, zero counters in all */
		if (!usvc->fwmark && !usvc->addr && !usvc->port) {
			ret = ip_vs_zero_all();
			goto out_unlock;
		}
	}

	/* Check for valid protocol: TCP or UDP, even for fwmark!=0 */
	if (usvc->protocol!=IPPROTO_TCP && usvc->protocol!=IPPROTO_UDP) {
		IP_VS_ERR("set_ctl: invalid protocol: %d %d.%d.%d.%d:%d %s\n",
			  usvc->protocol, NIPQUAD(usvc->addr),
			  ntohs(usvc->port), usvc->sched_name);
		ret = -EFAULT;
		goto out_unlock;
	}

	/* Lookup the exact service by <protocol, addr, port> or fwmark */
	if (usvc->fwmark == 0)
		svc = __ip_vs_service_get(usvc->protocol,
					  usvc->addr, usvc->port);
	else
		svc = __ip_vs_svc_fwm_get(usvc->fwmark);

	if (cmd != IP_VS_SO_SET_ADD
	    && (svc == NULL || svc->protocol != usvc->protocol)) {
		ret = -ESRCH;
		goto out_unlock;
	}

	switch (cmd) {
	case IP_VS_SO_SET_ADD:
		if (svc != NULL)
			ret = -EEXIST;
		else
			ret = ip_vs_add_service(usvc, &svc);
		break;
	case IP_VS_SO_SET_EDIT:
		ret = ip_vs_edit_service(svc, usvc);
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
		ret = ip_vs_add_dest(svc, udest);
		break;
	case IP_VS_SO_SET_EDITDEST:
		ret = ip_vs_edit_dest(svc, udest);
		break;
	case IP_VS_SO_SET_DELDEST:
		ret = ip_vs_del_dest(svc, udest);
		break;
	default:
		ret = -EINVAL;
	}

	if (svc)
		ip_vs_service_put(svc);

  out_unlock:
	mutex_unlock(&__ip_vs_mutex);
  out_dec:
	/* decrease the module use count */
	ip_vs_use_count_dec();

	return ret;
}


static void
ip_vs_copy_stats(struct ip_vs_stats_user *dst, struct ip_vs_stats *src)
{
	spin_lock_bh(&src->lock);
	memcpy(dst, src, (char*)&src->lock - (char*)src);
	spin_unlock_bh(&src->lock);
}

static void
ip_vs_copy_service(struct ip_vs_service_entry *dst, struct ip_vs_service *src)
{
	dst->protocol = src->protocol;
	dst->addr = src->addr;
	dst->port = src->port;
	dst->fwmark = src->fwmark;
	strlcpy(dst->sched_name, src->scheduler->name, sizeof(dst->sched_name));
	dst->flags = src->flags;
	dst->timeout = src->timeout / HZ;
	dst->netmask = src->netmask;
	dst->num_dests = src->num_dests;
	ip_vs_copy_stats(&dst->stats, &src->stats);
}

static inline int
__ip_vs_get_service_entries(const struct ip_vs_get_services *get,
			    struct ip_vs_get_services __user *uptr)
{
	int idx, count=0;
	struct ip_vs_service *svc;
	struct ip_vs_service_entry entry;
	int ret = 0;

	for (idx = 0; idx < IP_VS_SVC_TAB_SIZE; idx++) {
		list_for_each_entry(svc, &ip_vs_svc_table[idx], s_list) {
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
		list_for_each_entry(svc, &ip_vs_svc_fwm_table[idx], f_list) {
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
__ip_vs_get_dest_entries(const struct ip_vs_get_dests *get,
			 struct ip_vs_get_dests __user *uptr)
{
	struct ip_vs_service *svc;
	int ret = 0;

	if (get->fwmark)
		svc = __ip_vs_svc_fwm_get(get->fwmark);
	else
		svc = __ip_vs_service_get(get->protocol,
					  get->addr, get->port);
	if (svc) {
		int count = 0;
		struct ip_vs_dest *dest;
		struct ip_vs_dest_entry entry;

		list_for_each_entry(dest, &svc->destinations, n_list) {
			if (count >= get->num_dests)
				break;

			entry.addr = dest->addr;
			entry.port = dest->port;
			entry.conn_flags = atomic_read(&dest->conn_flags);
			entry.weight = atomic_read(&dest->weight);
			entry.u_threshold = dest->u_threshold;
			entry.l_threshold = dest->l_threshold;
			entry.activeconns = atomic_read(&dest->activeconns);
			entry.inactconns = atomic_read(&dest->inactconns);
			entry.persistconns = atomic_read(&dest->persistconns);
			ip_vs_copy_stats(&entry.stats, &dest->stats);
			if (copy_to_user(&uptr->entrytable[count],
					 &entry, sizeof(entry))) {
				ret = -EFAULT;
				break;
			}
			count++;
		}
		ip_vs_service_put(svc);
	} else
		ret = -ESRCH;
	return ret;
}

static inline void
__ip_vs_get_timeouts(struct ip_vs_timeout_user *u)
{
#ifdef CONFIG_IP_VS_PROTO_TCP
	u->tcp_timeout =
		ip_vs_protocol_tcp.timeout_table[IP_VS_TCP_S_ESTABLISHED] / HZ;
	u->tcp_fin_timeout =
		ip_vs_protocol_tcp.timeout_table[IP_VS_TCP_S_FIN_WAIT] / HZ;
#endif
#ifdef CONFIG_IP_VS_PROTO_UDP
	u->udp_timeout =
		ip_vs_protocol_udp.timeout_table[IP_VS_UDP_S_NORMAL] / HZ;
#endif
}


#define GET_CMDID(cmd)		(cmd - IP_VS_BASE_CTL)
#define GET_INFO_ARG_LEN	(sizeof(struct ip_vs_getinfo))
#define GET_SERVICES_ARG_LEN	(sizeof(struct ip_vs_get_services))
#define GET_SERVICE_ARG_LEN	(sizeof(struct ip_vs_service_entry))
#define GET_DESTS_ARG_LEN	(sizeof(struct ip_vs_get_dests))
#define GET_TIMEOUT_ARG_LEN	(sizeof(struct ip_vs_timeout_user))
#define GET_DAEMON_ARG_LEN	(sizeof(struct ip_vs_daemon_user) * 2)

static const unsigned char get_arglen[GET_CMDID(IP_VS_SO_GET_MAX)+1] = {
	[GET_CMDID(IP_VS_SO_GET_VERSION)]	= 64,
	[GET_CMDID(IP_VS_SO_GET_INFO)]		= GET_INFO_ARG_LEN,
	[GET_CMDID(IP_VS_SO_GET_SERVICES)]	= GET_SERVICES_ARG_LEN,
	[GET_CMDID(IP_VS_SO_GET_SERVICE)]	= GET_SERVICE_ARG_LEN,
	[GET_CMDID(IP_VS_SO_GET_DESTS)]		= GET_DESTS_ARG_LEN,
	[GET_CMDID(IP_VS_SO_GET_TIMEOUT)]	= GET_TIMEOUT_ARG_LEN,
	[GET_CMDID(IP_VS_SO_GET_DAEMON)]	= GET_DAEMON_ARG_LEN,
};

static int
do_ip_vs_get_ctl(struct sock *sk, int cmd, void __user *user, int *len)
{
	unsigned char arg[128];
	int ret = 0;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (*len < get_arglen[GET_CMDID(cmd)]) {
		IP_VS_ERR("get_ctl: len %u < %u\n",
			  *len, get_arglen[GET_CMDID(cmd)]);
		return -EINVAL;
	}

	if (copy_from_user(arg, user, get_arglen[GET_CMDID(cmd)]) != 0)
		return -EFAULT;

	if (mutex_lock_interruptible(&__ip_vs_mutex))
		return -ERESTARTSYS;

	switch (cmd) {
	case IP_VS_SO_GET_VERSION:
	{
		char buf[64];

		sprintf(buf, "IP Virtual Server version %d.%d.%d (size=%d)",
			NVERSION(IP_VS_VERSION_CODE), IP_VS_CONN_TAB_SIZE);
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
		info.size = IP_VS_CONN_TAB_SIZE;
		info.num_services = ip_vs_num_services;
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
			IP_VS_ERR("length: %u != %u\n", *len, size);
			ret = -EINVAL;
			goto out;
		}
		ret = __ip_vs_get_service_entries(get, user);
	}
	break;

	case IP_VS_SO_GET_SERVICE:
	{
		struct ip_vs_service_entry *entry;
		struct ip_vs_service *svc;

		entry = (struct ip_vs_service_entry *)arg;
		if (entry->fwmark)
			svc = __ip_vs_svc_fwm_get(entry->fwmark);
		else
			svc = __ip_vs_service_get(entry->protocol,
						  entry->addr, entry->port);
		if (svc) {
			ip_vs_copy_service(entry, svc);
			if (copy_to_user(user, entry, sizeof(*entry)) != 0)
				ret = -EFAULT;
			ip_vs_service_put(svc);
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
			IP_VS_ERR("length: %u != %u\n", *len, size);
			ret = -EINVAL;
			goto out;
		}
		ret = __ip_vs_get_dest_entries(get, user);
	}
	break;

	case IP_VS_SO_GET_TIMEOUT:
	{
		struct ip_vs_timeout_user t;

		__ip_vs_get_timeouts(&t);
		if (copy_to_user(user, &t, sizeof(t)) != 0)
			ret = -EFAULT;
	}
	break;

	case IP_VS_SO_GET_DAEMON:
	{
		struct ip_vs_daemon_user d[2];

		memset(&d, 0, sizeof(d));
		if (ip_vs_sync_state & IP_VS_STATE_MASTER) {
			d[0].state = IP_VS_STATE_MASTER;
			strlcpy(d[0].mcast_ifn, ip_vs_master_mcast_ifn, sizeof(d[0].mcast_ifn));
			d[0].syncid = ip_vs_master_syncid;
		}
		if (ip_vs_sync_state & IP_VS_STATE_BACKUP) {
			d[1].state = IP_VS_STATE_BACKUP;
			strlcpy(d[1].mcast_ifn, ip_vs_backup_mcast_ifn, sizeof(d[1].mcast_ifn));
			d[1].syncid = ip_vs_backup_syncid;
		}
		if (copy_to_user(user, &d, sizeof(d)) != 0)
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


int ip_vs_control_init(void)
{
	int ret;
	int idx;

	EnterFunction(2);

	ret = nf_register_sockopt(&ip_vs_sockopts);
	if (ret) {
		IP_VS_ERR("cannot register sockopt.\n");
		return ret;
	}

	proc_net_fops_create(&init_net, "ip_vs", 0, &ip_vs_info_fops);
	proc_net_fops_create(&init_net, "ip_vs_stats",0, &ip_vs_stats_fops);

	sysctl_header = register_sysctl_table(vs_root_table);

	/* Initialize ip_vs_svc_table, ip_vs_svc_fwm_table, ip_vs_rtable */
	for(idx = 0; idx < IP_VS_SVC_TAB_SIZE; idx++)  {
		INIT_LIST_HEAD(&ip_vs_svc_table[idx]);
		INIT_LIST_HEAD(&ip_vs_svc_fwm_table[idx]);
	}
	for(idx = 0; idx < IP_VS_RTAB_SIZE; idx++)  {
		INIT_LIST_HEAD(&ip_vs_rtable[idx]);
	}

	memset(&ip_vs_stats, 0, sizeof(ip_vs_stats));
	spin_lock_init(&ip_vs_stats.lock);
	ip_vs_new_estimator(&ip_vs_stats);

	/* Hook the defense timer */
	schedule_delayed_work(&defense_work, DEFENSE_TIMER_PERIOD);

	LeaveFunction(2);
	return 0;
}


void ip_vs_control_cleanup(void)
{
	EnterFunction(2);
	ip_vs_trash_cleanup();
	cancel_rearming_delayed_work(&defense_work);
	cancel_work_sync(&defense_work.work);
	ip_vs_kill_estimator(&ip_vs_stats);
	unregister_sysctl_table(sysctl_header);
	proc_net_remove(&init_net, "ip_vs_stats");
	proc_net_remove(&init_net, "ip_vs");
	nf_unregister_sockopt(&ip_vs_sockopts);
	LeaveFunction(2);
}
