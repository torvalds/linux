/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Connection state tracking for netfilter.  This is separated from,
 * but required by, the (future) NAT layer; it can also be used by an iptables
 * extension.
 *
 * 16 Dec 2003: Yasuyuki Kozakai @USAGI <yasuyuki.kozakai@toshiba.co.jp>
 *	- generalize L3 protocol dependent part.
 *
 * Derived from include/linux/netfiter_ipv4/ip_conntrack.h
 */

#ifndef _NF_CONNTRACK_H
#define _NF_CONNTRACK_H

#include <linux/bitops.h>
#include <linux/compiler.h>
#include <linux/android_vendor.h>
#include <linux/android_kabi.h>

#include <linux/netfilter/nf_conntrack_common.h>
#include <linux/netfilter/nf_conntrack_tcp.h>
#include <linux/netfilter/nf_conntrack_dccp.h>
#include <linux/netfilter/nf_conntrack_sctp.h>
#include <linux/netfilter/nf_conntrack_proto_gre.h>

#include <net/netfilter/nf_conntrack_tuple.h>

struct nf_ct_udp {
	unsigned long	stream_ts;
};

/* per conntrack: protocol private data */
union nf_conntrack_proto {
	/* insert conntrack proto private data here */
	struct nf_ct_dccp dccp;
	struct ip_ct_sctp sctp;
	struct ip_ct_tcp tcp;
	struct nf_ct_udp udp;
	struct nf_ct_gre gre;
	unsigned int tmpl_padto;
};

union nf_conntrack_expect_proto {
	/* insert expect proto private data here */
};

struct nf_conntrack_net {
	unsigned int users4;
	unsigned int users6;
	unsigned int users_bridge;
};

#include <linux/types.h>
#include <linux/skbuff.h>

#include <net/netfilter/ipv4/nf_conntrack_ipv4.h>
#include <net/netfilter/ipv6/nf_conntrack_ipv6.h>

struct nf_conn {
	/* Usage count in here is 1 for hash table, 1 per skb,
	 * plus 1 for any connection(s) we are `master' for
	 *
	 * Hint, SKB address this struct and refcnt via skb->_nfct and
	 * helpers nf_conntrack_get() and nf_conntrack_put().
	 * Helper nf_ct_put() equals nf_conntrack_put() by dec refcnt,
	 * beware nf_ct_get() is different and don't inc refcnt.
	 */
	struct nf_conntrack ct_general;

	spinlock_t	lock;
	/* jiffies32 when this ct is considered dead */
	u32 timeout;

#ifdef CONFIG_NF_CONNTRACK_ZONES
	struct nf_conntrack_zone zone;
#endif
	/* XXX should I move this to the tail ? - Y.K */
	/* These are my tuples; original and reply */
	struct nf_conntrack_tuple_hash tuplehash[IP_CT_DIR_MAX];

	/* Have we seen traffic both ways yet? (bitset) */
	unsigned long status;

	u16		cpu;
	possible_net_t ct_net;

#if IS_ENABLED(CONFIG_NF_NAT)
	struct hlist_node	nat_bysource;
#endif
	/* all members below initialized via memset */
	struct { } __nfct_init_offset;

	/* If we were expected by an expectation, this will be it */
	struct nf_conn *master;

#if defined(CONFIG_NF_CONNTRACK_MARK)
	u_int32_t mark;
#endif

#ifdef CONFIG_NF_CONNTRACK_SECMARK
	u_int32_t secmark;
#endif

	/* Extensions */
	struct nf_ct_ext *ext;

	/* Storage reserved for other modules, must be the last member */
	union nf_conntrack_proto proto;

	ANDROID_KABI_RESERVE(1);
	ANDROID_KABI_RESERVE(2);

	ANDROID_OEM_DATA(1);
};

static inline struct nf_conn *
nf_ct_tuplehash_to_ctrack(const struct nf_conntrack_tuple_hash *hash)
{
	return container_of(hash, struct nf_conn,
			    tuplehash[hash->tuple.dst.dir]);
}

static inline u_int16_t nf_ct_l3num(const struct nf_conn *ct)
{
	return ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.l3num;
}

