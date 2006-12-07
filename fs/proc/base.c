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
#include <linux/namespace.h>
#include <linux/mm.h>
#include <linux/smp_lock.h>
#include <linux/rcupdate.h>
#include <linux/kallsyms.h>
#include <linux/mount.h>
#include <linux/security.h>
#include <linux/ptrace.h>
#include <linux/seccomp.h>
#include <linux/cpuset.h>
#include <linux/audit.h>
#include <linux/poll.h>
#include <linux/nsproxy.h>
#include <linux/oom.h>
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


/* Worst case buffer size needed for holding an integer. */
#define PROC_NUMBUF 13

struct pid_entry {
	int len;
	char *name;
	mode_t mode;
	struct inode_operations *iop;
	struct file_operations *fop;
	union proc_op op;
};

#define NOD(NAME, MODE, IOP, FOP, OP) {			\
	.len  = sizeof(NAME) - 1,			\
	.name = (NAME),					\
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

static int proc_cwd_link(struct inode *inode, struct dentry **dentry, struct vfsmount **mnt)
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
		*mnt = mntget(fs->pwdmnt);
		*dentry = dget(fs->pwd);
		read_unlock(&fs->lock);
		result = 0;
		put_fs_struct(fs);
	}
	return result;
}

static int proc_root_link(struct inode *inode, struct dentry **dentry, struct vfsmount **mnt)
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
		*mnt = mntget(fs->rootmnt);
		*dentry = dget(fs->root);
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
	 (task->state == TASK_STOPPED || task->state == TASK_TRACED) && \
	 security_ptrace(current,task) == 0))

static int proc_pid_environ(struct task_struct *task, char * buffer)
{
	int res = 0;
	struct mm_struct *mm = get_task_mm(task);
	if (mm) {
		unsigned int len = mm->env_end - mm->env_start;
		if (len > PAGE_SIZE)
			len = PAGE_SIZE;
		res = access_process_vm(task, mm->env_start, buffer, len, 0);
		if (!ptrace_may_attach(task))
			res = -ESRCH;
		mmput(mm);
	}
	return res;
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
	char *modname;
	const char *sym_name;
	unsigned long wchan, size, offset;
	char namebuf[KSYM_NAME_LEN+1];

	wchan = get_wchan(task);

	sym_name = kallsyms_lookup(wchan, &size, &offset, &modname, namebuf);
	if (sym_name)
		return sprintf(buffer, "%s", sym_name);
	return sprintf(buffer, "%lu", wchan);
}
#endif /* CONFIG_KALLSYMS */

#ifdef CONFIG_SCHEDSTATS
/*
 * Provides /proc/PID/schedstat
 */
static int proc_pid_schedstat(struct task_struct *task, char *buffer)
{
	return sprintf(buffer, "%lu %lu %lu\n",
			task->sched_info.cpu_time,
			task->sched_info.run_delay,
			task->sched_info.pcnt);
}
#endif

/* The badness from the OOM killer */
unsigned long badness(struct task_struct *p, unsigned long uptime);
static int proc_oom_score(struct task_struct *task, char *buffer)
{
	unsigned long points;
	struct timespec uptime;

	do_posix_clock_monotonic_gettime(&uptime);
	points = badness(task, uptime.tv_sec);
	return sprintf(buffer, "%lu\n", points);
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
	if (!error) {
		error = security_inode_setattr(dentry, attr);
		if (!error)
			error = inode_setattr(inode, attr);
	}
	return error;
}

static struct inode_operations proc_def_inode_operations = {
	.setattr	= proc_setattr,
};

extern struct seq_operations mounts_op;
struct proc_mounts {
	struct seq_file m;
	int event;
};

