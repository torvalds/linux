// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 64);
	__type(key, __u32);
	__type(value, __u64);
} lru_map SEC(".maps");

int hits;

SEC("perf_event")
int oncpu(void *ctx)
{
	/*
	 * Key range deliberately wider than max_entries to force LRU
	 * eviction on every other update.
	 */
	__u32 key = bpf_get_prandom_u32() % 128;
	bool do_update = bpf_get_prandom_u32() & 1;
	__u64 val = 1;

	if (do_update)
		bpf_map_update_elem(&lru_map, &key, &val, BPF_ANY);
	else
		bpf_map_delete_elem(&lru_map, &key);
	__sync_fetch_and_add(&hits, 1);
	return 0;
}

char _license[] SEC("license") = "GPL";
