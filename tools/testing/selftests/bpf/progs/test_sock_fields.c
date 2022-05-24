// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */

#include <linux/bpf.h>
#include <netinet/in.h>
#include <stdbool.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include "bpf_tcp_helpers.h"

enum bpf_linum_array_idx {
	EGRESS_LINUM_IDX,
	INGRESS_LINUM_IDX,
	__NR_BPF_LINUM_ARRAY_IDX,
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, __NR_BPF_LINUM_ARRAY_IDX);
	__type(key, __u32);
	__type(value, __u32);
} linum_map SEC(".maps");

struct bpf_spinlock_cnt {
	struct bpf_spin_lock lock;
	__u32 cnt;
};

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct bpf_spinlock_cnt);
} sk_pkt_out_cnt SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct bpf_spinlock_cnt);
} sk_pkt_out_cnt10 SEC(".maps");

struct bpf_tcp_sock listen_tp = {};
struct sockaddr_in6 srv_sa6 = {};
struct bpf_tcp_sock cli_tp = {};
struct bpf_tcp_sock srv_tp = {};
struct bpf_sock listen_sk = {};
struct bpf_sock srv_sk = {};
struct bpf_sock cli_sk = {};
__u64 parent_cg_id = 0;
__u64 child_cg_id = 0;
__u64 lsndtime = 0;

static bool is_loopback6(__u32 *a6)
{
	return !a6[0] && !a6[1] && !a6[2] && a6[3] == bpf_htonl(1);
}

static void skcpy(struct bpf_sock *dst,
		  const struct bpf_sock *src)
{
	dst->bound_dev_if = src->bound_dev_if;
	dst->family = src->family;
	dst->type = src->type;
	dst->protocol = src->protocol;
	dst->mark = src->mark;
	dst->priority = src->priority;
	dst->src_ip4 = src->src_ip4;
	dst->src_ip6[0] = src->src_ip6[0];
	dst->src_ip6[1] = src->src_ip6[1];
	dst->src_ip6[2] = src->src_ip6[2];
	dst->src_ip6[3] = src->src_ip6[3];
	dst->src_port = src->src_port;
	dst->dst_ip4 = src->dst_ip4;
	dst->dst_ip6[0] = src->dst_ip6[0];
	dst->dst_ip6[1] = src->dst_ip6[1];
	dst->dst_ip6[2] = src->dst_ip6[2];
	dst->dst_ip6[3] = src->dst_ip6[3];
	dst->dst_port = src->dst_port;
	dst->state = src->state;
}

static void tpcpy(struct bpf_tcp_sock *dst,
		  const struct bpf_tcp_sock *src)
{
	dst->snd_cwnd = src->snd_cwnd;
	dst->srtt_us = src->srtt_us;
	dst->rtt_min = src->rtt_min;
	dst->snd_ssthresh = src->snd_ssthresh;
	dst->rcv_nxt = src->rcv_nxt;
	dst->snd_nxt = src->snd_nxt;
	dst->snd_una = src->snd_una;
	dst->mss_cache = src->mss_cache;
	dst->ecn_flags = src->ecn_flags;
	dst->rate_delivered = src->rate_delivered;
	dst->rate_interval_us = src->rate_interval_us;
	dst->packets_out = src->packets_out;
	dst->retrans_out = src->retrans_out;
	dst->total_retrans = src->total_retrans;
	dst->segs_in = src->segs_in;
	dst->data_segs_in = src->data_segs_in;
	dst->segs_out = src->segs_out;
	dst->data_segs_out = src->data_segs_out;
	dst->lost_out = src->lost_out;
	dst->sacked_out = src->sacked_out;
	dst->bytes_received = src->bytes_received;
	dst->bytes_acked = src->bytes_acked;
}

/* Always return CG_OK so that no pkt will be filtered out */
#define CG_OK 1

#define RET_LOG() ({						\
	linum = __LINE__;					\
	bpf_map_update_elem(&linum_map, &linum_idx, &linum, BPF_ANY);	\
	return CG_OK;						\
})

