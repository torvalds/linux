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
#include <linux/vmalloc.h>
#include <linux/netlink.h>
#include <linux/syscalls.h>
#include <linux/audit.h>
#include <linux/signal.h>
#include <linux/mutex.h>
#include <linux/nsproxy.h>
#include <linux/pid.h>
#include <linux/ipc_namespace.h>
#include <linux/user_namespace.h>
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

struct posix_msg_tree_node {
	struct rb_node		rb_node;
	struct list_head	msg_list;
	int			priority;
};

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

	struct rb_root msg_tree;
	struct posix_msg_tree_node *node_cache;
	struct mq_attr attr;

	struct sigevent notify;
	struct pid* notify_owner;
	struct user_namespace *notify_user_ns;
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

/* Auxiliary functions to manipulate messages' list */
static int msg_insert(struct msg_msg *msg, struct mqueue_inode_info *info)
{
	struct rb_node **p, *parent = NULL;
	struct posix_msg_tree_node *leaf;

	p = &info->msg_tree.rb_node;
	while (*p) {
		parent = *p;
		leaf = rb_entry(parent, struct posix_msg_tree_node, rb_node);

		if (likely(leaf->priority == msg->m_type))
			goto insert_msg;
		else if (msg->m_type < leaf->priority)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}
	if (info->node_cache) {
		leaf = info->node_cache;
		info->node_cache = NULL;
	} else {
		leaf = kmalloc(sizeof(*leaf), GFP_ATOMIC);
		if (!leaf)
			return -ENOMEM;
		INIT_LIST_HEAD(&leaf->msg_list);
		info->qsize += sizeof(*leaf);
	}
	leaf->priority = msg->m_type;
	rb_link_node(&leaf->rb_node, parent, p);
	rb_insert_color(&leaf->rb_node, &info->msg_tree);
insert_msg:
	info->attr.mq_curmsgs++;
	info->qsize += msg->m_ts;
	list_add_tail(&msg->m_list, &leaf->msg_list);
	return 0;
}

static inline struct msg_msg *msg_get(struct mqueue_inode_info *info)
{
	struct rb_node **p, *parent = NULL;
	struct posix_msg_tree_node *leaf;
	struct msg_msg *msg;

try_again:
	p = &info->msg_tree.rb_node;
	while (*p) {
		parent = *p;
		/*
		 * During insert, low priorities go to the left and high to the
		 * right.  On receive, we want the highest priorities first, so
		 * walk all the way to the right.
		 */
		p = &(*p)->rb_right;
	}
	if (!parent) {
		if (info->attr.mq_curmsgs) {
			pr_warn_once("Inconsistency in POSIX message queue, "
				     "no tree element, but supposedly messages "
				     "should exist!\n");
			info->attr.mq_curmsgs = 0;
		}
		return NULL;
	}
	leaf = rb_entry(parent, struct posix_msg_tree_node, rb_node);
	if (unlikely(list_empty(&leaf->msg_list))) {
		pr_warn_once("Inconsistency in POSIX message queue, "
			     "empty leaf node but we haven't implemented "
			     "lazy leaf delete!\n");
		rb_erase(&leaf->rb_node, &info->msg_tree);
		if (info->node_cache) {
			info->qsize -= sizeof(*leaf);
			kfree(leaf);
		} else {
			info->node_cache = leaf;
		}
		goto try_again;
	} else {
		msg = list_first_entry(&leaf->msg_list,
				       struct msg_msg, m_list);
		list_del(&msg->m_list);
		if (list_empty(&leaf->msg_list)) {
			rb_erase(&leaf->rb_node, &info->msg_tree);
			if (info->node_cache) {
				info->qsize -= sizeof(*leaf);
				kfree(leaf);
			} else {
				info->node_cache = leaf;
			}
		}
	}
	info->attr.mq_curmsgs--;
	info->qsize -= msg->m_ts;
	return msg;
}

static struct inode *mqueue_get_inode(struct super_block *sb,
		struct ipc_namespace *ipc_ns, umode_t mode,
		struct mq_attr *attr)
{
	struct user_struct *u = current_user();
	struct inode *inode;
	int ret = -ENOMEM;

	inode = new_inode(sb);
	if (!inode)
		goto err;

	inode->i_ino = get_next_ino();
	inode->i_mode = mode;
	inode->i_uid = current_fsuid();
	inode->i_gid = current_fsgid();
	inode->i_mtime = inode->i_ctime = inode->i_atime = CURRENT_TIME;

