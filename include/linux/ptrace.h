/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PTRACE_H
#define _LINUX_PTRACE_H

#include <linux/compiler.h>		/* For unlikely.  */
#include <linux/sched.h>		/* For struct task_struct.  */
#include <linux/sched/signal.h>		/* For send_sig(), same_thread_group(), etc. */
#include <linux/err.h>			/* for IS_ERR_VALUE */
#include <linux/bug.h>			/* For BUG_ON.  */
#include <linux/pid_namespace.h>	/* For task_active_pid_ns.  */
#include <uapi/linux/ptrace.h>
#include <linux/seccomp.h>

/* Add sp to seccomp_data, as seccomp is user API, we don't want to modify it */
struct syscall_info {
	__u64			sp;
	struct seccomp_data	data;
};

extern int ptrace_access_vm(struct task_struct *tsk, unsigned long addr,
			    void *buf, int len, unsigned int gup_flags);

/*
 * Ptrace flags
 *
 * The owner ship rules for task->ptrace which holds the ptrace
 * flags is simple.  When a task is running it owns it's task->ptrace
 * flags.  When the a task is stopped the ptracer owns task->ptrace.
 */

#define PT_SEIZED	0x00010000	/* SEIZE used, enable new behavior */
#define PT_PTRACED	0x00000001

#define PT_OPT_FLAG_SHIFT	3
/* PT_TRACE_* event enable flags */
#define PT_EVENT_FLAG(event)	(1 << (PT_OPT_FLAG_SHIFT + (event)))
#define PT_TRACESYSGOOD		PT_EVENT_FLAG(0)
#define PT_TRACE_FORK		PT_EVENT_FLAG(PTRACE_EVENT_FORK)
#define PT_TRACE_VFORK		PT_EVENT_FLAG(PTRACE_EVENT_VFORK)
#define PT_TRACE_CLONE		PT_EVENT_FLAG(PTRACE_EVENT_CLONE)
#define PT_TRACE_EXEC		PT_EVENT_FLAG(PTRACE_EVENT_EXEC)
#define PT_TRACE_VFORK_DONE	PT_EVENT_FLAG(PTRACE_EVENT_VFORK_DONE)
#define PT_TRACE_EXIT		PT_EVENT_FLAG(PTRACE_EVENT_EXIT)
#define PT_TRACE_SECCOMP	PT_EVENT_FLAG(PTRACE_EVENT_SECCOMP)

#define PT_EXITKILL		(PTRACE_O_EXITKILL << PT_OPT_FLAG_SHIFT)
#define PT_SUSPEND_SECCOMP	(PTRACE_O_SUSPEND_SECCOMP << PT_OPT_FLAG_SHIFT)

extern long arch_ptrace(struct task_struct *child, long request,
			unsigned long addr, unsigned long data);
extern int ptrace_readdata(struct task_struct *tsk, unsigned long src, char __user *dst, int len);
extern int ptrace_writedata(struct task_struct *tsk, char __user *src, unsigned long dst, int len);
extern void ptrace_disable(struct task_struct *);
extern int ptrace_request(struct task_struct *child, long request,
			  unsigned long addr, unsigned long data);
extern int ptrace_notify(int exit_code, unsigned long message);
extern void __ptrace_link(struct task_struct *child,
			  struct task_struct *new_parent,
			  const struct cred *ptracer_cred);
extern void __ptrace_unlink(struct task_struct *child);
extern void exit_ptrace(struct task_struct *tracer, struct list_head *dead);
#define PTRACE_MODE_READ	0x01
#define PTRACE_MODE_ATTACH	0x02
#define PTRACE_MODE_NOAUDIT	0x04
#define PTRACE_MODE_FSCREDS	0x08
#define PTRACE_MODE_REALCREDS	0x10

/* shorthands for READ/ATTACH and FSCREDS/REALCREDS combinations */
#define PTRACE_MODE_READ_FSCREDS (PTRACE_MODE_READ | PTRACE_MODE_FSCREDS)
#define PTRACE_MODE_READ_REALCREDS (PTRACE_MODE_READ | PTRACE_MODE_REALCREDS)
#define PTRACE_MODE_ATTACH_FSCREDS (PTRACE_MODE_ATTACH | PTRACE_MODE_FSCREDS)
#define PTRACE_MODE_ATTACH_REALCREDS (PTRACE_MODE_ATTACH | PTRACE_MODE_REALCREDS)

/**
 * ptrace_may_access - check whether the caller is permitted to access
 * a target task.
 * @task: target task
 * @mode: selects type of access and caller credentials
 *
 * Returns true on success, false on denial.
 *
 * One of the flags PTRACE_MODE_FSCREDS and PTRACE_MODE_REALCREDS must
 * be set in @mode to specify whether the access was requested through
 * a filesystem syscall (should use effective capabilities and fsuid
 * of the caller) or through an explicit syscall such as
 * process_vm_writev or ptrace (and should use the real credentials).
 */
