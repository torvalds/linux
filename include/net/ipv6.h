/*
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _NET_IPV6_H
#define _NET_IPV6_H

#include <linux/ipv6.h>
#include <linux/hardirq.h>
#include <linux/jhash.h>
#include <linux/refcount.h>
#include <net/if_inet6.h>
#include <net/ndisc.h>
#include <net/flow.h>
#include <net/flow_dissector.h>
#include <net/snmp.h>

#define SIN6_LEN_RFC2133	24

#define IPV6_MAXPLEN		65535

/*
 *	NextHeader field of IPv6 header
 */

#define NEXTHDR_HOP		0	/* Hop-by-hop option header. */
#define NEXTHDR_TCP		6	/* TCP segment. */
#define NEXTHDR_UDP		17	/* UDP message. */
#define NEXTHDR_IPV6		41	/* IPv6 in IPv6 */
#define NEXTHDR_ROUTING		43	/* Routing header. */
#define NEXTHDR_FRAGMENT	44	/* Fragmentation/reassembly header. */
#define NEXTHDR_GRE		47	/* GRE header. */
#define NEXTHDR_ESP		50	/* Encapsulating security payload. */
#define NEXTHDR_AUTH		51	/* Authentication header. */
#define NEXTHDR_ICMP		58	/* ICMP for IPv6. */
#define NEXTHDR_NONE		59	/* No next header */
#define NEXTHDR_DEST		60	/* Destination options header. */
#define NEXTHDR_SCTP		132	/* SCTP message. */
#define NEXTHDR_MOBILITY	135	/* Mobility header. */

#define NEXTHDR_MAX		255

#define IPV6_DEFAULT_HOPLIMIT   64
#define IPV6_DEFAULT_MCASTHOPS	1

/*
 *	Addr type
 *	
 *	type	-	unicast | multicast
 *	scope	-	local	| site	    | global
 *	v4	-	compat
 *	v4mapped
 *	any
 *	loopback
 */

#define IPV6_ADDR_ANY		0x0000U

#define IPV6_ADDR_UNICAST      	0x0001U	
#define IPV6_ADDR_MULTICAST    	0x0002U	

#define IPV6_ADDR_LOOPBACK	0x0010U
#define IPV6_ADDR_LINKLOCAL	0x0020U
#define IPV6_ADDR_SITELOCAL	0x0040U

#define IPV6_ADDR_COMPATv4	0x0080U

#define IPV6_ADDR_SCOPE_MASK	0x00f0U

#define IPV6_ADDR_MAPPED	0x1000U

/*
 *	Addr scopes
 */
#define IPV6_ADDR_MC_SCOPE(a)	\
	((a)->s6_addr[1] & 0x0f)	/* nonstandard */
#define __IPV6_ADDR_SCOPE_INVALID	-1
#define IPV6_ADDR_SCOPE_NODELOCAL	0x01
#define IPV6_ADDR_SCOPE_LINKLOCAL	0x02
#define IPV6_ADDR_SCOPE_SITELOCAL	0x05
#define IPV6_ADDR_SCOPE_ORGLOCAL	0x08
#define IPV6_ADDR_SCOPE_GLOBAL		0x0e

/*
 *	Addr flags
 */
#define IPV6_ADDR_MC_FLAG_TRANSIENT(a)	\
	((a)->s6_addr[1] & 0x10)
#define IPV6_ADDR_MC_FLAG_PREFIX(a)	\
	((a)->s6_addr[1] & 0x20)
#define IPV6_ADDR_MC_FLAG_RENDEZVOUS(a)	\
	((a)->s6_addr[1] & 0x40)

/*
 *	fragmentation header
 */

struct frag_hdr {
	__u8	nexthdr;
	__u8	reserved;
	__be16	frag_off;
	__be32	identification;
};

#define	IP6_MF		0x0001
#define	IP6_OFFSET	0xFFF8

#define IP6_REPLY_MARK(net, mark) \
	((net)->ipv6.sysctl.fwmark_reflect ? (mark) : 0)

#include <net/sock.h>

/* sysctls */
extern int sysctl_mld_max_msf;
extern int sysctl_mld_qrv;

#define _DEVINC(net, statname, mod, idev, field)			\
({									\
	struct inet6_dev *_idev = (idev);				\
	if (likely(_idev != NULL))					\
		mod##SNMP_INC_STATS64((_idev)->stats.statname, (field));\
	mod##SNMP_INC_STATS64((net)->mib.statname##_statistics, (field));\
})

