/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Implementation of the Transmission Control Protocol(TCP).
 *
 * Version:	$Id: tcp_output.c,v 1.146 2002/02/01 22:01:04 davem Exp $
 *
 * Authors:	Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Mark Evans, <evansmp@uhura.aston.ac.uk>
 *		Corey Minyard <wf-rch!minyard@relay.EU.net>
 *		Florian La Roche, <flla@stud.uni-sb.de>
 *		Charles Hedrick, <hedrick@klinzhai.rutgers.edu>
 *		Linus Torvalds, <torvalds@cs.helsinki.fi>
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *		Matthew Dillon, <dillon@apollo.west.oic.com>
 *		Arnt Gulbrandsen, <agulbra@nvg.unit.no>
 *		Jorge Cwik, <jorge@laser.satlink.net>
 */

/*
 * Changes:	Pedro Roque	:	Retransmit queue handled by TCP.
 *				:	Fragmentation on mtu decrease
 *				:	Segment collapse on retransmit
 *				:	AF independence
 *
 *		Linus Torvalds	:	send_delayed_ack
 *		David S. Miller	:	Charge memory using the right skb
 *					during syn/ack processing.
 *		David S. Miller :	Output engine completely rewritten.
 *		Andrea Arcangeli:	SYNACK carry ts_recent in tsecr.
 *		Cacophonix Gaul :	draft-minshall-nagle-01
 *		J Hadi Salim	:	ECN support
 *
 */

#include <net/tcp.h>

#include <linux/compiler.h>
#include <linux/module.h>
#include <linux/smp_lock.h>

/* People can turn this off for buggy TCP's found in printers etc. */
int sysctl_tcp_retrans_collapse = 1;

/* This limits the percentage of the congestion window which we
 * will allow a single TSO frame to consume.  Building TSO frames
 * which are too large can cause TCP streams to be bursty.
 */
int sysctl_tcp_tso_win_divisor = 3;

static void update_send_head(struct sock *sk, struct tcp_sock *tp,
			     struct sk_buff *skb)
{
	sk->sk_send_head = skb->next;
	if (sk->sk_send_head == (struct sk_buff *)&sk->sk_write_queue)
		sk->sk_send_head = NULL;
	tp->snd_nxt = TCP_SKB_CB(skb)->end_seq;
	tcp_packets_out_inc(sk, tp, skb);
}

/* SND.NXT, if window was not shrunk.
 * If window has been shrunk, what should we make? It is not clear at all.
 * Using SND.UNA we will fail to open window, SND.NXT is out of window. :-(
 * Anything in between SND.UNA...SND.UNA+SND.WND also can be already
 * invalid. OK, let's make this for now:
 */
static inline __u32 tcp_acceptable_seq(struct sock *sk, struct tcp_sock *tp)
{
	if (!before(tp->snd_una+tp->snd_wnd, tp->snd_nxt))
		return tp->snd_nxt;
	else
		return tp->snd_una+tp->snd_wnd;
}

/* Calculate mss to advertise in SYN segment.
 * RFC1122, RFC1063, draft-ietf-tcpimpl-pmtud-01 state that:
 *
 * 1. It is independent of path mtu.
 * 2. Ideally, it is maximal possible segment size i.e. 65535-40.
 * 3. For IPv4 it is reasonable to calculate it from maximal MTU of
 *    attached devices, because some buggy hosts are confused by
 *    large MSS.
 * 4. We do not make 3, we advertise MSS, calculated from first
 *    hop device mtu, but allow to raise it to ip_rt_min_advmss.
 *    This may be overridden via information stored in routing table.
 * 5. Value 65535 for MSS is valid in IPv6 and means "as large as possible,
 *    probably even Jumbo".
 */
static __u16 tcp_advertise_mss(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct dst_entry *dst = __sk_dst_get(sk);
	int mss = tp->advmss;

	if (dst && dst_metric(dst, RTAX_ADVMSS) < mss) {
		mss = dst_metric(dst, RTAX_ADVMSS);
		tp->advmss = mss;
	}

	return (__u16)mss;
}

/* RFC2861. Reset CWND after idle period longer RTO to "restart window".
 * This is the first part of cwnd validation mechanism. */
static void tcp_cwnd_restart(struct sock *sk, struct dst_entry *dst)
{
	struct tcp_sock *tp = tcp_sk(sk);
	s32 delta = tcp_time_stamp - tp->lsndtime;
	u32 restart_cwnd = tcp_init_cwnd(tp, dst);
	u32 cwnd = tp->snd_cwnd;

	tcp_ca_event(sk, CA_EVENT_CWND_RESTART);

	tp->snd_ssthresh = tcp_current_ssthresh(sk);
	restart_cwnd = min(restart_cwnd, cwnd);

	while ((delta -= inet_csk(sk)->icsk_rto) > 0 && cwnd > restart_cwnd)
		cwnd >>= 1;
	tp->snd_cwnd = max(cwnd, restart_cwnd);
	tp->snd_cwnd_stamp = tcp_time_stamp;
	tp->snd_cwnd_used = 0;
}

static void tcp_event_data_sent(struct tcp_sock *tp,
				struct sk_buff *skb, struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	const u32 now = tcp_time_stamp;

	if (!tp->packets_out && (s32)(now - tp->lsndtime) > icsk->icsk_rto)
		tcp_cwnd_restart(sk, __sk_dst_get(sk));

	tp->lsndtime = now;

	/* If it is a reply for ato after last received
	 * packet, enter pingpong mode.
	 */
	if ((u32)(now - icsk->icsk_ack.lrcvtime) < icsk->icsk_ack.ato)
		icsk->icsk_ack.pingpong = 1;
}

static inline void tcp_event_ack_sent(struct sock *sk, unsigned int pkts)
{
	tcp_dec_quickack_mode(sk, pkts);
	inet_csk_clear_xmit_timer(sk, ICSK_TIME_DACK);
}

/* Determine a window scaling and initial window to offer.
 * Based on the assumption that the given amount of space
 * will be offered. Store the results in the tp structure.
 * NOTE: for smooth operation initial space offering should
 * be a multiple of mss if possible. We assume here that mss >= 1.
 * This MUST be enforced by all callers.
 */
void tcp_select_initial_window(int __space, __u32 mss,
			       __u32 *rcv_wnd, __u32 *window_clamp,
			       int wscale_ok, __u8 *rcv_wscale)
{
	unsigned int space = (__space < 0 ? 0 : __space);

	/* If no clamp set the clamp to the max possible scaled window */
	if (*window_clamp == 0)
		(*window_clamp) = (65535 << 14);
	space = min(*window_clamp, space);

	/* Quantize space offering to a multiple of mss if possible. */
	if (space > mss)
		space = (space / mss) * mss;

	/* NOTE: offering an initial window larger than 32767
	 * will break some buggy TCP stacks. We try to be nice.
	 * If we are not window scaling, then this truncates
	 * our initial window offering to 32k. There should also
	 * be a sysctl option to stop being nice.
	 */
	(*rcv_wnd) = min(space, MAX_TCP_WINDOW);
	(*rcv_wscale) = 0;
	if (wscale_ok) {
		/* Set window scaling on max possible window
		 * See RFC1323 for an explanation of the limit to 14 
		 */
		space = max_t(u32, sysctl_tcp_rmem[2], sysctl_rmem_max);
		while (space > 65535 && (*rcv_wscale) < 14) {
			space >>= 1;
			(*rcv_wscale)++;
		}
	}

	/* Set initial window to value enough for senders,
	 * following RFC2414. Senders, not following this RFC,
	 * will be satisfied with 2.
	 */
	if (mss > (1<<*rcv_wscale)) {
		int init_cwnd = 4;
		if (mss > 1460*3)
			init_cwnd = 2;
		else if (mss > 1460)
			init_cwnd = 3;
		if (*rcv_wnd > init_cwnd*mss)
			*rcv_wnd = init_cwnd*mss;
	}

	/* Set the clamp no higher than max representable value */
	(*window_clamp) = min(65535U << (*rcv_wscale), *window_clamp);
}

/* Chose a new window to advertise, update state in tcp_sock for the
 * socket, and return result with RFC1323 scaling applied.  The return
 * value can be stuffed directly into th->window for an outgoing
 * frame.
 */
static u16 tcp_select_window(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	u32 cur_win = tcp_receive_window(tp);
	u32 new_win = __tcp_select_window(sk);

	/* Never shrink the offered window */
	if(new_win < cur_win) {
		/* Danger Will Robinson!
		 * Don't update rcv_wup/rcv_wnd here or else
		 * we will not be able to advertise a zero
		 * window in time.  --DaveM
		 *
		 * Relax Will Robinson.
		 */
		new_win = cur_win;
	}
	tp->rcv_wnd = new_win;
	tp->rcv_wup = tp->rcv_nxt;

	/* Make sure we do not exceed the maximum possible
	 * scaled window.
	 */
	if (!tp->rx_opt.rcv_wscale)
		new_win = min(new_win, MAX_TCP_WINDOW);
	else
		new_win = min(new_win, (65535U << tp->rx_opt.rcv_wscale));

	/* RFC1323 scaling applied */
	new_win >>= tp->rx_opt.rcv_wscale;

	/* If we advertise zero window, disable fast path. */
	if (new_win == 0)
		tp->pred_flags = 0;

	return new_win;
}

static void tcp_build_and_update_options(__u32 *ptr, struct tcp_sock *tp,
					 __u32 tstamp)
{
	if (tp->rx_opt.tstamp_ok) {
		*ptr++ = __constant_htonl((TCPOPT_NOP << 24) |
					  (TCPOPT_NOP << 16) |
					  (TCPOPT_TIMESTAMP << 8) |
					  TCPOLEN_TIMESTAMP);
		*ptr++ = htonl(tstamp);
		*ptr++ = htonl(tp->rx_opt.ts_recent);
	}
	if (tp->rx_opt.eff_sacks) {
		struct tcp_sack_block *sp = tp->rx_opt.dsack ? tp->duplicate_sack : tp->selective_acks;
		int this_sack;

		*ptr++ = htonl((TCPOPT_NOP  << 24) |
			       (TCPOPT_NOP  << 16) |
			       (TCPOPT_SACK <<  8) |
			       (TCPOLEN_SACK_BASE + (tp->rx_opt.eff_sacks *
						     TCPOLEN_SACK_PERBLOCK)));
		for(this_sack = 0; this_sack < tp->rx_opt.eff_sacks; this_sack++) {
			*ptr++ = htonl(sp[this_sack].start_seq);
			*ptr++ = htonl(sp[this_sack].end_seq);
		}
		if (tp->rx_opt.dsack) {
			tp->rx_opt.dsack = 0;
			tp->rx_opt.eff_sacks--;
		}
	}
}

