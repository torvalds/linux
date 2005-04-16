/* NAT for netfilter; shared with compatibility layer. */

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/skbuff.h>
#include <linux/netfilter_ipv4.h>
#include <linux/vmalloc.h>
#include <net/checksum.h>
#include <net/icmp.h>
#include <net/ip.h>
#include <net/tcp.h>  /* For tcp_prot in getorigdst */
#include <linux/icmp.h>
#include <linux/udp.h>
#include <linux/jhash.h>

#define ASSERT_READ_LOCK(x) MUST_BE_READ_LOCKED(&ip_nat_lock)
#define ASSERT_WRITE_LOCK(x) MUST_BE_WRITE_LOCKED(&ip_nat_lock)

#include <linux/netfilter_ipv4/ip_conntrack.h>
#include <linux/netfilter_ipv4/ip_conntrack_core.h>
#include <linux/netfilter_ipv4/ip_conntrack_protocol.h>
#include <linux/netfilter_ipv4/ip_nat.h>
#include <linux/netfilter_ipv4/ip_nat_protocol.h>
#include <linux/netfilter_ipv4/ip_nat_core.h>
#include <linux/netfilter_ipv4/ip_nat_helper.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#include <linux/netfilter_ipv4/listhelp.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

DECLARE_RWLOCK(ip_nat_lock);

/* Calculated at init based on memory size */
static unsigned int ip_nat_htable_size;

static struct list_head *bysource;
struct ip_nat_protocol *ip_nat_protos[MAX_IP_NAT_PROTO];


/* We keep an extra hash for each conntrack, for fast searching. */
static inline unsigned int
hash_by_src(const struct ip_conntrack_tuple *tuple)
{
	/* Original src, to ensure we map it consistently if poss. */
	return jhash_3words(tuple->src.ip, tuple->src.u.all,
			    tuple->dst.protonum, 0) % ip_nat_htable_size;
}

/* Noone using conntrack by the time this called. */
static void ip_nat_cleanup_conntrack(struct ip_conntrack *conn)
{
	if (!(conn->status & IPS_NAT_DONE_MASK))
		return;

	WRITE_LOCK(&ip_nat_lock);
	list_del(&conn->nat.info.bysource);
	WRITE_UNLOCK(&ip_nat_lock);
}

/* We do checksum mangling, so if they were wrong before they're still
 * wrong.  Also works for incomplete packets (eg. ICMP dest
 * unreachables.) */
u_int16_t
ip_nat_cheat_check(u_int32_t oldvalinv, u_int32_t newval, u_int16_t oldcheck)
{
	u_int32_t diffs[] = { oldvalinv, newval };
	return csum_fold(csum_partial((char *)diffs, sizeof(diffs),
				      oldcheck^0xFFFF));
}

/* Is this tuple already taken? (not by us) */
int
ip_nat_used_tuple(const struct ip_conntrack_tuple *tuple,
		  const struct ip_conntrack *ignored_conntrack)
{
	/* Conntrack tracking doesn't keep track of outgoing tuples; only
	   incoming ones.  NAT means they don't have a fixed mapping,
	   so we invert the tuple and look for the incoming reply.

	   We could keep a separate hash if this proves too slow. */
	struct ip_conntrack_tuple reply;

	invert_tuplepr(&reply, tuple);
	return ip_conntrack_tuple_taken(&reply, ignored_conntrack);
}

/* If we source map this tuple so reply looks like reply_tuple, will
 * that meet the constraints of range. */
static int
in_range(const struct ip_conntrack_tuple *tuple,
	 const struct ip_nat_range *range)
{
	struct ip_nat_protocol *proto = ip_nat_find_proto(tuple->dst.protonum);

	/* If we are supposed to map IPs, then we must be in the
	   range specified, otherwise let this drag us onto a new src IP. */
	if (range->flags & IP_NAT_RANGE_MAP_IPS) {
		if (ntohl(tuple->src.ip) < ntohl(range->min_ip)
		    || ntohl(tuple->src.ip) > ntohl(range->max_ip))
			return 0;
	}

	if (!(range->flags & IP_NAT_RANGE_PROTO_SPECIFIED)
	    || proto->in_range(tuple, IP_NAT_MANIP_SRC,
			       &range->min, &range->max))
		return 1;

	return 0;
}

