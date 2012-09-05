#include <linux/proc_fs.h>
#include <linux/nsproxy.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/fs_struct.h>
#include <linux/mount.h>
#include <linux/path.h>
#include <linux/namei.h>
#include <linux/file.h>
#include <linux/utsname.h>
#include <net/net_namespace.h>
#include <linux/ipc_namespace.h>
#include <linux/pid_namespace.h>
#include "internal.h"


static const struct proc_ns_operations *ns_entries[] = {
#ifdef CONFIG_NET_NS
	&netns_operations,
#endif
#ifdef CONFIG_UTS_NS
	&utsns_operations,
#endif
#ifdef CONFIG_IPC_NS
	&ipcns_operations,
#endif
};

static const struct file_operations ns_file_operations = {
	.llseek		= no_llseek,
};

static struct dentry *proc_ns_instantiate(struct inode *dir,
	struct dentry *dentry, struct task_struct *task, const void *ptr)
{
	const struct proc_ns_operations *ns_ops = ptr;
	struct inode *inode;
	struct proc_inode *ei;
	struct dentry *error = ERR_PTR(-ENOENT);
	void *ns;

	inode = proc_pid_make_inode(dir->i_sb, task);
	if (!inode)
		goto out;

	ns = ns_ops->get(task);
	if (!ns)
		goto out_iput;

	ei = PROC_I(inode);
	inode->i_mode = S_IFREG|S_IRUSR;
	inode->i_fop  = &ns_file_operations;
	ei->ns_ops    = ns_ops;
	ei->ns	      = ns;

	d_set_d_op(dentry, &pid_dentry_operations);
	d_add(dentry, inode);
	/* Close the race of the process dying before we return the dentry */
	if (pid_revalidate(dentry, 0))
		error = NULL;
out:
	return error;
out_iput:
	iput(inode);
	goto out;
}

static int proc_ns_fill_cache(struct file *filp, void *dirent,
	filldir_t filldir, struct task_struct *task,
	const struct proc_ns_operations *ops)
{
	return proc_fill_cache(filp, dirent, filldir,
				ops->name, strlen(ops->name),
				proc_ns_instantiate, task, ops);
}

static int proc_ns_dir_readdir(struct file *filp, void *dirent,
				filldir_t filldir)
{
	int i;
	struct dentry *dentry = filp->f_path.dentry;
	struct inode *inode = dentry->d_inode;
	struct task_struct *task = get_proc_task(inode);
	const struct proc_ns_operations **entry, **last;
	ino_t ino;
	int ret;

	ret = -ENOENT;
	if (!task)
		goto out_no_task;

	ret = -EPERM;
	if (!ptrace_may_access(task, PTRACE_MODE_READ))
		goto out;

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
		if (i >= ARRAY_SIZE(ns_entries)) {
			ret = 1;
			goto out;
		}
		entry = ns_entries + i;
		last = &ns_entries[ARRAY_SIZE(ns_entries) - 1];
		while (entry <= last) {
			if (proc_ns_fill_cache(filp, dirent, filldir,
						task, *entry) < 0)
				goto out;
			filp->f_pos++;
			entry++;
		}
	}

	ret = 1;
out:
	put_task_struct(task);
out_no_task:
	return ret;
}

const struct file_operations proc_ns_dir_operations = {
	.read		= generic_read_dir,
	.readdir	= proc_ns_dir_readdir,
};

static struct dentry *proc_ns_dir_lookup(struct inode *dir,
				struct dentry *dentry, unsigned int flags)
{
	struct dentry *error;
	struct task_struct *task = get_proc_task(dir);
	const struct proc_ns_operations **entry, **last;
	unsigned int len = dentry->d_name.len;

	error = ERR_PTR(-ENOENT);

	if (!task)
		goto out_no_task;

	error = ERR_PTR(-EPERM);
	if (!ptrace_may_access(task, PTRACE_MODE_READ))
		goto out;

	last = &ns_entries[ARRAY_SIZE(ns_entries)];
	for (entry = ns_entries; entry < last; entry++) {
		if (strlen((*entry)->name) != len)
			continue;
		if (!memcmp(dentry->d_name.name, (*entry)->name, len))
			break;
	}
	error = ERR_PTR(-ENOENT);
	if (entry == last)
		goto out;

	error = proc_ns_instantiate(dir, dentry, task, *entry);
out:
	put_task_struct(task);
out_no_task:
	return error;
}

const struct inode_operations proc_ns_dir_inode_operations = {
	.lookup		= proc_ns_dir_lookup,
	.getattr	= pid_getattr,
	.setattr	= proc_setattr,
};

struct file *proc_ns_fget(int fd)
{
	struct file *file;

	file = fget(fd);
	if (!file)
		return ERR_PTR(-EBADF);

	if (file->f_op != &ns_file_operations)
		goto out_invalid;

	return file;

out_invalid:
	fput(file);
	return ERR_PTR(-EINVAL);
}