/* Construct a tcp options header for a SYN or SYN_ACK packet.
 * If this is every changed make sure to change the definition of
 * MAX_SYN_SIZE to match the new maximum number of options that you
 * can generate.
 */
static void tcp_syn_build_options(__u32 *ptr, int mss, int ts, int sack,
				  int offer_wscale, int wscale, __u32 tstamp,
				  __u32 ts_recent)
{
	/* We always get an MSS option.
	 * The option bytes which will be seen in normal data
	 * packets should timestamps be used, must be in the MSS
	 * advertised.  But we subtract them from tp->mss_cache so
	 * that calculations in tcp_sendmsg are simpler etc.
	 * So account for this fact here if necessary.  If we
	 * don't do this correctly, as a receiver we won't
	 * recognize data packets as being full sized when we
	 * should, and thus we won't abide by the delayed ACK
	 * rules correctly.
	 * SACKs don't matter, we never delay an ACK when we
	 * have any of those going out.
	 */
	*ptr++ = htonl((TCPOPT_MSS << 24) | (TCPOLEN_MSS << 16) | mss);
	if (ts) {
		if(sack)
			*ptr++ = __constant_htonl((TCPOPT_SACK_PERM << 24) | (TCPOLEN_SACK_PERM << 16) |
						  (TCPOPT_TIMESTAMP << 8) | TCPOLEN_TIMESTAMP);
		else
			*ptr++ = __constant_htonl((TCPOPT_NOP << 24) | (TCPOPT_NOP << 16) |
						  (TCPOPT_TIMESTAMP << 8) | TCPOLEN_TIMESTAMP);
		*ptr++ = htonl(tstamp);		/* TSVAL */
		*ptr++ = htonl(ts_recent);	/* TSECR */
	} else if(sack)
		*ptr++ = __constant_htonl((TCPOPT_NOP << 24) | (TCPOPT_NOP << 16) |
					  (TCPOPT_SACK_PERM << 8) | TCPOLEN_SACK_PERM);
	if (offer_wscale)
		*ptr++ = htonl((TCPOPT_NOP << 24) | (TCPOPT_WINDOW << 16) | (TCPOLEN_WINDOW << 8) | (wscale));
}

/* This routine actually transmits TCP packets queued in by
 * tcp_do_sendmsg().  This is used by both the initial
 * transmission and possible later retransmissions.
 * All SKB's seen here are completely headerless.  It is our
 * job to build the TCP header, and pass the packet down to
 * IP so it can do the same plus pass the packet off to the
 * device.
 *
 * We are working here with either a clone of the original
 * SKB, or a fresh unique copy made by the retransmit engine.
 */
static int tcp_transmit_skb(struct sock *sk, struct sk_buff *skb, int clone_it, gfp_t gfp_mask)
{
	const struct inet_connection_sock *icsk = inet_csk(sk);
	struct inet_sock *inet;
	struct tcp_sock *tp;
	struct tcp_skb_cb *tcb;
	int tcp_header_size;
	struct tcphdr *th;
	int sysctl_flags;
	int err;

	BUG_ON(!skb || !tcp_skb_pcount(skb));

	/* If congestion control is doing timestamping, we must
	 * take such a timestamp before we potentially clone/copy.
	 */
	if (icsk->icsk_ca_ops->rtt_sample)
		__net_timestamp(skb);

	if (likely(clone_it)) {
		if (unlikely(skb_cloned(skb)))
			skb = pskb_copy(skb, gfp_mask);
		else
			skb = skb_clone(skb, gfp_mask);
		if (unlikely(!skb))
			return -ENOBUFS;
	}

	inet = inet_sk(sk);
	tp = tcp_sk(sk);
	tcb = TCP_SKB_CB(skb);
	tcp_header_size = tp->tcp_header_len;

#define SYSCTL_FLAG_TSTAMPS	0x1
#define SYSCTL_FLAG_WSCALE	0x2
#define SYSCTL_FLAG_SACK	0x4

	sysctl_flags = 0;
	if (unlikely(tcb->flags & TCPCB_FLAG_SYN)) {
		tcp_header_size = sizeof(struct tcphdr) + TCPOLEN_MSS;
		if(sysctl_tcp_timestamps) {
			tcp_header_size += TCPOLEN_TSTAMP_ALIGNED;
			sysctl_flags |= SYSCTL_FLAG_TSTAMPS;
		}
		if (sysctl_tcp_window_scaling) {
			tcp_header_size += TCPOLEN_WSCALE_ALIGNED;
			sysctl_flags |= SYSCTL_FLAG_WSCALE;
		}
		if (sysctl_tcp_sack) {
			sysctl_flags |= SYSCTL_FLAG_SACK;
			if (!(sysctl_flags & SYSCTL_FLAG_TSTAMPS))
				tcp_header_size += TCPOLEN_SACKPERM_ALIGNED;
		}
	} else if (unlikely(tp->rx_opt.eff_sacks)) {
		/* A SACK is 2 pad bytes, a 2 byte header, plus
		 * 2 32-bit sequence numbers for each SACK block.
		 */
		tcp_header_size += (TCPOLEN_SACK_BASE_ALIGNED +
				    (tp->rx_opt.eff_sacks *
				     TCPOLEN_SACK_PERBLOCK));
	}
		
	if (tcp_packets_in_flight(tp) == 0)
		tcp_ca_event(sk, CA_EVENT_TX_START);

	th = (struct tcphdr *) skb_push(skb, tcp_header_size);
	skb->h.th = th;
	skb_set_owner_w(skb, sk);

	/* Build TCP header and checksum it. */
	th->source		= inet->sport;
	th->dest		= inet->dport;
	th->seq			= htonl(tcb->seq);
	th->ack_seq		= htonl(tp->rcv_nxt);
	*(((__u16 *)th) + 6)	= htons(((tcp_header_size >> 2) << 12) |
					tcb->flags);

	if (unlikely(tcb->flags & TCPCB_FLAG_SYN)) {
		/* RFC1323: The window in SYN & SYN/ACK segments
		 * is never scaled.
		 */
		th->window	= htons(tp->rcv_wnd);
	} else {
		th->window	= htons(tcp_select_window(sk));
	}
	th->check		= 0;
	th->urg_ptr		= 0;

	if (unlikely(tp->urg_mode &&
		     between(tp->snd_up, tcb->seq+1, tcb->seq+0xFFFF))) {
		th->urg_ptr		= htons(tp->snd_up-tcb->seq);
		th->urg			= 1;
	}

	if (unlikely(tcb->flags & TCPCB_FLAG_SYN)) {
		tcp_syn_build_options((__u32 *)(th + 1),
				      tcp_advertise_mss(sk),
				      (sysctl_flags & SYSCTL_FLAG_TSTAMPS),
				      (sysctl_flags & SYSCTL_FLAG_SACK),
				      (sysctl_flags & SYSCTL_FLAG_WSCALE),
				      tp->rx_opt.rcv_wscale,
				      tcb->when,
				      tp->rx_opt.ts_recent);
	} else {
		tcp_build_and_update_options((__u32 *)(th + 1),
					     tp, tcb->when);
		TCP_ECN_send(sk, tp, skb, tcp_header_size);
	}

	icsk->icsk_af_ops->send_check(sk, skb->len, skb);

	if (likely(tcb->flags & TCPCB_FLAG_ACK))
		tcp_event_ack_sent(sk, tcp_skb_pcount(skb));

	if (skb->len != tcp_header_size)
		tcp_event_data_sent(tp, skb, sk);

	TCP_INC_STATS(TCP_MIB_OUTSEGS);

	err = icsk->icsk_af_ops->queue_xmit(skb, 0);
	if (unlikely(err <= 0))
		return err;

	tcp_enter_cwr(sk);

	/* NET_XMIT_CN is special. It does not guarantee,
	 * that this packet is lost. It tells that device
	 * is about to start to drop packets or already
	 * drops some packets of the same priority and
	 * invokes us to send less aggressively.
	 */
	return err == NET_XMIT_CN ? 0 : err;

#undef SYSCTL_FLAG_TSTAMPS
#undef SYSCTL_FLAG_WSCALE
#undef SYSCTL_FLAG_SACK
}


/* This routine just queue's the buffer 
 *
 * NOTE: probe0 timer is not checked, do not forget tcp_push_pending_frames,
 * otherwise socket can stall.
 */
static void tcp_queue_skb(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_sock *tp = tcp_sk(sk);

	/* Advance write_seq and place onto the write_queue. */
	tp->write_seq = TCP_SKB_CB(skb)->end_seq;
	skb_header_release(skb);
	__skb_queue_tail(&sk->sk_write_queue, skb);
	sk_charge_skb(sk, skb);

	/* Queue it, remembering where we must start sending. */
	if (sk->sk_send_head == NULL)
		sk->sk_send_head = skb;
}

static void tcp_set_skb_tso_segs(struct sock *sk, struct sk_buff *skb, unsigned int mss_now)
{
	if (skb->len <= mss_now ||
	    !(sk->sk_route_caps & NETIF_F_TSO)) {
		/* Avoid the costly divide in the normal
		 * non-TSO case.
		 */
		skb_shinfo(skb)->tso_segs = 1;
		skb_shinfo(skb)->tso_size = 0;
	} else {
		unsigned int factor;

		factor = skb->len + (mss_now - 1);
		factor /= mss_now;
		skb_shinfo(skb)->tso_segs = factor;
		skb_shinfo(skb)->tso_size = mss_now;
	}
}

/* Function to create two new TCP segments.  Shrinks the given segment
 * to the specified size and appends a new segment with the rest of the
 * packet to the list.  This won't be called frequently, I hope. 
 * Remember, these are still headerless SKBs at this point.
 */
