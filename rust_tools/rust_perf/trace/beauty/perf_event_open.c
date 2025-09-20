// SPDX-License-Identifier: LGPL-2.1
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

static size_t syscall_arg__scnprintf_perf_flags(char *bf, size_t size,
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

#define SCA_PERF_FLAGS syscall_arg__scnprintf_perf_flags

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
	return perf_event_attr___scnprintf((void *)arg->augmented.args->value, bf, size, arg->trace->show_zeros);
}

static size_t syscall_arg__scnprintf_perf_event_attr(char *bf, size_t size, struct syscall_arg *arg)
{
	if (arg->augmented.args)
		return syscall_arg__scnprintf_augmented_perf_event_attr(arg, bf, size);

	return scnprintf(bf, size, "%#lx", arg->val);
}

#define SCA_PERF_ATTR syscall_arg__scnprintf_perf_event_attr
// 'argname' is just documentational at this point, to remove the previous comment with that info
#define SCA_PERF_ATTR_FROM_USER(argname) \
          { .scnprintf  = SCA_PERF_ATTR, \
            .from_user  = true, }