static int mounts_open(struct inode *inode, struct file *file)
{
	struct task_struct *task = get_proc_task(inode);
	struct namespace *namespace = NULL;
	struct proc_mounts *p;
	int ret = -EINVAL;

	if (task) {
		task_lock(task);
		namespace = task->nsproxy->namespace;
		if (namespace)
			get_namespace(namespace);
		task_unlock(task);
		put_task_struct(task);
	}

	if (namespace) {
		ret = -ENOMEM;
		p = kmalloc(sizeof(struct proc_mounts), GFP_KERNEL);
		if (p) {
			file->private_data = &p->m;
			ret = seq_open(file, &mounts_op);
			if (!ret) {
				p->m.private = namespace;
				p->event = namespace->event;
				return 0;
			}
			kfree(p);
		}
		put_namespace(namespace);
	}
	return ret;
}

static int mounts_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = file->private_data;
	struct namespace *namespace = m->private;
	put_namespace(namespace);
	return seq_release(inode, file);
}

static unsigned mounts_poll(struct file *file, poll_table *wait)
{
	struct proc_mounts *p = file->private_data;
	struct namespace *ns = p->m.private;
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

static struct file_operations proc_mounts_operations = {
	.open		= mounts_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= mounts_release,
	.poll		= mounts_poll,
};

extern struct seq_operations mountstats_op;
static int mountstats_open(struct inode *inode, struct file *file)
{
	int ret = seq_open(file, &mountstats_op);

	if (!ret) {
		struct seq_file *m = file->private_data;
		struct namespace *namespace = NULL;
		struct task_struct *task = get_proc_task(inode);

		if (task) {
			task_lock(task);
			if (task->nsproxy)
				namespace = task->nsproxy->namespace;
			if (namespace)
				get_namespace(namespace);
			task_unlock(task);
			put_task_struct(task);
		}

		if (namespace)
			m->private = namespace;
		else {
			seq_release(inode, file);
			ret = -EINVAL;
		}
	}
	return ret;
}

static struct file_operations proc_mountstats_operations = {
	.open		= mountstats_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= mounts_release,
};

#define PROC_BLOCK_SIZE	(3*1024)		/* 4K page size but our output routines use some slack for overruns */

static ssize_t proc_info_read(struct file * file, char __user * buf,
			  size_t count, loff_t *ppos)
{
	struct inode * inode = file->f_dentry->d_inode;
	unsigned long page;
	ssize_t length;
	struct task_struct *task = get_proc_task(inode);

	length = -ESRCH;
	if (!task)
		goto out_no_task;

	if (count > PROC_BLOCK_SIZE)
		count = PROC_BLOCK_SIZE;

	length = -ENOMEM;
	if (!(page = __get_free_page(GFP_KERNEL)))
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

static struct file_operations proc_info_file_operations = {
	.read		= proc_info_read,
};

static int mem_open(struct inode* inode, struct file* file)
{
	file->private_data = (void*)((long)current->self_exec_id);
	return 0;
}

static ssize_t mem_read(struct file * file, char __user * buf,
			size_t count, loff_t *ppos)
{
	struct task_struct *task = get_proc_task(file->f_dentry->d_inode);
	char *page;
	unsigned long src = *ppos;
	int ret = -ESRCH;
	struct mm_struct *mm;

	if (!task)
		goto out_no_task;

	if (!MAY_PTRACE(task) || !ptrace_may_attach(task))
		goto out;

	ret = -ENOMEM;
	page = (char *)__get_free_page(GFP_USER);
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
static ssize_t mem_write(struct file * file, const char * buf,
			 size_t count, loff_t *ppos)
{
	int copied;
	char *page;
	struct task_struct *task = get_proc_task(file->f_dentry->d_inode);
	unsigned long dst = *ppos;

	copied = -ESRCH;
	if (!task)
		goto out_no_task;

	if (!MAY_PTRACE(task) || !ptrace_may_attach(task))
		goto out;

	copied = -ENOMEM;
	page = (char *)__get_free_page(GFP_USER);
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

static loff_t mem_lseek(struct file * file, loff_t offset, int orig)
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

static struct file_operations proc_mem_operations = {
	.llseek		= mem_lseek,
	.read		= mem_read,
	.write		= mem_write,
	.open		= mem_open,
};

static ssize_t oom_adjust_read(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	struct task_struct *task = get_proc_task(file->f_dentry->d_inode);
	char buffer[PROC_NUMBUF];
	size_t len;
	int oom_adjust;
	loff_t __ppos = *ppos;

	if (!task)
		return -ESRCH;
	oom_adjust = task->oomkilladj;
	put_task_struct(task);

	len = snprintf(buffer, sizeof(buffer), "%i\n", oom_adjust);
	if (__ppos >= len)
		return 0;
	if (count > len-__ppos)
		count = len-__ppos;
	if (copy_to_user(buf, buffer + __ppos, count))
		return -EFAULT;
	*ppos = __ppos + count;
	return count;
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
	task = get_proc_task(file->f_dentry->d_inode);
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

static struct file_operations proc_oom_adjust_operations = {
	.read		= oom_adjust_read,
	.write		= oom_adjust_write,
};

#ifdef CONFIG_AUDITSYSCALL
#define TMPBUFLEN 21
static ssize_t proc_loginuid_read(struct file * file, char __user * buf,
				  size_t count, loff_t *ppos)
{
	struct inode * inode = file->f_dentry->d_inode;
	struct task_struct *task = get_proc_task(inode);
	ssize_t length;
	char tmpbuf[TMPBUFLEN];

	if (!task)
		return -ESRCH;
	length = scnprintf(tmpbuf, TMPBUFLEN, "%u",
				audit_get_loginuid(task->audit_context));
	put_task_struct(task);
	return simple_read_from_buffer(buf, count, ppos, tmpbuf, length);
}

static ssize_t proc_loginuid_write(struct file * file, const char __user * buf,
				   size_t count, loff_t *ppos)
{
	struct inode * inode = file->f_dentry->d_inode;
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
	page = (char*)__get_free_page(GFP_USER);
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

static struct file_operations proc_loginuid_operations = {
	.read		= proc_loginuid_read,
	.write		= proc_loginuid_write,
};
#endif

#ifdef CONFIG_SECCOMP
static ssize_t seccomp_read(struct file *file, char __user *buf,
			    size_t count, loff_t *ppos)
{
	struct task_struct *tsk = get_proc_task(file->f_dentry->d_inode);
	char __buf[20];
	loff_t __ppos = *ppos;
	size_t len;

	if (!tsk)
		return -ESRCH;
	/* no need to print the trailing zero, so use only len */
	len = sprintf(__buf, "%u\n", tsk->seccomp.mode);
	put_task_struct(tsk);
	if (__ppos >= len)
		return 0;
	if (count > len - __ppos)
		count = len - __ppos;
	if (copy_to_user(buf, __buf + __ppos, count))
		return -EFAULT;
	*ppos = __ppos + count;
	return count;
}

static ssize_t seccomp_write(struct file *file, const char __user *buf,
			     size_t count, loff_t *ppos)
{
	struct task_struct *tsk = get_proc_task(file->f_dentry->d_inode);
	char __buf[20], *end;
	unsigned int seccomp_mode;
	ssize_t result;

	result = -ESRCH;
	if (!tsk)
		goto out_no_task;

	/* can set it only once to be even more secure */
	result = -EPERM;
	if (unlikely(tsk->seccomp.mode))
		goto out;

	result = -EFAULT;
	memset(__buf, 0, sizeof(__buf));
	count = min(count, sizeof(__buf) - 1);
	if (copy_from_user(__buf, buf, count))
		goto out;

	seccomp_mode = simple_strtoul(__buf, &end, 0);
	if (*end == '\n')
		end++;
	result = -EINVAL;
	if (seccomp_mode && seccomp_mode <= NR_SECCOMP_MODES) {
		tsk->seccomp.mode = seccomp_mode;
		set_tsk_thread_flag(tsk, TIF_SECCOMP);
	} else
		goto out;
	result = -EIO;
	if (unlikely(!(end - __buf)))
		goto out;
	result = end - __buf;
out:
	put_task_struct(tsk);
out_no_task:
	return result;
}

static struct file_operations proc_seccomp_operations = {
	.read		= seccomp_read,
	.write		= seccomp_write,
};
#endif /* CONFIG_SECCOMP */

static void *proc_pid_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	struct inode *inode = dentry->d_inode;
	int error = -EACCES;

	/* We don't need a base pointer in the /proc filesystem */
	path_release(nd);

	/* Are we allowed to snoop on the tasks file descriptors? */
	if (!proc_fd_access_allowed(inode))
		goto out;

	error = PROC_I(inode)->op.proc_get_link(inode, &nd->dentry, &nd->mnt);
	nd->last_type = LAST_BIND;
out:
	return ERR_PTR(error);
}

static int do_proc_readlink(struct dentry *dentry, struct vfsmount *mnt,
			    char __user *buffer, int buflen)
{
	struct inode * inode;
	char *tmp = (char*)__get_free_page(GFP_KERNEL), *path;
	int len;

	if (!tmp)
		return -ENOMEM;
		
	inode = dentry->d_inode;
	path = d_path(dentry, mnt, tmp, PAGE_SIZE);
	len = PTR_ERR(path);
	if (IS_ERR(path))
		goto out;
	len = tmp + PAGE_SIZE - 1 - path;

	if (len > buflen)
		len = buflen;
	if (copy_to_user(buffer, path, len))
		len = -EFAULT;
 out:
	free_page((unsigned long)tmp);
	return len;
}

static int proc_pid_readlink(struct dentry * dentry, char __user * buffer, int buflen)
{
	int error = -EACCES;
	struct inode *inode = dentry->d_inode;
	struct dentry *de;
	struct vfsmount *mnt = NULL;

	/* Are we allowed to snoop on the tasks file descriptors? */
	if (!proc_fd_access_allowed(inode))
		goto out;

	error = PROC_I(inode)->op.proc_get_link(inode, &de, &mnt);
	if (error)
		goto out;

	error = do_proc_readlink(de, mnt, buffer, buflen);
	dput(de);
	mntput(mnt);
out:
	return error;
}

static struct inode_operations proc_pid_link_inode_operations = {
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
		dumpable = mm->dumpable;
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

typedef struct dentry *instantiate_t(struct inode *, struct dentry *, struct task_struct *, void *);

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
	instantiate_t instantiate, struct task_struct *task, void *ptr)
{
	struct dentry *child, *dir = filp->f_dentry;
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

static int proc_fd_link(struct inode *inode, struct dentry **dentry, struct vfsmount **mnt)
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
			*mnt = mntget(file->f_vfsmnt);
			*dentry = dget(file->f_dentry);
			spin_unlock(&files->file_lock);
			put_files_struct(files);
			return 0;
		}
		spin_unlock(&files->file_lock);
		put_files_struct(files);
	}
	return -ENOENT;
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
	struct dentry *dentry, struct task_struct *task, void *ptr)
{
	unsigned fd = *(unsigned *)ptr;
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

static struct dentry *proc_lookupfd(struct inode * dir, struct dentry * dentry, struct nameidata *nd)
{
	struct task_struct *task = get_proc_task(dir);
	unsigned fd = name_to_int(dentry);
	struct dentry *result = ERR_PTR(-ENOENT);

	if (!task)
		goto out_no_task;
	if (fd == ~0U)
		goto out;

	result = proc_fd_instantiate(dir, dentry, task, &fd);
out:
	put_task_struct(task);
out_no_task:
	return result;
}

static int proc_fd_fill_cache(struct file *filp, void *dirent, filldir_t filldir,
	struct task_struct *task, int fd)
{
	char name[PROC_NUMBUF];
	int len = snprintf(name, sizeof(name), "%d", fd);
	return proc_fill_cache(filp, dirent, filldir, name, len,
				proc_fd_instantiate, task, &fd);
}

static int proc_readfd(struct file * filp, void * dirent, filldir_t filldir)
{
	struct dentry *dentry = filp->f_dentry;
	struct inode *inode = dentry->d_inode;
	struct task_struct *p = get_proc_task(inode);
	unsigned int fd, tid, ino;
	int retval;
	struct files_struct * files;
	struct fdtable *fdt;

	retval = -ENOENT;
	if (!p)
		goto out_no_task;
	retval = 0;
	tid = p->pid;

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

				if (!fcheck_files(files, fd))
					continue;
				rcu_read_unlock();

				if (proc_fd_fill_cache(filp, dirent, filldir, p, fd) < 0) {
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

static struct file_operations proc_fd_operations = {
	.read		= generic_read_dir,
	.readdir	= proc_readfd,
};

/*
 * proc directories can do almost nothing..
 */
static struct inode_operations proc_fd_inode_operations = {
	.lookup		= proc_lookupfd,
	.setattr	= proc_setattr,
};

static struct dentry *proc_pident_instantiate(struct inode *dir,
	struct dentry *dentry, struct task_struct *task, void *ptr)
{
	struct pid_entry *p = ptr;
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
					 struct pid_entry *ents,
					 unsigned int nents)
{
	struct inode *inode;
	struct dentry *error;
	struct task_struct *task = get_proc_task(dir);
	struct pid_entry *p, *last;

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

static int proc_pident_fill_cache(struct file *filp, void *dirent, filldir_t filldir,
	struct task_struct *task, struct pid_entry *p)
{
	return proc_fill_cache(filp, dirent, filldir, p->name, p->len,
				proc_pident_instantiate, task, p);
}

static int proc_pident_readdir(struct file *filp,
		void *dirent, filldir_t filldir,
		struct pid_entry *ents, unsigned int nents)
{
	int i;
	int pid;
	struct dentry *dentry = filp->f_dentry;
	struct inode *inode = dentry->d_inode;
	struct task_struct *task = get_proc_task(inode);
	struct pid_entry *p, *last;
	ino_t ino;
	int ret;

	ret = -ENOENT;
	if (!task)
		goto out_no_task;

	ret = 0;
	pid = task->pid;
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
	struct inode * inode = file->f_dentry->d_inode;
	unsigned long page;
	ssize_t length;
	struct task_struct *task = get_proc_task(inode);

	length = -ESRCH;
	if (!task)
		goto out_no_task;

	if (count > PAGE_SIZE)
		count = PAGE_SIZE;
	length = -ENOMEM;
	if (!(page = __get_free_page(GFP_KERNEL)))
		goto out;

	length = security_getprocattr(task,
				      (char*)file->f_dentry->d_name.name,
				      (void*)page, count);
	if (length >= 0)
		length = simple_read_from_buffer(buf, count, ppos, (char *)page, length);
	free_page(page);
out:
	put_task_struct(task);
out_no_task:
	return length;
}

static ssize_t proc_pid_attr_write(struct file * file, const char __user * buf,
				   size_t count, loff_t *ppos)
{
	struct inode * inode = file->f_dentry->d_inode;
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
	page = (char*)__get_free_page(GFP_USER);
	if (!page)
		goto out;

	length = -EFAULT;
	if (copy_from_user(page, buf, count))
		goto out_free;

	length = security_setprocattr(task,
				      (char*)file->f_dentry->d_name.name,
				      (void*)page, count);
out_free:
	free_page((unsigned long) page);
out:
	put_task_struct(task);
out_no_task:
	return length;
}

static struct file_operations proc_pid_attr_operations = {
	.read		= proc_pid_attr_read,
	.write		= proc_pid_attr_write,
};

static struct pid_entry attr_dir_stuff[] = {
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

static struct file_operations proc_attr_dir_operations = {
	.read		= generic_read_dir,
	.readdir	= proc_attr_dir_readdir,
};

static struct dentry *proc_attr_dir_lookup(struct inode *dir,
				struct dentry *dentry, struct nameidata *nd)
{
	return proc_pident_lookup(dir, dentry,
				  attr_dir_stuff, ARRAY_SIZE(attr_dir_stuff));
}

static struct inode_operations proc_attr_dir_inode_operations = {
	.lookup		= proc_attr_dir_lookup,
	.getattr	= pid_getattr,
	.setattr	= proc_setattr,
};

#endif

/*
 * /proc/self:
 */
static int proc_self_readlink(struct dentry *dentry, char __user *buffer,
			      int buflen)
{
	char tmp[PROC_NUMBUF];
	sprintf(tmp, "%d", current->tgid);
	return vfs_readlink(dentry,buffer,buflen,tmp);
}

static void *proc_self_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	char tmp[PROC_NUMBUF];
	sprintf(tmp, "%d", current->tgid);
	return ERR_PTR(vfs_follow_link(nd,tmp));
}

static struct inode_operations proc_self_inode_operations = {
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
static struct pid_entry proc_base_stuff[] = {
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
	struct dentry *dentry, struct task_struct *task, void *ptr)
{
	struct pid_entry *p = ptr;
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
	struct pid_entry *p, *last;

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

static int proc_base_fill_cache(struct file *filp, void *dirent, filldir_t filldir,
	struct task_struct *task, struct pid_entry *p)
{
	return proc_fill_cache(filp, dirent, filldir, p->name, p->len,
				proc_base_instantiate, task, p);
}

/*
 * Thread groups
 */
static struct file_operations proc_task_operations;
static struct inode_operations proc_task_inode_operations;

static struct pid_entry tgid_base_stuff[] = {
	DIR("task",       S_IRUGO|S_IXUGO, task),
	DIR("fd",         S_IRUSR|S_IXUSR, fd),
	INF("environ",    S_IRUSR, pid_environ),
	INF("auxv",       S_IRUSR, pid_auxv),
	INF("status",     S_IRUGO, pid_status),
	INF("cmdline",    S_IRUGO, pid_cmdline),
	INF("stat",       S_IRUGO, tgid_stat),
	INF("statm",      S_IRUGO, pid_statm),
	REG("maps",       S_IRUGO, maps),
#ifdef CONFIG_NUMA
	REG("numa_maps",  S_IRUGO, numa_maps),
#endif
	REG("mem",        S_IRUSR|S_IWUSR, mem),
#ifdef CONFIG_SECCOMP
	REG("seccomp",    S_IRUSR|S_IWUSR, seccomp),
#endif
	LNK("cwd",        cwd),
	LNK("root",       root),
	LNK("exe",        exe),
	REG("mounts",     S_IRUGO, mounts),
	REG("mountstats", S_IRUSR, mountstats),
#ifdef CONFIG_MMU
	REG("smaps",      S_IRUGO, smaps),
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
#ifdef CONFIG_CPUSETS
	REG("cpuset",     S_IRUGO, cpuset),
#endif
	INF("oom_score",  S_IRUGO, oom_score),
	REG("oom_adj",    S_IRUGO|S_IWUSR, oom_adjust),
#ifdef CONFIG_AUDITSYSCALL
	REG("loginuid",   S_IWUSR|S_IRUGO, loginuid),
#endif
};

static int proc_tgid_base_readdir(struct file * filp,
			     void * dirent, filldir_t filldir)
{
	return proc_pident_readdir(filp,dirent,filldir,
				   tgid_base_stuff,ARRAY_SIZE(tgid_base_stuff));
}

static struct file_operations proc_tgid_base_operations = {
	.read		= generic_read_dir,
	.readdir	= proc_tgid_base_readdir,
};

static struct dentry *proc_tgid_base_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nd){
	return proc_pident_lookup(dir, dentry,
				  tgid_base_stuff, ARRAY_SIZE(tgid_base_stuff));
}

static struct inode_operations proc_tgid_base_inode_operations = {
	.lookup		= proc_tgid_base_lookup,
	.getattr	= pid_getattr,
	.setattr	= proc_setattr,
};

/**
 * proc_flush_task -  Remove dcache entries for @task from the /proc dcache.
 *
 * @task: task that should be flushed.
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
	struct dentry *dentry, *leader, *dir;
	char buf[PROC_NUMBUF];
	struct qstr name;

	name.name = buf;
	name.len = snprintf(buf, sizeof(buf), "%d", task->pid);
	dentry = d_hash_and_lookup(proc_mnt->mnt_root, &name);
	if (dentry) {
		shrink_dcache_parent(dentry);
		d_drop(dentry);
		dput(dentry);
	}

	if (thread_group_leader(task))
		goto out;

	name.name = buf;
	name.len = snprintf(buf, sizeof(buf), "%d", task->tgid);
	leader = d_hash_and_lookup(proc_mnt->mnt_root, &name);
	if (!leader)
		goto out;

	name.name = "task";
	name.len = strlen(name.name);
	dir = d_hash_and_lookup(leader, &name);
	if (!dir)
		goto out_put_leader;

	name.name = buf;
	name.len = snprintf(buf, sizeof(buf), "%d", task->pid);
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

struct dentry *proc_pid_instantiate(struct inode *dir,
	struct dentry * dentry, struct task_struct *task, void *ptr)
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

struct dentry *proc_pid_lookup(struct inode *dir, struct dentry * dentry, struct nameidata *nd)
{
	struct dentry *result = ERR_PTR(-ENOENT);
	struct task_struct *task;
	unsigned tgid;

	result = proc_base_lookup(dir, dentry);
	if (!IS_ERR(result) || PTR_ERR(result) != -ENOENT)
		goto out;

	tgid = name_to_int(dentry);
	if (tgid == ~0U)
		goto out;

	rcu_read_lock();
	task = find_task_by_pid(tgid);
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
static struct task_struct *next_tgid(unsigned int tgid)
{
	struct task_struct *task;
	struct pid *pid;

	rcu_read_lock();
retry:
	task = NULL;
	pid = find_ge_pid(tgid);
	if (pid) {
		tgid = pid->nr + 1;
		task = pid_task(pid, PIDTYPE_PID);
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
		if (!task || !has_group_leader_pid(task))
			goto retry;
		get_task_struct(task);
	}
	rcu_read_unlock();
	return task;
}

#define TGID_OFFSET (FIRST_PROCESS_ENTRY + ARRAY_SIZE(proc_base_stuff))

static int proc_pid_fill_cache(struct file *filp, void *dirent, filldir_t filldir,
	struct task_struct *task, int tgid)
{
	char name[PROC_NUMBUF];
	int len = snprintf(name, sizeof(name), "%d", tgid);
	return proc_fill_cache(filp, dirent, filldir, name, len,
				proc_pid_instantiate, task, NULL);
}

/* for the /proc/ directory itself, after non-process stuff has been done */
int proc_pid_readdir(struct file * filp, void * dirent, filldir_t filldir)
{
	unsigned int nr = filp->f_pos - FIRST_PROCESS_ENTRY;
	struct task_struct *reaper = get_proc_task(filp->f_dentry->d_inode);
	struct task_struct *task;
	int tgid;

	if (!reaper)
		goto out_no_task;

	for (; nr < ARRAY_SIZE(proc_base_stuff); filp->f_pos++, nr++) {
		struct pid_entry *p = &proc_base_stuff[nr];
		if (proc_base_fill_cache(filp, dirent, filldir, reaper, p) < 0)
			goto out;
	}

	tgid = filp->f_pos - TGID_OFFSET;
	for (task = next_tgid(tgid);
	     task;
	     put_task_struct(task), task = next_tgid(tgid + 1)) {
		tgid = task->pid;
		filp->f_pos = tgid + TGID_OFFSET;
		if (proc_pid_fill_cache(filp, dirent, filldir, task, tgid) < 0) {
			put_task_struct(task);
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
static struct pid_entry tid_base_stuff[] = {
	DIR("fd",        S_IRUSR|S_IXUSR, fd),
	INF("environ",   S_IRUSR, pid_environ),
	INF("auxv",      S_IRUSR, pid_auxv),
	INF("status",    S_IRUGO, pid_status),
	INF("cmdline",   S_IRUGO, pid_cmdline),
	INF("stat",      S_IRUGO, tid_stat),
	INF("statm",     S_IRUGO, pid_statm),
	REG("maps",      S_IRUGO, maps),
#ifdef CONFIG_NUMA
	REG("numa_maps", S_IRUGO, numa_maps),
#endif
	REG("mem",       S_IRUSR|S_IWUSR, mem),
#ifdef CONFIG_SECCOMP
	REG("seccomp",   S_IRUSR|S_IWUSR, seccomp),
#endif
	LNK("cwd",       cwd),
	LNK("root",      root),
	LNK("exe",       exe),
	REG("mounts",    S_IRUGO, mounts),
#ifdef CONFIG_MMU
	REG("smaps",     S_IRUGO, smaps),
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
#ifdef CONFIG_CPUSETS
	REG("cpuset",    S_IRUGO, cpuset),
#endif
	INF("oom_score", S_IRUGO, oom_score),
	REG("oom_adj",   S_IRUGO|S_IWUSR, oom_adjust),
#ifdef CONFIG_AUDITSYSCALL
	REG("loginuid",  S_IWUSR|S_IRUGO, loginuid),
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

static struct file_operations proc_tid_base_operations = {
	.read		= generic_read_dir,
	.readdir	= proc_tid_base_readdir,
};

static struct inode_operations proc_tid_base_inode_operations = {
	.lookup		= proc_tid_base_lookup,
	.getattr	= pid_getattr,
	.setattr	= proc_setattr,
};

static struct dentry *proc_task_instantiate(struct inode *dir,
	struct dentry *dentry, struct task_struct *task, void *ptr)
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
	inode->i_nlink = 3;
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

	if (!leader)
		goto out_no_task;

	tid = name_to_int(dentry);
	if (tid == ~0U)
		goto out;

	rcu_read_lock();
	task = find_task_by_pid(tid);
	if (task)
		get_task_struct(task);
	rcu_read_unlock();
	if (!task)
		goto out;
	if (leader->tgid != task->tgid)
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
					int tid, int nr)
{
	struct task_struct *pos;

	rcu_read_lock();
	/* Attempt to start with the pid of a thread */
	if (tid && (nr > 0)) {
		pos = find_task_by_pid(tid);
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
	struct dentry *dentry = filp->f_dentry;
	struct inode *inode = dentry->d_inode;
	struct task_struct *leader = get_proc_task(inode);
	struct task_struct *task;
	int retval = -ENOENT;
	ino_t ino;
	int tid;
	unsigned long pos = filp->f_pos;  /* avoiding "long long" filp->f_pos */

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
	tid = filp->f_version;
	filp->f_version = 0;
	for (task = first_tid(leader, tid, pos - 2);
	     task;
	     task = next_tid(task), pos++) {
		tid = task->pid;
		if (proc_task_fill_cache(filp, dirent, filldir, task, tid) < 0) {
			/* returning this tgid failed, save it as the first
			 * pid for the next readir call */
			filp->f_version = tid;
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

static struct inode_operations proc_task_inode_operations = {
	.lookup		= proc_task_lookup,
	.getattr	= proc_task_getattr,
	.setattr	= proc_setattr,
};

static struct file_operations proc_task_operations = {
	.read		= generic_read_dir,
	.readdir	= proc_task_readdir,
};
