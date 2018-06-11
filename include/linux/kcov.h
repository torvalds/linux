/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KCOV_H
#define _LINUX_KCOV_H

#include <uapi/linux/kcov.h>

struct task_struct;

#ifdef CONFIG_KCOV

#define KCOV_PER_THREAD_AREA_SIZE (PAGE_SIZE << 4)

enum kcov_mode {
	/* Coverage collection is not enabled yet. */
	KCOV_MODE_DISABLED = 0,
	/* KCOV was initialized, but tracing mode hasn't been chosen yet. */
	KCOV_MODE_INIT = 1,
	/*
	 * Tracing coverage collection mode.
	 * Covered PCs are collected in a per-task buffer.
	 */
	KCOV_MODE_TRACE_PC = 2,
	/* Collecting comparison operands mode. */
	KCOV_MODE_TRACE_CMP = 3,
};

void kcov_task_init(struct task_struct *t);
void kcov_task_exit(struct task_struct *t);

void kcov_remote_start(u64 handle);
void kcov_remote_stop(void);

#else

static inline void kcov_task_init(struct task_struct *t) {}
static inline void kcov_task_exit(struct task_struct *t) {}

static inline void kcov_remote_start(u64 handle) {}
static inline void kcov_remote_stop(void) {}

#endif /* CONFIG_KCOV */
#endif /* _LINUX_KCOV_H */
