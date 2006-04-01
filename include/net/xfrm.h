#ifndef _NET_XFRM_H
#define _NET_XFRM_H

#include <linux/compiler.h>
#include <linux/in.h>
#include <linux/xfrm.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/crypto.h>
#include <linux/pfkeyv2.h>
#include <linux/in6.h>
#include <linux/mutex.h>

#include <net/sock.h>
#include <net/dst.h>
#include <net/route.h>
#include <net/ipv6.h>
#include <net/ip6_fib.h>

#define XFRM_ALIGN8(len)	(((len) + 7) & ~7)

extern struct sock *xfrm_nl;
extern u32 sysctl_xfrm_aevent_etime;
extern u32 sysctl_xfrm_aevent_rseqth;

extern struct mutex xfrm_cfg_mutex;

/* Organization of SPD aka "XFRM rules"
   ------------------------------------

   Basic objects:
   - policy rule, struct xfrm_policy (=SPD entry)
   - bundle of transformations, struct dst_entry == struct xfrm_dst (=SA bundle)
   - instance of a transformer, struct xfrm_state (=SA)
   - template to clone xfrm_state, struct xfrm_tmpl

   SPD is plain linear list of xfrm_policy rules, ordered by priority.
   (To be compatible with existing pfkeyv2 implementations,
   many rules with priority of 0x7fffffff are allowed to exist and
   such rules are ordered in an unpredictable way, thanks to bsd folks.)

   Lookup is plain linear search until the first match with selector.

   If "action" is "block", then we prohibit the flow, otherwise:
   if "xfrms_nr" is zero, the flow passes untransformed. Otherwise,
   policy entry has list of up to XFRM_MAX_DEPTH transformations,
   described by templates xfrm_tmpl. Each template is resolved
   to a complete xfrm_state (see below) and we pack bundle of transformations
   to a dst_entry returned to requestor.

   dst -. xfrm  .-> xfrm_state #1
    |---. child .-> dst -. xfrm .-> xfrm_state #2
                     |---. child .-> dst -. xfrm .-> xfrm_state #3
                                      |---. child .-> NULL

   Bundles are cached at xrfm_policy struct (field ->bundles).


   Resolution of xrfm_tmpl
   -----------------------
   Template contains:
   1. ->mode		Mode: transport or tunnel
   2. ->id.proto	Protocol: AH/ESP/IPCOMP
   3. ->id.daddr	Remote tunnel endpoint, ignored for transport mode.
      Q: allow to resolve security gateway?
   4. ->id.spi          If not zero, static SPI.
   5. ->saddr		Local tunnel endpoint, ignored for transport mode.
   6. ->algos		List of allowed algos. Plain bitmask now.
      Q: ealgos, aalgos, calgos. What a mess...
   7. ->share		Sharing mode.
      Q: how to implement private sharing mode? To add struct sock* to
      flow id?

   Having this template we search through SAD searching for entries
   with appropriate mode/proto/algo, permitted by selector.
   If no appropriate entry found, it is requested from key manager.

   PROBLEMS:
   Q: How to find all the bundles referring to a physical path for
      PMTU discovery? Seems, dst should contain list of all parents...
      and enter to infinite locking hierarchy disaster.
      No! It is easier, we will not search for them, let them find us.
      We add genid to each dst plus pointer to genid of raw IP route,
      pmtu disc will update pmtu on raw IP route and increase its genid.
      dst_check() will see this for top level and trigger resyncing
      metrics. Plus, it will be made via sk->sk_dst_cache. Solved.
 */

/* Full description of state of transformer. */
struct xfrm_state
{
	/* Note: bydst is re-used during gc */
	struct list_head	bydst;
	struct list_head	byspi;

	atomic_t		refcnt;
	spinlock_t		lock;

	struct xfrm_id		id;
	struct xfrm_selector	sel;

	/* Key manger bits */
	struct {
		u8		state;
		u8		dying;
		u32		seq;
	} km;

	/* Parameters of this state. */
	struct {
		u32		reqid;
		u8		mode;
		u8		replay_window;
		u8		aalgo, ealgo, calgo;
		u8		flags;
		u16		family;
		xfrm_address_t	saddr;
		int		header_len;
		int		trailer_len;
	} props;

	struct xfrm_lifetime_cfg lft;

	/* Data for transformer */
	struct xfrm_algo	*aalg;
	struct xfrm_algo	*ealg;
	struct xfrm_algo	*calg;

	/* Data for encapsulator */
	struct xfrm_encap_tmpl	*encap;

	/* IPComp needs an IPIP tunnel for handling uncompressed packets */
	struct xfrm_state	*tunnel;

