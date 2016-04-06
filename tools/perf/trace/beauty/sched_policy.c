#include <sched.h>

/*
 * Not defined anywhere else, probably, just to make sure we
 * catch future flags
 */
#define SCHED_POLICY_MASK 0xff

#ifndef SCHED_DEADLINE
#define SCHED_DEADLINE 6
#endif

static size_t syscall_arg__scnprintf_sched_policy(char *bf, size_t size,
						  struct syscall_arg *arg)
{
	const char *policies[] = {
		"NORMAL", "FIFO", "RR", "BATCH", "ISO", "IDLE", "DEADLINE",
	};
	size_t printed;
	int policy = arg->val,
	    flags = policy & ~SCHED_POLICY_MASK;

	policy &= SCHED_POLICY_MASK;
	if (policy <= SCHED_DEADLINE)
		printed = scnprintf(bf, size, "%s", policies[policy]);
	else
		printed = scnprintf(bf, size, "%#x", policy);

#define	P_POLICY_FLAG(n) \
	if (flags & SCHED_##n) { \
		printed += scnprintf(bf + printed, size - printed, "|%s", #n); \
		flags &= ~SCHED_##n; \
	}

	P_POLICY_FLAG(RESET_ON_FORK);
#undef P_POLICY_FLAG

	if (flags)
		printed += scnprintf(bf + printed, size - printed, "|%#x", flags);

	return printed;
}

#define SCA_SCHED_POLICY syscall_arg__scnprintf_sched_policy
