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
	int printed = 0, flags = arg->val;

	if (flags == 0)
		return 0;

#define	P_FLAG(n) \
	if (flags & PERF_FLAG_##n) { \
		printed += scnprintf(bf + printed, size - printed, "%s%s", printed ? "|" : "", #n); \
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
