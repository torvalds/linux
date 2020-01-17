/*
 * POSIX message queues filesystem for Linux.
 *
 * Copyright (C) 2003,2004  Krzysztof Benedyczak    (golbi@mat.uni.torun.pl)
 *                          Michal Wronski          (michal.wronski@gmail.com)
 *
 * Spinlocks:               Mohamed Abbas           (abbas.mohamed@intel.com)
 * Lockless receive & send, fd based yestify:
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
};

#define MQUEUE_MAGIC	0x19800202
#define DIRENT_SIZE	20
#define FILENT_SIZE	80

#define SEND		0
#define RECV		1

#define STATE_NONE	0
#define STATE_READY	1

struct posix_msg_tree_yesde {
	struct rb_yesde		rb_yesde;
	struct list_head	msg_list;
	int			priority;
};

struct ext_wait_queue {		/* queue of sleeping tasks */
	struct task_struct *task;
	struct list_head list;
	struct msg_msg *msg;	/* ptr of loaded message */
	int state;		/* one of STATE_* values */
};

struct mqueue_iyesde_info {
	spinlock_t lock;
	struct iyesde vfs_iyesde;
	wait_queue_head_t wait_q;

	struct rb_root msg_tree;
	struct rb_yesde *msg_tree_rightmost;
	struct posix_msg_tree_yesde *yesde_cache;
	struct mq_attr attr;

	struct sigevent yestify;
	struct pid *yestify_owner;
	struct user_namespace *yestify_user_ns;
	struct user_struct *user;	/* user who created, for accounting */
	struct sock *yestify_sock;
	struct sk_buff *yestify_cookie;

	/* for tasks waiting for free space and messages, respectively */
	struct ext_wait_queue e_wait_q[2];

	unsigned long qsize; /* size of queue in memory (sum of all msgs) */
};

static struct file_system_type mqueue_fs_type;
static const struct iyesde_operations mqueue_dir_iyesde_operations;
static const struct file_operations mqueue_file_operations;
static const struct super_operations mqueue_super_ops;
static const struct fs_context_operations mqueue_fs_context_ops;
static void remove_yestification(struct mqueue_iyesde_info *info);

static struct kmem_cache *mqueue_iyesde_cachep;

static struct ctl_table_header *mq_sysctl_table;

static inline struct mqueue_iyesde_info *MQUEUE_I(struct iyesde *iyesde)
{
	return container_of(iyesde, struct mqueue_iyesde_info, vfs_iyesde);
}

/*
 * This routine should be called with the mq_lock held.
 */
static inline struct ipc_namespace *__get_ns_from_iyesde(struct iyesde *iyesde)
{
	return get_ipc_ns(iyesde->i_sb->s_fs_info);
}

static struct ipc_namespace *get_ns_from_iyesde(struct iyesde *iyesde)
{
	struct ipc_namespace *ns;

	spin_lock(&mq_lock);
	ns = __get_ns_from_iyesde(iyesde);
	spin_unlock(&mq_lock);
	return ns;
}

/* Auxiliary functions to manipulate messages' list */
static int msg_insert(struct msg_msg *msg, struct mqueue_iyesde_info *info)
{
	struct rb_yesde **p, *parent = NULL;
	struct posix_msg_tree_yesde *leaf;
	bool rightmost = true;

	p = &info->msg_tree.rb_yesde;
	while (*p) {
		parent = *p;
		leaf = rb_entry(parent, struct posix_msg_tree_yesde, rb_yesde);

		if (likely(leaf->priority == msg->m_type))
			goto insert_msg;
		else if (msg->m_type < leaf->priority) {
			p = &(*p)->rb_left;
			rightmost = false;
		} else
			p = &(*p)->rb_right;
	}
	if (info->yesde_cache) {
		leaf = info->yesde_cache;
		info->yesde_cache = NULL;
	} else {
		leaf = kmalloc(sizeof(*leaf), GFP_ATOMIC);
		if (!leaf)
			return -ENOMEM;
		INIT_LIST_HEAD(&leaf->msg_list);
	}
	leaf->priority = msg->m_type;

	if (rightmost)
		info->msg_tree_rightmost = &leaf->rb_yesde;

	rb_link_yesde(&leaf->rb_yesde, parent, p);
	rb_insert_color(&leaf->rb_yesde, &info->msg_tree);
insert_msg:
	info->attr.mq_curmsgs++;
	info->qsize += msg->m_ts;
	list_add_tail(&msg->m_list, &leaf->msg_list);
	return 0;
}

