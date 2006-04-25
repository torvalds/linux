/* Connection state tracking for netfilter.  This is separated from,
   but required by, the NAT layer; it can also be used by an iptables
   extension. */

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 * (C) 2003,2004 USAGI/WIDE Project <http://www.linux-ipv6.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * 23 Apr 2001: Harald Welte <laforge@gnumonks.org>
 *	- new API and handling of conntrack/nat helpers
 *	- now capable of multiple expectations for one master
 * 16 Jul 2002: Harald Welte <laforge@gnumonks.org>
 *	- add usage/reference counts to ip_conntrack_expect
 *	- export ip_conntrack[_expect]_{find_get,put} functions
 * 16 Dec 2003: Yasuyuki Kozakai @USAGI <yasuyuki.kozakai@toshiba.co.jp>
 *	- generalize L3 protocol denendent part.
 * 23 Mar 2004: Yasuyuki Kozakai @USAGI <yasuyuki.kozakai@toshiba.co.jp>
 *	- add support various size of conntrack structures.
 * 26 Jan 2006: Harald Welte <laforge@netfilter.org>
 * 	- restructure nf_conn (introduce nf_conn_help)
 * 	- redesign 'features' how they were originally intended
 * 26 Feb 2006: Pablo Neira Ayuso <pablo@eurodev.net>
 * 	- add support for L3 protocol module load on demand.
 *
 * Derived from net/ipv4/netfilter/ip_conntrack_core.c
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/netfilter.h>
#include <linux/module.h>
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

/* This rwlock protects the main hash table, protocol/helper/expected
   registrations, conntrack timers*/
#define ASSERT_READ_LOCK(x)
#define ASSERT_WRITE_LOCK(x)

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_l3proto.h>
#include <net/netfilter/nf_conntrack_protocol.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <linux/netfilter_ipv4/listhelp.h>

#define NF_CONNTRACK_VERSION	"0.5.0"

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

DEFINE_RWLOCK(nf_conntrack_lock);

/* nf_conntrack_standalone needs this */
atomic_t nf_conntrack_count = ATOMIC_INIT(0);

void (*nf_conntrack_destroyed)(struct nf_conn *conntrack) = NULL;
LIST_HEAD(nf_conntrack_expect_list);
struct nf_conntrack_protocol **nf_ct_protos[PF_MAX];
struct nf_conntrack_l3proto *nf_ct_l3protos[PF_MAX];
static LIST_HEAD(helpers);
unsigned int nf_conntrack_htable_size = 0;
int nf_conntrack_max;
struct list_head *nf_conntrack_hash;
static kmem_cache_t *nf_conntrack_expect_cachep;
struct nf_conn nf_conntrack_untracked;
unsigned int nf_ct_log_invalid;
static LIST_HEAD(unconfirmed);
static int nf_conntrack_vmalloc;

static unsigned int nf_conntrack_next_id;
static unsigned int nf_conntrack_expect_next_id;
#ifdef CONFIG_NF_CONNTRACK_EVENTS
ATOMIC_NOTIFIER_HEAD(nf_conntrack_chain);
ATOMIC_NOTIFIER_HEAD(nf_conntrack_expect_chain);

DEFINE_PER_CPU(struct nf_conntrack_ecache, nf_conntrack_ecache);

/* deliver cached events and clear cache entry - must be called with locally
 * disabled softirqs */
static inline void
__nf_ct_deliver_cached_events(struct nf_conntrack_ecache *ecache)
{
	DEBUGP("ecache: delivering events for %p\n", ecache->ct);
	if (nf_ct_is_confirmed(ecache->ct) && !nf_ct_is_dying(ecache->ct)
	    && ecache->events)
		atomic_notifier_call_chain(&nf_conntrack_chain, ecache->events,
				    ecache->ct);

	ecache->events = 0;
	nf_ct_put(ecache->ct);
	ecache->ct = NULL;
}

/* Deliver all cached events for a particular conntrack. This is called
 * by code prior to async packet handling for freeing the skb */
void nf_ct_deliver_cached_events(const struct nf_conn *ct)
{
	struct nf_conntrack_ecache *ecache;

	local_bh_disable();
	ecache = &__get_cpu_var(nf_conntrack_ecache);
	if (ecache->ct == ct)
		__nf_ct_deliver_cached_events(ecache);
	local_bh_enable();
}

/* Deliver cached events for old pending events, if current conntrack != old */
void __nf_ct_event_cache_init(struct nf_conn *ct)
{
	struct nf_conntrack_ecache *ecache;
	
	/* take care of delivering potentially old events */
	ecache = &__get_cpu_var(nf_conntrack_ecache);
	BUG_ON(ecache->ct == ct);
	if (ecache->ct)
		__nf_ct_deliver_cached_events(ecache);
	/* initialize for this conntrack/packet */
	ecache->ct = ct;
	nf_conntrack_get(&ct->ct_general);
}

/* flush the event cache - touches other CPU's data and must not be called
 * while packets are still passing through the code */
static void nf_ct_event_cache_flush(void)
{
	struct nf_conntrack_ecache *ecache;
	int cpu;

	for_each_possible_cpu(cpu) {
		ecache = &per_cpu(nf_conntrack_ecache, cpu);
		if (ecache->ct)
			nf_ct_put(ecache->ct);
	}
}
#else
static inline void nf_ct_event_cache_flush(void) {}
#endif /* CONFIG_NF_CONNTRACK_EVENTS */

DEFINE_PER_CPU(struct ip_conntrack_stat, nf_conntrack_stat);
EXPORT_PER_CPU_SYMBOL(nf_conntrack_stat);

/*
 * This scheme offers various size of "struct nf_conn" dependent on
 * features(helper, nat, ...)
 */

#define NF_CT_FEATURES_NAMELEN	256
static struct {
	/* name of slab cache. printed in /proc/slabinfo */
	char *name;

	/* size of slab cache */
	size_t size;

	/* slab cache pointer */
	kmem_cache_t *cachep;

	/* allocated slab cache + modules which uses this slab cache */
	int use;

	/* Initialization */
	int (*init_conntrack)(struct nf_conn *, u_int32_t);

} nf_ct_cache[NF_CT_F_NUM];

/* protect members of nf_ct_cache except of "use" */
DEFINE_RWLOCK(nf_ct_cache_lock);

/* This avoids calling kmem_cache_create() with same name simultaneously */
static DEFINE_MUTEX(nf_ct_cache_mutex);

extern struct nf_conntrack_protocol nf_conntrack_generic_protocol;
struct nf_conntrack_protocol *
__nf_ct_proto_find(u_int16_t l3proto, u_int8_t protocol)
{
	if (unlikely(l3proto >= AF_MAX || nf_ct_protos[l3proto] == NULL))
		return &nf_conntrack_generic_protocol;

	return nf_ct_protos[l3proto][protocol];
}

/* this is guaranteed to always return a valid protocol helper, since
 * it falls back to generic_protocol */
struct nf_conntrack_protocol *
nf_ct_proto_find_get(u_int16_t l3proto, u_int8_t protocol)
{
	struct nf_conntrack_protocol *p;

	preempt_disable();
	p = __nf_ct_proto_find(l3proto, protocol);
	if (!try_module_get(p->me))
		p = &nf_conntrack_generic_protocol;
	preempt_enable();
	
	return p;
}

void nf_ct_proto_put(struct nf_conntrack_protocol *p)
{
	module_put(p->me);
}

struct nf_conntrack_l3proto *
nf_ct_l3proto_find_get(u_int16_t l3proto)
{
	struct nf_conntrack_l3proto *p;

	preempt_disable();
	p = __nf_ct_l3proto_find(l3proto);
	if (!try_module_get(p->me))
		p = &nf_conntrack_generic_l3proto;
	preempt_enable();

	return p;
}

void nf_ct_l3proto_put(struct nf_conntrack_l3proto *p)
{
	module_put(p->me);
}

