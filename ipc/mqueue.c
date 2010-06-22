/*
 * POSIX message queues filesystem for Linux.
 *
 * Copyright (C) 2003,2004  Krzysztof Benedyczak    (golbi@mat.uni.torun.pl)
 *                          Michal Wronski          (michal.wronski@gmail.com)
 *
 * Spinlocks:               Mohamed Abbas           (abbas.mohamed@intel.com)
 * Lockless receive & send, fd based notify:
 * 			    Manfred Spraul	    (manfred@colorfullife.com)
 *
 * Audit:                   George Wilson           (ltcgcw@us.ibm.com)
 *
 * This file is released under the GPL.
 */

#include <linux/capability.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/file.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/sysctl.h>
#include <linux/poll.h>
#include <linux/mqueue.h>
#include <linux/msg.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/syscalls.h>
#include <linux/audit.h>
#include <linux/signal.h>
#include <linux/mutex.h>
#include <linux/nsproxy.h>
#include <linux/pid.h>
#include <linux/ipc_namespace.h>
#include <linux/slab.h>

#include <net/sock.h>
#include "util.h"

#define MQUEUE_MAGIC	0x19800202
#define DIRENT_SIZE	20
#define FILENT_SIZE	80

#define SEND		0
#define RECV		1

#define STATE_NONE	0
#define STATE_PENDING	1
#define STATE_READY	2

struct ext_wait_queue {		/* queue of sleeping tasks */
	struct task_struct *task;
	struct list_head list;
	struct msg_msg *msg;	/* ptr of loaded message */
	int state;		/* one of STATE_* values */
};

struct mqueue_inode_info {
	spinlock_t lock;
	struct inode vfs_inode;
	wait_queue_head_t wait_q;

	struct msg_msg **messages;
	struct mq_attr attr;

	struct sigevent notify;
	struct pid* notify_owner;
	struct user_struct *user;	/* user who created, for accounting */
	struct sock *notify_sock;
	struct sk_buff *notify_cookie;

	/* for tasks waiting for free space and messages, respectively */
	struct ext_wait_queue e_wait_q[2];

	unsigned long qsize; /* size of queue in memory (sum of all msgs) */
};

static const struct inode_operations mqueue_dir_inode_operations;
static const struct file_operations mqueue_file_operations;
static const struct super_operations mqueue_super_ops;
static void remove_notification(struct mqueue_inode_info *info);

static struct kmem_cache *mqueue_inode_cachep;

static struct ctl_table_header * mq_sysctl_table;

static inline struct mqueue_inode_info *MQUEUE_I(struct inode *inode)
{
	return container_of(inode, struct mqueue_inode_info, vfs_inode);
}

/*
 * This routine should be called with the mq_lock held.
 */
static inline struct ipc_namespace *__get_ns_from_inode(struct inode *inode)
{
	return get_ipc_ns(inode->i_sb->s_fs_info);
}

static struct ipc_namespace *get_ns_from_inode(struct inode *inode)
{
	struct ipc_namespace *ns;

	spin_lock(&mq_lock);
	ns = __get_ns_from_inode(inode);
	spin_unlock(&mq_lock);
	return ns;
}

static struct inode *mqueue_get_inode(struct super_block *sb,
		struct ipc_namespace *ipc_ns, int mode,
		struct mq_attr *attr)
{
	struct user_struct *u = current_user();
	struct inode *inode;

	inode = new_inode(sb);
	if (inode) {
		inode->i_mode = mode;
		inode->i_uid = current_fsuid();
		inode->i_gid = current_fsgid();
		inode->i_mtime = inode->i_ctime = inode->i_atime =
				CURRENT_TIME;

		if (S_ISREG(mode)) {
			struct mqueue_inode_info *info;
			struct task_struct *p = current;
			unsigned long mq_bytes, mq_msg_tblsz;

			inode->i_fop = &mqueue_file_operations;
			inode->i_size = FILENT_SIZE;
			/* mqueue specific info */
			info = MQUEUE_I(inode);
			spin_lock_init(&info->lock);
			init_waitqueue_head(&info->wait_q);
			INIT_LIST_HEAD(&info->e_wait_q[0].list);
			INIT_LIST_HEAD(&info->e_wait_q[1].list);
			info->notify_owner = NULL;
			info->qsize = 0;
			info->user = NULL;	/* set when all is ok */
			memset(&info->attr, 0, sizeof(info->attr));
			info->attr.mq_maxmsg = ipc_ns->mq_msg_max;
			info->attr.mq_msgsize = ipc_ns->mq_msgsize_max;
			if (attr) {
				info->attr.mq_maxmsg = attr->mq_maxmsg;
				info->attr.mq_msgsize = attr->mq_msgsize;
			}
			mq_msg_tblsz = info->attr.mq_maxmsg * sizeof(struct msg_msg *);
			info->messages = kmalloc(mq_msg_tblsz, GFP_KERNEL);
			if (!info->messages)
				goto out_inode;

			mq_bytes = (mq_msg_tblsz +
				(info->attr.mq_maxmsg * info->attr.mq_msgsize));

			spin_lock(&mq_lock);
			if (u->mq_bytes + mq_bytes < u->mq_bytes ||
		 	    u->mq_bytes + mq_bytes >
			    task_rlimit(p, RLIMIT_MSGQUEUE)) {
				spin_unlock(&mq_lock);
				/* mqueue_delete_inode() releases info->messages */
				goto out_inode;
			}
			u->mq_bytes += mq_bytes;
			spin_unlock(&mq_lock);

			/* all is ok */
			info->user = get_uid(u);
		} else if (S_ISDIR(mode)) {
			inc_nlink(inode);
			/* Some things misbehave if size == 0 on a directory */
			inode->i_size = 2 * DIRENT_SIZE;
			inode->i_op = &mqueue_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;
		}
	}
	return inode;
out_inode:
	iput(inode);
	return NULL;
}

