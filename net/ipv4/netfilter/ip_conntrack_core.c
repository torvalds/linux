/* Connection state tracking for netfilter.  This is separated from,
   but required by, the NAT layer; it can also be used by an iptables
   extension. */

/* (C) 1999-2001 Paul `Rusty' Russell  
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * 23 Apr 2001: Harald Welte <laforge@gnumonks.org>
 * 	- new API and handling of conntrack/nat helpers
 * 	- now capable of multiple expectations for one master
 * 16 Jul 2002: Harald Welte <laforge@gnumonks.org>
 * 	- add usage/reference counts to ip_conntrack_expect
 *	- export ip_conntrack[_expect]_{find_get,put} functions
 * */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/icmp.h>
#include <linux/ip.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <net/checksum.h>
#include <net/ip.h>
#include <linux/stddef.h>
#include <linux/sysctl.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/jhash.h>
#include <linux/err.h>
#include <linux/percpu.h>
#include <linux/moduleparam.h>

/* This rwlock protects the main hash table, protocol/helper/expected
   registrations, conntrack timers*/
#define ASSERT_READ_LOCK(x) MUST_BE_READ_LOCKED(&ip_conntrack_lock)
#define ASSERT_WRITE_LOCK(x) MUST_BE_WRITE_LOCKED(&ip_conntrack_lock)

#include <linux/netfilter_ipv4/ip_conntrack.h>
#include <linux/netfilter_ipv4/ip_conntrack_protocol.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#include <linux/netfilter_ipv4/ip_conntrack_core.h>
#include <linux/netfilter_ipv4/listhelp.h>

#define IP_CONNTRACK_VERSION	"2.1"

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

DECLARE_RWLOCK(ip_conntrack_lock);

/* ip_conntrack_standalone needs this */
atomic_t ip_conntrack_count = ATOMIC_INIT(0);

void (*ip_conntrack_destroyed)(struct ip_conntrack *conntrack) = NULL;
LIST_HEAD(ip_conntrack_expect_list);
struct ip_conntrack_protocol *ip_ct_protos[MAX_IP_CT_PROTO];
static LIST_HEAD(helpers);
unsigned int ip_conntrack_htable_size = 0;
int ip_conntrack_max;
struct list_head *ip_conntrack_hash;
static kmem_cache_t *ip_conntrack_cachep;
static kmem_cache_t *ip_conntrack_expect_cachep;
struct ip_conntrack ip_conntrack_untracked;
unsigned int ip_ct_log_invalid;
static LIST_HEAD(unconfirmed);
static int ip_conntrack_vmalloc;

DEFINE_PER_CPU(struct ip_conntrack_stat, ip_conntrack_stat);

void 
ip_conntrack_put(struct ip_conntrack *ct)
{
	IP_NF_ASSERT(ct);
	nf_conntrack_put(&ct->ct_general);
}

static int ip_conntrack_hash_rnd_initted;
static unsigned int ip_conntrack_hash_rnd;

static u_int32_t
hash_conntrack(const struct ip_conntrack_tuple *tuple)
{
#if 0
	dump_tuple(tuple);
#endif
	return (jhash_3words(tuple->src.ip,
	                     (tuple->dst.ip ^ tuple->dst.protonum),
	                     (tuple->src.u.all | (tuple->dst.u.all << 16)),
	                     ip_conntrack_hash_rnd) % ip_conntrack_htable_size);
}

int
ip_ct_get_tuple(const struct iphdr *iph,
		const struct sk_buff *skb,
		unsigned int dataoff,
		struct ip_conntrack_tuple *tuple,
		const struct ip_conntrack_protocol *protocol)
{
	/* Never happen */
	if (iph->frag_off & htons(IP_OFFSET)) {
		printk("ip_conntrack_core: Frag of proto %u.\n",
		       iph->protocol);
		return 0;
	}

	tuple->src.ip = iph->saddr;
	tuple->dst.ip = iph->daddr;
	tuple->dst.protonum = iph->protocol;
	tuple->dst.dir = IP_CT_DIR_ORIGINAL;

	return protocol->pkt_to_tuple(skb, dataoff, tuple);
}

int
ip_ct_invert_tuple(struct ip_conntrack_tuple *inverse,
		   const struct ip_conntrack_tuple *orig,
		   const struct ip_conntrack_protocol *protocol)
{
	inverse->src.ip = orig->dst.ip;
	inverse->dst.ip = orig->src.ip;
	inverse->dst.protonum = orig->dst.protonum;
	inverse->dst.dir = !orig->dst.dir;

	return protocol->invert_tuple(inverse, orig);
}


/* ip_conntrack_expect helper functions */
static void destroy_expect(struct ip_conntrack_expect *exp)
{
	ip_conntrack_put(exp->master);
	IP_NF_ASSERT(!timer_pending(&exp->timeout));
	kmem_cache_free(ip_conntrack_expect_cachep, exp);
	CONNTRACK_STAT_INC(expect_delete);
}

static void unlink_expect(struct ip_conntrack_expect *exp)
{
	MUST_BE_WRITE_LOCKED(&ip_conntrack_lock);
	list_del(&exp->list);
	/* Logically in destroy_expect, but we hold the lock here. */
	exp->master->expecting--;
}

static void expectation_timed_out(unsigned long ul_expect)
{
	struct ip_conntrack_expect *exp = (void *)ul_expect;

	WRITE_LOCK(&ip_conntrack_lock);
	unlink_expect(exp);
	WRITE_UNLOCK(&ip_conntrack_lock);
	destroy_expect(exp);
}

/* If an expectation for this connection is found, it gets delete from
 * global list then returned. */
