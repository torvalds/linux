// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

__u32 set_pid = 0;
__u64 set_key = 0;
__u64 set_value = 0;

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 2);
	__type(key, __u64);
	__type(value, __u64);
} hash_map SEC(".maps");

SEC("tp/syscalls/sys_enter_getpgid")
int bpf_lookup_and_delete_test(const void *ctx)
{
	if (set_pid == bpf_get_current_pid_tgid() >> 32)
		bpf_map_update_elem(&hash_map, &set_key, &set_value, BPF_NOEXIST);

	return 0;
}

char _license[] SEC("license") = "GPL";
