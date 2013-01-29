#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/dcache.h>
#include <linux/path.h>
#include <linux/fdtable.h>
#include <linux/namei.h>
#include <linux/pid.h>
#include <linux/security.h>
#include <linux/file.h>
#include <linux/seq_file.h>

#include <linux/proc_fs.h>

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
		int fd = proc_fd(m->private);

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

	if (!ret) {
                seq_printf(m, "pos:\t%lli\nflags:\t0%o\n",
			   (long long)file->f_pos, f_flags);
		if (file->f_op->show_fdinfo)
			ret = file->f_op->show_fdinfo(m, file);
		fput(file);
	}

	return ret;
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

static int tid_fd_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct files_struct *files;
	struct task_struct *task;
	const struct cred *cred;
	struct inode *inode;
	int fd;

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	inode = dentry->d_inode;
	task = get_proc_task(inode);
	fd = proc_fd(inode);

	if (task) {
		files = get_files_struct(task);
		if (files) {
			struct file *file;

			rcu_read_lock();
			file = fcheck_files(files, fd);
			if (file) {
				unsigned f_mode = file->f_mode;

				rcu_read_unlock();
				put_files_struct(files);

				if (task_dumpable(task)) {
					rcu_read_lock();
					cred = __task_cred(task);
					inode->i_uid = cred->euid;
					inode->i_gid = cred->egid;
					rcu_read_unlock();
				} else {
					inode->i_uid = GLOBAL_ROOT_UID;
					inode->i_gid = GLOBAL_ROOT_GID;
				}

				if (S_ISLNK(inode->i_mode)) {
					unsigned i_mode = S_IFLNK;
					if (f_mode & FMODE_READ)
						i_mode |= S_IRUSR | S_IXUSR;
					if (f_mode & FMODE_WRITE)
						i_mode |= S_IWUSR | S_IXUSR;
					inode->i_mode = i_mode;
				}

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

static const struct dentry_operations tid_fd_dentry_operations = {
	.d_revalidate	= tid_fd_revalidate,
	.d_delete	= pid_delete_dentry,
};

static int proc_fd_link(struct dentry *dentry, struct path *path)
{
	struct files_struct *files = NULL;
	struct task_struct *task;
	int ret = -ENOENT;

	task = get_proc_task(dentry->d_inode);
	if (task) {
		files = get_files_struct(task);
		put_task_struct(task);
	}

	if (files) {
		int fd = proc_fd(dentry->d_inode);
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

static struct dentry *
proc_fd_instantiate(struct inode *dir, struct dentry *dentry,
		    struct task_struct *task, const void *ptr)
{
	struct dentry *error = ERR_PTR(-ENOENT);
	unsigned fd = (unsigned long)ptr;
	struct proc_inode *ei;
	struct inode *inode;

	inode = proc_pid_make_inode(dir->i_sb, task);
	if (!inode)
		goto out;

	ei = PROC_I(inode);
	ei->fd = fd;

	inode->i_mode = S_IFLNK;
	inode->i_op = &proc_pid_link_inode_operations;
	inode->i_size = 64;

	ei->op.proc_get_link = proc_fd_link;

	d_set_d_op(dentry, &tid_fd_dentry_operations);
	d_add(dentry, inode);

	/* Close the race of the process dying before we return the dentry */
	if (tid_fd_revalidate(dentry, 0))
		error = NULL;
 out:
	return error;
}

static struct dentry *proc_lookupfd_common(struct inode *dir,
					   struct dentry *dentry,
					   instantiate_t instantiate)
{
	struct task_struct *task = get_proc_task(dir);
	struct dentry *result = ERR_PTR(-ENOENT);
	unsigned fd = name_to_int(dentry);

	if (!task)
		goto out_no_task;
	if (fd == ~0U)
		goto out;

	result = instantiate(dir, dentry, task, (void *)(unsigned long)fd);
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
	struct files_struct *files;
	unsigned int fd, ino;
	int retval;

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
			for (fd = filp->f_pos - 2;
			     fd < files_fdtable(files)->max_fds;
			     fd++, filp->f_pos++) {
				char name[PROC_NUMBUF];
				int len;
				int rv;

				if (!fcheck_files(files, fd))
					continue;
				rcu_read_unlock();

				len = snprintf(name, sizeof(name), "%d", fd);
				rv = proc_fill_cache(filp, dirent, filldir,
						     name, len, instantiate, p,
						     (void *)(unsigned long)fd);
				if (rv < 0)
					goto out_fd_loop;
				rcu_read_lock();
			}
			rcu_read_unlock();
out_fd_loop:
			put_files_struct(files);
	}
out:
	put_task_struct(p);
out_no_task:
	return retval;
}

static int proc_readfd(struct file *filp, void *dirent, filldir_t filldir)
{
	return proc_readfd_common(filp, dirent, filldir, proc_fd_instantiate);
}

const struct file_operations proc_fd_operations = {
	.read		= generic_read_dir,
	.readdir	= proc_readfd,
	.llseek		= default_llseek,
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
	int rv = generic_permission(inode, mask);
	if (rv == 0)
		return 0;
	if (task_pid(current) == proc_pid(inode))
		rv = 0;
	return rv;
}

const struct inode_operations proc_fd_inode_operations = {
	.lookup		= proc_lookupfd,
	.permission	= proc_fd_permission,
	.setattr	= proc_setattr,
};

static struct dentry *
proc_fdinfo_instantiate(struct inode *dir, struct dentry *dentry,
			struct task_struct *task, const void *ptr)
{
	struct dentry *error = ERR_PTR(-ENOENT);
	unsigned fd = (unsigned long)ptr;
	struct proc_inode *ei;
	struct inode *inode;

	inode = proc_pid_make_inode(dir->i_sb, task);
	if (!inode)
		goto out;

	ei = PROC_I(inode);
	ei->fd = fd;

	inode->i_mode = S_IFREG | S_IRUSR;
	inode->i_fop = &proc_fdinfo_file_operations;

	d_set_d_op(dentry, &tid_fd_dentry_operations);
	d_add(dentry, inode);

	/* Close the race of the process dying before we return the dentry */
	if (tid_fd_revalidate(dentry, 0))
		error = NULL;
 out:
	return error;
}

static struct dentry *
proc_lookupfdinfo(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
	return proc_lookupfd_common(dir, dentry, proc_fdinfo_instantiate);
}

static int proc_readfdinfo(struct file *filp, void *dirent, filldir_t filldir)
{
	return proc_readfd_common(filp, dirent, filldir,
				  proc_fdinfo_instantiate);
}

const struct inode_operations proc_fdinfo_inode_operations = {
	.lookup		= proc_lookupfdinfo,
	.setattr	= proc_setattr,
};

const struct file_operations proc_fdinfo_operations = {
	.read		= generic_read_dir,
	.readdir	= proc_readfdinfo,
	.llseek		= default_llseek,
};
