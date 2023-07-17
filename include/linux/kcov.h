/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KCOV_H
#define _LINUX_KCOV_H

#include <linux/sched.h>
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

/* See Documentation/dev-tools/kcov.rst for usage details. */
void kcov_remote_start(u64 handle);
void kcov_remote_stop(void);
u64 kcov_common_handle(void);

static inline void kcov_remote_start_common(u64 id)
{
	kcov_remote_start(kcov_remote_handle(KCOV_SUBSYSTEM_COMMON, id));
}

static inline void kcov_remote_start_usb(u64 id)
{
	kcov_remote_start(kcov_remote_handle(KCOV_SUBSYSTEM_USB, id));
}

/*
 * The softirq flavor of kcov_remote_*() functions is introduced as a temporary
 * work around for kcov's lack of nested remote coverage sections support in
 * task context. Adding support for nested sections is tracked in:
 * https://bugzilla.kernel.org/show_bug.cgi?id=210337
 */

static inline void kcov_remote_start_usb_softirq(u64 id)
{
	if (in_serving_softirq())
		kcov_remote_start_usb(id);
}

static inline void kcov_remote_stop_softirq(void)
{
	if (in_serving_softirq())
		kcov_remote_stop();
}

#ifdef CONFIG_64BIT
typedef unsigned long kcov_u64;
#else
typedef unsigned long long kcov_u64;
#endif

void __sanitizer_cov_trace_pc(void);
void __sanitizer_cov_trace_cmp1(u8 arg1, u8 arg2);
void __sanitizer_cov_trace_cmp2(u16 arg1, u16 arg2);
void __sanitizer_cov_trace_cmp4(u32 arg1, u32 arg2);
void __sanitizer_cov_trace_cmp8(kcov_u64 arg1, kcov_u64 arg2);
void __sanitizer_cov_trace_const_cmp1(u8 arg1, u8 arg2);
void __sanitizer_cov_trace_const_cmp2(u16 arg1, u16 arg2);
void __sanitizer_cov_trace_const_cmp4(u32 arg1, u32 arg2);
void __sanitizer_cov_trace_const_cmp8(kcov_u64 arg1, kcov_u64 arg2);
void __sanitizer_cov_trace_switch(kcov_u64 val, void *cases);

#else

static inline void kcov_task_init(struct task_struct *t) {}
static inline void kcov_task_exit(struct task_struct *t) {}
static inline void kcov_prepare_switch(struct task_struct *t) {}
static inline void kcov_finish_switch(struct task_struct *t) {}
static inline void kcov_remote_start(u64 handle) {}
static inline void kcov_remote_stop(void) {}
static inline u64 kcov_common_handle(void)
{
	return 0;
}
static inline void kcov_remote_start_common(u64 id) {}
static inline void kcov_remote_start_usb(u64 id) {}
static inline void kcov_remote_start_usb_softirq(u64 id) {}
static inline void kcov_remote_stop_softirq(void) {}

#endif /* CONFIG_KCOV */
#endif /* _LINUX_KCOV_H */