static struct ip_conntrack_expect *
find_expectation(const struct ip_conntrack_tuple *tuple)
{
	struct ip_conntrack_expect *i;

	list_for_each_entry(i, &ip_conntrack_expect_list, list) {
		/* If master is not in hash table yet (ie. packet hasn't left
		   this machine yet), how can other end know about expected?
		   Hence these are not the droids you are looking for (if
		   master ct never got confirmed, we'd hold a reference to it
		   and weird things would happen to future packets). */
		if (ip_ct_tuple_mask_cmp(tuple, &i->tuple, &i->mask)
		    && is_confirmed(i->master)
		    && del_timer(&i->timeout)) {
			unlink_expect(i);
			return i;
		}
	}
	return NULL;
}

/* delete all expectations for this conntrack */
static void remove_expectations(struct ip_conntrack *ct)
{
	struct ip_conntrack_expect *i, *tmp;

	/* Optimization: most connection never expect any others. */
	if (ct->expecting == 0)
		return;

	list_for_each_entry_safe(i, tmp, &ip_conntrack_expect_list, list) {
		if (i->master == ct && del_timer(&i->timeout)) {
			unlink_expect(i);
			destroy_expect(i);
		}
	}
}

static void
clean_from_lists(struct ip_conntrack *ct)
{
	unsigned int ho, hr;
	
	DEBUGP("clean_from_lists(%p)\n", ct);
	MUST_BE_WRITE_LOCKED(&ip_conntrack_lock);

	ho = hash_conntrack(&ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple);
	hr = hash_conntrack(&ct->tuplehash[IP_CT_DIR_REPLY].tuple);
	LIST_DELETE(&ip_conntrack_hash[ho], &ct->tuplehash[IP_CT_DIR_ORIGINAL]);
	LIST_DELETE(&ip_conntrack_hash[hr], &ct->tuplehash[IP_CT_DIR_REPLY]);

	/* Destroy all pending expectations */
	remove_expectations(ct);
}

static void
destroy_conntrack(struct nf_conntrack *nfct)
{
	struct ip_conntrack *ct = (struct ip_conntrack *)nfct;
	struct ip_conntrack_protocol *proto;

	DEBUGP("destroy_conntrack(%p)\n", ct);
	IP_NF_ASSERT(atomic_read(&nfct->use) == 0);
	IP_NF_ASSERT(!timer_pending(&ct->timeout));

	/* To make sure we don't get any weird locking issues here:
	 * destroy_conntrack() MUST NOT be called with a write lock
	 * to ip_conntrack_lock!!! -HW */
	proto = ip_ct_find_proto(ct->tuplehash[IP_CT_DIR_REPLY].tuple.dst.protonum);
	if (proto && proto->destroy)
		proto->destroy(ct);

	if (ip_conntrack_destroyed)
		ip_conntrack_destroyed(ct);

	WRITE_LOCK(&ip_conntrack_lock);
	/* Expectations will have been removed in clean_from_lists,
	 * except TFTP can create an expectation on the first packet,
	 * before connection is in the list, so we need to clean here,
	 * too. */
	remove_expectations(ct);

	/* We overload first tuple to link into unconfirmed list. */
	if (!is_confirmed(ct)) {
		BUG_ON(list_empty(&ct->tuplehash[IP_CT_DIR_ORIGINAL].list));
		list_del(&ct->tuplehash[IP_CT_DIR_ORIGINAL].list);
	}

	CONNTRACK_STAT_INC(delete);
	WRITE_UNLOCK(&ip_conntrack_lock);

	if (ct->master)
		ip_conntrack_put(ct->master);

	DEBUGP("destroy_conntrack: returning ct=%p to slab\n", ct);
	kmem_cache_free(ip_conntrack_cachep, ct);
	atomic_dec(&ip_conntrack_count);
}

static void death_by_timeout(unsigned long ul_conntrack)
{
	struct ip_conntrack *ct = (void *)ul_conntrack;

	WRITE_LOCK(&ip_conntrack_lock);
	/* Inside lock so preempt is disabled on module removal path.
	 * Otherwise we can get spurious warnings. */
	CONNTRACK_STAT_INC(delete_list);
	clean_from_lists(ct);
	WRITE_UNLOCK(&ip_conntrack_lock);
	ip_conntrack_put(ct);
}

static inline int
conntrack_tuple_cmp(const struct ip_conntrack_tuple_hash *i,
		    const struct ip_conntrack_tuple *tuple,
		    const struct ip_conntrack *ignored_conntrack)
{
	MUST_BE_READ_LOCKED(&ip_conntrack_lock);
	return tuplehash_to_ctrack(i) != ignored_conntrack
		&& ip_ct_tuple_equal(tuple, &i->tuple);
}

static struct ip_conntrack_tuple_hash *
__ip_conntrack_find(const struct ip_conntrack_tuple *tuple,
		    const struct ip_conntrack *ignored_conntrack)
{
	struct ip_conntrack_tuple_hash *h;
	unsigned int hash = hash_conntrack(tuple);

	MUST_BE_READ_LOCKED(&ip_conntrack_lock);
	list_for_each_entry(h, &ip_conntrack_hash[hash], list) {
		if (conntrack_tuple_cmp(h, tuple, ignored_conntrack)) {
			CONNTRACK_STAT_INC(found);
			return h;
		}
		CONNTRACK_STAT_INC(searched);
	}

	return NULL;
}

/* Find a connection corresponding to a tuple. */
struct ip_conntrack_tuple_hash *
ip_conntrack_find_get(const struct ip_conntrack_tuple *tuple,
		      const struct ip_conntrack *ignored_conntrack)
{
	struct ip_conntrack_tuple_hash *h;

	READ_LOCK(&ip_conntrack_lock);
	h = __ip_conntrack_find(tuple, ignored_conntrack);
	if (h)
		atomic_inc(&tuplehash_to_ctrack(h)->ct_general.use);
	READ_UNLOCK(&ip_conntrack_lock);

	return h;
}