static inline u_int8_t nf_ct_protonum(const struct nf_conn *ct)
{
	return ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.protonum;
}

#define nf_ct_tuple(ct, dir) (&(ct)->tuplehash[dir].tuple)

/* get master conntrack via master expectation */
#define master_ct(conntr) (conntr->master)

extern struct net init_net;

static inline struct net *nf_ct_net(const struct nf_conn *ct)
{
	return read_pnet(&ct->ct_net);
}

/* Alter reply tuple (maybe alter helper). */
void nf_conntrack_alter_reply(struct nf_conn *ct,
			      const struct nf_conntrack_tuple *newreply);

/* Is this tuple taken? (ignoring any belonging to the given
   conntrack). */
int nf_conntrack_tuple_taken(const struct nf_conntrack_tuple *tuple,
			     const struct nf_conn *ignored_conntrack);

/* Return conntrack_info and tuple hash for given skb. */
static inline struct nf_conn *
nf_ct_get(const struct sk_buff *skb, enum ip_conntrack_info *ctinfo)
{
	unsigned long nfct = skb_get_nfct(skb);

	*ctinfo = nfct & NFCT_INFOMASK;
	return (struct nf_conn *)(nfct & NFCT_PTRMASK);
}

/* decrement reference count on a conntrack */
static inline void nf_ct_put(struct nf_conn *ct)
{
	WARN_ON(!ct);
	nf_conntrack_put(&ct->ct_general);
}

/* Protocol module loading */
int nf_ct_l3proto_try_module_get(unsigned short l3proto);
void nf_ct_l3proto_module_put(unsigned short l3proto);

/* load module; enable/disable conntrack in this namespace */
int nf_ct_netns_get(struct net *net, u8 nfproto);
void nf_ct_netns_put(struct net *net, u8 nfproto);

/*
 * Allocate a hashtable of hlist_head (if nulls == 0),
 * or hlist_nulls_head (if nulls == 1)
 */
void *nf_ct_alloc_hashtable(unsigned int *sizep, int nulls);

int nf_conntrack_hash_check_insert(struct nf_conn *ct);
bool nf_ct_delete(struct nf_conn *ct, u32 pid, int report);

bool nf_ct_get_tuplepr(const struct sk_buff *skb, unsigned int nhoff,
		       u_int16_t l3num, struct net *net,
		       struct nf_conntrack_tuple *tuple);

void __nf_ct_refresh_acct(struct nf_conn *ct, enum ip_conntrack_info ctinfo,
			  const struct sk_buff *skb,
			  u32 extra_jiffies, bool do_acct);

/* Refresh conntrack for this many jiffies and do accounting */
static inline void nf_ct_refresh_acct(struct nf_conn *ct,
				      enum ip_conntrack_info ctinfo,
				      const struct sk_buff *skb,
				      u32 extra_jiffies)
{
	__nf_ct_refresh_acct(ct, ctinfo, skb, extra_jiffies, true);
}

/* Refresh conntrack for this many jiffies */
static inline void nf_ct_refresh(struct nf_conn *ct,
				 const struct sk_buff *skb,
				 u32 extra_jiffies)
{
	__nf_ct_refresh_acct(ct, 0, skb, extra_jiffies, false);
}

/* kill conntrack and do accounting */
bool nf_ct_kill_acct(struct nf_conn *ct, enum ip_conntrack_info ctinfo,
		     const struct sk_buff *skb);

/* kill conntrack without accounting */
static inline bool nf_ct_kill(struct nf_conn *ct)
{
	return nf_ct_delete(ct, 0, 0);
}

/* Set all unconfirmed conntrack as dying */
void nf_ct_unconfirmed_destroy(struct net *);

/* Iterate over all conntracks: if iter returns true, it's deleted. */
void nf_ct_iterate_cleanup_net(struct net *net,
			       int (*iter)(struct nf_conn *i, void *data),
			       void *data, u32 portid, int report);

/* also set unconfirmed conntracks as dying. Only use in module exit path. */
void nf_ct_iterate_destroy(int (*iter)(struct nf_conn *i, void *data),
			   void *data);

struct nf_conntrack_zone;

