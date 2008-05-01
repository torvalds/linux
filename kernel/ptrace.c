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

#include <asm/pgtable.h>
#include <asm/uaccess.h>

/*
 * ptrace a task: make the debugger its new parent and
 * move it to the ptrace list.
 *
 * Must be called with the tasklist lock write-held.
 */
void __ptrace_link(struct task_struct *child, struct task_struct *new_parent)
{
	BUG_ON(!list_empty(&child->ptrace_list));
	if (child->parent == new_parent)
		return;
	list_add(&child->ptrace_list, &child->parent->ptrace_children);
	remove_parent(child);
	child->parent = new_parent;
	add_parent(child);
}
 
/*
 * Turn a tracing stop into a normal stop now, since with no tracer there
 * would be no way to wake it up with SIGCONT or SIGKILL.  If there was a
 * signal sent that would resume the child, but didn't because it was in
 * TASK_TRACED, resume it now.
 * Requires that irqs be disabled.
 */
void ptrace_untrace(struct task_struct *child)
{
	spin_lock(&child->sighand->siglock);
	if (task_is_traced(child)) {
		if (child->signal->flags & SIGNAL_STOP_STOPPED) {
			__set_task_state(child, TASK_STOPPED);
		} else {
			signal_wake_up(child, 1);
		}
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
	if (ptrace_reparented(child)) {
		list_del_init(&child->ptrace_list);
		remove_parent(child);
		child->parent = child->real_parent;
		add_parent(child);
	}

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
		wait_task_inactive(child);

	/* All systems go.. */
	return ret;
}

int __ptrace_may_attach(struct task_struct *task)
{
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
	if (((current->uid != task->euid) ||
	     (current->uid != task->suid) ||
	     (current->uid != task->uid) ||
	     (current->gid != task->egid) ||
	     (current->gid != task->sgid) ||
	     (current->gid != task->gid)) && !capable(CAP_SYS_PTRACE))
		return -EPERM;
	smp_rmb();
	if (task->mm)
		dumpable = get_dumpable(task->mm);
	if (!dumpable && !capable(CAP_SYS_PTRACE))
		return -EPERM;

	return security_ptrace(current, task);
}

int ptrace_may_attach(struct task_struct *task)
{
	int err;
	task_lock(task);
	err = __ptrace_may_attach(task);
	task_unlock(task);
	return !err;
}

int ptrace_attach(struct task_struct *task)
{
	int retval;
	unsigned long flags;

	audit_ptrace(task);

	retval = -EPERM;
	if (same_thread_group(task, current))
		goto out;

repeat:
	/*
	 * Nasty, nasty.
	 *
	 * We want to hold both the task-lock and the
	 * tasklist_lock for writing at the same time.
	 * But that's against the rules (tasklist_lock
	 * is taken for reading by interrupts on other
	 * cpu's that may have task_lock).
	 */
	task_lock(task);
	if (!write_trylock_irqsave(&tasklist_lock, flags)) {
		task_unlock(task);
		do {
			cpu_relax();
		} while (!write_can_lock(&tasklist_lock));
		goto repeat;
	}

	if (!task->mm)
		goto bad;
	/* the same process cannot be attached many times */
	if (task->ptrace & PT_PTRACED)
		goto bad;
	retval = __ptrace_may_attach(task);
	if (retval)
		goto bad;

	/* Go */
	task->ptrace |= PT_PTRACED;
	if (capable(CAP_SYS_PTRACE))
		task->ptrace |= PT_PTRACE_CAP;

	__ptrace_link(task, current);

	send_sig_info(SIGSTOP, SEND_SIG_FORCED, task);
bad:
	write_unlock_irqrestore(&tasklist_lock, flags);
	task_unlock(task);
out:
	return retval;
}

static inline void __ptrace_detach(struct task_struct *child, unsigned int data)
{
	child->exit_code = data;
	/* .. re-parent .. */
	__ptrace_unlink(child);
	/* .. and wake it up. */
	if (child->exit_state != EXIT_ZOMBIE)
		wake_up_process(child);
}

int ptrace_detach(struct task_struct *child, unsigned int data)
{
	if (!valid_signal(data))
		return -EIO;

	/* Architecture-specific hardware disable .. */
	ptrace_disable(child);
	clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);

	write_lock_irq(&tasklist_lock);
	/* protect against de_thread()->release_task() */
	if (child->ptrace)
		__ptrace_detach(child, data);
	write_unlock_irq(&tasklist_lock);

	return 0;
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
	int error = -ESRCH;

	read_lock(&tasklist_lock);
	if (likely(child->sighand != NULL)) {
		error = -EINVAL;
		spin_lock_irq(&child->sighand->siglock);
		if (likely(child->last_siginfo != NULL)) {
			*info = *child->last_siginfo;
			error = 0;
		}
		spin_unlock_irq(&child->sighand->siglock);
	}
	read_unlock(&tasklist_lock);
	return error;
}

static int ptrace_setsiginfo(struct task_struct *child, const siginfo_t *info)
{
	int error = -ESRCH;

	read_lock(&tasklist_lock);
	if (likely(child->sighand != NULL)) {
		error = -EINVAL;
		spin_lock_irq(&child->sighand->siglock);
		if (likely(child->last_siginfo != NULL)) {
			*child->last_siginfo = *info;
			error = 0;
		}
		spin_unlock_irq(&child->sighand->siglock);
	}
	read_unlock(&tasklist_lock);
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
	}
	else
		user_disable_single_step(child);

	child->exit_code = data;
	wake_up_process(child);

	return 0;
}

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

	default:
		break;
	}

	return ret;
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

	/*
	 * Are we already being traced?
	 */
	task_lock(current);
	if (!(current->ptrace & PT_PTRACED)) {
		ret = security_ptrace(current->parent, current);
		/*
		 * Set the ptrace bit in the process ptrace flags.
		 */
		if (!ret)
			current->ptrace |= PT_PTRACED;
	}
	task_unlock(current);
	return ret;
}

/**
 * ptrace_get_task_struct  --  grab a task struct reference for ptrace
 * @pid:       process id to grab a task_struct reference of
 *
 * This function is a helper for ptrace implementations.  It checks
 * permissions and then grabs a task struct for use of the actual
 * ptrace implementation.
 *
 * Returns the task_struct for @pid or an ERR_PTR() on failure.
 */
struct task_struct *ptrace_get_task_struct(pid_t pid)
{
	struct task_struct *child;

	read_lock(&tasklist_lock);
	child = find_task_by_vpid(pid);
	if (child)
		get_task_struct(child);

	read_unlock(&tasklist_lock);
	if (!child)
		return ERR_PTR(-ESRCH);
	return child;
}

#ifndef arch_ptrace_attach
#define arch_ptrace_attach(child)	do { } while (0)
#endif

asmlinkage long sys_ptrace(long request, long pid, long addr, long data)
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
	if (ret < 0)
		goto out_put_task_struct;

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

#if defined CONFIG_COMPAT && defined __ARCH_WANT_COMPAT_SYS_PTRACE
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
#endif	/* CONFIG_COMPAT && __ARCH_WANT_COMPAT_SYS_PTRACE */