/* per device counters are atomic_long_t */
#define _DEVINCATOMIC(net, statname, mod, idev, field)			\
({									\
	struct inet6_dev *_idev = (idev);				\
	if (likely(_idev != NULL))					\
		SNMP_INC_STATS_ATOMIC_LONG((_idev)->stats.statname##dev, (field)); \
	mod##SNMP_INC_STATS((net)->mib.statname##_statistics, (field));\
})

/* per device and per net counters are atomic_long_t */
#define _DEVINC_ATOMIC_ATOMIC(net, statname, idev, field)		\
({									\
	struct inet6_dev *_idev = (idev);				\
	if (likely(_idev != NULL))					\
		SNMP_INC_STATS_ATOMIC_LONG((_idev)->stats.statname##dev, (field)); \
	SNMP_INC_STATS_ATOMIC_LONG((net)->mib.statname##_statistics, (field));\
})

#define _DEVADD(net, statname, mod, idev, field, val)			\
({									\
	struct inet6_dev *_idev = (idev);				\
	if (likely(_idev != NULL))					\
		mod##SNMP_ADD_STATS((_idev)->stats.statname, (field), (val)); \
	mod##SNMP_ADD_STATS((net)->mib.statname##_statistics, (field), (val));\
})

#define _DEVUPD(net, statname, mod, idev, field, val)			\
({									\
	struct inet6_dev *_idev = (idev);				\
	if (likely(_idev != NULL))					\
		mod##SNMP_UPD_PO_STATS((_idev)->stats.statname, field, (val)); \
	mod##SNMP_UPD_PO_STATS((net)->mib.statname##_statistics, field, (val));\
})

/* MIBs */

#define IP6_INC_STATS(net, idev,field)		\
		_DEVINC(net, ipv6, , idev, field)
#define __IP6_INC_STATS(net, idev,field)	\
		_DEVINC(net, ipv6, __, idev, field)
#define IP6_ADD_STATS(net, idev,field,val)	\
		_DEVADD(net, ipv6, , idev, field, val)
#define __IP6_ADD_STATS(net, idev,field,val)	\
		_DEVADD(net, ipv6, __, idev, field, val)
#define IP6_UPD_PO_STATS(net, idev,field,val)   \
		_DEVUPD(net, ipv6, , idev, field, val)
#define __IP6_UPD_PO_STATS(net, idev,field,val)   \
		_DEVUPD(net, ipv6, __, idev, field, val)
#define ICMP6_INC_STATS(net, idev, field)	\
		_DEVINCATOMIC(net, icmpv6, , idev, field)
#define __ICMP6_INC_STATS(net, idev, field)	\
		_DEVINCATOMIC(net, icmpv6, __, idev, field)

#define ICMP6MSGOUT_INC_STATS(net, idev, field)		\
	_DEVINC_ATOMIC_ATOMIC(net, icmpv6msg, idev, field +256)
#define ICMP6MSGIN_INC_STATS(net, idev, field)	\
	_DEVINC_ATOMIC_ATOMIC(net, icmpv6msg, idev, field)

struct ip6_ra_chain {
	struct ip6_ra_chain	*next;
	struct sock		*sk;
	int			sel;
	void			(*destructor)(struct sock *);
};

extern struct ip6_ra_chain	*ip6_ra_chain;
extern rwlock_t ip6_ra_lock;

/*
   This structure is prepared by protocol, when parsing
   ancillary data and passed to IPv6.
 */

struct ipv6_txoptions {
	refcount_t		refcnt;
	/* Length of this structure */
	int			tot_len;

	/* length of extension headers   */

	__u16			opt_flen;	/* after fragment hdr */
	__u16			opt_nflen;	/* before fragment hdr */

	struct ipv6_opt_hdr	*hopopt;
	struct ipv6_opt_hdr	*dst0opt;
	struct ipv6_rt_hdr	*srcrt;	/* Routing Header */
	struct ipv6_opt_hdr	*dst1opt;
	struct rcu_head		rcu;
	/* Option buffer, as read by IPV6_PKTOPTIONS, starts here. */
};

struct ip6_flowlabel {
	struct ip6_flowlabel __rcu *next;
	__be32			label;
	atomic_t		users;
	struct in6_addr		dst;
	struct ipv6_txoptions	*opt;
	unsigned long		linger;
	struct rcu_head		rcu;
	u8			share;
	union {
		struct pid *pid;
		kuid_t uid;
	} owner;
	unsigned long		lastuse;
	unsigned long		expires;
	struct net		*fl_net;
};

#define IPV6_FLOWINFO_MASK		cpu_to_be32(0x0FFFFFFF)
#define IPV6_FLOWLABEL_MASK		cpu_to_be32(0x000FFFFF)
#define IPV6_FLOWLABEL_STATELESS_FLAG	cpu_to_be32(0x00080000)

#define IPV6_TCLASS_MASK (IPV6_FLOWINFO_MASK & ~IPV6_FLOWLABEL_MASK)
#define IPV6_TCLASS_SHIFT	20

struct ipv6_fl_socklist {
	struct ipv6_fl_socklist	__rcu	*next;
	struct ip6_flowlabel		*fl;
	struct rcu_head			rcu;
};

struct ipcm6_cookie {
	__s16 hlimit;
	__s16 tclass;
	__s8  dontfrag;
	struct ipv6_txoptions *opt;
};

static inline struct ipv6_txoptions *txopt_get(const struct ipv6_pinfo *np)
{
	struct ipv6_txoptions *opt;

	rcu_read_lock();
	opt = rcu_dereference(np->opt);
	if (opt) {
		if (!refcount_inc_not_zero(&opt->refcnt))
			opt = NULL;
		else
			opt = rcu_pointer_handoff(opt);
	}
	rcu_read_unlock();
	return opt;
}

static inline void txopt_put(struct ipv6_txoptions *opt)
{
	if (opt && refcount_dec_and_test(&opt->refcnt))
		kfree_rcu(opt, rcu);
}

struct ip6_flowlabel *fl6_sock_lookup(struct sock *sk, __be32 label);
struct ipv6_txoptions *fl6_merge_options(struct ipv6_txoptions *opt_space,
					 struct ip6_flowlabel *fl,
					 struct ipv6_txoptions *fopt);
void fl6_free_socklist(struct sock *sk);
int ipv6_flowlabel_opt(struct sock *sk, char __user *optval, int optlen);
int ipv6_flowlabel_opt_get(struct sock *sk, struct in6_flowlabel_req *freq,
			   int flags);
int ip6_flowlabel_init(void);
void ip6_flowlabel_cleanup(void);

static inline void fl6_sock_release(struct ip6_flowlabel *fl)
{
	if (fl)
		atomic_dec(&fl->users);
}

void icmpv6_notify(struct sk_buff *skb, u8 type, u8 code, __be32 info);

int icmpv6_push_pending_frames(struct sock *sk, struct flowi6 *fl6,
			       struct icmp6hdr *thdr, int len);

int ip6_ra_control(struct sock *sk, int sel);

int ipv6_parse_hopopts(struct sk_buff *skb);

struct ipv6_txoptions *ipv6_dup_options(struct sock *sk,
					struct ipv6_txoptions *opt);
struct ipv6_txoptions *ipv6_renew_options(struct sock *sk,
					  struct ipv6_txoptions *opt,
					  int newtype,
					  struct ipv6_opt_hdr __user *newopt,
					  int newoptlen);
struct ipv6_txoptions *
ipv6_renew_options_kern(struct sock *sk,
			struct ipv6_txoptions *opt,
			int newtype,
			struct ipv6_opt_hdr *newopt,
			int newoptlen);
struct ipv6_txoptions *ipv6_fixup_options(struct ipv6_txoptions *opt_space,
					  struct ipv6_txoptions *opt);

bool ipv6_opt_accepted(const struct sock *sk, const struct sk_buff *skb,
		       const struct inet6_skb_parm *opt);
struct ipv6_txoptions *ipv6_update_options(struct sock *sk,
					   struct ipv6_txoptions *opt);

static inline bool ipv6_accept_ra(struct inet6_dev *idev)
{
	/* If forwarding is enabled, RA are not accepted unless the special
	 * hybrid mode (accept_ra=2) is enabled.
	 */
	return idev->cnf.forwarding ? idev->cnf.accept_ra == 2 :
	    idev->cnf.accept_ra;
}

#if IS_ENABLED(CONFIG_IPV6)
static inline int ip6_frag_mem(struct net *net)
{
	return sum_frag_mem_limit(&net->ipv6.frags);
}
#endif

#define IPV6_FRAG_HIGH_THRESH	(4 * 1024*1024)	/* 4194304 */
#define IPV6_FRAG_LOW_THRESH	(3 * 1024*1024)	/* 3145728 */
#define IPV6_FRAG_TIMEOUT	(60 * HZ)	/* 60 seconds */

int __ipv6_addr_type(const struct in6_addr *addr);
static inline int ipv6_addr_type(const struct in6_addr *addr)
{
	return __ipv6_addr_type(addr) & 0xffff;
}

static inline int ipv6_addr_scope(const struct in6_addr *addr)
{
	return __ipv6_addr_type(addr) & IPV6_ADDR_SCOPE_MASK;
}

static inline int __ipv6_addr_src_scope(int type)
{
	return (type == IPV6_ADDR_ANY) ? __IPV6_ADDR_SCOPE_INVALID : (type >> 16);
}

static inline int ipv6_addr_src_scope(const struct in6_addr *addr)
{
	return __ipv6_addr_src_scope(__ipv6_addr_type(addr));
}

static inline bool __ipv6_addr_needs_scope_id(int type)
{
	return type & IPV6_ADDR_LINKLOCAL ||
	       (type & IPV6_ADDR_MULTICAST &&
		(type & (IPV6_ADDR_LOOPBACK|IPV6_ADDR_LINKLOCAL)));
}

static inline __u32 ipv6_iface_scope_id(const struct in6_addr *addr, int iface)
{
	return __ipv6_addr_needs_scope_id(__ipv6_addr_type(addr)) ? iface : 0;
}

static inline int ipv6_addr_cmp(const struct in6_addr *a1, const struct in6_addr *a2)
{
	return memcmp(a1, a2, sizeof(struct in6_addr));
}

static inline bool
ipv6_masked_addr_cmp(const struct in6_addr *a1, const struct in6_addr *m,
		     const struct in6_addr *a2)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) && BITS_PER_LONG == 64
	const unsigned long *ul1 = (const unsigned long *)a1;
	const unsigned long *ulm = (const unsigned long *)m;
	const unsigned long *ul2 = (const unsigned long *)a2;

	return !!(((ul1[0] ^ ul2[0]) & ulm[0]) |
		  ((ul1[1] ^ ul2[1]) & ulm[1]));
#else
	return !!(((a1->s6_addr32[0] ^ a2->s6_addr32[0]) & m->s6_addr32[0]) |
		  ((a1->s6_addr32[1] ^ a2->s6_addr32[1]) & m->s6_addr32[1]) |
		  ((a1->s6_addr32[2] ^ a2->s6_addr32[2]) & m->s6_addr32[2]) |
		  ((a1->s6_addr32[3] ^ a2->s6_addr32[3]) & m->s6_addr32[3]));
#endif
}

static inline void ipv6_addr_prefix(struct in6_addr *pfx, 
				    const struct in6_addr *addr,
				    int plen)
{
	/* caller must guarantee 0 <= plen <= 128 */
	int o = plen >> 3,
	    b = plen & 0x7;

	memset(pfx->s6_addr, 0, sizeof(pfx->s6_addr));
	memcpy(pfx->s6_addr, addr, o);
	if (b != 0)
		pfx->s6_addr[o] = addr->s6_addr[o] & (0xff00 >> b);
}

static inline void ipv6_addr_prefix_copy(struct in6_addr *addr,
					 const struct in6_addr *pfx,
					 int plen)
{
	/* caller must guarantee 0 <= plen <= 128 */
	int o = plen >> 3,
	    b = plen & 0x7;

	memcpy(addr->s6_addr, pfx, o);
	if (b != 0) {
		addr->s6_addr[o] &= ~(0xff00 >> b);
		addr->s6_addr[o] |= (pfx->s6_addr[o] & (0xff00 >> b));
	}
}

static inline void __ipv6_addr_set_half(__be32 *addr,
					__be32 wh, __be32 wl)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) && BITS_PER_LONG == 64