int tcp_fragment(struct sock *sk, struct sk_buff *skb, u32 len, unsigned int mss_now)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct sk_buff *buff;
	int nsize, old_factor;
	u16 flags;

	BUG_ON(len > skb->len);

 	clear_all_retrans_hints(tp);
	nsize = skb_headlen(skb) - len;
	if (nsize < 0)
		nsize = 0;

	if (skb_cloned(skb) &&
	    skb_is_nonlinear(skb) &&
	    pskb_expand_head(skb, 0, 0, GFP_ATOMIC))
		return -ENOMEM;

	/* Get a new skb... force flag on. */
	buff = sk_stream_alloc_skb(sk, nsize, GFP_ATOMIC);
	if (buff == NULL)
		return -ENOMEM; /* We'll just try again later. */
	sk_charge_skb(sk, buff);

	/* Correct the sequence numbers. */
	TCP_SKB_CB(buff)->seq = TCP_SKB_CB(skb)->seq + len;
	TCP_SKB_CB(buff)->end_seq = TCP_SKB_CB(skb)->end_seq;
	TCP_SKB_CB(skb)->end_seq = TCP_SKB_CB(buff)->seq;

	/* PSH and FIN should only be set in the second packet. */
	flags = TCP_SKB_CB(skb)->flags;
	TCP_SKB_CB(skb)->flags = flags & ~(TCPCB_FLAG_FIN|TCPCB_FLAG_PSH);
	TCP_SKB_CB(buff)->flags = flags;
	TCP_SKB_CB(buff)->sacked = TCP_SKB_CB(skb)->sacked;
	TCP_SKB_CB(skb)->sacked &= ~TCPCB_AT_TAIL;

	if (!skb_shinfo(skb)->nr_frags && skb->ip_summed != CHECKSUM_HW) {
		/* Copy and checksum data tail into the new buffer. */
		buff->csum = csum_partial_copy_nocheck(skb->data + len, skb_put(buff, nsize),
						       nsize, 0);

		skb_trim(skb, len);

		skb->csum = csum_block_sub(skb->csum, buff->csum, len);
	} else {
		skb->ip_summed = CHECKSUM_HW;
		skb_split(skb, buff, len);
	}

	buff->ip_summed = skb->ip_summed;

	/* Looks stupid, but our code really uses when of
	 * skbs, which it never sent before. --ANK
	 */
	TCP_SKB_CB(buff)->when = TCP_SKB_CB(skb)->when;
	buff->tstamp = skb->tstamp;

	old_factor = tcp_skb_pcount(skb);

	/* Fix up tso_factor for both original and new SKB.  */
	tcp_set_skb_tso_segs(sk, skb, mss_now);
	tcp_set_skb_tso_segs(sk, buff, mss_now);

	/* If this packet has been sent out already, we must
	 * adjust the various packet counters.
	 */
	if (!before(tp->snd_nxt, TCP_SKB_CB(buff)->end_seq)) {
		int diff = old_factor - tcp_skb_pcount(skb) -
			tcp_skb_pcount(buff);

		tp->packets_out -= diff;

		if (TCP_SKB_CB(skb)->sacked & TCPCB_SACKED_ACKED)
			tp->sacked_out -= diff;
		if (TCP_SKB_CB(skb)->sacked & TCPCB_SACKED_RETRANS)
			tp->retrans_out -= diff;

		if (TCP_SKB_CB(skb)->sacked & TCPCB_LOST) {
			tp->lost_out -= diff;
			tp->left_out -= diff;
		}

		if (diff > 0) {
			/* Adjust Reno SACK estimate. */
			if (!tp->rx_opt.sack_ok) {
				tp->sacked_out -= diff;
				if ((int)tp->sacked_out < 0)
					tp->sacked_out = 0;
				tcp_sync_left_out(tp);
			}

			tp->fackets_out -= diff;
			if ((int)tp->fackets_out < 0)
				tp->fackets_out = 0;
		}
	}

	/* Link BUFF into the send queue. */
	skb_header_release(buff);
	__skb_append(skb, buff, &sk->sk_write_queue);

	return 0;
}

/* This is similar to __pskb_pull_head() (it will go to core/skbuff.c
 * eventually). The difference is that pulled data not copied, but
 * immediately discarded.
 */
static unsigned char *__pskb_trim_head(struct sk_buff *skb, int len)
{
	int i, k, eat;

	eat = len;
	k = 0;
	for (i=0; i<skb_shinfo(skb)->nr_frags; i++) {
		if (skb_shinfo(skb)->frags[i].size <= eat) {
			put_page(skb_shinfo(skb)->frags[i].page);
			eat -= skb_shinfo(skb)->frags[i].size;
		} else {
			skb_shinfo(skb)->frags[k] = skb_shinfo(skb)->frags[i];
			if (eat) {
				skb_shinfo(skb)->frags[k].page_offset += eat;
				skb_shinfo(skb)->frags[k].size -= eat;
				eat = 0;
			}
			k++;
		}
	}
	skb_shinfo(skb)->nr_frags = k;

	skb->tail = skb->data;
	skb->data_len -= len;
	skb->len = skb->data_len;
	return skb->tail;
}

int tcp_trim_head(struct sock *sk, struct sk_buff *skb, u32 len)
{
	if (skb_cloned(skb) &&
	    pskb_expand_head(skb, 0, 0, GFP_ATOMIC))
		return -ENOMEM;

	if (len <= skb_headlen(skb)) {
		__skb_pull(skb, len);
	} else {
		if (__pskb_trim_head(skb, len-skb_headlen(skb)) == NULL)
			return -ENOMEM;
	}

	TCP_SKB_CB(skb)->seq += len;
	skb->ip_summed = CHECKSUM_HW;

	skb->truesize	     -= len;
	sk->sk_wmem_queued   -= len;
	sk->sk_forward_alloc += len;
	sock_set_flag(sk, SOCK_QUEUE_SHRUNK);

	/* Any change of skb->len requires recalculation of tso
	 * factor and mss.
	 */
	if (tcp_skb_pcount(skb) > 1)
		tcp_set_skb_tso_segs(sk, skb, tcp_current_mss(sk, 1));

	return 0;
}

/* This function synchronize snd mss to current pmtu/exthdr set.

   tp->rx_opt.user_mss is mss set by user by TCP_MAXSEG. It does NOT counts
   for TCP options, but includes only bare TCP header.

   tp->rx_opt.mss_clamp is mss negotiated at connection setup.
   It is minimum of user_mss and mss received with SYN.
   It also does not include TCP options.

   inet_csk(sk)->icsk_pmtu_cookie is last pmtu, seen by this function.

   tp->mss_cache is current effective sending mss, including
   all tcp options except for SACKs. It is evaluated,
   taking into account current pmtu, but never exceeds
   tp->rx_opt.mss_clamp.

   NOTE1. rfc1122 clearly states that advertised MSS
   DOES NOT include either tcp or ip options.

   NOTE2. inet_csk(sk)->icsk_pmtu_cookie and tp->mss_cache
   are READ ONLY outside this function.		--ANK (980731)
 */

unsigned int tcp_sync_mss(struct sock *sk, u32 pmtu)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct inet_connection_sock *icsk = inet_csk(sk);
	/* Calculate base mss without TCP options:
	   It is MMS_S - sizeof(tcphdr) of rfc1122
	 */
	int mss_now = (pmtu - icsk->icsk_af_ops->net_header_len -
		       sizeof(struct tcphdr));

	/* Clamp it (mss_clamp does not include tcp options) */
	if (mss_now > tp->rx_opt.mss_clamp)
		mss_now = tp->rx_opt.mss_clamp;

	/* Now subtract optional transport overhead */
	mss_now -= icsk->icsk_ext_hdr_len;

	/* Then reserve room for full set of TCP options and 8 bytes of data */
	if (mss_now < 48)
		mss_now = 48;

	/* Now subtract TCP options size, not including SACKs */
	mss_now -= tp->tcp_header_len - sizeof(struct tcphdr);

	/* Bound mss with half of window */
	if (tp->max_window && mss_now > (tp->max_window>>1))
		mss_now = max((tp->max_window>>1), 68U - tp->tcp_header_len);

	/* And store cached results */
	icsk->icsk_pmtu_cookie = pmtu;
	tp->mss_cache = mss_now;

	return mss_now;
}

/* Compute the current effective MSS, taking SACKs and IP options,
 * and even PMTU discovery events into account.
 *
 * LARGESEND note: !urg_mode is overkill, only frames up to snd_up
 * cannot be large. However, taking into account rare use of URG, this
 * is not a big flaw.
 */
unsigned int tcp_current_mss(struct sock *sk, int large_allowed)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct dst_entry *dst = __sk_dst_get(sk);
	u32 mss_now;
	u16 xmit_size_goal;
	int doing_tso = 0;

	mss_now = tp->mss_cache;

	if (large_allowed &&
	    (sk->sk_route_caps & NETIF_F_TSO) &&
	    !tp->urg_mode)
		doing_tso = 1;

	if (dst) {
		u32 mtu = dst_mtu(dst);
		if (mtu != inet_csk(sk)->icsk_pmtu_cookie)
			mss_now = tcp_sync_mss(sk, mtu);
	}

	if (tp->rx_opt.eff_sacks)
		mss_now -= (TCPOLEN_SACK_BASE_ALIGNED +
			    (tp->rx_opt.eff_sacks * TCPOLEN_SACK_PERBLOCK));

	xmit_size_goal = mss_now;

	if (doing_tso) {
		xmit_size_goal = (65535 -
				  inet_csk(sk)->icsk_af_ops->net_header_len -
				  inet_csk(sk)->icsk_ext_hdr_len -
				  tp->tcp_header_len);

		if (tp->max_window &&
		    (xmit_size_goal > (tp->max_window >> 1)))
			xmit_size_goal = max((tp->max_window >> 1),
					     68U - tp->tcp_header_len);

		xmit_size_goal -= (xmit_size_goal % mss_now);
	}
	tp->xmit_size_goal = xmit_size_goal;

	return mss_now;
}

/* Congestion window validation. (RFC2861) */

static void tcp_cwnd_validate(struct sock *sk, struct tcp_sock *tp)
{
	__u32 packets_out = tp->packets_out;

	if (packets_out >= tp->snd_cwnd) {
		/* Network is feed fully. */
		tp->snd_cwnd_used = 0;
		tp->snd_cwnd_stamp = tcp_time_stamp;
	} else {
		/* Network starves. */
		if (tp->packets_out > tp->snd_cwnd_used)
			tp->snd_cwnd_used = tp->packets_out;

		if ((s32)(tcp_time_stamp - tp->snd_cwnd_stamp) >= inet_csk(sk)->icsk_rto)
			tcp_cwnd_application_limited(sk);
	}
}

static unsigned int tcp_window_allows(struct tcp_sock *tp, struct sk_buff *skb, unsigned int mss_now, unsigned int cwnd)
{
	u32 window, cwnd_len;

	window = (tp->snd_una + tp->snd_wnd - TCP_SKB_CB(skb)->seq);
	cwnd_len = mss_now * cwnd;
	return min(window, cwnd_len);
}

