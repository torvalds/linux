// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

struct timer_val {
	struct bpf_timer timer;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, __u32);
	__type(value, struct timer_val);
	__uint(max_entries, 1);
} timer_prealloc SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, __u32);
	__type(value, struct timer_val);
	__uint(max_entries, 1);
	__uint(map_flags, BPF_F_NO_PREALLOC);
} timer_no_prealloc SEC(".maps");
