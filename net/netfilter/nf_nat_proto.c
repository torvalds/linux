// SPDX-License-Identifier: GPL-2.0-only
/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 */

#include <linux/types.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/icmp.h>
#include <linux/icmpv6.h>

#include <linux/dccp.h>
#include <linux/sctp.h>
#include <net/sctp/checksum.h>

#include <linux/netfilter.h>
#include <net/netfilter/nf_nat.h>

#include <linux/ipv6.h>
#include <linux/netfilter_ipv6.h>
#include <net/checksum.h>
#include <net/ip6_checksum.h>
#include <net/ip6_route.h>
#include <net/xfrm.h>
#include <net/ipv6.h>

#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack.h>
#include <linux/netfilter/nfnetlink_conntrack.h>

static void nf_csum_update(struct sk_buff *skb,
			   unsigned int iphdroff, __sum16 *check,
			   const struct nf_conntrack_tuple *t,
			   enum nf_nat_manip_type maniptype);

static void
__udp_manip_pkt(struct sk_buff *skb,
	        unsigned int iphdroff, struct udphdr *hdr,
	        const struct nf_conntrack_tuple *tuple,
	        enum nf_nat_manip_type maniptype, bool do_csum)
{
	__be16 *portptr, newport;

	if (maniptype == NF_NAT_MANIP_SRC) {
		/* Get rid of src port */
		newport = tuple->src.u.udp.port;
		portptr = &hdr->source;
	} else {
		/* Get rid of dst port */
		newport = tuple->dst.u.udp.port;
		portptr = &hdr->dest;
	}
	if (do_csum) {
		nf_csum_update(skb, iphdroff, &hdr->check, tuple, maniptype);
		inet_proto_csum_replace2(&hdr->check, skb, *portptr, newport,
					 false);
		if (!hdr->check)
			hdr->check = CSUM_MANGLED_0;
	}
	*portptr = newport;
}

static bool udp_manip_pkt(struct sk_buff *skb,
			  unsigned int iphdroff, unsigned int hdroff,
			  const struct nf_conntrack_tuple *tuple,
			  enum nf_nat_manip_type maniptype)
{
	struct udphdr *hdr;

	if (skb_ensure_writable(skb, hdroff + sizeof(*hdr)))
		return false;

	hdr = (struct udphdr *)(skb->data + hdroff);
	__udp_manip_pkt(skb, iphdroff, hdr, tuple, maniptype, !!hdr->check);

	return true;
}

static bool udplite_manip_pkt(struct sk_buff *skb,
			      unsigned int iphdroff, unsigned int hdroff,
			      const struct nf_conntrack_tuple *tuple,
			      enum nf_nat_manip_type maniptype)
{
#ifdef CONFIG_NF_CT_PROTO_UDPLITE
	struct udphdr *hdr;

	if (skb_ensure_writable(skb, hdroff + sizeof(*hdr)))
		return false;

	hdr = (struct udphdr *)(skb->data + hdroff);
	__udp_manip_pkt(skb, iphdroff, hdr, tuple, maniptype, true);
#endif
	return true;
}

static bool
sctp_manip_pkt(struct sk_buff *skb,
	       unsigned int iphdroff, unsigned int hdroff,
	       const struct nf_conntrack_tuple *tuple,
	       enum nf_nat_manip_type maniptype)
{
#ifdef CONFIG_NF_CT_PROTO_SCTP
	struct sctphdr *hdr;
	int hdrsize = 8;

	/* This could be an inner header returned in imcp packet; in such
	 * cases we cannot update the checksum field since it is outside
	 * of the 8 bytes of transport layer headers we are guaranteed.
	 */
	if (skb->len >= hdroff + sizeof(*hdr))
		hdrsize = sizeof(*hdr);

	if (skb_ensure_writable(skb, hdroff + hdrsize))
		return false;

	hdr = (struct sctphdr *)(skb->data + hdroff);

	if (maniptype == NF_NAT_MANIP_SRC) {
		/* Get rid of src port */
		hdr->source = tuple->src.u.sctp.port;
	} else {
		/* Get rid of dst port */
		hdr->dest = tuple->dst.u.sctp.port;
	}

	if (hdrsize < sizeof(*hdr))
		return true;

	if (skb->ip_summed != CHECKSUM_PARTIAL) {
		hdr->checksum = sctp_compute_cksum(skb, hdroff);
		skb->ip_summed = CHECKSUM_NONE;
	}

#endif
	return true;
}

static bool
tcp_manip_pkt(struct sk_buff *skb,
	      unsigned int iphdroff, unsigned int hdroff,
	      const struct nf_conntrack_tuple *tuple,
	      enum nf_nat_manip_type maniptype)
{
	struct tcphdr *hdr;
	__be16 *portptr, newport, oldport;
	int hdrsize = 8; /* TCP connection tracking guarantees this much */

	/* this could be a inner header returned in icmp packet; in such
	   cases we cannot update the checksum field since it is outside of
	   the 8 bytes of transport layer headers we are guaranteed */
	if (skb->len >= hdroff + sizeof(struct tcphdr))
		hdrsize = sizeof(struct tcphdr);