/* Can at least one segment of SKB be sent right now, according to the
 * congestion window rules?  If so, return how many segments are allowed.
 */
static inline unsigned int tcp_cwnd_test(struct tcp_sock *tp, struct sk_buff *skb)
{
	u32 in_flight, cwnd;

	/* Don't be strict about the congestion window for the final FIN.  */
	if (TCP_SKB_CB(skb)->flags & TCPCB_FLAG_FIN)
		return 1;

	in_flight = tcp_packets_in_flight(tp);
	cwnd = tp->snd_cwnd;
	if (in_flight < cwnd)
		return (cwnd - in_flight);

	return 0;
}

/* This must be invoked the first time we consider transmitting
 * SKB onto the wire.
 */
static int tcp_init_tso_segs(struct sock *sk, struct sk_buff *skb, unsigned int mss_now)
{
	int tso_segs = tcp_skb_pcount(skb);

	if (!tso_segs ||
	    (tso_segs > 1 &&
	     skb_shinfo(skb)->tso_size != mss_now)) {
		tcp_set_skb_tso_segs(sk, skb, mss_now);
		tso_segs = tcp_skb_pcount(skb);
	}
	return tso_segs;
}

static inline int tcp_minshall_check(const struct tcp_sock *tp)
{
	return after(tp->snd_sml,tp->snd_una) &&
		!after(tp->snd_sml, tp->snd_nxt);
}

/* Return 0, if packet can be sent now without violation Nagle's rules:
 * 1. It is full sized.
 * 2. Or it contains FIN. (already checked by caller)
 * 3. Or TCP_NODELAY was set.
 * 4. Or TCP_CORK is not set, and all sent packets are ACKed.
 *    With Minshall's modification: all sent small packets are ACKed.
 */

static inline int tcp_nagle_check(const struct tcp_sock *tp,
				  const struct sk_buff *skb, 
				  unsigned mss_now, int nonagle)
{
	return (skb->len < mss_now &&
		((nonagle&TCP_NAGLE_CORK) ||
		 (!nonagle &&
		  tp->packets_out &&
		  tcp_minshall_check(tp))));
}

/* Return non-zero if the Nagle test allows this packet to be
 * sent now.
 */
static inline int tcp_nagle_test(struct tcp_sock *tp, struct sk_buff *skb,
				 unsigned int cur_mss, int nonagle)
{
	/* Nagle rule does not apply to frames, which sit in the middle of the
	 * write_queue (they have no chances to get new data).
	 *
	 * This is implemented in the callers, where they modify the 'nonagle'
	 * argument based upon the location of SKB in the send queue.
	 */
	if (nonagle & TCP_NAGLE_PUSH)
		return 1;

	/* Don't use the nagle rule for urgent data (or for the final FIN).  */
	if (tp->urg_mode ||
	    (TCP_SKB_CB(skb)->flags & TCPCB_FLAG_FIN))
		return 1;

	if (!tcp_nagle_check(tp, skb, cur_mss, nonagle))
		return 1;

	return 0;
}

/* Does at least the first segment of SKB fit into the send window? */
static inline int tcp_snd_wnd_test(struct tcp_sock *tp, struct sk_buff *skb, unsigned int cur_mss)
{
	u32 end_seq = TCP_SKB_CB(skb)->end_seq;

	if (skb->len > cur_mss)
		end_seq = TCP_SKB_CB(skb)->seq + cur_mss;

	return !after(end_seq, tp->snd_una + tp->snd_wnd);
}

/* This checks if the data bearing packet SKB (usually sk->sk_send_head)
 * should be put on the wire right now.  If so, it returns the number of
 * packets allowed by the congestion window.
 */
static unsigned int tcp_snd_test(struct sock *sk, struct sk_buff *skb,
				 unsigned int cur_mss, int nonagle)
{
	struct tcp_sock *tp = tcp_sk(sk);
	unsigned int cwnd_quota;

	tcp_init_tso_segs(sk, skb, cur_mss);

	if (!tcp_nagle_test(tp, skb, cur_mss, nonagle))
		return 0;

	cwnd_quota = tcp_cwnd_test(tp, skb);
	if (cwnd_quota &&
	    !tcp_snd_wnd_test(tp, skb, cur_mss))
		cwnd_quota = 0;

	return cwnd_quota;
}

static inline int tcp_skb_is_last(const struct sock *sk, 
				  const struct sk_buff *skb)
{
	return skb->next == (struct sk_buff *)&sk->sk_write_queue;
}

int tcp_may_send_now(struct sock *sk, struct tcp_sock *tp)
{
	struct sk_buff *skb = sk->sk_send_head;

	return (skb &&
		tcp_snd_test(sk, skb, tcp_current_mss(sk, 1),
			     (tcp_skb_is_last(sk, skb) ?
			      TCP_NAGLE_PUSH :
			      tp->nonagle)));
}

/* Trim TSO SKB to LEN bytes, put the remaining data into a new packet
 * which is put after SKB on the list.  It is very much like
 * tcp_fragment() except that it may make several kinds of assumptions
 * in order to speed up the splitting operation.  In particular, we
 * know that all the data is in scatter-gather pages, and that the
 * packet has never been sent out before (and thus is not cloned).
 */
static int tso_fragment(struct sock *sk, struct sk_buff *skb, unsigned int len, unsigned int mss_now)
{
	struct sk_buff *buff;
	int nlen = skb->len - len;
	u16 flags;

	/* All of a TSO frame must be composed of paged data.  */
	if (skb->len != skb->data_len)
		return tcp_fragment(sk, skb, len, mss_now);

	buff = sk_stream_alloc_pskb(sk, 0, 0, GFP_ATOMIC);
	if (unlikely(buff == NULL))
		return -ENOMEM;

	buff->truesize = nlen;
	skb->truesize -= nlen;

	/* Correct the sequence numbers. */
	TCP_SKB_CB(buff)->seq = TCP_SKB_CB(skb)->seq + len;
	TCP_SKB_CB(buff)->end_seq = TCP_SKB_CB(skb)->end_seq;
	TCP_SKB_CB(skb)->end_seq = TCP_SKB_CB(buff)->seq;

	/* PSH and FIN should only be set in the second packet. */
	flags = TCP_SKB_CB(skb)->flags;
	TCP_SKB_CB(skb)->flags = flags & ~(TCPCB_FLAG_FIN|TCPCB_FLAG_PSH);
	TCP_SKB_CB(buff)->flags = flags;

	/* This packet was never sent out yet, so no SACK bits. */
	TCP_SKB_CB(buff)->sacked = 0;

	buff->ip_summed = skb->ip_summed = CHECKSUM_HW;
	skb_split(skb, buff, len);

	/* Fix up tso_factor for both original and new SKB.  */
	tcp_set_skb_tso_segs(sk, skb, mss_now);
	tcp_set_skb_tso_segs(sk, buff, mss_now);

	/* Link BUFF into the send queue. */
	skb_header_release(buff);
	__skb_append(skb, buff, &sk->sk_write_queue);

	return 0;
}

/* Try to defer sending, if possible, in order to minimize the amount
 * of TSO splitting we do.  View it as a kind of TSO Nagle test.
 *
 * This algorithm is from John Heffner.
 */
static int tcp_tso_should_defer(struct sock *sk, struct tcp_sock *tp, struct sk_buff *skb)
{
	const struct inet_connection_sock *icsk = inet_csk(sk);
	u32 send_win, cong_win, limit, in_flight;

	if (TCP_SKB_CB(skb)->flags & TCPCB_FLAG_FIN)
		return 0;

	if (icsk->icsk_ca_state != TCP_CA_Open)
		return 0;

	in_flight = tcp_packets_in_flight(tp);

	BUG_ON(tcp_skb_pcount(skb) <= 1 ||
	       (tp->snd_cwnd <= in_flight));

	send_win = (tp->snd_una + tp->snd_wnd) - TCP_SKB_CB(skb)->seq;

	/* From in_flight test above, we know that cwnd > in_flight.  */
	cong_win = (tp->snd_cwnd - in_flight) * tp->mss_cache;

	limit = min(send_win, cong_win);

	/* If a full-sized TSO skb can be sent, do it. */
	if (limit >= 65536)
		return 0;

	if (sysctl_tcp_tso_win_divisor) {
		u32 chunk = min(tp->snd_wnd, tp->snd_cwnd * tp->mss_cache);

		/* If at least some fraction of a window is available,
		 * just use it.
		 */
		chunk /= sysctl_tcp_tso_win_divisor;
		if (limit >= chunk)
			return 0;
	} else {
		/* Different approach, try not to defer past a single
		 * ACK.  Receiver should ACK every other full sized
		 * frame, so if we have space for more than 3 frames
		 * then send now.
		 */
		if (limit > tcp_max_burst(tp) * tp->mss_cache)
			return 0;
	}

	/* Ok, it looks like it is advisable to defer.  */
	return 1;
}

/* This routine writes packets to the network.  It advances the
 * send_head.  This happens as incoming acks open up the remote
 * window for us.
 *
 * Returns 1, if no segments are in flight and we have queued segments, but
 * cannot send anything now because of SWS or another problem.
 */
static int tcp_write_xmit(struct sock *sk, unsigned int mss_now, int nonagle)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct sk_buff *skb;
	unsigned int tso_segs, sent_pkts;
	int cwnd_quota;

	/* If we are closed, the bytes will have to remain here.
	 * In time closedown will finish, we empty the write queue and all
	 * will be happy.
	 */
	if (unlikely(sk->sk_state == TCP_CLOSE))
		return 0;

	sent_pkts = 0;
	while ((skb = sk->sk_send_head)) {
		unsigned int limit;

		tso_segs = tcp_init_tso_segs(sk, skb, mss_now);
		BUG_ON(!tso_segs);

		cwnd_quota = tcp_cwnd_test(tp, skb);
		if (!cwnd_quota)
			break;

		if (unlikely(!tcp_snd_wnd_test(tp, skb, mss_now)))
			break;

		if (tso_segs == 1) {
			if (unlikely(!tcp_nagle_test(tp, skb, mss_now,
						     (tcp_skb_is_last(sk, skb) ?
						      nonagle : TCP_NAGLE_PUSH))))
				break;
		} else {
			if (tcp_tso_should_defer(sk, tp, skb))
				break;
		}

		limit = mss_now;
		if (tso_segs > 1) {
			limit = tcp_window_allows(tp, skb,
						  mss_now, cwnd_quota);

			if (skb->len < limit) {
				unsigned int trim = skb->len % mss_now;

				if (trim)
					limit = skb->len - trim;
			}
		}

		if (skb->len > limit &&
		    unlikely(tso_fragment(sk, skb, limit, mss_now)))
			break;

		TCP_SKB_CB(skb)->when = tcp_time_stamp;

		if (unlikely(tcp_transmit_skb(sk, skb, 1, GFP_ATOMIC)))
			break;

		/* Advance the send_head.  This one is sent out.
		 * This call will increment packets_out.
		 */
		update_send_head(sk, tp, skb);

		tcp_minshall_update(tp, mss_now, skb);
		sent_pkts++;
	}

	if (likely(sent_pkts)) {
		tcp_cwnd_validate(sk, tp);
		return 0;
	}
	return !tp->packets_out && sk->sk_send_head;
}

