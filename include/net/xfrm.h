/* SPDX-License-Identifier: GPL-2.0 */
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
#include <linux/slab.h>
#include <linux/refcount.h>
#include <linux/sockptr.h>

#include <net/sock.h>
#include <net/dst.h>
#include <net/ip.h>
#include <net/route.h>
#include <net/ipv6.h>
#include <net/ip6_fib.h>
#include <net/flow.h>
#include <net/gro_cells.h>

#include <linux/interrupt.h>

#ifdef CONFIG_XFRM_STATISTICS
#include <net/snmp.h>
#endif

#define XFRM_PROTO_ESP		50
#define XFRM_PROTO_AH		51
#define XFRM_PROTO_COMP		108
#define XFRM_PROTO_IPIP		4
#define XFRM_PROTO_IPV6		41
#define XFRM_PROTO_ROUTING	IPPROTO_ROUTING
#define XFRM_PROTO_DSTOPTS	IPPROTO_DSTOPTS

#define XFRM_ALIGN4(len)	(((len) + 3) & ~3)
#define XFRM_ALIGN8(len)	(((len) + 7) & ~7)
#define MODULE_ALIAS_XFRM_MODE(family, encap) \
	MODULE_ALIAS("xfrm-mode-" __stringify(family) "-" __stringify(encap))
#define MODULE_ALIAS_XFRM_TYPE(family, proto) \
	MODULE_ALIAS("xfrm-type-" __stringify(family) "-" __stringify(proto))
#define MODULE_ALIAS_XFRM_OFFLOAD_TYPE(family, proto) \
	MODULE_ALIAS("xfrm-offload-" __stringify(family) "-" __stringify(proto))

#ifdef CONFIG_XFRM_STATISTICS
#define XFRM_INC_STATS(net, field)	SNMP_INC_STATS((net)->mib.xfrm_statistics, field)
#else
#define XFRM_INC_STATS(net, field)	((void)(net))
#endif


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

struct xfrm_state_walk {
	struct list_head	all;
	u8			state;
	u8			dying;
	u8			proto;
	u32			seq;
	struct xfrm_address_filter *filter;
};

struct xfrm_state_offload {
	struct net_device	*dev;
	netdevice_tracker	dev_tracker;
	struct net_device	*real_dev;
	unsigned long		offload_handle;
	unsigned int		num_exthdrs;
	u8			flags;
};

struct xfrm_mode {
	u8 encap;
	u8 family;
	u8 flags;
};

/* Flags for xfrm_mode. */
enum {
	XFRM_MODE_FLAG_TUNNEL = 1,
};

enum xfrm_replay_mode {
	XFRM_REPLAY_MODE_LEGACY,
	XFRM_REPLAY_MODE_BMP,
	XFRM_REPLAY_MODE_ESN,
};

/* Full description of state of transformer. */
struct xfrm_state {
	possible_net_t		xs_net;
	union {
		struct hlist_node	gclist;
		struct hlist_node	bydst;
	};
	struct hlist_node	bysrc;
	struct hlist_node	byspi;
	struct hlist_node	byseq;

	refcount_t		refcnt;
	spinlock_t		lock;

	struct xfrm_id		id;
	struct xfrm_selector	sel;
	struct xfrm_mark	mark;
	u32			if_id;
	u32			tfcpad;

	u32			genid;

	/* Key manager bits */
	struct xfrm_state_walk	km;

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
		u32		extra_flags;
		struct xfrm_mark	smark;
	} props;

	struct xfrm_lifetime_cfg lft;

	/* Data for transformer */
	struct xfrm_algo_auth	*aalg;
	struct xfrm_algo	*ealg;
	struct xfrm_algo	*calg;
	struct xfrm_algo_aead	*aead;
	const char		*geniv;

	/* mapping change rate limiting */
	__be16 new_mapping_sport;
	u32 new_mapping;	/* seconds */
	u32 mapping_maxage;	/* seconds for input SA */

	/* Data for encapsulator */
	struct xfrm_encap_tmpl	*encap;
	struct sock __rcu	*encap_sk;

	/* Data for care-of address */
	xfrm_address_t	*coaddr;

	/* IPComp needs an IPIP tunnel for handling uncompressed packets */
	struct xfrm_state	*tunnel;

	/* If a tunnel, number of users + 1 */
	atomic_t		tunnel_users;

	/* State for replay detection */
	struct xfrm_replay_state replay;
	struct xfrm_replay_state_esn *replay_esn;

	/* Replay detection state at the time we sent the last notification */
	struct xfrm_replay_state preplay;
	struct xfrm_replay_state_esn *preplay_esn;

	/* replay detection mode */
	enum xfrm_replay_mode    repl_mode;
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
	struct hrtimer		mtimer;

	struct xfrm_state_offload xso;

	/* used to fix curlft->add_time when changing date */
	long		saved_tmo;

	/* Last used time */
	time64_t		lastused;

	struct page_frag xfrag;

	/* Reference to data common to all the instances of this
	 * transformer. */
	const struct xfrm_type	*type;
	struct xfrm_mode	inner_mode;
	struct xfrm_mode	inner_mode_iaf;
	struct xfrm_mode	outer_mode;

	const struct xfrm_type_offload	*type_offload;

	/* Security context */
	struct xfrm_sec_ctx	*security;

	/* Private data of this transformer, format is opaque,
	 * interpreted by xfrm_type methods. */
	void			*data;
};

static inline struct net *xs_net(struct xfrm_state *x)
{
	return read_pnet(&x->xs_net);
}

/* xflags - make enum if more show up */
#define XFRM_TIME_DEFER	1
#define XFRM_SOFT_EXPIRE 2

enum {
	XFRM_STATE_VOID,
	XFRM_STATE_ACQ,
	XFRM_STATE_VALID,
	XFRM_STATE_ERROR,
	XFRM_STATE_EXPIRED,
	XFRM_STATE_DEAD
};

/* callback structure passed from either netlink or pfkey */
struct km_event {
	union {
		u32 hard;
		u32 proto;
		u32 byid;
		u32 aevent;
		u32 type;
	} data;

	u32	seq;
	u32	portid;
	u32	event;
	struct net *net;
};

struct xfrm_if_cb {
	struct xfrm_if	*(*decode_session)(struct sk_buff *skb,
					   unsigned short family);
};

void xfrm_if_register_cb(const struct xfrm_if_cb *ifcb);
void xfrm_if_unregister_cb(void);

struct net_device;
struct xfrm_type;
struct xfrm_dst;
struct xfrm_policy_afinfo {
	struct dst_ops		*dst_ops;
	struct dst_entry	*(*dst_lookup)(struct net *net,
					       int tos, int oif,
					       const xfrm_address_t *saddr,
					       const xfrm_address_t *daddr,
					       u32 mark);
	int			(*get_saddr)(struct net *net, int oif,
					     xfrm_address_t *saddr,
					     xfrm_address_t *daddr,
					     u32 mark);
	int			(*fill_dst)(struct xfrm_dst *xdst,
					    struct net_device *dev,
					    const struct flowi *fl);
	struct dst_entry	*(*blackhole_route)(struct net *net, struct dst_entry *orig);
};

int xfrm_policy_register_afinfo(const struct xfrm_policy_afinfo *afinfo, int family);
void xfrm_policy_unregister_afinfo(const struct xfrm_policy_afinfo *afinfo);
void km_policy_notify(struct xfrm_policy *xp, int dir,
		      const struct km_event *c);
void km_state_notify(struct xfrm_state *x, const struct km_event *c);

struct xfrm_tmpl;
int km_query(struct xfrm_state *x, struct xfrm_tmpl *t,
	     struct xfrm_policy *pol);
void km_state_expired(struct xfrm_state *x, int hard, u32 portid);
int __xfrm_state_delete(struct xfrm_state *x);

struct xfrm_state_afinfo {
	u8				family;
	u8				proto;

	const struct xfrm_type_offload *type_offload_esp;

	const struct xfrm_type		*type_esp;
	const struct xfrm_type		*type_ipip;
	const struct xfrm_type		*type_ipip6;
	const struct xfrm_type		*type_comp;
	const struct xfrm_type		*type_ah;
	const struct xfrm_type		*type_routing;
	const struct xfrm_type		*type_dstopts;

