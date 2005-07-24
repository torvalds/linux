/*
 * net/sunrpc/rpc_pipe.c
 *
 * Userland/kernel interface for rpcauth_gss.
 * Code shamelessly plagiarized from fs/nfsd/nfsctl.c
 * and fs/sysfs/inode.c
 *
 * Copyright (c) 2002, Trond Myklebust <trond.myklebust@fys.uio.no>
 *
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/pagemap.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/dnotify.h>
#include <linux/kernel.h>

#include <asm/ioctls.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/seq_file.h>

#include <linux/sunrpc/clnt.h>
#include <linux/workqueue.h>
#include <linux/sunrpc/rpc_pipe_fs.h>

static struct vfsmount *rpc_mount __read_mostly;
static int rpc_mount_count;

static struct file_system_type rpc_pipe_fs_type;


static kmem_cache_t *rpc_inode_cachep __read_mostly;

#define RPC_UPCALL_TIMEOUT (30*HZ)

static void
__rpc_purge_upcall(struct inode *inode, int err)
{
	struct rpc_inode *rpci = RPC_I(inode);
	struct rpc_pipe_msg *msg;

	while (!list_empty(&rpci->pipe)) {
		msg = list_entry(rpci->pipe.next, struct rpc_pipe_msg, list);
		list_del_init(&msg->list);
		msg->errno = err;
		rpci->ops->destroy_msg(msg);
	}
	while (!list_empty(&rpci->in_upcall)) {
		msg = list_entry(rpci->pipe.next, struct rpc_pipe_msg, list);
		list_del_init(&msg->list);
		msg->errno = err;
		rpci->ops->destroy_msg(msg);
	}
	rpci->pipelen = 0;
	wake_up(&rpci->waitq);
}

static void
rpc_timeout_upcall_queue(void *data)
{
	struct rpc_inode *rpci = (struct rpc_inode *)data;
	struct inode *inode = &rpci->vfs_inode;

	down(&inode->i_sem);
	if (rpci->nreaders == 0 && !list_empty(&rpci->pipe))
		__rpc_purge_upcall(inode, -ETIMEDOUT);
	up(&inode->i_sem);
}

int
rpc_queue_upcall(struct inode *inode, struct rpc_pipe_msg *msg)
{
	struct rpc_inode *rpci = RPC_I(inode);
	int res = 0;

	down(&inode->i_sem);
	if (rpci->nreaders) {
		list_add_tail(&msg->list, &rpci->pipe);
		rpci->pipelen += msg->len;
	} else if (rpci->flags & RPC_PIPE_WAIT_FOR_OPEN) {
		if (list_empty(&rpci->pipe))
			schedule_delayed_work(&rpci->queue_timeout,
					RPC_UPCALL_TIMEOUT);
		list_add_tail(&msg->list, &rpci->pipe);
		rpci->pipelen += msg->len;
	} else
		res = -EPIPE;
	up(&inode->i_sem);
	wake_up(&rpci->waitq);
	return res;
}

static void
rpc_close_pipes(struct inode *inode)
{
	struct rpc_inode *rpci = RPC_I(inode);

	cancel_delayed_work(&rpci->queue_timeout);
	flush_scheduled_work();
	down(&inode->i_sem);
	if (rpci->ops != NULL) {
		rpci->nreaders = 0;
		__rpc_purge_upcall(inode, -EPIPE);
		rpci->nwriters = 0;
		if (rpci->ops->release_pipe)
			rpci->ops->release_pipe(inode);
		rpci->ops = NULL;
	}
	up(&inode->i_sem);
}

static inline void
rpc_inode_setowner(struct inode *inode, void *private)
{
	RPC_I(inode)->private = private;
}

static struct inode *
rpc_alloc_inode(struct super_block *sb)
{
	struct rpc_inode *rpci;
	rpci = (struct rpc_inode *)kmem_cache_alloc(rpc_inode_cachep, SLAB_KERNEL);
	if (!rpci)
		return NULL;
	return &rpci->vfs_inode;
}

static void
rpc_destroy_inode(struct inode *inode)
{
	kmem_cache_free(rpc_inode_cachep, RPC_I(inode));
}

static int
rpc_pipe_open(struct inode *inode, struct file *filp)
{
	struct rpc_inode *rpci = RPC_I(inode);
	int res = -ENXIO;

	down(&inode->i_sem);
	if (rpci->ops != NULL) {
		if (filp->f_mode & FMODE_READ)
			rpci->nreaders ++;
		if (filp->f_mode & FMODE_WRITE)
			rpci->nwriters ++;
		res = 0;
	}
	up(&inode->i_sem);
	return res;
}

static int
rpc_pipe_release(struct inode *inode, struct file *filp)
{
	struct rpc_inode *rpci = RPC_I(filp->f_dentry->d_inode);
	struct rpc_pipe_msg *msg;

	down(&inode->i_sem);
	if (rpci->ops == NULL)
		goto out;
	msg = (struct rpc_pipe_msg *)filp->private_data;
	if (msg != NULL) {
		msg->errno = -EPIPE;
		list_del_init(&msg->list);
		rpci->ops->destroy_msg(msg);
	}
	if (filp->f_mode & FMODE_WRITE)
		rpci->nwriters --;
	if (filp->f_mode & FMODE_READ)
		rpci->nreaders --;
	if (!rpci->nreaders)
		__rpc_purge_upcall(inode, -EPIPE);
	if (rpci->ops->release_pipe)
		rpci->ops->release_pipe(inode);
out:
	up(&inode->i_sem);
	return 0;
}

static ssize_t
rpc_pipe_read(struct file *filp, char __user *buf, size_t len, loff_t *offset)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct rpc_inode *rpci = RPC_I(inode);
	struct rpc_pipe_msg *msg;
	int res = 0;

	down(&inode->i_sem);
	if (rpci->ops == NULL) {
		res = -EPIPE;
		goto out_unlock;
	}
	msg = filp->private_data;
	if (msg == NULL) {
		if (!list_empty(&rpci->pipe)) {
			msg = list_entry(rpci->pipe.next,
					struct rpc_pipe_msg,
					list);
			list_move(&msg->list, &rpci->in_upcall);
			rpci->pipelen -= msg->len;
			filp->private_data = msg;
			msg->copied = 0;
		}
		if (msg == NULL)
			goto out_unlock;
	}
	/* NOTE: it is up to the callback to update msg->copied */
	res = rpci->ops->upcall(filp, msg, buf, len);
	if (res < 0 || msg->len == msg->copied) {
		filp->private_data = NULL;
		list_del_init(&msg->list);
		rpci->ops->destroy_msg(msg);
	}
