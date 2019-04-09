/*
 * (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 * (C) 2011 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/skbuff.h>
#include <linux/gfp.h>
#include <net/xfrm.h>
#include <linux/jhash.h>
#include <linux/rtnetlink.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_helper.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_seqadj.h>
#include <net/netfilter/nf_conntrack_zones.h>
#include <linux/netfilter/nf_nat.h>

#include "nf_internals.h"

static spinlock_t nf_nat_locks[CONNTRACK_LOCKS];

static DEFINE_MUTEX(nf_nat_proto_mutex);
static unsigned int nat_net_id __read_mostly;

static struct hlist_head *nf_nat_bysource __read_mostly;
static unsigned int nf_nat_htable_size __read_mostly;
static unsigned int nf_nat_hash_rnd __read_mostly;

struct nf_nat_lookup_hook_priv {
	struct nf_hook_entries __rcu *entries;

	struct rcu_head rcu_head;
};

struct nf_nat_hooks_net {
	struct nf_hook_ops *nat_hook_ops;
	unsigned int users;
};

struct nat_net {
	struct nf_nat_hooks_net nat_proto_net[NFPROTO_NUMPROTO];
};

#ifdef CONFIG_XFRM
static void nf_nat_ipv4_decode_session(struct sk_buff *skb,
				       const struct nf_conn *ct,
				       enum ip_conntrack_dir dir,
				       unsigned long statusbit,
				       struct flowi *fl)
{
	const struct nf_conntrack_tuple *t = &ct->tuplehash[dir].tuple;
	struct flowi4 *fl4 = &fl->u.ip4;

	if (ct->status & statusbit) {
		fl4->daddr = t->dst.u3.ip;
		if (t->dst.protonum == IPPROTO_TCP ||
		    t->dst.protonum == IPPROTO_UDP ||
		    t->dst.protonum == IPPROTO_UDPLITE ||
		    t->dst.protonum == IPPROTO_DCCP ||
		    t->dst.protonum == IPPROTO_SCTP)
			fl4->fl4_dport = t->dst.u.all;
	}

	statusbit ^= IPS_NAT_MASK;

	if (ct->status & statusbit) {
		fl4->saddr = t->src.u3.ip;
		if (t->dst.protonum == IPPROTO_TCP ||
		    t->dst.protonum == IPPROTO_UDP ||
		    t->dst.protonum == IPPROTO_UDPLITE ||
		    t->dst.protonum == IPPROTO_DCCP ||
		    t->dst.protonum == IPPROTO_SCTP)
			fl4->fl4_sport = t->src.u.all;
	}
}

static void nf_nat_ipv6_decode_session(struct sk_buff *skb,
				       const struct nf_conn *ct,
				       enum ip_conntrack_dir dir,
				       unsigned long statusbit,
				       struct flowi *fl)
{
#if IS_ENABLED(CONFIG_IPV6)
	const struct nf_conntrack_tuple *t = &ct->tuplehash[dir].tuple;
	struct flowi6 *fl6 = &fl->u.ip6;

	if (ct->status & statusbit) {
		fl6->daddr = t->dst.u3.in6;
		if (t->dst.protonum == IPPROTO_TCP ||
		    t->dst.protonum == IPPROTO_UDP ||
		    t->dst.protonum == IPPROTO_UDPLITE ||
		    t->dst.protonum == IPPROTO_DCCP ||
		    t->dst.protonum == IPPROTO_SCTP)
			fl6->fl6_dport = t->dst.u.all;
	}

	statusbit ^= IPS_NAT_MASK;

	if (ct->status & statusbit) {
		fl6->saddr = t->src.u3.in6;
		if (t->dst.protonum == IPPROTO_TCP ||
		    t->dst.protonum == IPPROTO_UDP ||
		    t->dst.protonum == IPPROTO_UDPLITE ||
		    t->dst.protonum == IPPROTO_DCCP ||
		    t->dst.protonum == IPPROTO_SCTP)
			fl6->fl6_sport = t->src.u.all;
	}
#endif
}

static void __nf_nat_decode_session(struct sk_buff *skb, struct flowi *fl)
{
	const struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	enum ip_conntrack_dir dir;
	unsigned  long statusbit;
	u8 family;

	ct = nf_ct_get(skb, &ctinfo);
	if (ct == NULL)
		return;

	family = nf_ct_l3num(ct);
	dir = CTINFO2DIR(ctinfo);
	if (dir == IP_CT_DIR_ORIGINAL)
		statusbit = IPS_DST_NAT;
	else
		statusbit = IPS_SRC_NAT;

	switch (family) {
	case NFPROTO_IPV4:
		nf_nat_ipv4_decode_session(skb, ct, dir, statusbit, fl);
		return;
	case NFPROTO_IPV6:
		nf_nat_ipv6_decode_session(skb, ct, dir, statusbit, fl);
		return;
	}
}

int nf_xfrm_me_harder(struct net *net, struct sk_buff *skb, unsigned int family)
{
	struct flowi fl;
	unsigned int hh_len;
	struct dst_entry *dst;
	struct sock *sk = skb->sk;
	int err;

	err = xfrm_decode_session(skb, &fl, family);
	if (err < 0)
		return err;

	dst = skb_dst(skb);
	if (dst->xfrm)
		dst = ((struct xfrm_dst *)dst)->route;
	if (!dst_hold_safe(dst))
		return -EHOSTUNREACH;

	if (sk && !net_eq(net, sock_net(sk)))
		sk = NULL;

	dst = xfrm_lookup(net, dst, &fl, sk, 0);
	if (IS_ERR(dst))
		return PTR_ERR(dst);

	skb_dst_drop(skb);
	skb_dst_set(skb, dst);

	/* Change in oif may mean change in hh_len. */
	hh_len = skb_dst(skb)->dev->hard_header_len;
	if (skb_headroom(skb) < hh_len &&
	    pskb_expand_head(skb, hh_len - skb_headroom(skb), 0, GFP_ATOMIC))
		return -ENOMEM;
	return 0;
}
EXPORT_SYMBOL(nf_xfrm_me_harder);
#endif /* CONFIG_XFRM */