	int			(*output)(struct net *net, struct sock *sk, struct sk_buff *skb);
	int			(*transport_finish)(struct sk_buff *skb,
						    int async);
	void			(*local_error)(struct sk_buff *skb, u32 mtu);
};

int xfrm_state_register_afinfo(struct xfrm_state_afinfo *afinfo);
int xfrm_state_unregister_afinfo(struct xfrm_state_afinfo *afinfo);
struct xfrm_state_afinfo *xfrm_state_get_afinfo(unsigned int family);
struct xfrm_state_afinfo *xfrm_state_afinfo_get_rcu(unsigned int family);

struct xfrm_input_afinfo {
	u8			family;
	bool			is_ipip;
	int			(*callback)(struct sk_buff *skb, u8 protocol,
					    int err);
};

int xfrm_input_register_afinfo(const struct xfrm_input_afinfo *afinfo);
int xfrm_input_unregister_afinfo(const struct xfrm_input_afinfo *afinfo);

void xfrm_flush_gc(void);
void xfrm_state_delete_tunnel(struct xfrm_state *x);

struct xfrm_type {
	struct module		*owner;
	u8			proto;
	u8			flags;
#define XFRM_TYPE_NON_FRAGMENT	1
#define XFRM_TYPE_REPLAY_PROT	2
#define XFRM_TYPE_LOCAL_COADDR	4
#define XFRM_TYPE_REMOTE_COADDR	8

	int			(*init_state)(struct xfrm_state *x);
	void			(*destructor)(struct xfrm_state *);
	int			(*input)(struct xfrm_state *, struct sk_buff *skb);
	int			(*output)(struct xfrm_state *, struct sk_buff *pskb);
	int			(*reject)(struct xfrm_state *, struct sk_buff *,
					  const struct flowi *);
};

int xfrm_register_type(const struct xfrm_type *type, unsigned short family);
void xfrm_unregister_type(const struct xfrm_type *type, unsigned short family);

struct xfrm_type_offload {
	struct module	*owner;
	u8		proto;
	void		(*encap)(struct xfrm_state *, struct sk_buff *pskb);
	int		(*input_tail)(struct xfrm_state *x, struct sk_buff *skb);
	int		(*xmit)(struct xfrm_state *, struct sk_buff *pskb, netdev_features_t features);
};

int xfrm_register_type_offload(const struct xfrm_type_offload *type, unsigned short family);
void xfrm_unregister_type_offload(const struct xfrm_type_offload *type, unsigned short family);

static inline int xfrm_af2proto(unsigned int family)
{
	switch(family) {
	case AF_INET:
		return IPPROTO_IPIP;
	case AF_INET6:
		return IPPROTO_IPV6;
	default:
		return 0;
	}
}

static inline const struct xfrm_mode *xfrm_ip2inner_mode(struct xfrm_state *x, int ipproto)
{
	if ((ipproto == IPPROTO_IPIP && x->props.family == AF_INET) ||
	    (ipproto == IPPROTO_IPV6 && x->props.family == AF_INET6))
		return &x->inner_mode;
	else
		return &x->inner_mode_iaf;
}

struct xfrm_tmpl {
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

	u32			reqid;

/* Mode: transport, tunnel etc. */
	u8			mode;

/* Sharing mode: unique, this session only, this user only etc. */
	u8			share;

/* May skip this transfomration if no SA is found */
	u8			optional;

/* Skip aalgos/ealgos/calgos checks. */
	u8			allalgs;

/* Bit mask of algos allowed for acquisition */
	u32			aalgos;
	u32			ealgos;
	u32			calgos;
};

#define XFRM_MAX_DEPTH		6
#define XFRM_MAX_OFFLOAD_DEPTH	1

struct xfrm_policy_walk_entry {
	struct list_head	all;
	u8			dead;
};

struct xfrm_policy_walk {
	struct xfrm_policy_walk_entry walk;
	u8 type;
	u32 seq;
};

struct xfrm_policy_queue {
	struct sk_buff_head	hold_queue;
	struct timer_list	hold_timer;
	unsigned long		timeout;
};

struct xfrm_policy {
	possible_net_t		xp_net;
	struct hlist_node	bydst;
	struct hlist_node	byidx;

	/* This lock only affects elements except for entry. */
	rwlock_t		lock;
	refcount_t		refcnt;
	u32			pos;
	struct timer_list	timer;

	atomic_t		genid;
	u32			priority;
	u32			index;
	u32			if_id;
	struct xfrm_mark	mark;
	struct xfrm_selector	selector;
	struct xfrm_lifetime_cfg lft;
	struct xfrm_lifetime_cur curlft;
	struct xfrm_policy_walk_entry walk;
	struct xfrm_policy_queue polq;
	bool                    bydst_reinsert;
	u8			type;
	u8			action;
	u8			flags;
	u8			xfrm_nr;
	u16			family;
	struct xfrm_sec_ctx	*security;
	struct xfrm_tmpl       	xfrm_vec[XFRM_MAX_DEPTH];
	struct hlist_node	bydst_inexact_list;
	struct rcu_head		rcu;
};

static inline struct net *xp_net(const struct xfrm_policy *xp)
{
	return read_pnet(&xp->xp_net);
}

struct xfrm_kmaddress {
	xfrm_address_t          local;
	xfrm_address_t          remote;
	u32			reserved;
	u16			family;
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
/* what happened */
#define XFRM_REPLAY_UPDATE	XFRM_AE_CR
#define XFRM_REPLAY_TIMEOUT	XFRM_AE_CE

/* default aevent timeout in units of 100ms */
#define XFRM_AE_ETIME			10
/* Async Event timer multiplier */
#define XFRM_AE_ETH_M			10
/* default seq threshold size */
#define XFRM_AE_SEQT_SIZE		2

struct xfrm_mgr {
	struct list_head	list;
	int			(*notify)(struct xfrm_state *x, const struct km_event *c);
	int			(*acquire)(struct xfrm_state *x, struct xfrm_tmpl *, struct xfrm_policy *xp);
	struct xfrm_policy	*(*compile_policy)(struct sock *sk, int opt, u8 *data, int len, int *dir);
	int			(*new_mapping)(struct xfrm_state *x, xfrm_address_t *ipaddr, __be16 sport);
	int			(*notify_policy)(struct xfrm_policy *x, int dir, const struct km_event *c);
	int			(*report)(struct net *net, u8 proto, struct xfrm_selector *sel, xfrm_address_t *addr);
	int			(*migrate)(const struct xfrm_selector *sel,
					   u8 dir, u8 type,
					   const struct xfrm_migrate *m,
					   int num_bundles,
					   const struct xfrm_kmaddress *k,
					   const struct xfrm_encap_tmpl *encap);
	bool			(*is_alive)(const struct km_event *c);
};

int xfrm_register_km(struct xfrm_mgr *km);
int xfrm_unregister_km(struct xfrm_mgr *km);

struct xfrm_tunnel_skb_cb {
	union {
		struct inet_skb_parm h4;
		struct inet6_skb_parm h6;
	} header;

	union {
		struct ip_tunnel *ip4;
		struct ip6_tnl *ip6;
	} tunnel;
};

#define XFRM_TUNNEL_SKB_CB(__skb) ((struct xfrm_tunnel_skb_cb *)&((__skb)->cb[0]))

/*
 * This structure is used for the duration where packets are being
 * transformed by IPsec.  As soon as the packet leaves IPsec the
 * area beyond the generic IP part may be overwritten.
 */
struct xfrm_skb_cb {
	struct xfrm_tunnel_skb_cb header;

        /* Sequence number for replay protection. */
	union {
		struct {
			__u32 low;
			__u32 hi;
		} output;
		struct {
			__be32 low;
			__be32 hi;
		} input;
	} seq;
};

#define XFRM_SKB_CB(__skb) ((struct xfrm_skb_cb *)&((__skb)->cb[0]))

/*
 * This structure is used by the afinfo prepare_input/prepare_output functions
 * to transmit header information to the mode input/output functions.
 */
struct xfrm_mode_skb_cb {
	struct xfrm_tunnel_skb_cb header;

