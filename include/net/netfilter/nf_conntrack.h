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

#include <linux/netfilter/nf_conntrack_common.h>

#ifdef __KERNEL__
#include <linux/bitops.h>
#include <linux/compiler.h>
#include <asm/atomic.h>

#include <linux/netfilter/nf_conntrack_tcp.h>
#include <linux/netfilter/nf_conntrack_dccp.h>
#include <linux/netfilter/nf_conntrack_sctp.h>
#include <linux/netfilter/nf_conntrack_proto_gre.h>
#include <net/netfilter/ipv4/nf_conntrack_icmp.h>
#include <net/netfilter/ipv6/nf_conntrack_icmpv6.h>

#include <net/netfilter/nf_conntrack_tuple.h>

/* per conntrack: protocol private data */
union nf_conntrack_proto {
	/* insert conntrack proto private data here */
	struct nf_ct_dccp dccp;
	struct ip_ct_sctp sctp;
	struct ip_ct_tcp tcp;
	struct ip_ct_icmp icmp;
	struct nf_ct_icmpv6 icmpv6;
	struct nf_ct_gre gre;
};

union nf_conntrack_expect_proto {
	/* insert expect proto private data here */
};

/* Add protocol helper include file here */
#include <linux/netfilter/nf_conntrack_ftp.h>
#include <linux/netfilter/nf_conntrack_pptp.h>
#include <linux/netfilter/nf_conntrack_h323.h>
#include <linux/netfilter/nf_conntrack_sane.h>
#include <linux/netfilter/nf_conntrack_sip.h>

/* per conntrack: application helper private data */
union nf_conntrack_help {
	/* insert conntrack helper private data (master) here */
	struct nf_ct_ftp_master ct_ftp_info;
	struct nf_ct_pptp_master ct_pptp_info;
	struct nf_ct_h323_master ct_h323_info;
	struct nf_ct_sane_master ct_sane_info;
	struct nf_ct_sip_master ct_sip_info;
};

#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/timer.h>

#ifdef CONFIG_NETFILTER_DEBUG
#define NF_CT_ASSERT(x)		WARN_ON(!(x))
#else
#define NF_CT_ASSERT(x)
#endif

struct nf_conntrack_helper;

/* Must be kept in sync with the classes defined by helpers */
#define NF_CT_MAX_EXPECT_CLASSES	3

/* nf_conn feature for connections that have a helper */
struct nf_conn_help {
	/* Helper. if any */
	struct nf_conntrack_helper *helper;

	union nf_conntrack_help help;

	struct hlist_head expectations;

	/* Current number of expected connections */
	u8 expecting[NF_CT_MAX_EXPECT_CLASSES];
};

#include <net/netfilter/ipv4/nf_conntrack_ipv4.h>
#include <net/netfilter/ipv6/nf_conntrack_ipv6.h>

struct nf_conn
{
	/* Usage count in here is 1 for hash table/destruct timer, 1 per skb,
           plus 1 for any connection(s) we are `master' for */
	struct nf_conntrack ct_general;

	/* XXX should I move this to the tail ? - Y.K */
	/* These are my tuples; original and reply */
	struct nf_conntrack_tuple_hash tuplehash[IP_CT_DIR_MAX];

	/* Have we seen traffic both ways yet? (bitset) */
	unsigned long status;

	/* If we were expected by an expectation, this will be it */
	struct nf_conn *master;

	/* Timer function; drops refcnt when it goes off. */
	struct timer_list timeout;

#if defined(CONFIG_NF_CONNTRACK_MARK)
	u_int32_t mark;
#endif

#ifdef CONFIG_NF_CONNTRACK_SECMARK
	u_int32_t secmark;
#endif

	/* Storage reserved for other modules: */
	union nf_conntrack_proto proto;

