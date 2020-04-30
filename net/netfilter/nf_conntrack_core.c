/* Connection state tracking for netfilter.  This is separated from,
   but required by, the NAT layer; it can also be used by an iptables
   extension. */

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 * (C) 2003,2004 USAGI/WIDE Project <http://www.linux-ipv6.org>
 * (C) 2005-2012 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/netfilter.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <linux/stddef.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/jhash.h>
#include <linux/siphash.h>
#include <linux/err.h>
#include <linux/percpu.h>
#include <linux/moduleparam.h>
#include <linux/notifier.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/socket.h>
#include <linux/mm.h>
#include <linux/nsproxy.h>
#include <linux/rculist_nulls.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_seqadj.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_extend.h>
#include <net/netfilter/nf_conntrack_acct.h>
#include <net/netfilter/nf_conntrack_ecache.h>
#include <net/netfilter/nf_conntrack_zones.h>
#include <net/netfilter/nf_conntrack_timestamp.h>
#include <net/netfilter/nf_conntrack_timeout.h>
#include <net/netfilter/nf_conntrack_labels.h>
#include <net/netfilter/nf_conntrack_synproxy.h>
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_core.h>
#include <net/netfilter/nf_nat_helper.h>
#include <net/netns/hash.h>
#include <net/ip.h>

#include "nf_internals.h"

__cacheline_aligned_in_smp spinlock_t nf_conntrack_locks[CONNTRACK_LOCKS];
EXPORT_SYMBOL_GPL(nf_conntrack_locks);

__cacheline_aligned_in_smp DEFINE_SPINLOCK(nf_conntrack_expect_lock);
EXPORT_SYMBOL_GPL(nf_conntrack_expect_lock);

struct hlist_nulls_head *nf_conntrack_hash __read_mostly;
EXPORT_SYMBOL_GPL(nf_conntrack_hash);

struct conntrack_gc_work {
	struct delayed_work	dwork;
	u32			last_bucket;
	bool			exiting;
	bool			early_drop;
	long			next_gc_run;
};

static __read_mostly struct kmem_cache *nf_conntrack_cachep;
static __read_mostly spinlock_t nf_conntrack_locks_all_lock;
static __read_mostly DEFINE_SPINLOCK(nf_conntrack_locks_all_lock);
static __read_mostly bool nf_conntrack_locks_all;

/* every gc cycle scans at most 1/GC_MAX_BUCKETS_DIV part of table */
#define GC_MAX_BUCKETS_DIV	128u
/* upper bound of full table scan */
#define GC_MAX_SCAN_JIFFIES	(16u * HZ)
/* desired ratio of entries found to be expired */
#define GC_EVICT_RATIO	50u

static struct conntrack_gc_work conntrack_gc_work;

void nf_conntrack_lock(spinlock_t *lock) __acquires(lock)
{
	/* 1) Acquire the lock */
	spin_lock(lock);

	/* 2) read nf_conntrack_locks_all, with ACQUIRE semantics
	 * It pairs with the smp_store_release() in nf_conntrack_all_unlock()
	 */
	if (likely(smp_load_acquire(&nf_conntrack_locks_all) == false))
		return;

	/* fast path failed, unlock */
	spin_unlock(lock);

	/* Slow path 1) get global lock */
	spin_lock(&nf_conntrack_locks_all_lock);

	/* Slow path 2) get the lock we want */
	spin_lock(lock);

	/* Slow path 3) release the global lock */
	spin_unlock(&nf_conntrack_locks_all_lock);
}
EXPORT_SYMBOL_GPL(nf_conntrack_lock);

static void nf_conntrack_double_unlock(unsigned int h1, unsigned int h2)
{
	h1 %= CONNTRACK_LOCKS;
	h2 %= CONNTRACK_LOCKS;
	spin_unlock(&nf_conntrack_locks[h1]);
	if (h1 != h2)
		spin_unlock(&nf_conntrack_locks[h2]);
}

/* return true if we need to recompute hashes (in case hash table was resized) */
static bool nf_conntrack_double_lock(struct net *net, unsigned int h1,
				     unsigned int h2, unsigned int sequence)
{
	h1 %= CONNTRACK_LOCKS;
	h2 %= CONNTRACK_LOCKS;
	if (h1 <= h2) {
		nf_conntrack_lock(&nf_conntrack_locks[h1]);
		if (h1 != h2)
			spin_lock_nested(&nf_conntrack_locks[h2],
					 SINGLE_DEPTH_NESTING);
	} else {
		nf_conntrack_lock(&nf_conntrack_locks[h2]);
		spin_lock_nested(&nf_conntrack_locks[h1],
				 SINGLE_DEPTH_NESTING);
	}
	if (read_seqcount_retry(&nf_conntrack_generation, sequence)) {
		nf_conntrack_double_unlock(h1, h2);
		return true;
	}
	return false;
}

static void nf_conntrack_all_lock(void)
{
	int i;

	spin_lock(&nf_conntrack_locks_all_lock);

	nf_conntrack_locks_all = true;

	for (i = 0; i < CONNTRACK_LOCKS; i++) {
		spin_lock(&nf_conntrack_locks[i]);

		/* This spin_unlock provides the "release" to ensure that
		 * nf_conntrack_locks_all==true is visible to everyone that
		 * acquired spin_lock(&nf_conntrack_locks[]).
		 */
		spin_unlock(&nf_conntrack_locks[i]);
	}
}

static void nf_conntrack_all_unlock(void)
{
	/* All prior stores must be complete before we clear
	 * 'nf_conntrack_locks_all'. Otherwise nf_conntrack_lock()
	 * might observe the false value but not the entire
	 * critical section.
	 * It pairs with the smp_load_acquire() in nf_conntrack_lock()
	 */
	smp_store_release(&nf_conntrack_locks_all, false);
	spin_unlock(&nf_conntrack_locks_all_lock);
}

unsigned int nf_conntrack_htable_size __read_mostly;
EXPORT_SYMBOL_GPL(nf_conntrack_htable_size);

unsigned int nf_conntrack_max __read_mostly;
EXPORT_SYMBOL_GPL(nf_conntrack_max);
seqcount_t nf_conntrack_generation __read_mostly;
static unsigned int nf_conntrack_hash_rnd __read_mostly;

static u32 hash_conntrack_raw(const struct nf_conntrack_tuple *tuple,
			      const struct net *net)
{
	unsigned int n;
	u32 seed;

	get_random_once(&nf_conntrack_hash_rnd, sizeof(nf_conntrack_hash_rnd));

	/* The direction must be ignored, so we hash everything up to the
	 * destination ports (which is a multiple of 4) and treat the last
	 * three bytes manually.
	 */
	seed = nf_conntrack_hash_rnd ^ net_hash_mix(net);
	n = (sizeof(tuple->src) + sizeof(tuple->dst.u3)) / sizeof(u32);
	return jhash2((u32 *)tuple, n, seed ^
		      (((__force __u16)tuple->dst.u.all << 16) |
		      tuple->dst.protonum));
}

static u32 scale_hash(u32 hash)
{
	return reciprocal_scale(hash, nf_conntrack_htable_size);
}

static u32 __hash_conntrack(const struct net *net,
			    const struct nf_conntrack_tuple *tuple,
			    unsigned int size)
{
	return reciprocal_scale(hash_conntrack_raw(tuple, net), size);
}

static u32 hash_conntrack(const struct net *net,
			  const struct nf_conntrack_tuple *tuple)
{
	return scale_hash(hash_conntrack_raw(tuple, net));
}

static bool
nf_ct_get_tuple(const struct sk_buff *skb,
		unsigned int nhoff,
		unsigned int dataoff,
		u_int16_t l3num,
		u_int8_t protonum,
		struct net *net,
		struct nf_conntrack_tuple *tuple,
		const struct nf_conntrack_l4proto *l4proto)
{
	unsigned int size;
	const __be32 *ap;
	__be32 _addrs[8];
	struct {
		__be16 sport;
		__be16 dport;
	} _inet_hdr, *inet_hdr;

	memset(tuple, 0, sizeof(*tuple));

	tuple->src.l3num = l3num;
	switch (l3num) {
	case NFPROTO_IPV4:
		nhoff += offsetof(struct iphdr, saddr);
		size = 2 * sizeof(__be32);
		break;
	case NFPROTO_IPV6:
		nhoff += offsetof(struct ipv6hdr, saddr);
		size = sizeof(_addrs);
		break;
	default:
		return true;
	}

	ap = skb_header_pointer(skb, nhoff, size, _addrs);
	if (!ap)
		return false;

	switch (l3num) {
	case NFPROTO_IPV4:
		tuple->src.u3.ip = ap[0];
		tuple->dst.u3.ip = ap[1];
		break;
	case NFPROTO_IPV6:
		memcpy(tuple->src.u3.ip6, ap, sizeof(tuple->src.u3.ip6));
		memcpy(tuple->dst.u3.ip6, ap + 4, sizeof(tuple->dst.u3.ip6));
		break;
	}

	tuple->dst.protonum = protonum;
	tuple->dst.dir = IP_CT_DIR_ORIGINAL;

	if (unlikely(l4proto->pkt_to_tuple))
		return l4proto->pkt_to_tuple(skb, dataoff, net, tuple);

	/* Actually only need first 4 bytes to get ports. */
	inet_hdr = skb_header_pointer(skb, dataoff, sizeof(_inet_hdr), &_inet_hdr);
	if (!inet_hdr)
		return false;

	tuple->src.u.udp.port = inet_hdr->sport;
	tuple->dst.u.udp.port = inet_hdr->dport;
	return true;
}

static int ipv4_get_l4proto(const struct sk_buff *skb, unsigned int nhoff,
			    u_int8_t *protonum)
{
	int dataoff = -1;
	const struct iphdr *iph;
	struct iphdr _iph;

	iph = skb_header_pointer(skb, nhoff, sizeof(_iph), &_iph);
	if (!iph)
		return -1;

	/* Conntrack defragments packets, we might still see fragments
	 * inside ICMP packets though.
	 */
	if (iph->frag_off & htons(IP_OFFSET))
		return -1;

	dataoff = nhoff + (iph->ihl << 2);
	*protonum = iph->protocol;

	/* Check bogus IP headers */
	if (dataoff > skb->len) {
		pr_debug("bogus IPv4 packet: nhoff %u, ihl %u, skblen %u\n",
			 nhoff, iph->ihl << 2, skb->len);
		return -1;
	}
	return dataoff;
}