/* We keep an extra hash for each conntrack, for fast searching. */
static unsigned int
hash_by_src(const struct net *n, const struct nf_conntrack_tuple *tuple)
{
	unsigned int hash;

	get_random_once(&nf_nat_hash_rnd, sizeof(nf_nat_hash_rnd));

	/* Original src, to ensure we map it consistently if poss. */
	hash = jhash2((u32 *)&tuple->src, sizeof(tuple->src) / sizeof(u32),
		      tuple->dst.protonum ^ nf_nat_hash_rnd ^ net_hash_mix(n));

	return reciprocal_scale(hash, nf_nat_htable_size);
}

/* Is this tuple already taken? (not by us) */
static int
nf_nat_used_tuple(const struct nf_conntrack_tuple *tuple,
		  const struct nf_conn *ignored_conntrack)
{
	/* Conntrack tracking doesn't keep track of outgoing tuples; only
	 * incoming ones.  NAT means they don't have a fixed mapping,
	 * so we invert the tuple and look for the incoming reply.
	 *
	 * We could keep a separate hash if this proves too slow.
	 */
	struct nf_conntrack_tuple reply;

	nf_ct_invert_tuple(&reply, tuple);
	return nf_conntrack_tuple_taken(&reply, ignored_conntrack);
}

static bool nf_nat_inet_in_range(const struct nf_conntrack_tuple *t,
				 const struct nf_nat_range2 *range)
{
	if (t->src.l3num == NFPROTO_IPV4)
		return ntohl(t->src.u3.ip) >= ntohl(range->min_addr.ip) &&
		       ntohl(t->src.u3.ip) <= ntohl(range->max_addr.ip);

	return ipv6_addr_cmp(&t->src.u3.in6, &range->min_addr.in6) >= 0 &&
	       ipv6_addr_cmp(&t->src.u3.in6, &range->max_addr.in6) <= 0;
}

/* Is the manipable part of the tuple between min and max incl? */
static bool l4proto_in_range(const struct nf_conntrack_tuple *tuple,
			     enum nf_nat_manip_type maniptype,
			     const union nf_conntrack_man_proto *min,
			     const union nf_conntrack_man_proto *max)
{
	__be16 port;

	switch (tuple->dst.protonum) {
	case IPPROTO_ICMP:
	case IPPROTO_ICMPV6:
		return ntohs(tuple->src.u.icmp.id) >= ntohs(min->icmp.id) &&
		       ntohs(tuple->src.u.icmp.id) <= ntohs(max->icmp.id);
	case IPPROTO_GRE: /* all fall though */
	case IPPROTO_TCP:
	case IPPROTO_UDP:
	case IPPROTO_UDPLITE:
	case IPPROTO_DCCP:
	case IPPROTO_SCTP:
		if (maniptype == NF_NAT_MANIP_SRC)
			port = tuple->src.u.all;
		else
			port = tuple->dst.u.all;

		return ntohs(port) >= ntohs(min->all) &&
		       ntohs(port) <= ntohs(max->all);
	default:
		return true;
	}
}

/* If we source map this tuple so reply looks like reply_tuple, will
 * that meet the constraints of range.
 */
static int in_range(const struct nf_conntrack_tuple *tuple,
		    const struct nf_nat_range2 *range)
{
	/* If we are supposed to map IPs, then we must be in the
	 * range specified, otherwise let this drag us onto a new src IP.
	 */
	if (range->flags & NF_NAT_RANGE_MAP_IPS &&
	    !nf_nat_inet_in_range(tuple, range))
		return 0;

	if (!(range->flags & NF_NAT_RANGE_PROTO_SPECIFIED))
		return 1;

	return l4proto_in_range(tuple, NF_NAT_MANIP_SRC,
				&range->min_proto, &range->max_proto);
}

static inline int
same_src(const struct nf_conn *ct,
	 const struct nf_conntrack_tuple *tuple)
{
	const struct nf_conntrack_tuple *t;

	t = &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple;
	return (t->dst.protonum == tuple->dst.protonum &&
		nf_inet_addr_cmp(&t->src.u3, &tuple->src.u3) &&
		t->src.u.all == tuple->src.u.all);
}

/* Only called for SRC manip */
static int
find_appropriate_src(struct net *net,
		     const struct nf_conntrack_zone *zone,
		     const struct nf_conntrack_tuple *tuple,
		     struct nf_conntrack_tuple *result,
		     const struct nf_nat_range2 *range)
{
	unsigned int h = hash_by_src(net, tuple);
	const struct nf_conn *ct;

	hlist_for_each_entry_rcu(ct, &nf_nat_bysource[h], nat_bysource) {
		if (same_src(ct, tuple) &&
		    net_eq(net, nf_ct_net(ct)) &&
		    nf_ct_zone_equal(ct, zone, IP_CT_DIR_ORIGINAL)) {
			/* Copy source part from reply tuple. */
			nf_ct_invert_tuple(result,
				       &ct->tuplehash[IP_CT_DIR_REPLY].tuple);
			result->dst = tuple->dst;

			if (in_range(result, range))
				return 1;
		}
	}
	return 0;
}