/* Push out any pending frames which were held back due to
 * TCP_CORK or attempt at coalescing tiny packets.
 * The socket must be locked by the caller.
 */
void __tcp_push_pending_frames(struct sock *sk, struct tcp_sock *tp,
			       unsigned int cur_mss, int nonagle)
{
	struct sk_buff *skb = sk->sk_send_head;

	if (skb) {
		if (tcp_write_xmit(sk, cur_mss, nonagle))
			tcp_check_probe_timer(sk, tp);
	}
}

/* Send _single_ skb sitting at the send head. This function requires
 * true push pending frames to setup probe timer etc.
 */
void tcp_push_one(struct sock *sk, unsigned int mss_now)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct sk_buff *skb = sk->sk_send_head;
	unsigned int tso_segs, cwnd_quota;

	BUG_ON(!skb || skb->len < mss_now);

	tso_segs = tcp_init_tso_segs(sk, skb, mss_now);
	cwnd_quota = tcp_snd_test(sk, skb, mss_now, TCP_NAGLE_PUSH);

	if (likely(cwnd_quota)) {
		unsigned int limit;

		BUG_ON(!tso_segs);

		limit = mss_now;
		if (tso_segs > 1) {
			limit = tcp_window_allows(tp, skb,
						  mss_now, cwnd_quota);

			if (skb->len < limit) {
				unsigned int trim = skb->len % mss_now;

				if (trim)
					limit = skb->len - trim;
			}
		}

		if (skb->len > limit &&
		    unlikely(tso_fragment(sk, skb, limit, mss_now)))
			return;

		/* Send it out now. */
		TCP_SKB_CB(skb)->when = tcp_time_stamp;

		if (likely(!tcp_transmit_skb(sk, skb, 1, sk->sk_allocation))) {
			update_send_head(sk, tp, skb);
			tcp_cwnd_validate(sk, tp);
			return;
		}
	}
}

/* This function returns the amount that we can raise the
 * usable window based on the following constraints
 *  
 * 1. The window can never be shrunk once it is offered (RFC 793)
 * 2. We limit memory per socket
 *
 * RFC 1122:
 * "the suggested [SWS] avoidance algorithm for the receiver is to keep
 *  RECV.NEXT + RCV.WIN fixed until:
 *  RCV.BUFF - RCV.USER - RCV.WINDOW >= min(1/2 RCV.BUFF, MSS)"
 *
 * i.e. don't raise the right edge of the window until you can raise
 * it at least MSS bytes.
 *
 * Unfortunately, the recommended algorithm breaks header prediction,
 * since header prediction assumes th->window stays fixed.
 *
 * Strictly speaking, keeping th->window fixed violates the receiver
 * side SWS prevention criteria. The problem is that under this rule
 * a stream of single byte packets will cause the right side of the
 * window to always advance by a single byte.
 * 
 * Of course, if the sender implements sender side SWS prevention
 * then this will not be a problem.
 * 
 * BSD seems to make the following compromise:
 * 
 *	If the free space is less than the 1/4 of the maximum
 *	space available and the free space is less than 1/2 mss,
 *	then set the window to 0.
 *	[ Actually, bsd uses MSS and 1/4 of maximal _window_ ]
 *	Otherwise, just prevent the window from shrinking
 *	and from being larger than the largest representable value.
 *
 * This prevents incremental opening of the window in the regime
 * where TCP is limited by the speed of the reader side taking
 * data out of the TCP receive queue. It does nothing about
 * those cases where the window is constrained on the sender side
 * because the pipeline is full.
 *
 * BSD also seems to "accidentally" limit itself to windows that are a
 * multiple of MSS, at least until the free space gets quite small.
 * This would appear to be a side effect of the mbuf implementation.
 * Combining these two algorithms results in the observed behavior
 * of having a fixed window size at almost all times.
 *
 * Below we obtain similar behavior by forcing the offered window to
 * a multiple of the mss when it is feasible to do so.
 *
 * Note, we don't "adjust" for TIMESTAMP or SACK option bytes.
 * Regular options like TIMESTAMP are taken into account.
 */
u32 __tcp_select_window(struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	/* MSS for the peer's data.  Previous versions used mss_clamp
	 * here.  I don't know if the value based on our guesses
	 * of peer's MSS is better for the performance.  It's more correct
	 * but may be worse for the performance because of rcv_mss
	 * fluctuations.  --SAW  1998/11/1
	 */
	int mss = icsk->icsk_ack.rcv_mss;
	int free_space = tcp_space(sk);
	int full_space = min_t(int, tp->window_clamp, tcp_full_space(sk));
	int window;

	if (mss > full_space)
		mss = full_space; 

	if (free_space < full_space/2) {
		icsk->icsk_ack.quick = 0;

		if (tcp_memory_pressure)
			tp->rcv_ssthresh = min(tp->rcv_ssthresh, 4U*tp->advmss);

		if (free_space < mss)
			return 0;
	}

	if (free_space > tp->rcv_ssthresh)
		free_space = tp->rcv_ssthresh;

	/* Don't do rounding if we are using window scaling, since the
	 * scaled window will not line up with the MSS boundary anyway.
	 */
	window = tp->rcv_wnd;
	if (tp->rx_opt.rcv_wscale) {
		window = free_space;

		/* Advertise enough space so that it won't get scaled away.
		 * Import case: prevent zero window announcement if
		 * 1<<rcv_wscale > mss.
		 */
		if (((window >> tp->rx_opt.rcv_wscale) << tp->rx_opt.rcv_wscale) != window)
			window = (((window >> tp->rx_opt.rcv_wscale) + 1)
				  << tp->rx_opt.rcv_wscale);
	} else {
		/* Get the largest window that is a nice multiple of mss.
		 * Window clamp already applied above.
		 * If our current window offering is within 1 mss of the
		 * free space we just keep it. This prevents the divide
		 * and multiply from happening most of the time.
		 * We also don't do any window rounding when the free space
		 * is too small.
		 */
		if (window <= free_space - mss || window > free_space)
			window = (free_space/mss)*mss;
	}

	return window;
}

/* Attempt to collapse two adjacent SKB's during retransmission. */
static void tcp_retrans_try_collapse(struct sock *sk, struct sk_buff *skb, int mss_now)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct sk_buff *next_skb = skb->next;

	/* The first test we must make is that neither of these two
	 * SKB's are still referenced by someone else.
	 */
	if (!skb_cloned(skb) && !skb_cloned(next_skb)) {
		int skb_size = skb->len, next_skb_size = next_skb->len;
		u16 flags = TCP_SKB_CB(skb)->flags;

		/* Also punt if next skb has been SACK'd. */
		if(TCP_SKB_CB(next_skb)->sacked & TCPCB_SACKED_ACKED)
			return;

		/* Next skb is out of window. */
		if (after(TCP_SKB_CB(next_skb)->end_seq, tp->snd_una+tp->snd_wnd))
			return;

		/* Punt if not enough space exists in the first SKB for
		 * the data in the second, or the total combined payload
		 * would exceed the MSS.
		 */
		if ((next_skb_size > skb_tailroom(skb)) ||
		    ((skb_size + next_skb_size) > mss_now))
			return;

		BUG_ON(tcp_skb_pcount(skb) != 1 ||
		       tcp_skb_pcount(next_skb) != 1);

		/* changing transmit queue under us so clear hints */
		clear_all_retrans_hints(tp);

		/* Ok.	We will be able to collapse the packet. */
		__skb_unlink(next_skb, &sk->sk_write_queue);

		memcpy(skb_put(skb, next_skb_size), next_skb->data, next_skb_size);

		if (next_skb->ip_summed == CHECKSUM_HW)
			skb->ip_summed = CHECKSUM_HW;

		if (skb->ip_summed != CHECKSUM_HW)
			skb->csum = csum_block_add(skb->csum, next_skb->csum, skb_size);

		/* Update sequence range on original skb. */
		TCP_SKB_CB(skb)->end_seq = TCP_SKB_CB(next_skb)->end_seq;

		/* Merge over control information. */
		flags |= TCP_SKB_CB(next_skb)->flags; /* This moves PSH/FIN etc. over */
		TCP_SKB_CB(skb)->flags = flags;

		/* All done, get rid of second SKB and account for it so
		 * packet counting does not break.
		 */
		TCP_SKB_CB(skb)->sacked |= TCP_SKB_CB(next_skb)->sacked&(TCPCB_EVER_RETRANS|TCPCB_AT_TAIL);
		if (TCP_SKB_CB(next_skb)->sacked&TCPCB_SACKED_RETRANS)
			tp->retrans_out -= tcp_skb_pcount(next_skb);
		if (TCP_SKB_CB(next_skb)->sacked&TCPCB_LOST) {
			tp->lost_out -= tcp_skb_pcount(next_skb);
			tp->left_out -= tcp_skb_pcount(next_skb);
		}
		/* Reno case is special. Sigh... */
		if (!tp->rx_opt.sack_ok && tp->sacked_out) {
			tcp_dec_pcount_approx(&tp->sacked_out, next_skb);
			tp->left_out -= tcp_skb_pcount(next_skb);
		}

		/* Not quite right: it can be > snd.fack, but
		 * it is better to underestimate fackets.
		 */
		tcp_dec_pcount_approx(&tp->fackets_out, next_skb);
		tcp_packets_out_dec(tp, next_skb);
		sk_stream_free_skb(sk, next_skb);
	}
}

/* Do a simple retransmit without using the backoff mechanisms in
 * tcp_timer. This is used for path mtu discovery. 
 * The socket is already locked here.
 */ 