	/* If a tunnel, number of users + 1 */
	atomic_t		tunnel_users;

	/* State for replay detection */
	struct xfrm_replay_state replay;

	/* Replay detection state at the time we sent the last notification */
	struct xfrm_replay_state preplay;

	/* Replay detection notification settings */
	u32			replay_maxage;
	u32			replay_maxdiff;

	/* Replay detection notification timer */
	struct timer_list	rtimer;

	/* Statistics */
	struct xfrm_stats	stats;

	struct xfrm_lifetime_cur curlft;
	struct timer_list	timer;

	/* Reference to data common to all the instances of this
	 * transformer. */
	struct xfrm_type	*type;

	/* Security context */
	struct xfrm_sec_ctx	*security;

	/* Private data of this transformer, format is opaque,
	 * interpreted by xfrm_type methods. */
	void			*data;
};

enum {
	XFRM_STATE_VOID,
	XFRM_STATE_ACQ,
	XFRM_STATE_VALID,
	XFRM_STATE_ERROR,
	XFRM_STATE_EXPIRED,
	XFRM_STATE_DEAD
};

/* callback structure passed from either netlink or pfkey */
struct km_event
{
	union {
		u32 hard;
		u32 proto;
		u32 byid;
		u32 aevent;
	} data;

	u32	seq;
	u32	pid;
	u32	event;
};

struct xfrm_type;
struct xfrm_dst;
struct xfrm_policy_afinfo {
	unsigned short		family;
	rwlock_t		lock;
	struct xfrm_type_map	*type_map;
	struct dst_ops		*dst_ops;
	void			(*garbage_collect)(void);
	int			(*dst_lookup)(struct xfrm_dst **dst, struct flowi *fl);
	struct dst_entry	*(*find_bundle)(struct flowi *fl, struct xfrm_policy *policy);
	int			(*bundle_create)(struct xfrm_policy *policy, 
						 struct xfrm_state **xfrm, 
						 int nx,
						 struct flowi *fl, 
						 struct dst_entry **dst_p);
	void			(*decode_session)(struct sk_buff *skb,
						  struct flowi *fl);
};

extern int xfrm_policy_register_afinfo(struct xfrm_policy_afinfo *afinfo);
extern int xfrm_policy_unregister_afinfo(struct xfrm_policy_afinfo *afinfo);
extern void km_policy_notify(struct xfrm_policy *xp, int dir, struct km_event *c);
extern void km_state_notify(struct xfrm_state *x, struct km_event *c);
#define XFRM_ACQ_EXPIRES	30

struct xfrm_tmpl;
extern int km_query(struct xfrm_state *x, struct xfrm_tmpl *t, struct xfrm_policy *pol);
extern void km_state_expired(struct xfrm_state *x, int hard, u32 pid);
extern int __xfrm_state_delete(struct xfrm_state *x);

struct xfrm_state_afinfo {
	unsigned short		family;
	rwlock_t		lock;
	struct list_head	*state_bydst;
	struct list_head	*state_byspi;
	int			(*init_flags)(struct xfrm_state *x);
	void			(*init_tempsel)(struct xfrm_state *x, struct flowi *fl,
						struct xfrm_tmpl *tmpl,
						xfrm_address_t *daddr, xfrm_address_t *saddr);
	struct xfrm_state	*(*state_lookup)(xfrm_address_t *daddr, u32 spi, u8 proto);
	struct xfrm_state	*(*find_acq)(u8 mode, u32 reqid, u8 proto, 
					     xfrm_address_t *daddr, xfrm_address_t *saddr, 
					     int create);
};

extern int xfrm_state_register_afinfo(struct xfrm_state_afinfo *afinfo);
extern int xfrm_state_unregister_afinfo(struct xfrm_state_afinfo *afinfo);

extern void xfrm_state_delete_tunnel(struct xfrm_state *x);

struct xfrm_decap_state;
struct xfrm_type
{
	char			*description;
	struct module		*owner;
	__u8			proto;

	int			(*init_state)(struct xfrm_state *x);
	void			(*destructor)(struct xfrm_state *);
	int			(*input)(struct xfrm_state *, struct sk_buff *skb);
	int			(*output)(struct xfrm_state *, struct sk_buff *pskb);
	/* Estimate maximal size of result of transformation of a dgram */
	u32			(*get_max_size)(struct xfrm_state *, int size);
};

struct xfrm_type_map {
	rwlock_t		lock;
	struct xfrm_type	*map[256];
};

