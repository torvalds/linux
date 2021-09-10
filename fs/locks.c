// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/locks.c
 *
 *  Provide support for fcntl()'s F_GETLK, F_SETLK, and F_SETLKW calls.
 *  Doug Evans (dje@spiff.uucp), August 07, 1992
 *
 *  Deadlock detection added.
 *  FIXME: one thing isn't handled yet:
 *	- mandatory locks (requires lots of changes elsewhere)
 *  Kelly Carmichael (kelly@[142.24.8.65]), September 17, 1994.
 *
 *  Miscellaneous edits, and a total rewrite of posix_lock_file() code.
 *  Kai Petzke (wpp@marie.physik.tu-berlin.de), 1994
 *
 *  Converted file_lock_table to a linked list from an array, which eliminates
 *  the limits on how many active file locks are open.
 *  Chad Page (pageone@netcom.com), November 27, 1994
 *
 *  Removed dependency on file descriptors. dup()'ed file descriptors now
 *  get the same locks as the original file descriptors, and a close() on
 *  any file descriptor removes ALL the locks on the file for the current
 *  process. Since locks still depend on the process id, locks are inherited
 *  after an exec() but not after a fork(). This agrees with POSIX, and both
 *  BSD and SVR4 practice.
 *  Andy Walker (andy@lysaker.kvaerner.no), February 14, 1995
 *
 *  Scrapped free list which is redundant now that we allocate locks
 *  dynamically with kmalloc()/kfree().
 *  Andy Walker (andy@lysaker.kvaerner.no), February 21, 1995
 *
 *  Implemented two lock personalities - FL_FLOCK and FL_POSIX.
 *
 *  FL_POSIX locks are created with calls to fcntl() and lockf() through the
 *  fcntl() system call. They have the semantics described above.
 *
 *  FL_FLOCK locks are created with calls to flock(), through the flock()
 *  system call, which is new. Old C libraries implement flock() via fcntl()
 *  and will continue to use the old, broken implementation.
 *
 *  FL_FLOCK locks follow the 4.4 BSD flock() semantics. They are associated
 *  with a file pointer (filp). As a result they can be shared by a parent
 *  process and its children after a fork(). They are removed when the last
 *  file descriptor referring to the file pointer is closed (unless explicitly
 *  unlocked).
 *
 *  FL_FLOCK locks never deadlock, an existing lock is always removed before
 *  upgrading from shared to exclusive (or vice versa). When this happens
 *  any processes blocked by the current lock are woken up and allowed to
 *  run before the new lock is applied.
 *  Andy Walker (andy@lysaker.kvaerner.no), June 09, 1995
 *
 *  Removed some race conditions in flock_lock_file(), marked other possible
 *  races. Just grep for FIXME to see them.
 *  Dmitry Gorodchanin (pgmdsg@ibi.com), February 09, 1996.
 *
 *  Addressed Dmitry's concerns. Deadlock checking no longer recursive.
 *  Lock allocation changed to GFP_ATOMIC as we can't afford to sleep
 *  once we've checked for blocking and deadlocking.
 *  Andy Walker (andy@lysaker.kvaerner.no), April 03, 1996.
 *
 *  Initial implementation of mandatory locks. SunOS turned out to be
 *  a rotten model, so I implemented the "obvious" semantics.
 *  See 'Documentation/filesystems/mandatory-locking.rst' for details.
 *  Andy Walker (andy@lysaker.kvaerner.no), April 06, 1996.
 *
 *  Don't allow mandatory locks on mmap()'ed files. Added simple functions to
 *  check if a file has mandatory locks, used by mmap(), open() and creat() to
 *  see if system call should be rejected. Ref. HP-UX/SunOS/Solaris Reference
 *  Manual, Section 2.
 *  Andy Walker (andy@lysaker.kvaerner.no), April 09, 1996.
 *
 *  Tidied up block list handling. Added '/proc/locks' interface.
 *  Andy Walker (andy@lysaker.kvaerner.no), April 24, 1996.
 *
 *  Fixed deadlock condition for pathological code that mixes calls to
 *  flock() and fcntl().
 *  Andy Walker (andy@lysaker.kvaerner.no), April 29, 1996.
 *
 *  Allow only one type of locking scheme (FL_POSIX or FL_FLOCK) to be in use
 *  for a given file at a time. Changed the CONFIG_LOCK_MANDATORY scheme to
 *  guarantee sensible behaviour in the case where file system modules might
 *  be compiled with different options than the kernel itself.
 *  Andy Walker (andy@lysaker.kvaerner.no), May 15, 1996.
 *
 *  Added a couple of missing wake_up() calls. Thanks to Thomas Meckel
 *  (Thomas.Meckel@mni.fh-giessen.de) for spotting this.
 *  Andy Walker (andy@lysaker.kvaerner.no), May 15, 1996.
 *
 *  Changed FL_POSIX locks to use the block list in the same way as FL_FLOCK
 *  locks. Changed process synchronisation to avoid dereferencing locks that
 *  have already been freed.
 *  Andy Walker (andy@lysaker.kvaerner.no), Sep 21, 1996.
 *
 *  Made the block list a circular list to minimise searching in the list.
 *  Andy Walker (andy@lysaker.kvaerner.no), Sep 25, 1996.
 *
 *  Made mandatory locking a mount option. Default is not to allow mandatory
 *  locking.
 *  Andy Walker (andy@lysaker.kvaerner.no), Oct 04, 1996.
 *
 *  Some adaptations for NFS support.
 *  Olaf Kirch (okir@monad.swb.de), Dec 1996,
 *
 *  Fixed /proc/locks interface so that we can't overrun the buffer we are handed.
 *  Andy Walker (andy@lysaker.kvaerner.no), May 12, 1997.
 *
 *  Use slab allocator instead of kmalloc/kfree.
 *  Use generic list implementation from <linux/list.h>.
 *  Sped up posix_locks_deadlock by only considering blocked locks.
 *  Matthew Wilcox <willy@debian.org>, March, 2000.
 *
 *  Leases and LOCK_MAND
 *  Matthew Wilcox <willy@debian.org>, June, 2000.
 *  Stephen Rothwell <sfr@canb.auug.org.au>, June, 2000.
 *
 * Locking conflicts and dependencies:
 * If multiple threads attempt to lock the same byte (or flock the same file)
 * only one can be granted the lock, and other must wait their turn.
 * The first lock has been "applied" or "granted", the others are "waiting"
 * and are "blocked" by the "applied" lock..
 *
 * Waiting and applied locks are all kept in trees whose properties are:
 *
 *	- the root of a tree may be an applied or waiting lock.
 *	- every other node in the tree is a waiting lock that
 *	  conflicts with every ancestor of that node.
 *
 * Every such tree begins life as a waiting singleton which obviously
 * satisfies the above properties.
 *
 * The only ways we modify trees preserve these properties:
 *
 *	1. We may add a new leaf node, but only after first verifying that it
 *	   conflicts with all of its ancestors.
 *	2. We may remove the root of a tree, creating a new singleton
 *	   tree from the root and N new trees rooted in the immediate
 *	   children.
 *	3. If the root of a tree is not currently an applied lock, we may
 *	   apply it (if possible).
 *	4. We may upgrade the root of the tree (either extend its range,
 *	   or upgrade its entire range from read to write).
 *
 * When an applied lock is modified in a way that reduces or downgrades any
 * part of its range, we remove all its children (2 above).  This particularly
 * happens when a lock is unlocked.
 *
 * For each of those child trees we "wake up" the thread which is
 * waiting for the lock so it can continue handling as follows: if the
 * root of the tree applies, we do so (3).  If it doesn't, it must
 * conflict with some applied lock.  We remove (wake up) all of its children
 * (2), and add it is a new leaf to the tree rooted in the applied
 * lock (1).  We then repeat the process recursively with those
 * children.
 *
 */

#include <linux/capability.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/security.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/time.h>
#include <linux/rcupdate.h>
#include <linux/pid_namespace.h>
#include <linux/hashtable.h>
#include <linux/percpu.h>

#define CREATE_TRACE_POINTS
#include <trace/events/filelock.h>

#include <linux/uaccess.h>

#define IS_POSIX(fl)	(fl->fl_flags & FL_POSIX)
#define IS_FLOCK(fl)	(fl->fl_flags & FL_FLOCK)
#define IS_LEASE(fl)	(fl->fl_flags & (FL_LEASE|FL_DELEG|FL_LAYOUT))
#define IS_OFDLCK(fl)	(fl->fl_flags & FL_OFDLCK)
#define IS_REMOTELCK(fl)	(fl->fl_pid <= 0)

static bool lease_breaking(struct file_lock *fl)
{
	return fl->fl_flags & (FL_UNLOCK_PENDING | FL_DOWNGRADE_PENDING);
}

static int target_leasetype(struct file_lock *fl)
{
	if (fl->fl_flags & FL_UNLOCK_PENDING)
		return F_UNLCK;
	if (fl->fl_flags & FL_DOWNGRADE_PENDING)
		return F_RDLCK;
	return fl->fl_type;
}

int leases_enable = 1;
int lease_break_time = 45;

/*
 * The global file_lock_list is only used for displaying /proc/locks, so we
 * keep a list on each CPU, with each list protected by its own spinlock.
 * Global serialization is done using file_rwsem.
 *
 * Note that alterations to the list also require that the relevant flc_lock is
 * held.
 */
struct file_lock_list_struct {
	spinlock_t		lock;
	struct hlist_head	hlist;
};
static DEFINE_PER_CPU(struct file_lock_list_struct, file_lock_list);
DEFINE_STATIC_PERCPU_RWSEM(file_rwsem);


/*
 * The blocked_hash is used to find POSIX lock loops for deadlock detection.
 * It is protected by blocked_lock_lock.
 *
 * We hash locks by lockowner in order to optimize searching for the lock a
 * particular lockowner is waiting on.
 *
 * FIXME: make this value scale via some heuristic? We generally will want more
 * buckets when we have more lockowners holding locks, but that's a little
 * difficult to determine without knowing what the workload will look like.
 */
#define BLOCKED_HASH_BITS	7
static DEFINE_HASHTABLE(blocked_hash, BLOCKED_HASH_BITS);

/*
 * This lock protects the blocked_hash. Generally, if you're accessing it, you
 * want to be holding this lock.
 *
 * In addition, it also protects the fl->fl_blocked_requests list, and the
 * fl->fl_blocker pointer for file_lock structures that are acting as lock
 * requests (in contrast to those that are acting as records of acquired locks).
 *
 * Note that when we acquire this lock in order to change the above fields,
 * we often hold the flc_lock as well. In certain cases, when reading the fields
 * protected by this lock, we can skip acquiring it iff we already hold the
 * flc_lock.
 */
static DEFINE_SPINLOCK(blocked_lock_lock);

static struct kmem_cache *flctx_cache __read_mostly;
static struct kmem_cache *filelock_cache __read_mostly;

static struct file_lock_context *
locks_get_lock_context(struct inode *inode, int type)
{
	struct file_lock_context *ctx;

	/* paired with cmpxchg() below */
	ctx = smp_load_acquire(&inode->i_flctx);
	if (likely(ctx) || type == F_UNLCK)
		goto out;

	ctx = kmem_cache_alloc(flctx_cache, GFP_KERNEL);
	if (!ctx)
		goto out;

	spin_lock_init(&ctx->flc_lock);
	INIT_LIST_HEAD(&ctx->flc_flock);
	INIT_LIST_HEAD(&ctx->flc_posix);
	INIT_LIST_HEAD(&ctx->flc_lease);

	/*
	 * Assign the pointer if it's not already assigned. If it is, then
	 * free the context we just allocated.
	 */
	if (cmpxchg(&inode->i_flctx, NULL, ctx)) {
		kmem_cache_free(flctx_cache, ctx);
		ctx = smp_load_acquire(&inode->i_flctx);
	}
out:
	trace_locks_get_lock_context(inode, type, ctx);
	return ctx;
}