int
nf_ct_l3proto_try_module_get(unsigned short l3proto)
{
	int ret;
	struct nf_conntrack_l3proto *p;

retry:	p = nf_ct_l3proto_find_get(l3proto);
	if (p == &nf_conntrack_generic_l3proto) {
		ret = request_module("nf_conntrack-%d", l3proto);
		if (!ret)
			goto retry;

		return -EPROTOTYPE;
	}

	return 0;
}

void nf_ct_l3proto_module_put(unsigned short l3proto)
{
	struct nf_conntrack_l3proto *p;

	preempt_disable();
	p = __nf_ct_l3proto_find(l3proto);
	preempt_enable();

	module_put(p->me);
}

static int nf_conntrack_hash_rnd_initted;
static unsigned int nf_conntrack_hash_rnd;

static u_int32_t __hash_conntrack(const struct nf_conntrack_tuple *tuple,
				  unsigned int size, unsigned int rnd)
{
	unsigned int a, b;
	a = jhash((void *)tuple->src.u3.all, sizeof(tuple->src.u3.all),
		  ((tuple->src.l3num) << 16) | tuple->dst.protonum);
	b = jhash((void *)tuple->dst.u3.all, sizeof(tuple->dst.u3.all),
			(tuple->src.u.all << 16) | tuple->dst.u.all);

	return jhash_2words(a, b, rnd) % size;
}

static inline u_int32_t hash_conntrack(const struct nf_conntrack_tuple *tuple)
{
	return __hash_conntrack(tuple, nf_conntrack_htable_size,
				nf_conntrack_hash_rnd);
}

int nf_conntrack_register_cache(u_int32_t features, const char *name,
				size_t size)
{
	int ret = 0;
	char *cache_name;
	kmem_cache_t *cachep;

	DEBUGP("nf_conntrack_register_cache: features=0x%x, name=%s, size=%d\n",
	       features, name, size);

	if (features < NF_CT_F_BASIC || features >= NF_CT_F_NUM) {
		DEBUGP("nf_conntrack_register_cache: invalid features.: 0x%x\n",
			features);
		return -EINVAL;
	}

	mutex_lock(&nf_ct_cache_mutex);

	write_lock_bh(&nf_ct_cache_lock);
	/* e.g: multiple helpers are loaded */
	if (nf_ct_cache[features].use > 0) {
		DEBUGP("nf_conntrack_register_cache: already resisterd.\n");
		if ((!strncmp(nf_ct_cache[features].name, name,
			      NF_CT_FEATURES_NAMELEN))
		    && nf_ct_cache[features].size == size) {
			DEBUGP("nf_conntrack_register_cache: reusing.\n");
			nf_ct_cache[features].use++;
			ret = 0;
		} else
			ret = -EBUSY;

		write_unlock_bh(&nf_ct_cache_lock);
		mutex_unlock(&nf_ct_cache_mutex);
		return ret;
	}
	write_unlock_bh(&nf_ct_cache_lock);

	/*
	 * The memory space for name of slab cache must be alive until
	 * cache is destroyed.
	 */
	cache_name = kmalloc(sizeof(char)*NF_CT_FEATURES_NAMELEN, GFP_ATOMIC);
	if (cache_name == NULL) {
		DEBUGP("nf_conntrack_register_cache: can't alloc cache_name\n");
		ret = -ENOMEM;
		goto out_up_mutex;
	}

	if (strlcpy(cache_name, name, NF_CT_FEATURES_NAMELEN)
						>= NF_CT_FEATURES_NAMELEN) {
		printk("nf_conntrack_register_cache: name too long\n");
		ret = -EINVAL;
		goto out_free_name;
	}

	cachep = kmem_cache_create(cache_name, size, 0, 0,
				   NULL, NULL);
	if (!cachep) {
		printk("nf_conntrack_register_cache: Can't create slab cache "
		       "for the features = 0x%x\n", features);
		ret = -ENOMEM;
		goto out_free_name;
	}

	write_lock_bh(&nf_ct_cache_lock);
	nf_ct_cache[features].use = 1;
	nf_ct_cache[features].size = size;
	nf_ct_cache[features].cachep = cachep;
	nf_ct_cache[features].name = cache_name;
	write_unlock_bh(&nf_ct_cache_lock);

	goto out_up_mutex;

out_free_name:
	kfree(cache_name);
out_up_mutex:
	mutex_unlock(&nf_ct_cache_mutex);
	return ret;
}

/* FIXME: In the current, only nf_conntrack_cleanup() can call this function. */
void nf_conntrack_unregister_cache(u_int32_t features)
{
	kmem_cache_t *cachep;
	char *name;

	/*
	 * This assures that kmem_cache_create() isn't called before destroying
	 * slab cache.
	 */
	DEBUGP("nf_conntrack_unregister_cache: 0x%04x\n", features);
	mutex_lock(&nf_ct_cache_mutex);

	write_lock_bh(&nf_ct_cache_lock);
	if (--nf_ct_cache[features].use > 0) {
		write_unlock_bh(&nf_ct_cache_lock);
		mutex_unlock(&nf_ct_cache_mutex);
		return;
	}
	cachep = nf_ct_cache[features].cachep;
	name = nf_ct_cache[features].name;
	nf_ct_cache[features].cachep = NULL;
	nf_ct_cache[features].name = NULL;
	nf_ct_cache[features].size = 0;
	write_unlock_bh(&nf_ct_cache_lock);

	synchronize_net();

	kmem_cache_destroy(cachep);
	kfree(name);

	mutex_unlock(&nf_ct_cache_mutex);
}

int
nf_ct_get_tuple(const struct sk_buff *skb,
		unsigned int nhoff,
		unsigned int dataoff,
		u_int16_t l3num,
		u_int8_t protonum,
		struct nf_conntrack_tuple *tuple,
		const struct nf_conntrack_l3proto *l3proto,
		const struct nf_conntrack_protocol *protocol)
{
	NF_CT_TUPLE_U_BLANK(tuple);

	tuple->src.l3num = l3num;
	if (l3proto->pkt_to_tuple(skb, nhoff, tuple) == 0)
		return 0;

	tuple->dst.protonum = protonum;
	tuple->dst.dir = IP_CT_DIR_ORIGINAL;

	return protocol->pkt_to_tuple(skb, dataoff, tuple);
}

int
nf_ct_invert_tuple(struct nf_conntrack_tuple *inverse,
		   const struct nf_conntrack_tuple *orig,
		   const struct nf_conntrack_l3proto *l3proto,
		   const struct nf_conntrack_protocol *protocol)
{
	NF_CT_TUPLE_U_BLANK(inverse);

	inverse->src.l3num = orig->src.l3num;
	if (l3proto->invert_tuple(inverse, orig) == 0)
		return 0;

	inverse->dst.dir = !orig->dst.dir;

	inverse->dst.protonum = orig->dst.protonum;
	return protocol->invert_tuple(inverse, orig);
}

/* nf_conntrack_expect helper functions */
void nf_ct_unlink_expect(struct nf_conntrack_expect *exp)
{
	struct nf_conn_help *master_help = nfct_help(exp->master);

	NF_CT_ASSERT(master_help);
	ASSERT_WRITE_LOCK(&nf_conntrack_lock);
	NF_CT_ASSERT(!timer_pending(&exp->timeout));

	list_del(&exp->list);
	NF_CT_STAT_INC(expect_delete);
	master_help->expecting--;
	nf_conntrack_expect_put(exp);
}

static void expectation_timed_out(unsigned long ul_expect)
{
	struct nf_conntrack_expect *exp = (void *)ul_expect;

	write_lock_bh(&nf_conntrack_lock);
	nf_ct_unlink_expect(exp);
	write_unlock_bh(&nf_conntrack_lock);
	nf_conntrack_expect_put(exp);
}

