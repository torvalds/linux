// SPDX-License-Identifier: GPL-2.0
/*
 * Augment the raw_syscalls tracepoints with the contents of the pointer arguments.
 *
 * This exactly matches what is marshalled into the raw_syscall:sys_enter
 * payload expected by the 'perf trace' beautifiers.
 */

#include "vmlinux.h"
#include "perf_trace_u.h"

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
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

#define TRACE_AUG_MAX_BUF 128 /* for buffer augmentation in perf trace */

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
	__uint(max_entries, 1024);
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
	__uint(max_entries, 1024);
} syscalls_sys_exit SEC(".maps");

struct syscall_enter_args {
	union {
		unsigned long long common_tp_fields;
		unsigned short	   common_type;
	};
	long		   syscall_nr;
	unsigned long	   args[6];
};

struct syscall_exit_args {
	union {
		unsigned long long common_tp_fields;
		unsigned short	   common_type;
	};
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

/* With a target only its tasks are traced, those set false from their next exec. */
struct pids_to_trace {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, pid_t);
	__type(value, bool);
	__uint(max_entries, 16384);
} pids_to_trace SEC(".maps");

bool has_pids_to_trace;
/* Also trace the children of traced tasks. */
bool inherit;
/* Key pids_to_trace by tgid, so a process's threads share an entry. */
bool uses_tgid;
/* Tasks not traced as pids_to_trace was full. */
int lost_tasks;

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

