// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/kernel/ptrace.c
 *
 * (C) Copyright 1999 Linus Torvalds
 *
 * Common interfaces for "ptrace()" which we do not want
 * to continually duplicate across every architecture.
 */

#include <linux/capability.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/coredump.h>
#include <linux/sched/task.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/ptrace.h>
#include <linux/security.h>
#include <linux/signal.h>
#include <linux/uio.h>
#include <linux/audit.h>
#include <linux/pid_namespace.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/regset.h>
#include <linux/hw_breakpoint.h>
#include <linux/cn_proc.h>
#include <linux/compat.h>
#include <linux/sched/signal.h>
#include <linux/minmax.h>
#include <linux/syscall_user_dispatch.h>

#include <asm/syscall.h>	/* for syscall_get_* */

/*
 * Access another process' address space via ptrace.
 * Source/target buffer must be kernel space,
 * Do not walk the page table directly, use get_user_pages
 */
int ptrace_access_vm(struct task_struct *tsk, unsigned long addr,
		     void *buf, int len, unsigned int gup_flags)
{
	struct mm_struct *mm;
	int ret;

	mm = get_task_mm(tsk);
	if (!mm)
		return 0;

	if (!tsk->ptrace ||
	    (current != tsk->parent) ||
	    ((get_dumpable(mm) != SUID_DUMP_USER) &&
	     !ptracer_capable(tsk, mm->user_ns))) {
		mmput(mm);
		return 0;
	}

	ret = access_remote_vm(mm, addr, buf, len, gup_flags);
	mmput(mm);

	return ret;
}


void __ptrace_link(struct task_struct *child, struct task_struct *new_parent,
		   const struct cred *ptracer_cred)
{
	BUG_ON(!list_empty(&child->ptrace_entry));
	list_add(&child->ptrace_entry, &new_parent->ptraced);
	child->parent = new_parent;
	child->ptracer_cred = get_cred(ptracer_cred);
}

/*
 * ptrace a task: make the debugger its new parent and
 * move it to the ptrace list.
 *
 * Must be called with the tasklist lock write-held.
 */
static void ptrace_link(struct task_struct *child, struct task_struct *new_parent)
{
	__ptrace_link(child, new_parent, current_cred());
}

/**
 * __ptrace_unlink - unlink ptracee and restore its execution state
 * @child: ptracee to be unlinked
 *
 * Remove @child from the ptrace list, move it back to the original parent,
 * and restore the execution state so that it conforms to the group stop
 * state.
 *
 * Unlinking can happen via two paths - explicit PTRACE_DETACH or ptracer
 * exiting.  For PTRACE_DETACH, unless the ptracee has been killed between
 * ptrace_check_attach() and here, it's guaranteed to be in TASK_TRACED.
 * If the ptracer is exiting, the ptracee can be in any state.
 *
 * After detach, the ptracee should be in a state which conforms to the
 * group stop.  If the group is stopped or in the process of stopping, the
 * ptracee should be put into TASK_STOPPED; otherwise, it should be woken
 * up from TASK_TRACED.
 *
 * If the ptracee is in TASK_TRACED and needs to be moved to TASK_STOPPED,
 * it goes through TRACED -> RUNNING -> STOPPED transition which is similar
 * to but in the opposite direction of what happens while attaching to a
 * stopped task.  However, in this direction, the intermediate RUNNING
 * state is not hidden even from the current ptracer and if it immediately
 * re-attaches and performs a WNOHANG wait(2), it may fail.
 *
 * CONTEXT:
 * write_lock_irq(tasklist_lock)
 */
void __ptrace_unlink(struct task_struct *child)
{
	const struct cred *old_cred;
	BUG_ON(!child->ptrace);

	clear_task_syscall_work(child, SYSCALL_TRACE);
#if defined(CONFIG_GENERIC_ENTRY) || defined(TIF_SYSCALL_EMU)
	clear_task_syscall_work(child, SYSCALL_EMU);
#endif

	child->parent = child->real_parent;
	list_del_init(&child->ptrace_entry);
	old_cred = child->ptracer_cred;
	child->ptracer_cred = NULL;
	put_cred(old_cred);

	spin_lock(&child->sighand->siglock);
	child->ptrace = 0;
	/*
	 * Clear all pending traps and TRAPPING.  TRAPPING should be
	 * cleared regardless of JOBCTL_STOP_PENDING.  Do it explicitly.
	 */
	task_clear_jobctl_pending(child, JOBCTL_TRAP_MASK);
	task_clear_jobctl_trapping(child);

	/*
	 * Reinstate JOBCTL_STOP_PENDING if group stop is in effect and
	 * @child isn't dead.
	 */
	if (!(child->flags & PF_EXITING) &&
	    (child->signal->flags & SIGNAL_STOP_STOPPED ||
	     child->signal->group_stop_count)) {
		child->jobctl |= JOBCTL_STOP_PENDING;

		/*
		 * This is only possible if this thread was cloned by the
		 * traced task running in the stopped group, set the signal
		 * for the future reports.
		 * FIXME: we should change ptrace_init_task() to handle this
		 * case.
		 */
		if (!(child->jobctl & JOBCTL_STOP_SIGMASK))
			child->jobctl |= SIGSTOP;
	}

	/*
	 * If transition to TASK_STOPPED is pending or in TASK_TRACED, kick
	 * @child in the butt.  Note that @resume should be used iff @child
	 * is in TASK_TRACED; otherwise, we might unduly disrupt
	 * TASK_KILLABLE sleeps.
	 */
	if (child->jobctl & JOBCTL_STOP_PENDING || task_is_traced(child))
		ptrace_signal_wake_up(child, true);

	spin_unlock(&child->sighand->siglock);
}