void tcp_simple_retransmit(struct sock *sk)
{
	const struct inet_connection_sock *icsk = inet_csk(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	struct sk_buff *skb;
	unsigned int mss = tcp_current_mss(sk, 0);
	int lost = 0;

	sk_stream_for_retrans_queue(skb, sk) {
		if (skb->len > mss && 
		    !(TCP_SKB_CB(skb)->sacked&TCPCB_SACKED_ACKED)) {
			if (TCP_SKB_CB(skb)->sacked&TCPCB_SACKED_RETRANS) {
				TCP_SKB_CB(skb)->sacked &= ~TCPCB_SACKED_RETRANS;
				tp->retrans_out -= tcp_skb_pcount(skb);
			}
			if (!(TCP_SKB_CB(skb)->sacked&TCPCB_LOST)) {
				TCP_SKB_CB(skb)->sacked |= TCPCB_LOST;
				tp->lost_out += tcp_skb_pcount(skb);
				lost = 1;
			}
		}
	}

	clear_all_retrans_hints(tp);

	if (!lost)
		return;

	tcp_sync_left_out(tp);

 	/* Don't muck with the congestion window here.
	 * Reason is that we do not increase amount of _data_
	 * in network, but units changed and effective
	 * cwnd/ssthresh really reduced now.
	 */
	if (icsk->icsk_ca_state != TCP_CA_Loss) {
		tp->high_seq = tp->snd_nxt;
		tp->snd_ssthresh = tcp_current_ssthresh(sk);
		tp->prior_ssthresh = 0;
		tp->undo_marker = 0;
		tcp_set_ca_state(sk, TCP_CA_Loss);
	}
	tcp_xmit_retransmit_queue(sk);
}

/* This retransmits one SKB.  Policy decisions and retransmit queue
 * state updates are done by the caller.  Returns non-zero if an
 * error occurred which prevented the send.
 */
int tcp_retransmit_skb(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_sock *tp = tcp_sk(sk);
 	unsigned int cur_mss = tcp_current_mss(sk, 0);
	int err;

	/* Do not sent more than we queued. 1/4 is reserved for possible
	 * copying overhead: fragmentation, tunneling, mangling etc.
	 */
	if (atomic_read(&sk->sk_wmem_alloc) >
	    min(sk->sk_wmem_queued + (sk->sk_wmem_queued >> 2), sk->sk_sndbuf))
		return -EAGAIN;

	if (before(TCP_SKB_CB(skb)->seq, tp->snd_una)) {
		if (before(TCP_SKB_CB(skb)->end_seq, tp->snd_una))
			BUG();
		if (tcp_trim_head(sk, skb, tp->snd_una - TCP_SKB_CB(skb)->seq))
			return -ENOMEM;
	}

	/* If receiver has shrunk his window, and skb is out of
	 * new window, do not retransmit it. The exception is the
	 * case, when window is shrunk to zero. In this case
	 * our retransmit serves as a zero window probe.
	 */
	if (!before(TCP_SKB_CB(skb)->seq, tp->snd_una+tp->snd_wnd)
	    && TCP_SKB_CB(skb)->seq != tp->snd_una)
		return -EAGAIN;

	if (skb->len > cur_mss) {
		if (tcp_fragment(sk, skb, cur_mss, cur_mss))
			return -ENOMEM; /* We'll try again later. */
	}

	/* Collapse two adjacent packets if worthwhile and we can. */
	if(!(TCP_SKB_CB(skb)->flags & TCPCB_FLAG_SYN) &&
	   (skb->len < (cur_mss >> 1)) &&
	   (skb->next != sk->sk_send_head) &&
	   (skb->next != (struct sk_buff *)&sk->sk_write_queue) &&
	   (skb_shinfo(skb)->nr_frags == 0 && skb_shinfo(skb->next)->nr_frags == 0) &&
	   (tcp_skb_pcount(skb) == 1 && tcp_skb_pcount(skb->next) == 1) &&
	   (sysctl_tcp_retrans_collapse != 0))
		tcp_retrans_try_collapse(sk, skb, cur_mss);

	if (inet_csk(sk)->icsk_af_ops->rebuild_header(sk))
		return -EHOSTUNREACH; /* Routing failure or similar. */

	/* Some Solaris stacks overoptimize and ignore the FIN on a
	 * retransmit when old data is attached.  So strip it off
	 * since it is cheap to do so and saves bytes on the network.
	 */
	if(skb->len > 0 &&
	   (TCP_SKB_CB(skb)->flags & TCPCB_FLAG_FIN) &&
	   tp->snd_una == (TCP_SKB_CB(skb)->end_seq - 1)) {
		if (!pskb_trim(skb, 0)) {
			TCP_SKB_CB(skb)->seq = TCP_SKB_CB(skb)->end_seq - 1;
			skb_shinfo(skb)->tso_segs = 1;
			skb_shinfo(skb)->tso_size = 0;
			skb->ip_summed = CHECKSUM_NONE;
			skb->csum = 0;
		}
	}

	/* Make a copy, if the first transmission SKB clone we made
	 * is still in somebody's hands, else make a clone.
	 */
	TCP_SKB_CB(skb)->when = tcp_time_stamp;

	err = tcp_transmit_skb(sk, skb, 1, GFP_ATOMIC);

	if (err == 0) {
		/* Update global TCP statistics. */
		TCP_INC_STATS(TCP_MIB_RETRANSSEGS);

		tp->total_retrans++;

#if FASTRETRANS_DEBUG > 0
		if (TCP_SKB_CB(skb)->sacked&TCPCB_SACKED_RETRANS) {
			if (net_ratelimit())
				printk(KERN_DEBUG "retrans_out leaked.\n");
		}
#endif
		TCP_SKB_CB(skb)->sacked |= TCPCB_RETRANS;
		tp->retrans_out += tcp_skb_pcount(skb);

		/* Save stamp of the first retransmit. */
		if (!tp->retrans_stamp)
			tp->retrans_stamp = TCP_SKB_CB(skb)->when;

		tp->undo_retrans++;

		/* snd_nxt is stored to detect loss of retransmitted segment,
		 * see tcp_input.c tcp_sacktag_write_queue().
		 */
		TCP_SKB_CB(skb)->ack_seq = tp->snd_nxt;
	}
	return err;
}

/* This gets called after a retransmit timeout, and the initially
 * retransmitted data is acknowledged.  It tries to continue
 * resending the rest of the retransmit queue, until either
 * we've sent it all or the congestion window limit is reached.
 * If doing SACK, the first ACK which comes back for a timeout
 * based retransmit packet might feed us FACK information again.
 * If so, we use it to avoid unnecessarily retransmissions.
 */
void tcp_xmit_retransmit_queue(struct sock *sk)
{
	const struct inet_connection_sock *icsk = inet_csk(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	struct sk_buff *skb;
	int packet_cnt;

	if (tp->retransmit_skb_hint) {
		skb = tp->retransmit_skb_hint;
		packet_cnt = tp->retransmit_cnt_hint;
	}else{
		skb = sk->sk_write_queue.next;
		packet_cnt = 0;
	}

	/* First pass: retransmit lost packets. */
	if (tp->lost_out) {
		sk_stream_for_retrans_queue_from(skb, sk) {
			__u8 sacked = TCP_SKB_CB(skb)->sacked;

			/* we could do better than to assign each time */
			tp->retransmit_skb_hint = skb;
			tp->retransmit_cnt_hint = packet_cnt;

			/* Assume this retransmit will generate
			 * only one packet for congestion window
			 * calculation purposes.  This works because
			 * tcp_retransmit_skb() will chop up the
			 * packet to be MSS sized and all the
			 * packet counting works out.
			 */
			if (tcp_packets_in_flight(tp) >= tp->snd_cwnd)
				return;

			if (sacked & TCPCB_LOST) {
				if (!(sacked&(TCPCB_SACKED_ACKED|TCPCB_SACKED_RETRANS))) {
					if (tcp_retransmit_skb(sk, skb)) {
						tp->retransmit_skb_hint = NULL;
						return;
					}
					if (icsk->icsk_ca_state != TCP_CA_Loss)
						NET_INC_STATS_BH(LINUX_MIB_TCPFASTRETRANS);
					else
						NET_INC_STATS_BH(LINUX_MIB_TCPSLOWSTARTRETRANS);

					if (skb ==
					    skb_peek(&sk->sk_write_queue))
						inet_csk_reset_xmit_timer(sk, ICSK_TIME_RETRANS,
									  inet_csk(sk)->icsk_rto,
									  TCP_RTO_MAX);
				}

				packet_cnt += tcp_skb_pcount(skb);
				if (packet_cnt >= tp->lost_out)
					break;
			}
		}
	}

	/* OK, demanded retransmission is finished. */

	/* Forward retransmissions are possible only during Recovery. */
	if (icsk->icsk_ca_state != TCP_CA_Recovery)
		return;

	/* No forward retransmissions in Reno are possible. */
	if (!tp->rx_opt.sack_ok)
		return;

	/* Yeah, we have to make difficult choice between forward transmission
	 * and retransmission... Both ways have their merits...
	 *
	 * For now we do not retransmit anything, while we have some new
	 * segments to send.
	 */

	if (tcp_may_send_now(sk, tp))
		return;

	if (tp->forward_skb_hint) {
		skb = tp->forward_skb_hint;
		packet_cnt = tp->forward_cnt_hint;
	} else{
		skb = sk->sk_write_queue.next;
		packet_cnt = 0;
	}

	sk_stream_for_retrans_queue_from(skb, sk) {
		tp->forward_cnt_hint = packet_cnt;
		tp->forward_skb_hint = skb;

		/* Similar to the retransmit loop above we
		 * can pretend that the retransmitted SKB
		 * we send out here will be composed of one
		 * real MSS sized packet because tcp_retransmit_skb()
		 * will fragment it if necessary.
		 */
		if (++packet_cnt > tp->fackets_out)
			break;

		if (tcp_packets_in_flight(tp) >= tp->snd_cwnd)
			break;

		if (TCP_SKB_CB(skb)->sacked & TCPCB_TAGBITS)
			continue;

		/* Ok, retransmit it. */
		if (tcp_retransmit_skb(sk, skb)) {
			tp->forward_skb_hint = NULL;
			break;
		}

		if (skb == skb_peek(&sk->sk_write_queue))
			inet_csk_reset_xmit_timer(sk, ICSK_TIME_RETRANS,
						  inet_csk(sk)->icsk_rto,
						  TCP_RTO_MAX);

		NET_INC_STATS_BH(LINUX_MIB_TCPFORWARDRETRANS);
	}
}


/* Send a fin.  The caller locks the socket for us.  This cannot be
 * allowed to fail queueing a FIN frame under any circumstances.
 */
void tcp_send_fin(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);	
	struct sk_buff *skb = skb_peek_tail(&sk->sk_write_queue);
	int mss_now;
	
	/* Optimization, tack on the FIN if we have a queue of
	 * unsent frames.  But be careful about outgoing SACKS
	 * and IP options.
	 */
	mss_now = tcp_current_mss(sk, 1);

	if (sk->sk_send_head != NULL) {
		TCP_SKB_CB(skb)->flags |= TCPCB_FLAG_FIN;
		TCP_SKB_CB(skb)->end_seq++;
		tp->write_seq++;
	} else {
		/* Socket is locked, keep trying until memory is available. */
		for (;;) {
			skb = alloc_skb_fclone(MAX_TCP_HEADER, GFP_KERNEL);
			if (skb)
				break;
			yield();
		}

		/* Reserve space for headers and prepare control bits. */
		skb_reserve(skb, MAX_TCP_HEADER);
		skb->csum = 0;
		TCP_SKB_CB(skb)->flags = (TCPCB_FLAG_ACK | TCPCB_FLAG_FIN);
		TCP_SKB_CB(skb)->sacked = 0;
		skb_shinfo(skb)->tso_segs = 1;
		skb_shinfo(skb)->tso_size = 0;

		/* FIN eats a sequence byte, write_seq advanced by tcp_queue_skb(). */
		TCP_SKB_CB(skb)->seq = tp->write_seq;
		TCP_SKB_CB(skb)->end_seq = TCP_SKB_CB(skb)->seq + 1;
		tcp_queue_skb(sk, skb);
	}
	__tcp_push_pending_frames(sk, tp, mss_now, TCP_NAGLE_OFF);
}

/* We get here when a process closes a file descriptor (either due to
 * an explicit close() or as a byproduct of exit()'ing) and there
 * was unread data in the receive queue.  This behavior is recommended
 * by draft-ietf-tcpimpl-prob-03.txt section 3.10.  -DaveM
 */
void tcp_send_active_reset(struct sock *sk, gfp_t priority)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct sk_buff *skb;

	/* NOTE: No TCP options attached and we never retransmit this. */
	skb = alloc_skb(MAX_TCP_HEADER, priority);
	if (!skb) {
		NET_INC_STATS(LINUX_MIB_TCPABORTFAILED);
		return;
	}

	/* Reserve space for headers and prepare control bits. */
	skb_reserve(skb, MAX_TCP_HEADER);
	skb->csum = 0;
	TCP_SKB_CB(skb)->flags = (TCPCB_FLAG_ACK | TCPCB_FLAG_RST);
	TCP_SKB_CB(skb)->sacked = 0;
	skb_shinfo(skb)->tso_segs = 1;
	skb_shinfo(skb)->tso_size = 0;

	/* Send it off. */
	TCP_SKB_CB(skb)->seq = tcp_acceptable_seq(sk, tp);
	TCP_SKB_CB(skb)->end_seq = TCP_SKB_CB(skb)->seq;
	TCP_SKB_CB(skb)->when = tcp_time_stamp;
	if (tcp_transmit_skb(sk, skb, 0, priority))
		NET_INC_STATS(LINUX_MIB_TCPABORTFAILED);
}