SEC("cgroup_skb/egress")
int egress_read_sock_fields(struct __sk_buff *skb)
{
	struct bpf_spinlock_cnt cli_cnt_init = { .lock = 0, .cnt = 0xeB9F };
	struct bpf_spinlock_cnt *pkt_out_cnt, *pkt_out_cnt10;
	struct bpf_tcp_sock *tp, *tp_ret;
	struct bpf_sock *sk, *sk_ret;
	__u32 linum, linum_idx;
	struct tcp_sock *ktp;

	linum_idx = EGRESS_LINUM_IDX;

	sk = skb->sk;
	if (!sk)
		RET_LOG();

	/* Not the testing egress traffic or
	 * TCP_LISTEN (10) socket will be copied at the ingress side.
	 */
	if (sk->family != AF_INET6 || !is_loopback6(sk->src_ip6) ||
	    sk->state == 10)
		return CG_OK;

	if (sk->src_port == bpf_ntohs(srv_sa6.sin6_port)) {
		/* Server socket */
		sk_ret = &srv_sk;
		tp_ret = &srv_tp;
	} else if (sk->dst_port == srv_sa6.sin6_port) {
		/* Client socket */
		sk_ret = &cli_sk;
		tp_ret = &cli_tp;
	} else {
		/* Not the testing egress traffic */
		return CG_OK;
	}

	/* It must be a fullsock for cgroup_skb/egress prog */
	sk = bpf_sk_fullsock(sk);
	if (!sk)
		RET_LOG();

	/* Not the testing egress traffic */
	if (sk->protocol != IPPROTO_TCP)
		return CG_OK;

	tp = bpf_tcp_sock(sk);
	if (!tp)
		RET_LOG();

	skcpy(sk_ret, sk);
	tpcpy(tp_ret, tp);

	if (sk_ret == &srv_sk) {
		ktp = bpf_skc_to_tcp_sock(sk);

		if (!ktp)
			RET_LOG();

		lsndtime = ktp->lsndtime;

		child_cg_id = bpf_sk_cgroup_id(ktp);
		if (!child_cg_id)
			RET_LOG();

		parent_cg_id = bpf_sk_ancestor_cgroup_id(ktp, 2);
		if (!parent_cg_id)
			RET_LOG();

		/* The userspace has created it for srv sk */
		pkt_out_cnt = bpf_sk_storage_get(&sk_pkt_out_cnt, ktp, 0, 0);
		pkt_out_cnt10 = bpf_sk_storage_get(&sk_pkt_out_cnt10, ktp,
						   0, 0);
	} else {
		pkt_out_cnt = bpf_sk_storage_get(&sk_pkt_out_cnt, sk,
						 &cli_cnt_init,
						 BPF_SK_STORAGE_GET_F_CREATE);
		pkt_out_cnt10 = bpf_sk_storage_get(&sk_pkt_out_cnt10,
						   sk, &cli_cnt_init,
						   BPF_SK_STORAGE_GET_F_CREATE);
	}

	if (!pkt_out_cnt || !pkt_out_cnt10)
		RET_LOG();

	/* Even both cnt and cnt10 have lock defined in their BTF,
	 * intentionally one cnt takes lock while one does not
	 * as a test for the spinlock support in BPF_MAP_TYPE_SK_STORAGE.
	 */
	pkt_out_cnt->cnt += 1;
	bpf_spin_lock(&pkt_out_cnt10->lock);
	pkt_out_cnt10->cnt += 10;
	bpf_spin_unlock(&pkt_out_cnt10->lock);

	return CG_OK;
}

SEC("cgroup_skb/ingress")
int ingress_read_sock_fields(struct __sk_buff *skb)
{
	struct bpf_tcp_sock *tp;
	__u32 linum, linum_idx;
	struct bpf_sock *sk;

	linum_idx = INGRESS_LINUM_IDX;

	sk = skb->sk;
	if (!sk)
		RET_LOG();

	/* Not the testing ingress traffic to the server */
	if (sk->family != AF_INET6 || !is_loopback6(sk->src_ip6) ||
	    sk->src_port != bpf_ntohs(srv_sa6.sin6_port))
		return CG_OK;

	/* Only interested in TCP_LISTEN */
	if (sk->state != 10)
		return CG_OK;

	/* It must be a fullsock for cgroup_skb/ingress prog */
	sk = bpf_sk_fullsock(sk);
	if (!sk)
		RET_LOG();

	tp = bpf_tcp_sock(sk);
	if (!tp)
		RET_LOG();

	skcpy(&listen_sk, sk);
	tpcpy(&listen_tp, tp);

	return CG_OK;
}

char _license[] SEC("license") = "GPL";