static int mqueue_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *inode;
	struct ipc_namespace *ns = data;
	int error;

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = MQUEUE_MAGIC;
	sb->s_op = &mqueue_super_ops;

	inode = mqueue_get_inode(sb, ns, S_IFDIR | S_ISVTX | S_IRWXUGO,
				NULL);
	if (!inode) {
		error = -ENOMEM;
		goto out;
	}

	sb->s_root = d_alloc_root(inode);
	if (!sb->s_root) {
		iput(inode);
		error = -ENOMEM;
		goto out;
	}
	error = 0;

out:
	return error;
}

static int mqueue_get_sb(struct file_system_type *fs_type,
			 int flags, const char *dev_name,
			 void *data, struct vfsmount *mnt)
{
	if (!(flags & MS_KERNMOUNT))
		data = current->nsproxy->ipc_ns;
	return get_sb_ns(fs_type, flags, data, mqueue_fill_super, mnt);
}

static void init_once(void *foo)
{
	struct mqueue_inode_info *p = (struct mqueue_inode_info *) foo;

	inode_init_once(&p->vfs_inode);
}

static struct inode *mqueue_alloc_inode(struct super_block *sb)
{
	struct mqueue_inode_info *ei;

	ei = kmem_cache_alloc(mqueue_inode_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	return &ei->vfs_inode;
}

static void mqueue_destroy_inode(struct inode *inode)
{
	kmem_cache_free(mqueue_inode_cachep, MQUEUE_I(inode));
}

static void mqueue_delete_inode(struct inode *inode)
{
	struct mqueue_inode_info *info;
	struct user_struct *user;
	unsigned long mq_bytes;
	int i;
	struct ipc_namespace *ipc_ns;

	if (S_ISDIR(inode->i_mode)) {
		clear_inode(inode);
		return;
	}
	ipc_ns = get_ns_from_inode(inode);
	info = MQUEUE_I(inode);
	spin_lock(&info->lock);
	for (i = 0; i < info->attr.mq_curmsgs; i++)
		free_msg(info->messages[i]);
	kfree(info->messages);
	spin_unlock(&info->lock);

	clear_inode(inode);

	/* Total amount of bytes accounted for the mqueue */
	mq_bytes = info->attr.mq_maxmsg * (sizeof(struct msg_msg *)
	    + info->attr.mq_msgsize);
	user = info->user;
	if (user) {
		spin_lock(&mq_lock);
		user->mq_bytes -= mq_bytes;
		/*
		 * get_ns_from_inode() ensures that the
		 * (ipc_ns = sb->s_fs_info) is either a valid ipc_ns
		 * to which we now hold a reference, or it is NULL.
		 * We can't put it here under mq_lock, though.
		 */
		if (ipc_ns)
			ipc_ns->mq_queues_count--;
		spin_unlock(&mq_lock);
		free_uid(user);
	}
	if (ipc_ns)
		put_ipc_ns(ipc_ns);
}

static int mqueue_create(struct inode *dir, struct dentry *dentry,
				int mode, struct nameidata *nd)
{
	struct inode *inode;
	struct mq_attr *attr = dentry->d_fsdata;
	int error;
	struct ipc_namespace *ipc_ns;

	spin_lock(&mq_lock);
	ipc_ns = __get_ns_from_inode(dir);
	if (!ipc_ns) {
		error = -EACCES;
		goto out_unlock;
	}
	if (ipc_ns->mq_queues_count >= ipc_ns->mq_queues_max &&
			!capable(CAP_SYS_RESOURCE)) {
		error = -ENOSPC;
		goto out_unlock;
	}
	ipc_ns->mq_queues_count++;
	spin_unlock(&mq_lock);

	inode = mqueue_get_inode(dir->i_sb, ipc_ns, mode, attr);
	if (!inode) {
		error = -ENOMEM;
		spin_lock(&mq_lock);
		ipc_ns->mq_queues_count--;
		goto out_unlock;
	}

	put_ipc_ns(ipc_ns);
	dir->i_size += DIRENT_SIZE;
	dir->i_ctime = dir->i_mtime = dir->i_atime = CURRENT_TIME;

	d_instantiate(dentry, inode);
	dget(dentry);
	return 0;
out_unlock:
	spin_unlock(&mq_lock);
	if (ipc_ns)
		put_ipc_ns(ipc_ns);
	return error;
}

static int mqueue_unlink(struct inode *dir, struct dentry *dentry)
{
  	struct inode *inode = dentry->d_inode;

	dir->i_ctime = dir->i_mtime = dir->i_atime = CURRENT_TIME;
	dir->i_size -= DIRENT_SIZE;
  	drop_nlink(inode);
  	dput(dentry);
  	return 0;
}

/*
*	This is routine for system read from queue file.
*	To avoid mess with doing here some sort of mq_receive we allow
*	to read only queue size & notification info (the only values
*	that are interesting from user point of view and aren't accessible
*	through std routines)
*/
static ssize_t mqueue_read_file(struct file *filp, char __user *u_data,
				size_t count, loff_t *off)
{
	struct mqueue_inode_info *info = MQUEUE_I(filp->f_path.dentry->d_inode);
	char buffer[FILENT_SIZE];
	ssize_t ret;

	spin_lock(&info->lock);
	snprintf(buffer, sizeof(buffer),
			"QSIZE:%-10lu NOTIFY:%-5d SIGNO:%-5d NOTIFY_PID:%-6d\n",
			info->qsize,
			info->notify_owner ? info->notify.sigev_notify : 0,
			(info->notify_owner &&
			 info->notify.sigev_notify == SIGEV_SIGNAL) ?
				info->notify.sigev_signo : 0,
			pid_vnr(info->notify_owner));
	spin_unlock(&info->lock);
	buffer[sizeof(buffer)-1] = '\0';

	ret = simple_read_from_buffer(u_data, count, off, buffer,
				strlen(buffer));
	if (ret <= 0)
		return ret;

	filp->f_path.dentry->d_inode->i_atime = filp->f_path.dentry->d_inode->i_ctime = CURRENT_TIME;
	return ret;
}

static int mqueue_flush_file(struct file *filp, fl_owner_t id)
{
	struct mqueue_inode_info *info = MQUEUE_I(filp->f_path.dentry->d_inode);

	spin_lock(&info->lock);
	if (task_tgid(current) == info->notify_owner)
		remove_notification(info);

	spin_unlock(&info->lock);
	return 0;
}

static unsigned int mqueue_poll_file(struct file *filp, struct poll_table_struct *poll_tab)
{
	struct mqueue_inode_info *info = MQUEUE_I(filp->f_path.dentry->d_inode);
	int retval = 0;

	poll_wait(filp, &info->wait_q, poll_tab);

	spin_lock(&info->lock);
	if (info->attr.mq_curmsgs)
		retval = POLLIN | POLLRDNORM;

	if (info->attr.mq_curmsgs < info->attr.mq_maxmsg)
		retval |= POLLOUT | POLLWRNORM;
	spin_unlock(&info->lock);

	return retval;
}

/* Adds current to info->e_wait_q[sr] before element with smaller prio */
static void wq_add(struct mqueue_inode_info *info, int sr,
			struct ext_wait_queue *ewp)
{
	struct ext_wait_queue *walk;

