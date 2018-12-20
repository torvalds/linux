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

#include <unistd.h>
#include <pid_filter.h>

/* bpf-output associated map */
struct bpf_map SEC("maps") __augmented_syscalls__ = {
	.type = BPF_MAP_TYPE_PERF_EVENT_ARRAY,
	.key_size = sizeof(int),
	.value_size = sizeof(u32),
	.max_entries = __NR_CPUS__,
};

struct syscall {
	bool	enabled;
};

struct bpf_map SEC("maps") syscalls = {
	.type	     = BPF_MAP_TYPE_ARRAY,
	.key_size    = sizeof(int),
	.value_size  = sizeof(struct syscall),
	.max_entries = 512,
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

struct augmented_filename {
	unsigned int	size;
	int		reserved;
	char		value[256];
};

#define SYS_OPEN 2
#define SYS_ACCESS 21
#define SYS_OPENAT 257

pid_filter(pids_filtered);

SEC("raw_syscalls:sys_enter")
int sys_enter(struct syscall_enter_args *args)
{
	struct {
		struct syscall_enter_args args;
		struct augmented_filename filename;
	} augmented_args;
	struct syscall *syscall;
	unsigned int len = sizeof(augmented_args);
	const void *filename_arg = NULL;

	if (pid_filter__has(&pids_filtered, getpid()))
		return 0;

	probe_read(&augmented_args.args, sizeof(augmented_args.args), args);

	syscall = bpf_map_lookup_elem(&syscalls, &augmented_args.args.syscall_nr);
	if (syscall == NULL || !syscall->enabled)
		return 0;
	/*
	 * Yonghong and Edward Cree sayz:
	 *
	 * https://www.spinics.net/lists/netdev/msg531645.html
	 *
	 * >>   R0=inv(id=0) R1=inv2 R6=ctx(id=0,off=0,imm=0) R7=inv64 R10=fp0,call_-1
	 * >> 10: (bf) r1 = r6
	 * >> 11: (07) r1 += 16
	 * >> 12: (05) goto pc+2
	 * >> 15: (79) r3 = *(u64 *)(r1 +0)
	 * >> dereference of modified ctx ptr R1 off=16 disallowed
	 * > Aha, we at least got a different error message this time.
	 * > And indeed llvm has done that optimisation, rather than the more obvious
	 * > 11: r3 = *(u64 *)(r1 +16)
	 * > because it wants to have lots of reads share a single insn.  You may be able
	 * > to defeat that optimisation by adding compiler barriers, idk.  Maybe someone
	 * > with llvm knowledge can figure out how to stop it (ideally, llvm would know
	 * > when it's generating for bpf backend and not do that).  -O0?  ¯\_(ツ)_/¯
	 *
	 * The optimization mostly likes below:
	 *
	 *	br1:
	 * 	...
	 *	r1 += 16
	 *	goto merge
	 *	br2:
	 *	...
	 *	r1 += 20
	 *	goto merge
	 *	merge:
	 *	*(u64 *)(r1 + 0)
	 *
	 * The compiler tries to merge common loads. There is no easy way to
	 * stop this compiler optimization without turning off a lot of other
	 * optimizations. The easiest way is to add barriers:
	 *
	 * 	 __asm__ __volatile__("": : :"memory")
	 *
	 * 	 after the ctx memory access to prevent their down stream merging.
	 */
	switch (augmented_args.args.syscall_nr) {
	case SYS_ACCESS:
	case SYS_OPEN:	 filename_arg = (const void *)args->args[0];
			__asm__ __volatile__("": : :"memory");
			 break;
	case SYS_OPENAT: filename_arg = (const void *)args->args[1];
			 break;
	}

	if (filename_arg != NULL) {
		augmented_args.filename.reserved = 0;
		augmented_args.filename.size = probe_read_str(&augmented_args.filename.value,
							      sizeof(augmented_args.filename.value),
							      filename_arg);
		if (augmented_args.filename.size < sizeof(augmented_args.filename.value)) {
			len -= sizeof(augmented_args.filename.value) - augmented_args.filename.size;
			len &= sizeof(augmented_args.filename.value) - 1;
		}
	} else {
		len = sizeof(augmented_args.args);
	}

	perf_event_output(args, &__augmented_syscalls__, BPF_F_CURRENT_CPU, &augmented_args, len);
	return 0;
}

SEC("raw_syscalls:sys_exit")
int sys_exit(struct syscall_exit_args *args)
{
	struct syscall_exit_args exit_args;
	struct syscall *syscall;

	if (pid_filter__has(&pids_filtered, getpid()))
		return 0;

	probe_read(&exit_args, sizeof(exit_args), args);

	syscall = bpf_map_lookup_elem(&syscalls, &exit_args.syscall_nr);
	if (syscall == NULL || !syscall->enabled)
		return 0;

	return 1;
}

license(GPL);
