/* Copyright 2016 Netflix, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <linux/version.h>
#include <linux/ptrace.h>
#include <uapi/linux/bpf.h>
#include <uapi/linux/bpf_perf_event.h>
#include "bpf_helpers.h"

#define MAX_IPS		8192

struct bpf_map_def SEC("maps") ip_map = {
	.type = BPF_MAP_TYPE_HASH,
	.key_size = sizeof(u64),
	.value_size = sizeof(u32),
	.max_entries = MAX_IPS,
};

SEC("perf_event")
int do_sample(struct bpf_perf_event_data *ctx)
{
	u64 ip;
	u32 *value, init_val = 1;

	ip = ctx->regs.ip;
	value = bpf_map_lookup_elem(&ip_map, &ip);
	if (value)
		*value += 1;
	else
		/* E2BIG not tested for this example only */
		bpf_map_update_elem(&ip_map, &ip, &init_val, BPF_NOEXIST);

	return 0;
}
char _license[] SEC("license") = "GPL";
