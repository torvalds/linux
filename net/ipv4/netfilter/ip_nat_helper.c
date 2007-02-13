/* ip_nat_helper.c - generic support functions for NAT helpers
 *
 * (C) 2000-2002 Harald Welte <laforge@netfilter.org>
 * (C) 2003-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * 	14 Jan 2002 Harald Welte <laforge@gnumonks.org>:
 *		- add support for SACK adjustment
 *	14 Mar 2002 Harald Welte <laforge@gnumonks.org>:
 *		- merge SACK support into newnat API
 *	16 Aug 2002 Brian J. Murrell <netfilter@interlinx.bc.ca>:
 *		- make ip_nat_resize_packet more generic (TCP and UDP)
 *		- add ip_nat_mangle_udp_packet
 */
#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/skbuff.h>
#include <linux/netfilter_ipv4.h>
#include <net/checksum.h>
#include <net/icmp.h>
#include <net/ip.h>
#include <net/tcp.h>
#include <net/udp.h>

#include <linux/netfilter_ipv4/ip_conntrack.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#include <linux/netfilter_ipv4/ip_nat.h>
#include <linux/netfilter_ipv4/ip_nat_protocol.h>
#include <linux/netfilter_ipv4/ip_nat_core.h>
#include <linux/netfilter_ipv4/ip_nat_helper.h>

#if 0
#define DEBUGP printk
#define DUMP_OFFSET(x)	printk("offset_before=%d, offset_after=%d, correction_pos=%u\n", x->offset_before, x->offset_after, x->correction_pos);
#else
#define DEBUGP(format, args...)
#define DUMP_OFFSET(x)
#endif

static DEFINE_SPINLOCK(ip_nat_seqofs_lock);

/* Setup TCP sequence correction given this change at this sequence */
static inline void
adjust_tcp_sequence(u32 seq,
		    int sizediff,
		    struct ip_conntrack *ct,
		    enum ip_conntrack_info ctinfo)
{
	int dir;
	struct ip_nat_seq *this_way, *other_way;

	DEBUGP("ip_nat_resize_packet: old_size = %u, new_size = %u\n",
		(*skb)->len, new_size);

	dir = CTINFO2DIR(ctinfo);

	this_way = &ct->nat.info.seq[dir];
	other_way = &ct->nat.info.seq[!dir];

	DEBUGP("ip_nat_resize_packet: Seq_offset before: ");
	DUMP_OFFSET(this_way);

	spin_lock_bh(&ip_nat_seqofs_lock);

	/* SYN adjust. If it's uninitialized, or this is after last
	 * correction, record it: we don't handle more than one
	 * adjustment in the window, but do deal with common case of a
	 * retransmit */
	if (this_way->offset_before == this_way->offset_after
	    || before(this_way->correction_pos, seq)) {
		    this_way->correction_pos = seq;
		    this_way->offset_before = this_way->offset_after;
		    this_way->offset_after += sizediff;
	}
	spin_unlock_bh(&ip_nat_seqofs_lock);

	DEBUGP("ip_nat_resize_packet: Seq_offset after: ");
	DUMP_OFFSET(this_way);
}

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
	data = (unsigned char *)skb->nh.iph + dataoff;

	/* move post-replacement */
	memmove(data + match_offset + rep_len,
		data + match_offset + match_len,
		skb->tail - (data + match_offset + match_len));

	/* insert data from buffer */
	memcpy(data + match_offset, rep_buffer, rep_len);

	/* update skb info */
	if (rep_len > match_len) {
		DEBUGP("ip_nat_mangle_packet: Extending packet by "
			"%u from %u bytes\n", rep_len - match_len,
		       skb->len);
		skb_put(skb, rep_len - match_len);
	} else {
		DEBUGP("ip_nat_mangle_packet: Shrinking packet from "
			"%u from %u bytes\n", match_len - rep_len,
		       skb->len);
		__skb_trim(skb, skb->len + rep_len - match_len);
	}

	/* fix IP hdr checksum information */
	skb->nh.iph->tot_len = htons(skb->len);
	ip_send_check(skb->nh.iph);
}

