// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	IPV4 GSO/GRO offload support
 *	Linux INET implementation
 *
 *	TCPv4 GSO/GRO support
 */

#include <linux/indirect_call_wrapper.h>
#include <linux/skbuff.h>
#include <net/gro.h>
#include <net/gso.h>
#include <net/tcp.h>
#include <net/protocol.h>

static void tcp_gso_tstamp(struct sk_buff *skb, struct sk_buff *gso_skb,
			   unsigned int seq, unsigned int mss)
{
	u32 flags = skb_shinfo(gso_skb)->tx_flags & SKBTX_ANY_TSTAMP;
	u32 ts_seq = skb_shinfo(gso_skb)->tskey;

	while (skb) {
		if (before(ts_seq, seq + mss)) {
			skb_shinfo(skb)->tx_flags |= flags;
			skb_shinfo(skb)->tskey = ts_seq;
			return;
		}

		skb = skb->next;
		seq += mss;
	}
}

static void __tcpv4_gso_segment_csum(struct sk_buff *seg,
				     __be32 *oldip, __be32 newip,
				     __be16 *oldport, __be16 newport)
{
	struct tcphdr *th;
	struct iphdr *iph;

	if (*oldip == newip && *oldport == newport)
		return;

	th = tcp_hdr(seg);
	iph = ip_hdr(seg);

	inet_proto_csum_replace4(&th->check, seg, *oldip, newip, true);
	inet_proto_csum_replace2(&th->check, seg, *oldport, newport, false);
	*oldport = newport;

	csum_replace4(&iph->check, *oldip, newip);
	*oldip = newip;
}

static struct sk_buff *__tcpv4_gso_segment_list_csum(struct sk_buff *segs)
{
	const struct tcphdr *th;
	const struct iphdr *iph;
	struct sk_buff *seg;
	struct tcphdr *th2;
	struct iphdr *iph2;

	seg = segs;
	th = tcp_hdr(seg);
	iph = ip_hdr(seg);
	th2 = tcp_hdr(seg->next);
	iph2 = ip_hdr(seg->next);

	if (!(*(const u32 *)&th->source ^ *(const u32 *)&th2->source) &&
	    iph->daddr == iph2->daddr && iph->saddr == iph2->saddr)
		return segs;

	while ((seg = seg->next)) {
		th2 = tcp_hdr(seg);
		iph2 = ip_hdr(seg);

		__tcpv4_gso_segment_csum(seg,
					 &iph2->saddr, iph->saddr,
					 &th2->source, th->source);
		__tcpv4_gso_segment_csum(seg,
					 &iph2->daddr, iph->daddr,
					 &th2->dest, th->dest);
	}

	return segs;
}

static struct sk_buff *__tcp4_gso_segment_list(struct sk_buff *skb,
					      netdev_features_t features)
{
	skb = skb_segment_list(skb, features, skb_mac_header_len(skb));
	if (IS_ERR(skb))
		return skb;

	return __tcpv4_gso_segment_list_csum(skb);
}

static struct sk_buff *tcp4_gso_segment(struct sk_buff *skb,
					netdev_features_t features)
{
	if (!(skb_shinfo(skb)->gso_type & SKB_GSO_TCPV4))
		return ERR_PTR(-EINVAL);

	if (!pskb_may_pull(skb, sizeof(struct tcphdr)))
		return ERR_PTR(-EINVAL);

	if (skb_shinfo(skb)->gso_type & SKB_GSO_FRAGLIST) {
		struct tcphdr *th = tcp_hdr(skb);

		if (skb_pagelen(skb) - th->doff * 4 == skb_shinfo(skb)->gso_size)
			return __tcp4_gso_segment_list(skb, features);

		skb->ip_summed = CHECKSUM_NONE;
	}

	if (unlikely(skb->ip_summed != CHECKSUM_PARTIAL)) {
		const struct iphdr *iph = ip_hdr(skb);
		struct tcphdr *th = tcp_hdr(skb);

		/* Set up checksum pseudo header, usually expect stack to
		 * have done this already.
		 */

		th->check = 0;
		skb->ip_summed = CHECKSUM_PARTIAL;
		__tcp_v4_send_check(skb, iph->saddr, iph->daddr);
	}

	return tcp_gso_segment(skb, features);
}