struct nf_conntrack_expect *
__nf_conntrack_expect_find(const struct nf_conntrack_tuple *tuple)
{
	struct nf_conntrack_expect *i;
	
	list_for_each_entry(i, &nf_conntrack_expect_list, list) {
		if (nf_ct_tuple_mask_cmp(tuple, &i->tuple, &i->mask)) {
			atomic_inc(&i->use);
			return i;
		}
	}
	return NULL;
}

/* Just find a expectation corresponding to a tuple. */
struct nf_conntrack_expect *
nf_conntrack_expect_find(const struct nf_conntrack_tuple *tuple)
{
	struct nf_conntrack_expect *i;
	
	read_lock_bh(&nf_conntrack_lock);
	i = __nf_conntrack_expect_find(tuple);
	read_unlock_bh(&nf_conntrack_lock);

	return i;
}

/* If an expectation for this connection is found, it gets delete from
 * global list then returned. */
static struct nf_conntrack_expect *
find_expectation(const struct nf_conntrack_tuple *tuple)
{
	struct nf_conntrack_expect *i;

	list_for_each_entry(i, &nf_conntrack_expect_list, list) {
	/* If master is not in hash table yet (ie. packet hasn't left
	   this machine yet), how can other end know about expected?
	   Hence these are not the droids you are looking for (if
	   master ct never got confirmed, we'd hold a reference to it
	   and weird things would happen to future packets). */
		if (nf_ct_tuple_mask_cmp(tuple, &i->tuple, &i->mask)
		    && nf_ct_is_confirmed(i->master)) {
			if (i->flags & NF_CT_EXPECT_PERMANENT) {
				atomic_inc(&i->use);
				return i;
			} else if (del_timer(&i->timeout)) {
				nf_ct_unlink_expect(i);
				return i;
			}
		}
	}
	return NULL;
}

/* delete all expectations for this conntrack */
void nf_ct_remove_expectations(struct nf_conn *ct)
{
	struct nf_conntrack_expect *i, *tmp;
	struct nf_conn_help *help = nfct_help(ct);

	/* Optimization: most connection never expect any others. */
	if (!help || help->expecting == 0)
		return;

	list_for_each_entry_safe(i, tmp, &nf_conntrack_expect_list, list) {
		if (i->master == ct && del_timer(&i->timeout)) {
			nf_ct_unlink_expect(i);
			nf_conntrack_expect_put(i);
 		}
	}
}

static void
clean_from_lists(struct nf_conn *ct)
{
	unsigned int ho, hr;
	
	DEBUGP("clean_from_lists(%p)\n", ct);
	ASSERT_WRITE_LOCK(&nf_conntrack_lock);

	ho = hash_conntrack(&ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple);
	hr = hash_conntrack(&ct->tuplehash[IP_CT_DIR_REPLY].tuple);
	LIST_DELETE(&nf_conntrack_hash[ho], &ct->tuplehash[IP_CT_DIR_ORIGINAL]);
	LIST_DELETE(&nf_conntrack_hash[hr], &ct->tuplehash[IP_CT_DIR_REPLY]);

	/* Destroy all pending expectations */
	nf_ct_remove_expectations(ct);
}

static void
destroy_conntrack(struct nf_conntrack *nfct)
{
	struct nf_conn *ct = (struct nf_conn *)nfct;
	struct nf_conntrack_l3proto *l3proto;
	struct nf_conntrack_protocol *proto;

	DEBUGP("destroy_conntrack(%p)\n", ct);
	NF_CT_ASSERT(atomic_read(&nfct->use) == 0);
	NF_CT_ASSERT(!timer_pending(&ct->timeout));

	nf_conntrack_event(IPCT_DESTROY, ct);
	set_bit(IPS_DYING_BIT, &ct->status);

	/* To make sure we don't get any weird locking issues here:
	 * destroy_conntrack() MUST NOT be called with a write lock
	 * to nf_conntrack_lock!!! -HW */
	l3proto = __nf_ct_l3proto_find(ct->tuplehash[IP_CT_DIR_REPLY].tuple.src.l3num);
	if (l3proto && l3proto->destroy)
		l3proto->destroy(ct);

	proto = __nf_ct_proto_find(ct->tuplehash[IP_CT_DIR_REPLY].tuple.src.l3num, ct->tuplehash[IP_CT_DIR_REPLY].tuple.dst.protonum);
	if (proto && proto->destroy)
		proto->destroy(ct);

	if (nf_conntrack_destroyed)
		nf_conntrack_destroyed(ct);

	write_lock_bh(&nf_conntrack_lock);
	/* Expectations will have been removed in clean_from_lists,
	 * except TFTP can create an expectation on the first packet,
	 * before connection is in the list, so we need to clean here,
	 * too. */
	nf_ct_remove_expectations(ct);

	/* We overload first tuple to link into unconfirmed list. */
	if (!nf_ct_is_confirmed(ct)) {
		BUG_ON(list_empty(&ct->tuplehash[IP_CT_DIR_ORIGINAL].list));
		list_del(&ct->tuplehash[IP_CT_DIR_ORIGINAL].list);
	}

	NF_CT_STAT_INC(delete);
	write_unlock_bh(&nf_conntrack_lock);

	if (ct->master)
		nf_ct_put(ct->master);

	DEBUGP("destroy_conntrack: returning ct=%p to slab\n", ct);
	nf_conntrack_free(ct);
}

static void death_by_timeout(unsigned long ul_conntrack)
{
	struct nf_conn *ct = (void *)ul_conntrack;

	write_lock_bh(&nf_conntrack_lock);
	/* Inside lock so preempt is disabled on module removal path.
	 * Otherwise we can get spurious warnings. */
	NF_CT_STAT_INC(delete_list);
	clean_from_lists(ct);
	write_unlock_bh(&nf_conntrack_lock);
	nf_ct_put(ct);
}

static inline int
conntrack_tuple_cmp(const struct nf_conntrack_tuple_hash *i,
		    const struct nf_conntrack_tuple *tuple,
		    const struct nf_conn *ignored_conntrack)
{
	ASSERT_READ_LOCK(&nf_conntrack_lock);
	return nf_ct_tuplehash_to_ctrack(i) != ignored_conntrack
		&& nf_ct_tuple_equal(tuple, &i->tuple);
}

struct nf_conntrack_tuple_hash *
__nf_conntrack_find(const struct nf_conntrack_tuple *tuple,
		    const struct nf_conn *ignored_conntrack)
{
	struct nf_conntrack_tuple_hash *h;
	unsigned int hash = hash_conntrack(tuple);

	ASSERT_READ_LOCK(&nf_conntrack_lock);
	list_for_each_entry(h, &nf_conntrack_hash[hash], list) {
		if (conntrack_tuple_cmp(h, tuple, ignored_conntrack)) {
			NF_CT_STAT_INC(found);
			return h;
		}
		NF_CT_STAT_INC(searched);
	}

	return NULL;
}

/* Find a connection corresponding to a tuple. */
struct nf_conntrack_tuple_hash *
nf_conntrack_find_get(const struct nf_conntrack_tuple *tuple,
		      const struct nf_conn *ignored_conntrack)
{
	struct nf_conntrack_tuple_hash *h;

	read_lock_bh(&nf_conntrack_lock);
	h = __nf_conntrack_find(tuple, ignored_conntrack);
	if (h)
		atomic_inc(&nf_ct_tuplehash_to_ctrack(h)->ct_general.use);
	read_unlock_bh(&nf_conntrack_lock);

	return h;
}

static void __nf_conntrack_hash_insert(struct nf_conn *ct,
				       unsigned int hash,
				       unsigned int repl_hash) 
{
	ct->id = ++nf_conntrack_next_id;
	list_prepend(&nf_conntrack_hash[hash],
		     &ct->tuplehash[IP_CT_DIR_ORIGINAL].list);
	list_prepend(&nf_conntrack_hash[repl_hash],
		     &ct->tuplehash[IP_CT_DIR_REPLY].list);
}

