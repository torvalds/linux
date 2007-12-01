/* NAT for netfilter; shared with compatibility layer. */

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/skbuff.h>
#include <net/checksum.h>
#include <net/icmp.h>
#include <net/ip.h>
#include <net/tcp.h>  /* For tcp_prot in getorigdst */
#include <linux/icmp.h>
#include <linux/udp.h>
#include <linux/jhash.h>

#include <linux/netfilter_ipv4.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_protocol.h>
#include <net/netfilter/nf_nat_core.h>
#include <net/netfilter/nf_nat_helper.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_l3proto.h>
#include <net/netfilter/nf_conntrack_l4proto.h>

static DEFINE_RWLOCK(nf_nat_lock);

static struct nf_conntrack_l3proto *l3proto = NULL;

/* Calculated at init based on memory size */
static unsigned int nf_nat_htable_size;
static int nf_nat_vmalloced;

static struct hlist_head *bysource;

#define MAX_IP_NAT_PROTO 256
static struct nf_nat_protocol *nf_nat_protos[MAX_IP_NAT_PROTO];

static inline struct nf_nat_protocol *
__nf_nat_proto_find(u_int8_t protonum)
{
	return rcu_dereference(nf_nat_protos[protonum]);
}

struct nf_nat_protocol *
nf_nat_proto_find_get(u_int8_t protonum)
{
	struct nf_nat_protocol *p;

	rcu_read_lock();
	p = __nf_nat_proto_find(protonum);
	if (!try_module_get(p->me))
		p = &nf_nat_unknown_protocol;
	rcu_read_unlock();

	return p;
}
EXPORT_SYMBOL_GPL(nf_nat_proto_find_get);

void
nf_nat_proto_put(struct nf_nat_protocol *p)
{
	module_put(p->me);
}
EXPORT_SYMBOL_GPL(nf_nat_proto_put);

/* We keep an extra hash for each conntrack, for fast searching. */
static inline unsigned int
hash_by_src(const struct nf_conntrack_tuple *tuple)
{
	/* Original src, to ensure we map it consistently if poss. */
	return jhash_3words((__force u32)tuple->src.u3.ip,
			    (__force u32)tuple->src.u.all,
			    tuple->dst.protonum, 0) % nf_nat_htable_size;
}

/* Is this tuple already taken? (not by us) */
int
nf_nat_used_tuple(const struct nf_conntrack_tuple *tuple,
		  const struct nf_conn *ignored_conntrack)
{
	/* Conntrack tracking doesn't keep track of outgoing tuples; only
	   incoming ones.  NAT means they don't have a fixed mapping,
	   so we invert the tuple and look for the incoming reply.

	   We could keep a separate hash if this proves too slow. */
	struct nf_conntrack_tuple reply;

	nf_ct_invert_tuplepr(&reply, tuple);
	return nf_conntrack_tuple_taken(&reply, ignored_conntrack);
}
EXPORT_SYMBOL(nf_nat_used_tuple);

/* If we source map this tuple so reply looks like reply_tuple, will
 * that meet the constraints of range. */
static int
in_range(const struct nf_conntrack_tuple *tuple,
	 const struct nf_nat_range *range)
{
	struct nf_nat_protocol *proto;
	int ret = 0;

	/* If we are supposed to map IPs, then we must be in the
	   range specified, otherwise let this drag us onto a new src IP. */
	if (range->flags & IP_NAT_RANGE_MAP_IPS) {
		if (ntohl(tuple->src.u3.ip) < ntohl(range->min_ip) ||
		    ntohl(tuple->src.u3.ip) > ntohl(range->max_ip))
			return 0;
	}

	rcu_read_lock();
	proto = __nf_nat_proto_find(tuple->dst.protonum);
	if (!(range->flags & IP_NAT_RANGE_PROTO_SPECIFIED) ||
	    proto->in_range(tuple, IP_NAT_MANIP_SRC,
			    &range->min, &range->max))
		ret = 1;
	rcu_read_unlock();

	return ret;
}

