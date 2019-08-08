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

struct syscall {
	bool	enabled;
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
	int		reserved;
	char		value[PATH_MAX];
};

/* syscalls where the first arg is a string */
#define SYS_OPEN                 2
#define SYS_STAT                 4
#define SYS_LSTAT                6
#define SYS_ACCESS              21
#define SYS_EXECVE              59
#define SYS_TRUNCATE            76
#define SYS_CHDIR               80
#define SYS_RENAME              82
#define SYS_MKDIR               83
#define SYS_RMDIR               84
#define SYS_CREAT               85
#define SYS_LINK                86
#define SYS_UNLINK              87
#define SYS_SYMLINK             88
#define SYS_READLINK            89
#define SYS_CHMOD               90
#define SYS_CHOWN               92
#define SYS_LCHOWN              94
#define SYS_MKNOD              133
#define SYS_STATFS             137
#define SYS_PIVOT_ROOT         155
#define SYS_CHROOT             161
#define SYS_ACCT               163
#define SYS_SWAPON             167
#define SYS_SWAPOFF            168
#define SYS_DELETE_MODULE      176
#define SYS_SETXATTR           188
#define SYS_LSETXATTR          189
#define SYS_GETXATTR           191
#define SYS_LGETXATTR          192
#define SYS_LISTXATTR          194
#define SYS_LLISTXATTR         195
#define SYS_REMOVEXATTR        197
#define SYS_LREMOVEXATTR       198
#define SYS_MQ_OPEN            240
#define SYS_MQ_UNLINK          241
#define SYS_ADD_KEY            248
#define SYS_REQUEST_KEY        249
#define SYS_SYMLINKAT          266
#define SYS_MEMFD_CREATE       319

/* syscalls where the first arg is a string */

#define SYS_PWRITE64            18
#define SYS_EXECVE              59
#define SYS_RENAME              82
#define SYS_QUOTACTL           179
#define SYS_FSETXATTR          190
#define SYS_FGETXATTR          193
#define SYS_FREMOVEXATTR       199
#define SYS_MQ_TIMEDSEND       242
#define SYS_REQUEST_KEY        249
#define SYS_INOTIFY_ADD_WATCH  254
#define SYS_OPENAT             257
#define SYS_MKDIRAT            258
#define SYS_MKNODAT            259
#define SYS_FCHOWNAT           260
#define SYS_FUTIMESAT          261
#define SYS_NEWFSTATAT         262
#define SYS_UNLINKAT           263
#define SYS_RENAMEAT           264
#define SYS_LINKAT             265
#define SYS_READLINKAT         267
#define SYS_FCHMODAT           268
#define SYS_FACCESSAT          269
#define SYS_UTIMENSAT          280
#define SYS_NAME_TO_HANDLE_AT  303
#define SYS_FINIT_MODULE       313
#define SYS_RENAMEAT2          316
#define SYS_EXECVEAT           322
#define SYS_STATX              332

pid_filter(pids_filtered);

struct augmented_args_filename {
       struct syscall_enter_args args;
       struct augmented_filename filename;
};

bpf_map(augmented_filename_map, PERCPU_ARRAY, int, struct augmented_args_filename, 1);

SEC("raw_syscalls:sys_enter")
int sys_enter(struct syscall_enter_args *args)
{
	struct augmented_args_filename *augmented_args;
	unsigned int len = sizeof(*augmented_args);
	const void *filename_arg = NULL;
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
	 * This table of what args are strings will be provided by userspace,
	 * in the syscalls map, i.e. we will already have to do the lookup to
	 * see if this specific syscall is filtered, so we can as well get more
	 * info about what syscall args are strings or pointers, and how many
	 * bytes to copy, per arg, etc.
	 *
	 * For now hard code it, till we have all the basic mechanisms in place
	 * to automate everything and make the kernel part be completely driven
	 * by information obtained in userspace for each kernel version and
	 * processor architecture, making the kernel part the same no matter what
	 * kernel version or processor architecture it runs on.
	 */
	switch (augmented_args->args.syscall_nr) {
	case SYS_ACCT:
	case SYS_ADD_KEY:
	case SYS_CHDIR:
	case SYS_CHMOD:
	case SYS_CHOWN:
	case SYS_CHROOT:
	case SYS_CREAT:
	case SYS_DELETE_MODULE:
	case SYS_EXECVE:
	case SYS_GETXATTR:
	case SYS_LCHOWN:
	case SYS_LGETXATTR:
	case SYS_LINK:
	case SYS_LISTXATTR:
	case SYS_LLISTXATTR:
	case SYS_LREMOVEXATTR:
	case SYS_LSETXATTR:
	case SYS_LSTAT:
	case SYS_MEMFD_CREATE:
	case SYS_MKDIR:
	case SYS_MKNOD:
	case SYS_MQ_OPEN:
	case SYS_MQ_UNLINK:
	case SYS_PIVOT_ROOT:
	case SYS_READLINK:
	case SYS_REMOVEXATTR:
	case SYS_RENAME:
	case SYS_REQUEST_KEY:
	case SYS_RMDIR:
	case SYS_SETXATTR:
	case SYS_STAT:
	case SYS_STATFS:
	case SYS_SWAPOFF:
	case SYS_SWAPON:
	case SYS_SYMLINK:
	case SYS_SYMLINKAT:
	case SYS_TRUNCATE:
	case SYS_UNLINK:
	case SYS_ACCESS:
	case SYS_OPEN:	 filename_arg = (const void *)args->args[0];
			__asm__ __volatile__("": : :"memory");
			 break;
	case SYS_EXECVEAT:
	case SYS_FACCESSAT:
	case SYS_FCHMODAT:
	case SYS_FCHOWNAT:
	case SYS_FGETXATTR:
	case SYS_FINIT_MODULE:
	case SYS_FREMOVEXATTR:
	case SYS_FSETXATTR:
	case SYS_FUTIMESAT:
	case SYS_INOTIFY_ADD_WATCH:
	case SYS_LINKAT:
	case SYS_MKDIRAT:
	case SYS_MKNODAT:
	case SYS_MQ_TIMEDSEND:
	case SYS_NAME_TO_HANDLE_AT:
	case SYS_NEWFSTATAT:
	case SYS_PWRITE64:
	case SYS_QUOTACTL:
	case SYS_READLINKAT:
	case SYS_RENAMEAT:
	case SYS_RENAMEAT2:
	case SYS_STATX:
	case SYS_UNLINKAT:
	case SYS_UTIMENSAT:
	case SYS_OPENAT: filename_arg = (const void *)args->args[1];
			 break;
	}

	if (filename_arg != NULL) {
		augmented_args->filename.reserved = 0;
		augmented_args->filename.size = probe_read_str(&augmented_args->filename.value,
							      sizeof(augmented_args->filename.value),
							      filename_arg);
		if (augmented_args->filename.size < sizeof(augmented_args->filename.value)) {
			len -= sizeof(augmented_args->filename.value) - augmented_args->filename.size;
			len &= sizeof(augmented_args->filename.value) - 1;
		}
	} else {
		len = sizeof(augmented_args->args);
	}

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
