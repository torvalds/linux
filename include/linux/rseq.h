/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
#ifndef _LINUX_RSEQ_H
#define _LINUX_RSEQ_H

#ifdef CONFIG_RSEQ
#include <linux/sched.h>

void __rseq_handle_notify_resume(struct ksignal *sig, struct pt_regs *regs);

static inline void rseq_handle_notify_resume(struct pt_regs *regs)
{
	if (current->rseq.event.has_rseq)
		__rseq_handle_notify_resume(NULL, regs);
}

static inline void rseq_signal_deliver(struct ksignal *ksig, struct pt_regs *regs)
{
	if (current->rseq.event.has_rseq) {
		current->rseq.event.sched_switch = true;
		__rseq_handle_notify_resume(ksig, regs);
	}
}

static inline void rseq_sched_switch_event(struct task_struct *t)
{
	if (t->rseq.event.has_rseq) {
		t->rseq.event.sched_switch = true;
		set_tsk_thread_flag(t, TIF_NOTIFY_RESUME);
	}
}

static __always_inline void rseq_exit_to_user_mode(void)
{
	struct rseq_event *ev = &current->rseq.event;

	if (IS_ENABLED(CONFIG_DEBUG_RSEQ))
		WARN_ON_ONCE(ev->sched_switch);

	/*
	 * Ensure that event (especially user_irq) is cleared when the
	 * interrupt did not result in a schedule and therefore the
	 * rseq processing did not clear it.
	 */
	ev->events = 0;
}

/*
 * KVM/HYPERV invoke resume_user_mode_work() before entering guest mode,
 * which clears TIF_NOTIFY_RESUME. To avoid updating user space RSEQ in
 * that case just to do it eventually again before returning to user space,
 * the entry resume_user_mode_work() invocation is ignored as the register
 * argument is NULL.
 *
 * After returning from guest mode, they have to invoke this function to
 * re-raise TIF_NOTIFY_RESUME if necessary.
 */
static inline void rseq_virt_userspace_exit(void)
{
	if (current->rseq.event.sched_switch)
		set_tsk_thread_flag(current, TIF_NOTIFY_RESUME);
}

static inline void rseq_reset(struct task_struct *t)
{
	memset(&t->rseq, 0, sizeof(t->rseq));
	t->rseq.ids.cpu_cid = ~0ULL;
}

static inline void rseq_execve(struct task_struct *t)
{
	rseq_reset(t);
}

/*
 * If parent process has a registered restartable sequences area, the
 * child inherits. Unregister rseq for a clone with CLONE_VM set.
 */
static inline void rseq_fork(struct task_struct *t, u64 clone_flags)
{
	if (clone_flags & CLONE_VM) {
		rseq_reset(t);
	} else {
		t->rseq = current->rseq;
		t->rseq.ids.cpu_cid = ~0ULL;
	}
}

#else /* CONFIG_RSEQ */
static inline void rseq_handle_notify_resume(struct pt_regs *regs) { }
static inline void rseq_signal_deliver(struct ksignal *ksig, struct pt_regs *regs) { }
static inline void rseq_sched_switch_event(struct task_struct *t) { }
static inline void rseq_virt_userspace_exit(void) { }
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