static inline int
same_src(const struct nf_conn *ct,
	 const struct nf_conntrack_tuple *tuple)
{
	const struct nf_conntrack_tuple *t;

	t = &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple;
	return (t->dst.protonum == tuple->dst.protonum &&
		t->src.u3.ip == tuple->src.u3.ip &&
		t->src.u.all == tuple->src.u.all);
}

/* Only called for SRC manip */
static int
find_appropriate_src(const struct nf_conntrack_tuple *tuple,
		     struct nf_conntrack_tuple *result,
		     const struct nf_nat_range *range)
{
	unsigned int h = hash_by_src(tuple);
	struct nf_conn_nat *nat;
	struct nf_conn *ct;
	struct hlist_node *n;

	read_lock_bh(&nf_nat_lock);
	hlist_for_each_entry(nat, n, &bysource[h], bysource) {
		ct = nat->ct;
		if (same_src(ct, tuple)) {
			/* Copy source part from reply tuple. */
			nf_ct_invert_tuplepr(result,
				       &ct->tuplehash[IP_CT_DIR_REPLY].tuple);
			result->dst = tuple->dst;

			if (in_range(result, range)) {
				read_unlock_bh(&nf_nat_lock);
				return 1;
			}
		}
	}
	read_unlock_bh(&nf_nat_lock);
	return 0;
}

/* For [FUTURE] fragmentation handling, we want the least-used
   src-ip/dst-ip/proto triple.  Fairness doesn't come into it.  Thus
   if the range specifies 1.2.3.4 ports 10000-10005 and 1.2.3.5 ports
   1-65535, we don't do pro-rata allocation based on ports; we choose
   the ip with the lowest src-ip/dst-ip/proto usage.
*/
static void
find_best_ips_proto(struct nf_conntrack_tuple *tuple,
		    const struct nf_nat_range *range,
		    const struct nf_conn *ct,
		    enum nf_nat_manip_type maniptype)
{
	__be32 *var_ipp;
	/* Host order */
	u_int32_t minip, maxip, j;

	/* No IP mapping?  Do nothing. */
	if (!(range->flags & IP_NAT_RANGE_MAP_IPS))
		return;

	if (maniptype == IP_NAT_MANIP_SRC)
		var_ipp = &tuple->src.u3.ip;
	else
		var_ipp = &tuple->dst.u3.ip;

	/* Fast path: only one choice. */
	if (range->min_ip == range->max_ip) {
		*var_ipp = range->min_ip;
		return;
	}

	/* Hashing source and destination IPs gives a fairly even
	 * spread in practice (if there are a small number of IPs
	 * involved, there usually aren't that many connections
	 * anyway).  The consistency means that servers see the same
	 * client coming from the same IP (some Internet Banking sites
	 * like this), even across reboots. */
	minip = ntohl(range->min_ip);
	maxip = ntohl(range->max_ip);
	j = jhash_2words((__force u32)tuple->src.u3.ip,
			 (__force u32)tuple->dst.u3.ip, 0);
	*var_ipp = htonl(minip + j % (maxip - minip + 1));
}

/* Manipulate the tuple into the range given.  For NF_IP_POST_ROUTING,
 * we change the source to map into the range.  For NF_IP_PRE_ROUTING
 * and NF_IP_LOCAL_OUT, we change the destination to map into the
 * range.  It might not be possible to get a unique tuple, but we try.
 * At worst (or if we race), we will end up with a final duplicate in
 * __ip_conntrack_confirm and drop the packet. */
static void
get_unique_tuple(struct nf_conntrack_tuple *tuple,
		 const struct nf_conntrack_tuple *orig_tuple,
		 const struct nf_nat_range *range,
		 struct nf_conn *ct,
		 enum nf_nat_manip_type maniptype)
{
	struct nf_nat_protocol *proto;

	/* 1) If this srcip/proto/src-proto-part is currently mapped,
	   and that same mapping gives a unique tuple within the given
	   range, use that.

	   This is only required for source (ie. NAT/masq) mappings.
	   So far, we don't do local source mappings, so multiple
	   manips not an issue.  */
	if (maniptype == IP_NAT_MANIP_SRC) {
		if (find_appropriate_src(orig_tuple, tuple, range)) {
			pr_debug("get_unique_tuple: Found current src map\n");
			if (!(range->flags & IP_NAT_RANGE_PROTO_RANDOM))
				if (!nf_nat_used_tuple(tuple, ct))
					return;
		}
	}