	if (skb_ensure_writable(skb, hdroff + hdrsize))
		return false;

	hdr = (struct tcphdr *)(skb->data + hdroff);

	if (maniptype == NF_NAT_MANIP_SRC) {
		/* Get rid of src port */
		newport = tuple->src.u.tcp.port;
		portptr = &hdr->source;
	} else {
		/* Get rid of dst port */
		newport = tuple->dst.u.tcp.port;
		portptr = &hdr->dest;
	}

	oldport = *portptr;
	*portptr = newport;

	if (hdrsize < sizeof(*hdr))
		return true;

	nf_csum_update(skb, iphdroff, &hdr->check, tuple, maniptype);
	inet_proto_csum_replace2(&hdr->check, skb, oldport, newport, false);
	return true;
}

static bool
dccp_manip_pkt(struct sk_buff *skb,
	       unsigned int iphdroff, unsigned int hdroff,
	       const struct nf_conntrack_tuple *tuple,
	       enum nf_nat_manip_type maniptype)
{
#ifdef CONFIG_NF_CT_PROTO_DCCP
	struct dccp_hdr *hdr;
	__be16 *portptr, oldport, newport;
	int hdrsize = 8; /* DCCP connection tracking guarantees this much */

	if (skb->len >= hdroff + sizeof(struct dccp_hdr))
		hdrsize = sizeof(struct dccp_hdr);

	if (skb_ensure_writable(skb, hdroff + hdrsize))
		return false;

	hdr = (struct dccp_hdr *)(skb->data + hdroff);

	if (maniptype == NF_NAT_MANIP_SRC) {
		newport = tuple->src.u.dccp.port;
		portptr = &hdr->dccph_sport;
	} else {
		newport = tuple->dst.u.dccp.port;
		portptr = &hdr->dccph_dport;
	}

	oldport = *portptr;
	*portptr = newport;

	if (hdrsize < sizeof(*hdr))
		return true;

	nf_csum_update(skb, iphdroff, &hdr->dccph_checksum, tuple, maniptype);
	inet_proto_csum_replace2(&hdr->dccph_checksum, skb, oldport, newport,
				 false);
#endif
	return true;
}

static bool
icmp_manip_pkt(struct sk_buff *skb,
	       unsigned int iphdroff, unsigned int hdroff,
	       const struct nf_conntrack_tuple *tuple,
	       enum nf_nat_manip_type maniptype)
{
	struct icmphdr *hdr;

	if (skb_ensure_writable(skb, hdroff + sizeof(*hdr)))
		return false;

	hdr = (struct icmphdr *)(skb->data + hdroff);
	switch (hdr->type) {
	case ICMP_ECHO:
	case ICMP_ECHOREPLY:
	case ICMP_TIMESTAMP:
	case ICMP_TIMESTAMPREPLY:
	case ICMP_INFO_REQUEST:
	case ICMP_INFO_REPLY:
	case ICMP_ADDRESS:
	case ICMP_ADDRESSREPLY:
		break;
	default:
		return true;
	}
	inet_proto_csum_replace2(&hdr->checksum, skb,
				 hdr->un.echo.id, tuple->src.u.icmp.id, false);
	hdr->un.echo.id = tuple->src.u.icmp.id;
	return true;
}

static bool
icmpv6_manip_pkt(struct sk_buff *skb,
		 unsigned int iphdroff, unsigned int hdroff,
		 const struct nf_conntrack_tuple *tuple,
		 enum nf_nat_manip_type maniptype)
{
	struct icmp6hdr *hdr;

	if (skb_ensure_writable(skb, hdroff + sizeof(*hdr)))
		return false;

	hdr = (struct icmp6hdr *)(skb->data + hdroff);
	nf_csum_update(skb, iphdroff, &hdr->icmp6_cksum, tuple, maniptype);
	if (hdr->icmp6_type == ICMPV6_ECHO_REQUEST ||
	    hdr->icmp6_type == ICMPV6_ECHO_REPLY) {
		inet_proto_csum_replace2(&hdr->icmp6_cksum, skb,
					 hdr->icmp6_identifier,
					 tuple->src.u.icmp.id, false);
		hdr->icmp6_identifier = tuple->src.u.icmp.id;
	}
	return true;
}

/* manipulate a GRE packet according to maniptype */
static bool
gre_manip_pkt(struct sk_buff *skb,
	      unsigned int iphdroff, unsigned int hdroff,
	      const struct nf_conntrack_tuple *tuple,
	      enum nf_nat_manip_type maniptype)
{
#if IS_ENABLED(CONFIG_NF_CT_PROTO_GRE)
	const struct gre_base_hdr *greh;
	struct pptp_gre_header *pgreh;

	/* pgreh includes two optional 32bit fields which are not required
	 * to be there.  That's where the magic '8' comes from */
	if (skb_ensure_writable(skb, hdroff + sizeof(*pgreh) - 8))
		return false;

