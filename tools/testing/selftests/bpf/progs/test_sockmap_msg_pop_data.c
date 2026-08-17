// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_SOCKMAP);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, int);
} sock_map SEC(".maps");

#define POP_START 0x48a3
#define POP_LEN   0xfffffffd

long pop_data_ret = 1;

SEC("sk_msg")
int prog_msg_pop_data(struct sk_msg_md *msg)
{
	if (msg->size <= POP_START)
		return SK_PASS;

	pop_data_ret = bpf_msg_pop_data(msg, POP_START, POP_LEN, 0);
	return SK_PASS;
}

char _license[] SEC("license") = "GPL";
