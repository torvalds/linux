// SPDX-License-Identifier: GPL-2.0-only
/* Connection state tracking for netfilter.  This is separated from,
   but required by, the NAT layer; it can also be used by an iptables
   extension. */

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 * (C) 2003,2004 USAGI/WIDE Project <http://www.linux-ipv6.org>
 * (C) 2005-2012 Patrick McHardy <kaber@trash.net>
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
#include <trace/hooks/net.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_bpf.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <net/netfilter/nf_conntrack_helper.h>
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
	u32			next_bucket;
	u32			avg_timeout;
	u32			count;
	u32			start_time;
	bool			exiting;
	bool			early_drop;
};

static __read_mostly struct kmem_cache *nf_conntrack_cachep;
static DEFINE_SPINLOCK(nf_conntrack_locks_all_lock);
static __read_mostly bool nf_conntrack_locks_all;

/* serialize hash resizes and nf_ct_iterate_cleanup */
static DEFINE_MUTEX(nf_conntrack_mutex);

#define GC_SCAN_INTERVAL_MAX	(60ul * HZ)
#define GC_SCAN_INTERVAL_MIN	(1ul * HZ)

/* clamp timeouts to this value (TCP unacked) */
#define GC_SCAN_INTERVAL_CLAMP	(300ul * HZ)

/* Initial bias pretending we have 100 entries at the upper bound so we don't
 * wakeup often just because we have three entries with a 1s timeout while still
 * allowing non-idle machines to wakeup more often when needed.
 */
#define GC_SCAN_INITIAL_COUNT	100
#define GC_SCAN_INTERVAL_INIT	GC_SCAN_INTERVAL_MAX

#define GC_SCAN_MAX_DURATION	msecs_to_jiffies(10)
#define GC_SCAN_EXPIRED_MAX	(64000u / HZ)

#define MIN_CHAINLEN	50u
#define MAX_CHAINLEN	(80u - MIN_CHAINLEN)

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
	__acquires(&nf_conntrack_locks_all_lock)
{
	int i;

	spin_lock(&nf_conntrack_locks_all_lock);

	/* For nf_contrack_locks_all, only the latest time when another
	 * CPU will see an update is controlled, by the "release" of the
	 * spin_lock below.
	 * The earliest time is not controlled, an thus KCSAN could detect
	 * a race when nf_conntract_lock() reads the variable.
	 * WRITE_ONCE() is used to ensure the compiler will not
	 * optimize the write.
	 */
	WRITE_ONCE(nf_conntrack_locks_all, true);

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
	__releases(&nf_conntrack_locks_all_lock)
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
seqcount_spinlock_t nf_conntrack_generation __read_mostly;
static siphash_aligned_key_t nf_conntrack_hash_rnd;

static u32 hash_conntrack_raw(const struct nf_conntrack_tuple *tuple,
			      unsigned int zoneid,
			      const struct net *net)
{
	struct {
		struct nf_conntrack_man src;
		union nf_inet_addr dst_addr;
		unsigned int zone;
		u32 net_mix;
		u16 dport;
		u16 proto;
	} __aligned(SIPHASH_ALIGNMENT) combined;

	get_random_once(&nf_conntrack_hash_rnd, sizeof(nf_conntrack_hash_rnd));

	memset(&combined, 0, sizeof(combined));

	/* The direction must be ignored, so handle usable members manually. */
	combined.src = tuple->src;
	combined.dst_addr = tuple->dst.u3;
	combined.zone = zoneid;
	combined.net_mix = net_hash_mix(net);
	combined.dport = (__force __u16)tuple->dst.u.all;
	combined.proto = tuple->dst.protonum;

	return (u32)siphash(&combined, sizeof(combined), &nf_conntrack_hash_rnd);
}

static u32 scale_hash(u32 hash)
{
	return reciprocal_scale(hash, nf_conntrack_htable_size);
}

static u32 __hash_conntrack(const struct net *net,
			    const struct nf_conntrack_tuple *tuple,
			    unsigned int zoneid,
			    unsigned int size)
{
	return reciprocal_scale(hash_conntrack_raw(tuple, zoneid, net), size);
}

static u32 hash_conntrack(const struct net *net,
			  const struct nf_conntrack_tuple *tuple,
			  unsigned int zoneid)
{
	return scale_hash(hash_conntrack_raw(tuple, zoneid, net));
}

static bool nf_ct_get_tuple_ports(const struct sk_buff *skb,
				  unsigned int dataoff,
				  struct nf_conntrack_tuple *tuple)
{	struct {
		__be16 sport;
		__be16 dport;
	} _inet_hdr, *inet_hdr;

	/* Actually only need first 4 bytes to get ports. */
	inet_hdr = skb_header_pointer(skb, dataoff, sizeof(_inet_hdr), &_inet_hdr);
	if (!inet_hdr)
		return false;

	tuple->src.u.udp.port = inet_hdr->sport;
	tuple->dst.u.udp.port = inet_hdr->dport;
	return true;
}

static bool
nf_ct_get_tuple(const struct sk_buff *skb,
		unsigned int nhoff,
		unsigned int dataoff,
		u_int16_t l3num,
		u_int8_t protonum,
		struct net *net,
		struct nf_conntrack_tuple *tuple)
{
	unsigned int size;
	const __be32 *ap;
	__be32 _addrs[8];

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

	switch (protonum) {
#if IS_ENABLED(CONFIG_IPV6)
	case IPPROTO_ICMPV6:
		return icmpv6_pkt_to_tuple(skb, dataoff, net, tuple);
#endif
	case IPPROTO_ICMP:
		return icmp_pkt_to_tuple(skb, dataoff, net, tuple);
#ifdef CONFIG_NF_CT_PROTO_GRE
	case IPPROTO_GRE:
		return gre_pkt_to_tuple(skb, dataoff, net, tuple);
#endif
	case IPPROTO_TCP:
	case IPPROTO_UDP:
#ifdef CONFIG_NF_CT_PROTO_UDPLITE
	case IPPROTO_UDPLITE:
#endif
#ifdef CONFIG_NF_CT_PROTO_SCTP
	case IPPROTO_SCTP:
#endif
#ifdef CONFIG_NF_CT_PROTO_DCCP
	case IPPROTO_DCCP:
#endif
		/* fallthrough */
		return nf_ct_get_tuple_ports(skb, dataoff, tuple);
	default:
		break;
	}

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
	u8 protonum;
	int protoff;

	protoff = get_l4proto(skb, nhoff, l3num, &protonum);
	if (protoff <= 0)
		return false;

	return nf_ct_get_tuple(skb, nhoff, protoff, l3num, protonum, net, tuple);
}
EXPORT_SYMBOL_GPL(nf_ct_get_tuplepr);

bool
nf_ct_invert_tuple(struct nf_conntrack_tuple *inverse,
		   const struct nf_conntrack_tuple *orig)
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

	switch (orig->dst.protonum) {
	case IPPROTO_ICMP:
		return nf_conntrack_invert_icmp_tuple(inverse, orig);
#if IS_ENABLED(CONFIG_IPV6)
	case IPPROTO_ICMPV6:
		return nf_conntrack_invert_icmpv6_tuple(inverse, orig);
#endif
	}

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
	static siphash_aligned_key_t ct_id_seed;
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

#define NFCT_ALIGN(len)	(((len) + NFCT_INFOMASK) & ~NFCT_INFOMASK)

/* Released via nf_ct_destroy() */
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
	refcount_set(&tmpl->ct_general.use, 1);

	return tmpl;
}
EXPORT_SYMBOL_GPL(nf_ct_tmpl_alloc);

void nf_ct_tmpl_free(struct nf_conn *tmpl)
{
	kfree(tmpl->ext);

	if (ARCH_KMALLOC_MINALIGN <= NFCT_INFOMASK)
		kfree((char *)tmpl - tmpl->proto.tmpl_padto);
	else
		kfree(tmpl);
}
EXPORT_SYMBOL_GPL(nf_ct_tmpl_free);

