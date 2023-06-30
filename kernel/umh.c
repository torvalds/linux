// SPDX-License-Identifier: GPL-2.0-only
/*
 * umh - the kernel usermode helper
 */
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/binfmts.h>
#include <linux/syscalls.h>
#include <linux/unistd.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/cred.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/fs_struct.h>
#include <linux/workqueue.h>
#include <linux/security.h>
#include <linux/mount.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/resource.h>
#include <linux/notifier.h>
#include <linux/suspend.h>
#include <linux/rwsem.h>
#include <linux/ptrace.h>
#include <linux/async.h>
#include <linux/uaccess.h>
#include <linux/initrd.h>
#include <linux/freezer.h>

#include <trace/events/module.h>

static kernel_cap_t usermodehelper_bset = CAP_FULL_SET;
static kernel_cap_t usermodehelper_inheritable = CAP_FULL_SET;
static DEFINE_SPINLOCK(umh_sysctl_lock);
static DECLARE_RWSEM(umhelper_sem);

static void call_usermodehelper_freeinfo(struct subprocess_info *info)
{
	if (info->cleanup)
		(*info->cleanup)(info);
	kfree(info);
}

static void umh_complete(struct subprocess_info *sub_info)
{
	struct completion *comp = xchg(&sub_info->complete, NULL);
	/*
	 * See call_usermodehelper_exec(). If xchg() returns NULL
	 * we own sub_info, the UMH_KILLABLE caller has gone away
	 * or the caller used UMH_NO_WAIT.
	 */
	if (comp)
		complete(comp);
	else
		call_usermodehelper_freeinfo(sub_info);
}

/*
 * This is the task which runs the usermode application
 */
static int call_usermodehelper_exec_async(void *data)
{
	struct subprocess_info *sub_info = data;
	struct cred *new;
	int retval;

	spin_lock_irq(&current->sighand->siglock);
	flush_signal_handlers(current, 1);
	spin_unlock_irq(&current->sighand->siglock);

	/*
	 * Initial kernel threads share ther FS with init, in order to
	 * get the init root directory. But we've now created a new
	 * thread that is going to execve a user process and has its own
	 * 'struct fs_struct'. Reset umask to the default.
	 */
	current->fs->umask = 0022;

	/*
	 * Our parent (unbound workqueue) runs with elevated scheduling
	 * priority. Avoid propagating that into the userspace child.
	 */
	set_user_nice(current, 0);

	retval = -ENOMEM;
	new = prepare_kernel_cred(current);
	if (!new)
		goto out;

	spin_lock(&umh_sysctl_lock);
	new->cap_bset = cap_intersect(usermodehelper_bset, new->cap_bset);
	new->cap_inheritable = cap_intersect(usermodehelper_inheritable,
					     new->cap_inheritable);
	spin_unlock(&umh_sysctl_lock);

	if (sub_info->init) {
		retval = sub_info->init(sub_info, new);
		if (retval) {
			abort_creds(new);
			goto out;
		}
	}

	commit_creds(new);

	wait_for_initramfs();
	retval = kernel_execve(sub_info->path,
			       (const char *const *)sub_info->argv,
			       (const char *const *)sub_info->envp);
out:
	sub_info->retval = retval;
	/*
	 * call_usermodehelper_exec_sync() will call umh_complete
	 * if UHM_WAIT_PROC.
	 */
	if (!(sub_info->wait & UMH_WAIT_PROC))
		umh_complete(sub_info);
	if (!retval)
		return 0;
	do_exit(0);
}

/* Handles UMH_WAIT_PROC.  */
static void call_usermodehelper_exec_sync(struct subprocess_info *sub_info)
{
	pid_t pid;

	/* If SIGCLD is ignored do_wait won't populate the status. */
	kernel_sigaction(SIGCHLD, SIG_DFL);
	pid = user_mode_thread(call_usermodehelper_exec_async, sub_info, SIGCHLD);
	if (pid < 0)
		sub_info->retval = pid;
	else
		kernel_wait(pid, &sub_info->retval);

	/* Restore default kernel sig handler */
	kernel_sigaction(SIGCHLD, SIG_IGN);
	umh_complete(sub_info);
}

/*
 * We need to create the usermodehelper kernel thread from a task that is affine
 * to an optimized set of CPUs (or nohz housekeeping ones) such that they
 * inherit a widest affinity irrespective of call_usermodehelper() callers with
 * possibly reduced affinity (eg: per-cpu workqueues). We don't want
 * usermodehelper targets to contend a busy CPU.
 *
 * Unbound workqueues provide such wide affinity and allow to block on
 * UMH_WAIT_PROC requests without blocking pending request (up to some limit).
 *
 * Besides, workqueues provide the privilege level that caller might not have
 * to perform the usermodehelper request.
 *
 */
