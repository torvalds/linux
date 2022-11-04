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

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <linux/limits.h>

// FIXME: These should come from system headers
typedef char bool;
typedef int pid_t;

/* bpf-output associated map */
struct __augmented_syscalls__ {
	__uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
	__type(key, int);
	__type(value, __u32);
	__uint(max_entries, __NR_CPUS__);
} __augmented_syscalls__ SEC(".maps");

/*
 * string_args_len: one per syscall arg, 0 means not a string or don't copy it,
 * 		    PATH_MAX for copying everything, any other value to limit
 * 		    it a la 'strace -s strsize'.
 */
struct syscall {
	bool	enabled;
	__u16	string_args_len[6];
};

struct syscalls {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int);
	__type(value, struct syscall);
	__uint(max_entries, 512);
} syscalls SEC(".maps");

/*
 * What to augment at entry?
 *
 * Pointer arg payloads (filenames, etc) passed from userspace to the kernel
 */
struct syscalls_sys_enter {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__type(key, __u32);
	__type(value, __u32);
	__uint(max_entries, 512);
} syscalls_sys_enter SEC(".maps");

/*
 * What to augment at exit?
 *
 * Pointer arg payloads returned from the kernel (struct stat, etc) to userspace.
 */
struct syscalls_sys_exit {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__type(key, __u32);
	__type(value, __u32);
	__uint(max_entries, 512);
} syscalls_sys_exit SEC(".maps");

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

struct augmented_arg {
	unsigned int	size;
	int		err;
	char		value[PATH_MAX];
};

struct pids_filtered {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, pid_t);
	__type(value, bool);
	__uint(max_entries, 64);
} pids_filtered SEC(".maps");

/*
 * Desired design of maximum size and alignment (see RFC2553)
 */
#define SS_MAXSIZE   128     /* Implementation specific max size */

typedef unsigned short sa_family_t;

/*
 * FIXME: Should come from system headers
 *
 * The definition uses anonymous union and struct in order to control the
 * default alignment.
 */
struct sockaddr_storage {
	union {
		struct {
			sa_family_t    ss_family; /* address family */
			/* Following field(s) are implementation specific */
			char __data[SS_MAXSIZE - sizeof(unsigned short)];
				/* space to achieve desired size, */
				/* _SS_MAXSIZE value minus size of ss_family */
		};
		void *__align; /* implementation specific desired alignment */
	};
};

struct augmented_args_payload {
       struct syscall_enter_args args;
       union {
		struct {
			struct augmented_arg arg, arg2;
		};
		struct sockaddr_storage saddr;
		char   __data[sizeof(struct augmented_arg)];
	};
};

// We need more tmp space than the BPF stack can give us
struct augmented_args_tmp {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__type(key, int);
	__type(value, struct augmented_args_payload);
	__uint(max_entries, 1);
} augmented_args_tmp SEC(".maps");

static inline struct augmented_args_payload *augmented_args_payload(void)
{
	int key = 0;
	return bpf_map_lookup_elem(&augmented_args_tmp, &key);
}

static inline int augmented__output(void *ctx, struct augmented_args_payload *args, int len)
{
	/* If perf_event_output fails, return non-zero so that it gets recorded unaugmented */
	return bpf_perf_event_output(ctx, &__augmented_syscalls__, BPF_F_CURRENT_CPU, args, len);
}

static inline
unsigned int augmented_arg__read_str(struct augmented_arg *augmented_arg, const void *arg, unsigned int arg_len)
{
	unsigned int augmented_len = sizeof(*augmented_arg);
	int string_len = bpf_probe_read_str(&augmented_arg->value, arg_len, arg);

	augmented_arg->size = augmented_arg->err = 0;
	/*
	 * probe_read_str may return < 0, e.g. -EFAULT
	 * So we leave that in the augmented_arg->size that userspace will
	 */
	if (string_len > 0) {
		augmented_len -= sizeof(augmented_arg->value) - string_len;
		augmented_len &= sizeof(augmented_arg->value) - 1;
		augmented_arg->size = string_len;
	} else {
		/*
		 * So that username notice the error while still being able
		 * to skip this augmented arg record
		 */
		augmented_arg->err = string_len;
		augmented_len = offsetof(struct augmented_arg, value);
	}

	return augmented_len;
}