/* For [FUTURE] fragmentation handling, we want the least-used
 * src-ip/dst-ip/proto triple.  Fairness doesn't come into it.  Thus
 * if the range specifies 1.2.3.4 ports 10000-10005 and 1.2.3.5 ports
 * 1-65535, we don't do pro-rata allocation based on ports; we choose
 * the ip with the lowest src-ip/dst-ip/proto usage.
 */
static void
find_best_ips_proto(const struct nf_conntrack_zone *zone,
		    struct nf_conntrack_tuple *tuple,
		    const struct nf_nat_range2 *range,
		    const struct nf_conn *ct,
		    enum nf_nat_manip_type maniptype)
{
	union nf_inet_addr *var_ipp;
	unsigned int i, max;
	/* Host order */
	u32 minip, maxip, j, dist;
	bool full_range;

	/* No IP mapping?  Do nothing. */
	if (!(range->flags & NF_NAT_RANGE_MAP_IPS))
		return;

	if (maniptype == NF_NAT_MANIP_SRC)
		var_ipp = &tuple->src.u3;
	else
		var_ipp = &tuple->dst.u3;

	/* Fast path: only one choice. */
	if (nf_inet_addr_cmp(&range->min_addr, &range->max_addr)) {
		*var_ipp = range->min_addr;
		return;
	}

	if (nf_ct_l3num(ct) == NFPROTO_IPV4)
		max = sizeof(var_ipp->ip) / sizeof(u32) - 1;
	else
		max = sizeof(var_ipp->ip6) / sizeof(u32) - 1;

	/* Hashing source and destination IPs gives a fairly even
	 * spread in practice (if there are a small number of IPs
	 * involved, there usually aren't that many connections
	 * anyway).  The consistency means that servers see the same
	 * client coming from the same IP (some Internet Banking sites
	 * like this), even across reboots.
	 */
	j = jhash2((u32 *)&tuple->src.u3, sizeof(tuple->src.u3) / sizeof(u32),
		   range->flags & NF_NAT_RANGE_PERSISTENT ?
			0 : (__force u32)tuple->dst.u3.all[max] ^ zone->id);

	full_range = false;
	for (i = 0; i <= max; i++) {
		/* If first bytes of the address are at the maximum, use the
		 * distance. Otherwise use the full range.
		 */
		if (!full_range) {
			minip = ntohl((__force __be32)range->min_addr.all[i]);
			maxip = ntohl((__force __be32)range->max_addr.all[i]);
			dist  = maxip - minip + 1;
		} else {
			minip = 0;
			dist  = ~0;
		}

		var_ipp->all[i] = (__force __u32)
			htonl(minip + reciprocal_scale(j, dist));
		if (var_ipp->all[i] != range->max_addr.all[i])
			full_range = true;

		if (!(range->flags & NF_NAT_RANGE_PERSISTENT))
			j ^= (__force u32)tuple->dst.u3.all[i];
	}
}

/* Alter the per-proto part of the tuple (depending on maniptype), to
 * give a unique tuple in the given range if possible.
 *
 * Per-protocol part of tuple is initialized to the incoming packet.
 */
