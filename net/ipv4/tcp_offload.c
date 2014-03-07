/*
 *	IPV4 GSO/GRO offload support
 *	Linux INET implementation
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	TCPv4 GSO/GRO support
 */

#include <linux/skbuff.h>
#include <net/tcp.h>
#include <net/protocol.h>

struct sk_buff *tcp_gso_segment(struct sk_buff *skb,
				netdev_features_t features)
{
	struct sk_buff *segs = ERR_PTR(-EINVAL);
	unsigned int sum_truesize = 0;
	struct tcphdr *th;
	unsigned int thlen;
	unsigned int seq;
	__be32 delta;
	unsigned int oldlen;
	unsigned int mss;
	struct sk_buff *gso_skb = skb;
	__sum16 newcheck;
	bool ooo_okay, copy_destructor;

	if (!pskb_may_pull(skb, sizeof(*th)))
		goto out;

	th = tcp_hdr(skb);
	thlen = th->doff * 4;
	if (thlen < sizeof(*th))
		goto out;

	if (!pskb_may_pull(skb, thlen))
		goto out;

	oldlen = (u16)~skb->len;
	__skb_pull(skb, thlen);

	mss = tcp_skb_mss(skb);
	if (unlikely(skb->len <= mss))
		goto out;

	if (skb_gso_ok(skb, features | NETIF_F_GSO_ROBUST)) {
		/* Packet is from an untrusted source, reset gso_segs. */
		int type = skb_shinfo(skb)->gso_type;

		if (unlikely(type &
			     ~(SKB_GSO_TCPV4 |
			       SKB_GSO_DODGY |
			       SKB_GSO_TCP_ECN |
			       SKB_GSO_TCPV6 |
			       SKB_GSO_GRE |
			       SKB_GSO_IPIP |
			       SKB_GSO_SIT |
			       SKB_GSO_MPLS |
			       SKB_GSO_UDP_TUNNEL |
			       0) ||
			     !(type & (SKB_GSO_TCPV4 | SKB_GSO_TCPV6))))
			goto out;

		skb_shinfo(skb)->gso_segs = DIV_ROUND_UP(skb->len, mss);

		segs = NULL;
		goto out;
	}

	copy_destructor = gso_skb->destructor == tcp_wfree;
	ooo_okay = gso_skb->ooo_okay;
	/* All segments but the first should have ooo_okay cleared */
	skb->ooo_okay = 0;

	segs = skb_segment(skb, features);
	if (IS_ERR(segs))
		goto out;

	/* Only first segment might have ooo_okay set */
	segs->ooo_okay = ooo_okay;

	delta = htonl(oldlen + (thlen + mss));

	skb = segs;
	th = tcp_hdr(skb);
	seq = ntohl(th->seq);

	newcheck = ~csum_fold((__force __wsum)((__force u32)th->check +
					       (__force u32)delta));

	do {
		th->fin = th->psh = 0;
		th->check = newcheck;

		if (skb->ip_summed != CHECKSUM_PARTIAL)
			th->check =
			     csum_fold(csum_partial(skb_transport_header(skb),
						    thlen, skb->csum));

		seq += mss;
		if (copy_destructor) {
			skb->destructor = gso_skb->destructor;
			skb->sk = gso_skb->sk;
			sum_truesize += skb->truesize;
		}
		skb = skb->next;
		th = tcp_hdr(skb);

		th->seq = htonl(seq);
		th->cwr = 0;
	} while (skb->next);

	/* Following permits TCP Small Queues to work well with GSO :
	 * The callback to TCP stack will be called at the time last frag
	 * is freed at TX completion, and not right now when gso_skb
	 * is freed by GSO engine
	 */
	if (copy_destructor) {
		swap(gso_skb->sk, skb->sk);
		swap(gso_skb->destructor, skb->destructor);
		sum_truesize += skb->truesize;
		atomic_add(sum_truesize - gso_skb->truesize,
			   &skb->sk->sk_wmem_alloc);
	}

	delta = htonl(oldlen + (skb_tail_pointer(skb) -
				skb_transport_header(skb)) +
		      skb->data_len);
	th->check = ~csum_fold((__force __wsum)((__force u32)th->check +
				(__force u32)delta));
	if (skb->ip_summed != CHECKSUM_PARTIAL)
		th->check = csum_fold(csum_partial(skb_transport_header(skb),
						   thlen, skb->csum));
out:
	return segs;
}
EXPORT_SYMBOL(tcp_gso_segment);

struct sk_buff **tcp_gro_receive(struct sk_buff **head, struct sk_buff *skb)
{
	struct sk_buff **pp = NULL;
	struct sk_buff *p;
	struct tcphdr *th;
	struct tcphdr *th2;
	unsigned int len;
	unsigned int thlen;
	__be32 flags;
	unsigned int mss = 1;
	unsigned int hlen;
	unsigned int off;
	int flush = 1;
	int i;

	off = skb_gro_offset(skb);
	hlen = off + sizeof(*th);
	th = skb_gro_header_fast(skb, off);
	if (skb_gro_header_hard(skb, hlen)) {
		th = skb_gro_header_slow(skb, hlen, off);
		if (unlikely(!th))
			goto out;
	}

	thlen = th->doff * 4;
	if (thlen < sizeof(*th))
		goto out;