static void call_usermodehelper_exec_work(struct work_struct *work)
{
	struct subprocess_info *sub_info =
		container_of(work, struct subprocess_info, work);

	if (sub_info->wait & UMH_WAIT_PROC) {
		call_usermodehelper_exec_sync(sub_info);
	} else {
		pid_t pid;
		/*
		 * Use CLONE_PARENT to reparent it to kthreadd; we do not
		 * want to pollute current->children, and we need a parent
		 * that always ignores SIGCHLD to ensure auto-reaping.
		 */
		pid = user_mode_thread(call_usermodehelper_exec_async, sub_info,
				       CLONE_PARENT | SIGCHLD);
		if (pid < 0) {
			sub_info->retval = pid;
			umh_complete(sub_info);
		}
	}
}

/*
 * If set, call_usermodehelper_exec() will exit immediately returning -EBUSY
 * (used for preventing user land processes from being created after the user
 * land has been frozen during a system-wide hibernation or suspend operation).
 * Should always be manipulated under umhelper_sem acquired for write.
 */
static enum umh_disable_depth usermodehelper_disabled = UMH_DISABLED;

/* Number of helpers running */
static atomic_t running_helpers = ATOMIC_INIT(0);

/*
 * Wait queue head used by usermodehelper_disable() to wait for all running
 * helpers to finish.
 */
static DECLARE_WAIT_QUEUE_HEAD(running_helpers_waitq);

/*
 * Used by usermodehelper_read_lock_wait() to wait for usermodehelper_disabled
 * to become 'false'.
 */
static DECLARE_WAIT_QUEUE_HEAD(usermodehelper_disabled_waitq);

/*
 * Time to wait for running_helpers to become zero before the setting of
 * usermodehelper_disabled in usermodehelper_disable() fails
 */
#define RUNNING_HELPERS_TIMEOUT	(5 * HZ)

int usermodehelper_read_trylock(void)
{
	DEFINE_WAIT(wait);
	int ret = 0;

	down_read(&umhelper_sem);
	for (;;) {
		prepare_to_wait(&usermodehelper_disabled_waitq, &wait,
				TASK_INTERRUPTIBLE);
		if (!usermodehelper_disabled)
			break;

		if (usermodehelper_disabled == UMH_DISABLED)
			ret = -EAGAIN;

		up_read(&umhelper_sem);

		if (ret)
			break;

		schedule();
		try_to_freeze();

		down_read(&umhelper_sem);
	}
	finish_wait(&usermodehelper_disabled_waitq, &wait);
	return ret;
}
EXPORT_SYMBOL_GPL(usermodehelper_read_trylock);

long usermodehelper_read_lock_wait(long timeout)
{
	DEFINE_WAIT(wait);

	if (timeout < 0)
		return -EINVAL;

	down_read(&umhelper_sem);
	for (;;) {
		prepare_to_wait(&usermodehelper_disabled_waitq, &wait,
				TASK_UNINTERRUPTIBLE);
		if (!usermodehelper_disabled)
			break;

		up_read(&umhelper_sem);

		timeout = schedule_timeout(timeout);
		if (!timeout)
			break;

		down_read(&umhelper_sem);
	}
	finish_wait(&usermodehelper_disabled_waitq, &wait);
	return timeout;
}
EXPORT_SYMBOL_GPL(usermodehelper_read_lock_wait);

void usermodehelper_read_unlock(void)
{
	up_read(&umhelper_sem);
}
EXPORT_SYMBOL_GPL(usermodehelper_read_unlock);

/**
 * __usermodehelper_set_disable_depth - Modify usermodehelper_disabled.
 * @depth: New value to assign to usermodehelper_disabled.
 *
 * Change the value of usermodehelper_disabled (under umhelper_sem locked for
 * writing) and wakeup tasks waiting for it to change.
 */
void __usermodehelper_set_disable_depth(enum umh_disable_depth depth)
{
	down_write(&umhelper_sem);
	usermodehelper_disabled = depth;
	wake_up(&usermodehelper_disabled_waitq);
	up_write(&umhelper_sem);
}

/**
 * __usermodehelper_disable - Prevent new helpers from being started.
 * @depth: New value to assign to usermodehelper_disabled.
 *
 * Set usermodehelper_disabled to @depth and wait for running helpers to exit.
 */
