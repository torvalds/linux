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
#include <net/rstreason.h>

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
		const struct inet_sock *inet = inet_sk(sk);
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

	TP_printk("skbaddr=%p skaddr=%p family=%s sport=%hu dport=%hu saddr=%pI4 daddr=%pI4 saddrv6=%pI6c daddrv6=%pI6c state=%s",
		  __entry->skbaddr, __entry->skaddr,
		  show_family_name(__entry->family),
		  __entry->sport, __entry->dport, __entry->saddr, __entry->daddr,
		  __entry->saddr_v6, __entry->daddr_v6,
		  show_tcp_state_name(__entry->state))
);

DEFINE_EVENT(tcp_event_sk_skb, tcp_retransmit_skb,

	TP_PROTO(const struct sock *sk, const struct sk_buff *skb),

	TP_ARGS(sk, skb)
);

#undef FN
#define FN(reason)	TRACE_DEFINE_ENUM(SK_RST_REASON_##reason);
DEFINE_RST_REASON(FN, FN)

#undef FN
#undef FNe
#define FN(reason)	{ SK_RST_REASON_##reason, #reason },
#define FNe(reason)	{ SK_RST_REASON_##reason, #reason }

/*
 * skb of trace_tcp_send_reset is the skb that caused RST. In case of
 * active reset, skb should be NULL
 */
TRACE_EVENT(tcp_send_reset,

	TP_PROTO(const struct sock *sk,
		 const struct sk_buff *skb__nullable,
		 const enum sk_rst_reason reason),

	TP_ARGS(sk, skb__nullable, reason),

	TP_STRUCT__entry(
		__field(const void *, skbaddr)
		__field(const void *, skaddr)
		__field(int, state)
		__field(enum sk_rst_reason, reason)
		__array(__u8, saddr, sizeof(struct sockaddr_in6))
		__array(__u8, daddr, sizeof(struct sockaddr_in6))
	),

	TP_fast_assign(
		__entry->skbaddr = skb__nullable;
		__entry->skaddr = sk;
		/* Zero means unknown state. */
		__entry->state = sk ? sk->sk_state : 0;

		memset(__entry->saddr, 0, sizeof(struct sockaddr_in6));
		memset(__entry->daddr, 0, sizeof(struct sockaddr_in6));

		if (sk && sk_fullsock(sk)) {
			const struct inet_sock *inet = inet_sk(sk);

			TP_STORE_ADDR_PORTS(__entry, inet, sk);
		} else if (skb__nullable) {
			const struct tcphdr *th = (const struct tcphdr *)skb__nullable->data;
			/*
			 * We should reverse the 4-tuple of skb, so later
			 * it can print the right flow direction of rst.
			 */
			TP_STORE_ADDR_PORTS_SKB(skb__nullable, th, entry->daddr, entry->saddr);
		}
		__entry->reason = reason;
	),

	TP_printk("skbaddr=%p skaddr=%p src=%pISpc dest=%pISpc state=%s reason=%s",
		  __entry->skbaddr, __entry->skaddr,
		  __entry->saddr, __entry->daddr,
		  __entry->state ? show_tcp_state_name(__entry->state) : "UNKNOWN",
		  __print_symbolic(__entry->reason, DEFINE_RST_REASON(FN, FNe)))
);

#undef FN
#undef FNe

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
		__field(const void *, skbaddr)
		__field(const void *, skaddr)
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
		__entry->snd_cwnd = tcp_snd_cwnd(tp);
		__entry->snd_wnd = tp->snd_wnd;
		__entry->rcv_wnd = tp->rcv_wnd;
		__entry->ssthresh = tcp_current_ssthresh(sk);
		__entry->srtt = tp->srtt_us >> 3;
		__entry->sock_cookie = sock_gen_cookie(sk);

		__entry->skbaddr = skb;
		__entry->skaddr = sk;
	),

	TP_printk("family=%s src=%pISpc dest=%pISpc mark=%#x data_len=%d snd_nxt=%#x snd_una=%#x snd_cwnd=%u ssthresh=%u snd_wnd=%u srtt=%u rcv_wnd=%u sock_cookie=%llx skbaddr=%p skaddr=%p",
		  show_family_name(__entry->family),
		  __entry->saddr, __entry->daddr, __entry->mark,
		  __entry->data_len, __entry->snd_nxt, __entry->snd_una,
		  __entry->snd_cwnd, __entry->ssthresh, __entry->snd_wnd,
		  __entry->srtt, __entry->rcv_wnd, __entry->sock_cookie,
		  __entry->skbaddr, __entry->skaddr)
);

/*
 * tcp event with only skb
 */