#if IS_ENABLED(CONFIG_IPV6)
static int ipv6_get_l4proto(const struct sk_buff *skb, unsigned int nhoff,
			    u8 *protonum)
{
	int protoff = -1;
	unsigned int extoff = nhoff + sizeof(struct ipv6hdr);
	__be16 frag_off;
	u8 nexthdr;

	if (skb_copy_bits(skb, nhoff + offsetof(struct ipv6hdr, nexthdr),
			  &nexthdr, sizeof(nexthdr)) != 0) {
		pr_debug("can't get nexthdr\n");
		return -1;
	}
	protoff = ipv6_skip_exthdr(skb, extoff, &nexthdr, &frag_off);
	/*
	 * (protoff == skb->len) means the packet has not data, just
	 * IPv6 and possibly extensions headers, but it is tracked anyway
	 */
	if (protoff < 0 || (frag_off & htons(~0x7)) != 0) {
		pr_debug("can't find proto in pkt\n");
		return -1;
	}

	*protonum = nexthdr;
	return protoff;
}
#endif

static int get_l4proto(const struct sk_buff *skb,
		       unsigned int nhoff, u8 pf, u8 *l4num)
{
	switch (pf) {
	case NFPROTO_IPV4:
		return ipv4_get_l4proto(skb, nhoff, l4num);
#if IS_ENABLED(CONFIG_IPV6)
	case NFPROTO_IPV6:
		return ipv6_get_l4proto(skb, nhoff, l4num);
#endif
	default:
		*l4num = 0;
		break;
	}
	return -1;
}

bool nf_ct_get_tuplepr(const struct sk_buff *skb, unsigned int nhoff,
		       u_int16_t l3num,
		       struct net *net, struct nf_conntrack_tuple *tuple)
{
	const struct nf_conntrack_l4proto *l4proto;
	u8 protonum;
	int protoff;
	int ret;

	rcu_read_lock();

	protoff = get_l4proto(skb, nhoff, l3num, &protonum);
	if (protoff <= 0) {
		rcu_read_unlock();
		return false;
	}

	l4proto = __nf_ct_l4proto_find(l3num, protonum);