int __usermodehelper_disable(enum umh_disable_depth depth)
{
	long retval;

	if (!depth)
		return -EINVAL;

	down_write(&umhelper_sem);
	usermodehelper_disabled = depth;
	up_write(&umhelper_sem);

	/*
	 * From now on call_usermodehelper_exec() won't start any new
	 * helpers, so it is sufficient if running_helpers turns out to
	 * be zero at one point (it may be increased later, but that
	 * doesn't matter).
	 */
	retval = wait_event_timeout(running_helpers_waitq,
					atomic_read(&running_helpers) == 0,
					RUNNING_HELPERS_TIMEOUT);
	if (retval)
		return 0;

	__usermodehelper_set_disable_depth(UMH_ENABLED);
	return -EAGAIN;
}

static void helper_lock(void)
{
	atomic_inc(&running_helpers);
	smp_mb__after_atomic();
}

static void helper_unlock(void)
{
	if (atomic_dec_and_test(&running_helpers))
		wake_up(&running_helpers_waitq);
}

/**
 * call_usermodehelper_setup - prepare to call a usermode helper
 * @path: path to usermode executable
 * @argv: arg vector for process
 * @envp: environment for process
 * @gfp_mask: gfp mask for memory allocation
 * @init: an init function
 * @cleanup: a cleanup function
 * @data: arbitrary context sensitive data
 *
 * Returns either %NULL on allocation failure, or a subprocess_info
 * structure.  This should be passed to call_usermodehelper_exec to
 * exec the process and free the structure.
 *
 * The init function is used to customize the helper process prior to
 * exec.  A non-zero return code causes the process to error out, exit,
 * and return the failure to the calling process
 *
 * The cleanup function is just before the subprocess_info is about to
 * be freed.  This can be used for freeing the argv and envp.  The
 * Function must be runnable in either a process context or the
 * context in which call_usermodehelper_exec is called.
 */
struct subprocess_info *call_usermodehelper_setup(const char *path, char **argv,
		char **envp, gfp_t gfp_mask,
		int (*init)(struct subprocess_info *info, struct cred *new),
		void (*cleanup)(struct subprocess_info *info),
		void *data)
{
	struct subprocess_info *sub_info;
	sub_info = kzalloc(sizeof(struct subprocess_info), gfp_mask);
	if (!sub_info)
		goto out;

	INIT_WORK(&sub_info->work, call_usermodehelper_exec_work);

#ifdef CONFIG_STATIC_USERMODEHELPER
	sub_info->path = CONFIG_STATIC_USERMODEHELPER_PATH;
#else
	sub_info->path = path;
#endif
	sub_info->argv = argv;
	sub_info->envp = envp;

	sub_info->cleanup = cleanup;
	sub_info->init = init;
	sub_info->data = data;
  out:
	return sub_info;
}
EXPORT_SYMBOL(call_usermodehelper_setup);

/**
 * call_usermodehelper_exec - start a usermode application
 * @sub_info: information about the subprocess
 * @wait: wait for the application to finish and return status.
 *        when UMH_NO_WAIT don't wait at all, but you get no useful error back
 *        when the program couldn't be exec'ed. This makes it safe to call
 *        from interrupt context.
 *
 * Runs a user-space application.  The application is started
 * asynchronously if wait is not set, and runs as a child of system workqueues.
 * (ie. it runs with full root capabilities and optimized affinity).
 *
 * Note: successful return value does not guarantee the helper was called at
 * all. You can't rely on sub_info->{init,cleanup} being called even for
 * UMH_WAIT_* wait modes as STATIC_USERMODEHELPER_PATH="" turns all helpers
 * into a successful no-op.
 */
