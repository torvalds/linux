/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PERF_TARGET_H
#define _PERF_TARGET_H

#include <stdbool.h>
#include <sys/types.h>

struct target {
	const char   *pid;
	const char   *tid;
	const char   *cpu_list;
	const char   *uid_str;
	const char   *bpf_str;
	uid_t	     uid;
	bool	     system_wide;
	bool	     uses_mmap;
	bool	     default_per_cpu;
	bool	     per_thread;
	bool	     use_bpf;
	bool	     hybrid;
	int	     initial_delay;
	const char   *attr_map;
};

enum target_errno {
	TARGET_ERRNO__SUCCESS		= 0,

	/*
	 * Choose an arbitrary negative big number not to clash with standard
	 * errno since SUS requires the errno has distinct positive values.
	 * See 'Issue 6' in the link below.
	 *
	 * http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/errno.h.html
	 */
	__TARGET_ERRNO__START		= -10000,

	/* for target__validate() */
	TARGET_ERRNO__PID_OVERRIDE_CPU	= __TARGET_ERRNO__START,
	TARGET_ERRNO__PID_OVERRIDE_UID,
	TARGET_ERRNO__UID_OVERRIDE_CPU,
	TARGET_ERRNO__PID_OVERRIDE_SYSTEM,
	TARGET_ERRNO__UID_OVERRIDE_SYSTEM,
	TARGET_ERRNO__SYSTEM_OVERRIDE_THREAD,
	TARGET_ERRNO__BPF_OVERRIDE_CPU,
	TARGET_ERRNO__BPF_OVERRIDE_PID,
	TARGET_ERRNO__BPF_OVERRIDE_UID,
	TARGET_ERRNO__BPF_OVERRIDE_THREAD,

	/* for target__parse_uid() */
	TARGET_ERRNO__INVALID_UID,
	TARGET_ERRNO__USER_NOT_FOUND,

	__TARGET_ERRNO__END,
};

enum target_errno target__validate(struct target *target);
enum target_errno target__parse_uid(struct target *target);

int target__strerror(struct target *target, int errnum, char *buf, size_t buflen);

static inline bool target__has_task(struct target *target)
{
	return target->tid || target->pid || target->uid_str;
}

static inline bool target__has_cpu(struct target *target)
{
	return target->system_wide || target->cpu_list;
}

static inline bool target__none(struct target *target)
{
	return !target__has_task(target) && !target__has_cpu(target);
}

static inline bool target__enable_on_exec(struct target *target)
{
	/*
	 * Normally enable_on_exec should be set if:
	 *  1) The tracee process is forked (not attaching to existed task or cpu).
	 *  2) And initial_delay is not configured.
	 * Otherwise, we enable tracee events manually.
	 */
	return target__none(target) && !target->initial_delay;
}

static inline bool target__has_per_thread(struct target *target)
{
	return target->system_wide && target->per_thread;
}

static inline bool target__uses_dummy_map(struct target *target)
{
	bool use_dummy = false;

	if (target->default_per_cpu)
		use_dummy = target->per_thread ? true : false;
	else if (target__has_task(target) ||
	         (!target__has_cpu(target) && !target->uses_mmap))
		use_dummy = true;
	else if (target__has_per_thread(target))
		use_dummy = true;

	return use_dummy;
}

#endif /* _PERF_TARGET_H */