	greh = (void *)skb->data + hdroff;
	pgreh = (struct pptp_gre_header *)greh;

	/* we only have destination manip of a packet, since 'source key'
	 * is not present in the packet itself */
	if (maniptype != NF_NAT_MANIP_DST)
		return true;

	switch (greh->flags & GRE_VERSION) {
	case GRE_VERSION_0:
		/* We do not currently NAT any GREv0 packets.
		 * Try to behave like "nf_nat_proto_unknown" */
		break;
	case GRE_VERSION_1:
		pr_debug("call_id -> 0x%04x\n", ntohs(tuple->dst.u.gre.key));
		pgreh->call_id = tuple->dst.u.gre.key;
		break;
	default:
		pr_debug("can't nat unknown GRE version\n");
		return false;
	}
#endif
	return true;
}

static bool l4proto_manip_pkt(struct sk_buff *skb,
			      unsigned int iphdroff, unsigned int hdroff,
			      const struct nf_conntrack_tuple *tuple,
			      enum nf_nat_manip_type maniptype)
{
	switch (tuple->dst.protonum) {
	case IPPROTO_TCP:
		return tcp_manip_pkt(skb, iphdroff, hdroff,
				     tuple, maniptype);
	case IPPROTO_UDP:
		return udp_manip_pkt(skb, iphdroff, hdroff,
				     tuple, maniptype);
	case IPPROTO_UDPLITE:
		return udplite_manip_pkt(skb, iphdroff, hdroff,
					 tuple, maniptype);
	case IPPROTO_SCTP:
		return sctp_manip_pkt(skb, iphdroff, hdroff,
				      tuple, maniptype);
	case IPPROTO_ICMP:
		return icmp_manip_pkt(skb, iphdroff, hdroff,
				      tuple, maniptype);
	case IPPROTO_ICMPV6:
		return icmpv6_manip_pkt(skb, iphdroff, hdroff,
					tuple, maniptype);
	case IPPROTO_DCCP:
		return dccp_manip_pkt(skb, iphdroff, hdroff,
				      tuple, maniptype);
	case IPPROTO_GRE:
		return gre_manip_pkt(skb, iphdroff, hdroff,
				     tuple, maniptype);
	}

	/* If we don't know protocol -- no error, pass it unmodified. */
	return true;
}

static bool nf_nat_ipv4_manip_pkt(struct sk_buff *skb,
				  unsigned int iphdroff,
				  const struct nf_conntrack_tuple *target,
				  enum nf_nat_manip_type maniptype)
{
	struct iphdr *iph;
	unsigned int hdroff;

	if (skb_ensure_writable(skb, iphdroff + sizeof(*iph)))
		return false;

	iph = (void *)skb->data + iphdroff;
	hdroff = iphdroff + iph->ihl * 4;

	if (!l4proto_manip_pkt(skb, iphdroff, hdroff, target, maniptype))
		return false;
	iph = (void *)skb->data + iphdroff;

	if (maniptype == NF_NAT_MANIP_SRC) {
		csum_replace4(&iph->check, iph->saddr, target->src.u3.ip);
		iph->saddr = target->src.u3.ip;
	} else {
		csum_replace4(&iph->check, iph->daddr, target->dst.u3.ip);
		iph->daddr = target->dst.u3.ip;
	}
	return true;
}

static bool nf_nat_ipv6_manip_pkt(struct sk_buff *skb,
				  unsigned int iphdroff,
				  const struct nf_conntrack_tuple *target,
				  enum nf_nat_manip_type maniptype)
{
#if IS_ENABLED(CONFIG_IPV6)
	struct ipv6hdr *ipv6h;
	__be16 frag_off;
	int hdroff;
	u8 nexthdr;

	if (skb_ensure_writable(skb, iphdroff + sizeof(*ipv6h)))
		return false;

	ipv6h = (void *)skb->data + iphdroff;
	nexthdr = ipv6h->nexthdr;
	hdroff = ipv6_skip_exthdr(skb, iphdroff + sizeof(*ipv6h),
				  &nexthdr, &frag_off);
	if (hdroff < 0)
		goto manip_addr;

	if ((frag_off & htons(~0x7)) == 0 &&
	    !l4proto_manip_pkt(skb, iphdroff, hdroff, target, maniptype))
		return false;

	/* must reload, offset might have changed */
	ipv6h = (void *)skb->data + iphdroff;

manip_addr:
	if (maniptype == NF_NAT_MANIP_SRC)
		ipv6h->saddr = target->src.u3.in6;
	else
		ipv6h->daddr = target->dst.u3.in6;

#endif
	return true;
}

unsigned int nf_nat_manip_pkt(struct sk_buff *skb, struct nf_conn *ct,
			      enum nf_nat_manip_type mtype,
			      enum ip_conntrack_dir dir)
{
	struct nf_conntrack_tuple target;

	/* We are aiming to look like inverse of other direction. */
	nf_ct_invert_tuple(&target, &ct->tuplehash[!dir].tuple);