static bool looks_like_a_spurious_pid(struct task_struct *task)
{
	if (task->exit_code != ((PTRACE_EVENT_EXEC << 8) | SIGTRAP))
		return false;

	if (task_pid_vnr(task) == task->ptrace_message)
		return false;
	/*
	 * The tracee changed its pid but the PTRACE_EVENT_EXEC event
	 * was not wait()'ed, most probably debugger targets the old
	 * leader which was destroyed in de_thread().
	 */
	return true;
}

/*
 * Ensure that nothing can wake it up, even SIGKILL
 *
 * A task is switched to this state while a ptrace operation is in progress;
 * such that the ptrace operation is uninterruptible.
 */
static bool ptrace_freeze_traced(struct task_struct *task)
{
	bool ret = false;

	/* Lockless, nobody but us can set this flag */
	if (task->jobctl & JOBCTL_LISTENING)
		return ret;

	spin_lock_irq(&task->sighand->siglock);
	if (task_is_traced(task) && !looks_like_a_spurious_pid(task) &&
	    !__fatal_signal_pending(task)) {
		task->jobctl |= JOBCTL_PTRACE_FROZEN;
		ret = true;
	}
	spin_unlock_irq(&task->sighand->siglock);

	return ret;
}

static void ptrace_unfreeze_traced(struct task_struct *task)
{
	unsigned long flags;

	/*
	 * The child may be awake and may have cleared
	 * JOBCTL_PTRACE_FROZEN (see ptrace_resume).  The child will
	 * not set JOBCTL_PTRACE_FROZEN or enter __TASK_TRACED anew.
	 */
	if (lock_task_sighand(task, &flags)) {
		task->jobctl &= ~JOBCTL_PTRACE_FROZEN;
		if (__fatal_signal_pending(task)) {
			task->jobctl &= ~JOBCTL_TRACED;
			wake_up_state(task, __TASK_TRACED);
		}
		unlock_task_sighand(task, &flags);
	}
}

/**
 * ptrace_check_attach - check whether ptracee is ready for ptrace operation
 * @child: ptracee to check for
 * @ignore_state: don't check whether @child is currently %TASK_TRACED
 *
 * Check whether @child is being ptraced by %current and ready for further
 * ptrace operations.  If @ignore_state is %false, @child also should be in
 * %TASK_TRACED state and on return the child is guaranteed to be traced
 * and not executing.  If @ignore_state is %true, @child can be in any
 * state.
 *
 * CONTEXT:
 * Grabs and releases tasklist_lock and @child->sighand->siglock.
 *
 * RETURNS:
 * 0 on success, -ESRCH if %child is not ready.
 */
static int ptrace_check_attach(struct task_struct *child, bool ignore_state)
{
	int ret = -ESRCH;

	/*
	 * We take the read lock around doing both checks to close a
	 * possible race where someone else was tracing our child and
	 * detached between these two checks.  After this locked check,
	 * we are sure that this is our traced child and that can only
	 * be changed by us so it's not changing right after this.
	 */
	read_lock(&tasklist_lock);
	if (child->ptrace && child->parent == current) {
		/*
		 * child->sighand can't be NULL, release_task()
		 * does ptrace_unlink() before __exit_signal().
		 */
		if (ignore_state || ptrace_freeze_traced(child))
			ret = 0;
	}
	read_unlock(&tasklist_lock);

	if (!ret && !ignore_state &&
	    WARN_ON_ONCE(!wait_task_inactive(child, __TASK_TRACED|TASK_FROZEN)))
		ret = -ESRCH;

	return ret;
}

static bool ptrace_has_cap(struct user_namespace *ns, unsigned int mode)
{
	if (mode & PTRACE_MODE_NOAUDIT)
		return ns_capable_noaudit(ns, CAP_SYS_PTRACE);
	return ns_capable(ns, CAP_SYS_PTRACE);
}

/* Returns 0 on success, -errno on denial. */
static int __ptrace_may_access(struct task_struct *task, unsigned int mode)
{
	const struct cred *cred = current_cred(), *tcred;
	struct mm_struct *mm;
	kuid_t caller_uid;
	kgid_t caller_gid;

	if (!(mode & PTRACE_MODE_FSCREDS) == !(mode & PTRACE_MODE_REALCREDS)) {
		WARN(1, "denying ptrace access check without PTRACE_MODE_*CREDS\n");
		return -EPERM;
	}

	/* May we inspect the given task?
	 * This check is used both for attaching with ptrace
	 * and for allowing access to sensitive information in /proc.
	 *
	 * ptrace_attach denies several cases that /proc allows
	 * because setting up the necessary parent/child relationship
	 * or halting the specified task is impossible.
	 */

	/* Don't let security modules deny introspection */
	if (same_thread_group(task, current))
		return 0;
	rcu_read_lock();
	if (mode & PTRACE_MODE_FSCREDS) {
		caller_uid = cred->fsuid;
		caller_gid = cred->fsgid;
	} else {
		/*
		 * Using the euid would make more sense here, but something
		 * in userland might rely on the old behavior, and this
		 * shouldn't be a security problem since
		 * PTRACE_MODE_REALCREDS implies that the caller explicitly
		 * used a syscall that requests access to another process
		 * (and not a filesystem syscall to procfs).
		 */
		caller_uid = cred->uid;
		caller_gid = cred->gid;
	}
	tcred = __task_cred(task);
	if (uid_eq(caller_uid, tcred->euid) &&
	    uid_eq(caller_uid, tcred->suid) &&
	    uid_eq(caller_uid, tcred->uid)  &&
	    gid_eq(caller_gid, tcred->egid) &&
	    gid_eq(caller_gid, tcred->sgid) &&
	    gid_eq(caller_gid, tcred->gid))
		goto ok;
	if (ptrace_has_cap(tcred->user_ns, mode))
		goto ok;
	rcu_read_unlock();
	return -EPERM;
ok:
	rcu_read_unlock();
	/*
	 * If a task drops privileges and becomes nondumpable (through a syscall
	 * like setresuid()) while we are trying to access it, we must ensure
	 * that the dumpability is read after the credentials; otherwise,
	 * we may be able to attach to a task that we shouldn't be able to
	 * attach to (as if the task had dropped privileges without becoming
	 * nondumpable).
	 * Pairs with a write barrier in commit_creds().
	 */
	smp_rmb();
	mm = task->mm;
	if (mm &&
	    ((get_dumpable(mm) != SUID_DUMP_USER) &&
	     !ptrace_has_cap(mm->user_ns, mode)))
	    return -EPERM;

	return security_ptrace_access_check(task, mode);
}