	/* Extensions */
	struct nf_ct_ext *ext;
#ifdef CONFIG_NET_NS
	struct net *ct_net;
#endif
	struct rcu_head rcu;
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

/* get master conntrack via master expectation */
#define master_ct(conntr) (conntr->master)

extern struct net init_net;

static inline struct net *nf_ct_net(const struct nf_conn *ct)
{
#ifdef CONFIG_NET_NS
	return ct->ct_net;
#else
	return &init_net;
#endif
}

/* Alter reply tuple (maybe alter helper). */
extern void
nf_conntrack_alter_reply(struct nf_conn *ct,
			 const struct nf_conntrack_tuple *newreply);

/* Is this tuple taken? (ignoring any belonging to the given
   conntrack). */
extern int
nf_conntrack_tuple_taken(const struct nf_conntrack_tuple *tuple,
			 const struct nf_conn *ignored_conntrack);

/* Return conntrack_info and tuple hash for given skb. */
static inline struct nf_conn *
nf_ct_get(const struct sk_buff *skb, enum ip_conntrack_info *ctinfo)
{
	*ctinfo = skb->nfctinfo;
	return (struct nf_conn *)skb->nfct;
}

/* decrement reference count on a conntrack */
static inline void nf_ct_put(struct nf_conn *ct)
{
	NF_CT_ASSERT(ct);
	nf_conntrack_put(&ct->ct_general);
}

/* Protocol module loading */
extern int nf_ct_l3proto_try_module_get(unsigned short l3proto);
extern void nf_ct_l3proto_module_put(unsigned short l3proto);

extern struct hlist_head *nf_ct_alloc_hashtable(unsigned int *sizep, int *vmalloced);
extern void nf_ct_free_hashtable(struct hlist_head *hash, int vmalloced,
				 unsigned int size);

extern struct nf_conntrack_tuple_hash *
__nf_conntrack_find(struct net *net, const struct nf_conntrack_tuple *tuple);

extern void nf_conntrack_hash_insert(struct nf_conn *ct);

extern void nf_conntrack_flush(struct net *net);

extern bool nf_ct_get_tuplepr(const struct sk_buff *skb,
			      unsigned int nhoff, u_int16_t l3num,
			      struct nf_conntrack_tuple *tuple);
extern bool nf_ct_invert_tuplepr(struct nf_conntrack_tuple *inverse,
				 const struct nf_conntrack_tuple *orig);

extern void __nf_ct_refresh_acct(struct nf_conn *ct,
				 enum ip_conntrack_info ctinfo,
				 const struct sk_buff *skb,
				 unsigned long extra_jiffies,
				 int do_acct);

/* Refresh conntrack for this many jiffies and do accounting */
static inline void nf_ct_refresh_acct(struct nf_conn *ct,
				      enum ip_conntrack_info ctinfo,
				      const struct sk_buff *skb,
				      unsigned long extra_jiffies)
{
	__nf_ct_refresh_acct(ct, ctinfo, skb, extra_jiffies, 1);
}

/* Refresh conntrack for this many jiffies */
static inline void nf_ct_refresh(struct nf_conn *ct,
				 const struct sk_buff *skb,
				 unsigned long extra_jiffies)
{
	__nf_ct_refresh_acct(ct, 0, skb, extra_jiffies, 0);
}

extern bool __nf_ct_kill_acct(struct nf_conn *ct,
			      enum ip_conntrack_info ctinfo,
			      const struct sk_buff *skb,
			      int do_acct);

/* kill conntrack and do accounting */
static inline bool nf_ct_kill_acct(struct nf_conn *ct,
				   enum ip_conntrack_info ctinfo,
				   const struct sk_buff *skb)
{
	return __nf_ct_kill_acct(ct, ctinfo, skb, 1);
}

/* kill conntrack without accounting */
static inline bool nf_ct_kill(struct nf_conn *ct)
{
	return __nf_ct_kill_acct(ct, 0, NULL, 0);
}

/* These are for NAT.  Icky. */
/* Update TCP window tracking data when NAT mangles the packet */
extern void nf_conntrack_tcp_update(const struct sk_buff *skb,
				    unsigned int dataoff,
				    struct nf_conn *ct,
				    int dir);

/* Fake conntrack entry for untracked connections */
extern struct nf_conn nf_conntrack_untracked;

/* Iterate over all conntracks: if iter returns true, it's deleted. */
extern void
nf_ct_iterate_cleanup(struct net *net, int (*iter)(struct nf_conn *i, void *data), void *data);
extern void nf_conntrack_free(struct nf_conn *ct);
extern struct nf_conn *
nf_conntrack_alloc(struct net *net,
		   const struct nf_conntrack_tuple *orig,
		   const struct nf_conntrack_tuple *repl,
		   gfp_t gfp);

/* It's confirmed if it is, or has been in the hash table. */
static inline int nf_ct_is_confirmed(struct nf_conn *ct)
{
	return test_bit(IPS_CONFIRMED_BIT, &ct->status);
}

static inline int nf_ct_is_dying(struct nf_conn *ct)
{
	return test_bit(IPS_DYING_BIT, &ct->status);
}

static inline int nf_ct_is_untracked(const struct sk_buff *skb)
{
	return (skb->nfct == &nf_conntrack_untracked.ct_general);
}

extern int nf_conntrack_set_hashsize(const char *val, struct kernel_param *kp);
extern unsigned int nf_conntrack_htable_size;
extern int nf_conntrack_checksum;
extern int nf_conntrack_max;

#define NF_CT_STAT_INC(net, count)	\
	(per_cpu_ptr((net)->ct.stat, raw_smp_processor_id())->count++)
#define NF_CT_STAT_INC_ATOMIC(net, count)		\
do {							\
	local_bh_disable();				\
	per_cpu_ptr((net)->ct.stat, raw_smp_processor_id())->count++;	\
	local_bh_enable();				\
} while (0)

#endif /* __KERNEL__ */
#endif /* _NF_CONNTRACK_H */
