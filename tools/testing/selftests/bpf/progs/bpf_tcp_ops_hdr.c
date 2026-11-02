// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

/* Experimental option kind and payload written/parsed by this test. */
#define TEST_OPT_KIND	0xFD
#define TEST_OPT_LEN	4
#define TEST_OPT_D0	0xAB
#define TEST_OPT_D1	0xCD

int hdr_opt_len_cnt;
int write_cnt;
int parse_cnt;
int found_cnt;
__u8 found_d0;
__u8 found_d1;

SEC("struct_ops")
void BPF_PROG(test_listen, struct sock *sk)
{
	bpf_tcp_ops_set_flags((struct tcp_sock *)sk,
			      BPF_TCP_OPS_FLAG_PARSE_HDR_OPT_ALL |
			      BPF_TCP_OPS_FLAG_WRITE_HDR_OPT,
			      0);
}

SEC("struct_ops")
void BPF_PROG(test_connect, struct sock *sk)
{
	bpf_tcp_ops_set_flags((struct tcp_sock *)sk,
			      BPF_TCP_OPS_FLAG_PARSE_HDR_OPT_UNKNOWN |
			      BPF_TCP_OPS_FLAG_WRITE_HDR_OPT,
			      0);
}

SEC("struct_ops")
void BPF_PROG(test_hdr_opt_len, struct sock *sk, struct sk_buff *skb,
	      struct request_sock *req, struct sk_buff *syn_skb,
	      enum tcp_synack_type synack_type, unsigned int *remaining)
{
	hdr_opt_len_cnt++;

	/*
	 * Reserve TEST_OPT_LEN bytes; the helper decrements *remaining. Stacks
	 * with other progs in the cgroup hierarchy.
	 */
	bpf_reserve_hdr_opt(ctx, TEST_OPT_LEN, 0);
}

SEC("struct_ops")
void BPF_PROG(test_write_hdr_opt, struct sock *sk, struct sk_buff *skb,
	      struct request_sock *req, struct sk_buff *syn_skb,
	      enum tcp_synack_type synack_type, __u32 opt_off)
{
	__u8 opt[TEST_OPT_LEN] = {
		TEST_OPT_KIND, TEST_OPT_LEN, TEST_OPT_D0, TEST_OPT_D1,
	};

	/*
	 * bpf_store_hdr_opt() takes the program ctx (the kernel reads the
	 * outgoing skb from it); it appends after any options already written
	 * in the reserved window, rejects duplicates, and confines the write to
	 * the header option scratch. Stacks across progs in the cgroup hierarchy.
	 */
	if (bpf_store_hdr_opt(ctx, opt, sizeof(opt), 0))
		return;

	write_cnt++;
}

SEC("struct_ops")
void BPF_PROG(test_parse_hdr, struct sock *sk, struct sk_buff *skb)
{
	__u8 opt[TEST_OPT_LEN] = {
		TEST_OPT_KIND, TEST_OPT_LEN, TEST_OPT_D0, TEST_OPT_D1,
	};

	parse_cnt++;

	/*
	 * Look up the experimental option written by test_write_hdr_opt() in
	 * the incoming skb. For an experimental kind the search matches on the
	 * 2-byte magic in opt[2..3]; on a match the found option is copied back
	 * into opt[].
	 */
	if (bpf_load_hdr_opt(ctx, opt, sizeof(opt), 0) < 0)
		return;

	found_d0 = opt[2];
	found_d1 = opt[3];
	found_cnt++;
}

SEC(".struct_ops.link")
struct bpf_tcp_ops test_hdr_ops = {
	.listen		= (void *)test_listen,
	.connect	= (void *)test_connect,
	.hdr_opt_len	= (void *)test_hdr_opt_len,
	.write_hdr_opt	= (void *)test_write_hdr_opt,
	.parse_hdr	= (void *)test_parse_hdr,
};

char _license[] SEC("license") = "GPL";
