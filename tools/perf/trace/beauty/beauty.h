/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PERF_TRACE_BEAUTY_H
#define _PERF_TRACE_BEAUTY_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <sys/types.h>

struct strarray {
	int	    offset;
	int	    nr_entries;
	const char **entries;
};

#define DEFINE_STRARRAY(array) struct strarray strarray__##array = { \
	.nr_entries = ARRAY_SIZE(array), \
	.entries = array, \
}

#define DEFINE_STRARRAY_OFFSET(array, off) struct strarray strarray__##array = { \
	.offset	    = off, \
	.nr_entries = ARRAY_SIZE(array), \
	.entries = array, \
}

size_t strarray__scnprintf(struct strarray *sa, char *bf, size_t size, const char *intfmt, int val);
size_t strarray__scnprintf_flags(struct strarray *sa, char *bf, size_t size, unsigned long flags);

struct trace;
struct thread;

size_t pid__scnprintf_fd(struct trace *trace, pid_t pid, int fd, char *bf, size_t size);

extern struct strarray strarray__socket_families;

/**
 * augmented_arg: extra payload for syscall pointer arguments
 
 * If perf_sample->raw_size is more than what a syscall sys_enter_FOO puts,
 * then its the arguments contents, so that we can show more than just a
 * pointer. This will be done initially with eBPF, the start of that is at the
 * tools/perf/examples/bpf/augmented_syscalls.c example for the openat, but
 * will eventually be done automagically caching the running kernel tracefs
 * events data into an eBPF C script, that then gets compiled and its .o file
 * cached for subsequent use. For char pointers like the ones for 'open' like
 * syscalls its easy, for the rest we should use DWARF or better, BTF, much
 * more compact.
 *
 * @size: 8 if all we need is an integer, otherwise all of the augmented arg.
 * @int_arg: will be used for integer like pointer contents, like 'accept's 'upeer_addrlen'
 * @value: u64 aligned, for structs, pathnames
 */
struct augmented_arg {
	int  size;
	int  int_arg;
	u64  value[];
};

/**
 * @val: value of syscall argument being formatted
 * @args: All the args, use syscall_args__val(arg, nth) to access one
 * @augmented_args: Extra data that can be collected, for instance, with eBPF for expanding the pathname for open, etc
 * @augmented_args_size: augmented_args total payload size
 * @thread: tid state (maps, pid, tid, etc)
 * @trace: 'perf trace' internals: all threads, etc
 * @parm: private area, may be an strarray, for instance
 * @idx: syscall arg idx (is this the first?)
 * @mask: a syscall arg may mask another arg, see syscall_arg__scnprintf_futex_op
 */

struct syscall_arg {
	unsigned long val;
	unsigned char *args;
	struct {
		struct augmented_arg *args;
		int		     size;
	} augmented;
	struct thread *thread;
	struct trace  *trace;
	void	      *parm;
	u8	      idx;
	u8	      mask;
};

unsigned long syscall_arg__val(struct syscall_arg *arg, u8 idx);

size_t syscall_arg__scnprintf_strarrays(char *bf, size_t size, struct syscall_arg *arg);
#define SCA_STRARRAYS syscall_arg__scnprintf_strarrays

size_t syscall_arg__scnprintf_fd(char *bf, size_t size, struct syscall_arg *arg);
#define SCA_FD syscall_arg__scnprintf_fd

size_t syscall_arg__scnprintf_hex(char *bf, size_t size, struct syscall_arg *arg);
#define SCA_HEX syscall_arg__scnprintf_hex

size_t syscall_arg__scnprintf_int(char *bf, size_t size, struct syscall_arg *arg);
#define SCA_INT syscall_arg__scnprintf_int

size_t syscall_arg__scnprintf_long(char *bf, size_t size, struct syscall_arg *arg);
#define SCA_LONG syscall_arg__scnprintf_long

size_t syscall_arg__scnprintf_pid(char *bf, size_t size, struct syscall_arg *arg);
#define SCA_PID syscall_arg__scnprintf_pid