	/* Copied from header for IPv4, always set to zero and DF for IPv6. */
	__be16 id;
	__be16 frag_off;

	/* IP header length (excluding options or extension headers). */
	u8 ihl;

	/* TOS for IPv4, class for IPv6. */
	u8 tos;

	/* TTL for IPv4, hop limitfor IPv6. */
	u8 ttl;

	/* Protocol for IPv4, NH for IPv6. */
	u8 protocol;

	/* Option length for IPv4, zero for IPv6. */
	u8 optlen;

	/* Used by IPv6 only, zero for IPv4. */
	u8 flow_lbl[3];
};

#define XFRM_MODE_SKB_CB(__skb) ((struct xfrm_mode_skb_cb *)&((__skb)->cb[0]))

/*
 * This structure is used by the input processing to locate the SPI and
 * related information.
 */
struct xfrm_spi_skb_cb {
	struct xfrm_tunnel_skb_cb header;

	unsigned int daddroff;
	unsigned int family;
	__be32 seq;
};

#define XFRM_SPI_SKB_CB(__skb) ((struct xfrm_spi_skb_cb *)&((__skb)->cb[0]))

#ifdef CONFIG_AUDITSYSCALL
static inline struct audit_buffer *xfrm_audit_start(const char *op)
{
	struct audit_buffer *audit_buf = NULL;

	if (audit_enabled == AUDIT_OFF)
		return NULL;
	audit_buf = audit_log_start(audit_context(), GFP_ATOMIC,
				    AUDIT_MAC_IPSEC_EVENT);
	if (audit_buf == NULL)
		return NULL;
	audit_log_format(audit_buf, "op=%s", op);
	return audit_buf;
}

static inline void xfrm_audit_helper_usrinfo(bool task_valid,
					     struct audit_buffer *audit_buf)
{
	const unsigned int auid = from_kuid(&init_user_ns, task_valid ?
					    audit_get_loginuid(current) :
					    INVALID_UID);
	const unsigned int ses = task_valid ? audit_get_sessionid(current) :
		AUDIT_SID_UNSET;

	audit_log_format(audit_buf, " auid=%u ses=%u", auid, ses);
	audit_log_task_context(audit_buf);
}

void xfrm_audit_policy_add(struct xfrm_policy *xp, int result, bool task_valid);
void xfrm_audit_policy_delete(struct xfrm_policy *xp, int result,
			      bool task_valid);
void xfrm_audit_state_add(struct xfrm_state *x, int result, bool task_valid);
void xfrm_audit_state_delete(struct xfrm_state *x, int result, bool task_valid);
void xfrm_audit_state_replay_overflow(struct xfrm_state *x,
				      struct sk_buff *skb);
void xfrm_audit_state_replay(struct xfrm_state *x, struct sk_buff *skb,
			     __be32 net_seq);
void xfrm_audit_state_notfound_simple(struct sk_buff *skb, u16 family);
void xfrm_audit_state_notfound(struct sk_buff *skb, u16 family, __be32 net_spi,
			       __be32 net_seq);
void xfrm_audit_state_icvfail(struct xfrm_state *x, struct sk_buff *skb,
			      u8 proto);
#else

static inline void xfrm_audit_policy_add(struct xfrm_policy *xp, int result,
					 bool task_valid)
{
}

static inline void xfrm_audit_policy_delete(struct xfrm_policy *xp, int result,
					    bool task_valid)
{
}

static inline void xfrm_audit_state_add(struct xfrm_state *x, int result,
					bool task_valid)
{
}

static inline void xfrm_audit_state_delete(struct xfrm_state *x, int result,
					   bool task_valid)
{
}

static inline void xfrm_audit_state_replay_overflow(struct xfrm_state *x,
					     struct sk_buff *skb)
{
}

static inline void xfrm_audit_state_replay(struct xfrm_state *x,
					   struct sk_buff *skb, __be32 net_seq)
{
}

static inline void xfrm_audit_state_notfound_simple(struct sk_buff *skb,
				      u16 family)
{
}

static inline void xfrm_audit_state_notfound(struct sk_buff *skb, u16 family,
				      __be32 net_spi, __be32 net_seq)
{
}

static inline void xfrm_audit_state_icvfail(struct xfrm_state *x,
				     struct sk_buff *skb, u8 proto)
{
}
#endif /* CONFIG_AUDITSYSCALL */

static inline void xfrm_pol_hold(struct xfrm_policy *policy)
{
	if (likely(policy != NULL))
		refcount_inc(&policy->refcnt);
}

void xfrm_policy_destroy(struct xfrm_policy *policy);

static inline void xfrm_pol_put(struct xfrm_policy *policy)
{
	if (refcount_dec_and_test(&policy->refcnt))
		xfrm_policy_destroy(policy);
}

static inline void xfrm_pols_put(struct xfrm_policy **pols, int npols)
{
	int i;
	for (i = npols - 1; i >= 0; --i)
		xfrm_pol_put(pols[i]);
}

void __xfrm_state_destroy(struct xfrm_state *, bool);

static inline void __xfrm_state_put(struct xfrm_state *x)
{
	refcount_dec(&x->refcnt);
}

static inline void xfrm_state_put(struct xfrm_state *x)
{
	if (refcount_dec_and_test(&x->refcnt))
		__xfrm_state_destroy(x, false);
}

static inline void xfrm_state_put_sync(struct xfrm_state *x)
{
	if (refcount_dec_and_test(&x->refcnt))
		__xfrm_state_destroy(x, true);
}

static inline void xfrm_state_hold(struct xfrm_state *x)
{
	refcount_inc(&x->refcnt);
}

static inline bool addr_match(const void *token1, const void *token2,
			      unsigned int prefixlen)
{
	const __be32 *a1 = token1;
	const __be32 *a2 = token2;
	unsigned int pdw;
	unsigned int pbi;

	pdw = prefixlen >> 5;	  /* num of whole u32 in prefix */
	pbi = prefixlen &  0x1f;  /* num of bits in incomplete u32 in prefix */

	if (pdw)
		if (memcmp(a1, a2, pdw << 2))
			return false;

	if (pbi) {
		__be32 mask;

		mask = htonl((0xffffffff) << (32 - pbi));

		if ((a1[pdw] ^ a2[pdw]) & mask)
			return false;
	}

	return true;
}

static inline bool addr4_match(__be32 a1, __be32 a2, u8 prefixlen)
{
	/* C99 6.5.7 (3): u32 << 32 is undefined behaviour */
	if (sizeof(long) == 4 && prefixlen == 0)
		return true;
	return !((a1 ^ a2) & htonl(~0UL << (32 - prefixlen)));
}

static __inline__
__be16 xfrm_flowi_sport(const struct flowi *fl, const union flowi_uli *uli)
{
	__be16 port;
	switch(fl->flowi_proto) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
	case IPPROTO_UDPLITE:
	case IPPROTO_SCTP:
		port = uli->ports.sport;
		break;
	case IPPROTO_ICMP:
	case IPPROTO_ICMPV6:
		port = htons(uli->icmpt.type);
		break;
	case IPPROTO_MH:
		port = htons(uli->mht.type);
		break;
	case IPPROTO_GRE:
		port = htons(ntohl(uli->gre_key) >> 16);
		break;
	default:
		port = 0;	/*XXX*/
	}
	return port;
}

static __inline__
__be16 xfrm_flowi_dport(const struct flowi *fl, const union flowi_uli *uli)
{
	__be16 port;
	switch(fl->flowi_proto) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
	case IPPROTO_UDPLITE:
	case IPPROTO_SCTP:
		port = uli->ports.dport;
		break;
	case IPPROTO_ICMP:
	case IPPROTO_ICMPV6:
		port = htons(uli->icmpt.code);
		break;
	case IPPROTO_GRE:
		port = htons(ntohl(uli->gre_key) & 0xffff);
		break;
	default:
		port = 0;	/*XXX*/
	}
	return port;
}

bool xfrm_selector_match(const struct xfrm_selector *sel,
			 const struct flowi *fl, unsigned short family);