extern int xfrm_register_type(struct xfrm_type *type, unsigned short family);
extern int xfrm_unregister_type(struct xfrm_type *type, unsigned short family);
extern struct xfrm_type *xfrm_get_type(u8 proto, unsigned short family);
extern void xfrm_put_type(struct xfrm_type *type);

struct xfrm_tmpl
{
/* id in template is interpreted as:
 * daddr - destination of tunnel, may be zero for transport mode.
 * spi   - zero to acquire spi. Not zero if spi is static, then
 *	   daddr must be fixed too.
 * proto - AH/ESP/IPCOMP
 */
	struct xfrm_id		id;

/* Source address of tunnel. Ignored, if it is not a tunnel. */
	xfrm_address_t		saddr;

	__u32			reqid;

/* Mode: transport/tunnel */
	__u8			mode;

/* Sharing mode: unique, this session only, this user only etc. */
	__u8			share;

/* May skip this transfomration if no SA is found */
	__u8			optional;

/* Bit mask of algos allowed for acquisition */
	__u32			aalgos;
	__u32			ealgos;
	__u32			calgos;
};

#define XFRM_MAX_DEPTH		4

struct xfrm_policy
{
	struct xfrm_policy	*next;
	struct list_head	list;

	/* This lock only affects elements except for entry. */
	rwlock_t		lock;
	atomic_t		refcnt;
	struct timer_list	timer;

	u32			priority;
	u32			index;
	struct xfrm_selector	selector;
	struct xfrm_lifetime_cfg lft;
	struct xfrm_lifetime_cur curlft;
	struct dst_entry       *bundles;
	__u16			family;
	__u8			action;
	__u8			flags;
	__u8			dead;
	__u8			xfrm_nr;
	struct xfrm_sec_ctx	*security;
	struct xfrm_tmpl       	xfrm_vec[XFRM_MAX_DEPTH];
};

#define XFRM_KM_TIMEOUT                30
/* which seqno */
#define XFRM_REPLAY_SEQ		1
#define XFRM_REPLAY_OSEQ	2
#define XFRM_REPLAY_SEQ_MASK	3
/* what happened */
#define XFRM_REPLAY_UPDATE	XFRM_AE_CR
#define XFRM_REPLAY_TIMEOUT	XFRM_AE_CE

/* default aevent timeout in units of 100ms */
#define XFRM_AE_ETIME			10
/* Async Event timer multiplier */
#define XFRM_AE_ETH_M			10
/* default seq threshold size */
#define XFRM_AE_SEQT_SIZE		2

struct xfrm_mgr
{
	struct list_head	list;
	char			*id;
	int			(*notify)(struct xfrm_state *x, struct km_event *c);
	int			(*acquire)(struct xfrm_state *x, struct xfrm_tmpl *, struct xfrm_policy *xp, int dir);
	struct xfrm_policy	*(*compile_policy)(u16 family, int opt, u8 *data, int len, int *dir);
	int			(*new_mapping)(struct xfrm_state *x, xfrm_address_t *ipaddr, u16 sport);
	int			(*notify_policy)(struct xfrm_policy *x, int dir, struct km_event *c);
};

extern int xfrm_register_km(struct xfrm_mgr *km);
extern int xfrm_unregister_km(struct xfrm_mgr *km);


extern struct xfrm_policy *xfrm_policy_list[XFRM_POLICY_MAX*2];

static inline void xfrm_pol_hold(struct xfrm_policy *policy)
{
	if (likely(policy != NULL))
		atomic_inc(&policy->refcnt);
}

extern void __xfrm_policy_destroy(struct xfrm_policy *policy);

static inline void xfrm_pol_put(struct xfrm_policy *policy)
{
	if (atomic_dec_and_test(&policy->refcnt))
		__xfrm_policy_destroy(policy);
}

#define XFRM_DST_HSIZE		1024

static __inline__
unsigned __xfrm4_dst_hash(xfrm_address_t *addr)
{
	unsigned h;
	h = ntohl(addr->a4);
	h = (h ^ (h>>16)) % XFRM_DST_HSIZE;
	return h;
}

static __inline__
unsigned __xfrm6_dst_hash(xfrm_address_t *addr)
{
	unsigned h;
	h = ntohl(addr->a6[2]^addr->a6[3]);
	h = (h ^ (h>>16)) % XFRM_DST_HSIZE;
	return h;
}

static __inline__
unsigned xfrm_dst_hash(xfrm_address_t *addr, unsigned short family)
{
	switch (family) {
	case AF_INET:
		return __xfrm4_dst_hash(addr);
	case AF_INET6:
		return __xfrm6_dst_hash(addr);
	}
	return 0;
}

