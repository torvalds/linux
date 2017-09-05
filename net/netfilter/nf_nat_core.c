/*
 * (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 * (C) 2011 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

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
#include <net/netfilter/nf_nat_l3proto.h>
#include <net/netfilter/nf_nat_l4proto.h>
#include <net/netfilter/nf_nat_core.h>
#include <net/netfilter/nf_nat_helper.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_seqadj.h>
#include <net/netfilter/nf_conntrack_l3proto.h>
#include <net/netfilter/nf_conntrack_zones.h>
#include <linux/netfilter/nf_nat.h>

static DEFINE_MUTEX(nf_nat_proto_mutex);
static const struct nf_nat_l3proto __rcu *nf_nat_l3protos[NFPROTO_NUMPROTO]
						__read_mostly;
static const struct nf_nat_l4proto __rcu **nf_nat_l4protos[NFPROTO_NUMPROTO]
						__read_mostly;

struct nf_nat_conn_key {
	const struct net *net;
	const struct nf_conntrack_tuple *tuple;
	const struct nf_conntrack_zone *zone;
};

static struct rhltable nf_nat_bysource_table;

inline const struct nf_nat_l3proto *
__nf_nat_l3proto_find(u8 family)
{
	return rcu_dereference(nf_nat_l3protos[family]);
}

inline const struct nf_nat_l4proto *
__nf_nat_l4proto_find(u8 family, u8 protonum)
{
	return rcu_dereference(nf_nat_l4protos[family][protonum]);
}
EXPORT_SYMBOL_GPL(__nf_nat_l4proto_find);

#ifdef CONFIG_XFRM
static void __nf_nat_decode_session(struct sk_buff *skb, struct flowi *fl)
{
	const struct nf_nat_l3proto *l3proto;
	const struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	enum ip_conntrack_dir dir;
	unsigned  long statusbit;
	u8 family;

	ct = nf_ct_get(skb, &ctinfo);
	if (ct == NULL)
		return;

	family = nf_ct_l3num(ct);
	l3proto = __nf_nat_l3proto_find(family);
	if (l3proto == NULL)
		return;

	dir = CTINFO2DIR(ctinfo);
	if (dir == IP_CT_DIR_ORIGINAL)
		statusbit = IPS_DST_NAT;
	else
		statusbit = IPS_SRC_NAT;

	l3proto->decode_session(skb, ct, dir, statusbit, fl);
}

int nf_xfrm_me_harder(struct net *net, struct sk_buff *skb, unsigned int family)
{
	struct flowi fl;
	unsigned int hh_len;
	struct dst_entry *dst;
	int err;

	err = xfrm_decode_session(skb, &fl, family);
	if (err < 0)
		return err;

	dst = skb_dst(skb);
	if (dst->xfrm)
		dst = ((struct xfrm_dst *)dst)->route;
	dst_hold(dst);

	dst = xfrm_lookup(net, dst, &fl, skb->sk, 0);
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

static u32 nf_nat_bysource_hash(const void *data, u32 len, u32 seed)
{
	const struct nf_conntrack_tuple *t;
	const struct nf_conn *ct = data;

	t = &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple;
	/* Original src, to ensure we map it consistently if poss. */

	seed ^= net_hash_mix(nf_ct_net(ct));
	return jhash2((const u32 *)&t->src, sizeof(t->src) / sizeof(u32),
		      t->dst.protonum ^ seed);
}

/* Is this tuple already taken? (not by us) */
int
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

	nf_ct_invert_tuplepr(&reply, tuple);
	return nf_conntrack_tuple_taken(&reply, ignored_conntrack);
}
EXPORT_SYMBOL(nf_nat_used_tuple);

/* If we source map this tuple so reply looks like reply_tuple, will
 * that meet the constraints of range.
 */
