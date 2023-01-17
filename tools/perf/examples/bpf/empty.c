// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct syscall_enter_args;

SEC("raw_syscalls:sys_enter")
int sys_enter(struct syscall_enter_args *args)
{
	return 0;
}
char _license[] SEC("license") = "GPL";