	ret = nf_ct_get_tuple(skb, nhoff, protoff, l3num, protonum, net, tuple,
			      l4proto);

	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL_GPL(nf_ct_get_tuplepr);

bool
nf_ct_invert_tuple(struct nf_conntrack_tuple *inverse,
		   const struct nf_conntrack_tuple *orig,
		   const struct nf_conntrack_l4proto *l4proto)
{
	memset(inverse, 0, sizeof(*inverse));

	inverse->src.l3num = orig->src.l3num;

	switch (orig->src.l3num) {
	case NFPROTO_IPV4:
		inverse->src.u3.ip = orig->dst.u3.ip;
		inverse->dst.u3.ip = orig->src.u3.ip;
		break;
	case NFPROTO_IPV6:
		inverse->src.u3.in6 = orig->dst.u3.in6;
		inverse->dst.u3.in6 = orig->src.u3.in6;
		break;
	default:
		break;
	}

	inverse->dst.dir = !orig->dst.dir;

	inverse->dst.protonum = orig->dst.protonum;

	if (unlikely(l4proto->invert_tuple))
		return l4proto->invert_tuple(inverse, orig);

	inverse->src.u.all = orig->dst.u.all;
	inverse->dst.u.all = orig->src.u.all;
	return true;
}
EXPORT_SYMBOL_GPL(nf_ct_invert_tuple);

/* Generate a almost-unique pseudo-id for a given conntrack.
 *
 * intentionally doesn't re-use any of the seeds used for hash
 * table location, we assume id gets exposed to userspace.
 *
 * Following nf_conn items do not change throughout lifetime
 * of the nf_conn:
 *
 * 1. nf_conn address
 * 2. nf_conn->master address (normally NULL)
 * 3. the associated net namespace
 * 4. the original direction tuple
 */
u32 nf_ct_get_id(const struct nf_conn *ct)
{
	static __read_mostly siphash_key_t ct_id_seed;
	unsigned long a, b, c, d;

	net_get_random_once(&ct_id_seed, sizeof(ct_id_seed));

	a = (unsigned long)ct;
	b = (unsigned long)ct->master;
	c = (unsigned long)nf_ct_net(ct);
	d = (unsigned long)siphash(&ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple,
				   sizeof(ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple),
				   &ct_id_seed);
#ifdef CONFIG_64BIT
	return siphash_4u64((u64)a, (u64)b, (u64)c, (u64)d, &ct_id_seed);
#else
	return siphash_4u32((u32)a, (u32)b, (u32)c, (u32)d, &ct_id_seed);
#endif
}
EXPORT_SYMBOL_GPL(nf_ct_get_id);

static void
clean_from_lists(struct nf_conn *ct)
{
	pr_debug("clean_from_lists(%p)\n", ct);
	hlist_nulls_del_rcu(&ct->tuplehash[IP_CT_DIR_ORIGINAL].hnnode);
	hlist_nulls_del_rcu(&ct->tuplehash[IP_CT_DIR_REPLY].hnnode);

	/* Destroy all pending expectations */
	nf_ct_remove_expectations(ct);
}

/* must be called with local_bh_disable */
static void nf_ct_add_to_dying_list(struct nf_conn *ct)
{
	struct ct_pcpu *pcpu;

	/* add this conntrack to the (per cpu) dying list */
	ct->cpu = smp_processor_id();
	pcpu = per_cpu_ptr(nf_ct_net(ct)->ct.pcpu_lists, ct->cpu);

	spin_lock(&pcpu->lock);
	hlist_nulls_add_head(&ct->tuplehash[IP_CT_DIR_ORIGINAL].hnnode,
			     &pcpu->dying);
	spin_unlock(&pcpu->lock);
}

/* must be called with local_bh_disable */
static void nf_ct_add_to_unconfirmed_list(struct nf_conn *ct)
{
	struct ct_pcpu *pcpu;

	/* add this conntrack to the (per cpu) unconfirmed list */
	ct->cpu = smp_processor_id();
	pcpu = per_cpu_ptr(nf_ct_net(ct)->ct.pcpu_lists, ct->cpu);

	spin_lock(&pcpu->lock);
	hlist_nulls_add_head(&ct->tuplehash[IP_CT_DIR_ORIGINAL].hnnode,
			     &pcpu->unconfirmed);
	spin_unlock(&pcpu->lock);
}

/* must be called with local_bh_disable */
static void nf_ct_del_from_dying_or_unconfirmed_list(struct nf_conn *ct)
{
	struct ct_pcpu *pcpu;

	/* We overload first tuple to link into unconfirmed or dying list.*/
	pcpu = per_cpu_ptr(nf_ct_net(ct)->ct.pcpu_lists, ct->cpu);

	spin_lock(&pcpu->lock);
	BUG_ON(hlist_nulls_unhashed(&ct->tuplehash[IP_CT_DIR_ORIGINAL].hnnode));
	hlist_nulls_del_rcu(&ct->tuplehash[IP_CT_DIR_ORIGINAL].hnnode);
	spin_unlock(&pcpu->lock);
}

#define NFCT_ALIGN(len)	(((len) + NFCT_INFOMASK) & ~NFCT_INFOMASK)

/* Released via destroy_conntrack() */
struct nf_conn *nf_ct_tmpl_alloc(struct net *net,
				 const struct nf_conntrack_zone *zone,
				 gfp_t flags)
{
	struct nf_conn *tmpl, *p;

	if (ARCH_KMALLOC_MINALIGN <= NFCT_INFOMASK) {
		tmpl = kzalloc(sizeof(*tmpl) + NFCT_INFOMASK, flags);
		if (!tmpl)
			return NULL;

		p = tmpl;
		tmpl = (struct nf_conn *)NFCT_ALIGN((unsigned long)p);
		if (tmpl != p) {
			tmpl = (struct nf_conn *)NFCT_ALIGN((unsigned long)p);
			tmpl->proto.tmpl_padto = (char *)tmpl - (char *)p;
		}
	} else {
		tmpl = kzalloc(sizeof(*tmpl), flags);
		if (!tmpl)
			return NULL;
	}

	tmpl->status = IPS_TEMPLATE;
	write_pnet(&tmpl->ct_net, net);
	nf_ct_zone_add(tmpl, zone);
	atomic_set(&tmpl->ct_general.use, 0);

	return tmpl;
}
EXPORT_SYMBOL_GPL(nf_ct_tmpl_alloc);

void nf_ct_tmpl_free(struct nf_conn *tmpl)
{
	nf_ct_ext_destroy(tmpl);
	nf_ct_ext_free(tmpl);

	if (ARCH_KMALLOC_MINALIGN <= NFCT_INFOMASK)
		kfree((char *)tmpl - tmpl->proto.tmpl_padto);
	else
		kfree(tmpl);
}
EXPORT_SYMBOL_GPL(nf_ct_tmpl_free);

static void
destroy_conntrack(struct nf_conntrack *nfct)
{
	struct nf_conn *ct = (struct nf_conn *)nfct;
	const struct nf_conntrack_l4proto *l4proto;

	pr_debug("destroy_conntrack(%p)\n", ct);
	WARN_ON(atomic_read(&nfct->use) != 0);

	if (unlikely(nf_ct_is_template(ct))) {
		nf_ct_tmpl_free(ct);
		return;
	}
	l4proto = __nf_ct_l4proto_find(nf_ct_l3num(ct), nf_ct_protonum(ct));
	if (l4proto->destroy)
		l4proto->destroy(ct);

	local_bh_disable();
	/* Expectations will have been removed in clean_from_lists,
	 * except TFTP can create an expectation on the first packet,
	 * before connection is in the list, so we need to clean here,
	 * too.
	 */
	nf_ct_remove_expectations(ct);

	nf_ct_del_from_dying_or_unconfirmed_list(ct);

	local_bh_enable();

	if (ct->master)
		nf_ct_put(ct->master);

	pr_debug("destroy_conntrack: returning ct=%p to slab\n", ct);
	nf_conntrack_free(ct);
}

static void nf_ct_delete_from_lists(struct nf_conn *ct)
{
	struct net *net = nf_ct_net(ct);
	unsigned int hash, reply_hash;
	unsigned int sequence;

	nf_ct_helper_destroy(ct);

	local_bh_disable();
	do {
		sequence = read_seqcount_begin(&nf_conntrack_generation);
		hash = hash_conntrack(net,
				      &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple);
		reply_hash = hash_conntrack(net,
					   &ct->tuplehash[IP_CT_DIR_REPLY].tuple);
	} while (nf_conntrack_double_lock(net, hash, reply_hash, sequence));

	clean_from_lists(ct);
	nf_conntrack_double_unlock(hash, reply_hash);

	nf_ct_add_to_dying_list(ct);

	local_bh_enable();
}

bool nf_ct_delete(struct nf_conn *ct, u32 portid, int report)
{
	struct nf_conn_tstamp *tstamp;

	if (test_and_set_bit(IPS_DYING_BIT, &ct->status))
		return false;

	tstamp = nf_conn_tstamp_find(ct);
	if (tstamp && tstamp->stop == 0)
		tstamp->stop = ktime_get_real_ns();

	if (nf_conntrack_event_report(IPCT_DESTROY, ct,
				    portid, report) < 0) {
		/* destroy event was not delivered. nf_ct_put will
		 * be done by event cache worker on redelivery.
		 */
		nf_ct_delete_from_lists(ct);
		nf_conntrack_ecache_delayed_work(nf_ct_net(ct));
		return false;
	}

	nf_conntrack_ecache_work(nf_ct_net(ct));
	nf_ct_delete_from_lists(ct);
	nf_ct_put(ct);
	return true;
}
EXPORT_SYMBOL_GPL(nf_ct_delete);

static inline bool
nf_ct_key_equal(struct nf_conntrack_tuple_hash *h,
		const struct nf_conntrack_tuple *tuple,
		const struct nf_conntrack_zone *zone,
		const struct net *net)
{
	struct nf_conn *ct = nf_ct_tuplehash_to_ctrack(h);

	/* A conntrack can be recreated with the equal tuple,
	 * so we need to check that the conntrack is confirmed
	 */
	return nf_ct_tuple_equal(tuple, &h->tuple) &&
	       nf_ct_zone_equal(ct, zone, NF_CT_DIRECTION(h)) &&
	       nf_ct_is_confirmed(ct) &&
	       net_eq(net, nf_ct_net(ct));
}

static inline bool
nf_ct_match(const struct nf_conn *ct1, const struct nf_conn *ct2)
{
	return nf_ct_tuple_equal(&ct1->tuplehash[IP_CT_DIR_ORIGINAL].tuple,
				 &ct2->tuplehash[IP_CT_DIR_ORIGINAL].tuple) &&
	       nf_ct_tuple_equal(&ct1->tuplehash[IP_CT_DIR_REPLY].tuple,
				 &ct2->tuplehash[IP_CT_DIR_REPLY].tuple) &&
	       nf_ct_zone_equal(ct1, nf_ct_zone(ct2), IP_CT_DIR_ORIGINAL) &&
	       nf_ct_zone_equal(ct1, nf_ct_zone(ct2), IP_CT_DIR_REPLY) &&
	       net_eq(nf_ct_net(ct1), nf_ct_net(ct2));
}

/* caller must hold rcu readlock and none of the nf_conntrack_locks */
static void nf_ct_gc_expired(struct nf_conn *ct)
{
	if (!atomic_inc_not_zero(&ct->ct_general.use))
		return;

	if (nf_ct_should_gc(ct))
		nf_ct_kill(ct);

	nf_ct_put(ct);
}

/*
 * Warning :
 * - Caller must take a reference on returned object
 *   and recheck nf_ct_tuple_equal(tuple, &h->tuple)
 */
static struct nf_conntrack_tuple_hash *
____nf_conntrack_find(struct net *net, const struct nf_conntrack_zone *zone,
		      const struct nf_conntrack_tuple *tuple, u32 hash)
{
	struct nf_conntrack_tuple_hash *h;
	struct hlist_nulls_head *ct_hash;
	struct hlist_nulls_node *n;
	unsigned int bucket, hsize;

begin:
	nf_conntrack_get_ht(&ct_hash, &hsize);
	bucket = reciprocal_scale(hash, hsize);

	hlist_nulls_for_each_entry_rcu(h, n, &ct_hash[bucket], hnnode) {
		struct nf_conn *ct;

		ct = nf_ct_tuplehash_to_ctrack(h);
		if (nf_ct_is_expired(ct)) {
			nf_ct_gc_expired(ct);
			continue;
		}

		if (nf_ct_is_dying(ct))
			continue;

		if (nf_ct_key_equal(h, tuple, zone, net))
			return h;
	}
	/*
	 * if the nulls value we got at the end of this lookup is
	 * not the expected one, we must restart lookup.
	 * We probably met an item that was moved to another chain.
	 */
	if (get_nulls_value(n) != bucket) {
		NF_CT_STAT_INC_ATOMIC(net, search_restart);
		goto begin;
	}

	return NULL;
}

/* Find a connection corresponding to a tuple. */
static struct nf_conntrack_tuple_hash *
__nf_conntrack_find_get(struct net *net, const struct nf_conntrack_zone *zone,
			const struct nf_conntrack_tuple *tuple, u32 hash)
{
	struct nf_conntrack_tuple_hash *h;
	struct nf_conn *ct;

	rcu_read_lock();
begin:
	h = ____nf_conntrack_find(net, zone, tuple, hash);
	if (h) {
		ct = nf_ct_tuplehash_to_ctrack(h);
		if (unlikely(nf_ct_is_dying(ct) ||
			     !atomic_inc_not_zero(&ct->ct_general.use)))
			h = NULL;
		else {
			if (unlikely(!nf_ct_key_equal(h, tuple, zone, net))) {
				nf_ct_put(ct);
				goto begin;
			}
		}
	}
	rcu_read_unlock();

	return h;
}

struct nf_conntrack_tuple_hash *
nf_conntrack_find_get(struct net *net, const struct nf_conntrack_zone *zone,
		      const struct nf_conntrack_tuple *tuple)
{
	return __nf_conntrack_find_get(net, zone, tuple,
				       hash_conntrack_raw(tuple, net));
}
EXPORT_SYMBOL_GPL(nf_conntrack_find_get);

static void __nf_conntrack_hash_insert(struct nf_conn *ct,
				       unsigned int hash,
				       unsigned int reply_hash)
{
	hlist_nulls_add_head_rcu(&ct->tuplehash[IP_CT_DIR_ORIGINAL].hnnode,
			   &nf_conntrack_hash[hash]);
	hlist_nulls_add_head_rcu(&ct->tuplehash[IP_CT_DIR_REPLY].hnnode,
			   &nf_conntrack_hash[reply_hash]);
}

int
nf_conntrack_hash_check_insert(struct nf_conn *ct)
{
	const struct nf_conntrack_zone *zone;
	struct net *net = nf_ct_net(ct);
	unsigned int hash, reply_hash;
	struct nf_conntrack_tuple_hash *h;
	struct hlist_nulls_node *n;
	unsigned int sequence;

	zone = nf_ct_zone(ct);

	local_bh_disable();
	do {
		sequence = read_seqcount_begin(&nf_conntrack_generation);
		hash = hash_conntrack(net,
				      &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple);
		reply_hash = hash_conntrack(net,
					   &ct->tuplehash[IP_CT_DIR_REPLY].tuple);
	} while (nf_conntrack_double_lock(net, hash, reply_hash, sequence));

	/* See if there's one in the list already, including reverse */
	hlist_nulls_for_each_entry(h, n, &nf_conntrack_hash[hash], hnnode)
		if (nf_ct_key_equal(h, &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple,
				    zone, net))
			goto out;

	hlist_nulls_for_each_entry(h, n, &nf_conntrack_hash[reply_hash], hnnode)
		if (nf_ct_key_equal(h, &ct->tuplehash[IP_CT_DIR_REPLY].tuple,
				    zone, net))
			goto out;

	smp_wmb();
	/* The caller holds a reference to this object */
	atomic_set(&ct->ct_general.use, 2);
	__nf_conntrack_hash_insert(ct, hash, reply_hash);
	nf_conntrack_double_unlock(hash, reply_hash);
	NF_CT_STAT_INC(net, insert);
	local_bh_enable();
	return 0;

out:
	nf_conntrack_double_unlock(hash, reply_hash);
	NF_CT_STAT_INC(net, insert_failed);
	local_bh_enable();
	return -EEXIST;
}
EXPORT_SYMBOL_GPL(nf_conntrack_hash_check_insert);

static inline void nf_ct_acct_update(struct nf_conn *ct,
				     enum ip_conntrack_info ctinfo,
				     unsigned int len)
{
	struct nf_conn_acct *acct;

	acct = nf_conn_acct_find(ct);
	if (acct) {
		struct nf_conn_counter *counter = acct->counter;

		atomic64_inc(&counter[CTINFO2DIR(ctinfo)].packets);
		atomic64_add(len, &counter[CTINFO2DIR(ctinfo)].bytes);
	}
}

static void nf_ct_acct_merge(struct nf_conn *ct, enum ip_conntrack_info ctinfo,
			     const struct nf_conn *loser_ct)
{
	struct nf_conn_acct *acct;

	acct = nf_conn_acct_find(loser_ct);
	if (acct) {
		struct nf_conn_counter *counter = acct->counter;
		unsigned int bytes;

		/* u32 should be fine since we must have seen one packet. */
		bytes = atomic64_read(&counter[CTINFO2DIR(ctinfo)].bytes);
		nf_ct_acct_update(ct, ctinfo, bytes);
	}
}

/* Resolve race on insertion if this protocol allows this. */
static int nf_ct_resolve_clash(struct net *net, struct sk_buff *skb,
			       enum ip_conntrack_info ctinfo,
			       struct nf_conntrack_tuple_hash *h)
{
	/* This is the conntrack entry already in hashes that won race. */
	struct nf_conn *ct = nf_ct_tuplehash_to_ctrack(h);
	const struct nf_conntrack_l4proto *l4proto;
	enum ip_conntrack_info oldinfo;
	struct nf_conn *loser_ct = nf_ct_get(skb, &oldinfo);

