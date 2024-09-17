/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FILELOCK_H
#define _LINUX_FILELOCK_H

#include <linux/fs.h>

#define FL_POSIX	1
#define FL_FLOCK	2
#define FL_DELEG	4	/* NFSv4 delegation */
#define FL_ACCESS	8	/* not trying to lock, just looking */
#define FL_EXISTS	16	/* when unlocking, test for existence */
#define FL_LEASE	32	/* lease held on this file */
#define FL_CLOSE	64	/* unlock on close */
#define FL_SLEEP	128	/* A blocking lock */
#define FL_DOWNGRADE_PENDING	256 /* Lease is being downgraded */
#define FL_UNLOCK_PENDING	512 /* Lease is being broken */
#define FL_OFDLCK	1024	/* lock is "owned" by struct file */
#define FL_LAYOUT	2048	/* outstanding pNFS layout */
#define FL_RECLAIM	4096	/* reclaiming from a reboot server */

#define FL_CLOSE_POSIX (FL_POSIX | FL_CLOSE)

/*
 * Special return value from posix_lock_file() and vfs_lock_file() for
 * asynchronous locking.
 */
#define FILE_LOCK_DEFERRED 1

struct file_lock;
struct file_lease;

struct file_lock_operations {
	void (*fl_copy_lock)(struct file_lock *, struct file_lock *);
	void (*fl_release_private)(struct file_lock *);
};

struct lock_manager_operations {
	void *lm_mod_owner;
	fl_owner_t (*lm_get_owner)(fl_owner_t);
	void (*lm_put_owner)(fl_owner_t);
	void (*lm_notify)(struct file_lock *);	/* unblock callback */
	int (*lm_grant)(struct file_lock *, int);
	bool (*lm_lock_expirable)(struct file_lock *cfl);
	void (*lm_expire_lock)(void);
};

struct lease_manager_operations {
	bool (*lm_break)(struct file_lease *);
	int (*lm_change)(struct file_lease *, int, struct list_head *);
	void (*lm_setup)(struct file_lease *, void **);
	bool (*lm_breaker_owns_lease)(struct file_lease *);
};

struct lock_manager {
	struct list_head list;
	/*
	 * NFSv4 and up also want opens blocked during the grace period;
	 * NLM doesn't care:
	 */
	bool block_opens;
};

struct net;
void locks_start_grace(struct net *, struct lock_manager *);
void locks_end_grace(struct lock_manager *);
bool locks_in_grace(struct net *);
bool opens_in_grace(struct net *);

/*
 * struct file_lock has a union that some filesystems use to track
 * their own private info. The NFS side of things is defined here:
 */
#include <linux/nfs_fs_i.h>

/*
 * struct file_lock represents a generic "file lock". It's used to represent
 * POSIX byte range locks, BSD (flock) locks, and leases. It's important to
 * note that the same struct is used to represent both a request for a lock and
 * the lock itself, but the same object is never used for both.
 *
 * FIXME: should we create a separate "struct lock_request" to help distinguish
 * these two uses?
 *
 * The varous i_flctx lists are ordered by:
 *
 * 1) lock owner
 * 2) lock range start
 * 3) lock range end
 *
 * Obviously, the last two criteria only matter for POSIX locks.
 */

struct file_lock_core {
	struct file_lock_core *flc_blocker;	/* The lock that is blocking us */
	struct list_head flc_list;	/* link into file_lock_context */
	struct hlist_node flc_link;	/* node in global lists */
	struct list_head flc_blocked_requests;	/* list of requests with
						 * ->fl_blocker pointing here
						 */
	struct list_head flc_blocked_member;	/* node in
						 * ->fl_blocker->fl_blocked_requests
						 */
	fl_owner_t flc_owner;
	unsigned int flc_flags;
	unsigned char flc_type;
	pid_t flc_pid;
	int flc_link_cpu;		/* what cpu's list is this on? */
	wait_queue_head_t flc_wait;
	struct file *flc_file;
};

struct file_lock {
	struct file_lock_core c;
	loff_t fl_start;
	loff_t fl_end;

	const struct file_lock_operations *fl_ops;	/* Callbacks for filesystems */
	const struct lock_manager_operations *fl_lmops;	/* Callbacks for lockmanagers */
	union {
		struct nfs_lock_info	nfs_fl;
		struct nfs4_lock_info	nfs4_fl;
		struct {
			struct list_head link;	/* link in AFS vnode's pending_locks list */
			int state;		/* state of grant or error if -ve */
			unsigned int	debug_id;
		} afs;
		struct {
			struct inode *inode;
		} ceph;
	} fl_u;
} __randomize_layout;

struct file_lease {
	struct file_lock_core c;
	struct fasync_struct *	fl_fasync; /* for lease break notifications */
	/* for lease breaks: */
	unsigned long fl_break_time;
	unsigned long fl_downgrade_time;
	const struct lease_manager_operations *fl_lmops; /* Callbacks for lease managers */
} __randomize_layout;

struct file_lock_context {
	spinlock_t		flc_lock;
	struct list_head	flc_flock;
	struct list_head	flc_posix;
	struct list_head	flc_lease;
};

