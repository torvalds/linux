#undef TRACE_SYSTEM
#define TRACE_SYSTEM tcp

#if !defined(_TRACE_TCP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_TCP_H

#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <linux/tracepoint.h>
#include <net/ipv6.h>

#define tcp_state_name(state)	{ state, #state }
#define show_tcp_state_name(val)			\
	__print_symbolic(val,				\
		tcp_state_name(TCP_ESTABLISHED),	\
		tcp_state_name(TCP_SYN_SENT),		\
		tcp_state_name(TCP_SYN_RECV),		\
		tcp_state_name(TCP_FIN_WAIT1),		\
		tcp_state_name(TCP_FIN_WAIT2),		\
		tcp_state_name(TCP_TIME_WAIT),		\
		tcp_state_name(TCP_CLOSE),		\
		tcp_state_name(TCP_CLOSE_WAIT),		\
		tcp_state_name(TCP_LAST_ACK),		\
		tcp_state_name(TCP_LISTEN),		\
		tcp_state_name(TCP_CLOSING),		\
		tcp_state_name(TCP_NEW_SYN_RECV))

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
		__field(__u16, sport)
		__field(__u16, dport)
		__array(__u8, saddr, 4)
		__array(__u8, daddr, 4)
		__array(__u8, saddr_v6, 16)
		__array(__u8, daddr_v6, 16)
	),

	TP_fast_assign(
		struct inet_sock *inet = inet_sk(sk);
		struct in6_addr *pin6;
		__be32 *p32;

		__entry->skbaddr = skb;
		__entry->skaddr = sk;

		__entry->sport = ntohs(inet->inet_sport);
		__entry->dport = ntohs(inet->inet_dport);

		p32 = (__be32 *) __entry->saddr;
		*p32 = inet->inet_saddr;

		p32 = (__be32 *) __entry->daddr;
		*p32 =  inet->inet_daddr;

#if IS_ENABLED(CONFIG_IPV6)
		if (sk->sk_family == AF_INET6) {
			pin6 = (struct in6_addr *)__entry->saddr_v6;
			*pin6 = sk->sk_v6_rcv_saddr;
			pin6 = (struct in6_addr *)__entry->daddr_v6;
			*pin6 = sk->sk_v6_daddr;
		} else
#endif
		{
			pin6 = (struct in6_addr *)__entry->saddr_v6;
			ipv6_addr_set_v4mapped(inet->inet_saddr, pin6);
			pin6 = (struct in6_addr *)__entry->daddr_v6;
			ipv6_addr_set_v4mapped(inet->inet_daddr, pin6);
		}
	),

	TP_printk("sport=%hu dport=%hu saddr=%pI4 daddr=%pI4 saddrv6=%pI6c daddrv6=%pI6c",
		  __entry->sport, __entry->dport, __entry->saddr, __entry->daddr,
		  __entry->saddr_v6, __entry->daddr_v6)
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

	TP_PROTO(const struct sock *sk),

	TP_ARGS(sk),

	TP_STRUCT__entry(
		__field(const void *, skaddr)
		__field(__u16, sport)
		__field(__u16, dport)
		__array(__u8, saddr, 4)
		__array(__u8, daddr, 4)
		__array(__u8, saddr_v6, 16)
		__array(__u8, daddr_v6, 16)
	),

	TP_fast_assign(
		struct inet_sock *inet = inet_sk(sk);
		struct in6_addr *pin6;
		__be32 *p32;

		__entry->skaddr = sk;

		__entry->sport = ntohs(inet->inet_sport);
		__entry->dport = ntohs(inet->inet_dport);

		p32 = (__be32 *) __entry->saddr;
		*p32 = inet->inet_saddr;

		p32 = (__be32 *) __entry->daddr;
		*p32 =  inet->inet_daddr;

#if IS_ENABLED(CONFIG_IPV6)
		if (sk->sk_family == AF_INET6) {
			pin6 = (struct in6_addr *)__entry->saddr_v6;
			*pin6 = sk->sk_v6_rcv_saddr;
			pin6 = (struct in6_addr *)__entry->daddr_v6;
			*pin6 = sk->sk_v6_daddr;
		} else
#endif
		{
			pin6 = (struct in6_addr *)__entry->saddr_v6;
			ipv6_addr_set_v4mapped(inet->inet_saddr, pin6);
			pin6 = (struct in6_addr *)__entry->daddr_v6;
			ipv6_addr_set_v4mapped(inet->inet_daddr, pin6);
		}
	),

	TP_printk("sport=%hu dport=%hu saddr=%pI4 daddr=%pI4 saddrv6=%pI6c daddrv6=%pI6c",
		  __entry->sport, __entry->dport,
		  __entry->saddr, __entry->daddr,
		  __entry->saddr_v6, __entry->daddr_v6)
);