bool ptrace_may_access(struct task_struct *task, unsigned int mode)
{
	int err;
	task_lock(task);
	err = __ptrace_may_access(task, mode);
	task_unlock(task);
	return !err;
}

static int check_ptrace_options(unsigned long data)
{
	if (data & ~(unsigned long)PTRACE_O_MASK)
		return -EINVAL;

	if (unlikely(data & PTRACE_O_SUSPEND_SECCOMP)) {
		if (!IS_ENABLED(CONFIG_CHECKPOINT_RESTORE) ||
		    !IS_ENABLED(CONFIG_SECCOMP))
			return -EINVAL;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (seccomp_mode(&current->seccomp) != SECCOMP_MODE_DISABLED ||
		    current->ptrace & PT_SUSPEND_SECCOMP)
			return -EPERM;
	}
	return 0;
}

static int ptrace_attach(struct task_struct *task, long request,
			 unsigned long addr,
			 unsigned long flags)
{
	bool seize = (request == PTRACE_SEIZE);
	int retval;

	retval = -EIO;
	if (seize) {
		if (addr != 0)
			goto out;
		/*
		 * This duplicates the check in check_ptrace_options() because
		 * ptrace_attach() and ptrace_setoptions() have historically
		 * used different error codes for unknown ptrace options.
		 */
		if (flags & ~(unsigned long)PTRACE_O_MASK)
			goto out;
		retval = check_ptrace_options(flags);
		if (retval)
			return retval;
		flags = PT_PTRACED | PT_SEIZED | (flags << PT_OPT_FLAG_SHIFT);
	} else {
		flags = PT_PTRACED;
	}

	audit_ptrace(task);

	retval = -EPERM;
	if (unlikely(task->flags & PF_KTHREAD))
		goto out;
	if (same_thread_group(task, current))
		goto out;

	/*
	 * Protect exec's credential calculations against our interference;
	 * SUID, SGID and LSM creds get determined differently
	 * under ptrace.
	 */
	retval = -ERESTARTNOINTR;
	if (mutex_lock_interruptible(&task->signal->cred_guard_mutex))
		goto out;

	task_lock(task);
	retval = __ptrace_may_access(task, PTRACE_MODE_ATTACH_REALCREDS);
	task_unlock(task);
	if (retval)
		goto unlock_creds;

	write_lock_irq(&tasklist_lock);
	retval = -EPERM;
	if (unlikely(task->exit_state))
		goto unlock_tasklist;
	if (task->ptrace)
		goto unlock_tasklist;

	task->ptrace = flags;

	ptrace_link(task, current);

	/* SEIZE doesn't trap tracee on attach */
	if (!seize)
		send_sig_info(SIGSTOP, SEND_SIG_PRIV, task);

	spin_lock(&task->sighand->siglock);

	/*
	 * If the task is already STOPPED, set JOBCTL_TRAP_STOP and
	 * TRAPPING, and kick it so that it transits to TRACED.  TRAPPING
	 * will be cleared if the child completes the transition or any
	 * event which clears the group stop states happens.  We'll wait
	 * for the transition to complete before returning from this
	 * function.
	 *
	 * This hides STOPPED -> RUNNING -> TRACED transition from the
	 * attaching thread but a different thread in the same group can
	 * still observe the transient RUNNING state.  IOW, if another
	 * thread's WNOHANG wait(2) on the stopped tracee races against
	 * ATTACH, the wait(2) may fail due to the transient RUNNING.
	 *
	 * The following task_is_stopped() test is safe as both transitions
	 * in and out of STOPPED are protected by siglock.
	 */
	if (task_is_stopped(task) &&
	    task_set_jobctl_pending(task, JOBCTL_TRAP_STOP | JOBCTL_TRAPPING)) {
		task->jobctl &= ~JOBCTL_STOPPED;
		signal_wake_up_state(task, __TASK_STOPPED);
	}

	spin_unlock(&task->sighand->siglock);

	retval = 0;
unlock_tasklist:
	write_unlock_irq(&tasklist_lock);
unlock_creds:
	mutex_unlock(&task->signal->cred_guard_mutex);
out:
	if (!retval) {
		/*
		 * We do not bother to change retval or clear JOBCTL_TRAPPING
		 * if wait_on_bit() was interrupted by SIGKILL. The tracer will
		 * not return to user-mode, it will exit and clear this bit in
		 * __ptrace_unlink() if it wasn't already cleared by the tracee;
		 * and until then nobody can ptrace this task.
		 */
		wait_on_bit(&task->jobctl, JOBCTL_TRAPPING_BIT, TASK_KILLABLE);
		proc_ptrace_connector(task, PTRACE_ATTACH);
	}

	return retval;
}

/**
 * ptrace_traceme  --  helper for PTRACE_TRACEME
 *
 * Performs checks and sets PT_PTRACED.
 * Should be used by all ptrace implementations for PTRACE_TRACEME.
 */