#if defined(__BIG_ENDIAN)
	if (__builtin_constant_p(wh) && __builtin_constant_p(wl)) {
		*(__force u64 *)addr = ((__force u64)(wh) << 32 | (__force u64)(wl));
		return;
	}
#elif defined(__LITTLE_ENDIAN)
	if (__builtin_constant_p(wl) && __builtin_constant_p(wh)) {
		*(__force u64 *)addr = ((__force u64)(wl) << 32 | (__force u64)(wh));
		return;
	}
#endif
#endif
	addr[0] = wh;
	addr[1] = wl;
}

static inline void ipv6_addr_set(struct in6_addr *addr, 
				     __be32 w1, __be32 w2,
				     __be32 w3, __be32 w4)
{
	__ipv6_addr_set_half(&addr->s6_addr32[0], w1, w2);
	__ipv6_addr_set_half(&addr->s6_addr32[2], w3, w4);
}

static inline bool ipv6_addr_equal(const struct in6_addr *a1,
				   const struct in6_addr *a2)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) && BITS_PER_LONG == 64
	const unsigned long *ul1 = (const unsigned long *)a1;
	const unsigned long *ul2 = (const unsigned long *)a2;

	return ((ul1[0] ^ ul2[0]) | (ul1[1] ^ ul2[1])) == 0UL;