static inline int
same_src(const struct ip_conntrack *ct,
	 const struct ip_conntrack_tuple *tuple)
{
	return (ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.protonum
		== tuple->dst.protonum
		&& ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.ip
		== tuple->src.ip
		&& ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u.all
		== tuple->src.u.all);
}

/* Only called for SRC manip */
static int
find_appropriate_src(const struct ip_conntrack_tuple *tuple,
		     struct ip_conntrack_tuple *result,
		     const struct ip_nat_range *range)
{
	unsigned int h = hash_by_src(tuple);
	struct ip_conntrack *ct;

	READ_LOCK(&ip_nat_lock);
	list_for_each_entry(ct, &bysource[h], nat.info.bysource) {
		if (same_src(ct, tuple)) {
			/* Copy source part from reply tuple. */
			invert_tuplepr(result,
				       &ct->tuplehash[IP_CT_DIR_REPLY].tuple);
			result->dst = tuple->dst;

			if (in_range(result, range)) {
				READ_UNLOCK(&ip_nat_lock);
				return 1;
			}
		}
	}
	READ_UNLOCK(&ip_nat_lock);
	return 0;
}

/* For [FUTURE] fragmentation handling, we want the least-used
   src-ip/dst-ip/proto triple.  Fairness doesn't come into it.  Thus
   if the range specifies 1.2.3.4 ports 10000-10005 and 1.2.3.5 ports
   1-65535, we don't do pro-rata allocation based on ports; we choose
   the ip with the lowest src-ip/dst-ip/proto usage.
*/
static void
find_best_ips_proto(struct ip_conntrack_tuple *tuple,
		    const struct ip_nat_range *range,
		    const struct ip_conntrack *conntrack,
		    enum ip_nat_manip_type maniptype)
{
	u_int32_t *var_ipp;
	/* Host order */
	u_int32_t minip, maxip, j;

	/* No IP mapping?  Do nothing. */
	if (!(range->flags & IP_NAT_RANGE_MAP_IPS))
		return;

	if (maniptype == IP_NAT_MANIP_SRC)
		var_ipp = &tuple->src.ip;
	else
		var_ipp = &tuple->dst.ip;

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
	j = jhash_2words(tuple->src.ip, tuple->dst.ip, 0);
	*var_ipp = htonl(minip + j % (maxip - minip + 1));
}

/* Manipulate the tuple into the range given.  For NF_IP_POST_ROUTING,
 * we change the source to map into the range.  For NF_IP_PRE_ROUTING
 * and NF_IP_LOCAL_OUT, we change the destination to map into the
 * range.  It might not be possible to get a unique tuple, but we try.
 * At worst (or if we race), we will end up with a final duplicate in
 * __ip_conntrack_confirm and drop the packet. */
static void
get_unique_tuple(struct ip_conntrack_tuple *tuple,
		 const struct ip_conntrack_tuple *orig_tuple,
		 const struct ip_nat_range *range,
		 struct ip_conntrack *conntrack,
		 enum ip_nat_manip_type maniptype)
{
	struct ip_nat_protocol *proto
		= ip_nat_find_proto(orig_tuple->dst.protonum);

	/* 1) If this srcip/proto/src-proto-part is currently mapped,
	   and that same mapping gives a unique tuple within the given
	   range, use that.

	   This is only required for source (ie. NAT/masq) mappings.
	   So far, we don't do local source mappings, so multiple
	   manips not an issue.  */
	if (maniptype == IP_NAT_MANIP_SRC) {
		if (find_appropriate_src(orig_tuple, tuple, range)) {
			DEBUGP("get_unique_tuple: Found current src map\n");
			if (!ip_nat_used_tuple(tuple, conntrack))
				return;
		}
	}

	/* 2) Select the least-used IP/proto combination in the given
	   range. */
	*tuple = *orig_tuple;
	find_best_ips_proto(tuple, range, conntrack, maniptype);