void nf_conntrack_hash_insert(struct nf_conn *ct)
{
	unsigned int hash, repl_hash;

	hash = hash_conntrack(&ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple);
	repl_hash = hash_conntrack(&ct->tuplehash[IP_CT_DIR_REPLY].tuple);

	write_lock_bh(&nf_conntrack_lock);
	__nf_conntrack_hash_insert(ct, hash, repl_hash);
	write_unlock_bh(&nf_conntrack_lock);
}

/* Confirm a connection given skb; places it in hash table */
int
__nf_conntrack_confirm(struct sk_buff **pskb)
{
	unsigned int hash, repl_hash;
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;

	ct = nf_ct_get(*pskb, &ctinfo);

	/* ipt_REJECT uses nf_conntrack_attach to attach related
	   ICMP/TCP RST packets in other direction.  Actual packet
	   which created connection will be IP_CT_NEW or for an
	   expected connection, IP_CT_RELATED. */
	if (CTINFO2DIR(ctinfo) != IP_CT_DIR_ORIGINAL)
		return NF_ACCEPT;

	hash = hash_conntrack(&ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple);
	repl_hash = hash_conntrack(&ct->tuplehash[IP_CT_DIR_REPLY].tuple);

	/* We're not in hash table, and we refuse to set up related
	   connections for unconfirmed conns.  But packet copies and
	   REJECT will give spurious warnings here. */
	/* NF_CT_ASSERT(atomic_read(&ct->ct_general.use) == 1); */

	/* No external references means noone else could have
	   confirmed us. */
	NF_CT_ASSERT(!nf_ct_is_confirmed(ct));
	DEBUGP("Confirming conntrack %p\n", ct);

	write_lock_bh(&nf_conntrack_lock);

	/* See if there's one in the list already, including reverse:
	   NAT could have grabbed it without realizing, since we're
	   not in the hash.  If there is, we lost race. */
	if (!LIST_FIND(&nf_conntrack_hash[hash],
		       conntrack_tuple_cmp,
		       struct nf_conntrack_tuple_hash *,
		       &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple, NULL)
	    && !LIST_FIND(&nf_conntrack_hash[repl_hash],
			  conntrack_tuple_cmp,
			  struct nf_conntrack_tuple_hash *,
			  &ct->tuplehash[IP_CT_DIR_REPLY].tuple, NULL)) {
		struct nf_conn_help *help;
		/* Remove from unconfirmed list */
		list_del(&ct->tuplehash[IP_CT_DIR_ORIGINAL].list);

		__nf_conntrack_hash_insert(ct, hash, repl_hash);
		/* Timer relative to confirmation time, not original
		   setting time, otherwise we'd get timer wrap in
		   weird delay cases. */
		ct->timeout.expires += jiffies;
		add_timer(&ct->timeout);
		atomic_inc(&ct->ct_general.use);
		set_bit(IPS_CONFIRMED_BIT, &ct->status);
		NF_CT_STAT_INC(insert);
		write_unlock_bh(&nf_conntrack_lock);
		help = nfct_help(ct);
		if (help && help->helper)
			nf_conntrack_event_cache(IPCT_HELPER, *pskb);
#ifdef CONFIG_NF_NAT_NEEDED
		if (test_bit(IPS_SRC_NAT_DONE_BIT, &ct->status) ||
		    test_bit(IPS_DST_NAT_DONE_BIT, &ct->status))
			nf_conntrack_event_cache(IPCT_NATINFO, *pskb);
#endif
		nf_conntrack_event_cache(master_ct(ct) ?
					 IPCT_RELATED : IPCT_NEW, *pskb);
		return NF_ACCEPT;
	}

	NF_CT_STAT_INC(insert_failed);
	write_unlock_bh(&nf_conntrack_lock);
	return NF_DROP;
}

/* Returns true if a connection correspondings to the tuple (required
   for NAT). */
int
nf_conntrack_tuple_taken(const struct nf_conntrack_tuple *tuple,
			 const struct nf_conn *ignored_conntrack)
{
	struct nf_conntrack_tuple_hash *h;

	read_lock_bh(&nf_conntrack_lock);
	h = __nf_conntrack_find(tuple, ignored_conntrack);
	read_unlock_bh(&nf_conntrack_lock);

	return h != NULL;
}

/* There's a small race here where we may free a just-assured
   connection.  Too bad: we're in trouble anyway. */
static inline int unreplied(const struct nf_conntrack_tuple_hash *i)
{
	return !(test_bit(IPS_ASSURED_BIT,
			  &nf_ct_tuplehash_to_ctrack(i)->status));
}

static int early_drop(struct list_head *chain)
{
	/* Traverse backwards: gives us oldest, which is roughly LRU */
	struct nf_conntrack_tuple_hash *h;
	struct nf_conn *ct = NULL;
	int dropped = 0;

	read_lock_bh(&nf_conntrack_lock);
	h = LIST_FIND_B(chain, unreplied, struct nf_conntrack_tuple_hash *);
	if (h) {
		ct = nf_ct_tuplehash_to_ctrack(h);
		atomic_inc(&ct->ct_general.use);
	}
	read_unlock_bh(&nf_conntrack_lock);

	if (!ct)
		return dropped;

	if (del_timer(&ct->timeout)) {
		death_by_timeout((unsigned long)ct);
		dropped = 1;
		NF_CT_STAT_INC(early_drop);
	}
	nf_ct_put(ct);
	return dropped;
}

static inline int helper_cmp(const struct nf_conntrack_helper *i,
			     const struct nf_conntrack_tuple *rtuple)
{
	return nf_ct_tuple_mask_cmp(rtuple, &i->tuple, &i->mask);
}

static struct nf_conntrack_helper *
__nf_ct_helper_find(const struct nf_conntrack_tuple *tuple)
{
	return LIST_FIND(&helpers, helper_cmp,
			 struct nf_conntrack_helper *,
			 tuple);
}

struct nf_conntrack_helper *
nf_ct_helper_find_get( const struct nf_conntrack_tuple *tuple)
{
	struct nf_conntrack_helper *helper;

	/* need nf_conntrack_lock to assure that helper exists until
	 * try_module_get() is called */
	read_lock_bh(&nf_conntrack_lock);

	helper = __nf_ct_helper_find(tuple);
	if (helper) {
		/* need to increase module usage count to assure helper will
		 * not go away while the caller is e.g. busy putting a
		 * conntrack in the hash that uses the helper */
		if (!try_module_get(helper->me))
			helper = NULL;
	}

	read_unlock_bh(&nf_conntrack_lock);

	return helper;
}

void nf_ct_helper_put(struct nf_conntrack_helper *helper)
{
	module_put(helper->me);
}