static void destroy_gre_conntrack(struct nf_conn *ct)
{
#ifdef CONFIG_NF_CT_PROTO_GRE
	struct nf_conn *master = ct->master;

	if (master)
		nf_ct_gre_keymap_destroy(master);
#endif
}

void nf_ct_destroy(struct nf_conntrack *nfct)
{
	struct nf_conn *ct = (struct nf_conn *)nfct;

	pr_debug("%s(%p)\n", __func__, ct);
	WARN_ON(refcount_read(&nfct->use) != 0);

	if (unlikely(nf_ct_is_template(ct))) {
		nf_ct_tmpl_free(ct);
		return;
	}

	if (unlikely(nf_ct_protonum(ct) == IPPROTO_GRE))
		destroy_gre_conntrack(ct);

	/* Expectations will have been removed in clean_from_lists,
	 * except TFTP can create an expectation on the first packet,
	 * before connection is in the list, so we need to clean here,
	 * too.
	 */
	nf_ct_remove_expectations(ct);

	if (ct->master)
		nf_ct_put(ct->master);

	pr_debug("%s: returning ct=%p to slab\n", __func__, ct);
	nf_conntrack_free(ct);
}
EXPORT_SYMBOL(nf_ct_destroy);

static void __nf_ct_delete_from_lists(struct nf_conn *ct)
{
	struct net *net = nf_ct_net(ct);
	unsigned int hash, reply_hash;
	unsigned int sequence;

	do {
		sequence = read_seqcount_begin(&nf_conntrack_generation);
		hash = hash_conntrack(net,
				      &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple,
				      nf_ct_zone_id(nf_ct_zone(ct), IP_CT_DIR_ORIGINAL));
		reply_hash = hash_conntrack(net,
					   &ct->tuplehash[IP_CT_DIR_REPLY].tuple,
					   nf_ct_zone_id(nf_ct_zone(ct), IP_CT_DIR_REPLY));
	} while (nf_conntrack_double_lock(net, hash, reply_hash, sequence));

	clean_from_lists(ct);
	nf_conntrack_double_unlock(hash, reply_hash);
}

static void nf_ct_delete_from_lists(struct nf_conn *ct)
{
	nf_ct_helper_destroy(ct);
	local_bh_disable();

	__nf_ct_delete_from_lists(ct);

	local_bh_enable();
}

static void nf_ct_add_to_ecache_list(struct nf_conn *ct)
{
#ifdef CONFIG_NF_CONNTRACK_EVENTS
	struct nf_conntrack_net *cnet = nf_ct_pernet(nf_ct_net(ct));

	spin_lock(&cnet->ecache.dying_lock);
	hlist_nulls_add_head_rcu(&ct->tuplehash[IP_CT_DIR_ORIGINAL].hnnode,
				 &cnet->ecache.dying_list);
	spin_unlock(&cnet->ecache.dying_lock);
#endif
}

bool nf_ct_delete(struct nf_conn *ct, u32 portid, int report)
{
	struct nf_conn_tstamp *tstamp;
	struct net *net;

	if (test_and_set_bit(IPS_DYING_BIT, &ct->status))
		return false;

	tstamp = nf_conn_tstamp_find(ct);
	if (tstamp) {
		s32 timeout = READ_ONCE(ct->timeout) - nfct_time_stamp;

		tstamp->stop = ktime_get_real_ns();
		if (timeout < 0)
			tstamp->stop -= jiffies_to_nsecs(-timeout);
	}

	if (nf_conntrack_event_report(IPCT_DESTROY, ct,
				    portid, report) < 0) {
		/* destroy event was not delivered. nf_ct_put will
		 * be done by event cache worker on redelivery.
		 */
		nf_ct_helper_destroy(ct);
		local_bh_disable();
		__nf_ct_delete_from_lists(ct);
		nf_ct_add_to_ecache_list(ct);
		local_bh_enable();

		nf_conntrack_ecache_work(nf_ct_net(ct), NFCT_ECACHE_DESTROY_FAIL);
		return false;
	}

	net = nf_ct_net(ct);
	if (nf_conntrack_ecache_dwork_pending(net))
		nf_conntrack_ecache_work(net, NFCT_ECACHE_DESTROY_SENT);
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
	if (!refcount_inc_not_zero(&ct->ct_general.use))
		return;

	/* load ->status after refcount increase */
	smp_acquire__after_ctrl_dep();

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

	h = ____nf_conntrack_find(net, zone, tuple, hash);
	if (h) {
		/* We have a candidate that matches the tuple we're interested
		 * in, try to obtain a reference and re-check tuple
		 */
		ct = nf_ct_tuplehash_to_ctrack(h);
		if (likely(refcount_inc_not_zero(&ct->ct_general.use))) {
			/* re-check key after refcount */
			smp_acquire__after_ctrl_dep();

			if (likely(nf_ct_key_equal(h, tuple, zone, net)))
				goto found;

			/* TYPESAFE_BY_RCU recycled the candidate */
			nf_ct_put(ct);
		}

		h = NULL;
	}
found:
	rcu_read_unlock();

	return h;
}

struct nf_conntrack_tuple_hash *
nf_conntrack_find_get(struct net *net, const struct nf_conntrack_zone *zone,
		      const struct nf_conntrack_tuple *tuple)
{
	unsigned int rid, zone_id = nf_ct_zone_id(zone, IP_CT_DIR_ORIGINAL);
	struct nf_conntrack_tuple_hash *thash;

	thash = __nf_conntrack_find_get(net, zone, tuple,
					hash_conntrack_raw(tuple, zone_id, net));

	if (thash)
		return thash;

	rid = nf_ct_zone_id(zone, IP_CT_DIR_REPLY);
	if (rid != zone_id)
		return __nf_conntrack_find_get(net, zone, tuple,
					       hash_conntrack_raw(tuple, rid, net));
	return thash;
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

static bool nf_ct_ext_valid_pre(const struct nf_ct_ext *ext)
{
	/* if ext->gen_id is not equal to nf_conntrack_ext_genid, some extensions
	 * may contain stale pointers to e.g. helper that has been removed.
	 *
	 * The helper can't clear this because the nf_conn object isn't in
	 * any hash and synchronize_rcu() isn't enough because associated skb
	 * might sit in a queue.
	 */
	return !ext || ext->gen_id == atomic_read(&nf_conntrack_ext_genid);
}

static bool nf_ct_ext_valid_post(struct nf_ct_ext *ext)
{
	if (!ext)
		return true;

	if (ext->gen_id != atomic_read(&nf_conntrack_ext_genid))
		return false;

	/* inserted into conntrack table, nf_ct_iterate_cleanup()
	 * will find it.  Disable nf_ct_ext_find() id check.
	 */
	WRITE_ONCE(ext->gen_id, 0);
	return true;
}

int
nf_conntrack_hash_check_insert(struct nf_conn *ct)
{
	const struct nf_conntrack_zone *zone;
	struct net *net = nf_ct_net(ct);
	unsigned int hash, reply_hash;
	struct nf_conntrack_tuple_hash *h;
	struct hlist_nulls_node *n;
	unsigned int max_chainlen;
	unsigned int chainlen = 0;
	unsigned int sequence;
	int err = -EEXIST;

	zone = nf_ct_zone(ct);

	if (!nf_ct_ext_valid_pre(ct->ext))
		return -EAGAIN;

	local_bh_disable();
	do {
		sequence = read_seqcount_begin(&nf_conntrack_generation);
		hash = hash_conntrack(net,
				      &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple,
				      nf_ct_zone_id(nf_ct_zone(ct), IP_CT_DIR_ORIGINAL));
		reply_hash = hash_conntrack(net,
					   &ct->tuplehash[IP_CT_DIR_REPLY].tuple,
					   nf_ct_zone_id(nf_ct_zone(ct), IP_CT_DIR_REPLY));
	} while (nf_conntrack_double_lock(net, hash, reply_hash, sequence));

	max_chainlen = MIN_CHAINLEN + prandom_u32_max(MAX_CHAINLEN);

	/* See if there's one in the list already, including reverse */
	hlist_nulls_for_each_entry(h, n, &nf_conntrack_hash[hash], hnnode) {
		if (nf_ct_key_equal(h, &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple,
				    zone, net))
			goto out;

		if (chainlen++ > max_chainlen)
			goto chaintoolong;
	}

	chainlen = 0;

	hlist_nulls_for_each_entry(h, n, &nf_conntrack_hash[reply_hash], hnnode) {
		if (nf_ct_key_equal(h, &ct->tuplehash[IP_CT_DIR_REPLY].tuple,
				    zone, net))
			goto out;
		if (chainlen++ > max_chainlen)
			goto chaintoolong;
	}

	/* If genid has changed, we can't insert anymore because ct
	 * extensions could have stale pointers and nf_ct_iterate_destroy
	 * might have completed its table scan already.
	 *
	 * Increment of the ext genid right after this check is fine:
	 * nf_ct_iterate_destroy blocks until locks are released.
	 */
	if (!nf_ct_ext_valid_post(ct->ext)) {
		err = -EAGAIN;
		goto out;
	}

	ct->status |= IPS_CONFIRMED;
	smp_wmb();
	/* The caller holds a reference to this object */
	refcount_set(&ct->ct_general.use, 2);
	__nf_conntrack_hash_insert(ct, hash, reply_hash);
	nf_conntrack_double_unlock(hash, reply_hash);
	NF_CT_STAT_INC(net, insert);
	local_bh_enable();

	return 0;
chaintoolong:
	NF_CT_STAT_INC(net, chaintoolong);
	err = -ENOSPC;
out:
	nf_conntrack_double_unlock(hash, reply_hash);
	local_bh_enable();
	return err;
}
EXPORT_SYMBOL_GPL(nf_conntrack_hash_check_insert);

void nf_ct_acct_add(struct nf_conn *ct, u32 dir, unsigned int packets,
		    unsigned int bytes)
{
	struct nf_conn_acct *acct;

	acct = nf_conn_acct_find(ct);
	if (acct) {
		struct nf_conn_counter *counter = acct->counter;

		atomic64_add(packets, &counter[dir].packets);
		atomic64_add(bytes, &counter[dir].bytes);
	}
}
EXPORT_SYMBOL_GPL(nf_ct_acct_add);

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
		nf_ct_acct_update(ct, CTINFO2DIR(ctinfo), bytes);
	}
}