/* Confirm a connection given skb; places it in hash table */
int
__ip_conntrack_confirm(struct sk_buff **pskb)
{
	unsigned int hash, repl_hash;
	struct ip_conntrack *ct;
	enum ip_conntrack_info ctinfo;

	ct = ip_conntrack_get(*pskb, &ctinfo);

	/* ipt_REJECT uses ip_conntrack_attach to attach related
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
	/* IP_NF_ASSERT(atomic_read(&ct->ct_general.use) == 1); */

	/* No external references means noone else could have
           confirmed us. */
	IP_NF_ASSERT(!is_confirmed(ct));
	DEBUGP("Confirming conntrack %p\n", ct);

	WRITE_LOCK(&ip_conntrack_lock);

	/* See if there's one in the list already, including reverse:
           NAT could have grabbed it without realizing, since we're
           not in the hash.  If there is, we lost race. */
	if (!LIST_FIND(&ip_conntrack_hash[hash],
		       conntrack_tuple_cmp,
		       struct ip_conntrack_tuple_hash *,
		       &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple, NULL)
	    && !LIST_FIND(&ip_conntrack_hash[repl_hash],
			  conntrack_tuple_cmp,
			  struct ip_conntrack_tuple_hash *,
			  &ct->tuplehash[IP_CT_DIR_REPLY].tuple, NULL)) {
		/* Remove from unconfirmed list */
		list_del(&ct->tuplehash[IP_CT_DIR_ORIGINAL].list);

		list_prepend(&ip_conntrack_hash[hash],
			     &ct->tuplehash[IP_CT_DIR_ORIGINAL]);
		list_prepend(&ip_conntrack_hash[repl_hash],
			     &ct->tuplehash[IP_CT_DIR_REPLY]);
		/* Timer relative to confirmation time, not original
		   setting time, otherwise we'd get timer wrap in
		   weird delay cases. */
		ct->timeout.expires += jiffies;
		add_timer(&ct->timeout);
		atomic_inc(&ct->ct_general.use);
		set_bit(IPS_CONFIRMED_BIT, &ct->status);
		CONNTRACK_STAT_INC(insert);
		WRITE_UNLOCK(&ip_conntrack_lock);
		return NF_ACCEPT;
	}

	CONNTRACK_STAT_INC(insert_failed);
	WRITE_UNLOCK(&ip_conntrack_lock);

	return NF_DROP;
}

/* Returns true if a connection correspondings to the tuple (required
   for NAT). */
int
ip_conntrack_tuple_taken(const struct ip_conntrack_tuple *tuple,
			 const struct ip_conntrack *ignored_conntrack)
{
	struct ip_conntrack_tuple_hash *h;

	READ_LOCK(&ip_conntrack_lock);
	h = __ip_conntrack_find(tuple, ignored_conntrack);
	READ_UNLOCK(&ip_conntrack_lock);

	return h != NULL;
}

/* There's a small race here where we may free a just-assured
   connection.  Too bad: we're in trouble anyway. */
static inline int unreplied(const struct ip_conntrack_tuple_hash *i)
{
	return !(test_bit(IPS_ASSURED_BIT, &tuplehash_to_ctrack(i)->status));
}

static int early_drop(struct list_head *chain)
{
	/* Traverse backwards: gives us oldest, which is roughly LRU */
	struct ip_conntrack_tuple_hash *h;
	struct ip_conntrack *ct = NULL;
	int dropped = 0;

	READ_LOCK(&ip_conntrack_lock);
	h = LIST_FIND_B(chain, unreplied, struct ip_conntrack_tuple_hash *);
	if (h) {
		ct = tuplehash_to_ctrack(h);
		atomic_inc(&ct->ct_general.use);
	}
	READ_UNLOCK(&ip_conntrack_lock);

	if (!ct)
		return dropped;

	if (del_timer(&ct->timeout)) {
		death_by_timeout((unsigned long)ct);
		dropped = 1;
		CONNTRACK_STAT_INC(early_drop);
	}
	ip_conntrack_put(ct);
	return dropped;
}

static inline int helper_cmp(const struct ip_conntrack_helper *i,
			     const struct ip_conntrack_tuple *rtuple)
{
	return ip_ct_tuple_mask_cmp(rtuple, &i->tuple, &i->mask);
}

static struct ip_conntrack_helper *ip_ct_find_helper(const struct ip_conntrack_tuple *tuple)
{
	return LIST_FIND(&helpers, helper_cmp,
			 struct ip_conntrack_helper *,
			 tuple);
}

/* Allocate a new conntrack: we return -ENOMEM if classification
   failed due to stress.  Otherwise it really is unclassifiable. */
static struct ip_conntrack_tuple_hash *
init_conntrack(const struct ip_conntrack_tuple *tuple,
	       struct ip_conntrack_protocol *protocol,
	       struct sk_buff *skb)
{
	struct ip_conntrack *conntrack;
	struct ip_conntrack_tuple repl_tuple;
	size_t hash;
	struct ip_conntrack_expect *exp;

	if (!ip_conntrack_hash_rnd_initted) {
		get_random_bytes(&ip_conntrack_hash_rnd, 4);
		ip_conntrack_hash_rnd_initted = 1;
	}

	hash = hash_conntrack(tuple);