static struct nf_conn *
__nf_conntrack_alloc(const struct nf_conntrack_tuple *orig,
		     const struct nf_conntrack_tuple *repl,
		     const struct nf_conntrack_l3proto *l3proto)
{
	struct nf_conn *conntrack = NULL;
	u_int32_t features = 0;
	struct nf_conntrack_helper *helper;

	if (unlikely(!nf_conntrack_hash_rnd_initted)) {
		get_random_bytes(&nf_conntrack_hash_rnd, 4);
		nf_conntrack_hash_rnd_initted = 1;
	}

	if (nf_conntrack_max
	    && atomic_read(&nf_conntrack_count) >= nf_conntrack_max) {
		unsigned int hash = hash_conntrack(orig);
		/* Try dropping from this hash chain. */
		if (!early_drop(&nf_conntrack_hash[hash])) {
			if (net_ratelimit())
				printk(KERN_WARNING
				       "nf_conntrack: table full, dropping"
				       " packet.\n");
			return ERR_PTR(-ENOMEM);
		}
	}

	/*  find features needed by this conntrack. */
	features = l3proto->get_features(orig);

	/* FIXME: protect helper list per RCU */
	read_lock_bh(&nf_conntrack_lock);
	helper = __nf_ct_helper_find(repl);
	if (helper)
		features |= NF_CT_F_HELP;
	read_unlock_bh(&nf_conntrack_lock);

	DEBUGP("nf_conntrack_alloc: features=0x%x\n", features);

	read_lock_bh(&nf_ct_cache_lock);

	if (unlikely(!nf_ct_cache[features].use)) {
		DEBUGP("nf_conntrack_alloc: not supported features = 0x%x\n",
			features);
		goto out;
	}

	conntrack = kmem_cache_alloc(nf_ct_cache[features].cachep, GFP_ATOMIC);
	if (conntrack == NULL) {
		DEBUGP("nf_conntrack_alloc: Can't alloc conntrack from cache\n");
		goto out;
	}

	memset(conntrack, 0, nf_ct_cache[features].size);
	conntrack->features = features;
	if (helper) {
		struct nf_conn_help *help = nfct_help(conntrack);
		NF_CT_ASSERT(help);
		help->helper = helper;
	}

	atomic_set(&conntrack->ct_general.use, 1);
	conntrack->ct_general.destroy = destroy_conntrack;
	conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple = *orig;
	conntrack->tuplehash[IP_CT_DIR_REPLY].tuple = *repl;
	/* Don't set timer yet: wait for confirmation */
	init_timer(&conntrack->timeout);
	conntrack->timeout.data = (unsigned long)conntrack;
	conntrack->timeout.function = death_by_timeout;

	atomic_inc(&nf_conntrack_count);
out:
	read_unlock_bh(&nf_ct_cache_lock);
	return conntrack;
}

struct nf_conn *nf_conntrack_alloc(const struct nf_conntrack_tuple *orig,
				   const struct nf_conntrack_tuple *repl)
{
	struct nf_conntrack_l3proto *l3proto;

	l3proto = __nf_ct_l3proto_find(orig->src.l3num);
	return __nf_conntrack_alloc(orig, repl, l3proto);
}

void nf_conntrack_free(struct nf_conn *conntrack)
{
	u_int32_t features = conntrack->features;
	NF_CT_ASSERT(features >= NF_CT_F_BASIC && features < NF_CT_F_NUM);
	DEBUGP("nf_conntrack_free: features = 0x%x, conntrack=%p\n", features,
	       conntrack);
	kmem_cache_free(nf_ct_cache[features].cachep, conntrack);
	atomic_dec(&nf_conntrack_count);
}

/* Allocate a new conntrack: we return -ENOMEM if classification
   failed due to stress.  Otherwise it really is unclassifiable. */
static struct nf_conntrack_tuple_hash *
init_conntrack(const struct nf_conntrack_tuple *tuple,
	       struct nf_conntrack_l3proto *l3proto,
	       struct nf_conntrack_protocol *protocol,
	       struct sk_buff *skb,
	       unsigned int dataoff)
{
	struct nf_conn *conntrack;
	struct nf_conntrack_tuple repl_tuple;
	struct nf_conntrack_expect *exp;

	if (!nf_ct_invert_tuple(&repl_tuple, tuple, l3proto, protocol)) {
		DEBUGP("Can't invert tuple.\n");
		return NULL;
	}

	conntrack = __nf_conntrack_alloc(tuple, &repl_tuple, l3proto);
	if (conntrack == NULL || IS_ERR(conntrack)) {
		DEBUGP("Can't allocate conntrack.\n");
		return (struct nf_conntrack_tuple_hash *)conntrack;
	}

	if (!protocol->new(conntrack, skb, dataoff)) {
		nf_conntrack_free(conntrack);
		DEBUGP("init conntrack: can't track with proto module\n");
		return NULL;
	}

	write_lock_bh(&nf_conntrack_lock);
	exp = find_expectation(tuple);

	if (exp) {
		DEBUGP("conntrack: expectation arrives ct=%p exp=%p\n",
			conntrack, exp);
		/* Welcome, Mr. Bond.  We've been expecting you... */
		__set_bit(IPS_EXPECTED_BIT, &conntrack->status);
		conntrack->master = exp->master;
#ifdef CONFIG_NF_CONNTRACK_MARK
		conntrack->mark = exp->master->mark;
#endif
		nf_conntrack_get(&conntrack->master->ct_general);
		NF_CT_STAT_INC(expect_new);
	} else
		NF_CT_STAT_INC(new);

	/* Overload tuple linked list to put us in unconfirmed list. */
	list_add(&conntrack->tuplehash[IP_CT_DIR_ORIGINAL].list, &unconfirmed);

	write_unlock_bh(&nf_conntrack_lock);

	if (exp) {
		if (exp->expectfn)
			exp->expectfn(conntrack, exp);
		nf_conntrack_expect_put(exp);
	}

	return &conntrack->tuplehash[IP_CT_DIR_ORIGINAL];
}

/* On success, returns conntrack ptr, sets skb->nfct and ctinfo */
static inline struct nf_conn *
resolve_normal_ct(struct sk_buff *skb,
		  unsigned int dataoff,
		  u_int16_t l3num,
		  u_int8_t protonum,
		  struct nf_conntrack_l3proto *l3proto,
		  struct nf_conntrack_protocol *proto,
		  int *set_reply,
		  enum ip_conntrack_info *ctinfo)
{
	struct nf_conntrack_tuple tuple;
	struct nf_conntrack_tuple_hash *h;
	struct nf_conn *ct;

	if (!nf_ct_get_tuple(skb, (unsigned int)(skb->nh.raw - skb->data),
			     dataoff, l3num, protonum, &tuple, l3proto,
			     proto)) {
		DEBUGP("resolve_normal_ct: Can't get tuple\n");
		return NULL;
	}

	/* look for tuple match */
	h = nf_conntrack_find_get(&tuple, NULL);
	if (!h) {
		h = init_conntrack(&tuple, l3proto, proto, skb, dataoff);
		if (!h)
			return NULL;
		if (IS_ERR(h))
			return (void *)h;
	}
	ct = nf_ct_tuplehash_to_ctrack(h);

	/* It exists; we have (non-exclusive) reference. */
	if (NF_CT_DIRECTION(h) == IP_CT_DIR_REPLY) {
		*ctinfo = IP_CT_ESTABLISHED + IP_CT_IS_REPLY;
		/* Please set reply bit if this packet OK */
		*set_reply = 1;
	} else {
		/* Once we've had two way comms, always ESTABLISHED. */
		if (test_bit(IPS_SEEN_REPLY_BIT, &ct->status)) {
			DEBUGP("nf_conntrack_in: normal packet for %p\n", ct);
			*ctinfo = IP_CT_ESTABLISHED;
		} else if (test_bit(IPS_EXPECTED_BIT, &ct->status)) {
			DEBUGP("nf_conntrack_in: related packet for %p\n", ct);
			*ctinfo = IP_CT_RELATED;
		} else {
			DEBUGP("nf_conntrack_in: new packet for %p\n", ct);
			*ctinfo = IP_CT_NEW;
		}
		*set_reply = 0;
	}
	skb->nfct = &ct->ct_general;
	skb->nfctinfo = *ctinfo;
	return ct;
}

