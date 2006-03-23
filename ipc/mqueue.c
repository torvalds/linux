/*
 * POSIX message queues filesystem for Linux.
 *
 * Copyright (C) 2003,2004  Krzysztof Benedyczak    (golbi@mat.uni.torun.pl)
 *                          Michal Wronski          (Michal.Wronski@motorola.com)
 *
 * Spinlocks:               Mohamed Abbas           (abbas.mohamed@intel.com)
 * Lockless receive & send, fd based notify:
 * 			    Manfred Spraul	    (manfred@colorfullife.com)
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
#include <linux/signal.h>
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

/* used by sysctl */
#define FS_MQUEUE 	1
#define CTL_QUEUESMAX 	2
#define CTL_MSGMAX 	3
#define CTL_MSGSIZEMAX 	4

/* default values */
#define DFLT_QUEUESMAX	256	/* max number of message queues */
#define DFLT_MSGMAX 	10	/* max number of messages in each queue */
#define HARD_MSGMAX 	(131072/sizeof(void*))
#define DFLT_MSGSIZEMAX 8192	/* max message size */


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
	pid_t notify_owner;
	struct user_struct *user;	/* user who created, for accounting */
	struct sock *notify_sock;
	struct sk_buff *notify_cookie;

	/* for tasks waiting for free space and messages, respectively */
	struct ext_wait_queue e_wait_q[2];

	unsigned long qsize; /* size of queue in memory (sum of all msgs) */
};

static struct inode_operations mqueue_dir_inode_operations;
static struct file_operations mqueue_file_operations;
static struct super_operations mqueue_super_ops;
static void remove_notification(struct mqueue_inode_info *info);

static spinlock_t mq_lock;
static kmem_cache_t *mqueue_inode_cachep;
static struct vfsmount *mqueue_mnt;

static unsigned int queues_count;
static unsigned int queues_max 	= DFLT_QUEUESMAX;
static unsigned int msg_max 	= DFLT_MSGMAX;
static unsigned int msgsize_max = DFLT_MSGSIZEMAX;

static struct ctl_table_header * mq_sysctl_table;

static inline struct mqueue_inode_info *MQUEUE_I(struct inode *inode)
{
	return container_of(inode, struct mqueue_inode_info, vfs_inode);
}

static struct inode *mqueue_get_inode(struct super_block *sb, int mode,
							struct mq_attr *attr)
{
	struct inode *inode;

	inode = new_inode(sb);
	if (inode) {
		inode->i_mode = mode;
		inode->i_uid = current->fsuid;
		inode->i_gid = current->fsgid;
		inode->i_blksize = PAGE_CACHE_SIZE;
		inode->i_blocks = 0;
		inode->i_mtime = inode->i_ctime = inode->i_atime =
				CURRENT_TIME;

		if (S_ISREG(mode)) {
			struct mqueue_inode_info *info;
			struct task_struct *p = current;
			struct user_struct *u = p->user;
			unsigned long mq_bytes, mq_msg_tblsz;

			inode->i_fop = &mqueue_file_operations;
			inode->i_size = FILENT_SIZE;
			/* mqueue specific info */
			info = MQUEUE_I(inode);
			spin_lock_init(&info->lock);
			init_waitqueue_head(&info->wait_q);
			INIT_LIST_HEAD(&info->e_wait_q[0].list);
			INIT_LIST_HEAD(&info->e_wait_q[1].list);
			info->messages = NULL;
			info->notify_owner = 0;
			info->qsize = 0;
			info->user = NULL;	/* set when all is ok */
			memset(&info->attr, 0, sizeof(info->attr));
			info->attr.mq_maxmsg = DFLT_MSGMAX;
			info->attr.mq_msgsize = DFLT_MSGSIZEMAX;
			if (attr) {
				info->attr.mq_maxmsg = attr->mq_maxmsg;
				info->attr.mq_msgsize = attr->mq_msgsize;
			}
			mq_msg_tblsz = info->attr.mq_maxmsg * sizeof(struct msg_msg *);
			mq_bytes = (mq_msg_tblsz +
				(info->attr.mq_maxmsg * info->attr.mq_msgsize));

			spin_lock(&mq_lock);
			if (u->mq_bytes + mq_bytes < u->mq_bytes ||
		 	    u->mq_bytes + mq_bytes >
			    p->signal->rlim[RLIMIT_MSGQUEUE].rlim_cur) {
				spin_unlock(&mq_lock);
				goto out_inode;
			}
			u->mq_bytes += mq_bytes;
			spin_unlock(&mq_lock);

			info->messages = kmalloc(mq_msg_tblsz, GFP_KERNEL);
			if (!info->messages) {
				spin_lock(&mq_lock);
				u->mq_bytes -= mq_bytes;
				spin_unlock(&mq_lock);
				goto out_inode;
			}
			/* all is ok */
			info->user = get_uid(u);
		} else if (S_ISDIR(mode)) {
			inode->i_nlink++;
			/* Some things misbehave if size == 0 on a directory */
			inode->i_size = 2 * DIRENT_SIZE;
			inode->i_op = &mqueue_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;
		}
	}
	return inode;
out_inode:
	make_bad_inode(inode);
	iput(inode);
	return NULL;
}

