/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		ROUTE - implementation of the IP router.
 *
 * Authors:	Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *		Linus Torvalds, <Linus.Torvalds@helsinki.fi>
 *		Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 * Fixes:
 *		Alan Cox	:	Verify area fixes.
 *		Alan Cox	:	cli() protects routing changes
 *		Rui Oliveira	:	ICMP routing table updates
 *		(rco@di.uminho.pt)	Routing table insertion and update
 *		Linus Torvalds	:	Rewrote bits to be sensible
 *		Alan Cox	:	Added BSD route gw semantics
 *		Alan Cox	:	Super /proc >4K
 *		Alan Cox	:	MTU in route table
 *		Alan Cox	: 	MSS actually. Also added the window
 *					clamper.
 *		Sam Lantinga	:	Fixed route matching in rt_del()
 *		Alan Cox	:	Routing cache support.
 *		Alan Cox	:	Removed compatibility cruft.
 *		Alan Cox	:	RTF_REJECT support.
 *		Alan Cox	:	TCP irtt support.
 *		Jonathan Naylor	:	Added Metric support.
 *	Miquel van Smoorenburg	:	BSD API fixes.
 *	Miquel van Smoorenburg	:	Metrics.
 *		Alan Cox	:	Use __u32 properly
 *		Alan Cox	:	Aligned routing errors more closely with BSD
 *					our system is still very different.
 *		Alan Cox	:	Faster /proc handling
 *	Alexey Kuznetsov	:	Massive rework to support tree based routing,
 *					routing caches and better behaviour.
 *
 *		Olaf Erb	:	irtt wasn't being copied right.
 *		Bjorn Ekwall	:	Kerneld route support.
 *		Alan Cox	:	Multicast fixed (I hope)
 * 		Pavel Krauz	:	Limited broadcast fixed
 *		Mike McLagan	:	Routing by source
 *	Alexey Kuznetsov	:	End of old history. Split to fib.c and
 *					route.c and rewritten from scratch.
 *		Andi Kleen	:	Load-limit warning messages.
 *	Vitaly E. Lavrov	:	Transparent proxy revived after year coma.
 *	Vitaly E. Lavrov	:	Race condition in ip_route_input_slow.
 *	Tobias Ringstrom	:	Uninitialized res.type in ip_route_output_slow.
 *	Vladimir V. Ivanov	:	IP rule info (flowid) is really useful.
 *		Marc Boucher	:	routing by fwmark
 *	Robert Olsson		:	Added rt_cache statistics
 *	Arnaldo C. Melo		:	Convert proc stuff to seq_file
 *	Eric Dumazet		:	hashed spinlocks and rt_check_expire() fixes.
 * 	Ilia Sotnikov		:	Ignore TOS on PMTUD and Redirect
 * 	Ilia Sotnikov		:	Removed TOS from hash calculations
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) "IPv4: " fmt

#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/bitops.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/inetdevice.h>
#include <linux/igmp.h>
#include <linux/pkt_sched.h>
#include <linux/mroute.h>
#include <linux/netfilter_ipv4.h>
#include <linux/random.h>
#include <linux/rcupdate.h>
#include <linux/times.h>
#include <linux/slab.h>
#include <linux/jhash.h>
#include <net/dst.h>
#include <net/dst_metadata.h>
#include <net/net_namespace.h>
#include <net/protocol.h>
#include <net/ip.h>
#include <net/route.h>
#include <net/inetpeer.h>
#include <net/sock.h>
#include <net/ip_fib.h>
#include <net/arp.h>
#include <net/tcp.h>
#include <net/icmp.h>
#include <net/xfrm.h>
#include <net/lwtunnel.h>
#include <net/netevent.h>
#include <net/rtnetlink.h>
#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif
#include <net/secure_seq.h>
#include <net/ip_tunnels.h>
#include <net/l3mdev.h>

#include "fib_lookup.h"

#define RT_FL_TOS(oldflp4) \
	((oldflp4)->flowi4_tos & (IPTOS_RT_MASK | RTO_ONLINK))

#define RT_GC_TIMEOUT (300*HZ)

static int ip_rt_max_size;
static int ip_rt_redirect_number __read_mostly	= 9;
static int ip_rt_redirect_load __read_mostly	= HZ / 50;
static int ip_rt_redirect_silence __read_mostly	= ((HZ / 50) << (9 + 1));
static int ip_rt_error_cost __read_mostly	= HZ;
static int ip_rt_error_burst __read_mostly	= 5 * HZ;
static int ip_rt_mtu_expires __read_mostly	= 10 * 60 * HZ;
static u32 ip_rt_min_pmtu __read_mostly		= 512 + 20 + 20;
static int ip_rt_min_advmss __read_mostly	= 256;

static int ip_rt_gc_timeout __read_mostly	= RT_GC_TIMEOUT;

/*
 *	Interface to generic destination cache.
 */

static struct dst_entry *ipv4_dst_check(struct dst_entry *dst, u32 cookie);
static unsigned int	 ipv4_default_advmss(const struct dst_entry *dst);
static unsigned int	 ipv4_mtu(const struct dst_entry *dst);
static struct dst_entry *ipv4_negative_advice(struct dst_entry *dst);
static void		 ipv4_link_failure(struct sk_buff *skb);
static void		 ip_rt_update_pmtu(struct dst_entry *dst, struct sock *sk,
					   struct sk_buff *skb, u32 mtu);
static void		 ip_do_redirect(struct dst_entry *dst, struct sock *sk,
					struct sk_buff *skb);
static void		ipv4_dst_destroy(struct dst_entry *dst);

static u32 *ipv4_cow_metrics(struct dst_entry *dst, unsigned long old)
{
	WARN_ON(1);
	return NULL;
}

static struct neighbour *ipv4_neigh_lookup(const struct dst_entry *dst,
					   struct sk_buff *skb,
					   const void *daddr);
static void ipv4_confirm_neigh(const struct dst_entry *dst, const void *daddr);

static struct dst_ops ipv4_dst_ops = {
	.family =		AF_INET,
	.check =		ipv4_dst_check,
	.default_advmss =	ipv4_default_advmss,
	.mtu =			ipv4_mtu,
	.cow_metrics =		ipv4_cow_metrics,
	.destroy =		ipv4_dst_destroy,
	.negative_advice =	ipv4_negative_advice,
	.link_failure =		ipv4_link_failure,
	.update_pmtu =		ip_rt_update_pmtu,
	.redirect =		ip_do_redirect,
	.local_out =		__ip_local_out,
	.neigh_lookup =		ipv4_neigh_lookup,
	.confirm_neigh =	ipv4_confirm_neigh,
};

#define ECN_OR_COST(class)	TC_PRIO_##class

const __u8 ip_tos2prio[16] = {
	TC_PRIO_BESTEFFORT,
	ECN_OR_COST(BESTEFFORT),
	TC_PRIO_BESTEFFORT,
	ECN_OR_COST(BESTEFFORT),
	TC_PRIO_BULK,
	ECN_OR_COST(BULK),
	TC_PRIO_BULK,
	ECN_OR_COST(BULK),
	TC_PRIO_INTERACTIVE,
	ECN_OR_COST(INTERACTIVE),
	TC_PRIO_INTERACTIVE,
	ECN_OR_COST(INTERACTIVE),
	TC_PRIO_INTERACTIVE_BULK,
	ECN_OR_COST(INTERACTIVE_BULK),
	TC_PRIO_INTERACTIVE_BULK,
	ECN_OR_COST(INTERACTIVE_BULK)
};
EXPORT_SYMBOL(ip_tos2prio);

static DEFINE_PER_CPU(struct rt_cache_stat, rt_cache_stat);
#define RT_CACHE_STAT_INC(field) raw_cpu_inc(rt_cache_stat.field)

#ifdef CONFIG_PROC_FS
static void *rt_cache_seq_start(struct seq_file *seq, loff_t *pos)
{
	if (*pos)
		return NULL;
	return SEQ_START_TOKEN;
}

static void *rt_cache_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;
	return NULL;
}

static void rt_cache_seq_stop(struct seq_file *seq, void *v)
{
}

static int rt_cache_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN)
		seq_printf(seq, "%-127s\n",
			   "Iface\tDestination\tGateway \tFlags\t\tRefCnt\tUse\t"
			   "Metric\tSource\t\tMTU\tWindow\tIRTT\tTOS\tHHRef\t"
			   "HHUptod\tSpecDst");
	return 0;
}

static const struct seq_operations rt_cache_seq_ops = {
	.start  = rt_cache_seq_start,
	.next   = rt_cache_seq_next,
	.stop   = rt_cache_seq_stop,
	.show   = rt_cache_seq_show,
};

static int rt_cache_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &rt_cache_seq_ops);
}

static const struct file_operations rt_cache_seq_fops = {
	.open	 = rt_cache_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release,
};


static void *rt_cpu_seq_start(struct seq_file *seq, loff_t *pos)
{
	int cpu;

	if (*pos == 0)
		return SEQ_START_TOKEN;

	for (cpu = *pos-1; cpu < nr_cpu_ids; ++cpu) {
		if (!cpu_possible(cpu))
			continue;
		*pos = cpu+1;
		return &per_cpu(rt_cache_stat, cpu);
	}
	return NULL;
}

static void *rt_cpu_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	int cpu;

	for (cpu = *pos; cpu < nr_cpu_ids; ++cpu) {
		if (!cpu_possible(cpu))
			continue;
		*pos = cpu+1;
		return &per_cpu(rt_cache_stat, cpu);
	}
	return NULL;

}

static void rt_cpu_seq_stop(struct seq_file *seq, void *v)
{

}

static int rt_cpu_seq_show(struct seq_file *seq, void *v)
{
	struct rt_cache_stat *st = v;

	if (v == SEQ_START_TOKEN) {
		seq_printf(seq, "entries  in_hit in_slow_tot in_slow_mc in_no_route in_brd in_martian_dst in_martian_src  out_hit out_slow_tot out_slow_mc  gc_total gc_ignored gc_goal_miss gc_dst_overflow in_hlist_search out_hlist_search\n");
		return 0;
	}

	seq_printf(seq,"%08x  %08x %08x %08x %08x %08x %08x %08x "
		   " %08x %08x %08x %08x %08x %08x %08x %08x %08x \n",
		   dst_entries_get_slow(&ipv4_dst_ops),
		   0, /* st->in_hit */
		   st->in_slow_tot,
		   st->in_slow_mc,
		   st->in_no_route,
		   st->in_brd,
		   st->in_martian_dst,
		   st->in_martian_src,

		   0, /* st->out_hit */
		   st->out_slow_tot,
		   st->out_slow_mc,

		   0, /* st->gc_total */
		   0, /* st->gc_ignored */
		   0, /* st->gc_goal_miss */
		   0, /* st->gc_dst_overflow */
		   0, /* st->in_hlist_search */
		   0  /* st->out_hlist_search */
		);
	return 0;
}

static const struct seq_operations rt_cpu_seq_ops = {
	.start  = rt_cpu_seq_start,
	.next   = rt_cpu_seq_next,
	.stop   = rt_cpu_seq_stop,
	.show   = rt_cpu_seq_show,
};


static int rt_cpu_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &rt_cpu_seq_ops);
}

static const struct file_operations rt_cpu_seq_fops = {
	.open	 = rt_cpu_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release,
};

#ifdef CONFIG_IP_ROUTE_CLASSID
static int rt_acct_proc_show(struct seq_file *m, void *v)
{
	struct ip_rt_acct *dst, *src;
	unsigned int i, j;

	dst = kcalloc(256, sizeof(struct ip_rt_acct), GFP_KERNEL);
	if (!dst)
		return -ENOMEM;

	for_each_possible_cpu(i) {
		src = (struct ip_rt_acct *)per_cpu_ptr(ip_rt_acct, i);
		for (j = 0; j < 256; j++) {
			dst[j].o_bytes   += src[j].o_bytes;
			dst[j].o_packets += src[j].o_packets;
			dst[j].i_bytes   += src[j].i_bytes;
			dst[j].i_packets += src[j].i_packets;
		}
	}

	seq_write(m, dst, 256 * sizeof(struct ip_rt_acct));
	kfree(dst);
	return 0;
}
#endif

static int __net_init ip_rt_do_proc_init(struct net *net)
{
	struct proc_dir_entry *pde;

	pde = proc_create("rt_cache", 0444, net->proc_net,
			  &rt_cache_seq_fops);
	if (!pde)
		goto err1;

	pde = proc_create("rt_cache", 0444,
			  net->proc_net_stat, &rt_cpu_seq_fops);
	if (!pde)
		goto err2;

#ifdef CONFIG_IP_ROUTE_CLASSID
	pde = proc_create_single("rt_acct", 0, net->proc_net,
			rt_acct_proc_show);
	if (!pde)
		goto err3;
#endif
	return 0;

#ifdef CONFIG_IP_ROUTE_CLASSID
err3:
	remove_proc_entry("rt_cache", net->proc_net_stat);
#endif
err2:
	remove_proc_entry("rt_cache", net->proc_net);
err1:
	return -ENOMEM;
}

static void __net_exit ip_rt_do_proc_exit(struct net *net)
{
	remove_proc_entry("rt_cache", net->proc_net_stat);
	remove_proc_entry("rt_cache", net->proc_net);
#ifdef CONFIG_IP_ROUTE_CLASSID
	remove_proc_entry("rt_acct", net->proc_net);
#endif
}

static struct pernet_operations ip_rt_proc_ops __net_initdata =  {
	.init = ip_rt_do_proc_init,
	.exit = ip_rt_do_proc_exit,
};

static int __init ip_rt_proc_init(void)
{
	return register_pernet_subsys(&ip_rt_proc_ops);
}

#else
static inline int ip_rt_proc_init(void)
{
	return 0;
}
#endif /* CONFIG_PROC_FS */

static inline bool rt_is_expired(const struct rtable *rth)
{
	return rth->rt_genid != rt_genid_ipv4(dev_net(rth->dst.dev));
}