static void nf_nat_l4proto_unique_tuple(struct nf_conntrack_tuple *tuple,
					const struct nf_nat_range2 *range,
					enum nf_nat_manip_type maniptype,
					const struct nf_conn *ct)
{
	unsigned int range_size, min, max, i, attempts;
	__be16 *keyptr;
	u16 off;
	static const unsigned int max_attempts = 128;

	switch (tuple->dst.protonum) {
	case IPPROTO_ICMP: /* fallthrough */
	case IPPROTO_ICMPV6:
		/* id is same for either direction... */
		keyptr = &tuple->src.u.icmp.id;
		if (!(range->flags & NF_NAT_RANGE_PROTO_SPECIFIED)) {
			min = 0;
			range_size = 65536;
		} else {
			min = ntohs(range->min_proto.icmp.id);
			range_size = ntohs(range->max_proto.icmp.id) -
				     ntohs(range->min_proto.icmp.id) + 1;
		}
		goto find_free_id;
#if IS_ENABLED(CONFIG_NF_CT_PROTO_GRE)
	case IPPROTO_GRE:
		/* If there is no master conntrack we are not PPTP,
		   do not change tuples */
		if (!ct->master)
			return;

		if (maniptype == NF_NAT_MANIP_SRC)
			keyptr = &tuple->src.u.gre.key;
		else
			keyptr = &tuple->dst.u.gre.key;

		if (!(range->flags & NF_NAT_RANGE_PROTO_SPECIFIED)) {
			min = 1;
			range_size = 65535;
		} else {
			min = ntohs(range->min_proto.gre.key);
			range_size = ntohs(range->max_proto.gre.key) - min + 1;
		}
		goto find_free_id;
#endif
	case IPPROTO_UDP:	/* fallthrough */
	case IPPROTO_UDPLITE:	/* fallthrough */
	case IPPROTO_TCP:	/* fallthrough */
	case IPPROTO_SCTP:	/* fallthrough */
	case IPPROTO_DCCP:	/* fallthrough */
		if (maniptype == NF_NAT_MANIP_SRC)
			keyptr = &tuple->src.u.all;
		else
			keyptr = &tuple->dst.u.all;

		break;
	default:
		return;
	}

	/* If no range specified... */
	if (!(range->flags & NF_NAT_RANGE_PROTO_SPECIFIED)) {
		/* If it's dst rewrite, can't change port */
		if (maniptype == NF_NAT_MANIP_DST)
			return;

		if (ntohs(*keyptr) < 1024) {
			/* Loose convention: >> 512 is credential passing */
			if (ntohs(*keyptr) < 512) {
				min = 1;
				range_size = 511 - min + 1;
			} else {
				min = 600;
				range_size = 1023 - min + 1;
			}
		} else {
			min = 1024;
			range_size = 65535 - 1024 + 1;
		}
	} else {
		min = ntohs(range->min_proto.all);
		max = ntohs(range->max_proto.all);
		if (unlikely(max < min))
			swap(max, min);
		range_size = max - min + 1;
	}

find_free_id:
	if (range->flags & NF_NAT_RANGE_PROTO_OFFSET)
		off = (ntohs(*keyptr) - ntohs(range->base_proto.all));
	else
		off = prandom_u32();

	attempts = range_size;
	if (attempts > max_attempts)
		attempts = max_attempts;

	/* We are in softirq; doing a search of the entire range risks
	 * soft lockup when all tuples are already used.
	 *
	 * If we can't find any free port from first offset, pick a new
	 * one and try again, with ever smaller search window.
	 */
another_round:
	for (i = 0; i < attempts; i++, off++) {
		*keyptr = htons(min + off % range_size);
		if (!nf_nat_used_tuple(tuple, ct))
			return;
	}

	if (attempts >= range_size || attempts < 16)
		return;
	attempts /= 2;
	off = prandom_u32();
	goto another_round;
}

/* Manipulate the tuple into the range given. For NF_INET_POST_ROUTING,
 * we change the source to map into the range. For NF_INET_PRE_ROUTING
 * and NF_INET_LOCAL_OUT, we change the destination to map into the
 * range. It might not be possible to get a unique tuple, but we try.
 * At worst (or if we race), we will end up with a final duplicate in
 * __ip_conntrack_confirm and drop the packet. */
static void
get_unique_tuple(struct nf_conntrack_tuple *tuple,
		 const struct nf_conntrack_tuple *orig_tuple,
		 const struct nf_nat_range2 *range,
		 struct nf_conn *ct,
		 enum nf_nat_manip_type maniptype)
{
	const struct nf_conntrack_zone *zone;
	struct net *net = nf_ct_net(ct);

	zone = nf_ct_zone(ct);

	/* 1) If this srcip/proto/src-proto-part is currently mapped,
	 * and that same mapping gives a unique tuple within the given
	 * range, use that.
	 *
	 * This is only required for source (ie. NAT/masq) mappings.
	 * So far, we don't do local source mappings, so multiple
	 * manips not an issue.
	 */
	if (maniptype == NF_NAT_MANIP_SRC &&
	    !(range->flags & NF_NAT_RANGE_PROTO_RANDOM_ALL)) {
		/* try the original tuple first */
		if (in_range(orig_tuple, range)) {
			if (!nf_nat_used_tuple(orig_tuple, ct)) {
				*tuple = *orig_tuple;
				return;
			}
		} else if (find_appropriate_src(net, zone,
						orig_tuple, tuple, range)) {
			pr_debug("get_unique_tuple: Found current src map\n");
			if (!nf_nat_used_tuple(tuple, ct))
				return;
		}
	}

	/* 2) Select the least-used IP/proto combination in the given range */
	*tuple = *orig_tuple;
	find_best_ips_proto(zone, tuple, range, ct, maniptype);

	/* 3) The per-protocol part of the manip is made to map into
	 * the range to make a unique tuple.
	 */

	/* Only bother mapping if it's not already in range and unique */
	if (!(range->flags & NF_NAT_RANGE_PROTO_RANDOM_ALL)) {
		if (range->flags & NF_NAT_RANGE_PROTO_SPECIFIED) {
			if (!(range->flags & NF_NAT_RANGE_PROTO_OFFSET) &&
			    l4proto_in_range(tuple, maniptype,
			          &range->min_proto,
			          &range->max_proto) &&
			    (range->min_proto.all == range->max_proto.all ||
			     !nf_nat_used_tuple(tuple, ct)))
				return;
		} else if (!nf_nat_used_tuple(tuple, ct)) {
			return;
		}
	}

	/* Last chance: get protocol to try to obtain unique tuple. */
	nf_nat_l4proto_unique_tuple(tuple, range, maniptype, ct);
}

struct nf_conn_nat *nf_ct_nat_ext_add(struct nf_conn *ct)
{
	struct nf_conn_nat *nat = nfct_nat(ct);
	if (nat)
		return nat;

	if (!nf_ct_is_confirmed(ct))
		nat = nf_ct_ext_add(ct, NF_CT_EXT_NAT, GFP_ATOMIC);

	return nat;
}
EXPORT_SYMBOL_GPL(nf_ct_nat_ext_add);

unsigned int
nf_nat_setup_info(struct nf_conn *ct,
		  const struct nf_nat_range2 *range,
		  enum nf_nat_manip_type maniptype)
{
	struct net *net = nf_ct_net(ct);
	struct nf_conntrack_tuple curr_tuple, new_tuple;