	switch (target.src.l3num) {
	case NFPROTO_IPV6:
		if (nf_nat_ipv6_manip_pkt(skb, 0, &target, mtype))
			return NF_ACCEPT;
		break;
	case NFPROTO_IPV4:
		if (nf_nat_ipv4_manip_pkt(skb, 0, &target, mtype))
			return NF_ACCEPT;
		break;
	default:
		WARN_ON_ONCE(1);
		break;
	}

	return NF_DROP;
}

static void nf_nat_ipv4_csum_update(struct sk_buff *skb,
				    unsigned int iphdroff, __sum16 *check,
				    const struct nf_conntrack_tuple *t,
				    enum nf_nat_manip_type maniptype)
{
	struct iphdr *iph = (struct iphdr *)(skb->data + iphdroff);
	__be32 oldip, newip;

	if (maniptype == NF_NAT_MANIP_SRC) {
		oldip = iph->saddr;
		newip = t->src.u3.ip;
	} else {
		oldip = iph->daddr;
		newip = t->dst.u3.ip;
	}
	inet_proto_csum_replace4(check, skb, oldip, newip, true);
}

static void nf_nat_ipv6_csum_update(struct sk_buff *skb,
				    unsigned int iphdroff, __sum16 *check,
				    const struct nf_conntrack_tuple *t,
				    enum nf_nat_manip_type maniptype)
{
#if IS_ENABLED(CONFIG_IPV6)
	const struct ipv6hdr *ipv6h = (struct ipv6hdr *)(skb->data + iphdroff);
	const struct in6_addr *oldip, *newip;

	if (maniptype == NF_NAT_MANIP_SRC) {
		oldip = &ipv6h->saddr;
		newip = &t->src.u3.in6;
	} else {
		oldip = &ipv6h->daddr;
		newip = &t->dst.u3.in6;
	}
	inet_proto_csum_replace16(check, skb, oldip->s6_addr32,
				  newip->s6_addr32, true);
#endif
}

static void nf_csum_update(struct sk_buff *skb,
			   unsigned int iphdroff, __sum16 *check,
			   const struct nf_conntrack_tuple *t,
			   enum nf_nat_manip_type maniptype)
{
	switch (t->src.l3num) {
	case NFPROTO_IPV4:
		nf_nat_ipv4_csum_update(skb, iphdroff, check, t, maniptype);
		return;
	case NFPROTO_IPV6:
		nf_nat_ipv6_csum_update(skb, iphdroff, check, t, maniptype);
		return;
	}
}

static void nf_nat_ipv4_csum_recalc(struct sk_buff *skb,
				    u8 proto, void *data, __sum16 *check,
				    int datalen, int oldlen)
{
	if (skb->ip_summed != CHECKSUM_PARTIAL) {
		const struct iphdr *iph = ip_hdr(skb);

		skb->ip_summed = CHECKSUM_PARTIAL;
		skb->csum_start = skb_headroom(skb) + skb_network_offset(skb) +
			ip_hdrlen(skb);
		skb->csum_offset = (void *)check - data;
		*check = ~csum_tcpudp_magic(iph->saddr, iph->daddr, datalen,
					    proto, 0);
	} else {
		inet_proto_csum_replace2(check, skb,
					 htons(oldlen), htons(datalen), true);
	}
}

#if IS_ENABLED(CONFIG_IPV6)
static void nf_nat_ipv6_csum_recalc(struct sk_buff *skb,
				    u8 proto, void *data, __sum16 *check,
				    int datalen, int oldlen)
{
	if (skb->ip_summed != CHECKSUM_PARTIAL) {
		const struct ipv6hdr *ipv6h = ipv6_hdr(skb);

		skb->ip_summed = CHECKSUM_PARTIAL;
		skb->csum_start = skb_headroom(skb) + skb_network_offset(skb) +
			(data - (void *)skb->data);
		skb->csum_offset = (void *)check - data;
		*check = ~csum_ipv6_magic(&ipv6h->saddr, &ipv6h->daddr,
					  datalen, proto, 0);
	} else {
		inet_proto_csum_replace2(check, skb,
					 htons(oldlen), htons(datalen), true);
	}
}
#endif

void nf_nat_csum_recalc(struct sk_buff *skb,
			u8 nfproto, u8 proto, void *data, __sum16 *check,
			int datalen, int oldlen)
{
	switch (nfproto) {
	case NFPROTO_IPV4:
		nf_nat_ipv4_csum_recalc(skb, proto, data, check,
					datalen, oldlen);
		return;
#if IS_ENABLED(CONFIG_IPV6)
	case NFPROTO_IPV6:
		nf_nat_ipv6_csum_recalc(skb, proto, data, check,
					datalen, oldlen);
		return;
#endif
	}

	WARN_ON_ONCE(1);
}

