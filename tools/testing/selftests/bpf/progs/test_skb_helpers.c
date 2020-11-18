// SPDX-License-Identifier: GPL-2.0-only
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define TEST_COMM_LEN 16

struct {
	__uint(type, BPF_MAP_TYPE_CGROUP_ARRAY);
	__uint(max_entries, 1);
	__type(key, u32);
	__type(value, u32);
} cgroup_map SEC(".maps");

char _license[] SEC("license") = "GPL";

SEC("classifier/test_skb_helpers")
int test_skb_helpers(struct __sk_buff *skb)
{
	struct task_struct *task;
	char comm[TEST_COMM_LEN];
	__u32 tpid;

	task = (struct task_struct *)bpf_get_current_task();
	bpf_probe_read_kernel(&tpid , sizeof(tpid), &task->tgid);
	bpf_probe_read_kernel_str(&comm, sizeof(comm), &task->comm);
	return 0;
}