#else
	return ((a1->s6_addr32[0] ^ a2->s6_addr32[0]) |
		(a1->s6_addr32[1] ^ a2->s6_addr32[1]) |
		(a1->s6_addr32[2] ^ a2->s6_addr32[2]) |
		(a1->s6_addr32[3] ^ a2->s6_addr32[3])) == 0;
#endif
}

#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) && BITS_PER_LONG == 64
static inline bool __ipv6_prefix_equal64_half(const __be64 *a1,
					      const __be64 *a2,
					      unsigned int len)
{
	if (len && ((*a1 ^ *a2) & cpu_to_be64((~0UL) << (64 - len))))
		return false;
	return true;
}

static inline bool ipv6_prefix_equal(const struct in6_addr *addr1,
				     const struct in6_addr *addr2,
				     unsigned int prefixlen)
{
	const __be64 *a1 = (const __be64 *)addr1;
	const __be64 *a2 = (const __be64 *)addr2;

	if (prefixlen >= 64) {
		if (a1[0] ^ a2[0])
			return false;
		return __ipv6_prefix_equal64_half(a1 + 1, a2 + 1, prefixlen - 64);
	}
	return __ipv6_prefix_equal64_half(a1, a2, prefixlen);
}
#else
static inline bool ipv6_prefix_equal(const struct in6_addr *addr1,
				     const struct in6_addr *addr2,
				     unsigned int prefixlen)
{
	const __be32 *a1 = addr1->s6_addr32;
	const __be32 *a2 = addr2->s6_addr32;
	unsigned int pdw, pbi;

	/* check complete u32 in prefix */
	pdw = prefixlen >> 5;
	if (pdw && memcmp(a1, a2, pdw << 2))
		return false;

	/* check incomplete u32 in prefix */
	pbi = prefixlen & 0x1f;
	if (pbi && ((a1[pdw] ^ a2[pdw]) & htonl((0xffffffff) << (32 - pbi))))
		return false;

	return true;
}
#endif

