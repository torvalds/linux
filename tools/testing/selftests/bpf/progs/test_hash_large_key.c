// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 2);
	__type(key, struct bigelement);
	__type(value, __u32);
} hash_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct bigelement);
} key_map SEC(".maps");

struct bigelement {
	int a;
	char b[4096];
	long long c;
};

SEC("raw_tracepoint/sys_enter")
int bpf_hash_large_key_test(void *ctx)
{
	int zero = 0, err = 1, value = 42;
	struct bigelement *key;

	key = bpf_map_lookup_elem(&key_map, &zero);
	if (!key)
		return 0;

	key->c = 1;
	if (bpf_map_update_elem(&hash_map, key, &value, BPF_ANY))
		return 0;

	return 0;
}