int nf_nat_icmp_reply_translation(struct sk_buff *skb,
				  struct nf_conn *ct,
				  enum ip_conntrack_info ctinfo,
				  unsigned int hooknum)
{
	struct {
		struct icmphdr	icmp;
		struct iphdr	ip;
	} *inside;
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	enum nf_nat_manip_type manip = HOOK2MANIP(hooknum);
	unsigned int hdrlen = ip_hdrlen(skb);
	struct nf_conntrack_tuple target;
	unsigned long statusbit;

	WARN_ON(ctinfo != IP_CT_RELATED && ctinfo != IP_CT_RELATED_REPLY);

	if (skb_ensure_writable(skb, hdrlen + sizeof(*inside)))
		return 0;
	if (nf_ip_checksum(skb, hooknum, hdrlen, IPPROTO_ICMP))
		return 0;

	inside = (void *)skb->data + hdrlen;
	if (inside->icmp.type == ICMP_REDIRECT) {
		if ((ct->status & IPS_NAT_DONE_MASK) != IPS_NAT_DONE_MASK)
			return 0;
		if (ct->status & IPS_NAT_MASK)
			return 0;
	}

	if (manip == NF_NAT_MANIP_SRC)
		statusbit = IPS_SRC_NAT;
	else
		statusbit = IPS_DST_NAT;

	/* Invert if this is reply direction */
	if (dir == IP_CT_DIR_REPLY)
		statusbit ^= IPS_NAT_MASK;

	if (!(ct->status & statusbit))
		return 1;

	if (!nf_nat_ipv4_manip_pkt(skb, hdrlen + sizeof(inside->icmp),
				   &ct->tuplehash[!dir].tuple, !manip))
		return 0;

	if (skb->ip_summed != CHECKSUM_PARTIAL) {
		/* Reloading "inside" here since manip_pkt may reallocate */
		inside = (void *)skb->data + hdrlen;
		inside->icmp.checksum = 0;
		inside->icmp.checksum =
			csum_fold(skb_checksum(skb, hdrlen,
					       skb->len - hdrlen, 0));
	}

	/* Change outer to look like the reply to an incoming packet */
	nf_ct_invert_tuple(&target, &ct->tuplehash[!dir].tuple);
	target.dst.protonum = IPPROTO_ICMP;
	if (!nf_nat_ipv4_manip_pkt(skb, 0, &target, manip))
		return 0;

	return 1;
}
EXPORT_SYMBOL_GPL(nf_nat_icmp_reply_translation);

static unsigned int
nf_nat_ipv4_fn(void *priv, struct sk_buff *skb,
	       const struct nf_hook_state *state)
{
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct)
		return NF_ACCEPT;

	if (ctinfo == IP_CT_RELATED || ctinfo == IP_CT_RELATED_REPLY) {
		if (ip_hdr(skb)->protocol == IPPROTO_ICMP) {
			if (!nf_nat_icmp_reply_translation(skb, ct, ctinfo,
							   state->hook))
				return NF_DROP;
			else
				return NF_ACCEPT;
		}
	}

	return nf_nat_inet_fn(priv, skb, state);
}

static unsigned int
nf_nat_ipv4_pre_routing(void *priv, struct sk_buff *skb,
			const struct nf_hook_state *state)
{
	unsigned int ret;
	__be32 daddr = ip_hdr(skb)->daddr;

	ret = nf_nat_ipv4_fn(priv, skb, state);
	if (ret == NF_ACCEPT && daddr != ip_hdr(skb)->daddr)
		skb_dst_drop(skb);

	return ret;
}

#ifdef CONFIG_XFRM
static int nf_xfrm_me_harder(struct net *net, struct sk_buff *skb, unsigned int family)
{
	struct sock *sk = skb->sk;
	struct dst_entry *dst;
	unsigned int hh_len;
	struct flowi fl;
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
#endif

static bool nf_nat_inet_port_was_mangled(const struct sk_buff *skb, __be16 sport)
{
	enum ip_conntrack_info ctinfo;
	enum ip_conntrack_dir dir;
	const struct nf_conn *ct;

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct)
		return false;

	switch (nf_ct_protonum(ct)) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
		break;
	default:
		return false;
	}

	dir = CTINFO2DIR(ctinfo);
	if (dir != IP_CT_DIR_ORIGINAL)
		return false;

	return ct->tuplehash[!dir].tuple.dst.u.all != sport;
}

static unsigned int
nf_nat_ipv4_local_in(void *priv, struct sk_buff *skb,
		     const struct nf_hook_state *state)
{
	__be32 saddr = ip_hdr(skb)->saddr;
	struct sock *sk = skb->sk;
	unsigned int ret;

	ret = nf_nat_ipv4_fn(priv, skb, state);

	if (ret != NF_ACCEPT || !sk || inet_sk_transparent(sk))
		return ret;

	/* skb has a socket assigned via tcp edemux. We need to check
	 * if nf_nat_ipv4_fn() has mangled the packet in a way that
	 * edemux would not have found this socket.
	 *
	 * This includes both changes to the source address and changes
	 * to the source port, which are both handled by the
	 * nf_nat_ipv4_fn() call above -- long after tcp/udp early demux
	 * might have found a socket for the old (pre-snat) address.
	 */
	if (saddr != ip_hdr(skb)->saddr ||
	    nf_nat_inet_port_was_mangled(skb, sk->sk_dport))
		skb_orphan(skb); /* TCP edemux obtained wrong socket */

	return ret;
}

