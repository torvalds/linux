/*
 * linux/kernel/ptrace.c
 *
 * (C) Copyright 1999 Linus Torvalds
 *
 * Common interfaces for "ptrace()" which we do not want
 * to continually duplicate across every architecture.
 */

#include <linux/capability.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/smp_lock.h>
#include <linux/ptrace.h>
#include <linux/security.h>
#include <linux/signal.h>
#include <linux/audit.h>
#include <linux/pid_namespace.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/regset.h>


/*
 * ptrace a task: make the debugger its new parent and
 * move it to the ptrace list.
 *
 * Must be called with the tasklist lock write-held.
 */
void __ptrace_link(struct task_struct *child, struct task_struct *new_parent)
{
	BUG_ON(!list_empty(&child->ptrace_entry));
	list_add(&child->ptrace_entry, &new_parent->ptraced);
	child->parent = new_parent;
}

/*
 * Turn a tracing stop into a normal stop now, since with no tracer there
 * would be no way to wake it up with SIGCONT or SIGKILL.  If there was a
 * signal sent that would resume the child, but didn't because it was in
 * TASK_TRACED, resume it now.
 * Requires that irqs be disabled.
 */
static void ptrace_untrace(struct task_struct *child)
{
	spin_lock(&child->sighand->siglock);
	if (task_is_traced(child)) {
		/*
		 * If the group stop is completed or in progress,
		 * this thread was already counted as stopped.
		 */
		if (child->signal->flags & SIGNAL_STOP_STOPPED ||
		    child->signal->group_stop_count)
			__set_task_state(child, TASK_STOPPED);
		else
			signal_wake_up(child, 1);
	}
	spin_unlock(&child->sighand->siglock);
}

/*
 * unptrace a task: move it back to its original parent and
 * remove it from the ptrace list.
 *
 * Must be called with the tasklist lock write-held.
 */
void __ptrace_unlink(struct task_struct *child)
{
	BUG_ON(!child->ptrace);

	child->ptrace = 0;
	child->parent = child->real_parent;
	list_del_init(&child->ptrace_entry);

	arch_ptrace_untrace(child);
	if (task_is_traced(child))
		ptrace_untrace(child);
}

/*
 * Check that we have indeed attached to the thing..
 */
int ptrace_check_attach(struct task_struct *child, int kill)
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
	if ((child->ptrace & PT_PTRACED) && child->parent == current) {
		ret = 0;
		/*
		 * child->sighand can't be NULL, release_task()
		 * does ptrace_unlink() before __exit_signal().
		 */
		spin_lock_irq(&child->sighand->siglock);
		if (task_is_stopped(child))
			child->state = TASK_TRACED;
		else if (!task_is_traced(child) && !kill)
			ret = -ESRCH;
		spin_unlock_irq(&child->sighand->siglock);
	}
	read_unlock(&tasklist_lock);

	if (!ret && !kill)
		ret = wait_task_inactive(child, TASK_TRACED) ? 0 : -ESRCH;

	/* All systems go.. */
	return ret;
}