	if (ip_conntrack_max
	    && atomic_read(&ip_conntrack_count) >= ip_conntrack_max) {
		/* Try dropping from this hash chain. */
		if (!early_drop(&ip_conntrack_hash[hash])) {
			if (net_ratelimit())
				printk(KERN_WARNING
				       "ip_conntrack: table full, dropping"
				       " packet.\n");
			return ERR_PTR(-ENOMEM);
		}
	}

	if (!ip_ct_invert_tuple(&repl_tuple, tuple, protocol)) {
		DEBUGP("Can't invert tuple.\n");
		return NULL;
	}

	conntrack = kmem_cache_alloc(ip_conntrack_cachep, GFP_ATOMIC);
	if (!conntrack) {
		DEBUGP("Can't allocate conntrack.\n");
		return ERR_PTR(-ENOMEM);
	}

	memset(conntrack, 0, sizeof(*conntrack));
	atomic_set(&conntrack->ct_general.use, 1);
	conntrack->ct_general.destroy = destroy_conntrack;
	conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple = *tuple;
	conntrack->tuplehash[IP_CT_DIR_REPLY].tuple = repl_tuple;
	if (!protocol->new(conntrack, skb)) {
		kmem_cache_free(ip_conntrack_cachep, conntrack);
		return NULL;
	}
	/* Don't set timer yet: wait for confirmation */
	init_timer(&conntrack->timeout);
	conntrack->timeout.data = (unsigned long)conntrack;
	conntrack->timeout.function = death_by_timeout;

	WRITE_LOCK(&ip_conntrack_lock);
	exp = find_expectation(tuple);

	if (exp) {
		DEBUGP("conntrack: expectation arrives ct=%p exp=%p\n",
			conntrack, exp);
		/* Welcome, Mr. Bond.  We've been expecting you... */
		__set_bit(IPS_EXPECTED_BIT, &conntrack->status);
		conntrack->master = exp->master;
#if CONFIG_IP_NF_CONNTRACK_MARK
		conntrack->mark = exp->master->mark;
#endif
		nf_conntrack_get(&conntrack->master->ct_general);
		CONNTRACK_STAT_INC(expect_new);
	} else {
		conntrack->helper = ip_ct_find_helper(&repl_tuple);

		CONNTRACK_STAT_INC(new);
	}

	/* Overload tuple linked list to put us in unconfirmed list. */
	list_add(&conntrack->tuplehash[IP_CT_DIR_ORIGINAL].list, &unconfirmed);

	atomic_inc(&ip_conntrack_count);
	WRITE_UNLOCK(&ip_conntrack_lock);

	if (exp) {
		if (exp->expectfn)
			exp->expectfn(conntrack, exp);
		destroy_expect(exp);
	}

	return &conntrack->tuplehash[IP_CT_DIR_ORIGINAL];
}

/* On success, returns conntrack ptr, sets skb->nfct and ctinfo */
static inline struct ip_conntrack *
resolve_normal_ct(struct sk_buff *skb,
		  struct ip_conntrack_protocol *proto,
		  int *set_reply,
		  unsigned int hooknum,
		  enum ip_conntrack_info *ctinfo)
{
	struct ip_conntrack_tuple tuple;
	struct ip_conntrack_tuple_hash *h;
	struct ip_conntrack *ct;

	IP_NF_ASSERT((skb->nh.iph->frag_off & htons(IP_OFFSET)) == 0);

	if (!ip_ct_get_tuple(skb->nh.iph, skb, skb->nh.iph->ihl*4, 
				&tuple,proto))
		return NULL;

	/* look for tuple match */
	h = ip_conntrack_find_get(&tuple, NULL);
	if (!h) {
		h = init_conntrack(&tuple, proto, skb);
		if (!h)
			return NULL;
		if (IS_ERR(h))
			return (void *)h;
	}
	ct = tuplehash_to_ctrack(h);

	/* It exists; we have (non-exclusive) reference. */
	if (DIRECTION(h) == IP_CT_DIR_REPLY) {
		*ctinfo = IP_CT_ESTABLISHED + IP_CT_IS_REPLY;
		/* Please set reply bit if this packet OK */
		*set_reply = 1;
	} else {
		/* Once we've had two way comms, always ESTABLISHED. */
		if (test_bit(IPS_SEEN_REPLY_BIT, &ct->status)) {
			DEBUGP("ip_conntrack_in: normal packet for %p\n",
			       ct);
		        *ctinfo = IP_CT_ESTABLISHED;
		} else if (test_bit(IPS_EXPECTED_BIT, &ct->status)) {
			DEBUGP("ip_conntrack_in: related packet for %p\n",
			       ct);
			*ctinfo = IP_CT_RELATED;
		} else {
			DEBUGP("ip_conntrack_in: new packet for %p\n",
			       ct);
			*ctinfo = IP_CT_NEW;
		}
		*set_reply = 0;
	}
	skb->nfct = &ct->ct_general;
	skb->nfctinfo = *ctinfo;
	return ct;
}

