/*
	kmod, the new module loader (replaces kerneld)
	Kirk Petersen

	Reorganized not to be a daemon by Adam Richter, with guidance
	from Greg Zornetzer.

	Modified to avoid chroot and file sharing problems.
	Mikael Pettersson

	Limit the concurrent number of kmod modprobes to catch loops from
	"modprobe needs a service that is in a module".
	Keith Owens <kaos@ocs.com.au> December 1999

	Unblock all signals when we exec a usermode process.
	Shuu Yamaguchi <shuu@wondernetworkresources.com> December 2000

	call_usermodehelper wait flag, and remove exec_usermodehelper.
	Rusty Russell <rusty@rustcorp.com.au>  Jan 2003
*/
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/syscalls.h>
#include <linux/unistd.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/cred.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/workqueue.h>
#include <linux/security.h>
#include <linux/mount.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/resource.h>
#include <linux/notifier.h>
#include <linux/suspend.h>
#include <asm/uaccess.h>

#include <trace/events/module.h>

extern int max_threads;

static struct workqueue_struct *khelper_wq;

#define CAP_BSET	(void *)1
#define CAP_PI		(void *)2

static kernel_cap_t usermodehelper_bset = CAP_FULL_SET;
static kernel_cap_t usermodehelper_inheritable = CAP_FULL_SET;
static DEFINE_SPINLOCK(umh_sysctl_lock);

#ifdef CONFIG_MODULES

/*
	modprobe_path is set via /proc/sys.
*/
char modprobe_path[KMOD_PATH_LEN] = "/sbin/modprobe";

/**
 * __request_module - try to load a kernel module
 * @wait: wait (or not) for the operation to complete
 * @fmt: printf style format string for the name of the module
 * @...: arguments as specified in the format string
 *
 * Load a module using the user mode module loader. The function returns
 * zero on success or a negative errno code on failure. Note that a
 * successful module load does not mean the module did not then unload
 * and exit on an error of its own. Callers must check that the service
 * they requested is now available not blindly invoke it.
 *
 * If module auto-loading support is disabled then this function
 * becomes a no-operation.
 */
int __request_module(bool wait, const char *fmt, ...)
{
	va_list args;
	char module_name[MODULE_NAME_LEN];
	unsigned int max_modprobes;
	int ret;
	char *argv[] = { modprobe_path, "-q", "--", module_name, NULL };
	static char *envp[] = { "HOME=/",
				"TERM=linux",
				"PATH=/sbin:/usr/sbin:/bin:/usr/bin",
				NULL };
	static atomic_t kmod_concurrent = ATOMIC_INIT(0);
#define MAX_KMOD_CONCURRENT 50	/* Completely arbitrary value - KAO */
	static int kmod_loop_msg;

	va_start(args, fmt);
	ret = vsnprintf(module_name, MODULE_NAME_LEN, fmt, args);
	va_end(args);
	if (ret >= MODULE_NAME_LEN)
		return -ENAMETOOLONG;

	ret = security_kernel_module_request(module_name);
	if (ret)
		return ret;

	/* If modprobe needs a service that is in a module, we get a recursive
	 * loop.  Limit the number of running kmod threads to max_threads/2 or
	 * MAX_KMOD_CONCURRENT, whichever is the smaller.  A cleaner method
	 * would be to run the parents of this process, counting how many times
	 * kmod was invoked.  That would mean accessing the internals of the
	 * process tables to get the command line, proc_pid_cmdline is static
	 * and it is not worth changing the proc code just to handle this case. 
	 * KAO.
	 *
	 * "trace the ppid" is simple, but will fail if someone's
	 * parent exits.  I think this is as good as it gets. --RR
	 */
	max_modprobes = min(max_threads/2, MAX_KMOD_CONCURRENT);
	atomic_inc(&kmod_concurrent);
	if (atomic_read(&kmod_concurrent) > max_modprobes) {
		/* We may be blaming an innocent here, but unlikely */
		if (kmod_loop_msg < 5) {
			printk(KERN_ERR
			       "request_module: runaway loop modprobe %s\n",
			       module_name);
			kmod_loop_msg++;
		}
		atomic_dec(&kmod_concurrent);
		return -ENOMEM;
	}

	trace_module_request(module_name, wait, _RET_IP_);

	ret = call_usermodehelper_fns(modprobe_path, argv, envp,
			wait ? UMH_WAIT_PROC : UMH_WAIT_EXEC,
			NULL, NULL, NULL);

	atomic_dec(&kmod_concurrent);
	return ret;
}
EXPORT_SYMBOL(__request_module);
#endif /* CONFIG_MODULES */