int __ptrace_may_access(struct task_struct *task, unsigned int mode)
{
	const struct cred *cred = current_cred(), *tcred;

	/* May we inspect the given task?
	 * This check is used both for attaching with ptrace
	 * and for allowing access to sensitive information in /proc.
	 *
	 * ptrace_attach denies several cases that /proc allows
	 * because setting up the necessary parent/child relationship
	 * or halting the specified task is impossible.
	 */
	int dumpable = 0;
	/* Don't let security modules deny introspection */
	if (task == current)
		return 0;
	rcu_read_lock();
	tcred = __task_cred(task);
	if ((cred->uid != tcred->euid ||
	     cred->uid != tcred->suid ||
	     cred->uid != tcred->uid  ||
	     cred->gid != tcred->egid ||
	     cred->gid != tcred->sgid ||
	     cred->gid != tcred->gid) &&
	    !capable(CAP_SYS_PTRACE)) {
		rcu_read_unlock();
		return -EPERM;
	}
	rcu_read_unlock();
	smp_rmb();
	if (task->mm)
		dumpable = get_dumpable(task->mm);
	if (!dumpable && !capable(CAP_SYS_PTRACE))
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

int ptrace_attach(struct task_struct *task)
{
	int retval;

	audit_ptrace(task);

	retval = -EPERM;
	if (unlikely(task->flags & PF_KTHREAD))
		goto out;
	if (same_thread_group(task, current))
		goto out;

	/*
	 * Protect exec's credential calculations against our interference;
	 * interference; SUID, SGID and LSM creds get determined differently
	 * under ptrace.
	 */
	retval = -ERESTARTNOINTR;
	if (mutex_lock_interruptible(&task->cred_guard_mutex))
		goto out;

	task_lock(task);
	retval = __ptrace_may_access(task, PTRACE_MODE_ATTACH);
	task_unlock(task);
	if (retval)
		goto unlock_creds;

	write_lock_irq(&tasklist_lock);
	retval = -EPERM;
	if (unlikely(task->exit_state))
		goto unlock_tasklist;
	if (task->ptrace)
		goto unlock_tasklist;

	task->ptrace = PT_PTRACED;
	if (capable(CAP_SYS_PTRACE))
		task->ptrace |= PT_PTRACE_CAP;

	__ptrace_link(task, current);
	send_sig_info(SIGSTOP, SEND_SIG_FORCED, task);

	retval = 0;
unlock_tasklist:
	write_unlock_irq(&tasklist_lock);
unlock_creds:
	mutex_unlock(&task->cred_guard_mutex);
out:
	return retval;
}

/**
 * ptrace_traceme  --  helper for PTRACE_TRACEME
 *
 * Performs checks and sets PT_PTRACED.
 * Should be used by all ptrace implementations for PTRACE_TRACEME.
 */
int ptrace_traceme(void)
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
			__ptrace_link(current, current->real_parent);
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
	__ptrace_unlink(p);

	if (p->exit_state == EXIT_ZOMBIE) {
		if (!task_detached(p) && thread_group_empty(p)) {
			if (!same_thread_group(p->real_parent, tracer))
				do_notify_parent(p, p->exit_signal);
			else if (ignoring_children(tracer->sighand)) {
				__wake_up_parent(p, tracer);
				p->exit_signal = -1;
			}
		}
		if (task_detached(p)) {
			/* Mark it as in the process of being reaped. */
			p->exit_state = EXIT_DEAD;
			return true;
		}
	}

	return false;
}

int ptrace_detach(struct task_struct *child, unsigned int data)
{
	bool dead = false;

	if (!valid_signal(data))
		return -EIO;

	/* Architecture-specific hardware disable .. */
	ptrace_disable(child);
	clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);

	write_lock_irq(&tasklist_lock);
	/*
	 * This child can be already killed. Make sure de_thread() or
	 * our sub-thread doing do_wait() didn't do release_task() yet.
	 */
	if (child->ptrace) {
		child->exit_code = data;
		dead = __ptrace_detach(current, child);
		if (!child->exit_state)
			wake_up_process(child);
	}
	write_unlock_irq(&tasklist_lock);

	if (unlikely(dead))
		release_task(child);

	return 0;
}

/*
 * Detach all tasks we were using ptrace on.
 */
void exit_ptrace(struct task_struct *tracer)
{
	struct task_struct *p, *n;
	LIST_HEAD(ptrace_dead);

	write_lock_irq(&tasklist_lock);
	list_for_each_entry_safe(p, n, &tracer->ptraced, ptrace_entry) {
		if (__ptrace_detach(tracer, p))
			list_add(&p->ptrace_entry, &ptrace_dead);
	}
	write_unlock_irq(&tasklist_lock);

	BUG_ON(!list_empty(&tracer->ptraced));

	list_for_each_entry_safe(p, n, &ptrace_dead, ptrace_entry) {
		list_del_init(&p->ptrace_entry);
		release_task(p);
	}
}

