// SPDX-License-Identifier: GPL-2.0
#include <linux/mount.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/proc_ns.h>
#include <linux/magic.h>
#include <linux/ktime.h>
#include <linux/seq_file.h>
#include <linux/user_namespace.h>
#include <linux/nsfs.h>
#include <linux/uaccess.h>

static struct vfsmount *nsfs_mnt;

static long ns_ioctl(struct file *filp, unsigned int ioctl,
			unsigned long arg);
static const struct file_operations ns_file_operations = {
	.llseek		= no_llseek,
	.unlocked_ioctl = ns_ioctl,
};

static char *ns_dname(struct dentry *dentry, char *buffer, int buflen)
{
	struct inode *inode = d_inode(dentry);
	const struct proc_ns_operations *ns_ops = dentry->d_fsdata;

	return dynamic_dname(dentry, buffer, buflen, "%s:[%lu]",
		ns_ops->name, inode->i_ino);
}

static void ns_prune_dentry(struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);
	if (inode) {
		struct ns_common *ns = inode->i_private;
		atomic_long_set(&ns->stashed, 0);
	}
}

const struct dentry_operations ns_dentry_operations =
{
	.d_prune	= ns_prune_dentry,
	.d_delete	= always_delete_dentry,
	.d_dname	= ns_dname,
};

static void nsfs_evict(struct inode *inode)
{
	struct ns_common *ns = inode->i_private;
	clear_inode(inode);
	ns->ops->put(ns);
}

static void *__ns_get_path(struct path *path, struct ns_common *ns)
{
	struct vfsmount *mnt = nsfs_mnt;
	struct dentry *dentry;
	struct inode *inode;
	unsigned long d;

	rcu_read_lock();
	d = atomic_long_read(&ns->stashed);
	if (!d)
		goto slow;
	dentry = (struct dentry *)d;
	if (!lockref_get_not_dead(&dentry->d_lockref))
		goto slow;
	rcu_read_unlock();
	ns->ops->put(ns);
got_it:
	path->mnt = mntget(mnt);
	path->dentry = dentry;
	return NULL;
slow:
	rcu_read_unlock();
	inode = new_inode_pseudo(mnt->mnt_sb);
	if (!inode) {
		ns->ops->put(ns);
		return ERR_PTR(-ENOMEM);
	}
	inode->i_ino = ns->inum;
	inode->i_mtime = inode->i_atime = inode->i_ctime = current_time(inode);
	inode->i_flags |= S_IMMUTABLE;
	inode->i_mode = S_IFREG | S_IRUGO;
	inode->i_fop = &ns_file_operations;
	inode->i_private = ns;

	dentry = d_alloc_pseudo(mnt->mnt_sb, &empty_name);
	if (!dentry) {
		iput(inode);
		return ERR_PTR(-ENOMEM);
	}
	d_instantiate(dentry, inode);
	dentry->d_flags |= DCACHE_RCUACCESS;
	dentry->d_fsdata = (void *)ns->ops;
	d = atomic_long_cmpxchg(&ns->stashed, 0, (unsigned long)dentry);
	if (d) {
		d_delete(dentry);	/* make sure ->d_prune() does nothing */
		dput(dentry);
		cpu_relax();
		return ERR_PTR(-EAGAIN);
	}
	goto got_it;
}

void *ns_get_path(struct path *path, struct task_struct *task,
			const struct proc_ns_operations *ns_ops)
{
	struct ns_common *ns;
	void *ret;

again:
	ns = ns_ops->get(task);
	if (!ns)
		return ERR_PTR(-ENOENT);

	ret = __ns_get_path(path, ns);
	if (IS_ERR(ret) && PTR_ERR(ret) == -EAGAIN)
		goto again;
	return ret;
}

int open_related_ns(struct ns_common *ns,
		   struct ns_common *(*get_ns)(struct ns_common *ns))
{
	struct path path = {};
	struct file *f;
	void *err;
	int fd;

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0)
		return fd;

	while (1) {
		struct ns_common *relative;

		relative = get_ns(ns);
		if (IS_ERR(relative)) {
			put_unused_fd(fd);
			return PTR_ERR(relative);
		}

		err = __ns_get_path(&path, relative);
		if (IS_ERR(err) && PTR_ERR(err) == -EAGAIN)
			continue;
		break;
	}
	if (IS_ERR(err)) {
		put_unused_fd(fd);
		return PTR_ERR(err);
	}

	f = dentry_open(&path, O_RDONLY, current_cred());
	path_put(&path);
	if (IS_ERR(f)) {
		put_unused_fd(fd);
		fd = PTR_ERR(f);
	} else
		fd_install(fd, f);

	return fd;
}

static long ns_ioctl(struct file *filp, unsigned int ioctl,
			unsigned long arg)
{
	struct user_namespace *user_ns;
	struct ns_common *ns = get_proc_ns(file_inode(filp));
	uid_t __user *argp;
	uid_t uid;

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
	default:
		return -ENOTTY;
	}
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

static int nsfs_show_path(struct seq_file *seq, struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);
	const struct proc_ns_operations *ns_ops = dentry->d_fsdata;

	seq_printf(seq, "%s:[%lu]", ns_ops->name, inode->i_ino);
	return 0;
}

static const struct super_operations nsfs_ops = {
	.statfs = simple_statfs,
	.evict_inode = nsfs_evict,
	.show_path = nsfs_show_path,
};
static struct dentry *nsfs_mount(struct file_system_type *fs_type,
			int flags, const char *dev_name, void *data)
{
	return mount_pseudo(fs_type, "nsfs:", &nsfs_ops,
			&ns_dentry_operations, NSFS_MAGIC);
}
static struct file_system_type nsfs = {
	.name = "nsfs",
	.mount = nsfs_mount,
	.kill_sb = kill_anon_super,
};

void __init nsfs_init(void)
{
	nsfs_mnt = kern_mount(&nsfs);
	if (IS_ERR(nsfs_mnt))
		panic("can't set nsfs up\n");
	nsfs_mnt->mnt_sb->s_flags &= ~MS_NOUSER;
}