	l4proto = __nf_ct_l4proto_find(nf_ct_l3num(ct), nf_ct_protonum(ct));
	if (l4proto->allow_clash &&
	    !nf_ct_is_dying(ct) &&
	    atomic_inc_not_zero(&ct->ct_general.use)) {
		if (((ct->status & IPS_NAT_DONE_MASK) == 0) ||
		    nf_ct_match(ct, loser_ct)) {
			nf_ct_acct_merge(ct, ctinfo, loser_ct);
			nf_conntrack_put(&loser_ct->ct_general);
			nf_ct_set(skb, ct, oldinfo);
			return NF_ACCEPT;
		}
		nf_ct_put(ct);
	}
	NF_CT_STAT_INC(net, drop);
	return NF_DROP;
}

/* Confirm a connection given skb; places it in hash table */
int
__nf_conntrack_confirm(struct sk_buff *skb)
{
	const struct nf_conntrack_zone *zone;
	unsigned int hash, reply_hash;
	struct nf_conntrack_tuple_hash *h;
	struct nf_conn *ct;
	struct nf_conn_help *help;
	struct nf_conn_tstamp *tstamp;
	struct hlist_nulls_node *n;
	enum ip_conntrack_info ctinfo;
	struct net *net;
	unsigned int sequence;
	int ret = NF_DROP;

	ct = nf_ct_get(skb, &ctinfo);
	net = nf_ct_net(ct);

	/* ipt_REJECT uses nf_conntrack_attach to attach related
	   ICMP/TCP RST packets in other direction.  Actual packet
	   which created connection will be IP_CT_NEW or for an
	   expected connection, IP_CT_RELATED. */
	if (CTINFO2DIR(ctinfo) != IP_CT_DIR_ORIGINAL)
		return NF_ACCEPT;

	zone = nf_ct_zone(ct);
	local_bh_disable();

	do {
		sequence = read_seqcount_begin(&nf_conntrack_generation);
		/* reuse the hash saved before */
		hash = *(unsigned long *)&ct->tuplehash[IP_CT_DIR_REPLY].hnnode.pprev;
		hash = scale_hash(hash);
		reply_hash = hash_conntrack(net,
					   &ct->tuplehash[IP_CT_DIR_REPLY].tuple);

	} while (nf_conntrack_double_lock(net, hash, reply_hash, sequence));

	/* We're not in hash table, and we refuse to set up related
	 * connections for unconfirmed conns.  But packet copies and
	 * REJECT will give spurious warnings here.
	 */

	/* Another skb with the same unconfirmed conntrack may
	 * win the race. This may happen for bridge(br_flood)
	 * or broadcast/multicast packets do skb_clone with
	 * unconfirmed conntrack.
	 */
	if (unlikely(nf_ct_is_confirmed(ct))) {
		WARN_ON_ONCE(1);
		nf_conntrack_double_unlock(hash, reply_hash);
		local_bh_enable();
		return NF_DROP;
	}

	pr_debug("Confirming conntrack %p\n", ct);
	/* We have to check the DYING flag after unlink to prevent
	 * a race against nf_ct_get_next_corpse() possibly called from
	 * user context, else we insert an already 'dead' hash, blocking
	 * further use of that particular connection -JM.
	 */
	nf_ct_del_from_dying_or_unconfirmed_list(ct);

	if (unlikely(nf_ct_is_dying(ct))) {
		nf_ct_add_to_dying_list(ct);
		goto dying;
	}

	/* See if there's one in the list already, including reverse:
	   NAT could have grabbed it without realizing, since we're
	   not in the hash.  If there is, we lost race. */
	hlist_nulls_for_each_entry(h, n, &nf_conntrack_hash[hash], hnnode)
		if (nf_ct_key_equal(h, &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple,
				    zone, net))
			goto out;

	hlist_nulls_for_each_entry(h, n, &nf_conntrack_hash[reply_hash], hnnode)
		if (nf_ct_key_equal(h, &ct->tuplehash[IP_CT_DIR_REPLY].tuple,
				    zone, net))
			goto out;

	/* Timer relative to confirmation time, not original
	   setting time, otherwise we'd get timer wrap in
	   weird delay cases. */
	ct->timeout += nfct_time_stamp;
	atomic_inc(&ct->ct_general.use);
	ct->status |= IPS_CONFIRMED;

	/* set conntrack timestamp, if enabled. */
	tstamp = nf_conn_tstamp_find(ct);
	if (tstamp) {
		if (skb->tstamp == 0)
			__net_timestamp(skb);

		tstamp->start = ktime_to_ns(skb->tstamp);
	}
	/* Since the lookup is lockless, hash insertion must be done after
	 * starting the timer and setting the CONFIRMED bit. The RCU barriers
	 * guarantee that no other CPU can find the conntrack before the above
	 * stores are visible.
	 */
	__nf_conntrack_hash_insert(ct, hash, reply_hash);
	nf_conntrack_double_unlock(hash, reply_hash);
	local_bh_enable();

	help = nfct_help(ct);
	if (help && help->helper)
		nf_conntrack_event_cache(IPCT_HELPER, ct);

	nf_conntrack_event_cache(master_ct(ct) ?
				 IPCT_RELATED : IPCT_NEW, ct);
	return NF_ACCEPT;

out:
	nf_ct_add_to_dying_list(ct);
	ret = nf_ct_resolve_clash(net, skb, ctinfo, h);
dying:
	nf_conntrack_double_unlock(hash, reply_hash);
	NF_CT_STAT_INC(net, insert_failed);
	local_bh_enable();
	return ret;
}
EXPORT_SYMBOL_GPL(__nf_conntrack_confirm);

/* Returns true if a connection correspondings to the tuple (required
   for NAT). */
int
nf_conntrack_tuple_taken(const struct nf_conntrack_tuple *tuple,
			 const struct nf_conn *ignored_conntrack)
{
	struct net *net = nf_ct_net(ignored_conntrack);
	const struct nf_conntrack_zone *zone;
	struct nf_conntrack_tuple_hash *h;
	struct hlist_nulls_head *ct_hash;
	unsigned int hash, hsize;
	struct hlist_nulls_node *n;
	struct nf_conn *ct;

	zone = nf_ct_zone(ignored_conntrack);

	rcu_read_lock();
 begin:
	nf_conntrack_get_ht(&ct_hash, &hsize);
	hash = __hash_conntrack(net, tuple, hsize);

	hlist_nulls_for_each_entry_rcu(h, n, &ct_hash[hash], hnnode) {
		ct = nf_ct_tuplehash_to_ctrack(h);

		if (ct == ignored_conntrack)
			continue;

		if (nf_ct_is_expired(ct)) {
			nf_ct_gc_expired(ct);
			continue;
		}

		if (nf_ct_key_equal(h, tuple, zone, net)) {
			/* Tuple is taken already, so caller will need to find
			 * a new source port to use.
			 *
			 * Only exception:
			 * If the *original tuples* are identical, then both
			 * conntracks refer to the same flow.
			 * This is a rare situation, it can occur e.g. when
			 * more than one UDP packet is sent from same socket
			 * in different threads.
			 *
			 * Let nf_ct_resolve_clash() deal with this later.
			 */
			if (nf_ct_tuple_equal(&ignored_conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple,
					      &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple))
				continue;

			NF_CT_STAT_INC_ATOMIC(net, found);
			rcu_read_unlock();
			return 1;
		}
	}

	if (get_nulls_value(n) != hash) {
		NF_CT_STAT_INC_ATOMIC(net, search_restart);
		goto begin;
	}

	rcu_read_unlock();

	return 0;
}
EXPORT_SYMBOL_GPL(nf_conntrack_tuple_taken);

#define NF_CT_EVICTION_RANGE	8

/* There's a small race here where we may free a just-assured
   connection.  Too bad: we're in trouble anyway. */
static unsigned int early_drop_list(struct net *net,
				    struct hlist_nulls_head *head)
{
	struct nf_conntrack_tuple_hash *h;
	struct hlist_nulls_node *n;
	unsigned int drops = 0;
	struct nf_conn *tmp;

	hlist_nulls_for_each_entry_rcu(h, n, head, hnnode) {
		tmp = nf_ct_tuplehash_to_ctrack(h);

		if (test_bit(IPS_OFFLOAD_BIT, &tmp->status))
			continue;

		if (nf_ct_is_expired(tmp)) {
			nf_ct_gc_expired(tmp);
			continue;
		}

		if (test_bit(IPS_ASSURED_BIT, &tmp->status) ||
		    !net_eq(nf_ct_net(tmp), net) ||
		    nf_ct_is_dying(tmp))
			continue;

		if (!atomic_inc_not_zero(&tmp->ct_general.use))
			continue;

		/* kill only if still in same netns -- might have moved due to
		 * SLAB_TYPESAFE_BY_RCU rules.
		 *
		 * We steal the timer reference.  If that fails timer has
		 * already fired or someone else deleted it. Just drop ref
		 * and move to next entry.
		 */
		if (net_eq(nf_ct_net(tmp), net) &&
		    nf_ct_is_confirmed(tmp) &&
		    nf_ct_delete(tmp, 0, 0))
			drops++;

		nf_ct_put(tmp);
	}

