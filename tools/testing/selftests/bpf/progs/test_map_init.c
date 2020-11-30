// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Tessares SA <http://www.tessares.net> */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

__u64 inKey = 0;
__u64 inValue = 0;
__u32 inPid = 0;

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_HASH);
	__uint(max_entries, 2);
	__type(key, __u64);
	__type(value, __u64);
} hashmap1 SEC(".maps");


SEC("tp/syscalls/sys_enter_getpgid")
int sysenter_getpgid(const void *ctx)
{
	/* Just do it for once, when called from our own test prog. This
	 * ensures the map value is only updated for a single CPU.
	 */
	int cur_pid = bpf_get_current_pid_tgid() >> 32;

	if (cur_pid == inPid)
		bpf_map_update_elem(&hashmap1, &inKey, &inValue, BPF_NOEXIST);

	return 0;
}

char _license[] SEC("license") = "GPL";