void rt_cache_flush(struct net *net)
{
	rt_genid_bump_ipv4(net);
}

static struct neighbour *ipv4_neigh_lookup(const struct dst_entry *dst,
					   struct sk_buff *skb,
					   const void *daddr)
{
	struct net_device *dev = dst->dev;
	const __be32 *pkey = daddr;
	const struct rtable *rt;
	struct neighbour *n;

	rt = (const struct rtable *) dst;
	if (rt->rt_gateway)
		pkey = (const __be32 *) &rt->rt_gateway;
	else if (skb)
		pkey = &ip_hdr(skb)->daddr;

	n = __ipv4_neigh_lookup(dev, *(__force u32 *)pkey);
	if (n)
		return n;
	return neigh_create(&arp_tbl, pkey, dev);
}

static void ipv4_confirm_neigh(const struct dst_entry *dst, const void *daddr)
{
	struct net_device *dev = dst->dev;
	const __be32 *pkey = daddr;
	const struct rtable *rt;

	rt = (const struct rtable *)dst;
	if (rt->rt_gateway)
		pkey = (const __be32 *)&rt->rt_gateway;
	else if (!daddr ||
		 (rt->rt_flags &
		  (RTCF_MULTICAST | RTCF_BROADCAST | RTCF_LOCAL)))
		return;

	__ipv4_confirm_neigh(dev, *(__force u32 *)pkey);
}

#define IP_IDENTS_SZ 2048u

static atomic_t *ip_idents __read_mostly;
static u32 *ip_tstamps __read_mostly;

/* In order to protect privacy, we add a perturbation to identifiers
 * if one generator is seldom used. This makes hard for an attacker
 * to infer how many packets were sent between two points in time.
 */
u32 ip_idents_reserve(u32 hash, int segs)
{
	u32 *p_tstamp = ip_tstamps + hash % IP_IDENTS_SZ;
	atomic_t *p_id = ip_idents + hash % IP_IDENTS_SZ;
	u32 old = READ_ONCE(*p_tstamp);
	u32 now = (u32)jiffies;
	u32 new, delta = 0;

	if (old != now && cmpxchg(p_tstamp, old, now) == old)
		delta = prandom_u32_max(now - old);

	/* Do not use atomic_add_return() as it makes UBSAN unhappy */
	do {
		old = (u32)atomic_read(p_id);
		new = old + delta + segs;
	} while (atomic_cmpxchg(p_id, old, new) != old);

	return new - segs;
}
EXPORT_SYMBOL(ip_idents_reserve);

void __ip_select_ident(struct net *net, struct iphdr *iph, int segs)
{
	static u32 ip_idents_hashrnd __read_mostly;
	u32 hash, id;

	net_get_random_once(&ip_idents_hashrnd, sizeof(ip_idents_hashrnd));

	hash = jhash_3words((__force u32)iph->daddr,
			    (__force u32)iph->saddr,
			    iph->protocol ^ net_hash_mix(net),
			    ip_idents_hashrnd);
	id = ip_idents_reserve(hash, segs);
	iph->id = htons(id);
}
EXPORT_SYMBOL(__ip_select_ident);

static void __build_flow_key(const struct net *net, struct flowi4 *fl4,
			     const struct sock *sk,
			     const struct iphdr *iph,
			     int oif, u8 tos,
			     u8 prot, u32 mark, int flow_flags)
{
	if (sk) {
		const struct inet_sock *inet = inet_sk(sk);

		oif = sk->sk_bound_dev_if;
		mark = sk->sk_mark;
		tos = RT_CONN_FLAGS(sk);
		prot = inet->hdrincl ? IPPROTO_RAW : sk->sk_protocol;
	}
	flowi4_init_output(fl4, oif, mark, tos,
			   RT_SCOPE_UNIVERSE, prot,
			   flow_flags,
			   iph->daddr, iph->saddr, 0, 0,
			   sock_net_uid(net, sk));
}

static void build_skb_flow_key(struct flowi4 *fl4, const struct sk_buff *skb,
			       const struct sock *sk)
{
	const struct net *net = dev_net(skb->dev);
	const struct iphdr *iph = ip_hdr(skb);
	int oif = skb->dev->ifindex;
	u8 tos = RT_TOS(iph->tos);
	u8 prot = iph->protocol;
	u32 mark = skb->mark;

	__build_flow_key(net, fl4, sk, iph, oif, tos, prot, mark, 0);
}

static void build_sk_flow_key(struct flowi4 *fl4, const struct sock *sk)
{
	const struct inet_sock *inet = inet_sk(sk);
	const struct ip_options_rcu *inet_opt;
	__be32 daddr = inet->inet_daddr;

	rcu_read_lock();
	inet_opt = rcu_dereference(inet->inet_opt);
	if (inet_opt && inet_opt->opt.srr)
		daddr = inet_opt->opt.faddr;
	flowi4_init_output(fl4, sk->sk_bound_dev_if, sk->sk_mark,
			   RT_CONN_FLAGS(sk), RT_SCOPE_UNIVERSE,
			   inet->hdrincl ? IPPROTO_RAW : sk->sk_protocol,
			   inet_sk_flowi_flags(sk),
			   daddr, inet->inet_saddr, 0, 0, sk->sk_uid);
	rcu_read_unlock();
}

static void ip_rt_build_flow_key(struct flowi4 *fl4, const struct sock *sk,
				 const struct sk_buff *skb)
{
	if (skb)
		build_skb_flow_key(fl4, skb, sk);
	else
		build_sk_flow_key(fl4, sk);
}

static DEFINE_SPINLOCK(fnhe_lock);

static void fnhe_flush_routes(struct fib_nh_exception *fnhe)
{
	struct rtable *rt;

	rt = rcu_dereference(fnhe->fnhe_rth_input);
	if (rt) {
		RCU_INIT_POINTER(fnhe->fnhe_rth_input, NULL);
		dst_dev_put(&rt->dst);
		dst_release(&rt->dst);
	}
	rt = rcu_dereference(fnhe->fnhe_rth_output);
	if (rt) {
		RCU_INIT_POINTER(fnhe->fnhe_rth_output, NULL);
		dst_dev_put(&rt->dst);
		dst_release(&rt->dst);
	}
}

static struct fib_nh_exception *fnhe_oldest(struct fnhe_hash_bucket *hash)
{
	struct fib_nh_exception *fnhe, *oldest;

	oldest = rcu_dereference(hash->chain);
	for (fnhe = rcu_dereference(oldest->fnhe_next); fnhe;
	     fnhe = rcu_dereference(fnhe->fnhe_next)) {
		if (time_before(fnhe->fnhe_stamp, oldest->fnhe_stamp))
			oldest = fnhe;
	}
	fnhe_flush_routes(oldest);
	return oldest;
}

static inline u32 fnhe_hashfun(__be32 daddr)
{
	static u32 fnhe_hashrnd __read_mostly;
	u32 hval;

	net_get_random_once(&fnhe_hashrnd, sizeof(fnhe_hashrnd));
	hval = jhash_1word((__force u32) daddr, fnhe_hashrnd);
	return hash_32(hval, FNHE_HASH_SHIFT);
}

static void fill_route_from_fnhe(struct rtable *rt, struct fib_nh_exception *fnhe)
{
	rt->rt_pmtu = fnhe->fnhe_pmtu;
	rt->rt_mtu_locked = fnhe->fnhe_mtu_locked;
	rt->dst.expires = fnhe->fnhe_expires;

	if (fnhe->fnhe_gw) {
		rt->rt_flags |= RTCF_REDIRECTED;
		rt->rt_gateway = fnhe->fnhe_gw;
		rt->rt_uses_gateway = 1;
	}
}

static void update_or_create_fnhe(struct fib_nh *nh, __be32 daddr, __be32 gw,
				  u32 pmtu, bool lock, unsigned long expires)
{
	struct fnhe_hash_bucket *hash;
	struct fib_nh_exception *fnhe;
	struct rtable *rt;
	u32 genid, hval;
	unsigned int i;
	int depth;

	genid = fnhe_genid(dev_net(nh->nh_dev));
	hval = fnhe_hashfun(daddr);

	spin_lock_bh(&fnhe_lock);

	hash = rcu_dereference(nh->nh_exceptions);
	if (!hash) {
		hash = kcalloc(FNHE_HASH_SIZE, sizeof(*hash), GFP_ATOMIC);
		if (!hash)
			goto out_unlock;
		rcu_assign_pointer(nh->nh_exceptions, hash);
	}

	hash += hval;

	depth = 0;
	for (fnhe = rcu_dereference(hash->chain); fnhe;
	     fnhe = rcu_dereference(fnhe->fnhe_next)) {
		if (fnhe->fnhe_daddr == daddr)
			break;
		depth++;
	}

	if (fnhe) {
		if (fnhe->fnhe_genid != genid)
			fnhe->fnhe_genid = genid;
		if (gw)
			fnhe->fnhe_gw = gw;
		if (pmtu) {
			fnhe->fnhe_pmtu = pmtu;
			fnhe->fnhe_mtu_locked = lock;
		}
		fnhe->fnhe_expires = max(1UL, expires);
		/* Update all cached dsts too */
		rt = rcu_dereference(fnhe->fnhe_rth_input);
		if (rt)
			fill_route_from_fnhe(rt, fnhe);
		rt = rcu_dereference(fnhe->fnhe_rth_output);
		if (rt)
			fill_route_from_fnhe(rt, fnhe);
	} else {
		if (depth > FNHE_RECLAIM_DEPTH)
			fnhe = fnhe_oldest(hash);
		else {
			fnhe = kzalloc(sizeof(*fnhe), GFP_ATOMIC);
			if (!fnhe)
				goto out_unlock;

			fnhe->fnhe_next = hash->chain;
			rcu_assign_pointer(hash->chain, fnhe);
		}
		fnhe->fnhe_genid = genid;
		fnhe->fnhe_daddr = daddr;
		fnhe->fnhe_gw = gw;
		fnhe->fnhe_pmtu = pmtu;
		fnhe->fnhe_mtu_locked = lock;
		fnhe->fnhe_expires = max(1UL, expires);

		/* Exception created; mark the cached routes for the nexthop
		 * stale, so anyone caching it rechecks if this exception
		 * applies to them.
		 */
		rt = rcu_dereference(nh->nh_rth_input);
		if (rt)
			rt->dst.obsolete = DST_OBSOLETE_KILL;

		for_each_possible_cpu(i) {
			struct rtable __rcu **prt;
			prt = per_cpu_ptr(nh->nh_pcpu_rth_output, i);
			rt = rcu_dereference(*prt);
			if (rt)
				rt->dst.obsolete = DST_OBSOLETE_KILL;
		}
	}

	fnhe->fnhe_stamp = jiffies;

out_unlock:
	spin_unlock_bh(&fnhe_lock);
}

static void __ip_do_redirect(struct rtable *rt, struct sk_buff *skb, struct flowi4 *fl4,
			     bool kill_route)
{
	__be32 new_gw = icmp_hdr(skb)->un.gateway;
	__be32 old_gw = ip_hdr(skb)->saddr;
	struct net_device *dev = skb->dev;
	struct in_device *in_dev;
	struct fib_result res;
	struct neighbour *n;
	struct net *net;

	switch (icmp_hdr(skb)->code & 7) {
	case ICMP_REDIR_NET:
	case ICMP_REDIR_NETTOS:
	case ICMP_REDIR_HOST:
	case ICMP_REDIR_HOSTTOS:
		break;

	default:
		return;
	}

	if (rt->rt_gateway != old_gw)
		return;

	in_dev = __in_dev_get_rcu(dev);
	if (!in_dev)
		return;

	net = dev_net(dev);
	if (new_gw == old_gw || !IN_DEV_RX_REDIRECTS(in_dev) ||
	    ipv4_is_multicast(new_gw) || ipv4_is_lbcast(new_gw) ||
	    ipv4_is_zeronet(new_gw))
		goto reject_redirect;

	if (!IN_DEV_SHARED_MEDIA(in_dev)) {
		if (!inet_addr_onlink(in_dev, new_gw, old_gw))
			goto reject_redirect;
		if (IN_DEV_SEC_REDIRECTS(in_dev) && ip_fib_check_default(new_gw, dev))
			goto reject_redirect;
	} else {
		if (inet_addr_type(net, new_gw) != RTN_UNICAST)
			goto reject_redirect;
	}

	n = __ipv4_neigh_lookup(rt->dst.dev, new_gw);
	if (!n)
		n = neigh_create(&arp_tbl, &new_gw, rt->dst.dev);
	if (!IS_ERR(n)) {
		if (!(n->nud_state & NUD_VALID)) {
			neigh_event_send(n, NULL);
		} else {
			if (fib_lookup(net, fl4, &res, 0) == 0) {
				struct fib_nh *nh = &FIB_RES_NH(res);

				update_or_create_fnhe(nh, fl4->daddr, new_gw,
						0, false,
						jiffies + ip_rt_gc_timeout);
			}
			if (kill_route)
				rt->dst.obsolete = DST_OBSOLETE_KILL;
			call_netevent_notifiers(NETEVENT_NEIGH_UPDATE, n);
		}
		neigh_release(n);
	}
	return;

reject_redirect:
#ifdef CONFIG_IP_ROUTE_VERBOSE
	if (IN_DEV_LOG_MARTIANS(in_dev)) {
		const struct iphdr *iph = (const struct iphdr *) skb->data;
		__be32 daddr = iph->daddr;
		__be32 saddr = iph->saddr;

		net_info_ratelimited("Redirect from %pI4 on %s about %pI4 ignored\n"
				     "  Advised path = %pI4 -> %pI4\n",
				     &old_gw, dev->name, &new_gw,
				     &saddr, &daddr);
	}
#endif
	;
}