	ewp->task = current;

	list_for_each_entry(walk, &info->e_wait_q[sr].list, list) {
		if (walk->task->static_prio <= current->static_prio) {
			list_add_tail(&ewp->list, &walk->list);
			return;
		}
	}
	list_add_tail(&ewp->list, &info->e_wait_q[sr].list);
}

/*
 * Puts current task to sleep. Caller must hold queue lock. After return
 * lock isn't held.
 * sr: SEND or RECV
 */
static int wq_sleep(struct mqueue_inode_info *info, int sr,
		    ktime_t *timeout, struct ext_wait_queue *ewp)
{
	int retval;
	signed long time;

	wq_add(info, sr, ewp);

	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);

		spin_unlock(&info->lock);
		time = schedule_hrtimeout_range_clock(timeout,
		    HRTIMER_MODE_ABS, 0, CLOCK_REALTIME);

		while (ewp->state == STATE_PENDING)
			cpu_relax();

		if (ewp->state == STATE_READY) {
			retval = 0;
			goto out;
		}
		spin_lock(&info->lock);
		if (ewp->state == STATE_READY) {
			retval = 0;
			goto out_unlock;
		}
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
		if (time == 0) {
			retval = -ETIMEDOUT;
			break;
		}
	}
	list_del(&ewp->list);
out_unlock:
	spin_unlock(&info->lock);
out:
	return retval;
}

/*
 * Returns waiting task that should be serviced first or NULL if none exists
 */
static struct ext_wait_queue *wq_get_first_waiter(
		struct mqueue_inode_info *info, int sr)
{
	struct list_head *ptr;

	ptr = info->e_wait_q[sr].list.prev;
	if (ptr == &info->e_wait_q[sr].list)
		return NULL;
	return list_entry(ptr, struct ext_wait_queue, list);
}

/* Auxiliary functions to manipulate messages' list */
static void msg_insert(struct msg_msg *ptr, struct mqueue_inode_info *info)
{
	int k;

	k = info->attr.mq_curmsgs - 1;
	while (k >= 0 && info->messages[k]->m_type >= ptr->m_type) {
		info->messages[k + 1] = info->messages[k];
		k--;
	}
	info->attr.mq_curmsgs++;
	info->qsize += ptr->m_ts;
	info->messages[k + 1] = ptr;
}