	/* Can't setup nat info for confirmed ct. */
	if (nf_ct_is_confirmed(ct))
		return NF_ACCEPT;

	WARN_ON(maniptype != NF_NAT_MANIP_SRC &&
		maniptype != NF_NAT_MANIP_DST);

	if (WARN_ON(nf_nat_initialized(ct, maniptype)))
		return NF_DROP;

	/* What we've got will look like inverse of reply. Normally
	 * this is what is in the conntrack, except for prior
	 * manipulations (future optimization: if num_manips == 0,
	 * orig_tp = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple)
	 */
	nf_ct_invert_tuple(&curr_tuple,
			   &ct->tuplehash[IP_CT_DIR_REPLY].tuple);

	get_unique_tuple(&new_tuple, &curr_tuple, range, ct, maniptype);

	if (!nf_ct_tuple_equal(&new_tuple, &curr_tuple)) {
		struct nf_conntrack_tuple reply;

		/* Alter conntrack table so will recognize replies. */
		nf_ct_invert_tuple(&reply, &new_tuple);
		nf_conntrack_alter_reply(ct, &reply);

		/* Non-atomic: we own this at the moment. */
		if (maniptype == NF_NAT_MANIP_SRC)
			ct->status |= IPS_SRC_NAT;
		else
			ct->status |= IPS_DST_NAT;

		if (nfct_help(ct) && !nfct_seqadj(ct))
			if (!nfct_seqadj_ext_add(ct))
				return NF_DROP;
	}

	if (maniptype == NF_NAT_MANIP_SRC) {
		unsigned int srchash;
		spinlock_t *lock;

		srchash = hash_by_src(net,
				      &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple);
		lock = &nf_nat_locks[srchash % CONNTRACK_LOCKS];
		spin_lock_bh(lock);
		hlist_add_head_rcu(&ct->nat_bysource,
				   &nf_nat_bysource[srchash]);
		spin_unlock_bh(lock);
	}

	/* It's done. */
	if (maniptype == NF_NAT_MANIP_DST)
		ct->status |= IPS_DST_NAT_DONE;
	else
		ct->status |= IPS_SRC_NAT_DONE;

	return NF_ACCEPT;
}
EXPORT_SYMBOL(nf_nat_setup_info);

static unsigned int
__nf_nat_alloc_null_binding(struct nf_conn *ct, enum nf_nat_manip_type manip)
{
	/* Force range to this IP; let proto decide mapping for
	 * per-proto parts (hence not IP_NAT_RANGE_PROTO_SPECIFIED).
	 * Use reply in case it's already been mangled (eg local packet).
	 */
	union nf_inet_addr ip =
		(manip == NF_NAT_MANIP_SRC ?
		ct->tuplehash[IP_CT_DIR_REPLY].tuple.dst.u3 :
		ct->tuplehash[IP_CT_DIR_REPLY].tuple.src.u3);
	struct nf_nat_range2 range = {
		.flags		= NF_NAT_RANGE_MAP_IPS,
		.min_addr	= ip,
		.max_addr	= ip,
	};
	return nf_nat_setup_info(ct, &range, manip);
}

unsigned int
nf_nat_alloc_null_binding(struct nf_conn *ct, unsigned int hooknum)
{
	return __nf_nat_alloc_null_binding(ct, HOOK2MANIP(hooknum));
}
EXPORT_SYMBOL_GPL(nf_nat_alloc_null_binding);

/* Do packet manipulations according to nf_nat_setup_info. */
unsigned int nf_nat_packet(struct nf_conn *ct,
			   enum ip_conntrack_info ctinfo,
			   unsigned int hooknum,
			   struct sk_buff *skb)
{
	enum nf_nat_manip_type mtype = HOOK2MANIP(hooknum);
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	unsigned int verdict = NF_ACCEPT;
	unsigned long statusbit;

	if (mtype == NF_NAT_MANIP_SRC)
		statusbit = IPS_SRC_NAT;
	else
		statusbit = IPS_DST_NAT;

	/* Invert if this is reply dir. */
	if (dir == IP_CT_DIR_REPLY)
		statusbit ^= IPS_NAT_MASK;

	/* Non-atomic: these bits don't change. */
	if (ct->status & statusbit)
		verdict = nf_nat_manip_pkt(skb, ct, mtype, dir);

	return verdict;
}
EXPORT_SYMBOL_GPL(nf_nat_packet);