struct inet_frag_queue;

enum ip6_defrag_users {
	IP6_DEFRAG_LOCAL_DELIVER,
	IP6_DEFRAG_CONNTRACK_IN,
	__IP6_DEFRAG_CONNTRACK_IN	= IP6_DEFRAG_CONNTRACK_IN + USHRT_MAX,
	IP6_DEFRAG_CONNTRACK_OUT,
	__IP6_DEFRAG_CONNTRACK_OUT	= IP6_DEFRAG_CONNTRACK_OUT + USHRT_MAX,
	IP6_DEFRAG_CONNTRACK_BRIDGE_IN,
	__IP6_DEFRAG_CONNTRACK_BRIDGE_IN = IP6_DEFRAG_CONNTRACK_BRIDGE_IN + USHRT_MAX,
};

struct ip6_create_arg {
	__be32 id;
	u32 user;
	const struct in6_addr *src;
	const struct in6_addr *dst;
	int iif;
	u8 ecn;
};

void ip6_frag_init(struct inet_frag_queue *q, const void *a);
bool ip6_frag_match(const struct inet_frag_queue *q, const void *a);

/*
 *	Equivalent of ipv4 struct ip
 */
struct frag_queue {
	struct inet_frag_queue	q;

	__be32			id;		/* fragment id		*/
	u32			user;
	struct in6_addr		saddr;
	struct in6_addr		daddr;

	int			iif;
	unsigned int		csum;
	__u16			nhoffset;
	u8			ecn;
};

void ip6_expire_frag_queue(struct net *net, struct frag_queue *fq,
			   struct inet_frags *frags);

static inline bool ipv6_addr_any(const struct in6_addr *a)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) && BITS_PER_LONG == 64
	const unsigned long *ul = (const unsigned long *)a;

	return (ul[0] | ul[1]) == 0UL;
#else
	return (a->s6_addr32[0] | a->s6_addr32[1] |
		a->s6_addr32[2] | a->s6_addr32[3]) == 0;
#endif
}

static inline u32 ipv6_addr_hash(const struct in6_addr *a)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) && BITS_PER_LONG == 64
	const unsigned long *ul = (const unsigned long *)a;
	unsigned long x = ul[0] ^ ul[1];

	return (u32)(x ^ (x >> 32));
#else
	return (__force u32)(a->s6_addr32[0] ^ a->s6_addr32[1] ^
			     a->s6_addr32[2] ^ a->s6_addr32[3]);
#endif
}

/* more secured version of ipv6_addr_hash() */
static inline u32 __ipv6_addr_jhash(const struct in6_addr *a, const u32 initval)
{
	u32 v = (__force u32)a->s6_addr32[0] ^ (__force u32)a->s6_addr32[1];

	return jhash_3words(v,
			    (__force u32)a->s6_addr32[2],
			    (__force u32)a->s6_addr32[3],
			    initval);
}

static inline bool ipv6_addr_loopback(const struct in6_addr *a)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) && BITS_PER_LONG == 64
	const __be64 *be = (const __be64 *)a;

	return (be[0] | (be[1] ^ cpu_to_be64(1))) == 0UL;
#else
	return (a->s6_addr32[0] | a->s6_addr32[1] |
		a->s6_addr32[2] | (a->s6_addr32[3] ^ cpu_to_be32(1))) == 0;
#endif
}

/*
 * Note that we must __force cast these to unsigned long to make sparse happy,
 * since all of the endian-annotated types are fixed size regardless of arch.
 */
static inline bool ipv6_addr_v4mapped(const struct in6_addr *a)
{
	return (
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) && BITS_PER_LONG == 64
		*(unsigned long *)a |
#else
		(__force unsigned long)(a->s6_addr32[0] | a->s6_addr32[1]) |
#endif
		(__force unsigned long)(a->s6_addr32[2] ^
					cpu_to_be32(0x0000ffff))) == 0UL;
}

/*
 * Check for a RFC 4843 ORCHID address
 * (Overlay Routable Cryptographic Hash Identifiers)
 */
static inline bool ipv6_addr_orchid(const struct in6_addr *a)
{
	return (a->s6_addr32[0] & htonl(0xfffffff0)) == htonl(0x20010010);
}

static inline bool ipv6_addr_is_multicast(const struct in6_addr *addr)
{
	return (addr->s6_addr32[0] & htonl(0xFF000000)) == htonl(0xFF000000);
}

static inline void ipv6_addr_set_v4mapped(const __be32 addr,
					  struct in6_addr *v4mapped)
{
	ipv6_addr_set(v4mapped,
			0, 0,
			htonl(0x0000FFFF),
			addr);
}

/*
 * find the first different bit between two addresses
 * length of address must be a multiple of 32bits
 */