	/* 3) The per-protocol part of the manip is made to map into
	   the range to make a unique tuple. */

	/* Only bother mapping if it's not already in range and unique */
	if ((!(range->flags & IP_NAT_RANGE_PROTO_SPECIFIED)
	     || proto->in_range(tuple, maniptype, &range->min, &range->max))
	    && !ip_nat_used_tuple(tuple, conntrack))
		return;

	/* Last change: get protocol to try to obtain unique tuple. */
	proto->unique_tuple(tuple, range, maniptype, conntrack);
}

unsigned int
ip_nat_setup_info(struct ip_conntrack *conntrack,
		  const struct ip_nat_range *range,
		  unsigned int hooknum)
{
	struct ip_conntrack_tuple curr_tuple, new_tuple;
	struct ip_nat_info *info = &conntrack->nat.info;
	int have_to_hash = !(conntrack->status & IPS_NAT_DONE_MASK);
	enum ip_nat_manip_type maniptype = HOOK2MANIP(hooknum);

	IP_NF_ASSERT(hooknum == NF_IP_PRE_ROUTING
		     || hooknum == NF_IP_POST_ROUTING
		     || hooknum == NF_IP_LOCAL_IN
		     || hooknum == NF_IP_LOCAL_OUT);
	BUG_ON(ip_nat_initialized(conntrack, maniptype));

	/* What we've got will look like inverse of reply. Normally
	   this is what is in the conntrack, except for prior
	   manipulations (future optimization: if num_manips == 0,
	   orig_tp =
	   conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple) */
	invert_tuplepr(&curr_tuple,
		       &conntrack->tuplehash[IP_CT_DIR_REPLY].tuple);

	get_unique_tuple(&new_tuple, &curr_tuple, range, conntrack, maniptype);

	if (!ip_ct_tuple_equal(&new_tuple, &curr_tuple)) {
		struct ip_conntrack_tuple reply;

		/* Alter conntrack table so will recognize replies. */
		invert_tuplepr(&reply, &new_tuple);
		ip_conntrack_alter_reply(conntrack, &reply);

		/* Non-atomic: we own this at the moment. */
		if (maniptype == IP_NAT_MANIP_SRC)
			conntrack->status |= IPS_SRC_NAT;
		else
			conntrack->status |= IPS_DST_NAT;
	}

	/* Place in source hash if this is the first time. */
	if (have_to_hash) {
		unsigned int srchash
			= hash_by_src(&conntrack->tuplehash[IP_CT_DIR_ORIGINAL]
				      .tuple);
		WRITE_LOCK(&ip_nat_lock);
		list_add(&info->bysource, &bysource[srchash]);
		WRITE_UNLOCK(&ip_nat_lock);
	}

	/* It's done. */
	if (maniptype == IP_NAT_MANIP_DST)
		set_bit(IPS_DST_NAT_DONE_BIT, &conntrack->status);
	else
		set_bit(IPS_SRC_NAT_DONE_BIT, &conntrack->status);

	return NF_ACCEPT;
}

/* Returns true if succeeded. */
static int
manip_pkt(u_int16_t proto,
	  struct sk_buff **pskb,
	  unsigned int iphdroff,
	  const struct ip_conntrack_tuple *target,
	  enum ip_nat_manip_type maniptype)
{
	struct iphdr *iph;

	(*pskb)->nfcache |= NFC_ALTERED;
	if (!skb_ip_make_writable(pskb, iphdroff + sizeof(*iph)))
		return 0;

	iph = (void *)(*pskb)->data + iphdroff;

	/* Manipulate protcol part. */
	if (!ip_nat_find_proto(proto)->manip_pkt(pskb, iphdroff,
	                                         target, maniptype))
		return 0;

	iph = (void *)(*pskb)->data + iphdroff;

	if (maniptype == IP_NAT_MANIP_SRC) {
		iph->check = ip_nat_cheat_check(~iph->saddr, target->src.ip,
						iph->check);
		iph->saddr = target->src.ip;
	} else {
		iph->check = ip_nat_cheat_check(~iph->daddr, target->dst.ip,
						iph->check);
		iph->daddr = target->dst.ip;
	}
	return 1;
}

