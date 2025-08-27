// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#define MAX_ENTRIES 100000000

struct trie_key {
	__u32 prefixlen;
	__u32 data;
};

struct {
	__uint(type, BPF_MAP_TYPE_LPM_TRIE);
	__type(key, struct trie_key);
	__type(value, __u32);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__uint(max_entries, MAX_ENTRIES);
} trie_free_map SEC(".maps");