	/* 2) Select the least-used IP/proto combination in the given
	   range. */
	*tuple = *orig_tuple;
	find_best_ips_proto(tuple, range, ct, maniptype);

	/* 3) The per-protocol part of the manip is made to map into
	   the range to make a unique tuple. */

	rcu_read_lock();
	proto = __nf_nat_proto_find(orig_tuple->dst.protonum);

	/* Change protocol info to have some randomization */
	if (range->flags & IP_NAT_RANGE_PROTO_RANDOM) {
		proto->unique_tuple(tuple, range, maniptype, ct);
		goto out;
	}

	/* Only bother mapping if it's not already in range and unique */
	if ((!(range->flags & IP_NAT_RANGE_PROTO_SPECIFIED) ||
	     proto->in_range(tuple, maniptype, &range->min, &range->max)) &&
	    !nf_nat_used_tuple(tuple, ct))
		goto out;

	/* Last change: get protocol to try to obtain unique tuple. */
	proto->unique_tuple(tuple, range, maniptype, ct);
out:
	rcu_read_unlock();
}

unsigned int
nf_nat_setup_info(struct nf_conn *ct,
		  const struct nf_nat_range *range,
		  unsigned int hooknum)
{
	struct nf_conntrack_tuple curr_tuple, new_tuple;
	struct nf_conn_nat *nat;
	int have_to_hash = !(ct->status & IPS_NAT_DONE_MASK);
	enum nf_nat_manip_type maniptype = HOOK2MANIP(hooknum);

	/* nat helper or nfctnetlink also setup binding */
	nat = nfct_nat(ct);
	if (!nat) {
		nat = nf_ct_ext_add(ct, NF_CT_EXT_NAT, GFP_ATOMIC);
		if (nat == NULL) {
			pr_debug("failed to add NAT extension\n");
			return NF_ACCEPT;
		}
	}

	NF_CT_ASSERT(hooknum == NF_IP_PRE_ROUTING ||
		     hooknum == NF_IP_POST_ROUTING ||
		     hooknum == NF_IP_LOCAL_IN ||
		     hooknum == NF_IP_LOCAL_OUT);
	BUG_ON(nf_nat_initialized(ct, maniptype));

	/* What we've got will look like inverse of reply. Normally
	   this is what is in the conntrack, except for prior
	   manipulations (future optimization: if num_manips == 0,
	   orig_tp =
	   conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple) */
	nf_ct_invert_tuplepr(&curr_tuple,
			     &ct->tuplehash[IP_CT_DIR_REPLY].tuple);

	get_unique_tuple(&new_tuple, &curr_tuple, range, ct, maniptype);

	if (!nf_ct_tuple_equal(&new_tuple, &curr_tuple)) {
		struct nf_conntrack_tuple reply;

		/* Alter conntrack table so will recognize replies. */
		nf_ct_invert_tuplepr(&reply, &new_tuple);
		nf_conntrack_alter_reply(ct, &reply);

		/* Non-atomic: we own this at the moment. */
		if (maniptype == IP_NAT_MANIP_SRC)
			ct->status |= IPS_SRC_NAT;
		else
			ct->status |= IPS_DST_NAT;
	}

	/* Place in source hash if this is the first time. */
	if (have_to_hash) {
		unsigned int srchash;

		srchash = hash_by_src(&ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple);
		write_lock_bh(&nf_nat_lock);
		/* nf_conntrack_alter_reply might re-allocate exntension aera */
		nat = nfct_nat(ct);
		nat->ct = ct;
		hlist_add_head(&nat->bysource, &bysource[srchash]);
		write_unlock_bh(&nf_nat_lock);
	}

	/* It's done. */
	if (maniptype == IP_NAT_MANIP_DST)
		set_bit(IPS_DST_NAT_DONE_BIT, &ct->status);
	else
		set_bit(IPS_SRC_NAT_DONE_BIT, &ct->status);

	return NF_ACCEPT;
}
EXPORT_SYMBOL(nf_nat_setup_info);