static int ptrace_traceme(void)
{
	int ret = -EPERM;

	write_lock_irq(&tasklist_lock);
	/* Are we already being traced? */
	if (!current->ptrace) {
		ret = security_ptrace_traceme(current->parent);
		/*
		 * Check PF_EXITING to ensure ->real_parent has not passed
		 * exit_ptrace(). Otherwise we don't report the error but
		 * pretend ->real_parent untraces us right after return.
		 */
		if (!ret && !(current->real_parent->flags & PF_EXITING)) {
			current->ptrace = PT_PTRACED;
			ptrace_link(current, current->real_parent);
		}
	}
	write_unlock_irq(&tasklist_lock);

	return ret;
}

/*
 * Called with irqs disabled, returns true if childs should reap themselves.
 */
static int ignoring_children(struct sighand_struct *sigh)
{
	int ret;
	spin_lock(&sigh->siglock);
	ret = (sigh->action[SIGCHLD-1].sa.sa_handler == SIG_IGN) ||
	      (sigh->action[SIGCHLD-1].sa.sa_flags & SA_NOCLDWAIT);
	spin_unlock(&sigh->siglock);
	return ret;
}

/*
 * Called with tasklist_lock held for writing.
 * Unlink a traced task, and clean it up if it was a traced zombie.
 * Return true if it needs to be reaped with release_task().
 * (We can't call release_task() here because we already hold tasklist_lock.)
 *
 * If it's a zombie, our attachedness prevented normal parent notification
 * or self-reaping.  Do notification now if it would have happened earlier.
 * If it should reap itself, return true.
 *
 * If it's our own child, there is no notification to do. But if our normal
 * children self-reap, then this child was prevented by ptrace and we must
 * reap it now, in that case we must also wake up sub-threads sleeping in
 * do_wait().
 */
static bool __ptrace_detach(struct task_struct *tracer, struct task_struct *p)
{
	bool dead;

	__ptrace_unlink(p);

	if (p->exit_state != EXIT_ZOMBIE)
		return false;

	dead = !thread_group_leader(p);

	if (!dead && thread_group_empty(p)) {
		if (!same_thread_group(p->real_parent, tracer))
			dead = do_notify_parent(p, p->exit_signal);
		else if (ignoring_children(tracer->sighand)) {
			__wake_up_parent(p, tracer);
			dead = true;
		}
	}
	/* Mark it as in the process of being reaped. */
	if (dead)
		p->exit_state = EXIT_DEAD;
	return dead;
}

static int ptrace_detach(struct task_struct *child, unsigned int data)
{
	if (!valid_signal(data))
		return -EIO;

	/* Architecture-specific hardware disable .. */
	ptrace_disable(child);

	write_lock_irq(&tasklist_lock);
	/*
	 * We rely on ptrace_freeze_traced(). It can't be killed and
	 * untraced by another thread, it can't be a zombie.
	 */
	WARN_ON(!child->ptrace || child->exit_state);
	/*
	 * tasklist_lock avoids the race with wait_task_stopped(), see
	 * the comment in ptrace_resume().
	 */
	child->exit_code = data;
	__ptrace_detach(current, child);
	write_unlock_irq(&tasklist_lock);

	proc_ptrace_connector(child, PTRACE_DETACH);

	return 0;
}

/*
 * Detach all tasks we were using ptrace on. Called with tasklist held
 * for writing.
 */
void exit_ptrace(struct task_struct *tracer, struct list_head *dead)
{
	struct task_struct *p, *n;

	list_for_each_entry_safe(p, n, &tracer->ptraced, ptrace_entry) {
		if (unlikely(p->ptrace & PT_EXITKILL))
			send_sig_info(SIGKILL, SEND_SIG_PRIV, p);

		if (__ptrace_detach(tracer, p))
			list_add(&p->ptrace_entry, dead);
	}
}

int ptrace_readdata(struct task_struct *tsk, unsigned long src, char __user *dst, int len)
{
	int copied = 0;

	while (len > 0) {
		char buf[128];
		int this_len, retval;

		this_len = (len > sizeof(buf)) ? sizeof(buf) : len;
		retval = ptrace_access_vm(tsk, src, buf, this_len, FOLL_FORCE);

		if (!retval) {
			if (copied)
				break;
			return -EIO;
		}
		if (copy_to_user(dst, buf, retval))
			return -EFAULT;
		copied += retval;
		src += retval;
		dst += retval;
		len -= retval;
	}
	return copied;
}

int ptrace_writedata(struct task_struct *tsk, char __user *src, unsigned long dst, int len)
{
	int copied = 0;

	while (len > 0) {
		char buf[128];
		int this_len, retval;

		this_len = (len > sizeof(buf)) ? sizeof(buf) : len;
		if (copy_from_user(buf, src, this_len))
			return -EFAULT;
		retval = ptrace_access_vm(tsk, dst, buf, this_len,
				FOLL_FORCE | FOLL_WRITE);
		if (!retval) {
			if (copied)
				break;
			return -EIO;
		}
		copied += retval;
		src += retval;
		dst += retval;
		len -= retval;
	}
	return copied;
}

static int ptrace_setoptions(struct task_struct *child, unsigned long data)
{
	unsigned flags;
	int ret;

	ret = check_ptrace_options(data);
	if (ret)
		return ret;

	/* Avoid intermediate state when all opts are cleared */
	flags = child->ptrace;
	flags &= ~(PTRACE_O_MASK << PT_OPT_FLAG_SHIFT);
	flags |= (data << PT_OPT_FLAG_SHIFT);
	child->ptrace = flags;

	return 0;
}

static int ptrace_getsiginfo(struct task_struct *child, kernel_siginfo_t *info)
{
	unsigned long flags;
	int error = -ESRCH;

	if (lock_task_sighand(child, &flags)) {
		error = -EINVAL;
		if (likely(child->last_siginfo != NULL)) {
			copy_siginfo(info, child->last_siginfo);
			error = 0;
		}
		unlock_task_sighand(child, &flags);
	}
	return error;
}

