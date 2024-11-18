// SPDX-License-Identifier: GPL-2.0
/*
 * Augment the raw_syscalls tracepoints with the contents of the pointer arguments.
 *
 * This exactly matches what is marshalled into the raw_syscall:sys_enter
 * payload expected by the 'perf trace' beautifiers.
 */

#include "vmlinux.h"
#include "../trace_augment.h"

#include <bpf/bpf_helpers.h>
#include <linux/limits.h>

#define PERF_ALIGN(x, a)        __PERF_ALIGN_MASK(x, (typeof(x))(a)-1)
#define __PERF_ALIGN_MASK(x, mask)      (((x)+(mask))&~(mask))

/**
 * is_power_of_2() - check if a value is a power of two
 * @n: the value to check
 *
 * Determine whether some value is a power of two, where zero is *not*
 * considered a power of two.  Return: true if @n is a power of 2, otherwise
 * false.
 */
#define is_power_of_2(n) (n != 0 && ((n & (n - 1)) == 0))

#define MAX_CPUS  4096

/* bpf-output associated map */
struct __augmented_syscalls__ {
	__uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
	__type(key, int);
	__type(value, __u32);
	__uint(max_entries, MAX_CPUS);
} __augmented_syscalls__ SEC(".maps");

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

struct augmented_arg {
	unsigned int	size;
	int		err;
	union {
		char   value[PATH_MAX];
		struct sockaddr_storage saddr;
	};
};

struct pids_filtered {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, pid_t);
	__type(value, bool);
	__uint(max_entries, 64);
} pids_filtered SEC(".maps");

struct augmented_args_payload {
	struct syscall_enter_args args;
	struct augmented_arg arg, arg2; // We have to reserve space for two arguments (rename, etc)
};

// We need more tmp space than the BPF stack can give us
struct augmented_args_tmp {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__type(key, int);
	__type(value, struct augmented_args_payload);
	__uint(max_entries, 1);
} augmented_args_tmp SEC(".maps");

struct beauty_map_enter {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, int);
	__type(value, __u32[6]);
	__uint(max_entries, 512);
} beauty_map_enter SEC(".maps");

struct beauty_payload_enter {
	struct syscall_enter_args args;
	struct augmented_arg aug_args[6];
};

struct beauty_payload_enter_map {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__type(key, int);
	__type(value, struct beauty_payload_enter);
	__uint(max_entries, 1);
} beauty_payload_enter_map SEC(".maps");

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

static inline int augmented__beauty_output(void *ctx, void *data, int len)
{
	return bpf_perf_event_output(ctx, &__augmented_syscalls__, BPF_F_CURRENT_CPU, data, len);
}