static void
locks_dump_ctx_list(struct list_head *list, char *list_type)
{
	struct file_lock *fl;

	list_for_each_entry(fl, list, fl_list) {
		pr_warn("%s: fl_owner=%p fl_flags=0x%x fl_type=0x%x fl_pid=%u\n", list_type, fl->fl_owner, fl->fl_flags, fl->fl_type, fl->fl_pid);
	}
}

static void
locks_check_ctx_lists(struct inode *inode)
{
	struct file_lock_context *ctx = inode->i_flctx;

	if (unlikely(!list_empty(&ctx->flc_flock) ||
		     !list_empty(&ctx->flc_posix) ||
		     !list_empty(&ctx->flc_lease))) {
		pr_warn("Leaked locks on dev=0x%x:0x%x ino=0x%lx:\n",
			MAJOR(inode->i_sb->s_dev), MINOR(inode->i_sb->s_dev),
			inode->i_ino);
		locks_dump_ctx_list(&ctx->flc_flock, "FLOCK");
		locks_dump_ctx_list(&ctx->flc_posix, "POSIX");
		locks_dump_ctx_list(&ctx->flc_lease, "LEASE");
	}
}

static void
locks_check_ctx_file_list(struct file *filp, struct list_head *list,
				char *list_type)
{
	struct file_lock *fl;
	struct inode *inode = locks_inode(filp);

	list_for_each_entry(fl, list, fl_list)
		if (fl->fl_file == filp)
			pr_warn("Leaked %s lock on dev=0x%x:0x%x ino=0x%lx "
				" fl_owner=%p fl_flags=0x%x fl_type=0x%x fl_pid=%u\n",
				list_type, MAJOR(inode->i_sb->s_dev),
				MINOR(inode->i_sb->s_dev), inode->i_ino,
				fl->fl_owner, fl->fl_flags, fl->fl_type, fl->fl_pid);
}

void
locks_free_lock_context(struct inode *inode)
{
	struct file_lock_context *ctx = inode->i_flctx;

	if (unlikely(ctx)) {
		locks_check_ctx_lists(inode);
		kmem_cache_free(flctx_cache, ctx);
	}
}

static void locks_init_lock_heads(struct file_lock *fl)
{
	INIT_HLIST_NODE(&fl->fl_link);
	INIT_LIST_HEAD(&fl->fl_list);
	INIT_LIST_HEAD(&fl->fl_blocked_requests);
	INIT_LIST_HEAD(&fl->fl_blocked_member);
	init_waitqueue_head(&fl->fl_wait);
}

/* Allocate an empty lock structure. */
struct file_lock *locks_alloc_lock(void)
{
	struct file_lock *fl = kmem_cache_zalloc(filelock_cache, GFP_KERNEL);

	if (fl)
		locks_init_lock_heads(fl);

	return fl;
}
EXPORT_SYMBOL_GPL(locks_alloc_lock);

void locks_release_private(struct file_lock *fl)
{
	BUG_ON(waitqueue_active(&fl->fl_wait));
	BUG_ON(!list_empty(&fl->fl_list));
	BUG_ON(!list_empty(&fl->fl_blocked_requests));
	BUG_ON(!list_empty(&fl->fl_blocked_member));
	BUG_ON(!hlist_unhashed(&fl->fl_link));

	if (fl->fl_ops) {
		if (fl->fl_ops->fl_release_private)
			fl->fl_ops->fl_release_private(fl);
		fl->fl_ops = NULL;
	}

	if (fl->fl_lmops) {
		if (fl->fl_lmops->lm_put_owner) {
			fl->fl_lmops->lm_put_owner(fl->fl_owner);
			fl->fl_owner = NULL;
		}
		fl->fl_lmops = NULL;
	}
}
EXPORT_SYMBOL_GPL(locks_release_private);

/* Free a lock which is not in use. */
void locks_free_lock(struct file_lock *fl)
{
	locks_release_private(fl);
	kmem_cache_free(filelock_cache, fl);
}
EXPORT_SYMBOL(locks_free_lock);

static void
locks_dispose_list(struct list_head *dispose)
{
	struct file_lock *fl;

	while (!list_empty(dispose)) {
		fl = list_first_entry(dispose, struct file_lock, fl_list);
		list_del_init(&fl->fl_list);
		locks_free_lock(fl);
	}
}

void locks_init_lock(struct file_lock *fl)
{
	memset(fl, 0, sizeof(struct file_lock));
	locks_init_lock_heads(fl);
}
EXPORT_SYMBOL(locks_init_lock);

/*
 * Initialize a new lock from an existing file_lock structure.
 */
void locks_copy_conflock(struct file_lock *new, struct file_lock *fl)
{
	new->fl_owner = fl->fl_owner;
	new->fl_pid = fl->fl_pid;
	new->fl_file = NULL;
	new->fl_flags = fl->fl_flags;
	new->fl_type = fl->fl_type;
	new->fl_start = fl->fl_start;
	new->fl_end = fl->fl_end;
	new->fl_lmops = fl->fl_lmops;
	new->fl_ops = NULL;

	if (fl->fl_lmops) {
		if (fl->fl_lmops->lm_get_owner)
			fl->fl_lmops->lm_get_owner(fl->fl_owner);
	}
}
EXPORT_SYMBOL(locks_copy_conflock);

void locks_copy_lock(struct file_lock *new, struct file_lock *fl)
{
	/* "new" must be a freshly-initialized lock */
	WARN_ON_ONCE(new->fl_ops);

	locks_copy_conflock(new, fl);

	new->fl_file = fl->fl_file;
	new->fl_ops = fl->fl_ops;

	if (fl->fl_ops) {
		if (fl->fl_ops->fl_copy_lock)
			fl->fl_ops->fl_copy_lock(new, fl);
	}
}
EXPORT_SYMBOL(locks_copy_lock);

static void locks_move_blocks(struct file_lock *new, struct file_lock *fl)
{
	struct file_lock *f;

	/*
	 * As ctx->flc_lock is held, new requests cannot be added to
	 * ->fl_blocked_requests, so we don't need a lock to check if it
	 * is empty.
	 */
	if (list_empty(&fl->fl_blocked_requests))
		return;
	spin_lock(&blocked_lock_lock);
	list_splice_init(&fl->fl_blocked_requests, &new->fl_blocked_requests);
	list_for_each_entry(f, &new->fl_blocked_requests, fl_blocked_member)
		f->fl_blocker = new;
	spin_unlock(&blocked_lock_lock);
}

static inline int flock_translate_cmd(int cmd) {
	switch (cmd) {
	case LOCK_SH:
		return F_RDLCK;
	case LOCK_EX:
		return F_WRLCK;
	case LOCK_UN:
		return F_UNLCK;
	}
	return -EINVAL;
}

/* Fill in a file_lock structure with an appropriate FLOCK lock. */
static struct file_lock *
flock_make_lock(struct file *filp, unsigned int cmd, struct file_lock *fl)
{
	int type = flock_translate_cmd(cmd);

	if (type < 0)
		return ERR_PTR(type);

	if (fl == NULL) {
		fl = locks_alloc_lock();
		if (fl == NULL)
			return ERR_PTR(-ENOMEM);
	} else {
		locks_init_lock(fl);
	}

	fl->fl_file = filp;
	fl->fl_owner = filp;
	fl->fl_pid = current->tgid;
	fl->fl_flags = FL_FLOCK;
	fl->fl_type = type;
	fl->fl_end = OFFSET_MAX;

	return fl;
}

