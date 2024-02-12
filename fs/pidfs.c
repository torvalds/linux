// SPDX-License-Identifier: GPL-2.0
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/magic.h>
#include <linux/mount.h>
#include <linux/pid.h>
#include <linux/pid_namespace.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/proc_ns.h>
#include <linux/pseudo_fs.h>
#include <linux/seq_file.h>
#include <uapi/linux/pidfd.h>

static int pidfd_release(struct inode *inode, struct file *file)
{
	struct pid *pid = file->private_data;

	file->private_data = NULL;
	put_pid(pid);
	return 0;
}

#ifdef CONFIG_PROC_FS
/**
 * pidfd_show_fdinfo - print information about a pidfd
 * @m: proc fdinfo file
 * @f: file referencing a pidfd
 *
 * Pid:
 * This function will print the pid that a given pidfd refers to in the
 * pid namespace of the procfs instance.
 * If the pid namespace of the process is not a descendant of the pid
 * namespace of the procfs instance 0 will be shown as its pid. This is
 * similar to calling getppid() on a process whose parent is outside of
 * its pid namespace.
 *
 * NSpid:
 * If pid namespaces are supported then this function will also print
 * the pid of a given pidfd refers to for all descendant pid namespaces
 * starting from the current pid namespace of the instance, i.e. the
 * Pid field and the first entry in the NSpid field will be identical.
 * If the pid namespace of the process is not a descendant of the pid
 * namespace of the procfs instance 0 will be shown as its first NSpid
 * entry and no others will be shown.
 * Note that this differs from the Pid and NSpid fields in
 * /proc/<pid>/status where Pid and NSpid are always shown relative to
 * the  pid namespace of the procfs instance. The difference becomes
 * obvious when sending around a pidfd between pid namespaces from a
 * different branch of the tree, i.e. where no ancestral relation is
 * present between the pid namespaces:
 * - create two new pid namespaces ns1 and ns2 in the initial pid
 *   namespace (also take care to create new mount namespaces in the
 *   new pid namespace and mount procfs)
 * - create a process with a pidfd in ns1
 * - send pidfd from ns1 to ns2
 * - read /proc/self/fdinfo/<pidfd> and observe that both Pid and NSpid
 *   have exactly one entry, which is 0
 */
static void pidfd_show_fdinfo(struct seq_file *m, struct file *f)
{
	struct pid *pid = f->private_data;
	struct pid_namespace *ns;
	pid_t nr = -1;

	if (likely(pid_has_task(pid, PIDTYPE_PID))) {
		ns = proc_pid_ns(file_inode(m->file)->i_sb);
		nr = pid_nr_ns(pid, ns);
	}

	seq_put_decimal_ll(m, "Pid:\t", nr);

#ifdef CONFIG_PID_NS
	seq_put_decimal_ll(m, "\nNSpid:\t", nr);
	if (nr > 0) {
		int i;

		/* If nr is non-zero it means that 'pid' is valid and that
		 * ns, i.e. the pid namespace associated with the procfs
		 * instance, is in the pid namespace hierarchy of pid.
		 * Start at one below the already printed level.
		 */
		for (i = ns->level + 1; i <= pid->level; i++)
			seq_put_decimal_ll(m, "\t", pid->numbers[i].nr);
	}
#endif
	seq_putc(m, '\n');
}
#endif

/*
 * Poll support for process exit notification.
 */
static __poll_t pidfd_poll(struct file *file, struct poll_table_struct *pts)
{
	struct pid *pid = file->private_data;
	bool thread = file->f_flags & PIDFD_THREAD;
	struct task_struct *task;
	__poll_t poll_flags = 0;

	poll_wait(file, &pid->wait_pidfd, pts);
	/*
	 * Depending on PIDFD_THREAD, inform pollers when the thread
	 * or the whole thread-group exits.
	 */
	guard(rcu)();
	task = pid_task(pid, PIDTYPE_PID);
	if (!task)
		poll_flags = EPOLLIN | EPOLLRDNORM | EPOLLHUP;
	else if (task->exit_state && (thread || thread_group_empty(task)))
		poll_flags = EPOLLIN | EPOLLRDNORM;

	return poll_flags;
}

const struct file_operations pidfd_fops = {
	.release	= pidfd_release,
	.poll		= pidfd_poll,
#ifdef CONFIG_PROC_FS
	.show_fdinfo	= pidfd_show_fdinfo,
#endif
};