static void ip_do_redirect(struct dst_entry *dst, struct sock *sk, struct sk_buff *skb)
{
	struct rtable *rt;
	struct flowi4 fl4;
	const struct iphdr *iph = (const struct iphdr *) skb->data;
	struct net *net = dev_net(skb->dev);
	int oif = skb->dev->ifindex;
	u8 tos = RT_TOS(iph->tos);
	u8 prot = iph->protocol;
	u32 mark = skb->mark;

	rt = (struct rtable *) dst;

	__build_flow_key(net, &fl4, sk, iph, oif, tos, prot, mark, 0);
	__ip_do_redirect(rt, skb, &fl4, true);
}

static struct dst_entry *ipv4_negative_advice(struct dst_entry *dst)
{
	struct rtable *rt = (struct rtable *)dst;
	struct dst_entry *ret = dst;

	if (rt) {
		if (dst->obsolete > 0) {
			ip_rt_put(rt);
			ret = NULL;
		} else if ((rt->rt_flags & RTCF_REDIRECTED) ||
			   rt->dst.expires) {
			ip_rt_put(rt);
			ret = NULL;
		}
	}
	return ret;
}

/*
 * Algorithm:
 *	1. The first ip_rt_redirect_number redirects are sent
 *	   with exponential backoff, then we stop sending them at all,
 *	   assuming that the host ignores our redirects.
 *	2. If we did not see packets requiring redirects
 *	   during ip_rt_redirect_silence, we assume that the host
 *	   forgot redirected route and start to send redirects again.
 *
 * This algorithm is much cheaper and more intelligent than dumb load limiting
 * in icmp.c.
 *
 * NOTE. Do not forget to inhibit load limiting for redirects (redundant)
 * and "frag. need" (breaks PMTU discovery) in icmp.c.
 */

void ip_rt_send_redirect(struct sk_buff *skb)
{
	struct rtable *rt = skb_rtable(skb);
	struct in_device *in_dev;
	struct inet_peer *peer;
	struct net *net;
	int log_martians;
	int vif;

	rcu_read_lock();
	in_dev = __in_dev_get_rcu(rt->dst.dev);
	if (!in_dev || !IN_DEV_TX_REDIRECTS(in_dev)) {
		rcu_read_unlock();
		return;
	}
	log_martians = IN_DEV_LOG_MARTIANS(in_dev);
	vif = l3mdev_master_ifindex_rcu(rt->dst.dev);
	rcu_read_unlock();

	net = dev_net(rt->dst.dev);
	peer = inet_getpeer_v4(net->ipv4.peers, ip_hdr(skb)->saddr, vif, 1);
	if (!peer) {
		icmp_send(skb, ICMP_REDIRECT, ICMP_REDIR_HOST,
			  rt_nexthop(rt, ip_hdr(skb)->daddr));
		return;
	}

	/* No redirected packets during ip_rt_redirect_silence;
	 * reset the algorithm.
	 */
	if (time_after(jiffies, peer->rate_last + ip_rt_redirect_silence)) {
		peer->rate_tokens = 0;
		peer->n_redirects = 0;
	}

	/* Too many ignored redirects; do not send anything
	 * set dst.rate_last to the last seen redirected packet.
	 */
	if (peer->n_redirects >= ip_rt_redirect_number) {
		peer->rate_last = jiffies;
		goto out_put_peer;
	}

	/* Check for load limit; set rate_last to the latest sent
	 * redirect.
	 */
	if (peer->rate_tokens == 0 ||
	    time_after(jiffies,
		       (peer->rate_last +
			(ip_rt_redirect_load << peer->rate_tokens)))) {
		__be32 gw = rt_nexthop(rt, ip_hdr(skb)->daddr);

		icmp_send(skb, ICMP_REDIRECT, ICMP_REDIR_HOST, gw);
		peer->rate_last = jiffies;
		++peer->rate_tokens;
		++peer->n_redirects;
#ifdef CONFIG_IP_ROUTE_VERBOSE
		if (log_martians &&
		    peer->rate_tokens == ip_rt_redirect_number)
			net_warn_ratelimited("host %pI4/if%d ignores redirects for %pI4 to %pI4\n",
					     &ip_hdr(skb)->saddr, inet_iif(skb),
					     &ip_hdr(skb)->daddr, &gw);
#endif
	}
out_put_peer:
	inet_putpeer(peer);
}

static int ip_error(struct sk_buff *skb)
{
	struct rtable *rt = skb_rtable(skb);
	struct net_device *dev = skb->dev;
	struct in_device *in_dev;
	struct inet_peer *peer;
	unsigned long now;
	struct net *net;
	bool send;
	int code;

	if (netif_is_l3_master(skb->dev)) {
		dev = __dev_get_by_index(dev_net(skb->dev), IPCB(skb)->iif);
		if (!dev)
			goto out;
	}

	in_dev = __in_dev_get_rcu(dev);

	/* IP on this device is disabled. */
	if (!in_dev)
		goto out;

	net = dev_net(rt->dst.dev);
	if (!IN_DEV_FORWARD(in_dev)) {
		switch (rt->dst.error) {
		case EHOSTUNREACH:
			__IP_INC_STATS(net, IPSTATS_MIB_INADDRERRORS);
			break;

		case ENETUNREACH:
			__IP_INC_STATS(net, IPSTATS_MIB_INNOROUTES);
			break;
		}
		goto out;
	}

	switch (rt->dst.error) {
	case EINVAL:
	default:
		goto out;
	case EHOSTUNREACH:
		code = ICMP_HOST_UNREACH;
		break;
	case ENETUNREACH:
		code = ICMP_NET_UNREACH;
		__IP_INC_STATS(net, IPSTATS_MIB_INNOROUTES);
		break;
	case EACCES:
		code = ICMP_PKT_FILTERED;
		break;
	}

	peer = inet_getpeer_v4(net->ipv4.peers, ip_hdr(skb)->saddr,
			       l3mdev_master_ifindex(skb->dev), 1);

	send = true;
	if (peer) {
		now = jiffies;
		peer->rate_tokens += now - peer->rate_last;
		if (peer->rate_tokens > ip_rt_error_burst)
			peer->rate_tokens = ip_rt_error_burst;
		peer->rate_last = now;
		if (peer->rate_tokens >= ip_rt_error_cost)
			peer->rate_tokens -= ip_rt_error_cost;
		else
			send = false;
		inet_putpeer(peer);
	}
	if (send)
		icmp_send(skb, ICMP_DEST_UNREACH, code, 0);

out:	kfree_skb(skb);
	return 0;
}

static void __ip_rt_update_pmtu(struct rtable *rt, struct flowi4 *fl4, u32 mtu)
{
	struct dst_entry *dst = &rt->dst;
	u32 old_mtu = ipv4_mtu(dst);
	struct fib_result res;
	bool lock = false;

	if (ip_mtu_locked(dst))
		return;

	if (old_mtu < mtu)
		return;

	if (mtu < ip_rt_min_pmtu) {
		lock = true;
		mtu = min(old_mtu, ip_rt_min_pmtu);
	}

	if (rt->rt_pmtu == mtu && !lock &&
	    time_before(jiffies, dst->expires - ip_rt_mtu_expires / 2))
		return;

	rcu_read_lock();
	if (fib_lookup(dev_net(dst->dev), fl4, &res, 0) == 0) {
		struct fib_nh *nh = &FIB_RES_NH(res);

		update_or_create_fnhe(nh, fl4->daddr, 0, mtu, lock,
				      jiffies + ip_rt_mtu_expires);
	}
	rcu_read_unlock();
}

static void ip_rt_update_pmtu(struct dst_entry *dst, struct sock *sk,
			      struct sk_buff *skb, u32 mtu)
{
	struct rtable *rt = (struct rtable *) dst;
	struct flowi4 fl4;

	ip_rt_build_flow_key(&fl4, sk, skb);
	__ip_rt_update_pmtu(rt, &fl4, mtu);
}

void ipv4_update_pmtu(struct sk_buff *skb, struct net *net, u32 mtu,
		      int oif, u8 protocol)
{
	const struct iphdr *iph = (const struct iphdr *) skb->data;
	struct flowi4 fl4;
	struct rtable *rt;
	u32 mark = IP4_REPLY_MARK(net, skb->mark);

	__build_flow_key(net, &fl4, NULL, iph, oif,
			 RT_TOS(iph->tos), protocol, mark, 0);
	rt = __ip_route_output_key(net, &fl4);
	if (!IS_ERR(rt)) {
		__ip_rt_update_pmtu(rt, &fl4, mtu);
		ip_rt_put(rt);
	}
}
EXPORT_SYMBOL_GPL(ipv4_update_pmtu);

static void __ipv4_sk_update_pmtu(struct sk_buff *skb, struct sock *sk, u32 mtu)
{
	const struct iphdr *iph = (const struct iphdr *) skb->data;
	struct flowi4 fl4;
	struct rtable *rt;

	__build_flow_key(sock_net(sk), &fl4, sk, iph, 0, 0, 0, 0, 0);

	if (!fl4.flowi4_mark)
		fl4.flowi4_mark = IP4_REPLY_MARK(sock_net(sk), skb->mark);

	rt = __ip_route_output_key(sock_net(sk), &fl4);
	if (!IS_ERR(rt)) {
		__ip_rt_update_pmtu(rt, &fl4, mtu);
		ip_rt_put(rt);
	}
}

void ipv4_sk_update_pmtu(struct sk_buff *skb, struct sock *sk, u32 mtu)
{
	const struct iphdr *iph = (const struct iphdr *) skb->data;
	struct flowi4 fl4;
	struct rtable *rt;
	struct dst_entry *odst = NULL;
	bool new = false;
	struct net *net = sock_net(sk);

	bh_lock_sock(sk);

	if (!ip_sk_accept_pmtu(sk))
		goto out;

	odst = sk_dst_get(sk);

	if (sock_owned_by_user(sk) || !odst) {
		__ipv4_sk_update_pmtu(skb, sk, mtu);
		goto out;
	}

	__build_flow_key(net, &fl4, sk, iph, 0, 0, 0, 0, 0);

	rt = (struct rtable *)odst;
	if (odst->obsolete && !odst->ops->check(odst, 0)) {
		rt = ip_route_output_flow(sock_net(sk), &fl4, sk);
		if (IS_ERR(rt))
			goto out;

		new = true;
	}

	__ip_rt_update_pmtu((struct rtable *) xfrm_dst_path(&rt->dst), &fl4, mtu);

	if (!dst_check(&rt->dst, 0)) {
		if (new)
			dst_release(&rt->dst);

		rt = ip_route_output_flow(sock_net(sk), &fl4, sk);
		if (IS_ERR(rt))
			goto out;

		new = true;
	}

	if (new)
		sk_dst_set(sk, &rt->dst);

out:
	bh_unlock_sock(sk);
	dst_release(odst);
}
EXPORT_SYMBOL_GPL(ipv4_sk_update_pmtu);

void ipv4_redirect(struct sk_buff *skb, struct net *net,
		   int oif, u8 protocol)
{
	const struct iphdr *iph = (const struct iphdr *) skb->data;
	struct flowi4 fl4;
	struct rtable *rt;

	__build_flow_key(net, &fl4, NULL, iph, oif,
			 RT_TOS(iph->tos), protocol, 0, 0);
	rt = __ip_route_output_key(net, &fl4);
	if (!IS_ERR(rt)) {
		__ip_do_redirect(rt, skb, &fl4, false);
		ip_rt_put(rt);
	}
}
EXPORT_SYMBOL_GPL(ipv4_redirect);

void ipv4_sk_redirect(struct sk_buff *skb, struct sock *sk)
{
	const struct iphdr *iph = (const struct iphdr *) skb->data;
	struct flowi4 fl4;
	struct rtable *rt;
	struct net *net = sock_net(sk);

	__build_flow_key(net, &fl4, sk, iph, 0, 0, 0, 0, 0);
	rt = __ip_route_output_key(net, &fl4);
	if (!IS_ERR(rt)) {
		__ip_do_redirect(rt, skb, &fl4, false);
		ip_rt_put(rt);
	}
}
EXPORT_SYMBOL_GPL(ipv4_sk_redirect);

static struct dst_entry *ipv4_dst_check(struct dst_entry *dst, u32 cookie)
{
	struct rtable *rt = (struct rtable *) dst;

	/* All IPV4 dsts are created with ->obsolete set to the value
	 * DST_OBSOLETE_FORCE_CHK which forces validation calls down
	 * into this function always.
	 *
	 * When a PMTU/redirect information update invalidates a route,
	 * this is indicated by setting obsolete to DST_OBSOLETE_KILL or
	 * DST_OBSOLETE_DEAD by dst_free().
	 */
	if (dst->obsolete != DST_OBSOLETE_FORCE_CHK || rt_is_expired(rt))
		return NULL;
	return dst;
}

static void ipv4_link_failure(struct sk_buff *skb)
{
	struct ip_options opt;
	struct rtable *rt;
	int res;

	/* Recompile ip options since IPCB may not be valid anymore.
	 */
	memset(&opt, 0, sizeof(opt));
	opt.optlen = ip_hdr(skb)->ihl*4 - sizeof(struct iphdr);

	rcu_read_lock();
	res = __ip_options_compile(dev_net(skb->dev), &opt, skb, NULL);
	rcu_read_unlock();

	if (res)
		return;

	__icmp_send(skb, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH, 0, &opt);

	rt = skb_rtable(skb);
	if (rt)
		dst_set_expires(&rt->dst, 0);
}

static int ip_rt_bug(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	pr_debug("%s: %pI4 -> %pI4, %s\n",
		 __func__, &ip_hdr(skb)->saddr, &ip_hdr(skb)->daddr,
		 skb->dev ? skb->dev->name : "?");
	kfree_skb(skb);
	WARN_ON(1);
	return 0;
}

/*
   We do not cache source address of outgoing interface,
   because it is used only by IP RR, TS and SRR options,
   so that it out of fast path.

   BTW remember: "addr" is allowed to be not aligned
   in IP options!
 */

