// SPDX-License-Identifier: GPL-2.0
/*
 * Augment the raw_syscalls tracepoints with the contents of the pointer arguments.
 *
 * Test it with:
 *
 * perf trace -e tools/perf/examples/bpf/augmented_raw_syscalls.c cat /etc/passwd > /dev/null
 *
 * This exactly matches what is marshalled into the raw_syscall:sys_enter
 * payload expected by the 'perf trace' beautifiers.
 *
 * For now it just uses the existing tracepoint augmentation code in 'perf
 * trace', in the next csets we'll hook up these with the sys_enter/sys_exit
 * code that will combine entry/exit in a strace like way.
 */

#include <stdio.h>
#include <linux/socket.h>

/* bpf-output associated map */
struct bpf_map SEC("maps") __augmented_syscalls__ = {
	.type = BPF_MAP_TYPE_PERF_EVENT_ARRAY,
	.key_size = sizeof(int),
	.value_size = sizeof(u32),
	.max_entries = __NR_CPUS__,
};

struct syscall_enter_args {
	unsigned long long common_tp_fields;
	long		   syscall_nr;
	unsigned long	   args[6];
};

struct syscall_exit_args {
	unsigned long long common_tp_fields;
	long		   syscall_nr;
	long		   ret;
};

SEC("raw_syscalls:sys_enter")
int sys_enter(struct syscall_enter_args *args)
{
	struct {
		struct syscall_enter_args args;
	} augmented_args;
	unsigned int len = sizeof(augmented_args);

	probe_read(&augmented_args.args, sizeof(augmented_args.args), args);
	perf_event_output(args, &__augmented_syscalls__, BPF_F_CURRENT_CPU, &augmented_args, len);
	return 0;
}

SEC("raw_syscalls:sys_exit")
int sys_exit(struct syscall_exit_args *args)
{
	return 1; /* 0 as soon as we start copying data returned by the kernel, e.g. 'read' */
}

license(GPL);