SEC("!raw_syscalls:unaugmented")
int syscall_unaugmented(struct syscall_enter_args *args)
{
	return 1;
}

/*
 * These will be tail_called from SEC("raw_syscalls:sys_enter"), so will find in
 * augmented_args_tmp what was read by that raw_syscalls:sys_enter and go
 * on from there, reading the first syscall arg as a string, i.e. open's
 * filename.
 */
SEC("!syscalls:sys_enter_connect")
int sys_enter_connect(struct syscall_enter_args *args)
{
	struct augmented_args_payload *augmented_args = augmented_args_payload();
	const void *sockaddr_arg = (const void *)args->args[1];
	unsigned int socklen = args->args[2];
	unsigned int len = sizeof(augmented_args->args);

        if (augmented_args == NULL)
                return 1; /* Failure: don't filter */

	if (socklen > sizeof(augmented_args->saddr))
		socklen = sizeof(augmented_args->saddr);

	bpf_probe_read(&augmented_args->saddr, socklen, sockaddr_arg);

	return augmented__output(args, augmented_args, len + socklen);
}

SEC("!syscalls:sys_enter_sendto")
int sys_enter_sendto(struct syscall_enter_args *args)
{
	struct augmented_args_payload *augmented_args = augmented_args_payload();
	const void *sockaddr_arg = (const void *)args->args[4];
	unsigned int socklen = args->args[5];
	unsigned int len = sizeof(augmented_args->args);

        if (augmented_args == NULL)
                return 1; /* Failure: don't filter */

	if (socklen > sizeof(augmented_args->saddr))
		socklen = sizeof(augmented_args->saddr);

	bpf_probe_read(&augmented_args->saddr, socklen, sockaddr_arg);

	return augmented__output(args, augmented_args, len + socklen);
}

SEC("!syscalls:sys_enter_open")
int sys_enter_open(struct syscall_enter_args *args)
{
	struct augmented_args_payload *augmented_args = augmented_args_payload();
	const void *filename_arg = (const void *)args->args[0];
	unsigned int len = sizeof(augmented_args->args);

        if (augmented_args == NULL)
                return 1; /* Failure: don't filter */

	len += augmented_arg__read_str(&augmented_args->arg, filename_arg, sizeof(augmented_args->arg.value));

	return augmented__output(args, augmented_args, len);
}

SEC("!syscalls:sys_enter_openat")
int sys_enter_openat(struct syscall_enter_args *args)
{
	struct augmented_args_payload *augmented_args = augmented_args_payload();
	const void *filename_arg = (const void *)args->args[1];
	unsigned int len = sizeof(augmented_args->args);

        if (augmented_args == NULL)
                return 1; /* Failure: don't filter */

	len += augmented_arg__read_str(&augmented_args->arg, filename_arg, sizeof(augmented_args->arg.value));

	return augmented__output(args, augmented_args, len);
}

SEC("!syscalls:sys_enter_rename")
int sys_enter_rename(struct syscall_enter_args *args)
{
	struct augmented_args_payload *augmented_args = augmented_args_payload();
	const void *oldpath_arg = (const void *)args->args[0],
		   *newpath_arg = (const void *)args->args[1];
	unsigned int len = sizeof(augmented_args->args), oldpath_len;

        if (augmented_args == NULL)
                return 1; /* Failure: don't filter */

	oldpath_len = augmented_arg__read_str(&augmented_args->arg, oldpath_arg, sizeof(augmented_args->arg.value));
	len += oldpath_len + augmented_arg__read_str((void *)(&augmented_args->arg) + oldpath_len, newpath_arg, sizeof(augmented_args->arg.value));

	return augmented__output(args, augmented_args, len);
}