DECLARE_EVENT_CLASS(tcp_event_skb,

	TP_PROTO(const struct sk_buff *skb),

	TP_ARGS(skb),

	TP_STRUCT__entry(
		__field(const void *, skbaddr)
		__array(__u8, saddr, sizeof(struct sockaddr_in6))
		__array(__u8, daddr, sizeof(struct sockaddr_in6))
	),

	TP_fast_assign(
		const struct tcphdr *th = (const struct tcphdr *)skb->data;
		__entry->skbaddr = skb;

		memset(__entry->saddr, 0, sizeof(struct sockaddr_in6));
		memset(__entry->daddr, 0, sizeof(struct sockaddr_in6));

		TP_STORE_ADDR_PORTS_SKB(skb, th, __entry->saddr, __entry->daddr);
	),

	TP_printk("skbaddr=%p src=%pISpc dest=%pISpc",
		  __entry->skbaddr, __entry->saddr, __entry->daddr)
);

DEFINE_EVENT(tcp_event_skb, tcp_bad_csum,

	TP_PROTO(const struct sk_buff *skb),

	TP_ARGS(skb)
);

TRACE_EVENT(tcp_cong_state_set,

	TP_PROTO(struct sock *sk, const u8 ca_state),

	TP_ARGS(sk, ca_state),

	TP_STRUCT__entry(
		__field(const void *, skaddr)
		__field(__u16, sport)
		__field(__u16, dport)
		__field(__u16, family)
		__array(__u8, saddr, 4)
		__array(__u8, daddr, 4)
		__array(__u8, saddr_v6, 16)
		__array(__u8, daddr_v6, 16)
		__field(__u8, cong_state)
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

		__entry->cong_state = ca_state;
	),

	TP_printk("family=%s sport=%hu dport=%hu saddr=%pI4 daddr=%pI4 saddrv6=%pI6c daddrv6=%pI6c cong_state=%u",
		  show_family_name(__entry->family),
		  __entry->sport, __entry->dport,
		  __entry->saddr, __entry->daddr,
		  __entry->saddr_v6, __entry->daddr_v6,
		  __entry->cong_state)
);

DECLARE_EVENT_CLASS(tcp_hash_event,

	TP_PROTO(const struct sock *sk, const struct sk_buff *skb),

	TP_ARGS(sk, skb),

	TP_STRUCT__entry(
		__field(__u64, net_cookie)
		__field(const void *, skbaddr)
		__field(const void *, skaddr)
		__field(int, state)

		/* sockaddr_in6 is always bigger than sockaddr_in */
		__array(__u8, saddr, sizeof(struct sockaddr_in6))
		__array(__u8, daddr, sizeof(struct sockaddr_in6))
		__field(int, l3index)

		__field(__u16, sport)
		__field(__u16, dport)
		__field(__u16, family)

		__field(bool, fin)
		__field(bool, syn)
		__field(bool, rst)
		__field(bool, psh)
		__field(bool, ack)
	),

	TP_fast_assign(
		const struct tcphdr *th = (const struct tcphdr *)skb->data;

		__entry->net_cookie = sock_net(sk)->net_cookie;
		__entry->skbaddr = skb;
		__entry->skaddr = sk;
		__entry->state = sk->sk_state;

		memset(__entry->saddr, 0, sizeof(struct sockaddr_in6));
		memset(__entry->daddr, 0, sizeof(struct sockaddr_in6));
		TP_STORE_ADDR_PORTS_SKB(skb, th, __entry->saddr, __entry->daddr);
		__entry->l3index = inet_sdif(skb) ? inet_iif(skb) : 0;

		/* For filtering use */
		__entry->sport = ntohs(th->source);
		__entry->dport = ntohs(th->dest);
		__entry->family = sk->sk_family;

		__entry->fin = th->fin;
		__entry->syn = th->syn;
		__entry->rst = th->rst;
		__entry->psh = th->psh;
		__entry->ack = th->ack;
	),

	TP_printk("net=%llu state=%s family=%s src=%pISpc dest=%pISpc L3index=%d [%c%c%c%c%c]",
		  __entry->net_cookie,
		  show_tcp_state_name(__entry->state),
		  show_family_name(__entry->family),
		  __entry->saddr, __entry->daddr,
		  __entry->l3index,
		  __entry->fin ? 'F' : ' ',
		  __entry->syn ? 'S' : ' ',
		  __entry->rst ? 'R' : ' ',
		  __entry->psh ? 'P' : ' ',
		  __entry->ack ? '.' : ' ')
);

DEFINE_EVENT(tcp_hash_event, tcp_hash_bad_header,

	TP_PROTO(const struct sock *sk, const struct sk_buff *skb),
	TP_ARGS(sk, skb)
);