void ip_rt_get_source(u8 *addr, struct sk_buff *skb, struct rtable *rt)
{
	__be32 src;

	if (rt_is_output_route(rt))
		src = ip_hdr(skb)->saddr;
	else {
		struct fib_result res;
		struct iphdr *iph = ip_hdr(skb);
		struct flowi4 fl4 = {
			.daddr = iph->daddr,
			.saddr = iph->saddr,
			.flowi4_tos = RT_TOS(iph->tos),
			.flowi4_oif = rt->dst.dev->ifindex,
			.flowi4_iif = skb->dev->ifindex,
			.flowi4_mark = skb->mark,
		};

		rcu_read_lock();
		if (fib_lookup(dev_net(rt->dst.dev), &fl4, &res, 0) == 0)
			src = FIB_RES_PREFSRC(dev_net(rt->dst.dev), res);
		else
			src = inet_select_addr(rt->dst.dev,
					       rt_nexthop(rt, iph->daddr),
					       RT_SCOPE_UNIVERSE);
		rcu_read_unlock();
	}
	memcpy(addr, &src, 4);
}

#ifdef CONFIG_IP_ROUTE_CLASSID
static void set_class_tag(struct rtable *rt, u32 tag)
{
	if (!(rt->dst.tclassid & 0xFFFF))
		rt->dst.tclassid |= tag & 0xFFFF;
	if (!(rt->dst.tclassid & 0xFFFF0000))
		rt->dst.tclassid |= tag & 0xFFFF0000;
}
#endif

static unsigned int ipv4_default_advmss(const struct dst_entry *dst)
{
	unsigned int header_size = sizeof(struct tcphdr) + sizeof(struct iphdr);
	unsigned int advmss = max_t(unsigned int, ipv4_mtu(dst) - header_size,
				    ip_rt_min_advmss);

	return min(advmss, IPV4_MAX_PMTU - header_size);
}

static unsigned int ipv4_mtu(const struct dst_entry *dst)
{
	const struct rtable *rt = (const struct rtable *) dst;
	unsigned int mtu = rt->rt_pmtu;

	if (!mtu || time_after_eq(jiffies, rt->dst.expires))
		mtu = dst_metric_raw(dst, RTAX_MTU);

	if (mtu)
		return mtu;

	mtu = READ_ONCE(dst->dev->mtu);

	if (unlikely(ip_mtu_locked(dst))) {
		if (rt->rt_uses_gateway && mtu > 576)
			mtu = 576;
	}

	mtu = min_t(unsigned int, mtu, IP_MAX_MTU);

	return mtu - lwtunnel_headroom(dst->lwtstate, mtu);
}

static void ip_del_fnhe(struct fib_nh *nh, __be32 daddr)
{
	struct fnhe_hash_bucket *hash;
	struct fib_nh_exception *fnhe, __rcu **fnhe_p;
	u32 hval = fnhe_hashfun(daddr);

	spin_lock_bh(&fnhe_lock);

	hash = rcu_dereference_protected(nh->nh_exceptions,
					 lockdep_is_held(&fnhe_lock));
	hash += hval;

	fnhe_p = &hash->chain;
	fnhe = rcu_dereference_protected(*fnhe_p, lockdep_is_held(&fnhe_lock));
	while (fnhe) {
		if (fnhe->fnhe_daddr == daddr) {
			rcu_assign_pointer(*fnhe_p, rcu_dereference_protected(
				fnhe->fnhe_next, lockdep_is_held(&fnhe_lock)));
			/* set fnhe_daddr to 0 to ensure it won't bind with
			 * new dsts in rt_bind_exception().
			 */
			fnhe->fnhe_daddr = 0;
			fnhe_flush_routes(fnhe);
			kfree_rcu(fnhe, rcu);
			break;
		}
		fnhe_p = &fnhe->fnhe_next;
		fnhe = rcu_dereference_protected(fnhe->fnhe_next,
						 lockdep_is_held(&fnhe_lock));
	}

	spin_unlock_bh(&fnhe_lock);
}

static struct fib_nh_exception *find_exception(struct fib_nh *nh, __be32 daddr)
{
	struct fnhe_hash_bucket *hash = rcu_dereference(nh->nh_exceptions);
	struct fib_nh_exception *fnhe;
	u32 hval;

	if (!hash)
		return NULL;

	hval = fnhe_hashfun(daddr);

	for (fnhe = rcu_dereference(hash[hval].chain); fnhe;
	     fnhe = rcu_dereference(fnhe->fnhe_next)) {
		if (fnhe->fnhe_daddr == daddr) {
			if (fnhe->fnhe_expires &&
			    time_after(jiffies, fnhe->fnhe_expires)) {
				ip_del_fnhe(nh, daddr);
				break;
			}
			return fnhe;
		}
	}
	return NULL;
}

/* MTU selection:
 * 1. mtu on route is locked - use it
 * 2. mtu from nexthop exception
 * 3. mtu from egress device
 */

u32 ip_mtu_from_fib_result(struct fib_result *res, __be32 daddr)
{
	struct fib_info *fi = res->fi;
	struct fib_nh *nh = &fi->fib_nh[res->nh_sel];
	struct net_device *dev = nh->nh_dev;
	u32 mtu = 0;

	if (dev_net(dev)->ipv4.sysctl_ip_fwd_use_pmtu ||
	    fi->fib_metrics->metrics[RTAX_LOCK - 1] & (1 << RTAX_MTU))
		mtu = fi->fib_mtu;

	if (likely(!mtu)) {
		struct fib_nh_exception *fnhe;

		fnhe = find_exception(nh, daddr);
		if (fnhe && !time_after_eq(jiffies, fnhe->fnhe_expires))
			mtu = fnhe->fnhe_pmtu;
	}

	if (likely(!mtu))
		mtu = min(READ_ONCE(dev->mtu), IP_MAX_MTU);

	return mtu - lwtunnel_headroom(nh->nh_lwtstate, mtu);
}

static bool rt_bind_exception(struct rtable *rt, struct fib_nh_exception *fnhe,
			      __be32 daddr, const bool do_cache)
{
	bool ret = false;

	spin_lock_bh(&fnhe_lock);

	if (daddr == fnhe->fnhe_daddr) {
		struct rtable __rcu **porig;
		struct rtable *orig;
		int genid = fnhe_genid(dev_net(rt->dst.dev));

		if (rt_is_input_route(rt))
			porig = &fnhe->fnhe_rth_input;
		else
			porig = &fnhe->fnhe_rth_output;
		orig = rcu_dereference(*porig);

		if (fnhe->fnhe_genid != genid) {
			fnhe->fnhe_genid = genid;
			fnhe->fnhe_gw = 0;
			fnhe->fnhe_pmtu = 0;
			fnhe->fnhe_expires = 0;
			fnhe->fnhe_mtu_locked = false;
			fnhe_flush_routes(fnhe);
			orig = NULL;
		}
		fill_route_from_fnhe(rt, fnhe);
		if (!rt->rt_gateway)
			rt->rt_gateway = daddr;

		if (do_cache) {
			dst_hold(&rt->dst);
			rcu_assign_pointer(*porig, rt);
			if (orig) {
				dst_dev_put(&orig->dst);
				dst_release(&orig->dst);
			}
			ret = true;
		}

		fnhe->fnhe_stamp = jiffies;
	}
	spin_unlock_bh(&fnhe_lock);

	return ret;
}

static bool rt_cache_route(struct fib_nh *nh, struct rtable *rt)
{
	struct rtable *orig, *prev, **p;
	bool ret = true;

	if (rt_is_input_route(rt)) {
		p = (struct rtable **)&nh->nh_rth_input;
	} else {
		p = (struct rtable **)raw_cpu_ptr(nh->nh_pcpu_rth_output);
	}
	orig = *p;

	/* hold dst before doing cmpxchg() to avoid race condition
	 * on this dst
	 */
	dst_hold(&rt->dst);
	prev = cmpxchg(p, orig, rt);
	if (prev == orig) {
		if (orig) {
			dst_dev_put(&orig->dst);
			dst_release(&orig->dst);
		}
	} else {
		dst_release(&rt->dst);
		ret = false;
	}

	return ret;
}

struct uncached_list {
	spinlock_t		lock;
	struct list_head	head;
};

static DEFINE_PER_CPU_ALIGNED(struct uncached_list, rt_uncached_list);

void rt_add_uncached_list(struct rtable *rt)
{
	struct uncached_list *ul = raw_cpu_ptr(&rt_uncached_list);

	rt->rt_uncached_list = ul;

	spin_lock_bh(&ul->lock);
	list_add_tail(&rt->rt_uncached, &ul->head);
	spin_unlock_bh(&ul->lock);
}

void rt_del_uncached_list(struct rtable *rt)
{
	if (!list_empty(&rt->rt_uncached)) {
		struct uncached_list *ul = rt->rt_uncached_list;

		spin_lock_bh(&ul->lock);
		list_del(&rt->rt_uncached);
		spin_unlock_bh(&ul->lock);
	}
}

static void ipv4_dst_destroy(struct dst_entry *dst)
{
	struct rtable *rt = (struct rtable *)dst;

	ip_dst_metrics_put(dst);
	rt_del_uncached_list(rt);
}

void rt_flush_dev(struct net_device *dev)
{
	struct net *net = dev_net(dev);
	struct rtable *rt;
	int cpu;

	for_each_possible_cpu(cpu) {
		struct uncached_list *ul = &per_cpu(rt_uncached_list, cpu);

		spin_lock_bh(&ul->lock);
		list_for_each_entry(rt, &ul->head, rt_uncached) {
			if (rt->dst.dev != dev)
				continue;
			rt->dst.dev = net->loopback_dev;
			dev_hold(rt->dst.dev);
			dev_put(dev);
		}
		spin_unlock_bh(&ul->lock);
	}
}

static bool rt_cache_valid(const struct rtable *rt)
{
	return	rt &&
		rt->dst.obsolete == DST_OBSOLETE_FORCE_CHK &&
		!rt_is_expired(rt);
}

static void rt_set_nexthop(struct rtable *rt, __be32 daddr,
			   const struct fib_result *res,
			   struct fib_nh_exception *fnhe,
			   struct fib_info *fi, u16 type, u32 itag,
			   const bool do_cache)
{
	bool cached = false;

	if (fi) {
		struct fib_nh *nh = &FIB_RES_NH(*res);

		if (nh->nh_gw && nh->nh_scope == RT_SCOPE_LINK) {
			rt->rt_gateway = nh->nh_gw;
			rt->rt_uses_gateway = 1;
		}
		ip_dst_init_metrics(&rt->dst, fi->fib_metrics);

#ifdef CONFIG_IP_ROUTE_CLASSID
		rt->dst.tclassid = nh->nh_tclassid;
#endif
		rt->dst.lwtstate = lwtstate_get(nh->nh_lwtstate);
		if (unlikely(fnhe))
			cached = rt_bind_exception(rt, fnhe, daddr, do_cache);
		else if (do_cache)
			cached = rt_cache_route(nh, rt);
		if (unlikely(!cached)) {
			/* Routes we intend to cache in nexthop exception or
			 * FIB nexthop have the DST_NOCACHE bit clear.
			 * However, if we are unsuccessful at storing this
			 * route into the cache we really need to set it.
			 */
			if (!rt->rt_gateway)
				rt->rt_gateway = daddr;
			rt_add_uncached_list(rt);
		}
	} else
		rt_add_uncached_list(rt);

#ifdef CONFIG_IP_ROUTE_CLASSID
#ifdef CONFIG_IP_MULTIPLE_TABLES
	set_class_tag(rt, res->tclassid);
#endif
	set_class_tag(rt, itag);
#endif
}

struct rtable *rt_dst_alloc(struct net_device *dev,
			    unsigned int flags, u16 type,
			    bool nopolicy, bool noxfrm, bool will_cache)
{
	struct rtable *rt;

	rt = dst_alloc(&ipv4_dst_ops, dev, 1, DST_OBSOLETE_FORCE_CHK,
		       (will_cache ? 0 : DST_HOST) |
		       (nopolicy ? DST_NOPOLICY : 0) |
		       (noxfrm ? DST_NOXFRM : 0));

	if (rt) {
		rt->rt_genid = rt_genid_ipv4(dev_net(dev));
		rt->rt_flags = flags;
		rt->rt_type = type;
		rt->rt_is_input = 0;
		rt->rt_iif = 0;
		rt->rt_pmtu = 0;
		rt->rt_mtu_locked = 0;
		rt->rt_gateway = 0;
		rt->rt_uses_gateway = 0;
		INIT_LIST_HEAD(&rt->rt_uncached);

		rt->dst.output = ip_output;
		if (flags & RTCF_LOCAL)
			rt->dst.input = ip_local_deliver;
	}

	return rt;
}
EXPORT_SYMBOL(rt_dst_alloc);

/* called in rcu_read_lock() section */
int ip_mc_validate_source(struct sk_buff *skb, __be32 daddr, __be32 saddr,
			  u8 tos, struct net_device *dev,
			  struct in_device *in_dev, u32 *itag)
{
	int err;

	/* Primary sanity checks. */
	if (!in_dev)
		return -EINVAL;

	if (ipv4_is_multicast(saddr) || ipv4_is_lbcast(saddr) ||
	    skb->protocol != htons(ETH_P_IP))
		return -EINVAL;

	if (ipv4_is_loopback(saddr) && !IN_DEV_ROUTE_LOCALNET(in_dev))
		return -EINVAL;

	if (ipv4_is_zeronet(saddr)) {
		if (!ipv4_is_local_multicast(daddr) &&
		    ip_hdr(skb)->protocol != IPPROTO_IGMP)
			return -EINVAL;
	} else {
		err = fib_validate_source(skb, saddr, 0, tos, 0, dev,
					  in_dev, itag);
		if (err < 0)
			return err;
	}
	return 0;
}