	hlen = off + thlen;
	if (skb_gro_header_hard(skb, hlen)) {
		th = skb_gro_header_slow(skb, hlen, off);
		if (unlikely(!th))
			goto out;
	}

	skb_gro_pull(skb, thlen);

	len = skb_gro_len(skb);
	flags = tcp_flag_word(th);

	for (; (p = *head); head = &p->next) {
		if (!NAPI_GRO_CB(p)->same_flow)
			continue;

		th2 = tcp_hdr(p);

		if (*(u32 *)&th->source ^ *(u32 *)&th2->source) {
			NAPI_GRO_CB(p)->same_flow = 0;
			continue;
		}

		goto found;
	}

	goto out_check_final;

found:
	flush = NAPI_GRO_CB(p)->flush;
	flush |= (__force int)(flags & TCP_FLAG_CWR);
	flush |= (__force int)((flags ^ tcp_flag_word(th2)) &
		  ~(TCP_FLAG_CWR | TCP_FLAG_FIN | TCP_FLAG_PSH));
	flush |= (__force int)(th->ack_seq ^ th2->ack_seq);
	for (i = sizeof(*th); i < thlen; i += 4)
		flush |= *(u32 *)((u8 *)th + i) ^
			 *(u32 *)((u8 *)th2 + i);

	mss = tcp_skb_mss(p);

	flush |= (len - 1) >= mss;
	flush |= (ntohl(th2->seq) + skb_gro_len(p)) ^ ntohl(th->seq);

	if (flush || skb_gro_receive(head, skb)) {
		mss = 1;
		goto out_check_final;
	}

	p = *head;
	th2 = tcp_hdr(p);
	tcp_flag_word(th2) |= flags & (TCP_FLAG_FIN | TCP_FLAG_PSH);

out_check_final:
	flush = len < mss;
	flush |= (__force int)(flags & (TCP_FLAG_URG | TCP_FLAG_PSH |
					TCP_FLAG_RST | TCP_FLAG_SYN |
					TCP_FLAG_FIN));

	if (p && (!NAPI_GRO_CB(skb)->same_flow || flush))
		pp = head;

out:
	NAPI_GRO_CB(skb)->flush |= flush;

	return pp;
}
EXPORT_SYMBOL(tcp_gro_receive);

int tcp_gro_complete(struct sk_buff *skb)
{
	struct tcphdr *th = tcp_hdr(skb);

	skb->csum_start = skb_transport_header(skb) - skb->head;
	skb->csum_offset = offsetof(struct tcphdr, check);
	skb->ip_summed = CHECKSUM_PARTIAL;

	skb_shinfo(skb)->gso_segs = NAPI_GRO_CB(skb)->count;

	if (th->cwr)
		skb_shinfo(skb)->gso_type |= SKB_GSO_TCP_ECN;

	return 0;
}
EXPORT_SYMBOL(tcp_gro_complete);

static int tcp_v4_gso_send_check(struct sk_buff *skb)
{
	const struct iphdr *iph;
	struct tcphdr *th;

	if (!pskb_may_pull(skb, sizeof(*th)))
		return -EINVAL;

	iph = ip_hdr(skb);
	th = tcp_hdr(skb);

	th->check = 0;
	skb->ip_summed = CHECKSUM_PARTIAL;
	__tcp_v4_send_check(skb, iph->saddr, iph->daddr);
	return 0;
}

static struct sk_buff **tcp4_gro_receive(struct sk_buff **head, struct sk_buff *skb)
{
	const struct iphdr *iph = skb_gro_network_header(skb);
	__wsum wsum;

	/* Don't bother verifying checksum if we're going to flush anyway. */
	if (NAPI_GRO_CB(skb)->flush)
		goto skip_csum;

	wsum = skb->csum;

	switch (skb->ip_summed) {
	case CHECKSUM_NONE:
		wsum = skb_checksum(skb, skb_gro_offset(skb), skb_gro_len(skb),
				    0);

		/* fall through */

	case CHECKSUM_COMPLETE:
		if (!tcp_v4_check(skb_gro_len(skb), iph->saddr, iph->daddr,
				  wsum)) {
			skb->ip_summed = CHECKSUM_UNNECESSARY;
			break;
		}

		NAPI_GRO_CB(skb)->flush = 1;
		return NULL;
	}

skip_csum:
	return tcp_gro_receive(head, skb);
}

static int tcp4_gro_complete(struct sk_buff *skb)
{
	const struct iphdr *iph = ip_hdr(skb);
	struct tcphdr *th = tcp_hdr(skb);

	th->check = ~tcp_v4_check(skb->len - skb_transport_offset(skb),
				  iph->saddr, iph->daddr, 0);
	skb_shinfo(skb)->gso_type = SKB_GSO_TCPV4;

	return tcp_gro_complete(skb);
}

static const struct net_offload tcpv4_offload = {
	.callbacks = {
		.gso_send_check	=	tcp_v4_gso_send_check,
		.gso_segment	=	tcp_gso_segment,
		.gro_receive	=	tcp4_gro_receive,
		.gro_complete	=	tcp4_gro_complete,
	},
};

int __init tcpv4_offload_init(void)
{
	return inet_add_offload(&tcpv4_offload, IPPROTO_TCP);
}