/* Netfilter hook itself. */
unsigned int ip_conntrack_in(unsigned int hooknum,
			     struct sk_buff **pskb,
			     const struct net_device *in,
			     const struct net_device *out,
			     int (*okfn)(struct sk_buff *))
{
	struct ip_conntrack *ct;
	enum ip_conntrack_info ctinfo;
	struct ip_conntrack_protocol *proto;
	int set_reply;
	int ret;

	/* Previously seen (loopback or untracked)?  Ignore. */
	if ((*pskb)->nfct) {
		CONNTRACK_STAT_INC(ignore);
		return NF_ACCEPT;
	}

	/* Never happen */
	if ((*pskb)->nh.iph->frag_off & htons(IP_OFFSET)) {
		if (net_ratelimit()) {
		printk(KERN_ERR "ip_conntrack_in: Frag of proto %u (hook=%u)\n",
		       (*pskb)->nh.iph->protocol, hooknum);
		}
		return NF_DROP;
	}

	/* FIXME: Do this right please. --RR */
	(*pskb)->nfcache |= NFC_UNKNOWN;

/* Doesn't cover locally-generated broadcast, so not worth it. */
#if 0
	/* Ignore broadcast: no `connection'. */
	if ((*pskb)->pkt_type == PACKET_BROADCAST) {
		printk("Broadcast packet!\n");
		return NF_ACCEPT;
	} else if (((*pskb)->nh.iph->daddr & htonl(0x000000FF)) 
		   == htonl(0x000000FF)) {
		printk("Should bcast: %u.%u.%u.%u->%u.%u.%u.%u (sk=%p, ptype=%u)\n",
		       NIPQUAD((*pskb)->nh.iph->saddr),
		       NIPQUAD((*pskb)->nh.iph->daddr),
		       (*pskb)->sk, (*pskb)->pkt_type);
	}
#endif

	proto = ip_ct_find_proto((*pskb)->nh.iph->protocol);

	/* It may be an special packet, error, unclean...
	 * inverse of the return code tells to the netfilter
	 * core what to do with the packet. */
	if (proto->error != NULL 
	    && (ret = proto->error(*pskb, &ctinfo, hooknum)) <= 0) {
		CONNTRACK_STAT_INC(error);
		CONNTRACK_STAT_INC(invalid);
		return -ret;
	}

	if (!(ct = resolve_normal_ct(*pskb, proto,&set_reply,hooknum,&ctinfo))) {
		/* Not valid part of a connection */
		CONNTRACK_STAT_INC(invalid);
		return NF_ACCEPT;
	}

	if (IS_ERR(ct)) {
		/* Too stressed to deal. */
		CONNTRACK_STAT_INC(drop);
		return NF_DROP;
	}

	IP_NF_ASSERT((*pskb)->nfct);

	ret = proto->packet(ct, *pskb, ctinfo);
	if (ret < 0) {
		/* Invalid: inverse of the return code tells
		 * the netfilter core what to do*/
		nf_conntrack_put((*pskb)->nfct);
		(*pskb)->nfct = NULL;
		CONNTRACK_STAT_INC(invalid);
		return -ret;
	}

	if (set_reply)
		set_bit(IPS_SEEN_REPLY_BIT, &ct->status);

	return ret;
}

int invert_tuplepr(struct ip_conntrack_tuple *inverse,
		   const struct ip_conntrack_tuple *orig)
{
	return ip_ct_invert_tuple(inverse, orig, 
				  ip_ct_find_proto(orig->dst.protonum));
}

/* Would two expected things clash? */
static inline int expect_clash(const struct ip_conntrack_expect *a,
			       const struct ip_conntrack_expect *b)
{
	/* Part covered by intersection of masks must be unequal,
           otherwise they clash */
	struct ip_conntrack_tuple intersect_mask
		= { { a->mask.src.ip & b->mask.src.ip,
		      { a->mask.src.u.all & b->mask.src.u.all } },
		    { a->mask.dst.ip & b->mask.dst.ip,
		      { a->mask.dst.u.all & b->mask.dst.u.all },
		      a->mask.dst.protonum & b->mask.dst.protonum } };

	return ip_ct_tuple_mask_cmp(&a->tuple, &b->tuple, &intersect_mask);
}

static inline int expect_matches(const struct ip_conntrack_expect *a,
				 const struct ip_conntrack_expect *b)
{
	return a->master == b->master
		&& ip_ct_tuple_equal(&a->tuple, &b->tuple)
		&& ip_ct_tuple_equal(&a->mask, &b->mask);
}

/* Generally a bad idea to call this: could have matched already. */
void ip_conntrack_unexpect_related(struct ip_conntrack_expect *exp)
{
	struct ip_conntrack_expect *i;

	WRITE_LOCK(&ip_conntrack_lock);
	/* choose the the oldest expectation to evict */
	list_for_each_entry_reverse(i, &ip_conntrack_expect_list, list) {
		if (expect_matches(i, exp) && del_timer(&i->timeout)) {
			unlink_expect(i);
			WRITE_UNLOCK(&ip_conntrack_lock);
			destroy_expect(i);
			return;
		}
	}
	WRITE_UNLOCK(&ip_conntrack_lock);
}

struct ip_conntrack_expect *ip_conntrack_expect_alloc(void)
{
	struct ip_conntrack_expect *new;

	new = kmem_cache_alloc(ip_conntrack_expect_cachep, GFP_ATOMIC);
	if (!new) {
		DEBUGP("expect_related: OOM allocating expect\n");
		return NULL;
	}
	new->master = NULL;
	return new;
}

void ip_conntrack_expect_free(struct ip_conntrack_expect *expect)
{
	kmem_cache_free(ip_conntrack_expect_cachep, expect);
}

static void ip_conntrack_expect_insert(struct ip_conntrack_expect *exp)
{
	atomic_inc(&exp->master->ct_general.use);
	exp->master->expecting++;
	list_add(&exp->list, &ip_conntrack_expect_list);

	if (exp->master->helper->timeout) {
		init_timer(&exp->timeout);
		exp->timeout.data = (unsigned long)exp;
		exp->timeout.function = expectation_timed_out;
		exp->timeout.expires
			= jiffies + exp->master->helper->timeout * HZ;
		add_timer(&exp->timeout);
	} else
		exp->timeout.function = NULL;

	CONNTRACK_STAT_INC(expect_create);
}