	return drops;
}

static noinline int early_drop(struct net *net, unsigned int hash)
{
	unsigned int i, bucket;

	for (i = 0; i < NF_CT_EVICTION_RANGE; i++) {
		struct hlist_nulls_head *ct_hash;
		unsigned int hsize, drops;

		rcu_read_lock();
		nf_conntrack_get_ht(&ct_hash, &hsize);
		if (!i)
			bucket = reciprocal_scale(hash, hsize);
		else
			bucket = (bucket + 1) % hsize;

		drops = early_drop_list(net, &ct_hash[bucket]);
		rcu_read_unlock();

		if (drops) {
			NF_CT_STAT_ADD_ATOMIC(net, early_drop, drops);
			return true;
		}
	}

	return false;
}

static bool gc_worker_skip_ct(const struct nf_conn *ct)
{
	return !nf_ct_is_confirmed(ct) || nf_ct_is_dying(ct);
}

static bool gc_worker_can_early_drop(const struct nf_conn *ct)
{
	const struct nf_conntrack_l4proto *l4proto;

	if (!test_bit(IPS_ASSURED_BIT, &ct->status))
		return true;

	l4proto = __nf_ct_l4proto_find(nf_ct_l3num(ct), nf_ct_protonum(ct));
	if (l4proto->can_early_drop && l4proto->can_early_drop(ct))
		return true;

	return false;
}

#define	DAY	(86400 * HZ)

/* Set an arbitrary timeout large enough not to ever expire, this save
 * us a check for the IPS_OFFLOAD_BIT from the packet path via
 * nf_ct_is_expired().
 */
static void nf_ct_offload_timeout(struct nf_conn *ct)
{
	if (nf_ct_expires(ct) < DAY / 2)
		ct->timeout = nfct_time_stamp + DAY;
}

static void gc_worker(struct work_struct *work)
{
	unsigned int min_interval = max(HZ / GC_MAX_BUCKETS_DIV, 1u);
	unsigned int i, goal, buckets = 0, expired_count = 0;
	unsigned int nf_conntrack_max95 = 0;
	struct conntrack_gc_work *gc_work;
	unsigned int ratio, scanned = 0;
	unsigned long next_run;

	gc_work = container_of(work, struct conntrack_gc_work, dwork.work);

	goal = nf_conntrack_htable_size / GC_MAX_BUCKETS_DIV;
	i = gc_work->last_bucket;
	if (gc_work->early_drop)
		nf_conntrack_max95 = nf_conntrack_max / 100u * 95u;

	do {
		struct nf_conntrack_tuple_hash *h;
		struct hlist_nulls_head *ct_hash;
		struct hlist_nulls_node *n;
		unsigned int hashsz;
		struct nf_conn *tmp;

		i++;
		rcu_read_lock();

		nf_conntrack_get_ht(&ct_hash, &hashsz);
		if (i >= hashsz)
			i = 0;

		hlist_nulls_for_each_entry_rcu(h, n, &ct_hash[i], hnnode) {
			struct net *net;

			tmp = nf_ct_tuplehash_to_ctrack(h);

			scanned++;
			if (test_bit(IPS_OFFLOAD_BIT, &tmp->status)) {
				nf_ct_offload_timeout(tmp);
				continue;
			}

			if (nf_ct_is_expired(tmp)) {
				nf_ct_gc_expired(tmp);
				expired_count++;
				continue;
			}

			if (nf_conntrack_max95 == 0 || gc_worker_skip_ct(tmp))
				continue;

			net = nf_ct_net(tmp);
			if (atomic_read(&net->ct.count) < nf_conntrack_max95)
				continue;

			/* need to take reference to avoid possible races */
			if (!atomic_inc_not_zero(&tmp->ct_general.use))
				continue;

			if (gc_worker_skip_ct(tmp)) {
				nf_ct_put(tmp);
				continue;
			}

			if (gc_worker_can_early_drop(tmp))
				nf_ct_kill(tmp);

			nf_ct_put(tmp);
		}

		/* could check get_nulls_value() here and restart if ct
		 * was moved to another chain.  But given gc is best-effort
		 * we will just continue with next hash slot.
		 */
		rcu_read_unlock();
		cond_resched();
	} while (++buckets < goal);

	if (gc_work->exiting)
		return;

	/*
	 * Eviction will normally happen from the packet path, and not
	 * from this gc worker.
	 *
	 * This worker is only here to reap expired entries when system went
	 * idle after a busy period.
	 *
	 * The heuristics below are supposed to balance conflicting goals:
	 *
	 * 1. Minimize time until we notice a stale entry
	 * 2. Maximize scan intervals to not waste cycles
	 *
	 * Normally, expire ratio will be close to 0.
	 *
	 * As soon as a sizeable fraction of the entries have expired
	 * increase scan frequency.
	 */
	ratio = scanned ? expired_count * 100 / scanned : 0;
	if (ratio > GC_EVICT_RATIO) {
		gc_work->next_gc_run = min_interval;
	} else {
		unsigned int max = GC_MAX_SCAN_JIFFIES / GC_MAX_BUCKETS_DIV;

		BUILD_BUG_ON((GC_MAX_SCAN_JIFFIES / GC_MAX_BUCKETS_DIV) == 0);

		gc_work->next_gc_run += min_interval;
		if (gc_work->next_gc_run > max)
			gc_work->next_gc_run = max;
	}

	next_run = gc_work->next_gc_run;
	gc_work->last_bucket = i;
	gc_work->early_drop = false;
	queue_delayed_work(system_power_efficient_wq, &gc_work->dwork, next_run);
}

static void conntrack_gc_work_init(struct conntrack_gc_work *gc_work)
{
	INIT_DEFERRABLE_WORK(&gc_work->dwork, gc_worker);
	gc_work->next_gc_run = HZ;
	gc_work->exiting = false;
}

static struct nf_conn *
__nf_conntrack_alloc(struct net *net,
		     const struct nf_conntrack_zone *zone,
		     const struct nf_conntrack_tuple *orig,
		     const struct nf_conntrack_tuple *repl,
		     gfp_t gfp, u32 hash)
{
	struct nf_conn *ct;

	/* We don't want any race condition at early drop stage */
	atomic_inc(&net->ct.count);

	if (nf_conntrack_max &&
	    unlikely(atomic_read(&net->ct.count) > nf_conntrack_max)) {
		if (!early_drop(net, hash)) {
			if (!conntrack_gc_work.early_drop)
				conntrack_gc_work.early_drop = true;
			atomic_dec(&net->ct.count);
			net_warn_ratelimited("nf_conntrack: table full, dropping packet\n");
			return ERR_PTR(-ENOMEM);
		}
	}

	/*
	 * Do not use kmem_cache_zalloc(), as this cache uses
	 * SLAB_TYPESAFE_BY_RCU.
	 */
	ct = kmem_cache_alloc(nf_conntrack_cachep, gfp);
	if (ct == NULL)
		goto out;

	spin_lock_init(&ct->lock);
	ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple = *orig;
	ct->tuplehash[IP_CT_DIR_ORIGINAL].hnnode.pprev = NULL;
	ct->tuplehash[IP_CT_DIR_REPLY].tuple = *repl;
	/* save hash for reusing when confirming */
	*(unsigned long *)(&ct->tuplehash[IP_CT_DIR_REPLY].hnnode.pprev) = hash;
	ct->status = 0;
	write_pnet(&ct->ct_net, net);
	memset(&ct->__nfct_init_offset, 0,
	       offsetof(struct nf_conn, proto) -
	       offsetof(struct nf_conn, __nfct_init_offset));

	nf_ct_zone_add(ct, zone);

	/* Because we use RCU lookups, we set ct_general.use to zero before
	 * this is inserted in any list.
	 */
	atomic_set(&ct->ct_general.use, 0);
	return ct;
out:
	atomic_dec(&net->ct.count);
	return ERR_PTR(-ENOMEM);
}

struct nf_conn *nf_conntrack_alloc(struct net *net,
				   const struct nf_conntrack_zone *zone,
				   const struct nf_conntrack_tuple *orig,
				   const struct nf_conntrack_tuple *repl,
				   gfp_t gfp)
{
	return __nf_conntrack_alloc(net, zone, orig, repl, gfp, 0);
}
EXPORT_SYMBOL_GPL(nf_conntrack_alloc);

void nf_conntrack_free(struct nf_conn *ct)
{
	struct net *net = nf_ct_net(ct);

	/* A freed object has refcnt == 0, that's
	 * the golden rule for SLAB_TYPESAFE_BY_RCU
	 */
	WARN_ON(atomic_read(&ct->ct_general.use) != 0);

	nf_ct_ext_destroy(ct);
	nf_ct_ext_free(ct);
	kmem_cache_free(nf_conntrack_cachep, ct);
	smp_mb__before_atomic();
	atomic_dec(&net->ct.count);
}
EXPORT_SYMBOL_GPL(nf_conntrack_free);


/* Allocate a new conntrack: we return -ENOMEM if classification
   failed due to stress.  Otherwise it really is unclassifiable. */
static noinline struct nf_conntrack_tuple_hash *
init_conntrack(struct net *net, struct nf_conn *tmpl,
	       const struct nf_conntrack_tuple *tuple,
	       const struct nf_conntrack_l4proto *l4proto,
	       struct sk_buff *skb,
	       unsigned int dataoff, u32 hash)
{
	struct nf_conn *ct;
	struct nf_conn_help *help;
	struct nf_conntrack_tuple repl_tuple;
	struct nf_conntrack_ecache *ecache;
	struct nf_conntrack_expect *exp = NULL;
	const struct nf_conntrack_zone *zone;
	struct nf_conn_timeout *timeout_ext;
	struct nf_conntrack_zone tmp;

	if (!nf_ct_invert_tuple(&repl_tuple, tuple, l4proto)) {
		pr_debug("Can't invert tuple.\n");
		return NULL;
	}

	zone = nf_ct_zone_tmpl(tmpl, skb, &tmp);
	ct = __nf_conntrack_alloc(net, zone, tuple, &repl_tuple, GFP_ATOMIC,
				  hash);
	if (IS_ERR(ct))
		return (struct nf_conntrack_tuple_hash *)ct;

	if (!nf_ct_add_synproxy(ct, tmpl)) {
		nf_conntrack_free(ct);
		return ERR_PTR(-ENOMEM);
	}

	timeout_ext = tmpl ? nf_ct_timeout_find(tmpl) : NULL;

	if (!l4proto->new(ct, skb, dataoff)) {
		nf_conntrack_free(ct);
		pr_debug("can't track with proto module\n");
		return NULL;
	}

	if (timeout_ext)
		nf_ct_timeout_ext_add(ct, rcu_dereference(timeout_ext->timeout),
				      GFP_ATOMIC);

	nf_ct_acct_ext_add(ct, GFP_ATOMIC);
	nf_ct_tstamp_ext_add(ct, GFP_ATOMIC);
	nf_ct_labels_ext_add(ct);

	ecache = tmpl ? nf_ct_ecache_find(tmpl) : NULL;
	nf_ct_ecache_ext_add(ct, ecache ? ecache->ctmask : 0,
				 ecache ? ecache->expmask : 0,
			     GFP_ATOMIC);

	local_bh_disable();
	if (net->ct.expect_count) {
		spin_lock(&nf_conntrack_expect_lock);
		exp = nf_ct_find_expectation(net, zone, tuple);
		if (exp) {
			pr_debug("expectation arrives ct=%p exp=%p\n",
				 ct, exp);
			/* Welcome, Mr. Bond.  We've been expecting you... */
			__set_bit(IPS_EXPECTED_BIT, &ct->status);
			/* exp->master safe, refcnt bumped in nf_ct_find_expectation */
			ct->master = exp->master;
			if (exp->helper) {
				help = nf_ct_helper_ext_add(ct, GFP_ATOMIC);
				if (help)
					rcu_assign_pointer(help->helper, exp->helper);
			}

#ifdef CONFIG_NF_CONNTRACK_MARK
			ct->mark = exp->master->mark;
#endif
#ifdef CONFIG_NF_CONNTRACK_SECMARK
			ct->secmark = exp->master->secmark;
#endif
			NF_CT_STAT_INC(net, expect_new);
		}
		spin_unlock(&nf_conntrack_expect_lock);
	}
	if (!exp)
		__nf_ct_try_assign_helper(ct, tmpl, GFP_ATOMIC);

	/* Now it is inserted into the unconfirmed list, bump refcount */
	nf_conntrack_get(&ct->ct_general);
	nf_ct_add_to_unconfirmed_list(ct);

	local_bh_enable();

	if (exp) {
		if (exp->expectfn)
			exp->expectfn(ct, exp);
		nf_ct_expect_put(exp);
	}

	return &ct->tuplehash[IP_CT_DIR_ORIGINAL];
}

/* On success, returns 0, sets skb->_nfct | ctinfo */
static int
resolve_normal_ct(struct net *net, struct nf_conn *tmpl,
		  struct sk_buff *skb,
		  unsigned int dataoff,
		  u_int16_t l3num,
		  u_int8_t protonum,
		  const struct nf_conntrack_l4proto *l4proto)
{
	const struct nf_conntrack_zone *zone;
	struct nf_conntrack_tuple tuple;
	struct nf_conntrack_tuple_hash *h;
	enum ip_conntrack_info ctinfo;
	struct nf_conntrack_zone tmp;
	struct nf_conn *ct;
	u32 hash;

	if (!nf_ct_get_tuple(skb, skb_network_offset(skb),
			     dataoff, l3num, protonum, net, &tuple, l4proto)) {
		pr_debug("Can't get tuple\n");
		return 0;
	}

	/* look for tuple match */
	zone = nf_ct_zone_tmpl(tmpl, skb, &tmp);
	hash = hash_conntrack_raw(&tuple, net);
	h = __nf_conntrack_find_get(net, zone, &tuple, hash);
	if (!h) {
		h = init_conntrack(net, tmpl, &tuple, l4proto,
				   skb, dataoff, hash);
		if (!h)
			return 0;
		if (IS_ERR(h))
			return PTR_ERR(h);
	}
	ct = nf_ct_tuplehash_to_ctrack(h);

	/* It exists; we have (non-exclusive) reference. */
	if (NF_CT_DIRECTION(h) == IP_CT_DIR_REPLY) {
		ctinfo = IP_CT_ESTABLISHED_REPLY;
	} else {
		/* Once we've had two way comms, always ESTABLISHED. */
		if (test_bit(IPS_SEEN_REPLY_BIT, &ct->status)) {
			pr_debug("normal packet for %p\n", ct);
			ctinfo = IP_CT_ESTABLISHED;
		} else if (test_bit(IPS_EXPECTED_BIT, &ct->status)) {
			pr_debug("related packet for %p\n", ct);
			ctinfo = IP_CT_RELATED;
		} else {
			pr_debug("new packet for %p\n", ct);
			ctinfo = IP_CT_NEW;
		}
	}
	nf_ct_set(skb, ct, ctinfo);
	return 0;
}

unsigned int
nf_conntrack_in(struct net *net, u_int8_t pf, unsigned int hooknum,
		struct sk_buff *skb)
{
	const struct nf_conntrack_l4proto *l4proto;
	struct nf_conn *ct, *tmpl;
	enum ip_conntrack_info ctinfo;
	u_int8_t protonum;
	int dataoff, ret;

