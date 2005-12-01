/*
 * IPVS         An implementation of the IP virtual server support for the
 *              LINUX operating system.  IPVS is now implemented as a module
 *              over the Netfilter framework. IPVS can be used to build a
 *              high-performance and highly available server based on a
 *              cluster of servers.
 *
 * Version:     $Id: ip_vs_conn.c,v 1.31 2003/04/18 09:03:16 wensong Exp $
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
 * The IPVS code for kernel 2.2 was done by Wensong Zhang and Peter Kese,
 * with changes/fixes from Julian Anastasov, Lars Marowsky-Bree, Horms
 * and others. Many code here is taken from IP MASQ code of kernel 2.2.
 *
 * Changes:
 *
 */

#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/proc_fs.h>		/* for proc_net_* */
#include <linux/seq_file.h>
#include <linux/jhash.h>
#include <linux/random.h>

#include <net/ip_vs.h>


/*
 *  Connection hash table: for input and output packets lookups of IPVS
 */
static struct list_head *ip_vs_conn_tab;

/*  SLAB cache for IPVS connections */
static kmem_cache_t *ip_vs_conn_cachep __read_mostly;

/*  counter for current IPVS connections */
static atomic_t ip_vs_conn_count = ATOMIC_INIT(0);

/*  counter for no client port connections */
static atomic_t ip_vs_conn_no_cport_cnt = ATOMIC_INIT(0);

/* random value for IPVS connection hash */
static unsigned int ip_vs_conn_rnd;

/*
 *  Fine locking granularity for big connection hash table
 */
#define CT_LOCKARRAY_BITS  4
#define CT_LOCKARRAY_SIZE  (1<<CT_LOCKARRAY_BITS)
#define CT_LOCKARRAY_MASK  (CT_LOCKARRAY_SIZE-1)

struct ip_vs_aligned_lock
{
	rwlock_t	l;
} __attribute__((__aligned__(SMP_CACHE_BYTES)));

/* lock array for conn table */
static struct ip_vs_aligned_lock
__ip_vs_conntbl_lock_array[CT_LOCKARRAY_SIZE] __cacheline_aligned;

static inline void ct_read_lock(unsigned key)
{
	read_lock(&__ip_vs_conntbl_lock_array[key&CT_LOCKARRAY_MASK].l);
}

static inline void ct_read_unlock(unsigned key)
{
	read_unlock(&__ip_vs_conntbl_lock_array[key&CT_LOCKARRAY_MASK].l);
}

static inline void ct_write_lock(unsigned key)
{
	write_lock(&__ip_vs_conntbl_lock_array[key&CT_LOCKARRAY_MASK].l);
}

static inline void ct_write_unlock(unsigned key)
{
	write_unlock(&__ip_vs_conntbl_lock_array[key&CT_LOCKARRAY_MASK].l);
}

static inline void ct_read_lock_bh(unsigned key)
{
	read_lock_bh(&__ip_vs_conntbl_lock_array[key&CT_LOCKARRAY_MASK].l);
}

static inline void ct_read_unlock_bh(unsigned key)
{
	read_unlock_bh(&__ip_vs_conntbl_lock_array[key&CT_LOCKARRAY_MASK].l);
}

static inline void ct_write_lock_bh(unsigned key)
{
	write_lock_bh(&__ip_vs_conntbl_lock_array[key&CT_LOCKARRAY_MASK].l);
}

static inline void ct_write_unlock_bh(unsigned key)
{
	write_unlock_bh(&__ip_vs_conntbl_lock_array[key&CT_LOCKARRAY_MASK].l);
}


/*
 *	Returns hash value for IPVS connection entry
 */
static unsigned int ip_vs_conn_hashkey(unsigned proto, __u32 addr, __u16 port)
{
	return jhash_3words(addr, port, proto, ip_vs_conn_rnd)
		& IP_VS_CONN_TAB_MASK;
}


/*
 *	Hashes ip_vs_conn in ip_vs_conn_tab by proto,addr,port.
 *	returns bool success.
 */
