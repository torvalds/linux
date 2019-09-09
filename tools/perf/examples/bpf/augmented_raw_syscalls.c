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
#include <linux/limits.h>
#include <pid_filter.h>

/* bpf-output associated map */
bpf_map(__augmented_syscalls__, PERF_EVENT_ARRAY, int, u32, __NR_CPUS__);

/*
 * string_args_len: one per syscall arg, 0 means not a string or don't copy it,
 * 		    PATH_MAX for copying everything, any other value to limit
 * 		    it a la 'strace -s strsize'.
 */
struct syscall {
	bool	enabled;
	u16	string_args_len[6];
};

bpf_map(syscalls, ARRAY, int, struct syscall, 512);

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
	int		err;
	char		value[PATH_MAX];
};

pid_filter(pids_filtered);

struct augmented_args_filename {
       struct syscall_enter_args args;
       struct augmented_filename filename;
};

bpf_map(augmented_filename_map, PERCPU_ARRAY, int, struct augmented_args_filename, 1);

static inline
unsigned int augmented_filename__read(struct augmented_filename *augmented_filename,
				      const void *filename_arg, unsigned int filename_len)
{
	unsigned int len = sizeof(*augmented_filename);
	int size = probe_read_str(&augmented_filename->value, filename_len, filename_arg);

	augmented_filename->size = augmented_filename->err = 0;
	/*
	 * probe_read_str may return < 0, e.g. -EFAULT
	 * So we leave that in the augmented_filename->size that userspace will
	 */
	if (size > 0) {
		len -= sizeof(augmented_filename->value) - size;
		len &= sizeof(augmented_filename->value) - 1;
		augmented_filename->size = size;
	} else {
		/*
		 * So that username notice the error while still being able
		 * to skip this augmented arg record
		 */
		augmented_filename->err = size;
		len = offsetof(struct augmented_filename, value);
	}

	return len;
}

SEC("raw_syscalls:sys_enter")
int sys_enter(struct syscall_enter_args *args)
{
	struct augmented_args_filename *augmented_args;
	/*
	 * We start len, the amount of data that will be in the perf ring
	 * buffer, if this is not filtered out by one of pid_filter__has(),
	 * syscall->enabled, etc, with the non-augmented raw syscall payload,
	 * i.e. sizeof(augmented_args->args).
	 *
	 * We'll add to this as we add augmented syscalls right after that
	 * initial, non-augmented raw_syscalls:sys_enter payload.
	 */
	unsigned int len = sizeof(augmented_args->args);
	struct syscall *syscall;
	int key = 0;

        augmented_args = bpf_map_lookup_elem(&augmented_filename_map, &key);
        if (augmented_args == NULL)
                return 1;

	if (pid_filter__has(&pids_filtered, getpid()))
		return 0;

	probe_read(&augmented_args->args, sizeof(augmented_args->args), args);

	syscall = bpf_map_lookup_elem(&syscalls, &augmented_args->args.syscall_nr);
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
	/*
	 * For now copy just the first string arg, we need to improve the protocol
	 * and have more than one.
	 *
	 * Using the unrolled loop is not working, only when we do it manually,
	 * check this out later...

	u8 arg;
#pragma clang loop unroll(full)
	for (arg = 0; arg < 6; ++arg) {
		if (syscall->string_args_len[arg] != 0) {
			filename_len = syscall->string_args_len[arg];
			filename_arg = (const void *)args->args[arg];
			__asm__ __volatile__("": : :"memory");
			break;
		}
	}

	verifier log:

; if (syscall->string_args_len[arg] != 0) {
37: (69) r3 = *(u16 *)(r0 +2)
 R0=map_value(id=0,off=0,ks=4,vs=14,imm=0) R1_w=inv0 R2_w=map_value(id=0,off=2,ks=4,vs=14,imm=0) R6=ctx(id=0,off=0,imm=0) R7=map_value(id=0,off=0,ks=4,vs=4168,imm=0) R10=fp0,call_-1 fp-8=mmmmmmmm
; if (syscall->string_args_len[arg] != 0) {
38: (55) if r3 != 0x0 goto pc+5
 R0=map_value(id=0,off=0,ks=4,vs=14,imm=0) R1=inv0 R2=map_value(id=0,off=2,ks=4,vs=14,imm=0) R3=inv0 R6=ctx(id=0,off=0,imm=0) R7=map_value(id=0,off=0,ks=4,vs=4168,imm=0) R10=fp0,call_-1 fp-8=mmmmmmmm
39: (b7) r1 = 1
; if (syscall->string_args_len[arg] != 0) {
40: (bf) r2 = r0
41: (07) r2 += 4
42: (69) r3 = *(u16 *)(r0 +4)
 R0=map_value(id=0,off=0,ks=4,vs=14,imm=0) R1_w=inv1 R2_w=map_value(id=0,off=4,ks=4,vs=14,imm=0) R3_w=inv0 R6=ctx(id=0,off=0,imm=0) R7=map_value(id=0,off=0,ks=4,vs=4168,imm=0) R10=fp0,call_-1 fp-8=mmmmmmmm
; if (syscall->string_args_len[arg] != 0) {
43: (15) if r3 == 0x0 goto pc+32
 R0=map_value(id=0,off=0,ks=4,vs=14,imm=0) R1=inv1 R2=map_value(id=0,off=4,ks=4,vs=14,imm=0) R3=inv(id=0,umax_value=65535,var_off=(0x0; 0xffff)) R6=ctx(id=0,off=0,imm=0) R7=map_value(id=0,off=0,ks=4,vs=4168,imm=0) R10=fp0,call_-1 fp-8=mmmmmmmm
; filename_arg = (const void *)args->args[arg];
44: (67) r1 <<= 3
45: (bf) r3 = r6
46: (0f) r3 += r1
47: (b7) r5 = 64
48: (79) r3 = *(u64 *)(r3 +16)
dereference of modified ctx ptr R3 off=8 disallowed
processed 46 insns (limit 1000000) max_states_per_insn 0 total_states 12 peak_states 12 mark_read 7
	*/

#define __loop_iter(arg) \
	if (syscall->string_args_len[arg] != 0) { \
		unsigned int filename_len = syscall->string_args_len[arg]; \
		const void *filename_arg = (const void *)args->args[arg]; \
		if (filename_len <= sizeof(augmented_args->filename.value)) \
			len += augmented_filename__read(&augmented_args->filename, filename_arg, filename_len);
#define loop_iter_first() __loop_iter(0); }
#define loop_iter(arg) else __loop_iter(arg); }
#define loop_iter_last(arg) else __loop_iter(arg); __asm__ __volatile__("": : :"memory"); }

	loop_iter_first()
	loop_iter(1)
	loop_iter(2)
	loop_iter(3)
	loop_iter(4)
	loop_iter_last(5)

	/* If perf_event_output fails, return non-zero so that it gets recorded unaugmented */
	return perf_event_output(args, &__augmented_syscalls__, BPF_F_CURRENT_CPU, augmented_args, len);
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