static inline struct msg_msg *msg_get(struct mqueue_inode_info *info)
{
	info->qsize -= info->messages[--info->attr.mq_curmsgs]->m_ts;
	return info->messages[info->attr.mq_curmsgs];
}

static inline void set_cookie(struct sk_buff *skb, char code)
{
	((char*)skb->data)[NOTIFY_COOKIE_LEN-1] = code;
}

/*
 * The next function is only to split too long sys_mq_timedsend
 */
static void __do_notify(struct mqueue_inode_info *info)
{
	/* notification
	 * invoked when there is registered process and there isn't process
	 * waiting synchronously for message AND state of queue changed from
	 * empty to not empty. Here we are sure that no one is waiting
	 * synchronously. */
	if (info->notify_owner &&
	    info->attr.mq_curmsgs == 1) {
		struct siginfo sig_i;
		switch (info->notify.sigev_notify) {
		case SIGEV_NONE:
			break;
		case SIGEV_SIGNAL:
			/* sends signal */

			sig_i.si_signo = info->notify.sigev_signo;
			sig_i.si_errno = 0;
			sig_i.si_code = SI_MESGQ;
			sig_i.si_value = info->notify.sigev_value;
			sig_i.si_pid = task_tgid_nr_ns(current,
						ns_of_pid(info->notify_owner));
			sig_i.si_uid = current_uid();

			kill_pid_info(info->notify.sigev_signo,
				      &sig_i, info->notify_owner);
			break;
		case SIGEV_THREAD:
			set_cookie(info->notify_cookie, NOTIFY_WOKENUP);
			netlink_sendskb(info->notify_sock, info->notify_cookie);
			break;
		}
		/* after notification unregisters process */
		put_pid(info->notify_owner);
		info->notify_owner = NULL;
	}
	wake_up(&info->wait_q);
}

static int prepare_timeout(const struct timespec __user *u_abs_timeout,
			   ktime_t *expires, struct timespec *ts)
{
	if (copy_from_user(ts, u_abs_timeout, sizeof(struct timespec)))
		return -EFAULT;
	if (!timespec_valid(ts))
		return -EINVAL;

	*expires = timespec_to_ktime(*ts);
	return 0;
}

static void remove_notification(struct mqueue_inode_info *info)
{
	if (info->notify_owner != NULL &&
	    info->notify.sigev_notify == SIGEV_THREAD) {
		set_cookie(info->notify_cookie, NOTIFY_REMOVED);
		netlink_sendskb(info->notify_sock, info->notify_cookie);
	}
	put_pid(info->notify_owner);
	info->notify_owner = NULL;
}

static int mq_attr_ok(struct ipc_namespace *ipc_ns, struct mq_attr *attr)
{
	if (attr->mq_maxmsg <= 0 || attr->mq_msgsize <= 0)
		return 0;
	if (capable(CAP_SYS_RESOURCE)) {
		if (attr->mq_maxmsg > HARD_MSGMAX)
			return 0;
	} else {
		if (attr->mq_maxmsg > ipc_ns->mq_msg_max ||
				attr->mq_msgsize > ipc_ns->mq_msgsize_max)
			return 0;
	}
	/* check for overflow */
	if (attr->mq_msgsize > ULONG_MAX/attr->mq_maxmsg)
		return 0;
	if ((unsigned long)(attr->mq_maxmsg * (attr->mq_msgsize
	    + sizeof (struct msg_msg *))) <
	    (unsigned long)(attr->mq_maxmsg * attr->mq_msgsize))
		return 0;
	return 1;
}

/*
 * Invoked when creating a new queue via sys_mq_open
 */
static struct file *do_create(struct ipc_namespace *ipc_ns, struct dentry *dir,
			struct dentry *dentry, int oflag, mode_t mode,
			struct mq_attr *attr)
{
	const struct cred *cred = current_cred();
	struct file *result;
	int ret;

	if (attr) {
		if (!mq_attr_ok(ipc_ns, attr)) {
			ret = -EINVAL;
			goto out;
		}
		/* store for use during create */
		dentry->d_fsdata = attr;
	}

	mode &= ~current_umask();
	ret = mnt_want_write(ipc_ns->mq_mnt);
	if (ret)
		goto out;
	ret = vfs_create(dir->d_inode, dentry, mode, NULL);
	dentry->d_fsdata = NULL;
	if (ret)
		goto out_drop_write;

	result = dentry_open(dentry, ipc_ns->mq_mnt, oflag, cred);
	/*
	 * dentry_open() took a persistent mnt_want_write(),
	 * so we can now drop this one.
	 */
	mnt_drop_write(ipc_ns->mq_mnt);
	return result;

out_drop_write:
	mnt_drop_write(ipc_ns->mq_mnt);
out:
	dput(dentry);
	mntput(ipc_ns->mq_mnt);
	return ERR_PTR(ret);
}

