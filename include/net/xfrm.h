#ifndef _NET_XFRM_H
#define _NET_XFRM_H

#include <linux/compiler.h>
#include <linux/xfrm.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/pfkeyv2.h>
#include <linux/ipsec.h>
#include <linux/in6.h>
#include <linux/mutex.h>
#include <linux/audit.h>

#include <net/sock.h>
#include <net/dst.h>
#include <net/ip.h>
#include <net/route.h>
#include <net/ipv6.h>
#include <net/ip6_fib.h>

#define XFRM_PROTO_ESP		50
#define XFRM_PROTO_AH		51
#define XFRM_PROTO_COMP		108
#define XFRM_PROTO_IPIP		4
#define XFRM_PROTO_IPV6		41
#define XFRM_PROTO_ROUTING	IPPROTO_ROUTING
#define XFRM_PROTO_DSTOPTS	IPPROTO_DSTOPTS

#define XFRM_ALIGN8(len)	(((len) + 7) & ~7)
#define MODULE_ALIAS_XFRM_MODE(family, encap) \
	MODULE_ALIAS("xfrm-mode-" __stringify(family) "-" __stringify(encap))
#define MODULE_ALIAS_XFRM_TYPE(family, proto) \
	MODULE_ALIAS("xfrm-type-" __stringify(family) "-" __stringify(proto))

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
	struct hlist_node	bydst;
	struct hlist_node	bysrc;
	struct hlist_node	byspi;

	atomic_t		refcnt;
	spinlock_t		lock;

	struct xfrm_id		id;
	struct xfrm_selector	sel;

	u32			genid;

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

	/* Data for care-of address */
	xfrm_address_t	*coaddr;

	/* IPComp needs an IPIP tunnel for handling uncompressed packets */
	struct xfrm_state	*tunnel;

	/* If a tunnel, number of users + 1 */
	atomic_t		tunnel_users;

	/* State for replay detection */
	struct xfrm_replay_state replay;

	/* Replay detection state at the time we sent the last notification */
	struct xfrm_replay_state preplay;

	/* internal flag that only holds state for delayed aevent at the
	 * moment
	*/
	u32			xflags;

	/* Replay detection notification settings */
	u32			replay_maxage;
	u32			replay_maxdiff;

	/* Replay detection notification timer */
	struct timer_list	rtimer;

	/* Statistics */
	struct xfrm_stats	stats;

	struct xfrm_lifetime_cur curlft;
	struct timer_list	timer;

	/* Last used time */
	u64			lastused;

	/* Reference to data common to all the instances of this
	 * transformer. */
	struct xfrm_type	*type;
	struct xfrm_mode	*inner_mode;
	struct xfrm_mode	*outer_mode;

	/* Security context */
	struct xfrm_sec_ctx	*security;

	/* Private data of this transformer, format is opaque,
	 * interpreted by xfrm_type methods. */
	void			*data;
};

/* xflags - make enum if more show up */
#define XFRM_TIME_DEFER	1

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
		u32 type;
	} data;

	u32	seq;
	u32	pid;
	u32	event;
};