extern bool ptrace_may_access(struct task_struct *task, unsigned int mode);

static inline int ptrace_reparented(struct task_struct *child)
{
	return !same_thread_group(child->real_parent, child->parent);
}

static inline void ptrace_unlink(struct task_struct *child)
{
	if (unlikely(child->ptrace))
		__ptrace_unlink(child);
}

int generic_ptrace_peekdata(struct task_struct *tsk, unsigned long addr,
			    unsigned long data);
int generic_ptrace_pokedata(struct task_struct *tsk, unsigned long addr,
			    unsigned long data);

/**
 * ptrace_parent - return the task that is tracing the given task
 * @task: task to consider
 *
 * Returns %NULL if no one is tracing @task, or the &struct task_struct
 * pointer to its tracer.
 *
 * Must called under rcu_read_lock().  The pointer returned might be kept
 * live only by RCU.  During exec, this may be called with task_lock() held
 * on @task, still held from when check_unsafe_exec() was called.
 */
static inline struct task_struct *ptrace_parent(struct task_struct *task)
{
	if (unlikely(task->ptrace))
		return rcu_dereference(task->parent);
	return NULL;
}

/**
 * ptrace_event_enabled - test whether a ptrace event is enabled
 * @task: ptracee of interest
 * @event: %PTRACE_EVENT_* to test
 *
 * Test whether @event is enabled for ptracee @task.
 *
 * Returns %true if @event is enabled, %false otherwise.
 */
static inline bool ptrace_event_enabled(struct task_struct *task, int event)
{
	return task->ptrace & PT_EVENT_FLAG(event);
}

/**
 * ptrace_event - possibly stop for a ptrace event notification
 * @event:	%PTRACE_EVENT_* value to report
 * @message:	value for %PTRACE_GETEVENTMSG to return
 *
 * Check whether @event is enabled and, if so, report @event and @message
 * to the ptrace parent.
 *
 * Called without locks.
 */
static inline void ptrace_event(int event, unsigned long message)
{
	if (unlikely(ptrace_event_enabled(current, event))) {
		ptrace_notify((event << 8) | SIGTRAP, message);
	} else if (event == PTRACE_EVENT_EXEC) {
		/* legacy EXEC report via SIGTRAP */
		if ((current->ptrace & (PT_PTRACED|PT_SEIZED)) == PT_PTRACED)
			send_sig(SIGTRAP, current, 0);
	}
}

/**
 * ptrace_event_pid - possibly stop for a ptrace event notification
 * @event:	%PTRACE_EVENT_* value to report
 * @pid:	process identifier for %PTRACE_GETEVENTMSG to return
 *
 * Check whether @event is enabled and, if so, report @event and @pid
 * to the ptrace parent.  @pid is reported as the pid_t seen from the
 * ptrace parent's pid namespace.
 *
 * Called without locks.
 */
static inline void ptrace_event_pid(int event, struct pid *pid)
{
	/*
	 * FIXME: There's a potential race if a ptracer in a different pid
	 * namespace than parent attaches between computing message below and
	 * when we acquire tasklist_lock in ptrace_stop().  If this happens,
	 * the ptracer will get a bogus pid from PTRACE_GETEVENTMSG.
	 */
	unsigned long message = 0;
	struct pid_namespace *ns;

	rcu_read_lock();
	ns = task_active_pid_ns(rcu_dereference(current->parent));
	if (ns)
		message = pid_nr_ns(pid, ns);
	rcu_read_unlock();

	ptrace_event(event, message);
}

/**
 * ptrace_init_task - initialize ptrace state for a new child
 * @child:		new child task
 * @ptrace:		true if child should be ptrace'd by parent's tracer
 *
 * This is called immediately after adding @child to its parent's children
 * list.  @ptrace is false in the normal case, and true to ptrace @child.
 *
 * Called with current's siglock and write_lock_irq(&tasklist_lock) held.
 */
static inline void ptrace_init_task(struct task_struct *child, bool ptrace)
{
	INIT_LIST_HEAD(&child->ptrace_entry);
	INIT_LIST_HEAD(&child->ptraced);
	child->jobctl = 0;
	child->ptrace = 0;
	child->parent = child->real_parent;

	if (unlikely(ptrace) && current->ptrace) {
		child->ptrace = current->ptrace;
		__ptrace_link(child, current->parent, current->ptracer_cred);

		if (child->ptrace & PT_SEIZED)
			task_set_jobctl_pending(child, JOBCTL_TRAP_STOP);
		else
			sigaddset(&child->pending.signal, SIGSTOP);
	}
	else
		child->ptracer_cred = NULL;
}