/* WARNING: This routine must only be called when we have already sent
 * a SYN packet that crossed the incoming SYN that caused this routine
 * to get called. If this assumption fails then the initial rcv_wnd
 * and rcv_wscale values will not be correct.
 */
int tcp_send_synack(struct sock *sk)
{
	struct sk_buff* skb;

	skb = skb_peek(&sk->sk_write_queue);
	if (skb == NULL || !(TCP_SKB_CB(skb)->flags&TCPCB_FLAG_SYN)) {
		printk(KERN_DEBUG "tcp_send_synack: wrong queue state\n");
		return -EFAULT;
	}
	if (!(TCP_SKB_CB(skb)->flags&TCPCB_FLAG_ACK)) {
		if (skb_cloned(skb)) {
			struct sk_buff *nskb = skb_copy(skb, GFP_ATOMIC);
			if (nskb == NULL)
				return -ENOMEM;
			__skb_unlink(skb, &sk->sk_write_queue);
			skb_header_release(nskb);
			__skb_queue_head(&sk->sk_write_queue, nskb);
			sk_stream_free_skb(sk, skb);
			sk_charge_skb(sk, nskb);
			skb = nskb;
		}

		TCP_SKB_CB(skb)->flags |= TCPCB_FLAG_ACK;
		TCP_ECN_send_synack(tcp_sk(sk), skb);
	}
	TCP_SKB_CB(skb)->when = tcp_time_stamp;
	return tcp_transmit_skb(sk, skb, 1, GFP_ATOMIC);
}

/*
 * Prepare a SYN-ACK.
 */
struct sk_buff * tcp_make_synack(struct sock *sk, struct dst_entry *dst,
				 struct request_sock *req)
{
	struct inet_request_sock *ireq = inet_rsk(req);
	struct tcp_sock *tp = tcp_sk(sk);
	struct tcphdr *th;
	int tcp_header_size;
	struct sk_buff *skb;

	skb = sock_wmalloc(sk, MAX_TCP_HEADER + 15, 1, GFP_ATOMIC);
	if (skb == NULL)
		return NULL;

	/* Reserve space for headers. */
	skb_reserve(skb, MAX_TCP_HEADER);

	skb->dst = dst_clone(dst);

	tcp_header_size = (sizeof(struct tcphdr) + TCPOLEN_MSS +
			   (ireq->tstamp_ok ? TCPOLEN_TSTAMP_ALIGNED : 0) +
			   (ireq->wscale_ok ? TCPOLEN_WSCALE_ALIGNED : 0) +
			   /* SACK_PERM is in the place of NOP NOP of TS */
			   ((ireq->sack_ok && !ireq->tstamp_ok) ? TCPOLEN_SACKPERM_ALIGNED : 0));
	skb->h.th = th = (struct tcphdr *) skb_push(skb, tcp_header_size);

	memset(th, 0, sizeof(struct tcphdr));
	th->syn = 1;
	th->ack = 1;
	if (dst->dev->features&NETIF_F_TSO)
		ireq->ecn_ok = 0;
	TCP_ECN_make_synack(req, th);
	th->source = inet_sk(sk)->sport;
	th->dest = ireq->rmt_port;
	TCP_SKB_CB(skb)->seq = tcp_rsk(req)->snt_isn;
	TCP_SKB_CB(skb)->end_seq = TCP_SKB_CB(skb)->seq + 1;
	TCP_SKB_CB(skb)->sacked = 0;
	skb_shinfo(skb)->tso_segs = 1;
	skb_shinfo(skb)->tso_size = 0;
	th->seq = htonl(TCP_SKB_CB(skb)->seq);
	th->ack_seq = htonl(tcp_rsk(req)->rcv_isn + 1);
	if (req->rcv_wnd == 0) { /* ignored for retransmitted syns */
		__u8 rcv_wscale; 
		/* Set this up on the first call only */
		req->window_clamp = tp->window_clamp ? : dst_metric(dst, RTAX_WINDOW);
		/* tcp_full_space because it is guaranteed to be the first packet */
		tcp_select_initial_window(tcp_full_space(sk), 
			dst_metric(dst, RTAX_ADVMSS) - (ireq->tstamp_ok ? TCPOLEN_TSTAMP_ALIGNED : 0),
			&req->rcv_wnd,
			&req->window_clamp,
			ireq->wscale_ok,
			&rcv_wscale);
		ireq->rcv_wscale = rcv_wscale; 
	}

	/* RFC1323: The window in SYN & SYN/ACK segments is never scaled. */
	th->window = htons(req->rcv_wnd);

	TCP_SKB_CB(skb)->when = tcp_time_stamp;
	tcp_syn_build_options((__u32 *)(th + 1), dst_metric(dst, RTAX_ADVMSS), ireq->tstamp_ok,
			      ireq->sack_ok, ireq->wscale_ok, ireq->rcv_wscale,
			      TCP_SKB_CB(skb)->when,
			      req->ts_recent);

	skb->csum = 0;
	th->doff = (tcp_header_size >> 2);
	TCP_INC_STATS(TCP_MIB_OUTSEGS);
	return skb;
}

/* 
 * Do all connect socket setups that can be done AF independent.
 */ 