struct xfrm_type;
struct xfrm_dst;
struct xfrm_policy_afinfo {
	unsigned short		family;
	struct dst_ops		*dst_ops;
	void			(*garbage_collect)(void);
	int			(*dst_lookup)(struct xfrm_dst **dst, struct flowi *fl);
	int			(*get_saddr)(xfrm_address_t *saddr, xfrm_address_t *daddr);
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

struct xfrm_tmpl;
extern int km_query(struct xfrm_state *x, struct xfrm_tmpl *t, struct xfrm_policy *pol);
extern void km_state_expired(struct xfrm_state *x, int hard, u32 pid);
extern int __xfrm_state_delete(struct xfrm_state *x);

struct xfrm_state_afinfo {
	unsigned int		family;
	struct module		*owner;
	struct xfrm_type	*type_map[IPPROTO_MAX];
	struct xfrm_mode	*mode_map[XFRM_MODE_MAX];
	int			(*init_flags)(struct xfrm_state *x);
	void			(*init_tempsel)(struct xfrm_state *x, struct flowi *fl,
						struct xfrm_tmpl *tmpl,
						xfrm_address_t *daddr, xfrm_address_t *saddr);
	int			(*tmpl_sort)(struct xfrm_tmpl **dst, struct xfrm_tmpl **src, int n);
	int			(*state_sort)(struct xfrm_state **dst, struct xfrm_state **src, int n);
	int			(*output)(struct sk_buff *skb);
};

extern int xfrm_state_register_afinfo(struct xfrm_state_afinfo *afinfo);
extern int xfrm_state_unregister_afinfo(struct xfrm_state_afinfo *afinfo);

extern void xfrm_state_delete_tunnel(struct xfrm_state *x);

struct xfrm_type
{
	char			*description;
	struct module		*owner;
	__u8			proto;
	__u8			flags;
#define XFRM_TYPE_NON_FRAGMENT	1
#define XFRM_TYPE_REPLAY_PROT	2

	int			(*init_state)(struct xfrm_state *x);
	void			(*destructor)(struct xfrm_state *);
	int			(*input)(struct xfrm_state *, struct sk_buff *skb);
	int			(*output)(struct xfrm_state *, struct sk_buff *pskb);
	int			(*reject)(struct xfrm_state *, struct sk_buff *, struct flowi *);
	int			(*hdr_offset)(struct xfrm_state *, struct sk_buff *, u8 **);
	xfrm_address_t		*(*local_addr)(struct xfrm_state *, xfrm_address_t *);
	xfrm_address_t		*(*remote_addr)(struct xfrm_state *, xfrm_address_t *);
	/* Estimate maximal size of result of transformation of a dgram */
	u32			(*get_mtu)(struct xfrm_state *, int size);
};

extern int xfrm_register_type(struct xfrm_type *type, unsigned short family);
extern int xfrm_unregister_type(struct xfrm_type *type, unsigned short family);

struct xfrm_mode {
	int (*input)(struct xfrm_state *x, struct sk_buff *skb);

	/*
	 * Add encapsulation header.
	 *
	 * On exit, the transport header will be set to the start of the
	 * encapsulation header to be filled in by x->type->output and
	 * the mac header will be set to the nextheader (protocol for
	 * IPv4) field of the extension header directly preceding the
	 * encapsulation header, or in its absence, that of the top IP
	 * header.  The value of the network header will always point
	 * to the top IP header while skb->data will point to the payload.
	 */
	int (*output)(struct xfrm_state *x,struct sk_buff *skb);

	struct xfrm_state_afinfo *afinfo;
	struct module *owner;
	unsigned int encap;
	int flags;
};

/* Flags for xfrm_mode. */
enum {
	XFRM_MODE_FLAG_TUNNEL = 1,
};

extern int xfrm_register_mode(struct xfrm_mode *mode, int family);
extern int xfrm_unregister_mode(struct xfrm_mode *mode, int family);

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

	unsigned short		encap_family;