/* Race with expectations being used means we could have none to find; OK. */
static void evict_oldest_expect(struct ip_conntrack *master)
{
	struct ip_conntrack_expect *i;

	list_for_each_entry_reverse(i, &ip_conntrack_expect_list, list) {
		if (i->master == master) {
			if (del_timer(&i->timeout)) {
				unlink_expect(i);
				destroy_expect(i);
			}
			break;
		}
	}
}

static inline int refresh_timer(struct ip_conntrack_expect *i)
{
	if (!del_timer(&i->timeout))
		return 0;

	i->timeout.expires = jiffies + i->master->helper->timeout*HZ;
	add_timer(&i->timeout);
	return 1;
}

int ip_conntrack_expect_related(struct ip_conntrack_expect *expect)
{
	struct ip_conntrack_expect *i;
	int ret;

	DEBUGP("ip_conntrack_expect_related %p\n", related_to);
	DEBUGP("tuple: "); DUMP_TUPLE(&expect->tuple);
	DEBUGP("mask:  "); DUMP_TUPLE(&expect->mask);

	WRITE_LOCK(&ip_conntrack_lock);
	list_for_each_entry(i, &ip_conntrack_expect_list, list) {
		if (expect_matches(i, expect)) {
			/* Refresh timer: if it's dying, ignore.. */
			if (refresh_timer(i)) {
				ret = 0;
				/* We don't need the one they've given us. */
				ip_conntrack_expect_free(expect);
				goto out;
			}
		} else if (expect_clash(i, expect)) {
			ret = -EBUSY;
			goto out;
		}
	}

	/* Will be over limit? */
	if (expect->master->helper->max_expected && 
	    expect->master->expecting >= expect->master->helper->max_expected)
		evict_oldest_expect(expect->master);

	ip_conntrack_expect_insert(expect);
	ret = 0;
out:
	WRITE_UNLOCK(&ip_conntrack_lock);
 	return ret;
}

/* Alter reply tuple (maybe alter helper).  This is for NAT, and is
   implicitly racy: see __ip_conntrack_confirm */
void ip_conntrack_alter_reply(struct ip_conntrack *conntrack,
			      const struct ip_conntrack_tuple *newreply)
{
	WRITE_LOCK(&ip_conntrack_lock);
	/* Should be unconfirmed, so not in hash table yet */
	IP_NF_ASSERT(!is_confirmed(conntrack));

	DEBUGP("Altering reply tuple of %p to ", conntrack);
	DUMP_TUPLE(newreply);

	conntrack->tuplehash[IP_CT_DIR_REPLY].tuple = *newreply;
	if (!conntrack->master && conntrack->expecting == 0)
		conntrack->helper = ip_ct_find_helper(newreply);
	WRITE_UNLOCK(&ip_conntrack_lock);
}

int ip_conntrack_helper_register(struct ip_conntrack_helper *me)
{
	BUG_ON(me->timeout == 0);
	WRITE_LOCK(&ip_conntrack_lock);
	list_prepend(&helpers, me);
	WRITE_UNLOCK(&ip_conntrack_lock);

	return 0;
}

static inline int unhelp(struct ip_conntrack_tuple_hash *i,
			 const struct ip_conntrack_helper *me)
{
	if (tuplehash_to_ctrack(i)->helper == me)
		tuplehash_to_ctrack(i)->helper = NULL;
	return 0;
}

void ip_conntrack_helper_unregister(struct ip_conntrack_helper *me)
{
	unsigned int i;
	struct ip_conntrack_expect *exp, *tmp;

	/* Need write lock here, to delete helper. */
	WRITE_LOCK(&ip_conntrack_lock);
	LIST_DELETE(&helpers, me);

	/* Get rid of expectations */
	list_for_each_entry_safe(exp, tmp, &ip_conntrack_expect_list, list) {
		if (exp->master->helper == me && del_timer(&exp->timeout)) {
			unlink_expect(exp);
			destroy_expect(exp);
		}
	}
	/* Get rid of expecteds, set helpers to NULL. */
	LIST_FIND_W(&unconfirmed, unhelp, struct ip_conntrack_tuple_hash*, me);
	for (i = 0; i < ip_conntrack_htable_size; i++)
		LIST_FIND_W(&ip_conntrack_hash[i], unhelp,
			    struct ip_conntrack_tuple_hash *, me);
	WRITE_UNLOCK(&ip_conntrack_lock);

	/* Someone could be still looking at the helper in a bh. */
	synchronize_net();
}

static inline void ct_add_counters(struct ip_conntrack *ct,
				   enum ip_conntrack_info ctinfo,
				   const struct sk_buff *skb)
{
#ifdef CONFIG_IP_NF_CT_ACCT
	if (skb) {
		ct->counters[CTINFO2DIR(ctinfo)].packets++;
		ct->counters[CTINFO2DIR(ctinfo)].bytes += 
					ntohs(skb->nh.iph->tot_len);
	}
#endif
}

/* Refresh conntrack for this many jiffies and do accounting (if skb != NULL) */
void ip_ct_refresh_acct(struct ip_conntrack *ct, 
		        enum ip_conntrack_info ctinfo,
			const struct sk_buff *skb,
			unsigned long extra_jiffies)
{
	IP_NF_ASSERT(ct->timeout.data == (unsigned long)ct);

	/* If not in hash table, timer will not be active yet */
	if (!is_confirmed(ct)) {
		ct->timeout.expires = extra_jiffies;
		ct_add_counters(ct, ctinfo, skb);
	} else {
		WRITE_LOCK(&ip_conntrack_lock);
		/* Need del_timer for race avoidance (may already be dying). */
		if (del_timer(&ct->timeout)) {
			ct->timeout.expires = jiffies + extra_jiffies;
			add_timer(&ct->timeout);
		}
		ct_add_counters(ct, ctinfo, skb);
		WRITE_UNLOCK(&ip_conntrack_lock);
	}
}

