// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} array_map SEC(".maps");

static __u64
check_array_elem(struct bpf_map *map, __u32 *key, __u64 *val,
		 void *data)
{
	bpf_get_current_comm(key, sizeof(*key));
	return 0;
}

SEC("raw_tp/sys_enter")
int test_map_key_write(const void *ctx)
{
	bpf_for_each_map_elem(&array_map, check_array_elem, NULL, 0);
	return 0;
}

char _license[] SEC("license") = "GPL";