static void __nf_conntrack_insert_prepare(struct nf_conn *ct)
{
	struct nf_conn_tstamp *tstamp;

	refcount_inc(&ct->ct_general.use);

	/* set conntrack timestamp, if enabled. */
	tstamp = nf_conn_tstamp_find(ct);
	if (tstamp)
		tstamp->start = ktime_get_real_ns();
}

/* caller must hold locks to prevent concurrent changes */
static int __nf_ct_resolve_clash(struct sk_buff *skb,
				 struct nf_conntrack_tuple_hash *h)
{
	/* This is the conntrack entry already in hashes that won race. */
	struct nf_conn *ct = nf_ct_tuplehash_to_ctrack(h);
	enum ip_conntrack_info ctinfo;
	struct nf_conn *loser_ct;

	loser_ct = nf_ct_get(skb, &ctinfo);

	if (nf_ct_is_dying(ct))
		return NF_DROP;

	if (((ct->status & IPS_NAT_DONE_MASK) == 0) ||
	    nf_ct_match(ct, loser_ct)) {
		struct net *net = nf_ct_net(ct);

		nf_conntrack_get(&ct->ct_general);

		nf_ct_acct_merge(ct, ctinfo, loser_ct);
		nf_ct_put(loser_ct);
		nf_ct_set(skb, ct, ctinfo);

		NF_CT_STAT_INC(net, clash_resolve);
		return NF_ACCEPT;
	}

	return NF_DROP;
}

/**
 * nf_ct_resolve_clash_harder - attempt to insert clashing conntrack entry
 *
 * @skb: skb that causes the collision
 * @repl_idx: hash slot for reply direction
 *
 * Called when origin or reply direction had a clash.
 * The skb can be handled without packet drop provided the reply direction
 * is unique or there the existing entry has the identical tuple in both
 * directions.
 *
 * Caller must hold conntrack table locks to prevent concurrent updates.
 *
 * Returns NF_DROP if the clash could not be handled.
 */
static int nf_ct_resolve_clash_harder(struct sk_buff *skb, u32 repl_idx)
{
	struct nf_conn *loser_ct = (struct nf_conn *)skb_nfct(skb);
	const struct nf_conntrack_zone *zone;
	struct nf_conntrack_tuple_hash *h;
	struct hlist_nulls_node *n;
	struct net *net;

	zone = nf_ct_zone(loser_ct);
	net = nf_ct_net(loser_ct);

	/* Reply direction must never result in a clash, unless both origin
	 * and reply tuples are identical.
	 */
	hlist_nulls_for_each_entry(h, n, &nf_conntrack_hash[repl_idx], hnnode) {
		if (nf_ct_key_equal(h,
				    &loser_ct->tuplehash[IP_CT_DIR_REPLY].tuple,
				    zone, net))
			return __nf_ct_resolve_clash(skb, h);
	}

	/* We want the clashing entry to go away real soon: 1 second timeout. */
	WRITE_ONCE(loser_ct->timeout, nfct_time_stamp + HZ);

	/* IPS_NAT_CLASH removes the entry automatically on the first
	 * reply.  Also prevents UDP tracker from moving the entry to
	 * ASSURED state, i.e. the entry can always be evicted under
	 * pressure.
	 */
	loser_ct->status |= IPS_FIXED_TIMEOUT | IPS_NAT_CLASH;

	__nf_conntrack_insert_prepare(loser_ct);

	/* fake add for ORIGINAL dir: we want lookups to only find the entry
	 * already in the table.  This also hides the clashing entry from
	 * ctnetlink iteration, i.e. conntrack -L won't show them.
	 */
	hlist_nulls_add_fake(&loser_ct->tuplehash[IP_CT_DIR_ORIGINAL].hnnode);

	hlist_nulls_add_head_rcu(&loser_ct->tuplehash[IP_CT_DIR_REPLY].hnnode,
				 &nf_conntrack_hash[repl_idx]);

	NF_CT_STAT_INC(net, clash_resolve);
	return NF_ACCEPT;
}

/**
 * nf_ct_resolve_clash - attempt to handle clash without packet drop
 *
 * @skb: skb that causes the clash
 * @h: tuplehash of the clashing entry already in table
 * @reply_hash: hash slot for reply direction
 *
 * A conntrack entry can be inserted to the connection tracking table
 * if there is no existing entry with an identical tuple.
 *
 * If there is one, @skb (and the assocated, unconfirmed conntrack) has
 * to be dropped.  In case @skb is retransmitted, next conntrack lookup
 * will find the already-existing entry.
 *
 * The major problem with such packet drop is the extra delay added by
 * the packet loss -- it will take some time for a retransmit to occur
 * (or the sender to time out when waiting for a reply).
 *
 * This function attempts to handle the situation without packet drop.
 *
 * If @skb has no NAT transformation or if the colliding entries are
 * exactly the same, only the to-be-confirmed conntrack entry is discarded
 * and @skb is associated with the conntrack entry already in the table.
 *
 * Failing that, the new, unconfirmed conntrack is still added to the table
 * provided that the collision only occurs in the ORIGINAL direction.
 * The new entry will be added only in the non-clashing REPLY direction,
 * so packets in the ORIGINAL direction will continue to match the existing
 * entry.  The new entry will also have a fixed timeout so it expires --
 * due to the collision, it will only see reply traffic.
 *
 * Returns NF_DROP if the clash could not be resolved.
 */