static inline int ip_vs_conn_hash(struct ip_vs_conn *cp)
{
	unsigned hash;
	int ret;

	/* Hash by protocol, client address and port */
	hash = ip_vs_conn_hashkey(cp->protocol, cp->caddr, cp->cport);

	ct_write_lock(hash);

	if (!(cp->flags & IP_VS_CONN_F_HASHED)) {
		list_add(&cp->c_list, &ip_vs_conn_tab[hash]);
		cp->flags |= IP_VS_CONN_F_HASHED;
		atomic_inc(&cp->refcnt);
		ret = 1;
	} else {
		IP_VS_ERR("ip_vs_conn_hash(): request for already hashed, "
			  "called from %p\n", __builtin_return_address(0));
		ret = 0;
	}

	ct_write_unlock(hash);

	return ret;
}


/*
 *	UNhashes ip_vs_conn from ip_vs_conn_tab.
 *	returns bool success.
 */
static inline int ip_vs_conn_unhash(struct ip_vs_conn *cp)
{
	unsigned hash;
	int ret;

	/* unhash it and decrease its reference counter */
	hash = ip_vs_conn_hashkey(cp->protocol, cp->caddr, cp->cport);

	ct_write_lock(hash);

	if (cp->flags & IP_VS_CONN_F_HASHED) {
		list_del(&cp->c_list);
		cp->flags &= ~IP_VS_CONN_F_HASHED;
		atomic_dec(&cp->refcnt);
		ret = 1;
	} else
		ret = 0;

	ct_write_unlock(hash);

	return ret;
}


/*
 *  Gets ip_vs_conn associated with supplied parameters in the ip_vs_conn_tab.
 *  Called for pkts coming from OUTside-to-INside.
 *	s_addr, s_port: pkt source address (foreign host)
 *	d_addr, d_port: pkt dest address (load balancer)
 */
static inline struct ip_vs_conn *__ip_vs_conn_in_get
(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port)
{
	unsigned hash;
	struct ip_vs_conn *cp;

	hash = ip_vs_conn_hashkey(protocol, s_addr, s_port);

	ct_read_lock(hash);

	list_for_each_entry(cp, &ip_vs_conn_tab[hash], c_list) {
		if (s_addr==cp->caddr && s_port==cp->cport &&
		    d_port==cp->vport && d_addr==cp->vaddr &&
		    ((!s_port) ^ (!(cp->flags & IP_VS_CONN_F_NO_CPORT))) &&
		    protocol==cp->protocol) {
			/* HIT */
			atomic_inc(&cp->refcnt);
			ct_read_unlock(hash);
			return cp;
		}
	}

	ct_read_unlock(hash);

	return NULL;
}

struct ip_vs_conn *ip_vs_conn_in_get
(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port)
{
	struct ip_vs_conn *cp;

	cp = __ip_vs_conn_in_get(protocol, s_addr, s_port, d_addr, d_port);
	if (!cp && atomic_read(&ip_vs_conn_no_cport_cnt))
		cp = __ip_vs_conn_in_get(protocol, s_addr, 0, d_addr, d_port);

	IP_VS_DBG(7, "lookup/in %s %u.%u.%u.%u:%d->%u.%u.%u.%u:%d %s\n",
		  ip_vs_proto_name(protocol),
		  NIPQUAD(s_addr), ntohs(s_port),
		  NIPQUAD(d_addr), ntohs(d_port),
		  cp?"hit":"not hit");

	return cp;
}

/* Get reference to connection template */
struct ip_vs_conn *ip_vs_ct_in_get
(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port)
{
	unsigned hash;
	struct ip_vs_conn *cp;

	hash = ip_vs_conn_hashkey(protocol, s_addr, s_port);

	ct_read_lock(hash);

	list_for_each_entry(cp, &ip_vs_conn_tab[hash], c_list) {
		if (s_addr==cp->caddr && s_port==cp->cport &&
		    d_port==cp->vport && d_addr==cp->vaddr &&
		    cp->flags & IP_VS_CONN_F_TEMPLATE &&
		    protocol==cp->protocol) {
			/* HIT */
			atomic_inc(&cp->refcnt);
			goto out;
		}
	}
	cp = NULL;

  out:
	ct_read_unlock(hash);

	IP_VS_DBG(7, "template lookup/in %s %u.%u.%u.%u:%d->%u.%u.%u.%u:%d %s\n",
		  ip_vs_proto_name(protocol),
		  NIPQUAD(s_addr), ntohs(s_port),
		  NIPQUAD(d_addr), ntohs(d_port),
		  cp?"hit":"not hit");

	return cp;
}

/*
 *  Gets ip_vs_conn associated with supplied parameters in the ip_vs_conn_tab.
 *  Called for pkts coming from inside-to-OUTside.
 *	s_addr, s_port: pkt source address (inside host)
 *	d_addr, d_port: pkt dest address (foreign host)
 */