static inline void msg_tree_erase(struct posix_msg_tree_yesde *leaf,
				  struct mqueue_iyesde_info *info)
{
	struct rb_yesde *yesde = &leaf->rb_yesde;

	if (info->msg_tree_rightmost == yesde)
		info->msg_tree_rightmost = rb_prev(yesde);

	rb_erase(yesde, &info->msg_tree);
	if (info->yesde_cache) {
		kfree(leaf);
	} else {
		info->yesde_cache = leaf;
	}
}

static inline struct msg_msg *msg_get(struct mqueue_iyesde_info *info)
{
	struct rb_yesde *parent = NULL;
	struct posix_msg_tree_yesde *leaf;
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
				     "yes tree element, but supposedly messages "
				     "should exist!\n");
			info->attr.mq_curmsgs = 0;
		}
		return NULL;
	}
	leaf = rb_entry(parent, struct posix_msg_tree_yesde, rb_yesde);
	if (unlikely(list_empty(&leaf->msg_list))) {
		pr_warn_once("Inconsistency in POSIX message queue, "
			     "empty leaf yesde but we haven't implemented "
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

static struct iyesde *mqueue_get_iyesde(struct super_block *sb,
		struct ipc_namespace *ipc_ns, umode_t mode,
		struct mq_attr *attr)
{
	struct user_struct *u = current_user();
	struct iyesde *iyesde;
	int ret = -ENOMEM;

	iyesde = new_iyesde(sb);
	if (!iyesde)
		goto err;

	iyesde->i_iyes = get_next_iyes();
	iyesde->i_mode = mode;
	iyesde->i_uid = current_fsuid();
	iyesde->i_gid = current_fsgid();
	iyesde->i_mtime = iyesde->i_ctime = iyesde->i_atime = current_time(iyesde);

	if (S_ISREG(mode)) {
		struct mqueue_iyesde_info *info;
		unsigned long mq_bytes, mq_treesize;

		iyesde->i_fop = &mqueue_file_operations;
		iyesde->i_size = FILENT_SIZE;
		/* mqueue specific info */
		info = MQUEUE_I(iyesde);
		spin_lock_init(&info->lock);
		init_waitqueue_head(&info->wait_q);
		INIT_LIST_HEAD(&info->e_wait_q[0].list);
		INIT_LIST_HEAD(&info->e_wait_q[1].list);
		info->yestify_owner = NULL;
		info->yestify_user_ns = NULL;
		info->qsize = 0;
		info->user = NULL;	/* set when all is ok */
		info->msg_tree = RB_ROOT;
		info->msg_tree_rightmost = NULL;
		info->yesde_cache = NULL;
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
		 * possible message into the queue size. That's yes longer
		 * accurate as the queue is yesw an rbtree and will grow and
		 * shrink depending on usage patterns.  We can, however, still
		 * account one msg_msg struct per message, but the yesdes are
		 * allocated depending on priority usage, and most programs
		 * only use one, or a handful, of priorities.  However, since
		 * this is pinned memory, we need to assume worst case, so
		 * that means the min(mq_maxmsg, max_priorities) * struct
		 * posix_msg_tree_yesde.
		 */

		ret = -EINVAL;
		if (info->attr.mq_maxmsg <= 0 || info->attr.mq_msgsize <= 0)
			goto out_iyesde;
		if (capable(CAP_SYS_RESOURCE)) {
			if (info->attr.mq_maxmsg > HARD_MSGMAX ||
			    info->attr.mq_msgsize > HARD_MSGSIZEMAX)
				goto out_iyesde;
		} else {
			if (info->attr.mq_maxmsg > ipc_ns->mq_msg_max ||
					info->attr.mq_msgsize > ipc_ns->mq_msgsize_max)
				goto out_iyesde;
		}
		ret = -EOVERFLOW;
		/* check for overflow */
		if (info->attr.mq_msgsize > ULONG_MAX/info->attr.mq_maxmsg)
			goto out_iyesde;
		mq_treesize = info->attr.mq_maxmsg * sizeof(struct msg_msg) +
			min_t(unsigned int, info->attr.mq_maxmsg, MQ_PRIO_MAX) *
			sizeof(struct posix_msg_tree_yesde);
		mq_bytes = info->attr.mq_maxmsg * info->attr.mq_msgsize;
		if (mq_bytes + mq_treesize < mq_bytes)
			goto out_iyesde;
		mq_bytes += mq_treesize;
		spin_lock(&mq_lock);
		if (u->mq_bytes + mq_bytes < u->mq_bytes ||
		    u->mq_bytes + mq_bytes > rlimit(RLIMIT_MSGQUEUE)) {
			spin_unlock(&mq_lock);
			/* mqueue_evict_iyesde() releases info->messages */
			ret = -EMFILE;
			goto out_iyesde;
		}
		u->mq_bytes += mq_bytes;
		spin_unlock(&mq_lock);

		/* all is ok */
		info->user = get_uid(u);
	} else if (S_ISDIR(mode)) {
		inc_nlink(iyesde);
		/* Some things misbehave if size == 0 on a directory */
		iyesde->i_size = 2 * DIRENT_SIZE;
		iyesde->i_op = &mqueue_dir_iyesde_operations;
		iyesde->i_fop = &simple_dir_operations;
	}

	return iyesde;
out_iyesde:
	iput(iyesde);
err:
	return ERR_PTR(ret);
}

static int mqueue_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct iyesde *iyesde;
	struct ipc_namespace *ns = sb->s_fs_info;

	sb->s_iflags |= SB_I_NOEXEC | SB_I_NODEV;
	sb->s_blocksize = PAGE_SIZE;
	sb->s_blocksize_bits = PAGE_SHIFT;
	sb->s_magic = MQUEUE_MAGIC;
	sb->s_op = &mqueue_super_ops;

	iyesde = mqueue_get_iyesde(sb, ns, S_IFDIR | S_ISVTX | S_IRWXUGO, NULL);
	if (IS_ERR(iyesde))
		return PTR_ERR(iyesde);

	sb->s_root = d_make_root(iyesde);
	if (!sb->s_root)
		return -ENOMEM;
	return 0;
}

static int mqueue_get_tree(struct fs_context *fc)
{
	struct mqueue_fs_context *ctx = fc->fs_private;

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

static struct vfsmount *mq_create_mount(struct ipc_namespace *ns)
{
	struct mqueue_fs_context *ctx;
	struct fs_context *fc;
	struct vfsmount *mnt;

	fc = fs_context_for_mount(&mqueue_fs_type, SB_KERNMOUNT);
	if (IS_ERR(fc))
		return ERR_CAST(fc);

	ctx = fc->fs_private;
	put_ipc_ns(ctx->ipc_ns);
	ctx->ipc_ns = get_ipc_ns(ns);
	put_user_ns(fc->user_ns);
	fc->user_ns = get_user_ns(ctx->ipc_ns->user_ns);

	mnt = fc_mount(fc);
	put_fs_context(fc);
	return mnt;
}

static void init_once(void *foo)
{
	struct mqueue_iyesde_info *p = (struct mqueue_iyesde_info *) foo;

	iyesde_init_once(&p->vfs_iyesde);
}

static struct iyesde *mqueue_alloc_iyesde(struct super_block *sb)
{
	struct mqueue_iyesde_info *ei;

	ei = kmem_cache_alloc(mqueue_iyesde_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	return &ei->vfs_iyesde;
}

static void mqueue_free_iyesde(struct iyesde *iyesde)
{
	kmem_cache_free(mqueue_iyesde_cachep, MQUEUE_I(iyesde));
}

static void mqueue_evict_iyesde(struct iyesde *iyesde)
{
	struct mqueue_iyesde_info *info;
	struct user_struct *user;
	struct ipc_namespace *ipc_ns;
	struct msg_msg *msg, *nmsg;
	LIST_HEAD(tmp_msg);

	clear_iyesde(iyesde);

	if (S_ISDIR(iyesde->i_mode))
		return;

	ipc_ns = get_ns_from_iyesde(iyesde);
	info = MQUEUE_I(iyesde);
	spin_lock(&info->lock);
	while ((msg = msg_get(info)) != NULL)
		list_add_tail(&msg->m_list, &tmp_msg);
	kfree(info->yesde_cache);
	spin_unlock(&info->lock);

	list_for_each_entry_safe(msg, nmsg, &tmp_msg, m_list) {
		list_del(&msg->m_list);
		free_msg(msg);
	}

	user = info->user;
	if (user) {
		unsigned long mq_bytes, mq_treesize;

		/* Total amount of bytes accounted for the mqueue */
		mq_treesize = info->attr.mq_maxmsg * sizeof(struct msg_msg) +
			min_t(unsigned int, info->attr.mq_maxmsg, MQ_PRIO_MAX) *
			sizeof(struct posix_msg_tree_yesde);

		mq_bytes = mq_treesize + (info->attr.mq_maxmsg *
					  info->attr.mq_msgsize);

		spin_lock(&mq_lock);
		user->mq_bytes -= mq_bytes;
		/*
		 * get_ns_from_iyesde() ensures that the
		 * (ipc_ns = sb->s_fs_info) is either a valid ipc_ns
		 * to which we yesw hold a reference, or it is NULL.
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

static int mqueue_create_attr(struct dentry *dentry, umode_t mode, void *arg)
{
	struct iyesde *dir = dentry->d_parent->d_iyesde;
	struct iyesde *iyesde;
	struct mq_attr *attr = arg;
	int error;
	struct ipc_namespace *ipc_ns;

	spin_lock(&mq_lock);
	ipc_ns = __get_ns_from_iyesde(dir);
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

	iyesde = mqueue_get_iyesde(dir->i_sb, ipc_ns, mode, attr);
	if (IS_ERR(iyesde)) {
		error = PTR_ERR(iyesde);
		spin_lock(&mq_lock);
		ipc_ns->mq_queues_count--;
		goto out_unlock;
	}

	put_ipc_ns(ipc_ns);
	dir->i_size += DIRENT_SIZE;
	dir->i_ctime = dir->i_mtime = dir->i_atime = current_time(dir);

	d_instantiate(dentry, iyesde);
	dget(dentry);
	return 0;
out_unlock:
	spin_unlock(&mq_lock);
	if (ipc_ns)
		put_ipc_ns(ipc_ns);
	return error;
}

static int mqueue_create(struct iyesde *dir, struct dentry *dentry,
				umode_t mode, bool excl)
{
	return mqueue_create_attr(dentry, mode, NULL);
}

static int mqueue_unlink(struct iyesde *dir, struct dentry *dentry)
{
	struct iyesde *iyesde = d_iyesde(dentry);

	dir->i_ctime = dir->i_mtime = dir->i_atime = current_time(dir);
	dir->i_size -= DIRENT_SIZE;
	drop_nlink(iyesde);
	dput(dentry);
	return 0;
}

/*
*	This is routine for system read from queue file.
*	To avoid mess with doing here some sort of mq_receive we allow
*	to read only queue size & yestification info (the only values
*	that are interesting from user point of view and aren't accessible
*	through std routines)
*/
static ssize_t mqueue_read_file(struct file *filp, char __user *u_data,
				size_t count, loff_t *off)
{
	struct mqueue_iyesde_info *info = MQUEUE_I(file_iyesde(filp));
	char buffer[FILENT_SIZE];
	ssize_t ret;

	spin_lock(&info->lock);
	snprintf(buffer, sizeof(buffer),
			"QSIZE:%-10lu NOTIFY:%-5d SIGNO:%-5d NOTIFY_PID:%-6d\n",
			info->qsize,
			info->yestify_owner ? info->yestify.sigev_yestify : 0,
			(info->yestify_owner &&
			 info->yestify.sigev_yestify == SIGEV_SIGNAL) ?
				info->yestify.sigev_sigyes : 0,
			pid_vnr(info->yestify_owner));
	spin_unlock(&info->lock);
	buffer[sizeof(buffer)-1] = '\0';

	ret = simple_read_from_buffer(u_data, count, off, buffer,
				strlen(buffer));
	if (ret <= 0)
		return ret;

	file_iyesde(filp)->i_atime = file_iyesde(filp)->i_ctime = current_time(file_iyesde(filp));
	return ret;
}

static int mqueue_flush_file(struct file *filp, fl_owner_t id)
{
	struct mqueue_iyesde_info *info = MQUEUE_I(file_iyesde(filp));

	spin_lock(&info->lock);
	if (task_tgid(current) == info->yestify_owner)
		remove_yestification(info);

	spin_unlock(&info->lock);
	return 0;
}

static __poll_t mqueue_poll_file(struct file *filp, struct poll_table_struct *poll_tab)
{
	struct mqueue_iyesde_info *info = MQUEUE_I(file_iyesde(filp));
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
static void wq_add(struct mqueue_iyesde_info *info, int sr,
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
static int wq_sleep(struct mqueue_iyesde_info *info, int sr,
		    ktime_t *timeout, struct ext_wait_queue *ewp)
	__releases(&info->lock)
{
	int retval;
	signed long time;

	wq_add(info, sr, ewp);

	for (;;) {
		__set_current_state(TASK_INTERRUPTIBLE);

		spin_unlock(&info->lock);
		time = schedule_hrtimeout_range_clock(timeout, 0,
			HRTIMER_MODE_ABS, CLOCK_REALTIME);

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
 * Returns waiting task that should be serviced first or NULL if yesne exists
 */
static struct ext_wait_queue *wq_get_first_waiter(
		struct mqueue_iyesde_info *info, int sr)
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
static void __do_yestify(struct mqueue_iyesde_info *info)
{
	/* yestification
	 * invoked when there is registered process and there isn't process
	 * waiting synchroyesusly for message AND state of queue changed from
	 * empty to yest empty. Here we are sure that yes one is waiting
	 * synchroyesusly. */
	if (info->yestify_owner &&
	    info->attr.mq_curmsgs == 1) {
		struct kernel_siginfo sig_i;
		switch (info->yestify.sigev_yestify) {
		case SIGEV_NONE:
			break;
		case SIGEV_SIGNAL:
			/* sends signal */

			clear_siginfo(&sig_i);
			sig_i.si_sigyes = info->yestify.sigev_sigyes;
			sig_i.si_erryes = 0;
			sig_i.si_code = SI_MESGQ;
			sig_i.si_value = info->yestify.sigev_value;
			/* map current pid/uid into info->owner's namespaces */
			rcu_read_lock();
			sig_i.si_pid = task_tgid_nr_ns(current,
						ns_of_pid(info->yestify_owner));
			sig_i.si_uid = from_kuid_munged(info->yestify_user_ns, current_uid());
			rcu_read_unlock();

			kill_pid_info(info->yestify.sigev_sigyes,
				      &sig_i, info->yestify_owner);
			break;
		case SIGEV_THREAD:
			set_cookie(info->yestify_cookie, NOTIFY_WOKENUP);
			netlink_sendskb(info->yestify_sock, info->yestify_cookie);
			break;
		}
		/* after yestification unregisters process */
		put_pid(info->yestify_owner);
		put_user_ns(info->yestify_user_ns);
		info->yestify_owner = NULL;
		info->yestify_user_ns = NULL;
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

static void remove_yestification(struct mqueue_iyesde_info *info)
{
	if (info->yestify_owner != NULL &&
	    info->yestify.sigev_yestify == SIGEV_THREAD) {
		set_cookie(info->yestify_cookie, NOTIFY_REMOVED);
		netlink_sendskb(info->yestify_sock, info->yestify_cookie);
	}
	put_pid(info->yestify_owner);
	put_user_ns(info->yestify_user_ns);
	info->yestify_owner = NULL;
	info->yestify_user_ns = NULL;
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
		audit_iyesde_parent_hidden(name, dentry->d_parent);
		return vfs_mkobj(dentry, mode & ~current_umask(),
				  mqueue_create_attr, attr);
	}
	/* it already existed */
	audit_iyesde(name, dentry, 0);
	if ((oflag & (O_CREAT|O_EXCL)) == (O_CREAT|O_EXCL))
		return -EEXIST;
	if ((oflag & O_ACCMODE) == (O_RDWR | O_WRONLY))
		return -EINVAL;
	acc = oflag2acc[oflag & O_ACCMODE];
	return iyesde_permission(d_iyesde(dentry), acc);
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

	if (IS_ERR(name = getname(u_name)))
		return PTR_ERR(name);

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0)
		goto out_putname;

	ro = mnt_want_write(mnt);	/* we'll drop it in any case */
	iyesde_lock(d_iyesde(root));
	path.dentry = lookup_one_len(name->name, root, strlen(name->name));
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
	iyesde_unlock(d_iyesde(root));
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
	struct iyesde *iyesde = NULL;
	struct ipc_namespace *ipc_ns = current->nsproxy->ipc_ns;
	struct vfsmount *mnt = ipc_ns->mq_mnt;

	name = getname(u_name);
	if (IS_ERR(name))
		return PTR_ERR(name);

	audit_iyesde_parent_hidden(name, mnt->mnt_root);
	err = mnt_want_write(mnt);
	if (err)
		goto out_name;
	iyesde_lock_nested(d_iyesde(mnt->mnt_root), I_MUTEX_PARENT);
	dentry = lookup_one_len(name->name, mnt->mnt_root,
				strlen(name->name));
	if (IS_ERR(dentry)) {
		err = PTR_ERR(dentry);
		goto out_unlock;
	}

	iyesde = d_iyesde(dentry);
	if (!iyesde) {
		err = -ENOENT;
	} else {
		ihold(iyesde);
		err = vfs_unlink(d_iyesde(dentry->d_parent), dentry, NULL);
	}
	dput(dentry);

out_unlock:
	iyesde_unlock(d_iyesde(mnt->mnt_root));
	if (iyesde)
		iput(iyesde);
	mnt_drop_write(mnt);
out_name:
	putname(name);

	return err;
}

/* Pipelined send and receive functions.
 *
 * If a receiver finds yes waiting message, then it registers itself in the
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

/* pipelined_send() - send a message directly to the task waiting in
 * sys_mq_timedreceive() (without inserting message into a queue).
 */
static inline void pipelined_send(struct wake_q_head *wake_q,
				  struct mqueue_iyesde_info *info,
				  struct msg_msg *message,
				  struct ext_wait_queue *receiver)
{
	receiver->msg = message;
	list_del(&receiver->list);
	wake_q_add(wake_q, receiver->task);
	/*
	 * Rely on the implicit cmpxchg barrier from wake_q_add such
	 * that we can ensure that updating receiver->state is the last
	 * write operation: As once set, the receiver can continue,
	 * and if we don't have the reference count from the wake_q,
	 * yet, at that point we can later have a use-after-free
	 * condition and bogus wakeup.
	 */
	receiver->state = STATE_READY;
}

/* pipelined_receive() - if there is task waiting in sys_mq_timedsend()
 * gets its message and put to the queue (we have one free place for sure). */
static inline void pipelined_receive(struct wake_q_head *wake_q,
				     struct mqueue_iyesde_info *info)
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
	wake_q_add(wake_q, sender->task);
	sender->state = STATE_READY;
}

static int do_mq_timedsend(mqd_t mqdes, const char __user *u_msg_ptr,
		size_t msg_len, unsigned int msg_prio,
		struct timespec64 *ts)
{
	struct fd f;
	struct iyesde *iyesde;
	struct ext_wait_queue wait;
	struct ext_wait_queue *receiver;
	struct msg_msg *msg_ptr;
	struct mqueue_iyesde_info *info;
	ktime_t expires, *timeout = NULL;
	struct posix_msg_tree_yesde *new_leaf = NULL;
	int ret = 0;
	DEFINE_WAKE_Q(wake_q);

	if (unlikely(msg_prio >= (unsigned long) MQ_PRIO_MAX))
		return -EINVAL;

	if (ts) {
		expires = timespec64_to_ktime(*ts);
		timeout = &expires;
	}

	audit_mq_sendrecv(mqdes, msg_len, msg_prio, ts);

	f = fdget(mqdes);
	if (unlikely(!f.file)) {
		ret = -EBADF;
		goto out;
	}

	iyesde = file_iyesde(f.file);
	if (unlikely(f.file->f_op != &mqueue_file_operations)) {
		ret = -EBADF;
		goto out_fput;
	}
	info = MQUEUE_I(iyesde);
	audit_file(f.file);

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
	 * msg_insert really wants us to have a valid, spare yesde struct so
	 * it doesn't have to kmalloc a GFP_ATOMIC allocation, but it will
	 * fall back to that if necessary.
	 */
	if (!info->yesde_cache)
		new_leaf = kmalloc(sizeof(*new_leaf), GFP_KERNEL);

	spin_lock(&info->lock);

	if (!info->yesde_cache && new_leaf) {
		/* Save our speculative allocation into the cache */
		INIT_LIST_HEAD(&new_leaf->msg_list);
		info->yesde_cache = new_leaf;
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
			pipelined_send(&wake_q, info, msg_ptr, receiver);
		} else {
			/* adds message to the queue */
			ret = msg_insert(msg_ptr, info);
			if (ret)
				goto out_unlock;
			__do_yestify(info);
		}
		iyesde->i_atime = iyesde->i_mtime = iyesde->i_ctime =
				current_time(iyesde);
	}
out_unlock:
	spin_unlock(&info->lock);
	wake_up_q(&wake_q);
out_free:
	if (ret)
		free_msg(msg_ptr);
out_fput:
	fdput(f);
out:
	return ret;
}

static int do_mq_timedreceive(mqd_t mqdes, char __user *u_msg_ptr,
		size_t msg_len, unsigned int __user *u_msg_prio,
		struct timespec64 *ts)
{
	ssize_t ret;
	struct msg_msg *msg_ptr;
	struct fd f;
	struct iyesde *iyesde;
	struct mqueue_iyesde_info *info;
	struct ext_wait_queue wait;
	ktime_t expires, *timeout = NULL;
	struct posix_msg_tree_yesde *new_leaf = NULL;

	if (ts) {
		expires = timespec64_to_ktime(*ts);
		timeout = &expires;
	}

	audit_mq_sendrecv(mqdes, msg_len, 0, ts);

	f = fdget(mqdes);
	if (unlikely(!f.file)) {
		ret = -EBADF;
		goto out;
	}

	iyesde = file_iyesde(f.file);
	if (unlikely(f.file->f_op != &mqueue_file_operations)) {
		ret = -EBADF;
		goto out_fput;
	}
	info = MQUEUE_I(iyesde);
	audit_file(f.file);

	if (unlikely(!(f.file->f_mode & FMODE_READ))) {
		ret = -EBADF;
		goto out_fput;
	}

	/* checks if buffer is big eyesugh */
	if (unlikely(msg_len < info->attr.mq_msgsize)) {
		ret = -EMSGSIZE;
		goto out_fput;
	}

	/*
	 * msg_insert really wants us to have a valid, spare yesde struct so
	 * it doesn't have to kmalloc a GFP_ATOMIC allocation, but it will
	 * fall back to that if necessary.
	 */
	if (!info->yesde_cache)
		new_leaf = kmalloc(sizeof(*new_leaf), GFP_KERNEL);

	spin_lock(&info->lock);

	if (!info->yesde_cache && new_leaf) {
		/* Save our speculative allocation into the cache */
		INIT_LIST_HEAD(&new_leaf->msg_list);
		info->yesde_cache = new_leaf;
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
		DEFINE_WAKE_Q(wake_q);

		msg_ptr = msg_get(info);

		iyesde->i_atime = iyesde->i_mtime = iyesde->i_ctime =
				current_time(iyesde);

		/* There is yesw free space in queue. */
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
out_fput:
	fdput(f);
out:
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
 * and he isn't currently owner of yestification, will be silently discarded.
 * It isn't explicitly defined in the POSIX.
 */
static int do_mq_yestify(mqd_t mqdes, const struct sigevent *yestification)
{
	int ret;
	struct fd f;
	struct sock *sock;
	struct iyesde *iyesde;
	struct mqueue_iyesde_info *info;
	struct sk_buff *nc;

	audit_mq_yestify(mqdes, yestification);

	nc = NULL;
	sock = NULL;
	if (yestification != NULL) {
		if (unlikely(yestification->sigev_yestify != SIGEV_NONE &&
			     yestification->sigev_yestify != SIGEV_SIGNAL &&
			     yestification->sigev_yestify != SIGEV_THREAD))
			return -EINVAL;
		if (yestification->sigev_yestify == SIGEV_SIGNAL &&
			!valid_signal(yestification->sigev_sigyes)) {
			return -EINVAL;
		}
		if (yestification->sigev_yestify == SIGEV_THREAD) {
			long timeo;

			/* create the yestify skb */
			nc = alloc_skb(NOTIFY_COOKIE_LEN, GFP_KERNEL);
			if (!nc)
				return -ENOMEM;

			if (copy_from_user(nc->data,
					yestification->sigev_value.sival_ptr,
					NOTIFY_COOKIE_LEN)) {
				ret = -EFAULT;
				goto free_skb;
			}

			/* TODO: add a header? */
			skb_put(nc, NOTIFY_COOKIE_LEN);
			/* and attach it to the socket */
retry:
			f = fdget(yestification->sigev_sigyes);
			if (!f.file) {
				ret = -EBADF;
				goto out;
			}
			sock = netlink_getsockbyfilp(f.file);
			fdput(f);
			if (IS_ERR(sock)) {
				ret = PTR_ERR(sock);
				goto free_skb;
			}

			timeo = MAX_SCHEDULE_TIMEOUT;
			ret = netlink_attachskb(sock, nc, &timeo, NULL);
			if (ret == 1) {
				sock = NULL;
				goto retry;
			}
			if (ret)
				return ret;
		}
	}

	f = fdget(mqdes);
	if (!f.file) {
		ret = -EBADF;
		goto out;
	}

	iyesde = file_iyesde(f.file);
	if (unlikely(f.file->f_op != &mqueue_file_operations)) {
		ret = -EBADF;
		goto out_fput;
	}
	info = MQUEUE_I(iyesde);

	ret = 0;
	spin_lock(&info->lock);
	if (yestification == NULL) {
		if (info->yestify_owner == task_tgid(current)) {
			remove_yestification(info);
			iyesde->i_atime = iyesde->i_ctime = current_time(iyesde);
		}
	} else if (info->yestify_owner != NULL) {
		ret = -EBUSY;
	} else {
		switch (yestification->sigev_yestify) {
		case SIGEV_NONE:
			info->yestify.sigev_yestify = SIGEV_NONE;
			break;
		case SIGEV_THREAD:
			info->yestify_sock = sock;
			info->yestify_cookie = nc;
			sock = NULL;
			nc = NULL;
			info->yestify.sigev_yestify = SIGEV_THREAD;
			break;
		case SIGEV_SIGNAL:
			info->yestify.sigev_sigyes = yestification->sigev_sigyes;
			info->yestify.sigev_value = yestification->sigev_value;
			info->yestify.sigev_yestify = SIGEV_SIGNAL;
			break;
		}

		info->yestify_owner = get_pid(task_tgid(current));
		info->yestify_user_ns = get_user_ns(current_user_ns());
		iyesde->i_atime = iyesde->i_ctime = current_time(iyesde);
	}
	spin_unlock(&info->lock);
out_fput:
	fdput(f);
out:
	if (sock)
		netlink_detachskb(sock, nc);
	else
free_skb:
		dev_kfree_skb(nc);

	return ret;
}

SYSCALL_DEFINE2(mq_yestify, mqd_t, mqdes,
		const struct sigevent __user *, u_yestification)
{
	struct sigevent n, *p = NULL;
	if (u_yestification) {
		if (copy_from_user(&n, u_yestification, sizeof(struct sigevent)))
			return -EFAULT;
		p = &n;
	}
	return do_mq_yestify(mqdes, p);
}

static int do_mq_getsetattr(int mqdes, struct mq_attr *new, struct mq_attr *old)
{
	struct fd f;
	struct iyesde *iyesde;
	struct mqueue_iyesde_info *info;

	if (new && (new->mq_flags & (~O_NONBLOCK)))
		return -EINVAL;

	f = fdget(mqdes);
	if (!f.file)
		return -EBADF;

	if (unlikely(f.file->f_op != &mqueue_file_operations)) {
		fdput(f);
		return -EBADF;
	}

	iyesde = file_iyesde(f.file);
	info = MQUEUE_I(iyesde);

	spin_lock(&info->lock);

	if (old) {
		*old = info->attr;
		old->mq_flags = f.file->f_flags & O_NONBLOCK;
	}
	if (new) {
		audit_mq_getsetattr(mqdes, new);
		spin_lock(&f.file->f_lock);
		if (new->mq_flags & O_NONBLOCK)
			f.file->f_flags |= O_NONBLOCK;
		else
			f.file->f_flags &= ~O_NONBLOCK;
		spin_unlock(&f.file->f_lock);

		iyesde->i_atime = iyesde->i_ctime = current_time(iyesde);
	}

	spin_unlock(&info->lock);
	fdput(f);
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
	compat_long_t __reserved[4]; /* igyesred for input, zeroed for output */
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

COMPAT_SYSCALL_DEFINE2(mq_yestify, mqd_t, mqdes,
		       const struct compat_sigevent __user *, u_yestification)
{
	struct sigevent n, *p = NULL;
	if (u_yestification) {
		if (get_compat_sigevent(&n, u_yestification))
			return -EFAULT;
		if (n.sigev_yestify == SIGEV_THREAD)
			n.sigev_value.sival_ptr = compat_ptr(n.sigev_value.sival_int);
		p = &n;
	}
	return do_mq_yestify(mqdes, p);
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

static const struct iyesde_operations mqueue_dir_iyesde_operations = {
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
	.alloc_iyesde = mqueue_alloc_iyesde,
	.free_iyesde = mqueue_free_iyesde,
	.evict_iyesde = mqueue_evict_iyesde,
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

void mq_put_mnt(struct ipc_namespace *ns)
{
	kern_unmount(ns->mq_mnt);
}

static int __init init_mqueue_fs(void)
{
	int error;

	mqueue_iyesde_cachep = kmem_cache_create("mqueue_iyesde_cache",
				sizeof(struct mqueue_iyesde_info), 0,
				SLAB_HWCACHE_ALIGN|SLAB_ACCOUNT, init_once);
	if (mqueue_iyesde_cachep == NULL)
		return -ENOMEM;

	/* igyesre failures - they are yest fatal */
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
	kmem_cache_destroy(mqueue_iyesde_cachep);
	return error;
}

device_initcall(init_mqueue_fs);