static inline
unsigned int augmented_arg__read_str(struct augmented_arg *augmented_arg, const void *arg, unsigned int arg_len)
{
	unsigned int augmented_len = sizeof(*augmented_arg);
	int string_len = bpf_probe_read_user_str(&augmented_arg->value, arg_len, arg);

	augmented_arg->size = augmented_arg->err = 0;
	/*
	 * probe_read_str may return < 0, e.g. -EFAULT
	 * So we leave that in the augmented_arg->size that userspace will
	 */
	if (string_len > 0) {
		augmented_len -= sizeof(augmented_arg->value) - string_len;
		_Static_assert(is_power_of_2(sizeof(augmented_arg->value)), "sizeof(augmented_arg->value) needs to be a power of two");
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

SEC("tp/raw_syscalls/sys_enter")
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
SEC("tp/syscalls/sys_enter_connect")
int sys_enter_connect(struct syscall_enter_args *args)
{
	struct augmented_args_payload *augmented_args = augmented_args_payload();
	const void *sockaddr_arg = (const void *)args->args[1];
	unsigned int socklen = args->args[2];
	unsigned int len = sizeof(u64) + sizeof(augmented_args->args); // the size + err in all 'augmented_arg' structs

        if (augmented_args == NULL)
                return 1; /* Failure: don't filter */

	_Static_assert(is_power_of_2(sizeof(augmented_args->arg.saddr)), "sizeof(augmented_args->arg.saddr) needs to be a power of two");
	socklen &= sizeof(augmented_args->arg.saddr) - 1;

	bpf_probe_read_user(&augmented_args->arg.saddr, socklen, sockaddr_arg);
	augmented_args->arg.size = socklen;
	augmented_args->arg.err = 0;

	return augmented__output(args, augmented_args, len + socklen);
}

SEC("tp/syscalls/sys_enter_sendto")
int sys_enter_sendto(struct syscall_enter_args *args)
{
	struct augmented_args_payload *augmented_args = augmented_args_payload();
	const void *sockaddr_arg = (const void *)args->args[4];
	unsigned int socklen = args->args[5];
	unsigned int len = sizeof(u64) + sizeof(augmented_args->args); // the size + err in all 'augmented_arg' structs

        if (augmented_args == NULL)
                return 1; /* Failure: don't filter */

	socklen &= sizeof(augmented_args->arg.saddr) - 1;

	bpf_probe_read_user(&augmented_args->arg.saddr, socklen, sockaddr_arg);

	return augmented__output(args, augmented_args, len + socklen);
}

SEC("tp/syscalls/sys_enter_open")
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

SEC("tp/syscalls/sys_enter_openat")
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

SEC("tp/syscalls/sys_enter_rename")
int sys_enter_rename(struct syscall_enter_args *args)
{
	struct augmented_args_payload *augmented_args = augmented_args_payload();
	const void *oldpath_arg = (const void *)args->args[0],
		   *newpath_arg = (const void *)args->args[1];
	unsigned int len = sizeof(augmented_args->args), oldpath_len, newpath_len;

        if (augmented_args == NULL)
                return 1; /* Failure: don't filter */

	len += 2 * sizeof(u64); // The overhead of size and err, just before the payload...

	oldpath_len = augmented_arg__read_str(&augmented_args->arg, oldpath_arg, sizeof(augmented_args->arg.value));
	augmented_args->arg.size = PERF_ALIGN(oldpath_len + 1, sizeof(u64));
	len += augmented_args->arg.size;

	struct augmented_arg *arg2 = (void *)&augmented_args->arg.value + augmented_args->arg.size;

	newpath_len = augmented_arg__read_str(arg2, newpath_arg, sizeof(augmented_args->arg.value));
	arg2->size = newpath_len;

	len += newpath_len;

	return augmented__output(args, augmented_args, len);
}

SEC("tp/syscalls/sys_enter_renameat2")
int sys_enter_renameat2(struct syscall_enter_args *args)
{
	struct augmented_args_payload *augmented_args = augmented_args_payload();
	const void *oldpath_arg = (const void *)args->args[1],
		   *newpath_arg = (const void *)args->args[3];
	unsigned int len = sizeof(augmented_args->args), oldpath_len, newpath_len;

        if (augmented_args == NULL)
                return 1; /* Failure: don't filter */

	len += 2 * sizeof(u64); // The overhead of size and err, just before the payload...

	oldpath_len = augmented_arg__read_str(&augmented_args->arg, oldpath_arg, sizeof(augmented_args->arg.value));
	augmented_args->arg.size = PERF_ALIGN(oldpath_len + 1, sizeof(u64));
	len += augmented_args->arg.size;

	struct augmented_arg *arg2 = (void *)&augmented_args->arg.value + augmented_args->arg.size;

	newpath_len = augmented_arg__read_str(arg2, newpath_arg, sizeof(augmented_args->arg.value));
	arg2->size = newpath_len;

	len += newpath_len;

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

SEC("tp/syscalls/sys_enter_perf_event_open")
int sys_enter_perf_event_open(struct syscall_enter_args *args)
{
	struct augmented_args_payload *augmented_args = augmented_args_payload();
	const struct perf_event_attr_size *attr = (const struct perf_event_attr_size *)args->args[0], *attr_read;
	unsigned int len = sizeof(u64) + sizeof(augmented_args->args); // the size + err in all 'augmented_arg' structs

        if (augmented_args == NULL)
		goto failure;

	if (bpf_probe_read_user(&augmented_args->arg.value, sizeof(*attr), attr) < 0)
		goto failure;

	attr_read = (const struct perf_event_attr_size *)augmented_args->arg.value;

	__u32 size = attr_read->size;

	if (!size)
		size = PERF_ATTR_SIZE_VER0;

	if (size > sizeof(augmented_args->arg.value))
                goto failure;

	// Now that we read attr->size and tested it against the size limits, read it completely
	if (bpf_probe_read_user(&augmented_args->arg.value, size, attr) < 0)
		goto failure;

	return augmented__output(args, augmented_args, len + size);
failure:
	return 1; /* Failure: don't filter */
}

SEC("tp/syscalls/sys_enter_clock_nanosleep")
int sys_enter_clock_nanosleep(struct syscall_enter_args *args)
{
	struct augmented_args_payload *augmented_args = augmented_args_payload();
	const void *rqtp_arg = (const void *)args->args[2];
	unsigned int len = sizeof(u64) + sizeof(augmented_args->args); // the size + err in all 'augmented_arg' structs
	__u32 size = sizeof(struct timespec64);

        if (augmented_args == NULL)
		goto failure;

	if (size > sizeof(augmented_args->arg.value))
                goto failure;

	bpf_probe_read_user(&augmented_args->arg.value, size, rqtp_arg);

	return augmented__output(args, augmented_args, len + size);
failure:
	return 1; /* Failure: don't filter */
}

SEC("tp/syscalls/sys_enter_nanosleep")
int sys_enter_nanosleep(struct syscall_enter_args *args)
{
	struct augmented_args_payload *augmented_args = augmented_args_payload();
	const void *req_arg = (const void *)args->args[0];
	unsigned int len = sizeof(augmented_args->args);
	__u32 size = sizeof(struct timespec64);

        if (augmented_args == NULL)
		goto failure;

	if (size > sizeof(augmented_args->arg.value))
                goto failure;

	bpf_probe_read_user(&augmented_args->arg.value, size, req_arg);

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

static int augment_sys_enter(void *ctx, struct syscall_enter_args *args)
{
	bool augmented, do_output = false;
	int zero = 0, size, aug_size, index, output = 0,
	    value_size = sizeof(struct augmented_arg) - offsetof(struct augmented_arg, value);
	unsigned int nr, *beauty_map;
	struct beauty_payload_enter *payload;
	void *arg, *payload_offset;

	/* fall back to do predefined tail call */
	if (args == NULL)
		return 1;

	/* use syscall number to get beauty_map entry */
	nr             = (__u32)args->syscall_nr;
	beauty_map     = bpf_map_lookup_elem(&beauty_map_enter, &nr);

	/* set up payload for output */
	payload        = bpf_map_lookup_elem(&beauty_payload_enter_map, &zero);
	payload_offset = (void *)&payload->aug_args;

	if (beauty_map == NULL || payload == NULL)
		return 1;

	/* copy the sys_enter header, which has the syscall_nr */
	__builtin_memcpy(&payload->args, args, sizeof(struct syscall_enter_args));

	/*
	 * Determine what type of argument and how many bytes to read from user space, using the
	 * value in the beauty_map. This is the relation of parameter type and its corresponding
	 * value in the beauty map, and how many bytes we read eventually:
	 *
	 * string: 1			      -> size of string
	 * struct: size of struct	      -> size of struct
	 * buffer: -1 * (index of paired len) -> value of paired len (maximum: TRACE_AUG_MAX_BUF)
	 */
	for (int i = 0; i < 6; i++) {
		arg = (void *)args->args[i];
		augmented = false;
		size = beauty_map[i];
		aug_size = size; /* size of the augmented data read from user space */

		if (size == 0 || arg == NULL)
			continue;

		if (size == 1) { /* string */
			aug_size = bpf_probe_read_user_str(((struct augmented_arg *)payload_offset)->value, value_size, arg);
			/* minimum of 0 to pass the verifier */
			if (aug_size < 0)
				aug_size = 0;

			augmented = true;
		} else if (size > 0 && size <= value_size) { /* struct */
			if (!bpf_probe_read_user(((struct augmented_arg *)payload_offset)->value, size, arg))
				augmented = true;
		} else if (size < 0 && size >= -6) { /* buffer */
			index = -(size + 1);
			aug_size = args->args[index];

			if (aug_size > TRACE_AUG_MAX_BUF)
				aug_size = TRACE_AUG_MAX_BUF;

			if (aug_size > 0) {
				if (!bpf_probe_read_user(((struct augmented_arg *)payload_offset)->value, aug_size, arg))
					augmented = true;
			}
		}

		/* write data to payload */
		if (augmented) {
			int written = offsetof(struct augmented_arg, value) + aug_size;

			((struct augmented_arg *)payload_offset)->size = aug_size;
			output += written;
			payload_offset += written;
			do_output = true;
		}
	}

	if (!do_output)
		return 1;

	return augmented__beauty_output(ctx, payload, sizeof(struct syscall_enter_args) + output);
}

SEC("tp/raw_syscalls/sys_enter")
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

	if (pid_filter__has(&pids_filtered, getpid()))
		return 0;

	augmented_args = augmented_args_payload();
	if (augmented_args == NULL)
		return 1;

	bpf_probe_read_kernel(&augmented_args->args, sizeof(augmented_args->args), args);

	/*
	 * Jump to syscall specific augmenter, even if the default one,
	 * "!raw_syscalls:unaugmented" that will just return 1 to return the
	 * unaugmented tracepoint payload.
	 */
	if (augment_sys_enter(args, &augmented_args->args))
		bpf_tail_call(args, &syscalls_sys_enter, augmented_args->args.syscall_nr);

	// If not found on the PROG_ARRAY syscalls map, then we're filtering it:
	return 0;
}

SEC("tp/raw_syscalls/sys_exit")
int sys_exit(struct syscall_exit_args *args)
{
	struct syscall_exit_args exit_args;

	if (pid_filter__has(&pids_filtered, getpid()))
		return 0;

	bpf_probe_read_kernel(&exit_args, sizeof(exit_args), args);
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