/**
 * ptrace_release_task - final ptrace-related cleanup of a zombie being reaped
 * @task:	task in %EXIT_DEAD state
 *
 * Called with write_lock(&tasklist_lock) held.
 */
static inline void ptrace_release_task(struct task_struct *task)
{
	BUG_ON(!list_empty(&task->ptraced));
	ptrace_unlink(task);
	BUG_ON(!list_empty(&task->ptrace_entry));
}

#ifndef force_successful_syscall_return
/*
 * System call handlers that, upon successful completion, need to return a
 * negative value should call force_successful_syscall_return() right before
 * returning.  On architectures where the syscall convention provides for a
 * separate error flag (e.g., alpha, ia64, ppc{,64}, sparc{,64}, possibly
 * others), this macro can be used to ensure that the error flag will not get
 * set.  On architectures which do not support a separate error flag, the macro
 * is a no-op and the spurious error condition needs to be filtered out by some
 * other means (e.g., in user-level, by passing an extra argument to the
 * syscall handler, or something along those lines).
 */
#define force_successful_syscall_return() do { } while (0)
#endif

#ifndef is_syscall_success
/*
 * On most systems we can tell if a syscall is a success based on if the retval
 * is an error value.  On some systems like ia64 and powerpc they have different
 * indicators of success/failure and must define their own.
 */
#define is_syscall_success(regs) (!IS_ERR_VALUE((unsigned long)(regs_return_value(regs))))
#endif

/*
 * <asm/ptrace.h> should define the following things inside #ifdef __KERNEL__.
 *
 * These do-nothing inlines are used when the arch does not
 * implement single-step.  The kerneldoc comments are here
 * to document the interface for all arch definitions.
 */

#ifndef arch_has_single_step
/**
 * arch_has_single_step - does this CPU support user-mode single-step?
 *
 * If this is defined, then there must be function declarations or
 * inlines for user_enable_single_step() and user_disable_single_step().
 * arch_has_single_step() should evaluate to nonzero iff the machine
 * supports instruction single-step for user mode.
 * It can be a constant or it can test a CPU feature bit.
 */
#define arch_has_single_step()		(0)

/**
 * user_enable_single_step - single-step in user-mode task
 * @task: either current or a task stopped in %TASK_TRACED
 *
 * This can only be called when arch_has_single_step() has returned nonzero.
 * Set @task so that when it returns to user mode, it will trap after the
 * next single instruction executes.  If arch_has_block_step() is defined,
 * this must clear the effects of user_enable_block_step() too.
 */
static inline void user_enable_single_step(struct task_struct *task)
{
	BUG();			/* This can never be called.  */
}

/**
 * user_disable_single_step - cancel user-mode single-step
 * @task: either current or a task stopped in %TASK_TRACED
 *
 * Clear @task of the effects of user_enable_single_step() and
 * user_enable_block_step().  This can be called whether or not either
 * of those was ever called on @task, and even if arch_has_single_step()
 * returned zero.
 */
static inline void user_disable_single_step(struct task_struct *task)
{
}
#else
extern void user_enable_single_step(struct task_struct *);
extern void user_disable_single_step(struct task_struct *);
#endif	/* arch_has_single_step */

#ifndef arch_has_block_step
/**
 * arch_has_block_step - does this CPU support user-mode block-step?
 *
 * If this is defined, then there must be a function declaration or inline
 * for user_enable_block_step(), and arch_has_single_step() must be defined
 * too.  arch_has_block_step() should evaluate to nonzero iff the machine
 * supports step-until-branch for user mode.  It can be a constant or it
 * can test a CPU feature bit.
 */
#define arch_has_block_step()		(0)

/**
 * user_enable_block_step - step until branch in user-mode task
 * @task: either current or a task stopped in %TASK_TRACED
 *
 * This can only be called when arch_has_block_step() has returned nonzero,
 * and will never be called when single-instruction stepping is being used.
 * Set @task so that when it returns to user mode, it will trap after the
 * next branch or trap taken.
 */
static inline void user_enable_block_step(struct task_struct *task)
{
	BUG();			/* This can never be called.  */
}
#else
extern void user_enable_block_step(struct task_struct *);
#endif	/* arch_has_block_step */

#ifdef ARCH_HAS_USER_SINGLE_STEP_REPORT
extern void user_single_step_report(struct pt_regs *regs);
#else
static inline void user_single_step_report(struct pt_regs *regs)
{
	kernel_siginfo_t info;
	clear_siginfo(&info);
	info.si_signo = SIGTRAP;
	info.si_errno = 0;
	info.si_code = SI_USER;
	info.si_pid = 0;
	info.si_uid = 0;
	force_sig_info(&info);
}
#endif