static int mqueue_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *inode;

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = MQUEUE_MAGIC;
	sb->s_op = &mqueue_super_ops;

	inode = mqueue_get_inode(sb, S_IFDIR | S_ISVTX | S_IRWXUGO, NULL);
	if (!inode)
		return -ENOMEM;

	sb->s_root = d_alloc_root(inode);
	if (!sb->s_root) {
		iput(inode);
		return -ENOMEM;
	}

	return 0;
}

static struct super_block *mqueue_get_sb(struct file_system_type *fs_type,
					 int flags, const char *dev_name,
					 void *data)
{
	return get_sb_single(fs_type, flags, data, mqueue_fill_super);
}

static void init_once(void *foo, kmem_cache_t * cachep, unsigned long flags)
{
	struct mqueue_inode_info *p = (struct mqueue_inode_info *) foo;

	if ((flags & (SLAB_CTOR_VERIFY | SLAB_CTOR_CONSTRUCTOR)) ==
		SLAB_CTOR_CONSTRUCTOR)
		inode_init_once(&p->vfs_inode);
}

static struct inode *mqueue_alloc_inode(struct super_block *sb)
{
	struct mqueue_inode_info *ei;

	ei = kmem_cache_alloc(mqueue_inode_cachep, SLAB_KERNEL);
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

	if (S_ISDIR(inode->i_mode)) {
		clear_inode(inode);
		return;
	}
	info = MQUEUE_I(inode);
	spin_lock(&info->lock);
	for (i = 0; i < info->attr.mq_curmsgs; i++)
		free_msg(info->messages[i]);
	kfree(info->messages);
	spin_unlock(&info->lock);

	clear_inode(inode);

	mq_bytes = (info->attr.mq_maxmsg * sizeof(struct msg_msg *) +
		   (info->attr.mq_maxmsg * info->attr.mq_msgsize));
	user = info->user;
	if (user) {
		spin_lock(&mq_lock);
		user->mq_bytes -= mq_bytes;
		queues_count--;
		spin_unlock(&mq_lock);
		free_uid(user);
	}
}

static int mqueue_create(struct inode *dir, struct dentry *dentry,
				int mode, struct nameidata *nd)
{
	struct inode *inode;
	struct mq_attr *attr = dentry->d_fsdata;
	int error;

	spin_lock(&mq_lock);
	if (queues_count >= queues_max && !capable(CAP_SYS_RESOURCE)) {
		error = -ENOSPC;
		goto out_lock;
	}
	queues_count++;
	spin_unlock(&mq_lock);

	inode = mqueue_get_inode(dir->i_sb, mode, attr);
	if (!inode) {
		error = -ENOMEM;
		spin_lock(&mq_lock);
		queues_count--;
		goto out_lock;
	}

	dir->i_size += DIRENT_SIZE;
	dir->i_ctime = dir->i_mtime = dir->i_atime = CURRENT_TIME;

	d_instantiate(dentry, inode);
	dget(dentry);
	return 0;
out_lock:
	spin_unlock(&mq_lock);
	return error;
}