struct ip_vs_conn *ip_vs_conn_out_get
(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port)
{
	unsigned hash;
	struct ip_vs_conn *cp, *ret=NULL;

	/*
	 *	Check for "full" addressed entries
	 */
	hash = ip_vs_conn_hashkey(protocol, d_addr, d_port);

	ct_read_lock(hash);

	list_for_each_entry(cp, &ip_vs_conn_tab[hash], c_list) {
		if (d_addr == cp->caddr && d_port == cp->cport &&
		    s_port == cp->dport && s_addr == cp->daddr &&
		    protocol == cp->protocol) {
			/* HIT */
			atomic_inc(&cp->refcnt);
			ret = cp;
			break;
		}
	}

	ct_read_unlock(hash);

	IP_VS_DBG(7, "lookup/out %s %u.%u.%u.%u:%d->%u.%u.%u.%u:%d %s\n",
		  ip_vs_proto_name(protocol),
		  NIPQUAD(s_addr), ntohs(s_port),
		  NIPQUAD(d_addr), ntohs(d_port),
		  ret?"hit":"not hit");

	return ret;
}


/*
 *      Put back the conn and restart its timer with its timeout
 */
void ip_vs_conn_put(struct ip_vs_conn *cp)
{
	/* reset it expire in its timeout */
	mod_timer(&cp->timer, jiffies+cp->timeout);

	__ip_vs_conn_put(cp);
}


/*
 *	Fill a no_client_port connection with a client port number
 */
void ip_vs_conn_fill_cport(struct ip_vs_conn *cp, __u16 cport)
{
	if (ip_vs_conn_unhash(cp)) {
		spin_lock(&cp->lock);
		if (cp->flags & IP_VS_CONN_F_NO_CPORT) {
			atomic_dec(&ip_vs_conn_no_cport_cnt);
			cp->flags &= ~IP_VS_CONN_F_NO_CPORT;
			cp->cport = cport;
		}
		spin_unlock(&cp->lock);

		/* hash on new dport */
		ip_vs_conn_hash(cp);
	}
}


/*
 *	Bind a connection entry with the corresponding packet_xmit.
 *	Called by ip_vs_conn_new.
 */
static inline void ip_vs_bind_xmit(struct ip_vs_conn *cp)
{
	switch (IP_VS_FWD_METHOD(cp)) {
	case IP_VS_CONN_F_MASQ:
		cp->packet_xmit = ip_vs_nat_xmit;
		break;

	case IP_VS_CONN_F_TUNNEL:
		cp->packet_xmit = ip_vs_tunnel_xmit;
		break;

	case IP_VS_CONN_F_DROUTE:
		cp->packet_xmit = ip_vs_dr_xmit;
		break;

	case IP_VS_CONN_F_LOCALNODE:
		cp->packet_xmit = ip_vs_null_xmit;
		break;

	case IP_VS_CONN_F_BYPASS:
		cp->packet_xmit = ip_vs_bypass_xmit;
		break;
	}
}


static inline int ip_vs_dest_totalconns(struct ip_vs_dest *dest)
{
	return atomic_read(&dest->activeconns)
		+ atomic_read(&dest->inactconns);
}

/*
 *	Bind a connection entry with a virtual service destination
 *	Called just after a new connection entry is created.
 */
static inline void
ip_vs_bind_dest(struct ip_vs_conn *cp, struct ip_vs_dest *dest)
{
	/* if dest is NULL, then return directly */
	if (!dest)
		return;

	/* Increase the refcnt counter of the dest */
	atomic_inc(&dest->refcnt);

	/* Bind with the destination and its corresponding transmitter */
	cp->flags |= atomic_read(&dest->conn_flags);
	cp->dest = dest;

	IP_VS_DBG(9, "Bind-dest %s c:%u.%u.%u.%u:%d v:%u.%u.%u.%u:%d "
		  "d:%u.%u.%u.%u:%d fwd:%c s:%u flg:%X cnt:%d destcnt:%d\n",
		  ip_vs_proto_name(cp->protocol),
		  NIPQUAD(cp->caddr), ntohs(cp->cport),
		  NIPQUAD(cp->vaddr), ntohs(cp->vport),
		  NIPQUAD(cp->daddr), ntohs(cp->dport),
		  ip_vs_fwd_tag(cp), cp->state,
		  cp->flags, atomic_read(&cp->refcnt),
		  atomic_read(&dest->refcnt));

	/* Update the connection counters */
	if (!(cp->flags & IP_VS_CONN_F_TEMPLATE)) {
		/* It is a normal connection, so increase the inactive
		   connection counter because it is in TCP SYNRECV
		   state (inactive) or other protocol inacive state */
		atomic_inc(&dest->inactconns);
	} else {
		/* It is a persistent connection/template, so increase
		   the peristent connection counter */
		atomic_inc(&dest->persistconns);
	}

	if (dest->u_threshold != 0 &&
	    ip_vs_dest_totalconns(dest) >= dest->u_threshold)
		dest->flags |= IP_VS_DEST_F_OVERLOAD;
}


