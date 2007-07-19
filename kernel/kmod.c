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
#include <linux/mnt_namespace.h>
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
	struct work_struct work;
	struct completion *complete;
	char *path;
	char **argv;
	char **envp;
	struct key *ring;
	enum umh_wait wait;
	int retval;
	struct file *stdin;
	void (*cleanup)(char **argv, char **envp);
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

	/*
	 * Our parent is keventd, which runs with elevated scheduling priority.
	 * Avoid propagating that into the userspace child.
	 */
	set_user_nice(current, 0);

	retval = -EPERM;
	if (current->fs->root)
		retval = kernel_execve(sub_info->path,
				sub_info->argv, sub_info->envp);

	/* Exec failed? */
	sub_info->retval = retval;
	do_exit(0);
}

void call_usermodehelper_freeinfo(struct subprocess_info *info)
{
	if (info->cleanup)
		(*info->cleanup)(info->argv, info->envp);
	kfree(info);
}
EXPORT_SYMBOL(call_usermodehelper_freeinfo);

/* Keventd can't block, but this (a child) can. */
static int wait_for_helper(void *data)
{
	struct subprocess_info *sub_info = data;
	pid_t pid;

	/* Install a handler: if SIGCLD isn't handled sys_wait4 won't
	 * populate the status, but will return -ECHILD. */
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

	if (sub_info->wait == UMH_NO_WAIT)
		call_usermodehelper_freeinfo(sub_info);
	else
		complete(sub_info->complete);
	return 0;
}

/* This is run by khelper thread  */
static void __call_usermodehelper(struct work_struct *work)
{
	struct subprocess_info *sub_info =
		container_of(work, struct subprocess_info, work);
	pid_t pid;
	enum umh_wait wait = sub_info->wait;

	/* CLONE_VFORK: wait until the usermode helper has execve'd
	 * successfully We need the data structures to stay around
	 * until that is done.  */
	if (wait == UMH_WAIT_PROC || wait == UMH_NO_WAIT)
		pid = kernel_thread(wait_for_helper, sub_info,
				    CLONE_FS | CLONE_FILES | SIGCHLD);
	else
		pid = kernel_thread(____call_usermodehelper, sub_info,
				    CLONE_VFORK | SIGCHLD);

	switch (wait) {
	case UMH_NO_WAIT:
		break;

	case UMH_WAIT_PROC:
		if (pid > 0)
			break;
		sub_info->retval = pid;
		/* FALLTHROUGH */

	case UMH_WAIT_EXEC:
		complete(sub_info->complete);
	}
}

/**
 * call_usermodehelper_setup - prepare to call a usermode helper
 * @path - path to usermode executable
 * @argv - arg vector for process
 * @envp - environment for process
 *
 * Returns either NULL on allocation failure, or a subprocess_info
 * structure.  This should be passed to call_usermodehelper_exec to
 * exec the process and free the structure.
 */
struct subprocess_info *call_usermodehelper_setup(char *path,
						  char **argv, char **envp)
{
	struct subprocess_info *sub_info;
	sub_info = kzalloc(sizeof(struct subprocess_info),  GFP_ATOMIC);
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
 * call_usermodehelper_setkeys - set the session keys for usermode helper
 * @info: a subprocess_info returned by call_usermodehelper_setup
 * @session_keyring: the session keyring for the process
 */
void call_usermodehelper_setkeys(struct subprocess_info *info,
				 struct key *session_keyring)
{
	info->ring = session_keyring;
}
EXPORT_SYMBOL(call_usermodehelper_setkeys);

/**
 * call_usermodehelper_setcleanup - set a cleanup function
 * @info: a subprocess_info returned by call_usermodehelper_setup
 * @cleanup: a cleanup function
 *
 * The cleanup function is just befor ethe subprocess_info is about to
 * be freed.  This can be used for freeing the argv and envp.  The
 * Function must be runnable in either a process context or the
 * context in which call_usermodehelper_exec is called.
 */
void call_usermodehelper_setcleanup(struct subprocess_info *info,
				    void (*cleanup)(char **argv, char **envp))
{
	info->cleanup = cleanup;
}
EXPORT_SYMBOL(call_usermodehelper_setcleanup);

/**
 * call_usermodehelper_stdinpipe - set up a pipe to be used for stdin
 * @sub_info: a subprocess_info returned by call_usermodehelper_setup
 * @filp: set to the write-end of a pipe
 *
 * This constructs a pipe, and sets the read end to be the stdin of the
 * subprocess, and returns the write-end in *@filp.
 */
int call_usermodehelper_stdinpipe(struct subprocess_info *sub_info,
				  struct file **filp)
{
	struct file *f;

	f = create_write_pipe();
	if (IS_ERR(f))
		return PTR_ERR(f);
	*filp = f;

	f = create_read_pipe(f);
	if (IS_ERR(f)) {
		free_write_pipe(*filp);
		return PTR_ERR(f);
	}
	sub_info->stdin = f;

	return 0;
}
EXPORT_SYMBOL(call_usermodehelper_stdinpipe);

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
	int retval;

	if (sub_info->path[0] == '\0') {
		retval = 0;
		goto out;
	}

	if (!khelper_wq) {
		retval = -EBUSY;
		goto out;
	}

	sub_info->complete = &done;
	sub_info->wait = wait;

	queue_work(khelper_wq, &sub_info->work);
	if (wait == UMH_NO_WAIT) /* task has freed sub_info */
		return 0;
	wait_for_completion(&done);
	retval = sub_info->retval;

  out:
	call_usermodehelper_freeinfo(sub_info);
	return retval;
}
EXPORT_SYMBOL(call_usermodehelper_exec);

/**
 * call_usermodehelper_pipe - call a usermode helper process with a pipe stdin
 * @path: path to usermode executable
 * @argv: arg vector for process
 * @envp: environment for process
 * @filp: set to the write-end of a pipe
 *
 * This is a simple wrapper which executes a usermode-helper function
 * with a pipe as stdin.  It is implemented entirely in terms of
 * lower-level call_usermodehelper_* functions.
 */
int call_usermodehelper_pipe(char *path, char **argv, char **envp,
			     struct file **filp)
{
	struct subprocess_info *sub_info;
	int ret;

	sub_info = call_usermodehelper_setup(path, argv, envp);
	if (sub_info == NULL)
		return -ENOMEM;

	ret = call_usermodehelper_stdinpipe(sub_info, filp);
	if (ret < 0)
		goto out;

	return call_usermodehelper_exec(sub_info, 1);

  out:
	call_usermodehelper_freeinfo(sub_info);
	return ret;
}
EXPORT_SYMBOL(call_usermodehelper_pipe);

void __init usermodehelper_init(void)
{
	khelper_wq = create_singlethread_workqueue("khelper");
	BUG_ON(!khelper_wq);
}
