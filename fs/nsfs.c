#include <linux/mount.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/proc_ns.h>
#include <linux/magic.h>
#include <linux/ktime.h>
#include <linux/seq_file.h>

static struct vfsmount *nsfs_mnt;

static const struct file_operations ns_file_operations = {
	.llseek		= no_llseek,
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

void *ns_get_path(struct path *path, struct task_struct *task,
			const struct proc_ns_operations *ns_ops)
{
	struct vfsmount *mnt = mntget(nsfs_mnt);
	struct qstr qname = { .name = "", };
	struct dentry *dentry;
	struct inode *inode;
	struct ns_common *ns;
	unsigned long d;

again:
	ns = ns_ops->get(task);
	if (!ns) {
		mntput(mnt);
		return ERR_PTR(-ENOENT);
	}
	rcu_read_lock();
	d = atomic_long_read(&ns->stashed);
	if (!d)
		goto slow;
	dentry = (struct dentry *)d;
	if (!lockref_get_not_dead(&dentry->d_lockref))
		goto slow;
	rcu_read_unlock();
	ns_ops->put(ns);
got_it:
	path->mnt = mnt;
	path->dentry = dentry;
	return NULL;
slow:
	rcu_read_unlock();
	inode = new_inode_pseudo(mnt->mnt_sb);
	if (!inode) {
		ns_ops->put(ns);
		mntput(mnt);
		return ERR_PTR(-ENOMEM);
	}
	inode->i_ino = ns->inum;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	inode->i_flags |= S_IMMUTABLE;
	inode->i_mode = S_IFREG | S_IRUGO;
	inode->i_fop = &ns_file_operations;
	inode->i_private = ns;

	dentry = d_alloc_pseudo(mnt->mnt_sb, &qname);
	if (!dentry) {
		iput(inode);
		mntput(mnt);
		return ERR_PTR(-ENOMEM);
	}
	d_instantiate(dentry, inode);
	dentry->d_fsdata = (void *)ns_ops;
	d = atomic_long_cmpxchg(&ns->stashed, 0, (unsigned long)dentry);
	if (d) {
		d_delete(dentry);	/* make sure ->d_prune() does nothing */
		dput(dentry);
		cpu_relax();
		goto again;
	}
	goto got_it;
}

int ns_get_name(char *buf, size_t size, struct task_struct *task,
			const struct proc_ns_operations *ns_ops)
{
	struct ns_common *ns;
	int res = -ENOENT;
	ns = ns_ops->get(task);
	if (ns) {
		res = snprintf(buf, size, "%s:[%u]", ns_ops->name, ns->inum);
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

	return seq_printf(seq, "%s:[%lu]", ns_ops->name, inode->i_ino);
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
