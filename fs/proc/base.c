/*
 *  linux/fs/proc/base.c
 *
 *  Copyright (C) 1991, 1992 Linus Torvalds
 *
 *  proc base directory handling functions
 *
 *  1999, Al Viro. Rewritten. Now it covers the whole per-process part.
 *  Instead of using magical inumbers to determine the kind of object
 *  we allocate and fill in-core inodes upon lookup. They don't even
 *  go into icache. We cache the reference to task_struct upon lookup too.
 *  Eventually it should become a filesystem in its own. We don't use the
 *  rest of procfs anymore.
 *
 *
 *  Changelog:
 *  17-Jan-2005
 *  Allan Bezerra
 *  Bruna Moreira <bruna.moreira@indt.org.br>
 *  Edjard Mota <edjard.mota@indt.org.br>
 *  Ilias Biris <ilias.biris@indt.org.br>
 *  Mauricio Lin <mauricio.lin@indt.org.br>
 *
 *  Embedded Linux Lab - 10LE Instituto Nokia de Tecnologia - INdT
 *
 *  A new process specific entry (smaps) included in /proc. It shows the
 *  size of rss for each memory area. The maps entry lacks information
 *  about physical memory size (rss) for each mapped file, i.e.,
 *  rss information for executables and library files.
 *  This additional information is useful for any tools that need to know
 *  about physical memory consumption for a process specific library.
 *
 *  Changelog:
 *  21-Feb-2005
 *  Embedded Linux Lab - 10LE Instituto Nokia de Tecnologia - INdT
 *  Pud inclusion in the page table walking.
 *
 *  ChangeLog:
 *  10-Mar-2005
 *  10LE Instituto Nokia de Tecnologia - INdT:
 *  A better way to walks through the page table as suggested by Hugh Dickins.
 *
 *  Simo Piiroinen <simo.piiroinen@nokia.com>:
 *  Smaps information related to shared, private, clean and dirty pages.
 *
 *  Paul Mundt <paul.mundt@nokia.com>:
 *  Overall revision about smaps.
 */

#include <asm/uaccess.h>

#include <linux/errno.h>
#include <linux/time.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/capability.h>
#include <linux/file.h>
#include <linux/string.h>
#include <linux/seq_file.h>
#include <linux/namei.h>
#include <linux/mnt_namespace.h>
#include <linux/mm.h>
#include <linux/rcupdate.h>
#include <linux/kallsyms.h>
#include <linux/resource.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/security.h>
#include <linux/ptrace.h>
#include <linux/cgroup.h>
#include <linux/cpuset.h>
#include <linux/audit.h>
#include <linux/poll.h>
#include <linux/nsproxy.h>
#include <linux/oom.h>
#include <linux/elf.h>
#include <linux/pid_namespace.h>
#include "internal.h"

/* NOTE:
 *	Implementing inode permission operations in /proc is almost
 *	certainly an error.  Permission checks need to happen during
 *	each system call not at open time.  The reason is that most of
 *	what we wish to check for permissions in /proc varies at runtime.
 *
 *	The classic example of a problem is opening file descriptors
 *	in /proc for a task before it execs a suid executable.
 */

struct pid_entry {
	char *name;
	int len;
	mode_t mode;
	const struct inode_operations *iop;
	const struct file_operations *fop;
	union proc_op op;
};

#define NOD(NAME, MODE, IOP, FOP, OP) {			\
	.name = (NAME),					\
	.len  = sizeof(NAME) - 1,			\
	.mode = MODE,					\
	.iop  = IOP,					\
	.fop  = FOP,					\
	.op   = OP,					\
}

#define DIR(NAME, MODE, OTYPE)							\
	NOD(NAME, (S_IFDIR|(MODE)),						\
		&proc_##OTYPE##_inode_operations, &proc_##OTYPE##_operations,	\
		{} )
