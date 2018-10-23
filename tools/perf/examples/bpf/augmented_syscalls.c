// SPDX-License-Identifier: GPL-2.0
/*
 * Augment the openat syscall with the contents of the filename pointer argument.
 *
 * Test it with:
 *
 * perf trace -e tools/perf/examples/bpf/augmented_syscalls.c cat /etc/passwd > /dev/null
 *
 * It'll catch some openat syscalls related to the dynamic linked and
 * the last one should be the one for '/etc/passwd'.
 *
 * This matches what is marshalled into the raw_syscall:sys_enter payload
 * expected by the 'perf trace' beautifiers, and can be used by them unmodified,
 * which will be done as that feature is implemented in the next csets, for now
 * it will appear in a dump done by the default tracepoint handler in 'perf trace',
 * that uses bpf_output__fprintf() to just dump those contents, as done with
 * the bpf-output event associated with the __bpf_output__ map declared in
 * tools/perf/include/bpf/stdio.h.
 */

#include <stdio.h>

struct bpf_map SEC("maps") __augmented_syscalls__ = {
       .type = BPF_MAP_TYPE_PERF_EVENT_ARRAY,
       .key_size = sizeof(int),
       .value_size = sizeof(u32),
       .max_entries = __NR_CPUS__,
};

struct syscall_enter_openat_args {
	unsigned long long common_tp_fields;
	long		   syscall_nr;
	long		   dfd;
	char		   *filename_ptr;
	long		   flags;
	long		   mode;
};

struct augmented_enter_openat_args {
	struct syscall_enter_openat_args args;
	char				 filename[64];
};

int syscall_enter(openat)(struct syscall_enter_openat_args *args)
{
	struct augmented_enter_openat_args augmented_args;

	probe_read(&augmented_args.args, sizeof(augmented_args.args), args);
	probe_read_str(&augmented_args.filename, sizeof(augmented_args.filename), args->filename_ptr);
	perf_event_output(args, &__augmented_syscalls__, BPF_F_CURRENT_CPU,
			  &augmented_args, sizeof(augmented_args));
	return 1;
}

license(GPL);