out_unlock:
	up(&inode->i_sem);
	return res;
}

static ssize_t
rpc_pipe_write(struct file *filp, const char __user *buf, size_t len, loff_t *offset)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct rpc_inode *rpci = RPC_I(inode);
	int res;

	down(&inode->i_sem);
	res = -EPIPE;
	if (rpci->ops != NULL)
		res = rpci->ops->downcall(filp, buf, len);
	up(&inode->i_sem);
	return res;
}

static unsigned int
rpc_pipe_poll(struct file *filp, struct poll_table_struct *wait)
{
	struct rpc_inode *rpci;
	unsigned int mask = 0;

	rpci = RPC_I(filp->f_dentry->d_inode);
	poll_wait(filp, &rpci->waitq, wait);

	mask = POLLOUT | POLLWRNORM;
	if (rpci->ops == NULL)
		mask |= POLLERR | POLLHUP;
	if (!list_empty(&rpci->pipe))
		mask |= POLLIN | POLLRDNORM;
	return mask;
}

static int
rpc_pipe_ioctl(struct inode *ino, struct file *filp,
		unsigned int cmd, unsigned long arg)
{
	struct rpc_inode *rpci = RPC_I(filp->f_dentry->d_inode);
	int len;

	switch (cmd) {
	case FIONREAD:
		if (rpci->ops == NULL)
			return -EPIPE;
		len = rpci->pipelen;
		if (filp->private_data) {
			struct rpc_pipe_msg *msg;
			msg = (struct rpc_pipe_msg *)filp->private_data;
			len += msg->len - msg->copied;
		}
		return put_user(len, (int __user *)arg);
	default:
		return -EINVAL;
	}
}

static struct file_operations rpc_pipe_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= rpc_pipe_read,
	.write		= rpc_pipe_write,
	.poll		= rpc_pipe_poll,
	.ioctl		= rpc_pipe_ioctl,
	.open		= rpc_pipe_open,
	.release	= rpc_pipe_release,
};