static int ptrace_setsiginfo(struct task_struct *child, const kernel_siginfo_t *info)
{
	unsigned long flags;
	int error = -ESRCH;

	if (lock_task_sighand(child, &flags)) {
		error = -EINVAL;
		if (likely(child->last_siginfo != NULL)) {
			copy_siginfo(child->last_siginfo, info);
			error = 0;
		}
		unlock_task_sighand(child, &flags);
	}
	return error;
}

static int ptrace_peek_siginfo(struct task_struct *child,
				unsigned long addr,
				unsigned long data)
{
	struct ptrace_peeksiginfo_args arg;
	struct sigpending *pending;
	struct sigqueue *q;
	int ret, i;

	ret = copy_from_user(&arg, (void __user *) addr,
				sizeof(struct ptrace_peeksiginfo_args));
	if (ret)
		return -EFAULT;

	if (arg.flags & ~PTRACE_PEEKSIGINFO_SHARED)
		return -EINVAL; /* unknown flags */

	if (arg.nr < 0)
		return -EINVAL;

	/* Ensure arg.off fits in an unsigned long */
	if (arg.off > ULONG_MAX)
		return 0;

	if (arg.flags & PTRACE_PEEKSIGINFO_SHARED)
		pending = &child->signal->shared_pending;
	else
		pending = &child->pending;

	for (i = 0; i < arg.nr; ) {
		kernel_siginfo_t info;
		unsigned long off = arg.off + i;
		bool found = false;

		spin_lock_irq(&child->sighand->siglock);
		list_for_each_entry(q, &pending->list, list) {
			if (!off--) {
				found = true;
				copy_siginfo(&info, &q->info);
				break;
			}
		}
		spin_unlock_irq(&child->sighand->siglock);

		if (!found) /* beyond the end of the list */
			break;

#ifdef CONFIG_COMPAT
		if (unlikely(in_compat_syscall())) {
			compat_siginfo_t __user *uinfo = compat_ptr(data);

			if (copy_siginfo_to_user32(uinfo, &info)) {
				ret = -EFAULT;
				break;
			}

		} else
#endif
		{
			siginfo_t __user *uinfo = (siginfo_t __user *) data;

			if (copy_siginfo_to_user(uinfo, &info)) {
				ret = -EFAULT;
				break;
			}
		}

		data += sizeof(siginfo_t);
		i++;

		if (signal_pending(current))
			break;

		cond_resched();
	}

	if (i > 0)
		return i;

	return ret;
}

#ifdef CONFIG_RSEQ
static long ptrace_get_rseq_configuration(struct task_struct *task,
					  unsigned long size, void __user *data)
{
	struct ptrace_rseq_configuration conf = {
		.rseq_abi_pointer = (u64)(uintptr_t)task->rseq,
		.rseq_abi_size = task->rseq_len,
		.signature = task->rseq_sig,
		.flags = 0,
	};

	size = min_t(unsigned long, size, sizeof(conf));
	if (copy_to_user(data, &conf, size))
		return -EFAULT;
	return sizeof(conf);
}
#endif

#define is_singlestep(request)		((request) == PTRACE_SINGLESTEP)

#ifdef PTRACE_SINGLEBLOCK
#define is_singleblock(request)		((request) == PTRACE_SINGLEBLOCK)
#else
#define is_singleblock(request)		0
#endif

#ifdef PTRACE_SYSEMU
#define is_sysemu_singlestep(request)	((request) == PTRACE_SYSEMU_SINGLESTEP)
#else
#define is_sysemu_singlestep(request)	0
#endif

static int ptrace_resume(struct task_struct *child, long request,
			 unsigned long data)
{
	if (!valid_signal(data))
		return -EIO;

	if (request == PTRACE_SYSCALL)
		set_task_syscall_work(child, SYSCALL_TRACE);
	else
		clear_task_syscall_work(child, SYSCALL_TRACE);

#if defined(CONFIG_GENERIC_ENTRY) || defined(TIF_SYSCALL_EMU)
	if (request == PTRACE_SYSEMU || request == PTRACE_SYSEMU_SINGLESTEP)
		set_task_syscall_work(child, SYSCALL_EMU);
	else
		clear_task_syscall_work(child, SYSCALL_EMU);
#endif

	if (is_singleblock(request)) {
		if (unlikely(!arch_has_block_step()))
			return -EIO;
		user_enable_block_step(child);
	} else if (is_singlestep(request) || is_sysemu_singlestep(request)) {
		if (unlikely(!arch_has_single_step()))
			return -EIO;
		user_enable_single_step(child);
	} else {
		user_disable_single_step(child);
	}

	/*
	 * Change ->exit_code and ->state under siglock to avoid the race
	 * with wait_task_stopped() in between; a non-zero ->exit_code will
	 * wrongly look like another report from tracee.
	 *
	 * Note that we need siglock even if ->exit_code == data and/or this
	 * status was not reported yet, the new status must not be cleared by
	 * wait_task_stopped() after resume.
	 */
	spin_lock_irq(&child->sighand->siglock);
	child->exit_code = data;
	child->jobctl &= ~JOBCTL_TRACED;
	wake_up_state(child, __TASK_TRACED);
	spin_unlock_irq(&child->sighand->siglock);

	return 0;
}

#ifdef CONFIG_HAVE_ARCH_TRACEHOOK

static const struct user_regset *
find_regset(const struct user_regset_view *view, unsigned int type)
{
	const struct user_regset *regset;
	int n;

	for (n = 0; n < view->n; ++n) {
		regset = view->regsets + n;
		if (regset->core_note_type == type)
			return regset;
	}

	return NULL;
}

static int ptrace_regset(struct task_struct *task, int req, unsigned int type,
			 struct iovec *kiov)
{
	const struct user_regset_view *view = task_user_regset_view(task);
	const struct user_regset *regset = find_regset(view, type);
	int regset_no;

	if (!regset || (kiov->iov_len % regset->size) != 0)
		return -EINVAL;

