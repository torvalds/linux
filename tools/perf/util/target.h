#ifndef _PERF_TARGET_H
#define _PERF_TARGET_H

#include <stdbool.h>
#include <sys/types.h>

struct perf_target {
	const char   *pid;
	const char   *tid;
	const char   *cpu_list;
	const char   *uid_str;
	uid_t	     uid;
	bool	     system_wide;
	bool	     uses_mmap;
};

enum perf_target_errno {
	PERF_ERRNO_TARGET__SUCCESS		= 0,

	/*
	 * Choose an arbitrary negative big number not to clash with standard
	 * errno since SUS requires the errno has distinct positive values.
	 * See 'Issue 6' in the link below.
	 *
	 * http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/errno.h.html
	 */
	__PERF_ERRNO_TARGET__START		= -10000,


	/* for perf_target__validate() */
	PERF_ERRNO_TARGET__PID_OVERRIDE_CPU	= __PERF_ERRNO_TARGET__START,
	PERF_ERRNO_TARGET__PID_OVERRIDE_UID,
	PERF_ERRNO_TARGET__UID_OVERRIDE_CPU,
	PERF_ERRNO_TARGET__PID_OVERRIDE_SYSTEM,
	PERF_ERRNO_TARGET__UID_OVERRIDE_SYSTEM,

	/* for perf_target__parse_uid() */
	PERF_ERRNO_TARGET__INVALID_UID,
	PERF_ERRNO_TARGET__USER_NOT_FOUND,

	__PERF_ERRNO_TARGET__END,
};

enum perf_target_errno perf_target__validate(struct perf_target *target);
enum perf_target_errno perf_target__parse_uid(struct perf_target *target);

int perf_target__strerror(struct perf_target *target, int errnum, char *buf,
			  size_t buflen);

static inline bool perf_target__has_task(struct perf_target *target)
{
	return target->tid || target->pid || target->uid_str;
}

static inline bool perf_target__has_cpu(struct perf_target *target)
{
	return target->system_wide || target->cpu_list;
}

static inline bool perf_target__none(struct perf_target *target)
{
	return !perf_target__has_task(target) && !perf_target__has_cpu(target);
}

#endif /* _PERF_TARGET_H */