#ifdef CONFIG_SECURITY_NETWORK_XFRM
/*	If neither has a context --> match
 * 	Otherwise, both must have a context and the sids, doi, alg must match
 */
static inline bool xfrm_sec_ctx_match(struct xfrm_sec_ctx *s1, struct xfrm_sec_ctx *s2)
{
	return ((!s1 && !s2) ||
		(s1 && s2 &&
		 (s1->ctx_sid == s2->ctx_sid) &&
		 (s1->ctx_doi == s2->ctx_doi) &&
		 (s1->ctx_alg == s2->ctx_alg)));
}
#else
static inline bool xfrm_sec_ctx_match(struct xfrm_sec_ctx *s1, struct xfrm_sec_ctx *s2)
{
	return true;
}
#endif

/* A struct encoding bundle of transformations to apply to some set of flow.
 *
 * xdst->child points to the next element of bundle.
 * dst->xfrm  points to an instanse of transformer.
 *
 * Due to unfortunate limitations of current routing cache, which we
 * have no time to fix, it mirrors struct rtable and bound to the same
 * routing key, including saddr,daddr. However, we can have many of
 * bundles differing by session id. All the bundles grow from a parent
 * policy rule.
 */
struct xfrm_dst {
	union {
		struct dst_entry	dst;
		struct rtable		rt;
		struct rt6_info		rt6;
	} u;
	struct dst_entry *route;
	struct dst_entry *child;
	struct dst_entry *path;
	struct xfrm_policy *pols[XFRM_POLICY_TYPE_MAX];
	int num_pols, num_xfrms;
	u32 xfrm_genid;
	u32 policy_genid;
	u32 route_mtu_cached;
	u32 child_mtu_cached;
	u32 route_cookie;
	u32 path_cookie;
};

static inline struct dst_entry *xfrm_dst_path(const struct dst_entry *dst)
{
#ifdef CONFIG_XFRM
	if (dst->xfrm || (dst->flags & DST_XFRM_QUEUE)) {
		const struct xfrm_dst *xdst = (const struct xfrm_dst *) dst;

		return xdst->path;
	}
#endif
	return (struct dst_entry *) dst;
}

static inline struct dst_entry *xfrm_dst_child(const struct dst_entry *dst)
{
#ifdef CONFIG_XFRM
	if (dst->xfrm || (dst->flags & DST_XFRM_QUEUE)) {
		struct xfrm_dst *xdst = (struct xfrm_dst *) dst;
		return xdst->child;
	}
#endif
	return NULL;
}

#ifdef CONFIG_XFRM
static inline void xfrm_dst_set_child(struct xfrm_dst *xdst, struct dst_entry *child)
{
	xdst->child = child;
}

static inline void xfrm_dst_destroy(struct xfrm_dst *xdst)
{
	xfrm_pols_put(xdst->pols, xdst->num_pols);
	dst_release(xdst->route);
	if (likely(xdst->u.dst.xfrm))
		xfrm_state_put(xdst->u.dst.xfrm);
}
#endif

void xfrm_dst_ifdown(struct dst_entry *dst, struct net_device *dev);

struct xfrm_if_parms {
	int link;		/* ifindex of underlying L2 interface */
	u32 if_id;		/* interface identifyer */
};

struct xfrm_if {
	struct xfrm_if __rcu *next;	/* next interface in list */
	struct net_device *dev;		/* virtual device associated with interface */
	struct net *net;		/* netns for packet i/o */
	struct xfrm_if_parms p;		/* interface parms */

	struct gro_cells gro_cells;
};

struct xfrm_offload {
	/* Output sequence number for replay protection on offloading. */
	struct {
		__u32 low;
		__u32 hi;
	} seq;

	__u32			flags;
#define	SA_DELETE_REQ		1
#define	CRYPTO_DONE		2
#define	CRYPTO_NEXT_DONE	4
#define	CRYPTO_FALLBACK		8
#define	XFRM_GSO_SEGMENT	16
#define	XFRM_GRO		32
#define	XFRM_ESP_NO_TRAILER	64
#define	XFRM_DEV_RESUME		128
#define	XFRM_XMIT		256

	__u32			status;
#define CRYPTO_SUCCESS				1
#define CRYPTO_GENERIC_ERROR			2
#define CRYPTO_TRANSPORT_AH_AUTH_FAILED		4
#define CRYPTO_TRANSPORT_ESP_AUTH_FAILED	8
#define CRYPTO_TUNNEL_AH_AUTH_FAILED		16
#define CRYPTO_TUNNEL_ESP_AUTH_FAILED		32
#define CRYPTO_INVALID_PACKET_SYNTAX		64
#define CRYPTO_INVALID_PROTOCOL			128

	__u8			proto;
	__u8			inner_ipproto;
};

struct sec_path {
	int			len;
	int			olen;

	struct xfrm_state	*xvec[XFRM_MAX_DEPTH];
	struct xfrm_offload	ovec[XFRM_MAX_OFFLOAD_DEPTH];
};

struct sec_path *secpath_set(struct sk_buff *skb);

static inline void
secpath_reset(struct sk_buff *skb)
{
#ifdef CONFIG_XFRM
	skb_ext_del(skb, SKB_EXT_SEC_PATH);
#endif
}

static inline int
xfrm_addr_any(const xfrm_address_t *addr, unsigned short family)
{
	switch (family) {
	case AF_INET:
		return addr->a4 == 0;
	case AF_INET6:
		return ipv6_addr_any(&addr->in6);
	}
	return 0;
}

static inline int
__xfrm4_state_addr_cmp(const struct xfrm_tmpl *tmpl, const struct xfrm_state *x)
{
	return	(tmpl->saddr.a4 &&
		 tmpl->saddr.a4 != x->props.saddr.a4);
}

static inline int
__xfrm6_state_addr_cmp(const struct xfrm_tmpl *tmpl, const struct xfrm_state *x)
{
	return	(!ipv6_addr_any((struct in6_addr*)&tmpl->saddr) &&
		 !ipv6_addr_equal((struct in6_addr *)&tmpl->saddr, (struct in6_addr*)&x->props.saddr));
}

static inline int
xfrm_state_addr_cmp(const struct xfrm_tmpl *tmpl, const struct xfrm_state *x, unsigned short family)
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
int __xfrm_policy_check(struct sock *, int dir, struct sk_buff *skb,
			unsigned short family);

static inline bool __xfrm_check_nopolicy(struct net *net, struct sk_buff *skb,
					 int dir)
{
	if (!net->xfrm.policy_count[dir] && !secpath_exists(skb))
		return net->xfrm.policy_default[dir] == XFRM_USERPOLICY_ACCEPT;

	return false;
}

static inline int __xfrm_policy_check2(struct sock *sk, int dir,
				       struct sk_buff *skb,
				       unsigned int family, int reverse)
{
	struct net *net = dev_net(skb->dev);
	int ndir = dir | (reverse ? XFRM_POLICY_MASK + 1 : 0);

	if (sk && sk->sk_policy[XFRM_POLICY_IN])
		return __xfrm_policy_check(sk, ndir, skb, family);

	return __xfrm_check_nopolicy(net, skb, dir) ||
	       (skb_dst(skb) && (skb_dst(skb)->flags & DST_NOPOLICY)) ||
	       __xfrm_policy_check(sk, ndir, skb, family);
}

static inline int xfrm_policy_check(struct sock *sk, int dir, struct sk_buff *skb, unsigned short family)
{
	return __xfrm_policy_check2(sk, dir, skb, family, 0);
}

static inline int xfrm4_policy_check(struct sock *sk, int dir, struct sk_buff *skb)
{
	return xfrm_policy_check(sk, dir, skb, AF_INET);
}

static inline int xfrm6_policy_check(struct sock *sk, int dir, struct sk_buff *skb)
{
	return xfrm_policy_check(sk, dir, skb, AF_INET6);
}

static inline int xfrm4_policy_check_reverse(struct sock *sk, int dir,
					     struct sk_buff *skb)
{
	return __xfrm_policy_check2(sk, dir, skb, AF_INET, 1);
}

static inline int xfrm6_policy_check_reverse(struct sock *sk, int dir,
					     struct sk_buff *skb)
{
	return __xfrm_policy_check2(sk, dir, skb, AF_INET6, 1);
}