	regset_no = regset - view->regsets;
	kiov->iov_len = min(kiov->iov_len,
			    (__kernel_size_t) (regset->n * regset->size));

	if (req == PTRACE_GETREGSET)
		return copy_regset_to_user(task, view, regset_no, 0,
					   kiov->iov_len, kiov->iov_base);
	else
		return copy_regset_from_user(task, view, regset_no, 0,
					     kiov->iov_len, kiov->iov_base);
}

/*
 * This is declared in linux/regset.h and defined in machine-dependent
 * code.  We put the export here, near the primary machine-neutral use,
 * to ensure no machine forgets it.
 */
EXPORT_SYMBOL_GPL(task_user_regset_view);

static unsigned long
ptrace_get_syscall_info_entry(struct task_struct *child, struct pt_regs *regs,
			      struct ptrace_syscall_info *info)
{
	unsigned long args[ARRAY_SIZE(info->entry.args)];
	int i;

	info->op = PTRACE_SYSCALL_INFO_ENTRY;
	info->entry.nr = syscall_get_nr(child, regs);
	syscall_get_arguments(child, regs, args);
	for (i = 0; i < ARRAY_SIZE(args); i++)
		info->entry.args[i] = args[i];

	/* args is the last field in struct ptrace_syscall_info.entry */
	return offsetofend(struct ptrace_syscall_info, entry.args);
}

static unsigned long
ptrace_get_syscall_info_seccomp(struct task_struct *child, struct pt_regs *regs,
				struct ptrace_syscall_info *info)
{
	/*
	 * As struct ptrace_syscall_info.entry is currently a subset
	 * of struct ptrace_syscall_info.seccomp, it makes sense to
	 * initialize that subset using ptrace_get_syscall_info_entry().
	 * This can be reconsidered in the future if these structures
	 * diverge significantly enough.
	 */
	ptrace_get_syscall_info_entry(child, regs, info);
	info->op = PTRACE_SYSCALL_INFO_SECCOMP;
	info->seccomp.ret_data = child->ptrace_message;

	/* ret_data is the last field in struct ptrace_syscall_info.seccomp */
	return offsetofend(struct ptrace_syscall_info, seccomp.ret_data);
}

static unsigned long
ptrace_get_syscall_info_exit(struct task_struct *child, struct pt_regs *regs,
			     struct ptrace_syscall_info *info)
{
	info->op = PTRACE_SYSCALL_INFO_EXIT;
	info->exit.rval = syscall_get_error(child, regs);
	info->exit.is_error = !!info->exit.rval;
	if (!info->exit.is_error)
		info->exit.rval = syscall_get_return_value(child, regs);

	/* is_error is the last field in struct ptrace_syscall_info.exit */
	return offsetofend(struct ptrace_syscall_info, exit.is_error);
}

static int
ptrace_get_syscall_info(struct task_struct *child, unsigned long user_size,
			void __user *datavp)
{
	struct pt_regs *regs = task_pt_regs(child);
	struct ptrace_syscall_info info = {
		.op = PTRACE_SYSCALL_INFO_NONE,
		.arch = syscall_get_arch(child),
		.instruction_pointer = instruction_pointer(regs),
		.stack_pointer = user_stack_pointer(regs),
	};
	unsigned long actual_size = offsetof(struct ptrace_syscall_info, entry);
	unsigned long write_size;

	/*
	 * This does not need lock_task_sighand() to access
	 * child->last_siginfo because ptrace_freeze_traced()
	 * called earlier by ptrace_check_attach() ensures that
	 * the tracee cannot go away and clear its last_siginfo.
	 */
	switch (child->last_siginfo ? child->last_siginfo->si_code : 0) {
	case SIGTRAP | 0x80:
		switch (child->ptrace_message) {
		case PTRACE_EVENTMSG_SYSCALL_ENTRY:
			actual_size = ptrace_get_syscall_info_entry(child, regs,
								    &info);
			break;
		case PTRACE_EVENTMSG_SYSCALL_EXIT:
			actual_size = ptrace_get_syscall_info_exit(child, regs,
								   &info);
			break;
		}
		break;
	case SIGTRAP | (PTRACE_EVENT_SECCOMP << 8):
		actual_size = ptrace_get_syscall_info_seccomp(child, regs,
							      &info);
		break;
	}

	write_size = min(actual_size, user_size);
	return copy_to_user(datavp, &info, write_size) ? -EFAULT : actual_size;
}
#endif /* CONFIG_HAVE_ARCH_TRACEHOOK */

