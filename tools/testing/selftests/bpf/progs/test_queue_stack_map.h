/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (c) 2018 Politecnico di Torino
#include <stddef.h>
#include <string.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/pkt_cls.h>
#include "bpf_helpers.h"

int _version SEC("version") = 1;

struct {
	__uint(type, MAP_TYPE);
	__uint(max_entries, 32);
	__uint(map_flags, 0);
	__uint(key_size, 0);
	__uint(value_size, sizeof(__u32));
} map_in SEC(".maps");

struct {
	__uint(type, MAP_TYPE);
	__uint(max_entries, 32);
	__uint(map_flags, 0);
	__uint(key_size, 0);
	__uint(value_size, sizeof(__u32));
} map_out SEC(".maps");

SEC("test")
int _test(struct __sk_buff *skb)
{
	void *data_end = (void *)(long)skb->data_end;
	void *data = (void *)(long)skb->data;
	struct ethhdr *eth = (struct ethhdr *)(data);
	__u32 value;
	int err;

	if (eth + 1 > data_end)
		return TC_ACT_SHOT;

	struct iphdr *iph = (struct iphdr *)(eth + 1);

	if (iph + 1 > data_end)
		return TC_ACT_SHOT;

	err = bpf_map_pop_elem(&map_in, &value);
	if (err)
		return TC_ACT_SHOT;

	iph->daddr = value;

	err = bpf_map_push_elem(&map_out, &iph->saddr, 0);
	if (err)
		return TC_ACT_SHOT;

	return TC_ACT_OK;
}

char _license[] SEC("license") = "GPL";
