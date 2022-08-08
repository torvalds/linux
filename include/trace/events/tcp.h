/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM tcp

#if !defined(_TRACE_TCP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_TCP_H

#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <linux/tracepoint.h>
#include <net/ipv6.h>
#include <net/tcp.h>
#include <linux/sock_diag.h>

#define TP_STORE_V4MAPPED(__entry, saddr, daddr)		\
	do {							\
		struct in6_addr *pin6;				\
								\
		pin6 = (struct in6_addr *)__entry->saddr_v6;	\
		ipv6_addr_set_v4mapped(saddr, pin6);		\
		pin6 = (struct in6_addr *)__entry->daddr_v6;	\
		ipv6_addr_set_v4mapped(daddr, pin6);		\
	} while (0)

#if IS_ENABLED(CONFIG_IPV6)
#define TP_STORE_ADDRS(__entry, saddr, daddr, saddr6, daddr6)		\
	do {								\
		if (sk->sk_family == AF_INET6) {			\
			struct in6_addr *pin6;				\
									\
			pin6 = (struct in6_addr *)__entry->saddr_v6;	\
			*pin6 = saddr6;					\
			pin6 = (struct in6_addr *)__entry->daddr_v6;	\
			*pin6 = daddr6;					\
		} else {						\
			TP_STORE_V4MAPPED(__entry, saddr, daddr);	\
		}							\
	} while (0)
#else
#define TP_STORE_ADDRS(__entry, saddr, daddr, saddr6, daddr6)	\
	TP_STORE_V4MAPPED(__entry, saddr, daddr)
#endif

/*
 * tcp event with arguments sk and skb
 *
 * Note: this class requires a valid sk pointer; while skb pointer could
 *       be NULL.
 */
DECLARE_EVENT_CLASS(tcp_event_sk_skb,

	TP_PROTO(const struct sock *sk, const struct sk_buff *skb),

	TP_ARGS(sk, skb),

	TP_STRUCT__entry(
		__field(const void *, skbaddr)
		__field(const void *, skaddr)
		__field(int, state)
		__field(__u16, sport)
		__field(__u16, dport)
		__field(__u16, family)
		__array(__u8, saddr, 4)
		__array(__u8, daddr, 4)
		__array(__u8, saddr_v6, 16)
		__array(__u8, daddr_v6, 16)
	),

	TP_fast_assign(
		struct inet_sock *inet = inet_sk(sk);
		__be32 *p32;

		__entry->skbaddr = skb;
		__entry->skaddr = sk;
		__entry->state = sk->sk_state;

		__entry->sport = ntohs(inet->inet_sport);
		__entry->dport = ntohs(inet->inet_dport);
		__entry->family = sk->sk_family;

		p32 = (__be32 *) __entry->saddr;
		*p32 = inet->inet_saddr;

		p32 = (__be32 *) __entry->daddr;
		*p32 =  inet->inet_daddr;

		TP_STORE_ADDRS(__entry, inet->inet_saddr, inet->inet_daddr,
			      sk->sk_v6_rcv_saddr, sk->sk_v6_daddr);
	),

	TP_printk("family=%s sport=%hu dport=%hu saddr=%pI4 daddr=%pI4 saddrv6=%pI6c daddrv6=%pI6c state=%s",
		  show_family_name(__entry->family),
		  __entry->sport, __entry->dport, __entry->saddr, __entry->daddr,
		  __entry->saddr_v6, __entry->daddr_v6,
		  show_tcp_state_name(__entry->state))
);

DEFINE_EVENT(tcp_event_sk_skb, tcp_retransmit_skb,

	TP_PROTO(const struct sock *sk, const struct sk_buff *skb),

	TP_ARGS(sk, skb)
);

/*
 * skb of trace_tcp_send_reset is the skb that caused RST. In case of
 * active reset, skb should be NULL
 */
DEFINE_EVENT(tcp_event_sk_skb, tcp_send_reset,

	TP_PROTO(const struct sock *sk, const struct sk_buff *skb),

	TP_ARGS(sk, skb)
);

/*
 * tcp event with arguments sk
 *
 * Note: this class requires a valid sk pointer.
 */
DECLARE_EVENT_CLASS(tcp_event_sk,

	TP_PROTO(struct sock *sk),

	TP_ARGS(sk),

	TP_STRUCT__entry(
		__field(const void *, skaddr)
		__field(__u16, sport)
		__field(__u16, dport)
		__field(__u16, family)
		__array(__u8, saddr, 4)
		__array(__u8, daddr, 4)
		__array(__u8, saddr_v6, 16)
		__array(__u8, daddr_v6, 16)
		__field(__u64, sock_cookie)
	),

	TP_fast_assign(
		struct inet_sock *inet = inet_sk(sk);
		__be32 *p32;

		__entry->skaddr = sk;

		__entry->sport = ntohs(inet->inet_sport);
		__entry->dport = ntohs(inet->inet_dport);
		__entry->family = sk->sk_family;

		p32 = (__be32 *) __entry->saddr;
		*p32 = inet->inet_saddr;

		p32 = (__be32 *) __entry->daddr;
		*p32 =  inet->inet_daddr;

		TP_STORE_ADDRS(__entry, inet->inet_saddr, inet->inet_daddr,
			       sk->sk_v6_rcv_saddr, sk->sk_v6_daddr);

		__entry->sock_cookie = sock_gen_cookie(sk);
	),

	TP_printk("family=%s sport=%hu dport=%hu saddr=%pI4 daddr=%pI4 saddrv6=%pI6c daddrv6=%pI6c sock_cookie=%llx",
		  show_family_name(__entry->family),
		  __entry->sport, __entry->dport,
		  __entry->saddr, __entry->daddr,
		  __entry->saddr_v6, __entry->daddr_v6,
		  __entry->sock_cookie)
);