size_t syscall_arg__scnprintf_clone_flags(char *bf, size_t size, struct syscall_arg *arg);
#define SCA_CLONE_FLAGS syscall_arg__scnprintf_clone_flags

size_t syscall_arg__scnprintf_fcntl_cmd(char *bf, size_t size, struct syscall_arg *arg);
#define SCA_FCNTL_CMD syscall_arg__scnprintf_fcntl_cmd

size_t syscall_arg__scnprintf_fcntl_arg(char *bf, size_t size, struct syscall_arg *arg);
#define SCA_FCNTL_ARG syscall_arg__scnprintf_fcntl_arg

size_t syscall_arg__scnprintf_flock(char *bf, size_t size, struct syscall_arg *arg);
#define SCA_FLOCK syscall_arg__scnprintf_flock

size_t syscall_arg__scnprintf_ioctl_cmd(char *bf, size_t size, struct syscall_arg *arg);
#define SCA_IOCTL_CMD syscall_arg__scnprintf_ioctl_cmd

size_t syscall_arg__scnprintf_kcmp_type(char *bf, size_t size, struct syscall_arg *arg);
#define SCA_KCMP_TYPE syscall_arg__scnprintf_kcmp_type

size_t syscall_arg__scnprintf_kcmp_idx(char *bf, size_t size, struct syscall_arg *arg);
#define SCA_KCMP_IDX syscall_arg__scnprintf_kcmp_idx

unsigned long syscall_arg__mask_val_mount_flags(struct syscall_arg *arg, unsigned long flags);
#define SCAMV_MOUNT_FLAGS syscall_arg__mask_val_mount_flags

size_t syscall_arg__scnprintf_mount_flags(char *bf, size_t size, struct syscall_arg *arg);
#define SCA_MOUNT_FLAGS syscall_arg__scnprintf_mount_flags

size_t syscall_arg__scnprintf_pkey_alloc_access_rights(char *bf, size_t size, struct syscall_arg *arg);
#define SCA_PKEY_ALLOC_ACCESS_RIGHTS syscall_arg__scnprintf_pkey_alloc_access_rights

size_t syscall_arg__scnprintf_open_flags(char *bf, size_t size, struct syscall_arg *arg);
#define SCA_OPEN_FLAGS syscall_arg__scnprintf_open_flags

size_t syscall_arg__scnprintf_prctl_option(char *bf, size_t size, struct syscall_arg *arg);
#define SCA_PRCTL_OPTION syscall_arg__scnprintf_prctl_option

size_t syscall_arg__scnprintf_prctl_arg2(char *bf, size_t size, struct syscall_arg *arg);
#define SCA_PRCTL_ARG2 syscall_arg__scnprintf_prctl_arg2

size_t syscall_arg__scnprintf_prctl_arg3(char *bf, size_t size, struct syscall_arg *arg);
#define SCA_PRCTL_ARG3 syscall_arg__scnprintf_prctl_arg3

size_t syscall_arg__scnprintf_sockaddr(char *bf, size_t size, struct syscall_arg *arg);
#define SCA_SOCKADDR syscall_arg__scnprintf_sockaddr

size_t syscall_arg__scnprintf_socket_protocol(char *bf, size_t size, struct syscall_arg *arg);
#define SCA_SK_PROTO syscall_arg__scnprintf_socket_protocol

size_t syscall_arg__scnprintf_statx_flags(char *bf, size_t size, struct syscall_arg *arg);
#define SCA_STATX_FLAGS syscall_arg__scnprintf_statx_flags

size_t syscall_arg__scnprintf_statx_mask(char *bf, size_t size, struct syscall_arg *arg);
#define SCA_STATX_MASK syscall_arg__scnprintf_statx_mask

size_t open__scnprintf_flags(unsigned long flags, char *bf, size_t size);

void syscall_arg__set_ret_scnprintf(struct syscall_arg *arg,
				    size_t (*ret_scnprintf)(char *bf, size_t size, struct syscall_arg *arg));

const char *arch_syscalls__strerrno(const char *arch, int err);

#endif /* _PERF_TRACE_BEAUTY_H */
