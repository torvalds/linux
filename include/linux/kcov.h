/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KCOV_H
#define _LINUX_KCOV_H

#include <uapi/linux/kcov.h>

struct task_struct;

#ifdef CONFIG_KCOV

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

#define KCOV_IN_CTXSW	(1 << 30)

void kcov_task_init(struct task_struct *t);
void kcov_task_exit(struct task_struct *t);

#define kcov_prepare_switch(t)			\
do {						\
	(t)->kcov_mode |= KCOV_IN_CTXSW;	\
} while (0)

#define kcov_finish_switch(t)			\
do {						\
	(t)->kcov_mode &= ~KCOV_IN_CTXSW;	\
} while (0)

#else

static inline void kcov_task_init(struct task_struct *t) {}
static inline void kcov_task_exit(struct task_struct *t) {}
static inline void kcov_prepare_switch(struct task_struct *t) {}
static inline void kcov_finish_switch(struct task_struct *t) {}

#endif /* CONFIG_KCOV */
#endif /* _LINUX_KCOV_H */