void nf_conntrack_free(struct nf_conn *ct);
struct nf_conn *nf_conntrack_alloc(struct net *net,
				   const struct nf_conntrack_zone *zone,
				   const struct nf_conntrack_tuple *orig,
				   const struct nf_conntrack_tuple *repl,
				   gfp_t gfp);

static inline int nf_ct_is_template(const struct nf_conn *ct)
{
	return test_bit(IPS_TEMPLATE_BIT, &ct->status);
}

/* It's confirmed if it is, or has been in the hash table. */
static inline int nf_ct_is_confirmed(const struct nf_conn *ct)
{
	return test_bit(IPS_CONFIRMED_BIT, &ct->status);
}

static inline int nf_ct_is_dying(const struct nf_conn *ct)
{
	return test_bit(IPS_DYING_BIT, &ct->status);
}

/* Packet is received from loopback */
static inline bool nf_is_loopback_packet(const struct sk_buff *skb)
{
	return skb->dev && skb->skb_iif && skb->dev->flags & IFF_LOOPBACK;
}

#define nfct_time_stamp ((u32)(jiffies))

/* jiffies until ct expires, 0 if already expired */
static inline unsigned long nf_ct_expires(const struct nf_conn *ct)
{
	s32 timeout = ct->timeout - nfct_time_stamp;

	return timeout > 0 ? timeout : 0;
}

static inline bool nf_ct_is_expired(const struct nf_conn *ct)
{
	return (__s32)(ct->timeout - nfct_time_stamp) <= 0;
}

/* use after obtaining a reference count */
static inline bool nf_ct_should_gc(const struct nf_conn *ct)
{
	return nf_ct_is_expired(ct) && nf_ct_is_confirmed(ct) &&
	       !nf_ct_is_dying(ct);
}

#define	NF_CT_DAY	(86400 * HZ)

/* Set an arbitrary timeout large enough not to ever expire, this save
 * us a check for the IPS_OFFLOAD_BIT from the packet path via
 * nf_ct_is_expired().
 */
static inline void nf_ct_offload_timeout(struct nf_conn *ct)
{
	if (nf_ct_expires(ct) < NF_CT_DAY / 2)
		ct->timeout = nfct_time_stamp + NF_CT_DAY;
}

struct kernel_param;

int nf_conntrack_set_hashsize(const char *val, const struct kernel_param *kp);
int nf_conntrack_hash_resize(unsigned int hashsize);

extern struct hlist_nulls_head *nf_conntrack_hash;
extern unsigned int nf_conntrack_htable_size;
extern seqcount_spinlock_t nf_conntrack_generation;
extern unsigned int nf_conntrack_max;

/* must be called with rcu read lock held */
static inline void
nf_conntrack_get_ht(struct hlist_nulls_head **hash, unsigned int *hsize)
{
	struct hlist_nulls_head *hptr;
	unsigned int sequence, hsz;

	do {
		sequence = read_seqcount_begin(&nf_conntrack_generation);
		hsz = nf_conntrack_htable_size;
		hptr = nf_conntrack_hash;
	} while (read_seqcount_retry(&nf_conntrack_generation, sequence));

	*hash = hptr;
	*hsize = hsz;
}

struct nf_conn *nf_ct_tmpl_alloc(struct net *net,
				 const struct nf_conntrack_zone *zone,
				 gfp_t flags);
void nf_ct_tmpl_free(struct nf_conn *tmpl);

u32 nf_ct_get_id(const struct nf_conn *ct);

static inline void
nf_ct_set(struct sk_buff *skb, struct nf_conn *ct, enum ip_conntrack_info info)
{
	skb_set_nfct(skb, (unsigned long)ct | info);
}

#define NF_CT_STAT_INC(net, count)	  __this_cpu_inc((net)->ct.stat->count)
#define NF_CT_STAT_INC_ATOMIC(net, count) this_cpu_inc((net)->ct.stat->count)
#define NF_CT_STAT_ADD_ATOMIC(net, count, v) this_cpu_add((net)->ct.stat->count, (v))

#define MODULE_ALIAS_NFCT_HELPER(helper) \
        MODULE_ALIAS("nfct-helper-" helper)

#endif /* _NF_CONNTRACK_H */