/* Do packet manipulations according to ip_nat_setup_info. */
unsigned int nat_packet(struct ip_conntrack *ct,
			enum ip_conntrack_info ctinfo,
			unsigned int hooknum,
			struct sk_buff **pskb)
{
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	unsigned long statusbit;
	enum ip_nat_manip_type mtype = HOOK2MANIP(hooknum);

	if (test_bit(IPS_SEQ_ADJUST_BIT, &ct->status)
	    && (hooknum == NF_IP_POST_ROUTING || hooknum == NF_IP_LOCAL_IN)) {
		DEBUGP("ip_nat_core: adjusting sequence number\n");
		/* future: put this in a l4-proto specific function,
		 * and call this function here. */
		if (!ip_nat_seq_adjust(pskb, ct, ctinfo))
			return NF_DROP;
	}

	if (mtype == IP_NAT_MANIP_SRC)
		statusbit = IPS_SRC_NAT;
	else
		statusbit = IPS_DST_NAT;

	/* Invert if this is reply dir. */
	if (dir == IP_CT_DIR_REPLY)
		statusbit ^= IPS_NAT_MASK;

	/* Non-atomic: these bits don't change. */
	if (ct->status & statusbit) {
		struct ip_conntrack_tuple target;

		/* We are aiming to look like inverse of other direction. */
		invert_tuplepr(&target, &ct->tuplehash[!dir].tuple);

		if (!manip_pkt(target.dst.protonum, pskb, 0, &target, mtype))
			return NF_DROP;
	}
	return NF_ACCEPT;
}

/* Dir is direction ICMP is coming from (opposite to packet it contains) */
int icmp_reply_translation(struct sk_buff **pskb,
			   struct ip_conntrack *ct,
			   enum ip_nat_manip_type manip,
			   enum ip_conntrack_dir dir)
{
	struct {
		struct icmphdr icmp;
		struct iphdr ip;
	} *inside;
	struct ip_conntrack_tuple inner, target;
	int hdrlen = (*pskb)->nh.iph->ihl * 4;

	if (!skb_ip_make_writable(pskb, hdrlen + sizeof(*inside)))
		return 0;

	inside = (void *)(*pskb)->data + (*pskb)->nh.iph->ihl*4;

	/* We're actually going to mangle it beyond trivial checksum
	   adjustment, so make sure the current checksum is correct. */
	if ((*pskb)->ip_summed != CHECKSUM_UNNECESSARY) {
		hdrlen = (*pskb)->nh.iph->ihl * 4;
		if ((u16)csum_fold(skb_checksum(*pskb, hdrlen,
						(*pskb)->len - hdrlen, 0)))
			return 0;
	}

	/* Must be RELATED */
	IP_NF_ASSERT((*pskb)->nfctinfo == IP_CT_RELATED ||
		     (*pskb)->nfctinfo == IP_CT_RELATED+IP_CT_IS_REPLY);

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

	DEBUGP("icmp_reply_translation: translating error %p manp %u dir %s\n",
	       *pskb, manip, dir == IP_CT_DIR_ORIGINAL ? "ORIG" : "REPLY");

	if (!ip_ct_get_tuple(&inside->ip, *pskb, (*pskb)->nh.iph->ihl*4 +
	                     sizeof(struct icmphdr) + inside->ip.ihl*4,
	                     &inner, ip_ct_find_proto(inside->ip.protocol)))
		return 0;

	/* Change inner back to look like incoming packet.  We do the
	   opposite manip on this hook to normal, because it might not
	   pass all hooks (locally-generated ICMP).  Consider incoming
	   packet: PREROUTING (DST manip), routing produces ICMP, goes
	   through POSTROUTING (which must correct the DST manip). */
	if (!manip_pkt(inside->ip.protocol, pskb,
		       (*pskb)->nh.iph->ihl*4
		       + sizeof(inside->icmp),
		       &ct->tuplehash[!dir].tuple,
		       !manip))
		return 0;

