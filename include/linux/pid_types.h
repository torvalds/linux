/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PID_TYPES_H
#define _LINUX_PID_TYPES_H

enum pid_type {
	PIDTYPE_PID,
	PIDTYPE_TGID,
	PIDTYPE_PGID,
	PIDTYPE_SID,
	PIDTYPE_MAX,
};

struct pid_namespace;
extern struct pid_namespace init_pid_ns;

#endif /* _LINUX_PID_TYPES_H */