/* Opens existing queue */
static struct file *do_open(struct ipc_namespace *ipc_ns,
				struct dentry *dentry, int oflag)
{
	int ret;
	const struct cred *cred = current_cred();

	static const int oflag2acc[O_ACCMODE] = { MAY_READ, MAY_WRITE,
						  MAY_READ | MAY_WRITE };

	if ((oflag & O_ACCMODE) == (O_RDWR | O_WRONLY)) {
		ret = -EINVAL;
		goto err;
	}

	if (inode_permission(dentry->d_inode, oflag2acc[oflag & O_ACCMODE])) {
		ret = -EACCES;
		goto err;
	}

	return dentry_open(dentry, ipc_ns->mq_mnt, oflag, cred);

err:
	dput(dentry);
	mntput(ipc_ns->mq_mnt);
	return ERR_PTR(ret);
}

SYSCALL_DEFINE4(mq_open, const char __user *, u_name, int, oflag, mode_t, mode,
		struct mq_attr __user *, u_attr)
{
	struct dentry *dentry;
	struct file *filp;
	char *name;
	struct mq_attr attr;
	int fd, error;
	struct ipc_namespace *ipc_ns = current->nsproxy->ipc_ns;

	if (u_attr && copy_from_user(&attr, u_attr, sizeof(struct mq_attr)))
		return -EFAULT;

	audit_mq_open(oflag, mode, u_attr ? &attr : NULL);

	if (IS_ERR(name = getname(u_name)))
		return PTR_ERR(name);

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0)
		goto out_putname;

	mutex_lock(&ipc_ns->mq_mnt->mnt_root->d_inode->i_mutex);
	dentry = lookup_one_len(name, ipc_ns->mq_mnt->mnt_root, strlen(name));
	if (IS_ERR(dentry)) {
		error = PTR_ERR(dentry);
		goto out_putfd;
	}
	mntget(ipc_ns->mq_mnt);

	if (oflag & O_CREAT) {
		if (dentry->d_inode) {	/* entry already exists */
			audit_inode(name, dentry);
			if (oflag & O_EXCL) {
				error = -EEXIST;
				goto out;
			}
			filp = do_open(ipc_ns, dentry, oflag);
		} else {
			filp = do_create(ipc_ns, ipc_ns->mq_mnt->mnt_root,
						dentry, oflag, mode,
						u_attr ? &attr : NULL);
		}
	} else {
		if (!dentry->d_inode) {
			error = -ENOENT;
			goto out;
		}
		audit_inode(name, dentry);
		filp = do_open(ipc_ns, dentry, oflag);
	}

	if (IS_ERR(filp)) {
		error = PTR_ERR(filp);
		goto out_putfd;
	}

	fd_install(fd, filp);
	goto out_upsem;

out:
	dput(dentry);
	mntput(ipc_ns->mq_mnt);
out_putfd:
	put_unused_fd(fd);
	fd = error;
out_upsem:
	mutex_unlock(&ipc_ns->mq_mnt->mnt_root->d_inode->i_mutex);
out_putname:
	putname(name);
	return fd;
}

SYSCALL_DEFINE1(mq_unlink, const char __user *, u_name)
{
	int err;
	char *name;
	struct dentry *dentry;
	struct inode *inode = NULL;
	struct ipc_namespace *ipc_ns = current->nsproxy->ipc_ns;

	name = getname(u_name);
	if (IS_ERR(name))
		return PTR_ERR(name);

	mutex_lock_nested(&ipc_ns->mq_mnt->mnt_root->d_inode->i_mutex,
			I_MUTEX_PARENT);
	dentry = lookup_one_len(name, ipc_ns->mq_mnt->mnt_root, strlen(name));
	if (IS_ERR(dentry)) {
		err = PTR_ERR(dentry);
		goto out_unlock;
	}

	if (!dentry->d_inode) {
		err = -ENOENT;
		goto out_err;
	}

	inode = dentry->d_inode;
	if (inode)
		atomic_inc(&inode->i_count);
	err = mnt_want_write(ipc_ns->mq_mnt);
	if (err)
		goto out_err;
	err = vfs_unlink(dentry->d_parent->d_inode, dentry);
	mnt_drop_write(ipc_ns->mq_mnt);
out_err:
	dput(dentry);

out_unlock:
	mutex_unlock(&ipc_ns->mq_mnt->mnt_root->d_inode->i_mutex);
	putname(name);
	if (inode)
		iput(inode);

	return err;
}

/* Pipelined send and receive functions.
 *
 * If a receiver finds no waiting message, then it registers itself in the
 * list of waiting receivers. A sender checks that list before adding the new
 * message into the message array. If there is a waiting receiver, then it
 * bypasses the message array and directly hands the message over to the
 * receiver.
 * The receiver accepts the message and returns without grabbing the queue
 * spinlock. Therefore an intermediate STATE_PENDING state and memory barriers
 * are necessary. The same algorithm is used for sysv semaphores, see
 * ipc/sem.c for more details.
 *
 * The same algorithm is used for senders.
 */

/* pipelined_send() - send a message directly to the task waiting in
 * sys_mq_timedreceive() (without inserting message into a queue).
 */
