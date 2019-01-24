// SPDX-License-Identifier: GPL-2.0
/*
 * Augment the filename syscalls with the contents of the filename pointer argument
 * filtering only those that do not start with /etc/.
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

/* bpf-output associated map */
bpf_map(__augmented_syscalls__, PERF_EVENT_ARRAY, int, u32, __NR_CPUS__);

struct augmented_filename {
	int	size;
	int	reserved;
	char	value[64];
};

#define augmented_filename_syscall_enter(syscall) 						\
struct augmented_enter_##syscall##_args {			 				\
	struct syscall_enter_##syscall##_args	args;				 		\
	struct augmented_filename		filename;				 	\
};												\
int syscall_enter(syscall)(struct syscall_enter_##syscall##_args *args)				\
{												\
	char etc[6] = "/etc/";									\
	struct augmented_enter_##syscall##_args augmented_args = { .filename.reserved = 0, }; 	\
	probe_read(&augmented_args.args, sizeof(augmented_args.args), args);			\
	augmented_args.filename.size = probe_read_str(&augmented_args.filename.value, 		\
						      sizeof(augmented_args.filename.value), 	\
						      args->filename_ptr); 			\
	if (__builtin_memcmp(augmented_args.filename.value, etc, 4) != 0)			\
		return 0;									\
	/* If perf_event_output fails, return non-zero so that it gets recorded unaugmented */	\
	return perf_event_output(args, &__augmented_syscalls__, BPF_F_CURRENT_CPU, 		\
				 &augmented_args,						\
				 (sizeof(augmented_args) - sizeof(augmented_args.filename.value) + \
				 augmented_args.filename.size));				\
}

struct syscall_enter_openat_args {
	unsigned long long common_tp_fields;
	long		   syscall_nr;
	long		   dfd;
	char		   *filename_ptr;
	long		   flags;
	long		   mode;
};

augmented_filename_syscall_enter(openat);

struct syscall_enter_open_args {
	unsigned long long common_tp_fields;
	long		   syscall_nr;
	char		   *filename_ptr;
	long		   flags;
	long		   mode;
};

augmented_filename_syscall_enter(open);

license(GPL);
