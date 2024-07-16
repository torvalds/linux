// SPDX-License-Identifier: GPL-2.0
#include <linux/mount.h>
#include <linux/pseudo_fs.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/proc_ns.h>
#include <linux/magic.h>
#include <linux/ktime.h>
#include <linux/seq_file.h>
#include <linux/pid_namespace.h>
#include <linux/user_namespace.h>
#include <linux/nsfs.h>
#include <linux/uaccess.h>

#include "mount.h"
#include "internal.h"

static struct vfsmount *nsfs_mnt;

static long ns_ioctl(struct file *filp, unsigned int ioctl,
			unsigned long arg);
static const struct file_operations ns_file_operations = {
	.llseek		= no_llseek,
	.unlocked_ioctl = ns_ioctl,
	.compat_ioctl   = compat_ptr_ioctl,
};

static char *ns_dname(struct dentry *dentry, char *buffer, int buflen)
{
	struct inode *inode = d_inode(dentry);
	struct ns_common *ns = inode->i_private;
	const struct proc_ns_operations *ns_ops = ns->ops;

	return dynamic_dname(buffer, buflen, "%s:[%lu]",
		ns_ops->name, inode->i_ino);
}

const struct dentry_operations ns_dentry_operations = {
	.d_delete	= always_delete_dentry,
	.d_dname	= ns_dname,
	.d_prune	= stashed_dentry_prune,
};

static void nsfs_evict(struct inode *inode)
{
	struct ns_common *ns = inode->i_private;
	clear_inode(inode);
	ns->ops->put(ns);
}

int ns_get_path_cb(struct path *path, ns_get_path_helper_t *ns_get_cb,
		     void *private_data)
{
	struct ns_common *ns;

	ns = ns_get_cb(private_data);
	if (!ns)
		return -ENOENT;

	return path_from_stashed(&ns->stashed, nsfs_mnt, ns, path);
}

struct ns_get_path_task_args {
	const struct proc_ns_operations *ns_ops;
	struct task_struct *task;
};

static struct ns_common *ns_get_path_task(void *private_data)
{
	struct ns_get_path_task_args *args = private_data;

	return args->ns_ops->get(args->task);
}

int ns_get_path(struct path *path, struct task_struct *task,
		  const struct proc_ns_operations *ns_ops)
{
	struct ns_get_path_task_args args = {
		.ns_ops	= ns_ops,
		.task	= task,
	};

	return ns_get_path_cb(path, ns_get_path_task, &args);
}

/**
 * open_namespace - open a namespace
 * @ns: the namespace to open
 *
 * This will consume a reference to @ns indendent of success or failure.
 *
 * Return: A file descriptor on success or a negative error code on failure.
 */
int open_namespace(struct ns_common *ns)
{
	struct path path __free(path_put) = {};
	struct file *f;
	int err;

	/* call first to consume reference */
	err = path_from_stashed(&ns->stashed, nsfs_mnt, ns, &path);
	if (err < 0)
		return err;

	CLASS(get_unused_fd, fd)(O_CLOEXEC);
	if (fd < 0)
		return fd;

	f = dentry_open(&path, O_RDONLY, current_cred());
	if (IS_ERR(f))
		return PTR_ERR(f);

	fd_install(fd, f);
	return take_fd(fd);
}

int open_related_ns(struct ns_common *ns,
		   struct ns_common *(*get_ns)(struct ns_common *ns))
{
	struct ns_common *relative;

	relative = get_ns(ns);
	if (IS_ERR(relative))
		return PTR_ERR(relative);

	return open_namespace(relative);
}
EXPORT_SYMBOL_GPL(open_related_ns);