/*
 * This is the task which runs the usermode application
 */
static int ____call_usermodehelper(void *data)
{
	struct subprocess_info *sub_info = data;
	struct cred *new;
	int retval;

	spin_lock_irq(&current->sighand->siglock);
	flush_signal_handlers(current, 1);
	spin_unlock_irq(&current->sighand->siglock);

	/* We can run anywhere, unlike our parent keventd(). */
	set_cpus_allowed_ptr(current, cpu_all_mask);

	/*
	 * Our parent is keventd, which runs with elevated scheduling priority.
	 * Avoid propagating that into the userspace child.
	 */
	set_user_nice(current, 0);

	retval = -ENOMEM;
	new = prepare_kernel_cred(current);
	if (!new)
		goto fail;

	spin_lock(&umh_sysctl_lock);
	new->cap_bset = cap_intersect(usermodehelper_bset, new->cap_bset);
	new->cap_inheritable = cap_intersect(usermodehelper_inheritable,
					     new->cap_inheritable);
	spin_unlock(&umh_sysctl_lock);

	if (sub_info->init) {
		retval = sub_info->init(sub_info, new);
		if (retval) {
			abort_creds(new);
			goto fail;
		}
	}

	commit_creds(new);

	retval = kernel_execve(sub_info->path,
			       (const char *const *)sub_info->argv,
			       (const char *const *)sub_info->envp);

	/* Exec failed? */
fail:
	sub_info->retval = retval;
	do_exit(0);
}

void call_usermodehelper_freeinfo(struct subprocess_info *info)
{
	if (info->cleanup)
		(*info->cleanup)(info);
	kfree(info);
}
EXPORT_SYMBOL(call_usermodehelper_freeinfo);

/* Keventd can't block, but this (a child) can. */
static int wait_for_helper(void *data)
{
	struct subprocess_info *sub_info = data;
	pid_t pid;

	/* If SIGCLD is ignored sys_wait4 won't populate the status. */
	spin_lock_irq(&current->sighand->siglock);
	current->sighand->action[SIGCHLD-1].sa.sa_handler = SIG_DFL;
	spin_unlock_irq(&current->sighand->siglock);

	pid = kernel_thread(____call_usermodehelper, sub_info, SIGCHLD);
	if (pid < 0) {
		sub_info->retval = pid;
	} else {
		int ret = -ECHILD;
		/*
		 * Normally it is bogus to call wait4() from in-kernel because
		 * wait4() wants to write the exit code to a userspace address.
		 * But wait_for_helper() always runs as keventd, and put_user()
		 * to a kernel address works OK for kernel threads, due to their
		 * having an mm_segment_t which spans the entire address space.
		 *
		 * Thus the __user pointer cast is valid here.
		 */
		sys_wait4(pid, (int __user *)&ret, 0, NULL);

		/*
		 * If ret is 0, either ____call_usermodehelper failed and the
		 * real error code is already in sub_info->retval or
		 * sub_info->retval is 0 anyway, so don't mess with it then.
		 */
		if (ret)
			sub_info->retval = ret;
	}

	complete(sub_info->complete);
	return 0;
}

