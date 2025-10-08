// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 ByteDance */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

#ifndef PAGE_SIZE
#define PAGE_SIZE __PAGE_SIZE
#endif
#define BPF_SKB_MAX_LEN (PAGE_SIZE << 2)

struct {
	__uint(type, BPF_MAP_TYPE_SOCKMAP);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, int);
} sock_map_rx SEC(".maps");

long change_tail_ret = 1;

SEC("sk_skb")
int prog_skb_verdict(struct __sk_buff *skb)
{
	char *data, *data_end;

	bpf_skb_pull_data(skb, 1);
	data = (char *)(unsigned long)skb->data;
	data_end = (char *)(unsigned long)skb->data_end;

	if (data + 1 > data_end)
		return SK_PASS;

	if (data[0] == 'T') { /* Trim the packet */
		change_tail_ret = bpf_skb_change_tail(skb, skb->len - 1, 0);
		return SK_PASS;
	} else if (data[0] == 'G') { /* Grow the packet */
		change_tail_ret = bpf_skb_change_tail(skb, skb->len + 1, 0);
		return SK_PASS;
	} else if (data[0] == 'E') { /* Error */
		change_tail_ret = bpf_skb_change_tail(skb, BPF_SKB_MAX_LEN, 0);
		return SK_PASS;
	}
	return SK_PASS;
}

char _license[] SEC("license") = "GPL";