DEFINE_EVENT(tcp_event_sk, tcp_receive_reset,

	TP_PROTO(struct sock *sk),

	TP_ARGS(sk)
);

DEFINE_EVENT(tcp_event_sk, tcp_destroy_sock,

	TP_PROTO(struct sock *sk),

	TP_ARGS(sk)
);

DEFINE_EVENT(tcp_event_sk, tcp_rcv_space_adjust,

	TP_PROTO(struct sock *sk),

	TP_ARGS(sk)
);

TRACE_EVENT(tcp_retransmit_synack,

	TP_PROTO(const struct sock *sk, const struct request_sock *req),

	TP_ARGS(sk, req),

	TP_STRUCT__entry(
		__field(const void *, skaddr)
		__field(const void *, req)
		__field(__u16, sport)
		__field(__u16, dport)
		__field(__u16, family)
		__array(__u8, saddr, 4)
		__array(__u8, daddr, 4)
		__array(__u8, saddr_v6, 16)
		__array(__u8, daddr_v6, 16)
	),

	TP_fast_assign(
		struct inet_request_sock *ireq = inet_rsk(req);
		__be32 *p32;

		__entry->skaddr = sk;
		__entry->req = req;

		__entry->sport = ireq->ir_num;
		__entry->dport = ntohs(ireq->ir_rmt_port);
		__entry->family = sk->sk_family;

		p32 = (__be32 *) __entry->saddr;
		*p32 = ireq->ir_loc_addr;

		p32 = (__be32 *) __entry->daddr;
		*p32 = ireq->ir_rmt_addr;

		TP_STORE_ADDRS(__entry, ireq->ir_loc_addr, ireq->ir_rmt_addr,
			      ireq->ir_v6_loc_addr, ireq->ir_v6_rmt_addr);
	),

	TP_printk("family=%s sport=%hu dport=%hu saddr=%pI4 daddr=%pI4 saddrv6=%pI6c daddrv6=%pI6c",
		  show_family_name(__entry->family),
		  __entry->sport, __entry->dport,
		  __entry->saddr, __entry->daddr,
		  __entry->saddr_v6, __entry->daddr_v6)
);

#include <trace/events/net_probe_common.h>

TRACE_EVENT(tcp_probe,

	TP_PROTO(struct sock *sk, struct sk_buff *skb),

	TP_ARGS(sk, skb),

	TP_STRUCT__entry(
		/* sockaddr_in6 is always bigger than sockaddr_in */
		__array(__u8, saddr, sizeof(struct sockaddr_in6))
		__array(__u8, daddr, sizeof(struct sockaddr_in6))
		__field(__u16, sport)
		__field(__u16, dport)
		__field(__u16, family)
		__field(__u32, mark)
		__field(__u16, data_len)
		__field(__u32, snd_nxt)
		__field(__u32, snd_una)
		__field(__u32, snd_cwnd)
		__field(__u32, ssthresh)
		__field(__u32, snd_wnd)
		__field(__u32, srtt)
		__field(__u32, rcv_wnd)
		__field(__u64, sock_cookie)
	),

	TP_fast_assign(
		const struct tcphdr *th = (const struct tcphdr *)skb->data;
		const struct inet_sock *inet = inet_sk(sk);
		const struct tcp_sock *tp = tcp_sk(sk);

		memset(__entry->saddr, 0, sizeof(struct sockaddr_in6));
		memset(__entry->daddr, 0, sizeof(struct sockaddr_in6));

		TP_STORE_ADDR_PORTS(__entry, inet, sk);

		/* For filtering use */
		__entry->sport = ntohs(inet->inet_sport);
		__entry->dport = ntohs(inet->inet_dport);
		__entry->mark = skb->mark;
		__entry->family = sk->sk_family;

		__entry->data_len = skb->len - __tcp_hdrlen(th);
		__entry->snd_nxt = tp->snd_nxt;
		__entry->snd_una = tp->snd_una;
		__entry->snd_cwnd = tp->snd_cwnd;
		__entry->snd_wnd = tp->snd_wnd;
		__entry->rcv_wnd = tp->rcv_wnd;
		__entry->ssthresh = tcp_current_ssthresh(sk);
		__entry->srtt = tp->srtt_us >> 3;
		__entry->sock_cookie = sock_gen_cookie(sk);
	),

	TP_printk("family=%s src=%pISpc dest=%pISpc mark=%#x data_len=%d snd_nxt=%#x snd_una=%#x snd_cwnd=%u ssthresh=%u snd_wnd=%u srtt=%u rcv_wnd=%u sock_cookie=%llx",
		  show_family_name(__entry->family),
		  __entry->saddr, __entry->daddr, __entry->mark,
		  __entry->data_len, __entry->snd_nxt, __entry->snd_una,
		  __entry->snd_cwnd, __entry->ssthresh, __entry->snd_wnd,
		  __entry->srtt, __entry->rcv_wnd, __entry->sock_cookie)
);

#endif /* _TRACE_TCP_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