static unsigned int
nf_nat_ipv4_out(void *priv, struct sk_buff *skb,
		const struct nf_hook_state *state)
{
#ifdef CONFIG_XFRM
	const struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	int err;
#endif
	unsigned int ret;

	ret = nf_nat_ipv4_fn(priv, skb, state);
#ifdef CONFIG_XFRM
	if (ret != NF_ACCEPT)
		return ret;

	if (IPCB(skb)->flags & IPSKB_XFRM_TRANSFORMED)
		return ret;

	ct = nf_ct_get(skb, &ctinfo);
	if (ct) {
		enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);

		if (ct->tuplehash[dir].tuple.src.u3.ip !=
		     ct->tuplehash[!dir].tuple.dst.u3.ip ||
		    (ct->tuplehash[dir].tuple.dst.protonum != IPPROTO_ICMP &&
		     ct->tuplehash[dir].tuple.src.u.all !=
		     ct->tuplehash[!dir].tuple.dst.u.all)) {
			err = nf_xfrm_me_harder(state->net, skb, AF_INET);
			if (err < 0)
				ret = NF_DROP_ERR(err);
		}
	}
#endif
	return ret;
}

static unsigned int
nf_nat_ipv4_local_fn(void *priv, struct sk_buff *skb,
		     const struct nf_hook_state *state)
{
	const struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	unsigned int ret;
	int err;

	ret = nf_nat_ipv4_fn(priv, skb, state);
	if (ret != NF_ACCEPT)
		return ret;

	ct = nf_ct_get(skb, &ctinfo);
	if (ct) {
		enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);

		if (ct->tuplehash[dir].tuple.dst.u3.ip !=
		    ct->tuplehash[!dir].tuple.src.u3.ip) {
			err = ip_route_me_harder(state->net, state->sk, skb, RTN_UNSPEC);
			if (err < 0)
				ret = NF_DROP_ERR(err);
		}
#ifdef CONFIG_XFRM
		else if (!(IPCB(skb)->flags & IPSKB_XFRM_TRANSFORMED) &&
			 ct->tuplehash[dir].tuple.dst.protonum != IPPROTO_ICMP &&
			 ct->tuplehash[dir].tuple.dst.u.all !=
			 ct->tuplehash[!dir].tuple.src.u.all) {
			err = nf_xfrm_me_harder(state->net, skb, AF_INET);
			if (err < 0)
				ret = NF_DROP_ERR(err);
		}
#endif
	}
	return ret;
}

static const struct nf_hook_ops nf_nat_ipv4_ops[] = {
	/* Before packet filtering, change destination */
	{
		.hook		= nf_nat_ipv4_pre_routing,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_PRE_ROUTING,
		.priority	= NF_IP_PRI_NAT_DST,
	},
	/* After packet filtering, change source */
	{
		.hook		= nf_nat_ipv4_out,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_POST_ROUTING,
		.priority	= NF_IP_PRI_NAT_SRC,
	},
	/* Before packet filtering, change destination */
	{
		.hook		= nf_nat_ipv4_local_fn,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP_PRI_NAT_DST,
	},
	/* After packet filtering, change source */
	{
		.hook		= nf_nat_ipv4_local_in,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP_PRI_NAT_SRC,
	},
};

int nf_nat_ipv4_register_fn(struct net *net, const struct nf_hook_ops *ops)
{
	return nf_nat_register_fn(net, ops->pf, ops, nf_nat_ipv4_ops,
				  ARRAY_SIZE(nf_nat_ipv4_ops));
}
EXPORT_SYMBOL_GPL(nf_nat_ipv4_register_fn);

void nf_nat_ipv4_unregister_fn(struct net *net, const struct nf_hook_ops *ops)
{
	nf_nat_unregister_fn(net, ops->pf, ops, ARRAY_SIZE(nf_nat_ipv4_ops));
}
EXPORT_SYMBOL_GPL(nf_nat_ipv4_unregister_fn);

