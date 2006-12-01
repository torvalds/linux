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
#include <linux/smp_lock.h>
#include <linux/slab.h>
#include <linux/namespace.h>
#include <linux/completion.h>
#include <linux/file.h>
#include <linux/workqueue.h>
#include <linux/security.h>
#include <linux/mount.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/resource.h>
#include <asm/uaccess.h>

extern int max_threads;

static struct workqueue_struct *khelper_wq;

#ifdef CONFIG_KMOD

/*
	modprobe_path is set via /proc/sys.
*/
char modprobe_path[KMOD_PATH_LEN] = "/sbin/modprobe";

/**
 * request_module - try to load a kernel module
 * @fmt:     printf style format string for the name of the module
 * @varargs: arguements as specified in the format string
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
int request_module(const char *fmt, ...)
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
		if (kmod_loop_msg++ < 5)
			printk(KERN_ERR
			       "request_module: runaway loop modprobe %s\n",
			       module_name);
		atomic_dec(&kmod_concurrent);
		return -ENOMEM;
	}

	ret = call_usermodehelper(modprobe_path, argv, envp, 1);
	atomic_dec(&kmod_concurrent);
	return ret;
}
EXPORT_SYMBOL(request_module);
#endif /* CONFIG_KMOD */

struct subprocess_info {
	struct completion *complete;
	char *path;
	char **argv;
	char **envp;
	struct key *ring;
	int wait;
	int retval;
	struct file *stdin;
};

/*
 * This is the task which runs the usermode application
 */
static int ____call_usermodehelper(void *data)
{
	struct subprocess_info *sub_info = data;
	struct key *new_session, *old_session;
	int retval;

	/* Unblock all signals and set the session keyring. */
	new_session = key_get(sub_info->ring);
	flush_signals(current);
	spin_lock_irq(&current->sighand->siglock);
	old_session = __install_session_keyring(current, new_session);
	flush_signal_handlers(current, 1);
	sigemptyset(&current->blocked);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	key_put(old_session);

	/* Install input pipe when needed */
	if (sub_info->stdin) {
		struct files_struct *f = current->files;
		struct fdtable *fdt;
		/* no races because files should be private here */
		sys_close(0);
		fd_install(0, sub_info->stdin);
		spin_lock(&f->file_lock);
		fdt = files_fdtable(f);
		FD_SET(0, fdt->open_fds);
		FD_CLR(0, fdt->close_on_exec);
		spin_unlock(&f->file_lock);

		/* and disallow core files too */
		current->signal->rlim[RLIMIT_CORE] = (struct rlimit){0, 0};
	}

	/* We can run anywhere, unlike our parent keventd(). */
	set_cpus_allowed(current, CPU_MASK_ALL);

	retval = -EPERM;
	if (current->fs->root)
		retval = kernel_execve(sub_info->path,
				sub_info->argv, sub_info->envp);

	/* Exec failed? */
	sub_info->retval = retval;
	do_exit(0);
}

/* Keventd can't block, but this (a child) can. */
static int wait_for_helper(void *data)
{
	struct subprocess_info *sub_info = data;
	pid_t pid;
	struct k_sigaction sa;

	/* Install a handler: if SIGCLD isn't handled sys_wait4 won't
	 * populate the status, but will return -ECHILD. */
	sa.sa.sa_handler = SIG_IGN;
	sa.sa.sa_flags = 0;
	siginitset(&sa.sa.sa_mask, sigmask(SIGCHLD));
	do_sigaction(SIGCHLD, &sa, NULL);
	allow_signal(SIGCHLD);

	pid = kernel_thread(____call_usermodehelper, sub_info, SIGCHLD);
	if (pid < 0) {
		sub_info->retval = pid;
	} else {
		int ret;

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
static void __call_usermodehelper(void *data)
{
	struct subprocess_info *sub_info = data;
	pid_t pid;
	int wait = sub_info->wait;

	/* CLONE_VFORK: wait until the usermode helper has execve'd
	 * successfully We need the data structures to stay around
	 * until that is done.  */
	if (wait)
		pid = kernel_thread(wait_for_helper, sub_info,
				    CLONE_FS | CLONE_FILES | SIGCHLD);
	else
		pid = kernel_thread(____call_usermodehelper, sub_info,
				    CLONE_VFORK | SIGCHLD);

	if (pid < 0) {
		sub_info->retval = pid;
		complete(sub_info->complete);
	} else if (!wait)
		complete(sub_info->complete);
}

/**
 * call_usermodehelper_keys - start a usermode application
 * @path: pathname for the application
 * @argv: null-terminated argument list
 * @envp: null-terminated environment list
 * @session_keyring: session keyring for process (NULL for an empty keyring)
 * @wait: wait for the application to finish and return status.
 *
 * Runs a user-space application.  The application is started
 * asynchronously if wait is not set, and runs as a child of keventd.
 * (ie. it runs with full root capabilities).
 *
 * Must be called from process context.  Returns a negative error code
 * if program was not execed successfully, or 0.
 */
int call_usermodehelper_keys(char *path, char **argv, char **envp,
			     struct key *session_keyring, int wait)
{
	DECLARE_COMPLETION_ONSTACK(done);
	struct subprocess_info sub_info = {
		.complete	= &done,
		.path		= path,
		.argv		= argv,
		.envp		= envp,
		.ring		= session_keyring,
		.wait		= wait,
		.retval		= 0,
	};
	DECLARE_WORK(work, __call_usermodehelper, &sub_info);

	if (!khelper_wq)
		return -EBUSY;

	if (path[0] == '\0')
		return 0;

	queue_work(khelper_wq, &work);
	wait_for_completion(&done);
	return sub_info.retval;
}
EXPORT_SYMBOL(call_usermodehelper_keys);

int call_usermodehelper_pipe(char *path, char **argv, char **envp,
			     struct file **filp)
{
	DECLARE_COMPLETION(done);
	struct subprocess_info sub_info = {
		.complete	= &done,
		.path		= path,
		.argv		= argv,
		.envp		= envp,
		.retval		= 0,
	};
	struct file *f;
	DECLARE_WORK(work, __call_usermodehelper, &sub_info);

	if (!khelper_wq)
		return -EBUSY;

	if (path[0] == '\0')
		return 0;

	f = create_write_pipe();
	if (IS_ERR(f))
		return PTR_ERR(f);
	*filp = f;

	f = create_read_pipe(f);
	if (IS_ERR(f)) {
		free_write_pipe(*filp);
		return PTR_ERR(f);
	}
	sub_info.stdin = f;

	queue_work(khelper_wq, &work);
	wait_for_completion(&done);
	return sub_info.retval;
}
EXPORT_SYMBOL(call_usermodehelper_pipe);

void __init usermodehelper_init(void)
{
	khelper_wq = create_singlethread_workqueue("khelper");
	BUG_ON(!khelper_wq);
}