static int mqueue_unlink(struct inode *dir, struct dentry *dentry)
{
  	struct inode *inode = dentry->d_inode;

	dir->i_ctime = dir->i_mtime = dir->i_atime = CURRENT_TIME;
	dir->i_size -= DIRENT_SIZE;
  	inode->i_nlink--;
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
				size_t count, loff_t * off)
{
	struct mqueue_inode_info *info = MQUEUE_I(filp->f_dentry->d_inode);
	char buffer[FILENT_SIZE];
	size_t slen;
	loff_t o;

	if (!count)
		return 0;

	spin_lock(&info->lock);
	snprintf(buffer, sizeof(buffer),
			"QSIZE:%-10lu NOTIFY:%-5d SIGNO:%-5d NOTIFY_PID:%-6d\n",
			info->qsize,
			info->notify_owner ? info->notify.sigev_notify : 0,
			(info->notify_owner &&
			 info->notify.sigev_notify == SIGEV_SIGNAL) ?
				info->notify.sigev_signo : 0,
			info->notify_owner);
	spin_unlock(&info->lock);
	buffer[sizeof(buffer)-1] = '\0';
	slen = strlen(buffer)+1;

	o = *off;
	if (o > slen)
		return 0;

	if (o + count > slen)
		count = slen - o;

	if (copy_to_user(u_data, buffer + o, count))
		return -EFAULT;

	*off = o + count;
	filp->f_dentry->d_inode->i_atime = filp->f_dentry->d_inode->i_ctime = CURRENT_TIME;
	return count;
}

static int mqueue_flush_file(struct file *filp)
{
	struct mqueue_inode_info *info = MQUEUE_I(filp->f_dentry->d_inode);

	spin_lock(&info->lock);
	if (current->tgid == info->notify_owner)
		remove_notification(info);

	spin_unlock(&info->lock);
	return 0;
}

static unsigned int mqueue_poll_file(struct file *filp, struct poll_table_struct *poll_tab)
{
	struct mqueue_inode_info *info = MQUEUE_I(filp->f_dentry->d_inode);
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
			long timeout, struct ext_wait_queue *ewp)
{
	int retval;
	signed long time;

	wq_add(info, sr, ewp);

	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);

		spin_unlock(&info->lock);
		time = schedule_timeout(timeout);

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
			sig_i.si_pid = current->tgid;
			sig_i.si_uid = current->uid;

			kill_proc_info(info->notify.sigev_signo,
				       &sig_i, info->notify_owner);
			break;
		case SIGEV_THREAD:
			set_cookie(info->notify_cookie, NOTIFY_WOKENUP);
			netlink_sendskb(info->notify_sock,
					info->notify_cookie, 0);
			break;
		}
		/* after notification unregisters process */
		info->notify_owner = 0;
	}
	wake_up(&info->wait_q);
}

static long prepare_timeout(const struct timespec __user *u_arg)
{
	struct timespec ts, nowts;
	long timeout;

	if (u_arg) {
		if (unlikely(copy_from_user(&ts, u_arg,
					sizeof(struct timespec))))
			return -EFAULT;

		if (unlikely(ts.tv_nsec < 0 || ts.tv_sec < 0
			|| ts.tv_nsec >= NSEC_PER_SEC))
			return -EINVAL;
		nowts = CURRENT_TIME;
		/* first subtract as jiffies can't be too big */
		ts.tv_sec -= nowts.tv_sec;
		if (ts.tv_nsec < nowts.tv_nsec) {
			ts.tv_nsec += NSEC_PER_SEC;
			ts.tv_sec--;
		}
		ts.tv_nsec -= nowts.tv_nsec;
		if (ts.tv_sec < 0)
			return 0;

		timeout = timespec_to_jiffies(&ts) + 1;
	} else
		return MAX_SCHEDULE_TIMEOUT;

	return timeout;
}

static void remove_notification(struct mqueue_inode_info *info)
{
	if (info->notify_owner != 0 &&
	    info->notify.sigev_notify == SIGEV_THREAD) {
		set_cookie(info->notify_cookie, NOTIFY_REMOVED);
		netlink_sendskb(info->notify_sock, info->notify_cookie, 0);
	}
	info->notify_owner = 0;
}

static int mq_attr_ok(struct mq_attr *attr)
{
	if (attr->mq_maxmsg <= 0 || attr->mq_msgsize <= 0)
		return 0;
	if (capable(CAP_SYS_RESOURCE)) {
		if (attr->mq_maxmsg > HARD_MSGMAX)
			return 0;
	} else {
		if (attr->mq_maxmsg > msg_max ||
				attr->mq_msgsize > msgsize_max)
			return 0;
	}
	/* check for overflow */
	if (attr->mq_msgsize > ULONG_MAX/attr->mq_maxmsg)
		return 0;
	if ((unsigned long)(attr->mq_maxmsg * attr->mq_msgsize) +
	    (attr->mq_maxmsg * sizeof (struct msg_msg *)) <
	    (unsigned long)(attr->mq_maxmsg * attr->mq_msgsize))
		return 0;
	return 1;
}

