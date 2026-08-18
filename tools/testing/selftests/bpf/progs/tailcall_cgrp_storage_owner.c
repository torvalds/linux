// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE);
	__type(key, struct bpf_cgroup_storage_key);
	__type(value, __u64);
} storage_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
} prog_array SEC(".maps");

SEC("cgroup_skb/egress")
int prog_array_owner(struct __sk_buff *skb)
{
	__u64 *storage;

	storage = bpf_get_local_storage(&storage_map, 0);
	if (storage)
		*storage = 1;

	bpf_tail_call(skb, &prog_array, 0);
	return 1;
}

char _license[] SEC("license") = "GPL";