/* Returns true if succeeded. */
static int
manip_pkt(u_int16_t proto,
	  struct sk_buff *skb,
	  unsigned int iphdroff,
	  const struct nf_conntrack_tuple *target,
	  enum nf_nat_manip_type maniptype)
{
	struct iphdr *iph;
	struct nf_nat_protocol *p;

	if (!skb_make_writable(skb, iphdroff + sizeof(*iph)))
		return 0;

	iph = (void *)skb->data + iphdroff;

	/* Manipulate protcol part. */

	/* rcu_read_lock()ed by nf_hook_slow */
	p = __nf_nat_proto_find(proto);
	if (!p->manip_pkt(skb, iphdroff, target, maniptype))
		return 0;

	iph = (void *)skb->data + iphdroff;

	if (maniptype == IP_NAT_MANIP_SRC) {
		nf_csum_replace4(&iph->check, iph->saddr, target->src.u3.ip);
		iph->saddr = target->src.u3.ip;
	} else {
		nf_csum_replace4(&iph->check, iph->daddr, target->dst.u3.ip);
		iph->daddr = target->dst.u3.ip;
	}
	return 1;
}

/* Do packet manipulations according to nf_nat_setup_info. */
unsigned int nf_nat_packet(struct nf_conn *ct,
			   enum ip_conntrack_info ctinfo,
			   unsigned int hooknum,
			   struct sk_buff *skb)
{
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	unsigned long statusbit;
	enum nf_nat_manip_type mtype = HOOK2MANIP(hooknum);

	if (mtype == IP_NAT_MANIP_SRC)
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

		if (!manip_pkt(target.dst.protonum, skb, 0, &target, mtype))
			return NF_DROP;
	}
	return NF_ACCEPT;
}
EXPORT_SYMBOL_GPL(nf_nat_packet);

/* Dir is direction ICMP is coming from (opposite to packet it contains) */
int nf_nat_icmp_reply_translation(struct nf_conn *ct,
				  enum ip_conntrack_info ctinfo,
				  unsigned int hooknum,
				  struct sk_buff *skb)
{
	struct {
		struct icmphdr icmp;
		struct iphdr ip;
	} *inside;
	struct nf_conntrack_l4proto *l4proto;
	struct nf_conntrack_tuple inner, target;
	int hdrlen = ip_hdrlen(skb);
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	unsigned long statusbit;
	enum nf_nat_manip_type manip = HOOK2MANIP(hooknum);

	if (!skb_make_writable(skb, hdrlen + sizeof(*inside)))
		return 0;

	inside = (void *)skb->data + ip_hdrlen(skb);

	/* We're actually going to mangle it beyond trivial checksum
	   adjustment, so make sure the current checksum is correct. */
	if (nf_ip_checksum(skb, hooknum, hdrlen, 0))
		return 0;

	/* Must be RELATED */
	NF_CT_ASSERT(skb->nfctinfo == IP_CT_RELATED ||
		     skb->nfctinfo == IP_CT_RELATED+IP_CT_IS_REPLY);

	/* Redirects on non-null nats must be dropped, else they'll
	   start talking to each other without our translation, and be
	   confused... --RR */
	if (inside->icmp.type == ICMP_REDIRECT) {
		/* If NAT isn't finished, assume it and drop. */
		if ((ct->status & IPS_NAT_DONE_MASK) != IPS_NAT_DONE_MASK)
			return 0;

		if (ct->status & IPS_NAT_MASK)
			return 0;
	}

	pr_debug("icmp_reply_translation: translating error %p manip %u "
		 "dir %s\n", skb, manip,
		 dir == IP_CT_DIR_ORIGINAL ? "ORIG" : "REPLY");

	/* rcu_read_lock()ed by nf_hook_slow */
	l4proto = __nf_ct_l4proto_find(PF_INET, inside->ip.protocol);

	if (!nf_ct_get_tuple(skb,
			     ip_hdrlen(skb) + sizeof(struct icmphdr),
			     (ip_hdrlen(skb) +
			      sizeof(struct icmphdr) + inside->ip.ihl * 4),
			     (u_int16_t)AF_INET,
			     inside->ip.protocol,
			     &inner, l3proto, l4proto))
		return 0;