/*
 * Invoked when creating a new queue via sys_mq_open
 */
static struct file *do_create(struct dentry *dir, struct dentry *dentry,
			int oflag, mode_t mode, struct mq_attr __user *u_attr)
{
	struct mq_attr attr;
	int ret;

	if (u_attr) {
		ret = -EFAULT;
		if (copy_from_user(&attr, u_attr, sizeof(attr)))
			goto out;
		ret = -EINVAL;
		if (!mq_attr_ok(&attr))
			goto out;
		/* store for use during create */
		dentry->d_fsdata = &attr;
	}

	mode &= ~current->fs->umask;
	ret = vfs_create(dir->d_inode, dentry, mode, NULL);
	dentry->d_fsdata = NULL;
	if (ret)
		goto out;

	return dentry_open(dentry, mqueue_mnt, oflag);

out:
	dput(dentry);
	mntput(mqueue_mnt);
	return ERR_PTR(ret);
}

/* Opens existing queue */
static struct file *do_open(struct dentry *dentry, int oflag)
{
static int oflag2acc[O_ACCMODE] = { MAY_READ, MAY_WRITE,
					MAY_READ | MAY_WRITE };

	if ((oflag & O_ACCMODE) == (O_RDWR | O_WRONLY)) {
		dput(dentry);
		mntput(mqueue_mnt);
		return ERR_PTR(-EINVAL);
	}

	if (permission(dentry->d_inode, oflag2acc[oflag & O_ACCMODE], NULL)) {
		dput(dentry);
		mntput(mqueue_mnt);
		return ERR_PTR(-EACCES);
	}

	return dentry_open(dentry, mqueue_mnt, oflag);
}

asmlinkage long sys_mq_open(const char __user *u_name, int oflag, mode_t mode,
				struct mq_attr __user *u_attr)
{
	struct dentry *dentry;
	struct file *filp;
	char *name;
	int fd, error;

	if (IS_ERR(name = getname(u_name)))
		return PTR_ERR(name);

	fd = get_unused_fd();
	if (fd < 0)
		goto out_putname;

	mutex_lock(&mqueue_mnt->mnt_root->d_inode->i_mutex);
	dentry = lookup_one_len(name, mqueue_mnt->mnt_root, strlen(name));
	if (IS_ERR(dentry)) {
		error = PTR_ERR(dentry);
		goto out_err;
	}
	mntget(mqueue_mnt);

	if (oflag & O_CREAT) {
		if (dentry->d_inode) {	/* entry already exists */
			error = -EEXIST;
			if (oflag & O_EXCL)
				goto out;
			filp = do_open(dentry, oflag);
		} else {
			filp = do_create(mqueue_mnt->mnt_root, dentry,
						oflag, mode, u_attr);
		}
	} else {
		error = -ENOENT;
		if (!dentry->d_inode)
			goto out;
		filp = do_open(dentry, oflag);
	}

	if (IS_ERR(filp)) {
		error = PTR_ERR(filp);
		goto out_putfd;
	}

	set_close_on_exec(fd, 1);
	fd_install(fd, filp);
	goto out_upsem;

out:
	dput(dentry);
	mntput(mqueue_mnt);
out_putfd:
	put_unused_fd(fd);
out_err:
	fd = error;
out_upsem:
	mutex_unlock(&mqueue_mnt->mnt_root->d_inode->i_mutex);
out_putname:
	putname(name);
	return fd;
}