SEC("!syscalls:sys_enter_renameat")
int sys_enter_renameat(struct syscall_enter_args *args)
{
	struct augmented_args_payload *augmented_args = augmented_args_payload();
	const void *oldpath_arg = (const void *)args->args[1],
		   *newpath_arg = (const void *)args->args[3];
	unsigned int len = sizeof(augmented_args->args), oldpath_len;

        if (augmented_args == NULL)
                return 1; /* Failure: don't filter */

	oldpath_len = augmented_arg__read_str(&augmented_args->arg, oldpath_arg, sizeof(augmented_args->arg.value));
	len += oldpath_len + augmented_arg__read_str((void *)(&augmented_args->arg) + oldpath_len, newpath_arg, sizeof(augmented_args->arg.value));

	return augmented__output(args, augmented_args, len);
}

#define PERF_ATTR_SIZE_VER0     64      /* sizeof first published struct */

// we need just the start, get the size to then copy it
struct perf_event_attr_size {
        __u32                   type;
        /*
         * Size of the attr structure, for fwd/bwd compat.
         */
        __u32                   size;
};

SEC("!syscalls:sys_enter_perf_event_open")
int sys_enter_perf_event_open(struct syscall_enter_args *args)
{
	struct augmented_args_payload *augmented_args = augmented_args_payload();
	const struct perf_event_attr_size *attr = (const struct perf_event_attr_size *)args->args[0], *attr_read;
	unsigned int len = sizeof(augmented_args->args);

        if (augmented_args == NULL)
		goto failure;

	if (bpf_probe_read(&augmented_args->__data, sizeof(*attr), attr) < 0)
		goto failure;

	attr_read = (const struct perf_event_attr_size *)augmented_args->__data;

	__u32 size = attr_read->size;

	if (!size)
		size = PERF_ATTR_SIZE_VER0;

	if (size > sizeof(augmented_args->__data))
                goto failure;

	// Now that we read attr->size and tested it against the size limits, read it completely
	if (bpf_probe_read(&augmented_args->__data, size, attr) < 0)
		goto failure;

	return augmented__output(args, augmented_args, len + size);
failure:
	return 1; /* Failure: don't filter */
}

static pid_t getpid(void)
{
	return bpf_get_current_pid_tgid();
}

static bool pid_filter__has(struct pids_filtered *pids, pid_t pid)
{
	return bpf_map_lookup_elem(pids, &pid) != NULL;
}

SEC("raw_syscalls:sys_enter")
int sys_enter(struct syscall_enter_args *args)
{
	struct augmented_args_payload *augmented_args;
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

	if (pid_filter__has(&pids_filtered, getpid()))
		return 0;

	augmented_args = augmented_args_payload();
	if (augmented_args == NULL)
		return 1;

	bpf_probe_read(&augmented_args->args, sizeof(augmented_args->args), args);

	/*
	 * Jump to syscall specific augmenter, even if the default one,
	 * "!raw_syscalls:unaugmented" that will just return 1 to return the
	 * unaugmented tracepoint payload.
	 */
	bpf_tail_call(args, &syscalls_sys_enter, augmented_args->args.syscall_nr);

	// If not found on the PROG_ARRAY syscalls map, then we're filtering it:
	return 0;
}

SEC("raw_syscalls:sys_exit")
int sys_exit(struct syscall_exit_args *args)
{
	struct syscall_exit_args exit_args;

	if (pid_filter__has(&pids_filtered, getpid()))
		return 0;

	bpf_probe_read(&exit_args, sizeof(exit_args), args);
	/*
	 * Jump to syscall specific return augmenter, even if the default one,
	 * "!raw_syscalls:unaugmented" that will just return 1 to return the
	 * unaugmented tracepoint payload.
	 */
	bpf_tail_call(args, &syscalls_sys_exit, exit_args.syscall_nr);
	/*
	 * If not found on the PROG_ARRAY syscalls map, then we're filtering it:
	 */
	return 0;
}

char _license[] SEC("license") = "GPL";