static int in_range(const struct nf_nat_l3proto *l3proto,
		    const struct nf_nat_l4proto *l4proto,
		    const struct nf_conntrack_tuple *tuple,
		    const struct nf_nat_range *range)
{
	/* If we are supposed to map IPs, then we must be in the
	 * range specified, otherwise let this drag us onto a new src IP.
	 */
	if (range->flags & NF_NAT_RANGE_MAP_IPS &&
	    !l3proto->in_range(tuple, range))
		return 0;

	if (!(range->flags & NF_NAT_RANGE_PROTO_SPECIFIED) ||
	    l4proto->in_range(tuple, NF_NAT_MANIP_SRC,
			      &range->min_proto, &range->max_proto))
		return 1;

	return 0;
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

static int nf_nat_bysource_cmp(struct rhashtable_compare_arg *arg,
			       const void *obj)
{
	const struct nf_nat_conn_key *key = arg->key;
	const struct nf_conn *ct = obj;

	if (!same_src(ct, key->tuple) ||
	    !net_eq(nf_ct_net(ct), key->net) ||
	    !nf_ct_zone_equal(ct, key->zone, IP_CT_DIR_ORIGINAL))
		return 1;

	return 0;
}

static struct rhashtable_params nf_nat_bysource_params = {
	.head_offset = offsetof(struct nf_conn, nat_bysource),
	.obj_hashfn = nf_nat_bysource_hash,
	.obj_cmpfn = nf_nat_bysource_cmp,
	.nelem_hint = 256,
	.min_size = 1024,
};

/* Only called for SRC manip */
static int
find_appropriate_src(struct net *net,
		     const struct nf_conntrack_zone *zone,
		     const struct nf_nat_l3proto *l3proto,
		     const struct nf_nat_l4proto *l4proto,
		     const struct nf_conntrack_tuple *tuple,
		     struct nf_conntrack_tuple *result,
		     const struct nf_nat_range *range)
{
	const struct nf_conn *ct;
	struct nf_nat_conn_key key = {
		.net = net,
		.tuple = tuple,
		.zone = zone
	};
	struct rhlist_head *hl, *h;

	hl = rhltable_lookup(&nf_nat_bysource_table, &key,
			     nf_nat_bysource_params);

	rhl_for_each_entry_rcu(ct, h, hl, nat_bysource) {
		nf_ct_invert_tuplepr(result,
				     &ct->tuplehash[IP_CT_DIR_REPLY].tuple);
		result->dst = tuple->dst;

		if (in_range(l3proto, l4proto, result, range))
			return 1;
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
		    const struct nf_nat_range *range,
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

/* Manipulate the tuple into the range given. For NF_INET_POST_ROUTING,
 * we change the source to map into the range. For NF_INET_PRE_ROUTING
 * and NF_INET_LOCAL_OUT, we change the destination to map into the
 * range. It might not be possible to get a unique tuple, but we try.
 * At worst (or if we race), we will end up with a final duplicate in
 * __ip_conntrack_confirm and drop the packet. */
static void
get_unique_tuple(struct nf_conntrack_tuple *tuple,
		 const struct nf_conntrack_tuple *orig_tuple,
		 const struct nf_nat_range *range,
		 struct nf_conn *ct,
		 enum nf_nat_manip_type maniptype)
{
	const struct nf_conntrack_zone *zone;
	const struct nf_nat_l3proto *l3proto;
	const struct nf_nat_l4proto *l4proto;
	struct net *net = nf_ct_net(ct);

	zone = nf_ct_zone(ct);

	rcu_read_lock();
	l3proto = __nf_nat_l3proto_find(orig_tuple->src.l3num);
	l4proto = __nf_nat_l4proto_find(orig_tuple->src.l3num,
					orig_tuple->dst.protonum);

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
		if (in_range(l3proto, l4proto, orig_tuple, range)) {
			if (!nf_nat_used_tuple(orig_tuple, ct)) {
				*tuple = *orig_tuple;
				goto out;
			}
		} else if (find_appropriate_src(net, zone, l3proto, l4proto,
						orig_tuple, tuple, range)) {
			pr_debug("get_unique_tuple: Found current src map\n");
			if (!nf_nat_used_tuple(tuple, ct))
				goto out;
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
			if (l4proto->in_range(tuple, maniptype,
					      &range->min_proto,
					      &range->max_proto) &&
			    (range->min_proto.all == range->max_proto.all ||
			     !nf_nat_used_tuple(tuple, ct)))
				goto out;
		} else if (!nf_nat_used_tuple(tuple, ct)) {
			goto out;
		}
	}

	/* Last change: get protocol to try to obtain unique tuple. */
	l4proto->unique_tuple(l3proto, tuple, range, maniptype, ct);
out:
	rcu_read_unlock();
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
		  const struct nf_nat_range *range,
		  enum nf_nat_manip_type maniptype)
{
	struct nf_conntrack_tuple curr_tuple, new_tuple;

	/* Can't setup nat info for confirmed ct. */
	if (nf_ct_is_confirmed(ct))
		return NF_ACCEPT;

	NF_CT_ASSERT(maniptype == NF_NAT_MANIP_SRC ||
		     maniptype == NF_NAT_MANIP_DST);
	BUG_ON(nf_nat_initialized(ct, maniptype));

	/* What we've got will look like inverse of reply. Normally
	 * this is what is in the conntrack, except for prior
	 * manipulations (future optimization: if num_manips == 0,
	 * orig_tp = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple)
	 */
	nf_ct_invert_tuplepr(&curr_tuple,
			     &ct->tuplehash[IP_CT_DIR_REPLY].tuple);

	get_unique_tuple(&new_tuple, &curr_tuple, range, ct, maniptype);

	if (!nf_ct_tuple_equal(&new_tuple, &curr_tuple)) {
		struct nf_conntrack_tuple reply;

		/* Alter conntrack table so will recognize replies. */
		nf_ct_invert_tuplepr(&reply, &new_tuple);
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
		struct nf_nat_conn_key key = {
			.net = nf_ct_net(ct),
			.tuple = &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple,
			.zone = nf_ct_zone(ct),
		};
		int err;

		err = rhltable_insert_key(&nf_nat_bysource_table,
					  &key,
					  &ct->nat_bysource,
					  nf_nat_bysource_params);
		if (err)
			return NF_DROP;
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
	struct nf_nat_range range = {
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
	const struct nf_nat_l3proto *l3proto;
	const struct nf_nat_l4proto *l4proto;
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	unsigned long statusbit;
	enum nf_nat_manip_type mtype = HOOK2MANIP(hooknum);

	if (mtype == NF_NAT_MANIP_SRC)
		statusbit = IPS_SRC_NAT;
	else
		statusbit = IPS_DST_NAT;

	/* Invert if this is reply dir. */
	if (dir == IP_CT_DIR_REPLY)
		statusbit ^= IPS_NAT_MASK;

	/* Non-atomic: these bits don't change. */
	if (ct->status & statusbit) {
		struct nf_conntrack_tuple target;

		/* We are aiming to look like inverse of other direction. */
		nf_ct_invert_tuplepr(&target, &ct->tuplehash[!dir].tuple);

		l3proto = __nf_nat_l3proto_find(target.src.l3num);
		l4proto = __nf_nat_l4proto_find(target.src.l3num,
						target.dst.protonum);
		if (!l3proto->manip_pkt(skb, 0, l4proto, &target, mtype))
			return NF_DROP;
	}
	return NF_ACCEPT;
}
EXPORT_SYMBOL_GPL(nf_nat_packet);

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

static int nf_nat_proto_clean(struct nf_conn *ct, void *data)
{
	if (nf_nat_proto_remove(ct, data))
		return 1;

	if ((ct->status & IPS_SRC_NAT_DONE) == 0)
		return 0;

	/* This netns is being destroyed, and conntrack has nat null binding.
	 * Remove it from bysource hash, as the table will be freed soon.
	 *
	 * Else, when the conntrack is destoyed, nf_nat_cleanup_conntrack()
	 * will delete entry from already-freed table.
	 */
	clear_bit(IPS_SRC_NAT_DONE_BIT, &ct->status);
	rhltable_remove(&nf_nat_bysource_table, &ct->nat_bysource,
			nf_nat_bysource_params);

	/* don't delete conntrack.  Although that would make things a lot
	 * simpler, we'd end up flushing all conntracks on nat rmmod.
	 */
	return 0;
}

static void nf_nat_l4proto_clean(u8 l3proto, u8 l4proto)
{
	struct nf_nat_proto_clean clean = {
		.l3proto = l3proto,
		.l4proto = l4proto,
	};

	nf_ct_iterate_destroy(nf_nat_proto_remove, &clean);
}

static void nf_nat_l3proto_clean(u8 l3proto)
{
	struct nf_nat_proto_clean clean = {
		.l3proto = l3proto,
	};

	nf_ct_iterate_destroy(nf_nat_proto_remove, &clean);
}

/* Protocol registration. */
int nf_nat_l4proto_register(u8 l3proto, const struct nf_nat_l4proto *l4proto)
{
	const struct nf_nat_l4proto **l4protos;
	unsigned int i;
	int ret = 0;

	mutex_lock(&nf_nat_proto_mutex);
	if (nf_nat_l4protos[l3proto] == NULL) {
		l4protos = kmalloc(IPPROTO_MAX * sizeof(struct nf_nat_l4proto *),
				   GFP_KERNEL);
		if (l4protos == NULL) {
			ret = -ENOMEM;
			goto out;
		}

		for (i = 0; i < IPPROTO_MAX; i++)
			RCU_INIT_POINTER(l4protos[i], &nf_nat_l4proto_unknown);

		/* Before making proto_array visible to lockless readers,
		 * we must make sure its content is committed to memory.
		 */
		smp_wmb();

		nf_nat_l4protos[l3proto] = l4protos;
	}

	if (rcu_dereference_protected(
			nf_nat_l4protos[l3proto][l4proto->l4proto],
			lockdep_is_held(&nf_nat_proto_mutex)
			) != &nf_nat_l4proto_unknown) {
		ret = -EBUSY;
		goto out;
	}
	RCU_INIT_POINTER(nf_nat_l4protos[l3proto][l4proto->l4proto], l4proto);
 out:
	mutex_unlock(&nf_nat_proto_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(nf_nat_l4proto_register);

/* No one stores the protocol anywhere; simply delete it. */
void nf_nat_l4proto_unregister(u8 l3proto, const struct nf_nat_l4proto *l4proto)
{
	mutex_lock(&nf_nat_proto_mutex);
	RCU_INIT_POINTER(nf_nat_l4protos[l3proto][l4proto->l4proto],
			 &nf_nat_l4proto_unknown);
	mutex_unlock(&nf_nat_proto_mutex);
	synchronize_rcu();

	nf_nat_l4proto_clean(l3proto, l4proto->l4proto);
}
EXPORT_SYMBOL_GPL(nf_nat_l4proto_unregister);

int nf_nat_l3proto_register(const struct nf_nat_l3proto *l3proto)
{
	int err;

	err = nf_ct_l3proto_try_module_get(l3proto->l3proto);
	if (err < 0)
		return err;

	mutex_lock(&nf_nat_proto_mutex);
	RCU_INIT_POINTER(nf_nat_l4protos[l3proto->l3proto][IPPROTO_TCP],
			 &nf_nat_l4proto_tcp);
	RCU_INIT_POINTER(nf_nat_l4protos[l3proto->l3proto][IPPROTO_UDP],
			 &nf_nat_l4proto_udp);
#ifdef CONFIG_NF_NAT_PROTO_DCCP
	RCU_INIT_POINTER(nf_nat_l4protos[l3proto->l3proto][IPPROTO_DCCP],
			 &nf_nat_l4proto_dccp);
#endif
#ifdef CONFIG_NF_NAT_PROTO_SCTP
	RCU_INIT_POINTER(nf_nat_l4protos[l3proto->l3proto][IPPROTO_SCTP],
			 &nf_nat_l4proto_sctp);
#endif
#ifdef CONFIG_NF_NAT_PROTO_UDPLITE
	RCU_INIT_POINTER(nf_nat_l4protos[l3proto->l3proto][IPPROTO_UDPLITE],
			 &nf_nat_l4proto_udplite);
#endif
	mutex_unlock(&nf_nat_proto_mutex);

	RCU_INIT_POINTER(nf_nat_l3protos[l3proto->l3proto], l3proto);
	return 0;
}
EXPORT_SYMBOL_GPL(nf_nat_l3proto_register);

void nf_nat_l3proto_unregister(const struct nf_nat_l3proto *l3proto)
{
	mutex_lock(&nf_nat_proto_mutex);
	RCU_INIT_POINTER(nf_nat_l3protos[l3proto->l3proto], NULL);
	mutex_unlock(&nf_nat_proto_mutex);
	synchronize_rcu();

	nf_nat_l3proto_clean(l3proto->l3proto);
	nf_ct_l3proto_module_put(l3proto->l3proto);
}
EXPORT_SYMBOL_GPL(nf_nat_l3proto_unregister);

/* No one using conntrack by the time this called. */
static void nf_nat_cleanup_conntrack(struct nf_conn *ct)
{
	if (ct->status & IPS_SRC_NAT_DONE)
		rhltable_remove(&nf_nat_bysource_table, &ct->nat_bysource,
				nf_nat_bysource_params);
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

static int nfnetlink_parse_nat_proto(struct nlattr *attr,
				     const struct nf_conn *ct,
				     struct nf_nat_range *range)
{
	struct nlattr *tb[CTA_PROTONAT_MAX+1];
	const struct nf_nat_l4proto *l4proto;
	int err;

	err = nla_parse_nested(tb, CTA_PROTONAT_MAX, attr,
			       protonat_nla_policy, NULL);
	if (err < 0)
		return err;

	l4proto = __nf_nat_l4proto_find(nf_ct_l3num(ct), nf_ct_protonum(ct));
	if (l4proto->nlattr_to_range)
		err = l4proto->nlattr_to_range(tb, range);

	return err;
}

static const struct nla_policy nat_nla_policy[CTA_NAT_MAX+1] = {
	[CTA_NAT_V4_MINIP]	= { .type = NLA_U32 },
	[CTA_NAT_V4_MAXIP]	= { .type = NLA_U32 },
	[CTA_NAT_V6_MINIP]	= { .len = sizeof(struct in6_addr) },
	[CTA_NAT_V6_MAXIP]	= { .len = sizeof(struct in6_addr) },
	[CTA_NAT_PROTO]		= { .type = NLA_NESTED },
};

static int
nfnetlink_parse_nat(const struct nlattr *nat,
		    const struct nf_conn *ct, struct nf_nat_range *range,
		    const struct nf_nat_l3proto *l3proto)
{
	struct nlattr *tb[CTA_NAT_MAX+1];
	int err;

	memset(range, 0, sizeof(*range));

	err = nla_parse_nested(tb, CTA_NAT_MAX, nat, nat_nla_policy, NULL);
	if (err < 0)
		return err;

	err = l3proto->nlattr_to_range(tb, range);
	if (err < 0)
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
	struct nf_nat_range range;
	const struct nf_nat_l3proto *l3proto;
	int err;

	/* Should not happen, restricted to creating new conntracks
	 * via ctnetlink.
	 */
	if (WARN_ON_ONCE(nf_nat_initialized(ct, manip)))
		return -EEXIST;

	/* Make sure that L3 NAT is there by when we call nf_nat_setup_info to
	 * attach the null binding, otherwise this may oops.
	 */
	l3proto = __nf_nat_l3proto_find(nf_ct_l3num(ct));
	if (l3proto == NULL)
		return -EAGAIN;

	/* No NAT information has been passed, allocate the null-binding */
	if (attr == NULL)
		return __nf_nat_alloc_null_binding(ct, manip) == NF_DROP ? -ENOMEM : 0;

	err = nfnetlink_parse_nat(attr, ct, &range, l3proto);
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

static int __init nf_nat_init(void)
{
	int ret;

	ret = rhltable_init(&nf_nat_bysource_table, &nf_nat_bysource_params);
	if (ret)
		return ret;

	ret = nf_ct_extend_register(&nat_extend);
	if (ret < 0) {
		rhltable_destroy(&nf_nat_bysource_table);
		printk(KERN_ERR "nf_nat_core: Unable to register extension\n");
		return ret;
	}

	nf_ct_helper_expectfn_register(&follow_master_nat);

	BUG_ON(nfnetlink_parse_nat_setup_hook != NULL);
	RCU_INIT_POINTER(nfnetlink_parse_nat_setup_hook,
			   nfnetlink_parse_nat_setup);
#ifdef CONFIG_XFRM
	BUG_ON(nf_nat_decode_session_hook != NULL);
	RCU_INIT_POINTER(nf_nat_decode_session_hook, __nf_nat_decode_session);
#endif
	return 0;
}

static void __exit nf_nat_cleanup(void)
{
	struct nf_nat_proto_clean clean = {};
	unsigned int i;

	nf_ct_iterate_destroy(nf_nat_proto_clean, &clean);

	nf_ct_extend_unregister(&nat_extend);
	nf_ct_helper_expectfn_unregister(&follow_master_nat);
	RCU_INIT_POINTER(nfnetlink_parse_nat_setup_hook, NULL);
#ifdef CONFIG_XFRM
	RCU_INIT_POINTER(nf_nat_decode_session_hook, NULL);
#endif
	synchronize_rcu();

	for (i = 0; i < NFPROTO_NUMPROTO; i++)
		kfree(nf_nat_l4protos[i]);

	rhltable_destroy(&nf_nat_bysource_table);
}

MODULE_LICENSE("GPL");

module_init(nf_nat_init);
module_exit(nf_nat_cleanup);