/*
 *	Unbind a connection entry with its VS destination
 *	Called by the ip_vs_conn_expire function.
 */
static inline void ip_vs_unbind_dest(struct ip_vs_conn *cp)
{
	struct ip_vs_dest *dest = cp->dest;

	if (!dest)
		return;

	IP_VS_DBG(9, "Unbind-dest %s c:%u.%u.%u.%u:%d v:%u.%u.%u.%u:%d "
		  "d:%u.%u.%u.%u:%d fwd:%c s:%u flg:%X cnt:%d destcnt:%d\n",
		  ip_vs_proto_name(cp->protocol),
		  NIPQUAD(cp->caddr), ntohs(cp->cport),
		  NIPQUAD(cp->vaddr), ntohs(cp->vport),
		  NIPQUAD(cp->daddr), ntohs(cp->dport),
		  ip_vs_fwd_tag(cp), cp->state,
		  cp->flags, atomic_read(&cp->refcnt),
		  atomic_read(&dest->refcnt));

	/* Update the connection counters */
	if (!(cp->flags & IP_VS_CONN_F_TEMPLATE)) {
		/* It is a normal connection, so decrease the inactconns
		   or activeconns counter */
		if (cp->flags & IP_VS_CONN_F_INACTIVE) {
			atomic_dec(&dest->inactconns);
		} else {
			atomic_dec(&dest->activeconns);
		}
	} else {
		/* It is a persistent connection/template, so decrease
		   the peristent connection counter */
		atomic_dec(&dest->persistconns);
	}

	if (dest->l_threshold != 0) {
		if (ip_vs_dest_totalconns(dest) < dest->l_threshold)
			dest->flags &= ~IP_VS_DEST_F_OVERLOAD;
	} else if (dest->u_threshold != 0) {
		if (ip_vs_dest_totalconns(dest) * 4 < dest->u_threshold * 3)
			dest->flags &= ~IP_VS_DEST_F_OVERLOAD;
	} else {
		if (dest->flags & IP_VS_DEST_F_OVERLOAD)
			dest->flags &= ~IP_VS_DEST_F_OVERLOAD;
	}

	/*
	 * Simply decrease the refcnt of the dest, because the
	 * dest will be either in service's destination list
	 * or in the trash.
	 */
	atomic_dec(&dest->refcnt);
}


/*
 *	Checking if the destination of a connection template is available.
 *	If available, return 1, otherwise invalidate this connection
 *	template and return 0.
 */
int ip_vs_check_template(struct ip_vs_conn *ct)
{
	struct ip_vs_dest *dest = ct->dest;

	/*
	 * Checking the dest server status.
	 */
	if ((dest == NULL) ||
	    !(dest->flags & IP_VS_DEST_F_AVAILABLE) || 
	    (sysctl_ip_vs_expire_quiescent_template && 
	     (atomic_read(&dest->weight) == 0))) {
		IP_VS_DBG(9, "check_template: dest not available for "
			  "protocol %s s:%u.%u.%u.%u:%d v:%u.%u.%u.%u:%d "
			  "-> d:%u.%u.%u.%u:%d\n",
			  ip_vs_proto_name(ct->protocol),
			  NIPQUAD(ct->caddr), ntohs(ct->cport),
			  NIPQUAD(ct->vaddr), ntohs(ct->vport),
			  NIPQUAD(ct->daddr), ntohs(ct->dport));

		/*
		 * Invalidate the connection template
		 */
		if (ct->vport != 65535) {
			if (ip_vs_conn_unhash(ct)) {
				ct->dport = 65535;
				ct->vport = 65535;
				ct->cport = 0;
				ip_vs_conn_hash(ct);
			}
		}

		/*
		 * Simply decrease the refcnt of the template,
		 * don't restart its timer.
		 */
		atomic_dec(&ct->refcnt);
		return 0;
	}
	return 1;
}