unsigned int
nf_conntrack_in(int pf, unsigned int hooknum, struct sk_buff **pskb)
{
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	struct nf_conntrack_l3proto *l3proto;
	struct nf_conntrack_protocol *proto;
	unsigned int dataoff;
	u_int8_t protonum;
	int set_reply = 0;
	int ret;

	/* Previously seen (loopback or untracked)?  Ignore. */
	if ((*pskb)->nfct) {
		NF_CT_STAT_INC(ignore);
		return NF_ACCEPT;
	}

	l3proto = __nf_ct_l3proto_find((u_int16_t)pf);
	if ((ret = l3proto->prepare(pskb, hooknum, &dataoff, &protonum)) <= 0) {
		DEBUGP("not prepared to track yet or error occured\n");
		return -ret;
	}

	proto = __nf_ct_proto_find((u_int16_t)pf, protonum);

	/* It may be an special packet, error, unclean...
	 * inverse of the return code tells to the netfilter
	 * core what to do with the packet. */
	if (proto->error != NULL &&
	    (ret = proto->error(*pskb, dataoff, &ctinfo, pf, hooknum)) <= 0) {
		NF_CT_STAT_INC(error);
		NF_CT_STAT_INC(invalid);
		return -ret;
	}

	ct = resolve_normal_ct(*pskb, dataoff, pf, protonum, l3proto, proto,
			       &set_reply, &ctinfo);
	if (!ct) {
		/* Not valid part of a connection */
		NF_CT_STAT_INC(invalid);
		return NF_ACCEPT;
	}

	if (IS_ERR(ct)) {
		/* Too stressed to deal. */
		NF_CT_STAT_INC(drop);
		return NF_DROP;
	}

	NF_CT_ASSERT((*pskb)->nfct);

	ret = proto->packet(ct, *pskb, dataoff, ctinfo, pf, hooknum);
	if (ret < 0) {
		/* Invalid: inverse of the return code tells
		 * the netfilter core what to do */
		DEBUGP("nf_conntrack_in: Can't track with proto module\n");
		nf_conntrack_put((*pskb)->nfct);
		(*pskb)->nfct = NULL;
		NF_CT_STAT_INC(invalid);
		return -ret;
	}

	if (set_reply && !test_and_set_bit(IPS_SEEN_REPLY_BIT, &ct->status))
		nf_conntrack_event_cache(IPCT_STATUS, *pskb);

	return ret;
}

int nf_ct_invert_tuplepr(struct nf_conntrack_tuple *inverse,
			 const struct nf_conntrack_tuple *orig)
{
	return nf_ct_invert_tuple(inverse, orig,
				  __nf_ct_l3proto_find(orig->src.l3num),
				  __nf_ct_proto_find(orig->src.l3num,
						     orig->dst.protonum));
}

/* Would two expected things clash? */
static inline int expect_clash(const struct nf_conntrack_expect *a,
			       const struct nf_conntrack_expect *b)
{
	/* Part covered by intersection of masks must be unequal,
	   otherwise they clash */
	struct nf_conntrack_tuple intersect_mask;
	int count;

	intersect_mask.src.l3num = a->mask.src.l3num & b->mask.src.l3num;
	intersect_mask.src.u.all = a->mask.src.u.all & b->mask.src.u.all;
	intersect_mask.dst.u.all = a->mask.dst.u.all & b->mask.dst.u.all;
	intersect_mask.dst.protonum = a->mask.dst.protonum
					& b->mask.dst.protonum;

	for (count = 0; count < NF_CT_TUPLE_L3SIZE; count++){
		intersect_mask.src.u3.all[count] =
			a->mask.src.u3.all[count] & b->mask.src.u3.all[count];
	}

	for (count = 0; count < NF_CT_TUPLE_L3SIZE; count++){
		intersect_mask.dst.u3.all[count] =
			a->mask.dst.u3.all[count] & b->mask.dst.u3.all[count];
	}

	return nf_ct_tuple_mask_cmp(&a->tuple, &b->tuple, &intersect_mask);
}

static inline int expect_matches(const struct nf_conntrack_expect *a,
				 const struct nf_conntrack_expect *b)
{
	return a->master == b->master
		&& nf_ct_tuple_equal(&a->tuple, &b->tuple)
		&& nf_ct_tuple_equal(&a->mask, &b->mask);
}

/* Generally a bad idea to call this: could have matched already. */
void nf_conntrack_unexpect_related(struct nf_conntrack_expect *exp)
{
	struct nf_conntrack_expect *i;

	write_lock_bh(&nf_conntrack_lock);
	/* choose the the oldest expectation to evict */
	list_for_each_entry_reverse(i, &nf_conntrack_expect_list, list) {
		if (expect_matches(i, exp) && del_timer(&i->timeout)) {
			nf_ct_unlink_expect(i);
			write_unlock_bh(&nf_conntrack_lock);
			nf_conntrack_expect_put(i);
			return;
		}
	}
	write_unlock_bh(&nf_conntrack_lock);
}

/* We don't increase the master conntrack refcount for non-fulfilled
 * conntracks. During the conntrack destruction, the expectations are
 * always killed before the conntrack itself */
struct nf_conntrack_expect *nf_conntrack_expect_alloc(struct nf_conn *me)
{
	struct nf_conntrack_expect *new;

	new = kmem_cache_alloc(nf_conntrack_expect_cachep, GFP_ATOMIC);
	if (!new) {
		DEBUGP("expect_related: OOM allocating expect\n");
		return NULL;
	}
	new->master = me;
	atomic_set(&new->use, 1);
	return new;
}

void nf_conntrack_expect_put(struct nf_conntrack_expect *exp)
{
	if (atomic_dec_and_test(&exp->use))
		kmem_cache_free(nf_conntrack_expect_cachep, exp);
}

static void nf_conntrack_expect_insert(struct nf_conntrack_expect *exp)
{
	struct nf_conn_help *master_help = nfct_help(exp->master);

	atomic_inc(&exp->use);
	master_help->expecting++;
	list_add(&exp->list, &nf_conntrack_expect_list);

	init_timer(&exp->timeout);
	exp->timeout.data = (unsigned long)exp;
	exp->timeout.function = expectation_timed_out;
	exp->timeout.expires = jiffies + master_help->helper->timeout * HZ;
	add_timer(&exp->timeout);

	exp->id = ++nf_conntrack_expect_next_id;
	atomic_inc(&exp->use);
	NF_CT_STAT_INC(expect_create);
}

/* Race with expectations being used means we could have none to find; OK. */
static void evict_oldest_expect(struct nf_conn *master)
{
	struct nf_conntrack_expect *i;

	list_for_each_entry_reverse(i, &nf_conntrack_expect_list, list) {
		if (i->master == master) {
			if (del_timer(&i->timeout)) {
				nf_ct_unlink_expect(i);
				nf_conntrack_expect_put(i);
			}
			break;
		}
	}
}

static inline int refresh_timer(struct nf_conntrack_expect *i)
{
	struct nf_conn_help *master_help = nfct_help(i->master);

	if (!del_timer(&i->timeout))
		return 0;

	i->timeout.expires = jiffies + master_help->helper->timeout*HZ;
	add_timer(&i->timeout);
	return 1;
}

int nf_conntrack_expect_related(struct nf_conntrack_expect *expect)
{
	struct nf_conntrack_expect *i;
	struct nf_conn *master = expect->master;
	struct nf_conn_help *master_help = nfct_help(master);
	int ret;

	NF_CT_ASSERT(master_help);

	DEBUGP("nf_conntrack_expect_related %p\n", related_to);
	DEBUGP("tuple: "); NF_CT_DUMP_TUPLE(&expect->tuple);
	DEBUGP("mask:  "); NF_CT_DUMP_TUPLE(&expect->mask);

	write_lock_bh(&nf_conntrack_lock);
	list_for_each_entry(i, &nf_conntrack_expect_list, list) {
		if (expect_matches(i, expect)) {
			/* Refresh timer: if it's dying, ignore.. */
			if (refresh_timer(i)) {
				ret = 0;
				goto out;
			}
		} else if (expect_clash(i, expect)) {
			ret = -EBUSY;
			goto out;
		}
	}
	/* Will be over limit? */
	if (master_help->helper->max_expected &&
	    master_help->expecting >= master_help->helper->max_expected)
		evict_oldest_expect(master);

	nf_conntrack_expect_insert(expect);
	nf_conntrack_expect_event(IPEXP_NEW, expect);
	ret = 0;
out:
	write_unlock_bh(&nf_conntrack_lock);
	return ret;
}