DEFINE_EVENT(tcp_hash_event, tcp_hash_md5_required,

	TP_PROTO(const struct sock *sk, const struct sk_buff *skb),
	TP_ARGS(sk, skb)
);

DEFINE_EVENT(tcp_hash_event, tcp_hash_md5_unexpected,

	TP_PROTO(const struct sock *sk, const struct sk_buff *skb),
	TP_ARGS(sk, skb)
);

DEFINE_EVENT(tcp_hash_event, tcp_hash_md5_mismatch,

	TP_PROTO(const struct sock *sk, const struct sk_buff *skb),
	TP_ARGS(sk, skb)
);

DEFINE_EVENT(tcp_hash_event, tcp_hash_ao_required,

	TP_PROTO(const struct sock *sk, const struct sk_buff *skb),
	TP_ARGS(sk, skb)
);

DECLARE_EVENT_CLASS(tcp_ao_event,

	TP_PROTO(const struct sock *sk, const struct sk_buff *skb,
		 const __u8 keyid, const __u8 rnext, const __u8 maclen),

	TP_ARGS(sk, skb, keyid, rnext, maclen),

	TP_STRUCT__entry(
		__field(__u64, net_cookie)
		__field(const void *, skbaddr)
		__field(const void *, skaddr)
		__field(int, state)

		/* sockaddr_in6 is always bigger than sockaddr_in */
		__array(__u8, saddr, sizeof(struct sockaddr_in6))
		__array(__u8, daddr, sizeof(struct sockaddr_in6))
		__field(int, l3index)

		__field(__u16, sport)
		__field(__u16, dport)
		__field(__u16, family)

		__field(bool, fin)
		__field(bool, syn)
		__field(bool, rst)
		__field(bool, psh)
		__field(bool, ack)

		__field(__u8, keyid)
		__field(__u8, rnext)
		__field(__u8, maclen)
	),

	TP_fast_assign(
		const struct tcphdr *th = (const struct tcphdr *)skb->data;

		__entry->net_cookie = sock_net(sk)->net_cookie;
		__entry->skbaddr = skb;
		__entry->skaddr = sk;
		__entry->state = sk->sk_state;

		memset(__entry->saddr, 0, sizeof(struct sockaddr_in6));
		memset(__entry->daddr, 0, sizeof(struct sockaddr_in6));
		TP_STORE_ADDR_PORTS_SKB(skb, th, __entry->saddr, __entry->daddr);
		__entry->l3index = inet_sdif(skb) ? inet_iif(skb) : 0;

		/* For filtering use */
		__entry->sport = ntohs(th->source);
		__entry->dport = ntohs(th->dest);
		__entry->family = sk->sk_family;

		__entry->fin = th->fin;
		__entry->syn = th->syn;
		__entry->rst = th->rst;
		__entry->psh = th->psh;
		__entry->ack = th->ack;

		__entry->keyid = keyid;
		__entry->rnext = rnext;
		__entry->maclen = maclen;
	),

	TP_printk("net=%llu state=%s family=%s src=%pISpc dest=%pISpc L3index=%d [%c%c%c%c%c] keyid=%u rnext=%u maclen=%u",
		  __entry->net_cookie,
		  show_tcp_state_name(__entry->state),
		  show_family_name(__entry->family),
		  __entry->saddr, __entry->daddr,
		  __entry->l3index,
		  __entry->fin ? 'F' : ' ',
		  __entry->syn ? 'S' : ' ',
		  __entry->rst ? 'R' : ' ',
		  __entry->psh ? 'P' : ' ',
		  __entry->ack ? '.' : ' ',
		  __entry->keyid, __entry->rnext, __entry->maclen)
);

DEFINE_EVENT(tcp_ao_event, tcp_ao_handshake_failure,
	TP_PROTO(const struct sock *sk, const struct sk_buff *skb,
		 const __u8 keyid, const __u8 rnext, const __u8 maclen),
	TP_ARGS(sk, skb, keyid, rnext, maclen)
);

DEFINE_EVENT(tcp_ao_event, tcp_ao_wrong_maclen,
	TP_PROTO(const struct sock *sk, const struct sk_buff *skb,
		 const __u8 keyid, const __u8 rnext, const __u8 maclen),
	TP_ARGS(sk, skb, keyid, rnext, maclen)
);

DEFINE_EVENT(tcp_ao_event, tcp_ao_mismatch,
	TP_PROTO(const struct sock *sk, const struct sk_buff *skb,
		 const __u8 keyid, const __u8 rnext, const __u8 maclen),
	TP_ARGS(sk, skb, keyid, rnext, maclen)
);

