/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _TCP_ECN_H
#define _TCP_ECN_H

#include <linux/tcp.h>
#include <linux/skbuff.h>

#include <net/inet_connection_sock.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <net/inet_ecn.h>

static inline void tcp_ecn_queue_cwr(struct tcp_sock *tp)
{
	if (tcp_ecn_mode_rfc3168(tp))
		tp->ecn_flags |= TCP_ECN_QUEUE_CWR;
}

static inline void tcp_ecn_accept_cwr(struct sock *sk,
				      const struct sk_buff *skb)
{
	if (tcp_hdr(skb)->cwr) {
		tcp_sk(sk)->ecn_flags &= ~TCP_ECN_DEMAND_CWR;

		/* If the sender is telling us it has entered CWR, then its
		 * cwnd may be very low (even just 1 packet), so we should ACK
		 * immediately.
		 */
		if (TCP_SKB_CB(skb)->seq != TCP_SKB_CB(skb)->end_seq)
			inet_csk(sk)->icsk_ack.pending |= ICSK_ACK_NOW;
	}
}

static inline void tcp_ecn_withdraw_cwr(struct tcp_sock *tp)
{
	tp->ecn_flags &= ~TCP_ECN_QUEUE_CWR;
}

static inline void tcp_ecn_rcv_synack(struct tcp_sock *tp,
				      const struct tcphdr *th)
{
	if (tcp_ecn_mode_rfc3168(tp) && (!th->ece || th->cwr))
		tcp_ecn_mode_set(tp, TCP_ECN_DISABLED);
}

static inline void tcp_ecn_rcv_syn(struct tcp_sock *tp,
				   const struct tcphdr *th)
{
	if (tcp_ecn_mode_rfc3168(tp) && (!th->ece || !th->cwr))
		tcp_ecn_mode_set(tp, TCP_ECN_DISABLED);
}

static inline bool tcp_ecn_rcv_ecn_echo(const struct tcp_sock *tp,
					const struct tcphdr *th)
{
	if (th->ece && !th->syn && tcp_ecn_mode_rfc3168(tp))
		return true;
	return false;
}

/* Packet ECN state for a SYN-ACK */
static inline void tcp_ecn_send_synack(struct sock *sk, struct sk_buff *skb)
{
	const struct tcp_sock *tp = tcp_sk(sk);

	TCP_SKB_CB(skb)->tcp_flags &= ~TCPHDR_CWR;
	if (tcp_ecn_disabled(tp))
		TCP_SKB_CB(skb)->tcp_flags &= ~TCPHDR_ECE;
	else if (tcp_ca_needs_ecn(sk) ||
		 tcp_bpf_ca_needs_ecn(sk))
		INET_ECN_xmit(sk);
}

/* Packet ECN state for a SYN.  */
static inline void tcp_ecn_send_syn(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_sock *tp = tcp_sk(sk);
	bool bpf_needs_ecn = tcp_bpf_ca_needs_ecn(sk);
	bool use_ecn = READ_ONCE(sock_net(sk)->ipv4.sysctl_tcp_ecn) == 1 ||
		tcp_ca_needs_ecn(sk) || bpf_needs_ecn;

	if (!use_ecn) {
		const struct dst_entry *dst = __sk_dst_get(sk);

		if (dst && dst_feature(dst, RTAX_FEATURE_ECN))
			use_ecn = true;
	}

	tp->ecn_flags = 0;

	if (use_ecn) {
		if (tcp_ca_needs_ecn(sk) || bpf_needs_ecn)
			INET_ECN_xmit(sk);

		TCP_SKB_CB(skb)->tcp_flags |= TCPHDR_ECE | TCPHDR_CWR;
		tcp_ecn_mode_set(tp, TCP_ECN_MODE_RFC3168);
	}
}

static inline void tcp_ecn_clear_syn(struct sock *sk, struct sk_buff *skb)
{
	if (READ_ONCE(sock_net(sk)->ipv4.sysctl_tcp_ecn_fallback))
		/* tp->ecn_flags are cleared at a later point in time when
		 * SYN ACK is ultimatively being received.
		 */
		TCP_SKB_CB(skb)->tcp_flags &= ~(TCPHDR_ECE | TCPHDR_CWR);
}

static inline void
tcp_ecn_make_synack(const struct request_sock *req, struct tcphdr *th)
{
	if (inet_rsk(req)->ecn_ok)
		th->ece = 1;
}

#endif /* _LINUX_TCP_ECN_H */