unsigned int
nf_nat_inet_fn(void *priv, struct sk_buff *skb,
	       const struct nf_hook_state *state)
{
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	struct nf_conn_nat *nat;
	/* maniptype == SRC for postrouting. */
	enum nf_nat_manip_type maniptype = HOOK2MANIP(state->hook);

	ct = nf_ct_get(skb, &ctinfo);
	/* Can't track?  It's not due to stress, or conntrack would
	 * have dropped it.  Hence it's the user's responsibilty to
	 * packet filter it out, or implement conntrack/NAT for that
	 * protocol. 8) --RR
	 */
	if (!ct)
		return NF_ACCEPT;

	nat = nfct_nat(ct);

	switch (ctinfo) {
	case IP_CT_RELATED:
	case IP_CT_RELATED_REPLY:
		/* Only ICMPs can be IP_CT_IS_REPLY.  Fallthrough */
	case IP_CT_NEW:
		/* Seen it before?  This can happen for loopback, retrans,
		 * or local packets.
		 */
		if (!nf_nat_initialized(ct, maniptype)) {
			struct nf_nat_lookup_hook_priv *lpriv = priv;
			struct nf_hook_entries *e = rcu_dereference(lpriv->entries);
			unsigned int ret;
			int i;

			if (!e)
				goto null_bind;

			for (i = 0; i < e->num_hook_entries; i++) {
				ret = e->hooks[i].hook(e->hooks[i].priv, skb,
						       state);
				if (ret != NF_ACCEPT)
					return ret;
				if (nf_nat_initialized(ct, maniptype))
					goto do_nat;
			}
null_bind:
			ret = nf_nat_alloc_null_binding(ct, state->hook);
			if (ret != NF_ACCEPT)
				return ret;
		} else {
			pr_debug("Already setup manip %s for ct %p (status bits 0x%lx)\n",
				 maniptype == NF_NAT_MANIP_SRC ? "SRC" : "DST",
				 ct, ct->status);
			if (nf_nat_oif_changed(state->hook, ctinfo, nat,
					       state->out))
				goto oif_changed;
		}
		break;
	default:
		/* ESTABLISHED */
		WARN_ON(ctinfo != IP_CT_ESTABLISHED &&
			ctinfo != IP_CT_ESTABLISHED_REPLY);
		if (nf_nat_oif_changed(state->hook, ctinfo, nat, state->out))
			goto oif_changed;
	}
do_nat:
	return nf_nat_packet(ct, ctinfo, state->hook, skb);

oif_changed:
	nf_ct_kill_acct(ct, ctinfo, skb);
	return NF_DROP;
}
EXPORT_SYMBOL_GPL(nf_nat_inet_fn);

struct nf_nat_proto_clean {
	u8	l3proto;
	u8	l4proto;
};

/* kill conntracks with affected NAT section */
static int nf_nat_proto_remove(struct nf_conn *i, void *data)
{
	const struct nf_nat_proto_clean *clean = data;

	if ((clean->l3proto && nf_ct_l3num(i) != clean->l3proto) ||
	    (clean->l4proto && nf_ct_protonum(i) != clean->l4proto))
		return 0;

	return i->status & IPS_NAT_MASK ? 1 : 0;
}

static void __nf_nat_cleanup_conntrack(struct nf_conn *ct)
{
	unsigned int h;

	h = hash_by_src(nf_ct_net(ct), &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple);
	spin_lock_bh(&nf_nat_locks[h % CONNTRACK_LOCKS]);
	hlist_del_rcu(&ct->nat_bysource);
	spin_unlock_bh(&nf_nat_locks[h % CONNTRACK_LOCKS]);
}

static int nf_nat_proto_clean(struct nf_conn *ct, void *data)
{
	if (nf_nat_proto_remove(ct, data))
		return 1;

	/* This module is being removed and conntrack has nat null binding.
	 * Remove it from bysource hash, as the table will be freed soon.
	 *
	 * Else, when the conntrack is destoyed, nf_nat_cleanup_conntrack()
	 * will delete entry from already-freed table.
	 */
	if (test_and_clear_bit(IPS_SRC_NAT_DONE_BIT, &ct->status))
		__nf_nat_cleanup_conntrack(ct);

	/* don't delete conntrack.  Although that would make things a lot
	 * simpler, we'd end up flushing all conntracks on nat rmmod.
	 */
	return 0;
}

/* No one using conntrack by the time this called. */
static void nf_nat_cleanup_conntrack(struct nf_conn *ct)
{
	if (ct->status & IPS_SRC_NAT_DONE)
		__nf_nat_cleanup_conntrack(ct);
}

static struct nf_ct_ext_type nat_extend __read_mostly = {
	.len		= sizeof(struct nf_conn_nat),
	.align		= __alignof__(struct nf_conn_nat),
	.destroy	= nf_nat_cleanup_conntrack,
	.id		= NF_CT_EXT_NAT,
};

#if IS_ENABLED(CONFIG_NF_CT_NETLINK)

#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_conntrack.h>

static const struct nla_policy protonat_nla_policy[CTA_PROTONAT_MAX+1] = {
	[CTA_PROTONAT_PORT_MIN]	= { .type = NLA_U16 },
	[CTA_PROTONAT_PORT_MAX]	= { .type = NLA_U16 },
};

static int nf_nat_l4proto_nlattr_to_range(struct nlattr *tb[],
					  struct nf_nat_range2 *range)
{
	if (tb[CTA_PROTONAT_PORT_MIN]) {
		range->min_proto.all = nla_get_be16(tb[CTA_PROTONAT_PORT_MIN]);
		range->max_proto.all = range->min_proto.all;
		range->flags |= NF_NAT_RANGE_PROTO_SPECIFIED;
	}
	if (tb[CTA_PROTONAT_PORT_MAX]) {
		range->max_proto.all = nla_get_be16(tb[CTA_PROTONAT_PORT_MAX]);
		range->flags |= NF_NAT_RANGE_PROTO_SPECIFIED;
	}
	return 0;
}

static int nfnetlink_parse_nat_proto(struct nlattr *attr,
				     const struct nf_conn *ct,
				     struct nf_nat_range2 *range)
{
	struct nlattr *tb[CTA_PROTONAT_MAX+1];
	int err;