	if (S_ISREG(mode)) {
		struct mqueue_inode_info *info;
		unsigned long mq_bytes, mq_treesize;

		inode->i_fop = &mqueue_file_operations;
		inode->i_size = FILENT_SIZE;
		/* mqueue specific info */
		info = MQUEUE_I(inode);
		spin_lock_init(&info->lock);
		init_waitqueue_head(&info->wait_q);
		INIT_LIST_HEAD(&info->e_wait_q[0].list);
		INIT_LIST_HEAD(&info->e_wait_q[1].list);
		info->notify_owner = NULL;
		info->notify_user_ns = NULL;
		info->qsize = 0;
		info->user = NULL;	/* set when all is ok */
		info->msg_tree = RB_ROOT;
		info->node_cache = NULL;
		memset(&info->attr, 0, sizeof(info->attr));
		info->attr.mq_maxmsg = min(ipc_ns->mq_msg_max,
					   ipc_ns->mq_msg_default);
		info->attr.mq_msgsize = min(ipc_ns->mq_msgsize_max,
					    ipc_ns->mq_msgsize_default);
		if (attr) {
			info->attr.mq_maxmsg = attr->mq_maxmsg;
			info->attr.mq_msgsize = attr->mq_msgsize;
		}
		/*
		 * We used to allocate a static array of pointers and account
		 * the size of that array as well as one msg_msg struct per
		 * possible message into the queue size. That's no longer
		 * accurate as the queue is now an rbtree and will grow and
		 * shrink depending on usage patterns.  We can, however, still
		 * account one msg_msg struct per message, but the nodes are
		 * allocated depending on priority usage, and most programs
		 * only use one, or a handful, of priorities.  However, since
		 * this is pinned memory, we need to assume worst case, so
		 * that means the min(mq_maxmsg, max_priorities) * struct
		 * posix_msg_tree_node.
		 */
		mq_treesize = info->attr.mq_maxmsg * sizeof(struct msg_msg) +
			min_t(unsigned int, info->attr.mq_maxmsg, MQ_PRIO_MAX) *
			sizeof(struct posix_msg_tree_node);

		mq_bytes = mq_treesize + (info->attr.mq_maxmsg *
					  info->attr.mq_msgsize);

		spin_lock(&mq_lock);
		if (u->mq_bytes + mq_bytes < u->mq_bytes ||
		    u->mq_bytes + mq_bytes > rlimit(RLIMIT_MSGQUEUE)) {
			spin_unlock(&mq_lock);
			/* mqueue_evict_inode() releases info->messages */
			ret = -EMFILE;
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

	return inode;
out_inode:
	iput(inode);
err:
	return ERR_PTR(ret);
}

static int mqueue_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *inode;
	struct ipc_namespace *ns = data;

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = MQUEUE_MAGIC;
	sb->s_op = &mqueue_super_ops;

	inode = mqueue_get_inode(sb, ns, S_IFDIR | S_ISVTX | S_IRWXUGO, NULL);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
		return -ENOMEM;
	return 0;
}

static struct dentry *mqueue_mount(struct file_system_type *fs_type,
			 int flags, const char *dev_name,
			 void *data)
{
	if (!(flags & MS_KERNMOUNT)) {
		struct ipc_namespace *ns = current->nsproxy->ipc_ns;
		/* Don't allow mounting unless the caller has CAP_SYS_ADMIN
		 * over the ipc namespace.
		 */
		if (!ns_capable(ns->user_ns, CAP_SYS_ADMIN))
			return ERR_PTR(-EPERM);

		data = ns;
	}
	return mount_ns(fs_type, flags, data, mqueue_fill_super);
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

static void mqueue_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	kmem_cache_free(mqueue_inode_cachep, MQUEUE_I(inode));
}

static void mqueue_destroy_inode(struct inode *inode)
{
	call_rcu(&inode->i_rcu, mqueue_i_callback);
}

static void mqueue_evict_inode(struct inode *inode)
{
	struct mqueue_inode_info *info;
	struct user_struct *user;
	unsigned long mq_bytes, mq_treesize;
	struct ipc_namespace *ipc_ns;
	struct msg_msg *msg;

	clear_inode(inode);

	if (S_ISDIR(inode->i_mode))
		return;

	ipc_ns = get_ns_from_inode(inode);
	info = MQUEUE_I(inode);
	spin_lock(&info->lock);
	while ((msg = msg_get(info)) != NULL)
		free_msg(msg);
	kfree(info->node_cache);
	spin_unlock(&info->lock);

	/* Total amount of bytes accounted for the mqueue */
	mq_treesize = info->attr.mq_maxmsg * sizeof(struct msg_msg) +
		min_t(unsigned int, info->attr.mq_maxmsg, MQ_PRIO_MAX) *
		sizeof(struct posix_msg_tree_node);

	mq_bytes = mq_treesize + (info->attr.mq_maxmsg *
				  info->attr.mq_msgsize);

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
				umode_t mode, bool excl)
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
	if (ipc_ns->mq_queues_count >= HARD_QUEUESMAX ||
	    (ipc_ns->mq_queues_count >= ipc_ns->mq_queues_max &&
	     !capable(CAP_SYS_RESOURCE))) {
		error = -ENOSPC;
		goto out_unlock;
	}
	ipc_ns->mq_queues_count++;
	spin_unlock(&mq_lock);