	/* Change inner back to look like incoming packet.  We do the
	   opposite manip on this hook to normal, because it might not
	   pass all hooks (locally-generated ICMP).  Consider incoming
	   packet: PREROUTING (DST manip), routing produces ICMP, goes
	   through POSTROUTING (which must correct the DST manip). */
	if (!manip_pkt(inside->ip.protocol, skb,
		       ip_hdrlen(skb) + sizeof(inside->icmp),
		       &ct->tuplehash[!dir].tuple,
		       !manip))
		return 0;

	if (skb->ip_summed != CHECKSUM_PARTIAL) {
		/* Reloading "inside" here since manip_pkt inner. */
		inside = (void *)skb->data + ip_hdrlen(skb);
		inside->icmp.checksum = 0;
		inside->icmp.checksum =
			csum_fold(skb_checksum(skb, hdrlen,
					       skb->len - hdrlen, 0));
	}

	/* Change outer to look the reply to an incoming packet
	 * (proto 0 means don't invert per-proto part). */
	if (manip == IP_NAT_MANIP_SRC)
		statusbit = IPS_SRC_NAT;
	else
		statusbit = IPS_DST_NAT;

	/* Invert if this is reply dir. */
	if (dir == IP_CT_DIR_REPLY)
		statusbit ^= IPS_NAT_MASK;

	if (ct->status & statusbit) {
		nf_ct_invert_tuplepr(&target, &ct->tuplehash[!dir].tuple);
		if (!manip_pkt(0, skb, 0, &target, manip))
			return 0;
	}

	return 1;
}
EXPORT_SYMBOL_GPL(nf_nat_icmp_reply_translation);

/* Protocol registration. */
int nf_nat_protocol_register(struct nf_nat_protocol *proto)
{
	int ret = 0;

	write_lock_bh(&nf_nat_lock);
	if (nf_nat_protos[proto->protonum] != &nf_nat_unknown_protocol) {
		ret = -EBUSY;
		goto out;
	}
	rcu_assign_pointer(nf_nat_protos[proto->protonum], proto);
 out:
	write_unlock_bh(&nf_nat_lock);
	return ret;
}
EXPORT_SYMBOL(nf_nat_protocol_register);

/* Noone stores the protocol anywhere; simply delete it. */
void nf_nat_protocol_unregister(struct nf_nat_protocol *proto)
{
	write_lock_bh(&nf_nat_lock);
	rcu_assign_pointer(nf_nat_protos[proto->protonum],
			   &nf_nat_unknown_protocol);
	write_unlock_bh(&nf_nat_lock);
	synchronize_rcu();
}
EXPORT_SYMBOL(nf_nat_protocol_unregister);

#if defined(CONFIG_NF_CT_NETLINK) || defined(CONFIG_NF_CT_NETLINK_MODULE)
int
nf_nat_port_range_to_nlattr(struct sk_buff *skb,
			    const struct nf_nat_range *range)
{
	NLA_PUT(skb, CTA_PROTONAT_PORT_MIN, sizeof(__be16),
		&range->min.tcp.port);
	NLA_PUT(skb, CTA_PROTONAT_PORT_MAX, sizeof(__be16),
		&range->max.tcp.port);

	return 0;

nla_put_failure:
	return -1;
}
EXPORT_SYMBOL_GPL(nf_nat_port_nlattr_to_range);

