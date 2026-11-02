// SPDX-License-Identifier: GPL-2.0
/* Copyright 2026 Google LLC */
#include "vmlinux.h"

#include <string.h>
#include <limits.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

#include "bpf_tracing_net.h"

#define SOL_BPF			0xdeadbeef
#define BPF_TCP_AUTOLOWAT	0x8badf00d

//#define DEBUG /* For verbose output. */

struct rpc_descriptor {
	u32 header_len;
	u32 payload_len;
};

#define RPC_DESC_SIZE		(sizeof(struct rpc_descriptor))
#define MAX_RPC_DESC_PER_SKB	100

struct tcp_autolowat_cb {
	/* Don't put this field at the end; BPF verifier complains. */
	char rpc_desc_buf[RPC_DESC_SIZE];
	u32 rpc_desc_seq;
	u32 rpc_end_seq;
#ifdef DEBUG
	u32 isn;
#endif
	u8 rpc_desc_buff_len;
};

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct tcp_autolowat_cb);
} tcp_autolowat_map SEC(".maps");

char test_name[64];

#ifdef DEBUG
#define LOG(str, ...)							\
	bpf_printk("%s: " str, test_name, ##__VA_ARGS__)
#else
#define LOG(...)
#endif

#define SEQ(val)				\
	(val - cb->isn)
#define TP_SEQ(field)				\
	(tp->field - cb->isn)
#define CB_SEQ(field)				\
	(cb->field - cb->isn)

static int tcp_parse_descriptor(struct tcp_autolowat_cb *cb,
				struct bpf_dynptr *dptr,
				u32 seq, u32 end_seq)
{
	struct rpc_descriptor *rpc_desc;
	u32 rpc_copied_seq;
	u64 copy_len; /* u32 should work, but not for no_alu32 :/ */
	u64 rpc_len;
	int err;

	rpc_copied_seq = cb->rpc_desc_seq + cb->rpc_desc_buff_len;

	if (before(cb->rpc_desc_seq + RPC_DESC_SIZE, end_seq))
		copy_len = RPC_DESC_SIZE - cb->rpc_desc_buff_len;
	else
		copy_len = end_seq - rpc_copied_seq;

	if (copy_len == 0)
		goto disable; /* FIN. */
	if (copy_len > RPC_DESC_SIZE)
		goto disable; /* always false, only for verifier. */
	if (cb->rpc_desc_buff_len >= RPC_DESC_SIZE)
		goto disable; /* always false, only for verifier. */

	err = bpf_dynptr_read(cb->rpc_desc_buf + cb->rpc_desc_buff_len,
			      copy_len, dptr, rpc_copied_seq - seq, 0);
	if (err)
		goto disable;

	cb->rpc_desc_buff_len += copy_len;

	if (cb->rpc_desc_buff_len != RPC_DESC_SIZE) {
		LOG("Copied %d bytes: rpc_desc_buff_len: %u", copy_len, cb->rpc_desc_buff_len);
		goto partial;
	}

	rpc_desc = (struct rpc_descriptor *)cb->rpc_desc_buf;
	rpc_len = RPC_DESC_SIZE + rpc_desc->header_len + rpc_desc->payload_len;

	if (rpc_len > INT_MAX)
		goto disable;

	cb->rpc_end_seq = cb->rpc_desc_seq + rpc_len;

	LOG("Copied full descriptor: rpc_desc_seq: %u, rpc_end_seq: %u, header_len: %u, payload_len: %u",
	    CB_SEQ(rpc_desc_seq), CB_SEQ(rpc_end_seq),
	    rpc_desc->header_len, rpc_desc->payload_len);

	return 0;
disable:
	return -1;
partial:
	return 1;
}

static void tcp_set_autolowat(struct tcp_autolowat_cb *cb,
			      struct sock *sk)
{
	struct tcp_sock *tp = (struct tcp_sock *)sk;
	u32 val; /* To handle wraparound. */

	LOG("Setting rcvlowat: tp->copied_seq: %u, rpc_desc_seq: %u, rpc_end_seq: %u, rpc_desc_buff_len: %u",
	    TP_SEQ(copied_seq), CB_SEQ(rpc_desc_seq),
	    CB_SEQ(rpc_end_seq), cb->rpc_desc_buff_len);

	if (before(tp->copied_seq, cb->rpc_desc_seq))
		val = cb->rpc_desc_seq - tp->copied_seq;
	else if (cb->rpc_desc_buff_len != RPC_DESC_SIZE)
		val = RPC_DESC_SIZE;
	else
		val = cb->rpc_end_seq - tp->copied_seq;

	if (val != tp->inet_conn.icsk_inet.sk.sk_rcvlowat) {
		bpf_tcp_ops_set_rcvlowat(sk, val);

		LOG("Set rcvlowat: expected: %u, actual: %d\n",
		    val, tp->inet_conn.icsk_inet.sk.sk_rcvlowat);
	} else {
		LOG("No need to set rcvlowat: %u\n", val);
	}
}

static void tcp_disable_autolowat(struct sock *sk)
{
	bpf_tcp_ops_set_flags((struct tcp_sock *)sk, 0, BPF_TCP_OPS_FLAG_RCVQ);

	bpf_tcp_ops_set_rcvlowat(sk, 1);

	LOG("Disabled autolowat");
}

static void tcp_do_autolowat(struct tcp_autolowat_cb *cb,
			     struct sock *sk, struct sk_buff *skb)
{
	struct bpf_dynptr dptr;
	struct tcp_skb_cb *tcb;
	u32 seq, end_seq;
	int ret = 0, i;

	if (bpf_dynptr_from_skb((struct __sk_buff *)skb, 0, &dptr)) {
		ret = -1;
		goto update;
	}

	tcb = bpf_core_cast(skb->cb, struct tcp_skb_cb);
	seq = tcb->seq;
	end_seq = tcb->end_seq - !!(tcb->tcp_flags & TCPHDR_FIN);

	LOG("Start parsing skb: seq: %u, end_seq: %u, len: %u, rpc_desc_seq: %u, rpc_end_seq: %u, rpc_buff_len: %u",
	    SEQ(seq), SEQ(end_seq), end_seq - seq,
	    CB_SEQ(rpc_desc_seq), CB_SEQ(rpc_end_seq), cb->rpc_desc_buff_len);

	if (cb->rpc_desc_buff_len != RPC_DESC_SIZE) {
		ret = tcp_parse_descriptor(cb, &dptr, seq, end_seq);
		if (ret)
			goto update;
	}

	i = 0;

	while (1) {
		if (i++ > MAX_RPC_DESC_PER_SKB) {
			ret = -1;
			break;
		}

		if (after(cb->rpc_end_seq, end_seq)) {
			LOG("No more descriptor: rpc_end_seq: %u, end_seq: %u",
			    CB_SEQ(rpc_end_seq), SEQ(end_seq));
			break;
		}

		cb->rpc_desc_seq = cb->rpc_end_seq;
		cb->rpc_desc_buff_len = 0;

		if (cb->rpc_end_seq == end_seq)
			break;

		LOG("Found next descriptor: rpc_end_seq: %u, end_seq: %u, len: %u",
		    CB_SEQ(rpc_end_seq), SEQ(end_seq), end_seq - cb->rpc_end_seq);

		ret = tcp_parse_descriptor(cb, &dptr, seq, end_seq);
		if (ret)
			break;
	}

update:
	if (ret >= 0)
		tcp_set_autolowat(cb, sk);
	else
		tcp_disable_autolowat(sk);
}

SEC("struct_ops")
void BPF_PROG(tcp_autolowat_enqueue_rcvq, struct sock *sk, struct sk_buff *skb)
{
	struct tcp_autolowat_cb *cb;

	cb = bpf_sk_storage_get(&tcp_autolowat_map, sk, 0, 0);
	if (!cb)
		return;

	tcp_do_autolowat(cb, sk, skb);
}

SEC("struct_ops")
void BPF_PROG(tcp_autolowat_dequeue_rcvq, struct sock *sk)
{
	struct tcp_autolowat_cb *cb;

	cb = bpf_sk_storage_get(&tcp_autolowat_map, sk, 0, 0);
	if (!cb)
		return;

	tcp_set_autolowat(cb, sk);
}

SEC(".struct_ops.link")
struct bpf_tcp_ops tcp_autolowat_ops = {
	.enqueue_rcvq = (void *)tcp_autolowat_enqueue_rcvq,
	.dequeue_rcvq = (void *)tcp_autolowat_dequeue_rcvq,
};

static int tcp_init_autolowat_cb(struct bpf_tcp_sock *btp)
{
	struct tcp_autolowat_cb *cb;
	struct tcp_sock *tp;

	cb = bpf_sk_storage_get(&tcp_autolowat_map, btp, 0,
				BPF_SK_STORAGE_GET_F_CREATE);
	if (!cb)
		return -1;

	tp = bpf_core_cast(btp, struct tcp_sock);

	cb->rpc_desc_seq = tp->copied_seq;
	cb->rpc_end_seq = tp->copied_seq;
#ifdef DEBUG
	cb->isn = tp->copied_seq;
#endif

	return bpf_tcp_ops_set_flags((struct tcp_sock *)btp,
				     BPF_TCP_OPS_FLAG_RCVQ, 0);
}

SEC("cgroup/setsockopt")
int tcp_autolowat_setsockopt(struct bpf_sockopt *ctx)
{
	void *optval_end = ctx->optval_end;
	int *optval = ctx->optval;
	struct bpf_tcp_sock *btp;

	if (ctx->level != SOL_BPF || ctx->optname != BPF_TCP_AUTOLOWAT)
		goto out;

	if (optval + 1 > optval_end)
		return 0; /* -EPERM */

	btp = bpf_tcp_sock(ctx->sk);
	if (!btp)
		goto out;

	if (*optval && tcp_init_autolowat_cb(btp))
		return 0; /* -EPERM */

	/*
	 * BPF has consumed this option, don't call kernel
	 * setsockopt handler.
	 */
	ctx->optlen = -1;
out:
	return 1;
}

char _license[] SEC("license") = "GPL";
