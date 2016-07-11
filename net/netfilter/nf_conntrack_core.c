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
#include <net/netfilter/nf_conntrack_l3proto.h>
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

#define NF_CONNTRACK_VERSION	"0.5.0"

int (*nfnetlink_parse_nat_setup_hook)(struct nf_conn *ct,
				      enum nf_nat_manip_type manip,
				      const struct nlattr *attr) __read_mostly;
EXPORT_SYMBOL_GPL(nfnetlink_parse_nat_setup_hook);

__cacheline_aligned_in_smp spinlock_t nf_conntrack_locks[CONNTRACK_LOCKS];
EXPORT_SYMBOL_GPL(nf_conntrack_locks);

__cacheline_aligned_in_smp DEFINE_SPINLOCK(nf_conntrack_expect_lock);
EXPORT_SYMBOL_GPL(nf_conntrack_expect_lock);

struct hlist_nulls_head *nf_conntrack_hash __read_mostly;
EXPORT_SYMBOL_GPL(nf_conntrack_hash);

static __read_mostly struct kmem_cache *nf_conntrack_cachep;
static __read_mostly spinlock_t nf_conntrack_locks_all_lock;
static __read_mostly seqcount_t nf_conntrack_generation;
static __read_mostly DEFINE_SPINLOCK(nf_conntrack_locks_all_lock);
static __read_mostly bool nf_conntrack_locks_all;

void nf_conntrack_lock(spinlock_t *lock) __acquires(lock)
{
	spin_lock(lock);
	while (unlikely(nf_conntrack_locks_all)) {
		spin_unlock(lock);
		spin_unlock_wait(&nf_conntrack_locks_all_lock);
		spin_lock(lock);
	}
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
		spin_unlock_wait(&nf_conntrack_locks[i]);
	}
}

static void nf_conntrack_all_unlock(void)
{
	nf_conntrack_locks_all = false;
	spin_unlock(&nf_conntrack_locks_all_lock);
}

unsigned int nf_conntrack_htable_size __read_mostly;
EXPORT_SYMBOL_GPL(nf_conntrack_htable_size);

unsigned int nf_conntrack_max __read_mostly;
EXPORT_SYMBOL_GPL(nf_conntrack_max);

DEFINE_PER_CPU(struct nf_conn, nf_conntrack_untracked);
EXPORT_PER_CPU_SYMBOL(nf_conntrack_untracked);

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

bool
nf_ct_get_tuple(const struct sk_buff *skb,
		unsigned int nhoff,
		unsigned int dataoff,
		u_int16_t l3num,
		u_int8_t protonum,
		struct net *net,
		struct nf_conntrack_tuple *tuple,
		const struct nf_conntrack_l3proto *l3proto,
		const struct nf_conntrack_l4proto *l4proto)
{
	memset(tuple, 0, sizeof(*tuple));

	tuple->src.l3num = l3num;
	if (l3proto->pkt_to_tuple(skb, nhoff, tuple) == 0)
		return false;

	tuple->dst.protonum = protonum;
	tuple->dst.dir = IP_CT_DIR_ORIGINAL;

	return l4proto->pkt_to_tuple(skb, dataoff, net, tuple);
}
EXPORT_SYMBOL_GPL(nf_ct_get_tuple);

bool nf_ct_get_tuplepr(const struct sk_buff *skb, unsigned int nhoff,
		       u_int16_t l3num,
		       struct net *net, struct nf_conntrack_tuple *tuple)
{
	struct nf_conntrack_l3proto *l3proto;
	struct nf_conntrack_l4proto *l4proto;
	unsigned int protoff;
	u_int8_t protonum;
	int ret;

	rcu_read_lock();

	l3proto = __nf_ct_l3proto_find(l3num);
	ret = l3proto->get_l4proto(skb, nhoff, &protoff, &protonum);
	if (ret != NF_ACCEPT) {
		rcu_read_unlock();
		return false;
	}

	l4proto = __nf_ct_l4proto_find(l3num, protonum);