static void ip_vs_conn_expire(unsigned long data)
{
	struct ip_vs_conn *cp = (struct ip_vs_conn *)data;

	cp->timeout = 60*HZ;

	/*
	 *	hey, I'm using it
	 */
	atomic_inc(&cp->refcnt);

	/*
	 *	do I control anybody?
	 */
	if (atomic_read(&cp->n_control))
		goto expire_later;

	/*
	 *	unhash it if it is hashed in the conn table
	 */
	if (!ip_vs_conn_unhash(cp))
		goto expire_later;

	/*
	 *	refcnt==1 implies I'm the only one referrer
	 */
	if (likely(atomic_read(&cp->refcnt) == 1)) {
		/* delete the timer if it is activated by other users */
		if (timer_pending(&cp->timer))
			del_timer(&cp->timer);

		/* does anybody control me? */
		if (cp->control)
			ip_vs_control_del(cp);

		if (unlikely(cp->app != NULL))
			ip_vs_unbind_app(cp);
		ip_vs_unbind_dest(cp);
		if (cp->flags & IP_VS_CONN_F_NO_CPORT)
			atomic_dec(&ip_vs_conn_no_cport_cnt);
		atomic_dec(&ip_vs_conn_count);

		kmem_cache_free(ip_vs_conn_cachep, cp);
		return;
	}

	/* hash it back to the table */
	ip_vs_conn_hash(cp);

  expire_later:
	IP_VS_DBG(7, "delayed: refcnt-1=%d conn.n_control=%d\n",
		  atomic_read(&cp->refcnt)-1,
		  atomic_read(&cp->n_control));

	ip_vs_conn_put(cp);
}


void ip_vs_conn_expire_now(struct ip_vs_conn *cp)
{
	if (del_timer(&cp->timer))
		mod_timer(&cp->timer, jiffies);
}


/*
 *	Create a new connection entry and hash it into the ip_vs_conn_tab
 */
struct ip_vs_conn *
ip_vs_conn_new(int proto, __u32 caddr, __u16 cport, __u32 vaddr, __u16 vport,
	       __u32 daddr, __u16 dport, unsigned flags,
	       struct ip_vs_dest *dest)
{
	struct ip_vs_conn *cp;
	struct ip_vs_protocol *pp = ip_vs_proto_get(proto);

	cp = kmem_cache_alloc(ip_vs_conn_cachep, GFP_ATOMIC);
	if (cp == NULL) {
		IP_VS_ERR_RL("ip_vs_conn_new: no memory available.\n");
		return NULL;
	}

	memset(cp, 0, sizeof(*cp));
	INIT_LIST_HEAD(&cp->c_list);
	init_timer(&cp->timer);
	cp->timer.data     = (unsigned long)cp;
	cp->timer.function = ip_vs_conn_expire;
	cp->protocol	   = proto;
	cp->caddr	   = caddr;
	cp->cport	   = cport;
	cp->vaddr	   = vaddr;
	cp->vport	   = vport;
	cp->daddr          = daddr;
	cp->dport          = dport;
	cp->flags	   = flags;
	spin_lock_init(&cp->lock);

	/*
	 * Set the entry is referenced by the current thread before hashing
	 * it in the table, so that other thread run ip_vs_random_dropentry
	 * but cannot drop this entry.
	 */
	atomic_set(&cp->refcnt, 1);

	atomic_set(&cp->n_control, 0);
	atomic_set(&cp->in_pkts, 0);

	atomic_inc(&ip_vs_conn_count);
	if (flags & IP_VS_CONN_F_NO_CPORT)
		atomic_inc(&ip_vs_conn_no_cport_cnt);

	/* Bind the connection with a destination server */
	ip_vs_bind_dest(cp, dest);

	/* Set its state and timeout */
	cp->state = 0;
	cp->timeout = 3*HZ;

	/* Bind its packet transmitter */
	ip_vs_bind_xmit(cp);

	if (unlikely(pp && atomic_read(&pp->appcnt)))
		ip_vs_bind_app(cp, pp);

	/* Hash it in the ip_vs_conn_tab finally */
	ip_vs_conn_hash(cp);