int nf_conntrack_helper_register(struct nf_conntrack_helper *me)
{
	int ret;
	BUG_ON(me->timeout == 0);

	ret = nf_conntrack_register_cache(NF_CT_F_HELP, "nf_conntrack:help",
					  sizeof(struct nf_conn)
					  + sizeof(struct nf_conn_help)
					  + __alignof__(struct nf_conn_help));
	if (ret < 0) {
		printk(KERN_ERR "nf_conntrack_helper_reigster: Unable to create slab cache for conntracks\n");
		return ret;
	}
	write_lock_bh(&nf_conntrack_lock);
	list_prepend(&helpers, me);
	write_unlock_bh(&nf_conntrack_lock);

	return 0;
}

struct nf_conntrack_helper *
__nf_conntrack_helper_find_byname(const char *name)
{
	struct nf_conntrack_helper *h;

	list_for_each_entry(h, &helpers, list) {
		if (!strcmp(h->name, name))
			return h;
	}

	return NULL;
}

static inline int unhelp(struct nf_conntrack_tuple_hash *i,
			 const struct nf_conntrack_helper *me)
{
	struct nf_conn *ct = nf_ct_tuplehash_to_ctrack(i);
	struct nf_conn_help *help = nfct_help(ct);

	if (help && help->helper == me) {
		nf_conntrack_event(IPCT_HELPER, ct);
		help->helper = NULL;
	}
	return 0;
}

void nf_conntrack_helper_unregister(struct nf_conntrack_helper *me)
{
	unsigned int i;
	struct nf_conntrack_expect *exp, *tmp;

	/* Need write lock here, to delete helper. */
	write_lock_bh(&nf_conntrack_lock);
	LIST_DELETE(&helpers, me);

	/* Get rid of expectations */
	list_for_each_entry_safe(exp, tmp, &nf_conntrack_expect_list, list) {
		struct nf_conn_help *help = nfct_help(exp->master);
		if (help->helper == me && del_timer(&exp->timeout)) {
			nf_ct_unlink_expect(exp);
			nf_conntrack_expect_put(exp);
		}
	}

	/* Get rid of expecteds, set helpers to NULL. */
	LIST_FIND_W(&unconfirmed, unhelp, struct nf_conntrack_tuple_hash*, me);
	for (i = 0; i < nf_conntrack_htable_size; i++)
		LIST_FIND_W(&nf_conntrack_hash[i], unhelp,
			    struct nf_conntrack_tuple_hash *, me);
	write_unlock_bh(&nf_conntrack_lock);

	/* Someone could be still looking at the helper in a bh. */
	synchronize_net();
}

/* Refresh conntrack for this many jiffies and do accounting if do_acct is 1 */
void __nf_ct_refresh_acct(struct nf_conn *ct,
			  enum ip_conntrack_info ctinfo,
			  const struct sk_buff *skb,
			  unsigned long extra_jiffies,
			  int do_acct)
{
	int event = 0;

	NF_CT_ASSERT(ct->timeout.data == (unsigned long)ct);
	NF_CT_ASSERT(skb);

	write_lock_bh(&nf_conntrack_lock);

	/* If not in hash table, timer will not be active yet */
	if (!nf_ct_is_confirmed(ct)) {
		ct->timeout.expires = extra_jiffies;
		event = IPCT_REFRESH;
	} else {
		/* Need del_timer for race avoidance (may already be dying). */
		if (del_timer(&ct->timeout)) {
			ct->timeout.expires = jiffies + extra_jiffies;
			add_timer(&ct->timeout);
			event = IPCT_REFRESH;
		}
	}

#ifdef CONFIG_NF_CT_ACCT
	if (do_acct) {
		ct->counters[CTINFO2DIR(ctinfo)].packets++;
		ct->counters[CTINFO2DIR(ctinfo)].bytes +=
			skb->len - (unsigned int)(skb->nh.raw - skb->data);
	if ((ct->counters[CTINFO2DIR(ctinfo)].packets & 0x80000000)
	    || (ct->counters[CTINFO2DIR(ctinfo)].bytes & 0x80000000))
		event |= IPCT_COUNTER_FILLING;
	}
#endif

	write_unlock_bh(&nf_conntrack_lock);

	/* must be unlocked when calling event cache */
	if (event)
		nf_conntrack_event_cache(event, skb);
}

#if defined(CONFIG_NF_CT_NETLINK) || \
    defined(CONFIG_NF_CT_NETLINK_MODULE)

#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_conntrack.h>
#include <linux/mutex.h>


/* Generic function for tcp/udp/sctp/dccp and alike. This needs to be
 * in ip_conntrack_core, since we don't want the protocols to autoload
 * or depend on ctnetlink */
int nf_ct_port_tuple_to_nfattr(struct sk_buff *skb,
			       const struct nf_conntrack_tuple *tuple)
{
	NFA_PUT(skb, CTA_PROTO_SRC_PORT, sizeof(u_int16_t),
		&tuple->src.u.tcp.port);
	NFA_PUT(skb, CTA_PROTO_DST_PORT, sizeof(u_int16_t),
		&tuple->dst.u.tcp.port);
	return 0;

nfattr_failure:
	return -1;
}

static const size_t cta_min_proto[CTA_PROTO_MAX] = {
	[CTA_PROTO_SRC_PORT-1]  = sizeof(u_int16_t),
	[CTA_PROTO_DST_PORT-1]  = sizeof(u_int16_t)
};

int nf_ct_port_nfattr_to_tuple(struct nfattr *tb[],
			       struct nf_conntrack_tuple *t)
{
	if (!tb[CTA_PROTO_SRC_PORT-1] || !tb[CTA_PROTO_DST_PORT-1])
		return -EINVAL;

	if (nfattr_bad_size(tb, CTA_PROTO_MAX, cta_min_proto))
		return -EINVAL;

	t->src.u.tcp.port =
		*(u_int16_t *)NFA_DATA(tb[CTA_PROTO_SRC_PORT-1]);
	t->dst.u.tcp.port =
		*(u_int16_t *)NFA_DATA(tb[CTA_PROTO_DST_PORT-1]);

	return 0;
}
#endif

/* Used by ipt_REJECT and ip6t_REJECT. */
void __nf_conntrack_attach(struct sk_buff *nskb, struct sk_buff *skb)
{
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;

	/* This ICMP is in reverse direction to the packet which caused it */
	ct = nf_ct_get(skb, &ctinfo);
	if (CTINFO2DIR(ctinfo) == IP_CT_DIR_ORIGINAL)
		ctinfo = IP_CT_RELATED + IP_CT_IS_REPLY;
	else
		ctinfo = IP_CT_RELATED;

	/* Attach to new skbuff, and increment count */
	nskb->nfct = &ct->ct_general;
	nskb->nfctinfo = ctinfo;
	nf_conntrack_get(nskb->nfct);
}

static inline int
do_iter(const struct nf_conntrack_tuple_hash *i,
	int (*iter)(struct nf_conn *i, void *data),
	void *data)
{
	return iter(nf_ct_tuplehash_to_ctrack(i), data);
}

/* Bring out ya dead! */
static struct nf_conntrack_tuple_hash *
get_next_corpse(int (*iter)(struct nf_conn *i, void *data),
		void *data, unsigned int *bucket)
{
	struct nf_conntrack_tuple_hash *h = NULL;

	write_lock_bh(&nf_conntrack_lock);
	for (; *bucket < nf_conntrack_htable_size; (*bucket)++) {
		h = LIST_FIND_W(&nf_conntrack_hash[*bucket], do_iter,
				struct nf_conntrack_tuple_hash *, iter, data);
		if (h)
			break;
 	}
	if (!h)
		h = LIST_FIND_W(&unconfirmed, do_iter,
				struct nf_conntrack_tuple_hash *, iter, data);
	if (h)
		atomic_inc(&nf_ct_tuplehash_to_ctrack(h)->ct_general.use);
	write_unlock_bh(&nf_conntrack_lock);

	return h;
}