static __inline__
unsigned __xfrm4_spi_hash(xfrm_address_t *addr, u32 spi, u8 proto)
{
	unsigned h;
	h = ntohl(addr->a4^spi^proto);
	h = (h ^ (h>>10) ^ (h>>20)) % XFRM_DST_HSIZE;
	return h;
}

static __inline__
unsigned __xfrm6_spi_hash(xfrm_address_t *addr, u32 spi, u8 proto)
{
	unsigned h;
	h = ntohl(addr->a6[2]^addr->a6[3]^spi^proto);
	h = (h ^ (h>>10) ^ (h>>20)) % XFRM_DST_HSIZE;
	return h;
}

static __inline__
unsigned xfrm_spi_hash(xfrm_address_t *addr, u32 spi, u8 proto, unsigned short family)
{
	switch (family) {
	case AF_INET:
		return __xfrm4_spi_hash(addr, spi, proto);
	case AF_INET6:
		return __xfrm6_spi_hash(addr, spi, proto);
	}
	return 0;	/*XXX*/
}

extern void __xfrm_state_destroy(struct xfrm_state *);

static inline void __xfrm_state_put(struct xfrm_state *x)
{
	atomic_dec(&x->refcnt);
}

static inline void xfrm_state_put(struct xfrm_state *x)
{
	if (atomic_dec_and_test(&x->refcnt))
		__xfrm_state_destroy(x);
}

static inline void xfrm_state_hold(struct xfrm_state *x)
{
	atomic_inc(&x->refcnt);
}

static __inline__ int addr_match(void *token1, void *token2, int prefixlen)
{
	__u32 *a1 = token1;
	__u32 *a2 = token2;
	int pdw;
	int pbi;

	pdw = prefixlen >> 5;	  /* num of whole __u32 in prefix */
	pbi = prefixlen &  0x1f;  /* num of bits in incomplete u32 in prefix */

	if (pdw)
		if (memcmp(a1, a2, pdw << 2))
			return 0;

	if (pbi) {
		__u32 mask;

		mask = htonl((0xffffffff) << (32 - pbi));

		if ((a1[pdw] ^ a2[pdw]) & mask)
			return 0;
	}

	return 1;
}

static __inline__
u16 xfrm_flowi_sport(struct flowi *fl)
{
	u16 port;
	switch(fl->proto) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
	case IPPROTO_SCTP:
		port = fl->fl_ip_sport;
		break;
	case IPPROTO_ICMP:
	case IPPROTO_ICMPV6:
		port = htons(fl->fl_icmp_type);
		break;
	default:
		port = 0;	/*XXX*/
	}
	return port;
}

static __inline__
u16 xfrm_flowi_dport(struct flowi *fl)
{
	u16 port;
	switch(fl->proto) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
	case IPPROTO_SCTP:
		port = fl->fl_ip_dport;
		break;
	case IPPROTO_ICMP:
	case IPPROTO_ICMPV6:
		port = htons(fl->fl_icmp_code);
		break;
	default:
		port = 0;	/*XXX*/
	}
	return port;
}

static inline int
__xfrm4_selector_match(struct xfrm_selector *sel, struct flowi *fl)
{
	return  addr_match(&fl->fl4_dst, &sel->daddr, sel->prefixlen_d) &&
		addr_match(&fl->fl4_src, &sel->saddr, sel->prefixlen_s) &&
		!((xfrm_flowi_dport(fl) ^ sel->dport) & sel->dport_mask) &&
		!((xfrm_flowi_sport(fl) ^ sel->sport) & sel->sport_mask) &&
		(fl->proto == sel->proto || !sel->proto) &&
		(fl->oif == sel->ifindex || !sel->ifindex);
}

static inline int
__xfrm6_selector_match(struct xfrm_selector *sel, struct flowi *fl)
{
	return  addr_match(&fl->fl6_dst, &sel->daddr, sel->prefixlen_d) &&
		addr_match(&fl->fl6_src, &sel->saddr, sel->prefixlen_s) &&
		!((xfrm_flowi_dport(fl) ^ sel->dport) & sel->dport_mask) &&
		!((xfrm_flowi_sport(fl) ^ sel->sport) & sel->sport_mask) &&
		(fl->proto == sel->proto || !sel->proto) &&
		(fl->oif == sel->ifindex || !sel->ifindex);
}

static inline int
xfrm_selector_match(struct xfrm_selector *sel, struct flowi *fl,
		    unsigned short family)
{
	switch (family) {
	case AF_INET:
		return __xfrm4_selector_match(sel, fl);
	case AF_INET6:
		return __xfrm6_selector_match(sel, fl);
	}
	return 0;
}

