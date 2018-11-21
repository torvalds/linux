// SPDX-License-Identifier: GPL-2.0
#include <linux/sched/signal.h>
#include <linux/errno.h>
#include <linux/dcache.h>
#include <linux/path.h>
#include <linux/fdtable.h>
#include <linux/namei.h>
#include <linux/pid.h>
#include <linux/security.h>
#include <linux/file.h>
#include <linux/seq_file.h>
#include <linux/fs.h>

#include <linux/proc_fs.h>

#include "../mount.h"
#include "internal.h"
#include "fd.h"

static int seq_show(struct seq_file *m, void *v)
{
	struct files_struct *files = NULL;
	int f_flags = 0, ret = -ENOENT;
	struct file *file = NULL;
	struct task_struct *task;

	task = get_proc_task(m->private);
	if (!task)
		return -ENOENT;

	files = get_files_struct(task);
	put_task_struct(task);

	if (files) {
		unsigned int fd = proc_fd(m->private);

		spin_lock(&files->file_lock);
		file = fcheck_files(files, fd);
		if (file) {
			struct fdtable *fdt = files_fdtable(files);

			f_flags = file->f_flags;
			if (close_on_exec(fd, fdt))
				f_flags |= O_CLOEXEC;

			get_file(file);
			ret = 0;
		}
		spin_unlock(&files->file_lock);
		put_files_struct(files);
	}

	if (ret)
		return ret;

	seq_printf(m, "pos:\t%lli\nflags:\t0%o\nmnt_id:\t%i\n",
		   (long long)file->f_pos, f_flags,
		   real_mount(file->f_path.mnt)->mnt_id);

	show_fd_locks(m, file, files);
	if (seq_has_overflowed(m))
		goto out;

	if (file->f_op->show_fdinfo)
		file->f_op->show_fdinfo(m, file);

out:
	fput(file);
	return 0;
}

static int seq_fdinfo_open(struct inode *inode, struct file *file)
{
	return single_open(file, seq_show, inode);
}

static const struct file_operations proc_fdinfo_file_operations = {
	.open		= seq_fdinfo_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static bool tid_fd_mode(struct task_struct *task, unsigned fd, fmode_t *mode)
{
	struct files_struct *files = get_files_struct(task);
	struct file *file;

	if (!files)
		return false;

	rcu_read_lock();
	file = fcheck_files(files, fd);
	if (file)
		*mode = file->f_mode;
	rcu_read_unlock();
	put_files_struct(files);
	return !!file;
}

static void tid_fd_update_inode(struct task_struct *task, struct inode *inode,
				fmode_t f_mode)
{
	task_dump_owner(task, 0, &inode->i_uid, &inode->i_gid);

	if (S_ISLNK(inode->i_mode)) {
		unsigned i_mode = S_IFLNK;
		if (f_mode & FMODE_READ)
			i_mode |= S_IRUSR | S_IXUSR;
		if (f_mode & FMODE_WRITE)
			i_mode |= S_IWUSR | S_IXUSR;
		inode->i_mode = i_mode;
	}
	security_task_to_inode(task, inode);
}

static int tid_fd_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct task_struct *task;
	struct inode *inode;
	unsigned int fd;

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	inode = d_inode(dentry);
	task = get_proc_task(inode);
	fd = proc_fd(inode);

	if (task) {
		fmode_t f_mode;
		if (tid_fd_mode(task, fd, &f_mode)) {
			tid_fd_update_inode(task, inode, f_mode);
			put_task_struct(task);
			return 1;
		}
		put_task_struct(task);
	}
	return 0;
}

static const struct dentry_operations tid_fd_dentry_operations = {
	.d_revalidate	= tid_fd_revalidate,
	.d_delete	= pid_delete_dentry,
};

static int proc_fd_link(struct dentry *dentry, struct path *path)
{
	struct files_struct *files = NULL;
	struct task_struct *task;
	int ret = -ENOENT;

	task = get_proc_task(d_inode(dentry));
	if (task) {
		files = get_files_struct(task);
		put_task_struct(task);
	}

	if (files) {
		unsigned int fd = proc_fd(d_inode(dentry));
		struct file *fd_file;

		spin_lock(&files->file_lock);
		fd_file = fcheck_files(files, fd);
		if (fd_file) {
			*path = fd_file->f_path;
			path_get(&fd_file->f_path);
			ret = 0;
		}
		spin_unlock(&files->file_lock);
		put_files_struct(files);
	}

	return ret;
}

struct fd_data {
	fmode_t mode;
	unsigned fd;
};

static struct dentry *proc_fd_instantiate(struct dentry *dentry,
	struct task_struct *task, const void *ptr)
{
	const struct fd_data *data = ptr;
	struct proc_inode *ei;
	struct inode *inode;

	inode = proc_pid_make_inode(dentry->d_sb, task, S_IFLNK);
	if (!inode)
		return ERR_PTR(-ENOENT);

