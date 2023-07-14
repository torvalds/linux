// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct __bpf_stdout__ {
	__uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
	__type(key, int);
	__type(value, __u32);
	__uint(max_entries, __NR_CPUS__);
} __bpf_stdout__ SEC(".maps");

#define puts(from) \
	({ const int __len = sizeof(from); \
	   char __from[sizeof(from)] = from;			\
	   bpf_perf_event_output(args, &__bpf_stdout__, BPF_F_CURRENT_CPU, \
			  &__from, __len & (sizeof(from) - 1)); })

struct syscall_enter_args;

SEC("raw_syscalls:sys_enter")
int sys_enter(struct syscall_enter_args *args)
{
	puts("Hello, world\n");
	return 0;
}

char _license[] SEC("license") = "GPL";