int ptrace_request(struct task_struct *child, long request,
		   unsigned long addr, unsigned long data)
{
	bool seized = child->ptrace & PT_SEIZED;
	int ret = -EIO;
	kernel_siginfo_t siginfo, *si;
	void __user *datavp = (void __user *) data;
	unsigned long __user *datalp = datavp;
	unsigned long flags;

	switch (request) {
	case PTRACE_PEEKTEXT:
	case PTRACE_PEEKDATA:
		return generic_ptrace_peekdata(child, addr, data);
	case PTRACE_POKETEXT:
	case PTRACE_POKEDATA:
		return generic_ptrace_pokedata(child, addr, data);

#ifdef PTRACE_OLDSETOPTIONS
	case PTRACE_OLDSETOPTIONS:
#endif
	case PTRACE_SETOPTIONS:
		ret = ptrace_setoptions(child, data);
		break;
	case PTRACE_GETEVENTMSG:
		ret = put_user(child->ptrace_message, datalp);
		break;

	case PTRACE_PEEKSIGINFO:
		ret = ptrace_peek_siginfo(child, addr, data);
		break;

	case PTRACE_GETSIGINFO:
		ret = ptrace_getsiginfo(child, &siginfo);
		if (!ret)
			ret = copy_siginfo_to_user(datavp, &siginfo);
		break;

	case PTRACE_SETSIGINFO:
		ret = copy_siginfo_from_user(&siginfo, datavp);
		if (!ret)
			ret = ptrace_setsiginfo(child, &siginfo);
		break;

	case PTRACE_GETSIGMASK: {
		sigset_t *mask;

		if (addr != sizeof(sigset_t)) {
			ret = -EINVAL;
			break;
		}

		if (test_tsk_restore_sigmask(child))
			mask = &child->saved_sigmask;
		else
			mask = &child->blocked;

		if (copy_to_user(datavp, mask, sizeof(sigset_t)))
			ret = -EFAULT;
		else
			ret = 0;

		break;
	}

	case PTRACE_SETSIGMASK: {
		sigset_t new_set;

		if (addr != sizeof(sigset_t)) {
			ret = -EINVAL;
			break;
		}

		if (copy_from_user(&new_set, datavp, sizeof(sigset_t))) {
			ret = -EFAULT;
			break;
		}

		sigdelsetmask(&new_set, sigmask(SIGKILL)|sigmask(SIGSTOP));

		/*
		 * Every thread does recalc_sigpending() after resume, so
		 * retarget_shared_pending() and recalc_sigpending() are not
		 * called here.
		 */
		spin_lock_irq(&child->sighand->siglock);
		child->blocked = new_set;
		spin_unlock_irq(&child->sighand->siglock);

		clear_tsk_restore_sigmask(child);

		ret = 0;
		break;
	}

	case PTRACE_INTERRUPT:
		/*
		 * Stop tracee without any side-effect on signal or job
		 * control.  At least one trap is guaranteed to happen
		 * after this request.  If @child is already trapped, the
		 * current trap is not disturbed and another trap will
		 * happen after the current trap is ended with PTRACE_CONT.
		 *
		 * The actual trap might not be PTRACE_EVENT_STOP trap but
		 * the pending condition is cleared regardless.
		 */
		if (unlikely(!seized || !lock_task_sighand(child, &flags)))
			break;

		/*
		 * INTERRUPT doesn't disturb existing trap sans one
		 * exception.  If ptracer issued LISTEN for the current
		 * STOP, this INTERRUPT should clear LISTEN and re-trap
		 * tracee into STOP.
		 */
		if (likely(task_set_jobctl_pending(child, JOBCTL_TRAP_STOP)))
			ptrace_signal_wake_up(child, child->jobctl & JOBCTL_LISTENING);

		unlock_task_sighand(child, &flags);
		ret = 0;
		break;

	case PTRACE_LISTEN:
		/*
		 * Listen for events.  Tracee must be in STOP.  It's not
		 * resumed per-se but is not considered to be in TRACED by
		 * wait(2) or ptrace(2).  If an async event (e.g. group
		 * stop state change) happens, tracee will enter STOP trap
		 * again.  Alternatively, ptracer can issue INTERRUPT to
		 * finish listening and re-trap tracee into STOP.
		 */
		if (unlikely(!seized || !lock_task_sighand(child, &flags)))
			break;

		si = child->last_siginfo;
		if (likely(si && (si->si_code >> 8) == PTRACE_EVENT_STOP)) {
			child->jobctl |= JOBCTL_LISTENING;
			/*
			 * If NOTIFY is set, it means event happened between
			 * start of this trap and now.  Trigger re-trap.
			 */
			if (child->jobctl & JOBCTL_TRAP_NOTIFY)
				ptrace_signal_wake_up(child, true);
			ret = 0;
		}
		unlock_task_sighand(child, &flags);
		break;

	case PTRACE_DETACH:	 /* detach a process that was attached. */
		ret = ptrace_detach(child, data);
		break;

#ifdef CONFIG_BINFMT_ELF_FDPIC
	case PTRACE_GETFDPIC: {
		struct mm_struct *mm = get_task_mm(child);
		unsigned long tmp = 0;

		ret = -ESRCH;
		if (!mm)
			break;

		switch (addr) {
		case PTRACE_GETFDPIC_EXEC:
			tmp = mm->context.exec_fdpic_loadmap;
			break;
		case PTRACE_GETFDPIC_INTERP:
			tmp = mm->context.interp_fdpic_loadmap;
			break;
		default:
			break;
		}
		mmput(mm);

		ret = put_user(tmp, datalp);
		break;
	}
#endif

	case PTRACE_SINGLESTEP:
#ifdef PTRACE_SINGLEBLOCK
	case PTRACE_SINGLEBLOCK:
#endif
#ifdef PTRACE_SYSEMU
	case PTRACE_SYSEMU:
	case PTRACE_SYSEMU_SINGLESTEP:
#endif
	case PTRACE_SYSCALL:
	case PTRACE_CONT:
		return ptrace_resume(child, request, data);

	case PTRACE_KILL:
		send_sig_info(SIGKILL, SEND_SIG_NOINFO, child);
		return 0;

#ifdef CONFIG_HAVE_ARCH_TRACEHOOK
	case PTRACE_GETREGSET:
	case PTRACE_SETREGSET: {
		struct iovec kiov;
		struct iovec __user *uiov = datavp;

		if (!access_ok(uiov, sizeof(*uiov)))
			return -EFAULT;

		if (__get_user(kiov.iov_base, &uiov->iov_base) ||
		    __get_user(kiov.iov_len, &uiov->iov_len))
			return -EFAULT;

		ret = ptrace_regset(child, request, addr, &kiov);
		if (!ret)
			ret = __put_user(kiov.iov_len, &uiov->iov_len);
		break;
	}

	case PTRACE_GET_SYSCALL_INFO:
		ret = ptrace_get_syscall_info(child, addr, datavp);
		break;
#endif

	case PTRACE_SECCOMP_GET_FILTER:
		ret = seccomp_get_filter(child, addr, datavp);
		break;

	case PTRACE_SECCOMP_GET_METADATA:
		ret = seccomp_get_metadata(child, addr, datavp);
		break;

#ifdef CONFIG_RSEQ
	case PTRACE_GET_RSEQ_CONFIGURATION:
		ret = ptrace_get_rseq_configuration(child, addr, datavp);
		break;
#endif

	case PTRACE_SET_SYSCALL_USER_DISPATCH_CONFIG:
		ret = syscall_user_dispatch_set_config(child, addr, datavp);
		break;

	case PTRACE_GET_SYSCALL_USER_DISPATCH_CONFIG:
		ret = syscall_user_dispatch_get_config(child, addr, datavp);
		break;

	default:
		break;
	}

	return ret;
}