#ifdef CONFIG_FILE_LOCKING
int fcntl_getlk(struct file *, unsigned int, struct flock *);
int fcntl_setlk(unsigned int, struct file *, unsigned int,
			struct flock *);

#if BITS_PER_LONG == 32
int fcntl_getlk64(struct file *, unsigned int, struct flock64 *);
int fcntl_setlk64(unsigned int, struct file *, unsigned int,
			struct flock64 *);
#endif

int fcntl_setlease(unsigned int fd, struct file *filp, int arg);
int fcntl_getlease(struct file *filp);

static inline bool lock_is_unlock(struct file_lock *fl)
{
	return fl->c.flc_type == F_UNLCK;
}

static inline bool lock_is_read(struct file_lock *fl)
{
	return fl->c.flc_type == F_RDLCK;
}

static inline bool lock_is_write(struct file_lock *fl)
{
	return fl->c.flc_type == F_WRLCK;
}

static inline void locks_wake_up(struct file_lock *fl)
{
	wake_up(&fl->c.flc_wait);
}

/* fs/locks.c */
void locks_free_lock_context(struct inode *inode);
void locks_free_lock(struct file_lock *fl);
void locks_init_lock(struct file_lock *);
struct file_lock *locks_alloc_lock(void);
void locks_copy_lock(struct file_lock *, struct file_lock *);
void locks_copy_conflock(struct file_lock *, struct file_lock *);
void locks_remove_posix(struct file *, fl_owner_t);
void locks_remove_file(struct file *);
void locks_release_private(struct file_lock *);
void posix_test_lock(struct file *, struct file_lock *);
int posix_lock_file(struct file *, struct file_lock *, struct file_lock *);
int locks_delete_block(struct file_lock *);
int vfs_test_lock(struct file *, struct file_lock *);
int vfs_lock_file(struct file *, unsigned int, struct file_lock *, struct file_lock *);
int vfs_cancel_lock(struct file *filp, struct file_lock *fl);
bool vfs_inode_has_locks(struct inode *inode);
int locks_lock_inode_wait(struct inode *inode, struct file_lock *fl);

void locks_init_lease(struct file_lease *);
void locks_free_lease(struct file_lease *fl);
struct file_lease *locks_alloc_lease(void);
int __break_lease(struct inode *inode, unsigned int flags, unsigned int type);
void lease_get_mtime(struct inode *, struct timespec64 *time);
int generic_setlease(struct file *, int, struct file_lease **, void **priv);
int kernel_setlease(struct file *, int, struct file_lease **, void **);
int vfs_setlease(struct file *, int, struct file_lease **, void **);
int lease_modify(struct file_lease *, int, struct list_head *);

struct notifier_block;
int lease_register_notifier(struct notifier_block *);
void lease_unregister_notifier(struct notifier_block *);

struct files_struct;
void show_fd_locks(struct seq_file *f,
			 struct file *filp, struct files_struct *files);
bool locks_owner_has_blockers(struct file_lock_context *flctx,
			fl_owner_t owner);

static inline struct file_lock_context *
locks_inode_context(const struct inode *inode)
{
	return smp_load_acquire(&inode->i_flctx);
}

#else /* !CONFIG_FILE_LOCKING */
static inline int fcntl_getlk(struct file *file, unsigned int cmd,
			      struct flock __user *user)
{
	return -EINVAL;
}

static inline int fcntl_setlk(unsigned int fd, struct file *file,
			      unsigned int cmd, struct flock __user *user)
{
	return -EACCES;
}

#if BITS_PER_LONG == 32
static inline int fcntl_getlk64(struct file *file, unsigned int cmd,
				struct flock64 *user)
{
	return -EINVAL;
}

static inline int fcntl_setlk64(unsigned int fd, struct file *file,
				unsigned int cmd, struct flock64 *user)
{
	return -EACCES;
}
#endif
static inline int fcntl_setlease(unsigned int fd, struct file *filp, int arg)
{
	return -EINVAL;
}

static inline int fcntl_getlease(struct file *filp)
{
	return F_UNLCK;
}

static inline bool lock_is_unlock(struct file_lock *fl)
{
	return false;
}

static inline bool lock_is_read(struct file_lock *fl)
{
	return false;
}

static inline bool lock_is_write(struct file_lock *fl)
{
	return false;
}

static inline void locks_wake_up(struct file_lock *fl)
{
}

static inline void
locks_free_lock_context(struct inode *inode)
{
}

static inline void locks_init_lock(struct file_lock *fl)
{
	return;
}

static inline void locks_init_lease(struct file_lease *fl)
{
	return;
}

static inline void locks_copy_conflock(struct file_lock *new, struct file_lock *fl)
{
	return;
}

static inline void locks_copy_lock(struct file_lock *new, struct file_lock *fl)
{
	return;
}

static inline void locks_remove_posix(struct file *filp, fl_owner_t owner)
{
	return;
}

static inline void locks_remove_file(struct file *filp)
{
	return;
}

static inline void posix_test_lock(struct file *filp, struct file_lock *fl)
{
	return;
}

