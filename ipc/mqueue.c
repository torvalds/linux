/*
 * POSIX message queues filesystem for Linux.
 *
 * Copyright (C) 2003,2004  Krzysztof Benedyczak    (golbi@mat.uni.torun.pl)
 *                          Michal Wronski          (michal.wronski@gmail.com)
 *
 * Spinlocks:               Mohamed Abbas           (abbas.mohamed@intel.com)
 * Lockless receive & send, fd based notify:
 *			    Manfred Spraul	    (manfred@colorfullife.com)
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
#include <linux/fs_context.h>
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
#include <linux/sched/wake_q.h>
#include <linux/sched/signal.h>
#include <linux/sched/user.h>

#include <net/sock.h>
#include "util.h"

struct mqueue_fs_context {
	struct ipc_namespace	*ipc_ns;
	bool			 newns;	/* Set if newly created ipc namespace */
};

#define MQUEUE_MAGIC	0x19800202
#define DIRENT_SIZE	20
#define FILENT_SIZE	80

#define SEND		0
#define RECV		1

#define STATE_NONE	0
#define STATE_READY	1

struct posix_msg_tree_node {
	struct rb_node		rb_node;
	struct list_head	msg_list;
	int			priority;
};

/*
 * Locking:
 *
 * Accesses to a message queue are synchronized by acquiring info->lock.
 *
 * There are two notable exceptions:
 * - The actual wakeup of a sleeping task is performed using the wake_q
 *   framework. info->lock is already released when wake_up_q is called.
 * - The exit codepaths after sleeping check ext_wait_queue->state without
 *   any locks. If it is STATE_READY, then the syscall is completed without
 *   acquiring info->lock.
 *
 * MQ_BARRIER:
 * To achieve proper release/acquire memory barrier pairing, the state is set to
 * STATE_READY with smp_store_release(), and it is read with READ_ONCE followed
 * by smp_acquire__after_ctrl_dep(). In addition, wake_q_add_safe() is used.
 *
 * This prevents the following races:
 *
 * 1) With the simple wake_q_add(), the task could be gone already before
 *    the increase of the reference happens
 * Thread A
 *				Thread B
 * WRITE_ONCE(wait.state, STATE_NONE);
 * schedule_hrtimeout()
 *				wake_q_add(A)
 *				if (cmpxchg()) // success
 *				   ->state = STATE_READY (reordered)
 * <timeout returns>
 * if (wait.state == STATE_READY) return;
 * sysret to user space
 * sys_exit()
 *				get_task_struct() // UaF
 *
 * Solution: Use wake_q_add_safe() and perform the get_task_struct() before
 * the smp_store_release() that does ->state = STATE_READY.
 *
 * 2) Without proper _release/_acquire barriers, the woken up task
 *    could read stale data
 *
 * Thread A
 *				Thread B
 * do_mq_timedreceive
 * WRITE_ONCE(wait.state, STATE_NONE);
 * schedule_hrtimeout()
 *				state = STATE_READY;
 * <timeout returns>
 * if (wait.state == STATE_READY) return;
 * msg_ptr = wait.msg;		// Access to stale data!
 *				receiver->msg = message; (reordered)
 *
 * Solution: use _release and _acquire barriers.
 *
 * 3) There is intentionally no barrier when setting current->state
 *    to TASK_INTERRUPTIBLE: spin_unlock(&info->lock) provides the
 *    release memory barrier, and the wakeup is triggered when holding
 *    info->lock, i.e. spin_lock(&info->lock) provided a pairing
 *    acquire memory barrier.
 */

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
	struct rb_node *msg_tree_rightmost;
	struct posix_msg_tree_node *node_cache;
	struct mq_attr attr;

	struct sigevent notify;
	struct pid *notify_owner;
	u32 notify_self_exec_id;
	struct user_namespace *notify_user_ns;
	struct ucounts *ucounts;	/* user who created, for accounting */
	struct sock *notify_sock;
	struct sk_buff *notify_cookie;

	/* for tasks waiting for free space and messages, respectively */
	struct ext_wait_queue e_wait_q[2];

	unsigned long qsize; /* size of queue in memory (sum of all msgs) */
};

static struct file_system_type mqueue_fs_type;
static const struct inode_operations mqueue_dir_inode_operations;
static const struct file_operations mqueue_file_operations;
static const struct super_operations mqueue_super_ops;
static const struct fs_context_operations mqueue_fs_context_ops;
static void remove_notification(struct mqueue_inode_info *info);

static struct kmem_cache *mqueue_inode_cachep;

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
	bool rightmost = true;

	p = &info->msg_tree.rb_node;
	while (*p) {
		parent = *p;
		leaf = rb_entry(parent, struct posix_msg_tree_node, rb_node);

		if (likely(leaf->priority == msg->m_type))
			goto insert_msg;
		else if (msg->m_type < leaf->priority) {
			p = &(*p)->rb_left;
			rightmost = false;
		} else
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
	}
	leaf->priority = msg->m_type;

	if (rightmost)
		info->msg_tree_rightmost = &leaf->rb_node;

	rb_link_node(&leaf->rb_node, parent, p);
	rb_insert_color(&leaf->rb_node, &info->msg_tree);
insert_msg:
	info->attr.mq_curmsgs++;
	info->qsize += msg->m_ts;
	list_add_tail(&msg->m_list, &leaf->msg_list);
	return 0;
}