	inode = mqueue_get_inode(dir->i_sb, ipc_ns, mode, attr);
	if (IS_ERR(inode)) {
		error = PTR_ERR(inode);
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
	struct mqueue_inode_info *info = MQUEUE_I(file_inode(filp));
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

	file_inode(filp)->i_atime = file_inode(filp)->i_ctime = CURRENT_TIME;
	return ret;
}

static int mqueue_flush_file(struct file *filp, fl_owner_t id)
{
	struct mqueue_inode_info *info = MQUEUE_I(file_inode(filp));

	spin_lock(&info->lock);
	if (task_tgid(current) == info->notify_owner)
		remove_notification(info);

	spin_unlock(&info->lock);
	return 0;
}

static unsigned int mqueue_poll_file(struct file *filp, struct poll_table_struct *poll_tab)
{
	struct mqueue_inode_info *info = MQUEUE_I(file_inode(filp));
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
		time = schedule_hrtimeout_range_clock(timeout, 0,
			HRTIMER_MODE_ABS, CLOCK_REALTIME);

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
			/* map current pid/uid into info->owner's namespaces */
			rcu_read_lock();
			sig_i.si_pid = task_tgid_nr_ns(current,
						ns_of_pid(info->notify_owner));
			sig_i.si_uid = from_kuid_munged(info->notify_user_ns, current_uid());
			rcu_read_unlock();

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
		put_user_ns(info->notify_user_ns);
		info->notify_owner = NULL;
		info->notify_user_ns = NULL;
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
	put_user_ns(info->notify_user_ns);
	info->notify_owner = NULL;
	info->notify_user_ns = NULL;
}

static int mq_attr_ok(struct ipc_namespace *ipc_ns, struct mq_attr *attr)
{
	int mq_treesize;
	unsigned long total_size;

	if (attr->mq_maxmsg <= 0 || attr->mq_msgsize <= 0)
		return -EINVAL;
	if (capable(CAP_SYS_RESOURCE)) {
		if (attr->mq_maxmsg > HARD_MSGMAX ||
		    attr->mq_msgsize > HARD_MSGSIZEMAX)
			return -EINVAL;
	} else {
		if (attr->mq_maxmsg > ipc_ns->mq_msg_max ||
				attr->mq_msgsize > ipc_ns->mq_msgsize_max)
			return -EINVAL;
	}
	/* check for overflow */
	if (attr->mq_msgsize > ULONG_MAX/attr->mq_maxmsg)
		return -EOVERFLOW;
	mq_treesize = attr->mq_maxmsg * sizeof(struct msg_msg) +
		min_t(unsigned int, attr->mq_maxmsg, MQ_PRIO_MAX) *
		sizeof(struct posix_msg_tree_node);
	total_size = attr->mq_maxmsg * attr->mq_msgsize;
	if (total_size + mq_treesize < total_size)
		return -EOVERFLOW;
	return 0;
}

/*
 * Invoked when creating a new queue via sys_mq_open
 */
static struct file *do_create(struct ipc_namespace *ipc_ns, struct inode *dir,
			struct path *path, int oflag, umode_t mode,
			struct mq_attr *attr)
{
	const struct cred *cred = current_cred();
	int ret;

	if (attr) {
		ret = mq_attr_ok(ipc_ns, attr);
		if (ret)
			return ERR_PTR(ret);
		/* store for use during create */
		path->dentry->d_fsdata = attr;
	} else {
		struct mq_attr def_attr;

		def_attr.mq_maxmsg = min(ipc_ns->mq_msg_max,
					 ipc_ns->mq_msg_default);
		def_attr.mq_msgsize = min(ipc_ns->mq_msgsize_max,
					  ipc_ns->mq_msgsize_default);
		ret = mq_attr_ok(ipc_ns, &def_attr);
		if (ret)
			return ERR_PTR(ret);
	}