int __xfrm_decode_session(struct sk_buff *skb, struct flowi *fl,
			  unsigned int family, int reverse);

static inline int xfrm_decode_session(struct sk_buff *skb, struct flowi *fl,
				      unsigned int family)
{
	return __xfrm_decode_session(skb, fl, family, 0);
}

static inline int xfrm_decode_session_reverse(struct sk_buff *skb,
					      struct flowi *fl,
					      unsigned int family)
{
	return __xfrm_decode_session(skb, fl, family, 1);
}

int __xfrm_route_forward(struct sk_buff *skb, unsigned short family);

static inline int xfrm_route_forward(struct sk_buff *skb, unsigned short family)
{
	struct net *net = dev_net(skb->dev);

	if (!net->xfrm.policy_count[XFRM_POLICY_OUT] &&
	    net->xfrm.policy_default[XFRM_POLICY_OUT] == XFRM_USERPOLICY_ACCEPT)
		return true;

	return (skb_dst(skb)->flags & DST_NOXFRM) ||
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

int __xfrm_sk_clone_policy(struct sock *sk, const struct sock *osk);

static inline int xfrm_sk_clone_policy(struct sock *sk, const struct sock *osk)
{
	sk->sk_policy[0] = NULL;
	sk->sk_policy[1] = NULL;
	if (unlikely(osk->sk_policy[0] || osk->sk_policy[1]))
		return __xfrm_sk_clone_policy(sk, osk);
	return 0;
}

int xfrm_policy_delete(struct xfrm_policy *pol, int dir);

static inline void xfrm_sk_free_policy(struct sock *sk)
{
	struct xfrm_policy *pol;

	pol = rcu_dereference_protected(sk->sk_policy[0], 1);
	if (unlikely(pol != NULL)) {
		xfrm_policy_delete(pol, XFRM_POLICY_MAX);
		sk->sk_policy[0] = NULL;
	}
	pol = rcu_dereference_protected(sk->sk_policy[1], 1);
	if (unlikely(pol != NULL)) {
		xfrm_policy_delete(pol, XFRM_POLICY_MAX+1);
		sk->sk_policy[1] = NULL;
	}
}

#else

static inline void xfrm_sk_free_policy(struct sock *sk) {}
static inline int xfrm_sk_clone_policy(struct sock *sk, const struct sock *osk) { return 0; }
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
static inline int xfrm_decode_session_reverse(struct sk_buff *skb,
					      struct flowi *fl,
					      unsigned int family)
{
	return -ENOSYS;
}
static inline int xfrm4_policy_check_reverse(struct sock *sk, int dir,
					     struct sk_buff *skb)
{
	return 1;
}
static inline int xfrm6_policy_check_reverse(struct sock *sk, int dir,
					     struct sk_buff *skb)
{
	return 1;
}
#endif

static __inline__
xfrm_address_t *xfrm_flowi_daddr(const struct flowi *fl, unsigned short family)
{
	switch (family){
	case AF_INET:
		return (xfrm_address_t *)&fl->u.ip4.daddr;
	case AF_INET6:
		return (xfrm_address_t *)&fl->u.ip6.daddr;
	}
	return NULL;
}

static __inline__
xfrm_address_t *xfrm_flowi_saddr(const struct flowi *fl, unsigned short family)
{
	switch (family){
	case AF_INET:
		return (xfrm_address_t *)&fl->u.ip4.saddr;
	case AF_INET6:
		return (xfrm_address_t *)&fl->u.ip6.saddr;
	}
	return NULL;
}

static __inline__
void xfrm_flowi_addr_get(const struct flowi *fl,
			 xfrm_address_t *saddr, xfrm_address_t *daddr,
			 unsigned short family)
{
	switch(family) {
	case AF_INET:
		memcpy(&saddr->a4, &fl->u.ip4.saddr, sizeof(saddr->a4));
		memcpy(&daddr->a4, &fl->u.ip4.daddr, sizeof(daddr->a4));
		break;
	case AF_INET6:
		saddr->in6 = fl->u.ip6.saddr;
		daddr->in6 = fl->u.ip6.daddr;
		break;
	}
}

static __inline__ int
__xfrm4_state_addr_check(const struct xfrm_state *x,
			 const xfrm_address_t *daddr, const xfrm_address_t *saddr)
{
	if (daddr->a4 == x->id.daddr.a4 &&
	    (saddr->a4 == x->props.saddr.a4 || !saddr->a4 || !x->props.saddr.a4))
		return 1;
	return 0;
}

static __inline__ int
__xfrm6_state_addr_check(const struct xfrm_state *x,
			 const xfrm_address_t *daddr, const xfrm_address_t *saddr)
{
	if (ipv6_addr_equal((struct in6_addr *)daddr, (struct in6_addr *)&x->id.daddr) &&
	    (ipv6_addr_equal((struct in6_addr *)saddr, (struct in6_addr *)&x->props.saddr) ||
	     ipv6_addr_any((struct in6_addr *)saddr) ||
	     ipv6_addr_any((struct in6_addr *)&x->props.saddr)))
		return 1;
	return 0;
}

static __inline__ int
xfrm_state_addr_check(const struct xfrm_state *x,
		      const xfrm_address_t *daddr, const xfrm_address_t *saddr,
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
xfrm_state_addr_flow_check(const struct xfrm_state *x, const struct flowi *fl,
			   unsigned short family)
{
	switch (family) {
	case AF_INET:
		return __xfrm4_state_addr_check(x,
						(const xfrm_address_t *)&fl->u.ip4.daddr,
						(const xfrm_address_t *)&fl->u.ip4.saddr);
	case AF_INET6:
		return __xfrm6_state_addr_check(x,
						(const xfrm_address_t *)&fl->u.ip6.daddr,
						(const xfrm_address_t *)&fl->u.ip6.saddr);
	}
	return 0;
}

static inline int xfrm_state_kern(const struct xfrm_state *x)
{
	return atomic_read(&x->tunnel_users);
}

static inline bool xfrm_id_proto_valid(u8 proto)
{
	switch (proto) {
	case IPPROTO_AH:
	case IPPROTO_ESP:
	case IPPROTO_COMP:
#if IS_ENABLED(CONFIG_IPV6)
	case IPPROTO_ROUTING:
	case IPPROTO_DSTOPTS:
#endif
		return true;
	default:
		return false;
	}
}

/* IPSEC_PROTO_ANY only matches 3 IPsec protocols, 0 could match all. */
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
struct xfrm_algo_aead_info {
	char *geniv;
	u16 icv_truncbits;
};

struct xfrm_algo_auth_info {
	u16 icv_truncbits;
	u16 icv_fullbits;
};

struct xfrm_algo_encr_info {
	char *geniv;
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
	u8 pfkey_supported:1;
	union {
		struct xfrm_algo_aead_info aead;
		struct xfrm_algo_auth_info auth;
		struct xfrm_algo_encr_info encr;
		struct xfrm_algo_comp_info comp;
	} uinfo;
	struct sadb_alg desc;
};

/* XFRM protocol handlers.  */
struct xfrm4_protocol {
	int (*handler)(struct sk_buff *skb);
	int (*input_handler)(struct sk_buff *skb, int nexthdr, __be32 spi,
			     int encap_type);
	int (*cb_handler)(struct sk_buff *skb, int err);
	int (*err_handler)(struct sk_buff *skb, u32 info);

	struct xfrm4_protocol __rcu *next;
	int priority;
};

struct xfrm6_protocol {
	int (*handler)(struct sk_buff *skb);
	int (*input_handler)(struct sk_buff *skb, int nexthdr, __be32 spi,
			     int encap_type);
	int (*cb_handler)(struct sk_buff *skb, int err);
	int (*err_handler)(struct sk_buff *skb, struct inet6_skb_parm *opt,
			   u8 type, u8 code, int offset, __be32 info);

	struct xfrm6_protocol __rcu *next;
	int priority;
};

/* XFRM tunnel handlers.  */
struct xfrm_tunnel {
	int (*handler)(struct sk_buff *skb);
	int (*cb_handler)(struct sk_buff *skb, int err);
	int (*err_handler)(struct sk_buff *skb, u32 info);

	struct xfrm_tunnel __rcu *next;
	int priority;
};

struct xfrm6_tunnel {
	int (*handler)(struct sk_buff *skb);
	int (*cb_handler)(struct sk_buff *skb, int err);
	int (*err_handler)(struct sk_buff *skb, struct inet6_skb_parm *opt,
			   u8 type, u8 code, int offset, __be32 info);
	struct xfrm6_tunnel __rcu *next;
	int priority;
};

void xfrm_init(void);
void xfrm4_init(void);
int xfrm_state_init(struct net *net);
void xfrm_state_fini(struct net *net);
void xfrm4_state_init(void);
void xfrm4_protocol_init(void);
#ifdef CONFIG_XFRM
int xfrm6_init(void);
void xfrm6_fini(void);
int xfrm6_state_init(void);
void xfrm6_state_fini(void);
int xfrm6_protocol_init(void);
void xfrm6_protocol_fini(void);
#else
static inline int xfrm6_init(void)
{
	return 0;
}
static inline void xfrm6_fini(void)
{
	;
}
#endif

#ifdef CONFIG_XFRM_STATISTICS
int xfrm_proc_init(struct net *net);
void xfrm_proc_fini(struct net *net);
#endif

int xfrm_sysctl_init(struct net *net);
#ifdef CONFIG_SYSCTL
void xfrm_sysctl_fini(struct net *net);
#else
static inline void xfrm_sysctl_fini(struct net *net)
{
}
#endif

void xfrm_state_walk_init(struct xfrm_state_walk *walk, u8 proto,
			  struct xfrm_address_filter *filter);
int xfrm_state_walk(struct net *net, struct xfrm_state_walk *walk,
		    int (*func)(struct xfrm_state *, int, void*), void *);
void xfrm_state_walk_done(struct xfrm_state_walk *walk, struct net *net);
struct xfrm_state *xfrm_state_alloc(struct net *net);
void xfrm_state_free(struct xfrm_state *x);
struct xfrm_state *xfrm_state_find(const xfrm_address_t *daddr,
				   const xfrm_address_t *saddr,
				   const struct flowi *fl,
				   struct xfrm_tmpl *tmpl,
				   struct xfrm_policy *pol, int *err,
				   unsigned short family, u32 if_id);
struct xfrm_state *xfrm_stateonly_find(struct net *net, u32 mark, u32 if_id,
				       xfrm_address_t *daddr,
				       xfrm_address_t *saddr,
				       unsigned short family,
				       u8 mode, u8 proto, u32 reqid);
struct xfrm_state *xfrm_state_lookup_byspi(struct net *net, __be32 spi,
					      unsigned short family);
int xfrm_state_check_expire(struct xfrm_state *x);
void xfrm_state_insert(struct xfrm_state *x);
int xfrm_state_add(struct xfrm_state *x);
int xfrm_state_update(struct xfrm_state *x);
struct xfrm_state *xfrm_state_lookup(struct net *net, u32 mark,
				     const xfrm_address_t *daddr, __be32 spi,
				     u8 proto, unsigned short family);
struct xfrm_state *xfrm_state_lookup_byaddr(struct net *net, u32 mark,
					    const xfrm_address_t *daddr,
					    const xfrm_address_t *saddr,
					    u8 proto,
					    unsigned short family);
#ifdef CONFIG_XFRM_SUB_POLICY
void xfrm_tmpl_sort(struct xfrm_tmpl **dst, struct xfrm_tmpl **src, int n,
		    unsigned short family);
void xfrm_state_sort(struct xfrm_state **dst, struct xfrm_state **src, int n,
		     unsigned short family);
#else
static inline void xfrm_tmpl_sort(struct xfrm_tmpl **d, struct xfrm_tmpl **s,
				  int n, unsigned short family)
{
}

static inline void xfrm_state_sort(struct xfrm_state **d, struct xfrm_state **s,
				   int n, unsigned short family)
{
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

struct xfrm_state *xfrm_find_acq_byseq(struct net *net, u32 mark, u32 seq);
int xfrm_state_delete(struct xfrm_state *x);
int xfrm_state_flush(struct net *net, u8 proto, bool task_valid, bool sync);
int xfrm_dev_state_flush(struct net *net, struct net_device *dev, bool task_valid);
void xfrm_sad_getinfo(struct net *net, struct xfrmk_sadinfo *si);
void xfrm_spd_getinfo(struct net *net, struct xfrmk_spdinfo *si);
u32 xfrm_replay_seqhi(struct xfrm_state *x, __be32 net_seq);
int xfrm_init_replay(struct xfrm_state *x);
u32 xfrm_state_mtu(struct xfrm_state *x, int mtu);
int __xfrm_init_state(struct xfrm_state *x, bool init_replay, bool offload);
int xfrm_init_state(struct xfrm_state *x);
int xfrm_input(struct sk_buff *skb, int nexthdr, __be32 spi, int encap_type);
int xfrm_input_resume(struct sk_buff *skb, int nexthdr);
int xfrm_trans_queue_net(struct net *net, struct sk_buff *skb,
			 int (*finish)(struct net *, struct sock *,
				       struct sk_buff *));
int xfrm_trans_queue(struct sk_buff *skb,
		     int (*finish)(struct net *, struct sock *,
				   struct sk_buff *));
int xfrm_output_resume(struct sock *sk, struct sk_buff *skb, int err);
int xfrm_output(struct sock *sk, struct sk_buff *skb);

#if IS_ENABLED(CONFIG_NET_PKTGEN)
int pktgen_xfrm_outer_mode_output(struct xfrm_state *x, struct sk_buff *skb);
#endif

void xfrm_local_error(struct sk_buff *skb, int mtu);
int xfrm4_extract_input(struct xfrm_state *x, struct sk_buff *skb);
int xfrm4_rcv_encap(struct sk_buff *skb, int nexthdr, __be32 spi,
		    int encap_type);
int xfrm4_transport_finish(struct sk_buff *skb, int async);
int xfrm4_rcv(struct sk_buff *skb);

static inline int xfrm4_rcv_spi(struct sk_buff *skb, int nexthdr, __be32 spi)
{
	XFRM_TUNNEL_SKB_CB(skb)->tunnel.ip4 = NULL;
	XFRM_SPI_SKB_CB(skb)->family = AF_INET;
	XFRM_SPI_SKB_CB(skb)->daddroff = offsetof(struct iphdr, daddr);
	return xfrm_input(skb, nexthdr, spi, 0);
}

int xfrm4_output(struct net *net, struct sock *sk, struct sk_buff *skb);
int xfrm4_protocol_register(struct xfrm4_protocol *handler, unsigned char protocol);
int xfrm4_protocol_deregister(struct xfrm4_protocol *handler, unsigned char protocol);
int xfrm4_tunnel_register(struct xfrm_tunnel *handler, unsigned short family);
int xfrm4_tunnel_deregister(struct xfrm_tunnel *handler, unsigned short family);
void xfrm4_local_error(struct sk_buff *skb, u32 mtu);
int xfrm6_extract_input(struct xfrm_state *x, struct sk_buff *skb);
int xfrm6_rcv_spi(struct sk_buff *skb, int nexthdr, __be32 spi,
		  struct ip6_tnl *t);
int xfrm6_rcv_encap(struct sk_buff *skb, int nexthdr, __be32 spi,
		    int encap_type);
int xfrm6_transport_finish(struct sk_buff *skb, int async);
int xfrm6_rcv_tnl(struct sk_buff *skb, struct ip6_tnl *t);
int xfrm6_rcv(struct sk_buff *skb);
int xfrm6_input_addr(struct sk_buff *skb, xfrm_address_t *daddr,
		     xfrm_address_t *saddr, u8 proto);
void xfrm6_local_error(struct sk_buff *skb, u32 mtu);
int xfrm6_protocol_register(struct xfrm6_protocol *handler, unsigned char protocol);
int xfrm6_protocol_deregister(struct xfrm6_protocol *handler, unsigned char protocol);
int xfrm6_tunnel_register(struct xfrm6_tunnel *handler, unsigned short family);
int xfrm6_tunnel_deregister(struct xfrm6_tunnel *handler, unsigned short family);
__be32 xfrm6_tunnel_alloc_spi(struct net *net, xfrm_address_t *saddr);
__be32 xfrm6_tunnel_spi_lookup(struct net *net, const xfrm_address_t *saddr);
int xfrm6_output(struct net *net, struct sock *sk, struct sk_buff *skb);

#ifdef CONFIG_XFRM
void xfrm6_local_rxpmtu(struct sk_buff *skb, u32 mtu);
int xfrm4_udp_encap_rcv(struct sock *sk, struct sk_buff *skb);
int xfrm6_udp_encap_rcv(struct sock *sk, struct sk_buff *skb);
int xfrm_user_policy(struct sock *sk, int optname, sockptr_t optval,
		     int optlen);
#else
static inline int xfrm_user_policy(struct sock *sk, int optname,
				   sockptr_t optval, int optlen)
{
 	return -ENOPROTOOPT;
}
#endif

struct dst_entry *__xfrm_dst_lookup(struct net *net, int tos, int oif,
				    const xfrm_address_t *saddr,
				    const xfrm_address_t *daddr,
				    int family, u32 mark);

struct xfrm_policy *xfrm_policy_alloc(struct net *net, gfp_t gfp);

void xfrm_policy_walk_init(struct xfrm_policy_walk *walk, u8 type);
int xfrm_policy_walk(struct net *net, struct xfrm_policy_walk *walk,
		     int (*func)(struct xfrm_policy *, int, int, void*),
		     void *);
void xfrm_policy_walk_done(struct xfrm_policy_walk *walk, struct net *net);
int xfrm_policy_insert(int dir, struct xfrm_policy *policy, int excl);
struct xfrm_policy *xfrm_policy_bysel_ctx(struct net *net,
					  const struct xfrm_mark *mark,
					  u32 if_id, u8 type, int dir,
					  struct xfrm_selector *sel,
					  struct xfrm_sec_ctx *ctx, int delete,
					  int *err);
struct xfrm_policy *xfrm_policy_byid(struct net *net,
				     const struct xfrm_mark *mark, u32 if_id,
				     u8 type, int dir, u32 id, int delete,
				     int *err);
int xfrm_policy_flush(struct net *net, u8 type, bool task_valid);
void xfrm_policy_hash_rebuild(struct net *net);
u32 xfrm_get_acqseq(void);
int verify_spi_info(u8 proto, u32 min, u32 max);
int xfrm_alloc_spi(struct xfrm_state *x, u32 minspi, u32 maxspi);
struct xfrm_state *xfrm_find_acq(struct net *net, const struct xfrm_mark *mark,
				 u8 mode, u32 reqid, u32 if_id, u8 proto,
				 const xfrm_address_t *daddr,
				 const xfrm_address_t *saddr, int create,
				 unsigned short family);
int xfrm_sk_policy_insert(struct sock *sk, int dir, struct xfrm_policy *pol);

#ifdef CONFIG_XFRM_MIGRATE
int km_migrate(const struct xfrm_selector *sel, u8 dir, u8 type,
	       const struct xfrm_migrate *m, int num_bundles,
	       const struct xfrm_kmaddress *k,
	       const struct xfrm_encap_tmpl *encap);
struct xfrm_state *xfrm_migrate_state_find(struct xfrm_migrate *m, struct net *net,
						u32 if_id);
struct xfrm_state *xfrm_state_migrate(struct xfrm_state *x,
				      struct xfrm_migrate *m,
				      struct xfrm_encap_tmpl *encap);
int xfrm_migrate(const struct xfrm_selector *sel, u8 dir, u8 type,
		 struct xfrm_migrate *m, int num_bundles,
		 struct xfrm_kmaddress *k, struct net *net,
		 struct xfrm_encap_tmpl *encap, u32 if_id);
#endif

int km_new_mapping(struct xfrm_state *x, xfrm_address_t *ipaddr, __be16 sport);
void km_policy_expired(struct xfrm_policy *pol, int dir, int hard, u32 portid);
int km_report(struct net *net, u8 proto, struct xfrm_selector *sel,
	      xfrm_address_t *addr);

void xfrm_input_init(void);
int xfrm_parse_spi(struct sk_buff *skb, u8 nexthdr, __be32 *spi, __be32 *seq);

void xfrm_probe_algs(void);
int xfrm_count_pfkey_auth_supported(void);
int xfrm_count_pfkey_enc_supported(void);
struct xfrm_algo_desc *xfrm_aalg_get_byidx(unsigned int idx);
struct xfrm_algo_desc *xfrm_ealg_get_byidx(unsigned int idx);
struct xfrm_algo_desc *xfrm_aalg_get_byid(int alg_id);
struct xfrm_algo_desc *xfrm_ealg_get_byid(int alg_id);
struct xfrm_algo_desc *xfrm_calg_get_byid(int alg_id);
struct xfrm_algo_desc *xfrm_aalg_get_byname(const char *name, int probe);
struct xfrm_algo_desc *xfrm_ealg_get_byname(const char *name, int probe);
struct xfrm_algo_desc *xfrm_calg_get_byname(const char *name, int probe);
struct xfrm_algo_desc *xfrm_aead_get_byname(const char *name, int icv_len,
					    int probe);

static inline bool xfrm6_addr_equal(const xfrm_address_t *a,
				    const xfrm_address_t *b)
{
	return ipv6_addr_equal((const struct in6_addr *)a,
			       (const struct in6_addr *)b);
}

static inline bool xfrm_addr_equal(const xfrm_address_t *a,
				   const xfrm_address_t *b,
				   sa_family_t family)
{
	switch (family) {
	default:
	case AF_INET:
		return ((__force u32)a->a4 ^ (__force u32)b->a4) == 0;
	case AF_INET6:
		return xfrm6_addr_equal(a, b);
	}
}

static inline int xfrm_policy_id2dir(u32 index)
{
	return index & 7;
}

#ifdef CONFIG_XFRM
void xfrm_replay_advance(struct xfrm_state *x, __be32 net_seq);
int xfrm_replay_check(struct xfrm_state *x, struct sk_buff *skb, __be32 net_seq);
void xfrm_replay_notify(struct xfrm_state *x, int event);
int xfrm_replay_overflow(struct xfrm_state *x, struct sk_buff *skb);
int xfrm_replay_recheck(struct xfrm_state *x, struct sk_buff *skb, __be32 net_seq);

static inline int xfrm_aevent_is_on(struct net *net)
{
	struct sock *nlsk;
	int ret = 0;

	rcu_read_lock();
	nlsk = rcu_dereference(net->xfrm.nlsk);
	if (nlsk)
		ret = netlink_has_listeners(nlsk, XFRMNLGRP_AEVENTS);
	rcu_read_unlock();
	return ret;
}

static inline int xfrm_acquire_is_on(struct net *net)
{
	struct sock *nlsk;
	int ret = 0;

	rcu_read_lock();
	nlsk = rcu_dereference(net->xfrm.nlsk);
	if (nlsk)
		ret = netlink_has_listeners(nlsk, XFRMNLGRP_ACQUIRE);
	rcu_read_unlock();

	return ret;
}
#endif

static inline unsigned int aead_len(struct xfrm_algo_aead *alg)
{
	return sizeof(*alg) + ((alg->alg_key_len + 7) / 8);
}

static inline unsigned int xfrm_alg_len(const struct xfrm_algo *alg)
{
	return sizeof(*alg) + ((alg->alg_key_len + 7) / 8);
}

static inline unsigned int xfrm_alg_auth_len(const struct xfrm_algo_auth *alg)
{
	return sizeof(*alg) + ((alg->alg_key_len + 7) / 8);
}

static inline unsigned int xfrm_replay_state_esn_len(struct xfrm_replay_state_esn *replay_esn)
{
	return sizeof(*replay_esn) + replay_esn->bmp_len * sizeof(__u32);
}

#ifdef CONFIG_XFRM_MIGRATE
static inline int xfrm_replay_clone(struct xfrm_state *x,
				     struct xfrm_state *orig)
{

	x->replay_esn = kmemdup(orig->replay_esn,
				xfrm_replay_state_esn_len(orig->replay_esn),
				GFP_KERNEL);
	if (!x->replay_esn)
		return -ENOMEM;
	x->preplay_esn = kmemdup(orig->preplay_esn,
				 xfrm_replay_state_esn_len(orig->preplay_esn),
				 GFP_KERNEL);
	if (!x->preplay_esn)
		return -ENOMEM;

	return 0;
}

static inline struct xfrm_algo_aead *xfrm_algo_aead_clone(struct xfrm_algo_aead *orig)
{
	return kmemdup(orig, aead_len(orig), GFP_KERNEL);
}


static inline struct xfrm_algo *xfrm_algo_clone(struct xfrm_algo *orig)
{
	return kmemdup(orig, xfrm_alg_len(orig), GFP_KERNEL);
}

static inline struct xfrm_algo_auth *xfrm_algo_auth_clone(struct xfrm_algo_auth *orig)
{
	return kmemdup(orig, xfrm_alg_auth_len(orig), GFP_KERNEL);
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

#ifdef CONFIG_XFRM
static inline struct xfrm_state *xfrm_input_state(struct sk_buff *skb)
{
	struct sec_path *sp = skb_sec_path(skb);

	return sp->xvec[sp->len - 1];
}
#endif

static inline struct xfrm_offload *xfrm_offload(struct sk_buff *skb)
{
#ifdef CONFIG_XFRM
	struct sec_path *sp = skb_sec_path(skb);

	if (!sp || !sp->olen || sp->len != sp->olen)
		return NULL;

	return &sp->ovec[sp->olen - 1];
#else
	return NULL;
#endif
}

void __init xfrm_dev_init(void);

#ifdef CONFIG_XFRM_OFFLOAD
void xfrm_dev_resume(struct sk_buff *skb);
void xfrm_dev_backlog(struct softnet_data *sd);
struct sk_buff *validate_xmit_xfrm(struct sk_buff *skb, netdev_features_t features, bool *again);
int xfrm_dev_state_add(struct net *net, struct xfrm_state *x,
		       struct xfrm_user_offload *xuo);
bool xfrm_dev_offload_ok(struct sk_buff *skb, struct xfrm_state *x);

static inline void xfrm_dev_state_advance_esn(struct xfrm_state *x)
{
	struct xfrm_state_offload *xso = &x->xso;

	if (xso->dev && xso->dev->xfrmdev_ops->xdo_dev_state_advance_esn)
		xso->dev->xfrmdev_ops->xdo_dev_state_advance_esn(x);
}

static inline bool xfrm_dst_offload_ok(struct dst_entry *dst)
{
	struct xfrm_state *x = dst->xfrm;
	struct xfrm_dst *xdst;

	if (!x || !x->type_offload)
		return false;

	xdst = (struct xfrm_dst *) dst;
	if (!x->xso.offload_handle && !xdst->child->xfrm)
		return true;
	if (x->xso.offload_handle && (x->xso.dev == xfrm_dst_path(dst)->dev) &&
	    !xdst->child->xfrm)
		return true;

	return false;
}

static inline void xfrm_dev_state_delete(struct xfrm_state *x)
{
	struct xfrm_state_offload *xso = &x->xso;

	if (xso->dev)
		xso->dev->xfrmdev_ops->xdo_dev_state_delete(x);
}

static inline void xfrm_dev_state_free(struct xfrm_state *x)
{
	struct xfrm_state_offload *xso = &x->xso;
	struct net_device *dev = xso->dev;

	if (dev && dev->xfrmdev_ops) {
		if (dev->xfrmdev_ops->xdo_dev_state_free)
			dev->xfrmdev_ops->xdo_dev_state_free(x);
		xso->dev = NULL;
		dev_put_track(dev, &xso->dev_tracker);
	}
}
#else
static inline void xfrm_dev_resume(struct sk_buff *skb)
{
}

static inline void xfrm_dev_backlog(struct softnet_data *sd)
{
}

static inline struct sk_buff *validate_xmit_xfrm(struct sk_buff *skb, netdev_features_t features, bool *again)
{
	return skb;
}

static inline int xfrm_dev_state_add(struct net *net, struct xfrm_state *x, struct xfrm_user_offload *xuo)
{
	return 0;
}

static inline void xfrm_dev_state_delete(struct xfrm_state *x)
{
}

static inline void xfrm_dev_state_free(struct xfrm_state *x)
{
}

static inline bool xfrm_dev_offload_ok(struct sk_buff *skb, struct xfrm_state *x)
{
	return false;
}

static inline void xfrm_dev_state_advance_esn(struct xfrm_state *x)
{
}

static inline bool xfrm_dst_offload_ok(struct dst_entry *dst)
{
	return false;
}
#endif

static inline int xfrm_mark_get(struct nlattr **attrs, struct xfrm_mark *m)
{
	if (attrs[XFRMA_MARK])
		memcpy(m, nla_data(attrs[XFRMA_MARK]), sizeof(struct xfrm_mark));
	else
		m->v = m->m = 0;

	return m->v & m->m;
}

static inline int xfrm_mark_put(struct sk_buff *skb, const struct xfrm_mark *m)
{
	int ret = 0;

	if (m->m | m->v)
		ret = nla_put(skb, XFRMA_MARK, sizeof(struct xfrm_mark), m);
	return ret;
}

static inline __u32 xfrm_smark_get(__u32 mark, struct xfrm_state *x)
{
	struct xfrm_mark *m = &x->props.smark;

	return (m->v & m->m) | (mark & ~m->m);
}

static inline int xfrm_if_id_put(struct sk_buff *skb, __u32 if_id)
{
	int ret = 0;

	if (if_id)
		ret = nla_put_u32(skb, XFRMA_IF_ID, if_id);
	return ret;
}

static inline int xfrm_tunnel_check(struct sk_buff *skb, struct xfrm_state *x,
				    unsigned int family)
{
	bool tunnel = false;

	switch(family) {
	case AF_INET:
		if (XFRM_TUNNEL_SKB_CB(skb)->tunnel.ip4)
			tunnel = true;
		break;
	case AF_INET6:
		if (XFRM_TUNNEL_SKB_CB(skb)->tunnel.ip6)
			tunnel = true;
		break;
	}
	if (tunnel && !(x->outer_mode.flags & XFRM_MODE_FLAG_TUNNEL))
		return -EINVAL;

	return 0;
}

extern const int xfrm_msg_min[XFRM_NR_MSGTYPES];
extern const struct nla_policy xfrma_policy[XFRMA_MAX+1];

struct xfrm_translator {
	/* Allocate frag_list and put compat translation there */
	int (*alloc_compat)(struct sk_buff *skb, const struct nlmsghdr *src);

	/* Allocate nlmsg with 64-bit translaton of received 32-bit message */
	struct nlmsghdr *(*rcv_msg_compat)(const struct nlmsghdr *nlh,
			int maxtype, const struct nla_policy *policy,
			struct netlink_ext_ack *extack);

	/* Translate 32-bit user_policy from sockptr */
	int (*xlate_user_policy_sockptr)(u8 **pdata32, int optlen);

	struct module *owner;
};

#if IS_ENABLED(CONFIG_XFRM_USER_COMPAT)
extern int xfrm_register_translator(struct xfrm_translator *xtr);
extern int xfrm_unregister_translator(struct xfrm_translator *xtr);
extern struct xfrm_translator *xfrm_get_translator(void);
extern void xfrm_put_translator(struct xfrm_translator *xtr);
#else
static inline struct xfrm_translator *xfrm_get_translator(void)
{
	return NULL;
}
static inline void xfrm_put_translator(struct xfrm_translator *xtr)
{
}
#endif

#if IS_ENABLED(CONFIG_IPV6)
static inline bool xfrm6_local_dontfrag(const struct sock *sk)
{
	int proto;

	if (!sk || sk->sk_family != AF_INET6)
		return false;

	proto = sk->sk_protocol;
	if (proto == IPPROTO_UDP || proto == IPPROTO_RAW)
		return inet6_sk(sk)->dontfrag;

	return false;
}
#endif
#endif	/* _NET_XFRM_H */