	__u32			reqid;

/* Mode: transport, tunnel etc. */
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

#define XFRM_MAX_DEPTH		6

struct xfrm_policy
{
	struct xfrm_policy	*next;
	struct hlist_node	bydst;
	struct hlist_node	byidx;

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
	u16			family;
	u8			type;
	u8			action;
	u8			flags;
	u8			dead;
	u8			xfrm_nr;
	/* XXX 1 byte hole, try to pack */
	struct xfrm_sec_ctx	*security;
	struct xfrm_tmpl       	xfrm_vec[XFRM_MAX_DEPTH];
};

struct xfrm_migrate {
	xfrm_address_t		old_daddr;
	xfrm_address_t		old_saddr;
	xfrm_address_t		new_daddr;
	xfrm_address_t		new_saddr;
	u8			proto;
	u8			mode;
	u16			reserved;
	u32			reqid;
	u16			old_family;
	u16			new_family;
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
	struct xfrm_policy	*(*compile_policy)(struct sock *sk, int opt, u8 *data, int len, int *dir);
	int			(*new_mapping)(struct xfrm_state *x, xfrm_address_t *ipaddr, __be16 sport);
	int			(*notify_policy)(struct xfrm_policy *x, int dir, struct km_event *c);
	int			(*report)(u8 proto, struct xfrm_selector *sel, xfrm_address_t *addr);
	int			(*migrate)(struct xfrm_selector *sel, u8 dir, u8 type, struct xfrm_migrate *m, int num_bundles);
};

extern int xfrm_register_km(struct xfrm_mgr *km);
extern int xfrm_unregister_km(struct xfrm_mgr *km);

extern unsigned int xfrm_policy_count[XFRM_POLICY_MAX*2];

/*
 * This structure is used for the duration where packets are being
 * transformed by IPsec.  As soon as the packet leaves IPsec the
 * area beyond the generic IP part may be overwritten.
 */
struct xfrm_skb_cb {
	union {
		struct inet_skb_parm h4;
		struct inet6_skb_parm h6;
        } header;

        /* Sequence number for replay protection. */
        u64 seq;
};

#define XFRM_SKB_CB(__skb) ((struct xfrm_skb_cb *)&((__skb)->cb[0]))

/* Audit Information */
struct xfrm_audit
{
	u32	loginuid;
	u32	secid;
};

#ifdef CONFIG_AUDITSYSCALL
static inline struct audit_buffer *xfrm_audit_start(u32 auid, u32 sid)
{
	struct audit_buffer *audit_buf = NULL;
	char *secctx;
	u32 secctx_len;

	audit_buf = audit_log_start(current->audit_context, GFP_ATOMIC,
			      AUDIT_MAC_IPSEC_EVENT);
	if (audit_buf == NULL)
		return NULL;

	audit_log_format(audit_buf, "auid=%u", auid);

	if (sid != 0 &&
	    security_secid_to_secctx(sid, &secctx, &secctx_len) == 0) {
		audit_log_format(audit_buf, " subj=%s", secctx);
		security_release_secctx(secctx, secctx_len);
	} else
		audit_log_task_context(audit_buf);
	return audit_buf;
}

extern void xfrm_audit_policy_add(struct xfrm_policy *xp, int result,
				  u32 auid, u32 sid);
extern void xfrm_audit_policy_delete(struct xfrm_policy *xp, int result,
				  u32 auid, u32 sid);
extern void xfrm_audit_state_add(struct xfrm_state *x, int result,
				 u32 auid, u32 sid);
extern void xfrm_audit_state_delete(struct xfrm_state *x, int result,
				    u32 auid, u32 sid);
#else
#define xfrm_audit_policy_add(x, r, a, s)	do { ; } while (0)
#define xfrm_audit_policy_delete(x, r, a, s)	do { ; } while (0)
#define xfrm_audit_state_add(x, r, a, s)	do { ; } while (0)
#define xfrm_audit_state_delete(x, r, a, s)	do { ; } while (0)
#endif /* CONFIG_AUDITSYSCALL */

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

#ifdef CONFIG_XFRM_SUB_POLICY
static inline void xfrm_pols_put(struct xfrm_policy **pols, int npols)
{
	int i;
	for (i = npols - 1; i >= 0; --i)
		xfrm_pol_put(pols[i]);
}
#else
static inline void xfrm_pols_put(struct xfrm_policy **pols, int npols)
{
	xfrm_pol_put(pols[0]);
}
#endif

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
	__be32 *a1 = token1;
	__be32 *a2 = token2;
	int pdw;
	int pbi;

	pdw = prefixlen >> 5;	  /* num of whole __u32 in prefix */
	pbi = prefixlen &  0x1f;  /* num of bits in incomplete u32 in prefix */

	if (pdw)
		if (memcmp(a1, a2, pdw << 2))
			return 0;

	if (pbi) {
		__be32 mask;

		mask = htonl((0xffffffff) << (32 - pbi));

		if ((a1[pdw] ^ a2[pdw]) & mask)
			return 0;
	}