	mode &= ~current_umask();
	ret = vfs_create(dir, path->dentry, mode, true);
	path->dentry->d_fsdata = NULL;
	if (ret)
		return ERR_PTR(ret);
	return dentry_open(path, oflag, cred);
}

/* Opens existing queue */
static struct file *do_open(struct path *path, int oflag)
{
	static const int oflag2acc[O_ACCMODE] = { MAY_READ, MAY_WRITE,
						  MAY_READ | MAY_WRITE };
	int acc;
	if ((oflag & O_ACCMODE) == (O_RDWR | O_WRONLY))
		return ERR_PTR(-EINVAL);
	acc = oflag2acc[oflag & O_ACCMODE];
	if (inode_permission(path->dentry->d_inode, acc))
		return ERR_PTR(-EACCES);
	return dentry_open(path, oflag, current_cred());
}

SYSCALL_DEFINE4(mq_open, const char __user *, u_name, int, oflag, umode_t, mode,
		struct mq_attr __user *, u_attr)
{
	struct path path;
	struct file *filp;
	struct filename *name;
	struct mq_attr attr;
	int fd, error;
	struct ipc_namespace *ipc_ns = current->nsproxy->ipc_ns;
	struct vfsmount *mnt = ipc_ns->mq_mnt;
	struct dentry *root = mnt->mnt_root;
	int ro;

	if (u_attr && copy_from_user(&attr, u_attr, sizeof(struct mq_attr)))
		return -EFAULT;

	audit_mq_open(oflag, mode, u_attr ? &attr : NULL);

	if (IS_ERR(name = getname(u_name)))
		return PTR_ERR(name);

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0)
		goto out_putname;

	ro = mnt_want_write(mnt);	/* we'll drop it in any case */
	error = 0;
	mutex_lock(&root->d_inode->i_mutex);
	path.dentry = lookup_one_len(name->name, root, strlen(name->name));
	if (IS_ERR(path.dentry)) {
		error = PTR_ERR(path.dentry);
		goto out_putfd;
	}
	path.mnt = mntget(mnt);

	if (oflag & O_CREAT) {
		if (path.dentry->d_inode) {	/* entry already exists */
			audit_inode(name, path.dentry, 0);
			if (oflag & O_EXCL) {
				error = -EEXIST;
				goto out;
			}
			filp = do_open(&path, oflag);
		} else {
			if (ro) {
				error = ro;
				goto out;
			}
			filp = do_create(ipc_ns, root->d_inode,
						&path, oflag, mode,
						u_attr ? &attr : NULL);
		}
	} else {
		if (!path.dentry->d_inode) {
			error = -ENOENT;
			goto out;
		}
		audit_inode(name, path.dentry, 0);
		filp = do_open(&path, oflag);
	}

	if (!IS_ERR(filp))
		fd_install(fd, filp);
	else
		error = PTR_ERR(filp);
out:
	path_put(&path);
out_putfd:
	if (error) {
		put_unused_fd(fd);
		fd = error;
	}
	mutex_unlock(&root->d_inode->i_mutex);
	if (!ro)
		mnt_drop_write(mnt);
out_putname:
	putname(name);
	return fd;
}