	return cp;
}


/*
 *	/proc/net/ip_vs_conn entries
 */
#ifdef CONFIG_PROC_FS

static void *ip_vs_conn_array(struct seq_file *seq, loff_t pos)
{
	int idx;
	struct ip_vs_conn *cp;
	
	for(idx = 0; idx < IP_VS_CONN_TAB_SIZE; idx++) {
		ct_read_lock_bh(idx);
		list_for_each_entry(cp, &ip_vs_conn_tab[idx], c_list) {
			if (pos-- == 0) {
				seq->private = &ip_vs_conn_tab[idx];
				return cp;
			}
		}
		ct_read_unlock_bh(idx);
	}

	return NULL;
}

static void *ip_vs_conn_seq_start(struct seq_file *seq, loff_t *pos)
{
	seq->private = NULL;
	return *pos ? ip_vs_conn_array(seq, *pos - 1) :SEQ_START_TOKEN;
}

static void *ip_vs_conn_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct ip_vs_conn *cp = v;
	struct list_head *e, *l = seq->private;
	int idx;

	++*pos;
	if (v == SEQ_START_TOKEN) 
		return ip_vs_conn_array(seq, 0);

	/* more on same hash chain? */
	if ((e = cp->c_list.next) != l)
		return list_entry(e, struct ip_vs_conn, c_list);

	idx = l - ip_vs_conn_tab;
	ct_read_unlock_bh(idx);

	while (++idx < IP_VS_CONN_TAB_SIZE) {
		ct_read_lock_bh(idx);
		list_for_each_entry(cp, &ip_vs_conn_tab[idx], c_list) {
			seq->private = &ip_vs_conn_tab[idx];
			return cp;
		}	
		ct_read_unlock_bh(idx);
	}
	seq->private = NULL;
	return NULL;
}

static void ip_vs_conn_seq_stop(struct seq_file *seq, void *v)
{
	struct list_head *l = seq->private;

	if (l)
		ct_read_unlock_bh(l - ip_vs_conn_tab);
}

static int ip_vs_conn_seq_show(struct seq_file *seq, void *v)
{

	if (v == SEQ_START_TOKEN)
		seq_puts(seq,
   "Pro FromIP   FPrt ToIP     TPrt DestIP   DPrt State       Expires\n");
	else {
		const struct ip_vs_conn *cp = v;

		seq_printf(seq,
			"%-3s %08X %04X %08X %04X %08X %04X %-11s %7lu\n",
				ip_vs_proto_name(cp->protocol),
				ntohl(cp->caddr), ntohs(cp->cport),
				ntohl(cp->vaddr), ntohs(cp->vport),
				ntohl(cp->daddr), ntohs(cp->dport),
				ip_vs_state_name(cp->protocol, cp->state),
				(cp->timer.expires-jiffies)/HZ);
	}
	return 0;
}

static struct seq_operations ip_vs_conn_seq_ops = {
	.start = ip_vs_conn_seq_start,
	.next  = ip_vs_conn_seq_next,
	.stop  = ip_vs_conn_seq_stop,
	.show  = ip_vs_conn_seq_show,
};

static int ip_vs_conn_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &ip_vs_conn_seq_ops);
}

static struct file_operations ip_vs_conn_fops = {
	.owner	 = THIS_MODULE,
	.open    = ip_vs_conn_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};
#endif


/*
 *      Randomly drop connection entries before running out of memory
 */
static inline int todrop_entry(struct ip_vs_conn *cp)
{
	/*
	 * The drop rate array needs tuning for real environments.
	 * Called from timer bh only => no locking
	 */
	static const char todrop_rate[9] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
	static char todrop_counter[9] = {0};
	int i;

	/* if the conn entry hasn't lasted for 60 seconds, don't drop it.
	   This will leave enough time for normal connection to get
	   through. */
	if (time_before(cp->timeout + jiffies, cp->timer.expires + 60*HZ))
		return 0;

	/* Don't drop the entry if its number of incoming packets is not
	   located in [0, 8] */
	i = atomic_read(&cp->in_pkts);
	if (i > 8 || i < 0) return 0;

	if (!todrop_rate[i]) return 0;
	if (--todrop_counter[i] > 0) return 0;

	todrop_counter[i] = todrop_rate[i];
	return 1;
}