	tmpl = nf_ct_get(skb, &ctinfo);
	if (tmpl || ctinfo == IP_CT_UNTRACKED) {
		/* Previously seen (loopback or untracked)?  Ignore. */
		if ((tmpl && !nf_ct_is_template(tmpl)) ||
		     ctinfo == IP_CT_UNTRACKED) {
			NF_CT_STAT_INC_ATOMIC(net, ignore);
			return NF_ACCEPT;
		}
		skb->_nfct = 0;
	}

	/* rcu_read_lock()ed by nf_hook_thresh */
	dataoff = get_l4proto(skb, skb_network_offset(skb), pf, &protonum);
	if (dataoff <= 0) {
		pr_debug("not prepared to track yet or error occurred\n");
		NF_CT_STAT_INC_ATOMIC(net, error);
		NF_CT_STAT_INC_ATOMIC(net, invalid);
		ret = NF_ACCEPT;
		goto out;
	}

	l4proto = __nf_ct_l4proto_find(pf, protonum);

	/* It may be an special packet, error, unclean...
	 * inverse of the return code tells to the netfilter
	 * core what to do with the packet. */
	if (l4proto->error != NULL) {
		ret = l4proto->error(net, tmpl, skb, dataoff, pf, hooknum);
		if (ret <= 0) {
			NF_CT_STAT_INC_ATOMIC(net, error);
			NF_CT_STAT_INC_ATOMIC(net, invalid);
			ret = -ret;
			goto out;
		}
		/* ICMP[v6] protocol trackers may assign one conntrack. */
		if (skb->_nfct)
			goto out;
	}
repeat:
	ret = resolve_normal_ct(net, tmpl, skb, dataoff, pf, protonum, l4proto);
	if (ret < 0) {
		/* Too stressed to deal. */
		NF_CT_STAT_INC_ATOMIC(net, drop);
		ret = NF_DROP;
		goto out;
	}

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct) {
		/* Not valid part of a connection */
		NF_CT_STAT_INC_ATOMIC(net, invalid);
		ret = NF_ACCEPT;
		goto out;
	}

	ret = l4proto->packet(ct, skb, dataoff, ctinfo);
	if (ret <= 0) {
		/* Invalid: inverse of the return code tells
		 * the netfilter core what to do */
		pr_debug("nf_conntrack_in: Can't track with proto module\n");
		nf_conntrack_put(&ct->ct_general);
		skb->_nfct = 0;
		NF_CT_STAT_INC_ATOMIC(net, invalid);
		if (ret == -NF_DROP)
			NF_CT_STAT_INC_ATOMIC(net, drop);
		/* Special case: TCP tracker reports an attempt to reopen a
		 * closed/aborted connection. We have to go back and create a
		 * fresh conntrack.
		 */
		if (ret == -NF_REPEAT)
			goto repeat;
		ret = -ret;
		goto out;
	}

	if (ctinfo == IP_CT_ESTABLISHED_REPLY &&
	    !test_and_set_bit(IPS_SEEN_REPLY_BIT, &ct->status))
		nf_conntrack_event_cache(IPCT_REPLY, ct);
out:
	if (tmpl)
		nf_ct_put(tmpl);

	return ret;
}
EXPORT_SYMBOL_GPL(nf_conntrack_in);

bool nf_ct_invert_tuplepr(struct nf_conntrack_tuple *inverse,
			  const struct nf_conntrack_tuple *orig)
{
	bool ret;

	rcu_read_lock();
	ret = nf_ct_invert_tuple(inverse, orig,
				 __nf_ct_l4proto_find(orig->src.l3num,
						      orig->dst.protonum));
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL_GPL(nf_ct_invert_tuplepr);

/* Alter reply tuple (maybe alter helper).  This is for NAT, and is
   implicitly racy: see __nf_conntrack_confirm */
void nf_conntrack_alter_reply(struct nf_conn *ct,
			      const struct nf_conntrack_tuple *newreply)
{
	struct nf_conn_help *help = nfct_help(ct);

	/* Should be unconfirmed, so not in hash table yet */
	WARN_ON(nf_ct_is_confirmed(ct));

	pr_debug("Altering reply tuple of %p to ", ct);
	nf_ct_dump_tuple(newreply);

	ct->tuplehash[IP_CT_DIR_REPLY].tuple = *newreply;
	if (ct->master || (help && !hlist_empty(&help->expectations)))
		return;

	rcu_read_lock();
	__nf_ct_try_assign_helper(ct, NULL, GFP_ATOMIC);
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(nf_conntrack_alter_reply);

/* Refresh conntrack for this many jiffies and do accounting if do_acct is 1 */
void __nf_ct_refresh_acct(struct nf_conn *ct,
			  enum ip_conntrack_info ctinfo,
			  const struct sk_buff *skb,
			  unsigned long extra_jiffies,
			  int do_acct)
{
	WARN_ON(!skb);

	/* Only update if this is not a fixed timeout */
	if (test_bit(IPS_FIXED_TIMEOUT_BIT, &ct->status))
		goto acct;

	/* If not in hash table, timer will not be active yet */
	if (nf_ct_is_confirmed(ct))
		extra_jiffies += nfct_time_stamp;

	ct->timeout = extra_jiffies;
acct:
	if (do_acct)
		nf_ct_acct_update(ct, ctinfo, skb->len);
}
EXPORT_SYMBOL_GPL(__nf_ct_refresh_acct);

bool nf_ct_kill_acct(struct nf_conn *ct,
		     enum ip_conntrack_info ctinfo,
		     const struct sk_buff *skb)
{
	nf_ct_acct_update(ct, ctinfo, skb->len);

	return nf_ct_delete(ct, 0, 0);
}
EXPORT_SYMBOL_GPL(nf_ct_kill_acct);

#if IS_ENABLED(CONFIG_NF_CT_NETLINK)

#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_conntrack.h>
#include <linux/mutex.h>

/* Generic function for tcp/udp/sctp/dccp and alike. This needs to be
 * in ip_conntrack_core, since we don't want the protocols to autoload
 * or depend on ctnetlink */
int nf_ct_port_tuple_to_nlattr(struct sk_buff *skb,
			       const struct nf_conntrack_tuple *tuple)
{
	if (nla_put_be16(skb, CTA_PROTO_SRC_PORT, tuple->src.u.tcp.port) ||
	    nla_put_be16(skb, CTA_PROTO_DST_PORT, tuple->dst.u.tcp.port))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}
EXPORT_SYMBOL_GPL(nf_ct_port_tuple_to_nlattr);

const struct nla_policy nf_ct_port_nla_policy[CTA_PROTO_MAX+1] = {
	[CTA_PROTO_SRC_PORT]  = { .type = NLA_U16 },
	[CTA_PROTO_DST_PORT]  = { .type = NLA_U16 },
};
EXPORT_SYMBOL_GPL(nf_ct_port_nla_policy);

int nf_ct_port_nlattr_to_tuple(struct nlattr *tb[],
			       struct nf_conntrack_tuple *t)
{
	if (!tb[CTA_PROTO_SRC_PORT] || !tb[CTA_PROTO_DST_PORT])
		return -EINVAL;

	t->src.u.tcp.port = nla_get_be16(tb[CTA_PROTO_SRC_PORT]);
	t->dst.u.tcp.port = nla_get_be16(tb[CTA_PROTO_DST_PORT]);

	return 0;
}
EXPORT_SYMBOL_GPL(nf_ct_port_nlattr_to_tuple);

unsigned int nf_ct_port_nlattr_tuple_size(void)
{
	static unsigned int size __read_mostly;

	if (!size)
		size = nla_policy_len(nf_ct_port_nla_policy, CTA_PROTO_MAX + 1);

	return size;
}
EXPORT_SYMBOL_GPL(nf_ct_port_nlattr_tuple_size);
#endif

/* Used by ipt_REJECT and ip6t_REJECT. */
static void nf_conntrack_attach(struct sk_buff *nskb, const struct sk_buff *skb)
{
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;

	/* This ICMP is in reverse direction to the packet which caused it */
	ct = nf_ct_get(skb, &ctinfo);
	if (CTINFO2DIR(ctinfo) == IP_CT_DIR_ORIGINAL)
		ctinfo = IP_CT_RELATED_REPLY;
	else
		ctinfo = IP_CT_RELATED;

	/* Attach to new skbuff, and increment count */
	nf_ct_set(nskb, ct, ctinfo);
	nf_conntrack_get(skb_nfct(nskb));
}

static int nf_conntrack_update(struct net *net, struct sk_buff *skb)
{
	const struct nf_conntrack_l4proto *l4proto;
	struct nf_conntrack_tuple_hash *h;
	struct nf_conntrack_tuple tuple;
	enum ip_conntrack_info ctinfo;
	struct nf_nat_hook *nat_hook;
	unsigned int status;
	struct nf_conn *ct;
	int dataoff;
	u16 l3num;
	u8 l4num;

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct || nf_ct_is_confirmed(ct))
		return 0;

	l3num = nf_ct_l3num(ct);

	dataoff = get_l4proto(skb, skb_network_offset(skb), l3num, &l4num);
	if (dataoff <= 0)
		return -1;

	l4proto = nf_ct_l4proto_find_get(l3num, l4num);

	if (!nf_ct_get_tuple(skb, skb_network_offset(skb), dataoff, l3num,
			     l4num, net, &tuple, l4proto))
		return -1;

	if (ct->status & IPS_SRC_NAT) {
		memcpy(tuple.src.u3.all,
		       ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u3.all,
		       sizeof(tuple.src.u3.all));
		tuple.src.u.all =
			ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u.all;
	}

	if (ct->status & IPS_DST_NAT) {
		memcpy(tuple.dst.u3.all,
		       ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.u3.all,
		       sizeof(tuple.dst.u3.all));
		tuple.dst.u.all =
			ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.u.all;
	}

	h = nf_conntrack_find_get(net, nf_ct_zone(ct), &tuple);
	if (!h)
		return 0;

	/* Store status bits of the conntrack that is clashing to re-do NAT
	 * mangling according to what it has been done already to this packet.
	 */
	status = ct->status;

	nf_ct_put(ct);
	ct = nf_ct_tuplehash_to_ctrack(h);
	nf_ct_set(skb, ct, ctinfo);

	nat_hook = rcu_dereference(nf_nat_hook);
	if (!nat_hook)
		return 0;

	if (status & IPS_SRC_NAT &&
	    nat_hook->manip_pkt(skb, ct, NF_NAT_MANIP_SRC,
				IP_CT_DIR_ORIGINAL) == NF_DROP)
		return -1;

	if (status & IPS_DST_NAT &&
	    nat_hook->manip_pkt(skb, ct, NF_NAT_MANIP_DST,
				IP_CT_DIR_ORIGINAL) == NF_DROP)
		return -1;

	return 0;
}

static bool nf_conntrack_get_tuple_skb(struct nf_conntrack_tuple *dst_tuple,
				       const struct sk_buff *skb)
{
	const struct nf_conntrack_tuple *src_tuple;
	const struct nf_conntrack_tuple_hash *hash;
	struct nf_conntrack_tuple srctuple;
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct;

	ct = nf_ct_get(skb, &ctinfo);
	if (ct) {
		src_tuple = nf_ct_tuple(ct, CTINFO2DIR(ctinfo));
		memcpy(dst_tuple, src_tuple, sizeof(*dst_tuple));
		return true;
	}

	if (!nf_ct_get_tuplepr(skb, skb_network_offset(skb),
			       NFPROTO_IPV4, dev_net(skb->dev),
			       &srctuple))
		return false;

	hash = nf_conntrack_find_get(dev_net(skb->dev),
				     &nf_ct_zone_dflt,
				     &srctuple);
	if (!hash)
		return false;

	ct = nf_ct_tuplehash_to_ctrack(hash);
	src_tuple = nf_ct_tuple(ct, !hash->tuple.dst.dir);
	memcpy(dst_tuple, src_tuple, sizeof(*dst_tuple));
	nf_ct_put(ct);

	return true;
}

/* Bring out ya dead! */
static struct nf_conn *
get_next_corpse(int (*iter)(struct nf_conn *i, void *data),
		void *data, unsigned int *bucket)
{
	struct nf_conntrack_tuple_hash *h;
	struct nf_conn *ct;
	struct hlist_nulls_node *n;
	spinlock_t *lockp;

	for (; *bucket < nf_conntrack_htable_size; (*bucket)++) {
		lockp = &nf_conntrack_locks[*bucket % CONNTRACK_LOCKS];
		local_bh_disable();
		nf_conntrack_lock(lockp);
		if (*bucket < nf_conntrack_htable_size) {
			hlist_nulls_for_each_entry(h, n, &nf_conntrack_hash[*bucket], hnnode) {
				if (NF_CT_DIRECTION(h) != IP_CT_DIR_ORIGINAL)
					continue;
				ct = nf_ct_tuplehash_to_ctrack(h);
				if (iter(ct, data))
					goto found;
			}
		}
		spin_unlock(lockp);
		local_bh_enable();
		cond_resched();
	}

	return NULL;
found:
	atomic_inc(&ct->ct_general.use);
	spin_unlock(lockp);
	local_bh_enable();
	return ct;
}

static void nf_ct_iterate_cleanup(int (*iter)(struct nf_conn *i, void *data),
				  void *data, u32 portid, int report)
{
	unsigned int bucket = 0, sequence;
	struct nf_conn *ct;

	might_sleep();

	for (;;) {
		sequence = read_seqcount_begin(&nf_conntrack_generation);

		while ((ct = get_next_corpse(iter, data, &bucket)) != NULL) {
			/* Time to push up daises... */

			nf_ct_delete(ct, portid, report);
			nf_ct_put(ct);
			cond_resched();
		}

		if (!read_seqcount_retry(&nf_conntrack_generation, sequence))
			break;
		bucket = 0;
	}
}

struct iter_data {
	int (*iter)(struct nf_conn *i, void *data);
	void *data;
	struct net *net;
};

static int iter_net_only(struct nf_conn *i, void *data)
{
	struct iter_data *d = data;

	if (!net_eq(d->net, nf_ct_net(i)))
		return 0;

	return d->iter(i, d->data);
}

static void
__nf_ct_unconfirmed_destroy(struct net *net)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		struct nf_conntrack_tuple_hash *h;
		struct hlist_nulls_node *n;
		struct ct_pcpu *pcpu;

		pcpu = per_cpu_ptr(net->ct.pcpu_lists, cpu);

		spin_lock_bh(&pcpu->lock);
		hlist_nulls_for_each_entry(h, n, &pcpu->unconfirmed, hnnode) {
			struct nf_conn *ct;

			ct = nf_ct_tuplehash_to_ctrack(h);

			/* we cannot call iter() on unconfirmed list, the
			 * owning cpu can reallocate ct->ext at any time.
			 */
			set_bit(IPS_DYING_BIT, &ct->status);
		}
		spin_unlock_bh(&pcpu->lock);
		cond_resched();
	}
}