static inline int posix_lock_file(struct file *filp, struct file_lock *fl,
				  struct file_lock *conflock)
{
	return -ENOLCK;
}

static inline int locks_delete_block(struct file_lock *waiter)
{
	return -ENOENT;
}

static inline int vfs_test_lock(struct file *filp, struct file_lock *fl)
{
	return 0;
}

static inline int vfs_lock_file(struct file *filp, unsigned int cmd,
				struct file_lock *fl, struct file_lock *conf)
{
	return -ENOLCK;
}

static inline int vfs_cancel_lock(struct file *filp, struct file_lock *fl)
{
	return 0;
}

static inline bool vfs_inode_has_locks(struct inode *inode)
{
	return false;
}

static inline int locks_lock_inode_wait(struct inode *inode, struct file_lock *fl)
{
	return -ENOLCK;
}

static inline int __break_lease(struct inode *inode, unsigned int mode, unsigned int type)
{
	return 0;
}

static inline void lease_get_mtime(struct inode *inode,
				   struct timespec64 *time)
{
	return;
}

static inline int generic_setlease(struct file *filp, int arg,
				    struct file_lease **flp, void **priv)
{
	return -EINVAL;
}

static inline int kernel_setlease(struct file *filp, int arg,
			       struct file_lease **lease, void **priv)
{
	return -EINVAL;
}

static inline int vfs_setlease(struct file *filp, int arg,
			       struct file_lease **lease, void **priv)
{
	return -EINVAL;
}

static inline int lease_modify(struct file_lease *fl, int arg,
			       struct list_head *dispose)
{
	return -EINVAL;
}

struct files_struct;
static inline void show_fd_locks(struct seq_file *f,
			struct file *filp, struct files_struct *files) {}
static inline bool locks_owner_has_blockers(struct file_lock_context *flctx,
			fl_owner_t owner)
{
	return false;
}

static inline struct file_lock_context *
locks_inode_context(const struct inode *inode)
{
	return NULL;
}

#endif /* !CONFIG_FILE_LOCKING */

/* for walking lists of file_locks linked by fl_list */
#define for_each_file_lock(_fl, _head)	list_for_each_entry(_fl, _head, c.flc_list)

static inline int locks_lock_file_wait(struct file *filp, struct file_lock *fl)
{
	return locks_lock_inode_wait(file_inode(filp), fl);
}

#ifdef CONFIG_FILE_LOCKING
static inline int break_lease(struct inode *inode, unsigned int mode)
{
	struct file_lock_context *flctx;

	/*
	 * Since this check is lockless, we must ensure that any refcounts
	 * taken are done before checking i_flctx->flc_lease. Otherwise, we
	 * could end up racing with tasks trying to set a new lease on this
	 * file.
	 */
	flctx = READ_ONCE(inode->i_flctx);
	if (!flctx)
		return 0;
	smp_mb();
	if (!list_empty_careful(&flctx->flc_lease))
		return __break_lease(inode, mode, FL_LEASE);
	return 0;
}

static inline int break_deleg(struct inode *inode, unsigned int mode)
{
	struct file_lock_context *flctx;

	/*
	 * Since this check is lockless, we must ensure that any refcounts
	 * taken are done before checking i_flctx->flc_lease. Otherwise, we
	 * could end up racing with tasks trying to set a new lease on this
	 * file.
	 */
	flctx = READ_ONCE(inode->i_flctx);
	if (!flctx)
		return 0;
	smp_mb();
	if (!list_empty_careful(&flctx->flc_lease))
		return __break_lease(inode, mode, FL_DELEG);
	return 0;
}

static inline int try_break_deleg(struct inode *inode, struct inode **delegated_inode)
{
	int ret;

	ret = break_deleg(inode, O_WRONLY|O_NONBLOCK);
	if (ret == -EWOULDBLOCK && delegated_inode) {
		*delegated_inode = inode;
		ihold(inode);
	}
	return ret;
}

static inline int break_deleg_wait(struct inode **delegated_inode)
{
	int ret;

	ret = break_deleg(*delegated_inode, O_WRONLY);
	iput(*delegated_inode);
	*delegated_inode = NULL;
	return ret;
}

static inline int break_layout(struct inode *inode, bool wait)
{
	smp_mb();
	if (inode->i_flctx && !list_empty_careful(&inode->i_flctx->flc_lease))
		return __break_lease(inode,
				wait ? O_WRONLY : O_WRONLY | O_NONBLOCK,
				FL_LAYOUT);
	return 0;
}

#else /* !CONFIG_FILE_LOCKING */
static inline int break_lease(struct inode *inode, unsigned int mode)
{
	return 0;
}

static inline int break_deleg(struct inode *inode, unsigned int mode)
{
	return 0;
}

static inline int try_break_deleg(struct inode *inode, struct inode **delegated_inode)
{
	return 0;
}

static inline int break_deleg_wait(struct inode **delegated_inode)
{
	BUG();
	return 0;
}

static inline int break_layout(struct inode *inode, bool wait)
{
	return 0;
}

#endif /* CONFIG_FILE_LOCKING */

#endif /* _LINUX_FILELOCK_H */