static inline void msg_tree_erase(struct posix_msg_tree_node *leaf,
				  struct mqueue_inode_info *info)
{
	struct rb_node *node = &leaf->rb_node;

	if (info->msg_tree_rightmost == node)
		info->msg_tree_rightmost = rb_prev(node);

	rb_erase(node, &info->msg_tree);
	if (info->node_cache)
		kfree(leaf);
	else
		info->node_cache = leaf;
}

static inline struct msg_msg *msg_get(struct mqueue_inode_info *info)
{
	struct rb_node *parent = NULL;
	struct posix_msg_tree_node *leaf;
	struct msg_msg *msg;

try_again:
	/*
	 * During insert, low priorities go to the left and high to the
	 * right.  On receive, we want the highest priorities first, so
	 * walk all the way to the right.
	 */
	parent = info->msg_tree_rightmost;
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
		msg_tree_erase(leaf, info);
		goto try_again;
	} else {
		msg = list_first_entry(&leaf->msg_list,
				       struct msg_msg, m_list);
		list_del(&msg->m_list);
		if (list_empty(&leaf->msg_list)) {
			msg_tree_erase(leaf, info);
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
	struct inode *inode;
	int ret = -ENOMEM;

	inode = new_inode(sb);
	if (!inode)
		goto err;

	inode->i_ino = get_next_ino();
	inode->i_mode = mode;
	inode->i_uid = current_fsuid();
	inode->i_gid = current_fsgid();
	simple_inode_init_ts(inode);

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
		info->ucounts = NULL;	/* set when all is ok */
		info->msg_tree = RB_ROOT;
		info->msg_tree_rightmost = NULL;
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

		ret = -EINVAL;
		if (info->attr.mq_maxmsg <= 0 || info->attr.mq_msgsize <= 0)
			goto out_inode;
		if (capable(CAP_SYS_RESOURCE)) {
			if (info->attr.mq_maxmsg > HARD_MSGMAX ||
			    info->attr.mq_msgsize > HARD_MSGSIZEMAX)
				goto out_inode;
		} else {
			if (info->attr.mq_maxmsg > ipc_ns->mq_msg_max ||
					info->attr.mq_msgsize > ipc_ns->mq_msgsize_max)
				goto out_inode;
		}
		ret = -EOVERFLOW;
		/* check for overflow */
		if (info->attr.mq_msgsize > ULONG_MAX/info->attr.mq_maxmsg)
			goto out_inode;
		mq_treesize = info->attr.mq_maxmsg * sizeof(struct msg_msg) +
			min_t(unsigned int, info->attr.mq_maxmsg, MQ_PRIO_MAX) *
			sizeof(struct posix_msg_tree_node);
		mq_bytes = info->attr.mq_maxmsg * info->attr.mq_msgsize;
		if (mq_bytes + mq_treesize < mq_bytes)
			goto out_inode;
		mq_bytes += mq_treesize;
		info->ucounts = get_ucounts(current_ucounts());
		if (info->ucounts) {
			long msgqueue;

			spin_lock(&mq_lock);
			msgqueue = inc_rlimit_ucounts(info->ucounts, UCOUNT_RLIMIT_MSGQUEUE, mq_bytes);
			if (msgqueue == LONG_MAX || msgqueue > rlimit(RLIMIT_MSGQUEUE)) {
				dec_rlimit_ucounts(info->ucounts, UCOUNT_RLIMIT_MSGQUEUE, mq_bytes);
				spin_unlock(&mq_lock);
				put_ucounts(info->ucounts);
				info->ucounts = NULL;
				/* mqueue_evict_inode() releases info->messages */
				ret = -EMFILE;
				goto out_inode;
			}
			spin_unlock(&mq_lock);
		}
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

static int mqueue_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct inode *inode;
	struct ipc_namespace *ns = sb->s_fs_info;

	sb->s_iflags |= SB_I_NOEXEC | SB_I_NODEV;
	sb->s_blocksize = PAGE_SIZE;
	sb->s_blocksize_bits = PAGE_SHIFT;
	sb->s_magic = MQUEUE_MAGIC;
	sb->s_op = &mqueue_super_ops;
	sb->s_d_flags = DCACHE_DONTCACHE;

	inode = mqueue_get_inode(sb, ns, S_IFDIR | S_ISVTX | S_IRWXUGO, NULL);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
		return -ENOMEM;
	return 0;
}

static int mqueue_get_tree(struct fs_context *fc)
{
	struct mqueue_fs_context *ctx = fc->fs_private;

	/*
	 * With a newly created ipc namespace, we don't need to do a search
	 * for an ipc namespace match, but we still need to set s_fs_info.
	 */
	if (ctx->newns) {
		fc->s_fs_info = ctx->ipc_ns;
		return get_tree_nodev(fc, mqueue_fill_super);
	}
	return get_tree_keyed(fc, mqueue_fill_super, ctx->ipc_ns);
}

static void mqueue_fs_context_free(struct fs_context *fc)
{
	struct mqueue_fs_context *ctx = fc->fs_private;

	put_ipc_ns(ctx->ipc_ns);
	kfree(ctx);
}

static int mqueue_init_fs_context(struct fs_context *fc)
{
	struct mqueue_fs_context *ctx;

	ctx = kzalloc(sizeof(struct mqueue_fs_context), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->ipc_ns = get_ipc_ns(current->nsproxy->ipc_ns);
	put_user_ns(fc->user_ns);
	fc->user_ns = get_user_ns(ctx->ipc_ns->user_ns);
	fc->fs_private = ctx;
	fc->ops = &mqueue_fs_context_ops;
	return 0;
}

/*
 * mq_init_ns() is currently the only caller of mq_create_mount().
 * So the ns parameter is always a newly created ipc namespace.
 */
static struct vfsmount *mq_create_mount(struct ipc_namespace *ns)
{
	struct mqueue_fs_context *ctx;
	struct fs_context *fc;
	struct vfsmount *mnt;

	fc = fs_context_for_mount(&mqueue_fs_type, SB_KERNMOUNT);
	if (IS_ERR(fc))
		return ERR_CAST(fc);

	ctx = fc->fs_private;
	ctx->newns = true;
	put_ipc_ns(ctx->ipc_ns);
	ctx->ipc_ns = get_ipc_ns(ns);
	put_user_ns(fc->user_ns);
	fc->user_ns = get_user_ns(ctx->ipc_ns->user_ns);

	mnt = fc_mount_longterm(fc);
	put_fs_context(fc);
	return mnt;
}

static void init_once(void *foo)
{
	struct mqueue_inode_info *p = foo;

	inode_init_once(&p->vfs_inode);
}

static struct inode *mqueue_alloc_inode(struct super_block *sb)
{
	struct mqueue_inode_info *ei;

	ei = alloc_inode_sb(sb, mqueue_inode_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	return &ei->vfs_inode;
}

static void mqueue_free_inode(struct inode *inode)
{
	kmem_cache_free(mqueue_inode_cachep, MQUEUE_I(inode));
}

static void mqueue_evict_inode(struct inode *inode)
{
	struct mqueue_inode_info *info;
	struct ipc_namespace *ipc_ns;
	struct msg_msg *msg, *nmsg;
	LIST_HEAD(tmp_msg);

	clear_inode(inode);

	if (S_ISDIR(inode->i_mode))
		return;

	ipc_ns = get_ns_from_inode(inode);
	info = MQUEUE_I(inode);
	spin_lock(&info->lock);
	while ((msg = msg_get(info)) != NULL)
		list_add_tail(&msg->m_list, &tmp_msg);
	kfree(info->node_cache);
	spin_unlock(&info->lock);

	list_for_each_entry_safe(msg, nmsg, &tmp_msg, m_list) {
		list_del(&msg->m_list);
		free_msg(msg);
	}

	if (info->ucounts) {
		unsigned long mq_bytes, mq_treesize;

		/* Total amount of bytes accounted for the mqueue */
		mq_treesize = info->attr.mq_maxmsg * sizeof(struct msg_msg) +
			min_t(unsigned int, info->attr.mq_maxmsg, MQ_PRIO_MAX) *
			sizeof(struct posix_msg_tree_node);

		mq_bytes = mq_treesize + (info->attr.mq_maxmsg *
					  info->attr.mq_msgsize);

		spin_lock(&mq_lock);
		dec_rlimit_ucounts(info->ucounts, UCOUNT_RLIMIT_MSGQUEUE, mq_bytes);
		/*
		 * get_ns_from_inode() ensures that the
		 * (ipc_ns = sb->s_fs_info) is either a valid ipc_ns
		 * to which we now hold a reference, or it is NULL.
		 * We can't put it here under mq_lock, though.
		 */
		if (ipc_ns)
			ipc_ns->mq_queues_count--;
		spin_unlock(&mq_lock);
		put_ucounts(info->ucounts);
		info->ucounts = NULL;
	}
	if (ipc_ns)
		put_ipc_ns(ipc_ns);
}

static int mqueue_create_attr(struct dentry *dentry, umode_t mode, void *arg)
{
	struct inode *dir = dentry->d_parent->d_inode;
	struct inode *inode;
	struct mq_attr *attr = arg;
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
	if (IS_ERR(inode)) {
		error = PTR_ERR(inode);
		spin_lock(&mq_lock);
		ipc_ns->mq_queues_count--;
		goto out_unlock;
	}

	put_ipc_ns(ipc_ns);
	dir->i_size += DIRENT_SIZE;
	simple_inode_init_ts(dir);

	d_instantiate(dentry, inode);
	dget(dentry);
	return 0;
out_unlock:
	spin_unlock(&mq_lock);
	if (ipc_ns)
		put_ipc_ns(ipc_ns);
	return error;
}

static int mqueue_create(struct mnt_idmap *idmap, struct inode *dir,
			 struct dentry *dentry, umode_t mode, bool excl)
{
	return mqueue_create_attr(dentry, mode, NULL);
}

static int mqueue_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);

	simple_inode_init_ts(dir);
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
	struct inode *inode = file_inode(filp);
	struct mqueue_inode_info *info = MQUEUE_I(inode);
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

	inode_set_atime_to_ts(inode, inode_set_ctime_current(inode));
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

static __poll_t mqueue_poll_file(struct file *filp, struct poll_table_struct *poll_tab)
{
	struct mqueue_inode_info *info = MQUEUE_I(file_inode(filp));
	__poll_t retval = 0;

	poll_wait(filp, &info->wait_q, poll_tab);

	spin_lock(&info->lock);
	if (info->attr.mq_curmsgs)
		retval = EPOLLIN | EPOLLRDNORM;

	if (info->attr.mq_curmsgs < info->attr.mq_maxmsg)
		retval |= EPOLLOUT | EPOLLWRNORM;
	spin_unlock(&info->lock);

	return retval;
}

/* Adds current to info->e_wait_q[sr] before element with smaller prio */
static void wq_add(struct mqueue_inode_info *info, int sr,
			struct ext_wait_queue *ewp)
{
	struct ext_wait_queue *walk;

	list_for_each_entry(walk, &info->e_wait_q[sr].list, list) {
		if (walk->task->prio <= current->prio) {
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
	__releases(&info->lock)
{
	int retval;
	signed long time;

	wq_add(info, sr, ewp);

	for (;;) {
		/* memory barrier not required, we hold info->lock */
		__set_current_state(TASK_INTERRUPTIBLE);

		spin_unlock(&info->lock);
		time = schedule_hrtimeout_range_clock(timeout, 0,
			HRTIMER_MODE_ABS, CLOCK_REALTIME);

		if (READ_ONCE(ewp->state) == STATE_READY) {
			/* see MQ_BARRIER for purpose/pairing */
			smp_acquire__after_ctrl_dep();
			retval = 0;
			goto out;
		}
		spin_lock(&info->lock);

		/* we hold info->lock, so no memory barrier required */
		if (READ_ONCE(ewp->state) == STATE_READY) {
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
	((char *)skb->data)[NOTIFY_COOKIE_LEN-1] = code;
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
		switch (info->notify.sigev_notify) {
		case SIGEV_NONE:
			break;
		case SIGEV_SIGNAL: {
			struct kernel_siginfo sig_i;
			struct task_struct *task;

			/* do_mq_notify() accepts sigev_signo == 0, why?? */
			if (!info->notify.sigev_signo)
				break;

			clear_siginfo(&sig_i);
			sig_i.si_signo = info->notify.sigev_signo;
			sig_i.si_errno = 0;
			sig_i.si_code = SI_MESGQ;
			sig_i.si_value = info->notify.sigev_value;
			rcu_read_lock();
			/* map current pid/uid into info->owner's namespaces */
			sig_i.si_pid = task_tgid_nr_ns(current,
						ns_of_pid(info->notify_owner));
			sig_i.si_uid = from_kuid_munged(info->notify_user_ns,
						current_uid());
			/*
			 * We can't use kill_pid_info(), this signal should
			 * bypass check_kill_permission(). It is from kernel
			 * but si_fromuser() can't know this.
			 * We do check the self_exec_id, to avoid sending
			 * signals to programs that don't expect them.
			 */
			task = pid_task(info->notify_owner, PIDTYPE_TGID);
			if (task && task->self_exec_id ==
						info->notify_self_exec_id) {
				do_send_sig_info(info->notify.sigev_signo,
						&sig_i, task, PIDTYPE_TGID);
			}
			rcu_read_unlock();
			break;
		}
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

static int prepare_timeout(const struct __kernel_timespec __user *u_abs_timeout,
			   struct timespec64 *ts)
{
	if (get_timespec64(ts, u_abs_timeout))
		return -EFAULT;
	if (!timespec64_valid(ts))
		return -EINVAL;
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

static int prepare_open(struct dentry *dentry, int oflag, int ro,
			umode_t mode, struct filename *name,
			struct mq_attr *attr)
{
	static const int oflag2acc[O_ACCMODE] = { MAY_READ, MAY_WRITE,
						  MAY_READ | MAY_WRITE };
	int acc;

	if (d_really_is_negative(dentry)) {
		if (!(oflag & O_CREAT))
			return -ENOENT;
		if (ro)
			return ro;
		audit_inode_parent_hidden(name, dentry->d_parent);
		return vfs_mkobj(dentry, mode & ~current_umask(),
				  mqueue_create_attr, attr);
	}
	/* it already existed */
	audit_inode(name, dentry, 0);
	if ((oflag & (O_CREAT|O_EXCL)) == (O_CREAT|O_EXCL))
		return -EEXIST;
	if ((oflag & O_ACCMODE) == (O_RDWR | O_WRONLY))
		return -EINVAL;
	acc = oflag2acc[oflag & O_ACCMODE];
	return inode_permission(&nop_mnt_idmap, d_inode(dentry), acc);
}

static int do_mq_open(const char __user *u_name, int oflag, umode_t mode,
		      struct mq_attr *attr)
{
	struct vfsmount *mnt = current->nsproxy->ipc_ns->mq_mnt;
	struct dentry *root = mnt->mnt_root;
	struct filename *name;
	struct path path;
	int fd, error;
	int ro;

	audit_mq_open(oflag, mode, attr);

	name = getname(u_name);
	if (IS_ERR(name))
		return PTR_ERR(name);

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0)
		goto out_putname;

	ro = mnt_want_write(mnt);	/* we'll drop it in any case */
	inode_lock(d_inode(root));
	path.dentry = lookup_noperm(&QSTR(name->name), root);
	if (IS_ERR(path.dentry)) {
		error = PTR_ERR(path.dentry);
		goto out_putfd;
	}
	path.mnt = mntget(mnt);
	error = prepare_open(path.dentry, oflag, ro, mode, name, attr);
	if (!error) {
		struct file *file = dentry_open(&path, oflag, current_cred());
		if (!IS_ERR(file))
			fd_install(fd, file);
		else
			error = PTR_ERR(file);
	}
	path_put(&path);
out_putfd:
	if (error) {
		put_unused_fd(fd);
		fd = error;
	}
	inode_unlock(d_inode(root));
	if (!ro)
		mnt_drop_write(mnt);
out_putname:
	putname(name);
	return fd;
}

SYSCALL_DEFINE4(mq_open, const char __user *, u_name, int, oflag, umode_t, mode,
		struct mq_attr __user *, u_attr)
{
	struct mq_attr attr;
	if (u_attr && copy_from_user(&attr, u_attr, sizeof(struct mq_attr)))
		return -EFAULT;

	return do_mq_open(u_name, oflag, mode, u_attr ? &attr : NULL);
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

	audit_inode_parent_hidden(name, mnt->mnt_root);
	err = mnt_want_write(mnt);
	if (err)
		goto out_name;
	inode_lock_nested(d_inode(mnt->mnt_root), I_MUTEX_PARENT);
	dentry = lookup_noperm(&QSTR(name->name), mnt->mnt_root);
	if (IS_ERR(dentry)) {
		err = PTR_ERR(dentry);
		goto out_unlock;
	}

	inode = d_inode(dentry);
	if (!inode) {
		err = -ENOENT;
	} else {
		ihold(inode);
		err = vfs_unlink(&nop_mnt_idmap, d_inode(dentry->d_parent),
				 dentry, NULL);
	}
	dput(dentry);

out_unlock:
	inode_unlock(d_inode(mnt->mnt_root));
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
 * receiver. The receiver accepts the message and returns without grabbing the
 * queue spinlock:
 *
 * - Set pointer to message.
 * - Queue the receiver task for later wakeup (without the info->lock).
 * - Update its state to STATE_READY. Now the receiver can continue.
 * - Wake up the process after the lock is dropped. Should the process wake up
 *   before this wakeup (due to a timeout or a signal) it will either see
 *   STATE_READY and continue or acquire the lock to check the state again.
 *
 * The same algorithm is used for senders.
 */

static inline void __pipelined_op(struct wake_q_head *wake_q,
				  struct mqueue_inode_info *info,
				  struct ext_wait_queue *this)
{
	struct task_struct *task;

	list_del(&this->list);
	task = get_task_struct(this->task);

	/* see MQ_BARRIER for purpose/pairing */
	smp_store_release(&this->state, STATE_READY);
	wake_q_add_safe(wake_q, task);
}

/* pipelined_send() - send a message directly to the task waiting in
 * sys_mq_timedreceive() (without inserting message into a queue).
 */
static inline void pipelined_send(struct wake_q_head *wake_q,
				  struct mqueue_inode_info *info,
				  struct msg_msg *message,
				  struct ext_wait_queue *receiver)
{
	receiver->msg = message;
	__pipelined_op(wake_q, info, receiver);
}

/* pipelined_receive() - if there is task waiting in sys_mq_timedsend()
 * gets its message and put to the queue (we have one free place for sure). */
static inline void pipelined_receive(struct wake_q_head *wake_q,
				     struct mqueue_inode_info *info)
{
	struct ext_wait_queue *sender = wq_get_first_waiter(info, SEND);

	if (!sender) {
		/* for poll */
		wake_up_interruptible(&info->wait_q);
		return;
	}
	if (msg_insert(sender->msg, info))
		return;

	__pipelined_op(wake_q, info, sender);
}

static int do_mq_timedsend(mqd_t mqdes, const char __user *u_msg_ptr,
		size_t msg_len, unsigned int msg_prio,
		struct timespec64 *ts)
{
	struct inode *inode;
	struct ext_wait_queue wait;
	struct ext_wait_queue *receiver;
	struct msg_msg *msg_ptr;
	struct mqueue_inode_info *info;
	ktime_t expires, *timeout = NULL;
	struct posix_msg_tree_node *new_leaf = NULL;
	int ret = 0;
	DEFINE_WAKE_Q(wake_q);

	if (unlikely(msg_prio >= (unsigned long) MQ_PRIO_MAX))
		return -EINVAL;

	if (ts) {
		expires = timespec64_to_ktime(*ts);
		timeout = &expires;
	}

	audit_mq_sendrecv(mqdes, msg_len, msg_prio, ts);

	CLASS(fd, f)(mqdes);
	if (fd_empty(f))
		return -EBADF;

	inode = file_inode(fd_file(f));
	if (unlikely(fd_file(f)->f_op != &mqueue_file_operations))
		return -EBADF;
	info = MQUEUE_I(inode);
	audit_file(fd_file(f));

	if (unlikely(!(fd_file(f)->f_mode & FMODE_WRITE)))
		return -EBADF;

	if (unlikely(msg_len > info->attr.mq_msgsize))
		return -EMSGSIZE;

	/* First try to allocate memory, before doing anything with
	 * existing queues. */
	msg_ptr = load_msg(u_msg_ptr, msg_len);
	if (IS_ERR(msg_ptr))
		return PTR_ERR(msg_ptr);
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
		new_leaf = NULL;
	} else {
		kfree(new_leaf);
	}

	if (info->attr.mq_curmsgs == info->attr.mq_maxmsg) {
		if (fd_file(f)->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
		} else {
			wait.task = current;
			wait.msg = (void *) msg_ptr;

			/* memory barrier not required, we hold info->lock */
			WRITE_ONCE(wait.state, STATE_NONE);
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
			pipelined_send(&wake_q, info, msg_ptr, receiver);
		} else {
			/* adds message to the queue */
			ret = msg_insert(msg_ptr, info);
			if (ret)
				goto out_unlock;
			__do_notify(info);
		}
		simple_inode_init_ts(inode);
	}
out_unlock:
	spin_unlock(&info->lock);
	wake_up_q(&wake_q);
out_free:
	if (ret)
		free_msg(msg_ptr);
	return ret;
}

static int do_mq_timedreceive(mqd_t mqdes, char __user *u_msg_ptr,
		size_t msg_len, unsigned int __user *u_msg_prio,
		struct timespec64 *ts)
{
	ssize_t ret;
	struct msg_msg *msg_ptr;
	struct inode *inode;
	struct mqueue_inode_info *info;
	struct ext_wait_queue wait;
	ktime_t expires, *timeout = NULL;
	struct posix_msg_tree_node *new_leaf = NULL;

	if (ts) {
		expires = timespec64_to_ktime(*ts);
		timeout = &expires;
	}

	audit_mq_sendrecv(mqdes, msg_len, 0, ts);

	CLASS(fd, f)(mqdes);
	if (fd_empty(f))
		return -EBADF;

	inode = file_inode(fd_file(f));
	if (unlikely(fd_file(f)->f_op != &mqueue_file_operations))
		return -EBADF;
	info = MQUEUE_I(inode);
	audit_file(fd_file(f));

	if (unlikely(!(fd_file(f)->f_mode & FMODE_READ)))
		return -EBADF;

	/* checks if buffer is big enough */
	if (unlikely(msg_len < info->attr.mq_msgsize))
		return -EMSGSIZE;

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
	} else {
		kfree(new_leaf);
	}

	if (info->attr.mq_curmsgs == 0) {
		if (fd_file(f)->f_flags & O_NONBLOCK) {
			spin_unlock(&info->lock);
			ret = -EAGAIN;
		} else {
			wait.task = current;

			/* memory barrier not required, we hold info->lock */
			WRITE_ONCE(wait.state, STATE_NONE);
			ret = wq_sleep(info, RECV, timeout, &wait);
			msg_ptr = wait.msg;
		}
	} else {
		DEFINE_WAKE_Q(wake_q);

		msg_ptr = msg_get(info);

		simple_inode_init_ts(inode);

		/* There is now free space in queue. */
		pipelined_receive(&wake_q, info);
		spin_unlock(&info->lock);
		wake_up_q(&wake_q);
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
	return ret;
}

SYSCALL_DEFINE5(mq_timedsend, mqd_t, mqdes, const char __user *, u_msg_ptr,
		size_t, msg_len, unsigned int, msg_prio,
		const struct __kernel_timespec __user *, u_abs_timeout)
{
	struct timespec64 ts, *p = NULL;
	if (u_abs_timeout) {
		int res = prepare_timeout(u_abs_timeout, &ts);
		if (res)
			return res;
		p = &ts;
	}
	return do_mq_timedsend(mqdes, u_msg_ptr, msg_len, msg_prio, p);
}

SYSCALL_DEFINE5(mq_timedreceive, mqd_t, mqdes, char __user *, u_msg_ptr,
		size_t, msg_len, unsigned int __user *, u_msg_prio,
		const struct __kernel_timespec __user *, u_abs_timeout)
{
	struct timespec64 ts, *p = NULL;
	if (u_abs_timeout) {
		int res = prepare_timeout(u_abs_timeout, &ts);
		if (res)
			return res;
		p = &ts;
	}
	return do_mq_timedreceive(mqdes, u_msg_ptr, msg_len, u_msg_prio, p);
}

/*
 * Notes: the case when user wants us to deregister (with NULL as pointer)
 * and he isn't currently owner of notification, will be silently discarded.
 * It isn't explicitly defined in the POSIX.
 */
static int do_mq_notify(mqd_t mqdes, const struct sigevent *notification)
{
	int ret;
	struct sock *sock;
	struct inode *inode;
	struct mqueue_inode_info *info;
	struct sk_buff *nc;

	audit_mq_notify(mqdes, notification);

	nc = NULL;
	sock = NULL;
	if (notification != NULL) {
		if (unlikely(notification->sigev_notify != SIGEV_NONE &&
			     notification->sigev_notify != SIGEV_SIGNAL &&
			     notification->sigev_notify != SIGEV_THREAD))
			return -EINVAL;
		if (notification->sigev_notify == SIGEV_SIGNAL &&
			!valid_signal(notification->sigev_signo)) {
			return -EINVAL;
		}
		if (notification->sigev_notify == SIGEV_THREAD) {
			long timeo;

			/* create the notify skb */
			nc = alloc_skb(NOTIFY_COOKIE_LEN, GFP_KERNEL);
			if (!nc)
				return -ENOMEM;

			if (copy_from_user(nc->data,
					notification->sigev_value.sival_ptr,
					NOTIFY_COOKIE_LEN)) {
				kfree_skb(nc);
				return -EFAULT;
			}

			/* TODO: add a header? */
			skb_put(nc, NOTIFY_COOKIE_LEN);
			/* and attach it to the socket */
retry:
			sock = netlink_getsockbyfd(notification->sigev_signo);
			if (IS_ERR(sock)) {
				kfree_skb(nc);
				return PTR_ERR(sock);
			}

			timeo = MAX_SCHEDULE_TIMEOUT;
			ret = netlink_attachskb(sock, nc, &timeo, NULL);
			if (ret == 1)
				goto retry;
			if (ret)
				return ret;
		}
	}

	CLASS(fd, f)(mqdes);
	if (fd_empty(f)) {
		ret = -EBADF;
		goto out;
	}

	inode = file_inode(fd_file(f));
	if (unlikely(fd_file(f)->f_op != &mqueue_file_operations)) {
		ret = -EBADF;
		goto out;
	}
	info = MQUEUE_I(inode);

	ret = 0;
	spin_lock(&info->lock);
	if (notification == NULL) {
		if (info->notify_owner == task_tgid(current)) {
			remove_notification(info);
			inode_set_atime_to_ts(inode,
					      inode_set_ctime_current(inode));
		}
	} else if (info->notify_owner != NULL) {
		ret = -EBUSY;
	} else {
		switch (notification->sigev_notify) {
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
			info->notify.sigev_signo = notification->sigev_signo;
			info->notify.sigev_value = notification->sigev_value;
			info->notify.sigev_notify = SIGEV_SIGNAL;
			info->notify_self_exec_id = current->self_exec_id;
			break;
		}

		info->notify_owner = get_pid(task_tgid(current));
		info->notify_user_ns = get_user_ns(current_user_ns());
		inode_set_atime_to_ts(inode, inode_set_ctime_current(inode));
	}
	spin_unlock(&info->lock);
out:
	if (sock)
		netlink_detachskb(sock, nc);
	return ret;
}

SYSCALL_DEFINE2(mq_notify, mqd_t, mqdes,
		const struct sigevent __user *, u_notification)
{
	struct sigevent n, *p = NULL;
	if (u_notification) {
		if (copy_from_user(&n, u_notification, sizeof(struct sigevent)))
			return -EFAULT;
		p = &n;
	}
	return do_mq_notify(mqdes, p);
}

static int do_mq_getsetattr(int mqdes, struct mq_attr *new, struct mq_attr *old)
{
	struct inode *inode;
	struct mqueue_inode_info *info;

	if (new && (new->mq_flags & (~O_NONBLOCK)))
		return -EINVAL;

	CLASS(fd, f)(mqdes);
	if (fd_empty(f))
		return -EBADF;

	if (unlikely(fd_file(f)->f_op != &mqueue_file_operations))
		return -EBADF;

	inode = file_inode(fd_file(f));
	info = MQUEUE_I(inode);

	spin_lock(&info->lock);

	if (old) {
		*old = info->attr;
		old->mq_flags = fd_file(f)->f_flags & O_NONBLOCK;
	}
	if (new) {
		audit_mq_getsetattr(mqdes, new);
		spin_lock(&fd_file(f)->f_lock);
		if (new->mq_flags & O_NONBLOCK)
			fd_file(f)->f_flags |= O_NONBLOCK;
		else
			fd_file(f)->f_flags &= ~O_NONBLOCK;
		spin_unlock(&fd_file(f)->f_lock);

		inode_set_atime_to_ts(inode, inode_set_ctime_current(inode));
	}

	spin_unlock(&info->lock);
	return 0;
}

SYSCALL_DEFINE3(mq_getsetattr, mqd_t, mqdes,
		const struct mq_attr __user *, u_mqstat,
		struct mq_attr __user *, u_omqstat)
{
	int ret;
	struct mq_attr mqstat, omqstat;
	struct mq_attr *new = NULL, *old = NULL;

	if (u_mqstat) {
		new = &mqstat;
		if (copy_from_user(new, u_mqstat, sizeof(struct mq_attr)))
			return -EFAULT;
	}
	if (u_omqstat)
		old = &omqstat;

	ret = do_mq_getsetattr(mqdes, new, old);
	if (ret || !old)
		return ret;

	if (copy_to_user(u_omqstat, old, sizeof(struct mq_attr)))
		return -EFAULT;
	return 0;
}

#ifdef CONFIG_COMPAT

struct compat_mq_attr {
	compat_long_t mq_flags;      /* message queue flags		     */
	compat_long_t mq_maxmsg;     /* maximum number of messages	     */
	compat_long_t mq_msgsize;    /* maximum message size		     */
	compat_long_t mq_curmsgs;    /* number of messages currently queued  */
	compat_long_t __reserved[4]; /* ignored for input, zeroed for output */
};

static inline int get_compat_mq_attr(struct mq_attr *attr,
			const struct compat_mq_attr __user *uattr)
{
	struct compat_mq_attr v;

	if (copy_from_user(&v, uattr, sizeof(*uattr)))
		return -EFAULT;

	memset(attr, 0, sizeof(*attr));
	attr->mq_flags = v.mq_flags;
	attr->mq_maxmsg = v.mq_maxmsg;
	attr->mq_msgsize = v.mq_msgsize;
	attr->mq_curmsgs = v.mq_curmsgs;
	return 0;
}

static inline int put_compat_mq_attr(const struct mq_attr *attr,
			struct compat_mq_attr __user *uattr)
{
	struct compat_mq_attr v;

	memset(&v, 0, sizeof(v));
	v.mq_flags = attr->mq_flags;
	v.mq_maxmsg = attr->mq_maxmsg;
	v.mq_msgsize = attr->mq_msgsize;
	v.mq_curmsgs = attr->mq_curmsgs;
	if (copy_to_user(uattr, &v, sizeof(*uattr)))
		return -EFAULT;
	return 0;
}

COMPAT_SYSCALL_DEFINE4(mq_open, const char __user *, u_name,
		       int, oflag, compat_mode_t, mode,
		       struct compat_mq_attr __user *, u_attr)
{
	struct mq_attr attr, *p = NULL;
	if (u_attr && oflag & O_CREAT) {
		p = &attr;
		if (get_compat_mq_attr(&attr, u_attr))
			return -EFAULT;
	}
	return do_mq_open(u_name, oflag, mode, p);
}

COMPAT_SYSCALL_DEFINE2(mq_notify, mqd_t, mqdes,
		       const struct compat_sigevent __user *, u_notification)
{
	struct sigevent n, *p = NULL;
	if (u_notification) {
		if (get_compat_sigevent(&n, u_notification))
			return -EFAULT;
		if (n.sigev_notify == SIGEV_THREAD)
			n.sigev_value.sival_ptr = compat_ptr(n.sigev_value.sival_int);
		p = &n;
	}
	return do_mq_notify(mqdes, p);
}

COMPAT_SYSCALL_DEFINE3(mq_getsetattr, mqd_t, mqdes,
		       const struct compat_mq_attr __user *, u_mqstat,
		       struct compat_mq_attr __user *, u_omqstat)
{
	int ret;
	struct mq_attr mqstat, omqstat;
	struct mq_attr *new = NULL, *old = NULL;

	if (u_mqstat) {
		new = &mqstat;
		if (get_compat_mq_attr(new, u_mqstat))
			return -EFAULT;
	}
	if (u_omqstat)
		old = &omqstat;

	ret = do_mq_getsetattr(mqdes, new, old);
	if (ret || !old)
		return ret;

	if (put_compat_mq_attr(old, u_omqstat))
		return -EFAULT;
	return 0;
}
#endif

#ifdef CONFIG_COMPAT_32BIT_TIME
static int compat_prepare_timeout(const struct old_timespec32 __user *p,
				   struct timespec64 *ts)
{
	if (get_old_timespec32(ts, p))
		return -EFAULT;
	if (!timespec64_valid(ts))
		return -EINVAL;
	return 0;
}

SYSCALL_DEFINE5(mq_timedsend_time32, mqd_t, mqdes,
		const char __user *, u_msg_ptr,
		unsigned int, msg_len, unsigned int, msg_prio,
		const struct old_timespec32 __user *, u_abs_timeout)
{
	struct timespec64 ts, *p = NULL;
	if (u_abs_timeout) {
		int res = compat_prepare_timeout(u_abs_timeout, &ts);
		if (res)
			return res;
		p = &ts;
	}
	return do_mq_timedsend(mqdes, u_msg_ptr, msg_len, msg_prio, p);
}

SYSCALL_DEFINE5(mq_timedreceive_time32, mqd_t, mqdes,
		char __user *, u_msg_ptr,
		unsigned int, msg_len, unsigned int __user *, u_msg_prio,
		const struct old_timespec32 __user *, u_abs_timeout)
{
	struct timespec64 ts, *p = NULL;
	if (u_abs_timeout) {
		int res = compat_prepare_timeout(u_abs_timeout, &ts);
		if (res)
			return res;
		p = &ts;
	}
	return do_mq_timedreceive(mqdes, u_msg_ptr, msg_len, u_msg_prio, p);
}
#endif

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
	.free_inode = mqueue_free_inode,
	.evict_inode = mqueue_evict_inode,
	.statfs = simple_statfs,
};

static const struct fs_context_operations mqueue_fs_context_ops = {
	.free		= mqueue_fs_context_free,
	.get_tree	= mqueue_get_tree,
};

static struct file_system_type mqueue_fs_type = {
	.name			= "mqueue",
	.init_fs_context	= mqueue_init_fs_context,
	.kill_sb		= kill_litter_super,
	.fs_flags		= FS_USERNS_MOUNT,
};

int mq_init_ns(struct ipc_namespace *ns)
{
	struct vfsmount *m;

	ns->mq_queues_count  = 0;
	ns->mq_queues_max    = DFLT_QUEUESMAX;
	ns->mq_msg_max       = DFLT_MSGMAX;
	ns->mq_msgsize_max   = DFLT_MSGSIZEMAX;
	ns->mq_msg_default   = DFLT_MSG;
	ns->mq_msgsize_default  = DFLT_MSGSIZE;

	m = mq_create_mount(ns);
	if (IS_ERR(m))
		return PTR_ERR(m);
	ns->mq_mnt = m;
	return 0;
}

void mq_clear_sbinfo(struct ipc_namespace *ns)
{
	ns->mq_mnt->mnt_sb->s_fs_info = NULL;
}

static int __init init_mqueue_fs(void)
{
	int error;

	mqueue_inode_cachep = kmem_cache_create("mqueue_inode_cache",
				sizeof(struct mqueue_inode_info), 0,
				SLAB_HWCACHE_ALIGN|SLAB_ACCOUNT, init_once);
	if (mqueue_inode_cachep == NULL)
		return -ENOMEM;

	if (!setup_mq_sysctls(&init_ipc_ns)) {
		pr_warn("sysctl registration failed\n");
		error = -ENOMEM;
		goto out_kmem;
	}

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
	retire_mq_sysctls(&init_ipc_ns);
out_kmem:
	kmem_cache_destroy(mqueue_inode_cachep);
	return error;
}

device_initcall(init_mqueue_fs);