	return 1;
}

static __inline__
__be16 xfrm_flowi_sport(struct flowi *fl)
{
	__be16 port;
	switch(fl->proto) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
	case IPPROTO_UDPLITE:
	case IPPROTO_SCTP:
		port = fl->fl_ip_sport;
		break;
	case IPPROTO_ICMP:
	case IPPROTO_ICMPV6:
		port = htons(fl->fl_icmp_type);
		break;
	case IPPROTO_MH:
		port = htons(fl->fl_mh_type);
		break;
	default:
		port = 0;	/*XXX*/
	}
	return port;
}

static __inline__
__be16 xfrm_flowi_dport(struct flowi *fl)
{
	__be16 port;
	switch(fl->proto) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
	case IPPROTO_UDPLITE:
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

extern int xfrm_selector_match(struct xfrm_selector *sel, struct flowi *fl,
			       unsigned short family);

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
		struct dst_entry	dst;
		struct rtable		rt;
		struct rt6_info		rt6;
	} u;
	struct dst_entry *route;
#ifdef CONFIG_XFRM_SUB_POLICY
	struct flowi *origin;
	struct xfrm_selector *partner;
#endif
	u32 genid;
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
#ifdef CONFIG_XFRM_SUB_POLICY
	kfree(xdst->origin);
	xdst->origin = NULL;
	kfree(xdst->partner);
	xdst->partner = NULL;
#endif
}

extern void xfrm_dst_ifdown(struct dst_entry *dst, struct net_device *dev);