#if IS_ENABLED(CONFIG_IPV6)
int nf_nat_icmpv6_reply_translation(struct sk_buff *skb,
				    struct nf_conn *ct,
				    enum ip_conntrack_info ctinfo,
				    unsigned int hooknum,
				    unsigned int hdrlen)
{
	struct {
		struct icmp6hdr	icmp6;
		struct ipv6hdr	ip6;
	} *inside;
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	enum nf_nat_manip_type manip = HOOK2MANIP(hooknum);
	struct nf_conntrack_tuple target;
	unsigned long statusbit;

	WARN_ON(ctinfo != IP_CT_RELATED && ctinfo != IP_CT_RELATED_REPLY);

	if (skb_ensure_writable(skb, hdrlen + sizeof(*inside)))
		return 0;
	if (nf_ip6_checksum(skb, hooknum, hdrlen, IPPROTO_ICMPV6))
		return 0;

	inside = (void *)skb->data + hdrlen;
	if (inside->icmp6.icmp6_type == NDISC_REDIRECT) {
		if ((ct->status & IPS_NAT_DONE_MASK) != IPS_NAT_DONE_MASK)
			return 0;
		if (ct->status & IPS_NAT_MASK)
			return 0;
	}

	if (manip == NF_NAT_MANIP_SRC)
		statusbit = IPS_SRC_NAT;
	else
		statusbit = IPS_DST_NAT;

	/* Invert if this is reply direction */
	if (dir == IP_CT_DIR_REPLY)
		statusbit ^= IPS_NAT_MASK;

	if (!(ct->status & statusbit))
		return 1;

	if (!nf_nat_ipv6_manip_pkt(skb, hdrlen + sizeof(inside->icmp6),
				   &ct->tuplehash[!dir].tuple, !manip))
		return 0;

	if (skb->ip_summed != CHECKSUM_PARTIAL) {
		struct ipv6hdr *ipv6h = ipv6_hdr(skb);

		inside = (void *)skb->data + hdrlen;
		inside->icmp6.icmp6_cksum = 0;
		inside->icmp6.icmp6_cksum =
			csum_ipv6_magic(&ipv6h->saddr, &ipv6h->daddr,
					skb->len - hdrlen, IPPROTO_ICMPV6,
					skb_checksum(skb, hdrlen,
						     skb->len - hdrlen, 0));
	}

	nf_ct_invert_tuple(&target, &ct->tuplehash[!dir].tuple);
	target.dst.protonum = IPPROTO_ICMPV6;
	if (!nf_nat_ipv6_manip_pkt(skb, 0, &target, manip))
		return 0;

	return 1;
}
EXPORT_SYMBOL_GPL(nf_nat_icmpv6_reply_translation);

static unsigned int
nf_nat_ipv6_fn(void *priv, struct sk_buff *skb,
	       const struct nf_hook_state *state)
{
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	__be16 frag_off;
	int hdrlen;
	u8 nexthdr;

	ct = nf_ct_get(skb, &ctinfo);
	/* Can't track?  It's not due to stress, or conntrack would
	 * have dropped it.  Hence it's the user's responsibilty to
	 * packet filter it out, or implement conntrack/NAT for that
	 * protocol. 8) --RR
	 */
	if (!ct)
		return NF_ACCEPT;

	if (ctinfo == IP_CT_RELATED || ctinfo == IP_CT_RELATED_REPLY) {
		nexthdr = ipv6_hdr(skb)->nexthdr;
		hdrlen = ipv6_skip_exthdr(skb, sizeof(struct ipv6hdr),
					  &nexthdr, &frag_off);

		if (hdrlen >= 0 && nexthdr == IPPROTO_ICMPV6) {
			if (!nf_nat_icmpv6_reply_translation(skb, ct, ctinfo,
							     state->hook,
							     hdrlen))
				return NF_DROP;
			else
				return NF_ACCEPT;
		}
	}

	return nf_nat_inet_fn(priv, skb, state);
}

static unsigned int
nf_nat_ipv6_local_in(void *priv, struct sk_buff *skb,
		     const struct nf_hook_state *state)
{
	struct in6_addr saddr = ipv6_hdr(skb)->saddr;
	struct sock *sk = skb->sk;
	unsigned int ret;

	ret = nf_nat_ipv6_fn(priv, skb, state);

	if (ret != NF_ACCEPT || !sk || inet_sk_transparent(sk))
		return ret;

	/* see nf_nat_ipv4_local_in */
	if (ipv6_addr_cmp(&saddr, &ipv6_hdr(skb)->saddr) ||
	    nf_nat_inet_port_was_mangled(skb, sk->sk_dport))
		skb_orphan(skb);

	return ret;
}

static unsigned int
nf_nat_ipv6_in(void *priv, struct sk_buff *skb,
	       const struct nf_hook_state *state)
{
	unsigned int ret, verdict;
	struct in6_addr daddr = ipv6_hdr(skb)->daddr;

	ret = nf_nat_ipv6_fn(priv, skb, state);
	verdict = ret & NF_VERDICT_MASK;
	if (verdict != NF_DROP && verdict != NF_STOLEN &&
	    ipv6_addr_cmp(&daddr, &ipv6_hdr(skb)->daddr))
		skb_dst_drop(skb);

	return ret;
}