static int assign_type(struct file_lock *fl, long type)
{
	switch (type) {
	case F_RDLCK:
	case F_WRLCK:
	case F_UNLCK:
		fl->fl_type = type;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int flock64_to_posix_lock(struct file *filp, struct file_lock *fl,
				 struct flock64 *l)
{
	switch (l->l_whence) {
	case SEEK_SET:
		fl->fl_start = 0;
		break;
	case SEEK_CUR:
		fl->fl_start = filp->f_pos;
		break;
	case SEEK_END:
		fl->fl_start = i_size_read(file_inode(filp));
		break;
	default:
		return -EINVAL;
	}
	if (l->l_start > OFFSET_MAX - fl->fl_start)
		return -EOVERFLOW;
	fl->fl_start += l->l_start;
	if (fl->fl_start < 0)
		return -EINVAL;

	/* POSIX-1996 leaves the case l->l_len < 0 undefined;
	   POSIX-2001 defines it. */
	if (l->l_len > 0) {
		if (l->l_len - 1 > OFFSET_MAX - fl->fl_start)
			return -EOVERFLOW;
		fl->fl_end = fl->fl_start + (l->l_len - 1);

	} else if (l->l_len < 0) {
		if (fl->fl_start + l->l_len < 0)
			return -EINVAL;
		fl->fl_end = fl->fl_start - 1;
		fl->fl_start += l->l_len;
	} else
		fl->fl_end = OFFSET_MAX;

	fl->fl_owner = current->files;
	fl->fl_pid = current->tgid;
	fl->fl_file = filp;
	fl->fl_flags = FL_POSIX;
	fl->fl_ops = NULL;
	fl->fl_lmops = NULL;

	return assign_type(fl, l->l_type);
}

/* Verify a "struct flock" and copy it to a "struct file_lock" as a POSIX
 * style lock.
 */
static int flock_to_posix_lock(struct file *filp, struct file_lock *fl,
			       struct flock *l)
{
	struct flock64 ll = {
		.l_type = l->l_type,
		.l_whence = l->l_whence,
		.l_start = l->l_start,
		.l_len = l->l_len,
	};

	return flock64_to_posix_lock(filp, fl, &ll);
}

/* default lease lock manager operations */
static bool
lease_break_callback(struct file_lock *fl)
{
	kill_fasync(&fl->fl_fasync, SIGIO, POLL_MSG);
	return false;
}

static void
lease_setup(struct file_lock *fl, void **priv)
{
	struct file *filp = fl->fl_file;
	struct fasync_struct *fa = *priv;

	/*
	 * fasync_insert_entry() returns the old entry if any. If there was no
	 * old entry, then it used "priv" and inserted it into the fasync list.
	 * Clear the pointer to indicate that it shouldn't be freed.
	 */
	if (!fasync_insert_entry(fa->fa_fd, filp, &fl->fl_fasync, fa))
		*priv = NULL;

	__f_setown(filp, task_pid(current), PIDTYPE_TGID, 0);
}

static const struct lock_manager_operations lease_manager_ops = {
	.lm_break = lease_break_callback,
	.lm_change = lease_modify,
	.lm_setup = lease_setup,
};

/*
 * Initialize a lease, use the default lock manager operations
 */
static int lease_init(struct file *filp, long type, struct file_lock *fl)
{
	if (assign_type(fl, type) != 0)
		return -EINVAL;

	fl->fl_owner = filp;
	fl->fl_pid = current->tgid;

	fl->fl_file = filp;
	fl->fl_flags = FL_LEASE;
	fl->fl_start = 0;
	fl->fl_end = OFFSET_MAX;
	fl->fl_ops = NULL;
	fl->fl_lmops = &lease_manager_ops;
	return 0;
}

/* Allocate a file_lock initialised to this type of lease */
static struct file_lock *lease_alloc(struct file *filp, long type)
{
	struct file_lock *fl = locks_alloc_lock();
	int error = -ENOMEM;

	if (fl == NULL)
		return ERR_PTR(error);

	error = lease_init(filp, type, fl);
	if (error) {
		locks_free_lock(fl);
		return ERR_PTR(error);
	}
	return fl;
}

/* Check if two locks overlap each other.
 */
static inline int locks_overlap(struct file_lock *fl1, struct file_lock *fl2)
{
	return ((fl1->fl_end >= fl2->fl_start) &&
		(fl2->fl_end >= fl1->fl_start));
}

/*
 * Check whether two locks have the same owner.
 */
static int posix_same_owner(struct file_lock *fl1, struct file_lock *fl2)
{
	return fl1->fl_owner == fl2->fl_owner;
}

/* Must be called with the flc_lock held! */
static void locks_insert_global_locks(struct file_lock *fl)
{
	struct file_lock_list_struct *fll = this_cpu_ptr(&file_lock_list);

	percpu_rwsem_assert_held(&file_rwsem);

	spin_lock(&fll->lock);
	fl->fl_link_cpu = smp_processor_id();
	hlist_add_head(&fl->fl_link, &fll->hlist);
	spin_unlock(&fll->lock);
}

/* Must be called with the flc_lock held! */
static void locks_delete_global_locks(struct file_lock *fl)
{
	struct file_lock_list_struct *fll;

	percpu_rwsem_assert_held(&file_rwsem);

	/*
	 * Avoid taking lock if already unhashed. This is safe since this check
	 * is done while holding the flc_lock, and new insertions into the list
	 * also require that it be held.
	 */
	if (hlist_unhashed(&fl->fl_link))
		return;

	fll = per_cpu_ptr(&file_lock_list, fl->fl_link_cpu);
	spin_lock(&fll->lock);
	hlist_del_init(&fl->fl_link);
	spin_unlock(&fll->lock);
}

static unsigned long
posix_owner_key(struct file_lock *fl)
{
	return (unsigned long)fl->fl_owner;
}

static void locks_insert_global_blocked(struct file_lock *waiter)
{
	lockdep_assert_held(&blocked_lock_lock);

	hash_add(blocked_hash, &waiter->fl_link, posix_owner_key(waiter));
}

static void locks_delete_global_blocked(struct file_lock *waiter)
{
	lockdep_assert_held(&blocked_lock_lock);

	hash_del(&waiter->fl_link);
}

/* Remove waiter from blocker's block list.
 * When blocker ends up pointing to itself then the list is empty.
 *
 * Must be called with blocked_lock_lock held.
 */
static void __locks_delete_block(struct file_lock *waiter)
{
	locks_delete_global_blocked(waiter);
	list_del_init(&waiter->fl_blocked_member);
}

static void __locks_wake_up_blocks(struct file_lock *blocker)
{
	while (!list_empty(&blocker->fl_blocked_requests)) {
		struct file_lock *waiter;

		waiter = list_first_entry(&blocker->fl_blocked_requests,
					  struct file_lock, fl_blocked_member);
		__locks_delete_block(waiter);
		if (waiter->fl_lmops && waiter->fl_lmops->lm_notify)
			waiter->fl_lmops->lm_notify(waiter);
		else
			wake_up(&waiter->fl_wait);

		/*
		 * The setting of fl_blocker to NULL marks the "done"
		 * point in deleting a block. Paired with acquire at the top
		 * of locks_delete_block().
		 */
		smp_store_release(&waiter->fl_blocker, NULL);
	}
}

/**
 *	locks_delete_block - stop waiting for a file lock
 *	@waiter: the lock which was waiting
 *
 *	lockd/nfsd need to disconnect the lock while working on it.
 */
int locks_delete_block(struct file_lock *waiter)
{
	int status = -ENOENT;

	/*
	 * If fl_blocker is NULL, it won't be set again as this thread "owns"
	 * the lock and is the only one that might try to claim the lock.
	 *
	 * We use acquire/release to manage fl_blocker so that we can
	 * optimize away taking the blocked_lock_lock in many cases.
	 *
	 * The smp_load_acquire guarantees two things:
	 *
	 * 1/ that fl_blocked_requests can be tested locklessly. If something
	 * was recently added to that list it must have been in a locked region
	 * *before* the locked region when fl_blocker was set to NULL.
	 *
	 * 2/ that no other thread is accessing 'waiter', so it is safe to free
	 * it.  __locks_wake_up_blocks is careful not to touch waiter after
	 * fl_blocker is released.
	 *
	 * If a lockless check of fl_blocker shows it to be NULL, we know that
	 * no new locks can be inserted into its fl_blocked_requests list, and
	 * can avoid doing anything further if the list is empty.
	 */
	if (!smp_load_acquire(&waiter->fl_blocker) &&
	    list_empty(&waiter->fl_blocked_requests))
		return status;

	spin_lock(&blocked_lock_lock);
	if (waiter->fl_blocker)
		status = 0;
	__locks_wake_up_blocks(waiter);
	__locks_delete_block(waiter);

	/*
	 * The setting of fl_blocker to NULL marks the "done" point in deleting
	 * a block. Paired with acquire at the top of this function.
	 */
	smp_store_release(&waiter->fl_blocker, NULL);
	spin_unlock(&blocked_lock_lock);
	return status;
}
EXPORT_SYMBOL(locks_delete_block);

/* Insert waiter into blocker's block list.
 * We use a circular list so that processes can be easily woken up in
 * the order they blocked. The documentation doesn't require this but
 * it seems like the reasonable thing to do.
 *
 * Must be called with both the flc_lock and blocked_lock_lock held. The
 * fl_blocked_requests list itself is protected by the blocked_lock_lock,
 * but by ensuring that the flc_lock is also held on insertions we can avoid
 * taking the blocked_lock_lock in some cases when we see that the
 * fl_blocked_requests list is empty.
 *
 * Rather than just adding to the list, we check for conflicts with any existing
 * waiters, and add beneath any waiter that blocks the new waiter.
 * Thus wakeups don't happen until needed.
 */
static void __locks_insert_block(struct file_lock *blocker,
				 struct file_lock *waiter,
				 bool conflict(struct file_lock *,
					       struct file_lock *))
{
	struct file_lock *fl;
	BUG_ON(!list_empty(&waiter->fl_blocked_member));

new_blocker:
	list_for_each_entry(fl, &blocker->fl_blocked_requests, fl_blocked_member)
		if (conflict(fl, waiter)) {
			blocker =  fl;
			goto new_blocker;
		}
	waiter->fl_blocker = blocker;
	list_add_tail(&waiter->fl_blocked_member, &blocker->fl_blocked_requests);
	if (IS_POSIX(blocker) && !IS_OFDLCK(blocker))
		locks_insert_global_blocked(waiter);

	/* The requests in waiter->fl_blocked are known to conflict with
	 * waiter, but might not conflict with blocker, or the requests
	 * and lock which block it.  So they all need to be woken.
	 */
	__locks_wake_up_blocks(waiter);
}

/* Must be called with flc_lock held. */
static void locks_insert_block(struct file_lock *blocker,
			       struct file_lock *waiter,
			       bool conflict(struct file_lock *,
					     struct file_lock *))
{
	spin_lock(&blocked_lock_lock);
	__locks_insert_block(blocker, waiter, conflict);
	spin_unlock(&blocked_lock_lock);
}

/*
 * Wake up processes blocked waiting for blocker.
 *
 * Must be called with the inode->flc_lock held!
 */
static void locks_wake_up_blocks(struct file_lock *blocker)
{
	/*
	 * Avoid taking global lock if list is empty. This is safe since new
	 * blocked requests are only added to the list under the flc_lock, and
	 * the flc_lock is always held here. Note that removal from the
	 * fl_blocked_requests list does not require the flc_lock, so we must
	 * recheck list_empty() after acquiring the blocked_lock_lock.
	 */
	if (list_empty(&blocker->fl_blocked_requests))
		return;

	spin_lock(&blocked_lock_lock);
	__locks_wake_up_blocks(blocker);
	spin_unlock(&blocked_lock_lock);
}

static void
locks_insert_lock_ctx(struct file_lock *fl, struct list_head *before)
{
	list_add_tail(&fl->fl_list, before);
	locks_insert_global_locks(fl);
}

static void
locks_unlink_lock_ctx(struct file_lock *fl)
{
	locks_delete_global_locks(fl);
	list_del_init(&fl->fl_list);
	locks_wake_up_blocks(fl);
}

static void
locks_delete_lock_ctx(struct file_lock *fl, struct list_head *dispose)
{
	locks_unlink_lock_ctx(fl);
	if (dispose)
		list_add(&fl->fl_list, dispose);
	else
		locks_free_lock(fl);
}

/* Determine if lock sys_fl blocks lock caller_fl. Common functionality
 * checks for shared/exclusive status of overlapping locks.
 */
static bool locks_conflict(struct file_lock *caller_fl,
			   struct file_lock *sys_fl)
{
	if (sys_fl->fl_type == F_WRLCK)
		return true;
	if (caller_fl->fl_type == F_WRLCK)
		return true;
	return false;
}

/* Determine if lock sys_fl blocks lock caller_fl. POSIX specific
 * checking before calling the locks_conflict().
 */
static bool posix_locks_conflict(struct file_lock *caller_fl,
				 struct file_lock *sys_fl)
{
	/* POSIX locks owned by the same process do not conflict with
	 * each other.
	 */
	if (posix_same_owner(caller_fl, sys_fl))
		return false;

	/* Check whether they overlap */
	if (!locks_overlap(caller_fl, sys_fl))
		return false;

	return locks_conflict(caller_fl, sys_fl);
}

/* Determine if lock sys_fl blocks lock caller_fl. FLOCK specific
 * checking before calling the locks_conflict().
 */
static bool flock_locks_conflict(struct file_lock *caller_fl,
				 struct file_lock *sys_fl)
{
	/* FLOCK locks referring to the same filp do not conflict with
	 * each other.
	 */
	if (caller_fl->fl_file == sys_fl->fl_file)
		return false;

	return locks_conflict(caller_fl, sys_fl);
}

void
posix_test_lock(struct file *filp, struct file_lock *fl)
{
	struct file_lock *cfl;
	struct file_lock_context *ctx;
	struct inode *inode = locks_inode(filp);

	ctx = smp_load_acquire(&inode->i_flctx);
	if (!ctx || list_empty_careful(&ctx->flc_posix)) {
		fl->fl_type = F_UNLCK;
		return;
	}

	spin_lock(&ctx->flc_lock);
	list_for_each_entry(cfl, &ctx->flc_posix, fl_list) {
		if (posix_locks_conflict(fl, cfl)) {
			locks_copy_conflock(fl, cfl);
			goto out;
		}
	}
	fl->fl_type = F_UNLCK;
out:
	spin_unlock(&ctx->flc_lock);
	return;
}
EXPORT_SYMBOL(posix_test_lock);

/*
 * Deadlock detection:
 *
 * We attempt to detect deadlocks that are due purely to posix file
 * locks.
 *
 * We assume that a task can be waiting for at most one lock at a time.
 * So for any acquired lock, the process holding that lock may be
 * waiting on at most one other lock.  That lock in turns may be held by
 * someone waiting for at most one other lock.  Given a requested lock
 * caller_fl which is about to wait for a conflicting lock block_fl, we
 * follow this chain of waiters to ensure we are not about to create a
 * cycle.
 *
 * Since we do this before we ever put a process to sleep on a lock, we
 * are ensured that there is never a cycle; that is what guarantees that
 * the while() loop in posix_locks_deadlock() eventually completes.
 *
 * Note: the above assumption may not be true when handling lock
 * requests from a broken NFS client. It may also fail in the presence
 * of tasks (such as posix threads) sharing the same open file table.
 * To handle those cases, we just bail out after a few iterations.
 *
 * For FL_OFDLCK locks, the owner is the filp, not the files_struct.
 * Because the owner is not even nominally tied to a thread of
 * execution, the deadlock detection below can't reasonably work well. Just
 * skip it for those.
 *
 * In principle, we could do a more limited deadlock detection on FL_OFDLCK
 * locks that just checks for the case where two tasks are attempting to
 * upgrade from read to write locks on the same inode.
 */

#define MAX_DEADLK_ITERATIONS 10

/* Find a lock that the owner of the given block_fl is blocking on. */
static struct file_lock *what_owner_is_waiting_for(struct file_lock *block_fl)
{
	struct file_lock *fl;

	hash_for_each_possible(blocked_hash, fl, fl_link, posix_owner_key(block_fl)) {
		if (posix_same_owner(fl, block_fl)) {
			while (fl->fl_blocker)
				fl = fl->fl_blocker;
			return fl;
		}
	}
	return NULL;
}

/* Must be called with the blocked_lock_lock held! */
static int posix_locks_deadlock(struct file_lock *caller_fl,
				struct file_lock *block_fl)
{
	int i = 0;

	lockdep_assert_held(&blocked_lock_lock);

	/*
	 * This deadlock detector can't reasonably detect deadlocks with
	 * FL_OFDLCK locks, since they aren't owned by a process, per-se.
	 */
	if (IS_OFDLCK(caller_fl))
		return 0;

	while ((block_fl = what_owner_is_waiting_for(block_fl))) {
		if (i++ > MAX_DEADLK_ITERATIONS)
			return 0;
		if (posix_same_owner(caller_fl, block_fl))
			return 1;
	}
	return 0;
}

/* Try to create a FLOCK lock on filp. We always insert new FLOCK locks
 * after any leases, but before any posix locks.
 *
 * Note that if called with an FL_EXISTS argument, the caller may determine
 * whether or not a lock was successfully freed by testing the return
 * value for -ENOENT.
 */
static int flock_lock_inode(struct inode *inode, struct file_lock *request)
{
	struct file_lock *new_fl = NULL;
	struct file_lock *fl;
	struct file_lock_context *ctx;
	int error = 0;
	bool found = false;
	LIST_HEAD(dispose);

	ctx = locks_get_lock_context(inode, request->fl_type);
	if (!ctx) {
		if (request->fl_type != F_UNLCK)
			return -ENOMEM;
		return (request->fl_flags & FL_EXISTS) ? -ENOENT : 0;
	}

	if (!(request->fl_flags & FL_ACCESS) && (request->fl_type != F_UNLCK)) {
		new_fl = locks_alloc_lock();
		if (!new_fl)
			return -ENOMEM;
	}

	percpu_down_read(&file_rwsem);
	spin_lock(&ctx->flc_lock);
	if (request->fl_flags & FL_ACCESS)
		goto find_conflict;

	list_for_each_entry(fl, &ctx->flc_flock, fl_list) {
		if (request->fl_file != fl->fl_file)
			continue;
		if (request->fl_type == fl->fl_type)
			goto out;
		found = true;
		locks_delete_lock_ctx(fl, &dispose);
		break;
	}

	if (request->fl_type == F_UNLCK) {
		if ((request->fl_flags & FL_EXISTS) && !found)
			error = -ENOENT;
		goto out;
	}

find_conflict:
	list_for_each_entry(fl, &ctx->flc_flock, fl_list) {
		if (!flock_locks_conflict(request, fl))
			continue;
		error = -EAGAIN;
		if (!(request->fl_flags & FL_SLEEP))
			goto out;
		error = FILE_LOCK_DEFERRED;
		locks_insert_block(fl, request, flock_locks_conflict);
		goto out;
	}
	if (request->fl_flags & FL_ACCESS)
		goto out;
	locks_copy_lock(new_fl, request);
	locks_move_blocks(new_fl, request);
	locks_insert_lock_ctx(new_fl, &ctx->flc_flock);
	new_fl = NULL;
	error = 0;

out:
	spin_unlock(&ctx->flc_lock);
	percpu_up_read(&file_rwsem);
	if (new_fl)
		locks_free_lock(new_fl);
	locks_dispose_list(&dispose);
	trace_flock_lock_inode(inode, request, error);
	return error;
}

static int posix_lock_inode(struct inode *inode, struct file_lock *request,
			    struct file_lock *conflock)
{
	struct file_lock *fl, *tmp;
	struct file_lock *new_fl = NULL;
	struct file_lock *new_fl2 = NULL;
	struct file_lock *left = NULL;
	struct file_lock *right = NULL;
	struct file_lock_context *ctx;
	int error;
	bool added = false;
	LIST_HEAD(dispose);

	ctx = locks_get_lock_context(inode, request->fl_type);
	if (!ctx)
		return (request->fl_type == F_UNLCK) ? 0 : -ENOMEM;

	/*
	 * We may need two file_lock structures for this operation,
	 * so we get them in advance to avoid races.
	 *
	 * In some cases we can be sure, that no new locks will be needed
	 */
	if (!(request->fl_flags & FL_ACCESS) &&
	    (request->fl_type != F_UNLCK ||
	     request->fl_start != 0 || request->fl_end != OFFSET_MAX)) {
		new_fl = locks_alloc_lock();
		new_fl2 = locks_alloc_lock();
	}

	percpu_down_read(&file_rwsem);
	spin_lock(&ctx->flc_lock);
	/*
	 * New lock request. Walk all POSIX locks and look for conflicts. If
	 * there are any, either return error or put the request on the
	 * blocker's list of waiters and the global blocked_hash.
	 */
	if (request->fl_type != F_UNLCK) {
		list_for_each_entry(fl, &ctx->flc_posix, fl_list) {
			if (!posix_locks_conflict(request, fl))
				continue;
			if (conflock)
				locks_copy_conflock(conflock, fl);
			error = -EAGAIN;
			if (!(request->fl_flags & FL_SLEEP))
				goto out;
			/*
			 * Deadlock detection and insertion into the blocked
			 * locks list must be done while holding the same lock!
			 */
			error = -EDEADLK;
			spin_lock(&blocked_lock_lock);
			/*
			 * Ensure that we don't find any locks blocked on this
			 * request during deadlock detection.
			 */
			__locks_wake_up_blocks(request);
			if (likely(!posix_locks_deadlock(request, fl))) {
				error = FILE_LOCK_DEFERRED;
				__locks_insert_block(fl, request,
						     posix_locks_conflict);
			}
			spin_unlock(&blocked_lock_lock);
			goto out;
		}
	}

	/* If we're just looking for a conflict, we're done. */
	error = 0;
	if (request->fl_flags & FL_ACCESS)
		goto out;

	/* Find the first old lock with the same owner as the new lock */
	list_for_each_entry(fl, &ctx->flc_posix, fl_list) {
		if (posix_same_owner(request, fl))
			break;
	}

	/* Process locks with this owner. */
	list_for_each_entry_safe_from(fl, tmp, &ctx->flc_posix, fl_list) {
		if (!posix_same_owner(request, fl))
			break;

		/* Detect adjacent or overlapping regions (if same lock type) */
		if (request->fl_type == fl->fl_type) {
			/* In all comparisons of start vs end, use
			 * "start - 1" rather than "end + 1". If end
			 * is OFFSET_MAX, end + 1 will become negative.
			 */
			if (fl->fl_end < request->fl_start - 1)
				continue;
			/* If the next lock in the list has entirely bigger
			 * addresses than the new one, insert the lock here.
			 */
			if (fl->fl_start - 1 > request->fl_end)
				break;

			/* If we come here, the new and old lock are of the
			 * same type and adjacent or overlapping. Make one
			 * lock yielding from the lower start address of both
			 * locks to the higher end address.
			 */
			if (fl->fl_start > request->fl_start)
				fl->fl_start = request->fl_start;
			else
				request->fl_start = fl->fl_start;
			if (fl->fl_end < request->fl_end)
				fl->fl_end = request->fl_end;
			else
				request->fl_end = fl->fl_end;
			if (added) {
				locks_delete_lock_ctx(fl, &dispose);
				continue;
			}
			request = fl;
			added = true;
		} else {
			/* Processing for different lock types is a bit
			 * more complex.
			 */
			if (fl->fl_end < request->fl_start)
				continue;
			if (fl->fl_start > request->fl_end)
				break;
			if (request->fl_type == F_UNLCK)
				added = true;
			if (fl->fl_start < request->fl_start)
				left = fl;
			/* If the next lock in the list has a higher end
			 * address than the new one, insert the new one here.
			 */
			if (fl->fl_end > request->fl_end) {
				right = fl;
				break;
			}
			if (fl->fl_start >= request->fl_start) {
				/* The new lock completely replaces an old
				 * one (This may happen several times).
				 */
				if (added) {
					locks_delete_lock_ctx(fl, &dispose);
					continue;
				}
				/*
				 * Replace the old lock with new_fl, and
				 * remove the old one. It's safe to do the
				 * insert here since we know that we won't be
				 * using new_fl later, and that the lock is
				 * just replacing an existing lock.
				 */
				error = -ENOLCK;
				if (!new_fl)
					goto out;
				locks_copy_lock(new_fl, request);
				locks_move_blocks(new_fl, request);
				request = new_fl;
				new_fl = NULL;
				locks_insert_lock_ctx(request, &fl->fl_list);
				locks_delete_lock_ctx(fl, &dispose);
				added = true;
			}
		}
	}

	/*
	 * The above code only modifies existing locks in case of merging or
	 * replacing. If new lock(s) need to be inserted all modifications are
	 * done below this, so it's safe yet to bail out.
	 */
	error = -ENOLCK; /* "no luck" */
	if (right && left == right && !new_fl2)
		goto out;

	error = 0;
	if (!added) {
		if (request->fl_type == F_UNLCK) {
			if (request->fl_flags & FL_EXISTS)
				error = -ENOENT;
			goto out;
		}

		if (!new_fl) {
			error = -ENOLCK;
			goto out;
		}
		locks_copy_lock(new_fl, request);
		locks_move_blocks(new_fl, request);
		locks_insert_lock_ctx(new_fl, &fl->fl_list);
		fl = new_fl;
		new_fl = NULL;
	}
	if (right) {
		if (left == right) {
			/* The new lock breaks the old one in two pieces,
			 * so we have to use the second new lock.
			 */
			left = new_fl2;
			new_fl2 = NULL;
			locks_copy_lock(left, right);
			locks_insert_lock_ctx(left, &fl->fl_list);
		}
		right->fl_start = request->fl_end + 1;
		locks_wake_up_blocks(right);
	}
	if (left) {
		left->fl_end = request->fl_start - 1;
		locks_wake_up_blocks(left);
	}
 out:
	spin_unlock(&ctx->flc_lock);
	percpu_up_read(&file_rwsem);
	/*
	 * Free any unused locks.
	 */
	if (new_fl)
		locks_free_lock(new_fl);
	if (new_fl2)
		locks_free_lock(new_fl2);
	locks_dispose_list(&dispose);
	trace_posix_lock_inode(inode, request, error);

	return error;
}

/**
 * posix_lock_file - Apply a POSIX-style lock to a file
 * @filp: The file to apply the lock to
 * @fl: The lock to be applied
 * @conflock: Place to return a copy of the conflicting lock, if found.
 *
 * Add a POSIX style lock to a file.
 * We merge adjacent & overlapping locks whenever possible.
 * POSIX locks are sorted by owner task, then by starting address
 *
 * Note that if called with an FL_EXISTS argument, the caller may determine
 * whether or not a lock was successfully freed by testing the return
 * value for -ENOENT.
 */
int posix_lock_file(struct file *filp, struct file_lock *fl,
			struct file_lock *conflock)
{
	return posix_lock_inode(locks_inode(filp), fl, conflock);
}
EXPORT_SYMBOL(posix_lock_file);

/**
 * posix_lock_inode_wait - Apply a POSIX-style lock to a file
 * @inode: inode of file to which lock request should be applied
 * @fl: The lock to be applied
 *
 * Apply a POSIX style lock request to an inode.
 */
static int posix_lock_inode_wait(struct inode *inode, struct file_lock *fl)
{
	int error;
	might_sleep ();
	for (;;) {
		error = posix_lock_inode(inode, fl, NULL);
		if (error != FILE_LOCK_DEFERRED)
			break;
		error = wait_event_interruptible(fl->fl_wait,
					list_empty(&fl->fl_blocked_member));
		if (error)
			break;
	}
	locks_delete_block(fl);
	return error;
}

static void lease_clear_pending(struct file_lock *fl, int arg)
{
	switch (arg) {
	case F_UNLCK:
		fl->fl_flags &= ~FL_UNLOCK_PENDING;
		fallthrough;
	case F_RDLCK:
		fl->fl_flags &= ~FL_DOWNGRADE_PENDING;
	}
}

/* We already had a lease on this file; just change its type */
int lease_modify(struct file_lock *fl, int arg, struct list_head *dispose)
{
	int error = assign_type(fl, arg);

	if (error)
		return error;
	lease_clear_pending(fl, arg);
	locks_wake_up_blocks(fl);
	if (arg == F_UNLCK) {
		struct file *filp = fl->fl_file;

		f_delown(filp);
		filp->f_owner.signum = 0;
		fasync_helper(0, fl->fl_file, 0, &fl->fl_fasync);
		if (fl->fl_fasync != NULL) {
			printk(KERN_ERR "locks_delete_lock: fasync == %p\n", fl->fl_fasync);
			fl->fl_fasync = NULL;
		}
		locks_delete_lock_ctx(fl, dispose);
	}
	return 0;
}
EXPORT_SYMBOL(lease_modify);

static bool past_time(unsigned long then)
{
	if (!then)
		/* 0 is a special value meaning "this never expires": */
		return false;
	return time_after(jiffies, then);
}

static void time_out_leases(struct inode *inode, struct list_head *dispose)
{
	struct file_lock_context *ctx = inode->i_flctx;
	struct file_lock *fl, *tmp;

	lockdep_assert_held(&ctx->flc_lock);

	list_for_each_entry_safe(fl, tmp, &ctx->flc_lease, fl_list) {
		trace_time_out_leases(inode, fl);
		if (past_time(fl->fl_downgrade_time))
			lease_modify(fl, F_RDLCK, dispose);
		if (past_time(fl->fl_break_time))
			lease_modify(fl, F_UNLCK, dispose);
	}
}

static bool leases_conflict(struct file_lock *lease, struct file_lock *breaker)
{
	bool rc;

	if (lease->fl_lmops->lm_breaker_owns_lease
			&& lease->fl_lmops->lm_breaker_owns_lease(lease))
		return false;
	if ((breaker->fl_flags & FL_LAYOUT) != (lease->fl_flags & FL_LAYOUT)) {
		rc = false;
		goto trace;
	}
	if ((breaker->fl_flags & FL_DELEG) && (lease->fl_flags & FL_LEASE)) {
		rc = false;
		goto trace;
	}

	rc = locks_conflict(breaker, lease);
trace:
	trace_leases_conflict(rc, lease, breaker);
	return rc;
}

static bool
any_leases_conflict(struct inode *inode, struct file_lock *breaker)
{
	struct file_lock_context *ctx = inode->i_flctx;
	struct file_lock *fl;

	lockdep_assert_held(&ctx->flc_lock);

	list_for_each_entry(fl, &ctx->flc_lease, fl_list) {
		if (leases_conflict(fl, breaker))
			return true;
	}
	return false;
}

/**
 *	__break_lease	-	revoke all outstanding leases on file
 *	@inode: the inode of the file to return
 *	@mode: O_RDONLY: break only write leases; O_WRONLY or O_RDWR:
 *	    break all leases
 *	@type: FL_LEASE: break leases and delegations; FL_DELEG: break
 *	    only delegations
 *
 *	break_lease (inlined for speed) has checked there already is at least
 *	some kind of lock (maybe a lease) on this file.  Leases are broken on
 *	a call to open() or truncate().  This function can sleep unless you
 *	specified %O_NONBLOCK to your open().
 */
int __break_lease(struct inode *inode, unsigned int mode, unsigned int type)
{
	int error = 0;
	struct file_lock_context *ctx;
	struct file_lock *new_fl, *fl, *tmp;
	unsigned long break_time;
	int want_write = (mode & O_ACCMODE) != O_RDONLY;
	LIST_HEAD(dispose);

	new_fl = lease_alloc(NULL, want_write ? F_WRLCK : F_RDLCK);
	if (IS_ERR(new_fl))
		return PTR_ERR(new_fl);
	new_fl->fl_flags = type;

	/* typically we will check that ctx is non-NULL before calling */
	ctx = smp_load_acquire(&inode->i_flctx);
	if (!ctx) {
		WARN_ON_ONCE(1);
		goto free_lock;
	}

	percpu_down_read(&file_rwsem);
	spin_lock(&ctx->flc_lock);

	time_out_leases(inode, &dispose);

	if (!any_leases_conflict(inode, new_fl))
		goto out;

	break_time = 0;
	if (lease_break_time > 0) {
		break_time = jiffies + lease_break_time * HZ;
		if (break_time == 0)
			break_time++;	/* so that 0 means no break time */
	}

	list_for_each_entry_safe(fl, tmp, &ctx->flc_lease, fl_list) {
		if (!leases_conflict(fl, new_fl))
			continue;
		if (want_write) {
			if (fl->fl_flags & FL_UNLOCK_PENDING)
				continue;
			fl->fl_flags |= FL_UNLOCK_PENDING;
			fl->fl_break_time = break_time;
		} else {
			if (lease_breaking(fl))
				continue;
			fl->fl_flags |= FL_DOWNGRADE_PENDING;
			fl->fl_downgrade_time = break_time;
		}
		if (fl->fl_lmops->lm_break(fl))
			locks_delete_lock_ctx(fl, &dispose);
	}

	if (list_empty(&ctx->flc_lease))
		goto out;

	if (mode & O_NONBLOCK) {
		trace_break_lease_noblock(inode, new_fl);
		error = -EWOULDBLOCK;
		goto out;
	}

restart:
	fl = list_first_entry(&ctx->flc_lease, struct file_lock, fl_list);
	break_time = fl->fl_break_time;
	if (break_time != 0)
		break_time -= jiffies;
	if (break_time == 0)
		break_time++;
	locks_insert_block(fl, new_fl, leases_conflict);
	trace_break_lease_block(inode, new_fl);
	spin_unlock(&ctx->flc_lock);
	percpu_up_read(&file_rwsem);

	locks_dispose_list(&dispose);
	error = wait_event_interruptible_timeout(new_fl->fl_wait,
					list_empty(&new_fl->fl_blocked_member),
					break_time);

	percpu_down_read(&file_rwsem);
	spin_lock(&ctx->flc_lock);
	trace_break_lease_unblock(inode, new_fl);
	locks_delete_block(new_fl);
	if (error >= 0) {
		/*
		 * Wait for the next conflicting lease that has not been
		 * broken yet
		 */
		if (error == 0)
			time_out_leases(inode, &dispose);
		if (any_leases_conflict(inode, new_fl))
			goto restart;
		error = 0;
	}
out:
	spin_unlock(&ctx->flc_lock);
	percpu_up_read(&file_rwsem);
	locks_dispose_list(&dispose);
free_lock:
	locks_free_lock(new_fl);
	return error;
}
EXPORT_SYMBOL(__break_lease);

/**
 *	lease_get_mtime - update modified time of an inode with exclusive lease
 *	@inode: the inode
 *      @time:  pointer to a timespec which contains the last modified time
 *
 * This is to force NFS clients to flush their caches for files with
 * exclusive leases.  The justification is that if someone has an
 * exclusive lease, then they could be modifying it.
 */
void lease_get_mtime(struct inode *inode, struct timespec64 *time)
{
	bool has_lease = false;
	struct file_lock_context *ctx;
	struct file_lock *fl;

	ctx = smp_load_acquire(&inode->i_flctx);
	if (ctx && !list_empty_careful(&ctx->flc_lease)) {
		spin_lock(&ctx->flc_lock);
		fl = list_first_entry_or_null(&ctx->flc_lease,
					      struct file_lock, fl_list);
		if (fl && (fl->fl_type == F_WRLCK))
			has_lease = true;
		spin_unlock(&ctx->flc_lock);
	}

	if (has_lease)
		*time = current_time(inode);
}
EXPORT_SYMBOL(lease_get_mtime);

/**
 *	fcntl_getlease - Enquire what lease is currently active
 *	@filp: the file
 *
 *	The value returned by this function will be one of
 *	(if no lease break is pending):
 *
 *	%F_RDLCK to indicate a shared lease is held.
 *
 *	%F_WRLCK to indicate an exclusive lease is held.
 *
 *	%F_UNLCK to indicate no lease is held.
 *
 *	(if a lease break is pending):
 *
 *	%F_RDLCK to indicate an exclusive lease needs to be
 *		changed to a shared lease (or removed).
 *
 *	%F_UNLCK to indicate the lease needs to be removed.
 *
 *	XXX: sfr & willy disagree over whether F_INPROGRESS
 *	should be returned to userspace.
 */
int fcntl_getlease(struct file *filp)
{
	struct file_lock *fl;
	struct inode *inode = locks_inode(filp);
	struct file_lock_context *ctx;
	int type = F_UNLCK;
	LIST_HEAD(dispose);

	ctx = smp_load_acquire(&inode->i_flctx);
	if (ctx && !list_empty_careful(&ctx->flc_lease)) {
		percpu_down_read(&file_rwsem);
		spin_lock(&ctx->flc_lock);
		time_out_leases(inode, &dispose);
		list_for_each_entry(fl, &ctx->flc_lease, fl_list) {
			if (fl->fl_file != filp)
				continue;
			type = target_leasetype(fl);
			break;
		}
		spin_unlock(&ctx->flc_lock);
		percpu_up_read(&file_rwsem);

		locks_dispose_list(&dispose);
	}
	return type;
}

/**
 * check_conflicting_open - see if the given file points to an inode that has
 *			    an existing open that would conflict with the
 *			    desired lease.
 * @filp:	file to check
 * @arg:	type of lease that we're trying to acquire
 * @flags:	current lock flags
 *
 * Check to see if there's an existing open fd on this file that would
 * conflict with the lease we're trying to set.
 */
static int
check_conflicting_open(struct file *filp, const long arg, int flags)
{
	struct inode *inode = locks_inode(filp);
	int self_wcount = 0, self_rcount = 0;

	if (flags & FL_LAYOUT)
		return 0;
	if (flags & FL_DELEG)
		/* We leave these checks to the caller */
		return 0;

	if (arg == F_RDLCK)
		return inode_is_open_for_write(inode) ? -EAGAIN : 0;
	else if (arg != F_WRLCK)
		return 0;

	/*
	 * Make sure that only read/write count is from lease requestor.
	 * Note that this will result in denying write leases when i_writecount
	 * is negative, which is what we want.  (We shouldn't grant write leases
	 * on files open for execution.)
	 */
	if (filp->f_mode & FMODE_WRITE)
		self_wcount = 1;
	else if (filp->f_mode & FMODE_READ)
		self_rcount = 1;

	if (atomic_read(&inode->i_writecount) != self_wcount ||
	    atomic_read(&inode->i_readcount) != self_rcount)
		return -EAGAIN;

	return 0;
}

static int
generic_add_lease(struct file *filp, long arg, struct file_lock **flp, void **priv)
{
	struct file_lock *fl, *my_fl = NULL, *lease;
	struct inode *inode = locks_inode(filp);
	struct file_lock_context *ctx;
	bool is_deleg = (*flp)->fl_flags & FL_DELEG;
	int error;
	LIST_HEAD(dispose);

	lease = *flp;
	trace_generic_add_lease(inode, lease);

	/* Note that arg is never F_UNLCK here */
	ctx = locks_get_lock_context(inode, arg);
	if (!ctx)
		return -ENOMEM;

	/*
	 * In the delegation case we need mutual exclusion with
	 * a number of operations that take the i_mutex.  We trylock
	 * because delegations are an optional optimization, and if
	 * there's some chance of a conflict--we'd rather not
	 * bother, maybe that's a sign this just isn't a good file to
	 * hand out a delegation on.
	 */
	if (is_deleg && !inode_trylock(inode))
		return -EAGAIN;

	if (is_deleg && arg == F_WRLCK) {
		/* Write delegations are not currently supported: */
		inode_unlock(inode);
		WARN_ON_ONCE(1);
		return -EINVAL;
	}

	percpu_down_read(&file_rwsem);
	spin_lock(&ctx->flc_lock);
	time_out_leases(inode, &dispose);
	error = check_conflicting_open(filp, arg, lease->fl_flags);
	if (error)
		goto out;

	/*
	 * At this point, we know that if there is an exclusive
	 * lease on this file, then we hold it on this filp
	 * (otherwise our open of this file would have blocked).
	 * And if we are trying to acquire an exclusive lease,
	 * then the file is not open by anyone (including us)
	 * except for this filp.
	 */
	error = -EAGAIN;
	list_for_each_entry(fl, &ctx->flc_lease, fl_list) {
		if (fl->fl_file == filp &&
		    fl->fl_owner == lease->fl_owner) {
			my_fl = fl;
			continue;
		}

		/*
		 * No exclusive leases if someone else has a lease on
		 * this file:
		 */
		if (arg == F_WRLCK)
			goto out;
		/*
		 * Modifying our existing lease is OK, but no getting a
		 * new lease if someone else is opening for write:
		 */
		if (fl->fl_flags & FL_UNLOCK_PENDING)
			goto out;
	}

	if (my_fl != NULL) {
		lease = my_fl;
		error = lease->fl_lmops->lm_change(lease, arg, &dispose);
		if (error)
			goto out;
		goto out_setup;
	}

	error = -EINVAL;
	if (!leases_enable)
		goto out;

	locks_insert_lock_ctx(lease, &ctx->flc_lease);
	/*
	 * The check in break_lease() is lockless. It's possible for another
	 * open to race in after we did the earlier check for a conflicting
	 * open but before the lease was inserted. Check again for a
	 * conflicting open and cancel the lease if there is one.
	 *
	 * We also add a barrier here to ensure that the insertion of the lock
	 * precedes these checks.
	 */
	smp_mb();
	error = check_conflicting_open(filp, arg, lease->fl_flags);
	if (error) {
		locks_unlink_lock_ctx(lease);
		goto out;
	}

out_setup:
	if (lease->fl_lmops->lm_setup)
		lease->fl_lmops->lm_setup(lease, priv);
out:
	spin_unlock(&ctx->flc_lock);
	percpu_up_read(&file_rwsem);
	locks_dispose_list(&dispose);
	if (is_deleg)
		inode_unlock(inode);
	if (!error && !my_fl)
		*flp = NULL;
	return error;
}

static int generic_delete_lease(struct file *filp, void *owner)
{
	int error = -EAGAIN;
	struct file_lock *fl, *victim = NULL;
	struct inode *inode = locks_inode(filp);
	struct file_lock_context *ctx;
	LIST_HEAD(dispose);

	ctx = smp_load_acquire(&inode->i_flctx);
	if (!ctx) {
		trace_generic_delete_lease(inode, NULL);
		return error;
	}

	percpu_down_read(&file_rwsem);
	spin_lock(&ctx->flc_lock);
	list_for_each_entry(fl, &ctx->flc_lease, fl_list) {
		if (fl->fl_file == filp &&
		    fl->fl_owner == owner) {
			victim = fl;
			break;
		}
	}
	trace_generic_delete_lease(inode, victim);
	if (victim)
		error = fl->fl_lmops->lm_change(victim, F_UNLCK, &dispose);
	spin_unlock(&ctx->flc_lock);
	percpu_up_read(&file_rwsem);
	locks_dispose_list(&dispose);
	return error;
}

/**
 *	generic_setlease	-	sets a lease on an open file
 *	@filp:	file pointer
 *	@arg:	type of lease to obtain
 *	@flp:	input - file_lock to use, output - file_lock inserted
 *	@priv:	private data for lm_setup (may be NULL if lm_setup
 *		doesn't require it)
 *
 *	The (input) flp->fl_lmops->lm_break function is required
 *	by break_lease().
 */
int generic_setlease(struct file *filp, long arg, struct file_lock **flp,
			void **priv)
{
	struct inode *inode = locks_inode(filp);
	int error;

	if ((!uid_eq(current_fsuid(), inode->i_uid)) && !capable(CAP_LEASE))
		return -EACCES;
	if (!S_ISREG(inode->i_mode))
		return -EINVAL;
	error = security_file_lock(filp, arg);
	if (error)
		return error;

	switch (arg) {
	case F_UNLCK:
		return generic_delete_lease(filp, *priv);
	case F_RDLCK:
	case F_WRLCK:
		if (!(*flp)->fl_lmops->lm_break) {
			WARN_ON_ONCE(1);
			return -ENOLCK;
		}

		return generic_add_lease(filp, arg, flp, priv);
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL(generic_setlease);

#if IS_ENABLED(CONFIG_SRCU)
/*
 * Kernel subsystems can register to be notified on any attempt to set
 * a new lease with the lease_notifier_chain. This is used by (e.g.) nfsd
 * to close files that it may have cached when there is an attempt to set a
 * conflicting lease.
 */
static struct srcu_notifier_head lease_notifier_chain;

static inline void
lease_notifier_chain_init(void)
{
	srcu_init_notifier_head(&lease_notifier_chain);
}

static inline void
setlease_notifier(long arg, struct file_lock *lease)
{
	if (arg != F_UNLCK)
		srcu_notifier_call_chain(&lease_notifier_chain, arg, lease);
}

int lease_register_notifier(struct notifier_block *nb)
{
	return srcu_notifier_chain_register(&lease_notifier_chain, nb);
}
EXPORT_SYMBOL_GPL(lease_register_notifier);

void lease_unregister_notifier(struct notifier_block *nb)
{
	srcu_notifier_chain_unregister(&lease_notifier_chain, nb);
}
EXPORT_SYMBOL_GPL(lease_unregister_notifier);

#else /* !IS_ENABLED(CONFIG_SRCU) */
static inline void
lease_notifier_chain_init(void)
{
}

static inline void
setlease_notifier(long arg, struct file_lock *lease)
{
}

int lease_register_notifier(struct notifier_block *nb)
{
	return 0;
}
EXPORT_SYMBOL_GPL(lease_register_notifier);

void lease_unregister_notifier(struct notifier_block *nb)
{
}
EXPORT_SYMBOL_GPL(lease_unregister_notifier);

#endif /* IS_ENABLED(CONFIG_SRCU) */

/**
 * vfs_setlease        -       sets a lease on an open file
 * @filp:	file pointer
 * @arg:	type of lease to obtain
 * @lease:	file_lock to use when adding a lease
 * @priv:	private info for lm_setup when adding a lease (may be
 *		NULL if lm_setup doesn't require it)
 *
 * Call this to establish a lease on the file. The "lease" argument is not
 * used for F_UNLCK requests and may be NULL. For commands that set or alter
 * an existing lease, the ``(*lease)->fl_lmops->lm_break`` operation must be
 * set; if not, this function will return -ENOLCK (and generate a scary-looking
 * stack trace).
 *
 * The "priv" pointer is passed directly to the lm_setup function as-is. It
 * may be NULL if the lm_setup operation doesn't require it.
 */
int
vfs_setlease(struct file *filp, long arg, struct file_lock **lease, void **priv)
{
	if (lease)
		setlease_notifier(arg, *lease);
	if (filp->f_op->setlease)
		return filp->f_op->setlease(filp, arg, lease, priv);
	else
		return generic_setlease(filp, arg, lease, priv);
}
EXPORT_SYMBOL_GPL(vfs_setlease);

static int do_fcntl_add_lease(unsigned int fd, struct file *filp, long arg)
{
	struct file_lock *fl;
	struct fasync_struct *new;
	int error;

	fl = lease_alloc(filp, arg);
	if (IS_ERR(fl))
		return PTR_ERR(fl);

	new = fasync_alloc();
	if (!new) {
		locks_free_lock(fl);
		return -ENOMEM;
	}
	new->fa_fd = fd;

	error = vfs_setlease(filp, arg, &fl, (void **)&new);
	if (fl)
		locks_free_lock(fl);
	if (new)
		fasync_free(new);
	return error;
}

/**
 *	fcntl_setlease	-	sets a lease on an open file
 *	@fd: open file descriptor
 *	@filp: file pointer
 *	@arg: type of lease to obtain
 *
 *	Call this fcntl to establish a lease on the file.
 *	Note that you also need to call %F_SETSIG to
 *	receive a signal when the lease is broken.
 */
int fcntl_setlease(unsigned int fd, struct file *filp, long arg)
{
	if (arg == F_UNLCK)
		return vfs_setlease(filp, F_UNLCK, NULL, (void **)&filp);
	return do_fcntl_add_lease(fd, filp, arg);
}

/**
 * flock_lock_inode_wait - Apply a FLOCK-style lock to a file
 * @inode: inode of the file to apply to
 * @fl: The lock to be applied
 *
 * Apply a FLOCK style lock request to an inode.
 */
static int flock_lock_inode_wait(struct inode *inode, struct file_lock *fl)
{
	int error;
	might_sleep();
	for (;;) {
		error = flock_lock_inode(inode, fl);
		if (error != FILE_LOCK_DEFERRED)
			break;
		error = wait_event_interruptible(fl->fl_wait,
				list_empty(&fl->fl_blocked_member));
		if (error)
			break;
	}
	locks_delete_block(fl);
	return error;
}

/**
 * locks_lock_inode_wait - Apply a lock to an inode
 * @inode: inode of the file to apply to
 * @fl: The lock to be applied
 *
 * Apply a POSIX or FLOCK style lock request to an inode.
 */
int locks_lock_inode_wait(struct inode *inode, struct file_lock *fl)
{
	int res = 0;
	switch (fl->fl_flags & (FL_POSIX|FL_FLOCK)) {
		case FL_POSIX:
			res = posix_lock_inode_wait(inode, fl);
			break;
		case FL_FLOCK:
			res = flock_lock_inode_wait(inode, fl);
			break;
		default:
			BUG();
	}
	return res;
}
EXPORT_SYMBOL(locks_lock_inode_wait);

/**
 *	sys_flock: - flock() system call.
 *	@fd: the file descriptor to lock.
 *	@cmd: the type of lock to apply.
 *
 *	Apply a %FL_FLOCK style lock to an open file descriptor.
 *	The @cmd can be one of:
 *
 *	- %LOCK_SH -- a shared lock.
 *	- %LOCK_EX -- an exclusive lock.
 *	- %LOCK_UN -- remove an existing lock.
 *	- %LOCK_MAND -- a 'mandatory' flock. (DEPRECATED)
 *
 *	%LOCK_MAND support has been removed from the kernel.
 */
SYSCALL_DEFINE2(flock, unsigned int, fd, unsigned int, cmd)
{
	struct fd f = fdget(fd);
	struct file_lock *lock;
	int can_sleep, unlock;
	int error;

	error = -EBADF;
	if (!f.file)
		goto out;

	can_sleep = !(cmd & LOCK_NB);
	cmd &= ~LOCK_NB;
	unlock = (cmd == LOCK_UN);

	if (!unlock && !(f.file->f_mode & (FMODE_READ|FMODE_WRITE)))
		goto out_putf;

	/*
	 * LOCK_MAND locks were broken for a long time in that they never
	 * conflicted with one another and didn't prevent any sort of open,
	 * read or write activity.
	 *
	 * Just ignore these requests now, to preserve legacy behavior, but
	 * throw a warning to let people know that they don't actually work.
	 */
	if (cmd & LOCK_MAND) {
		pr_warn_once("Attempt to set a LOCK_MAND lock via flock(2). This support has been removed and the request ignored.\n");
		error = 0;
		goto out_putf;
	}

	lock = flock_make_lock(f.file, cmd, NULL);
	if (IS_ERR(lock)) {
		error = PTR_ERR(lock);
		goto out_putf;
	}

	if (can_sleep)
		lock->fl_flags |= FL_SLEEP;

	error = security_file_lock(f.file, lock->fl_type);
	if (error)
		goto out_free;

	if (f.file->f_op->flock)
		error = f.file->f_op->flock(f.file,
					  (can_sleep) ? F_SETLKW : F_SETLK,
					  lock);
	else
		error = locks_lock_file_wait(f.file, lock);

 out_free:
	locks_free_lock(lock);

 out_putf:
	fdput(f);
 out:
	return error;
}

/**
 * vfs_test_lock - test file byte range lock
 * @filp: The file to test lock for
 * @fl: The lock to test; also used to hold result
 *
 * Returns -ERRNO on failure.  Indicates presence of conflicting lock by
 * setting conf->fl_type to something other than F_UNLCK.
 */
int vfs_test_lock(struct file *filp, struct file_lock *fl)
{
	if (filp->f_op->lock)
		return filp->f_op->lock(filp, F_GETLK, fl);
	posix_test_lock(filp, fl);
	return 0;
}
EXPORT_SYMBOL_GPL(vfs_test_lock);

/**
 * locks_translate_pid - translate a file_lock's fl_pid number into a namespace
 * @fl: The file_lock who's fl_pid should be translated
 * @ns: The namespace into which the pid should be translated
 *
 * Used to tranlate a fl_pid into a namespace virtual pid number
 */
static pid_t locks_translate_pid(struct file_lock *fl, struct pid_namespace *ns)
{
	pid_t vnr;
	struct pid *pid;

	if (IS_OFDLCK(fl))
		return -1;
	if (IS_REMOTELCK(fl))
		return fl->fl_pid;
	/*
	 * If the flock owner process is dead and its pid has been already
	 * freed, the translation below won't work, but we still want to show
	 * flock owner pid number in init pidns.
	 */
	if (ns == &init_pid_ns)
		return (pid_t)fl->fl_pid;

	rcu_read_lock();
	pid = find_pid_ns(fl->fl_pid, &init_pid_ns);
	vnr = pid_nr_ns(pid, ns);
	rcu_read_unlock();
	return vnr;
}

static int posix_lock_to_flock(struct flock *flock, struct file_lock *fl)
{
	flock->l_pid = locks_translate_pid(fl, task_active_pid_ns(current));
#if BITS_PER_LONG == 32
	/*
	 * Make sure we can represent the posix lock via
	 * legacy 32bit flock.
	 */
	if (fl->fl_start > OFFT_OFFSET_MAX)
		return -EOVERFLOW;
	if (fl->fl_end != OFFSET_MAX && fl->fl_end > OFFT_OFFSET_MAX)
		return -EOVERFLOW;
#endif
	flock->l_start = fl->fl_start;
	flock->l_len = fl->fl_end == OFFSET_MAX ? 0 :
		fl->fl_end - fl->fl_start + 1;
	flock->l_whence = 0;
	flock->l_type = fl->fl_type;
	return 0;
}

#if BITS_PER_LONG == 32
static void posix_lock_to_flock64(struct flock64 *flock, struct file_lock *fl)
{
	flock->l_pid = locks_translate_pid(fl, task_active_pid_ns(current));
	flock->l_start = fl->fl_start;
	flock->l_len = fl->fl_end == OFFSET_MAX ? 0 :
		fl->fl_end - fl->fl_start + 1;
	flock->l_whence = 0;
	flock->l_type = fl->fl_type;
}
#endif

/* Report the first existing lock that would conflict with l.
 * This implements the F_GETLK command of fcntl().
 */
int fcntl_getlk(struct file *filp, unsigned int cmd, struct flock *flock)
{
	struct file_lock *fl;
	int error;

	fl = locks_alloc_lock();
	if (fl == NULL)
		return -ENOMEM;
	error = -EINVAL;
	if (flock->l_type != F_RDLCK && flock->l_type != F_WRLCK)
		goto out;

	error = flock_to_posix_lock(filp, fl, flock);
	if (error)
		goto out;

	if (cmd == F_OFD_GETLK) {
		error = -EINVAL;
		if (flock->l_pid != 0)
			goto out;

		fl->fl_flags |= FL_OFDLCK;
		fl->fl_owner = filp;
	}

	error = vfs_test_lock(filp, fl);
	if (error)
		goto out;

	flock->l_type = fl->fl_type;
	if (fl->fl_type != F_UNLCK) {
		error = posix_lock_to_flock(flock, fl);
		if (error)
			goto out;
	}
out:
	locks_free_lock(fl);
	return error;
}

/**
 * vfs_lock_file - file byte range lock
 * @filp: The file to apply the lock to
 * @cmd: type of locking operation (F_SETLK, F_GETLK, etc.)
 * @fl: The lock to be applied
 * @conf: Place to return a copy of the conflicting lock, if found.
 *
 * A caller that doesn't care about the conflicting lock may pass NULL
 * as the final argument.
 *
 * If the filesystem defines a private ->lock() method, then @conf will
 * be left unchanged; so a caller that cares should initialize it to
 * some acceptable default.
 *
 * To avoid blocking kernel daemons, such as lockd, that need to acquire POSIX
 * locks, the ->lock() interface may return asynchronously, before the lock has
 * been granted or denied by the underlying filesystem, if (and only if)
 * lm_grant is set. Callers expecting ->lock() to return asynchronously
 * will only use F_SETLK, not F_SETLKW; they will set FL_SLEEP if (and only if)
 * the request is for a blocking lock. When ->lock() does return asynchronously,
 * it must return FILE_LOCK_DEFERRED, and call ->lm_grant() when the lock
 * request completes.
 * If the request is for non-blocking lock the file system should return
 * FILE_LOCK_DEFERRED then try to get the lock and call the callback routine
 * with the result. If the request timed out the callback routine will return a
 * nonzero return code and the file system should release the lock. The file
 * system is also responsible to keep a corresponding posix lock when it
 * grants a lock so the VFS can find out which locks are locally held and do
 * the correct lock cleanup when required.
 * The underlying filesystem must not drop the kernel lock or call
 * ->lm_grant() before returning to the caller with a FILE_LOCK_DEFERRED
 * return code.
 */
int vfs_lock_file(struct file *filp, unsigned int cmd, struct file_lock *fl, struct file_lock *conf)
{
	if (filp->f_op->lock)
		return filp->f_op->lock(filp, cmd, fl);
	else
		return posix_lock_file(filp, fl, conf);
}
EXPORT_SYMBOL_GPL(vfs_lock_file);

static int do_lock_file_wait(struct file *filp, unsigned int cmd,
			     struct file_lock *fl)
{
	int error;

	error = security_file_lock(filp, fl->fl_type);
	if (error)
		return error;

	for (;;) {
		error = vfs_lock_file(filp, cmd, fl, NULL);
		if (error != FILE_LOCK_DEFERRED)
			break;
		error = wait_event_interruptible(fl->fl_wait,
					list_empty(&fl->fl_blocked_member));
		if (error)
			break;
	}
	locks_delete_block(fl);

	return error;
}

/* Ensure that fl->fl_file has compatible f_mode for F_SETLK calls */
static int
check_fmode_for_setlk(struct file_lock *fl)
{
	switch (fl->fl_type) {
	case F_RDLCK:
		if (!(fl->fl_file->f_mode & FMODE_READ))
			return -EBADF;
		break;
	case F_WRLCK:
		if (!(fl->fl_file->f_mode & FMODE_WRITE))
			return -EBADF;
	}
	return 0;
}

/* Apply the lock described by l to an open file descriptor.
 * This implements both the F_SETLK and F_SETLKW commands of fcntl().
 */
int fcntl_setlk(unsigned int fd, struct file *filp, unsigned int cmd,
		struct flock *flock)
{
	struct file_lock *file_lock = locks_alloc_lock();
	struct inode *inode = locks_inode(filp);
	struct file *f;
	int error;

	if (file_lock == NULL)
		return -ENOLCK;

	error = flock_to_posix_lock(filp, file_lock, flock);
	if (error)
		goto out;

	error = check_fmode_for_setlk(file_lock);
	if (error)
		goto out;

	/*
	 * If the cmd is requesting file-private locks, then set the
	 * FL_OFDLCK flag and override the owner.
	 */
	switch (cmd) {
	case F_OFD_SETLK:
		error = -EINVAL;
		if (flock->l_pid != 0)
			goto out;

		cmd = F_SETLK;
		file_lock->fl_flags |= FL_OFDLCK;
		file_lock->fl_owner = filp;
		break;
	case F_OFD_SETLKW:
		error = -EINVAL;
		if (flock->l_pid != 0)
			goto out;

		cmd = F_SETLKW;
		file_lock->fl_flags |= FL_OFDLCK;
		file_lock->fl_owner = filp;
		fallthrough;
	case F_SETLKW:
		file_lock->fl_flags |= FL_SLEEP;
	}

	error = do_lock_file_wait(filp, cmd, file_lock);

	/*
	 * Attempt to detect a close/fcntl race and recover by releasing the
	 * lock that was just acquired. There is no need to do that when we're
	 * unlocking though, or for OFD locks.
	 */
	if (!error && file_lock->fl_type != F_UNLCK &&
	    !(file_lock->fl_flags & FL_OFDLCK)) {
		struct files_struct *files = current->files;
		/*
		 * We need that spin_lock here - it prevents reordering between
		 * update of i_flctx->flc_posix and check for it done in
		 * close(). rcu_read_lock() wouldn't do.
		 */
		spin_lock(&files->file_lock);
		f = files_lookup_fd_locked(files, fd);
		spin_unlock(&files->file_lock);
		if (f != filp) {
			file_lock->fl_type = F_UNLCK;
			error = do_lock_file_wait(filp, cmd, file_lock);
			WARN_ON_ONCE(error);
			error = -EBADF;
		}
	}
out:
	trace_fcntl_setlk(inode, file_lock, error);
	locks_free_lock(file_lock);
	return error;
}

#if BITS_PER_LONG == 32
/* Report the first existing lock that would conflict with l.
 * This implements the F_GETLK command of fcntl().
 */
int fcntl_getlk64(struct file *filp, unsigned int cmd, struct flock64 *flock)
{
	struct file_lock *fl;
	int error;

	fl = locks_alloc_lock();
	if (fl == NULL)
		return -ENOMEM;

	error = -EINVAL;
	if (flock->l_type != F_RDLCK && flock->l_type != F_WRLCK)
		goto out;

	error = flock64_to_posix_lock(filp, fl, flock);
	if (error)
		goto out;

	if (cmd == F_OFD_GETLK) {
		error = -EINVAL;
		if (flock->l_pid != 0)
			goto out;

		cmd = F_GETLK64;
		fl->fl_flags |= FL_OFDLCK;
		fl->fl_owner = filp;
	}

	error = vfs_test_lock(filp, fl);
	if (error)
		goto out;

	flock->l_type = fl->fl_type;
	if (fl->fl_type != F_UNLCK)
		posix_lock_to_flock64(flock, fl);

out:
	locks_free_lock(fl);
	return error;
}

/* Apply the lock described by l to an open file descriptor.
 * This implements both the F_SETLK and F_SETLKW commands of fcntl().
 */
int fcntl_setlk64(unsigned int fd, struct file *filp, unsigned int cmd,
		struct flock64 *flock)
{
	struct file_lock *file_lock = locks_alloc_lock();
	struct file *f;
	int error;

	if (file_lock == NULL)
		return -ENOLCK;

	error = flock64_to_posix_lock(filp, file_lock, flock);
	if (error)
		goto out;

	error = check_fmode_for_setlk(file_lock);
	if (error)
		goto out;

	/*
	 * If the cmd is requesting file-private locks, then set the
	 * FL_OFDLCK flag and override the owner.
	 */
	switch (cmd) {
	case F_OFD_SETLK:
		error = -EINVAL;
		if (flock->l_pid != 0)
			goto out;

		cmd = F_SETLK64;
		file_lock->fl_flags |= FL_OFDLCK;
		file_lock->fl_owner = filp;
		break;
	case F_OFD_SETLKW:
		error = -EINVAL;
		if (flock->l_pid != 0)
			goto out;

		cmd = F_SETLKW64;
		file_lock->fl_flags |= FL_OFDLCK;
		file_lock->fl_owner = filp;
		fallthrough;
	case F_SETLKW64:
		file_lock->fl_flags |= FL_SLEEP;
	}

	error = do_lock_file_wait(filp, cmd, file_lock);

	/*
	 * Attempt to detect a close/fcntl race and recover by releasing the
	 * lock that was just acquired. There is no need to do that when we're
	 * unlocking though, or for OFD locks.
	 */
	if (!error && file_lock->fl_type != F_UNLCK &&
	    !(file_lock->fl_flags & FL_OFDLCK)) {
		struct files_struct *files = current->files;
		/*
		 * We need that spin_lock here - it prevents reordering between
		 * update of i_flctx->flc_posix and check for it done in
		 * close(). rcu_read_lock() wouldn't do.
		 */
		spin_lock(&files->file_lock);
		f = files_lookup_fd_locked(files, fd);
		spin_unlock(&files->file_lock);
		if (f != filp) {
			file_lock->fl_type = F_UNLCK;
			error = do_lock_file_wait(filp, cmd, file_lock);
			WARN_ON_ONCE(error);
			error = -EBADF;
		}
	}
out:
	locks_free_lock(file_lock);
	return error;
}
#endif /* BITS_PER_LONG == 32 */

/*
 * This function is called when the file is being removed
 * from the task's fd array.  POSIX locks belonging to this task
 * are deleted at this time.
 */
void locks_remove_posix(struct file *filp, fl_owner_t owner)
{
	int error;
	struct inode *inode = locks_inode(filp);
	struct file_lock lock;
	struct file_lock_context *ctx;

	/*
	 * If there are no locks held on this file, we don't need to call
	 * posix_lock_file().  Another process could be setting a lock on this
	 * file at the same time, but we wouldn't remove that lock anyway.
	 */
	ctx =  smp_load_acquire(&inode->i_flctx);
	if (!ctx || list_empty(&ctx->flc_posix))
		return;

	locks_init_lock(&lock);
	lock.fl_type = F_UNLCK;
	lock.fl_flags = FL_POSIX | FL_CLOSE;
	lock.fl_start = 0;
	lock.fl_end = OFFSET_MAX;
	lock.fl_owner = owner;
	lock.fl_pid = current->tgid;
	lock.fl_file = filp;
	lock.fl_ops = NULL;
	lock.fl_lmops = NULL;

	error = vfs_lock_file(filp, F_SETLK, &lock, NULL);

	if (lock.fl_ops && lock.fl_ops->fl_release_private)
		lock.fl_ops->fl_release_private(&lock);
	trace_locks_remove_posix(inode, &lock, error);
}
EXPORT_SYMBOL(locks_remove_posix);

/* The i_flctx must be valid when calling into here */
static void
locks_remove_flock(struct file *filp, struct file_lock_context *flctx)
{
	struct file_lock fl;
	struct inode *inode = locks_inode(filp);

	if (list_empty(&flctx->flc_flock))
		return;

	flock_make_lock(filp, LOCK_UN, &fl);
	fl.fl_flags |= FL_CLOSE;

	if (filp->f_op->flock)
		filp->f_op->flock(filp, F_SETLKW, &fl);
	else
		flock_lock_inode(inode, &fl);

	if (fl.fl_ops && fl.fl_ops->fl_release_private)
		fl.fl_ops->fl_release_private(&fl);
}

/* The i_flctx must be valid when calling into here */
static void
locks_remove_lease(struct file *filp, struct file_lock_context *ctx)
{
	struct file_lock *fl, *tmp;
	LIST_HEAD(dispose);

	if (list_empty(&ctx->flc_lease))
		return;

	percpu_down_read(&file_rwsem);
	spin_lock(&ctx->flc_lock);
	list_for_each_entry_safe(fl, tmp, &ctx->flc_lease, fl_list)
		if (filp == fl->fl_file)
			lease_modify(fl, F_UNLCK, &dispose);
	spin_unlock(&ctx->flc_lock);
	percpu_up_read(&file_rwsem);

	locks_dispose_list(&dispose);
}

/*
 * This function is called on the last close of an open file.
 */
void locks_remove_file(struct file *filp)
{
	struct file_lock_context *ctx;

	ctx = smp_load_acquire(&locks_inode(filp)->i_flctx);
	if (!ctx)
		return;

	/* remove any OFD locks */
	locks_remove_posix(filp, filp);

	/* remove flock locks */
	locks_remove_flock(filp, ctx);

	/* remove any leases */
	locks_remove_lease(filp, ctx);

	spin_lock(&ctx->flc_lock);
	locks_check_ctx_file_list(filp, &ctx->flc_posix, "POSIX");
	locks_check_ctx_file_list(filp, &ctx->flc_flock, "FLOCK");
	locks_check_ctx_file_list(filp, &ctx->flc_lease, "LEASE");
	spin_unlock(&ctx->flc_lock);
}

/**
 * vfs_cancel_lock - file byte range unblock lock
 * @filp: The file to apply the unblock to
 * @fl: The lock to be unblocked
 *
 * Used by lock managers to cancel blocked requests
 */
int vfs_cancel_lock(struct file *filp, struct file_lock *fl)
{
	if (filp->f_op->lock)
		return filp->f_op->lock(filp, F_CANCELLK, fl);
	return 0;
}
EXPORT_SYMBOL_GPL(vfs_cancel_lock);

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

struct locks_iterator {
	int	li_cpu;
	loff_t	li_pos;
};

static void lock_get_status(struct seq_file *f, struct file_lock *fl,
			    loff_t id, char *pfx, int repeat)
{
	struct inode *inode = NULL;
	unsigned int fl_pid;
	struct pid_namespace *proc_pidns = proc_pid_ns(file_inode(f->file)->i_sb);
	int type;

	fl_pid = locks_translate_pid(fl, proc_pidns);
	/*
	 * If lock owner is dead (and pid is freed) or not visible in current
	 * pidns, zero is shown as a pid value. Check lock info from
	 * init_pid_ns to get saved lock pid value.
	 */

	if (fl->fl_file != NULL)
		inode = locks_inode(fl->fl_file);

	seq_printf(f, "%lld: ", id);

	if (repeat)
		seq_printf(f, "%*s", repeat - 1 + (int)strlen(pfx), pfx);

	if (IS_POSIX(fl)) {
		if (fl->fl_flags & FL_ACCESS)
			seq_puts(f, "ACCESS");
		else if (IS_OFDLCK(fl))
			seq_puts(f, "OFDLCK");
		else
			seq_puts(f, "POSIX ");

		seq_printf(f, " %s ",
			     (inode == NULL) ? "*NOINODE*" : "ADVISORY ");
	} else if (IS_FLOCK(fl)) {
		seq_puts(f, "FLOCK  ADVISORY  ");
	} else if (IS_LEASE(fl)) {
		if (fl->fl_flags & FL_DELEG)
			seq_puts(f, "DELEG  ");
		else
			seq_puts(f, "LEASE  ");

		if (lease_breaking(fl))
			seq_puts(f, "BREAKING  ");
		else if (fl->fl_file)
			seq_puts(f, "ACTIVE    ");
		else
			seq_puts(f, "BREAKER   ");
	} else {
		seq_puts(f, "UNKNOWN UNKNOWN  ");
	}
	type = IS_LEASE(fl) ? target_leasetype(fl) : fl->fl_type;

	seq_printf(f, "%s ", (type == F_WRLCK) ? "WRITE" :
			     (type == F_RDLCK) ? "READ" : "UNLCK");
	if (inode) {
		/* userspace relies on this representation of dev_t */
		seq_printf(f, "%d %02x:%02x:%lu ", fl_pid,
				MAJOR(inode->i_sb->s_dev),
				MINOR(inode->i_sb->s_dev), inode->i_ino);
	} else {
		seq_printf(f, "%d <none>:0 ", fl_pid);
	}
	if (IS_POSIX(fl)) {
		if (fl->fl_end == OFFSET_MAX)
			seq_printf(f, "%Ld EOF\n", fl->fl_start);
		else
			seq_printf(f, "%Ld %Ld\n", fl->fl_start, fl->fl_end);
	} else {
		seq_puts(f, "0 EOF\n");
	}
}

static struct file_lock *get_next_blocked_member(struct file_lock *node)
{
	struct file_lock *tmp;

	/* NULL node or root node */
	if (node == NULL || node->fl_blocker == NULL)
		return NULL;

	/* Next member in the linked list could be itself */
	tmp = list_next_entry(node, fl_blocked_member);
	if (list_entry_is_head(tmp, &node->fl_blocker->fl_blocked_requests, fl_blocked_member)
		|| tmp == node) {
		return NULL;
	}

	return tmp;
}

static int locks_show(struct seq_file *f, void *v)
{
	struct locks_iterator *iter = f->private;
	struct file_lock *cur, *tmp;
	struct pid_namespace *proc_pidns = proc_pid_ns(file_inode(f->file)->i_sb);
	int level = 0;

	cur = hlist_entry(v, struct file_lock, fl_link);

	if (locks_translate_pid(cur, proc_pidns) == 0)
		return 0;

	/* View this crossed linked list as a binary tree, the first member of fl_blocked_requests
	 * is the left child of current node, the next silibing in fl_blocked_member is the
	 * right child, we can alse get the parent of current node from fl_blocker, so this
	 * question becomes traversal of a binary tree
	 */
	while (cur != NULL) {
		if (level)
			lock_get_status(f, cur, iter->li_pos, "-> ", level);
		else
			lock_get_status(f, cur, iter->li_pos, "", level);

		if (!list_empty(&cur->fl_blocked_requests)) {
			/* Turn left */
			cur = list_first_entry_or_null(&cur->fl_blocked_requests,
				struct file_lock, fl_blocked_member);
			level++;
		} else {
			/* Turn right */
			tmp = get_next_blocked_member(cur);
			/* Fall back to parent node */
			while (tmp == NULL && cur->fl_blocker != NULL) {
				cur = cur->fl_blocker;
				level--;
				tmp = get_next_blocked_member(cur);
			}
			cur = tmp;
		}
	}

	return 0;
}

static void __show_fd_locks(struct seq_file *f,
			struct list_head *head, int *id,
			struct file *filp, struct files_struct *files)
{
	struct file_lock *fl;

	list_for_each_entry(fl, head, fl_list) {

		if (filp != fl->fl_file)
			continue;
		if (fl->fl_owner != files &&
		    fl->fl_owner != filp)
			continue;

		(*id)++;
		seq_puts(f, "lock:\t");
		lock_get_status(f, fl, *id, "", 0);
	}
}

void show_fd_locks(struct seq_file *f,
		  struct file *filp, struct files_struct *files)
{
	struct inode *inode = locks_inode(filp);
	struct file_lock_context *ctx;
	int id = 0;

	ctx = smp_load_acquire(&inode->i_flctx);
	if (!ctx)
		return;

	spin_lock(&ctx->flc_lock);
	__show_fd_locks(f, &ctx->flc_flock, &id, filp, files);
	__show_fd_locks(f, &ctx->flc_posix, &id, filp, files);
	__show_fd_locks(f, &ctx->flc_lease, &id, filp, files);
	spin_unlock(&ctx->flc_lock);
}

static void *locks_start(struct seq_file *f, loff_t *pos)
	__acquires(&blocked_lock_lock)
{
	struct locks_iterator *iter = f->private;

	iter->li_pos = *pos + 1;
	percpu_down_write(&file_rwsem);
	spin_lock(&blocked_lock_lock);
	return seq_hlist_start_percpu(&file_lock_list.hlist, &iter->li_cpu, *pos);
}

static void *locks_next(struct seq_file *f, void *v, loff_t *pos)
{
	struct locks_iterator *iter = f->private;

	++iter->li_pos;
	return seq_hlist_next_percpu(v, &file_lock_list.hlist, &iter->li_cpu, pos);
}

static void locks_stop(struct seq_file *f, void *v)
	__releases(&blocked_lock_lock)
{
	spin_unlock(&blocked_lock_lock);
	percpu_up_write(&file_rwsem);
}

static const struct seq_operations locks_seq_operations = {
	.start	= locks_start,
	.next	= locks_next,
	.stop	= locks_stop,
	.show	= locks_show,
};

static int __init proc_locks_init(void)
{
	proc_create_seq_private("locks", 0, NULL, &locks_seq_operations,
			sizeof(struct locks_iterator), NULL);
	return 0;
}
fs_initcall(proc_locks_init);
#endif

static int __init filelock_init(void)
{
	int i;

	flctx_cache = kmem_cache_create("file_lock_ctx",
			sizeof(struct file_lock_context), 0, SLAB_PANIC, NULL);

	filelock_cache = kmem_cache_create("file_lock_cache",
			sizeof(struct file_lock), 0, SLAB_PANIC, NULL);

	for_each_possible_cpu(i) {
		struct file_lock_list_struct *fll = per_cpu_ptr(&file_lock_list, i);

		spin_lock_init(&fll->lock);
		INIT_HLIST_HEAD(&fll->hlist);
	}

	lease_notifier_chain_init();
	return 0;
}
core_initcall(filelock_init);