/* This is run by khelper thread  */
static void __call_usermodehelper(struct work_struct *work)
{
	struct subprocess_info *sub_info =
		container_of(work, struct subprocess_info, work);
	enum umh_wait wait = sub_info->wait;
	pid_t pid;

	/* CLONE_VFORK: wait until the usermode helper has execve'd
	 * successfully We need the data structures to stay around
	 * until that is done.  */
	if (wait == UMH_WAIT_PROC)
		pid = kernel_thread(wait_for_helper, sub_info,
				    CLONE_FS | CLONE_FILES | SIGCHLD);
	else
		pid = kernel_thread(____call_usermodehelper, sub_info,
				    CLONE_VFORK | SIGCHLD);

	switch (wait) {
	case UMH_NO_WAIT:
		call_usermodehelper_freeinfo(sub_info);
		break;

	case UMH_WAIT_PROC:
		if (pid > 0)
			break;
		/* FALLTHROUGH */
	case UMH_WAIT_EXEC:
		if (pid < 0)
			sub_info->retval = pid;
		complete(sub_info->complete);
	}
}

/*
 * If set, call_usermodehelper_exec() will exit immediately returning -EBUSY
 * (used for preventing user land processes from being created after the user
 * land has been frozen during a system-wide hibernation or suspend operation).
 */
static int usermodehelper_disabled = 1;

/* Number of helpers running */
static atomic_t running_helpers = ATOMIC_INIT(0);

/*
 * Wait queue head used by usermodehelper_pm_callback() to wait for all running
 * helpers to finish.
 */
static DECLARE_WAIT_QUEUE_HEAD(running_helpers_waitq);

/*
 * Time to wait for running_helpers to become zero before the setting of
 * usermodehelper_disabled in usermodehelper_pm_callback() fails
 */
#define RUNNING_HELPERS_TIMEOUT	(5 * HZ)

/**
 * usermodehelper_disable - prevent new helpers from being started
 */
int usermodehelper_disable(void)
{
	long retval;

	usermodehelper_disabled = 1;
	smp_mb();
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

	usermodehelper_disabled = 0;
	return -EAGAIN;
}

/**
 * usermodehelper_enable - allow new helpers to be started again
 */
void usermodehelper_enable(void)
{
	usermodehelper_disabled = 0;
}

/**
 * usermodehelper_is_disabled - check if new helpers are allowed to be started
 */
bool usermodehelper_is_disabled(void)
{
	return usermodehelper_disabled;
}
EXPORT_SYMBOL_GPL(usermodehelper_is_disabled);

