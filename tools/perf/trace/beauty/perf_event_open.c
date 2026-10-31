// SPDX-License-Identifier: LGPL-2.1
#include <string.h>
#include "trace/beauty/beauty.h"
#include "util/evsel_fprintf.h"
#include <linux/perf_event.h>

#ifndef PERF_FLAG_FD_NO_GROUP
# define PERF_FLAG_FD_NO_GROUP		(1UL << 0)
#endif

#ifndef PERF_FLAG_FD_OUTPUT
# define PERF_FLAG_FD_OUTPUT		(1UL << 1)
#endif

#ifndef PERF_FLAG_PID_CGROUP
# define PERF_FLAG_PID_CGROUP		(1UL << 2) /* pid=cgroup id, per-cpu mode only */
#endif

#ifndef PERF_FLAG_FD_CLOEXEC
# define PERF_FLAG_FD_CLOEXEC		(1UL << 3) /* O_CLOEXEC */
#endif

size_t syscall_arg__scnprintf_perf_flags(char *bf, size_t size,
					 struct syscall_arg *arg)
{
	bool show_prefix = arg->show_string_prefix;
	const char *prefix = "PERF_";
	int printed = 0, flags = arg->val;

	if (flags == 0)
		return 0;

#define	P_FLAG(n) \
	if (flags & PERF_FLAG_##n) { \
		printed += scnprintf(bf + printed, size - printed, "%s%s%s", printed ? "|" : "", show_prefix ? prefix : "", #n); \
		flags &= ~PERF_FLAG_##n; \
	}

	P_FLAG(FD_NO_GROUP);
	P_FLAG(FD_OUTPUT);
	P_FLAG(PID_CGROUP);
	P_FLAG(FD_CLOEXEC);
#undef P_FLAG

	if (flags)
		printed += scnprintf(bf + printed, size - printed, "%s%#x", printed ? "|" : "", flags);

	return printed;
}



struct attr_fprintf_args {
	size_t size, printed;
	char *bf;
	bool first;
};

static int attr__fprintf(FILE *fp __maybe_unused, const char *name, const char *val, void *priv)
{
	struct attr_fprintf_args *args = priv;
	size_t printed = scnprintf(args->bf + args->printed , args->size - args->printed, "%s%s: %s", args->first ? "" : ", ", name, val);

	args->first = false;
	args->printed += printed;
	return printed;
}

static size_t perf_event_attr___scnprintf(struct perf_event_attr *attr, char *bf, size_t size, bool show_zeros __maybe_unused)
{
	struct attr_fprintf_args args = {
		.printed = scnprintf(bf, size, "{ "),
		.size    = size,
		.first   = true,
		.bf	 = bf,
	};

	perf_event_attr__fprintf(stdout, attr, attr__fprintf, &args);
	return args.printed + scnprintf(bf + args.printed, size - args.printed, " }");
}

static size_t syscall_arg__scnprintf_augmented_perf_event_attr(struct syscall_arg *arg, char *bf, size_t size)
{
	const struct augmented_arg *augmented_arg = arg->augmented.args;
	struct perf_event_attr local_attr;
	size_t copied = (size_t)augmented_arg->size;

	/* Zero pad, as the tracee's attr may be smaller than perf's. */
	if (copied > sizeof(local_attr))
		copied = sizeof(local_attr);

	memcpy(&local_attr, augmented_arg->value, copied);
	memset((void *)&local_attr + copied, 0, sizeof(local_attr) - copied);

	/* The BPF program copies PERF_ATTR_SIZE_VER0 bytes for size 0. */
	if (!local_attr.size)
		local_attr.size = PERF_ATTR_SIZE_VER0;

	return perf_event_attr___scnprintf(&local_attr, bf, size,
					   trace__show_zeros(arg->trace));
}

size_t syscall_arg__scnprintf_perf_event_attr(char *bf, size_t size, struct syscall_arg *arg)
{
	if (syscall_arg__augmented_args_valid(arg, PERF_ATTR_SIZE_VER0))
		return syscall_arg__scnprintf_augmented_perf_event_attr(arg, bf, size);

	return scnprintf(bf, size, "%#lx", arg->val);
}