SYSCALL_DEFINE4(ptrace, long, request, long, pid, unsigned long, addr,
		unsigned long, data)
{
	struct task_struct *child;
	long ret;

	if (request == PTRACE_TRACEME) {
		ret = ptrace_traceme();
		goto out;
	}

	child = find_get_task_by_vpid(pid);
	if (!child) {
		ret = -ESRCH;
		goto out;
	}

	if (request == PTRACE_ATTACH || request == PTRACE_SEIZE) {
		ret = ptrace_attach(child, request, addr, data);
		goto out_put_task_struct;
	}

	ret = ptrace_check_attach(child, request == PTRACE_KILL ||
				  request == PTRACE_INTERRUPT);
	if (ret < 0)
		goto out_put_task_struct;

	ret = arch_ptrace(child, request, addr, data);
	if (ret || request != PTRACE_DETACH)
		ptrace_unfreeze_traced(child);

 out_put_task_struct:
	put_task_struct(child);
 out:
	return ret;
}

int generic_ptrace_peekdata(struct task_struct *tsk, unsigned long addr,
			    unsigned long data)
{
	unsigned long tmp;
	int copied;

	copied = ptrace_access_vm(tsk, addr, &tmp, sizeof(tmp), FOLL_FORCE);
	if (copied != sizeof(tmp))
		return -EIO;
	return put_user(tmp, (unsigned long __user *)data);
}

int generic_ptrace_pokedata(struct task_struct *tsk, unsigned long addr,
			    unsigned long data)
{
	int copied;

	copied = ptrace_access_vm(tsk, addr, &data, sizeof(data),
			FOLL_FORCE | FOLL_WRITE);
	return (copied == sizeof(data)) ? 0 : -EIO;
}

#if defined CONFIG_COMPAT

int compat_ptrace_request(struct task_struct *child, compat_long_t request,
			  compat_ulong_t addr, compat_ulong_t data)
{
	compat_ulong_t __user *datap = compat_ptr(data);
	compat_ulong_t word;
	kernel_siginfo_t siginfo;
	int ret;

	switch (request) {
	case PTRACE_PEEKTEXT:
	case PTRACE_PEEKDATA:
		ret = ptrace_access_vm(child, addr, &word, sizeof(word),
				FOLL_FORCE);
		if (ret != sizeof(word))
			ret = -EIO;
		else
			ret = put_user(word, datap);
		break;

	case PTRACE_POKETEXT:
	case PTRACE_POKEDATA:
		ret = ptrace_access_vm(child, addr, &data, sizeof(data),
				FOLL_FORCE | FOLL_WRITE);
		ret = (ret != sizeof(data) ? -EIO : 0);
		break;

	case PTRACE_GETEVENTMSG:
		ret = put_user((compat_ulong_t) child->ptrace_message, datap);
		break;

	case PTRACE_GETSIGINFO:
		ret = ptrace_getsiginfo(child, &siginfo);
		if (!ret)
			ret = copy_siginfo_to_user32(
				(struct compat_siginfo __user *) datap,
				&siginfo);
		break;

	case PTRACE_SETSIGINFO:
		ret = copy_siginfo_from_user32(
			&siginfo, (struct compat_siginfo __user *) datap);
		if (!ret)
			ret = ptrace_setsiginfo(child, &siginfo);
		break;
#ifdef CONFIG_HAVE_ARCH_TRACEHOOK
	case PTRACE_GETREGSET:
	case PTRACE_SETREGSET:
	{
		struct iovec kiov;
		struct compat_iovec __user *uiov =
			(struct compat_iovec __user *) datap;
		compat_uptr_t ptr;
		compat_size_t len;

		if (!access_ok(uiov, sizeof(*uiov)))
			return -EFAULT;

		if (__get_user(ptr, &uiov->iov_base) ||
		    __get_user(len, &uiov->iov_len))
			return -EFAULT;

		kiov.iov_base = compat_ptr(ptr);
		kiov.iov_len = len;

		ret = ptrace_regset(child, request, addr, &kiov);
		if (!ret)
			ret = __put_user(kiov.iov_len, &uiov->iov_len);
		break;
	}
#endif

	default:
		ret = ptrace_request(child, request, addr, data);
	}

	return ret;
}

COMPAT_SYSCALL_DEFINE4(ptrace, compat_long_t, request, compat_long_t, pid,
		       compat_long_t, addr, compat_long_t, data)
{
	struct task_struct *child;
	long ret;

	if (request == PTRACE_TRACEME) {
		ret = ptrace_traceme();
		goto out;
	}

	child = find_get_task_by_vpid(pid);
	if (!child) {
		ret = -ESRCH;
		goto out;
	}

	if (request == PTRACE_ATTACH || request == PTRACE_SEIZE) {
		ret = ptrace_attach(child, request, addr, data);
		goto out_put_task_struct;
	}

	ret = ptrace_check_attach(child, request == PTRACE_KILL ||
				  request == PTRACE_INTERRUPT);
	if (!ret) {
		ret = compat_arch_ptrace(child, request, addr, data);
		if (ret || request != PTRACE_DETACH)
			ptrace_unfreeze_traced(child);
	}

 out_put_task_struct:
	put_task_struct(child);
 out:
	return ret;
}
#endif	/* CONFIG_COMPAT */
