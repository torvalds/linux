#ifndef _NET_TCP_ECN_H_
#define _NET_TCP_ECN_H_ 1

#include <net/inet_ecn.h>
#include <net/request_sock.h>

#define TCP_HP_BITS (~(TCP_RESERVED_BITS|TCP_FLAG_PSH))

#define	TCP_ECN_OK		1
#define TCP_ECN_QUEUE_CWR	2
#define TCP_ECN_DEMAND_CWR	4

static inline void TCP_ECN_queue_cwr(struct tcp_sock *tp)
{
	if (tp->ecn_flags&TCP_ECN_OK)
		tp->ecn_flags |= TCP_ECN_QUEUE_CWR;
}


/* Output functions */

static inline void TCP_ECN_send_synack(struct tcp_sock *tp,
				       struct sk_buff *skb)
{
	TCP_SKB_CB(skb)->flags &= ~TCPCB_FLAG_CWR;
	if (!(tp->ecn_flags&TCP_ECN_OK))
		TCP_SKB_CB(skb)->flags &= ~TCPCB_FLAG_ECE;
}

static inline void TCP_ECN_send_syn(struct sock *sk, struct tcp_sock *tp,
				    struct sk_buff *skb)
{
	tp->ecn_flags = 0;
	if (sysctl_tcp_ecn) {
		TCP_SKB_CB(skb)->flags |= TCPCB_FLAG_ECE|TCPCB_FLAG_CWR;
		tp->ecn_flags = TCP_ECN_OK;
	}
}

static __inline__ void
TCP_ECN_make_synack(struct request_sock *req, struct tcphdr *th)
{
	if (inet_rsk(req)->ecn_ok)
		th->ece = 1;
}

static inline void TCP_ECN_send(struct sock *sk, struct tcp_sock *tp,
				struct sk_buff *skb, int tcp_header_len)
{
	if (tp->ecn_flags & TCP_ECN_OK) {
		/* Not-retransmitted data segment: set ECT and inject CWR. */
		if (skb->len != tcp_header_len &&
		    !before(TCP_SKB_CB(skb)->seq, tp->snd_nxt)) {
			INET_ECN_xmit(sk);
			if (tp->ecn_flags&TCP_ECN_QUEUE_CWR) {
				tp->ecn_flags &= ~TCP_ECN_QUEUE_CWR;
				skb->h.th->cwr = 1;
				skb_shinfo(skb)->gso_type |= SKB_GSO_TCP_ECN;
			}
		} else {
			/* ACK or retransmitted segment: clear ECT|CE */
			INET_ECN_dontxmit(sk);
		}
		if (tp->ecn_flags & TCP_ECN_DEMAND_CWR)
			skb->h.th->ece = 1;
	}
}

/* Input functions */

static inline void TCP_ECN_accept_cwr(struct tcp_sock *tp, struct sk_buff *skb)
{
	if (skb->h.th->cwr)
		tp->ecn_flags &= ~TCP_ECN_DEMAND_CWR;
}

static inline void TCP_ECN_withdraw_cwr(struct tcp_sock *tp)
{
	tp->ecn_flags &= ~TCP_ECN_DEMAND_CWR;
}

static inline void TCP_ECN_check_ce(struct tcp_sock *tp, struct sk_buff *skb)
{
	if (tp->ecn_flags&TCP_ECN_OK) {
		if (INET_ECN_is_ce(TCP_SKB_CB(skb)->flags))
			tp->ecn_flags |= TCP_ECN_DEMAND_CWR;
		/* Funny extension: if ECT is not set on a segment,
		 * it is surely retransmit. It is not in ECN RFC,
		 * but Linux follows this rule. */
		else if (INET_ECN_is_not_ect((TCP_SKB_CB(skb)->flags)))
			tcp_enter_quickack_mode((struct sock *)tp);
	}
}

static inline void TCP_ECN_rcv_synack(struct tcp_sock *tp, struct tcphdr *th)
{
	if ((tp->ecn_flags&TCP_ECN_OK) && (!th->ece || th->cwr))
		tp->ecn_flags &= ~TCP_ECN_OK;
}

static inline void TCP_ECN_rcv_syn(struct tcp_sock *tp, struct tcphdr *th)
{
	if ((tp->ecn_flags&TCP_ECN_OK) && (!th->ece || !th->cwr))
		tp->ecn_flags &= ~TCP_ECN_OK;
}

static inline int TCP_ECN_rcv_ecn_echo(struct tcp_sock *tp, struct tcphdr *th)
{
	if (th->ece && !th->syn && (tp->ecn_flags&TCP_ECN_OK))
		return 1;
	return 0;
}

static inline void TCP_ECN_openreq_child(struct tcp_sock *tp,
					 struct request_sock *req)
{
	tp->ecn_flags = inet_rsk(req)->ecn_ok ? TCP_ECN_OK : 0;
}

static __inline__ void
TCP_ECN_create_request(struct request_sock *req, struct tcphdr *th)
{
	if (sysctl_tcp_ecn && th->ece && th->cwr)
		inet_rsk(req)->ecn_ok = 1;
}

#endif
