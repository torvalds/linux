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
	/* The process owns a KCOV remote reference. */
	KCOV_MODE_REMOTE = 4,
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
 * workaround for KCOV's lack of nested remote coverage sections support.
 *
 * Adding support is tracked in https://bugzilla.kernel.org/show_bug.cgi?id=210337.
 *
 * kcov_remote_start_usb_softirq():
 *
 * 1. Only collects coverage when called in the softirq context. This allows
 *    avoiding nested remote coverage collection sections in the task context.
 *    For example, USB/IP calls usb_hcd_giveback_urb() in the task context
 *    within an existing remote coverage collection section. Thus, KCOV should
 *    not attempt to start collecting coverage within the coverage collection
 *    section in __usb_hcd_giveback_urb() in this case.
 *
 * 2. Disables interrupts for the duration of the coverage collection section.
 *    This allows avoiding nested remote coverage collection sections in the
 *    softirq context (a softirq might occur during the execution of a work in
 *    the BH workqueue, which runs with in_serving_softirq() > 0).
 *    For example, usb_giveback_urb_bh() runs in the BH workqueue with
 *    interrupts enabled, so __usb_hcd_giveback_urb() might be interrupted in
 *    the middle of its remote coverage collection section, and the interrupt
 *    handler might invoke __usb_hcd_giveback_urb() again.
 */

static inline unsigned long kcov_remote_start_usb_softirq(u64 id)
{
	unsigned long flags = 0;

	if (in_serving_softirq()) {
		local_irq_save(flags);
		kcov_remote_start_usb(id);
	}

	return flags;
}

static inline void kcov_remote_stop_softirq(unsigned long flags)
{
	if (in_serving_softirq()) {
		kcov_remote_stop();
		local_irq_restore(flags);
	}
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
static inline unsigned long kcov_remote_start_usb_softirq(u64 id)
{
	return 0;
}
static inline void kcov_remote_stop_softirq(unsigned long flags) {}

#endif /* CONFIG_KCOV */
#endif /* _LINUX_KCOV_H */