void
nf_ct_iterate_cleanup(int (*iter)(struct nf_conn *i, void *data), void *data)
{
	struct nf_conntrack_tuple_hash *h;
	unsigned int bucket = 0;

	while ((h = get_next_corpse(iter, data, &bucket)) != NULL) {
		struct nf_conn *ct = nf_ct_tuplehash_to_ctrack(h);
		/* Time to push up daises... */
		if (del_timer(&ct->timeout))
			death_by_timeout((unsigned long)ct);
		/* ... else the timer will get him soon. */

		nf_ct_put(ct);
	}
}

static int kill_all(struct nf_conn *i, void *data)
{
	return 1;
}

static void free_conntrack_hash(struct list_head *hash, int vmalloced, int size)
{
	if (vmalloced)
		vfree(hash);
	else
		free_pages((unsigned long)hash, 
			   get_order(sizeof(struct list_head) * size));
}

void nf_conntrack_flush()
{
	nf_ct_iterate_cleanup(kill_all, NULL);
}

/* Mishearing the voices in his head, our hero wonders how he's
   supposed to kill the mall. */
void nf_conntrack_cleanup(void)
{
	int i;

	ip_ct_attach = NULL;

	/* This makes sure all current packets have passed through
	   netfilter framework.  Roll on, two-stage module
	   delete... */
	synchronize_net();

	nf_ct_event_cache_flush();
 i_see_dead_people:
	nf_conntrack_flush();
	if (atomic_read(&nf_conntrack_count) != 0) {
		schedule();
		goto i_see_dead_people;
	}
	/* wait until all references to nf_conntrack_untracked are dropped */
	while (atomic_read(&nf_conntrack_untracked.ct_general.use) > 1)
		schedule();

	for (i = 0; i < NF_CT_F_NUM; i++) {
		if (nf_ct_cache[i].use == 0)
			continue;

		NF_CT_ASSERT(nf_ct_cache[i].use == 1);
		nf_ct_cache[i].use = 1;
		nf_conntrack_unregister_cache(i);
	}
	kmem_cache_destroy(nf_conntrack_expect_cachep);
	free_conntrack_hash(nf_conntrack_hash, nf_conntrack_vmalloc,
			    nf_conntrack_htable_size);

	/* free l3proto protocol tables */
	for (i = 0; i < PF_MAX; i++)
		if (nf_ct_protos[i]) {
			kfree(nf_ct_protos[i]);
			nf_ct_protos[i] = NULL;
		}
}

static struct list_head *alloc_hashtable(int size, int *vmalloced)
{
	struct list_head *hash;
	unsigned int i;

	*vmalloced = 0; 
	hash = (void*)__get_free_pages(GFP_KERNEL, 
				       get_order(sizeof(struct list_head)
						 * size));
	if (!hash) { 
		*vmalloced = 1;
		printk(KERN_WARNING "nf_conntrack: falling back to vmalloc.\n");
		hash = vmalloc(sizeof(struct list_head) * size);
	}

	if (hash)
		for (i = 0; i < size; i++) 
			INIT_LIST_HEAD(&hash[i]);

	return hash;
}

int set_hashsize(const char *val, struct kernel_param *kp)
{
	int i, bucket, hashsize, vmalloced;
	int old_vmalloced, old_size;
	int rnd;
	struct list_head *hash, *old_hash;
	struct nf_conntrack_tuple_hash *h;

	/* On boot, we can set this without any fancy locking. */
	if (!nf_conntrack_htable_size)
		return param_set_uint(val, kp);

	hashsize = simple_strtol(val, NULL, 0);
	if (!hashsize)
		return -EINVAL;

	hash = alloc_hashtable(hashsize, &vmalloced);
	if (!hash)
		return -ENOMEM;

	/* We have to rehahs for the new table anyway, so we also can
	 * use a newrandom seed */
	get_random_bytes(&rnd, 4);

	write_lock_bh(&nf_conntrack_lock);
	for (i = 0; i < nf_conntrack_htable_size; i++) {
		while (!list_empty(&nf_conntrack_hash[i])) {
			h = list_entry(nf_conntrack_hash[i].next,
				       struct nf_conntrack_tuple_hash, list);
			list_del(&h->list);
			bucket = __hash_conntrack(&h->tuple, hashsize, rnd);
			list_add_tail(&h->list, &hash[bucket]);
		}
	}
	old_size = nf_conntrack_htable_size;
	old_vmalloced = nf_conntrack_vmalloc;
	old_hash = nf_conntrack_hash;

	nf_conntrack_htable_size = hashsize;
	nf_conntrack_vmalloc = vmalloced;
	nf_conntrack_hash = hash;
	nf_conntrack_hash_rnd = rnd;
	write_unlock_bh(&nf_conntrack_lock);

	free_conntrack_hash(old_hash, old_vmalloced, old_size);
	return 0;
}

module_param_call(hashsize, set_hashsize, param_get_uint,
		  &nf_conntrack_htable_size, 0600);

int __init nf_conntrack_init(void)
{
	unsigned int i;
	int ret;

	/* Idea from tcp.c: use 1/16384 of memory.  On i386: 32MB
	 * machine has 256 buckets.  >= 1GB machines have 8192 buckets. */
	if (!nf_conntrack_htable_size) {
		nf_conntrack_htable_size
			= (((num_physpages << PAGE_SHIFT) / 16384)
			   / sizeof(struct list_head));
		if (num_physpages > (1024 * 1024 * 1024 / PAGE_SIZE))
			nf_conntrack_htable_size = 8192;
		if (nf_conntrack_htable_size < 16)
			nf_conntrack_htable_size = 16;
	}
	nf_conntrack_max = 8 * nf_conntrack_htable_size;

	printk("nf_conntrack version %s (%u buckets, %d max)\n",
	       NF_CONNTRACK_VERSION, nf_conntrack_htable_size,
	       nf_conntrack_max);

	nf_conntrack_hash = alloc_hashtable(nf_conntrack_htable_size,
					    &nf_conntrack_vmalloc);
	if (!nf_conntrack_hash) {
		printk(KERN_ERR "Unable to create nf_conntrack_hash\n");
		goto err_out;
	}

	ret = nf_conntrack_register_cache(NF_CT_F_BASIC, "nf_conntrack:basic",
					  sizeof(struct nf_conn));
	if (ret < 0) {
		printk(KERN_ERR "Unable to create nf_conn slab cache\n");
		goto err_free_hash;
	}

	nf_conntrack_expect_cachep = kmem_cache_create("nf_conntrack_expect",
					sizeof(struct nf_conntrack_expect),
					0, 0, NULL, NULL);
	if (!nf_conntrack_expect_cachep) {
		printk(KERN_ERR "Unable to create nf_expect slab cache\n");
		goto err_free_conntrack_slab;
	}

	/* Don't NEED lock here, but good form anyway. */
	write_lock_bh(&nf_conntrack_lock);
        for (i = 0; i < PF_MAX; i++)
		nf_ct_l3protos[i] = &nf_conntrack_generic_l3proto;
        write_unlock_bh(&nf_conntrack_lock);

	/* For use by REJECT target */
	ip_ct_attach = __nf_conntrack_attach;

	/* Set up fake conntrack:
	    - to never be deleted, not in any hashes */
	atomic_set(&nf_conntrack_untracked.ct_general.use, 1);
	/*  - and look it like as a confirmed connection */
	set_bit(IPS_CONFIRMED_BIT, &nf_conntrack_untracked.status);

	return ret;

err_free_conntrack_slab:
	nf_conntrack_unregister_cache(NF_CT_F_BASIC);
err_free_hash:
	free_conntrack_hash(nf_conntrack_hash, nf_conntrack_vmalloc,
			    nf_conntrack_htable_size);
err_out:
	return -ENOMEM;
}