/* Returning 0 would drop the tracepoint for other perf sessions. */
static inline int augmented__output(void *ctx, void *args, int len)
{
	bpf_perf_event_output(ctx, &__augmented_syscalls__, BPF_F_CURRENT_CPU, args, len);
	return 1;
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
int sys_enter_unaugmented(struct syscall_enter_args *args)
{
	struct augmented_args_payload *augmented_args = augmented_args_payload();

	if (augmented_args)
		augmented__output(args, &augmented_args->args, sizeof(augmented_args->args));
	return 1;
}

SEC("tp/raw_syscalls/sys_exit")
int sys_exit_unaugmented(struct syscall_exit_args *args)
{
	struct augmented_args_payload *augmented_args = augmented_args_payload();

	if (augmented_args)
		augmented__output(args, &augmented_args->args, sizeof(struct syscall_exit_args));
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
	int err;

        if (augmented_args == NULL)
                return 1; /* Failure: don't filter */

	_Static_assert(is_power_of_2(sizeof(augmented_args->arg.saddr)), "sizeof(augmented_args->arg.saddr) needs to be a power of two");
	socklen &= sizeof(augmented_args->arg.saddr) - 1;

	err = bpf_probe_read_user(&augmented_args->arg.saddr, socklen, sockaddr_arg);
	if (err)
		socklen = 0;
	augmented_args->arg.size = socklen;
	augmented_args->arg.err = err;

	return augmented__output(args, augmented_args, len + socklen);
}

SEC("tp/syscalls/sys_enter_sendto")
int sys_enter_sendto(struct syscall_enter_args *args)
{
	struct augmented_args_payload *augmented_args = augmented_args_payload();
	const void *sockaddr_arg = (const void *)args->args[4];
	unsigned int socklen = args->args[5];
	unsigned int len = sizeof(u64) + sizeof(augmented_args->args); // the size + err in all 'augmented_arg' structs
	int err;

        if (augmented_args == NULL)
                return 1; /* Failure: don't filter */

	socklen &= sizeof(augmented_args->arg.saddr) - 1;

	err = bpf_probe_read_user(&augmented_args->arg.saddr, socklen, sockaddr_arg);
	if (err)
		socklen = 0;
	augmented_args->arg.size = socklen;
	augmented_args->arg.err = err;

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

	/* Every read from userspace is limited to value size */
	if (augmented_args->arg.size > sizeof(augmented_args->arg.value))
		return 1; /* Failure: don't filter */

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

	/* Every read from userspace is limited to value size */
	if (augmented_args->arg.size > sizeof(augmented_args->arg.value))
		return 1; /* Failure: don't filter */

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

	augmented_args->arg.size = size;
	augmented_args->arg.err = 0;

	return augmented__output(args, augmented_args, len + size);
failure:
	if (augmented_args)
		augmented__output(args, augmented_args, sizeof(augmented_args->args));
	return 1;
}

SEC("tp/syscalls/sys_enter_clock_nanosleep")
int sys_enter_clock_nanosleep(struct syscall_enter_args *args)
{
	struct augmented_args_payload *augmented_args = augmented_args_payload();
	const void *rqtp_arg = (const void *)args->args[2];
	unsigned int len = sizeof(u64) + sizeof(augmented_args->args); // the size + err in all 'augmented_arg' structs
	__u32 size = sizeof(struct timespec64);
	int err;

        if (augmented_args == NULL)
		goto failure;

	if (size > sizeof(augmented_args->arg.value))
                goto failure;

	err = bpf_probe_read_user(&augmented_args->arg.value, size, rqtp_arg);
	if (err)
		size = 0;
	augmented_args->arg.size = size;
	augmented_args->arg.err = err;

	return augmented__output(args, augmented_args, len + size);
failure:
	return 1; /* Failure: don't filter */
}

SEC("tp/syscalls/sys_enter_nanosleep")
int sys_enter_nanosleep(struct syscall_enter_args *args)
{
	struct augmented_args_payload *augmented_args = augmented_args_payload();
	const void *req_arg = (const void *)args->args[0];
	/* the size + err in all 'augmented_arg' structs */
	unsigned int len = sizeof(u64) + sizeof(augmented_args->args);
	__u32 size = sizeof(struct timespec64);
	int err;

        if (augmented_args == NULL)
		goto failure;

	if (size > sizeof(augmented_args->arg.value))
                goto failure;

	err = bpf_probe_read_user(&augmented_args->arg.value, size, req_arg);
	if (err)
		size = 0;
	augmented_args->arg.size = size;
	augmented_args->arg.err = err;

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

static bool task_traced(void)
{
	u64 pid_tgid;
	pid_t pid;
	bool *traced;

	if (!has_pids_to_trace)
		return true;

	pid_tgid = bpf_get_current_pid_tgid();
	pid = uses_tgid ? pid_tgid >> 32 : (pid_t)pid_tgid;
	traced = bpf_map_lookup_elem(&pids_to_trace, &pid);
	return traced && *traced;
}

u64 ZERO = 0;

/*
 * Determine what type of argument and how many bytes to read from user space, using the
 * value in the beauty_map. This is the relation of parameter type and its corresponding
 * value in the beauty map, and how many bytes we read eventually:
 *
 * string: 1			          -> size of string
 * struct: size of struct	          -> size of struct
 * buffer: -(0-based index of paired len + 1) -> value of paired len (maximum: TRACE_AUG_MAX_BUF)
 */
static inline int augment_arg(struct syscall_enter_args *args, int i,
			      unsigned int *beauty_map,
			      struct beauty_payload_enter *payload, u64 offset)
{
	int index, value_size = sizeof(struct augmented_arg) - offsetof(struct augmented_arg, value);
	int read_err = 0;
	struct augmented_arg *payload_offset;
	s64 aug_size, size;
	bool augmented;
	void *arg;

	arg = (void *)args->args[i];
	augmented = false;
	size = beauty_map[i];
	aug_size = size; /* size of the augmented data read from user space */

	if (size == 0 || arg == NULL)
		return 0;

	/* bounds check for the verifier */
	if (offset > sizeof(payload->aug_args) - sizeof(payload->aug_args[0]))
		return -1;
	barrier_var(offset);
	payload_offset = (struct augmented_arg *)((void *)&payload->aug_args + offset);

	if (size == 1) { /* string */
		aug_size = bpf_probe_read_user_str(payload_offset->value, value_size, arg);
		/* minimum of 0 to pass the verifier */
		if (aug_size < 0) {
			read_err = aug_size;
			aug_size = 0;
		}

		augmented = true;
	} else if (size > 0 && size <= value_size) { /* struct */
		if (!bpf_probe_read_user(payload_offset->value, size, arg))
			augmented = true;
	} else if ((int)size < 0 && size >= -6) { /* buffer */
		index = -(size + 1);
		barrier_var(index); // Prevent clang (noticed with v18) from removing the &= 7 trick.
		index &= 7;	    // Satisfy the bounds checking with the verifier in some kernels.
		aug_size = args->args[index] > TRACE_AUG_MAX_BUF ? TRACE_AUG_MAX_BUF : args->args[index];

		if (aug_size > 0) {
			if (!bpf_probe_read_user(payload_offset->value, aug_size, arg))
				augmented = true;
		}
	}

	/* Augmented data size is limited to sizeof(augmented_arg->unnamed union with value field) */
	if (aug_size > value_size)
		aug_size = value_size;

	/* write data to payload */
	if (augmented) {
		int written = offsetof(struct augmented_arg, value) + aug_size;

		if (written < 0 || written > sizeof(struct augmented_arg))
			return -1;

		payload_offset->size = aug_size;
		payload_offset->err = read_err;
		return written;
	}

	return 0;
}

static int augment_sys_enter(void *ctx, struct syscall_enter_args *args)
{
	bool do_output = false;
	int i, zero = 0, written;
	u64 output = 0; /* has to be u64, otherwise it won't pass the verifier */
	unsigned int nr, *beauty_map;
	struct beauty_payload_enter *payload;

	/* fall back to do predefined tail call */
	if (args == NULL)
		return 1;

	/* use syscall number to get beauty_map entry */
	nr             = (__u32)args->syscall_nr;
	beauty_map     = bpf_map_lookup_elem(&beauty_map_enter, &nr);

	/* set up payload for output */
	payload        = bpf_map_lookup_elem(&beauty_payload_enter_map, &zero);

	if (beauty_map == NULL || payload == NULL)
		return 1;

	/* copy the sys_enter header, which has the syscall_nr */
	__builtin_memcpy(&payload->args, args, sizeof(struct syscall_enter_args));
	payload->args.common_type = SYSCALL_TRACE_ENTER;

	if (bpf_ksym_exists(bpf_iter_num_new)) {
		bpf_for(i, 0, 6) {
			written = augment_arg(args, i, beauty_map, payload, output);
			if (written < 0)
				return 1;
			if (written > 0) {
				output += written;
				/*
				 * guide the verifier to forget range of `output`, which
				 * helps to prove convergence of the loop
				 */
				output += ZERO;
				do_output = true;
			}
		}
	} else {
		for (i = 0; i < 6; i++) {
			written = augment_arg(args, i, beauty_map, payload, output);
			if (written < 0)
				return 1;
			if (written > 0) {
				output += written;
				do_output = true;
			}
		}
	}

	if (!do_output || (sizeof(struct syscall_enter_args) + output) > sizeof(struct beauty_payload_enter))
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

	if (!task_traced())
		return 1;

	if (pid_filter__has(&pids_filtered, getpid()))
		return 1;

	augmented_args = augmented_args_payload();
	if (augmented_args == NULL)
		return 1;

	bpf_probe_read_kernel(&augmented_args->args, sizeof(augmented_args->args), args);
	augmented_args->args.common_type = SYSCALL_TRACE_ENTER;

	/*
	 * Jump to syscall specific augmenter, even if the default one,
	 * "!raw_syscalls:unaugmented" that will just output the unaugmented
	 * payload.
	 */
	if (augment_sys_enter(args, &augmented_args->args))
		bpf_tail_call(args, &syscalls_sys_enter, augmented_args->args.syscall_nr);

	return 1;
}

SEC("tp/raw_syscalls/sys_exit")
int sys_exit(struct syscall_exit_args *args)
{
	struct augmented_args_payload *augmented_args;

	if (!task_traced())
		return 1;

	if (pid_filter__has(&pids_filtered, getpid()))
		return 1;

	augmented_args = augmented_args_payload();
	if (augmented_args == NULL)
		return 1;

	bpf_probe_read_kernel(&augmented_args->args, sizeof(*args), args);
	augmented_args->args.common_type = SYSCALL_TRACE_EXIT;

	/*
	 * Jump to syscall specific return augmenter, even if the default one,
	 * "!raw_syscalls:unaugmented" that will just output the unaugmented
	 * payload.
	 */
	bpf_tail_call(args, &syscalls_sys_exit, augmented_args->args.syscall_nr);
	/*
	 * If not found on the PROG_ARRAY syscalls map, then we're filtering it
	 * by not emitting bpf-output event.
	 */
	return 1;
}

/* Trace the children of traced tasks, added before they can run. */
SEC("tp_btf/sched_process_fork")
int BPF_PROG(sched_process_fork, struct task_struct *parent, struct task_struct *child)
{
	pid_t parent_pid = parent->pid, child_pid = child->pid;
	bool *traced = NULL, val;

	if (uses_tgid) {
		/* A new thread is traced with its process. */
		if (child->tgid != child_pid)
			return 0;
		parent_pid = parent->tgid;
	}
	if (inherit)
		traced = bpf_map_lookup_elem(&pids_to_trace, &parent_pid);
	if (traced) {
		val = *traced;
		/* Not __sync_fetch_and_add(), as its fetch needs a 5.12 kernel. */
		if (bpf_map_update_elem(&pids_to_trace, &child_pid, &val, BPF_ANY))
			__atomic_fetch_add(&lost_tasks, 1, __ATOMIC_RELAXED);
	} else {
		/* A new pid, so any entry was seeded for an exited task, e.g. a zombie. */
		bpf_map_delete_elem(&pids_to_trace, &child_pid);
	}
	return 0;
}

/* Forget exited tasks, as their pid may be reused. */
SEC("tp_btf/sched_process_exit")
int BPF_PROG(sched_process_exit, struct task_struct *task)
{
	pid_t pid = task->pid;

	if (uses_tgid) {
		/* A process exits with its last thread. */
		if (task->signal->live.counter)
			return 0;
		pid = task->tgid;
	}
	bpf_map_delete_elem(&pids_to_trace, &pid);
	return 0;
}

/* Start tracing waiting tasks, and follow a thread given the leader's pid by exec. */
SEC("tp_btf/sched_process_exec")
int BPF_PROG(sched_process_exec, struct task_struct *task, pid_t old_pid)
{
	pid_t pid = task->pid;
	bool traced = true;

	/* A process keeps its tgid, which pid now is. */
	if (uses_tgid)
		old_pid = pid;
	if (!bpf_map_lookup_elem(&pids_to_trace, &old_pid))
		return 0;

	if (pid != old_pid)
		bpf_map_delete_elem(&pids_to_trace, &old_pid);
	/* A fork may have taken old_pid's entry, leaving the map full. */
	if (bpf_map_update_elem(&pids_to_trace, &pid, &traced, BPF_ANY))
		__atomic_fetch_add(&lost_tasks, 1, __ATOMIC_RELAXED);
	return 0;
}

char _license[] SEC("license") = "GPL";