DEFINE_EVENT(tcp_event_sk, tcp_receive_reset,

	TP_PROTO(const struct sock *sk),

	TP_ARGS(sk)
);

DEFINE_EVENT(tcp_event_sk, tcp_destroy_sock,

	TP_PROTO(const struct sock *sk),

	TP_ARGS(sk)
);

TRACE_EVENT(tcp_set_state,

	TP_PROTO(const struct sock *sk, const int oldstate, const int newstate),

	TP_ARGS(sk, oldstate, newstate),

	TP_STRUCT__entry(
		__field(const void *, skaddr)
		__field(int, oldstate)
		__field(int, newstate)
		__field(__u16, sport)
		__field(__u16, dport)
		__array(__u8, saddr, 4)
		__array(__u8, daddr, 4)
		__array(__u8, saddr_v6, 16)
		__array(__u8, daddr_v6, 16)
	),

	TP_fast_assign(
		struct inet_sock *inet = inet_sk(sk);
		struct in6_addr *pin6;
		__be32 *p32;

		__entry->skaddr = sk;
		__entry->oldstate = oldstate;
		__entry->newstate = newstate;

		__entry->sport = ntohs(inet->inet_sport);
		__entry->dport = ntohs(inet->inet_dport);

		p32 = (__be32 *) __entry->saddr;
		*p32 = inet->inet_saddr;

		p32 = (__be32 *) __entry->daddr;
		*p32 =  inet->inet_daddr;

#if IS_ENABLED(CONFIG_IPV6)
		if (sk->sk_family == AF_INET6) {
			pin6 = (struct in6_addr *)__entry->saddr_v6;
			*pin6 = sk->sk_v6_rcv_saddr;
			pin6 = (struct in6_addr *)__entry->daddr_v6;
			*pin6 = sk->sk_v6_daddr;
		} else
#endif
		{
			pin6 = (struct in6_addr *)__entry->saddr_v6;
			ipv6_addr_set_v4mapped(inet->inet_saddr, pin6);
			pin6 = (struct in6_addr *)__entry->daddr_v6;
			ipv6_addr_set_v4mapped(inet->inet_daddr, pin6);
		}
	),

	TP_printk("sport=%hu dport=%hu saddr=%pI4 daddr=%pI4 saddrv6=%pI6c daddrv6=%pI6c oldstate=%s newstate=%s",
		  __entry->sport, __entry->dport,
		  __entry->saddr, __entry->daddr,
		  __entry->saddr_v6, __entry->daddr_v6,
		  show_tcp_state_name(__entry->oldstate),
		  show_tcp_state_name(__entry->newstate))
);

TRACE_EVENT(tcp_retransmit_synack,

	TP_PROTO(const struct sock *sk, const struct request_sock *req),

	TP_ARGS(sk, req),

	TP_STRUCT__entry(
		__field(const void *, skaddr)
		__field(const void *, req)
		__field(__u16, sport)
		__field(__u16, dport)
		__array(__u8, saddr, 4)
		__array(__u8, daddr, 4)
		__array(__u8, saddr_v6, 16)
		__array(__u8, daddr_v6, 16)
	),

	TP_fast_assign(
		struct inet_request_sock *ireq = inet_rsk(req);
		struct in6_addr *pin6;
		__be32 *p32;

		__entry->skaddr = sk;
		__entry->req = req;

		__entry->sport = ireq->ir_num;
		__entry->dport = ntohs(ireq->ir_rmt_port);

		p32 = (__be32 *) __entry->saddr;
		*p32 = ireq->ir_loc_addr;

		p32 = (__be32 *) __entry->daddr;
		*p32 = ireq->ir_rmt_addr;

#if IS_ENABLED(CONFIG_IPV6)
		if (sk->sk_family == AF_INET6) {
			pin6 = (struct in6_addr *)__entry->saddr_v6;
			*pin6 = ireq->ir_v6_loc_addr;
			pin6 = (struct in6_addr *)__entry->daddr_v6;
			*pin6 = ireq->ir_v6_rmt_addr;
		} else
#endif
		{
			pin6 = (struct in6_addr *)__entry->saddr_v6;
			ipv6_addr_set_v4mapped(ireq->ir_loc_addr, pin6);
			pin6 = (struct in6_addr *)__entry->daddr_v6;
			ipv6_addr_set_v4mapped(ireq->ir_rmt_addr, pin6);
		}
	),

	TP_printk("sport=%hu dport=%hu saddr=%pI4 daddr=%pI4 saddrv6=%pI6c daddrv6=%pI6c",
		  __entry->sport, __entry->dport,
		  __entry->saddr, __entry->daddr,
		  __entry->saddr_v6, __entry->daddr_v6)
);

#endif /* _TRACE_TCP_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