void nf_ct_unconfirmed_destroy(struct net *net)
{
	might_sleep();

	if (atomic_read(&net->ct.count) > 0) {
		__nf_ct_unconfirmed_destroy(net);
		nf_queue_nf_hook_drop(net);
		synchronize_net();
	}
}
EXPORT_SYMBOL_GPL(nf_ct_unconfirmed_destroy);

void nf_ct_iterate_cleanup_net(struct net *net,
			       int (*iter)(struct nf_conn *i, void *data),
			       void *data, u32 portid, int report)
{
	struct iter_data d;

	might_sleep();

	if (atomic_read(&net->ct.count) == 0)
		return;

	d.iter = iter;
	d.data = data;
	d.net = net;

	nf_ct_iterate_cleanup(iter_net_only, &d, portid, report);
}
EXPORT_SYMBOL_GPL(nf_ct_iterate_cleanup_net);

/**
 * nf_ct_iterate_destroy - destroy unconfirmed conntracks and iterate table
 * @iter: callback to invoke for each conntrack
 * @data: data to pass to @iter
 *
 * Like nf_ct_iterate_cleanup, but first marks conntracks on the
 * unconfirmed list as dying (so they will not be inserted into
 * main table).
 *
 * Can only be called in module exit path.
 */
void
nf_ct_iterate_destroy(int (*iter)(struct nf_conn *i, void *data), void *data)
{
	struct net *net;

	down_read(&net_rwsem);
	for_each_net(net) {
		if (atomic_read(&net->ct.count) == 0)
			continue;
		__nf_ct_unconfirmed_destroy(net);
		nf_queue_nf_hook_drop(net);
	}
	up_read(&net_rwsem);

	/* Need to wait for netns cleanup worker to finish, if its
	 * running -- it might have deleted a net namespace from
	 * the global list, so our __nf_ct_unconfirmed_destroy() might
	 * not have affected all namespaces.
	 */
	net_ns_barrier();

	/* a conntrack could have been unlinked from unconfirmed list
	 * before we grabbed pcpu lock in __nf_ct_unconfirmed_destroy().
	 * This makes sure its inserted into conntrack table.
	 */
	synchronize_net();

	nf_ct_iterate_cleanup(iter, data, 0, 0);
}
EXPORT_SYMBOL_GPL(nf_ct_iterate_destroy);

static int kill_all(struct nf_conn *i, void *data)
{
	return net_eq(nf_ct_net(i), data);
}

void nf_conntrack_cleanup_start(void)
{
	conntrack_gc_work.exiting = true;
	RCU_INIT_POINTER(ip_ct_attach, NULL);
}

void nf_conntrack_cleanup_end(void)
{
	RCU_INIT_POINTER(nf_ct_hook, NULL);
	cancel_delayed_work_sync(&conntrack_gc_work.dwork);
	kvfree(nf_conntrack_hash);

	nf_conntrack_proto_fini();
	nf_conntrack_seqadj_fini();
	nf_conntrack_labels_fini();
	nf_conntrack_helper_fini();
	nf_conntrack_timeout_fini();
	nf_conntrack_ecache_fini();
	nf_conntrack_tstamp_fini();
	nf_conntrack_acct_fini();
	nf_conntrack_expect_fini();

	kmem_cache_destroy(nf_conntrack_cachep);
}

/*
 * Mishearing the voices in his head, our hero wonders how he's
 * supposed to kill the mall.
 */
void nf_conntrack_cleanup_net(struct net *net)
{
	LIST_HEAD(single);

	list_add(&net->exit_list, &single);
	nf_conntrack_cleanup_net_list(&single);
}

void nf_conntrack_cleanup_net_list(struct list_head *net_exit_list)
{
	int busy;
	struct net *net;

	/*
	 * This makes sure all current packets have passed through
	 *  netfilter framework.  Roll on, two-stage module
	 *  delete...
	 */
	synchronize_net();
i_see_dead_people:
	busy = 0;
	list_for_each_entry(net, net_exit_list, exit_list) {
		nf_ct_iterate_cleanup(kill_all, net, 0, 0);
		if (atomic_read(&net->ct.count) != 0)
			busy = 1;
	}
	if (busy) {
		schedule();
		goto i_see_dead_people;
	}

	list_for_each_entry(net, net_exit_list, exit_list) {
		nf_conntrack_proto_pernet_fini(net);
		nf_conntrack_helper_pernet_fini(net);
		nf_conntrack_ecache_pernet_fini(net);
		nf_conntrack_tstamp_pernet_fini(net);
		nf_conntrack_acct_pernet_fini(net);
		nf_conntrack_expect_pernet_fini(net);
		free_percpu(net->ct.stat);
		free_percpu(net->ct.pcpu_lists);
	}
}