struct sk_buff *tcp_gso_segment(struct sk_buff *skb,
				netdev_features_t features)
{
	struct sk_buff *segs = ERR_PTR(-EINVAL);
	unsigned int sum_truesize = 0;
	struct tcphdr *th;
	unsigned int thlen;
	unsigned int seq;
	unsigned int oldlen;
	unsigned int mss;
	struct sk_buff *gso_skb = skb;
	__sum16 newcheck;
	bool ooo_okay, copy_destructor;
	bool ecn_cwr_mask;
	__wsum delta;

	th = tcp_hdr(skb);
	thlen = th->doff * 4;
	if (thlen < sizeof(*th))
		goto out;

	if (unlikely(skb_checksum_start(skb) != skb_transport_header(skb)))
		goto out;

	if (!pskb_may_pull(skb, thlen))
		goto out;

	oldlen = ~skb->len;
	__skb_pull(skb, thlen);

	mss = skb_shinfo(skb)->gso_size;
	if (unlikely(skb->len <= mss))
		goto out;

	if (skb_gso_ok(skb, features | NETIF_F_GSO_ROBUST)) {
		/* Packet is from an untrusted source, reset gso_segs. */

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

	/* GSO partial and frag_list segmentation only requires splitting
	 * the frame into an MSS multiple and possibly a remainder, both
	 * cases return a GSO skb. So update the mss now.
	 */
	if (skb_is_gso(segs))
		mss *= skb_shinfo(segs)->gso_segs;

	delta = (__force __wsum)htonl(oldlen + thlen + mss);

	skb = segs;
	th = tcp_hdr(skb);
	seq = ntohl(th->seq);

	if (unlikely(skb_shinfo(gso_skb)->tx_flags & SKBTX_ANY_TSTAMP))
		tcp_gso_tstamp(segs, gso_skb, seq, mss);

	newcheck = ~csum_fold(csum_add(csum_unfold(th->check), delta));

	ecn_cwr_mask = !!(skb_shinfo(gso_skb)->gso_type & SKB_GSO_TCP_ACCECN);

	while (skb->next) {
		th->fin = th->psh = 0;
		th->check = newcheck;

		if (skb->ip_summed == CHECKSUM_PARTIAL)
			gso_reset_checksum(skb, ~th->check);
		else
			th->check = gso_make_checksum(skb, ~th->check);

		seq += mss;
		if (copy_destructor) {
			skb->destructor = gso_skb->destructor;
			skb->sk = gso_skb->sk;
			sum_truesize += skb->truesize;
		}
		skb = skb->next;
		th = tcp_hdr(skb);

		th->seq = htonl(seq);

		th->cwr &= ecn_cwr_mask;
	}

	/* Following permits TCP Small Queues to work well with GSO :
	 * The callback to TCP stack will be called at the time last frag
	 * is freed at TX completion, and not right now when gso_skb
	 * is freed by GSO engine
	 */
	if (copy_destructor) {
		int delta;

		swap(gso_skb->sk, skb->sk);
		swap(gso_skb->destructor, skb->destructor);
		sum_truesize += skb->truesize;
		delta = sum_truesize - gso_skb->truesize;
		/* In some pathological cases, delta can be negative.
		 * We need to either use refcount_add() or refcount_sub_and_test()
		 */
		if (likely(delta >= 0))
			refcount_add(delta, &skb->sk->sk_wmem_alloc);
		else
			WARN_ON_ONCE(refcount_sub_and_test(-delta, &skb->sk->sk_wmem_alloc));
	}

	delta = (__force __wsum)htonl(oldlen +
				      (skb_tail_pointer(skb) -
				       skb_transport_header(skb)) +
				      skb->data_len);
	th->check = ~csum_fold(csum_add(csum_unfold(th->check), delta));
	if (skb->ip_summed == CHECKSUM_PARTIAL)
		gso_reset_checksum(skb, ~th->check);
	else
		th->check = gso_make_checksum(skb, ~th->check);
out:
	return segs;
}

struct sk_buff *tcp_gro_lookup(struct list_head *head, struct tcphdr *th)
{
	struct tcphdr *th2;
	struct sk_buff *p;

	list_for_each_entry(p, head, list) {
		if (!NAPI_GRO_CB(p)->same_flow)
			continue;

		th2 = tcp_hdr(p);
		if (*(u32 *)&th->source ^ *(u32 *)&th2->source) {
			NAPI_GRO_CB(p)->same_flow = 0;
			continue;
		}

		return p;
	}

	return NULL;
}

struct tcphdr *tcp_gro_pull_header(struct sk_buff *skb)
{
	unsigned int thlen, hlen, off;
	struct tcphdr *th;

	off = skb_gro_offset(skb);
	hlen = off + sizeof(*th);
	th = skb_gro_header(skb, hlen, off);
	if (unlikely(!th))
		return NULL;

	thlen = th->doff * 4;
	if (thlen < sizeof(*th))
		return NULL;

	hlen = off + thlen;
	if (!skb_gro_may_pull(skb, hlen)) {
		th = skb_gro_header_slow(skb, hlen, off);
		if (unlikely(!th))
			return NULL;
	}

	skb_gro_pull(skb, thlen);

	return th;
}

struct sk_buff *tcp_gro_receive(struct list_head *head, struct sk_buff *skb,
				struct tcphdr *th)
{
	unsigned int thlen = th->doff * 4;
	struct sk_buff *pp = NULL;
	struct sk_buff *p;
	struct tcphdr *th2;
	unsigned int len;
	__be32 flags;
	unsigned int mss = 1;
	int flush = 1;
	int i;

	len = skb_gro_len(skb);
	flags = tcp_flag_word(th);

	p = tcp_gro_lookup(head, th);
	if (!p)
		goto out_check_final;

	th2 = tcp_hdr(p);
	flush = (__force int)(flags & TCP_FLAG_CWR);
	flush |= (__force int)((flags ^ tcp_flag_word(th2)) &
		  ~(TCP_FLAG_FIN | TCP_FLAG_PSH));
	flush |= (__force int)(th->ack_seq ^ th2->ack_seq);
	for (i = sizeof(*th); i < thlen; i += 4)
		flush |= *(u32 *)((u8 *)th + i) ^
			 *(u32 *)((u8 *)th2 + i);

	flush |= gro_receive_network_flush(th, th2, p);

	mss = skb_shinfo(p)->gso_size;

	/* If skb is a GRO packet, make sure its gso_size matches prior packet mss.
	 * If it is a single frame, do not aggregate it if its length
	 * is bigger than our mss.
	 */
	if (unlikely(skb_is_gso(skb)))
		flush |= (mss != skb_shinfo(skb)->gso_size);
	else
		flush |= (len - 1) >= mss;

	flush |= (ntohl(th2->seq) + skb_gro_len(p)) ^ ntohl(th->seq);
	flush |= skb_cmp_decrypted(p, skb);

	if (unlikely(NAPI_GRO_CB(p)->is_flist)) {
		flush |= (__force int)(flags ^ tcp_flag_word(th2));
		flush |= skb->ip_summed != p->ip_summed;
		flush |= skb->csum_level != p->csum_level;
		flush |= NAPI_GRO_CB(p)->count >= 64;
		skb_set_network_header(skb, skb_gro_receive_network_offset(skb));

		if (flush || skb_gro_receive_list(p, skb))
			mss = 1;

		goto out_check_final;
	}

	if (flush || skb_gro_receive(p, skb)) {
		mss = 1;
		goto out_check_final;
	}

	tcp_flag_word(th2) |= flags & (TCP_FLAG_FIN | TCP_FLAG_PSH);

out_check_final:
	/* Force a flush if last segment is smaller than mss. */
	if (unlikely(skb_is_gso(skb)))
		flush = len != NAPI_GRO_CB(skb)->count * skb_shinfo(skb)->gso_size;
	else
		flush = len < mss;

	flush |= (__force int)(flags & (TCP_FLAG_URG | TCP_FLAG_PSH |
					TCP_FLAG_RST | TCP_FLAG_SYN |
					TCP_FLAG_FIN));

	if (p && (!NAPI_GRO_CB(skb)->same_flow || flush))
		pp = p;

	NAPI_GRO_CB(skb)->flush |= (flush != 0);

	return pp;
}

void tcp_gro_complete(struct sk_buff *skb)
{
	struct tcphdr *th = tcp_hdr(skb);
	struct skb_shared_info *shinfo;

	if (skb->encapsulation)
		skb->inner_transport_header = skb->transport_header;

	skb->csum_start = (unsigned char *)th - skb->head;
	skb->csum_offset = offsetof(struct tcphdr, check);
	skb->ip_summed = CHECKSUM_PARTIAL;

	shinfo = skb_shinfo(skb);
	shinfo->gso_segs = NAPI_GRO_CB(skb)->count;

	if (th->cwr)
		shinfo->gso_type |= SKB_GSO_TCP_ACCECN;
}
EXPORT_SYMBOL(tcp_gro_complete);

static void tcp4_check_fraglist_gro(struct list_head *head, struct sk_buff *skb,
				    struct tcphdr *th)
{
	const struct iphdr *iph;
	struct sk_buff *p;
	struct sock *sk;
	struct net *net;
	int iif, sdif;

	if (likely(!(skb->dev->features & NETIF_F_GRO_FRAGLIST)))
		return;

	p = tcp_gro_lookup(head, th);
	if (p) {
		NAPI_GRO_CB(skb)->is_flist = NAPI_GRO_CB(p)->is_flist;
		return;
	}

	inet_get_iif_sdif(skb, &iif, &sdif);
	iph = skb_gro_network_header(skb);
	net = dev_net_rcu(skb->dev);
	sk = __inet_lookup_established(net, net->ipv4.tcp_death_row.hashinfo,
				       iph->saddr, th->source,
				       iph->daddr, ntohs(th->dest),
				       iif, sdif);
	NAPI_GRO_CB(skb)->is_flist = !sk;
	if (sk)
		sock_gen_put(sk);
}

INDIRECT_CALLABLE_SCOPE
struct sk_buff *tcp4_gro_receive(struct list_head *head, struct sk_buff *skb)
{
	struct tcphdr *th;

	/* Don't bother verifying checksum if we're going to flush anyway. */
	if (!NAPI_GRO_CB(skb)->flush &&
	    skb_gro_checksum_validate(skb, IPPROTO_TCP,
				      inet_gro_compute_pseudo))
		goto flush;

	th = tcp_gro_pull_header(skb);
	if (!th)
		goto flush;

	tcp4_check_fraglist_gro(head, skb, th);

	return tcp_gro_receive(head, skb, th);

flush:
	NAPI_GRO_CB(skb)->flush = 1;
	return NULL;
}

INDIRECT_CALLABLE_SCOPE int tcp4_gro_complete(struct sk_buff *skb, int thoff)
{
	const u16 offset = NAPI_GRO_CB(skb)->network_offsets[skb->encapsulation];
	const struct iphdr *iph = (struct iphdr *)(skb->data + offset);
	struct tcphdr *th = tcp_hdr(skb);

	if (unlikely(NAPI_GRO_CB(skb)->is_flist)) {
		skb_shinfo(skb)->gso_type |= SKB_GSO_FRAGLIST | SKB_GSO_TCPV4;
		skb_shinfo(skb)->gso_segs = NAPI_GRO_CB(skb)->count;

		__skb_incr_checksum_unnecessary(skb);

		return 0;
	}

	th->check = ~tcp_v4_check(skb->len - thoff, iph->saddr,
				  iph->daddr, 0);

	skb_shinfo(skb)->gso_type |= SKB_GSO_TCPV4 |
			(NAPI_GRO_CB(skb)->ip_fixedid * SKB_GSO_TCP_FIXEDID);

	tcp_gro_complete(skb);
	return 0;
}

int __init tcpv4_offload_init(void)
{
	net_hotdata.tcpv4_offload = (struct net_offload) {
		.callbacks = {
			.gso_segment	=	tcp4_gso_segment,
			.gro_receive	=	tcp4_gro_receive,
			.gro_complete	=	tcp4_gro_complete,
		},
	};
	return inet_add_offload(&net_hotdata.tcpv4_offload, IPPROTO_TCP);
}