#ifdef CONFIG_SECURITY_NETWORK_XFRM
/*	If neither has a context --> match
 * 	Otherwise, both must have a context and the sids, doi, alg must match
 */
static inline int xfrm_sec_ctx_match(struct xfrm_sec_ctx *s1, struct xfrm_sec_ctx *s2)
{
	return ((!s1 && !s2) ||
		(s1 && s2 &&
		 (s1->ctx_sid == s2->ctx_sid) &&
		 (s1->ctx_doi == s2->ctx_doi) &&
		 (s1->ctx_alg == s2->ctx_alg)));
}
#else
static inline int xfrm_sec_ctx_match(struct xfrm_sec_ctx *s1, struct xfrm_sec_ctx *s2)
{
	return 1;
}
#endif

/* A struct encoding bundle of transformations to apply to some set of flow.
 *
 * dst->child points to the next element of bundle.
 * dst->xfrm  points to an instanse of transformer.
 *
 * Due to unfortunate limitations of current routing cache, which we
 * have no time to fix, it mirrors struct rtable and bound to the same
 * routing key, including saddr,daddr. However, we can have many of
 * bundles differing by session id. All the bundles grow from a parent
 * policy rule.
 */
struct xfrm_dst
{
	union {
		struct xfrm_dst		*next;
		struct dst_entry	dst;
		struct rtable		rt;
		struct rt6_info		rt6;
	} u;
	struct dst_entry *route;
	u32 route_mtu_cached;
	u32 child_mtu_cached;
	u32 route_cookie;
	u32 path_cookie;
};

static inline void xfrm_dst_destroy(struct xfrm_dst *xdst)
{
	dst_release(xdst->route);
	if (likely(xdst->u.dst.xfrm))
		xfrm_state_put(xdst->u.dst.xfrm);
}

extern void xfrm_dst_ifdown(struct dst_entry *dst, struct net_device *dev);

/* Decapsulation state, used by the input to store data during
 * decapsulation procedure, to be used later (during the policy
 * check
 */
struct xfrm_decap_state {
	char	decap_data[20];
	__u16	decap_type;
};   

struct sec_decap_state {
	struct xfrm_state	*xvec;
	struct xfrm_decap_state decap;
};

struct sec_path
{
	atomic_t		refcnt;
	int			len;
	struct sec_decap_state	x[XFRM_MAX_DEPTH];
};

static inline struct sec_path *
secpath_get(struct sec_path *sp)
{
	if (sp)
		atomic_inc(&sp->refcnt);
	return sp;
}

extern void __secpath_destroy(struct sec_path *sp);

static inline void
secpath_put(struct sec_path *sp)
{
	if (sp && atomic_dec_and_test(&sp->refcnt))
		__secpath_destroy(sp);
}

extern struct sec_path *secpath_dup(struct sec_path *src);

static inline void
secpath_reset(struct sk_buff *skb)
{
#ifdef CONFIG_XFRM
	secpath_put(skb->sp);
	skb->sp = NULL;
#endif
}

static inline int
__xfrm4_state_addr_cmp(struct xfrm_tmpl *tmpl, struct xfrm_state *x)
{
	return	(tmpl->saddr.a4 &&
		 tmpl->saddr.a4 != x->props.saddr.a4);
}

static inline int
__xfrm6_state_addr_cmp(struct xfrm_tmpl *tmpl, struct xfrm_state *x)
{
	return	(!ipv6_addr_any((struct in6_addr*)&tmpl->saddr) &&
		 ipv6_addr_cmp((struct in6_addr *)&tmpl->saddr, (struct in6_addr*)&x->props.saddr));
}

static inline int
xfrm_state_addr_cmp(struct xfrm_tmpl *tmpl, struct xfrm_state *x, unsigned short family)
{
	switch (family) {
	case AF_INET:
		return __xfrm4_state_addr_cmp(tmpl, x);
	case AF_INET6:
		return __xfrm6_state_addr_cmp(tmpl, x);
	}
	return !0;
}

#ifdef CONFIG_XFRM

extern int __xfrm_policy_check(struct sock *, int dir, struct sk_buff *skb, unsigned short family);

static inline int xfrm_policy_check(struct sock *sk, int dir, struct sk_buff *skb, unsigned short family)
{
	if (sk && sk->sk_policy[XFRM_POLICY_IN])
		return __xfrm_policy_check(sk, dir, skb, family);
		
	return	(!xfrm_policy_list[dir] && !skb->sp) ||
		(skb->dst->flags & DST_NOPOLICY) ||
		__xfrm_policy_check(sk, dir, skb, family);
}