/* Called from keventd and must protect itself from softirqs */
void ip_vs_random_dropentry(void)
{
	int idx;
	struct ip_vs_conn *cp;

	/*
	 * Randomly scan 1/32 of the whole table every second
	 */
	for (idx = 0; idx < (IP_VS_CONN_TAB_SIZE>>5); idx++) {
		unsigned hash = net_random() & IP_VS_CONN_TAB_MASK;

		/*
		 *  Lock is actually needed in this loop.
		 */
		ct_write_lock_bh(hash);

		list_for_each_entry(cp, &ip_vs_conn_tab[hash], c_list) {
			if (cp->flags & IP_VS_CONN_F_TEMPLATE)
				/* connection template */
				continue;

			if (cp->protocol == IPPROTO_TCP) {
				switch(cp->state) {
				case IP_VS_TCP_S_SYN_RECV:
				case IP_VS_TCP_S_SYNACK:
					break;

				case IP_VS_TCP_S_ESTABLISHED:
					if (todrop_entry(cp))
						break;
					continue;

				default:
					continue;
				}
			} else {
				if (!todrop_entry(cp))
					continue;
			}

			IP_VS_DBG(4, "del connection\n");
			ip_vs_conn_expire_now(cp);
			if (cp->control) {
				IP_VS_DBG(4, "del conn template\n");
				ip_vs_conn_expire_now(cp->control);
			}
		}
		ct_write_unlock_bh(hash);
	}
}


/*
 *      Flush all the connection entries in the ip_vs_conn_tab
 */
static void ip_vs_conn_flush(void)
{
	int idx;
	struct ip_vs_conn *cp;

  flush_again:
	for (idx=0; idx<IP_VS_CONN_TAB_SIZE; idx++) {
		/*
		 *  Lock is actually needed in this loop.
		 */
		ct_write_lock_bh(idx);

		list_for_each_entry(cp, &ip_vs_conn_tab[idx], c_list) {

			IP_VS_DBG(4, "del connection\n");
			ip_vs_conn_expire_now(cp);
			if (cp->control) {
				IP_VS_DBG(4, "del conn template\n");
				ip_vs_conn_expire_now(cp->control);
			}
		}
		ct_write_unlock_bh(idx);
	}

	/* the counter may be not NULL, because maybe some conn entries
	   are run by slow timer handler or unhashed but still referred */
	if (atomic_read(&ip_vs_conn_count) != 0) {
		schedule();
		goto flush_again;
	}
}


int ip_vs_conn_init(void)
{
	int idx;

	/*
	 * Allocate the connection hash table and initialize its list heads
	 */
	ip_vs_conn_tab = vmalloc(IP_VS_CONN_TAB_SIZE*sizeof(struct list_head));
	if (!ip_vs_conn_tab)
		return -ENOMEM;

	/* Allocate ip_vs_conn slab cache */
	ip_vs_conn_cachep = kmem_cache_create("ip_vs_conn",
					      sizeof(struct ip_vs_conn), 0,
					      SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (!ip_vs_conn_cachep) {
		vfree(ip_vs_conn_tab);
		return -ENOMEM;
	}

	IP_VS_INFO("Connection hash table configured "
		   "(size=%d, memory=%ldKbytes)\n",
		   IP_VS_CONN_TAB_SIZE,
		   (long)(IP_VS_CONN_TAB_SIZE*sizeof(struct list_head))/1024);
	IP_VS_DBG(0, "Each connection entry needs %Zd bytes at least\n",
		  sizeof(struct ip_vs_conn));

	for (idx = 0; idx < IP_VS_CONN_TAB_SIZE; idx++) {
		INIT_LIST_HEAD(&ip_vs_conn_tab[idx]);
	}

	for (idx = 0; idx < CT_LOCKARRAY_SIZE; idx++)  {
		rwlock_init(&__ip_vs_conntbl_lock_array[idx].l);
	}

	proc_net_fops_create("ip_vs_conn", 0, &ip_vs_conn_fops);

	/* calculate the random value for connection hash */
	get_random_bytes(&ip_vs_conn_rnd, sizeof(ip_vs_conn_rnd));

	return 0;
}


void ip_vs_conn_cleanup(void)
{
	/* flush all the connection entries first */
	ip_vs_conn_flush();

	/* Release the empty cache */
	kmem_cache_destroy(ip_vs_conn_cachep);
	proc_net_remove("ip_vs_conn");
	vfree(ip_vs_conn_tab);
}
