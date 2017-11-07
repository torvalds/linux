/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KCOV_H
#define _LINUX_KCOV_H

#include <uapi/linux/kcov.h>

struct task_struct;

#ifdef CONFIG_KCOV

void kcov_task_init(struct task_struct *t);
void kcov_task_exit(struct task_struct *t);

enum kcov_mode {
	/* Coverage collection is not enabled yet. */
	KCOV_MODE_DISABLED = 0,
	/*
	 * Tracing coverage collection mode.
	 * Covered PCs are collected in a per-task buffer.
	 */
	KCOV_MODE_TRACE = 1,
};

#else

static inline void kcov_task_init(struct task_struct *t) {}
static inline void kcov_task_exit(struct task_struct *t) {}

#endif /* CONFIG_KCOV */
#endif /* _LINUX_KCOV_H */