static inline int __ipv6_addr_diff32(const void *token1, const void *token2, int addrlen)
{
	const __be32 *a1 = token1, *a2 = token2;
	int i;

	addrlen >>= 2;

	for (i = 0; i < addrlen; i++) {
		__be32 xb = a1[i] ^ a2[i];
		if (xb)
			return i * 32 + 31 - __fls(ntohl(xb));
	}

	/*
	 *	we should *never* get to this point since that 
	 *	would mean the addrs are equal
	 *
	 *	However, we do get to it 8) And exacly, when
	 *	addresses are equal 8)
	 *
	 *	ip route add 1111::/128 via ...
	 *	ip route add 1111::/64 via ...
	 *	and we are here.
	 *
	 *	Ideally, this function should stop comparison
	 *	at prefix length. It does not, but it is still OK,
	 *	if returned value is greater than prefix length.
	 *					--ANK (980803)
	 */
	return addrlen << 5;
}

#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) && BITS_PER_LONG == 64
static inline int __ipv6_addr_diff64(const void *token1, const void *token2, int addrlen)
{
	const __be64 *a1 = token1, *a2 = token2;
	int i;

	addrlen >>= 3;

	for (i = 0; i < addrlen; i++) {
		__be64 xb = a1[i] ^ a2[i];
		if (xb)
			return i * 64 + 63 - __fls(be64_to_cpu(xb));
	}

	return addrlen << 6;
}
#endif

static inline int __ipv6_addr_diff(const void *token1, const void *token2, int addrlen)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) && BITS_PER_LONG == 64
	if (__builtin_constant_p(addrlen) && !(addrlen & 7))
		return __ipv6_addr_diff64(token1, token2, addrlen);
#endif
	return __ipv6_addr_diff32(token1, token2, addrlen);
}

static inline int ipv6_addr_diff(const struct in6_addr *a1, const struct in6_addr *a2)
{
	return __ipv6_addr_diff(a1, a2, sizeof(struct in6_addr));
}

__be32 ipv6_select_ident(struct net *net,
			 const struct in6_addr *daddr,
			 const struct in6_addr *saddr);
void ipv6_proxy_select_ident(struct net *net, struct sk_buff *skb);

int ip6_dst_hoplimit(struct dst_entry *dst);

static inline int ip6_sk_dst_hoplimit(struct ipv6_pinfo *np, struct flowi6 *fl6,
				      struct dst_entry *dst)
{
	int hlimit;

	if (ipv6_addr_is_multicast(&fl6->daddr))
		hlimit = np->mcast_hops;
	else
		hlimit = np->hop_limit;
	if (hlimit < 0)
		hlimit = ip6_dst_hoplimit(dst);
	return hlimit;
}

/* copy IPv6 saddr & daddr to flow_keys, possibly using 64bit load/store
 * Equivalent to :	flow->v6addrs.src = iph->saddr;
 *			flow->v6addrs.dst = iph->daddr;
 */
static inline void iph_to_flow_copy_v6addrs(struct flow_keys *flow,
					    const struct ipv6hdr *iph)
{
	BUILD_BUG_ON(offsetof(typeof(flow->addrs), v6addrs.dst) !=
		     offsetof(typeof(flow->addrs), v6addrs.src) +
		     sizeof(flow->addrs.v6addrs.src));
	memcpy(&flow->addrs.v6addrs, &iph->saddr, sizeof(flow->addrs.v6addrs));
	flow->control.addr_type = FLOW_DISSECTOR_KEY_IPV6_ADDRS;
}

#if IS_ENABLED(CONFIG_IPV6)

/* Sysctl settings for net ipv6.auto_flowlabels */
#define IP6_AUTO_FLOW_LABEL_OFF		0
#define IP6_AUTO_FLOW_LABEL_OPTOUT	1
#define IP6_AUTO_FLOW_LABEL_OPTIN	2
#define IP6_AUTO_FLOW_LABEL_FORCED	3

#define IP6_AUTO_FLOW_LABEL_MAX		IP6_AUTO_FLOW_LABEL_FORCED

#define IP6_DEFAULT_AUTO_FLOW_LABELS	IP6_AUTO_FLOW_LABEL_OPTOUT

static inline __be32 ip6_make_flowlabel(struct net *net, struct sk_buff *skb,
					__be32 flowlabel, bool autolabel,
					struct flowi6 *fl6)
{
	u32 hash;

	/* @flowlabel may include more than a flow label, eg, the traffic class.
	 * Here we want only the flow label value.
	 */
	flowlabel &= IPV6_FLOWLABEL_MASK;

	if (flowlabel ||
	    net->ipv6.sysctl.auto_flowlabels == IP6_AUTO_FLOW_LABEL_OFF ||
	    (!autolabel &&
	     net->ipv6.sysctl.auto_flowlabels != IP6_AUTO_FLOW_LABEL_FORCED))
		return flowlabel;

	hash = skb_get_hash_flowi6(skb, fl6);

	/* Since this is being sent on the wire obfuscate hash a bit
	 * to minimize possbility that any useful information to an
	 * attacker is leaked. Only lower 20 bits are relevant.
	 */
	rol32(hash, 16);

	flowlabel = (__force __be32)hash & IPV6_FLOWLABEL_MASK;

	if (net->ipv6.sysctl.flowlabel_state_ranges)
		flowlabel |= IPV6_FLOWLABEL_STATELESS_FLAG;