static inline void pipelined_send(struct mqueue_inode_info *info,
				  struct msg_msg *message,
				  struct ext_wait_queue *receiver)
{
	receiver->msg = message;
	list_del(&receiver->list);
	receiver->state = STATE_PENDING;
	wake_up_process(receiver->task);
	smp_wmb();
	receiver->state = STATE_READY;
}

/* pipelined_receive() - if there is task waiting in sys_mq_timedsend()
 * gets its message and put to the queue (we have one free place for sure). */
static inline void pipelined_receive(struct mqueue_inode_info *info)
{
	struct ext_wait_queue *sender = wq_get_first_waiter(info, SEND);

	if (!sender) {
		/* for poll */
		wake_up_interruptible(&info->wait_q);
		return;
	}
	msg_insert(sender->msg, info);
	list_del(&sender->list);
	sender->state = STATE_PENDING;
	wake_up_process(sender->task);
	smp_wmb();
	sender->state = STATE_READY;
}

SYSCALL_DEFINE5(mq_timedsend, mqd_t, mqdes, const char __user *, u_msg_ptr,
		size_t, msg_len, unsigned int, msg_prio,
		const struct timespec __user *, u_abs_timeout)
{
	struct file *filp;
	struct inode *inode;
	struct ext_wait_queue wait;
	struct ext_wait_queue *receiver;
	struct msg_msg *msg_ptr;
	struct mqueue_inode_info *info;
	ktime_t expires, *timeout = NULL;
	struct timespec ts;
	int ret;

	if (u_abs_timeout) {
		int res = prepare_timeout(u_abs_timeout, &expires, &ts);
		if (res)
			return res;
		timeout = &expires;
	}

	if (unlikely(msg_prio >= (unsigned long) MQ_PRIO_MAX))
		return -EINVAL;

	audit_mq_sendrecv(mqdes, msg_len, msg_prio, timeout ? &ts : NULL);

	filp = fget(mqdes);
	if (unlikely(!filp)) {
		ret = -EBADF;
		goto out;
	}

	inode = filp->f_path.dentry->d_inode;
	if (unlikely(filp->f_op != &mqueue_file_operations)) {
		ret = -EBADF;
		goto out_fput;
	}
	info = MQUEUE_I(inode);
	audit_inode(NULL, filp->f_path.dentry);

	if (unlikely(!(filp->f_mode & FMODE_WRITE))) {
		ret = -EBADF;
		goto out_fput;
	}

	if (unlikely(msg_len > info->attr.mq_msgsize)) {
		ret = -EMSGSIZE;
		goto out_fput;
	}

	/* First try to allocate memory, before doing anything with
	 * existing queues. */
	msg_ptr = load_msg(u_msg_ptr, msg_len);
	if (IS_ERR(msg_ptr)) {
		ret = PTR_ERR(msg_ptr);
		goto out_fput;
	}
	msg_ptr->m_ts = msg_len;
	msg_ptr->m_type = msg_prio;

	spin_lock(&info->lock);

	if (info->attr.mq_curmsgs == info->attr.mq_maxmsg) {
		if (filp->f_flags & O_NONBLOCK) {
			spin_unlock(&info->lock);
			ret = -EAGAIN;
		} else {
			wait.task = current;
			wait.msg = (void *) msg_ptr;
			wait.state = STATE_NONE;
			ret = wq_sleep(info, SEND, timeout, &wait);
		}
		if (ret < 0)
			free_msg(msg_ptr);
	} else {
		receiver = wq_get_first_waiter(info, RECV);
		if (receiver) {
			pipelined_send(info, msg_ptr, receiver);
		} else {
			/* adds message to the queue */
			msg_insert(msg_ptr, info);
			__do_notify(info);
		}
		inode->i_atime = inode->i_mtime = inode->i_ctime =
				CURRENT_TIME;
		spin_unlock(&info->lock);
		ret = 0;
	}
out_fput:
	fput(filp);
out:
	return ret;
}