static inline int xfrm4_policy_check(struct sock *sk, int dir, struct sk_buff *skb)
{
	return xfrm_policy_check(sk, dir, skb, AF_INET);
}

static inline int xfrm6_policy_check(struct sock *sk, int dir, struct sk_buff *skb)
{
	return xfrm_policy_check(sk, dir, skb, AF_INET6);
}

extern int xfrm_decode_session(struct sk_buff *skb, struct flowi *fl, unsigned short family);
extern int __xfrm_route_forward(struct sk_buff *skb, unsigned short family);

static inline int xfrm_route_forward(struct sk_buff *skb, unsigned short family)
{
	return	!xfrm_policy_list[XFRM_POLICY_OUT] ||
		(skb->dst->flags & DST_NOXFRM) ||
		__xfrm_route_forward(skb, family);
}

static inline int xfrm4_route_forward(struct sk_buff *skb)
{
	return xfrm_route_forward(skb, AF_INET);
}

static inline int xfrm6_route_forward(struct sk_buff *skb)
{
	return xfrm_route_forward(skb, AF_INET6);
}

extern int __xfrm_sk_clone_policy(struct sock *sk);

static inline int xfrm_sk_clone_policy(struct sock *sk)
{
	if (unlikely(sk->sk_policy[0] || sk->sk_policy[1]))
		return __xfrm_sk_clone_policy(sk);
	return 0;
}

extern int xfrm_policy_delete(struct xfrm_policy *pol, int dir);

static inline void xfrm_sk_free_policy(struct sock *sk)
{
	if (unlikely(sk->sk_policy[0] != NULL)) {
		xfrm_policy_delete(sk->sk_policy[0], XFRM_POLICY_MAX);
		sk->sk_policy[0] = NULL;
	}
	if (unlikely(sk->sk_policy[1] != NULL)) {
		xfrm_policy_delete(sk->sk_policy[1], XFRM_POLICY_MAX+1);
		sk->sk_policy[1] = NULL;
	}
}

#else

static inline void xfrm_sk_free_policy(struct sock *sk) {}
static inline int xfrm_sk_clone_policy(struct sock *sk) { return 0; }
static inline int xfrm6_route_forward(struct sk_buff *skb) { return 1; }  
static inline int xfrm4_route_forward(struct sk_buff *skb) { return 1; } 
static inline int xfrm6_policy_check(struct sock *sk, int dir, struct sk_buff *skb)
{ 
	return 1; 
} 
static inline int xfrm4_policy_check(struct sock *sk, int dir, struct sk_buff *skb)
{
	return 1;
}
static inline int xfrm_policy_check(struct sock *sk, int dir, struct sk_buff *skb, unsigned short family)
{
	return 1;
}
#endif

static __inline__
xfrm_address_t *xfrm_flowi_daddr(struct flowi *fl, unsigned short family)
{
	switch (family){
	case AF_INET:
		return (xfrm_address_t *)&fl->fl4_dst;
	case AF_INET6:
		return (xfrm_address_t *)&fl->fl6_dst;
	}
	return NULL;
}

static __inline__
xfrm_address_t *xfrm_flowi_saddr(struct flowi *fl, unsigned short family)
{
	switch (family){
	case AF_INET:
		return (xfrm_address_t *)&fl->fl4_src;
	case AF_INET6:
		return (xfrm_address_t *)&fl->fl6_src;
	}
	return NULL;
}

static __inline__ int
__xfrm4_state_addr_check(struct xfrm_state *x,
			 xfrm_address_t *daddr, xfrm_address_t *saddr)
{
	if (daddr->a4 == x->id.daddr.a4 &&
	    (saddr->a4 == x->props.saddr.a4 || !saddr->a4 || !x->props.saddr.a4))
		return 1;
	return 0;
}

static __inline__ int
__xfrm6_state_addr_check(struct xfrm_state *x,
			 xfrm_address_t *daddr, xfrm_address_t *saddr)
{
	if (!ipv6_addr_cmp((struct in6_addr *)daddr, (struct in6_addr *)&x->id.daddr) &&
	    (!ipv6_addr_cmp((struct in6_addr *)saddr, (struct in6_addr *)&x->props.saddr)|| 
	     ipv6_addr_any((struct in6_addr *)saddr) || 
	     ipv6_addr_any((struct in6_addr *)&x->props.saddr)))
		return 1;
	return 0;
}

