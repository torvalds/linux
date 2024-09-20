// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */

#include "bpf_tracing_net.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#ifndef ENOENT
#define ENOENT 2
#endif

struct sockaddr_in6 srv_sa6 = {};
__u16 listen_tp_sport = 0;
__u16 req_sk_sport = 0;
__u32 recv_cookie = 0;
__u32 gen_cookie = 0;
__u32 linum = 0;

#define LOG() ({ if (!linum) linum = __LINE__; })

static void test_syncookie_helper(struct ipv6hdr *ip6h, struct tcphdr *th,
				  struct tcp_sock *tp,
				  struct __sk_buff *skb)
{
	if (th->syn) {
		__s64 mss_cookie;
		void *data_end;

		data_end = (void *)(long)(skb->data_end);

		if (th->doff * 4 != 40) {
			LOG();
			return;
		}

		if ((void *)th + 40 > data_end) {
			LOG();
			return;
		}

		mss_cookie = bpf_tcp_gen_syncookie(tp, ip6h, sizeof(*ip6h),
						   th, 40);
		if (mss_cookie < 0) {
			if (mss_cookie != -ENOENT)
				LOG();
		} else {
			gen_cookie = (__u32)mss_cookie;
		}
	} else if (gen_cookie) {
		/* It was in cookie mode */
		int ret = bpf_tcp_check_syncookie(tp, ip6h, sizeof(*ip6h),
						  th, sizeof(*th));

		if (ret < 0) {
			if (ret != -ENOENT)
				LOG();
		} else {
			recv_cookie = bpf_ntohl(th->ack_seq) - 1;
		}
	}
}

static int handle_ip6_tcp(struct ipv6hdr *ip6h, struct __sk_buff *skb)
{
	struct bpf_sock_tuple *tuple;
	struct bpf_sock *bpf_skc;
	unsigned int tuple_len;
	struct tcphdr *th;
	void *data_end;

	data_end = (void *)(long)(skb->data_end);

	th = (struct tcphdr *)(ip6h + 1);
	if (th + 1 > data_end)
		return TC_ACT_OK;

	/* Is it the testing traffic? */
	if (th->dest != srv_sa6.sin6_port)
		return TC_ACT_OK;

	tuple_len = sizeof(tuple->ipv6);
	tuple = (struct bpf_sock_tuple *)&ip6h->saddr;
	if ((void *)tuple + tuple_len > data_end) {
		LOG();
		return TC_ACT_OK;
	}

	bpf_skc = bpf_skc_lookup_tcp(skb, tuple, tuple_len,
				     BPF_F_CURRENT_NETNS, 0);
	if (!bpf_skc) {
		LOG();
		return TC_ACT_OK;
	}

	if (bpf_skc->state == BPF_TCP_NEW_SYN_RECV) {
		struct request_sock *req_sk;

		req_sk = (struct request_sock *)bpf_skc_to_tcp_request_sock(bpf_skc);
		if (!req_sk) {
			LOG();
			goto release;
		}

		if (bpf_sk_assign(skb, req_sk, 0)) {
			LOG();
			goto release;
		}

		req_sk_sport = req_sk->__req_common.skc_num;

		bpf_sk_release(req_sk);
		return TC_ACT_OK;
	} else if (bpf_skc->state == BPF_TCP_LISTEN) {
		struct tcp_sock *tp;

		tp = bpf_skc_to_tcp_sock(bpf_skc);
		if (!tp) {
			LOG();
			goto release;
		}

		if (bpf_sk_assign(skb, tp, 0)) {
			LOG();
			goto release;
		}

		listen_tp_sport = tp->inet_conn.icsk_inet.sk.__sk_common.skc_num;

		test_syncookie_helper(ip6h, th, tp, skb);
		bpf_sk_release(tp);
		return TC_ACT_OK;
	}

	if (bpf_sk_assign(skb, bpf_skc, 0))
		LOG();

release:
	bpf_sk_release(bpf_skc);
	return TC_ACT_OK;
}

SEC("tc")
int cls_ingress(struct __sk_buff *skb)
{
	struct ipv6hdr *ip6h;
	struct ethhdr *eth;
	void *data_end;

	data_end = (void *)(long)(skb->data_end);

	eth = (struct ethhdr *)(long)(skb->data);
	if (eth + 1 > data_end)
		return TC_ACT_OK;

	if (eth->h_proto != bpf_htons(ETH_P_IPV6))
		return TC_ACT_OK;

	ip6h = (struct ipv6hdr *)(eth + 1);
	if (ip6h + 1 > data_end)
		return TC_ACT_OK;

	if (ip6h->nexthdr == IPPROTO_TCP)
		return handle_ip6_tcp(ip6h, skb);

	return TC_ACT_OK;
}

char _license[] SEC("license") = "GPL";