SYSCALL_DEFINE5(mq_timedreceive, mqd_t, mqdes, char __user *, u_msg_ptr,
		size_t, msg_len, unsigned int __user *, u_msg_prio,
		const struct timespec __user *, u_abs_timeout)
{
	ssize_t ret;
	struct msg_msg *msg_ptr;
	struct file *filp;
	struct inode *inode;
	struct mqueue_inode_info *info;
	struct ext_wait_queue wait;
	ktime_t expires, *timeout = NULL;
	struct timespec ts;

	if (u_abs_timeout) {
		int res = prepare_timeout(u_abs_timeout, &expires, &ts);
		if (res)
			return res;
		timeout = &expires;
	}

	audit_mq_sendrecv(mqdes, msg_len, 0, timeout ? &ts : NULL);

	filp = fget(mqdes);
	if (unlikely(!filp)) {
		ret = -EBADF;
		goto out;
	}

	inode = filp->f_path.dentry->d_inode;
	if (unlikely(filp->f_op != &mqueue_file_operations)) {
		ret = -EBADF;
		goto out_fput;
	}
	info = MQUEUE_I(inode);
	audit_inode(NULL, filp->f_path.dentry);

	if (unlikely(!(filp->f_mode & FMODE_READ))) {
		ret = -EBADF;
		goto out_fput;
	}

	/* checks if buffer is big enough */
	if (unlikely(msg_len < info->attr.mq_msgsize)) {
		ret = -EMSGSIZE;
		goto out_fput;
	}

	spin_lock(&info->lock);
	if (info->attr.mq_curmsgs == 0) {
		if (filp->f_flags & O_NONBLOCK) {
			spin_unlock(&info->lock);
			ret = -EAGAIN;
		} else {
			wait.task = current;
			wait.state = STATE_NONE;
			ret = wq_sleep(info, RECV, timeout, &wait);
			msg_ptr = wait.msg;
		}
	} else {
		msg_ptr = msg_get(info);

		inode->i_atime = inode->i_mtime = inode->i_ctime =
				CURRENT_TIME;

		/* There is now free space in queue. */
		pipelined_receive(info);
		spin_unlock(&info->lock);
		ret = 0;
	}
	if (ret == 0) {
		ret = msg_ptr->m_ts;

		if ((u_msg_prio && put_user(msg_ptr->m_type, u_msg_prio)) ||
			store_msg(u_msg_ptr, msg_ptr, msg_ptr->m_ts)) {
			ret = -EFAULT;
		}
		free_msg(msg_ptr);
	}
out_fput:
	fput(filp);
out:
	return ret;
}

/*
 * Notes: the case when user wants us to deregister (with NULL as pointer)
 * and he isn't currently owner of notification, will be silently discarded.
 * It isn't explicitly defined in the POSIX.
 */
SYSCALL_DEFINE2(mq_notify, mqd_t, mqdes,
		const struct sigevent __user *, u_notification)
{
	int ret;
	struct file *filp;
	struct sock *sock;
	struct inode *inode;
	struct sigevent notification;
	struct mqueue_inode_info *info;
	struct sk_buff *nc;

	if (u_notification) {
		if (copy_from_user(&notification, u_notification,
					sizeof(struct sigevent)))
			return -EFAULT;
	}

	audit_mq_notify(mqdes, u_notification ? &notification : NULL);

	nc = NULL;
	sock = NULL;
	if (u_notification != NULL) {
		if (unlikely(notification.sigev_notify != SIGEV_NONE &&
			     notification.sigev_notify != SIGEV_SIGNAL &&
			     notification.sigev_notify != SIGEV_THREAD))
			return -EINVAL;
		if (notification.sigev_notify == SIGEV_SIGNAL &&
			!valid_signal(notification.sigev_signo)) {
			return -EINVAL;
		}
		if (notification.sigev_notify == SIGEV_THREAD) {
			long timeo;

			/* create the notify skb */
			nc = alloc_skb(NOTIFY_COOKIE_LEN, GFP_KERNEL);
			if (!nc) {
				ret = -ENOMEM;
				goto out;
			}
			if (copy_from_user(nc->data,
					notification.sigev_value.sival_ptr,
					NOTIFY_COOKIE_LEN)) {
				ret = -EFAULT;
				goto out;
			}

			/* TODO: add a header? */
			skb_put(nc, NOTIFY_COOKIE_LEN);
			/* and attach it to the socket */
retry:
			filp = fget(notification.sigev_signo);
			if (!filp) {
				ret = -EBADF;
				goto out;
			}
			sock = netlink_getsockbyfilp(filp);
			fput(filp);
			if (IS_ERR(sock)) {
				ret = PTR_ERR(sock);
				sock = NULL;
				goto out;
			}

			timeo = MAX_SCHEDULE_TIMEOUT;
			ret = netlink_attachskb(sock, nc, &timeo, NULL);
			if (ret == 1)
				goto retry;
			if (ret) {
				sock = NULL;
				nc = NULL;
				goto out;
			}
		}
	}

	filp = fget(mqdes);
	if (!filp) {
		ret = -EBADF;
		goto out;
	}

	inode = filp->f_path.dentry->d_inode;
	if (unlikely(filp->f_op != &mqueue_file_operations)) {
		ret = -EBADF;
		goto out_fput;
	}
	info = MQUEUE_I(inode);

	ret = 0;
	spin_lock(&info->lock);
	if (u_notification == NULL) {
		if (info->notify_owner == task_tgid(current)) {
			remove_notification(info);
			inode->i_atime = inode->i_ctime = CURRENT_TIME;
		}
	} else if (info->notify_owner != NULL) {
		ret = -EBUSY;
	} else {
		switch (notification.sigev_notify) {
		case SIGEV_NONE:
			info->notify.sigev_notify = SIGEV_NONE;
			break;
		case SIGEV_THREAD:
			info->notify_sock = sock;
			info->notify_cookie = nc;
			sock = NULL;
			nc = NULL;
			info->notify.sigev_notify = SIGEV_THREAD;
			break;
		case SIGEV_SIGNAL:
			info->notify.sigev_signo = notification.sigev_signo;
			info->notify.sigev_value = notification.sigev_value;
			info->notify.sigev_notify = SIGEV_SIGNAL;
			break;
		}

		info->notify_owner = get_pid(task_tgid(current));
		inode->i_atime = inode->i_ctime = CURRENT_TIME;
	}
	spin_unlock(&info->lock);
out_fput:
	fput(filp);
out:
	if (sock) {
		netlink_detachskb(sock, nc);
	} else if (nc) {
		dev_kfree_skb(nc);
	}
	return ret;
}