/* Returns new sk_buff, or NULL */
struct sk_buff *
ip_ct_gather_frags(struct sk_buff *skb, u_int32_t user)
{
	struct sock *sk = skb->sk;
#ifdef CONFIG_NETFILTER_DEBUG
	unsigned int olddebug = skb->nf_debug;
#endif

	if (sk) {
		sock_hold(sk);
		skb_orphan(skb);
	}

	local_bh_disable(); 
	skb = ip_defrag(skb, user);
	local_bh_enable();

	if (!skb) {
		if (sk)
			sock_put(sk);
		return skb;
	}

	if (sk) {
		skb_set_owner_w(skb, sk);
		sock_put(sk);
	}

	ip_send_check(skb->nh.iph);
	skb->nfcache |= NFC_ALTERED;
#ifdef CONFIG_NETFILTER_DEBUG
	/* Packet path as if nothing had happened. */
	skb->nf_debug = olddebug;
#endif
	return skb;
}

/* Used by ipt_REJECT. */
static void ip_conntrack_attach(struct sk_buff *nskb, struct sk_buff *skb)
{
	struct ip_conntrack *ct;
	enum ip_conntrack_info ctinfo;

	/* This ICMP is in reverse direction to the packet which caused it */
	ct = ip_conntrack_get(skb, &ctinfo);
	
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
do_iter(const struct ip_conntrack_tuple_hash *i,
	int (*iter)(struct ip_conntrack *i, void *data),
	void *data)
{
	return iter(tuplehash_to_ctrack(i), data);
}

/* Bring out ya dead! */
static struct ip_conntrack_tuple_hash *
get_next_corpse(int (*iter)(struct ip_conntrack *i, void *data),
		void *data, unsigned int *bucket)
{
	struct ip_conntrack_tuple_hash *h = NULL;

	WRITE_LOCK(&ip_conntrack_lock);
	for (; *bucket < ip_conntrack_htable_size; (*bucket)++) {
		h = LIST_FIND_W(&ip_conntrack_hash[*bucket], do_iter,
				struct ip_conntrack_tuple_hash *, iter, data);
		if (h)
			break;
	}
	if (!h)
		h = LIST_FIND_W(&unconfirmed, do_iter,
				struct ip_conntrack_tuple_hash *, iter, data);
	if (h)
		atomic_inc(&tuplehash_to_ctrack(h)->ct_general.use);
	WRITE_UNLOCK(&ip_conntrack_lock);

	return h;
}

void
ip_ct_iterate_cleanup(int (*iter)(struct ip_conntrack *i, void *), void *data)
{
	struct ip_conntrack_tuple_hash *h;
	unsigned int bucket = 0;

	while ((h = get_next_corpse(iter, data, &bucket)) != NULL) {
		struct ip_conntrack *ct = tuplehash_to_ctrack(h);
		/* Time to push up daises... */
		if (del_timer(&ct->timeout))
			death_by_timeout((unsigned long)ct);
		/* ... else the timer will get him soon. */

		ip_conntrack_put(ct);
	}
}

/* Fast function for those who don't want to parse /proc (and I don't
   blame them). */
/* Reversing the socket's dst/src point of view gives us the reply
   mapping. */
static int
getorigdst(struct sock *sk, int optval, void __user *user, int *len)
{
	struct inet_sock *inet = inet_sk(sk);
	struct ip_conntrack_tuple_hash *h;
	struct ip_conntrack_tuple tuple;
	
	IP_CT_TUPLE_U_BLANK(&tuple);
	tuple.src.ip = inet->rcv_saddr;
	tuple.src.u.tcp.port = inet->sport;
	tuple.dst.ip = inet->daddr;
	tuple.dst.u.tcp.port = inet->dport;
	tuple.dst.protonum = IPPROTO_TCP;

	/* We only do TCP at the moment: is there a better way? */
	if (strcmp(sk->sk_prot->name, "TCP")) {
		DEBUGP("SO_ORIGINAL_DST: Not a TCP socket\n");
		return -ENOPROTOOPT;
	}

	if ((unsigned int) *len < sizeof(struct sockaddr_in)) {
		DEBUGP("SO_ORIGINAL_DST: len %u not %u\n",
		       *len, sizeof(struct sockaddr_in));
		return -EINVAL;
	}

	h = ip_conntrack_find_get(&tuple, NULL);
	if (h) {
		struct sockaddr_in sin;
		struct ip_conntrack *ct = tuplehash_to_ctrack(h);

		sin.sin_family = AF_INET;
		sin.sin_port = ct->tuplehash[IP_CT_DIR_ORIGINAL]
			.tuple.dst.u.tcp.port;
		sin.sin_addr.s_addr = ct->tuplehash[IP_CT_DIR_ORIGINAL]
			.tuple.dst.ip;

		DEBUGP("SO_ORIGINAL_DST: %u.%u.%u.%u %u\n",
		       NIPQUAD(sin.sin_addr.s_addr), ntohs(sin.sin_port));
		ip_conntrack_put(ct);
		if (copy_to_user(user, &sin, sizeof(sin)) != 0)
			return -EFAULT;
		else
			return 0;
	}
	DEBUGP("SO_ORIGINAL_DST: Can't find %u.%u.%u.%u/%u-%u.%u.%u.%u/%u.\n",
	       NIPQUAD(tuple.src.ip), ntohs(tuple.src.u.tcp.port),
	       NIPQUAD(tuple.dst.ip), ntohs(tuple.dst.u.tcp.port));
	return -ENOENT;
}

static struct nf_sockopt_ops so_getorigdst = {
	.pf		= PF_INET,
	.get_optmin	= SO_ORIGINAL_DST,
	.get_optmax	= SO_ORIGINAL_DST+1,
	.get		= &getorigdst,
};

static int kill_all(struct ip_conntrack *i, void *data)
{
	return 1;
}

static void free_conntrack_hash(void)
{
	if (ip_conntrack_vmalloc)
		vfree(ip_conntrack_hash);
	else
		free_pages((unsigned long)ip_conntrack_hash, 
			   get_order(sizeof(struct list_head)
				     * ip_conntrack_htable_size));
}

/* Mishearing the voices in his head, our hero wonders how he's
   supposed to kill the mall. */
void ip_conntrack_cleanup(void)
{
	ip_ct_attach = NULL;
	/* This makes sure all current packets have passed through
           netfilter framework.  Roll on, two-stage module
           delete... */
	synchronize_net();
 
 i_see_dead_people:
	ip_ct_iterate_cleanup(kill_all, NULL);
	if (atomic_read(&ip_conntrack_count) != 0) {
		schedule();
		goto i_see_dead_people;
	}

	kmem_cache_destroy(ip_conntrack_cachep);
	kmem_cache_destroy(ip_conntrack_expect_cachep);
	free_conntrack_hash();
	nf_unregister_sockopt(&so_getorigdst);
}

static int hashsize;
module_param(hashsize, int, 0400);

int __init ip_conntrack_init(void)
{
	unsigned int i;
	int ret;

	/* Idea from tcp.c: use 1/16384 of memory.  On i386: 32MB
	 * machine has 256 buckets.  >= 1GB machines have 8192 buckets. */
 	if (hashsize) {
 		ip_conntrack_htable_size = hashsize;
 	} else {
		ip_conntrack_htable_size
			= (((num_physpages << PAGE_SHIFT) / 16384)
			   / sizeof(struct list_head));
		if (num_physpages > (1024 * 1024 * 1024 / PAGE_SIZE))
			ip_conntrack_htable_size = 8192;
		if (ip_conntrack_htable_size < 16)
			ip_conntrack_htable_size = 16;
	}
	ip_conntrack_max = 8 * ip_conntrack_htable_size;

	printk("ip_conntrack version %s (%u buckets, %d max)"
	       " - %Zd bytes per conntrack\n", IP_CONNTRACK_VERSION,
	       ip_conntrack_htable_size, ip_conntrack_max,
	       sizeof(struct ip_conntrack));

	ret = nf_register_sockopt(&so_getorigdst);
	if (ret != 0) {
		printk(KERN_ERR "Unable to register netfilter socket option\n");
		return ret;
	}

	/* AK: the hash table is twice as big than needed because it
	   uses list_head.  it would be much nicer to caches to use a
	   single pointer list head here. */
	ip_conntrack_vmalloc = 0; 
	ip_conntrack_hash 
		=(void*)__get_free_pages(GFP_KERNEL, 
					 get_order(sizeof(struct list_head)
						   *ip_conntrack_htable_size));
	if (!ip_conntrack_hash) { 
		ip_conntrack_vmalloc = 1;
		printk(KERN_WARNING "ip_conntrack: falling back to vmalloc.\n");
		ip_conntrack_hash = vmalloc(sizeof(struct list_head)
					    * ip_conntrack_htable_size);
	}
	if (!ip_conntrack_hash) {
		printk(KERN_ERR "Unable to create ip_conntrack_hash\n");
		goto err_unreg_sockopt;
	}

	ip_conntrack_cachep = kmem_cache_create("ip_conntrack",
	                                        sizeof(struct ip_conntrack), 0,
	                                        0, NULL, NULL);
	if (!ip_conntrack_cachep) {
		printk(KERN_ERR "Unable to create ip_conntrack slab cache\n");
		goto err_free_hash;
	}

	ip_conntrack_expect_cachep = kmem_cache_create("ip_conntrack_expect",
					sizeof(struct ip_conntrack_expect),
					0, 0, NULL, NULL);
	if (!ip_conntrack_expect_cachep) {
		printk(KERN_ERR "Unable to create ip_expect slab cache\n");
		goto err_free_conntrack_slab;
	}

	/* Don't NEED lock here, but good form anyway. */
	WRITE_LOCK(&ip_conntrack_lock);
	for (i = 0; i < MAX_IP_CT_PROTO; i++)
		ip_ct_protos[i] = &ip_conntrack_generic_protocol;
	/* Sew in builtin protocols. */
	ip_ct_protos[IPPROTO_TCP] = &ip_conntrack_protocol_tcp;
	ip_ct_protos[IPPROTO_UDP] = &ip_conntrack_protocol_udp;
	ip_ct_protos[IPPROTO_ICMP] = &ip_conntrack_protocol_icmp;
	WRITE_UNLOCK(&ip_conntrack_lock);

	for (i = 0; i < ip_conntrack_htable_size; i++)
		INIT_LIST_HEAD(&ip_conntrack_hash[i]);

	/* For use by ipt_REJECT */
	ip_ct_attach = ip_conntrack_attach;

	/* Set up fake conntrack:
	    - to never be deleted, not in any hashes */
	atomic_set(&ip_conntrack_untracked.ct_general.use, 1);
	/*  - and look it like as a confirmed connection */
	set_bit(IPS_CONFIRMED_BIT, &ip_conntrack_untracked.status);

	return ret;

err_free_conntrack_slab:
	kmem_cache_destroy(ip_conntrack_cachep);
err_free_hash:
	free_conntrack_hash();
err_unreg_sockopt:
	nf_unregister_sockopt(&so_getorigdst);

	return -ENOMEM;
}