static __cold noinline int
nf_ct_resolve_clash(struct sk_buff *skb, struct nf_conntrack_tuple_hash *h,
		    u32 reply_hash)
{
	/* This is the conntrack entry already in hashes that won race. */
	struct nf_conn *ct = nf_ct_tuplehash_to_ctrack(h);
	const struct nf_conntrack_l4proto *l4proto;
	enum ip_conntrack_info ctinfo;
	struct nf_conn *loser_ct;
	struct net *net;
	int ret;

	loser_ct = nf_ct_get(skb, &ctinfo);
	net = nf_ct_net(loser_ct);

	l4proto = nf_ct_l4proto_find(nf_ct_protonum(ct));
	if (!l4proto->allow_clash)
		goto drop;

	ret = __nf_ct_resolve_clash(skb, h);
	if (ret == NF_ACCEPT)
		return ret;

	ret = nf_ct_resolve_clash_harder(skb, reply_hash);
	if (ret == NF_ACCEPT)
		return ret;

drop:
	NF_CT_STAT_INC(net, drop);
	NF_CT_STAT_INC(net, insert_failed);
	return NF_DROP;
}

/* Confirm a connection given skb; places it in hash table */
int
__nf_conntrack_confirm(struct sk_buff *skb)
{
	unsigned int chainlen = 0, sequence, max_chainlen;
	const struct nf_conntrack_zone *zone;
	unsigned int hash, reply_hash;
	struct nf_conntrack_tuple_hash *h;
	struct nf_conn *ct;
	struct nf_conn_help *help;
	struct hlist_nulls_node *n;
	enum ip_conntrack_info ctinfo;
	struct net *net;
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
					   &ct->tuplehash[IP_CT_DIR_REPLY].tuple,
					   nf_ct_zone_id(nf_ct_zone(ct), IP_CT_DIR_REPLY));
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

	if (!nf_ct_ext_valid_pre(ct->ext)) {
		NF_CT_STAT_INC(net, insert_failed);
		goto dying;
	}

	pr_debug("Confirming conntrack %p\n", ct);
	/* We have to check the DYING flag after unlink to prevent
	 * a race against nf_ct_get_next_corpse() possibly called from
	 * user context, else we insert an already 'dead' hash, blocking
	 * further use of that particular connection -JM.
	 */
	ct->status |= IPS_CONFIRMED;

	if (unlikely(nf_ct_is_dying(ct))) {
		NF_CT_STAT_INC(net, insert_failed);
		goto dying;
	}

	max_chainlen = MIN_CHAINLEN + prandom_u32_max(MAX_CHAINLEN);
	/* See if there's one in the list already, including reverse:
	   NAT could have grabbed it without realizing, since we're
	   not in the hash.  If there is, we lost race. */
	hlist_nulls_for_each_entry(h, n, &nf_conntrack_hash[hash], hnnode) {
		if (nf_ct_key_equal(h, &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple,
				    zone, net))
			goto out;
		if (chainlen++ > max_chainlen)
			goto chaintoolong;
	}

	chainlen = 0;
	hlist_nulls_for_each_entry(h, n, &nf_conntrack_hash[reply_hash], hnnode) {
		if (nf_ct_key_equal(h, &ct->tuplehash[IP_CT_DIR_REPLY].tuple,
				    zone, net))
			goto out;
		if (chainlen++ > max_chainlen) {
chaintoolong:
			NF_CT_STAT_INC(net, chaintoolong);
			NF_CT_STAT_INC(net, insert_failed);
			ret = NF_DROP;
			goto dying;
		}
	}

	/* Timer relative to confirmation time, not original
	   setting time, otherwise we'd get timer wrap in
	   weird delay cases. */
	ct->timeout += nfct_time_stamp;

	__nf_conntrack_insert_prepare(ct);

	/* Since the lookup is lockless, hash insertion must be done after
	 * starting the timer and setting the CONFIRMED bit. The RCU barriers
	 * guarantee that no other CPU can find the conntrack before the above
	 * stores are visible.
	 */
	__nf_conntrack_hash_insert(ct, hash, reply_hash);
	nf_conntrack_double_unlock(hash, reply_hash);
	local_bh_enable();

	/* ext area is still valid (rcu read lock is held,
	 * but will go out of scope soon, we need to remove
	 * this conntrack again.
	 */
	if (!nf_ct_ext_valid_post(ct->ext)) {
		nf_ct_kill(ct);
		NF_CT_STAT_INC_ATOMIC(net, drop);
		return NF_DROP;
	}

	help = nfct_help(ct);
	if (help && help->helper)
		nf_conntrack_event_cache(IPCT_HELPER, ct);

	nf_conntrack_event_cache(master_ct(ct) ?
				 IPCT_RELATED : IPCT_NEW, ct);
	return NF_ACCEPT;

out:
	ret = nf_ct_resolve_clash(skb, h, reply_hash);
dying:
	nf_conntrack_double_unlock(hash, reply_hash);
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
	hash = __hash_conntrack(net, tuple, nf_ct_zone_id(zone, IP_CT_DIR_REPLY), hsize);

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
					      &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple) &&
					      nf_ct_zone_equal(ct, zone, IP_CT_DIR_ORIGINAL))
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

		if (!refcount_inc_not_zero(&tmp->ct_general.use))
			continue;

		/* load ->ct_net and ->status after refcount increase */
		smp_acquire__after_ctrl_dep();

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

	l4proto = nf_ct_l4proto_find(nf_ct_protonum(ct));
	if (l4proto->can_early_drop && l4proto->can_early_drop(ct))
		return true;

	return false;
}

static void gc_worker(struct work_struct *work)
{
	unsigned int i, hashsz, nf_conntrack_max95 = 0;
	u32 end_time, start_time = nfct_time_stamp;
	struct conntrack_gc_work *gc_work;
	unsigned int expired_count = 0;
	unsigned long next_run;
	s32 delta_time;
	long count;

	gc_work = container_of(work, struct conntrack_gc_work, dwork.work);

	i = gc_work->next_bucket;
	if (gc_work->early_drop)
		nf_conntrack_max95 = nf_conntrack_max / 100u * 95u;

	if (i == 0) {
		gc_work->avg_timeout = GC_SCAN_INTERVAL_INIT;
		gc_work->count = GC_SCAN_INITIAL_COUNT;
		gc_work->start_time = start_time;
	}

	next_run = gc_work->avg_timeout;
	count = gc_work->count;

	end_time = start_time + GC_SCAN_MAX_DURATION;

	do {
		struct nf_conntrack_tuple_hash *h;
		struct hlist_nulls_head *ct_hash;
		struct hlist_nulls_node *n;
		struct nf_conn *tmp;

		rcu_read_lock();

		nf_conntrack_get_ht(&ct_hash, &hashsz);
		if (i >= hashsz) {
			rcu_read_unlock();
			break;
		}

		hlist_nulls_for_each_entry_rcu(h, n, &ct_hash[i], hnnode) {
			struct nf_conntrack_net *cnet;
			struct net *net;
			long expires;

			tmp = nf_ct_tuplehash_to_ctrack(h);

			if (test_bit(IPS_OFFLOAD_BIT, &tmp->status)) {
				nf_ct_offload_timeout(tmp);
				continue;
			}

			if (expired_count > GC_SCAN_EXPIRED_MAX) {
				rcu_read_unlock();

				gc_work->next_bucket = i;
				gc_work->avg_timeout = next_run;
				gc_work->count = count;

				delta_time = nfct_time_stamp - gc_work->start_time;

				/* re-sched immediately if total cycle time is exceeded */
				next_run = delta_time < (s32)GC_SCAN_INTERVAL_MAX;
				goto early_exit;
			}

			if (nf_ct_is_expired(tmp)) {
				nf_ct_gc_expired(tmp);
				expired_count++;
				continue;
			}

			expires = clamp(nf_ct_expires(tmp), GC_SCAN_INTERVAL_MIN, GC_SCAN_INTERVAL_CLAMP);
			expires = (expires - (long)next_run) / ++count;
			next_run += expires;

			if (nf_conntrack_max95 == 0 || gc_worker_skip_ct(tmp))
				continue;

			net = nf_ct_net(tmp);
			cnet = nf_ct_pernet(net);
			if (atomic_read(&cnet->count) < nf_conntrack_max95)
				continue;

			/* need to take reference to avoid possible races */
			if (!refcount_inc_not_zero(&tmp->ct_general.use))
				continue;

			/* load ->status after refcount increase */
			smp_acquire__after_ctrl_dep();

			if (gc_worker_skip_ct(tmp)) {
				nf_ct_put(tmp);
				continue;
			}

			if (gc_worker_can_early_drop(tmp)) {
				nf_ct_kill(tmp);
				expired_count++;
			}

			nf_ct_put(tmp);
		}

		/* could check get_nulls_value() here and restart if ct
		 * was moved to another chain.  But given gc is best-effort
		 * we will just continue with next hash slot.
		 */
		rcu_read_unlock();
		cond_resched();
		i++;

		delta_time = nfct_time_stamp - end_time;
		if (delta_time > 0 && i < hashsz) {
			gc_work->avg_timeout = next_run;
			gc_work->count = count;
			gc_work->next_bucket = i;
			next_run = 0;
			goto early_exit;
		}
	} while (i < hashsz);

	gc_work->next_bucket = 0;

	next_run = clamp(next_run, GC_SCAN_INTERVAL_MIN, GC_SCAN_INTERVAL_MAX);

	delta_time = max_t(s32, nfct_time_stamp - gc_work->start_time, 1);
	if (next_run > (unsigned long)delta_time)
		next_run -= delta_time;
	else
		next_run = 1;

early_exit:
	if (gc_work->exiting)
		return;

	if (next_run)
		gc_work->early_drop = false;

	queue_delayed_work(system_power_efficient_wq, &gc_work->dwork, next_run);
}

