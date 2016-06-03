#include <linux/seccomp.h>

#ifndef SECCOMP_SET_MODE_STRICT
#define SECCOMP_SET_MODE_STRICT 0
#endif
#ifndef SECCOMP_SET_MODE_FILTER
#define SECCOMP_SET_MODE_FILTER 1
#endif

static size_t syscall_arg__scnprintf_seccomp_op(char *bf, size_t size, struct syscall_arg *arg)
{
	int op = arg->val;
	size_t printed = 0;

	switch (op) {
#define	P_SECCOMP_SET_MODE_OP(n) case SECCOMP_SET_MODE_##n: printed = scnprintf(bf, size, #n); break
	P_SECCOMP_SET_MODE_OP(STRICT);
	P_SECCOMP_SET_MODE_OP(FILTER);
#undef P_SECCOMP_SET_MODE_OP
	default: printed = scnprintf(bf, size, "%#x", op);			  break;
	}

	return printed;
}

#define SCA_SECCOMP_OP  syscall_arg__scnprintf_seccomp_op

#ifndef SECCOMP_FILTER_FLAG_TSYNC
#define SECCOMP_FILTER_FLAG_TSYNC 1
#endif

static size_t syscall_arg__scnprintf_seccomp_flags(char *bf, size_t size,
						   struct syscall_arg *arg)
{
	int printed = 0, flags = arg->val;

#define	P_FLAG(n) \
	if (flags & SECCOMP_FILTER_FLAG_##n) { \
		printed += scnprintf(bf + printed, size - printed, "%s%s", printed ? "|" : "", #n); \
		flags &= ~SECCOMP_FILTER_FLAG_##n; \
	}

	P_FLAG(TSYNC);
#undef P_FLAG

	if (flags)
		printed += scnprintf(bf + printed, size - printed, "%s%#x", printed ? "|" : "", flags);

	return printed;
}

#define SCA_SECCOMP_FLAGS syscall_arg__scnprintf_seccomp_flags