	return flowlabel;
}

static inline int ip6_default_np_autolabel(struct net *net)
{
	switch (net->ipv6.sysctl.auto_flowlabels) {
	case IP6_AUTO_FLOW_LABEL_OFF:
	case IP6_AUTO_FLOW_LABEL_OPTIN:
	default:
		return 0;
	case IP6_AUTO_FLOW_LABEL_OPTOUT:
	case IP6_AUTO_FLOW_LABEL_FORCED:
		return 1;
	}
}
#else
static inline void ip6_set_txhash(struct sock *sk) { }
static inline __be32 ip6_make_flowlabel(struct net *net, struct sk_buff *skb,
					__be32 flowlabel, bool autolabel,
					struct flowi6 *fl6)
{
	return flowlabel;
}
static inline int ip6_default_np_autolabel(struct net *net)
{
	return 0;
}
#endif


/*
 *	Header manipulation
 */
static inline void ip6_flow_hdr(struct ipv6hdr *hdr, unsigned int tclass,
				__be32 flowlabel)
{
	*(__be32 *)hdr = htonl(0x60000000 | (tclass << 20)) | flowlabel;
}

static inline __be32 ip6_flowinfo(const struct ipv6hdr *hdr)
{
	return *(__be32 *)hdr & IPV6_FLOWINFO_MASK;
}

static inline __be32 ip6_flowlabel(const struct ipv6hdr *hdr)
{
	return *(__be32 *)hdr & IPV6_FLOWLABEL_MASK;
}

static inline u8 ip6_tclass(__be32 flowinfo)
{
	return ntohl(flowinfo & IPV6_TCLASS_MASK) >> IPV6_TCLASS_SHIFT;
}

static inline __be32 ip6_make_flowinfo(unsigned int tclass, __be32 flowlabel)
{
	return htonl(tclass << IPV6_TCLASS_SHIFT) | flowlabel;
}

/*
 *	Prototypes exported by ipv6
 */

/*
 *	rcv function (called from netdevice level)
 */

int ipv6_rcv(struct sk_buff *skb, struct net_device *dev,
	     struct packet_type *pt, struct net_device *orig_dev);

int ip6_rcv_finish(struct net *net, struct sock *sk, struct sk_buff *skb);

/*
 *	upper-layer output functions
 */
int ip6_xmit(const struct sock *sk, struct sk_buff *skb, struct flowi6 *fl6,
	     __u32 mark, struct ipv6_txoptions *opt, int tclass);

int ip6_find_1stfragopt(struct sk_buff *skb, u8 **nexthdr);

int ip6_append_data(struct sock *sk,
		    int getfrag(void *from, char *to, int offset, int len,
				int odd, struct sk_buff *skb),
		    void *from, int length, int transhdrlen,
		    struct ipcm6_cookie *ipc6, struct flowi6 *fl6,
		    struct rt6_info *rt, unsigned int flags,
		    const struct sockcm_cookie *sockc);

int ip6_push_pending_frames(struct sock *sk);

void ip6_flush_pending_frames(struct sock *sk);

int ip6_send_skb(struct sk_buff *skb);

struct sk_buff *__ip6_make_skb(struct sock *sk, struct sk_buff_head *queue,
			       struct inet_cork_full *cork,
			       struct inet6_cork *v6_cork);
struct sk_buff *ip6_make_skb(struct sock *sk,
			     int getfrag(void *from, char *to, int offset,
					 int len, int odd, struct sk_buff *skb),
			     void *from, int length, int transhdrlen,
			     struct ipcm6_cookie *ipc6, struct flowi6 *fl6,
			     struct rt6_info *rt, unsigned int flags,
			     const struct sockcm_cookie *sockc);

static inline struct sk_buff *ip6_finish_skb(struct sock *sk)
{
	return __ip6_make_skb(sk, &sk->sk_write_queue, &inet_sk(sk)->cork,
			      &inet6_sk(sk)->cork);
}

int ip6_dst_lookup(struct net *net, struct sock *sk, struct dst_entry **dst,
		   struct flowi6 *fl6);
struct dst_entry *ip6_dst_lookup_flow(const struct sock *sk, struct flowi6 *fl6,
				      const struct in6_addr *final_dst);
struct dst_entry *ip6_sk_dst_lookup_flow(struct sock *sk, struct flowi6 *fl6,
					 const struct in6_addr *final_dst);
struct dst_entry *ip6_blackhole_route(struct net *net,
				      struct dst_entry *orig_dst);

/*
 *	skb processing functions
 */

int ip6_output(struct net *net, struct sock *sk, struct sk_buff *skb);
int ip6_forward(struct sk_buff *skb);
int ip6_input(struct sk_buff *skb);
int ip6_mc_input(struct sk_buff *skb);

int __ip6_local_out(struct net *net, struct sock *sk, struct sk_buff *skb);
int ip6_local_out(struct net *net, struct sock *sk, struct sk_buff *skb);

/*
 *	Extension header (options) processing
 */

void ipv6_push_nfrag_opts(struct sk_buff *skb, struct ipv6_txoptions *opt,
			  u8 *proto, struct in6_addr **daddr_p,
			  struct in6_addr *saddr);