	ret = nf_ct_get_tuple(skb, nhoff, protoff, l3num, protonum, net, tuple,
			      l3proto, l4proto);

	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL_GPL(nf_ct_get_tuplepr);

bool
nf_ct_invert_tuple(struct nf_conntrack_tuple *inverse,
		   const struct nf_conntrack_tuple *orig,
		   const struct nf_conntrack_l3proto *l3proto,
		   const struct nf_conntrack_l4proto *l4proto)
{
	memset(inverse, 0, sizeof(*inverse));

	inverse->src.l3num = orig->src.l3num;
	if (l3proto->invert_tuple(inverse, orig) == 0)
		return false;

	inverse->dst.dir = !orig->dst.dir;

	inverse->dst.protonum = orig->dst.protonum;
	return l4proto->invert_tuple(inverse, orig);
}
EXPORT_SYMBOL_GPL(nf_ct_invert_tuple);

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

/* Released via destroy_conntrack() */
struct nf_conn *nf_ct_tmpl_alloc(struct net *net,
				 const struct nf_conntrack_zone *zone,
				 gfp_t flags)
{
	struct nf_conn *tmpl;

	tmpl = kzalloc(sizeof(*tmpl), flags);
	if (tmpl == NULL)
		return NULL;

	tmpl->status = IPS_TEMPLATE;
	write_pnet(&tmpl->ct_net, net);

	if (nf_ct_zone_add(tmpl, flags, zone) < 0)
		goto out_free;

	atomic_set(&tmpl->ct_general.use, 0);

	return tmpl;
out_free:
	kfree(tmpl);
	return NULL;
}
EXPORT_SYMBOL_GPL(nf_ct_tmpl_alloc);

void nf_ct_tmpl_free(struct nf_conn *tmpl)
{
	nf_ct_ext_destroy(tmpl);
	nf_ct_ext_free(tmpl);
	kfree(tmpl);
}
EXPORT_SYMBOL_GPL(nf_ct_tmpl_free);

static void
destroy_conntrack(struct nf_conntrack *nfct)
{
	struct nf_conn *ct = (struct nf_conn *)nfct;
	struct net *net = nf_ct_net(ct);
	struct nf_conntrack_l4proto *l4proto;

	pr_debug("destroy_conntrack(%p)\n", ct);
	NF_CT_ASSERT(atomic_read(&nfct->use) == 0);
	NF_CT_ASSERT(!timer_pending(&ct->timeout));

	if (unlikely(nf_ct_is_template(ct))) {
		nf_ct_tmpl_free(ct);
		return;
	}
	rcu_read_lock();
	l4proto = __nf_ct_l4proto_find(nf_ct_l3num(ct), nf_ct_protonum(ct));
	if (l4proto->destroy)
		l4proto->destroy(ct);

	rcu_read_unlock();

	local_bh_disable();
	/* Expectations will have been removed in clean_from_lists,
	 * except TFTP can create an expectation on the first packet,
	 * before connection is in the list, so we need to clean here,
	 * too.
	 */
	nf_ct_remove_expectations(ct);

	nf_ct_del_from_dying_or_unconfirmed_list(ct);

	NF_CT_STAT_INC(net, delete);
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

	NF_CT_STAT_INC(net, delete_list);
	local_bh_enable();
}

bool nf_ct_delete(struct nf_conn *ct, u32 portid, int report)
{
	struct nf_conn_tstamp *tstamp;

	tstamp = nf_conn_tstamp_find(ct);
	if (tstamp && tstamp->stop == 0)
		tstamp->stop = ktime_get_real_ns();

	if (nf_ct_is_dying(ct))
		goto delete;

	if (nf_conntrack_event_report(IPCT_DESTROY, ct,
				    portid, report) < 0) {
		/* destroy event was not delivered */
		nf_ct_delete_from_lists(ct);
		nf_conntrack_ecache_delayed_work(nf_ct_net(ct));
		return false;
	}

	nf_conntrack_ecache_work(nf_ct_net(ct));
	set_bit(IPS_DYING_BIT, &ct->status);
 delete:
	nf_ct_delete_from_lists(ct);
	nf_ct_put(ct);
	return true;
}
EXPORT_SYMBOL_GPL(nf_ct_delete);

static void death_by_timeout(unsigned long ul_conntrack)
{
	nf_ct_delete((struct nf_conn *)ul_conntrack, 0, 0);
}

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
	unsigned int bucket, sequence;

begin:
	do {
		sequence = read_seqcount_begin(&nf_conntrack_generation);
		bucket = scale_hash(hash);
		ct_hash = nf_conntrack_hash;
	} while (read_seqcount_retry(&nf_conntrack_generation, sequence));