	/* Reloading "inside" here since manip_pkt inner. */
	inside = (void *)(*pskb)->data + (*pskb)->nh.iph->ihl*4;
	inside->icmp.checksum = 0;
	inside->icmp.checksum = csum_fold(skb_checksum(*pskb, hdrlen,
						       (*pskb)->len - hdrlen,
						       0));

	/* Change outer to look the reply to an incoming packet
	 * (proto 0 means don't invert per-proto part). */

	/* Obviously, we need to NAT destination IP, but source IP
	   should be NAT'ed only if it is from a NAT'd host.

	   Explanation: some people use NAT for anonymizing.  Also,
	   CERT recommends dropping all packets from private IP
	   addresses (although ICMP errors from internal links with
	   such addresses are not too uncommon, as Alan Cox points
	   out) */
	if (manip != IP_NAT_MANIP_SRC
	    || ((*pskb)->nh.iph->saddr == ct->tuplehash[dir].tuple.src.ip)) {
		invert_tuplepr(&target, &ct->tuplehash[!dir].tuple);
		if (!manip_pkt(0, pskb, 0, &target, manip))
			return 0;
	}

	return 1;
}

/* Protocol registration. */
int ip_nat_protocol_register(struct ip_nat_protocol *proto)
{
	int ret = 0;

	WRITE_LOCK(&ip_nat_lock);
	if (ip_nat_protos[proto->protonum] != &ip_nat_unknown_protocol) {
		ret = -EBUSY;
		goto out;
	}
	ip_nat_protos[proto->protonum] = proto;
 out:
	WRITE_UNLOCK(&ip_nat_lock);
	return ret;
}

/* Noone stores the protocol anywhere; simply delete it. */
void ip_nat_protocol_unregister(struct ip_nat_protocol *proto)
{
	WRITE_LOCK(&ip_nat_lock);
	ip_nat_protos[proto->protonum] = &ip_nat_unknown_protocol;
	WRITE_UNLOCK(&ip_nat_lock);

	/* Someone could be still looking at the proto in a bh. */
	synchronize_net();
}

int __init ip_nat_init(void)
{
	size_t i;

	/* Leave them the same for the moment. */
	ip_nat_htable_size = ip_conntrack_htable_size;

	/* One vmalloc for both hash tables */
	bysource = vmalloc(sizeof(struct list_head) * ip_nat_htable_size);
	if (!bysource)
		return -ENOMEM;

	/* Sew in builtin protocols. */
	WRITE_LOCK(&ip_nat_lock);
	for (i = 0; i < MAX_IP_NAT_PROTO; i++)
		ip_nat_protos[i] = &ip_nat_unknown_protocol;
	ip_nat_protos[IPPROTO_TCP] = &ip_nat_protocol_tcp;
	ip_nat_protos[IPPROTO_UDP] = &ip_nat_protocol_udp;
	ip_nat_protos[IPPROTO_ICMP] = &ip_nat_protocol_icmp;
	WRITE_UNLOCK(&ip_nat_lock);

	for (i = 0; i < ip_nat_htable_size; i++) {
		INIT_LIST_HEAD(&bysource[i]);
	}

	/* FIXME: Man, this is a hack.  <SIGH> */
	IP_NF_ASSERT(ip_conntrack_destroyed == NULL);
	ip_conntrack_destroyed = &ip_nat_cleanup_conntrack;

	/* Initialize fake conntrack so that NAT will skip it */
	ip_conntrack_untracked.status |= IPS_NAT_DONE_MASK;
	return 0;
}

/* Clear NAT section of all conntracks, in case we're loaded again. */
static int clean_nat(struct ip_conntrack *i, void *data)
{
	memset(&i->nat, 0, sizeof(i->nat));
	i->status &= ~(IPS_NAT_MASK | IPS_NAT_DONE_MASK | IPS_SEQ_ADJUST);
	return 0;
}

/* Not __exit: called from ip_nat_standalone.c:init_or_cleanup() --RR */
void ip_nat_cleanup(void)
{
	ip_ct_iterate_cleanup(&clean_nat, NULL);
	ip_conntrack_destroyed = NULL;
	vfree(bysource);
}