static void helper_lock(void)
{
	atomic_inc(&running_helpers);
	smp_mb__after_atomic_inc();
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
 *
 * Returns either %NULL on allocation failure, or a subprocess_info
 * structure.  This should be passed to call_usermodehelper_exec to
 * exec the process and free the structure.
 */
struct subprocess_info *call_usermodehelper_setup(char *path, char **argv,
						  char **envp, gfp_t gfp_mask)
{
	struct subprocess_info *sub_info;
	sub_info = kzalloc(sizeof(struct subprocess_info), gfp_mask);
	if (!sub_info)
		goto out;

	INIT_WORK(&sub_info->work, __call_usermodehelper);
	sub_info->path = path;
	sub_info->argv = argv;
	sub_info->envp = envp;
  out:
	return sub_info;
}
EXPORT_SYMBOL(call_usermodehelper_setup);

/**
 * call_usermodehelper_setfns - set a cleanup/init function
 * @info: a subprocess_info returned by call_usermodehelper_setup
 * @cleanup: a cleanup function
 * @init: an init function
 * @data: arbitrary context sensitive data
 *
 * The init function is used to customize the helper process prior to
 * exec.  A non-zero return code causes the process to error out, exit,
 * and return the failure to the calling process
 *
 * The cleanup function is just before ethe subprocess_info is about to
 * be freed.  This can be used for freeing the argv and envp.  The
 * Function must be runnable in either a process context or the
 * context in which call_usermodehelper_exec is called.
 */
void call_usermodehelper_setfns(struct subprocess_info *info,
		    int (*init)(struct subprocess_info *info, struct cred *new),
		    void (*cleanup)(struct subprocess_info *info),
		    void *data)
{
	info->cleanup = cleanup;
	info->init = init;
	info->data = data;
}
EXPORT_SYMBOL(call_usermodehelper_setfns);

/**
 * call_usermodehelper_exec - start a usermode application
 * @sub_info: information about the subprocessa
 * @wait: wait for the application to finish and return status.
 *        when -1 don't wait at all, but you get no useful error back when
 *        the program couldn't be exec'ed. This makes it safe to call
 *        from interrupt context.
 *
 * Runs a user-space application.  The application is started
 * asynchronously if wait is not set, and runs as a child of keventd.
 * (ie. it runs with full root capabilities).
 */
int call_usermodehelper_exec(struct subprocess_info *sub_info,
			     enum umh_wait wait)
{
	DECLARE_COMPLETION_ONSTACK(done);
	int retval = 0;

	helper_lock();
	if (sub_info->path[0] == '\0')
		goto out;

	if (!khelper_wq || usermodehelper_disabled) {
		retval = -EBUSY;
		goto out;
	}

	sub_info->complete = &done;
	sub_info->wait = wait;

	queue_work(khelper_wq, &sub_info->work);
	if (wait == UMH_NO_WAIT)	/* task has freed sub_info */
		goto unlock;
	wait_for_completion(&done);
	retval = sub_info->retval;

out:
	call_usermodehelper_freeinfo(sub_info);
unlock:
	helper_unlock();
	return retval;
}
EXPORT_SYMBOL(call_usermodehelper_exec);

static int proc_cap_handler(struct ctl_table *table, int write,
			 void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table t;
	unsigned long cap_array[_KERNEL_CAPABILITY_U32S];
	kernel_cap_t new_cap;
	int err, i;

	if (write && (!capable(CAP_SETPCAP) ||
		      !capable(CAP_SYS_MODULE)))
		return -EPERM;

	/*
	 * convert from the global kernel_cap_t to the ulong array to print to
	 * userspace if this is a read.
	 */
	spin_lock(&umh_sysctl_lock);
	for (i = 0; i < _KERNEL_CAPABILITY_U32S; i++)  {
		if (table->data == CAP_BSET)
			cap_array[i] = usermodehelper_bset.cap[i];
		else if (table->data == CAP_PI)
			cap_array[i] = usermodehelper_inheritable.cap[i];
		else
			BUG();
	}
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

	/*
	 * convert from the sysctl array of ulongs to the kernel_cap_t
	 * internal representation
	 */
	for (i = 0; i < _KERNEL_CAPABILITY_U32S; i++)
		new_cap.cap[i] = cap_array[i];

	/*
	 * Drop everything not in the new_cap (but don't add things)
	 */
	spin_lock(&umh_sysctl_lock);
	if (write) {
		if (table->data == CAP_BSET)
			usermodehelper_bset = cap_intersect(usermodehelper_bset, new_cap);
		if (table->data == CAP_PI)
			usermodehelper_inheritable = cap_intersect(usermodehelper_inheritable, new_cap);
	}
	spin_unlock(&umh_sysctl_lock);

	return 0;
}

struct ctl_table usermodehelper_table[] = {
	{
		.procname	= "bset",
		.data		= CAP_BSET,
		.maxlen		= _KERNEL_CAPABILITY_U32S * sizeof(unsigned long),
		.mode		= 0600,
		.proc_handler	= proc_cap_handler,
	},
	{
		.procname	= "inheritable",
		.data		= CAP_PI,
		.maxlen		= _KERNEL_CAPABILITY_U32S * sizeof(unsigned long),
		.mode		= 0600,
		.proc_handler	= proc_cap_handler,
	},
	{ }
};

void __init usermodehelper_init(void)
{
	khelper_wq = create_singlethread_workqueue("khelper");
	BUG_ON(!khelper_wq);
}