static int
rpc_show_info(struct seq_file *m, void *v)
{
	struct rpc_clnt *clnt = m->private;

	seq_printf(m, "RPC server: %s\n", clnt->cl_server);
	seq_printf(m, "service: %s (%d) version %d\n", clnt->cl_protname,
			clnt->cl_prog, clnt->cl_vers);
	seq_printf(m, "address: %u.%u.%u.%u\n",
			NIPQUAD(clnt->cl_xprt->addr.sin_addr.s_addr));
	seq_printf(m, "protocol: %s\n",
			clnt->cl_xprt->prot == IPPROTO_UDP ? "udp" : "tcp");
	return 0;
}

static int
rpc_info_open(struct inode *inode, struct file *file)
{
	struct rpc_clnt *clnt;
	int ret = single_open(file, rpc_show_info, NULL);

	if (!ret) {
		struct seq_file *m = file->private_data;
		down(&inode->i_sem);
		clnt = RPC_I(inode)->private;
		if (clnt) {
			atomic_inc(&clnt->cl_users);
			m->private = clnt;
		} else {
			single_release(inode, file);
			ret = -EINVAL;
		}
		up(&inode->i_sem);
	}
	return ret;
}

static int
rpc_info_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = file->private_data;
	struct rpc_clnt *clnt = (struct rpc_clnt *)m->private;

	if (clnt)
		rpc_release_client(clnt);
	return single_release(inode, file);
}

static struct file_operations rpc_info_operations = {
	.owner		= THIS_MODULE,
	.open		= rpc_info_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= rpc_info_release,
};


/*
 * We have a single directory with 1 node in it.
 */
enum {
	RPCAUTH_Root = 1,
	RPCAUTH_lockd,
	RPCAUTH_mount,
	RPCAUTH_nfs,
	RPCAUTH_portmap,
	RPCAUTH_statd,
	RPCAUTH_RootEOF
};

/*
 * Description of fs contents.
 */
struct rpc_filelist {
	char *name;
	struct file_operations *i_fop;
	int mode;
};

static struct rpc_filelist files[] = {
	[RPCAUTH_lockd] = {
		.name = "lockd",
		.mode = S_IFDIR | S_IRUGO | S_IXUGO,
	},
	[RPCAUTH_mount] = {
		.name = "mount",
		.mode = S_IFDIR | S_IRUGO | S_IXUGO,
	},
	[RPCAUTH_nfs] = {
		.name = "nfs",
		.mode = S_IFDIR | S_IRUGO | S_IXUGO,
	},
	[RPCAUTH_portmap] = {
		.name = "portmap",
		.mode = S_IFDIR | S_IRUGO | S_IXUGO,
	},
	[RPCAUTH_statd] = {
		.name = "statd",
		.mode = S_IFDIR | S_IRUGO | S_IXUGO,
	},
};

enum {
	RPCAUTH_info = 2,
	RPCAUTH_EOF
};

static struct rpc_filelist authfiles[] = {
	[RPCAUTH_info] = {
		.name = "info",
		.i_fop = &rpc_info_operations,
		.mode = S_IFREG | S_IRUSR,
	},
};

static int
rpc_get_mount(void)
{
	return simple_pin_fs("rpc_pipefs", &rpc_mount, &rpc_mount_count);
}

static void
rpc_put_mount(void)
{
	simple_release_fs(&rpc_mount, &rpc_mount_count);
}

static struct inode *
rpc_get_inode(struct super_block *sb, int mode)
{
	struct inode *inode = new_inode(sb);
	if (!inode)
		return NULL;
	inode->i_mode = mode;
	inode->i_uid = inode->i_gid = 0;
	inode->i_blksize = PAGE_CACHE_SIZE;
	inode->i_blocks = 0;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	switch(mode & S_IFMT) {
		case S_IFDIR:
			inode->i_fop = &simple_dir_operations;
			inode->i_op = &simple_dir_inode_operations;
			inode->i_nlink++;
		default:
			break;
	}
	return inode;
}

/*
 * FIXME: This probably has races.
 */