	ei = PROC_I(inode);
	ei->fd = data->fd;

	inode->i_op = &proc_pid_link_inode_operations;
	inode->i_size = 64;

	ei->op.proc_get_link = proc_fd_link;
	tid_fd_update_inode(task, inode, data->mode);

	d_set_d_op(dentry, &tid_fd_dentry_operations);
	return d_splice_alias(inode, dentry);
}

static struct dentry *proc_lookupfd_common(struct inode *dir,
					   struct dentry *dentry,
					   instantiate_t instantiate)
{
	struct task_struct *task = get_proc_task(dir);
	struct fd_data data = {.fd = name_to_int(&dentry->d_name)};
	struct dentry *result = ERR_PTR(-ENOENT);

	if (!task)
		goto out_no_task;
	if (data.fd == ~0U)
		goto out;
	if (!tid_fd_mode(task, data.fd, &data.mode))
		goto out;

	result = instantiate(dentry, task, &data);
out:
	put_task_struct(task);
out_no_task:
	return result;
}

static int proc_readfd_common(struct file *file, struct dir_context *ctx,
			      instantiate_t instantiate)
{
	struct task_struct *p = get_proc_task(file_inode(file));
	struct files_struct *files;
	unsigned int fd;

	if (!p)
		return -ENOENT;

	if (!dir_emit_dots(file, ctx))
		goto out;
	files = get_files_struct(p);
	if (!files)
		goto out;

	rcu_read_lock();
	for (fd = ctx->pos - 2;
	     fd < files_fdtable(files)->max_fds;
	     fd++, ctx->pos++) {
		struct file *f;
		struct fd_data data;
		char name[10 + 1];
		unsigned int len;

		f = fcheck_files(files, fd);
		if (!f)
			continue;
		data.mode = f->f_mode;
		rcu_read_unlock();
		data.fd = fd;

		len = snprintf(name, sizeof(name), "%u", fd);
		if (!proc_fill_cache(file, ctx,
				     name, len, instantiate, p,
				     &data))
			goto out_fd_loop;
		cond_resched();
		rcu_read_lock();
	}
	rcu_read_unlock();
out_fd_loop:
	put_files_struct(files);
out:
	put_task_struct(p);
	return 0;
}

static int proc_readfd(struct file *file, struct dir_context *ctx)
{
	return proc_readfd_common(file, ctx, proc_fd_instantiate);
}

const struct file_operations proc_fd_operations = {
	.read		= generic_read_dir,
	.iterate_shared	= proc_readfd,
	.llseek		= generic_file_llseek,
};

static struct dentry *proc_lookupfd(struct inode *dir, struct dentry *dentry,
				    unsigned int flags)
{
	return proc_lookupfd_common(dir, dentry, proc_fd_instantiate);
}

/*
 * /proc/pid/fd needs a special permission handler so that a process can still
 * access /proc/self/fd after it has executed a setuid().
 */
int proc_fd_permission(struct inode *inode, int mask)
{
	struct task_struct *p;
	int rv;

	rv = generic_permission(inode, mask);
	if (rv == 0)
		return rv;

	rcu_read_lock();
	p = pid_task(proc_pid(inode), PIDTYPE_PID);
	if (p && same_thread_group(p, current))
		rv = 0;
	rcu_read_unlock();

	return rv;
}

const struct inode_operations proc_fd_inode_operations = {
	.lookup		= proc_lookupfd,
	.permission	= proc_fd_permission,
	.setattr	= proc_setattr,
};

static struct dentry *proc_fdinfo_instantiate(struct dentry *dentry,
	struct task_struct *task, const void *ptr)
{
	const struct fd_data *data = ptr;
	struct proc_inode *ei;
	struct inode *inode;

	inode = proc_pid_make_inode(dentry->d_sb, task, S_IFREG | S_IRUSR);
	if (!inode)
		return ERR_PTR(-ENOENT);

	ei = PROC_I(inode);
	ei->fd = data->fd;

	inode->i_fop = &proc_fdinfo_file_operations;
	tid_fd_update_inode(task, inode, 0);

	d_set_d_op(dentry, &tid_fd_dentry_operations);
	return d_splice_alias(inode, dentry);
}

static struct dentry *
proc_lookupfdinfo(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
	return proc_lookupfd_common(dir, dentry, proc_fdinfo_instantiate);
}

static int proc_readfdinfo(struct file *file, struct dir_context *ctx)
{
	return proc_readfd_common(file, ctx,
				  proc_fdinfo_instantiate);
}

const struct inode_operations proc_fdinfo_inode_operations = {
	.lookup		= proc_lookupfdinfo,
	.setattr	= proc_setattr,
};

const struct file_operations proc_fdinfo_operations = {
	.read		= generic_read_dir,
	.iterate_shared	= proc_readfdinfo,
	.llseek		= generic_file_llseek,
};
