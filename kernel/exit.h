// SPDX-License-Identifier: GPL-2.0-only
#ifndef LINUX_WAITID_H
#define LINUX_WAITID_H

struct waitid_info {
	pid_t pid;
	uid_t uid;
	int status;
	int cause;
};

struct wait_opts {
	enum pid_type		wo_type;
	int			wo_flags;
	struct pid		*wo_pid;

	struct waitid_info	*wo_info;
	int			wo_stat;
	struct rusage		*wo_rusage;

	wait_queue_entry_t		child_wait;
	int			notask_error;
};

bool pid_child_should_wake(struct wait_opts *wo, struct task_struct *p);
long __do_wait(struct wait_opts *wo);
int kernel_waitid_prepare(struct wait_opts *wo, int which, pid_t upid,
			  struct waitid_info *infop, int options,
			  struct rusage *ru);
#endif