SYSCALL_DEFINE3(mq_getsetattr, mqd_t, mqdes,
		const struct mq_attr __user *, u_mqstat,
		struct mq_attr __user *, u_omqstat)
{
	int ret;
	struct mq_attr mqstat, omqstat;
	struct file *filp;
	struct inode *inode;
	struct mqueue_inode_info *info;

	if (u_mqstat != NULL) {
		if (copy_from_user(&mqstat, u_mqstat, sizeof(struct mq_attr)))
			return -EFAULT;
		if (mqstat.mq_flags & (~O_NONBLOCK))
			return -EINVAL;
	}

	filp = fget(mqdes);
	if (!filp) {
		ret = -EBADF;
		goto out;
	}

	inode = filp->f_path.dentry->d_inode;
	if (unlikely(filp->f_op != &mqueue_file_operations)) {
		ret = -EBADF;
		goto out_fput;
	}
	info = MQUEUE_I(inode);

	spin_lock(&info->lock);

	omqstat = info->attr;
	omqstat.mq_flags = filp->f_flags & O_NONBLOCK;
	if (u_mqstat) {
		audit_mq_getsetattr(mqdes, &mqstat);
		spin_lock(&filp->f_lock);
		if (mqstat.mq_flags & O_NONBLOCK)
			filp->f_flags |= O_NONBLOCK;
		else
			filp->f_flags &= ~O_NONBLOCK;
		spin_unlock(&filp->f_lock);

		inode->i_atime = inode->i_ctime = CURRENT_TIME;
	}

	spin_unlock(&info->lock);

	ret = 0;
	if (u_omqstat != NULL && copy_to_user(u_omqstat, &omqstat,
						sizeof(struct mq_attr)))
		ret = -EFAULT;

out_fput:
	fput(filp);
out:
	return ret;
}

static const struct inode_operations mqueue_dir_inode_operations = {
	.lookup = simple_lookup,
	.create = mqueue_create,
	.unlink = mqueue_unlink,
};

static const struct file_operations mqueue_file_operations = {
	.flush = mqueue_flush_file,
	.poll = mqueue_poll_file,
	.read = mqueue_read_file,
};

static const struct super_operations mqueue_super_ops = {
	.alloc_inode = mqueue_alloc_inode,
	.destroy_inode = mqueue_destroy_inode,
	.statfs = simple_statfs,
	.delete_inode = mqueue_delete_inode,
	.drop_inode = generic_delete_inode,
};

static struct file_system_type mqueue_fs_type = {
	.name = "mqueue",
	.get_sb = mqueue_get_sb,
	.kill_sb = kill_litter_super,
};

int mq_init_ns(struct ipc_namespace *ns)
{
	ns->mq_queues_count  = 0;
	ns->mq_queues_max    = DFLT_QUEUESMAX;
	ns->mq_msg_max       = DFLT_MSGMAX;
	ns->mq_msgsize_max   = DFLT_MSGSIZEMAX;

	ns->mq_mnt = kern_mount_data(&mqueue_fs_type, ns);
	if (IS_ERR(ns->mq_mnt)) {
		int err = PTR_ERR(ns->mq_mnt);
		ns->mq_mnt = NULL;
		return err;
	}
	return 0;
}

void mq_clear_sbinfo(struct ipc_namespace *ns)
{
	ns->mq_mnt->mnt_sb->s_fs_info = NULL;
}

void mq_put_mnt(struct ipc_namespace *ns)
{
	mntput(ns->mq_mnt);
}

static int __init init_mqueue_fs(void)
{
	int error;

	mqueue_inode_cachep = kmem_cache_create("mqueue_inode_cache",
				sizeof(struct mqueue_inode_info), 0,
				SLAB_HWCACHE_ALIGN, init_once);
	if (mqueue_inode_cachep == NULL)
		return -ENOMEM;

	/* ignore failures - they are not fatal */
	mq_sysctl_table = mq_register_sysctl_table();

	error = register_filesystem(&mqueue_fs_type);
	if (error)
		goto out_sysctl;

	spin_lock_init(&mq_lock);

	init_ipc_ns.mq_mnt = kern_mount_data(&mqueue_fs_type, &init_ipc_ns);
	if (IS_ERR(init_ipc_ns.mq_mnt)) {
		error = PTR_ERR(init_ipc_ns.mq_mnt);
		goto out_filesystem;
	}

	return 0;

out_filesystem:
	unregister_filesystem(&mqueue_fs_type);
out_sysctl:
	if (mq_sysctl_table)
		unregister_sysctl_table(mq_sysctl_table);
	kmem_cache_destroy(mqueue_inode_cachep);
	return error;
}

__initcall(init_mqueue_fs);