int ptrace_readdata(struct task_struct *tsk, unsigned long src, char __user *dst, int len)
{
	int copied = 0;

	while (len > 0) {
		char buf[128];
		int this_len, retval;

		this_len = (len > sizeof(buf)) ? sizeof(buf) : len;
		retval = access_process_vm(tsk, src, buf, this_len, 0);
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
		retval = access_process_vm(tsk, dst, buf, this_len, 1);
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

static int ptrace_setoptions(struct task_struct *child, long data)
{
	child->ptrace &= ~PT_TRACE_MASK;

	if (data & PTRACE_O_TRACESYSGOOD)
		child->ptrace |= PT_TRACESYSGOOD;

	if (data & PTRACE_O_TRACEFORK)
		child->ptrace |= PT_TRACE_FORK;

	if (data & PTRACE_O_TRACEVFORK)
		child->ptrace |= PT_TRACE_VFORK;

	if (data & PTRACE_O_TRACECLONE)
		child->ptrace |= PT_TRACE_CLONE;

	if (data & PTRACE_O_TRACEEXEC)
		child->ptrace |= PT_TRACE_EXEC;

	if (data & PTRACE_O_TRACEVFORKDONE)
		child->ptrace |= PT_TRACE_VFORK_DONE;

	if (data & PTRACE_O_TRACEEXIT)
		child->ptrace |= PT_TRACE_EXIT;

	return (data & ~PTRACE_O_MASK) ? -EINVAL : 0;
}

static int ptrace_getsiginfo(struct task_struct *child, siginfo_t *info)
{
	unsigned long flags;
	int error = -ESRCH;

	if (lock_task_sighand(child, &flags)) {
		error = -EINVAL;
		if (likely(child->last_siginfo != NULL)) {
			*info = *child->last_siginfo;
			error = 0;
		}
		unlock_task_sighand(child, &flags);
	}
	return error;
}

static int ptrace_setsiginfo(struct task_struct *child, const siginfo_t *info)
{
	unsigned long flags;
	int error = -ESRCH;

	if (lock_task_sighand(child, &flags)) {
		error = -EINVAL;
		if (likely(child->last_siginfo != NULL)) {
			*child->last_siginfo = *info;
			error = 0;
		}
		unlock_task_sighand(child, &flags);
	}
	return error;
}


#ifdef PTRACE_SINGLESTEP
#define is_singlestep(request)		((request) == PTRACE_SINGLESTEP)
#else
#define is_singlestep(request)		0
#endif

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

static int ptrace_resume(struct task_struct *child, long request, long data)
{
	if (!valid_signal(data))
		return -EIO;

	if (request == PTRACE_SYSCALL)
		set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
	else
		clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);

#ifdef TIF_SYSCALL_EMU
	if (request == PTRACE_SYSEMU || request == PTRACE_SYSEMU_SINGLESTEP)
		set_tsk_thread_flag(child, TIF_SYSCALL_EMU);
	else
		clear_tsk_thread_flag(child, TIF_SYSCALL_EMU);
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

	child->exit_code = data;
	wake_up_process(child);

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

#endif

int ptrace_request(struct task_struct *child, long request,
		   long addr, long data)
{
	int ret = -EIO;
	siginfo_t siginfo;

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
		ret = put_user(child->ptrace_message, (unsigned long __user *) data);
		break;

	case PTRACE_GETSIGINFO:
		ret = ptrace_getsiginfo(child, &siginfo);
		if (!ret)
			ret = copy_siginfo_to_user((siginfo_t __user *) data,
						   &siginfo);
		break;

	case PTRACE_SETSIGINFO:
		if (copy_from_user(&siginfo, (siginfo_t __user *) data,
				   sizeof siginfo))
			ret = -EFAULT;
		else
			ret = ptrace_setsiginfo(child, &siginfo);
		break;

	case PTRACE_DETACH:	 /* detach a process that was attached. */
		ret = ptrace_detach(child, data);
		break;

#ifdef PTRACE_SINGLESTEP
	case PTRACE_SINGLESTEP:
#endif
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
		if (child->exit_state)	/* already dead */
			return 0;
		return ptrace_resume(child, request, SIGKILL);

#ifdef CONFIG_HAVE_ARCH_TRACEHOOK
	case PTRACE_GETREGSET:
	case PTRACE_SETREGSET:
	{
		struct iovec kiov;
		struct iovec __user *uiov = (struct iovec __user *) data;

		if (!access_ok(VERIFY_WRITE, uiov, sizeof(*uiov)))
			return -EFAULT;

		if (__get_user(kiov.iov_base, &uiov->iov_base) ||
		    __get_user(kiov.iov_len, &uiov->iov_len))
			return -EFAULT;

		ret = ptrace_regset(child, request, addr, &kiov);
		if (!ret)
			ret = __put_user(kiov.iov_len, &uiov->iov_len);
		break;
	}
#endif
	default:
		break;
	}

	return ret;
}

static struct task_struct *ptrace_get_task_struct(pid_t pid)
{
	struct task_struct *child;

	rcu_read_lock();
	child = find_task_by_vpid(pid);
	if (child)
		get_task_struct(child);
	rcu_read_unlock();

	if (!child)
		return ERR_PTR(-ESRCH);
	return child;
}

#ifndef arch_ptrace_attach
#define arch_ptrace_attach(child)	do { } while (0)
#endif

SYSCALL_DEFINE4(ptrace, long, request, long, pid, long, addr, long, data)
{
	struct task_struct *child;
	long ret;

	/*
	 * This lock_kernel fixes a subtle race with suid exec
	 */
	lock_kernel();
	if (request == PTRACE_TRACEME) {
		ret = ptrace_traceme();
		if (!ret)
			arch_ptrace_attach(current);
		goto out;
	}

	child = ptrace_get_task_struct(pid);
	if (IS_ERR(child)) {
		ret = PTR_ERR(child);
		goto out;
	}

	if (request == PTRACE_ATTACH) {
		ret = ptrace_attach(child);
		/*
		 * Some architectures need to do book-keeping after
		 * a ptrace attach.
		 */
		if (!ret)
			arch_ptrace_attach(child);
		goto out_put_task_struct;
	}

	ret = ptrace_check_attach(child, request == PTRACE_KILL);
	if (ret < 0)
		goto out_put_task_struct;

	ret = arch_ptrace(child, request, addr, data);

 out_put_task_struct:
	put_task_struct(child);
 out:
	unlock_kernel();
	return ret;
}

int generic_ptrace_peekdata(struct task_struct *tsk, long addr, long data)
{
	unsigned long tmp;
	int copied;

	copied = access_process_vm(tsk, addr, &tmp, sizeof(tmp), 0);
	if (copied != sizeof(tmp))
		return -EIO;
	return put_user(tmp, (unsigned long __user *)data);
}

int generic_ptrace_pokedata(struct task_struct *tsk, long addr, long data)
{
	int copied;

	copied = access_process_vm(tsk, addr, &data, sizeof(data), 1);
	return (copied == sizeof(data)) ? 0 : -EIO;
}

#if defined CONFIG_COMPAT
#include <linux/compat.h>

int compat_ptrace_request(struct task_struct *child, compat_long_t request,
			  compat_ulong_t addr, compat_ulong_t data)
{
	compat_ulong_t __user *datap = compat_ptr(data);
	compat_ulong_t word;
	siginfo_t siginfo;
	int ret;

	switch (request) {
	case PTRACE_PEEKTEXT:
	case PTRACE_PEEKDATA:
		ret = access_process_vm(child, addr, &word, sizeof(word), 0);
		if (ret != sizeof(word))
			ret = -EIO;
		else
			ret = put_user(word, datap);
		break;

	case PTRACE_POKETEXT:
	case PTRACE_POKEDATA:
		ret = access_process_vm(child, addr, &data, sizeof(data), 1);
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
		memset(&siginfo, 0, sizeof siginfo);
		if (copy_siginfo_from_user32(
			    &siginfo, (struct compat_siginfo __user *) datap))
			ret = -EFAULT;
		else
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

		if (!access_ok(VERIFY_WRITE, uiov, sizeof(*uiov)))
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

asmlinkage long compat_sys_ptrace(compat_long_t request, compat_long_t pid,
				  compat_long_t addr, compat_long_t data)
{
	struct task_struct *child;
	long ret;

	/*
	 * This lock_kernel fixes a subtle race with suid exec
	 */
	lock_kernel();
	if (request == PTRACE_TRACEME) {
		ret = ptrace_traceme();
		goto out;
	}

	child = ptrace_get_task_struct(pid);
	if (IS_ERR(child)) {
		ret = PTR_ERR(child);
		goto out;
	}

	if (request == PTRACE_ATTACH) {
		ret = ptrace_attach(child);
		/*
		 * Some architectures need to do book-keeping after
		 * a ptrace attach.
		 */
		if (!ret)
			arch_ptrace_attach(child);
		goto out_put_task_struct;
	}

	ret = ptrace_check_attach(child, request == PTRACE_KILL);
	if (!ret)
		ret = compat_arch_ptrace(child, request, addr, data);

 out_put_task_struct:
	put_task_struct(child);
 out:
	unlock_kernel();
	return ret;
}
#endif	/* CONFIG_COMPAT */