	hlist_nulls_for_each_entry_rcu(h, n, &ct_hash[bucket], hnnode) {
		if (nf_ct_key_equal(h, tuple, zone, net)) {
			NF_CT_STAT_INC_ATOMIC(net, found);
			return h;
		}
		NF_CT_STAT_INC_ATOMIC(net, searched);
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

	add_timer(&ct->timeout);
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
	struct nf_conntrack_l4proto *l4proto;

	l4proto = __nf_ct_l4proto_find(nf_ct_l3num(ct), nf_ct_protonum(ct));
	if (l4proto->allow_clash &&
	    !nfct_nat(ct) &&
	    !nf_ct_is_dying(ct) &&
	    atomic_inc_not_zero(&ct->ct_general.use)) {
		nf_ct_acct_merge(ct, ctinfo, (struct nf_conn *)skb->nfct);
		nf_conntrack_put(skb->nfct);
		/* Assign conntrack already in hashes to this skbuff. Don't
		 * modify skb->nfctinfo to ensure consistent stateful filtering.
		 */
		skb->nfct = &ct->ct_general;
		return NF_ACCEPT;
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
	/* NF_CT_ASSERT(atomic_read(&ct->ct_general.use) == 1); */

	/* No external references means no one else could have
	 * confirmed us.
	 */
	NF_CT_ASSERT(!nf_ct_is_confirmed(ct));
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
	ct->timeout.expires += jiffies;
	add_timer(&ct->timeout);
	atomic_inc(&ct->ct_general.use);
	ct->status |= IPS_CONFIRMED;

	/* set conntrack timestamp, if enabled. */
	tstamp = nf_conn_tstamp_find(ct);
	if (tstamp) {
		if (skb->tstamp.tv64 == 0)
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
	NF_CT_STAT_INC(net, insert);
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
	unsigned int hash, sequence;
	struct hlist_nulls_node *n;
	struct nf_conn *ct;

	zone = nf_ct_zone(ignored_conntrack);

	rcu_read_lock();
	do {
		sequence = read_seqcount_begin(&nf_conntrack_generation);
		hash = hash_conntrack(net, tuple);
		ct_hash = nf_conntrack_hash;
	} while (read_seqcount_retry(&nf_conntrack_generation, sequence));

	hlist_nulls_for_each_entry_rcu(h, n, &ct_hash[hash], hnnode) {
		ct = nf_ct_tuplehash_to_ctrack(h);
		if (ct != ignored_conntrack &&
		    nf_ct_key_equal(h, tuple, zone, net)) {
			NF_CT_STAT_INC_ATOMIC(net, found);
			rcu_read_unlock();
			return 1;
		}
		NF_CT_STAT_INC_ATOMIC(net, searched);
	}
	rcu_read_unlock();

	return 0;
}
EXPORT_SYMBOL_GPL(nf_conntrack_tuple_taken);

#define NF_CT_EVICTION_RANGE	8

/* There's a small race here where we may free a just-assured
   connection.  Too bad: we're in trouble anyway. */
static noinline int early_drop(struct net *net, unsigned int _hash)
{
	/* Use oldest entry, which is roughly LRU */
	struct nf_conntrack_tuple_hash *h;
	struct nf_conn *tmp;
	struct hlist_nulls_node *n;
	unsigned int i, hash, sequence;
	struct nf_conn *ct = NULL;
	spinlock_t *lockp;
	bool ret = false;

	i = 0;

	local_bh_disable();
restart:
	sequence = read_seqcount_begin(&nf_conntrack_generation);
	for (; i < NF_CT_EVICTION_RANGE; i++) {
		hash = scale_hash(_hash++);
		lockp = &nf_conntrack_locks[hash % CONNTRACK_LOCKS];
		nf_conntrack_lock(lockp);
		if (read_seqcount_retry(&nf_conntrack_generation, sequence)) {
			spin_unlock(lockp);
			goto restart;
		}
		hlist_nulls_for_each_entry_rcu(h, n, &nf_conntrack_hash[hash],
					       hnnode) {
			tmp = nf_ct_tuplehash_to_ctrack(h);

			if (test_bit(IPS_ASSURED_BIT, &tmp->status) ||
			    !net_eq(nf_ct_net(tmp), net) ||
			    nf_ct_is_dying(tmp))
				continue;

			if (atomic_inc_not_zero(&tmp->ct_general.use)) {
				ct = tmp;
				break;
			}
		}

		spin_unlock(lockp);
		if (ct)
			break;
	}

	local_bh_enable();

	if (!ct)
		return false;

	/* kill only if in same netns -- might have moved due to
	 * SLAB_DESTROY_BY_RCU rules
	 */
	if (net_eq(nf_ct_net(ct), net) && del_timer(&ct->timeout)) {
		if (nf_ct_delete(ct, 0, 0)) {
			NF_CT_STAT_INC_ATOMIC(net, early_drop);
			ret = true;
		}
	}

	nf_ct_put(ct);
	return ret;
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
			atomic_dec(&net->ct.count);
			net_warn_ratelimited("nf_conntrack: table full, dropping packet\n");
			return ERR_PTR(-ENOMEM);
		}
	}

	/*
	 * Do not use kmem_cache_zalloc(), as this cache uses
	 * SLAB_DESTROY_BY_RCU.
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
	/* Don't set timer yet: wait for confirmation */
	setup_timer(&ct->timeout, death_by_timeout, (unsigned long)ct);
	write_pnet(&ct->ct_net, net);
	memset(&ct->__nfct_init_offset[0], 0,
	       offsetof(struct nf_conn, proto) -
	       offsetof(struct nf_conn, __nfct_init_offset[0]));

	if (zone && nf_ct_zone_add(ct, GFP_ATOMIC, zone) < 0)
		goto out_free;

	/* Because we use RCU lookups, we set ct_general.use to zero before
	 * this is inserted in any list.
	 */
	atomic_set(&ct->ct_general.use, 0);
	return ct;
out_free:
	kmem_cache_free(nf_conntrack_cachep, ct);
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
	 * the golden rule for SLAB_DESTROY_BY_RCU
	 */
	NF_CT_ASSERT(atomic_read(&ct->ct_general.use) == 0);

	nf_ct_ext_destroy(ct);
	nf_ct_ext_free(ct);
	kmem_cache_free(nf_conntrack_cachep, ct);
	smp_mb__before_atomic();
	atomic_dec(&net->ct.count);
}
EXPORT_SYMBOL_GPL(nf_conntrack_free);


/* Allocate a new conntrack: we return -ENOMEM if classification
   failed due to stress.  Otherwise it really is unclassifiable. */
static struct nf_conntrack_tuple_hash *
init_conntrack(struct net *net, struct nf_conn *tmpl,
	       const struct nf_conntrack_tuple *tuple,
	       struct nf_conntrack_l3proto *l3proto,
	       struct nf_conntrack_l4proto *l4proto,
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
	unsigned int *timeouts;

	if (!nf_ct_invert_tuple(&repl_tuple, tuple, l3proto, l4proto)) {
		pr_debug("Can't invert tuple.\n");
		return NULL;
	}

	zone = nf_ct_zone_tmpl(tmpl, skb, &tmp);
	ct = __nf_conntrack_alloc(net, zone, tuple, &repl_tuple, GFP_ATOMIC,
				  hash);
	if (IS_ERR(ct))
		return (struct nf_conntrack_tuple_hash *)ct;

	if (tmpl && nfct_synproxy(tmpl)) {
		nfct_seqadj_ext_add(ct);
		nfct_synproxy_ext_add(ct);
	}

	timeout_ext = tmpl ? nf_ct_timeout_find(tmpl) : NULL;
	if (timeout_ext) {
		timeouts = nf_ct_timeout_data(timeout_ext);
		if (unlikely(!timeouts))
			timeouts = l4proto->get_timeouts(net);
	} else {
		timeouts = l4proto->get_timeouts(net);
	}

	if (!l4proto->new(ct, skb, dataoff, timeouts)) {
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
				help = nf_ct_helper_ext_add(ct, exp->helper,
							    GFP_ATOMIC);
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
	if (!exp) {
		__nf_ct_try_assign_helper(ct, tmpl, GFP_ATOMIC);
		NF_CT_STAT_INC(net, new);
	}

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

/* On success, returns conntrack ptr, sets skb->nfct and ctinfo */
static inline struct nf_conn *
resolve_normal_ct(struct net *net, struct nf_conn *tmpl,
		  struct sk_buff *skb,
		  unsigned int dataoff,
		  u_int16_t l3num,
		  u_int8_t protonum,
		  struct nf_conntrack_l3proto *l3proto,
		  struct nf_conntrack_l4proto *l4proto,
		  int *set_reply,
		  enum ip_conntrack_info *ctinfo)
{
	const struct nf_conntrack_zone *zone;
	struct nf_conntrack_tuple tuple;
	struct nf_conntrack_tuple_hash *h;
	struct nf_conntrack_zone tmp;
	struct nf_conn *ct;
	u32 hash;

	if (!nf_ct_get_tuple(skb, skb_network_offset(skb),
			     dataoff, l3num, protonum, net, &tuple, l3proto,
			     l4proto)) {
		pr_debug("Can't get tuple\n");
		return NULL;
	}

	/* look for tuple match */
	zone = nf_ct_zone_tmpl(tmpl, skb, &tmp);
	hash = hash_conntrack_raw(&tuple, net);
	h = __nf_conntrack_find_get(net, zone, &tuple, hash);
	if (!h) {
		h = init_conntrack(net, tmpl, &tuple, l3proto, l4proto,
				   skb, dataoff, hash);
		if (!h)
			return NULL;
		if (IS_ERR(h))
			return (void *)h;
	}
	ct = nf_ct_tuplehash_to_ctrack(h);

	/* It exists; we have (non-exclusive) reference. */
	if (NF_CT_DIRECTION(h) == IP_CT_DIR_REPLY) {
		*ctinfo = IP_CT_ESTABLISHED_REPLY;
		/* Please set reply bit if this packet OK */
		*set_reply = 1;
	} else {
		/* Once we've had two way comms, always ESTABLISHED. */
		if (test_bit(IPS_SEEN_REPLY_BIT, &ct->status)) {
			pr_debug("normal packet for %p\n", ct);
			*ctinfo = IP_CT_ESTABLISHED;
		} else if (test_bit(IPS_EXPECTED_BIT, &ct->status)) {
			pr_debug("related packet for %p\n", ct);
			*ctinfo = IP_CT_RELATED;
		} else {
			pr_debug("new packet for %p\n", ct);
			*ctinfo = IP_CT_NEW;
		}
		*set_reply = 0;
	}
	skb->nfct = &ct->ct_general;
	skb->nfctinfo = *ctinfo;
	return ct;
}

unsigned int
nf_conntrack_in(struct net *net, u_int8_t pf, unsigned int hooknum,
		struct sk_buff *skb)
{
	struct nf_conn *ct, *tmpl = NULL;
	enum ip_conntrack_info ctinfo;
	struct nf_conntrack_l3proto *l3proto;
	struct nf_conntrack_l4proto *l4proto;
	unsigned int *timeouts;
	unsigned int dataoff;
	u_int8_t protonum;
	int set_reply = 0;
	int ret;

	if (skb->nfct) {
		/* Previously seen (loopback or untracked)?  Ignore. */
		tmpl = (struct nf_conn *)skb->nfct;
		if (!nf_ct_is_template(tmpl)) {
			NF_CT_STAT_INC_ATOMIC(net, ignore);
			return NF_ACCEPT;
		}
		skb->nfct = NULL;
	}

	/* rcu_read_lock()ed by nf_hook_slow */
	l3proto = __nf_ct_l3proto_find(pf);
	ret = l3proto->get_l4proto(skb, skb_network_offset(skb),
				   &dataoff, &protonum);
	if (ret <= 0) {
		pr_debug("not prepared to track yet or error occurred\n");
		NF_CT_STAT_INC_ATOMIC(net, error);
		NF_CT_STAT_INC_ATOMIC(net, invalid);
		ret = -ret;
		goto out;
	}

	l4proto = __nf_ct_l4proto_find(pf, protonum);

	/* It may be an special packet, error, unclean...
	 * inverse of the return code tells to the netfilter
	 * core what to do with the packet. */
	if (l4proto->error != NULL) {
		ret = l4proto->error(net, tmpl, skb, dataoff, &ctinfo,
				     pf, hooknum);
		if (ret <= 0) {
			NF_CT_STAT_INC_ATOMIC(net, error);
			NF_CT_STAT_INC_ATOMIC(net, invalid);
			ret = -ret;
			goto out;
		}
		/* ICMP[v6] protocol trackers may assign one conntrack. */
		if (skb->nfct)
			goto out;
	}

	ct = resolve_normal_ct(net, tmpl, skb, dataoff, pf, protonum,
			       l3proto, l4proto, &set_reply, &ctinfo);
	if (!ct) {
		/* Not valid part of a connection */
		NF_CT_STAT_INC_ATOMIC(net, invalid);
		ret = NF_ACCEPT;
		goto out;
	}

	if (IS_ERR(ct)) {
		/* Too stressed to deal. */
		NF_CT_STAT_INC_ATOMIC(net, drop);
		ret = NF_DROP;
		goto out;
	}

	NF_CT_ASSERT(skb->nfct);

	/* Decide what timeout policy we want to apply to this flow. */
	timeouts = nf_ct_timeout_lookup(net, ct, l4proto);

	ret = l4proto->packet(ct, skb, dataoff, ctinfo, pf, hooknum, timeouts);
	if (ret <= 0) {
		/* Invalid: inverse of the return code tells
		 * the netfilter core what to do */
		pr_debug("nf_conntrack_in: Can't track with proto module\n");
		nf_conntrack_put(skb->nfct);
		skb->nfct = NULL;
		NF_CT_STAT_INC_ATOMIC(net, invalid);
		if (ret == -NF_DROP)
			NF_CT_STAT_INC_ATOMIC(net, drop);
		ret = -ret;
		goto out;
	}

	if (set_reply && !test_and_set_bit(IPS_SEEN_REPLY_BIT, &ct->status))
		nf_conntrack_event_cache(IPCT_REPLY, ct);
out:
	if (tmpl) {
		/* Special case: we have to repeat this hook, assign the
		 * template again to this packet. We assume that this packet
		 * has no conntrack assigned. This is used by nf_ct_tcp. */
		if (ret == NF_REPEAT)
			skb->nfct = (struct nf_conntrack *)tmpl;
		else
			nf_ct_put(tmpl);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(nf_conntrack_in);

bool nf_ct_invert_tuplepr(struct nf_conntrack_tuple *inverse,
			  const struct nf_conntrack_tuple *orig)
{
	bool ret;

	rcu_read_lock();
	ret = nf_ct_invert_tuple(inverse, orig,
				 __nf_ct_l3proto_find(orig->src.l3num),
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
	NF_CT_ASSERT(!nf_ct_is_confirmed(ct));

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
	NF_CT_ASSERT(ct->timeout.data == (unsigned long)ct);
	NF_CT_ASSERT(skb);

	/* Only update if this is not a fixed timeout */
	if (test_bit(IPS_FIXED_TIMEOUT_BIT, &ct->status))
		goto acct;

	/* If not in hash table, timer will not be active yet */
	if (!nf_ct_is_confirmed(ct)) {
		ct->timeout.expires = extra_jiffies;
	} else {
		unsigned long newtime = jiffies + extra_jiffies;

		/* Only update the timeout if the new timeout is at least
		   HZ jiffies from the old timeout. Need del_timer for race
		   avoidance (may already be dying). */
		if (newtime - ct->timeout.expires >= HZ)
			mod_timer_pending(&ct->timeout, newtime);
	}

acct:
	if (do_acct)
		nf_ct_acct_update(ct, ctinfo, skb->len);
}
EXPORT_SYMBOL_GPL(__nf_ct_refresh_acct);

bool __nf_ct_kill_acct(struct nf_conn *ct,
		       enum ip_conntrack_info ctinfo,
		       const struct sk_buff *skb,
		       int do_acct)
{
	if (do_acct)
		nf_ct_acct_update(ct, ctinfo, skb->len);

	if (del_timer(&ct->timeout)) {
		ct->timeout.function((unsigned long)ct);
		return true;
	}
	return false;
}
EXPORT_SYMBOL_GPL(__nf_ct_kill_acct);

#ifdef CONFIG_NF_CONNTRACK_ZONES
static struct nf_ct_ext_type nf_ct_zone_extend __read_mostly = {
	.len	= sizeof(struct nf_conntrack_zone),
	.align	= __alignof__(struct nf_conntrack_zone),
	.id	= NF_CT_EXT_ZONE,
};
#endif

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

int nf_ct_port_nlattr_tuple_size(void)
{
	return nla_policy_len(nf_ct_port_nla_policy, CTA_PROTO_MAX + 1);
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
	nskb->nfct = &ct->ct_general;
	nskb->nfctinfo = ctinfo;
	nf_conntrack_get(nskb->nfct);
}

/* Bring out ya dead! */
static struct nf_conn *
get_next_corpse(struct net *net, int (*iter)(struct nf_conn *i, void *data),
		void *data, unsigned int *bucket)
{
	struct nf_conntrack_tuple_hash *h;
	struct nf_conn *ct;
	struct hlist_nulls_node *n;
	int cpu;
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
				if (net_eq(nf_ct_net(ct), net) &&
				    iter(ct, data))
					goto found;
			}
		}
		spin_unlock(lockp);
		local_bh_enable();
		cond_resched();
	}

	for_each_possible_cpu(cpu) {
		struct ct_pcpu *pcpu = per_cpu_ptr(net->ct.pcpu_lists, cpu);

		spin_lock_bh(&pcpu->lock);
		hlist_nulls_for_each_entry(h, n, &pcpu->unconfirmed, hnnode) {
			ct = nf_ct_tuplehash_to_ctrack(h);
			if (iter(ct, data))
				set_bit(IPS_DYING_BIT, &ct->status);
		}
		spin_unlock_bh(&pcpu->lock);
		cond_resched();
	}
	return NULL;
found:
	atomic_inc(&ct->ct_general.use);
	spin_unlock(lockp);
	local_bh_enable();
	return ct;
}

void nf_ct_iterate_cleanup(struct net *net,
			   int (*iter)(struct nf_conn *i, void *data),
			   void *data, u32 portid, int report)
{
	struct nf_conn *ct;
	unsigned int bucket = 0;

	might_sleep();

	if (atomic_read(&net->ct.count) == 0)
		return;

	while ((ct = get_next_corpse(net, iter, data, &bucket)) != NULL) {
		/* Time to push up daises... */
		if (del_timer(&ct->timeout))
			nf_ct_delete(ct, portid, report);

		/* ... else the timer will get him soon. */

		nf_ct_put(ct);
		cond_resched();
	}
}
EXPORT_SYMBOL_GPL(nf_ct_iterate_cleanup);

static int kill_all(struct nf_conn *i, void *data)
{
	return 1;
}

void nf_ct_free_hashtable(void *hash, unsigned int size)
{
	if (is_vmalloc_addr(hash))
		vfree(hash);
	else
		free_pages((unsigned long)hash,
			   get_order(sizeof(struct hlist_head) * size));
}
EXPORT_SYMBOL_GPL(nf_ct_free_hashtable);

static int untrack_refs(void)
{
	int cnt = 0, cpu;

	for_each_possible_cpu(cpu) {
		struct nf_conn *ct = &per_cpu(nf_conntrack_untracked, cpu);

		cnt += atomic_read(&ct->ct_general.use) - 1;
	}
	return cnt;
}

void nf_conntrack_cleanup_start(void)
{
	RCU_INIT_POINTER(ip_ct_attach, NULL);
}

void nf_conntrack_cleanup_end(void)
{
	RCU_INIT_POINTER(nf_ct_destroy, NULL);
	while (untrack_refs() > 0)
		schedule();

	nf_ct_free_hashtable(nf_conntrack_hash, nf_conntrack_htable_size);

#ifdef CONFIG_NF_CONNTRACK_ZONES
	nf_ct_extend_unregister(&nf_ct_zone_extend);
#endif
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
		nf_ct_iterate_cleanup(net, kill_all, NULL, 0, 0);
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
	size_t sz;

	if (*sizep > (UINT_MAX / sizeof(struct hlist_nulls_head)))
		return NULL;

	BUILD_BUG_ON(sizeof(struct hlist_nulls_head) != sizeof(struct hlist_head));
	nr_slots = *sizep = roundup(*sizep, PAGE_SIZE / sizeof(struct hlist_nulls_head));

	if (nr_slots > (UINT_MAX / sizeof(struct hlist_nulls_head)))
		return NULL;

	sz = nr_slots * sizeof(struct hlist_nulls_head);
	hash = (void *)__get_free_pages(GFP_KERNEL | __GFP_NOWARN | __GFP_ZERO,
					get_order(sz));
	if (!hash)
		hash = vzalloc(sz);

	if (hash && nulls)
		for (i = 0; i < nr_slots; i++)
			INIT_HLIST_NULLS_HEAD(&hash[i], i);

	return hash;
}
EXPORT_SYMBOL_GPL(nf_ct_alloc_hashtable);

int nf_conntrack_set_hashsize(const char *val, struct kernel_param *kp)
{
	int i, bucket, rc;
	unsigned int hashsize, old_size;
	struct hlist_nulls_head *hash, *old_hash;
	struct nf_conntrack_tuple_hash *h;
	struct nf_conn *ct;

	if (current->nsproxy->net_ns != &init_net)
		return -EOPNOTSUPP;

	/* On boot, we can set this without any fancy locking. */
	if (!nf_conntrack_htable_size)
		return param_set_uint(val, kp);

	rc = kstrtouint(val, 0, &hashsize);
	if (rc)
		return rc;
	if (!hashsize)
		return -EINVAL;

	hash = nf_ct_alloc_hashtable(&hashsize, 1);
	if (!hash)
		return -ENOMEM;

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
	nf_ct_free_hashtable(old_hash, old_size);
	return 0;
}
EXPORT_SYMBOL_GPL(nf_conntrack_set_hashsize);

module_param_call(hashsize, nf_conntrack_set_hashsize, param_get_uint,
		  &nf_conntrack_htable_size, 0600);

void nf_ct_untracked_status_or(unsigned long bits)
{
	int cpu;

	for_each_possible_cpu(cpu)
		per_cpu(nf_conntrack_untracked, cpu).status |= bits;
}
EXPORT_SYMBOL_GPL(nf_ct_untracked_status_or);

int nf_conntrack_init_start(void)
{
	int max_factor = 8;
	int ret = -ENOMEM;
	int i, cpu;

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
						sizeof(struct nf_conn), 0,
						SLAB_DESTROY_BY_RCU, NULL);
	if (!nf_conntrack_cachep)
		goto err_cachep;

	printk(KERN_INFO "nf_conntrack version %s (%u buckets, %d max)\n",
	       NF_CONNTRACK_VERSION, nf_conntrack_htable_size,
	       nf_conntrack_max);

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

#ifdef CONFIG_NF_CONNTRACK_ZONES
	ret = nf_ct_extend_register(&nf_ct_zone_extend);
	if (ret < 0)
		goto err_extend;
#endif
	ret = nf_conntrack_proto_init();
	if (ret < 0)
		goto err_proto;

	/* Set up fake conntrack: to never be deleted, not in any hashes */
	for_each_possible_cpu(cpu) {
		struct nf_conn *ct = &per_cpu(nf_conntrack_untracked, cpu);
		write_pnet(&ct->ct_net, &init_net);
		atomic_set(&ct->ct_general.use, 1);
	}
	/*  - and look it like as a confirmed connection */
	nf_ct_untracked_status_or(IPS_CONFIRMED | IPS_UNTRACKED);
	return 0;

err_proto:
#ifdef CONFIG_NF_CONNTRACK_ZONES
	nf_ct_extend_unregister(&nf_ct_zone_extend);
err_extend:
#endif
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
	nf_ct_free_hashtable(nf_conntrack_hash, nf_conntrack_htable_size);
	return ret;
}

void nf_conntrack_init_end(void)
{
	/* For use by REJECT target */
	RCU_INIT_POINTER(ip_ct_attach, nf_conntrack_attach);
	RCU_INIT_POINTER(nf_ct_destroy, destroy_conntrack);
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