SYSCALL_DEFINE1(mq_unlink, const char __user *, u_name)
{
	int err;
	struct filename *name;
	struct dentry *dentry;
	struct inode *inode = NULL;
	struct ipc_namespace *ipc_ns = current->nsproxy->ipc_ns;
	struct vfsmount *mnt = ipc_ns->mq_mnt;

	name = getname(u_name);
	if (IS_ERR(name))
		return PTR_ERR(name);

	err = mnt_want_write(mnt);
	if (err)
		goto out_name;
	mutex_lock_nested(&mnt->mnt_root->d_inode->i_mutex, I_MUTEX_PARENT);
	dentry = lookup_one_len(name->name, mnt->mnt_root,
				strlen(name->name));
	if (IS_ERR(dentry)) {
		err = PTR_ERR(dentry);
		goto out_unlock;
	}

	inode = dentry->d_inode;
	if (!inode) {
		err = -ENOENT;
	} else {
		ihold(inode);
		err = vfs_unlink(dentry->d_parent->d_inode, dentry);
	}
	dput(dentry);

out_unlock:
	mutex_unlock(&mnt->mnt_root->d_inode->i_mutex);
	if (inode)
		iput(inode);
	mnt_drop_write(mnt);
out_name:
	putname(name);

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
	if (msg_insert(sender->msg, info))
		return;
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
	struct fd f;
	struct inode *inode;
	struct ext_wait_queue wait;
	struct ext_wait_queue *receiver;
	struct msg_msg *msg_ptr;
	struct mqueue_inode_info *info;
	ktime_t expires, *timeout = NULL;
	struct timespec ts;
	struct posix_msg_tree_node *new_leaf = NULL;
	int ret = 0;

	if (u_abs_timeout) {
		int res = prepare_timeout(u_abs_timeout, &expires, &ts);
		if (res)
			return res;
		timeout = &expires;
	}

	if (unlikely(msg_prio >= (unsigned long) MQ_PRIO_MAX))
		return -EINVAL;

	audit_mq_sendrecv(mqdes, msg_len, msg_prio, timeout ? &ts : NULL);

	f = fdget(mqdes);
	if (unlikely(!f.file)) {
		ret = -EBADF;
		goto out;
	}

	inode = file_inode(f.file);
	if (unlikely(f.file->f_op != &mqueue_file_operations)) {
		ret = -EBADF;
		goto out_fput;
	}
	info = MQUEUE_I(inode);
	audit_inode(NULL, f.file->f_path.dentry, 0);

	if (unlikely(!(f.file->f_mode & FMODE_WRITE))) {
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

	/*
	 * msg_insert really wants us to have a valid, spare node struct so
	 * it doesn't have to kmalloc a GFP_ATOMIC allocation, but it will
	 * fall back to that if necessary.
	 */
	if (!info->node_cache)
		new_leaf = kmalloc(sizeof(*new_leaf), GFP_KERNEL);

	spin_lock(&info->lock);

	if (!info->node_cache && new_leaf) {
		/* Save our speculative allocation into the cache */
		INIT_LIST_HEAD(&new_leaf->msg_list);
		info->node_cache = new_leaf;
		info->qsize += sizeof(*new_leaf);
		new_leaf = NULL;
	} else {
		kfree(new_leaf);
	}

	if (info->attr.mq_curmsgs == info->attr.mq_maxmsg) {
		if (f.file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
		} else {
			wait.task = current;
			wait.msg = (void *) msg_ptr;
			wait.state = STATE_NONE;
			ret = wq_sleep(info, SEND, timeout, &wait);
			/*
			 * wq_sleep must be called with info->lock held, and
			 * returns with the lock released
			 */
			goto out_free;
		}
	} else {
		receiver = wq_get_first_waiter(info, RECV);
		if (receiver) {
			pipelined_send(info, msg_ptr, receiver);
		} else {
			/* adds message to the queue */
			ret = msg_insert(msg_ptr, info);
			if (ret)
				goto out_unlock;
			__do_notify(info);
		}
		inode->i_atime = inode->i_mtime = inode->i_ctime =
				CURRENT_TIME;
	}
out_unlock:
	spin_unlock(&info->lock);
out_free:
	if (ret)
		free_msg(msg_ptr);
out_fput:
	fdput(f);
out:
	return ret;
}

SYSCALL_DEFINE5(mq_timedreceive, mqd_t, mqdes, char __user *, u_msg_ptr,
		size_t, msg_len, unsigned int __user *, u_msg_prio,
		const struct timespec __user *, u_abs_timeout)
{
	ssize_t ret;
	struct msg_msg *msg_ptr;
	struct fd f;
	struct inode *inode;
	struct mqueue_inode_info *info;
	struct ext_wait_queue wait;
	ktime_t expires, *timeout = NULL;
	struct timespec ts;
	struct posix_msg_tree_node *new_leaf = NULL;

	if (u_abs_timeout) {
		int res = prepare_timeout(u_abs_timeout, &expires, &ts);
		if (res)
			return res;
		timeout = &expires;
	}

	audit_mq_sendrecv(mqdes, msg_len, 0, timeout ? &ts : NULL);

	f = fdget(mqdes);
	if (unlikely(!f.file)) {
		ret = -EBADF;
		goto out;
	}

	inode = file_inode(f.file);
	if (unlikely(f.file->f_op != &mqueue_file_operations)) {
		ret = -EBADF;
		goto out_fput;
	}
	info = MQUEUE_I(inode);
	audit_inode(NULL, f.file->f_path.dentry, 0);

	if (unlikely(!(f.file->f_mode & FMODE_READ))) {
		ret = -EBADF;
		goto out_fput;
	}

	/* checks if buffer is big enough */
	if (unlikely(msg_len < info->attr.mq_msgsize)) {
		ret = -EMSGSIZE;
		goto out_fput;
	}

	/*
	 * msg_insert really wants us to have a valid, spare node struct so
	 * it doesn't have to kmalloc a GFP_ATOMIC allocation, but it will
	 * fall back to that if necessary.
	 */
	if (!info->node_cache)
		new_leaf = kmalloc(sizeof(*new_leaf), GFP_KERNEL);

	spin_lock(&info->lock);

	if (!info->node_cache && new_leaf) {
		/* Save our speculative allocation into the cache */
		INIT_LIST_HEAD(&new_leaf->msg_list);
		info->node_cache = new_leaf;
		info->qsize += sizeof(*new_leaf);
	} else {
		kfree(new_leaf);
	}

	if (info->attr.mq_curmsgs == 0) {
		if (f.file->f_flags & O_NONBLOCK) {
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
	fdput(f);
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
	struct fd f;
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
			f = fdget(notification.sigev_signo);
			if (!f.file) {
				ret = -EBADF;
				goto out;
			}
			sock = netlink_getsockbyfilp(f.file);
			fdput(f);
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

	f = fdget(mqdes);
	if (!f.file) {
		ret = -EBADF;
		goto out;
	}

	inode = file_inode(f.file);
	if (unlikely(f.file->f_op != &mqueue_file_operations)) {
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
		info->notify_user_ns = get_user_ns(current_user_ns());
		inode->i_atime = inode->i_ctime = CURRENT_TIME;
	}
	spin_unlock(&info->lock);
out_fput:
	fdput(f);
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
	struct fd f;
	struct inode *inode;
	struct mqueue_inode_info *info;

	if (u_mqstat != NULL) {
		if (copy_from_user(&mqstat, u_mqstat, sizeof(struct mq_attr)))
			return -EFAULT;
		if (mqstat.mq_flags & (~O_NONBLOCK))
			return -EINVAL;
	}

	f = fdget(mqdes);
	if (!f.file) {
		ret = -EBADF;
		goto out;
	}

	inode = file_inode(f.file);
	if (unlikely(f.file->f_op != &mqueue_file_operations)) {
		ret = -EBADF;
		goto out_fput;
	}
	info = MQUEUE_I(inode);

	spin_lock(&info->lock);

	omqstat = info->attr;
	omqstat.mq_flags = f.file->f_flags & O_NONBLOCK;
	if (u_mqstat) {
		audit_mq_getsetattr(mqdes, &mqstat);
		spin_lock(&f.file->f_lock);
		if (mqstat.mq_flags & O_NONBLOCK)
			f.file->f_flags |= O_NONBLOCK;
		else
			f.file->f_flags &= ~O_NONBLOCK;
		spin_unlock(&f.file->f_lock);

		inode->i_atime = inode->i_ctime = CURRENT_TIME;
	}

	spin_unlock(&info->lock);

	ret = 0;
	if (u_omqstat != NULL && copy_to_user(u_omqstat, &omqstat,
						sizeof(struct mq_attr)))
		ret = -EFAULT;

out_fput:
	fdput(f);
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
	.llseek = default_llseek,
};

static const struct super_operations mqueue_super_ops = {
	.alloc_inode = mqueue_alloc_inode,
	.destroy_inode = mqueue_destroy_inode,
	.evict_inode = mqueue_evict_inode,
	.statfs = simple_statfs,
};

static struct file_system_type mqueue_fs_type = {
	.name = "mqueue",
	.mount = mqueue_mount,
	.kill_sb = kill_litter_super,
	.fs_flags = FS_USERNS_MOUNT,
};

int mq_init_ns(struct ipc_namespace *ns)
{
	ns->mq_queues_count  = 0;
	ns->mq_queues_max    = DFLT_QUEUESMAX;
	ns->mq_msg_max       = DFLT_MSGMAX;
	ns->mq_msgsize_max   = DFLT_MSGSIZEMAX;
	ns->mq_msg_default   = DFLT_MSG;
	ns->mq_msgsize_default  = DFLT_MSGSIZE;

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
	kern_unmount(ns->mq_mnt);
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

	error = mq_init_ns(&init_ipc_ns);
	if (error)
		goto out_filesystem;

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
