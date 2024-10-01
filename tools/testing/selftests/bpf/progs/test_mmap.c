// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook

#include <linux/bpf.h>
#include <stdint.h>
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(map_flags, BPF_F_MMAPABLE | BPF_F_RDONLY_PROG);
	__type(key, __u32);
	__type(value, char);
} rdonly_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(map_flags, BPF_F_MMAPABLE);
	__type(key, __u32);
	__type(value, __u64);
} data_map SEC(".maps");

__u64 in_val = 0;
__u64 out_val = 0;

SEC("raw_tracepoint/sys_enter")
int test_mmap(void *ctx)
{
	int zero = 0, one = 1, two = 2, far = 1500;
	__u64 val, *p;

	out_val = in_val;

	/* data_map[2] = in_val; */
	bpf_map_update_elem(&data_map, &two, (const void *)&in_val, 0);

	/* data_map[1] = data_map[0] * 2; */
	p = bpf_map_lookup_elem(&data_map, &zero);
	if (p) {
		val = (*p) * 2;
		bpf_map_update_elem(&data_map, &one, &val, 0);
	}

	/* data_map[far] = in_val * 3; */
	val = in_val * 3;
	bpf_map_update_elem(&data_map, &far, &val, 0);

	return 0;
}