static __inline__ int
xfrm_state_addr_check(struct xfrm_state *x,
		      xfrm_address_t *daddr, xfrm_address_t *saddr,
		      unsigned short family)
{
	switch (family) {
	case AF_INET:
		return __xfrm4_state_addr_check(x, daddr, saddr);
	case AF_INET6:
		return __xfrm6_state_addr_check(x, daddr, saddr);
	}
	return 0;
}

static inline int xfrm_state_kern(struct xfrm_state *x)
{
	return atomic_read(&x->tunnel_users);
}

/*
 * xfrm algorithm information
 */
struct xfrm_algo_auth_info {
	u16 icv_truncbits;
	u16 icv_fullbits;
};

struct xfrm_algo_encr_info {
	u16 blockbits;
	u16 defkeybits;
};

struct xfrm_algo_comp_info {
	u16 threshold;
};

struct xfrm_algo_desc {
	char *name;
	u8 available:1;
	union {
		struct xfrm_algo_auth_info auth;
		struct xfrm_algo_encr_info encr;
		struct xfrm_algo_comp_info comp;
	} uinfo;
	struct sadb_alg desc;
};

/* XFRM tunnel handlers.  */
struct xfrm_tunnel {
	int (*handler)(struct sk_buff *skb);
	int (*err_handler)(struct sk_buff *skb, __u32 info);

	struct xfrm_tunnel *next;
	int priority;
};

struct xfrm6_tunnel {
	int (*handler)(struct sk_buff *skb);
	int (*err_handler)(struct sk_buff *skb, struct inet6_skb_parm *opt,
			   int type, int code, int offset, __u32 info);

	struct xfrm6_tunnel *next;
	int priority;
};

extern void xfrm_init(void);
extern void xfrm4_init(void);
extern void xfrm6_init(void);
extern void xfrm6_fini(void);
extern void xfrm_state_init(void);
extern void xfrm4_state_init(void);
extern void xfrm6_state_init(void);
extern void xfrm6_state_fini(void);

extern int xfrm_state_walk(u8 proto, int (*func)(struct xfrm_state *, int, void*), void *);
extern struct xfrm_state *xfrm_state_alloc(void);
extern struct xfrm_state *xfrm_state_find(xfrm_address_t *daddr, xfrm_address_t *saddr, 
					  struct flowi *fl, struct xfrm_tmpl *tmpl,
					  struct xfrm_policy *pol, int *err,
					  unsigned short family);
extern int xfrm_state_check_expire(struct xfrm_state *x);
extern void xfrm_state_insert(struct xfrm_state *x);
extern int xfrm_state_add(struct xfrm_state *x);
extern int xfrm_state_update(struct xfrm_state *x);
extern struct xfrm_state *xfrm_state_lookup(xfrm_address_t *daddr, u32 spi, u8 proto, unsigned short family);
extern struct xfrm_state *xfrm_find_acq_byseq(u32 seq);
extern int xfrm_state_delete(struct xfrm_state *x);
extern void xfrm_state_flush(u8 proto);
extern int xfrm_replay_check(struct xfrm_state *x, u32 seq);
extern void xfrm_replay_advance(struct xfrm_state *x, u32 seq);
extern void xfrm_replay_notify(struct xfrm_state *x, int event);
extern int xfrm_state_check(struct xfrm_state *x, struct sk_buff *skb);
extern int xfrm_state_mtu(struct xfrm_state *x, int mtu);
extern int xfrm_init_state(struct xfrm_state *x);
extern int xfrm4_rcv(struct sk_buff *skb);
extern int xfrm4_output(struct sk_buff *skb);
extern int xfrm4_tunnel_register(struct xfrm_tunnel *handler);
extern int xfrm4_tunnel_deregister(struct xfrm_tunnel *handler);
extern int xfrm6_rcv_spi(struct sk_buff *skb, u32 spi);
extern int xfrm6_rcv(struct sk_buff **pskb);
extern int xfrm6_tunnel_register(struct xfrm6_tunnel *handler);
extern int xfrm6_tunnel_deregister(struct xfrm6_tunnel *handler);
extern u32 xfrm6_tunnel_alloc_spi(xfrm_address_t *saddr);
extern void xfrm6_tunnel_free_spi(xfrm_address_t *saddr);
extern u32 xfrm6_tunnel_spi_lookup(xfrm_address_t *saddr);
extern int xfrm6_output(struct sk_buff *skb);

#ifdef CONFIG_XFRM
extern int xfrm4_rcv_encap(struct sk_buff *skb, __u16 encap_type);
extern int xfrm_user_policy(struct sock *sk, int optname, u8 __user *optval, int optlen);
extern int xfrm_dst_lookup(struct xfrm_dst **dst, struct flowi *fl, unsigned short family);
#else
static inline int xfrm_user_policy(struct sock *sk, int optname, u8 __user *optval, int optlen)
{
 	return -ENOPROTOOPT;
} 