	err = nla_parse_nested(tb, CTA_PROTONAT_MAX, attr,
			       protonat_nla_policy, NULL);
	if (err < 0)
		return err;

	return nf_nat_l4proto_nlattr_to_range(tb, range);
}

static const struct nla_policy nat_nla_policy[CTA_NAT_MAX+1] = {
	[CTA_NAT_V4_MINIP]	= { .type = NLA_U32 },
	[CTA_NAT_V4_MAXIP]	= { .type = NLA_U32 },
	[CTA_NAT_V6_MINIP]	= { .len = sizeof(struct in6_addr) },
	[CTA_NAT_V6_MAXIP]	= { .len = sizeof(struct in6_addr) },
	[CTA_NAT_PROTO]		= { .type = NLA_NESTED },
};

static int nf_nat_ipv4_nlattr_to_range(struct nlattr *tb[],
				       struct nf_nat_range2 *range)
{
	if (tb[CTA_NAT_V4_MINIP]) {
		range->min_addr.ip = nla_get_be32(tb[CTA_NAT_V4_MINIP]);
		range->flags |= NF_NAT_RANGE_MAP_IPS;
	}

	if (tb[CTA_NAT_V4_MAXIP])
		range->max_addr.ip = nla_get_be32(tb[CTA_NAT_V4_MAXIP]);
	else
		range->max_addr.ip = range->min_addr.ip;

	return 0;
}

static int nf_nat_ipv6_nlattr_to_range(struct nlattr *tb[],
				       struct nf_nat_range2 *range)
{
	if (tb[CTA_NAT_V6_MINIP]) {
		nla_memcpy(&range->min_addr.ip6, tb[CTA_NAT_V6_MINIP],
			   sizeof(struct in6_addr));
		range->flags |= NF_NAT_RANGE_MAP_IPS;
	}

	if (tb[CTA_NAT_V6_MAXIP])
		nla_memcpy(&range->max_addr.ip6, tb[CTA_NAT_V6_MAXIP],
			   sizeof(struct in6_addr));
	else
		range->max_addr = range->min_addr;

	return 0;
}

static int
nfnetlink_parse_nat(const struct nlattr *nat,
		    const struct nf_conn *ct, struct nf_nat_range2 *range)
{
	struct nlattr *tb[CTA_NAT_MAX+1];
	int err;

	memset(range, 0, sizeof(*range));

	err = nla_parse_nested(tb, CTA_NAT_MAX, nat, nat_nla_policy, NULL);
	if (err < 0)
		return err;

	switch (nf_ct_l3num(ct)) {
	case NFPROTO_IPV4:
		err = nf_nat_ipv4_nlattr_to_range(tb, range);
		break;
	case NFPROTO_IPV6:
		err = nf_nat_ipv6_nlattr_to_range(tb, range);
		break;
	default:
		err = -EPROTONOSUPPORT;
		break;
	}

	if (err)
		return err;

	if (!tb[CTA_NAT_PROTO])
		return 0;

	return nfnetlink_parse_nat_proto(tb[CTA_NAT_PROTO], ct, range);
}

/* This function is called under rcu_read_lock() */
static int
nfnetlink_parse_nat_setup(struct nf_conn *ct,
			  enum nf_nat_manip_type manip,
			  const struct nlattr *attr)
{
	struct nf_nat_range2 range;
	int err;

	/* Should not happen, restricted to creating new conntracks
	 * via ctnetlink.
	 */
	if (WARN_ON_ONCE(nf_nat_initialized(ct, manip)))
		return -EEXIST;

	/* No NAT information has been passed, allocate the null-binding */
	if (attr == NULL)
		return __nf_nat_alloc_null_binding(ct, manip) == NF_DROP ? -ENOMEM : 0;

	err = nfnetlink_parse_nat(attr, ct, &range);
	if (err < 0)
		return err;

	return nf_nat_setup_info(ct, &range, manip) == NF_DROP ? -ENOMEM : 0;
}
#else
static int
nfnetlink_parse_nat_setup(struct nf_conn *ct,
			  enum nf_nat_manip_type manip,
			  const struct nlattr *attr)
{
	return -EOPNOTSUPP;
}
#endif

static struct nf_ct_helper_expectfn follow_master_nat = {
	.name		= "nat-follow-master",
	.expectfn	= nf_nat_follow_master,
};

