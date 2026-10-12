/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PID_TYPES_H
#define _LINUX_PID_TYPES_H

#include <linux/build_bug.h>

enum pid_type {
	PIDTYPE_PID,
	PIDTYPE_TGID,
	PIDTYPE_PGID,
	PIDTYPE_SID,
	PIDTYPE_MAX,
};

struct pid;

/* An array of struct pid indexed by pid type, PIDTYPE_PID up to @last. */
#define DECLARE_PIDS(name, last)					\
	struct pid *name[(last) + 1 + BUILD_BUG_ON_ZERO((last) >= PIDTYPE_MAX)]

struct pid_namespace;
extern struct pid_namespace init_pid_ns;

#endif /* _LINUX_PID_TYPES_H */