static void conntrack_gc_work_init(struct conntrack_gc_work *gc_work)
{
	INIT_DELAYED_WORK(&gc_work->dwork, gc_worker);
	gc_work->exiting = false;
}

static struct nf_conn *
__nf_conntrack_alloc(struct net *net,
		     const struct nf_conntrack_zone *zone,
		     const struct nf_conntrack_tuple *orig,
		     const struct nf_conntrack_tuple *repl,
		     gfp_t gfp, u32 hash)
{
	struct nf_conntrack_net *cnet = nf_ct_pernet(net);
	unsigned int ct_count;
	struct nf_conn *ct;

	/* We don't want any race condition at early drop stage */
	ct_count = atomic_inc_return(&cnet->count);

	if (nf_conntrack_max && unlikely(ct_count > nf_conntrack_max)) {
		if (!early_drop(net, hash)) {
			if (!conntrack_gc_work.early_drop)
				conntrack_gc_work.early_drop = true;
			atomic_dec(&cnet->count);
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
	WRITE_ONCE(ct->timeout, 0);
	write_pnet(&ct->ct_net, net);
	memset_after(ct, 0, __nfct_init_offset);

	nf_ct_zone_add(ct, zone);

	trace_android_rvh_nf_conn_alloc(ct);

	/* Because we use RCU lookups, we set ct_general.use to zero before
	 * this is inserted in any list.
	 */
	refcount_set(&ct->ct_general.use, 0);
	return ct;
out:
	atomic_dec(&cnet->count);
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
	struct nf_conntrack_net *cnet;

	/* A freed object has refcnt == 0, that's
	 * the golden rule for SLAB_TYPESAFE_BY_RCU
	 */
	WARN_ON(refcount_read(&ct->ct_general.use) != 0);

	if (ct->status & IPS_SRC_NAT_DONE) {
		const struct nf_nat_hook *nat_hook;

		rcu_read_lock();
		nat_hook = rcu_dereference(nf_nat_hook);
		if (nat_hook)
			nat_hook->remove_nat_bysrc(ct);
		rcu_read_unlock();
	}

	kfree(ct->ext);
	kmem_cache_free(nf_conntrack_cachep, ct);
	cnet = nf_ct_pernet(net);

	smp_mb__before_atomic();
	trace_android_rvh_nf_conn_free(ct);
	atomic_dec(&cnet->count);
}
EXPORT_SYMBOL_GPL(nf_conntrack_free);


/* Allocate a new conntrack: we return -ENOMEM if classification
   failed due to stress.  Otherwise it really is unclassifiable. */
static noinline struct nf_conntrack_tuple_hash *
init_conntrack(struct net *net, struct nf_conn *tmpl,
	       const struct nf_conntrack_tuple *tuple,
	       struct sk_buff *skb,
	       unsigned int dataoff, u32 hash)
{
	struct nf_conn *ct;
	struct nf_conn_help *help;
	struct nf_conntrack_tuple repl_tuple;
#ifdef CONFIG_NF_CONNTRACK_EVENTS
	struct nf_conntrack_ecache *ecache;
#endif
	struct nf_conntrack_expect *exp = NULL;
	const struct nf_conntrack_zone *zone;
	struct nf_conn_timeout *timeout_ext;
	struct nf_conntrack_zone tmp;
	struct nf_conntrack_net *cnet;

	if (!nf_ct_invert_tuple(&repl_tuple, tuple)) {
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

	if (timeout_ext)
		nf_ct_timeout_ext_add(ct, rcu_dereference(timeout_ext->timeout),
				      GFP_ATOMIC);

	nf_ct_acct_ext_add(ct, GFP_ATOMIC);
	nf_ct_tstamp_ext_add(ct, GFP_ATOMIC);
	nf_ct_labels_ext_add(ct);

#ifdef CONFIG_NF_CONNTRACK_EVENTS
	ecache = tmpl ? nf_ct_ecache_find(tmpl) : NULL;

	if ((ecache || net->ct.sysctl_events) &&
	    !nf_ct_ecache_ext_add(ct, ecache ? ecache->ctmask : 0,
				  ecache ? ecache->expmask : 0,
				  GFP_ATOMIC)) {
		nf_conntrack_free(ct);
		return ERR_PTR(-ENOMEM);
	}
#endif

	cnet = nf_ct_pernet(net);
	if (cnet->expect_count) {
		spin_lock_bh(&nf_conntrack_expect_lock);
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
			ct->mark = READ_ONCE(exp->master->mark);
#endif
#ifdef CONFIG_NF_CONNTRACK_SECMARK
			ct->secmark = exp->master->secmark;
#endif
			NF_CT_STAT_INC(net, expect_new);
		}
		spin_unlock_bh(&nf_conntrack_expect_lock);
	}
	if (!exp && tmpl)
		__nf_ct_try_assign_helper(ct, tmpl, GFP_ATOMIC);

	/* Other CPU might have obtained a pointer to this object before it was
	 * released.  Because refcount is 0, refcount_inc_not_zero() will fail.
	 *
	 * After refcount_set(1) it will succeed; ensure that zeroing of
	 * ct->status and the correct ct->net pointer are visible; else other
	 * core might observe CONFIRMED bit which means the entry is valid and
	 * in the hash table, but its not (anymore).
	 */
	smp_wmb();

	/* Now it is going to be associated with an sk_buff, set refcount to 1. */
	refcount_set(&ct->ct_general.use, 1);

	if (exp) {
		if (exp->expectfn)
			exp->expectfn(ct, exp);
		nf_ct_expect_put(exp);
	}

	return &ct->tuplehash[IP_CT_DIR_ORIGINAL];
}

/* On success, returns 0, sets skb->_nfct | ctinfo */
static int
resolve_normal_ct(struct nf_conn *tmpl,
		  struct sk_buff *skb,
		  unsigned int dataoff,
		  u_int8_t protonum,
		  const struct nf_hook_state *state)
{
	const struct nf_conntrack_zone *zone;
	struct nf_conntrack_tuple tuple;
	struct nf_conntrack_tuple_hash *h;
	enum ip_conntrack_info ctinfo;
	struct nf_conntrack_zone tmp;
	u32 hash, zone_id, rid;
	struct nf_conn *ct;

	if (!nf_ct_get_tuple(skb, skb_network_offset(skb),
			     dataoff, state->pf, protonum, state->net,
			     &tuple)) {
		pr_debug("Can't get tuple\n");
		return 0;
	}

	/* look for tuple match */
	zone = nf_ct_zone_tmpl(tmpl, skb, &tmp);

	zone_id = nf_ct_zone_id(zone, IP_CT_DIR_ORIGINAL);
	hash = hash_conntrack_raw(&tuple, zone_id, state->net);
	h = __nf_conntrack_find_get(state->net, zone, &tuple, hash);

	if (!h) {
		rid = nf_ct_zone_id(zone, IP_CT_DIR_REPLY);
		if (zone_id != rid) {
			u32 tmp = hash_conntrack_raw(&tuple, rid, state->net);

			h = __nf_conntrack_find_get(state->net, zone, &tuple, tmp);
		}
	}

	if (!h) {
		h = init_conntrack(state->net, tmpl, &tuple,
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

/*
 * icmp packets need special treatment to handle error messages that are
 * related to a connection.
 *
 * Callers need to check if skb has a conntrack assigned when this
 * helper returns; in such case skb belongs to an already known connection.
 */
static unsigned int __cold
nf_conntrack_handle_icmp(struct nf_conn *tmpl,
			 struct sk_buff *skb,
			 unsigned int dataoff,
			 u8 protonum,
			 const struct nf_hook_state *state)
{
	int ret;

	if (state->pf == NFPROTO_IPV4 && protonum == IPPROTO_ICMP)
		ret = nf_conntrack_icmpv4_error(tmpl, skb, dataoff, state);
#if IS_ENABLED(CONFIG_IPV6)
	else if (state->pf == NFPROTO_IPV6 && protonum == IPPROTO_ICMPV6)
		ret = nf_conntrack_icmpv6_error(tmpl, skb, dataoff, state);
#endif
	else
		return NF_ACCEPT;

	if (ret <= 0)
		NF_CT_STAT_INC_ATOMIC(state->net, error);

	return ret;
}

static int generic_packet(struct nf_conn *ct, struct sk_buff *skb,
			  enum ip_conntrack_info ctinfo)
{
	const unsigned int *timeout = nf_ct_timeout_lookup(ct);

	if (!timeout)
		timeout = &nf_generic_pernet(nf_ct_net(ct))->timeout;

	nf_ct_refresh_acct(ct, ctinfo, skb, *timeout);
	return NF_ACCEPT;
}

/* Returns verdict for packet, or -1 for invalid. */
static int nf_conntrack_handle_packet(struct nf_conn *ct,
				      struct sk_buff *skb,
				      unsigned int dataoff,
				      enum ip_conntrack_info ctinfo,
				      const struct nf_hook_state *state)
{
	switch (nf_ct_protonum(ct)) {
	case IPPROTO_TCP:
		return nf_conntrack_tcp_packet(ct, skb, dataoff,
					       ctinfo, state);
	case IPPROTO_UDP:
		return nf_conntrack_udp_packet(ct, skb, dataoff,
					       ctinfo, state);
	case IPPROTO_ICMP:
		return nf_conntrack_icmp_packet(ct, skb, ctinfo, state);
#if IS_ENABLED(CONFIG_IPV6)
	case IPPROTO_ICMPV6:
		return nf_conntrack_icmpv6_packet(ct, skb, ctinfo, state);
#endif
#ifdef CONFIG_NF_CT_PROTO_UDPLITE
	case IPPROTO_UDPLITE:
		return nf_conntrack_udplite_packet(ct, skb, dataoff,
						   ctinfo, state);
#endif
#ifdef CONFIG_NF_CT_PROTO_SCTP
	case IPPROTO_SCTP:
		return nf_conntrack_sctp_packet(ct, skb, dataoff,
						ctinfo, state);
#endif
#ifdef CONFIG_NF_CT_PROTO_DCCP
	case IPPROTO_DCCP:
		return nf_conntrack_dccp_packet(ct, skb, dataoff,
						ctinfo, state);
#endif
#ifdef CONFIG_NF_CT_PROTO_GRE
	case IPPROTO_GRE:
		return nf_conntrack_gre_packet(ct, skb, dataoff,
					       ctinfo, state);
#endif
	}

	return generic_packet(ct, skb, ctinfo);
}

unsigned int
nf_conntrack_in(struct sk_buff *skb, const struct nf_hook_state *state)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct, *tmpl;
	u_int8_t protonum;
	int dataoff, ret;

	tmpl = nf_ct_get(skb, &ctinfo);
	if (tmpl || ctinfo == IP_CT_UNTRACKED) {
		/* Previously seen (loopback or untracked)?  Ignore. */
		if ((tmpl && !nf_ct_is_template(tmpl)) ||
		     ctinfo == IP_CT_UNTRACKED)
			return NF_ACCEPT;
		skb->_nfct = 0;
	}

	/* rcu_read_lock()ed by nf_hook_thresh */
	dataoff = get_l4proto(skb, skb_network_offset(skb), state->pf, &protonum);
	if (dataoff <= 0) {
		pr_debug("not prepared to track yet or error occurred\n");
		NF_CT_STAT_INC_ATOMIC(state->net, invalid);
		ret = NF_ACCEPT;
		goto out;
	}

	if (protonum == IPPROTO_ICMP || protonum == IPPROTO_ICMPV6) {
		ret = nf_conntrack_handle_icmp(tmpl, skb, dataoff,
					       protonum, state);
		if (ret <= 0) {
			ret = -ret;
			goto out;
		}
		/* ICMP[v6] protocol trackers may assign one conntrack. */
		if (skb->_nfct)
			goto out;
	}
repeat:
	ret = resolve_normal_ct(tmpl, skb, dataoff,
				protonum, state);
	if (ret < 0) {
		/* Too stressed to deal. */
		NF_CT_STAT_INC_ATOMIC(state->net, drop);
		ret = NF_DROP;
		goto out;
	}

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct) {
		/* Not valid part of a connection */
		NF_CT_STAT_INC_ATOMIC(state->net, invalid);
		ret = NF_ACCEPT;
		goto out;
	}

	ret = nf_conntrack_handle_packet(ct, skb, dataoff, ctinfo, state);
	if (ret <= 0) {
		/* Invalid: inverse of the return code tells
		 * the netfilter core what to do */
		pr_debug("nf_conntrack_in: Can't track with proto module\n");
		nf_ct_put(ct);
		skb->_nfct = 0;
		/* Special case: TCP tracker reports an attempt to reopen a
		 * closed/aborted connection. We have to go back and create a
		 * fresh conntrack.
		 */
		if (ret == -NF_REPEAT)
			goto repeat;

		NF_CT_STAT_INC_ATOMIC(state->net, invalid);
		if (ret == -NF_DROP)
			NF_CT_STAT_INC_ATOMIC(state->net, drop);

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
}
EXPORT_SYMBOL_GPL(nf_conntrack_alter_reply);

/* Refresh conntrack for this many jiffies and do accounting if do_acct is 1 */
void __nf_ct_refresh_acct(struct nf_conn *ct,
			  enum ip_conntrack_info ctinfo,
			  const struct sk_buff *skb,
			  u32 extra_jiffies,
			  bool do_acct)
{
	/* Only update if this is not a fixed timeout */
	if (test_bit(IPS_FIXED_TIMEOUT_BIT, &ct->status))
		goto acct;

	/* If not in hash table, timer will not be active yet */
	if (nf_ct_is_confirmed(ct))
		extra_jiffies += nfct_time_stamp;

	if (READ_ONCE(ct->timeout) != extra_jiffies)
		WRITE_ONCE(ct->timeout, extra_jiffies);
acct:
	if (do_acct)
		nf_ct_acct_update(ct, CTINFO2DIR(ctinfo), skb->len);
}
EXPORT_SYMBOL_GPL(__nf_ct_refresh_acct);

bool nf_ct_kill_acct(struct nf_conn *ct,
		     enum ip_conntrack_info ctinfo,
		     const struct sk_buff *skb)
{
	nf_ct_acct_update(ct, CTINFO2DIR(ctinfo), skb->len);

	return nf_ct_delete(ct, 0, 0);
}
EXPORT_SYMBOL_GPL(nf_ct_kill_acct);

#if IS_ENABLED(CONFIG_NF_CT_NETLINK)

#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_conntrack.h>
#include <linux/mutex.h>

/* Generic function for tcp/udp/sctp/dccp and alike. */
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
			       struct nf_conntrack_tuple *t,
			       u_int32_t flags)
{
	if (flags & CTA_FILTER_FLAG(CTA_PROTO_SRC_PORT)) {
		if (!tb[CTA_PROTO_SRC_PORT])
			return -EINVAL;

		t->src.u.tcp.port = nla_get_be16(tb[CTA_PROTO_SRC_PORT]);
	}

	if (flags & CTA_FILTER_FLAG(CTA_PROTO_DST_PORT)) {
		if (!tb[CTA_PROTO_DST_PORT])
			return -EINVAL;

		t->dst.u.tcp.port = nla_get_be16(tb[CTA_PROTO_DST_PORT]);
	}

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

static int __nf_conntrack_update(struct net *net, struct sk_buff *skb,
				 struct nf_conn *ct,
				 enum ip_conntrack_info ctinfo)
{
	const struct nf_nat_hook *nat_hook;
	struct nf_conntrack_tuple_hash *h;
	struct nf_conntrack_tuple tuple;
	unsigned int status;
	int dataoff;
	u16 l3num;
	u8 l4num;

	l3num = nf_ct_l3num(ct);

	dataoff = get_l4proto(skb, skb_network_offset(skb), l3num, &l4num);
	if (dataoff <= 0)
		return -1;

	if (!nf_ct_get_tuple(skb, skb_network_offset(skb), dataoff, l3num,
			     l4num, net, &tuple))
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

/* This packet is coming from userspace via nf_queue, complete the packet
 * processing after the helper invocation in nf_confirm().
 */
static int nf_confirm_cthelper(struct sk_buff *skb, struct nf_conn *ct,
			       enum ip_conntrack_info ctinfo)
{
	const struct nf_conntrack_helper *helper;
	const struct nf_conn_help *help;
	int protoff;

	help = nfct_help(ct);
	if (!help)
		return 0;

	helper = rcu_dereference(help->helper);
	if (!(helper->flags & NF_CT_HELPER_F_USERSPACE))
		return 0;

	switch (nf_ct_l3num(ct)) {
	case NFPROTO_IPV4:
		protoff = skb_network_offset(skb) + ip_hdrlen(skb);
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case NFPROTO_IPV6: {
		__be16 frag_off;
		u8 pnum;

		pnum = ipv6_hdr(skb)->nexthdr;
		protoff = ipv6_skip_exthdr(skb, sizeof(struct ipv6hdr), &pnum,
					   &frag_off);
		if (protoff < 0 || (frag_off & htons(~0x7)) != 0)
			return 0;
		break;
	}
#endif
	default:
		return 0;
	}

	if (test_bit(IPS_SEQ_ADJUST_BIT, &ct->status) &&
	    !nf_is_loopback_packet(skb)) {
		if (!nf_ct_seq_adjust(skb, ct, ctinfo, protoff)) {
			NF_CT_STAT_INC_ATOMIC(nf_ct_net(ct), drop);
			return -1;
		}
	}

	/* We've seen it coming out the other side: confirm it */
	return nf_conntrack_confirm(skb) == NF_DROP ? - 1 : 0;
}

static int nf_conntrack_update(struct net *net, struct sk_buff *skb)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct;
	int err;

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct)
		return 0;

	if (!nf_ct_is_confirmed(ct)) {
		err = __nf_conntrack_update(net, skb, ct, ctinfo);
		if (err < 0)
			return err;

		ct = nf_ct_get(skb, &ctinfo);
	}

	return nf_confirm_cthelper(skb, ct, ctinfo);
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
		const struct nf_ct_iter_data *iter_data, unsigned int *bucket)
{
	struct nf_conntrack_tuple_hash *h;
	struct nf_conn *ct;
	struct hlist_nulls_node *n;
	spinlock_t *lockp;

	for (; *bucket < nf_conntrack_htable_size; (*bucket)++) {
		struct hlist_nulls_head *hslot = &nf_conntrack_hash[*bucket];

		if (hlist_nulls_empty(hslot))
			continue;

		lockp = &nf_conntrack_locks[*bucket % CONNTRACK_LOCKS];
		local_bh_disable();
		nf_conntrack_lock(lockp);
		hlist_nulls_for_each_entry(h, n, hslot, hnnode) {
			if (NF_CT_DIRECTION(h) != IP_CT_DIR_REPLY)
				continue;
			/* All nf_conn objects are added to hash table twice, one
			 * for original direction tuple, once for the reply tuple.
			 *
			 * Exception: In the IPS_NAT_CLASH case, only the reply
			 * tuple is added (the original tuple already existed for
			 * a different object).
			 *
			 * We only need to call the iterator once for each
			 * conntrack, so we just use the 'reply' direction
			 * tuple while iterating.
			 */
			ct = nf_ct_tuplehash_to_ctrack(h);

			if (iter_data->net &&
			    !net_eq(iter_data->net, nf_ct_net(ct)))
				continue;

			if (iter(ct, iter_data->data))
				goto found;
		}
		spin_unlock(lockp);
		local_bh_enable();
		cond_resched();
	}

	return NULL;
found:
	refcount_inc(&ct->ct_general.use);
	spin_unlock(lockp);
	local_bh_enable();
	return ct;
}

static void nf_ct_iterate_cleanup(int (*iter)(struct nf_conn *i, void *data),
				  const struct nf_ct_iter_data *iter_data)
{
	unsigned int bucket = 0;
	struct nf_conn *ct;

	might_sleep();

	mutex_lock(&nf_conntrack_mutex);
	while ((ct = get_next_corpse(iter, iter_data, &bucket)) != NULL) {
		/* Time to push up daises... */

		nf_ct_delete(ct, iter_data->portid, iter_data->report);
		nf_ct_put(ct);
		cond_resched();
	}
	mutex_unlock(&nf_conntrack_mutex);
}

void nf_ct_iterate_cleanup_net(int (*iter)(struct nf_conn *i, void *data),
			       const struct nf_ct_iter_data *iter_data)
{
	struct net *net = iter_data->net;
	struct nf_conntrack_net *cnet = nf_ct_pernet(net);

	might_sleep();

	if (atomic_read(&cnet->count) == 0)
		return;

	nf_ct_iterate_cleanup(iter, iter_data);
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
	struct nf_ct_iter_data iter_data = {};
	struct net *net;

	down_read(&net_rwsem);
	for_each_net(net) {
		struct nf_conntrack_net *cnet = nf_ct_pernet(net);

		if (atomic_read(&cnet->count) == 0)
			continue;
		nf_queue_nf_hook_drop(net);
	}
	up_read(&net_rwsem);

	/* Need to wait for netns cleanup worker to finish, if its
	 * running -- it might have deleted a net namespace from
	 * the global list, so hook drop above might not have
	 * affected all namespaces.
	 */
	net_ns_barrier();

	/* a skb w. unconfirmed conntrack could have been reinjected just
	 * before we called nf_queue_nf_hook_drop().
	 *
	 * This makes sure its inserted into conntrack table.
	 */
	synchronize_net();

	nf_ct_ext_bump_genid();
	iter_data.data = data;
	nf_ct_iterate_cleanup(iter, &iter_data);

	/* Another cpu might be in a rcu read section with
	 * rcu protected pointer cleared in iter callback
	 * or hidden via nf_ct_ext_bump_genid() above.
	 *
	 * Wait until those are done.
	 */
	synchronize_rcu();
}
EXPORT_SYMBOL_GPL(nf_ct_iterate_destroy);

static int kill_all(struct nf_conn *i, void *data)
{
	return 1;
}

void nf_conntrack_cleanup_start(void)
{
	cleanup_nf_conntrack_bpf();
	conntrack_gc_work.exiting = true;
}

void nf_conntrack_cleanup_end(void)
{
	RCU_INIT_POINTER(nf_ct_hook, NULL);
	cancel_delayed_work_sync(&conntrack_gc_work.dwork);
	kvfree(nf_conntrack_hash);

	nf_conntrack_proto_fini();
	nf_conntrack_helper_fini();
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
	struct nf_ct_iter_data iter_data = {};
	struct net *net;
	int busy;

	/*
	 * This makes sure all current packets have passed through
	 *  netfilter framework.  Roll on, two-stage module
	 *  delete...
	 */
	synchronize_net();
i_see_dead_people:
	busy = 0;
	list_for_each_entry(net, net_exit_list, exit_list) {
		struct nf_conntrack_net *cnet = nf_ct_pernet(net);

		iter_data.net = net;
		nf_ct_iterate_cleanup_net(kill_all, &iter_data);
		if (atomic_read(&cnet->count) != 0)
			busy = 1;
	}
	if (busy) {
		schedule();
		goto i_see_dead_people;
	}

	list_for_each_entry(net, net_exit_list, exit_list) {
		nf_conntrack_ecache_pernet_fini(net);
		nf_conntrack_expect_pernet_fini(net);
		free_percpu(net->ct.stat);
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

	hash = kvcalloc(nr_slots, sizeof(struct hlist_nulls_head), GFP_KERNEL);

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

	mutex_lock(&nf_conntrack_mutex);
	old_size = nf_conntrack_htable_size;
	if (old_size == hashsize) {
		mutex_unlock(&nf_conntrack_mutex);
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
			unsigned int zone_id;

			h = hlist_nulls_entry(nf_conntrack_hash[i].first,
					      struct nf_conntrack_tuple_hash, hnnode);
			ct = nf_ct_tuplehash_to_ctrack(h);
			hlist_nulls_del_rcu(&h->hnnode);

			zone_id = nf_ct_zone_id(nf_ct_zone(ct), NF_CT_DIRECTION(h));
			bucket = __hash_conntrack(nf_ct_net(ct),
						  &h->tuple, zone_id, hashsize);
			hlist_nulls_add_head_rcu(&h->hnnode, &hash[bucket]);
		}
	}
	old_hash = nf_conntrack_hash;

	nf_conntrack_hash = hash;
	nf_conntrack_htable_size = hashsize;

	write_seqcount_end(&nf_conntrack_generation);
	nf_conntrack_all_unlock();
	local_bh_enable();

	mutex_unlock(&nf_conntrack_mutex);

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

int nf_conntrack_init_start(void)
{
	unsigned long nr_pages = totalram_pages();
	int max_factor = 8;
	int ret = -ENOMEM;
	int i;

	seqcount_spinlock_init(&nf_conntrack_generation,
			       &nf_conntrack_locks_all_lock);

	for (i = 0; i < CONNTRACK_LOCKS; i++)
		spin_lock_init(&nf_conntrack_locks[i]);

	if (!nf_conntrack_htable_size) {
		nf_conntrack_htable_size
			= (((nr_pages << PAGE_SHIFT) / 16384)
			   / sizeof(struct hlist_head));
		if (BITS_PER_LONG >= 64 &&
		    nr_pages > (4 * (1024 * 1024 * 1024 / PAGE_SIZE)))
			nf_conntrack_htable_size = 262144;
		else if (nr_pages > (1024 * 1024 * 1024 / PAGE_SIZE))
			nf_conntrack_htable_size = 65536;

		if (nf_conntrack_htable_size < 1024)
			nf_conntrack_htable_size = 1024;
		/* Use a max. factor of one by default to keep the average
		 * hash chain length at 2 entries.  Each entry has to be added
		 * twice (once for original direction, once for reply).
		 * When a table size is given we use the old value of 8 to
		 * avoid implicit reduction of the max entries setting.
		 */
		max_factor = 1;
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

	ret = nf_conntrack_helper_init();
	if (ret < 0)
		goto err_helper;

	ret = nf_conntrack_proto_init();
	if (ret < 0)
		goto err_proto;

	conntrack_gc_work_init(&conntrack_gc_work);
	queue_delayed_work(system_power_efficient_wq, &conntrack_gc_work.dwork, HZ);

	ret = register_nf_conntrack_bpf();
	if (ret < 0)
		goto err_kfunc;

	return 0;

err_kfunc:
	cancel_delayed_work_sync(&conntrack_gc_work.dwork);
	nf_conntrack_proto_fini();
err_proto:
	nf_conntrack_helper_fini();
err_helper:
	nf_conntrack_expect_fini();
err_expect:
	kmem_cache_destroy(nf_conntrack_cachep);
err_cachep:
	kvfree(nf_conntrack_hash);
	return ret;
}

static const struct nf_ct_hook nf_conntrack_hook = {
	.update		= nf_conntrack_update,
	.destroy	= nf_ct_destroy,
	.get_tuple_skb  = nf_conntrack_get_tuple_skb,
	.attach		= nf_conntrack_attach,
};

void nf_conntrack_init_end(void)
{
	RCU_INIT_POINTER(nf_ct_hook, &nf_conntrack_hook);
}

/*
 * We need to use special "null" values, not used in hash table
 */
#define UNCONFIRMED_NULLS_VAL	((1<<30)+0)

int nf_conntrack_init_net(struct net *net)
{
	struct nf_conntrack_net *cnet = nf_ct_pernet(net);
	int ret = -ENOMEM;

	BUILD_BUG_ON(IP_CT_UNTRACKED == IP_CT_NUMBER);
	BUILD_BUG_ON_NOT_POWER_OF_2(CONNTRACK_LOCKS);
	atomic_set(&cnet->count, 0);

	net->ct.stat = alloc_percpu(struct ip_conntrack_stat);
	if (!net->ct.stat)
		return ret;

	ret = nf_conntrack_expect_pernet_init(net);
	if (ret < 0)
		goto err_expect;

	nf_conntrack_acct_pernet_init(net);
	nf_conntrack_tstamp_pernet_init(net);
	nf_conntrack_ecache_pernet_init(net);
	nf_conntrack_proto_pernet_init(net);

	return 0;

err_expect:
	free_percpu(net->ct.stat);
	return ret;
}

/* ctnetlink code shared by both ctnetlink and nf_conntrack_bpf */

int __nf_ct_change_timeout(struct nf_conn *ct, u64 timeout)
{
	if (test_bit(IPS_FIXED_TIMEOUT_BIT, &ct->status))
		return -EPERM;

	__nf_ct_set_timeout(ct, timeout);

	if (test_bit(IPS_DYING_BIT, &ct->status))
		return -ETIME;

	return 0;
}
EXPORT_SYMBOL_GPL(__nf_ct_change_timeout);

void __nf_ct_change_status(struct nf_conn *ct, unsigned long on, unsigned long off)
{
	unsigned int bit;

	/* Ignore these unchangable bits */
	on &= ~IPS_UNCHANGEABLE_MASK;
	off &= ~IPS_UNCHANGEABLE_MASK;

	for (bit = 0; bit < __IPS_MAX_BIT; bit++) {
		if (on & (1 << bit))
			set_bit(bit, &ct->status);
		else if (off & (1 << bit))
			clear_bit(bit, &ct->status);
	}
}
EXPORT_SYMBOL_GPL(__nf_ct_change_status);

int nf_ct_change_status_common(struct nf_conn *ct, unsigned int status)
{
	unsigned long d;

	d = ct->status ^ status;

	if (d & (IPS_EXPECTED|IPS_CONFIRMED|IPS_DYING))
		/* unchangeable */
		return -EBUSY;

	if (d & IPS_SEEN_REPLY && !(status & IPS_SEEN_REPLY))
		/* SEEN_REPLY bit can only be set */
		return -EBUSY;

	if (d & IPS_ASSURED && !(status & IPS_ASSURED))
		/* ASSURED bit can only be set */
		return -EBUSY;

	__nf_ct_change_status(ct, status, 0);
	return 0;
}
EXPORT_SYMBOL_GPL(nf_ct_change_status_common);