/* called in rcu_read_lock() section */
static int ip_route_input_mc(struct sk_buff *skb, __be32 daddr, __be32 saddr,
			     u8 tos, struct net_device *dev, int our)
{
	struct in_device *in_dev = __in_dev_get_rcu(dev);
	unsigned int flags = RTCF_MULTICAST;
	struct rtable *rth;
	u32 itag = 0;
	int err;

	err = ip_mc_validate_source(skb, daddr, saddr, tos, dev, in_dev, &itag);
	if (err)
		return err;

	if (our)
		flags |= RTCF_LOCAL;

	rth = rt_dst_alloc(dev_net(dev)->loopback_dev, flags, RTN_MULTICAST,
			   IN_DEV_CONF_GET(in_dev, NOPOLICY), false, false);
	if (!rth)
		return -ENOBUFS;

#ifdef CONFIG_IP_ROUTE_CLASSID
	rth->dst.tclassid = itag;
#endif
	rth->dst.output = ip_rt_bug;
	rth->rt_is_input= 1;

#ifdef CONFIG_IP_MROUTE
	if (!ipv4_is_local_multicast(daddr) && IN_DEV_MFORWARD(in_dev))
		rth->dst.input = ip_mr_input;
#endif
	RT_CACHE_STAT_INC(in_slow_mc);

	skb_dst_set(skb, &rth->dst);
	return 0;
}


static void ip_handle_martian_source(struct net_device *dev,
				     struct in_device *in_dev,
				     struct sk_buff *skb,
				     __be32 daddr,
				     __be32 saddr)
{
	RT_CACHE_STAT_INC(in_martian_src);
#ifdef CONFIG_IP_ROUTE_VERBOSE
	if (IN_DEV_LOG_MARTIANS(in_dev) && net_ratelimit()) {
		/*
		 *	RFC1812 recommendation, if source is martian,
		 *	the only hint is MAC header.
		 */
		pr_warn("martian source %pI4 from %pI4, on dev %s\n",
			&daddr, &saddr, dev->name);
		if (dev->hard_header_len && skb_mac_header_was_set(skb)) {
			print_hex_dump(KERN_WARNING, "ll header: ",
				       DUMP_PREFIX_OFFSET, 16, 1,
				       skb_mac_header(skb),
				       dev->hard_header_len, false);
		}
	}
#endif
}

/* called in rcu_read_lock() section */
static int __mkroute_input(struct sk_buff *skb,
			   const struct fib_result *res,
			   struct in_device *in_dev,
			   __be32 daddr, __be32 saddr, u32 tos)
{
	struct fib_nh_exception *fnhe;
	struct rtable *rth;
	int err;
	struct in_device *out_dev;
	bool do_cache;
	u32 itag = 0;

	/* get a working reference to the output device */
	out_dev = __in_dev_get_rcu(FIB_RES_DEV(*res));
	if (!out_dev) {
		net_crit_ratelimited("Bug in ip_route_input_slow(). Please report.\n");
		return -EINVAL;
	}

	err = fib_validate_source(skb, saddr, daddr, tos, FIB_RES_OIF(*res),
				  in_dev->dev, in_dev, &itag);
	if (err < 0) {
		ip_handle_martian_source(in_dev->dev, in_dev, skb, daddr,
					 saddr);

		goto cleanup;
	}

	do_cache = res->fi && !itag;
	if (out_dev == in_dev && err && IN_DEV_TX_REDIRECTS(out_dev) &&
	    skb->protocol == htons(ETH_P_IP) &&
	    (IN_DEV_SHARED_MEDIA(out_dev) ||
	     inet_addr_onlink(out_dev, saddr, FIB_RES_GW(*res))))
		IPCB(skb)->flags |= IPSKB_DOREDIRECT;

	if (skb->protocol != htons(ETH_P_IP)) {
		/* Not IP (i.e. ARP). Do not create route, if it is
		 * invalid for proxy arp. DNAT routes are always valid.
		 *
		 * Proxy arp feature have been extended to allow, ARP
		 * replies back to the same interface, to support
		 * Private VLAN switch technologies. See arp.c.
		 */
		if (out_dev == in_dev &&
		    IN_DEV_PROXY_ARP_PVLAN(in_dev) == 0) {
			err = -EINVAL;
			goto cleanup;
		}
	}

	fnhe = find_exception(&FIB_RES_NH(*res), daddr);
	if (do_cache) {
		if (fnhe)
			rth = rcu_dereference(fnhe->fnhe_rth_input);
		else
			rth = rcu_dereference(FIB_RES_NH(*res).nh_rth_input);
		if (rt_cache_valid(rth)) {
			skb_dst_set_noref(skb, &rth->dst);
			goto out;
		}
	}

	rth = rt_dst_alloc(out_dev->dev, 0, res->type,
			   IN_DEV_CONF_GET(in_dev, NOPOLICY),
			   IN_DEV_CONF_GET(out_dev, NOXFRM), do_cache);
	if (!rth) {
		err = -ENOBUFS;
		goto cleanup;
	}

	rth->rt_is_input = 1;
	RT_CACHE_STAT_INC(in_slow_tot);

	rth->dst.input = ip_forward;

	rt_set_nexthop(rth, daddr, res, fnhe, res->fi, res->type, itag,
		       do_cache);
	lwtunnel_set_redirect(&rth->dst);
	skb_dst_set(skb, &rth->dst);
out:
	err = 0;
 cleanup:
	return err;
}

#ifdef CONFIG_IP_ROUTE_MULTIPATH
/* To make ICMP packets follow the right flow, the multipath hash is
 * calculated from the inner IP addresses.
 */
static void ip_multipath_l3_keys(const struct sk_buff *skb,
				 struct flow_keys *hash_keys)
{
	const struct iphdr *outer_iph = ip_hdr(skb);
	const struct iphdr *key_iph = outer_iph;
	const struct iphdr *inner_iph;
	const struct icmphdr *icmph;
	struct iphdr _inner_iph;
	struct icmphdr _icmph;

	if (likely(outer_iph->protocol != IPPROTO_ICMP))
		goto out;

	if (unlikely((outer_iph->frag_off & htons(IP_OFFSET)) != 0))
		goto out;

	icmph = skb_header_pointer(skb, outer_iph->ihl * 4, sizeof(_icmph),
				   &_icmph);
	if (!icmph)
		goto out;

	if (icmph->type != ICMP_DEST_UNREACH &&
	    icmph->type != ICMP_REDIRECT &&
	    icmph->type != ICMP_TIME_EXCEEDED &&
	    icmph->type != ICMP_PARAMETERPROB)
		goto out;

	inner_iph = skb_header_pointer(skb,
				       outer_iph->ihl * 4 + sizeof(_icmph),
				       sizeof(_inner_iph), &_inner_iph);
	if (!inner_iph)
		goto out;

	key_iph = inner_iph;
out:
	hash_keys->addrs.v4addrs.src = key_iph->saddr;
	hash_keys->addrs.v4addrs.dst = key_iph->daddr;
}

/* if skb is set it will be used and fl4 can be NULL */
int fib_multipath_hash(const struct net *net, const struct flowi4 *fl4,
		       const struct sk_buff *skb, struct flow_keys *flkeys)
{
	u32 multipath_hash = fl4 ? fl4->flowi4_multipath_hash : 0;
	struct flow_keys hash_keys;
	u32 mhash;

	switch (net->ipv4.sysctl_fib_multipath_hash_policy) {
	case 0:
		memset(&hash_keys, 0, sizeof(hash_keys));
		hash_keys.control.addr_type = FLOW_DISSECTOR_KEY_IPV4_ADDRS;
		if (skb) {
			ip_multipath_l3_keys(skb, &hash_keys);
		} else {
			hash_keys.addrs.v4addrs.src = fl4->saddr;
			hash_keys.addrs.v4addrs.dst = fl4->daddr;
		}
		break;
	case 1:
		/* skb is currently provided only when forwarding */
		if (skb) {
			unsigned int flag = FLOW_DISSECTOR_F_STOP_AT_ENCAP;
			struct flow_keys keys;

			/* short-circuit if we already have L4 hash present */
			if (skb->l4_hash)
				return skb_get_hash_raw(skb) >> 1;

			memset(&hash_keys, 0, sizeof(hash_keys));

			if (!flkeys) {
				skb_flow_dissect_flow_keys(skb, &keys, flag);
				flkeys = &keys;
			}

			hash_keys.control.addr_type = FLOW_DISSECTOR_KEY_IPV4_ADDRS;
			hash_keys.addrs.v4addrs.src = flkeys->addrs.v4addrs.src;
			hash_keys.addrs.v4addrs.dst = flkeys->addrs.v4addrs.dst;
			hash_keys.ports.src = flkeys->ports.src;
			hash_keys.ports.dst = flkeys->ports.dst;
			hash_keys.basic.ip_proto = flkeys->basic.ip_proto;
		} else {
			memset(&hash_keys, 0, sizeof(hash_keys));
			hash_keys.control.addr_type = FLOW_DISSECTOR_KEY_IPV4_ADDRS;
			hash_keys.addrs.v4addrs.src = fl4->saddr;
			hash_keys.addrs.v4addrs.dst = fl4->daddr;
			hash_keys.ports.src = fl4->fl4_sport;
			hash_keys.ports.dst = fl4->fl4_dport;
			hash_keys.basic.ip_proto = fl4->flowi4_proto;
		}
		break;
	}
	mhash = flow_hash_from_keys(&hash_keys);

	if (multipath_hash)
		mhash = jhash_2words(mhash, multipath_hash, 0);

	return mhash >> 1;
}
#endif /* CONFIG_IP_ROUTE_MULTIPATH */

static int ip_mkroute_input(struct sk_buff *skb,
			    struct fib_result *res,
			    struct in_device *in_dev,
			    __be32 daddr, __be32 saddr, u32 tos,
			    struct flow_keys *hkeys)
{
#ifdef CONFIG_IP_ROUTE_MULTIPATH
	if (res->fi && res->fi->fib_nhs > 1) {
		int h = fib_multipath_hash(res->fi->fib_net, NULL, skb, hkeys);

		fib_select_multipath(res, h);
	}
#endif

	/* create a routing cache entry */
	return __mkroute_input(skb, res, in_dev, daddr, saddr, tos);
}

/*
 *	NOTE. We drop all the packets that has local source
 *	addresses, because every properly looped back packet
 *	must have correct destination already attached by output routine.
 *
 *	Such approach solves two big problems:
 *	1. Not simplex devices are handled properly.
 *	2. IP spoofing attempts are filtered with 100% of guarantee.
 *	called with rcu_read_lock()
 */

static int ip_route_input_slow(struct sk_buff *skb, __be32 daddr, __be32 saddr,
			       u8 tos, struct net_device *dev,
			       struct fib_result *res)
{
	struct in_device *in_dev = __in_dev_get_rcu(dev);
	struct flow_keys *flkeys = NULL, _flkeys;
	struct net    *net = dev_net(dev);
	struct ip_tunnel_info *tun_info;
	int		err = -EINVAL;
	unsigned int	flags = 0;
	u32		itag = 0;
	struct rtable	*rth;
	struct flowi4	fl4;
	bool do_cache;

	/* IP on this device is disabled. */

	if (!in_dev)
		goto out;

	/* Check for the most weird martians, which can be not detected
	   by fib_lookup.
	 */

	tun_info = skb_tunnel_info(skb);
	if (tun_info && !(tun_info->mode & IP_TUNNEL_INFO_TX))
		fl4.flowi4_tun_key.tun_id = tun_info->key.tun_id;
	else
		fl4.flowi4_tun_key.tun_id = 0;
	skb_dst_drop(skb);

	if (ipv4_is_multicast(saddr) || ipv4_is_lbcast(saddr))
		goto martian_source;

	res->fi = NULL;
	res->table = NULL;
	if (ipv4_is_lbcast(daddr) || (saddr == 0 && daddr == 0))
		goto brd_input;

	/* Accept zero addresses only to limited broadcast;
	 * I even do not know to fix it or not. Waiting for complains :-)
	 */
	if (ipv4_is_zeronet(saddr))
		goto martian_source;

	if (ipv4_is_zeronet(daddr))
		goto martian_destination;

	/* Following code try to avoid calling IN_DEV_NET_ROUTE_LOCALNET(),
	 * and call it once if daddr or/and saddr are loopback addresses
	 */
	if (ipv4_is_loopback(daddr)) {
		if (!IN_DEV_NET_ROUTE_LOCALNET(in_dev, net))
			goto martian_destination;
	} else if (ipv4_is_loopback(saddr)) {
		if (!IN_DEV_NET_ROUTE_LOCALNET(in_dev, net))
			goto martian_source;
	}

	/*
	 *	Now we are ready to route packet.
	 */
	fl4.flowi4_oif = 0;
	fl4.flowi4_iif = dev->ifindex;
	fl4.flowi4_mark = skb->mark;
	fl4.flowi4_tos = tos;
	fl4.flowi4_scope = RT_SCOPE_UNIVERSE;
	fl4.flowi4_flags = 0;
	fl4.daddr = daddr;
	fl4.saddr = saddr;
	fl4.flowi4_uid = sock_net_uid(net, NULL);

	if (fib4_rules_early_flow_dissect(net, skb, &fl4, &_flkeys)) {
		flkeys = &_flkeys;
	} else {
		fl4.flowi4_proto = 0;
		fl4.fl4_sport = 0;
		fl4.fl4_dport = 0;
	}

	err = fib_lookup(net, &fl4, res, 0);
	if (err != 0) {
		if (!IN_DEV_FORWARD(in_dev))
			err = -EHOSTUNREACH;
		goto no_route;
	}

	if (res->type == RTN_BROADCAST) {
		if (IN_DEV_BFORWARD(in_dev))
			goto make_route;
		goto brd_input;
	}

	if (res->type == RTN_LOCAL) {
		err = fib_validate_source(skb, saddr, daddr, tos,
					  0, dev, in_dev, &itag);
		if (err < 0)
			goto martian_source;
		goto local_input;
	}