#define LNK(NAME, OTYPE)					\
	NOD(NAME, (S_IFLNK|S_IRWXUGO),				\
		&proc_pid_link_inode_operations, NULL,		\
		{ .proc_get_link = &proc_##OTYPE##_link } )
#define REG(NAME, MODE, OTYPE)				\
	NOD(NAME, (S_IFREG|(MODE)), NULL,		\
		&proc_##OTYPE##_operations, {})
#define INF(NAME, MODE, OTYPE)				\
	NOD(NAME, (S_IFREG|(MODE)), 			\
		NULL, &proc_info_file_operations,	\
		{ .proc_read = &proc_##OTYPE } )
#define ONE(NAME, MODE, OTYPE)				\
	NOD(NAME, (S_IFREG|(MODE)), 			\
		NULL, &proc_single_file_operations,	\
		{ .proc_show = &proc_##OTYPE } )

int maps_protect;
EXPORT_SYMBOL(maps_protect);

static struct fs_struct *get_fs_struct(struct task_struct *task)
{
	struct fs_struct *fs;
	task_lock(task);
	fs = task->fs;
	if(fs)
		atomic_inc(&fs->count);
	task_unlock(task);
	return fs;
}

static int get_nr_threads(struct task_struct *tsk)
{
	/* Must be called with the rcu_read_lock held */
	unsigned long flags;
	int count = 0;

	if (lock_task_sighand(tsk, &flags)) {
		count = atomic_read(&tsk->signal->count);
		unlock_task_sighand(tsk, &flags);
	}
	return count;
}

static int proc_cwd_link(struct inode *inode, struct path *path)
{
	struct task_struct *task = get_proc_task(inode);
	struct fs_struct *fs = NULL;
	int result = -ENOENT;

	if (task) {
		fs = get_fs_struct(task);
		put_task_struct(task);
	}
	if (fs) {
		read_lock(&fs->lock);
		*path = fs->pwd;
		path_get(&fs->pwd);
		read_unlock(&fs->lock);
		result = 0;
		put_fs_struct(fs);
	}
	return result;
}

static int proc_root_link(struct inode *inode, struct path *path)
{
	struct task_struct *task = get_proc_task(inode);
	struct fs_struct *fs = NULL;
	int result = -ENOENT;

	if (task) {
		fs = get_fs_struct(task);
		put_task_struct(task);
	}
	if (fs) {
		read_lock(&fs->lock);
		*path = fs->root;
		path_get(&fs->root);
		read_unlock(&fs->lock);
		result = 0;
		put_fs_struct(fs);
	}
	return result;
}

#define MAY_PTRACE(task) \
	(task == current || \
	(task->parent == current && \
	(task->ptrace & PT_PTRACED) && \
	 (task_is_stopped_or_traced(task)) && \
	 security_ptrace(current,task) == 0))

struct mm_struct *mm_for_maps(struct task_struct *task)
{
	struct mm_struct *mm = get_task_mm(task);
	if (!mm)
		return NULL;
	down_read(&mm->mmap_sem);
	task_lock(task);
	if (task->mm != mm)
		goto out;
	if (task->mm != current->mm && __ptrace_may_attach(task) < 0)
		goto out;
	task_unlock(task);
	return mm;
out:
	task_unlock(task);
	up_read(&mm->mmap_sem);
	mmput(mm);
	return NULL;
}

static int proc_pid_cmdline(struct task_struct *task, char * buffer)
{
	int res = 0;
	unsigned int len;
	struct mm_struct *mm = get_task_mm(task);
	if (!mm)
		goto out;
	if (!mm->arg_end)
		goto out_mm;	/* Shh! No looking before we're done */

 	len = mm->arg_end - mm->arg_start;
 
	if (len > PAGE_SIZE)
		len = PAGE_SIZE;
 
	res = access_process_vm(task, mm->arg_start, buffer, len, 0);

	// If the nul at the end of args has been overwritten, then
	// assume application is using setproctitle(3).
	if (res > 0 && buffer[res-1] != '\0' && len < PAGE_SIZE) {
		len = strnlen(buffer, res);
		if (len < res) {
		    res = len;
		} else {
			len = mm->env_end - mm->env_start;
			if (len > PAGE_SIZE - res)
				len = PAGE_SIZE - res;
			res += access_process_vm(task, mm->env_start, buffer+res, len, 0);
			res = strnlen(buffer, res);
		}
	}
out_mm:
	mmput(mm);
out:
	return res;
}

static int proc_pid_auxv(struct task_struct *task, char *buffer)
{
	int res = 0;
	struct mm_struct *mm = get_task_mm(task);
	if (mm) {
		unsigned int nwords = 0;
		do
			nwords += 2;
		while (mm->saved_auxv[nwords - 2] != 0); /* AT_NULL */
		res = nwords * sizeof(mm->saved_auxv[0]);
		if (res > PAGE_SIZE)
			res = PAGE_SIZE;
		memcpy(buffer, mm->saved_auxv, res);
		mmput(mm);
	}
	return res;
}


#ifdef CONFIG_KALLSYMS
/*
 * Provides a wchan file via kallsyms in a proper one-value-per-file format.
 * Returns the resolved symbol.  If that fails, simply return the address.
 */
static int proc_pid_wchan(struct task_struct *task, char *buffer)
{
	unsigned long wchan;
	char symname[KSYM_NAME_LEN];

	wchan = get_wchan(task);

	if (lookup_symbol_name(wchan, symname) < 0)
		return sprintf(buffer, "%lu", wchan);
	else
		return sprintf(buffer, "%s", symname);
}
#endif /* CONFIG_KALLSYMS */

#ifdef CONFIG_SCHEDSTATS
/*
 * Provides /proc/PID/schedstat
 */
static int proc_pid_schedstat(struct task_struct *task, char *buffer)
{
	return sprintf(buffer, "%llu %llu %lu\n",
			task->sched_info.cpu_time,
			task->sched_info.run_delay,
			task->sched_info.pcount);
}
#endif

#ifdef CONFIG_LATENCYTOP
static int lstats_show_proc(struct seq_file *m, void *v)
{
	int i;
	struct inode *inode = m->private;
	struct task_struct *task = get_proc_task(inode);

	if (!task)
		return -ESRCH;
	seq_puts(m, "Latency Top version : v0.1\n");
	for (i = 0; i < 32; i++) {
		if (task->latency_record[i].backtrace[0]) {
			int q;
			seq_printf(m, "%i %li %li ",
				task->latency_record[i].count,
				task->latency_record[i].time,
				task->latency_record[i].max);
			for (q = 0; q < LT_BACKTRACEDEPTH; q++) {
				char sym[KSYM_NAME_LEN];
				char *c;
				if (!task->latency_record[i].backtrace[q])
					break;
				if (task->latency_record[i].backtrace[q] == ULONG_MAX)
					break;
				sprint_symbol(sym, task->latency_record[i].backtrace[q]);
				c = strchr(sym, '+');
				if (c)
					*c = 0;
				seq_printf(m, "%s ", sym);
			}
			seq_printf(m, "\n");
		}

	}
	put_task_struct(task);
	return 0;
}

static int lstats_open(struct inode *inode, struct file *file)
{
	return single_open(file, lstats_show_proc, inode);
}

static ssize_t lstats_write(struct file *file, const char __user *buf,
			    size_t count, loff_t *offs)
{
	struct task_struct *task = get_proc_task(file->f_dentry->d_inode);

	if (!task)
		return -ESRCH;
	clear_all_latency_tracing(task);
	put_task_struct(task);

	return count;
}

static const struct file_operations proc_lstats_operations = {
	.open		= lstats_open,
	.read		= seq_read,
	.write		= lstats_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

#endif

/* The badness from the OOM killer */
unsigned long badness(struct task_struct *p, unsigned long uptime);
static int proc_oom_score(struct task_struct *task, char *buffer)
{
	unsigned long points;
	struct timespec uptime;

	do_posix_clock_monotonic_gettime(&uptime);
	read_lock(&tasklist_lock);
	points = badness(task, uptime.tv_sec);
	read_unlock(&tasklist_lock);
	return sprintf(buffer, "%lu\n", points);
}

struct limit_names {
	char *name;
	char *unit;
};

static const struct limit_names lnames[RLIM_NLIMITS] = {
	[RLIMIT_CPU] = {"Max cpu time", "ms"},
	[RLIMIT_FSIZE] = {"Max file size", "bytes"},
	[RLIMIT_DATA] = {"Max data size", "bytes"},
	[RLIMIT_STACK] = {"Max stack size", "bytes"},
	[RLIMIT_CORE] = {"Max core file size", "bytes"},
	[RLIMIT_RSS] = {"Max resident set", "bytes"},
	[RLIMIT_NPROC] = {"Max processes", "processes"},
	[RLIMIT_NOFILE] = {"Max open files", "files"},
	[RLIMIT_MEMLOCK] = {"Max locked memory", "bytes"},
	[RLIMIT_AS] = {"Max address space", "bytes"},
	[RLIMIT_LOCKS] = {"Max file locks", "locks"},
	[RLIMIT_SIGPENDING] = {"Max pending signals", "signals"},
	[RLIMIT_MSGQUEUE] = {"Max msgqueue size", "bytes"},
	[RLIMIT_NICE] = {"Max nice priority", NULL},
	[RLIMIT_RTPRIO] = {"Max realtime priority", NULL},
	[RLIMIT_RTTIME] = {"Max realtime timeout", "us"},
};

/* Display limits for a process */
static int proc_pid_limits(struct task_struct *task, char *buffer)
{
	unsigned int i;
	int count = 0;
	unsigned long flags;
	char *bufptr = buffer;

	struct rlimit rlim[RLIM_NLIMITS];

	rcu_read_lock();
	if (!lock_task_sighand(task,&flags)) {
		rcu_read_unlock();
		return 0;
	}
	memcpy(rlim, task->signal->rlim, sizeof(struct rlimit) * RLIM_NLIMITS);
	unlock_task_sighand(task, &flags);
	rcu_read_unlock();

	/*
	 * print the file header
	 */
	count += sprintf(&bufptr[count], "%-25s %-20s %-20s %-10s\n",
			"Limit", "Soft Limit", "Hard Limit", "Units");

	for (i = 0; i < RLIM_NLIMITS; i++) {
		if (rlim[i].rlim_cur == RLIM_INFINITY)
			count += sprintf(&bufptr[count], "%-25s %-20s ",
					 lnames[i].name, "unlimited");
		else
			count += sprintf(&bufptr[count], "%-25s %-20lu ",
					 lnames[i].name, rlim[i].rlim_cur);

		if (rlim[i].rlim_max == RLIM_INFINITY)
			count += sprintf(&bufptr[count], "%-20s ", "unlimited");
		else
			count += sprintf(&bufptr[count], "%-20lu ",
					 rlim[i].rlim_max);

		if (lnames[i].unit)
			count += sprintf(&bufptr[count], "%-10s\n",
					 lnames[i].unit);
		else
			count += sprintf(&bufptr[count], "\n");
	}

	return count;
}

/************************************************************************/
/*                       Here the fs part begins                        */
/************************************************************************/

/* permission checks */
static int proc_fd_access_allowed(struct inode *inode)
{
	struct task_struct *task;
	int allowed = 0;
	/* Allow access to a task's file descriptors if it is us or we
	 * may use ptrace attach to the process and find out that
	 * information.
	 */
	task = get_proc_task(inode);
	if (task) {
		allowed = ptrace_may_attach(task);
		put_task_struct(task);
	}
	return allowed;
}

static int proc_setattr(struct dentry *dentry, struct iattr *attr)
{
	int error;
	struct inode *inode = dentry->d_inode;

	if (attr->ia_valid & ATTR_MODE)
		return -EPERM;

	error = inode_change_ok(inode, attr);
	if (!error)
		error = inode_setattr(inode, attr);
	return error;
}

static const struct inode_operations proc_def_inode_operations = {
	.setattr	= proc_setattr,
};

extern const struct seq_operations mounts_op;
struct proc_mounts {
	struct seq_file m;
	int event;
};

static int mounts_open(struct inode *inode, struct file *file)
{
	struct task_struct *task = get_proc_task(inode);
	struct nsproxy *nsp;
	struct mnt_namespace *ns = NULL;
	struct proc_mounts *p;
	int ret = -EINVAL;

	if (task) {
		rcu_read_lock();
		nsp = task_nsproxy(task);
		if (nsp) {
			ns = nsp->mnt_ns;
			if (ns)
				get_mnt_ns(ns);
		}
		rcu_read_unlock();

		put_task_struct(task);
	}

	if (ns) {
		ret = -ENOMEM;
		p = kmalloc(sizeof(struct proc_mounts), GFP_KERNEL);
		if (p) {
			file->private_data = &p->m;
			ret = seq_open(file, &mounts_op);
			if (!ret) {
				p->m.private = ns;
				p->event = ns->event;
				return 0;
			}
			kfree(p);
		}
		put_mnt_ns(ns);
	}
	return ret;
}

static int mounts_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = file->private_data;
	struct mnt_namespace *ns = m->private;
	put_mnt_ns(ns);
	return seq_release(inode, file);
}

static unsigned mounts_poll(struct file *file, poll_table *wait)
{
	struct proc_mounts *p = file->private_data;
	struct mnt_namespace *ns = p->m.private;
	unsigned res = 0;

	poll_wait(file, &ns->poll, wait);

	spin_lock(&vfsmount_lock);
	if (p->event != ns->event) {
		p->event = ns->event;
		res = POLLERR;
	}
	spin_unlock(&vfsmount_lock);

	return res;
}

static const struct file_operations proc_mounts_operations = {
	.open		= mounts_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= mounts_release,
	.poll		= mounts_poll,
};

extern const struct seq_operations mountstats_op;
static int mountstats_open(struct inode *inode, struct file *file)
{
	int ret = seq_open(file, &mountstats_op);

	if (!ret) {
		struct seq_file *m = file->private_data;
		struct nsproxy *nsp;
		struct mnt_namespace *mnt_ns = NULL;
		struct task_struct *task = get_proc_task(inode);

		if (task) {
			rcu_read_lock();
			nsp = task_nsproxy(task);
			if (nsp) {
				mnt_ns = nsp->mnt_ns;
				if (mnt_ns)
					get_mnt_ns(mnt_ns);
			}
			rcu_read_unlock();

			put_task_struct(task);
		}

		if (mnt_ns)
			m->private = mnt_ns;
		else {
			seq_release(inode, file);
			ret = -EINVAL;
		}
	}
	return ret;
}

static const struct file_operations proc_mountstats_operations = {
	.open		= mountstats_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= mounts_release,
};

#define PROC_BLOCK_SIZE	(3*1024)		/* 4K page size but our output routines use some slack for overruns */

static ssize_t proc_info_read(struct file * file, char __user * buf,
			  size_t count, loff_t *ppos)
{
	struct inode * inode = file->f_path.dentry->d_inode;
	unsigned long page;
	ssize_t length;
	struct task_struct *task = get_proc_task(inode);

	length = -ESRCH;
	if (!task)
		goto out_no_task;

	if (count > PROC_BLOCK_SIZE)
		count = PROC_BLOCK_SIZE;

	length = -ENOMEM;
	if (!(page = __get_free_page(GFP_TEMPORARY)))
		goto out;

	length = PROC_I(inode)->op.proc_read(task, (char*)page);

	if (length >= 0)
		length = simple_read_from_buffer(buf, count, ppos, (char *)page, length);
	free_page(page);
out:
	put_task_struct(task);
out_no_task:
	return length;
}

static const struct file_operations proc_info_file_operations = {
	.read		= proc_info_read,
};

static int proc_single_show(struct seq_file *m, void *v)
{
	struct inode *inode = m->private;
	struct pid_namespace *ns;
	struct pid *pid;
	struct task_struct *task;
	int ret;

	ns = inode->i_sb->s_fs_info;
	pid = proc_pid(inode);
	task = get_pid_task(pid, PIDTYPE_PID);
	if (!task)
		return -ESRCH;

	ret = PROC_I(inode)->op.proc_show(m, ns, pid, task);

	put_task_struct(task);
	return ret;
}

static int proc_single_open(struct inode *inode, struct file *filp)
{
	int ret;
	ret = single_open(filp, proc_single_show, NULL);
	if (!ret) {
		struct seq_file *m = filp->private_data;

		m->private = inode;
	}
	return ret;
}

static const struct file_operations proc_single_file_operations = {
	.open		= proc_single_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int mem_open(struct inode* inode, struct file* file)
{
	file->private_data = (void*)((long)current->self_exec_id);
	return 0;
}

static ssize_t mem_read(struct file * file, char __user * buf,
			size_t count, loff_t *ppos)
{
	struct task_struct *task = get_proc_task(file->f_path.dentry->d_inode);
	char *page;
	unsigned long src = *ppos;
	int ret = -ESRCH;
	struct mm_struct *mm;

	if (!task)
		goto out_no_task;

	if (!MAY_PTRACE(task) || !ptrace_may_attach(task))
		goto out;

	ret = -ENOMEM;
	page = (char *)__get_free_page(GFP_TEMPORARY);
	if (!page)
		goto out;

	ret = 0;
 
	mm = get_task_mm(task);
	if (!mm)
		goto out_free;

	ret = -EIO;
 
	if (file->private_data != (void*)((long)current->self_exec_id))
		goto out_put;

	ret = 0;
 
	while (count > 0) {
		int this_len, retval;

		this_len = (count > PAGE_SIZE) ? PAGE_SIZE : count;
		retval = access_process_vm(task, src, page, this_len, 0);
		if (!retval || !MAY_PTRACE(task) || !ptrace_may_attach(task)) {
			if (!ret)
				ret = -EIO;
			break;
		}

		if (copy_to_user(buf, page, retval)) {
			ret = -EFAULT;
			break;
		}
 
		ret += retval;
		src += retval;
		buf += retval;
		count -= retval;
	}
	*ppos = src;

out_put:
	mmput(mm);
out_free:
	free_page((unsigned long) page);
out:
	put_task_struct(task);
out_no_task:
	return ret;
}

#define mem_write NULL

#ifndef mem_write
/* This is a security hazard */
static ssize_t mem_write(struct file * file, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	int copied;
	char *page;
	struct task_struct *task = get_proc_task(file->f_path.dentry->d_inode);
	unsigned long dst = *ppos;

	copied = -ESRCH;
	if (!task)
		goto out_no_task;

	if (!MAY_PTRACE(task) || !ptrace_may_attach(task))
		goto out;

	copied = -ENOMEM;
	page = (char *)__get_free_page(GFP_TEMPORARY);
	if (!page)
		goto out;

	copied = 0;
	while (count > 0) {
		int this_len, retval;

		this_len = (count > PAGE_SIZE) ? PAGE_SIZE : count;
		if (copy_from_user(page, buf, this_len)) {
			copied = -EFAULT;
			break;
		}
		retval = access_process_vm(task, dst, page, this_len, 1);
		if (!retval) {
			if (!copied)
				copied = -EIO;
			break;
		}
		copied += retval;
		buf += retval;
		dst += retval;
		count -= retval;			
	}
	*ppos = dst;
	free_page((unsigned long) page);
out:
	put_task_struct(task);
out_no_task:
	return copied;
}
#endif

loff_t mem_lseek(struct file *file, loff_t offset, int orig)
{
	switch (orig) {
	case 0:
		file->f_pos = offset;
		break;
	case 1:
		file->f_pos += offset;
		break;
	default:
		return -EINVAL;
	}
	force_successful_syscall_return();
	return file->f_pos;
}

static const struct file_operations proc_mem_operations = {
	.llseek		= mem_lseek,
	.read		= mem_read,
	.write		= mem_write,
	.open		= mem_open,
};

static ssize_t environ_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct task_struct *task = get_proc_task(file->f_dentry->d_inode);
	char *page;
	unsigned long src = *ppos;
	int ret = -ESRCH;
	struct mm_struct *mm;

	if (!task)
		goto out_no_task;

	if (!ptrace_may_attach(task))
		goto out;

	ret = -ENOMEM;
	page = (char *)__get_free_page(GFP_TEMPORARY);
	if (!page)
		goto out;

	ret = 0;

	mm = get_task_mm(task);
	if (!mm)
		goto out_free;

	while (count > 0) {
		int this_len, retval, max_len;

		this_len = mm->env_end - (mm->env_start + src);

		if (this_len <= 0)
			break;

		max_len = (count > PAGE_SIZE) ? PAGE_SIZE : count;
		this_len = (this_len > max_len) ? max_len : this_len;

		retval = access_process_vm(task, (mm->env_start + src),
			page, this_len, 0);

		if (retval <= 0) {
			ret = retval;
			break;
		}

		if (copy_to_user(buf, page, retval)) {
			ret = -EFAULT;
			break;
		}

		ret += retval;
		src += retval;
		buf += retval;
		count -= retval;
	}
	*ppos = src;

	mmput(mm);
out_free:
	free_page((unsigned long) page);
out:
	put_task_struct(task);
out_no_task:
	return ret;
}

static const struct file_operations proc_environ_operations = {
	.read		= environ_read,
};

static ssize_t oom_adjust_read(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	struct task_struct *task = get_proc_task(file->f_path.dentry->d_inode);
	char buffer[PROC_NUMBUF];
	size_t len;
	int oom_adjust;

	if (!task)
		return -ESRCH;
	oom_adjust = task->oomkilladj;
	put_task_struct(task);

	len = snprintf(buffer, sizeof(buffer), "%i\n", oom_adjust);

	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static ssize_t oom_adjust_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	struct task_struct *task;
	char buffer[PROC_NUMBUF], *end;
	int oom_adjust;

	memset(buffer, 0, sizeof(buffer));
	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;
	if (copy_from_user(buffer, buf, count))
		return -EFAULT;
	oom_adjust = simple_strtol(buffer, &end, 0);
	if ((oom_adjust < OOM_ADJUST_MIN || oom_adjust > OOM_ADJUST_MAX) &&
	     oom_adjust != OOM_DISABLE)
		return -EINVAL;
	if (*end == '\n')
		end++;
	task = get_proc_task(file->f_path.dentry->d_inode);
	if (!task)
		return -ESRCH;
	if (oom_adjust < task->oomkilladj && !capable(CAP_SYS_RESOURCE)) {
		put_task_struct(task);
		return -EACCES;
	}
	task->oomkilladj = oom_adjust;
	put_task_struct(task);
	if (end - buffer == 0)
		return -EIO;
	return end - buffer;
}

static const struct file_operations proc_oom_adjust_operations = {
	.read		= oom_adjust_read,
	.write		= oom_adjust_write,
};

#ifdef CONFIG_AUDITSYSCALL
#define TMPBUFLEN 21
static ssize_t proc_loginuid_read(struct file * file, char __user * buf,
				  size_t count, loff_t *ppos)
{
	struct inode * inode = file->f_path.dentry->d_inode;
	struct task_struct *task = get_proc_task(inode);
	ssize_t length;
	char tmpbuf[TMPBUFLEN];

	if (!task)
		return -ESRCH;
	length = scnprintf(tmpbuf, TMPBUFLEN, "%u",
				audit_get_loginuid(task));
	put_task_struct(task);
	return simple_read_from_buffer(buf, count, ppos, tmpbuf, length);
}

static ssize_t proc_loginuid_write(struct file * file, const char __user * buf,
				   size_t count, loff_t *ppos)
{
	struct inode * inode = file->f_path.dentry->d_inode;
	char *page, *tmp;
	ssize_t length;
	uid_t loginuid;

	if (!capable(CAP_AUDIT_CONTROL))
		return -EPERM;

	if (current != pid_task(proc_pid(inode), PIDTYPE_PID))
		return -EPERM;

	if (count >= PAGE_SIZE)
		count = PAGE_SIZE - 1;

	if (*ppos != 0) {
		/* No partial writes. */
		return -EINVAL;
	}
	page = (char*)__get_free_page(GFP_TEMPORARY);
	if (!page)
		return -ENOMEM;
	length = -EFAULT;
	if (copy_from_user(page, buf, count))
		goto out_free_page;

	page[count] = '\0';
	loginuid = simple_strtoul(page, &tmp, 10);
	if (tmp == page) {
		length = -EINVAL;
		goto out_free_page;

	}
	length = audit_set_loginuid(current, loginuid);
	if (likely(length == 0))
		length = count;

out_free_page:
	free_page((unsigned long) page);
	return length;
}

static const struct file_operations proc_loginuid_operations = {
	.read		= proc_loginuid_read,
	.write		= proc_loginuid_write,
};
#endif

#ifdef CONFIG_FAULT_INJECTION
static ssize_t proc_fault_inject_read(struct file * file, char __user * buf,
				      size_t count, loff_t *ppos)
{
	struct task_struct *task = get_proc_task(file->f_dentry->d_inode);
	char buffer[PROC_NUMBUF];
	size_t len;
	int make_it_fail;

	if (!task)
		return -ESRCH;
	make_it_fail = task->make_it_fail;
	put_task_struct(task);

	len = snprintf(buffer, sizeof(buffer), "%i\n", make_it_fail);

	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static ssize_t proc_fault_inject_write(struct file * file,
			const char __user * buf, size_t count, loff_t *ppos)
{
	struct task_struct *task;
	char buffer[PROC_NUMBUF], *end;
	int make_it_fail;

	if (!capable(CAP_SYS_RESOURCE))
		return -EPERM;
	memset(buffer, 0, sizeof(buffer));
	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;
	if (copy_from_user(buffer, buf, count))
		return -EFAULT;
	make_it_fail = simple_strtol(buffer, &end, 0);
	if (*end == '\n')
		end++;
	task = get_proc_task(file->f_dentry->d_inode);
	if (!task)
		return -ESRCH;
	task->make_it_fail = make_it_fail;
	put_task_struct(task);
	if (end - buffer == 0)
		return -EIO;
	return end - buffer;
}

static const struct file_operations proc_fault_inject_operations = {
	.read		= proc_fault_inject_read,
	.write		= proc_fault_inject_write,
};
#endif


#ifdef CONFIG_SCHED_DEBUG
/*
 * Print out various scheduling related per-task fields:
 */
static int sched_show(struct seq_file *m, void *v)
{
	struct inode *inode = m->private;
	struct task_struct *p;

	WARN_ON(!inode);

	p = get_proc_task(inode);
	if (!p)
		return -ESRCH;
	proc_sched_show_task(p, m);

	put_task_struct(p);

	return 0;
}

static ssize_t
sched_write(struct file *file, const char __user *buf,
	    size_t count, loff_t *offset)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	struct task_struct *p;

	WARN_ON(!inode);

	p = get_proc_task(inode);
	if (!p)
		return -ESRCH;
	proc_sched_set_task(p);

	put_task_struct(p);

	return count;
}

static int sched_open(struct inode *inode, struct file *filp)
{
	int ret;

	ret = single_open(filp, sched_show, NULL);
	if (!ret) {
		struct seq_file *m = filp->private_data;

		m->private = inode;
	}
	return ret;
}

static const struct file_operations proc_pid_sched_operations = {
	.open		= sched_open,
	.read		= seq_read,
	.write		= sched_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

#endif

static void *proc_pid_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	struct inode *inode = dentry->d_inode;
	int error = -EACCES;

	/* We don't need a base pointer in the /proc filesystem */
	path_put(&nd->path);

	/* Are we allowed to snoop on the tasks file descriptors? */
	if (!proc_fd_access_allowed(inode))
		goto out;

	error = PROC_I(inode)->op.proc_get_link(inode, &nd->path);
	nd->last_type = LAST_BIND;
out:
	return ERR_PTR(error);
}

static int do_proc_readlink(struct path *path, char __user *buffer, int buflen)
{
	char *tmp = (char*)__get_free_page(GFP_TEMPORARY);
	char *pathname;
	int len;

	if (!tmp)
		return -ENOMEM;

	pathname = d_path(path, tmp, PAGE_SIZE);
	len = PTR_ERR(pathname);
	if (IS_ERR(pathname))
		goto out;
	len = tmp + PAGE_SIZE - 1 - pathname;

	if (len > buflen)
		len = buflen;
	if (copy_to_user(buffer, pathname, len))
		len = -EFAULT;
 out:
	free_page((unsigned long)tmp);
	return len;
}

static int proc_pid_readlink(struct dentry * dentry, char __user * buffer, int buflen)
{
	int error = -EACCES;
	struct inode *inode = dentry->d_inode;
	struct path path;

	/* Are we allowed to snoop on the tasks file descriptors? */
	if (!proc_fd_access_allowed(inode))
		goto out;

	error = PROC_I(inode)->op.proc_get_link(inode, &path);
	if (error)
		goto out;

	error = do_proc_readlink(&path, buffer, buflen);
	path_put(&path);
out:
	return error;
}

static const struct inode_operations proc_pid_link_inode_operations = {
	.readlink	= proc_pid_readlink,
	.follow_link	= proc_pid_follow_link,
	.setattr	= proc_setattr,
};


/* building an inode */

static int task_dumpable(struct task_struct *task)
{
	int dumpable = 0;
	struct mm_struct *mm;

	task_lock(task);
	mm = task->mm;
	if (mm)
		dumpable = get_dumpable(mm);
	task_unlock(task);
	if(dumpable == 1)
		return 1;
	return 0;
}


static struct inode *proc_pid_make_inode(struct super_block * sb, struct task_struct *task)
{
	struct inode * inode;
	struct proc_inode *ei;

	/* We need a new inode */

	inode = new_inode(sb);
	if (!inode)
		goto out;

	/* Common stuff */
	ei = PROC_I(inode);
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	inode->i_op = &proc_def_inode_operations;

	/*
	 * grab the reference to task.
	 */
	ei->pid = get_task_pid(task, PIDTYPE_PID);
	if (!ei->pid)
		goto out_unlock;

	inode->i_uid = 0;
	inode->i_gid = 0;
	if (task_dumpable(task)) {
		inode->i_uid = task->euid;
		inode->i_gid = task->egid;
	}
	security_task_to_inode(task, inode);

out:
	return inode;

out_unlock:
	iput(inode);
	return NULL;
}

static int pid_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
	struct task_struct *task;
	generic_fillattr(inode, stat);

	rcu_read_lock();
	stat->uid = 0;
	stat->gid = 0;
	task = pid_task(proc_pid(inode), PIDTYPE_PID);
	if (task) {
		if ((inode->i_mode == (S_IFDIR|S_IRUGO|S_IXUGO)) ||
		    task_dumpable(task)) {
			stat->uid = task->euid;
			stat->gid = task->egid;
		}
	}
	rcu_read_unlock();
	return 0;
}

/* dentry stuff */

/*
 *	Exceptional case: normally we are not allowed to unhash a busy
 * directory. In this case, however, we can do it - no aliasing problems
 * due to the way we treat inodes.
 *
 * Rewrite the inode's ownerships here because the owning task may have
 * performed a setuid(), etc.
 *
 * Before the /proc/pid/status file was created the only way to read
 * the effective uid of a /process was to stat /proc/pid.  Reading
 * /proc/pid/status is slow enough that procps and other packages
 * kept stating /proc/pid.  To keep the rules in /proc simple I have
 * made this apply to all per process world readable and executable
 * directories.
 */
static int pid_revalidate(struct dentry *dentry, struct nameidata *nd)
{
	struct inode *inode = dentry->d_inode;
	struct task_struct *task = get_proc_task(inode);
	if (task) {
		if ((inode->i_mode == (S_IFDIR|S_IRUGO|S_IXUGO)) ||
		    task_dumpable(task)) {
			inode->i_uid = task->euid;
			inode->i_gid = task->egid;
		} else {
			inode->i_uid = 0;
			inode->i_gid = 0;
		}
		inode->i_mode &= ~(S_ISUID | S_ISGID);
		security_task_to_inode(task, inode);
		put_task_struct(task);
		return 1;
	}
	d_drop(dentry);
	return 0;
}

static int pid_delete_dentry(struct dentry * dentry)
{
	/* Is the task we represent dead?
	 * If so, then don't put the dentry on the lru list,
	 * kill it immediately.
	 */
	return !proc_pid(dentry->d_inode)->tasks[PIDTYPE_PID].first;
}

static struct dentry_operations pid_dentry_operations =
{
	.d_revalidate	= pid_revalidate,
	.d_delete	= pid_delete_dentry,
};

/* Lookups */

typedef struct dentry *instantiate_t(struct inode *, struct dentry *,
				struct task_struct *, const void *);

/*
 * Fill a directory entry.
 *
 * If possible create the dcache entry and derive our inode number and
 * file type from dcache entry.
 *
 * Since all of the proc inode numbers are dynamically generated, the inode
 * numbers do not exist until the inode is cache.  This means creating the
 * the dcache entry in readdir is necessary to keep the inode numbers
 * reported by readdir in sync with the inode numbers reported
 * by stat.
 */
static int proc_fill_cache(struct file *filp, void *dirent, filldir_t filldir,
	char *name, int len,
	instantiate_t instantiate, struct task_struct *task, const void *ptr)
{
	struct dentry *child, *dir = filp->f_path.dentry;
	struct inode *inode;
	struct qstr qname;
	ino_t ino = 0;
	unsigned type = DT_UNKNOWN;

	qname.name = name;
	qname.len  = len;
	qname.hash = full_name_hash(name, len);

	child = d_lookup(dir, &qname);
	if (!child) {
		struct dentry *new;
		new = d_alloc(dir, &qname);
		if (new) {
			child = instantiate(dir->d_inode, new, task, ptr);
			if (child)
				dput(new);
			else
				child = new;
		}
	}
	if (!child || IS_ERR(child) || !child->d_inode)
		goto end_instantiate;
	inode = child->d_inode;
	if (inode) {
		ino = inode->i_ino;
		type = inode->i_mode >> 12;
	}
	dput(child);
end_instantiate:
	if (!ino)
		ino = find_inode_number(dir, &qname);
	if (!ino)
		ino = 1;
	return filldir(dirent, name, len, filp->f_pos, ino, type);
}

static unsigned name_to_int(struct dentry *dentry)
{
	const char *name = dentry->d_name.name;
	int len = dentry->d_name.len;
	unsigned n = 0;

	if (len > 1 && *name == '0')
		goto out;
	while (len-- > 0) {
		unsigned c = *name++ - '0';
		if (c > 9)
			goto out;
		if (n >= (~0U-9)/10)
			goto out;
		n *= 10;
		n += c;
	}
	return n;
out:
	return ~0U;
}

#define PROC_FDINFO_MAX 64

static int proc_fd_info(struct inode *inode, struct path *path, char *info)
{
	struct task_struct *task = get_proc_task(inode);
	struct files_struct *files = NULL;
	struct file *file;
	int fd = proc_fd(inode);

	if (task) {
		files = get_files_struct(task);
		put_task_struct(task);
	}
	if (files) {
		/*
		 * We are not taking a ref to the file structure, so we must
		 * hold ->file_lock.
		 */
		spin_lock(&files->file_lock);
		file = fcheck_files(files, fd);
		if (file) {
			if (path) {
				*path = file->f_path;
				path_get(&file->f_path);
			}
			if (info)
				snprintf(info, PROC_FDINFO_MAX,
					 "pos:\t%lli\n"
					 "flags:\t0%o\n",
					 (long long) file->f_pos,
					 file->f_flags);
			spin_unlock(&files->file_lock);
			put_files_struct(files);
			return 0;
		}
		spin_unlock(&files->file_lock);
		put_files_struct(files);
	}
	return -ENOENT;
}

static int proc_fd_link(struct inode *inode, struct path *path)
{
	return proc_fd_info(inode, path, NULL);
}

static int tid_fd_revalidate(struct dentry *dentry, struct nameidata *nd)
{
	struct inode *inode = dentry->d_inode;
	struct task_struct *task = get_proc_task(inode);
	int fd = proc_fd(inode);
	struct files_struct *files;

	if (task) {
		files = get_files_struct(task);
		if (files) {
			rcu_read_lock();
			if (fcheck_files(files, fd)) {
				rcu_read_unlock();
				put_files_struct(files);
				if (task_dumpable(task)) {
					inode->i_uid = task->euid;
					inode->i_gid = task->egid;
				} else {
					inode->i_uid = 0;
					inode->i_gid = 0;
				}
				inode->i_mode &= ~(S_ISUID | S_ISGID);
				security_task_to_inode(task, inode);
				put_task_struct(task);
				return 1;
			}
			rcu_read_unlock();
			put_files_struct(files);
		}
		put_task_struct(task);
	}
	d_drop(dentry);
	return 0;
}

static struct dentry_operations tid_fd_dentry_operations =
{
	.d_revalidate	= tid_fd_revalidate,
	.d_delete	= pid_delete_dentry,
};

static struct dentry *proc_fd_instantiate(struct inode *dir,
	struct dentry *dentry, struct task_struct *task, const void *ptr)
{
	unsigned fd = *(const unsigned *)ptr;
	struct file *file;
	struct files_struct *files;
 	struct inode *inode;
 	struct proc_inode *ei;
	struct dentry *error = ERR_PTR(-ENOENT);

	inode = proc_pid_make_inode(dir->i_sb, task);
	if (!inode)
		goto out;
	ei = PROC_I(inode);
	ei->fd = fd;
	files = get_files_struct(task);
	if (!files)
		goto out_iput;
	inode->i_mode = S_IFLNK;

	/*
	 * We are not taking a ref to the file structure, so we must
	 * hold ->file_lock.
	 */
	spin_lock(&files->file_lock);
	file = fcheck_files(files, fd);
	if (!file)
		goto out_unlock;
	if (file->f_mode & 1)
		inode->i_mode |= S_IRUSR | S_IXUSR;
	if (file->f_mode & 2)
		inode->i_mode |= S_IWUSR | S_IXUSR;
	spin_unlock(&files->file_lock);
	put_files_struct(files);

	inode->i_op = &proc_pid_link_inode_operations;
	inode->i_size = 64;
	ei->op.proc_get_link = proc_fd_link;
	dentry->d_op = &tid_fd_dentry_operations;
	d_add(dentry, inode);
	/* Close the race of the process dying before we return the dentry */
	if (tid_fd_revalidate(dentry, NULL))
		error = NULL;

 out:
	return error;
out_unlock:
	spin_unlock(&files->file_lock);
	put_files_struct(files);
out_iput:
	iput(inode);
	goto out;
}

static struct dentry *proc_lookupfd_common(struct inode *dir,
					   struct dentry *dentry,
					   instantiate_t instantiate)
{
	struct task_struct *task = get_proc_task(dir);
	unsigned fd = name_to_int(dentry);
	struct dentry *result = ERR_PTR(-ENOENT);

	if (!task)
		goto out_no_task;
	if (fd == ~0U)
		goto out;

	result = instantiate(dir, dentry, task, &fd);
out:
	put_task_struct(task);
out_no_task:
	return result;
}

static int proc_readfd_common(struct file * filp, void * dirent,
			      filldir_t filldir, instantiate_t instantiate)
{
	struct dentry *dentry = filp->f_path.dentry;
	struct inode *inode = dentry->d_inode;
	struct task_struct *p = get_proc_task(inode);
	unsigned int fd, ino;
	int retval;
	struct files_struct * files;
	struct fdtable *fdt;

	retval = -ENOENT;
	if (!p)
		goto out_no_task;
	retval = 0;

	fd = filp->f_pos;
	switch (fd) {
		case 0:
			if (filldir(dirent, ".", 1, 0, inode->i_ino, DT_DIR) < 0)
				goto out;
			filp->f_pos++;
		case 1:
			ino = parent_ino(dentry);
			if (filldir(dirent, "..", 2, 1, ino, DT_DIR) < 0)
				goto out;
			filp->f_pos++;
		default:
			files = get_files_struct(p);
			if (!files)
				goto out;
			rcu_read_lock();
			fdt = files_fdtable(files);
			for (fd = filp->f_pos-2;
			     fd < fdt->max_fds;
			     fd++, filp->f_pos++) {
				char name[PROC_NUMBUF];
				int len;

				if (!fcheck_files(files, fd))
					continue;
				rcu_read_unlock();

				len = snprintf(name, sizeof(name), "%d", fd);
				if (proc_fill_cache(filp, dirent, filldir,
						    name, len, instantiate,
						    p, &fd) < 0) {
					rcu_read_lock();
					break;
				}
				rcu_read_lock();
			}
			rcu_read_unlock();
			put_files_struct(files);
	}
out:
	put_task_struct(p);
out_no_task:
	return retval;
}

static struct dentry *proc_lookupfd(struct inode *dir, struct dentry *dentry,
				    struct nameidata *nd)
{
	return proc_lookupfd_common(dir, dentry, proc_fd_instantiate);
}

static int proc_readfd(struct file *filp, void *dirent, filldir_t filldir)
{
	return proc_readfd_common(filp, dirent, filldir, proc_fd_instantiate);
}

static ssize_t proc_fdinfo_read(struct file *file, char __user *buf,
				      size_t len, loff_t *ppos)
{
	char tmp[PROC_FDINFO_MAX];
	int err = proc_fd_info(file->f_path.dentry->d_inode, NULL, tmp);
	if (!err)
		err = simple_read_from_buffer(buf, len, ppos, tmp, strlen(tmp));
	return err;
}

static const struct file_operations proc_fdinfo_file_operations = {
	.open		= nonseekable_open,
	.read		= proc_fdinfo_read,
};

static const struct file_operations proc_fd_operations = {
	.read		= generic_read_dir,
	.readdir	= proc_readfd,
};

/*
 * /proc/pid/fd needs a special permission handler so that a process can still
 * access /proc/self/fd after it has executed a setuid().
 */
static int proc_fd_permission(struct inode *inode, int mask,
				struct nameidata *nd)
{
	int rv;

	rv = generic_permission(inode, mask, NULL);
	if (rv == 0)
		return 0;
	if (task_pid(current) == proc_pid(inode))
		rv = 0;
	return rv;
}

/*
 * proc directories can do almost nothing..
 */
static const struct inode_operations proc_fd_inode_operations = {
	.lookup		= proc_lookupfd,
	.permission	= proc_fd_permission,
	.setattr	= proc_setattr,
};

static struct dentry *proc_fdinfo_instantiate(struct inode *dir,
	struct dentry *dentry, struct task_struct *task, const void *ptr)
{
	unsigned fd = *(unsigned *)ptr;
 	struct inode *inode;
 	struct proc_inode *ei;
	struct dentry *error = ERR_PTR(-ENOENT);

	inode = proc_pid_make_inode(dir->i_sb, task);
	if (!inode)
		goto out;
	ei = PROC_I(inode);
	ei->fd = fd;
	inode->i_mode = S_IFREG | S_IRUSR;
	inode->i_fop = &proc_fdinfo_file_operations;
	dentry->d_op = &tid_fd_dentry_operations;
	d_add(dentry, inode);
	/* Close the race of the process dying before we return the dentry */
	if (tid_fd_revalidate(dentry, NULL))
		error = NULL;

 out:
	return error;
}

static struct dentry *proc_lookupfdinfo(struct inode *dir,
					struct dentry *dentry,
					struct nameidata *nd)
{
	return proc_lookupfd_common(dir, dentry, proc_fdinfo_instantiate);
}

static int proc_readfdinfo(struct file *filp, void *dirent, filldir_t filldir)
{
	return proc_readfd_common(filp, dirent, filldir,
				  proc_fdinfo_instantiate);
}

static const struct file_operations proc_fdinfo_operations = {
	.read		= generic_read_dir,
	.readdir	= proc_readfdinfo,
};

/*
 * proc directories can do almost nothing..
 */
static const struct inode_operations proc_fdinfo_inode_operations = {
	.lookup		= proc_lookupfdinfo,
	.setattr	= proc_setattr,
};


static struct dentry *proc_pident_instantiate(struct inode *dir,
	struct dentry *dentry, struct task_struct *task, const void *ptr)
{
	const struct pid_entry *p = ptr;
	struct inode *inode;
	struct proc_inode *ei;
	struct dentry *error = ERR_PTR(-EINVAL);

	inode = proc_pid_make_inode(dir->i_sb, task);
	if (!inode)
		goto out;

	ei = PROC_I(inode);
	inode->i_mode = p->mode;
	if (S_ISDIR(inode->i_mode))
		inode->i_nlink = 2;	/* Use getattr to fix if necessary */
	if (p->iop)
		inode->i_op = p->iop;
	if (p->fop)
		inode->i_fop = p->fop;
	ei->op = p->op;
	dentry->d_op = &pid_dentry_operations;
	d_add(dentry, inode);
	/* Close the race of the process dying before we return the dentry */
	if (pid_revalidate(dentry, NULL))
		error = NULL;
out:
	return error;
}

static struct dentry *proc_pident_lookup(struct inode *dir, 
					 struct dentry *dentry,
					 const struct pid_entry *ents,
					 unsigned int nents)
{
	struct inode *inode;
	struct dentry *error;
	struct task_struct *task = get_proc_task(dir);
	const struct pid_entry *p, *last;

	error = ERR_PTR(-ENOENT);
	inode = NULL;

	if (!task)
		goto out_no_task;

	/*
	 * Yes, it does not scale. And it should not. Don't add
	 * new entries into /proc/<tgid>/ without very good reasons.
	 */
	last = &ents[nents - 1];
	for (p = ents; p <= last; p++) {
		if (p->len != dentry->d_name.len)
			continue;
		if (!memcmp(dentry->d_name.name, p->name, p->len))
			break;
	}
	if (p > last)
		goto out;

	error = proc_pident_instantiate(dir, dentry, task, p);
out:
	put_task_struct(task);
out_no_task:
	return error;
}

static int proc_pident_fill_cache(struct file *filp, void *dirent,
	filldir_t filldir, struct task_struct *task, const struct pid_entry *p)
{
	return proc_fill_cache(filp, dirent, filldir, p->name, p->len,
				proc_pident_instantiate, task, p);
}

static int proc_pident_readdir(struct file *filp,
		void *dirent, filldir_t filldir,
		const struct pid_entry *ents, unsigned int nents)
{
	int i;
	struct dentry *dentry = filp->f_path.dentry;
	struct inode *inode = dentry->d_inode;
	struct task_struct *task = get_proc_task(inode);
	const struct pid_entry *p, *last;
	ino_t ino;
	int ret;

	ret = -ENOENT;
	if (!task)
		goto out_no_task;

	ret = 0;
	i = filp->f_pos;
	switch (i) {
	case 0:
		ino = inode->i_ino;
		if (filldir(dirent, ".", 1, i, ino, DT_DIR) < 0)
			goto out;
		i++;
		filp->f_pos++;
		/* fall through */
	case 1:
		ino = parent_ino(dentry);
		if (filldir(dirent, "..", 2, i, ino, DT_DIR) < 0)
			goto out;
		i++;
		filp->f_pos++;
		/* fall through */
	default:
		i -= 2;
		if (i >= nents) {
			ret = 1;
			goto out;
		}
		p = ents + i;
		last = &ents[nents - 1];
		while (p <= last) {
			if (proc_pident_fill_cache(filp, dirent, filldir, task, p) < 0)
				goto out;
			filp->f_pos++;
			p++;
		}
	}

	ret = 1;
out:
	put_task_struct(task);
out_no_task:
	return ret;
}

#ifdef CONFIG_SECURITY
static ssize_t proc_pid_attr_read(struct file * file, char __user * buf,
				  size_t count, loff_t *ppos)
{
	struct inode * inode = file->f_path.dentry->d_inode;
	char *p = NULL;
	ssize_t length;
	struct task_struct *task = get_proc_task(inode);

	if (!task)
		return -ESRCH;

	length = security_getprocattr(task,
				      (char*)file->f_path.dentry->d_name.name,
				      &p);
	put_task_struct(task);
	if (length > 0)
		length = simple_read_from_buffer(buf, count, ppos, p, length);
	kfree(p);
	return length;
}

static ssize_t proc_pid_attr_write(struct file * file, const char __user * buf,
				   size_t count, loff_t *ppos)
{
	struct inode * inode = file->f_path.dentry->d_inode;
	char *page;
	ssize_t length;
	struct task_struct *task = get_proc_task(inode);

	length = -ESRCH;
	if (!task)
		goto out_no_task;
	if (count > PAGE_SIZE)
		count = PAGE_SIZE;

	/* No partial writes. */
	length = -EINVAL;
	if (*ppos != 0)
		goto out;

	length = -ENOMEM;
	page = (char*)__get_free_page(GFP_TEMPORARY);
	if (!page)
		goto out;

	length = -EFAULT;
	if (copy_from_user(page, buf, count))
		goto out_free;

	length = security_setprocattr(task,
				      (char*)file->f_path.dentry->d_name.name,
				      (void*)page, count);
out_free:
	free_page((unsigned long) page);
out:
	put_task_struct(task);
out_no_task:
	return length;
}

static const struct file_operations proc_pid_attr_operations = {
	.read		= proc_pid_attr_read,
	.write		= proc_pid_attr_write,
};

static const struct pid_entry attr_dir_stuff[] = {
	REG("current",    S_IRUGO|S_IWUGO, pid_attr),
	REG("prev",       S_IRUGO,	   pid_attr),
	REG("exec",       S_IRUGO|S_IWUGO, pid_attr),
	REG("fscreate",   S_IRUGO|S_IWUGO, pid_attr),
	REG("keycreate",  S_IRUGO|S_IWUGO, pid_attr),
	REG("sockcreate", S_IRUGO|S_IWUGO, pid_attr),
};

static int proc_attr_dir_readdir(struct file * filp,
			     void * dirent, filldir_t filldir)
{
	return proc_pident_readdir(filp,dirent,filldir,
				   attr_dir_stuff,ARRAY_SIZE(attr_dir_stuff));
}

static const struct file_operations proc_attr_dir_operations = {
	.read		= generic_read_dir,
	.readdir	= proc_attr_dir_readdir,
};

static struct dentry *proc_attr_dir_lookup(struct inode *dir,
				struct dentry *dentry, struct nameidata *nd)
{
	return proc_pident_lookup(dir, dentry,
				  attr_dir_stuff, ARRAY_SIZE(attr_dir_stuff));
}

static const struct inode_operations proc_attr_dir_inode_operations = {
	.lookup		= proc_attr_dir_lookup,
	.getattr	= pid_getattr,
	.setattr	= proc_setattr,
};

#endif

#if defined(USE_ELF_CORE_DUMP) && defined(CONFIG_ELF_CORE)
static ssize_t proc_coredump_filter_read(struct file *file, char __user *buf,
					 size_t count, loff_t *ppos)
{
	struct task_struct *task = get_proc_task(file->f_dentry->d_inode);
	struct mm_struct *mm;
	char buffer[PROC_NUMBUF];
	size_t len;
	int ret;

	if (!task)
		return -ESRCH;

	ret = 0;
	mm = get_task_mm(task);
	if (mm) {
		len = snprintf(buffer, sizeof(buffer), "%08lx\n",
			       ((mm->flags & MMF_DUMP_FILTER_MASK) >>
				MMF_DUMP_FILTER_SHIFT));
		mmput(mm);
		ret = simple_read_from_buffer(buf, count, ppos, buffer, len);
	}

	put_task_struct(task);

	return ret;
}

static ssize_t proc_coredump_filter_write(struct file *file,
					  const char __user *buf,
					  size_t count,
					  loff_t *ppos)
{
	struct task_struct *task;
	struct mm_struct *mm;
	char buffer[PROC_NUMBUF], *end;
	unsigned int val;
	int ret;
	int i;
	unsigned long mask;

	ret = -EFAULT;
	memset(buffer, 0, sizeof(buffer));
	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;
	if (copy_from_user(buffer, buf, count))
		goto out_no_task;

	ret = -EINVAL;
	val = (unsigned int)simple_strtoul(buffer, &end, 0);
	if (*end == '\n')
		end++;
	if (end - buffer == 0)
		goto out_no_task;

	ret = -ESRCH;
	task = get_proc_task(file->f_dentry->d_inode);
	if (!task)
		goto out_no_task;

	ret = end - buffer;
	mm = get_task_mm(task);
	if (!mm)
		goto out_no_mm;

	for (i = 0, mask = 1; i < MMF_DUMP_FILTER_BITS; i++, mask <<= 1) {
		if (val & mask)
			set_bit(i + MMF_DUMP_FILTER_SHIFT, &mm->flags);
		else
			clear_bit(i + MMF_DUMP_FILTER_SHIFT, &mm->flags);
	}

	mmput(mm);
 out_no_mm:
	put_task_struct(task);
 out_no_task:
	return ret;
}

static const struct file_operations proc_coredump_filter_operations = {
	.read		= proc_coredump_filter_read,
	.write		= proc_coredump_filter_write,
};
#endif

/*
 * /proc/self:
 */
static int proc_self_readlink(struct dentry *dentry, char __user *buffer,
			      int buflen)
{
	struct pid_namespace *ns = dentry->d_sb->s_fs_info;
	pid_t tgid = task_tgid_nr_ns(current, ns);
	char tmp[PROC_NUMBUF];
	if (!tgid)
		return -ENOENT;
	sprintf(tmp, "%d", tgid);
	return vfs_readlink(dentry,buffer,buflen,tmp);
}

static void *proc_self_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	struct pid_namespace *ns = dentry->d_sb->s_fs_info;
	pid_t tgid = task_tgid_nr_ns(current, ns);
	char tmp[PROC_NUMBUF];
	if (!tgid)
		return ERR_PTR(-ENOENT);
	sprintf(tmp, "%d", task_tgid_nr_ns(current, ns));
	return ERR_PTR(vfs_follow_link(nd,tmp));
}

static const struct inode_operations proc_self_inode_operations = {
	.readlink	= proc_self_readlink,
	.follow_link	= proc_self_follow_link,
};

/*
 * proc base
 *
 * These are the directory entries in the root directory of /proc
 * that properly belong to the /proc filesystem, as they describe
 * describe something that is process related.
 */
static const struct pid_entry proc_base_stuff[] = {
	NOD("self", S_IFLNK|S_IRWXUGO,
		&proc_self_inode_operations, NULL, {}),
};

/*
 *	Exceptional case: normally we are not allowed to unhash a busy
 * directory. In this case, however, we can do it - no aliasing problems
 * due to the way we treat inodes.
 */
static int proc_base_revalidate(struct dentry *dentry, struct nameidata *nd)
{
	struct inode *inode = dentry->d_inode;
	struct task_struct *task = get_proc_task(inode);
	if (task) {
		put_task_struct(task);
		return 1;
	}
	d_drop(dentry);
	return 0;
}

static struct dentry_operations proc_base_dentry_operations =
{
	.d_revalidate	= proc_base_revalidate,
	.d_delete	= pid_delete_dentry,
};

static struct dentry *proc_base_instantiate(struct inode *dir,
	struct dentry *dentry, struct task_struct *task, const void *ptr)
{
	const struct pid_entry *p = ptr;
	struct inode *inode;
	struct proc_inode *ei;
	struct dentry *error = ERR_PTR(-EINVAL);

	/* Allocate the inode */
	error = ERR_PTR(-ENOMEM);
	inode = new_inode(dir->i_sb);
	if (!inode)
		goto out;

	/* Initialize the inode */
	ei = PROC_I(inode);
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;

	/*
	 * grab the reference to the task.
	 */
	ei->pid = get_task_pid(task, PIDTYPE_PID);
	if (!ei->pid)
		goto out_iput;

	inode->i_uid = 0;
	inode->i_gid = 0;
	inode->i_mode = p->mode;
	if (S_ISDIR(inode->i_mode))
		inode->i_nlink = 2;
	if (S_ISLNK(inode->i_mode))
		inode->i_size = 64;
	if (p->iop)
		inode->i_op = p->iop;
	if (p->fop)
		inode->i_fop = p->fop;
	ei->op = p->op;
	dentry->d_op = &proc_base_dentry_operations;
	d_add(dentry, inode);
	error = NULL;
out:
	return error;
out_iput:
	iput(inode);
	goto out;
}

static struct dentry *proc_base_lookup(struct inode *dir, struct dentry *dentry)
{
	struct dentry *error;
	struct task_struct *task = get_proc_task(dir);
	const struct pid_entry *p, *last;

	error = ERR_PTR(-ENOENT);

	if (!task)
		goto out_no_task;

	/* Lookup the directory entry */
	last = &proc_base_stuff[ARRAY_SIZE(proc_base_stuff) - 1];
	for (p = proc_base_stuff; p <= last; p++) {
		if (p->len != dentry->d_name.len)
			continue;
		if (!memcmp(dentry->d_name.name, p->name, p->len))
			break;
	}
	if (p > last)
		goto out;

	error = proc_base_instantiate(dir, dentry, task, p);

out:
	put_task_struct(task);
out_no_task:
	return error;
}

static int proc_base_fill_cache(struct file *filp, void *dirent,
	filldir_t filldir, struct task_struct *task, const struct pid_entry *p)
{
	return proc_fill_cache(filp, dirent, filldir, p->name, p->len,
				proc_base_instantiate, task, p);
}

#ifdef CONFIG_TASK_IO_ACCOUNTING
static int proc_pid_io_accounting(struct task_struct *task, char *buffer)
{
	return sprintf(buffer,
#ifdef CONFIG_TASK_XACCT
			"rchar: %llu\n"
			"wchar: %llu\n"
			"syscr: %llu\n"
			"syscw: %llu\n"
#endif
			"read_bytes: %llu\n"
			"write_bytes: %llu\n"
			"cancelled_write_bytes: %llu\n",
#ifdef CONFIG_TASK_XACCT
			(unsigned long long)task->rchar,
			(unsigned long long)task->wchar,
			(unsigned long long)task->syscr,
			(unsigned long long)task->syscw,
#endif
			(unsigned long long)task->ioac.read_bytes,
			(unsigned long long)task->ioac.write_bytes,
			(unsigned long long)task->ioac.cancelled_write_bytes);
}
#endif

/*
 * Thread groups
 */
static const struct file_operations proc_task_operations;
static const struct inode_operations proc_task_inode_operations;

static const struct pid_entry tgid_base_stuff[] = {
	DIR("task",       S_IRUGO|S_IXUGO, task),
	DIR("fd",         S_IRUSR|S_IXUSR, fd),
	DIR("fdinfo",     S_IRUSR|S_IXUSR, fdinfo),
#ifdef CONFIG_NET
	DIR("net",        S_IRUGO|S_IXUSR, net),
#endif
	REG("environ",    S_IRUSR, environ),
	INF("auxv",       S_IRUSR, pid_auxv),
	ONE("status",     S_IRUGO, pid_status),
	INF("limits",	  S_IRUSR, pid_limits),
#ifdef CONFIG_SCHED_DEBUG
	REG("sched",      S_IRUGO|S_IWUSR, pid_sched),
#endif
	INF("cmdline",    S_IRUGO, pid_cmdline),
	ONE("stat",       S_IRUGO, tgid_stat),
	ONE("statm",      S_IRUGO, pid_statm),
	REG("maps",       S_IRUGO, maps),
#ifdef CONFIG_NUMA
	REG("numa_maps",  S_IRUGO, numa_maps),
#endif
	REG("mem",        S_IRUSR|S_IWUSR, mem),
	LNK("cwd",        cwd),
	LNK("root",       root),
	LNK("exe",        exe),
	REG("mounts",     S_IRUGO, mounts),
	REG("mountstats", S_IRUSR, mountstats),
#ifdef CONFIG_PROC_PAGE_MONITOR
	REG("clear_refs", S_IWUSR, clear_refs),
	REG("smaps",      S_IRUGO, smaps),
	REG("pagemap",    S_IRUSR, pagemap),
#endif
#ifdef CONFIG_SECURITY
	DIR("attr",       S_IRUGO|S_IXUGO, attr_dir),
#endif
#ifdef CONFIG_KALLSYMS
	INF("wchan",      S_IRUGO, pid_wchan),
#endif
#ifdef CONFIG_SCHEDSTATS
	INF("schedstat",  S_IRUGO, pid_schedstat),
#endif
#ifdef CONFIG_LATENCYTOP
	REG("latency",  S_IRUGO, lstats),
#endif
#ifdef CONFIG_PROC_PID_CPUSET
	REG("cpuset",     S_IRUGO, cpuset),
#endif
#ifdef CONFIG_CGROUPS
	REG("cgroup",  S_IRUGO, cgroup),
#endif
	INF("oom_score",  S_IRUGO, oom_score),
	REG("oom_adj",    S_IRUGO|S_IWUSR, oom_adjust),
#ifdef CONFIG_AUDITSYSCALL
	REG("loginuid",   S_IWUSR|S_IRUGO, loginuid),
#endif
#ifdef CONFIG_FAULT_INJECTION
	REG("make-it-fail", S_IRUGO|S_IWUSR, fault_inject),
#endif
#if defined(USE_ELF_CORE_DUMP) && defined(CONFIG_ELF_CORE)
	REG("coredump_filter", S_IRUGO|S_IWUSR, coredump_filter),
#endif
#ifdef CONFIG_TASK_IO_ACCOUNTING
	INF("io",	S_IRUGO, pid_io_accounting),
#endif
};

static int proc_tgid_base_readdir(struct file * filp,
			     void * dirent, filldir_t filldir)
{
	return proc_pident_readdir(filp,dirent,filldir,
				   tgid_base_stuff,ARRAY_SIZE(tgid_base_stuff));
}

static const struct file_operations proc_tgid_base_operations = {
	.read		= generic_read_dir,
	.readdir	= proc_tgid_base_readdir,
};

static struct dentry *proc_tgid_base_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nd){
	return proc_pident_lookup(dir, dentry,
				  tgid_base_stuff, ARRAY_SIZE(tgid_base_stuff));
}

static const struct inode_operations proc_tgid_base_inode_operations = {
	.lookup		= proc_tgid_base_lookup,
	.getattr	= pid_getattr,
	.setattr	= proc_setattr,
};

static void proc_flush_task_mnt(struct vfsmount *mnt, pid_t pid, pid_t tgid)
{
	struct dentry *dentry, *leader, *dir;
	char buf[PROC_NUMBUF];
	struct qstr name;

	name.name = buf;
	name.len = snprintf(buf, sizeof(buf), "%d", pid);
	dentry = d_hash_and_lookup(mnt->mnt_root, &name);
	if (dentry) {
		if (!(current->flags & PF_EXITING))
			shrink_dcache_parent(dentry);
		d_drop(dentry);
		dput(dentry);
	}

	if (tgid == 0)
		goto out;

	name.name = buf;
	name.len = snprintf(buf, sizeof(buf), "%d", tgid);
	leader = d_hash_and_lookup(mnt->mnt_root, &name);
	if (!leader)
		goto out;

	name.name = "task";
	name.len = strlen(name.name);
	dir = d_hash_and_lookup(leader, &name);
	if (!dir)
		goto out_put_leader;

	name.name = buf;
	name.len = snprintf(buf, sizeof(buf), "%d", pid);
	dentry = d_hash_and_lookup(dir, &name);
	if (dentry) {
		shrink_dcache_parent(dentry);
		d_drop(dentry);
		dput(dentry);
	}

	dput(dir);
out_put_leader:
	dput(leader);
out:
	return;
}

/**
 * proc_flush_task -  Remove dcache entries for @task from the /proc dcache.
 * @task: task that should be flushed.
 *
 * When flushing dentries from proc, one needs to flush them from global
 * proc (proc_mnt) and from all the namespaces' procs this task was seen
 * in. This call is supposed to do all of this job.
 *
 * Looks in the dcache for
 * /proc/@pid
 * /proc/@tgid/task/@pid
 * if either directory is present flushes it and all of it'ts children
 * from the dcache.
 *
 * It is safe and reasonable to cache /proc entries for a task until
 * that task exits.  After that they just clog up the dcache with
 * useless entries, possibly causing useful dcache entries to be
 * flushed instead.  This routine is proved to flush those useless
 * dcache entries at process exit time.
 *
 * NOTE: This routine is just an optimization so it does not guarantee
 *       that no dcache entries will exist at process exit time it
 *       just makes it very unlikely that any will persist.
 */

void proc_flush_task(struct task_struct *task)
{
	int i;
	struct pid *pid, *tgid = NULL;
	struct upid *upid;

	pid = task_pid(task);
	if (thread_group_leader(task))
		tgid = task_tgid(task);

	for (i = 0; i <= pid->level; i++) {
		upid = &pid->numbers[i];
		proc_flush_task_mnt(upid->ns->proc_mnt, upid->nr,
			tgid ? tgid->numbers[i].nr : 0);
	}

	upid = &pid->numbers[pid->level];
	if (upid->nr == 1)
		pid_ns_release_proc(upid->ns);
}

static struct dentry *proc_pid_instantiate(struct inode *dir,
					   struct dentry * dentry,
					   struct task_struct *task, const void *ptr)
{
	struct dentry *error = ERR_PTR(-ENOENT);
	struct inode *inode;

	inode = proc_pid_make_inode(dir->i_sb, task);
	if (!inode)
		goto out;

	inode->i_mode = S_IFDIR|S_IRUGO|S_IXUGO;
	inode->i_op = &proc_tgid_base_inode_operations;
	inode->i_fop = &proc_tgid_base_operations;
	inode->i_flags|=S_IMMUTABLE;
	inode->i_nlink = 5;
#ifdef CONFIG_SECURITY
	inode->i_nlink += 1;
#endif

	dentry->d_op = &pid_dentry_operations;

	d_add(dentry, inode);
	/* Close the race of the process dying before we return the dentry */
	if (pid_revalidate(dentry, NULL))
		error = NULL;
out:
	return error;
}

struct dentry *proc_pid_lookup(struct inode *dir, struct dentry * dentry, struct nameidata *nd)
{
	struct dentry *result = ERR_PTR(-ENOENT);
	struct task_struct *task;
	unsigned tgid;
	struct pid_namespace *ns;

	result = proc_base_lookup(dir, dentry);
	if (!IS_ERR(result) || PTR_ERR(result) != -ENOENT)
		goto out;

	tgid = name_to_int(dentry);
	if (tgid == ~0U)
		goto out;

	ns = dentry->d_sb->s_fs_info;
	rcu_read_lock();
	task = find_task_by_pid_ns(tgid, ns);
	if (task)
		get_task_struct(task);
	rcu_read_unlock();
	if (!task)
		goto out;

	result = proc_pid_instantiate(dir, dentry, task, NULL);
	put_task_struct(task);
out:
	return result;
}

/*
 * Find the first task with tgid >= tgid
 *
 */
struct tgid_iter {
	unsigned int tgid;
	struct task_struct *task;
};
static struct tgid_iter next_tgid(struct pid_namespace *ns, struct tgid_iter iter)
{
	struct pid *pid;

	if (iter.task)
		put_task_struct(iter.task);
	rcu_read_lock();
retry:
	iter.task = NULL;
	pid = find_ge_pid(iter.tgid, ns);
	if (pid) {
		iter.tgid = pid_nr_ns(pid, ns);
		iter.task = pid_task(pid, PIDTYPE_PID);
		/* What we to know is if the pid we have find is the
		 * pid of a thread_group_leader.  Testing for task
		 * being a thread_group_leader is the obvious thing
		 * todo but there is a window when it fails, due to
		 * the pid transfer logic in de_thread.
		 *
		 * So we perform the straight forward test of seeing
		 * if the pid we have found is the pid of a thread
		 * group leader, and don't worry if the task we have
		 * found doesn't happen to be a thread group leader.
		 * As we don't care in the case of readdir.
		 */
		if (!iter.task || !has_group_leader_pid(iter.task)) {
			iter.tgid += 1;
			goto retry;
		}
		get_task_struct(iter.task);
	}
	rcu_read_unlock();
	return iter;
}

#define TGID_OFFSET (FIRST_PROCESS_ENTRY + ARRAY_SIZE(proc_base_stuff))

static int proc_pid_fill_cache(struct file *filp, void *dirent, filldir_t filldir,
	struct tgid_iter iter)
{
	char name[PROC_NUMBUF];
	int len = snprintf(name, sizeof(name), "%d", iter.tgid);
	return proc_fill_cache(filp, dirent, filldir, name, len,
				proc_pid_instantiate, iter.task, NULL);
}

/* for the /proc/ directory itself, after non-process stuff has been done */
int proc_pid_readdir(struct file * filp, void * dirent, filldir_t filldir)
{
	unsigned int nr = filp->f_pos - FIRST_PROCESS_ENTRY;
	struct task_struct *reaper = get_proc_task(filp->f_path.dentry->d_inode);
	struct tgid_iter iter;
	struct pid_namespace *ns;

	if (!reaper)
		goto out_no_task;

	for (; nr < ARRAY_SIZE(proc_base_stuff); filp->f_pos++, nr++) {
		const struct pid_entry *p = &proc_base_stuff[nr];
		if (proc_base_fill_cache(filp, dirent, filldir, reaper, p) < 0)
			goto out;
	}

	ns = filp->f_dentry->d_sb->s_fs_info;
	iter.task = NULL;
	iter.tgid = filp->f_pos - TGID_OFFSET;
	for (iter = next_tgid(ns, iter);
	     iter.task;
	     iter.tgid += 1, iter = next_tgid(ns, iter)) {
		filp->f_pos = iter.tgid + TGID_OFFSET;
		if (proc_pid_fill_cache(filp, dirent, filldir, iter) < 0) {
			put_task_struct(iter.task);
			goto out;
		}
	}
	filp->f_pos = PID_MAX_LIMIT + TGID_OFFSET;
out:
	put_task_struct(reaper);
out_no_task:
	return 0;
}

/*
 * Tasks
 */
static const struct pid_entry tid_base_stuff[] = {
	DIR("fd",        S_IRUSR|S_IXUSR, fd),
	DIR("fdinfo",    S_IRUSR|S_IXUSR, fdinfo),
	REG("environ",   S_IRUSR, environ),
	INF("auxv",      S_IRUSR, pid_auxv),
	ONE("status",    S_IRUGO, pid_status),
	INF("limits",	 S_IRUSR, pid_limits),
#ifdef CONFIG_SCHED_DEBUG
	REG("sched",     S_IRUGO|S_IWUSR, pid_sched),
#endif
	INF("cmdline",   S_IRUGO, pid_cmdline),
	ONE("stat",      S_IRUGO, tid_stat),
	ONE("statm",     S_IRUGO, pid_statm),
	REG("maps",      S_IRUGO, maps),
#ifdef CONFIG_NUMA
	REG("numa_maps", S_IRUGO, numa_maps),
#endif
	REG("mem",       S_IRUSR|S_IWUSR, mem),
	LNK("cwd",       cwd),
	LNK("root",      root),
	LNK("exe",       exe),
	REG("mounts",    S_IRUGO, mounts),
#ifdef CONFIG_PROC_PAGE_MONITOR
	REG("clear_refs", S_IWUSR, clear_refs),
	REG("smaps",     S_IRUGO, smaps),
	REG("pagemap",    S_IRUSR, pagemap),
#endif
#ifdef CONFIG_SECURITY
	DIR("attr",      S_IRUGO|S_IXUGO, attr_dir),
#endif
#ifdef CONFIG_KALLSYMS
	INF("wchan",     S_IRUGO, pid_wchan),
#endif
#ifdef CONFIG_SCHEDSTATS
	INF("schedstat", S_IRUGO, pid_schedstat),
#endif
#ifdef CONFIG_LATENCYTOP
	REG("latency",  S_IRUGO, lstats),
#endif
#ifdef CONFIG_PROC_PID_CPUSET
	REG("cpuset",    S_IRUGO, cpuset),
#endif
#ifdef CONFIG_CGROUPS
	REG("cgroup",  S_IRUGO, cgroup),
#endif
	INF("oom_score", S_IRUGO, oom_score),
	REG("oom_adj",   S_IRUGO|S_IWUSR, oom_adjust),
#ifdef CONFIG_AUDITSYSCALL
	REG("loginuid",  S_IWUSR|S_IRUGO, loginuid),
#endif
#ifdef CONFIG_FAULT_INJECTION
	REG("make-it-fail", S_IRUGO|S_IWUSR, fault_inject),
#endif
};

static int proc_tid_base_readdir(struct file * filp,
			     void * dirent, filldir_t filldir)
{
	return proc_pident_readdir(filp,dirent,filldir,
				   tid_base_stuff,ARRAY_SIZE(tid_base_stuff));
}

static struct dentry *proc_tid_base_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nd){
	return proc_pident_lookup(dir, dentry,
				  tid_base_stuff, ARRAY_SIZE(tid_base_stuff));
}

static const struct file_operations proc_tid_base_operations = {
	.read		= generic_read_dir,
	.readdir	= proc_tid_base_readdir,
};

static const struct inode_operations proc_tid_base_inode_operations = {
	.lookup		= proc_tid_base_lookup,
	.getattr	= pid_getattr,
	.setattr	= proc_setattr,
};

static struct dentry *proc_task_instantiate(struct inode *dir,
	struct dentry *dentry, struct task_struct *task, const void *ptr)
{
	struct dentry *error = ERR_PTR(-ENOENT);
	struct inode *inode;
	inode = proc_pid_make_inode(dir->i_sb, task);

	if (!inode)
		goto out;
	inode->i_mode = S_IFDIR|S_IRUGO|S_IXUGO;
	inode->i_op = &proc_tid_base_inode_operations;
	inode->i_fop = &proc_tid_base_operations;
	inode->i_flags|=S_IMMUTABLE;
	inode->i_nlink = 4;
#ifdef CONFIG_SECURITY
	inode->i_nlink += 1;
#endif

	dentry->d_op = &pid_dentry_operations;

	d_add(dentry, inode);
	/* Close the race of the process dying before we return the dentry */
	if (pid_revalidate(dentry, NULL))
		error = NULL;
out:
	return error;
}

static struct dentry *proc_task_lookup(struct inode *dir, struct dentry * dentry, struct nameidata *nd)
{
	struct dentry *result = ERR_PTR(-ENOENT);
	struct task_struct *task;
	struct task_struct *leader = get_proc_task(dir);
	unsigned tid;
	struct pid_namespace *ns;

	if (!leader)
		goto out_no_task;

	tid = name_to_int(dentry);
	if (tid == ~0U)
		goto out;

	ns = dentry->d_sb->s_fs_info;
	rcu_read_lock();
	task = find_task_by_pid_ns(tid, ns);
	if (task)
		get_task_struct(task);
	rcu_read_unlock();
	if (!task)
		goto out;
	if (!same_thread_group(leader, task))
		goto out_drop_task;

	result = proc_task_instantiate(dir, dentry, task, NULL);
out_drop_task:
	put_task_struct(task);
out:
	put_task_struct(leader);
out_no_task:
	return result;
}

/*
 * Find the first tid of a thread group to return to user space.
 *
 * Usually this is just the thread group leader, but if the users
 * buffer was too small or there was a seek into the middle of the
 * directory we have more work todo.
 *
 * In the case of a short read we start with find_task_by_pid.
 *
 * In the case of a seek we start with the leader and walk nr
 * threads past it.
 */
static struct task_struct *first_tid(struct task_struct *leader,
		int tid, int nr, struct pid_namespace *ns)
{
	struct task_struct *pos;

	rcu_read_lock();
	/* Attempt to start with the pid of a thread */
	if (tid && (nr > 0)) {
		pos = find_task_by_pid_ns(tid, ns);
		if (pos && (pos->group_leader == leader))
			goto found;
	}

	/* If nr exceeds the number of threads there is nothing todo */
	pos = NULL;
	if (nr && nr >= get_nr_threads(leader))
		goto out;

	/* If we haven't found our starting place yet start
	 * with the leader and walk nr threads forward.
	 */
	for (pos = leader; nr > 0; --nr) {
		pos = next_thread(pos);
		if (pos == leader) {
			pos = NULL;
			goto out;
		}
	}
found:
	get_task_struct(pos);
out:
	rcu_read_unlock();
	return pos;
}

/*
 * Find the next thread in the thread list.
 * Return NULL if there is an error or no next thread.
 *
 * The reference to the input task_struct is released.
 */
static struct task_struct *next_tid(struct task_struct *start)
{
	struct task_struct *pos = NULL;
	rcu_read_lock();
	if (pid_alive(start)) {
		pos = next_thread(start);
		if (thread_group_leader(pos))
			pos = NULL;
		else
			get_task_struct(pos);
	}
	rcu_read_unlock();
	put_task_struct(start);
	return pos;
}

static int proc_task_fill_cache(struct file *filp, void *dirent, filldir_t filldir,
	struct task_struct *task, int tid)
{
	char name[PROC_NUMBUF];
	int len = snprintf(name, sizeof(name), "%d", tid);
	return proc_fill_cache(filp, dirent, filldir, name, len,
				proc_task_instantiate, task, NULL);
}

/* for the /proc/TGID/task/ directories */
static int proc_task_readdir(struct file * filp, void * dirent, filldir_t filldir)
{
	struct dentry *dentry = filp->f_path.dentry;
	struct inode *inode = dentry->d_inode;
	struct task_struct *leader = NULL;
	struct task_struct *task;
	int retval = -ENOENT;
	ino_t ino;
	int tid;
	unsigned long pos = filp->f_pos;  /* avoiding "long long" filp->f_pos */
	struct pid_namespace *ns;

	task = get_proc_task(inode);
	if (!task)
		goto out_no_task;
	rcu_read_lock();
	if (pid_alive(task)) {
		leader = task->group_leader;
		get_task_struct(leader);
	}
	rcu_read_unlock();
	put_task_struct(task);
	if (!leader)
		goto out_no_task;
	retval = 0;

	switch (pos) {
	case 0:
		ino = inode->i_ino;
		if (filldir(dirent, ".", 1, pos, ino, DT_DIR) < 0)
			goto out;
		pos++;
		/* fall through */
	case 1:
		ino = parent_ino(dentry);
		if (filldir(dirent, "..", 2, pos, ino, DT_DIR) < 0)
			goto out;
		pos++;
		/* fall through */
	}

	/* f_version caches the tgid value that the last readdir call couldn't
	 * return. lseek aka telldir automagically resets f_version to 0.
	 */
	ns = filp->f_dentry->d_sb->s_fs_info;
	tid = (int)filp->f_version;
	filp->f_version = 0;
	for (task = first_tid(leader, tid, pos - 2, ns);
	     task;
	     task = next_tid(task), pos++) {
		tid = task_pid_nr_ns(task, ns);
		if (proc_task_fill_cache(filp, dirent, filldir, task, tid) < 0) {
			/* returning this tgid failed, save it as the first
			 * pid for the next readir call */
			filp->f_version = (u64)tid;
			put_task_struct(task);
			break;
		}
	}
out:
	filp->f_pos = pos;
	put_task_struct(leader);
out_no_task:
	return retval;
}

static int proc_task_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
	struct task_struct *p = get_proc_task(inode);
	generic_fillattr(inode, stat);

	if (p) {
		rcu_read_lock();
		stat->nlink += get_nr_threads(p);
		rcu_read_unlock();
		put_task_struct(p);
	}

	return 0;
}

static const struct inode_operations proc_task_inode_operations = {
	.lookup		= proc_task_lookup,
	.getattr	= proc_task_getattr,
	.setattr	= proc_setattr,
};

static const struct file_operations proc_task_operations = {
	.read		= generic_read_dir,
	.readdir	= proc_task_readdir,
};