void ipv6_push_frag_opts(struct sk_buff *skb, struct ipv6_txoptions *opt,
			 u8 *proto);

int ipv6_skip_exthdr(const struct sk_buff *, int start, u8 *nexthdrp,
		     __be16 *frag_offp);

bool ipv6_ext_hdr(u8 nexthdr);

enum {
	IP6_FH_F_FRAG		= (1 << 0),
	IP6_FH_F_AUTH		= (1 << 1),
	IP6_FH_F_SKIP_RH	= (1 << 2),
};

/* find specified header and get offset to it */
int ipv6_find_hdr(const struct sk_buff *skb, unsigned int *offset, int target,
		  unsigned short *fragoff, int *fragflg);

int ipv6_find_tlv(const struct sk_buff *skb, int offset, int type);

struct in6_addr *fl6_update_dst(struct flowi6 *fl6,
				const struct ipv6_txoptions *opt,
				struct in6_addr *orig);

/*
 *	socket options (ipv6_sockglue.c)
 */

int ipv6_setsockopt(struct sock *sk, int level, int optname,
		    char __user *optval, unsigned int optlen);
int ipv6_getsockopt(struct sock *sk, int level, int optname,
		    char __user *optval, int __user *optlen);
int compat_ipv6_setsockopt(struct sock *sk, int level, int optname,
			   char __user *optval, unsigned int optlen);
int compat_ipv6_getsockopt(struct sock *sk, int level, int optname,
			   char __user *optval, int __user *optlen);

int __ip6_datagram_connect(struct sock *sk, struct sockaddr *addr,
			   int addr_len);
int ip6_datagram_connect(struct sock *sk, struct sockaddr *addr, int addr_len);
int ip6_datagram_connect_v6_only(struct sock *sk, struct sockaddr *addr,
				 int addr_len);
int ip6_datagram_dst_update(struct sock *sk, bool fix_sk_saddr);
void ip6_datagram_release_cb(struct sock *sk);

int ipv6_recv_error(struct sock *sk, struct msghdr *msg, int len,
		    int *addr_len);
int ipv6_recv_rxpmtu(struct sock *sk, struct msghdr *msg, int len,
		     int *addr_len);
void ipv6_icmp_error(struct sock *sk, struct sk_buff *skb, int err, __be16 port,
		     u32 info, u8 *payload);
void ipv6_local_error(struct sock *sk, int err, struct flowi6 *fl6, u32 info);
void ipv6_local_rxpmtu(struct sock *sk, struct flowi6 *fl6, u32 mtu);

int inet6_release(struct socket *sock);
int inet6_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len);
int inet6_getname(struct socket *sock, struct sockaddr *uaddr, int *uaddr_len,
		  int peer);
int inet6_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg);

int inet6_hash_connect(struct inet_timewait_death_row *death_row,
			      struct sock *sk);

/*
 * reassembly.c
 */
extern const struct proto_ops inet6_stream_ops;
extern const struct proto_ops inet6_dgram_ops;
extern const struct proto_ops inet6_sockraw_ops;

struct group_source_req;
struct group_filter;

int ip6_mc_source(int add, int omode, struct sock *sk,
		  struct group_source_req *pgsr);
int ip6_mc_msfilter(struct sock *sk, struct group_filter *gsf);
int ip6_mc_msfget(struct sock *sk, struct group_filter *gsf,
		  struct group_filter __user *optval, int __user *optlen);

#ifdef CONFIG_PROC_FS
int ac6_proc_init(struct net *net);
void ac6_proc_exit(struct net *net);
int raw6_proc_init(void);
void raw6_proc_exit(void);
int tcp6_proc_init(struct net *net);
void tcp6_proc_exit(struct net *net);
int udp6_proc_init(struct net *net);
void udp6_proc_exit(struct net *net);
int udplite6_proc_init(void);
void udplite6_proc_exit(void);
int ipv6_misc_proc_init(void);
void ipv6_misc_proc_exit(void);
int snmp6_register_dev(struct inet6_dev *idev);
int snmp6_unregister_dev(struct inet6_dev *idev);

#else
static inline int ac6_proc_init(struct net *net) { return 0; }
static inline void ac6_proc_exit(struct net *net) { }
static inline int snmp6_register_dev(struct inet6_dev *idev) { return 0; }
static inline int snmp6_unregister_dev(struct inet6_dev *idev) { return 0; }
#endif

#ifdef CONFIG_SYSCTL
extern struct ctl_table ipv6_route_table_template[];

struct ctl_table *ipv6_icmp_sysctl_init(struct net *net);
struct ctl_table *ipv6_route_sysctl_init(struct net *net);
int ipv6_sysctl_register(void);
void ipv6_sysctl_unregister(void);
#endif

int ipv6_sock_mc_join(struct sock *sk, int ifindex,
		      const struct in6_addr *addr);
int ipv6_sock_mc_drop(struct sock *sk, int ifindex,
		      const struct in6_addr *addr);
#endif /* _NET_IPV6_H */