void *nf_ct_alloc_hashtable(unsigned int *sizep, int nulls)
{
	struct hlist_nulls_head *hash;
	unsigned int nr_slots, i;

	if (*sizep > (UINT_MAX / sizeof(struct hlist_nulls_head)))
		return NULL;

	BUILD_BUG_ON(sizeof(struct hlist_nulls_head) != sizeof(struct hlist_head));
	nr_slots = *sizep = roundup(*sizep, PAGE_SIZE / sizeof(struct hlist_nulls_head));

	hash = kvmalloc_array(nr_slots, sizeof(struct hlist_nulls_head),
			      GFP_KERNEL | __GFP_ZERO);

	if (hash && nulls)
		for (i = 0; i < nr_slots; i++)
			INIT_HLIST_NULLS_HEAD(&hash[i], i);

	return hash;
}
EXPORT_SYMBOL_GPL(nf_ct_alloc_hashtable);

int nf_conntrack_hash_resize(unsigned int hashsize)
{
	int i, bucket;
	unsigned int old_size;
	struct hlist_nulls_head *hash, *old_hash;
	struct nf_conntrack_tuple_hash *h;
	struct nf_conn *ct;

	if (!hashsize)
		return -EINVAL;

	hash = nf_ct_alloc_hashtable(&hashsize, 1);
	if (!hash)
		return -ENOMEM;

	old_size = nf_conntrack_htable_size;
	if (old_size == hashsize) {
		kvfree(hash);
		return 0;
	}

	local_bh_disable();
	nf_conntrack_all_lock();
	write_seqcount_begin(&nf_conntrack_generation);

	/* Lookups in the old hash might happen in parallel, which means we
	 * might get false negatives during connection lookup. New connections
	 * created because of a false negative won't make it into the hash
	 * though since that required taking the locks.
	 */

	for (i = 0; i < nf_conntrack_htable_size; i++) {
		while (!hlist_nulls_empty(&nf_conntrack_hash[i])) {
			h = hlist_nulls_entry(nf_conntrack_hash[i].first,
					      struct nf_conntrack_tuple_hash, hnnode);
			ct = nf_ct_tuplehash_to_ctrack(h);
			hlist_nulls_del_rcu(&h->hnnode);
			bucket = __hash_conntrack(nf_ct_net(ct),
						  &h->tuple, hashsize);
			hlist_nulls_add_head_rcu(&h->hnnode, &hash[bucket]);
		}
	}
	old_size = nf_conntrack_htable_size;
	old_hash = nf_conntrack_hash;

	nf_conntrack_hash = hash;
	nf_conntrack_htable_size = hashsize;

	write_seqcount_end(&nf_conntrack_generation);
	nf_conntrack_all_unlock();
	local_bh_enable();

	synchronize_net();
	kvfree(old_hash);
	return 0;
}

int nf_conntrack_set_hashsize(const char *val, const struct kernel_param *kp)
{
	unsigned int hashsize;
	int rc;

	if (current->nsproxy->net_ns != &init_net)
		return -EOPNOTSUPP;

	/* On boot, we can set this without any fancy locking. */
	if (!nf_conntrack_hash)
		return param_set_uint(val, kp);

	rc = kstrtouint(val, 0, &hashsize);
	if (rc)
		return rc;

	return nf_conntrack_hash_resize(hashsize);
}
EXPORT_SYMBOL_GPL(nf_conntrack_set_hashsize);

static __always_inline unsigned int total_extension_size(void)
{
	/* remember to add new extensions below */
	BUILD_BUG_ON(NF_CT_EXT_NUM > 9);

	return sizeof(struct nf_ct_ext) +
	       sizeof(struct nf_conn_help)
#if IS_ENABLED(CONFIG_NF_NAT)
		+ sizeof(struct nf_conn_nat)
#endif
		+ sizeof(struct nf_conn_seqadj)
		+ sizeof(struct nf_conn_acct)
#ifdef CONFIG_NF_CONNTRACK_EVENTS
		+ sizeof(struct nf_conntrack_ecache)
#endif
#ifdef CONFIG_NF_CONNTRACK_TIMESTAMP
		+ sizeof(struct nf_conn_tstamp)
#endif
#ifdef CONFIG_NF_CONNTRACK_TIMEOUT
		+ sizeof(struct nf_conn_timeout)
#endif
#ifdef CONFIG_NF_CONNTRACK_LABELS
		+ sizeof(struct nf_conn_labels)
#endif
#if IS_ENABLED(CONFIG_NETFILTER_SYNPROXY)
		+ sizeof(struct nf_conn_synproxy)
#endif
	;
};

int nf_conntrack_init_start(void)
{
	int max_factor = 8;
	int ret = -ENOMEM;
	int i;

	/* struct nf_ct_ext uses u8 to store offsets/size */
	BUILD_BUG_ON(total_extension_size() > 255u);

	seqcount_init(&nf_conntrack_generation);

	for (i = 0; i < CONNTRACK_LOCKS; i++)
		spin_lock_init(&nf_conntrack_locks[i]);

	if (!nf_conntrack_htable_size) {
		/* Idea from tcp.c: use 1/16384 of memory.
		 * On i386: 32MB machine has 512 buckets.
		 * >= 1GB machines have 16384 buckets.
		 * >= 4GB machines have 65536 buckets.
		 */
		nf_conntrack_htable_size
			= (((totalram_pages << PAGE_SHIFT) / 16384)
			   / sizeof(struct hlist_head));
		if (totalram_pages > (4 * (1024 * 1024 * 1024 / PAGE_SIZE)))
			nf_conntrack_htable_size = 65536;
		else if (totalram_pages > (1024 * 1024 * 1024 / PAGE_SIZE))
			nf_conntrack_htable_size = 16384;
		if (nf_conntrack_htable_size < 32)
			nf_conntrack_htable_size = 32;

		/* Use a max. factor of four by default to get the same max as
		 * with the old struct list_heads. When a table size is given
		 * we use the old value of 8 to avoid reducing the max.
		 * entries. */
		max_factor = 4;
	}

	nf_conntrack_hash = nf_ct_alloc_hashtable(&nf_conntrack_htable_size, 1);
	if (!nf_conntrack_hash)
		return -ENOMEM;

	nf_conntrack_max = max_factor * nf_conntrack_htable_size;

	nf_conntrack_cachep = kmem_cache_create("nf_conntrack",
						sizeof(struct nf_conn),
						NFCT_INFOMASK + 1,
						SLAB_TYPESAFE_BY_RCU | SLAB_HWCACHE_ALIGN, NULL);
	if (!nf_conntrack_cachep)
		goto err_cachep;

	ret = nf_conntrack_expect_init();
	if (ret < 0)
		goto err_expect;

	ret = nf_conntrack_acct_init();
	if (ret < 0)
		goto err_acct;

	ret = nf_conntrack_tstamp_init();
	if (ret < 0)
		goto err_tstamp;

	ret = nf_conntrack_ecache_init();
	if (ret < 0)
		goto err_ecache;

	ret = nf_conntrack_timeout_init();
	if (ret < 0)
		goto err_timeout;

	ret = nf_conntrack_helper_init();
	if (ret < 0)
		goto err_helper;

	ret = nf_conntrack_labels_init();
	if (ret < 0)
		goto err_labels;

	ret = nf_conntrack_seqadj_init();
	if (ret < 0)
		goto err_seqadj;

	ret = nf_conntrack_proto_init();
	if (ret < 0)
		goto err_proto;

	conntrack_gc_work_init(&conntrack_gc_work);
	queue_delayed_work(system_power_efficient_wq, &conntrack_gc_work.dwork, HZ);

	return 0;

err_proto:
	nf_conntrack_seqadj_fini();
err_seqadj:
	nf_conntrack_labels_fini();
err_labels:
	nf_conntrack_helper_fini();
err_helper:
	nf_conntrack_timeout_fini();
err_timeout:
	nf_conntrack_ecache_fini();
err_ecache:
	nf_conntrack_tstamp_fini();
err_tstamp:
	nf_conntrack_acct_fini();
err_acct:
	nf_conntrack_expect_fini();
err_expect:
	kmem_cache_destroy(nf_conntrack_cachep);
err_cachep:
	kvfree(nf_conntrack_hash);
	return ret;
}

static struct nf_ct_hook nf_conntrack_hook = {
	.update		= nf_conntrack_update,
	.destroy	= destroy_conntrack,
	.get_tuple_skb  = nf_conntrack_get_tuple_skb,
};

void nf_conntrack_init_end(void)
{
	/* For use by REJECT target */
	RCU_INIT_POINTER(ip_ct_attach, nf_conntrack_attach);
	RCU_INIT_POINTER(nf_ct_hook, &nf_conntrack_hook);
}

/*
 * We need to use special "null" values, not used in hash table
 */
#define UNCONFIRMED_NULLS_VAL	((1<<30)+0)
#define DYING_NULLS_VAL		((1<<30)+1)
#define TEMPLATE_NULLS_VAL	((1<<30)+2)

int nf_conntrack_init_net(struct net *net)
{
	int ret = -ENOMEM;
	int cpu;

	BUILD_BUG_ON(IP_CT_UNTRACKED == IP_CT_NUMBER);
	atomic_set(&net->ct.count, 0);

	net->ct.pcpu_lists = alloc_percpu(struct ct_pcpu);
	if (!net->ct.pcpu_lists)
		goto err_stat;

	for_each_possible_cpu(cpu) {
		struct ct_pcpu *pcpu = per_cpu_ptr(net->ct.pcpu_lists, cpu);

		spin_lock_init(&pcpu->lock);
		INIT_HLIST_NULLS_HEAD(&pcpu->unconfirmed, UNCONFIRMED_NULLS_VAL);
		INIT_HLIST_NULLS_HEAD(&pcpu->dying, DYING_NULLS_VAL);
	}

	net->ct.stat = alloc_percpu(struct ip_conntrack_stat);
	if (!net->ct.stat)
		goto err_pcpu_lists;

	ret = nf_conntrack_expect_pernet_init(net);
	if (ret < 0)
		goto err_expect;
	ret = nf_conntrack_acct_pernet_init(net);
	if (ret < 0)
		goto err_acct;
	ret = nf_conntrack_tstamp_pernet_init(net);
	if (ret < 0)
		goto err_tstamp;
	ret = nf_conntrack_ecache_pernet_init(net);
	if (ret < 0)
		goto err_ecache;
	ret = nf_conntrack_helper_pernet_init(net);
	if (ret < 0)
		goto err_helper;
	ret = nf_conntrack_proto_pernet_init(net);
	if (ret < 0)
		goto err_proto;
	return 0;

err_proto:
	nf_conntrack_helper_pernet_fini(net);
err_helper:
	nf_conntrack_ecache_pernet_fini(net);
err_ecache:
	nf_conntrack_tstamp_pernet_fini(net);
err_tstamp:
	nf_conntrack_acct_pernet_fini(net);
err_acct:
	nf_conntrack_expect_pernet_fini(net);
err_expect:
	free_percpu(net->ct.stat);
err_pcpu_lists:
	free_percpu(net->ct.pcpu_lists);
err_stat:
	return ret;
}