	if (!IN_DEV_FORWARD(in_dev)) {
		err = -EHOSTUNREACH;
		goto no_route;
	}
	if (res->type != RTN_UNICAST)
		goto martian_destination;

make_route:
	err = ip_mkroute_input(skb, res, in_dev, daddr, saddr, tos, flkeys);
out:	return err;

brd_input:
	if (skb->protocol != htons(ETH_P_IP))
		goto e_inval;

	if (!ipv4_is_zeronet(saddr)) {
		err = fib_validate_source(skb, saddr, 0, tos, 0, dev,
					  in_dev, &itag);
		if (err < 0)
			goto martian_source;
	}
	flags |= RTCF_BROADCAST;
	res->type = RTN_BROADCAST;
	RT_CACHE_STAT_INC(in_brd);

local_input:
	do_cache = false;
	if (res->fi) {
		if (!itag) {
			rth = rcu_dereference(FIB_RES_NH(*res).nh_rth_input);
			if (rt_cache_valid(rth)) {
				skb_dst_set_noref(skb, &rth->dst);
				err = 0;
				goto out;
			}
			do_cache = true;
		}
	}

	rth = rt_dst_alloc(l3mdev_master_dev_rcu(dev) ? : net->loopback_dev,
			   flags | RTCF_LOCAL, res->type,
			   IN_DEV_CONF_GET(in_dev, NOPOLICY), false, do_cache);
	if (!rth)
		goto e_nobufs;

	rth->dst.output= ip_rt_bug;
#ifdef CONFIG_IP_ROUTE_CLASSID
	rth->dst.tclassid = itag;
#endif
	rth->rt_is_input = 1;

	RT_CACHE_STAT_INC(in_slow_tot);
	if (res->type == RTN_UNREACHABLE) {
		rth->dst.input= ip_error;
		rth->dst.error= -err;
		rth->rt_flags 	&= ~RTCF_LOCAL;
	}

	if (do_cache) {
		struct fib_nh *nh = &FIB_RES_NH(*res);

		rth->dst.lwtstate = lwtstate_get(nh->nh_lwtstate);
		if (lwtunnel_input_redirect(rth->dst.lwtstate)) {
			WARN_ON(rth->dst.input == lwtunnel_input);
			rth->dst.lwtstate->orig_input = rth->dst.input;
			rth->dst.input = lwtunnel_input;
		}

		if (unlikely(!rt_cache_route(nh, rth)))
			rt_add_uncached_list(rth);
	}
	skb_dst_set(skb, &rth->dst);
	err = 0;
	goto out;

no_route:
	RT_CACHE_STAT_INC(in_no_route);
	res->type = RTN_UNREACHABLE;
	res->fi = NULL;
	res->table = NULL;
	goto local_input;

	/*
	 *	Do not cache martian addresses: they should be logged (RFC1812)
	 */
martian_destination:
	RT_CACHE_STAT_INC(in_martian_dst);
#ifdef CONFIG_IP_ROUTE_VERBOSE
	if (IN_DEV_LOG_MARTIANS(in_dev))
		net_warn_ratelimited("martian destination %pI4 from %pI4, dev %s\n",
				     &daddr, &saddr, dev->name);
#endif

e_inval:
	err = -EINVAL;
	goto out;

e_nobufs:
	err = -ENOBUFS;
	goto out;

martian_source:
	ip_handle_martian_source(dev, in_dev, skb, daddr, saddr);
	goto out;
}

int ip_route_input_noref(struct sk_buff *skb, __be32 daddr, __be32 saddr,
			 u8 tos, struct net_device *dev)
{
	struct fib_result res;
	int err;

	tos &= IPTOS_RT_MASK;
	rcu_read_lock();
	err = ip_route_input_rcu(skb, daddr, saddr, tos, dev, &res);
	rcu_read_unlock();

	return err;
}
EXPORT_SYMBOL(ip_route_input_noref);

/* called with rcu_read_lock held */
int ip_route_input_rcu(struct sk_buff *skb, __be32 daddr, __be32 saddr,
		       u8 tos, struct net_device *dev, struct fib_result *res)
{
	/* Multicast recognition logic is moved from route cache to here.
	   The problem was that too many Ethernet cards have broken/missing
	   hardware multicast filters :-( As result the host on multicasting
	   network acquires a lot of useless route cache entries, sort of
	   SDR messages from all the world. Now we try to get rid of them.
	   Really, provided software IP multicast filter is organized
	   reasonably (at least, hashed), it does not result in a slowdown
	   comparing with route cache reject entries.
	   Note, that multicast routers are not affected, because
	   route cache entry is created eventually.
	 */
	if (ipv4_is_multicast(daddr)) {
		struct in_device *in_dev = __in_dev_get_rcu(dev);
		int our = 0;
		int err = -EINVAL;

		if (!in_dev)
			return err;
		our = ip_check_mc_rcu(in_dev, daddr, saddr,
				      ip_hdr(skb)->protocol);

		/* check l3 master if no match yet */
		if (!our && netif_is_l3_slave(dev)) {
			struct in_device *l3_in_dev;

			l3_in_dev = __in_dev_get_rcu(skb->dev);
			if (l3_in_dev)
				our = ip_check_mc_rcu(l3_in_dev, daddr, saddr,
						      ip_hdr(skb)->protocol);
		}

		if (our
#ifdef CONFIG_IP_MROUTE
			||
		    (!ipv4_is_local_multicast(daddr) &&
		     IN_DEV_MFORWARD(in_dev))
#endif
		   ) {
			err = ip_route_input_mc(skb, daddr, saddr,
						tos, dev, our);
		}
		return err;
	}

	return ip_route_input_slow(skb, daddr, saddr, tos, dev, res);
}

/* called with rcu_read_lock() */
static struct rtable *__mkroute_output(const struct fib_result *res,
				       const struct flowi4 *fl4, int orig_oif,
				       struct net_device *dev_out,
				       unsigned int flags)
{
	struct fib_info *fi = res->fi;
	struct fib_nh_exception *fnhe;
	struct in_device *in_dev;
	u16 type = res->type;
	struct rtable *rth;
	bool do_cache;

	in_dev = __in_dev_get_rcu(dev_out);
	if (!in_dev)
		return ERR_PTR(-EINVAL);

	if (likely(!IN_DEV_ROUTE_LOCALNET(in_dev)))
		if (ipv4_is_loopback(fl4->saddr) &&
		    !(dev_out->flags & IFF_LOOPBACK) &&
		    !netif_is_l3_master(dev_out))
			return ERR_PTR(-EINVAL);

	if (ipv4_is_lbcast(fl4->daddr))
		type = RTN_BROADCAST;
	else if (ipv4_is_multicast(fl4->daddr))
		type = RTN_MULTICAST;
	else if (ipv4_is_zeronet(fl4->daddr))
		return ERR_PTR(-EINVAL);

	if (dev_out->flags & IFF_LOOPBACK)
		flags |= RTCF_LOCAL;

	do_cache = true;
	if (type == RTN_BROADCAST) {
		flags |= RTCF_BROADCAST | RTCF_LOCAL;
		fi = NULL;
	} else if (type == RTN_MULTICAST) {
		flags |= RTCF_MULTICAST | RTCF_LOCAL;
		if (!ip_check_mc_rcu(in_dev, fl4->daddr, fl4->saddr,
				     fl4->flowi4_proto))
			flags &= ~RTCF_LOCAL;
		else
			do_cache = false;
		/* If multicast route do not exist use
		 * default one, but do not gateway in this case.
		 * Yes, it is hack.
		 */
		if (fi && res->prefixlen < 4)
			fi = NULL;
	} else if ((type == RTN_LOCAL) && (orig_oif != 0) &&
		   (orig_oif != dev_out->ifindex)) {
		/* For local routes that require a particular output interface
		 * we do not want to cache the result.  Caching the result
		 * causes incorrect behaviour when there are multiple source
		 * addresses on the interface, the end result being that if the
		 * intended recipient is waiting on that interface for the
		 * packet he won't receive it because it will be delivered on
		 * the loopback interface and the IP_PKTINFO ipi_ifindex will
		 * be set to the loopback interface as well.
		 */
		do_cache = false;
	}

	fnhe = NULL;
	do_cache &= fi != NULL;
	if (fi) {
		struct rtable __rcu **prth;
		struct fib_nh *nh = &FIB_RES_NH(*res);

		fnhe = find_exception(nh, fl4->daddr);
		if (!do_cache)
			goto add;
		if (fnhe) {
			prth = &fnhe->fnhe_rth_output;
		} else {
			if (unlikely(fl4->flowi4_flags &
				     FLOWI_FLAG_KNOWN_NH &&
				     !(nh->nh_gw &&
				       nh->nh_scope == RT_SCOPE_LINK))) {
				do_cache = false;
				goto add;
			}
			prth = raw_cpu_ptr(nh->nh_pcpu_rth_output);
		}
		rth = rcu_dereference(*prth);
		if (rt_cache_valid(rth) && dst_hold_safe(&rth->dst))
			return rth;
	}

add:
	rth = rt_dst_alloc(dev_out, flags, type,
			   IN_DEV_CONF_GET(in_dev, NOPOLICY),
			   IN_DEV_CONF_GET(in_dev, NOXFRM),
			   do_cache);
	if (!rth)
		return ERR_PTR(-ENOBUFS);

	rth->rt_iif = orig_oif;

	RT_CACHE_STAT_INC(out_slow_tot);

	if (flags & (RTCF_BROADCAST | RTCF_MULTICAST)) {
		if (flags & RTCF_LOCAL &&
		    !(dev_out->flags & IFF_LOOPBACK)) {
			rth->dst.output = ip_mc_output;
			RT_CACHE_STAT_INC(out_slow_mc);
		}
#ifdef CONFIG_IP_MROUTE
		if (type == RTN_MULTICAST) {
			if (IN_DEV_MFORWARD(in_dev) &&
			    !ipv4_is_local_multicast(fl4->daddr)) {
				rth->dst.input = ip_mr_input;
				rth->dst.output = ip_mc_output;
			}
		}
#endif
	}

	rt_set_nexthop(rth, fl4->daddr, res, fnhe, fi, type, 0, do_cache);
	lwtunnel_set_redirect(&rth->dst);

	return rth;
}

/*
 * Major route resolver routine.
 */

struct rtable *ip_route_output_key_hash(struct net *net, struct flowi4 *fl4,
					const struct sk_buff *skb)
{
	__u8 tos = RT_FL_TOS(fl4);
	struct fib_result res = {
		.type		= RTN_UNSPEC,
		.fi		= NULL,
		.table		= NULL,
		.tclassid	= 0,
	};
	struct rtable *rth;

	fl4->flowi4_iif = LOOPBACK_IFINDEX;
	fl4->flowi4_tos = tos & IPTOS_RT_MASK;
	fl4->flowi4_scope = ((tos & RTO_ONLINK) ?
			 RT_SCOPE_LINK : RT_SCOPE_UNIVERSE);

	rcu_read_lock();
	rth = ip_route_output_key_hash_rcu(net, fl4, &res, skb);
	rcu_read_unlock();

	return rth;
}
EXPORT_SYMBOL_GPL(ip_route_output_key_hash);