static unsigned int
nf_nat_ipv6_out(void *priv, struct sk_buff *skb,
		const struct nf_hook_state *state)
{
#ifdef CONFIG_XFRM
	const struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	int err;
#endif
	unsigned int ret;

	ret = nf_nat_ipv6_fn(priv, skb, state);
#ifdef CONFIG_XFRM
	if (ret != NF_ACCEPT)
		return ret;

	if (IP6CB(skb)->flags & IP6SKB_XFRM_TRANSFORMED)
		return ret;
	ct = nf_ct_get(skb, &ctinfo);
	if (ct) {
		enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);

		if (!nf_inet_addr_cmp(&ct->tuplehash[dir].tuple.src.u3,
				      &ct->tuplehash[!dir].tuple.dst.u3) ||
		    (ct->tuplehash[dir].tuple.dst.protonum != IPPROTO_ICMPV6 &&
		     ct->tuplehash[dir].tuple.src.u.all !=
		     ct->tuplehash[!dir].tuple.dst.u.all)) {
			err = nf_xfrm_me_harder(state->net, skb, AF_INET6);
			if (err < 0)
				ret = NF_DROP_ERR(err);
		}
	}
#endif

	return ret;
}

static unsigned int
nf_nat_ipv6_local_fn(void *priv, struct sk_buff *skb,
		     const struct nf_hook_state *state)
{
	const struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	unsigned int ret;
	int err;

	ret = nf_nat_ipv6_fn(priv, skb, state);
	if (ret != NF_ACCEPT)
		return ret;

	ct = nf_ct_get(skb, &ctinfo);
	if (ct) {
		enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);

		if (!nf_inet_addr_cmp(&ct->tuplehash[dir].tuple.dst.u3,
				      &ct->tuplehash[!dir].tuple.src.u3)) {
			err = nf_ip6_route_me_harder(state->net, state->sk, skb);
			if (err < 0)
				ret = NF_DROP_ERR(err);
		}
#ifdef CONFIG_XFRM
		else if (!(IP6CB(skb)->flags & IP6SKB_XFRM_TRANSFORMED) &&
			 ct->tuplehash[dir].tuple.dst.protonum != IPPROTO_ICMPV6 &&
			 ct->tuplehash[dir].tuple.dst.u.all !=
			 ct->tuplehash[!dir].tuple.src.u.all) {
			err = nf_xfrm_me_harder(state->net, skb, AF_INET6);
			if (err < 0)
				ret = NF_DROP_ERR(err);
		}
#endif
	}

	return ret;
}

static const struct nf_hook_ops nf_nat_ipv6_ops[] = {
	/* Before packet filtering, change destination */
	{
		.hook		= nf_nat_ipv6_in,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_PRE_ROUTING,
		.priority	= NF_IP6_PRI_NAT_DST,
	},
	/* After packet filtering, change source */
	{
		.hook		= nf_nat_ipv6_out,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_POST_ROUTING,
		.priority	= NF_IP6_PRI_NAT_SRC,
	},
	/* Before packet filtering, change destination */
	{
		.hook		= nf_nat_ipv6_local_fn,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP6_PRI_NAT_DST,
	},
	/* After packet filtering, change source */
	{
		.hook		= nf_nat_ipv6_local_in,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP6_PRI_NAT_SRC,
	},
};

int nf_nat_ipv6_register_fn(struct net *net, const struct nf_hook_ops *ops)
{
	return nf_nat_register_fn(net, ops->pf, ops, nf_nat_ipv6_ops,
				  ARRAY_SIZE(nf_nat_ipv6_ops));
}
EXPORT_SYMBOL_GPL(nf_nat_ipv6_register_fn);

void nf_nat_ipv6_unregister_fn(struct net *net, const struct nf_hook_ops *ops)
{
	nf_nat_unregister_fn(net, ops->pf, ops, ARRAY_SIZE(nf_nat_ipv6_ops));
}
EXPORT_SYMBOL_GPL(nf_nat_ipv6_unregister_fn);
#endif /* CONFIG_IPV6 */

#if defined(CONFIG_NF_TABLES_INET) && IS_ENABLED(CONFIG_NFT_NAT)
int nf_nat_inet_register_fn(struct net *net, const struct nf_hook_ops *ops)
{
	int ret;

	if (WARN_ON_ONCE(ops->pf != NFPROTO_INET))
		return -EINVAL;

	ret = nf_nat_register_fn(net, NFPROTO_IPV6, ops, nf_nat_ipv6_ops,
				 ARRAY_SIZE(nf_nat_ipv6_ops));
	if (ret)
		return ret;

	ret = nf_nat_register_fn(net, NFPROTO_IPV4, ops, nf_nat_ipv4_ops,
				 ARRAY_SIZE(nf_nat_ipv4_ops));
	if (ret)
		nf_nat_unregister_fn(net, NFPROTO_IPV6, ops,
					ARRAY_SIZE(nf_nat_ipv6_ops));
	return ret;
}
EXPORT_SYMBOL_GPL(nf_nat_inet_register_fn);

void nf_nat_inet_unregister_fn(struct net *net, const struct nf_hook_ops *ops)
{
	nf_nat_unregister_fn(net, NFPROTO_IPV4, ops, ARRAY_SIZE(nf_nat_ipv4_ops));
	nf_nat_unregister_fn(net, NFPROTO_IPV6, ops, ARRAY_SIZE(nf_nat_ipv6_ops));
}
EXPORT_SYMBOL_GPL(nf_nat_inet_unregister_fn);
#endif /* NFT INET NAT */