static void
rpc_depopulate(struct dentry *parent)
{
	struct inode *dir = parent->d_inode;
	struct list_head *pos, *next;
	struct dentry *dentry, *dvec[10];
	int n = 0;

	down(&dir->i_sem);
repeat:
	spin_lock(&dcache_lock);
	list_for_each_safe(pos, next, &parent->d_subdirs) {
		dentry = list_entry(pos, struct dentry, d_child);
		spin_lock(&dentry->d_lock);
		if (!d_unhashed(dentry)) {
			dget_locked(dentry);
			__d_drop(dentry);
			spin_unlock(&dentry->d_lock);
			dvec[n++] = dentry;
			if (n == ARRAY_SIZE(dvec))
				break;
		} else
			spin_unlock(&dentry->d_lock);
	}
	spin_unlock(&dcache_lock);
	if (n) {
		do {
			dentry = dvec[--n];
			if (dentry->d_inode) {
				rpc_close_pipes(dentry->d_inode);
				rpc_inode_setowner(dentry->d_inode, NULL);
				simple_unlink(dir, dentry);
			}
			dput(dentry);
		} while (n);
		goto repeat;
	}
	up(&dir->i_sem);
}

static int
rpc_populate(struct dentry *parent,
		struct rpc_filelist *files,
		int start, int eof)
{
	struct inode *inode, *dir = parent->d_inode;
	void *private = RPC_I(dir)->private;
	struct dentry *dentry;
	int mode, i;

	down(&dir->i_sem);
	for (i = start; i < eof; i++) {
		dentry = d_alloc_name(parent, files[i].name);
		if (!dentry)
			goto out_bad;
		mode = files[i].mode;
		inode = rpc_get_inode(dir->i_sb, mode);
		if (!inode) {
			dput(dentry);
			goto out_bad;
		}
		inode->i_ino = i;
		if (files[i].i_fop)
			inode->i_fop = files[i].i_fop;
		if (private)
			rpc_inode_setowner(inode, private);
		if (S_ISDIR(mode))
			dir->i_nlink++;
		d_add(dentry, inode);
	}
	up(&dir->i_sem);
	return 0;
out_bad:
	up(&dir->i_sem);
	printk(KERN_WARNING "%s: %s failed to populate directory %s\n",
			__FILE__, __FUNCTION__, parent->d_name.name);
	return -ENOMEM;
}

struct dentry *
rpc_mkdir(struct dentry *parent, char *name, struct rpc_clnt *rpc_client)
{
	struct inode *dir;
	struct dentry *dentry;
	struct inode *inode;
	int error;

	if (!parent)
		parent = rpc_mount->mnt_root;

	dir = parent->d_inode;
	
	error = rpc_get_mount();
	if (error)
		return ERR_PTR(error);

	down(&dir->i_sem);
	dentry = lookup_one_len(name, parent, strlen(name));
	if (IS_ERR(dentry))
		goto out_unlock;
	if (dentry->d_inode) {
		dentry = ERR_PTR(-EEXIST);
		goto out_dput;
	}

	inode = rpc_get_inode(dir->i_sb, S_IFDIR | S_IRUSR | S_IXUSR);
	if (!inode)
		goto out_dput;
	inode->i_ino = iunique(dir->i_sb, 100);
	dir->i_nlink++;
	RPC_I(dentry->d_inode)->private = rpc_client;

	d_instantiate(dentry, inode);
	dget(dentry);
	up(&dir->i_sem);

	inode_dir_notify(dir, DN_CREATE);

	error = rpc_populate(dentry, authfiles,
			RPCAUTH_info, RPCAUTH_EOF);
	if (error)
		goto out_depopulate;

	return dentry;

 out_depopulate:
	rpc_rmdir(dentry);
 out_dput:
	dput(dentry);
 out_unlock:
	up(&dir->i_sem);
	rpc_put_mount();
	return dentry;
}

void
rpc_rmdir(struct dentry *dentry)
{
	struct dentry *parent = dentry->d_parent;

	rpc_depopulate(dentry);

	down(&parent->d_inode->i_sem);
	if (dentry->d_inode) {
		rpc_close_pipes(dentry->d_inode);
		rpc_inode_setowner(dentry->d_inode, NULL);
		simple_rmdir(parent->d_inode, dentry);
	}
	up(&parent->d_inode->i_sem);

	inode_dir_notify(parent->d_inode, DN_DELETE);
	rpc_put_mount();
}

struct dentry *
rpc_mkpipe(struct dentry *parent, char *name, void *private,
	   struct rpc_pipe_ops *ops, int flags)
{
	struct inode *dir = parent->d_inode;
	struct dentry *dentry;
	struct inode *inode;
	struct rpc_inode *rpci;
	int error;

