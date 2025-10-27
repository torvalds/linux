/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
#ifndef _LINUX_RSEQ_H
#define _LINUX_RSEQ_H

#ifdef CONFIG_RSEQ
#include <linux/sched.h>

void __rseq_handle_notify_resume(struct ksignal *sig, struct pt_regs *regs);

static inline void rseq_handle_notify_resume(struct pt_regs *regs)
{
	if (current->rseq)
		__rseq_handle_notify_resume(NULL, regs);
}

static inline void rseq_signal_deliver(struct ksignal *ksig, struct pt_regs *regs)
{
	if (current->rseq) {
		current->rseq_event_pending = true;
		__rseq_handle_notify_resume(ksig, regs);
	}
}

static inline void rseq_sched_switch_event(struct task_struct *t)
{
	if (t->rseq) {
		t->rseq_event_pending = true;
		set_tsk_thread_flag(t, TIF_NOTIFY_RESUME);
	}
}

static __always_inline void rseq_exit_to_user_mode(void)
{
	if (IS_ENABLED(CONFIG_DEBUG_RSEQ)) {
		if (WARN_ON_ONCE(current->rseq && current->rseq_event_pending))
			current->rseq_event_pending = false;
	}
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
		t->rseq_event_pending = false;
	} else {
		t->rseq = current->rseq;
		t->rseq_len = current->rseq_len;
		t->rseq_sig = current->rseq_sig;
		t->rseq_event_pending = current->rseq_event_pending;
	}
}

static inline void rseq_execve(struct task_struct *t)
{
	t->rseq = NULL;
	t->rseq_len = 0;
	t->rseq_sig = 0;
	t->rseq_event_pending = false;
}

#else /* CONFIG_RSEQ */
static inline void rseq_handle_notify_resume(struct pt_regs *regs) { }
static inline void rseq_signal_deliver(struct ksignal *ksig, struct pt_regs *regs) { }
static inline void rseq_sched_switch_event(struct task_struct *t) { }
static inline void rseq_fork(struct task_struct *t, u64 clone_flags) { }
static inline void rseq_execve(struct task_struct *t) { }
static inline void rseq_exit_to_user_mode(void) { }
#endif  /* !CONFIG_RSEQ */

#ifdef CONFIG_DEBUG_RSEQ
void rseq_syscall(struct pt_regs *regs);
#else /* CONFIG_DEBUG_RSEQ */
static inline void rseq_syscall(struct pt_regs *regs) { }
#endif /* !CONFIG_DEBUG_RSEQ */

#endif /* _LINUX_RSEQ_H */