struct rtable *ip_route_output_key_hash_rcu(struct net *net, struct flowi4 *fl4,
					    struct fib_result *res,
					    const struct sk_buff *skb)
{
	struct net_device *dev_out = NULL;
	int orig_oif = fl4->flowi4_oif;
	unsigned int flags = 0;
	struct rtable *rth;
	int err = -ENETUNREACH;

	if (fl4->saddr) {
		rth = ERR_PTR(-EINVAL);
		if (ipv4_is_multicast(fl4->saddr) ||
		    ipv4_is_lbcast(fl4->saddr) ||
		    ipv4_is_zeronet(fl4->saddr))
			goto out;

		/* I removed check for oif == dev_out->oif here.
		   It was wrong for two reasons:
		   1. ip_dev_find(net, saddr) can return wrong iface, if saddr
		      is assigned to multiple interfaces.
		   2. Moreover, we are allowed to send packets with saddr
		      of another iface. --ANK
		 */

		if (fl4->flowi4_oif == 0 &&
		    (ipv4_is_multicast(fl4->daddr) ||
		     ipv4_is_lbcast(fl4->daddr))) {
			/* It is equivalent to inet_addr_type(saddr) == RTN_LOCAL */
			dev_out = __ip_dev_find(net, fl4->saddr, false);
			if (!dev_out)
				goto out;

			/* Special hack: user can direct multicasts
			   and limited broadcast via necessary interface
			   without fiddling with IP_MULTICAST_IF or IP_PKTINFO.
			   This hack is not just for fun, it allows
			   vic,vat and friends to work.
			   They bind socket to loopback, set ttl to zero
			   and expect that it will work.
			   From the viewpoint of routing cache they are broken,
			   because we are not allowed to build multicast path
			   with loopback source addr (look, routing cache
			   cannot know, that ttl is zero, so that packet
			   will not leave this host and route is valid).
			   Luckily, this hack is good workaround.
			 */

			fl4->flowi4_oif = dev_out->ifindex;
			goto make_route;
		}

		if (!(fl4->flowi4_flags & FLOWI_FLAG_ANYSRC)) {
			/* It is equivalent to inet_addr_type(saddr) == RTN_LOCAL */
			if (!__ip_dev_find(net, fl4->saddr, false))
				goto out;
		}
	}


	if (fl4->flowi4_oif) {
		dev_out = dev_get_by_index_rcu(net, fl4->flowi4_oif);
		rth = ERR_PTR(-ENODEV);
		if (!dev_out)
			goto out;

		/* RACE: Check return value of inet_select_addr instead. */
		if (!(dev_out->flags & IFF_UP) || !__in_dev_get_rcu(dev_out)) {
			rth = ERR_PTR(-ENETUNREACH);
			goto out;
		}
		if (ipv4_is_local_multicast(fl4->daddr) ||
		    ipv4_is_lbcast(fl4->daddr) ||
		    fl4->flowi4_proto == IPPROTO_IGMP) {
			if (!fl4->saddr)
				fl4->saddr = inet_select_addr(dev_out, 0,
							      RT_SCOPE_LINK);
			goto make_route;
		}
		if (!fl4->saddr) {
			if (ipv4_is_multicast(fl4->daddr))
				fl4->saddr = inet_select_addr(dev_out, 0,
							      fl4->flowi4_scope);
			else if (!fl4->daddr)
				fl4->saddr = inet_select_addr(dev_out, 0,
							      RT_SCOPE_HOST);
		}
	}

	if (!fl4->daddr) {
		fl4->daddr = fl4->saddr;
		if (!fl4->daddr)
			fl4->daddr = fl4->saddr = htonl(INADDR_LOOPBACK);
		dev_out = net->loopback_dev;
		fl4->flowi4_oif = LOOPBACK_IFINDEX;
		res->type = RTN_LOCAL;
		flags |= RTCF_LOCAL;
		goto make_route;
	}

	err = fib_lookup(net, fl4, res, 0);
	if (err) {
		res->fi = NULL;
		res->table = NULL;
		if (fl4->flowi4_oif &&
		    (ipv4_is_multicast(fl4->daddr) ||
		    !netif_index_is_l3_master(net, fl4->flowi4_oif))) {
			/* Apparently, routing tables are wrong. Assume,
			   that the destination is on link.

			   WHY? DW.
			   Because we are allowed to send to iface
			   even if it has NO routes and NO assigned
			   addresses. When oif is specified, routing
			   tables are looked up with only one purpose:
			   to catch if destination is gatewayed, rather than
			   direct. Moreover, if MSG_DONTROUTE is set,
			   we send packet, ignoring both routing tables
			   and ifaddr state. --ANK


			   We could make it even if oif is unknown,
			   likely IPv6, but we do not.
			 */

			if (fl4->saddr == 0)
				fl4->saddr = inet_select_addr(dev_out, 0,
							      RT_SCOPE_LINK);
			res->type = RTN_UNICAST;
			goto make_route;
		}
		rth = ERR_PTR(err);
		goto out;
	}

	if (res->type == RTN_LOCAL) {
		if (!fl4->saddr) {
			if (res->fi->fib_prefsrc)
				fl4->saddr = res->fi->fib_prefsrc;
			else
				fl4->saddr = fl4->daddr;
		}

		/* L3 master device is the loopback for that domain */
		dev_out = l3mdev_master_dev_rcu(FIB_RES_DEV(*res)) ? :
			net->loopback_dev;

		/* make sure orig_oif points to fib result device even
		 * though packet rx/tx happens over loopback or l3mdev
		 */
		orig_oif = FIB_RES_OIF(*res);

		fl4->flowi4_oif = dev_out->ifindex;
		flags |= RTCF_LOCAL;
		goto make_route;
	}

	fib_select_path(net, res, fl4, skb);

	dev_out = FIB_RES_DEV(*res);
	fl4->flowi4_oif = dev_out->ifindex;


make_route:
	rth = __mkroute_output(res, fl4, orig_oif, dev_out, flags);

out:
	return rth;
}

static struct dst_entry *ipv4_blackhole_dst_check(struct dst_entry *dst, u32 cookie)
{
	return NULL;
}

static unsigned int ipv4_blackhole_mtu(const struct dst_entry *dst)
{
	unsigned int mtu = dst_metric_raw(dst, RTAX_MTU);

	return mtu ? : dst->dev->mtu;
}

static void ipv4_rt_blackhole_update_pmtu(struct dst_entry *dst, struct sock *sk,
					  struct sk_buff *skb, u32 mtu)
{
}

static void ipv4_rt_blackhole_redirect(struct dst_entry *dst, struct sock *sk,
				       struct sk_buff *skb)
{
}

static u32 *ipv4_rt_blackhole_cow_metrics(struct dst_entry *dst,
					  unsigned long old)
{
	return NULL;
}

static struct dst_ops ipv4_dst_blackhole_ops = {
	.family			=	AF_INET,
	.check			=	ipv4_blackhole_dst_check,
	.mtu			=	ipv4_blackhole_mtu,
	.default_advmss		=	ipv4_default_advmss,
	.update_pmtu		=	ipv4_rt_blackhole_update_pmtu,
	.redirect		=	ipv4_rt_blackhole_redirect,
	.cow_metrics		=	ipv4_rt_blackhole_cow_metrics,
	.neigh_lookup		=	ipv4_neigh_lookup,
};

struct dst_entry *ipv4_blackhole_route(struct net *net, struct dst_entry *dst_orig)
{
	struct rtable *ort = (struct rtable *) dst_orig;
	struct rtable *rt;

	rt = dst_alloc(&ipv4_dst_blackhole_ops, NULL, 1, DST_OBSOLETE_DEAD, 0);
	if (rt) {
		struct dst_entry *new = &rt->dst;

		new->__use = 1;
		new->input = dst_discard;
		new->output = dst_discard_out;

		new->dev = net->loopback_dev;
		if (new->dev)
			dev_hold(new->dev);

		rt->rt_is_input = ort->rt_is_input;
		rt->rt_iif = ort->rt_iif;
		rt->rt_pmtu = ort->rt_pmtu;
		rt->rt_mtu_locked = ort->rt_mtu_locked;

		rt->rt_genid = rt_genid_ipv4(net);
		rt->rt_flags = ort->rt_flags;
		rt->rt_type = ort->rt_type;
		rt->rt_gateway = ort->rt_gateway;
		rt->rt_uses_gateway = ort->rt_uses_gateway;

		INIT_LIST_HEAD(&rt->rt_uncached);
	}

	dst_release(dst_orig);

	return rt ? &rt->dst : ERR_PTR(-ENOMEM);
}

struct rtable *ip_route_output_flow(struct net *net, struct flowi4 *flp4,
				    const struct sock *sk)
{
	struct rtable *rt = __ip_route_output_key(net, flp4);

	if (IS_ERR(rt))
		return rt;

	if (flp4->flowi4_proto)
		rt = (struct rtable *)xfrm_lookup_route(net, &rt->dst,
							flowi4_to_flowi(flp4),
							sk, 0);

	return rt;
}
EXPORT_SYMBOL_GPL(ip_route_output_flow);

/* called with rcu_read_lock held */
static int rt_fill_info(struct net *net, __be32 dst, __be32 src,
			struct rtable *rt, u32 table_id, struct flowi4 *fl4,
			struct sk_buff *skb, u32 portid, u32 seq)
{
	struct rtmsg *r;
	struct nlmsghdr *nlh;
	unsigned long expires = 0;
	u32 error;
	u32 metrics[RTAX_MAX];

	nlh = nlmsg_put(skb, portid, seq, RTM_NEWROUTE, sizeof(*r), 0);
	if (!nlh)
		return -EMSGSIZE;

	r = nlmsg_data(nlh);
	r->rtm_family	 = AF_INET;
	r->rtm_dst_len	= 32;
	r->rtm_src_len	= 0;
	r->rtm_tos	= fl4->flowi4_tos;
	r->rtm_table	= table_id < 256 ? table_id : RT_TABLE_COMPAT;
	if (nla_put_u32(skb, RTA_TABLE, table_id))
		goto nla_put_failure;
	r->rtm_type	= rt->rt_type;
	r->rtm_scope	= RT_SCOPE_UNIVERSE;
	r->rtm_protocol = RTPROT_UNSPEC;
	r->rtm_flags	= (rt->rt_flags & ~0xFFFF) | RTM_F_CLONED;
	if (rt->rt_flags & RTCF_NOTIFY)
		r->rtm_flags |= RTM_F_NOTIFY;
	if (IPCB(skb)->flags & IPSKB_DOREDIRECT)
		r->rtm_flags |= RTCF_DOREDIRECT;

	if (nla_put_in_addr(skb, RTA_DST, dst))
		goto nla_put_failure;
	if (src) {
		r->rtm_src_len = 32;
		if (nla_put_in_addr(skb, RTA_SRC, src))
			goto nla_put_failure;
	}
	if (rt->dst.dev &&
	    nla_put_u32(skb, RTA_OIF, rt->dst.dev->ifindex))
		goto nla_put_failure;
#ifdef CONFIG_IP_ROUTE_CLASSID
	if (rt->dst.tclassid &&
	    nla_put_u32(skb, RTA_FLOW, rt->dst.tclassid))
		goto nla_put_failure;
#endif
	if (!rt_is_input_route(rt) &&
	    fl4->saddr != src) {
		if (nla_put_in_addr(skb, RTA_PREFSRC, fl4->saddr))
			goto nla_put_failure;
	}
	if (rt->rt_uses_gateway &&
	    nla_put_in_addr(skb, RTA_GATEWAY, rt->rt_gateway))
		goto nla_put_failure;

	expires = rt->dst.expires;
	if (expires) {
		unsigned long now = jiffies;

		if (time_before(now, expires))
			expires -= now;
		else
			expires = 0;
	}

	memcpy(metrics, dst_metrics_ptr(&rt->dst), sizeof(metrics));
	if (rt->rt_pmtu && expires)
		metrics[RTAX_MTU - 1] = rt->rt_pmtu;
	if (rt->rt_mtu_locked && expires)
		metrics[RTAX_LOCK - 1] |= BIT(RTAX_MTU);
	if (rtnetlink_put_metrics(skb, metrics) < 0)
		goto nla_put_failure;

	if (fl4->flowi4_mark &&
	    nla_put_u32(skb, RTA_MARK, fl4->flowi4_mark))
		goto nla_put_failure;

	if (!uid_eq(fl4->flowi4_uid, INVALID_UID) &&
	    nla_put_u32(skb, RTA_UID,
			from_kuid_munged(current_user_ns(), fl4->flowi4_uid)))
		goto nla_put_failure;

	error = rt->dst.error;

	if (rt_is_input_route(rt)) {
#ifdef CONFIG_IP_MROUTE
		if (ipv4_is_multicast(dst) && !ipv4_is_local_multicast(dst) &&
		    IPV4_DEVCONF_ALL(net, MC_FORWARDING)) {
			int err = ipmr_get_route(net, skb,
						 fl4->saddr, fl4->daddr,
						 r, portid);

			if (err <= 0) {
				if (err == 0)
					return 0;
				goto nla_put_failure;
			}
		} else
#endif
			if (nla_put_u32(skb, RTA_IIF, fl4->flowi4_iif))
				goto nla_put_failure;
	}

	if (rtnl_put_cacheinfo(skb, &rt->dst, 0, expires, error) < 0)
		goto nla_put_failure;

	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static struct sk_buff *inet_rtm_getroute_build_skb(__be32 src, __be32 dst,
						   u8 ip_proto, __be16 sport,
						   __be16 dport)
{
	struct sk_buff *skb;
	struct iphdr *iph;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return NULL;

	/* Reserve room for dummy headers, this skb can pass
	 * through good chunk of routing engine.
	 */
	skb_reset_mac_header(skb);
	skb_reset_network_header(skb);
	skb->protocol = htons(ETH_P_IP);
	iph = skb_put(skb, sizeof(struct iphdr));
	iph->protocol = ip_proto;
	iph->saddr = src;
	iph->daddr = dst;
	iph->version = 0x4;
	iph->frag_off = 0;
	iph->ihl = 0x5;
	skb_set_transport_header(skb, skb->len);

	switch (iph->protocol) {
	case IPPROTO_UDP: {
		struct udphdr *udph;

		udph = skb_put_zero(skb, sizeof(struct udphdr));
		udph->source = sport;
		udph->dest = dport;
		udph->len = sizeof(struct udphdr);
		udph->check = 0;
		break;
	}
	case IPPROTO_TCP: {
		struct tcphdr *tcph;

		tcph = skb_put_zero(skb, sizeof(struct tcphdr));
		tcph->source	= sport;
		tcph->dest	= dport;
		tcph->doff	= sizeof(struct tcphdr) / 4;
		tcph->rst = 1;
		tcph->check = ~tcp_v4_check(sizeof(struct tcphdr),
					    src, dst, 0);
		break;
	}
	case IPPROTO_ICMP: {
		struct icmphdr *icmph;

		icmph = skb_put_zero(skb, sizeof(struct icmphdr));
		icmph->type = ICMP_ECHO;
		icmph->code = 0;
	}
	}

	return skb;
}

static int inet_rtm_valid_getroute_req(struct sk_buff *skb,
				       const struct nlmsghdr *nlh,
				       struct nlattr **tb,
				       struct netlink_ext_ack *extack)
{
	struct rtmsg *rtm;
	int i, err;

	if (nlh->nlmsg_len < nlmsg_msg_size(sizeof(*rtm))) {
		NL_SET_ERR_MSG(extack,
			       "ipv4: Invalid header for route get request");
		return -EINVAL;
	}

	if (!netlink_strict_get_check(skb))
		return nlmsg_parse(nlh, sizeof(*rtm), tb, RTA_MAX,
				   rtm_ipv4_policy, extack);

	rtm = nlmsg_data(nlh);
	if ((rtm->rtm_src_len && rtm->rtm_src_len != 32) ||
	    (rtm->rtm_dst_len && rtm->rtm_dst_len != 32) ||
	    rtm->rtm_table || rtm->rtm_protocol ||
	    rtm->rtm_scope || rtm->rtm_type) {
		NL_SET_ERR_MSG(extack, "ipv4: Invalid values in header for route get request");
		return -EINVAL;
	}

	if (rtm->rtm_flags & ~(RTM_F_NOTIFY |
			       RTM_F_LOOKUP_TABLE |
			       RTM_F_FIB_MATCH)) {
		NL_SET_ERR_MSG(extack, "ipv4: Unsupported rtm_flags for route get request");
		return -EINVAL;
	}

