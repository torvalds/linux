/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
#ifndef _LINUX_RSEQ_H
#define _LINUX_RSEQ_H

#ifdef CONFIG_RSEQ
#include <linux/sched.h>

#include <uapi/linux/rseq.h>

void __rseq_handle_slowpath(struct pt_regs *regs);

/* Invoked from resume_user_mode_work() */
static inline void rseq_handle_slowpath(struct pt_regs *regs)
{
	if (IS_ENABLED(CONFIG_GENERIC_ENTRY)) {
		if (current->rseq.event.slowpath)
			__rseq_handle_slowpath(regs);
	} else {
		/* '&' is intentional to spare one conditional branch */
		if (current->rseq.event.sched_switch & current->rseq.event.has_rseq)
			__rseq_handle_slowpath(regs);
	}
}

void __rseq_signal_deliver(int sig, struct pt_regs *regs);

/*
 * Invoked from signal delivery to fixup based on the register context before
 * switching to the signal delivery context.
 */
static inline void rseq_signal_deliver(struct ksignal *ksig, struct pt_regs *regs)
{
	if (IS_ENABLED(CONFIG_GENERIC_IRQ_ENTRY)) {
		/* '&' is intentional to spare one conditional branch */
		if (current->rseq.event.has_rseq & current->rseq.event.user_irq)
			__rseq_signal_deliver(ksig->sig, regs);
	} else {
		if (current->rseq.event.has_rseq)
			__rseq_signal_deliver(ksig->sig, regs);
	}
}

static inline void rseq_raise_notify_resume(struct task_struct *t)
{
	set_tsk_thread_flag(t, TIF_RSEQ);
}

/* Invoked from context switch to force evaluation on exit to user */
static __always_inline void rseq_sched_switch_event(struct task_struct *t)
{
	struct rseq_event *ev = &t->rseq.event;

	if (IS_ENABLED(CONFIG_GENERIC_IRQ_ENTRY)) {
		/*
		 * Avoid a boat load of conditionals by using simple logic
		 * to determine whether NOTIFY_RESUME needs to be raised.
		 *
		 * It's required when the CPU or MM CID has changed or
		 * the entry was from user space.
		 */
		bool raise = (ev->user_irq | ev->ids_changed) & ev->has_rseq;

		if (raise) {
			ev->sched_switch = true;
			rseq_raise_notify_resume(t);
		}
	} else {
		if (ev->has_rseq) {
			t->rseq.event.sched_switch = true;
			rseq_raise_notify_resume(t);
		}
	}
}

/*
 * Invoked from __set_task_cpu() when a task migrates or from
 * mm_cid_schedin() when the CID changes to enforce an IDs update.
 *
 * This does not raise TIF_NOTIFY_RESUME as that happens in
 * rseq_sched_switch_event().
 */
static __always_inline void rseq_sched_set_ids_changed(struct task_struct *t)
{
	t->rseq.event.ids_changed = true;
}

/* Enforce a full update after RSEQ registration and when execve() failed */
static inline void rseq_force_update(void)
{
	if (current->rseq.event.has_rseq) {
		current->rseq.event.ids_changed = true;
		current->rseq.event.sched_switch = true;
		rseq_raise_notify_resume(current);
	}
}

/*
 * KVM/HYPERV invoke resume_user_mode_work() before entering guest mode,
 * which clears TIF_NOTIFY_RESUME on architectures that don't use the
 * generic TIF bits and therefore can't provide a separate TIF_RSEQ flag.
 *
 * To avoid updating user space RSEQ in that case just to do it eventually
 * again before returning to user space, because __rseq_handle_slowpath()
 * does nothing when invoked with NULL register state.
 *
 * After returning from guest mode, before exiting to userspace, hypervisors
 * must invoke this function to re-raise TIF_NOTIFY_RESUME if necessary.
 */
static inline void rseq_virt_userspace_exit(void)
{
	/*
	 * The generic optimization for deferring RSEQ updates until the next
	 * exit relies on having a dedicated TIF_RSEQ.
	 */
	if (!IS_ENABLED(CONFIG_HAVE_GENERIC_TIF_BITS) &&
	    current->rseq.event.sched_switch)
		rseq_raise_notify_resume(current);
}

static inline void rseq_reset(struct task_struct *t)
{
	memset(&t->rseq, 0, sizeof(t->rseq));
	t->rseq.ids.cpu_id = RSEQ_CPU_ID_UNINITIALIZED;
}

static inline void rseq_execve(struct task_struct *t)
{
	rseq_reset(t);
}

/*
 * If parent process has a registered restartable sequences area, the
 * child inherits. Unregister rseq for a clone with CLONE_VM set.
 *
 * On fork, keep the IDs (CPU, MMCID) of the parent, which avoids a fault
 * on the COW page on exit to user space, when the child stays on the same
 * CPU as the parent. That's obviously not guaranteed, but in overcommit
 * scenarios it is more likely and optimizes for the fork/exec case without
 * taking the fault.
 */
static inline void rseq_fork(struct task_struct *t, u64 clone_flags)
{
	if (clone_flags & CLONE_VM)
		rseq_reset(t);
	else
		t->rseq = current->rseq;
}

#else /* CONFIG_RSEQ */
static inline void rseq_handle_slowpath(struct pt_regs *regs) { }
static inline void rseq_signal_deliver(struct ksignal *ksig, struct pt_regs *regs) { }
static inline void rseq_sched_switch_event(struct task_struct *t) { }
static inline void rseq_sched_set_ids_changed(struct task_struct *t) { }
static inline void rseq_force_update(void) { }
static inline void rseq_virt_userspace_exit(void) { }
static inline void rseq_fork(struct task_struct *t, u64 clone_flags) { }
static inline void rseq_execve(struct task_struct *t) { }
#endif  /* !CONFIG_RSEQ */

#ifdef CONFIG_DEBUG_RSEQ
void rseq_syscall(struct pt_regs *regs);
#else /* CONFIG_DEBUG_RSEQ */
static inline void rseq_syscall(struct pt_regs *regs) { }
#endif /* !CONFIG_DEBUG_RSEQ */

#endif /* _LINUX_RSEQ_H */