static long ns_ioctl(struct file *filp, unsigned int ioctl,
			unsigned long arg)
{
	struct user_namespace *user_ns;
	struct pid_namespace *pid_ns;
	struct task_struct *tsk;
	struct ns_common *ns = get_proc_ns(file_inode(filp));
	uid_t __user *argp;
	uid_t uid;
	int ret;

	switch (ioctl) {
	case NS_GET_USERNS:
		return open_related_ns(ns, ns_get_owner);
	case NS_GET_PARENT:
		if (!ns->ops->get_parent)
			return -EINVAL;
		return open_related_ns(ns, ns->ops->get_parent);
	case NS_GET_NSTYPE:
		return ns->ops->type;
	case NS_GET_OWNER_UID:
		if (ns->ops->type != CLONE_NEWUSER)
			return -EINVAL;
		user_ns = container_of(ns, struct user_namespace, ns);
		argp = (uid_t __user *) arg;
		uid = from_kuid_munged(current_user_ns(), user_ns->owner);
		return put_user(uid, argp);
	case NS_GET_MNTNS_ID: {
		struct mnt_namespace *mnt_ns;
		__u64 __user *idp;
		__u64 id;

		if (ns->ops->type != CLONE_NEWNS)
			return -EINVAL;

		mnt_ns = container_of(ns, struct mnt_namespace, ns);
		idp = (__u64 __user *)arg;
		id = mnt_ns->seq;
		return put_user(id, idp);
	}
	case NS_GET_PID_FROM_PIDNS:
		fallthrough;
	case NS_GET_TGID_FROM_PIDNS:
		fallthrough;
	case NS_GET_PID_IN_PIDNS:
		fallthrough;
	case NS_GET_TGID_IN_PIDNS: {
		if (ns->ops->type != CLONE_NEWPID)
			return -EINVAL;

		ret = -ESRCH;
		pid_ns = container_of(ns, struct pid_namespace, ns);

		guard(rcu)();

		if (ioctl == NS_GET_PID_IN_PIDNS ||
		    ioctl == NS_GET_TGID_IN_PIDNS)
			tsk = find_task_by_vpid(arg);
		else
			tsk = find_task_by_pid_ns(arg, pid_ns);
		if (!tsk)
			break;

		switch (ioctl) {
		case NS_GET_PID_FROM_PIDNS:
			ret = task_pid_vnr(tsk);
			break;
		case NS_GET_TGID_FROM_PIDNS:
			ret = task_tgid_vnr(tsk);
			break;
		case NS_GET_PID_IN_PIDNS:
			ret = task_pid_nr_ns(tsk, pid_ns);
			break;
		case NS_GET_TGID_IN_PIDNS:
			ret = task_tgid_nr_ns(tsk, pid_ns);
			break;
		default:
			ret = 0;
			break;
		}

		if (!ret)
			ret = -ESRCH;
		break;
	}
	default:
		ret = -ENOTTY;
	}

	return ret;
}

int ns_get_name(char *buf, size_t size, struct task_struct *task,
			const struct proc_ns_operations *ns_ops)
{
	struct ns_common *ns;
	int res = -ENOENT;
	const char *name;
	ns = ns_ops->get(task);
	if (ns) {
		name = ns_ops->real_ns_name ? : ns_ops->name;
		res = snprintf(buf, size, "%s:[%u]", name, ns->inum);
		ns_ops->put(ns);
	}
	return res;
}

bool proc_ns_file(const struct file *file)
{
	return file->f_op == &ns_file_operations;
}

/**
 * ns_match() - Returns true if current namespace matches dev/ino provided.
 * @ns: current namespace
 * @dev: dev_t from nsfs that will be matched against current nsfs
 * @ino: ino_t from nsfs that will be matched against current nsfs
 *
 * Return: true if dev and ino matches the current nsfs.
 */
bool ns_match(const struct ns_common *ns, dev_t dev, ino_t ino)
{
	return (ns->inum == ino) && (nsfs_mnt->mnt_sb->s_dev == dev);
}


static int nsfs_show_path(struct seq_file *seq, struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);
	const struct ns_common *ns = inode->i_private;
	const struct proc_ns_operations *ns_ops = ns->ops;

	seq_printf(seq, "%s:[%lu]", ns_ops->name, inode->i_ino);
	return 0;
}

static const struct super_operations nsfs_ops = {
	.statfs = simple_statfs,
	.evict_inode = nsfs_evict,
	.show_path = nsfs_show_path,
};

static int nsfs_init_inode(struct inode *inode, void *data)
{
	struct ns_common *ns = data;

	inode->i_private = data;
	inode->i_mode |= S_IRUGO;
	inode->i_fop = &ns_file_operations;
	inode->i_ino = ns->inum;
	return 0;
}

static void nsfs_put_data(void *data)
{
	struct ns_common *ns = data;
	ns->ops->put(ns);
}

static const struct stashed_operations nsfs_stashed_ops = {
	.init_inode = nsfs_init_inode,
	.put_data = nsfs_put_data,
};

static int nsfs_init_fs_context(struct fs_context *fc)
{
	struct pseudo_fs_context *ctx = init_pseudo(fc, NSFS_MAGIC);
	if (!ctx)
		return -ENOMEM;
	ctx->ops = &nsfs_ops;
	ctx->dops = &ns_dentry_operations;
	fc->s_fs_info = (void *)&nsfs_stashed_ops;
	return 0;
}

static struct file_system_type nsfs = {
	.name = "nsfs",
	.init_fs_context = nsfs_init_fs_context,
	.kill_sb = kill_anon_super,
};

void __init nsfs_init(void)
{
	nsfs_mnt = kern_mount(&nsfs);
	if (IS_ERR(nsfs_mnt))
		panic("can't set nsfs up\n");
	nsfs_mnt->mnt_sb->s_flags &= ~SB_NOUSER;
}