static void tcp_connect_init(struct sock *sk)
{
	struct dst_entry *dst = __sk_dst_get(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	__u8 rcv_wscale;

	/* We'll fix this up when we get a response from the other end.
	 * See tcp_input.c:tcp_rcv_state_process case TCP_SYN_SENT.
	 */
	tp->tcp_header_len = sizeof(struct tcphdr) +
		(sysctl_tcp_timestamps ? TCPOLEN_TSTAMP_ALIGNED : 0);

	/* If user gave his TCP_MAXSEG, record it to clamp */
	if (tp->rx_opt.user_mss)
		tp->rx_opt.mss_clamp = tp->rx_opt.user_mss;
	tp->max_window = 0;
	tcp_sync_mss(sk, dst_mtu(dst));

	if (!tp->window_clamp)
		tp->window_clamp = dst_metric(dst, RTAX_WINDOW);
	tp->advmss = dst_metric(dst, RTAX_ADVMSS);
	tcp_initialize_rcv_mss(sk);

	tcp_select_initial_window(tcp_full_space(sk),
				  tp->advmss - (tp->rx_opt.ts_recent_stamp ? tp->tcp_header_len - sizeof(struct tcphdr) : 0),
				  &tp->rcv_wnd,
				  &tp->window_clamp,
				  sysctl_tcp_window_scaling,
				  &rcv_wscale);

	tp->rx_opt.rcv_wscale = rcv_wscale;
	tp->rcv_ssthresh = tp->rcv_wnd;

	sk->sk_err = 0;
	sock_reset_flag(sk, SOCK_DONE);
	tp->snd_wnd = 0;
	tcp_init_wl(tp, tp->write_seq, 0);
	tp->snd_una = tp->write_seq;
	tp->snd_sml = tp->write_seq;
	tp->rcv_nxt = 0;
	tp->rcv_wup = 0;
	tp->copied_seq = 0;

	inet_csk(sk)->icsk_rto = TCP_TIMEOUT_INIT;
	inet_csk(sk)->icsk_retransmits = 0;
	tcp_clear_retrans(tp);
}

/*
 * Build a SYN and send it off.
 */ 
int tcp_connect(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct sk_buff *buff;

	tcp_connect_init(sk);

	buff = alloc_skb_fclone(MAX_TCP_HEADER + 15, sk->sk_allocation);
	if (unlikely(buff == NULL))
		return -ENOBUFS;

	/* Reserve space for headers. */
	skb_reserve(buff, MAX_TCP_HEADER);

	TCP_SKB_CB(buff)->flags = TCPCB_FLAG_SYN;
	TCP_ECN_send_syn(sk, tp, buff);
	TCP_SKB_CB(buff)->sacked = 0;
	skb_shinfo(buff)->tso_segs = 1;
	skb_shinfo(buff)->tso_size = 0;
	buff->csum = 0;
	TCP_SKB_CB(buff)->seq = tp->write_seq++;
	TCP_SKB_CB(buff)->end_seq = tp->write_seq;
	tp->snd_nxt = tp->write_seq;
	tp->pushed_seq = tp->write_seq;

	/* Send it off. */
	TCP_SKB_CB(buff)->when = tcp_time_stamp;
	tp->retrans_stamp = TCP_SKB_CB(buff)->when;
	skb_header_release(buff);
	__skb_queue_tail(&sk->sk_write_queue, buff);
	sk_charge_skb(sk, buff);
	tp->packets_out += tcp_skb_pcount(buff);
	tcp_transmit_skb(sk, buff, 1, GFP_KERNEL);
	TCP_INC_STATS(TCP_MIB_ACTIVEOPENS);

	/* Timer for repeating the SYN until an answer. */
	inet_csk_reset_xmit_timer(sk, ICSK_TIME_RETRANS,
				  inet_csk(sk)->icsk_rto, TCP_RTO_MAX);
	return 0;
}

/* Send out a delayed ack, the caller does the policy checking
 * to see if we should even be here.  See tcp_input.c:tcp_ack_snd_check()
 * for details.
 */
void tcp_send_delayed_ack(struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	int ato = icsk->icsk_ack.ato;
	unsigned long timeout;

	if (ato > TCP_DELACK_MIN) {
		const struct tcp_sock *tp = tcp_sk(sk);
		int max_ato = HZ/2;

		if (icsk->icsk_ack.pingpong || (icsk->icsk_ack.pending & ICSK_ACK_PUSHED))
			max_ato = TCP_DELACK_MAX;

		/* Slow path, intersegment interval is "high". */

		/* If some rtt estimate is known, use it to bound delayed ack.
		 * Do not use inet_csk(sk)->icsk_rto here, use results of rtt measurements
		 * directly.
		 */
		if (tp->srtt) {
			int rtt = max(tp->srtt>>3, TCP_DELACK_MIN);

			if (rtt < max_ato)
				max_ato = rtt;
		}

		ato = min(ato, max_ato);
	}

	/* Stay within the limit we were given */
	timeout = jiffies + ato;

	/* Use new timeout only if there wasn't a older one earlier. */
	if (icsk->icsk_ack.pending & ICSK_ACK_TIMER) {
		/* If delack timer was blocked or is about to expire,
		 * send ACK now.
		 */
		if (icsk->icsk_ack.blocked ||
		    time_before_eq(icsk->icsk_ack.timeout, jiffies + (ato >> 2))) {
			tcp_send_ack(sk);
			return;
		}

		if (!time_before(timeout, icsk->icsk_ack.timeout))
			timeout = icsk->icsk_ack.timeout;
	}
	icsk->icsk_ack.pending |= ICSK_ACK_SCHED | ICSK_ACK_TIMER;
	icsk->icsk_ack.timeout = timeout;
	sk_reset_timer(sk, &icsk->icsk_delack_timer, timeout);
}

/* This routine sends an ack and also updates the window. */
void tcp_send_ack(struct sock *sk)
{
	/* If we have been reset, we may not send again. */
	if (sk->sk_state != TCP_CLOSE) {
		struct tcp_sock *tp = tcp_sk(sk);
		struct sk_buff *buff;

		/* We are not putting this on the write queue, so
		 * tcp_transmit_skb() will set the ownership to this
		 * sock.
		 */
		buff = alloc_skb(MAX_TCP_HEADER, GFP_ATOMIC);
		if (buff == NULL) {
			inet_csk_schedule_ack(sk);
			inet_csk(sk)->icsk_ack.ato = TCP_ATO_MIN;
			inet_csk_reset_xmit_timer(sk, ICSK_TIME_DACK,
						  TCP_DELACK_MAX, TCP_RTO_MAX);
			return;
		}

		/* Reserve space for headers and prepare control bits. */
		skb_reserve(buff, MAX_TCP_HEADER);
		buff->csum = 0;
		TCP_SKB_CB(buff)->flags = TCPCB_FLAG_ACK;
		TCP_SKB_CB(buff)->sacked = 0;
		skb_shinfo(buff)->tso_segs = 1;
		skb_shinfo(buff)->tso_size = 0;

		/* Send it off, this clears delayed acks for us. */
		TCP_SKB_CB(buff)->seq = TCP_SKB_CB(buff)->end_seq = tcp_acceptable_seq(sk, tp);
		TCP_SKB_CB(buff)->when = tcp_time_stamp;
		tcp_transmit_skb(sk, buff, 0, GFP_ATOMIC);
	}
}

/* This routine sends a packet with an out of date sequence
 * number. It assumes the other end will try to ack it.
 *
 * Question: what should we make while urgent mode?
 * 4.4BSD forces sending single byte of data. We cannot send
 * out of window data, because we have SND.NXT==SND.MAX...
 *
 * Current solution: to send TWO zero-length segments in urgent mode:
 * one is with SEG.SEQ=SND.UNA to deliver urgent pointer, another is
 * out-of-date with SND.UNA-1 to probe window.
 */
static int tcp_xmit_probe_skb(struct sock *sk, int urgent)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct sk_buff *skb;

	/* We don't queue it, tcp_transmit_skb() sets ownership. */
	skb = alloc_skb(MAX_TCP_HEADER, GFP_ATOMIC);
	if (skb == NULL) 
		return -1;

	/* Reserve space for headers and set control bits. */
	skb_reserve(skb, MAX_TCP_HEADER);
	skb->csum = 0;
	TCP_SKB_CB(skb)->flags = TCPCB_FLAG_ACK;
	TCP_SKB_CB(skb)->sacked = urgent;
	skb_shinfo(skb)->tso_segs = 1;
	skb_shinfo(skb)->tso_size = 0;

	/* Use a previous sequence.  This should cause the other
	 * end to send an ack.  Don't queue or clone SKB, just
	 * send it.
	 */
	TCP_SKB_CB(skb)->seq = urgent ? tp->snd_una : tp->snd_una - 1;
	TCP_SKB_CB(skb)->end_seq = TCP_SKB_CB(skb)->seq;
	TCP_SKB_CB(skb)->when = tcp_time_stamp;
	return tcp_transmit_skb(sk, skb, 0, GFP_ATOMIC);
}

int tcp_write_wakeup(struct sock *sk)
{
	if (sk->sk_state != TCP_CLOSE) {
		struct tcp_sock *tp = tcp_sk(sk);
		struct sk_buff *skb;

		if ((skb = sk->sk_send_head) != NULL &&
		    before(TCP_SKB_CB(skb)->seq, tp->snd_una+tp->snd_wnd)) {
			int err;
			unsigned int mss = tcp_current_mss(sk, 0);
			unsigned int seg_size = tp->snd_una+tp->snd_wnd-TCP_SKB_CB(skb)->seq;

			if (before(tp->pushed_seq, TCP_SKB_CB(skb)->end_seq))
				tp->pushed_seq = TCP_SKB_CB(skb)->end_seq;

			/* We are probing the opening of a window
			 * but the window size is != 0
			 * must have been a result SWS avoidance ( sender )
			 */
			if (seg_size < TCP_SKB_CB(skb)->end_seq - TCP_SKB_CB(skb)->seq ||
			    skb->len > mss) {
				seg_size = min(seg_size, mss);
				TCP_SKB_CB(skb)->flags |= TCPCB_FLAG_PSH;
				if (tcp_fragment(sk, skb, seg_size, mss))
					return -1;
			} else if (!tcp_skb_pcount(skb))
				tcp_set_skb_tso_segs(sk, skb, mss);

			TCP_SKB_CB(skb)->flags |= TCPCB_FLAG_PSH;
			TCP_SKB_CB(skb)->when = tcp_time_stamp;
			err = tcp_transmit_skb(sk, skb, 1, GFP_ATOMIC);
			if (!err) {
				update_send_head(sk, tp, skb);
			}
			return err;
		} else {
			if (tp->urg_mode &&
			    between(tp->snd_up, tp->snd_una+1, tp->snd_una+0xFFFF))
				tcp_xmit_probe_skb(sk, TCPCB_URG);
			return tcp_xmit_probe_skb(sk, 0);
		}
	}
	return -1;
}

/* A window probe timeout has occurred.  If window is not closed send
 * a partial packet else a zero probe.
 */
void tcp_send_probe0(struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	int err;

	err = tcp_write_wakeup(sk);

	if (tp->packets_out || !sk->sk_send_head) {
		/* Cancel probe timer, if it is not required. */
		icsk->icsk_probes_out = 0;
		icsk->icsk_backoff = 0;
		return;
	}

	if (err <= 0) {
		if (icsk->icsk_backoff < sysctl_tcp_retries2)
			icsk->icsk_backoff++;
		icsk->icsk_probes_out++;
		inet_csk_reset_xmit_timer(sk, ICSK_TIME_PROBE0, 
					  min(icsk->icsk_rto << icsk->icsk_backoff, TCP_RTO_MAX),
					  TCP_RTO_MAX);
	} else {
		/* If packet was not sent due to local congestion,
		 * do not backoff and do not remember icsk_probes_out.
		 * Let local senders to fight for local resources.
		 *
		 * Use accumulated backoff yet.
		 */
		if (!icsk->icsk_probes_out)
			icsk->icsk_probes_out = 1;
		inet_csk_reset_xmit_timer(sk, ICSK_TIME_PROBE0, 
					  min(icsk->icsk_rto << icsk->icsk_backoff,
					      TCP_RESOURCE_PROBE_INTERVAL),
					  TCP_RTO_MAX);
	}
}

EXPORT_SYMBOL(tcp_connect);
EXPORT_SYMBOL(tcp_make_synack);
EXPORT_SYMBOL(tcp_simple_retransmit);
EXPORT_SYMBOL(tcp_sync_mss);
EXPORT_SYMBOL(sysctl_tcp_tso_win_divisor);