int nf_nat_register_fn(struct net *net, const struct nf_hook_ops *ops,
		       const struct nf_hook_ops *orig_nat_ops, unsigned int ops_count)
{
	struct nat_net *nat_net = net_generic(net, nat_net_id);
	struct nf_nat_hooks_net *nat_proto_net;
	struct nf_nat_lookup_hook_priv *priv;
	unsigned int hooknum = ops->hooknum;
	struct nf_hook_ops *nat_ops;
	int i, ret;

	if (WARN_ON_ONCE(ops->pf >= ARRAY_SIZE(nat_net->nat_proto_net)))
		return -EINVAL;

	nat_proto_net = &nat_net->nat_proto_net[ops->pf];

	for (i = 0; i < ops_count; i++) {
		if (WARN_ON(orig_nat_ops[i].pf != ops->pf))
			return -EINVAL;
		if (orig_nat_ops[i].hooknum == hooknum) {
			hooknum = i;
			break;
		}
	}

	if (WARN_ON_ONCE(i == ops_count))
		return -EINVAL;

	mutex_lock(&nf_nat_proto_mutex);
	if (!nat_proto_net->nat_hook_ops) {
		WARN_ON(nat_proto_net->users != 0);

		nat_ops = kmemdup(orig_nat_ops, sizeof(*orig_nat_ops) * ops_count, GFP_KERNEL);
		if (!nat_ops) {
			mutex_unlock(&nf_nat_proto_mutex);
			return -ENOMEM;
		}

		for (i = 0; i < ops_count; i++) {
			priv = kzalloc(sizeof(*priv), GFP_KERNEL);
			if (priv) {
				nat_ops[i].priv = priv;
				continue;
			}
			mutex_unlock(&nf_nat_proto_mutex);
			while (i)
				kfree(nat_ops[--i].priv);
			kfree(nat_ops);
			return -ENOMEM;
		}

		ret = nf_register_net_hooks(net, nat_ops, ops_count);
		if (ret < 0) {
			mutex_unlock(&nf_nat_proto_mutex);
			for (i = 0; i < ops_count; i++)
				kfree(nat_ops[i].priv);
			kfree(nat_ops);
			return ret;
		}

		nat_proto_net->nat_hook_ops = nat_ops;
	}

	nat_ops = nat_proto_net->nat_hook_ops;
	priv = nat_ops[hooknum].priv;
	if (WARN_ON_ONCE(!priv)) {
		mutex_unlock(&nf_nat_proto_mutex);
		return -EOPNOTSUPP;
	}

	ret = nf_hook_entries_insert_raw(&priv->entries, ops);
	if (ret == 0)
		nat_proto_net->users++;

	mutex_unlock(&nf_nat_proto_mutex);
	return ret;
}

void nf_nat_unregister_fn(struct net *net, const struct nf_hook_ops *ops,
		          unsigned int ops_count)
{
	struct nat_net *nat_net = net_generic(net, nat_net_id);
	struct nf_nat_hooks_net *nat_proto_net;
	struct nf_nat_lookup_hook_priv *priv;
	struct nf_hook_ops *nat_ops;
	int hooknum = ops->hooknum;
	int i;

	if (ops->pf >= ARRAY_SIZE(nat_net->nat_proto_net))
		return;

	nat_proto_net = &nat_net->nat_proto_net[ops->pf];

	mutex_lock(&nf_nat_proto_mutex);
	if (WARN_ON(nat_proto_net->users == 0))
		goto unlock;

	nat_proto_net->users--;

	nat_ops = nat_proto_net->nat_hook_ops;
	for (i = 0; i < ops_count; i++) {
		if (nat_ops[i].hooknum == hooknum) {
			hooknum = i;
			break;
		}
	}
	if (WARN_ON_ONCE(i == ops_count))
		goto unlock;
	priv = nat_ops[hooknum].priv;
	nf_hook_entries_delete_raw(&priv->entries, ops);

	if (nat_proto_net->users == 0) {
		nf_unregister_net_hooks(net, nat_ops, ops_count);

		for (i = 0; i < ops_count; i++) {
			priv = nat_ops[i].priv;
			kfree_rcu(priv, rcu_head);
		}

		nat_proto_net->nat_hook_ops = NULL;
		kfree(nat_ops);
	}
unlock:
	mutex_unlock(&nf_nat_proto_mutex);
}

static struct pernet_operations nat_net_ops = {
	.id = &nat_net_id,
	.size = sizeof(struct nat_net),
};

static struct nf_nat_hook nat_hook = {
	.parse_nat_setup	= nfnetlink_parse_nat_setup,
#ifdef CONFIG_XFRM
	.decode_session		= __nf_nat_decode_session,
#endif
	.manip_pkt		= nf_nat_manip_pkt,
};

static int __init nf_nat_init(void)
{
	int ret, i;

	/* Leave them the same for the moment. */
	nf_nat_htable_size = nf_conntrack_htable_size;
	if (nf_nat_htable_size < CONNTRACK_LOCKS)
		nf_nat_htable_size = CONNTRACK_LOCKS;

	nf_nat_bysource = nf_ct_alloc_hashtable(&nf_nat_htable_size, 0);
	if (!nf_nat_bysource)
		return -ENOMEM;

	ret = nf_ct_extend_register(&nat_extend);
	if (ret < 0) {
		kvfree(nf_nat_bysource);
		pr_err("Unable to register extension\n");
		return ret;
	}

	for (i = 0; i < CONNTRACK_LOCKS; i++)
		spin_lock_init(&nf_nat_locks[i]);

	ret = register_pernet_subsys(&nat_net_ops);
	if (ret < 0) {
		nf_ct_extend_unregister(&nat_extend);
		return ret;
	}

	nf_ct_helper_expectfn_register(&follow_master_nat);

	WARN_ON(nf_nat_hook != NULL);
	RCU_INIT_POINTER(nf_nat_hook, &nat_hook);

	return 0;
}

static void __exit nf_nat_cleanup(void)
{
	struct nf_nat_proto_clean clean = {};

	nf_ct_iterate_destroy(nf_nat_proto_clean, &clean);

	nf_ct_extend_unregister(&nat_extend);
	nf_ct_helper_expectfn_unregister(&follow_master_nat);
	RCU_INIT_POINTER(nf_nat_hook, NULL);

	synchronize_net();
	kvfree(nf_nat_bysource);
	unregister_pernet_subsys(&nat_net_ops);
}

MODULE_LICENSE("GPL");

module_init(nf_nat_init);
module_exit(nf_nat_cleanup);