/* Unusual, but possible case. */
static int enlarge_skb(struct sk_buff **pskb, unsigned int extra)
{
	struct sk_buff *nskb;

	if ((*pskb)->len + extra > 65535)
		return 0;

	nskb = skb_copy_expand(*pskb, skb_headroom(*pskb), extra, GFP_ATOMIC);
	if (!nskb)
		return 0;

	/* Transfer socket to new skb. */
	if ((*pskb)->sk)
		skb_set_owner_w(nskb, (*pskb)->sk);
	kfree_skb(*pskb);
	*pskb = nskb;
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
int
ip_nat_mangle_tcp_packet(struct sk_buff **pskb,
			 struct ip_conntrack *ct,
			 enum ip_conntrack_info ctinfo,
			 unsigned int match_offset,
			 unsigned int match_len,
			 const char *rep_buffer,
			 unsigned int rep_len)
{
	struct iphdr *iph;
	struct tcphdr *tcph;
	int oldlen, datalen;

	if (!skb_make_writable(pskb, (*pskb)->len))
		return 0;

	if (rep_len > match_len
	    && rep_len - match_len > skb_tailroom(*pskb)
	    && !enlarge_skb(pskb, rep_len - match_len))
		return 0;

	SKB_LINEAR_ASSERT(*pskb);

	iph = (*pskb)->nh.iph;
	tcph = (void *)iph + iph->ihl*4;

	oldlen = (*pskb)->len - iph->ihl*4;
	mangle_contents(*pskb, iph->ihl*4 + tcph->doff*4,
			match_offset, match_len, rep_buffer, rep_len);

	datalen = (*pskb)->len - iph->ihl*4;
	if ((*pskb)->ip_summed != CHECKSUM_PARTIAL) {
		tcph->check = 0;
		tcph->check = tcp_v4_check(datalen,
					   iph->saddr, iph->daddr,
					   csum_partial((char *)tcph,
							datalen, 0));
	} else
		nf_proto_csum_replace2(&tcph->check, *pskb,
					htons(oldlen), htons(datalen), 1);

	if (rep_len != match_len) {
		set_bit(IPS_SEQ_ADJUST_BIT, &ct->status);
		adjust_tcp_sequence(ntohl(tcph->seq),
				    (int)rep_len - (int)match_len,
				    ct, ctinfo);
		/* Tell TCP window tracking about seq change */
		ip_conntrack_tcp_update(*pskb, ct, CTINFO2DIR(ctinfo));
	}
	return 1;
}
EXPORT_SYMBOL(ip_nat_mangle_tcp_packet);

/* Generic function for mangling variable-length address changes inside
 * NATed UDP connections (like the CONNECT DATA XXXXX MESG XXXXX INDEX XXXXX
 * command in the Amanda protocol)
 *
 * Takes care about all the nasty sequence number changes, checksumming,
 * skb enlargement, ...
 *
 * XXX - This function could be merged with ip_nat_mangle_tcp_packet which
 *       should be fairly easy to do.
 */
int
ip_nat_mangle_udp_packet(struct sk_buff **pskb,
			 struct ip_conntrack *ct,
			 enum ip_conntrack_info ctinfo,
			 unsigned int match_offset,
			 unsigned int match_len,
			 const char *rep_buffer,
			 unsigned int rep_len)
{
	struct iphdr *iph;
	struct udphdr *udph;
	int datalen, oldlen;

	/* UDP helpers might accidentally mangle the wrong packet */
	iph = (*pskb)->nh.iph;
	if ((*pskb)->len < iph->ihl*4 + sizeof(*udph) +
			       match_offset + match_len)
		return 0;

	if (!skb_make_writable(pskb, (*pskb)->len))
		return 0;

	if (rep_len > match_len
	    && rep_len - match_len > skb_tailroom(*pskb)
	    && !enlarge_skb(pskb, rep_len - match_len))
		return 0;

	iph = (*pskb)->nh.iph;
	udph = (void *)iph + iph->ihl*4;

	oldlen = (*pskb)->len - iph->ihl*4;
	mangle_contents(*pskb, iph->ihl*4 + sizeof(*udph),
			match_offset, match_len, rep_buffer, rep_len);

	/* update the length of the UDP packet */
	datalen = (*pskb)->len - iph->ihl*4;
	udph->len = htons(datalen);

	/* fix udp checksum if udp checksum was previously calculated */
	if (!udph->check && (*pskb)->ip_summed != CHECKSUM_PARTIAL)
		return 1;

	if ((*pskb)->ip_summed != CHECKSUM_PARTIAL) {
		udph->check = 0;
		udph->check = csum_tcpudp_magic(iph->saddr, iph->daddr,
						datalen, IPPROTO_UDP,
						csum_partial((char *)udph,
							     datalen, 0));
		if (!udph->check)
			udph->check = CSUM_MANGLED_0;
	} else
		nf_proto_csum_replace2(&udph->check, *pskb,
					htons(oldlen), htons(datalen), 1);
	return 1;
}
EXPORT_SYMBOL(ip_nat_mangle_udp_packet);

/* Adjust one found SACK option including checksum correction */
static void
sack_adjust(struct sk_buff *skb,
	    struct tcphdr *tcph,
	    unsigned int sackoff,
	    unsigned int sackend,
	    struct ip_nat_seq *natseq)
{
	while (sackoff < sackend) {
		struct tcp_sack_block_wire *sack;
		__be32 new_start_seq, new_end_seq;

		sack = (void *)skb->data + sackoff;
		if (after(ntohl(sack->start_seq) - natseq->offset_before,
			  natseq->correction_pos))
			new_start_seq = htonl(ntohl(sack->start_seq)
					- natseq->offset_after);
		else
			new_start_seq = htonl(ntohl(sack->start_seq)
					- natseq->offset_before);

		if (after(ntohl(sack->end_seq) - natseq->offset_before,
			  natseq->correction_pos))
			new_end_seq = htonl(ntohl(sack->end_seq)
				      - natseq->offset_after);
		else
			new_end_seq = htonl(ntohl(sack->end_seq)
				      - natseq->offset_before);

		DEBUGP("sack_adjust: start_seq: %d->%d, end_seq: %d->%d\n",
			ntohl(sack->start_seq), new_start_seq,
			ntohl(sack->end_seq), new_end_seq);

		nf_proto_csum_replace4(&tcph->check, skb,
					sack->start_seq, new_start_seq, 0);
		nf_proto_csum_replace4(&tcph->check, skb,
					sack->end_seq, new_end_seq, 0);
		sack->start_seq = new_start_seq;
		sack->end_seq = new_end_seq;
		sackoff += sizeof(*sack);
	}
}

/* TCP SACK sequence number adjustment */
static inline unsigned int
ip_nat_sack_adjust(struct sk_buff **pskb,
		   struct tcphdr *tcph,
		   struct ip_conntrack *ct,
		   enum ip_conntrack_info ctinfo)
{
	unsigned int dir, optoff, optend;

	optoff = (*pskb)->nh.iph->ihl*4 + sizeof(struct tcphdr);
	optend = (*pskb)->nh.iph->ihl*4 + tcph->doff*4;

	if (!skb_make_writable(pskb, optend))
		return 0;

	dir = CTINFO2DIR(ctinfo);

	while (optoff < optend) {
		/* Usually: option, length. */
		unsigned char *op = (*pskb)->data + optoff;

		switch (op[0]) {
		case TCPOPT_EOL:
			return 1;
		case TCPOPT_NOP:
			optoff++;
			continue;
		default:
			/* no partial options */
			if (optoff + 1 == optend
			    || optoff + op[1] > optend
			    || op[1] < 2)
				return 0;
			if (op[0] == TCPOPT_SACK
			    && op[1] >= 2+TCPOLEN_SACK_PERBLOCK
			    && ((op[1] - 2) % TCPOLEN_SACK_PERBLOCK) == 0)
				sack_adjust(*pskb, tcph, optoff+2,
					    optoff+op[1],
					    &ct->nat.info.seq[!dir]);
			optoff += op[1];
		}
	}
	return 1;
}

/* TCP sequence number adjustment.  Returns 1 on success, 0 on failure */
int
ip_nat_seq_adjust(struct sk_buff **pskb,
		  struct ip_conntrack *ct,
		  enum ip_conntrack_info ctinfo)
{
	struct tcphdr *tcph;
	int dir;
	__be32 newseq, newack;
	struct ip_nat_seq *this_way, *other_way;

	dir = CTINFO2DIR(ctinfo);

	this_way = &ct->nat.info.seq[dir];
	other_way = &ct->nat.info.seq[!dir];

	if (!skb_make_writable(pskb, (*pskb)->nh.iph->ihl*4+sizeof(*tcph)))
		return 0;

	tcph = (void *)(*pskb)->data + (*pskb)->nh.iph->ihl*4;
	if (after(ntohl(tcph->seq), this_way->correction_pos))
		newseq = htonl(ntohl(tcph->seq) + this_way->offset_after);
	else
		newseq = htonl(ntohl(tcph->seq) + this_way->offset_before);

	if (after(ntohl(tcph->ack_seq) - other_way->offset_before,
		  other_way->correction_pos))
		newack = htonl(ntohl(tcph->ack_seq) - other_way->offset_after);
	else
		newack = htonl(ntohl(tcph->ack_seq) - other_way->offset_before);

	nf_proto_csum_replace4(&tcph->check, *pskb, tcph->seq, newseq, 0);
	nf_proto_csum_replace4(&tcph->check, *pskb, tcph->ack_seq, newack, 0);

	DEBUGP("Adjusting sequence number from %u->%u, ack from %u->%u\n",
		ntohl(tcph->seq), ntohl(newseq), ntohl(tcph->ack_seq),
		ntohl(newack));

	tcph->seq = newseq;
	tcph->ack_seq = newack;

	if (!ip_nat_sack_adjust(pskb, tcph, ct, ctinfo))
		return 0;

	ip_conntrack_tcp_update(*pskb, ct, dir);

	return 1;
}
EXPORT_SYMBOL(ip_nat_seq_adjust);

/* Setup NAT on this expected conntrack so it follows master. */
/* If we fail to get a free NAT slot, we'll get dropped on confirm */
void ip_nat_follow_master(struct ip_conntrack *ct,
			  struct ip_conntrack_expect *exp)
{
	struct ip_nat_range range;

	/* This must be a fresh one. */
	BUG_ON(ct->status & IPS_NAT_DONE_MASK);

	/* Change src to where master sends to */
	range.flags = IP_NAT_RANGE_MAP_IPS;
	range.min_ip = range.max_ip
		= ct->master->tuplehash[!exp->dir].tuple.dst.ip;
	/* hook doesn't matter, but it has to do source manip */
	ip_nat_setup_info(ct, &range, NF_IP_POST_ROUTING);

	/* For DST manip, map port here to where it's expected. */
	range.flags = (IP_NAT_RANGE_MAP_IPS | IP_NAT_RANGE_PROTO_SPECIFIED);
	range.min = range.max = exp->saved_proto;
	range.min_ip = range.max_ip
		= ct->master->tuplehash[!exp->dir].tuple.src.ip;
	/* hook doesn't matter, but it has to do destination manip */
	ip_nat_setup_info(ct, &range, NF_IP_PRE_ROUTING);
}
EXPORT_SYMBOL(ip_nat_follow_master);