int call_usermodehelper_exec(struct subprocess_info *sub_info, int wait)
{
	unsigned int state = TASK_UNINTERRUPTIBLE;
	DECLARE_COMPLETION_ONSTACK(done);
	int retval = 0;

	if (!sub_info->path) {
		call_usermodehelper_freeinfo(sub_info);
		return -EINVAL;
	}
	helper_lock();
	if (usermodehelper_disabled) {
		retval = -EBUSY;
		goto out;
	}

	/*
	 * If there is no binary for us to call, then just return and get out of
	 * here.  This allows us to set STATIC_USERMODEHELPER_PATH to "" and
	 * disable all call_usermodehelper() calls.
	 */
	if (strlen(sub_info->path) == 0)
		goto out;

	/*
	 * Set the completion pointer only if there is a waiter.
	 * This makes it possible to use umh_complete to free
	 * the data structure in case of UMH_NO_WAIT.
	 */
	sub_info->complete = (wait == UMH_NO_WAIT) ? NULL : &done;
	sub_info->wait = wait;

	queue_work(system_unbound_wq, &sub_info->work);
	if (wait == UMH_NO_WAIT)	/* task has freed sub_info */
		goto unlock;

	if (wait & UMH_FREEZABLE)
		state |= TASK_FREEZABLE;

	if (wait & UMH_KILLABLE) {
		retval = wait_for_completion_state(&done, state | TASK_KILLABLE);
		if (!retval)
			goto wait_done;

		/* umh_complete() will see NULL and free sub_info */
		if (xchg(&sub_info->complete, NULL))
			goto unlock;

		/*
		 * fallthrough; in case of -ERESTARTSYS now do uninterruptible
		 * wait_for_completion_state(). Since umh_complete() shall call
		 * complete() in a moment if xchg() above returned NULL, this
		 * uninterruptible wait_for_completion_state() will not block
		 * SIGKILL'ed processes for long.
		 */
	}
	wait_for_completion_state(&done, state);

wait_done:
	retval = sub_info->retval;
out:
	call_usermodehelper_freeinfo(sub_info);
unlock:
	helper_unlock();
	return retval;
}
EXPORT_SYMBOL(call_usermodehelper_exec);

/**
 * call_usermodehelper() - prepare and start a usermode application
 * @path: path to usermode executable
 * @argv: arg vector for process
 * @envp: environment for process
 * @wait: wait for the application to finish and return status.
 *        when UMH_NO_WAIT don't wait at all, but you get no useful error back
 *        when the program couldn't be exec'ed. This makes it safe to call
 *        from interrupt context.
 *
 * This function is the equivalent to use call_usermodehelper_setup() and
 * call_usermodehelper_exec().
 */
int call_usermodehelper(const char *path, char **argv, char **envp, int wait)
{
	struct subprocess_info *info;
	gfp_t gfp_mask = (wait == UMH_NO_WAIT) ? GFP_ATOMIC : GFP_KERNEL;

	info = call_usermodehelper_setup(path, argv, envp, gfp_mask,
					 NULL, NULL, NULL);
	if (info == NULL)
		return -ENOMEM;

	return call_usermodehelper_exec(info, wait);
}
EXPORT_SYMBOL(call_usermodehelper);

#if defined(CONFIG_SYSCTL)
static int proc_cap_handler(struct ctl_table *table, int write,
			 void *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table t;
	unsigned long cap_array[2];
	kernel_cap_t new_cap, *cap;
	int err;

	if (write && (!capable(CAP_SETPCAP) ||
		      !capable(CAP_SYS_MODULE)))
		return -EPERM;

	/*
	 * convert from the global kernel_cap_t to the ulong array to print to
	 * userspace if this is a read.
	 *
	 * Legacy format: capabilities are exposed as two 32-bit values
	 */
	cap = table->data;
	spin_lock(&umh_sysctl_lock);
	cap_array[0] = (u32) cap->val;
	cap_array[1] = cap->val >> 32;
	spin_unlock(&umh_sysctl_lock);

	t = *table;
	t.data = &cap_array;

	/*
	 * actually read or write and array of ulongs from userspace.  Remember
	 * these are least significant 32 bits first
	 */
	err = proc_doulongvec_minmax(&t, write, buffer, lenp, ppos);
	if (err < 0)
		return err;

	new_cap.val = (u32)cap_array[0];
	new_cap.val += (u64)cap_array[1] << 32;

	/*
	 * Drop everything not in the new_cap (but don't add things)
	 */
	if (write) {
		spin_lock(&umh_sysctl_lock);
		*cap = cap_intersect(*cap, new_cap);
		spin_unlock(&umh_sysctl_lock);
	}

	return 0;
}

static struct ctl_table usermodehelper_table[] = {
	{
		.procname	= "bset",
		.data		= &usermodehelper_bset,
		.maxlen		= 2 * sizeof(unsigned long),
		.mode		= 0600,
		.proc_handler	= proc_cap_handler,
	},
	{
		.procname	= "inheritable",
		.data		= &usermodehelper_inheritable,
		.maxlen		= 2 * sizeof(unsigned long),
		.mode		= 0600,
		.proc_handler	= proc_cap_handler,
	},
	{ }
};

static int __init init_umh_sysctls(void)
{
	register_sysctl_init("kernel/usermodehelper", usermodehelper_table);
	return 0;
}
early_initcall(init_umh_sysctls);
#endif /* CONFIG_SYSCTL */