static inline int xfrm4_rcv_encap(struct sk_buff *skb, __u16 encap_type)
{
 	/* should not happen */
 	kfree_skb(skb);
	return 0;
}
static inline int xfrm_dst_lookup(struct xfrm_dst **dst, struct flowi *fl, unsigned short family)
{
	return -EINVAL;
} 
#endif

struct xfrm_policy *xfrm_policy_alloc(gfp_t gfp);
extern int xfrm_policy_walk(int (*func)(struct xfrm_policy *, int, int, void*), void *);
int xfrm_policy_insert(int dir, struct xfrm_policy *policy, int excl);
struct xfrm_policy *xfrm_policy_bysel_ctx(int dir, struct xfrm_selector *sel,
					  struct xfrm_sec_ctx *ctx, int delete);
struct xfrm_policy *xfrm_policy_byid(int dir, u32 id, int delete);
void xfrm_policy_flush(void);
u32 xfrm_get_acqseq(void);
void xfrm_alloc_spi(struct xfrm_state *x, u32 minspi, u32 maxspi);
struct xfrm_state * xfrm_find_acq(u8 mode, u32 reqid, u8 proto, 
				  xfrm_address_t *daddr, xfrm_address_t *saddr, 
				  int create, unsigned short family);
extern void xfrm_policy_flush(void);
extern int xfrm_sk_policy_insert(struct sock *sk, int dir, struct xfrm_policy *pol);
extern int xfrm_flush_bundles(void);
extern void xfrm_flush_all_bundles(void);
extern int xfrm_bundle_ok(struct xfrm_dst *xdst, struct flowi *fl, int family);
extern void xfrm_init_pmtu(struct dst_entry *dst);

extern wait_queue_head_t km_waitq;
extern int km_new_mapping(struct xfrm_state *x, xfrm_address_t *ipaddr, u16 sport);
extern void km_policy_expired(struct xfrm_policy *pol, int dir, int hard, u32 pid);

extern void xfrm_input_init(void);
extern int xfrm_parse_spi(struct sk_buff *skb, u8 nexthdr, u32 *spi, u32 *seq);

extern void xfrm_probe_algs(void);
extern int xfrm_count_auth_supported(void);
extern int xfrm_count_enc_supported(void);
extern struct xfrm_algo_desc *xfrm_aalg_get_byidx(unsigned int idx);
extern struct xfrm_algo_desc *xfrm_ealg_get_byidx(unsigned int idx);
extern struct xfrm_algo_desc *xfrm_aalg_get_byid(int alg_id);
extern struct xfrm_algo_desc *xfrm_ealg_get_byid(int alg_id);
extern struct xfrm_algo_desc *xfrm_calg_get_byid(int alg_id);
extern struct xfrm_algo_desc *xfrm_aalg_get_byname(char *name, int probe);
extern struct xfrm_algo_desc *xfrm_ealg_get_byname(char *name, int probe);
extern struct xfrm_algo_desc *xfrm_calg_get_byname(char *name, int probe);

struct crypto_tfm;
typedef void (icv_update_fn_t)(struct crypto_tfm *, struct scatterlist *, unsigned int);

extern void skb_icv_walk(const struct sk_buff *skb, struct crypto_tfm *tfm,
			 int offset, int len, icv_update_fn_t icv_update);

static inline int xfrm_addr_cmp(xfrm_address_t *a, xfrm_address_t *b,
				int family)
{
	switch (family) {
	default:
	case AF_INET:
		return a->a4 - b->a4;
	case AF_INET6:
		return ipv6_addr_cmp((struct in6_addr *)a,
				     (struct in6_addr *)b);
	}
}

static inline int xfrm_policy_id2dir(u32 index)
{
	return index & 7;
}

static inline int xfrm_aevent_is_on(void)
{
	struct sock *nlsk;
	int ret = 0;

	rcu_read_lock();
	nlsk = rcu_dereference(xfrm_nl);
	if (nlsk)
		ret = netlink_has_listeners(nlsk, XFRMNLGRP_AEVENTS);
	rcu_read_unlock();
	return ret;
}

static inline void xfrm_aevent_doreplay(struct xfrm_state *x)
{
	if (xfrm_aevent_is_on())
		xfrm_replay_notify(x, XFRM_REPLAY_UPDATE);
}


#endif	/* _NET_XFRM_H */