asmlinkage long sys_mq_unlink(const char __user *u_name)
{
	int err;
	char *name;
	struct dentry *dentry;
	struct inode *inode = NULL;

	name = getname(u_name);
	if (IS_ERR(name))
		return PTR_ERR(name);

	mutex_lock(&mqueue_mnt->mnt_root->d_inode->i_mutex);
	dentry = lookup_one_len(name, mqueue_mnt->mnt_root, strlen(name));
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

	err = vfs_unlink(dentry->d_parent->d_inode, dentry);
out_err:
	dput(dentry);

out_unlock:
	mutex_unlock(&mqueue_mnt->mnt_root->d_inode->i_mutex);
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
 * ipc/sem.c fore more details.
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

asmlinkage long sys_mq_timedsend(mqd_t mqdes, const char __user *u_msg_ptr,
	size_t msg_len, unsigned int msg_prio,
	const struct timespec __user *u_abs_timeout)
{
	struct file *filp;
	struct inode *inode;
	struct ext_wait_queue wait;
	struct ext_wait_queue *receiver;
	struct msg_msg *msg_ptr;
	struct mqueue_inode_info *info;
	long timeout;
	int ret;

	if (unlikely(msg_prio >= (unsigned long) MQ_PRIO_MAX))
		return -EINVAL;

	timeout = prepare_timeout(u_abs_timeout);

	ret = -EBADF;
	filp = fget(mqdes);
	if (unlikely(!filp))
		goto out;

	inode = filp->f_dentry->d_inode;
	if (unlikely(filp->f_op != &mqueue_file_operations))
		goto out_fput;
	info = MQUEUE_I(inode);

	if (unlikely(!(filp->f_mode & FMODE_WRITE)))
		goto out_fput;

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
		} else if (unlikely(timeout < 0)) {
			spin_unlock(&info->lock);
			ret = timeout;
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

asmlinkage ssize_t sys_mq_timedreceive(mqd_t mqdes, char __user *u_msg_ptr,
	size_t msg_len, unsigned int __user *u_msg_prio,
	const struct timespec __user *u_abs_timeout)
{
	long timeout;
	ssize_t ret;
	struct msg_msg *msg_ptr;
	struct file *filp;
	struct inode *inode;
	struct mqueue_inode_info *info;
	struct ext_wait_queue wait;

	timeout = prepare_timeout(u_abs_timeout);

	ret = -EBADF;
	filp = fget(mqdes);
	if (unlikely(!filp))
		goto out;

	inode = filp->f_dentry->d_inode;
	if (unlikely(filp->f_op != &mqueue_file_operations))
		goto out_fput;
	info = MQUEUE_I(inode);

	if (unlikely(!(filp->f_mode & FMODE_READ)))
		goto out_fput;

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
			msg_ptr = NULL;
		} else if (unlikely(timeout < 0)) {
			spin_unlock(&info->lock);
			ret = timeout;
			msg_ptr = NULL;
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
asmlinkage long sys_mq_notify(mqd_t mqdes,
				const struct sigevent __user *u_notification)
{
	int ret;
	struct file *filp;
	struct sock *sock;
	struct inode *inode;
	struct sigevent notification;
	struct mqueue_inode_info *info;
	struct sk_buff *nc;

	nc = NULL;
	sock = NULL;
	if (u_notification != NULL) {
		if (copy_from_user(&notification, u_notification,
					sizeof(struct sigevent)))
			return -EFAULT;

		if (unlikely(notification.sigev_notify != SIGEV_NONE &&
			     notification.sigev_notify != SIGEV_SIGNAL &&
			     notification.sigev_notify != SIGEV_THREAD))
			return -EINVAL;
		if (notification.sigev_notify == SIGEV_SIGNAL &&
			!valid_signal(notification.sigev_signo)) {
			return -EINVAL;
		}
		if (notification.sigev_notify == SIGEV_THREAD) {
			/* create the notify skb */
			nc = alloc_skb(NOTIFY_COOKIE_LEN, GFP_KERNEL);
			ret = -ENOMEM;
			if (!nc)
				goto out;
			ret = -EFAULT;
			if (copy_from_user(nc->data,
					notification.sigev_value.sival_ptr,
					NOTIFY_COOKIE_LEN)) {
				goto out;
			}

			/* TODO: add a header? */
			skb_put(nc, NOTIFY_COOKIE_LEN);
			/* and attach it to the socket */
retry:
			filp = fget(notification.sigev_signo);
			ret = -EBADF;
			if (!filp)
				goto out;
			sock = netlink_getsockbyfilp(filp);
			fput(filp);
			if (IS_ERR(sock)) {
				ret = PTR_ERR(sock);
				sock = NULL;
				goto out;
			}

			ret = netlink_attachskb(sock, nc, 0,
					MAX_SCHEDULE_TIMEOUT, NULL);
			if (ret == 1)
		       		goto retry;
			if (ret) {
				sock = NULL;
				nc = NULL;
				goto out;
			}
		}
	}

	ret = -EBADF;
	filp = fget(mqdes);
	if (!filp)
		goto out;

	inode = filp->f_dentry->d_inode;
	if (unlikely(filp->f_op != &mqueue_file_operations))
		goto out_fput;
	info = MQUEUE_I(inode);

	ret = 0;
	spin_lock(&info->lock);
	if (u_notification == NULL) {
		if (info->notify_owner == current->tgid) {
			remove_notification(info);
			inode->i_atime = inode->i_ctime = CURRENT_TIME;
		}
	} else if (info->notify_owner != 0) {
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
		info->notify_owner = current->tgid;
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

asmlinkage long sys_mq_getsetattr(mqd_t mqdes,
			const struct mq_attr __user *u_mqstat,
			struct mq_attr __user *u_omqstat)
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

	ret = -EBADF;
	filp = fget(mqdes);
	if (!filp)
		goto out;

	inode = filp->f_dentry->d_inode;
	if (unlikely(filp->f_op != &mqueue_file_operations))
		goto out_fput;
	info = MQUEUE_I(inode);

	spin_lock(&info->lock);

	omqstat = info->attr;
	omqstat.mq_flags = filp->f_flags & O_NONBLOCK;
	if (u_mqstat) {
		if (mqstat.mq_flags & O_NONBLOCK)
			filp->f_flags |= O_NONBLOCK;
		else
			filp->f_flags &= ~O_NONBLOCK;

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

static struct inode_operations mqueue_dir_inode_operations = {
	.lookup = simple_lookup,
	.create = mqueue_create,
	.unlink = mqueue_unlink,
};

static struct file_operations mqueue_file_operations = {
	.flush = mqueue_flush_file,
	.poll = mqueue_poll_file,
	.read = mqueue_read_file,
};

static struct super_operations mqueue_super_ops = {
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

static int msg_max_limit_min = DFLT_MSGMAX;
static int msg_max_limit_max = HARD_MSGMAX;

static int msg_maxsize_limit_min = DFLT_MSGSIZEMAX;
static int msg_maxsize_limit_max = INT_MAX;

static ctl_table mq_sysctls[] = {
	{
		.ctl_name	= CTL_QUEUESMAX,
		.procname	= "queues_max",
		.data		= &queues_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= CTL_MSGMAX,
		.procname	= "msg_max",
		.data		= &msg_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.extra1		= &msg_max_limit_min,
		.extra2		= &msg_max_limit_max,
	},
	{
		.ctl_name	= CTL_MSGSIZEMAX,
		.procname	= "msgsize_max",
		.data		= &msgsize_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.extra1		= &msg_maxsize_limit_min,
		.extra2		= &msg_maxsize_limit_max,
	},
	{ .ctl_name = 0 }
};

static ctl_table mq_sysctl_dir[] = {
	{
		.ctl_name	= FS_MQUEUE,
		.procname	= "mqueue",
		.mode		= 0555,
		.child		= mq_sysctls,
	},
	{ .ctl_name = 0 }
};

static ctl_table mq_sysctl_root[] = {
	{
		.ctl_name	= CTL_FS,
		.procname	= "fs",
		.mode		= 0555,
		.child		= mq_sysctl_dir,
	},
	{ .ctl_name = 0 }
};

static int __init init_mqueue_fs(void)
{
	int error;

	mqueue_inode_cachep = kmem_cache_create("mqueue_inode_cache",
				sizeof(struct mqueue_inode_info), 0,
				SLAB_HWCACHE_ALIGN, init_once, NULL);
	if (mqueue_inode_cachep == NULL)
		return -ENOMEM;

	/* ignore failues - they are not fatal */
	mq_sysctl_table = register_sysctl_table(mq_sysctl_root, 0);

	error = register_filesystem(&mqueue_fs_type);
	if (error)
		goto out_sysctl;

	if (IS_ERR(mqueue_mnt = kern_mount(&mqueue_fs_type))) {
		error = PTR_ERR(mqueue_mnt);
		goto out_filesystem;
	}

	/* internal initialization - not common for vfs */
	queues_count = 0;
	spin_lock_init(&mq_lock);

	return 0;

out_filesystem:
	unregister_filesystem(&mqueue_fs_type);
out_sysctl:
	if (mq_sysctl_table)
		unregister_sysctl_table(mq_sysctl_table);
	if (kmem_cache_destroy(mqueue_inode_cachep)) {
		printk(KERN_INFO
			"mqueue_inode_cache: not all structures were freed\n");
	}
	return error;
}

__initcall(init_mqueue_fs);