	err = nlmsg_parse_strict(nlh, sizeof(*rtm), tb, RTA_MAX,
				 rtm_ipv4_policy, extack);
	if (err)
		return err;

	if ((tb[RTA_SRC] && !rtm->rtm_src_len) ||
	    (tb[RTA_DST] && !rtm->rtm_dst_len)) {
		NL_SET_ERR_MSG(extack, "ipv4: rtm_src_len and rtm_dst_len must be 32 for IPv4");
		return -EINVAL;
	}

	for (i = 0; i <= RTA_MAX; i++) {
		if (!tb[i])
			continue;

		switch (i) {
		case RTA_IIF:
		case RTA_OIF:
		case RTA_SRC:
		case RTA_DST:
		case RTA_IP_PROTO:
		case RTA_SPORT:
		case RTA_DPORT:
		case RTA_MARK:
		case RTA_UID:
			break;
		default:
			NL_SET_ERR_MSG(extack, "ipv4: Unsupported attribute in route get request");
			return -EINVAL;
		}
	}

	return 0;
}

static int inet_rtm_getroute(struct sk_buff *in_skb, struct nlmsghdr *nlh,
			     struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(in_skb->sk);
	struct nlattr *tb[RTA_MAX+1];
	u32 table_id = RT_TABLE_MAIN;
	__be16 sport = 0, dport = 0;
	struct fib_result res = {};
	u8 ip_proto = IPPROTO_UDP;
	struct rtable *rt = NULL;
	struct sk_buff *skb;
	struct rtmsg *rtm;
	struct flowi4 fl4 = {};
	__be32 dst = 0;
	__be32 src = 0;
	kuid_t uid;
	u32 iif;
	int err;
	int mark;

	err = inet_rtm_valid_getroute_req(in_skb, nlh, tb, extack);
	if (err < 0)
		return err;

	rtm = nlmsg_data(nlh);
	src = tb[RTA_SRC] ? nla_get_in_addr(tb[RTA_SRC]) : 0;
	dst = tb[RTA_DST] ? nla_get_in_addr(tb[RTA_DST]) : 0;
	iif = tb[RTA_IIF] ? nla_get_u32(tb[RTA_IIF]) : 0;
	mark = tb[RTA_MARK] ? nla_get_u32(tb[RTA_MARK]) : 0;
	if (tb[RTA_UID])
		uid = make_kuid(current_user_ns(), nla_get_u32(tb[RTA_UID]));
	else
		uid = (iif ? INVALID_UID : current_uid());

	if (tb[RTA_IP_PROTO]) {
		err = rtm_getroute_parse_ip_proto(tb[RTA_IP_PROTO],
						  &ip_proto, AF_INET, extack);
		if (err)
			return err;
	}

	if (tb[RTA_SPORT])
		sport = nla_get_be16(tb[RTA_SPORT]);

	if (tb[RTA_DPORT])
		dport = nla_get_be16(tb[RTA_DPORT]);

	skb = inet_rtm_getroute_build_skb(src, dst, ip_proto, sport, dport);
	if (!skb)
		return -ENOBUFS;

	fl4.daddr = dst;
	fl4.saddr = src;
	fl4.flowi4_tos = rtm->rtm_tos;
	fl4.flowi4_oif = tb[RTA_OIF] ? nla_get_u32(tb[RTA_OIF]) : 0;
	fl4.flowi4_mark = mark;
	fl4.flowi4_uid = uid;
	if (sport)
		fl4.fl4_sport = sport;
	if (dport)
		fl4.fl4_dport = dport;
	fl4.flowi4_proto = ip_proto;

	rcu_read_lock();

	if (iif) {
		struct net_device *dev;

		dev = dev_get_by_index_rcu(net, iif);
		if (!dev) {
			err = -ENODEV;
			goto errout_rcu;
		}

		fl4.flowi4_iif = iif; /* for rt_fill_info */
		skb->dev	= dev;
		skb->mark	= mark;
		err = ip_route_input_rcu(skb, dst, src, rtm->rtm_tos,
					 dev, &res);

		rt = skb_rtable(skb);
		if (err == 0 && rt->dst.error)
			err = -rt->dst.error;
	} else {
		fl4.flowi4_iif = LOOPBACK_IFINDEX;
		skb->dev = net->loopback_dev;
		rt = ip_route_output_key_hash_rcu(net, &fl4, &res, skb);
		err = 0;
		if (IS_ERR(rt))
			err = PTR_ERR(rt);
		else
			skb_dst_set(skb, &rt->dst);
	}

	if (err)
		goto errout_rcu;

	if (rtm->rtm_flags & RTM_F_NOTIFY)
		rt->rt_flags |= RTCF_NOTIFY;

	if (rtm->rtm_flags & RTM_F_LOOKUP_TABLE)
		table_id = res.table ? res.table->tb_id : 0;

	/* reset skb for netlink reply msg */
	skb_trim(skb, 0);
	skb_reset_network_header(skb);
	skb_reset_transport_header(skb);
	skb_reset_mac_header(skb);

	if (rtm->rtm_flags & RTM_F_FIB_MATCH) {
		if (!res.fi) {
			err = fib_props[res.type].error;
			if (!err)
				err = -EHOSTUNREACH;
			goto errout_rcu;
		}
		err = fib_dump_info(skb, NETLINK_CB(in_skb).portid,
				    nlh->nlmsg_seq, RTM_NEWROUTE, table_id,
				    rt->rt_type, res.prefix, res.prefixlen,
				    fl4.flowi4_tos, res.fi, 0);
	} else {
		err = rt_fill_info(net, dst, src, rt, table_id, &fl4, skb,
				   NETLINK_CB(in_skb).portid, nlh->nlmsg_seq);
	}
	if (err < 0)
		goto errout_rcu;

	rcu_read_unlock();

	err = rtnl_unicast(skb, net, NETLINK_CB(in_skb).portid);

errout_free:
	return err;
errout_rcu:
	rcu_read_unlock();
	kfree_skb(skb);
	goto errout_free;
}

void ip_rt_multicast_event(struct in_device *in_dev)
{
	rt_cache_flush(dev_net(in_dev->dev));
}

#ifdef CONFIG_SYSCTL
static int ip_rt_gc_interval __read_mostly  = 60 * HZ;
static int ip_rt_gc_min_interval __read_mostly	= HZ / 2;
static int ip_rt_gc_elasticity __read_mostly	= 8;
static int ip_min_valid_pmtu __read_mostly	= IPV4_MIN_MTU;

static int ipv4_sysctl_rtcache_flush(struct ctl_table *__ctl, int write,
					void __user *buffer,
					size_t *lenp, loff_t *ppos)
{
	struct net *net = (struct net *)__ctl->extra1;

	if (write) {
		rt_cache_flush(net);
		fnhe_genid_bump(net);
		return 0;
	}

	return -EINVAL;
}

static struct ctl_table ipv4_route_table[] = {
	{
		.procname	= "gc_thresh",
		.data		= &ipv4_dst_ops.gc_thresh,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "max_size",
		.data		= &ip_rt_max_size,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		/*  Deprecated. Use gc_min_interval_ms */

		.procname	= "gc_min_interval",
		.data		= &ip_rt_gc_min_interval,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{
		.procname	= "gc_min_interval_ms",
		.data		= &ip_rt_gc_min_interval,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_ms_jiffies,
	},
	{
		.procname	= "gc_timeout",
		.data		= &ip_rt_gc_timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{
		.procname	= "gc_interval",
		.data		= &ip_rt_gc_interval,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{
		.procname	= "redirect_load",
		.data		= &ip_rt_redirect_load,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "redirect_number",
		.data		= &ip_rt_redirect_number,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "redirect_silence",
		.data		= &ip_rt_redirect_silence,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "error_cost",
		.data		= &ip_rt_error_cost,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "error_burst",
		.data		= &ip_rt_error_burst,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "gc_elasticity",
		.data		= &ip_rt_gc_elasticity,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "mtu_expires",
		.data		= &ip_rt_mtu_expires,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{
		.procname	= "min_pmtu",
		.data		= &ip_rt_min_pmtu,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &ip_min_valid_pmtu,
	},
	{
		.procname	= "min_adv_mss",
		.data		= &ip_rt_min_advmss,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{ }
};

static struct ctl_table ipv4_route_flush_table[] = {
	{
		.procname	= "flush",
		.maxlen		= sizeof(int),
		.mode		= 0200,
		.proc_handler	= ipv4_sysctl_rtcache_flush,
	},
	{ },
};

static __net_init int sysctl_route_net_init(struct net *net)
{
	struct ctl_table *tbl;

	tbl = ipv4_route_flush_table;
	if (!net_eq(net, &init_net)) {
		tbl = kmemdup(tbl, sizeof(ipv4_route_flush_table), GFP_KERNEL);
		if (!tbl)
			goto err_dup;

		/* Don't export sysctls to unprivileged users */
		if (net->user_ns != &init_user_ns)
			tbl[0].procname = NULL;
	}
	tbl[0].extra1 = net;

	net->ipv4.route_hdr = register_net_sysctl(net, "net/ipv4/route", tbl);
	if (!net->ipv4.route_hdr)
		goto err_reg;
	return 0;

err_reg:
	if (tbl != ipv4_route_flush_table)
		kfree(tbl);
err_dup:
	return -ENOMEM;
}

static __net_exit void sysctl_route_net_exit(struct net *net)
{
	struct ctl_table *tbl;

	tbl = net->ipv4.route_hdr->ctl_table_arg;
	unregister_net_sysctl_table(net->ipv4.route_hdr);
	BUG_ON(tbl == ipv4_route_flush_table);
	kfree(tbl);
}

static __net_initdata struct pernet_operations sysctl_route_ops = {
	.init = sysctl_route_net_init,
	.exit = sysctl_route_net_exit,
};
#endif

static __net_init int rt_genid_init(struct net *net)
{
	atomic_set(&net->ipv4.rt_genid, 0);
	atomic_set(&net->fnhe_genid, 0);
	atomic_set(&net->ipv4.dev_addr_genid, get_random_int());
	return 0;
}

static __net_initdata struct pernet_operations rt_genid_ops = {
	.init = rt_genid_init,
};

static int __net_init ipv4_inetpeer_init(struct net *net)
{
	struct inet_peer_base *bp = kmalloc(sizeof(*bp), GFP_KERNEL);

	if (!bp)
		return -ENOMEM;
	inet_peer_base_init(bp);
	net->ipv4.peers = bp;
	return 0;
}

static void __net_exit ipv4_inetpeer_exit(struct net *net)
{
	struct inet_peer_base *bp = net->ipv4.peers;

	net->ipv4.peers = NULL;
	inetpeer_invalidate_tree(bp);
	kfree(bp);
}

static __net_initdata struct pernet_operations ipv4_inetpeer_ops = {
	.init	=	ipv4_inetpeer_init,
	.exit	=	ipv4_inetpeer_exit,
};

#ifdef CONFIG_IP_ROUTE_CLASSID
struct ip_rt_acct __percpu *ip_rt_acct __read_mostly;
#endif /* CONFIG_IP_ROUTE_CLASSID */

int __init ip_rt_init(void)
{
	int cpu;

	ip_idents = kmalloc_array(IP_IDENTS_SZ, sizeof(*ip_idents),
				  GFP_KERNEL);
	if (!ip_idents)
		panic("IP: failed to allocate ip_idents\n");

	prandom_bytes(ip_idents, IP_IDENTS_SZ * sizeof(*ip_idents));

	ip_tstamps = kcalloc(IP_IDENTS_SZ, sizeof(*ip_tstamps), GFP_KERNEL);
	if (!ip_tstamps)
		panic("IP: failed to allocate ip_tstamps\n");

	for_each_possible_cpu(cpu) {
		struct uncached_list *ul = &per_cpu(rt_uncached_list, cpu);

		INIT_LIST_HEAD(&ul->head);
		spin_lock_init(&ul->lock);
	}
#ifdef CONFIG_IP_ROUTE_CLASSID
	ip_rt_acct = __alloc_percpu(256 * sizeof(struct ip_rt_acct), __alignof__(struct ip_rt_acct));
	if (!ip_rt_acct)
		panic("IP: failed to allocate ip_rt_acct\n");
#endif

	ipv4_dst_ops.kmem_cachep =
		kmem_cache_create("ip_dst_cache", sizeof(struct rtable), 0,
				  SLAB_HWCACHE_ALIGN|SLAB_PANIC, NULL);

	ipv4_dst_blackhole_ops.kmem_cachep = ipv4_dst_ops.kmem_cachep;

	if (dst_entries_init(&ipv4_dst_ops) < 0)
		panic("IP: failed to allocate ipv4_dst_ops counter\n");

	if (dst_entries_init(&ipv4_dst_blackhole_ops) < 0)
		panic("IP: failed to allocate ipv4_dst_blackhole_ops counter\n");

	ipv4_dst_ops.gc_thresh = ~0;
	ip_rt_max_size = INT_MAX;

	devinet_init();
	ip_fib_init();

	if (ip_rt_proc_init())
		pr_err("Unable to create route proc files\n");
#ifdef CONFIG_XFRM
	xfrm_init();
	xfrm4_init();
#endif
	rtnl_register(PF_INET, RTM_GETROUTE, inet_rtm_getroute, NULL,
		      RTNL_FLAG_DOIT_UNLOCKED);

#ifdef CONFIG_SYSCTL
	register_pernet_subsys(&sysctl_route_ops);
#endif
	register_pernet_subsys(&rt_genid_ops);
	register_pernet_subsys(&ipv4_inetpeer_ops);
	return 0;
}

#ifdef CONFIG_SYSCTL
/*
 * We really need to sanitize the damn ipv4 init order, then all
 * this nonsense will go away.
 */
void __init ip_static_sysctl_init(void)
{
	register_net_sysctl(&init_net, "net/ipv4/route", ipv4_route_table);
}
#endif
