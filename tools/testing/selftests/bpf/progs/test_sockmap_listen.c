// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Cloudflare

#include <errno.h>
#include <stdbool.h>
#include <linux/bpf.h>

#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_SOCKMAP);
	__uint(max_entries, 2);
	__type(key, __u32);
	__type(value, __u64);
} sock_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_SOCKHASH);
	__uint(max_entries, 2);
	__type(key, __u32);
	__type(value, __u64);
} sock_hash SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 2);
	__type(key, int);
	__type(value, unsigned int);
} verdict_map SEC(".maps");

bool test_sockmap = false; /* toggled by user-space */
bool test_ingress = false; /* toggled by user-space */

SEC("sk_skb/stream_parser")
int prog_stream_parser(struct __sk_buff *skb)
{
	return skb->len;
}

SEC("sk_skb/stream_verdict")
int prog_stream_verdict(struct __sk_buff *skb)
{
	unsigned int *count;
	__u32 zero = 0;
	int verdict;

	if (test_sockmap)
		verdict = bpf_sk_redirect_map(skb, &sock_map, zero, 0);
	else
		verdict = bpf_sk_redirect_hash(skb, &sock_hash, &zero, 0);

	count = bpf_map_lookup_elem(&verdict_map, &verdict);
	if (count)
		(*count)++;

	return verdict;
}

SEC("sk_skb")
int prog_skb_verdict(struct __sk_buff *skb)
{
	unsigned int *count;
	__u32 zero = 0;
	int verdict;

	if (test_sockmap)
		verdict = bpf_sk_redirect_map(skb, &sock_map, zero,
					      test_ingress ? BPF_F_INGRESS : 0);
	else
		verdict = bpf_sk_redirect_hash(skb, &sock_hash, &zero,
					       test_ingress ? BPF_F_INGRESS : 0);

	count = bpf_map_lookup_elem(&verdict_map, &verdict);
	if (count)
		(*count)++;

	return verdict;
}

SEC("sk_msg")
int prog_msg_verdict(struct sk_msg_md *msg)
{
	unsigned int *count;
	__u32 zero = 0;
	int verdict;

	if (test_sockmap)
		verdict = bpf_msg_redirect_map(msg, &sock_map, zero, 0);
	else
		verdict = bpf_msg_redirect_hash(msg, &sock_hash, &zero, 0);

	count = bpf_map_lookup_elem(&verdict_map, &verdict);
	if (count)
		(*count)++;

	return verdict;
}

SEC("sk_reuseport")
int prog_reuseport(struct sk_reuseport_md *reuse)
{
	unsigned int *count;
	int err, verdict;
	__u32 zero = 0;

	if (test_sockmap)
		err = bpf_sk_select_reuseport(reuse, &sock_map, &zero, 0);
	else
		err = bpf_sk_select_reuseport(reuse, &sock_hash, &zero, 0);
	verdict = err ? SK_DROP : SK_PASS;

	count = bpf_map_lookup_elem(&verdict_map, &verdict);
	if (count)
		(*count)++;

	return verdict;
}

int _version SEC("version") = 1;
char _license[] SEC("license") = "GPL";