#ifndef arch_ptrace_stop_needed
/**
 * arch_ptrace_stop_needed - Decide whether arch_ptrace_stop() should be called
 *
 * This is called with the siglock held, to decide whether or not it's
 * necessary to release the siglock and call arch_ptrace_stop().  It can be
 * defined to a constant if arch_ptrace_stop() is never required, or always
 * is.  On machines where this makes sense, it should be defined to a quick
 * test to optimize out calling arch_ptrace_stop() when it would be
 * superfluous.  For example, if the thread has not been back to user mode
 * since the last stop, the thread state might indicate that nothing needs
 * to be done.
 *
 * This is guaranteed to be invoked once before a task stops for ptrace and
 * may include arch-specific operations necessary prior to a ptrace stop.
 */
#define arch_ptrace_stop_needed()	(0)
#endif

#ifndef arch_ptrace_stop
/**
 * arch_ptrace_stop - Do machine-specific work before stopping for ptrace
 *
 * This is called with no locks held when arch_ptrace_stop_needed() has
 * just returned nonzero.  It is allowed to block, e.g. for user memory
 * access.  The arch can have machine-specific work to be done before
 * ptrace stops.  On ia64, register backing store gets written back to user
 * memory here.  Since this can be costly (requires dropping the siglock),
 * we only do it when the arch requires it for this particular stop, as
 * indicated by arch_ptrace_stop_needed().
 */
#define arch_ptrace_stop()		do { } while (0)
#endif

#ifndef current_pt_regs
#define current_pt_regs() task_pt_regs(current)
#endif

/*
 * unlike current_pt_regs(), this one is equal to task_pt_regs(current)
 * on *all* architectures; the only reason to have a per-arch definition
 * is optimisation.
 */
#ifndef signal_pt_regs
#define signal_pt_regs() task_pt_regs(current)
#endif

#ifndef current_user_stack_pointer
#define current_user_stack_pointer() user_stack_pointer(current_pt_regs())
#endif

extern int task_current_syscall(struct task_struct *target, struct syscall_info *info);

extern void sigaction_compat_abi(struct k_sigaction *act, struct k_sigaction *oact);

/*
 * ptrace report for syscall entry and exit looks identical.
 */
static inline int ptrace_report_syscall(unsigned long message)
{
	int ptrace = current->ptrace;
	int signr;

	if (!(ptrace & PT_PTRACED))
		return 0;

	signr = ptrace_notify(SIGTRAP | ((ptrace & PT_TRACESYSGOOD) ? 0x80 : 0),
			      message);

	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (signr)
		send_sig(signr, current, 1);

	return fatal_signal_pending(current);
}

/**
 * ptrace_report_syscall_entry - task is about to attempt a system call
 * @regs:		user register state of current task
 *
 * This will be called if %SYSCALL_WORK_SYSCALL_TRACE or
 * %SYSCALL_WORK_SYSCALL_EMU have been set, when the current task has just
 * entered the kernel for a system call.  Full user register state is
 * available here.  Changing the values in @regs can affect the system
 * call number and arguments to be tried.  It is safe to block here,
 * preventing the system call from beginning.
 *
 * Returns zero normally, or nonzero if the calling arch code should abort
 * the system call.  That must prevent normal entry so no system call is
 * made.  If @task ever returns to user mode after this, its register state
 * is unspecified, but should be something harmless like an %ENOSYS error
 * return.  It should preserve enough information so that syscall_rollback()
 * can work (see asm-generic/syscall.h).
 *
 * Called without locks, just after entering kernel mode.
 */
static inline __must_check int ptrace_report_syscall_entry(
	struct pt_regs *regs)
{
	return ptrace_report_syscall(PTRACE_EVENTMSG_SYSCALL_ENTRY);
}

/**
 * ptrace_report_syscall_exit - task has just finished a system call
 * @regs:		user register state of current task
 * @step:		nonzero if simulating single-step or block-step
 *
 * This will be called if %SYSCALL_WORK_SYSCALL_TRACE has been set, when
 * the current task has just finished an attempted system call.  Full
 * user register state is available here.  It is safe to block here,
 * preventing signals from being processed.
 *
 * If @step is nonzero, this report is also in lieu of the normal
 * trap that would follow the system call instruction because
 * user_enable_block_step() or user_enable_single_step() was used.
 * In this case, %SYSCALL_WORK_SYSCALL_TRACE might not be set.
 *
 * Called without locks, just before checking for pending signals.
 */
static inline void ptrace_report_syscall_exit(struct pt_regs *regs, int step)
{
	if (step)
		user_single_step_report(regs);
	else
		ptrace_report_syscall(PTRACE_EVENTMSG_SYSCALL_EXIT);
}
#endif