int
nf_nat_port_nlattr_to_range(struct nlattr *tb[], struct nf_nat_range *range)
{
	int ret = 0;

	/* we have to return whether we actually parsed something or not */

	if (tb[CTA_PROTONAT_PORT_MIN]) {
		ret = 1;
		range->min.tcp.port =
			*(__be16 *)nla_data(tb[CTA_PROTONAT_PORT_MIN]);
	}

	if (!tb[CTA_PROTONAT_PORT_MAX]) {
		if (ret)
			range->max.tcp.port = range->min.tcp.port;
	} else {
		ret = 1;
		range->max.tcp.port =
			*(__be16 *)nla_data(tb[CTA_PROTONAT_PORT_MAX]);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(nf_nat_port_range_to_nlattr);
#endif

/* Noone using conntrack by the time this called. */
static void nf_nat_cleanup_conntrack(struct nf_conn *ct)
{
	struct nf_conn_nat *nat = nf_ct_ext_find(ct, NF_CT_EXT_NAT);

	if (nat == NULL || nat->ct == NULL)
		return;

	NF_CT_ASSERT(nat->ct->status & IPS_NAT_DONE_MASK);

	write_lock_bh(&nf_nat_lock);
	hlist_del(&nat->bysource);
	nat->ct = NULL;
	write_unlock_bh(&nf_nat_lock);
}

static void nf_nat_move_storage(struct nf_conn *conntrack, void *old)
{
	struct nf_conn_nat *new_nat = nf_ct_ext_find(conntrack, NF_CT_EXT_NAT);
	struct nf_conn_nat *old_nat = (struct nf_conn_nat *)old;
	struct nf_conn *ct = old_nat->ct;

	if (!ct || !(ct->status & IPS_NAT_DONE_MASK))
		return;

	write_lock_bh(&nf_nat_lock);
	hlist_replace_rcu(&old_nat->bysource, &new_nat->bysource);
	new_nat->ct = ct;
	write_unlock_bh(&nf_nat_lock);
}

static struct nf_ct_ext_type nat_extend __read_mostly = {
	.len		= sizeof(struct nf_conn_nat),
	.align		= __alignof__(struct nf_conn_nat),
	.destroy	= nf_nat_cleanup_conntrack,
	.move		= nf_nat_move_storage,
	.id		= NF_CT_EXT_NAT,
	.flags		= NF_CT_EXT_F_PREALLOC,
};

static int __init nf_nat_init(void)
{
	size_t i;
	int ret;

	ret = nf_ct_extend_register(&nat_extend);
	if (ret < 0) {
		printk(KERN_ERR "nf_nat_core: Unable to register extension\n");
		return ret;
	}

	/* Leave them the same for the moment. */
	nf_nat_htable_size = nf_conntrack_htable_size;

	bysource = nf_ct_alloc_hashtable(&nf_nat_htable_size,
					 &nf_nat_vmalloced);
	if (!bysource) {
		ret = -ENOMEM;
		goto cleanup_extend;
	}

	/* Sew in builtin protocols. */
	write_lock_bh(&nf_nat_lock);
	for (i = 0; i < MAX_IP_NAT_PROTO; i++)
		rcu_assign_pointer(nf_nat_protos[i], &nf_nat_unknown_protocol);
	rcu_assign_pointer(nf_nat_protos[IPPROTO_TCP], &nf_nat_protocol_tcp);
	rcu_assign_pointer(nf_nat_protos[IPPROTO_UDP], &nf_nat_protocol_udp);
	rcu_assign_pointer(nf_nat_protos[IPPROTO_ICMP], &nf_nat_protocol_icmp);
	write_unlock_bh(&nf_nat_lock);

	for (i = 0; i < nf_nat_htable_size; i++) {
		INIT_HLIST_HEAD(&bysource[i]);
	}

	/* Initialize fake conntrack so that NAT will skip it */
	nf_conntrack_untracked.status |= IPS_NAT_DONE_MASK;

	l3proto = nf_ct_l3proto_find_get((u_int16_t)AF_INET);
	return 0;

 cleanup_extend:
	nf_ct_extend_unregister(&nat_extend);
	return ret;
}

/* Clear NAT section of all conntracks, in case we're loaded again. */
static int clean_nat(struct nf_conn *i, void *data)
{
	struct nf_conn_nat *nat = nfct_nat(i);

	if (!nat)
		return 0;
	memset(nat, 0, sizeof(*nat));
	i->status &= ~(IPS_NAT_MASK | IPS_NAT_DONE_MASK | IPS_SEQ_ADJUST);
	return 0;
}

static void __exit nf_nat_cleanup(void)
{
	nf_ct_iterate_cleanup(&clean_nat, NULL);
	synchronize_rcu();
	nf_ct_free_hashtable(bysource, nf_nat_vmalloced, nf_nat_htable_size);
	nf_ct_l3proto_put(l3proto);
	nf_ct_extend_unregister(&nat_extend);
}

MODULE_LICENSE("GPL");

module_init(nf_nat_init);
module_exit(nf_nat_cleanup);
