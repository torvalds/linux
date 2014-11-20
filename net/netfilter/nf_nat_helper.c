/* nf_nat_helper.c - generic support functions for NAT helpers
 *
 * (C) 2000-2002 Harald Welte <laforge@netfilter.org>
 * (C) 2003-2006 Netfilter Core Team <coreteam@netfilter.org>
 * (C) 2007-2012 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <net/tcp.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_ecache.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <net/netfilter/nf_conntrack_seqadj.h>
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_l3proto.h>
#include <net/netfilter/nf_nat_l4proto.h>
#include <net/netfilter/nf_nat_core.h>
#include <net/netfilter/nf_nat_helper.h>

/* Frobs data inside this packet, which is linear. */
static void mangle_contents(struct sk_buff *skb,
			    unsigned int dataoff,
			    unsigned int match_offset,
			    unsigned int match_len,
			    const char *rep_buffer,
			    unsigned int rep_len)
{
	unsigned char *data;

	BUG_ON(skb_is_nonlinear(skb));
	data = skb_network_header(skb) + dataoff;

	/* move post-replacement */
	memmove(data + match_offset + rep_len,
		data + match_offset + match_len,
		skb_tail_pointer(skb) - (skb_network_header(skb) + dataoff +
			     match_offset + match_len));

	/* insert data from buffer */
	memcpy(data + match_offset, rep_buffer, rep_len);

	/* update skb info */
	if (rep_len > match_len) {
		pr_debug("nf_nat_mangle_packet: Extending packet by "
			 "%u from %u bytes\n", rep_len - match_len, skb->len);
		skb_put(skb, rep_len - match_len);
	} else {
		pr_debug("nf_nat_mangle_packet: Shrinking packet from "
			 "%u from %u bytes\n", match_len - rep_len, skb->len);
		__skb_trim(skb, skb->len + rep_len - match_len);
	}

	if (nf_ct_l3num((struct nf_conn *)skb->nfct) == NFPROTO_IPV4) {
		/* fix IP hdr checksum information */
		ip_hdr(skb)->tot_len = htons(skb->len);
		ip_send_check(ip_hdr(skb));
	} else
		ipv6_hdr(skb)->payload_len =
			htons(skb->len - sizeof(struct ipv6hdr));
}

/* Unusual, but possible case. */
static int enlarge_skb(struct sk_buff *skb, unsigned int extra)
{
	if (skb->len + extra > 65535)
		return 0;

	if (pskb_expand_head(skb, 0, extra - skb_tailroom(skb), GFP_ATOMIC))
		return 0;

	return 1;
}

/* Generic function for mangling variable-length address changes inside
 * NATed TCP connections (like the PORT XXX,XXX,XXX,XXX,XXX,XXX
 * command in FTP).
 *
 * Takes care about all the nasty sequence number changes, checksumming,
 * skb enlargement, ...
 *
 * */
int __nf_nat_mangle_tcp_packet(struct sk_buff *skb,
			       struct nf_conn *ct,
			       enum ip_conntrack_info ctinfo,
			       unsigned int protoff,
			       unsigned int match_offset,
			       unsigned int match_len,
			       const char *rep_buffer,
			       unsigned int rep_len, bool adjust)
{
	const struct nf_nat_l3proto *l3proto;
	struct tcphdr *tcph;
	int oldlen, datalen;

	if (!skb_make_writable(skb, skb->len))
		return 0;

	if (rep_len > match_len &&
	    rep_len - match_len > skb_tailroom(skb) &&
	    !enlarge_skb(skb, rep_len - match_len))
		return 0;

	SKB_LINEAR_ASSERT(skb);

	tcph = (void *)skb->data + protoff;

	oldlen = skb->len - protoff;
	mangle_contents(skb, protoff + tcph->doff*4,
			match_offset, match_len, rep_buffer, rep_len);

	datalen = skb->len - protoff;

	l3proto = __nf_nat_l3proto_find(nf_ct_l3num(ct));
	l3proto->csum_recalc(skb, IPPROTO_TCP, tcph, &tcph->check,
			     datalen, oldlen);

	if (adjust && rep_len != match_len)
		nf_ct_seqadj_set(ct, ctinfo, tcph->seq,
				 (int)rep_len - (int)match_len);

	return 1;
}
EXPORT_SYMBOL(__nf_nat_mangle_tcp_packet);

/* Generic function for mangling variable-length address changes inside
 * NATed UDP connections (like the CONNECT DATA XXXXX MESG XXXXX INDEX XXXXX
 * command in the Amanda protocol)
 *
 * Takes care about all the nasty sequence number changes, checksumming,
 * skb enlargement, ...
 *
 * XXX - This function could be merged with nf_nat_mangle_tcp_packet which
 *       should be fairly easy to do.
 */
int
nf_nat_mangle_udp_packet(struct sk_buff *skb,
			 struct nf_conn *ct,
			 enum ip_conntrack_info ctinfo,
			 unsigned int protoff,
			 unsigned int match_offset,
			 unsigned int match_len,
			 const char *rep_buffer,
			 unsigned int rep_len)
{
	const struct nf_nat_l3proto *l3proto;
	struct udphdr *udph;
	int datalen, oldlen;

	if (!skb_make_writable(skb, skb->len))
		return 0;

	if (rep_len > match_len &&
	    rep_len - match_len > skb_tailroom(skb) &&
	    !enlarge_skb(skb, rep_len - match_len))
		return 0;

	udph = (void *)skb->data + protoff;

	oldlen = skb->len - protoff;
	mangle_contents(skb, protoff + sizeof(*udph),
			match_offset, match_len, rep_buffer, rep_len);

	/* update the length of the UDP packet */
	datalen = skb->len - protoff;
	udph->len = htons(datalen);

	/* fix udp checksum if udp checksum was previously calculated */
	if (!udph->check && skb->ip_summed != CHECKSUM_PARTIAL)
		return 1;

	l3proto = __nf_nat_l3proto_find(nf_ct_l3num(ct));
	l3proto->csum_recalc(skb, IPPROTO_UDP, udph, &udph->check,
			     datalen, oldlen);

	return 1;
}
EXPORT_SYMBOL(nf_nat_mangle_udp_packet);

/* Setup NAT on this expected conntrack so it follows master. */
/* If we fail to get a free NAT slot, we'll get dropped on confirm */
void nf_nat_follow_master(struct nf_conn *ct,
			  struct nf_conntrack_expect *exp)
{
	struct nf_nat_range range;

	/* This must be a fresh one. */
	BUG_ON(ct->status & IPS_NAT_DONE_MASK);

	/* Change src to where master sends to */
	range.flags = NF_NAT_RANGE_MAP_IPS;
	range.min_addr = range.max_addr
		= ct->master->tuplehash[!exp->dir].tuple.dst.u3;
	nf_nat_setup_info(ct, &range, NF_NAT_MANIP_SRC);

	/* For DST manip, map port here to where it's expected. */
	range.flags = (NF_NAT_RANGE_MAP_IPS | NF_NAT_RANGE_PROTO_SPECIFIED);
	range.min_proto = range.max_proto = exp->saved_proto;
	range.min_addr = range.max_addr
		= ct->master->tuplehash[!exp->dir].tuple.src.u3;
	nf_nat_setup_info(ct, &range, NF_NAT_MANIP_DST);
}
EXPORT_SYMBOL(nf_nat_follow_master);