DEFINE_EVENT(tcp_ao_event, tcp_ao_key_not_found,
	TP_PROTO(const struct sock *sk, const struct sk_buff *skb,
		 const __u8 keyid, const __u8 rnext, const __u8 maclen),
	TP_ARGS(sk, skb, keyid, rnext, maclen)
);

DEFINE_EVENT(tcp_ao_event, tcp_ao_rnext_request,
	TP_PROTO(const struct sock *sk, const struct sk_buff *skb,
		 const __u8 keyid, const __u8 rnext, const __u8 maclen),
	TP_ARGS(sk, skb, keyid, rnext, maclen)
);

DECLARE_EVENT_CLASS(tcp_ao_event_sk,

	TP_PROTO(const struct sock *sk, const __u8 keyid, const __u8 rnext),

	TP_ARGS(sk, keyid, rnext),

	TP_STRUCT__entry(
		__field(__u64, net_cookie)
		__field(const void *, skaddr)
		__field(int, state)

		/* sockaddr_in6 is always bigger than sockaddr_in */
		__array(__u8, saddr, sizeof(struct sockaddr_in6))
		__array(__u8, daddr, sizeof(struct sockaddr_in6))

		__field(__u16, sport)
		__field(__u16, dport)
		__field(__u16, family)

		__field(__u8, keyid)
		__field(__u8, rnext)
	),

	TP_fast_assign(
		const struct inet_sock *inet = inet_sk(sk);

		__entry->net_cookie = sock_net(sk)->net_cookie;
		__entry->skaddr = sk;
		__entry->state = sk->sk_state;

		memset(__entry->saddr, 0, sizeof(struct sockaddr_in6));
		memset(__entry->daddr, 0, sizeof(struct sockaddr_in6));
		TP_STORE_ADDR_PORTS(__entry, inet, sk);

		/* For filtering use */
		__entry->sport = ntohs(inet->inet_sport);
		__entry->dport = ntohs(inet->inet_dport);
		__entry->family = sk->sk_family;

		__entry->keyid = keyid;
		__entry->rnext = rnext;
	),

	TP_printk("net=%llu state=%s family=%s src=%pISpc dest=%pISpc keyid=%u rnext=%u",
		  __entry->net_cookie,
		  show_tcp_state_name(__entry->state),
		  show_family_name(__entry->family),
		  __entry->saddr, __entry->daddr,
		  __entry->keyid, __entry->rnext)
);

DEFINE_EVENT(tcp_ao_event_sk, tcp_ao_synack_no_key,
	TP_PROTO(const struct sock *sk, const __u8 keyid, const __u8 rnext),
	TP_ARGS(sk, keyid, rnext)
);

DECLARE_EVENT_CLASS(tcp_ao_event_sne,

	TP_PROTO(const struct sock *sk, __u32 new_sne),

	TP_ARGS(sk, new_sne),

	TP_STRUCT__entry(
		__field(__u64, net_cookie)
		__field(const void *, skaddr)
		__field(int, state)

		/* sockaddr_in6 is always bigger than sockaddr_in */
		__array(__u8, saddr, sizeof(struct sockaddr_in6))
		__array(__u8, daddr, sizeof(struct sockaddr_in6))

		__field(__u16, sport)
		__field(__u16, dport)
		__field(__u16, family)

		__field(__u32, new_sne)
	),

	TP_fast_assign(
		const struct inet_sock *inet = inet_sk(sk);

		__entry->net_cookie = sock_net(sk)->net_cookie;
		__entry->skaddr = sk;
		__entry->state = sk->sk_state;

		memset(__entry->saddr, 0, sizeof(struct sockaddr_in6));
		memset(__entry->daddr, 0, sizeof(struct sockaddr_in6));
		TP_STORE_ADDR_PORTS(__entry, inet, sk);

		/* For filtering use */
		__entry->sport = ntohs(inet->inet_sport);
		__entry->dport = ntohs(inet->inet_dport);
		__entry->family = sk->sk_family;

		__entry->new_sne = new_sne;
	),

	TP_printk("net=%llu state=%s family=%s src=%pISpc dest=%pISpc sne=%u",
		  __entry->net_cookie,
		  show_tcp_state_name(__entry->state),
		  show_family_name(__entry->family),
		  __entry->saddr, __entry->daddr,
		  __entry->new_sne)
);

DEFINE_EVENT(tcp_ao_event_sne, tcp_ao_snd_sne_update,
	TP_PROTO(const struct sock *sk, __u32 new_sne),
	TP_ARGS(sk, new_sne)
);

DEFINE_EVENT(tcp_ao_event_sne, tcp_ao_rcv_sne_update,
	TP_PROTO(const struct sock *sk, __u32 new_sne),
	TP_ARGS(sk, new_sne)
);

#endif /* _TRACE_TCP_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
