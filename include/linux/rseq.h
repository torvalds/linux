/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
#ifndef _LINUX_RSEQ_H
#define _LINUX_RSEQ_H

#ifdef CONFIG_RSEQ

#include <linux/preempt.h>
#include <linux/sched.h>

#ifdef CONFIG_MEMBARRIER
# define RSEQ_EVENT_GUARD	irq
#else
# define RSEQ_EVENT_GUARD	preempt
#endif

/*
 * Map the event mask on the user-space ABI enum rseq_cs_flags
 * for direct mask checks.
 */
enum rseq_event_mask_bits {
	RSEQ_EVENT_PREEMPT_BIT	= RSEQ_CS_FLAG_NO_RESTART_ON_PREEMPT_BIT,
	RSEQ_EVENT_SIGNAL_BIT	= RSEQ_CS_FLAG_NO_RESTART_ON_SIGNAL_BIT,
	RSEQ_EVENT_MIGRATE_BIT	= RSEQ_CS_FLAG_NO_RESTART_ON_MIGRATE_BIT,
};

enum rseq_event_mask {
	RSEQ_EVENT_PREEMPT	= (1U << RSEQ_EVENT_PREEMPT_BIT),
	RSEQ_EVENT_SIGNAL	= (1U << RSEQ_EVENT_SIGNAL_BIT),
	RSEQ_EVENT_MIGRATE	= (1U << RSEQ_EVENT_MIGRATE_BIT),
};

static inline void rseq_set_notify_resume(struct task_struct *t)
{
	if (t->rseq)
		set_tsk_thread_flag(t, TIF_NOTIFY_RESUME);
}

void __rseq_handle_notify_resume(struct ksignal *sig, struct pt_regs *regs);

static inline void rseq_handle_notify_resume(struct ksignal *ksig,
					     struct pt_regs *regs)
{
	if (current->rseq)
		__rseq_handle_notify_resume(ksig, regs);
}

static inline void rseq_signal_deliver(struct ksignal *ksig,
				       struct pt_regs *regs)
{
	scoped_guard(RSEQ_EVENT_GUARD)
		__set_bit(RSEQ_EVENT_SIGNAL_BIT, &current->rseq_event_mask);
	rseq_handle_notify_resume(ksig, regs);
}

/* rseq_preempt() requires preemption to be disabled. */
static inline void rseq_preempt(struct task_struct *t)
{
	__set_bit(RSEQ_EVENT_PREEMPT_BIT, &t->rseq_event_mask);
	rseq_set_notify_resume(t);
}

/* rseq_migrate() requires preemption to be disabled. */
static inline void rseq_migrate(struct task_struct *t)
{
	__set_bit(RSEQ_EVENT_MIGRATE_BIT, &t->rseq_event_mask);
	rseq_set_notify_resume(t);
}

/*
 * If parent process has a registered restartable sequences area, the
 * child inherits. Unregister rseq for a clone with CLONE_VM set.
 */
static inline void rseq_fork(struct task_struct *t, u64 clone_flags)
{
	if (clone_flags & CLONE_VM) {
		t->rseq = NULL;
		t->rseq_len = 0;
		t->rseq_sig = 0;
		t->rseq_event_mask = 0;
	} else {
		t->rseq = current->rseq;
		t->rseq_len = current->rseq_len;
		t->rseq_sig = current->rseq_sig;
		t->rseq_event_mask = current->rseq_event_mask;
	}
}

static inline void rseq_execve(struct task_struct *t)
{
	t->rseq = NULL;
	t->rseq_len = 0;
	t->rseq_sig = 0;
	t->rseq_event_mask = 0;
}

#else

static inline void rseq_set_notify_resume(struct task_struct *t)
{
}
static inline void rseq_handle_notify_resume(struct ksignal *ksig,
					     struct pt_regs *regs)
{
}
static inline void rseq_signal_deliver(struct ksignal *ksig,
				       struct pt_regs *regs)
{
}
static inline void rseq_preempt(struct task_struct *t)
{
}
static inline void rseq_migrate(struct task_struct *t)
{
}
static inline void rseq_fork(struct task_struct *t, u64 clone_flags)
{
}
static inline void rseq_execve(struct task_struct *t)
{
}

#endif

#ifdef CONFIG_DEBUG_RSEQ

void rseq_syscall(struct pt_regs *regs);

#else

static inline void rseq_syscall(struct pt_regs *regs)
{
}

#endif

#endif /* _LINUX_RSEQ_H */