	error = rpc_get_mount();
	if (error)
		return ERR_PTR(error);

	down(&parent->d_inode->i_sem);
	dentry = lookup_one_len(name, parent, strlen(name));
	if (IS_ERR(dentry))
		goto out_unlock;
	if (dentry->d_inode) {
		dentry = ERR_PTR(-EEXIST);
		goto out_dput;
	}

	inode = rpc_get_inode(parent->d_inode->i_sb,
			S_IFSOCK | S_IRUSR | S_IWUSR);
	if (!inode) {
		dentry = ERR_PTR(-ENOMEM);
		goto out_dput;
	}

	inode->i_ino = iunique(dir->i_sb, 100);
	inode->i_fop = &rpc_pipe_fops;

	rpci = RPC_I(inode);
	rpci->private = private;
	rpci->flags = flags;
	rpci->ops = ops;

	d_instantiate(dentry, inode);
	dget(dentry);
	up(&parent->d_inode->i_sem);

	inode_dir_notify(dir, DN_CREATE);
	return dentry;

 out_dput:
	dput(dentry);
 out_unlock:
	up(&parent->d_inode->i_sem);
	rpc_put_mount();
	return dentry;
}

void
rpc_unlink(struct dentry *dentry)
{
	struct dentry *parent = dentry->d_parent;

	down(&parent->d_inode->i_sem);
	if (dentry->d_inode) {
		rpc_close_pipes(dentry->d_inode);
		rpc_inode_setowner(dentry->d_inode, NULL);
		simple_unlink(parent->d_inode, dentry);
	}
	up(&parent->d_inode->i_sem);

	inode_dir_notify(parent->d_inode, DN_DELETE);
	rpc_put_mount();
}

/*
 * populate the filesystem
 */
static struct super_operations s_ops = {
	.alloc_inode	= rpc_alloc_inode,
	.destroy_inode	= rpc_destroy_inode,
	.statfs		= simple_statfs,
};

#define RPCAUTH_GSSMAGIC 0x67596969

static int
rpc_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *inode;
	struct dentry *root;

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = RPCAUTH_GSSMAGIC;
	sb->s_op = &s_ops;
	sb->s_time_gran = 1;

	inode = rpc_get_inode(sb, S_IFDIR | 0755);
	if (!inode)
		return -ENOMEM;
	root = d_alloc_root(inode);
	if (!root) {
		iput(inode);
		return -ENOMEM;
	}
	if (rpc_populate(root, files, RPCAUTH_Root + 1, RPCAUTH_RootEOF))
		goto out;
	sb->s_root = root;
	return 0;
out:
	d_genocide(root);
	dput(root);
	return -ENOMEM;
}

static struct super_block *
rpc_get_sb(struct file_system_type *fs_type,
		int flags, const char *dev_name, void *data)
{
	return get_sb_single(fs_type, flags, data, rpc_fill_super);
}

static struct file_system_type rpc_pipe_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "rpc_pipefs",
	.get_sb		= rpc_get_sb,
	.kill_sb	= kill_litter_super,
};

static void
init_once(void * foo, kmem_cache_t * cachep, unsigned long flags)
{
	struct rpc_inode *rpci = (struct rpc_inode *) foo;

	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR) {
		inode_init_once(&rpci->vfs_inode);
		rpci->private = NULL;
		rpci->nreaders = 0;
		rpci->nwriters = 0;
		INIT_LIST_HEAD(&rpci->in_upcall);
		INIT_LIST_HEAD(&rpci->pipe);
		rpci->pipelen = 0;
		init_waitqueue_head(&rpci->waitq);
		INIT_WORK(&rpci->queue_timeout, rpc_timeout_upcall_queue, rpci);
		rpci->ops = NULL;
	}
}

int register_rpc_pipefs(void)
{
	rpc_inode_cachep = kmem_cache_create("rpc_inode_cache",
                                             sizeof(struct rpc_inode),
                                             0, SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT,
                                             init_once, NULL);
	if (!rpc_inode_cachep)
		return -ENOMEM;
	register_filesystem(&rpc_pipe_fs_type);
	return 0;
}

void unregister_rpc_pipefs(void)
{
	if (kmem_cache_destroy(rpc_inode_cachep))
		printk(KERN_WARNING "RPC: unable to free inode cache\n");
	unregister_filesystem(&rpc_pipe_fs_type);
}