struct sec_path
{
	atomic_t		refcnt;
	int			len;
	struct xfrm_state	*xvec[XFRM_MAX_DEPTH];
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
xfrm_addr_any(xfrm_address_t *addr, unsigned short family)
{
	switch (family) {
	case AF_INET:
		return addr->a4 == 0;
	case AF_INET6:
		return ipv6_addr_any((struct in6_addr *)&addr->a6);
	}
	return 0;
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

	return	(!xfrm_policy_count[dir] && !skb->sp) ||
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
	return	!xfrm_policy_count[XFRM_POLICY_OUT] ||
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

static __inline__ int
xfrm_state_addr_flow_check(struct xfrm_state *x, struct flowi *fl,
			   unsigned short family)
{
	switch (family) {
	case AF_INET:
		return __xfrm4_state_addr_check(x,
						(xfrm_address_t *)&fl->fl4_dst,
						(xfrm_address_t *)&fl->fl4_src);
	case AF_INET6:
		return __xfrm6_state_addr_check(x,
						(xfrm_address_t *)&fl->fl6_dst,
						(xfrm_address_t *)&fl->fl6_src);
	}
	return 0;
}

static inline int xfrm_state_kern(struct xfrm_state *x)
{
	return atomic_read(&x->tunnel_users);
}

static inline int xfrm_id_proto_match(u8 proto, u8 userproto)
{
	return (!userproto || proto == userproto ||
		(userproto == IPSEC_PROTO_ANY && (proto == IPPROTO_AH ||
						  proto == IPPROTO_ESP ||
						  proto == IPPROTO_COMP)));
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
	char *compat;
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
			   int type, int code, int offset, __be32 info);
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
extern struct xfrm_state * xfrm_stateonly_find(xfrm_address_t *daddr,
					       xfrm_address_t *saddr,
					       unsigned short family,
					       u8 mode, u8 proto, u32 reqid);
extern int xfrm_state_check_expire(struct xfrm_state *x);
extern void xfrm_state_insert(struct xfrm_state *x);
extern int xfrm_state_add(struct xfrm_state *x);
extern int xfrm_state_update(struct xfrm_state *x);
extern struct xfrm_state *xfrm_state_lookup(xfrm_address_t *daddr, __be32 spi, u8 proto, unsigned short family);
extern struct xfrm_state *xfrm_state_lookup_byaddr(xfrm_address_t *daddr, xfrm_address_t *saddr, u8 proto, unsigned short family);
#ifdef CONFIG_XFRM_SUB_POLICY
extern int xfrm_tmpl_sort(struct xfrm_tmpl **dst, struct xfrm_tmpl **src,
			  int n, unsigned short family);
extern int xfrm_state_sort(struct xfrm_state **dst, struct xfrm_state **src,
			   int n, unsigned short family);
#else
static inline int xfrm_tmpl_sort(struct xfrm_tmpl **dst, struct xfrm_tmpl **src,
				 int n, unsigned short family)
{
	return -ENOSYS;
}

static inline int xfrm_state_sort(struct xfrm_state **dst, struct xfrm_state **src,
				  int n, unsigned short family)
{
	return -ENOSYS;
}
#endif

struct xfrmk_sadinfo {
	u32 sadhcnt; /* current hash bkts */
	u32 sadhmcnt; /* max allowed hash bkts */
	u32 sadcnt; /* current running count */
};

struct xfrmk_spdinfo {
	u32 incnt;
	u32 outcnt;
	u32 fwdcnt;
	u32 inscnt;
	u32 outscnt;
	u32 fwdscnt;
	u32 spdhcnt;
	u32 spdhmcnt;
};

extern struct xfrm_state *xfrm_find_acq_byseq(u32 seq);
extern int xfrm_state_delete(struct xfrm_state *x);
extern int xfrm_state_flush(u8 proto, struct xfrm_audit *audit_info);
extern void xfrm_sad_getinfo(struct xfrmk_sadinfo *si);
extern void xfrm_spd_getinfo(struct xfrmk_spdinfo *si);
extern int xfrm_replay_check(struct xfrm_state *x, __be32 seq);
extern void xfrm_replay_advance(struct xfrm_state *x, __be32 seq);
extern void xfrm_replay_notify(struct xfrm_state *x, int event);
extern int xfrm_state_mtu(struct xfrm_state *x, int mtu);
extern int xfrm_init_state(struct xfrm_state *x);
extern int xfrm_output(struct sk_buff *skb);
extern int xfrm4_rcv_encap(struct sk_buff *skb, int nexthdr, __be32 spi,
			   int encap_type);
extern int xfrm4_rcv(struct sk_buff *skb);

static inline int xfrm4_rcv_spi(struct sk_buff *skb, int nexthdr, __be32 spi)
{
	return xfrm4_rcv_encap(skb, nexthdr, spi, 0);
}

extern int xfrm4_output(struct sk_buff *skb);
extern int xfrm4_tunnel_register(struct xfrm_tunnel *handler, unsigned short family);
extern int xfrm4_tunnel_deregister(struct xfrm_tunnel *handler, unsigned short family);
extern int xfrm6_rcv_spi(struct sk_buff *skb, int nexthdr, __be32 spi);
extern int xfrm6_rcv(struct sk_buff *skb);
extern int xfrm6_input_addr(struct sk_buff *skb, xfrm_address_t *daddr,
			    xfrm_address_t *saddr, u8 proto);
extern int xfrm6_tunnel_register(struct xfrm6_tunnel *handler, unsigned short family);
extern int xfrm6_tunnel_deregister(struct xfrm6_tunnel *handler, unsigned short family);
extern __be32 xfrm6_tunnel_alloc_spi(xfrm_address_t *saddr);
extern void xfrm6_tunnel_free_spi(xfrm_address_t *saddr);
extern __be32 xfrm6_tunnel_spi_lookup(xfrm_address_t *saddr);
extern int xfrm6_output(struct sk_buff *skb);
extern int xfrm6_find_1stfragopt(struct xfrm_state *x, struct sk_buff *skb,
				 u8 **prevhdr);

#ifdef CONFIG_XFRM
extern int xfrm4_udp_encap_rcv(struct sock *sk, struct sk_buff *skb);
extern int xfrm_user_policy(struct sock *sk, int optname, u8 __user *optval, int optlen);
extern int xfrm_dst_lookup(struct xfrm_dst **dst, struct flowi *fl, unsigned short family);
#else
static inline int xfrm_user_policy(struct sock *sk, int optname, u8 __user *optval, int optlen)
{
 	return -ENOPROTOOPT;
} 

static inline int xfrm4_udp_encap_rcv(struct sock *sk, struct sk_buff *skb)
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
extern int xfrm_policy_walk(u8 type, int (*func)(struct xfrm_policy *, int, int, void*), void *);
int xfrm_policy_insert(int dir, struct xfrm_policy *policy, int excl);
struct xfrm_policy *xfrm_policy_bysel_ctx(u8 type, int dir,
					  struct xfrm_selector *sel,
					  struct xfrm_sec_ctx *ctx, int delete,
					  int *err);
struct xfrm_policy *xfrm_policy_byid(u8, int dir, u32 id, int delete, int *err);
int xfrm_policy_flush(u8 type, struct xfrm_audit *audit_info);
u32 xfrm_get_acqseq(void);
extern int xfrm_alloc_spi(struct xfrm_state *x, u32 minspi, u32 maxspi);
struct xfrm_state * xfrm_find_acq(u8 mode, u32 reqid, u8 proto,
				  xfrm_address_t *daddr, xfrm_address_t *saddr,
				  int create, unsigned short family);
extern int xfrm_policy_flush(u8 type, struct xfrm_audit *audit_info);
extern int xfrm_sk_policy_insert(struct sock *sk, int dir, struct xfrm_policy *pol);
extern int xfrm_bundle_ok(struct xfrm_policy *pol, struct xfrm_dst *xdst,
			  struct flowi *fl, int family, int strict);
extern void xfrm_init_pmtu(struct dst_entry *dst);

#ifdef CONFIG_XFRM_MIGRATE
extern int km_migrate(struct xfrm_selector *sel, u8 dir, u8 type,
		      struct xfrm_migrate *m, int num_bundles);
extern struct xfrm_state * xfrm_migrate_state_find(struct xfrm_migrate *m);
extern struct xfrm_state * xfrm_state_migrate(struct xfrm_state *x,
					      struct xfrm_migrate *m);
extern int xfrm_migrate(struct xfrm_selector *sel, u8 dir, u8 type,
			struct xfrm_migrate *m, int num_bundles);
#endif

extern wait_queue_head_t km_waitq;
extern int km_new_mapping(struct xfrm_state *x, xfrm_address_t *ipaddr, __be16 sport);
extern void km_policy_expired(struct xfrm_policy *pol, int dir, int hard, u32 pid);
extern int km_report(u8 proto, struct xfrm_selector *sel, xfrm_address_t *addr);

extern void xfrm_input_init(void);
extern int xfrm_parse_spi(struct sk_buff *skb, u8 nexthdr, __be32 *spi, __be32 *seq);

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

struct hash_desc;
struct scatterlist;
typedef int (icv_update_fn_t)(struct hash_desc *, struct scatterlist *,
			      unsigned int);

extern int skb_icv_walk(const struct sk_buff *skb, struct hash_desc *tfm,
			int offset, int len, icv_update_fn_t icv_update);

static inline int xfrm_addr_cmp(xfrm_address_t *a, xfrm_address_t *b,
				int family)
{
	switch (family) {
	default:
	case AF_INET:
		return (__force __u32)a->a4 - (__force __u32)b->a4;
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

#ifdef CONFIG_XFRM_MIGRATE
static inline struct xfrm_algo *xfrm_algo_clone(struct xfrm_algo *orig)
{
	return (struct xfrm_algo *)kmemdup(orig, sizeof(*orig) + orig->alg_key_len, GFP_KERNEL);
}

static inline void xfrm_states_put(struct xfrm_state **states, int n)
{
	int i;
	for (i = 0; i < n; i++)
		xfrm_state_put(*(states + i));
}

static inline void xfrm_states_delete(struct xfrm_state **states, int n)
{
	int i;
	for (i = 0; i < n; i++)
		xfrm_state_delete(*(states + i));
}
#endif

#endif	/* _NET_XFRM_H */
