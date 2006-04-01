/*
 *  kernel/cpuset.c
 *
 *  Processor and Memory placement constraints for sets of tasks.
 *
 *  Copyright (C) 2003 BULL SA.
 *  Copyright (C) 2004-2006 Silicon Graphics, Inc.
 *
 *  Portions derived from Patrick Mochel's sysfs code.
 *  sysfs is Copyright (c) 2001-3 Patrick Mochel
 *
 *  2003-10-10 Written by Simon Derr.
 *  2003-10-22 Updates by Stephen Hemminger.
 *  2004 May-July Rework by Paul Jackson.
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of the Linux
 *  distribution for more details.
 */

#include <linux/config.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/cpuset.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/list.h>
#include <linux/mempolicy.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/pagemap.h>
#include <linux/proc_fs.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/backing-dev.h>
#include <linux/sort.h>

#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <linux/mutex.h>

#define CPUSET_SUPER_MAGIC		0x27e0eb

/*
 * Tracks how many cpusets are currently defined in system.
 * When there is only one cpuset (the root cpuset) we can
 * short circuit some hooks.
 */
int number_of_cpusets __read_mostly;

/* See "Frequency meter" comments, below. */

struct fmeter {
	int cnt;		/* unprocessed events count */
	int val;		/* most recent output value */
	time_t time;		/* clock (secs) when val computed */
	spinlock_t lock;	/* guards read or write of above */
};

struct cpuset {
	unsigned long flags;		/* "unsigned long" so bitops work */
	cpumask_t cpus_allowed;		/* CPUs allowed to tasks in cpuset */
	nodemask_t mems_allowed;	/* Memory Nodes allowed to tasks */

	/*
	 * Count is atomic so can incr (fork) or decr (exit) without a lock.
	 */
	atomic_t count;			/* count tasks using this cpuset */

	/*
	 * We link our 'sibling' struct into our parents 'children'.
	 * Our children link their 'sibling' into our 'children'.
	 */
	struct list_head sibling;	/* my parents children */
	struct list_head children;	/* my children */

	struct cpuset *parent;		/* my parent */
	struct dentry *dentry;		/* cpuset fs entry */

	/*
	 * Copy of global cpuset_mems_generation as of the most
	 * recent time this cpuset changed its mems_allowed.
	 */
	int mems_generation;

	struct fmeter fmeter;		/* memory_pressure filter */
};

/* bits in struct cpuset flags field */
typedef enum {
	CS_CPU_EXCLUSIVE,
	CS_MEM_EXCLUSIVE,
	CS_MEMORY_MIGRATE,
	CS_REMOVED,
	CS_NOTIFY_ON_RELEASE,
	CS_SPREAD_PAGE,
	CS_SPREAD_SLAB,
} cpuset_flagbits_t;

/* convenient tests for these bits */
static inline int is_cpu_exclusive(const struct cpuset *cs)
{
	return test_bit(CS_CPU_EXCLUSIVE, &cs->flags);
}

static inline int is_mem_exclusive(const struct cpuset *cs)
{
	return test_bit(CS_MEM_EXCLUSIVE, &cs->flags);
}

static inline int is_removed(const struct cpuset *cs)
{
	return test_bit(CS_REMOVED, &cs->flags);
}

static inline int notify_on_release(const struct cpuset *cs)
{
	return test_bit(CS_NOTIFY_ON_RELEASE, &cs->flags);
}

static inline int is_memory_migrate(const struct cpuset *cs)
{
	return test_bit(CS_MEMORY_MIGRATE, &cs->flags);
}

static inline int is_spread_page(const struct cpuset *cs)
{
	return test_bit(CS_SPREAD_PAGE, &cs->flags);
}

static inline int is_spread_slab(const struct cpuset *cs)
{
	return test_bit(CS_SPREAD_SLAB, &cs->flags);
}

/*
 * Increment this integer everytime any cpuset changes its
 * mems_allowed value.  Users of cpusets can track this generation
 * number, and avoid having to lock and reload mems_allowed unless
 * the cpuset they're using changes generation.
 *
 * A single, global generation is needed because attach_task() could
 * reattach a task to a different cpuset, which must not have its
 * generation numbers aliased with those of that tasks previous cpuset.
 *
 * Generations are needed for mems_allowed because one task cannot
 * modify anothers memory placement.  So we must enable every task,
 * on every visit to __alloc_pages(), to efficiently check whether
 * its current->cpuset->mems_allowed has changed, requiring an update
 * of its current->mems_allowed.
 *
 * Since cpuset_mems_generation is guarded by manage_mutex,
 * there is no need to mark it atomic.
 */
static int cpuset_mems_generation;

static struct cpuset top_cpuset = {
	.flags = ((1 << CS_CPU_EXCLUSIVE) | (1 << CS_MEM_EXCLUSIVE)),
	.cpus_allowed = CPU_MASK_ALL,
	.mems_allowed = NODE_MASK_ALL,
	.count = ATOMIC_INIT(0),
	.sibling = LIST_HEAD_INIT(top_cpuset.sibling),
	.children = LIST_HEAD_INIT(top_cpuset.children),
};

static struct vfsmount *cpuset_mount;
static struct super_block *cpuset_sb;

/*
 * We have two global cpuset mutexes below.  They can nest.
 * It is ok to first take manage_mutex, then nest callback_mutex.  We also
 * require taking task_lock() when dereferencing a tasks cpuset pointer.
 * See "The task_lock() exception", at the end of this comment.
 *
 * A task must hold both mutexes to modify cpusets.  If a task
 * holds manage_mutex, then it blocks others wanting that mutex,
 * ensuring that it is the only task able to also acquire callback_mutex
 * and be able to modify cpusets.  It can perform various checks on
 * the cpuset structure first, knowing nothing will change.  It can
 * also allocate memory while just holding manage_mutex.  While it is
 * performing these checks, various callback routines can briefly
 * acquire callback_mutex to query cpusets.  Once it is ready to make
 * the changes, it takes callback_mutex, blocking everyone else.
 *
 * Calls to the kernel memory allocator can not be made while holding
 * callback_mutex, as that would risk double tripping on callback_mutex
 * from one of the callbacks into the cpuset code from within
 * __alloc_pages().
 *
 * If a task is only holding callback_mutex, then it has read-only
 * access to cpusets.
 *
 * The task_struct fields mems_allowed and mems_generation may only
 * be accessed in the context of that task, so require no locks.
 *
 * Any task can increment and decrement the count field without lock.
 * So in general, code holding manage_mutex or callback_mutex can't rely
 * on the count field not changing.  However, if the count goes to
 * zero, then only attach_task(), which holds both mutexes, can
 * increment it again.  Because a count of zero means that no tasks
 * are currently attached, therefore there is no way a task attached
 * to that cpuset can fork (the other way to increment the count).
 * So code holding manage_mutex or callback_mutex can safely assume that
 * if the count is zero, it will stay zero.  Similarly, if a task
 * holds manage_mutex or callback_mutex on a cpuset with zero count, it
 * knows that the cpuset won't be removed, as cpuset_rmdir() needs
 * both of those mutexes.
 *
 * The cpuset_common_file_write handler for operations that modify
 * the cpuset hierarchy holds manage_mutex across the entire operation,
 * single threading all such cpuset modifications across the system.
 *
 * The cpuset_common_file_read() handlers only hold callback_mutex across
 * small pieces of code, such as when reading out possibly multi-word
 * cpumasks and nodemasks.
 *
 * The fork and exit callbacks cpuset_fork() and cpuset_exit(), don't
 * (usually) take either mutex.  These are the two most performance
 * critical pieces of code here.  The exception occurs on cpuset_exit(),
 * when a task in a notify_on_release cpuset exits.  Then manage_mutex
 * is taken, and if the cpuset count is zero, a usermode call made
 * to /sbin/cpuset_release_agent with the name of the cpuset (path
 * relative to the root of cpuset file system) as the argument.
 *
 * A cpuset can only be deleted if both its 'count' of using tasks
 * is zero, and its list of 'children' cpusets is empty.  Since all
 * tasks in the system use _some_ cpuset, and since there is always at
 * least one task in the system (init, pid == 1), therefore, top_cpuset
 * always has either children cpusets and/or using tasks.  So we don't
 * need a special hack to ensure that top_cpuset cannot be deleted.
 *
 * The above "Tale of Two Semaphores" would be complete, but for:
 *
 *	The task_lock() exception
 *
 * The need for this exception arises from the action of attach_task(),
 * which overwrites one tasks cpuset pointer with another.  It does
 * so using both mutexes, however there are several performance
 * critical places that need to reference task->cpuset without the
 * expense of grabbing a system global mutex.  Therefore except as
 * noted below, when dereferencing or, as in attach_task(), modifying
 * a tasks cpuset pointer we use task_lock(), which acts on a spinlock
 * (task->alloc_lock) already in the task_struct routinely used for
 * such matters.
 *
 * P.S.  One more locking exception.  RCU is used to guard the
 * update of a tasks cpuset pointer by attach_task() and the
 * access of task->cpuset->mems_generation via that pointer in
 * the routine cpuset_update_task_memory_state().
 */

static DEFINE_MUTEX(manage_mutex);
static DEFINE_MUTEX(callback_mutex);

/*
 * A couple of forward declarations required, due to cyclic reference loop:
 *  cpuset_mkdir -> cpuset_create -> cpuset_populate_dir -> cpuset_add_file
 *  -> cpuset_create_file -> cpuset_dir_inode_operations -> cpuset_mkdir.
 */

static int cpuset_mkdir(struct inode *dir, struct dentry *dentry, int mode);
static int cpuset_rmdir(struct inode *unused_dir, struct dentry *dentry);

static struct backing_dev_info cpuset_backing_dev_info = {
	.ra_pages = 0,		/* No readahead */
	.capabilities	= BDI_CAP_NO_ACCT_DIRTY | BDI_CAP_NO_WRITEBACK,
};

static struct inode *cpuset_new_inode(mode_t mode)
{
	struct inode *inode = new_inode(cpuset_sb);

	if (inode) {
		inode->i_mode = mode;
		inode->i_uid = current->fsuid;
		inode->i_gid = current->fsgid;
		inode->i_blksize = PAGE_CACHE_SIZE;
		inode->i_blocks = 0;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		inode->i_mapping->backing_dev_info = &cpuset_backing_dev_info;
	}
	return inode;
}

static void cpuset_diput(struct dentry *dentry, struct inode *inode)
{
	/* is dentry a directory ? if so, kfree() associated cpuset */
	if (S_ISDIR(inode->i_mode)) {
		struct cpuset *cs = dentry->d_fsdata;
		BUG_ON(!(is_removed(cs)));
		kfree(cs);
	}
	iput(inode);
}

static struct dentry_operations cpuset_dops = {
	.d_iput = cpuset_diput,
};

static struct dentry *cpuset_get_dentry(struct dentry *parent, const char *name)
{
	struct dentry *d = lookup_one_len(name, parent, strlen(name));
	if (!IS_ERR(d))
		d->d_op = &cpuset_dops;
	return d;
}

static void remove_dir(struct dentry *d)
{
	struct dentry *parent = dget(d->d_parent);

	d_delete(d);
	simple_rmdir(parent->d_inode, d);
	dput(parent);
}

/*
 * NOTE : the dentry must have been dget()'ed
 */
static void cpuset_d_remove_dir(struct dentry *dentry)
{
	struct list_head *node;

	spin_lock(&dcache_lock);
	node = dentry->d_subdirs.next;
	while (node != &dentry->d_subdirs) {
		struct dentry *d = list_entry(node, struct dentry, d_u.d_child);
		list_del_init(node);
		if (d->d_inode) {
			d = dget_locked(d);
			spin_unlock(&dcache_lock);
			d_delete(d);
			simple_unlink(dentry->d_inode, d);
			dput(d);
			spin_lock(&dcache_lock);
		}
		node = dentry->d_subdirs.next;
	}
	list_del_init(&dentry->d_u.d_child);
	spin_unlock(&dcache_lock);
	remove_dir(dentry);
}

static struct super_operations cpuset_ops = {
	.statfs = simple_statfs,
	.drop_inode = generic_delete_inode,
};

static int cpuset_fill_super(struct super_block *sb, void *unused_data,
							int unused_silent)
{
	struct inode *inode;
	struct dentry *root;

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = CPUSET_SUPER_MAGIC;
	sb->s_op = &cpuset_ops;
	cpuset_sb = sb;

	inode = cpuset_new_inode(S_IFDIR | S_IRUGO | S_IXUGO | S_IWUSR);
	if (inode) {
		inode->i_op = &simple_dir_inode_operations;
		inode->i_fop = &simple_dir_operations;
		/* directories start off with i_nlink == 2 (for "." entry) */
		inode->i_nlink++;
	} else {
		return -ENOMEM;
	}

	root = d_alloc_root(inode);
	if (!root) {
		iput(inode);
		return -ENOMEM;
	}
	sb->s_root = root;
	return 0;
}

static struct super_block *cpuset_get_sb(struct file_system_type *fs_type,
					int flags, const char *unused_dev_name,
					void *data)
{
	return get_sb_single(fs_type, flags, data, cpuset_fill_super);
}

static struct file_system_type cpuset_fs_type = {
	.name = "cpuset",
	.get_sb = cpuset_get_sb,
	.kill_sb = kill_litter_super,
};

/* struct cftype:
 *
 * The files in the cpuset filesystem mostly have a very simple read/write
 * handling, some common function will take care of it. Nevertheless some cases
 * (read tasks) are special and therefore I define this structure for every
 * kind of file.
 *
 *
 * When reading/writing to a file:
 *	- the cpuset to use in file->f_dentry->d_parent->d_fsdata
 *	- the 'cftype' of the file is file->f_dentry->d_fsdata
 */

struct cftype {
	char *name;
	int private;
	int (*open) (struct inode *inode, struct file *file);
	ssize_t (*read) (struct file *file, char __user *buf, size_t nbytes,
							loff_t *ppos);
	int (*write) (struct file *file, const char __user *buf, size_t nbytes,
							loff_t *ppos);
	int (*release) (struct inode *inode, struct file *file);
};

static inline struct cpuset *__d_cs(struct dentry *dentry)
{
	return dentry->d_fsdata;
}

static inline struct cftype *__d_cft(struct dentry *dentry)
{
	return dentry->d_fsdata;
}

/*
 * Call with manage_mutex held.  Writes path of cpuset into buf.
 * Returns 0 on success, -errno on error.
 */

static int cpuset_path(const struct cpuset *cs, char *buf, int buflen)
{
	char *start;

	start = buf + buflen;

	*--start = '\0';
	for (;;) {
		int len = cs->dentry->d_name.len;
		if ((start -= len) < buf)
			return -ENAMETOOLONG;
		memcpy(start, cs->dentry->d_name.name, len);
		cs = cs->parent;
		if (!cs)
			break;
		if (!cs->parent)
			continue;
		if (--start < buf)
			return -ENAMETOOLONG;
		*start = '/';
	}
	memmove(buf, start, buf + buflen - start);
	return 0;
}

/*
 * Notify userspace when a cpuset is released, by running
 * /sbin/cpuset_release_agent with the name of the cpuset (path
 * relative to the root of cpuset file system) as the argument.
 *
 * Most likely, this user command will try to rmdir this cpuset.
 *
 * This races with the possibility that some other task will be
 * attached to this cpuset before it is removed, or that some other
 * user task will 'mkdir' a child cpuset of this cpuset.  That's ok.
 * The presumed 'rmdir' will fail quietly if this cpuset is no longer
 * unused, and this cpuset will be reprieved from its death sentence,
 * to continue to serve a useful existence.  Next time it's released,
 * we will get notified again, if it still has 'notify_on_release' set.
 *
 * The final arg to call_usermodehelper() is 0, which means don't
 * wait.  The separate /sbin/cpuset_release_agent task is forked by
 * call_usermodehelper(), then control in this thread returns here,
 * without waiting for the release agent task.  We don't bother to
 * wait because the caller of this routine has no use for the exit
 * status of the /sbin/cpuset_release_agent task, so no sense holding
 * our caller up for that.
 *
 * When we had only one cpuset mutex, we had to call this
 * without holding it, to avoid deadlock when call_usermodehelper()
 * allocated memory.  With two locks, we could now call this while
 * holding manage_mutex, but we still don't, so as to minimize
 * the time manage_mutex is held.
 */

static void cpuset_release_agent(const char *pathbuf)
{
	char *argv[3], *envp[3];
	int i;

	if (!pathbuf)
		return;

	i = 0;
	argv[i++] = "/sbin/cpuset_release_agent";
	argv[i++] = (char *)pathbuf;
	argv[i] = NULL;

	i = 0;
	/* minimal command environment */
	envp[i++] = "HOME=/";
	envp[i++] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";
	envp[i] = NULL;

	call_usermodehelper(argv[0], argv, envp, 0);
	kfree(pathbuf);
}

/*
 * Either cs->count of using tasks transitioned to zero, or the
 * cs->children list of child cpusets just became empty.  If this
 * cs is notify_on_release() and now both the user count is zero and
 * the list of children is empty, prepare cpuset path in a kmalloc'd
 * buffer, to be returned via ppathbuf, so that the caller can invoke
 * cpuset_release_agent() with it later on, once manage_mutex is dropped.
 * Call here with manage_mutex held.
 *
 * This check_for_release() routine is responsible for kmalloc'ing
 * pathbuf.  The above cpuset_release_agent() is responsible for
 * kfree'ing pathbuf.  The caller of these routines is responsible
 * for providing a pathbuf pointer, initialized to NULL, then
 * calling check_for_release() with manage_mutex held and the address
 * of the pathbuf pointer, then dropping manage_mutex, then calling
 * cpuset_release_agent() with pathbuf, as set by check_for_release().
 */

static void check_for_release(struct cpuset *cs, char **ppathbuf)
{
	if (notify_on_release(cs) && atomic_read(&cs->count) == 0 &&
	    list_empty(&cs->children)) {
		char *buf;

		buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
		if (!buf)
			return;
		if (cpuset_path(cs, buf, PAGE_SIZE) < 0)
			kfree(buf);
		else
			*ppathbuf = buf;
	}
}

/*
 * Return in *pmask the portion of a cpusets's cpus_allowed that
 * are online.  If none are online, walk up the cpuset hierarchy
 * until we find one that does have some online cpus.  If we get
 * all the way to the top and still haven't found any online cpus,
 * return cpu_online_map.  Or if passed a NULL cs from an exit'ing
 * task, return cpu_online_map.
 *
 * One way or another, we guarantee to return some non-empty subset
 * of cpu_online_map.
 *
 * Call with callback_mutex held.
 */

static void guarantee_online_cpus(const struct cpuset *cs, cpumask_t *pmask)
{
	while (cs && !cpus_intersects(cs->cpus_allowed, cpu_online_map))
		cs = cs->parent;
	if (cs)
		cpus_and(*pmask, cs->cpus_allowed, cpu_online_map);
	else
		*pmask = cpu_online_map;
	BUG_ON(!cpus_intersects(*pmask, cpu_online_map));
}

/*
 * Return in *pmask the portion of a cpusets's mems_allowed that
 * are online.  If none are online, walk up the cpuset hierarchy
 * until we find one that does have some online mems.  If we get
 * all the way to the top and still haven't found any online mems,
 * return node_online_map.
 *
 * One way or another, we guarantee to return some non-empty subset
 * of node_online_map.
 *
 * Call with callback_mutex held.
 */

static void guarantee_online_mems(const struct cpuset *cs, nodemask_t *pmask)
{
	while (cs && !nodes_intersects(cs->mems_allowed, node_online_map))
		cs = cs->parent;
	if (cs)
		nodes_and(*pmask, cs->mems_allowed, node_online_map);
	else
		*pmask = node_online_map;
	BUG_ON(!nodes_intersects(*pmask, node_online_map));
}

/**
 * cpuset_update_task_memory_state - update task memory placement
 *
 * If the current tasks cpusets mems_allowed changed behind our
 * backs, update current->mems_allowed, mems_generation and task NUMA
 * mempolicy to the new value.
 *
 * Task mempolicy is updated by rebinding it relative to the
 * current->cpuset if a task has its memory placement changed.
 * Do not call this routine if in_interrupt().
 *
 * Call without callback_mutex or task_lock() held.  May be
 * called with or without manage_mutex held.  Thanks in part to
 * 'the_top_cpuset_hack', the tasks cpuset pointer will never
 * be NULL.  This routine also might acquire callback_mutex and
 * current->mm->mmap_sem during call.
 *
 * Reading current->cpuset->mems_generation doesn't need task_lock
 * to guard the current->cpuset derefence, because it is guarded
 * from concurrent freeing of current->cpuset by attach_task(),
 * using RCU.
 *
 * The rcu_dereference() is technically probably not needed,
 * as I don't actually mind if I see a new cpuset pointer but
 * an old value of mems_generation.  However this really only
 * matters on alpha systems using cpusets heavily.  If I dropped
 * that rcu_dereference(), it would save them a memory barrier.
 * For all other arch's, rcu_dereference is a no-op anyway, and for
 * alpha systems not using cpusets, another planned optimization,
 * avoiding the rcu critical section for tasks in the root cpuset
 * which is statically allocated, so can't vanish, will make this
 * irrelevant.  Better to use RCU as intended, than to engage in
 * some cute trick to save a memory barrier that is impossible to
 * test, for alpha systems using cpusets heavily, which might not
 * even exist.
 *
 * This routine is needed to update the per-task mems_allowed data,
 * within the tasks context, when it is trying to allocate memory
 * (in various mm/mempolicy.c routines) and notices that some other
 * task has been modifying its cpuset.
 */

void cpuset_update_task_memory_state(void)
{
	int my_cpusets_mem_gen;
	struct task_struct *tsk = current;
	struct cpuset *cs;

	if (tsk->cpuset == &top_cpuset) {
		/* Don't need rcu for top_cpuset.  It's never freed. */
		my_cpusets_mem_gen = top_cpuset.mems_generation;
	} else {
		rcu_read_lock();
		cs = rcu_dereference(tsk->cpuset);
		my_cpusets_mem_gen = cs->mems_generation;
		rcu_read_unlock();
	}

	if (my_cpusets_mem_gen != tsk->cpuset_mems_generation) {
		mutex_lock(&callback_mutex);
		task_lock(tsk);
		cs = tsk->cpuset;	/* Maybe changed when task not locked */
		guarantee_online_mems(cs, &tsk->mems_allowed);
		tsk->cpuset_mems_generation = cs->mems_generation;
		if (is_spread_page(cs))
			tsk->flags |= PF_SPREAD_PAGE;
		else
			tsk->flags &= ~PF_SPREAD_PAGE;
		if (is_spread_slab(cs))
			tsk->flags |= PF_SPREAD_SLAB;
		else
			tsk->flags &= ~PF_SPREAD_SLAB;
		task_unlock(tsk);
		mutex_unlock(&callback_mutex);
		mpol_rebind_task(tsk, &tsk->mems_allowed);
	}
}

/*
 * is_cpuset_subset(p, q) - Is cpuset p a subset of cpuset q?
 *
 * One cpuset is a subset of another if all its allowed CPUs and
 * Memory Nodes are a subset of the other, and its exclusive flags
 * are only set if the other's are set.  Call holding manage_mutex.
 */

static int is_cpuset_subset(const struct cpuset *p, const struct cpuset *q)
{
	return	cpus_subset(p->cpus_allowed, q->cpus_allowed) &&
		nodes_subset(p->mems_allowed, q->mems_allowed) &&
		is_cpu_exclusive(p) <= is_cpu_exclusive(q) &&
		is_mem_exclusive(p) <= is_mem_exclusive(q);
}

/*
 * validate_change() - Used to validate that any proposed cpuset change
 *		       follows the structural rules for cpusets.
 *
 * If we replaced the flag and mask values of the current cpuset
 * (cur) with those values in the trial cpuset (trial), would
 * our various subset and exclusive rules still be valid?  Presumes
 * manage_mutex held.
 *
 * 'cur' is the address of an actual, in-use cpuset.  Operations
 * such as list traversal that depend on the actual address of the
 * cpuset in the list must use cur below, not trial.
 *
 * 'trial' is the address of bulk structure copy of cur, with
 * perhaps one or more of the fields cpus_allowed, mems_allowed,
 * or flags changed to new, trial values.
 *
 * Return 0 if valid, -errno if not.
 */

static int validate_change(const struct cpuset *cur, const struct cpuset *trial)
{
	struct cpuset *c, *par;

	/* Each of our child cpusets must be a subset of us */
	list_for_each_entry(c, &cur->children, sibling) {
		if (!is_cpuset_subset(c, trial))
			return -EBUSY;
	}

	/* Remaining checks don't apply to root cpuset */
	if ((par = cur->parent) == NULL)
		return 0;

	/* We must be a subset of our parent cpuset */
	if (!is_cpuset_subset(trial, par))
		return -EACCES;

	/* If either I or some sibling (!= me) is exclusive, we can't overlap */
	list_for_each_entry(c, &par->children, sibling) {
		if ((is_cpu_exclusive(trial) || is_cpu_exclusive(c)) &&
		    c != cur &&
		    cpus_intersects(trial->cpus_allowed, c->cpus_allowed))
			return -EINVAL;
		if ((is_mem_exclusive(trial) || is_mem_exclusive(c)) &&
		    c != cur &&
		    nodes_intersects(trial->mems_allowed, c->mems_allowed))
			return -EINVAL;
	}

	return 0;
}

/*
 * For a given cpuset cur, partition the system as follows
 * a. All cpus in the parent cpuset's cpus_allowed that are not part of any
 *    exclusive child cpusets
 * b. All cpus in the current cpuset's cpus_allowed that are not part of any
 *    exclusive child cpusets
 * Build these two partitions by calling partition_sched_domains
 *
 * Call with manage_mutex held.  May nest a call to the
 * lock_cpu_hotplug()/unlock_cpu_hotplug() pair.
 */

static void update_cpu_domains(struct cpuset *cur)
{
	struct cpuset *c, *par = cur->parent;
	cpumask_t pspan, cspan;

	if (par == NULL || cpus_empty(cur->cpus_allowed))
		return;

	/*
	 * Get all cpus from parent's cpus_allowed not part of exclusive
	 * children
	 */
	pspan = par->cpus_allowed;
	list_for_each_entry(c, &par->children, sibling) {
		if (is_cpu_exclusive(c))
			cpus_andnot(pspan, pspan, c->cpus_allowed);
	}
	if (is_removed(cur) || !is_cpu_exclusive(cur)) {
		cpus_or(pspan, pspan, cur->cpus_allowed);
		if (cpus_equal(pspan, cur->cpus_allowed))
			return;
		cspan = CPU_MASK_NONE;
	} else {
		if (cpus_empty(pspan))
			return;
		cspan = cur->cpus_allowed;
		/*
		 * Get all cpus from current cpuset's cpus_allowed not part
		 * of exclusive children
		 */
		list_for_each_entry(c, &cur->children, sibling) {
			if (is_cpu_exclusive(c))
				cpus_andnot(cspan, cspan, c->cpus_allowed);
		}
	}

	lock_cpu_hotplug();
	partition_sched_domains(&pspan, &cspan);
	unlock_cpu_hotplug();
}

/*
 * Call with manage_mutex held.  May take callback_mutex during call.
 */

static int update_cpumask(struct cpuset *cs, char *buf)
{
	struct cpuset trialcs;
	int retval, cpus_unchanged;

	trialcs = *cs;
	retval = cpulist_parse(buf, trialcs.cpus_allowed);
	if (retval < 0)
		return retval;
	cpus_and(trialcs.cpus_allowed, trialcs.cpus_allowed, cpu_online_map);
	if (cpus_empty(trialcs.cpus_allowed))
		return -ENOSPC;
	retval = validate_change(cs, &trialcs);
	if (retval < 0)
		return retval;
	cpus_unchanged = cpus_equal(cs->cpus_allowed, trialcs.cpus_allowed);
	mutex_lock(&callback_mutex);
	cs->cpus_allowed = trialcs.cpus_allowed;
	mutex_unlock(&callback_mutex);
	if (is_cpu_exclusive(cs) && !cpus_unchanged)
		update_cpu_domains(cs);
	return 0;
}

/*
 * cpuset_migrate_mm
 *
 *    Migrate memory region from one set of nodes to another.
 *
 *    Temporarilly set tasks mems_allowed to target nodes of migration,
 *    so that the migration code can allocate pages on these nodes.
 *
 *    Call holding manage_mutex, so our current->cpuset won't change
 *    during this call, as manage_mutex holds off any attach_task()
 *    calls.  Therefore we don't need to take task_lock around the
 *    call to guarantee_online_mems(), as we know no one is changing
 *    our tasks cpuset.
 *
 *    Hold callback_mutex around the two modifications of our tasks
 *    mems_allowed to synchronize with cpuset_mems_allowed().
 *
 *    While the mm_struct we are migrating is typically from some
 *    other task, the task_struct mems_allowed that we are hacking
 *    is for our current task, which must allocate new pages for that
 *    migrating memory region.
 *
 *    We call cpuset_update_task_memory_state() before hacking
 *    our tasks mems_allowed, so that we are assured of being in
 *    sync with our tasks cpuset, and in particular, callbacks to
 *    cpuset_update_task_memory_state() from nested page allocations
 *    won't see any mismatch of our cpuset and task mems_generation
 *    values, so won't overwrite our hacked tasks mems_allowed
 *    nodemask.
 */

static void cpuset_migrate_mm(struct mm_struct *mm, const nodemask_t *from,
							const nodemask_t *to)
{
	struct task_struct *tsk = current;

	cpuset_update_task_memory_state();

	mutex_lock(&callback_mutex);
	tsk->mems_allowed = *to;
	mutex_unlock(&callback_mutex);

	do_migrate_pages(mm, from, to, MPOL_MF_MOVE_ALL);

	mutex_lock(&callback_mutex);
	guarantee_online_mems(tsk->cpuset, &tsk->mems_allowed);
	mutex_unlock(&callback_mutex);
}

/*
 * Handle user request to change the 'mems' memory placement
 * of a cpuset.  Needs to validate the request, update the
 * cpusets mems_allowed and mems_generation, and for each
 * task in the cpuset, rebind any vma mempolicies and if
 * the cpuset is marked 'memory_migrate', migrate the tasks
 * pages to the new memory.
 *
 * Call with manage_mutex held.  May take callback_mutex during call.
 * Will take tasklist_lock, scan tasklist for tasks in cpuset cs,
 * lock each such tasks mm->mmap_sem, scan its vma's and rebind
 * their mempolicies to the cpusets new mems_allowed.
 */

static int update_nodemask(struct cpuset *cs, char *buf)
{
	struct cpuset trialcs;
	nodemask_t oldmem;
	struct task_struct *g, *p;
	struct mm_struct **mmarray;
	int i, n, ntasks;
	int migrate;
	int fudge;
	int retval;

	trialcs = *cs;
	retval = nodelist_parse(buf, trialcs.mems_allowed);
	if (retval < 0)
		goto done;
	nodes_and(trialcs.mems_allowed, trialcs.mems_allowed, node_online_map);
	oldmem = cs->mems_allowed;
	if (nodes_equal(oldmem, trialcs.mems_allowed)) {
		retval = 0;		/* Too easy - nothing to do */
		goto done;
	}
	if (nodes_empty(trialcs.mems_allowed)) {
		retval = -ENOSPC;
		goto done;
	}
	retval = validate_change(cs, &trialcs);
	if (retval < 0)
		goto done;

	mutex_lock(&callback_mutex);
	cs->mems_allowed = trialcs.mems_allowed;
	cs->mems_generation = cpuset_mems_generation++;
	mutex_unlock(&callback_mutex);

	set_cpuset_being_rebound(cs);		/* causes mpol_copy() rebind */

	fudge = 10;				/* spare mmarray[] slots */
	fudge += cpus_weight(cs->cpus_allowed);	/* imagine one fork-bomb/cpu */
	retval = -ENOMEM;

	/*
	 * Allocate mmarray[] to hold mm reference for each task
	 * in cpuset cs.  Can't kmalloc GFP_KERNEL while holding
	 * tasklist_lock.  We could use GFP_ATOMIC, but with a
	 * few more lines of code, we can retry until we get a big
	 * enough mmarray[] w/o using GFP_ATOMIC.
	 */
	while (1) {
		ntasks = atomic_read(&cs->count);	/* guess */
		ntasks += fudge;
		mmarray = kmalloc(ntasks * sizeof(*mmarray), GFP_KERNEL);
		if (!mmarray)
			goto done;
		write_lock_irq(&tasklist_lock);		/* block fork */
		if (atomic_read(&cs->count) <= ntasks)
			break;				/* got enough */
		write_unlock_irq(&tasklist_lock);	/* try again */
		kfree(mmarray);
	}

	n = 0;

	/* Load up mmarray[] with mm reference for each task in cpuset. */
	do_each_thread(g, p) {
		struct mm_struct *mm;

		if (n >= ntasks) {
			printk(KERN_WARNING
				"Cpuset mempolicy rebind incomplete.\n");
			continue;
		}
		if (p->cpuset != cs)
			continue;
		mm = get_task_mm(p);
		if (!mm)
			continue;
		mmarray[n++] = mm;
	} while_each_thread(g, p);
	write_unlock_irq(&tasklist_lock);

	/*
	 * Now that we've dropped the tasklist spinlock, we can
	 * rebind the vma mempolicies of each mm in mmarray[] to their
	 * new cpuset, and release that mm.  The mpol_rebind_mm()
	 * call takes mmap_sem, which we couldn't take while holding
	 * tasklist_lock.  Forks can happen again now - the mpol_copy()
	 * cpuset_being_rebound check will catch such forks, and rebind
	 * their vma mempolicies too.  Because we still hold the global
	 * cpuset manage_mutex, we know that no other rebind effort will
	 * be contending for the global variable cpuset_being_rebound.
	 * It's ok if we rebind the same mm twice; mpol_rebind_mm()
	 * is idempotent.  Also migrate pages in each mm to new nodes.
	 */
	migrate = is_memory_migrate(cs);
	for (i = 0; i < n; i++) {
		struct mm_struct *mm = mmarray[i];

		mpol_rebind_mm(mm, &cs->mems_allowed);
		if (migrate)
			cpuset_migrate_mm(mm, &oldmem, &cs->mems_allowed);
		mmput(mm);
	}

	/* We're done rebinding vma's to this cpusets new mems_allowed. */
	kfree(mmarray);
	set_cpuset_being_rebound(NULL);
	retval = 0;
done:
	return retval;
}

/*
 * Call with manage_mutex held.
 */

static int update_memory_pressure_enabled(struct cpuset *cs, char *buf)
{
	if (simple_strtoul(buf, NULL, 10) != 0)
		cpuset_memory_pressure_enabled = 1;
	else
		cpuset_memory_pressure_enabled = 0;
	return 0;
}

/*
 * update_flag - read a 0 or a 1 in a file and update associated flag
 * bit:	the bit to update (CS_CPU_EXCLUSIVE, CS_MEM_EXCLUSIVE,
 *				CS_NOTIFY_ON_RELEASE, CS_MEMORY_MIGRATE,
 *				CS_SPREAD_PAGE, CS_SPREAD_SLAB)
 * cs:	the cpuset to update
 * buf:	the buffer where we read the 0 or 1
 *
 * Call with manage_mutex held.
 */

static int update_flag(cpuset_flagbits_t bit, struct cpuset *cs, char *buf)
{
	int turning_on;
	struct cpuset trialcs;
	int err, cpu_exclusive_changed;

	turning_on = (simple_strtoul(buf, NULL, 10) != 0);

	trialcs = *cs;
	if (turning_on)
		set_bit(bit, &trialcs.flags);
	else
		clear_bit(bit, &trialcs.flags);

	err = validate_change(cs, &trialcs);
	if (err < 0)
		return err;
	cpu_exclusive_changed =
		(is_cpu_exclusive(cs) != is_cpu_exclusive(&trialcs));
	mutex_lock(&callback_mutex);
	if (turning_on)
		set_bit(bit, &cs->flags);
	else
		clear_bit(bit, &cs->flags);
	mutex_unlock(&callback_mutex);

	if (cpu_exclusive_changed)
                update_cpu_domains(cs);
	return 0;
}

/*
 * Frequency meter - How fast is some event occuring?
 *
 * These routines manage a digitally filtered, constant time based,
 * event frequency meter.  There are four routines:
 *   fmeter_init() - initialize a frequency meter.
 *   fmeter_markevent() - called each time the event happens.
 *   fmeter_getrate() - returns the recent rate of such events.
 *   fmeter_update() - internal routine used to update fmeter.
 *
 * A common data structure is passed to each of these routines,
 * which is used to keep track of the state required to manage the
 * frequency meter and its digital filter.
 *
 * The filter works on the number of events marked per unit time.
 * The filter is single-pole low-pass recursive (IIR).  The time unit
 * is 1 second.  Arithmetic is done using 32-bit integers scaled to
 * simulate 3 decimal digits of precision (multiplied by 1000).
 *
 * With an FM_COEF of 933, and a time base of 1 second, the filter
 * has a half-life of 10 seconds, meaning that if the events quit
 * happening, then the rate returned from the fmeter_getrate()
 * will be cut in half each 10 seconds, until it converges to zero.
 *
 * It is not worth doing a real infinitely recursive filter.  If more
 * than FM_MAXTICKS ticks have elapsed since the last filter event,
 * just compute FM_MAXTICKS ticks worth, by which point the level
 * will be stable.
 *
 * Limit the count of unprocessed events to FM_MAXCNT, so as to avoid
 * arithmetic overflow in the fmeter_update() routine.
 *
 * Given the simple 32 bit integer arithmetic used, this meter works
 * best for reporting rates between one per millisecond (msec) and
 * one per 32 (approx) seconds.  At constant rates faster than one
 * per msec it maxes out at values just under 1,000,000.  At constant
 * rates between one per msec, and one per second it will stabilize
 * to a value N*1000, where N is the rate of events per second.
 * At constant rates between one per second and one per 32 seconds,
 * it will be choppy, moving up on the seconds that have an event,
 * and then decaying until the next event.  At rates slower than
 * about one in 32 seconds, it decays all the way back to zero between
 * each event.
 */

#define FM_COEF 933		/* coefficient for half-life of 10 secs */
#define FM_MAXTICKS ((time_t)99) /* useless computing more ticks than this */
#define FM_MAXCNT 1000000	/* limit cnt to avoid overflow */
#define FM_SCALE 1000		/* faux fixed point scale */

/* Initialize a frequency meter */
static void fmeter_init(struct fmeter *fmp)
{
	fmp->cnt = 0;
	fmp->val = 0;
	fmp->time = 0;
	spin_lock_init(&fmp->lock);
}

/* Internal meter update - process cnt events and update value */
static void fmeter_update(struct fmeter *fmp)
{
	time_t now = get_seconds();
	time_t ticks = now - fmp->time;

	if (ticks == 0)
		return;

	ticks = min(FM_MAXTICKS, ticks);
	while (ticks-- > 0)
		fmp->val = (FM_COEF * fmp->val) / FM_SCALE;
	fmp->time = now;

	fmp->val += ((FM_SCALE - FM_COEF) * fmp->cnt) / FM_SCALE;
	fmp->cnt = 0;
}

/* Process any previous ticks, then bump cnt by one (times scale). */
static void fmeter_markevent(struct fmeter *fmp)
{
	spin_lock(&fmp->lock);
	fmeter_update(fmp);
	fmp->cnt = min(FM_MAXCNT, fmp->cnt + FM_SCALE);
	spin_unlock(&fmp->lock);
}

/* Process any previous ticks, then return current value. */
static int fmeter_getrate(struct fmeter *fmp)
{
	int val;

	spin_lock(&fmp->lock);
	fmeter_update(fmp);
	val = fmp->val;
	spin_unlock(&fmp->lock);
	return val;
}

/*
 * Attack task specified by pid in 'pidbuf' to cpuset 'cs', possibly
 * writing the path of the old cpuset in 'ppathbuf' if it needs to be
 * notified on release.
 *
 * Call holding manage_mutex.  May take callback_mutex and task_lock of
 * the task 'pid' during call.
 */

static int attach_task(struct cpuset *cs, char *pidbuf, char **ppathbuf)
{
	pid_t pid;
	struct task_struct *tsk;
	struct cpuset *oldcs;
	cpumask_t cpus;
	nodemask_t from, to;
	struct mm_struct *mm;

	if (sscanf(pidbuf, "%d", &pid) != 1)
		return -EIO;
	if (cpus_empty(cs->cpus_allowed) || nodes_empty(cs->mems_allowed))
		return -ENOSPC;

	if (pid) {
		read_lock(&tasklist_lock);

		tsk = find_task_by_pid(pid);
		if (!tsk || tsk->flags & PF_EXITING) {
			read_unlock(&tasklist_lock);
			return -ESRCH;
		}

		get_task_struct(tsk);
		read_unlock(&tasklist_lock);

		if ((current->euid) && (current->euid != tsk->uid)
		    && (current->euid != tsk->suid)) {
			put_task_struct(tsk);
			return -EACCES;
		}
	} else {
		tsk = current;
		get_task_struct(tsk);
	}

	mutex_lock(&callback_mutex);

	task_lock(tsk);
	oldcs = tsk->cpuset;
	if (!oldcs) {
		task_unlock(tsk);
		mutex_unlock(&callback_mutex);
		put_task_struct(tsk);
		return -ESRCH;
	}
	atomic_inc(&cs->count);
	rcu_assign_pointer(tsk->cpuset, cs);
	task_unlock(tsk);

	guarantee_online_cpus(cs, &cpus);
	set_cpus_allowed(tsk, cpus);

	from = oldcs->mems_allowed;
	to = cs->mems_allowed;

	mutex_unlock(&callback_mutex);

	mm = get_task_mm(tsk);
	if (mm) {
		mpol_rebind_mm(mm, &to);
		if (is_memory_migrate(cs))
			cpuset_migrate_mm(mm, &from, &to);
		mmput(mm);
	}

	put_task_struct(tsk);
	synchronize_rcu();
	if (atomic_dec_and_test(&oldcs->count))
		check_for_release(oldcs, ppathbuf);
	return 0;
}

/* The various types of files and directories in a cpuset file system */

typedef enum {
	FILE_ROOT,
	FILE_DIR,
	FILE_MEMORY_MIGRATE,
	FILE_CPULIST,
	FILE_MEMLIST,
	FILE_CPU_EXCLUSIVE,
	FILE_MEM_EXCLUSIVE,
	FILE_NOTIFY_ON_RELEASE,
	FILE_MEMORY_PRESSURE_ENABLED,
	FILE_MEMORY_PRESSURE,
	FILE_SPREAD_PAGE,
	FILE_SPREAD_SLAB,
	FILE_TASKLIST,
} cpuset_filetype_t;

static ssize_t cpuset_common_file_write(struct file *file, const char __user *userbuf,
					size_t nbytes, loff_t *unused_ppos)
{
	struct cpuset *cs = __d_cs(file->f_dentry->d_parent);
	struct cftype *cft = __d_cft(file->f_dentry);
	cpuset_filetype_t type = cft->private;
	char *buffer;
	char *pathbuf = NULL;
	int retval = 0;

	/* Crude upper limit on largest legitimate cpulist user might write. */
	if (nbytes > 100 + 6 * NR_CPUS)
		return -E2BIG;

	/* +1 for nul-terminator */
	if ((buffer = kmalloc(nbytes + 1, GFP_KERNEL)) == 0)
		return -ENOMEM;

	if (copy_from_user(buffer, userbuf, nbytes)) {
		retval = -EFAULT;
		goto out1;
	}
	buffer[nbytes] = 0;	/* nul-terminate */

	mutex_lock(&manage_mutex);

	if (is_removed(cs)) {
		retval = -ENODEV;
		goto out2;
	}

	switch (type) {
	case FILE_CPULIST:
		retval = update_cpumask(cs, buffer);
		break;
	case FILE_MEMLIST:
		retval = update_nodemask(cs, buffer);
		break;
	case FILE_CPU_EXCLUSIVE:
		retval = update_flag(CS_CPU_EXCLUSIVE, cs, buffer);
		break;
	case FILE_MEM_EXCLUSIVE:
		retval = update_flag(CS_MEM_EXCLUSIVE, cs, buffer);
		break;
	case FILE_NOTIFY_ON_RELEASE:
		retval = update_flag(CS_NOTIFY_ON_RELEASE, cs, buffer);
		break;
	case FILE_MEMORY_MIGRATE:
		retval = update_flag(CS_MEMORY_MIGRATE, cs, buffer);
		break;
	case FILE_MEMORY_PRESSURE_ENABLED:
		retval = update_memory_pressure_enabled(cs, buffer);
		break;
	case FILE_MEMORY_PRESSURE:
		retval = -EACCES;
		break;
	case FILE_SPREAD_PAGE:
		retval = update_flag(CS_SPREAD_PAGE, cs, buffer);
		cs->mems_generation = cpuset_mems_generation++;
		break;
	case FILE_SPREAD_SLAB:
		retval = update_flag(CS_SPREAD_SLAB, cs, buffer);
		cs->mems_generation = cpuset_mems_generation++;
		break;
	case FILE_TASKLIST:
		retval = attach_task(cs, buffer, &pathbuf);
		break;
	default:
		retval = -EINVAL;
		goto out2;
	}

	if (retval == 0)
		retval = nbytes;
out2:
	mutex_unlock(&manage_mutex);
	cpuset_release_agent(pathbuf);
out1:
	kfree(buffer);
	return retval;
}

static ssize_t cpuset_file_write(struct file *file, const char __user *buf,
						size_t nbytes, loff_t *ppos)
{
	ssize_t retval = 0;
	struct cftype *cft = __d_cft(file->f_dentry);
	if (!cft)
		return -ENODEV;

	/* special function ? */
	if (cft->write)
		retval = cft->write(file, buf, nbytes, ppos);
	else
		retval = cpuset_common_file_write(file, buf, nbytes, ppos);

	return retval;
}

/*
 * These ascii lists should be read in a single call, by using a user
 * buffer large enough to hold the entire map.  If read in smaller
 * chunks, there is no guarantee of atomicity.  Since the display format
 * used, list of ranges of sequential numbers, is variable length,
 * and since these maps can change value dynamically, one could read
 * gibberish by doing partial reads while a list was changing.
 * A single large read to a buffer that crosses a page boundary is
 * ok, because the result being copied to user land is not recomputed
 * across a page fault.
 */

static int cpuset_sprintf_cpulist(char *page, struct cpuset *cs)
{
	cpumask_t mask;

	mutex_lock(&callback_mutex);
	mask = cs->cpus_allowed;
	mutex_unlock(&callback_mutex);

	return cpulist_scnprintf(page, PAGE_SIZE, mask);
}

static int cpuset_sprintf_memlist(char *page, struct cpuset *cs)
{
	nodemask_t mask;

	mutex_lock(&callback_mutex);
	mask = cs->mems_allowed;
	mutex_unlock(&callback_mutex);

	return nodelist_scnprintf(page, PAGE_SIZE, mask);
}

static ssize_t cpuset_common_file_read(struct file *file, char __user *buf,
				size_t nbytes, loff_t *ppos)
{
	struct cftype *cft = __d_cft(file->f_dentry);
	struct cpuset *cs = __d_cs(file->f_dentry->d_parent);
	cpuset_filetype_t type = cft->private;
	char *page;
	ssize_t retval = 0;
	char *s;

	if (!(page = (char *)__get_free_page(GFP_KERNEL)))
		return -ENOMEM;

	s = page;

	switch (type) {
	case FILE_CPULIST:
		s += cpuset_sprintf_cpulist(s, cs);
		break;
	case FILE_MEMLIST:
		s += cpuset_sprintf_memlist(s, cs);
		break;
	case FILE_CPU_EXCLUSIVE:
		*s++ = is_cpu_exclusive(cs) ? '1' : '0';
		break;
	case FILE_MEM_EXCLUSIVE:
		*s++ = is_mem_exclusive(cs) ? '1' : '0';
		break;
	case FILE_NOTIFY_ON_RELEASE:
		*s++ = notify_on_release(cs) ? '1' : '0';
		break;
	case FILE_MEMORY_MIGRATE:
		*s++ = is_memory_migrate(cs) ? '1' : '0';
		break;
	case FILE_MEMORY_PRESSURE_ENABLED:
		*s++ = cpuset_memory_pressure_enabled ? '1' : '0';
		break;
	case FILE_MEMORY_PRESSURE:
		s += sprintf(s, "%d", fmeter_getrate(&cs->fmeter));
		break;
	case FILE_SPREAD_PAGE:
		*s++ = is_spread_page(cs) ? '1' : '0';
		break;
	case FILE_SPREAD_SLAB:
		*s++ = is_spread_slab(cs) ? '1' : '0';
		break;
	default:
		retval = -EINVAL;
		goto out;
	}
	*s++ = '\n';

	retval = simple_read_from_buffer(buf, nbytes, ppos, page, s - page);
out:
	free_page((unsigned long)page);
	return retval;
}

static ssize_t cpuset_file_read(struct file *file, char __user *buf, size_t nbytes,
								loff_t *ppos)
{
	ssize_t retval = 0;
	struct cftype *cft = __d_cft(file->f_dentry);
	if (!cft)
		return -ENODEV;

	/* special function ? */
	if (cft->read)
		retval = cft->read(file, buf, nbytes, ppos);
	else
		retval = cpuset_common_file_read(file, buf, nbytes, ppos);

	return retval;
}

static int cpuset_file_open(struct inode *inode, struct file *file)
{
	int err;
	struct cftype *cft;

	err = generic_file_open(inode, file);
	if (err)
		return err;

	cft = __d_cft(file->f_dentry);
	if (!cft)
		return -ENODEV;
	if (cft->open)
		err = cft->open(inode, file);
	else
		err = 0;

	return err;
}

static int cpuset_file_release(struct inode *inode, struct file *file)
{
	struct cftype *cft = __d_cft(file->f_dentry);
	if (cft->release)
		return cft->release(inode, file);
	return 0;
}

/*
 * cpuset_rename - Only allow simple rename of directories in place.
 */
static int cpuset_rename(struct inode *old_dir, struct dentry *old_dentry,
                  struct inode *new_dir, struct dentry *new_dentry)
{
	if (!S_ISDIR(old_dentry->d_inode->i_mode))
		return -ENOTDIR;
	if (new_dentry->d_inode)
		return -EEXIST;
	if (old_dir != new_dir)
		return -EIO;
	return simple_rename(old_dir, old_dentry, new_dir, new_dentry);
}

static struct file_operations cpuset_file_operations = {
	.read = cpuset_file_read,
	.write = cpuset_file_write,
	.llseek = generic_file_llseek,
	.open = cpuset_file_open,
	.release = cpuset_file_release,
};

static struct inode_operations cpuset_dir_inode_operations = {
	.lookup = simple_lookup,
	.mkdir = cpuset_mkdir,
	.rmdir = cpuset_rmdir,
	.rename = cpuset_rename,
};

static int cpuset_create_file(struct dentry *dentry, int mode)
{
	struct inode *inode;

	if (!dentry)
		return -ENOENT;
	if (dentry->d_inode)
		return -EEXIST;

	inode = cpuset_new_inode(mode);
	if (!inode)
		return -ENOMEM;

	if (S_ISDIR(mode)) {
		inode->i_op = &cpuset_dir_inode_operations;
		inode->i_fop = &simple_dir_operations;

		/* start off with i_nlink == 2 (for "." entry) */
		inode->i_nlink++;
	} else if (S_ISREG(mode)) {
		inode->i_size = 0;
		inode->i_fop = &cpuset_file_operations;
	}

	d_instantiate(dentry, inode);
	dget(dentry);	/* Extra count - pin the dentry in core */
	return 0;
}

/*
 *	cpuset_create_dir - create a directory for an object.
 *	cs:	the cpuset we create the directory for.
 *		It must have a valid ->parent field
 *		And we are going to fill its ->dentry field.
 *	name:	The name to give to the cpuset directory. Will be copied.
 *	mode:	mode to set on new directory.
 */

static int cpuset_create_dir(struct cpuset *cs, const char *name, int mode)
{
	struct dentry *dentry = NULL;
	struct dentry *parent;
	int error = 0;

	parent = cs->parent->dentry;
	dentry = cpuset_get_dentry(parent, name);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);
	error = cpuset_create_file(dentry, S_IFDIR | mode);
	if (!error) {
		dentry->d_fsdata = cs;
		parent->d_inode->i_nlink++;
		cs->dentry = dentry;
	}
	dput(dentry);

	return error;
}

static int cpuset_add_file(struct dentry *dir, const struct cftype *cft)
{
	struct dentry *dentry;
	int error;

	mutex_lock(&dir->d_inode->i_mutex);
	dentry = cpuset_get_dentry(dir, cft->name);
	if (!IS_ERR(dentry)) {
		error = cpuset_create_file(dentry, 0644 | S_IFREG);
		if (!error)
			dentry->d_fsdata = (void *)cft;
		dput(dentry);
	} else
		error = PTR_ERR(dentry);
	mutex_unlock(&dir->d_inode->i_mutex);
	return error;
}

/*
 * Stuff for reading the 'tasks' file.
 *
 * Reading this file can return large amounts of data if a cpuset has
 * *lots* of attached tasks. So it may need several calls to read(),
 * but we cannot guarantee that the information we produce is correct
 * unless we produce it entirely atomically.
 *
 * Upon tasks file open(), a struct ctr_struct is allocated, that
 * will have a pointer to an array (also allocated here).  The struct
 * ctr_struct * is stored in file->private_data.  Its resources will
 * be freed by release() when the file is closed.  The array is used
 * to sprintf the PIDs and then used by read().
 */

/* cpusets_tasks_read array */

struct ctr_struct {
	char *buf;
	int bufsz;
};

/*
 * Load into 'pidarray' up to 'npids' of the tasks using cpuset 'cs'.
 * Return actual number of pids loaded.  No need to task_lock(p)
 * when reading out p->cpuset, as we don't really care if it changes
 * on the next cycle, and we are not going to try to dereference it.
 */
static int pid_array_load(pid_t *pidarray, int npids, struct cpuset *cs)
{
	int n = 0;
	struct task_struct *g, *p;

	read_lock(&tasklist_lock);

	do_each_thread(g, p) {
		if (p->cpuset == cs) {
			pidarray[n++] = p->pid;
			if (unlikely(n == npids))
				goto array_full;
		}
	} while_each_thread(g, p);

array_full:
	read_unlock(&tasklist_lock);
	return n;
}

static int cmppid(const void *a, const void *b)
{
	return *(pid_t *)a - *(pid_t *)b;
}

/*
 * Convert array 'a' of 'npids' pid_t's to a string of newline separated
 * decimal pids in 'buf'.  Don't write more than 'sz' chars, but return
 * count 'cnt' of how many chars would be written if buf were large enough.
 */
static int pid_array_to_buf(char *buf, int sz, pid_t *a, int npids)
{
	int cnt = 0;
	int i;

	for (i = 0; i < npids; i++)
		cnt += snprintf(buf + cnt, max(sz - cnt, 0), "%d\n", a[i]);
	return cnt;
}

/*
 * Handle an open on 'tasks' file.  Prepare a buffer listing the
 * process id's of tasks currently attached to the cpuset being opened.
 *
 * Does not require any specific cpuset mutexes, and does not take any.
 */
static int cpuset_tasks_open(struct inode *unused, struct file *file)
{
	struct cpuset *cs = __d_cs(file->f_dentry->d_parent);
	struct ctr_struct *ctr;
	pid_t *pidarray;
	int npids;
	char c;

	if (!(file->f_mode & FMODE_READ))
		return 0;

	ctr = kmalloc(sizeof(*ctr), GFP_KERNEL);
	if (!ctr)
		goto err0;

	/*
	 * If cpuset gets more users after we read count, we won't have
	 * enough space - tough.  This race is indistinguishable to the
	 * caller from the case that the additional cpuset users didn't
	 * show up until sometime later on.
	 */
	npids = atomic_read(&cs->count);
	pidarray = kmalloc(npids * sizeof(pid_t), GFP_KERNEL);
	if (!pidarray)
		goto err1;

	npids = pid_array_load(pidarray, npids, cs);
	sort(pidarray, npids, sizeof(pid_t), cmppid, NULL);

	/* Call pid_array_to_buf() twice, first just to get bufsz */
	ctr->bufsz = pid_array_to_buf(&c, sizeof(c), pidarray, npids) + 1;
	ctr->buf = kmalloc(ctr->bufsz, GFP_KERNEL);
	if (!ctr->buf)
		goto err2;
	ctr->bufsz = pid_array_to_buf(ctr->buf, ctr->bufsz, pidarray, npids);

	kfree(pidarray);
	file->private_data = ctr;
	return 0;

err2:
	kfree(pidarray);
err1:
	kfree(ctr);
err0:
	return -ENOMEM;
}

static ssize_t cpuset_tasks_read(struct file *file, char __user *buf,
						size_t nbytes, loff_t *ppos)
{
	struct ctr_struct *ctr = file->private_data;

	if (*ppos + nbytes > ctr->bufsz)
		nbytes = ctr->bufsz - *ppos;
	if (copy_to_user(buf, ctr->buf + *ppos, nbytes))
		return -EFAULT;
	*ppos += nbytes;
	return nbytes;
}

static int cpuset_tasks_release(struct inode *unused_inode, struct file *file)
{
	struct ctr_struct *ctr;

	if (file->f_mode & FMODE_READ) {
		ctr = file->private_data;
		kfree(ctr->buf);
		kfree(ctr);
	}
	return 0;
}

/*
 * for the common functions, 'private' gives the type of file
 */

static struct cftype cft_tasks = {
	.name = "tasks",
	.open = cpuset_tasks_open,
	.read = cpuset_tasks_read,
	.release = cpuset_tasks_release,
	.private = FILE_TASKLIST,
};

static struct cftype cft_cpus = {
	.name = "cpus",
	.private = FILE_CPULIST,
};

static struct cftype cft_mems = {
	.name = "mems",
	.private = FILE_MEMLIST,
};

static struct cftype cft_cpu_exclusive = {
	.name = "cpu_exclusive",
	.private = FILE_CPU_EXCLUSIVE,
};

static struct cftype cft_mem_exclusive = {
	.name = "mem_exclusive",
	.private = FILE_MEM_EXCLUSIVE,
};

static struct cftype cft_notify_on_release = {
	.name = "notify_on_release",
	.private = FILE_NOTIFY_ON_RELEASE,
};

static struct cftype cft_memory_migrate = {
	.name = "memory_migrate",
	.private = FILE_MEMORY_MIGRATE,
};

static struct cftype cft_memory_pressure_enabled = {
	.name = "memory_pressure_enabled",
	.private = FILE_MEMORY_PRESSURE_ENABLED,
};

static struct cftype cft_memory_pressure = {
	.name = "memory_pressure",
	.private = FILE_MEMORY_PRESSURE,
};

static struct cftype cft_spread_page = {
	.name = "memory_spread_page",
	.private = FILE_SPREAD_PAGE,
};

static struct cftype cft_spread_slab = {
	.name = "memory_spread_slab",
	.private = FILE_SPREAD_SLAB,
};

static int cpuset_populate_dir(struct dentry *cs_dentry)
{
	int err;

	if ((err = cpuset_add_file(cs_dentry, &cft_cpus)) < 0)
		return err;
	if ((err = cpuset_add_file(cs_dentry, &cft_mems)) < 0)
		return err;
	if ((err = cpuset_add_file(cs_dentry, &cft_cpu_exclusive)) < 0)
		return err;
	if ((err = cpuset_add_file(cs_dentry, &cft_mem_exclusive)) < 0)
		return err;
	if ((err = cpuset_add_file(cs_dentry, &cft_notify_on_release)) < 0)
		return err;
	if ((err = cpuset_add_file(cs_dentry, &cft_memory_migrate)) < 0)
		return err;
	if ((err = cpuset_add_file(cs_dentry, &cft_memory_pressure)) < 0)
		return err;
	if ((err = cpuset_add_file(cs_dentry, &cft_spread_page)) < 0)
		return err;
	if ((err = cpuset_add_file(cs_dentry, &cft_spread_slab)) < 0)
		return err;
	if ((err = cpuset_add_file(cs_dentry, &cft_tasks)) < 0)
		return err;
	return 0;
}

/*
 *	cpuset_create - create a cpuset
 *	parent:	cpuset that will be parent of the new cpuset.
 *	name:		name of the new cpuset. Will be strcpy'ed.
 *	mode:		mode to set on new inode
 *
 *	Must be called with the mutex on the parent inode held
 */

static long cpuset_create(struct cpuset *parent, const char *name, int mode)
{
	struct cpuset *cs;
	int err;

	cs = kmalloc(sizeof(*cs), GFP_KERNEL);
	if (!cs)
		return -ENOMEM;

	mutex_lock(&manage_mutex);
	cpuset_update_task_memory_state();
	cs->flags = 0;
	if (notify_on_release(parent))
		set_bit(CS_NOTIFY_ON_RELEASE, &cs->flags);
	if (is_spread_page(parent))
		set_bit(CS_SPREAD_PAGE, &cs->flags);
	if (is_spread_slab(parent))
		set_bit(CS_SPREAD_SLAB, &cs->flags);
	cs->cpus_allowed = CPU_MASK_NONE;
	cs->mems_allowed = NODE_MASK_NONE;
	atomic_set(&cs->count, 0);
	INIT_LIST_HEAD(&cs->sibling);
	INIT_LIST_HEAD(&cs->children);
	cs->mems_generation = cpuset_mems_generation++;
	fmeter_init(&cs->fmeter);

	cs->parent = parent;

	mutex_lock(&callback_mutex);
	list_add(&cs->sibling, &cs->parent->children);
	number_of_cpusets++;
	mutex_unlock(&callback_mutex);

	err = cpuset_create_dir(cs, name, mode);
	if (err < 0)
		goto err;

	/*
	 * Release manage_mutex before cpuset_populate_dir() because it
	 * will down() this new directory's i_mutex and if we race with
	 * another mkdir, we might deadlock.
	 */
	mutex_unlock(&manage_mutex);

	err = cpuset_populate_dir(cs->dentry);
	/* If err < 0, we have a half-filled directory - oh well ;) */
	return 0;
err:
	list_del(&cs->sibling);
	mutex_unlock(&manage_mutex);
	kfree(cs);
	return err;
}

static int cpuset_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	struct cpuset *c_parent = dentry->d_parent->d_fsdata;

	/* the vfs holds inode->i_mutex already */
	return cpuset_create(c_parent, dentry->d_name.name, mode | S_IFDIR);
}

static int cpuset_rmdir(struct inode *unused_dir, struct dentry *dentry)
{
	struct cpuset *cs = dentry->d_fsdata;
	struct dentry *d;
	struct cpuset *parent;
	char *pathbuf = NULL;

	/* the vfs holds both inode->i_mutex already */

	mutex_lock(&manage_mutex);
	cpuset_update_task_memory_state();
	if (atomic_read(&cs->count) > 0) {
		mutex_unlock(&manage_mutex);
		return -EBUSY;
	}
	if (!list_empty(&cs->children)) {
		mutex_unlock(&manage_mutex);
		return -EBUSY;
	}
	parent = cs->parent;
	mutex_lock(&callback_mutex);
	set_bit(CS_REMOVED, &cs->flags);
	if (is_cpu_exclusive(cs))
		update_cpu_domains(cs);
	list_del(&cs->sibling);	/* delete my sibling from parent->children */
	spin_lock(&cs->dentry->d_lock);
	d = dget(cs->dentry);
	cs->dentry = NULL;
	spin_unlock(&d->d_lock);
	cpuset_d_remove_dir(d);
	dput(d);
	number_of_cpusets--;
	mutex_unlock(&callback_mutex);
	if (list_empty(&parent->children))
		check_for_release(parent, &pathbuf);
	mutex_unlock(&manage_mutex);
	cpuset_release_agent(pathbuf);
	return 0;
}

/*
 * cpuset_init_early - just enough so that the calls to
 * cpuset_update_task_memory_state() in early init code
 * are harmless.
 */

int __init cpuset_init_early(void)
{
	struct task_struct *tsk = current;

	tsk->cpuset = &top_cpuset;
	tsk->cpuset->mems_generation = cpuset_mems_generation++;
	return 0;
}

/**
 * cpuset_init - initialize cpusets at system boot
 *
 * Description: Initialize top_cpuset and the cpuset internal file system,
 **/

int __init cpuset_init(void)
{
	struct dentry *root;
	int err;

	top_cpuset.cpus_allowed = CPU_MASK_ALL;
	top_cpuset.mems_allowed = NODE_MASK_ALL;

	fmeter_init(&top_cpuset.fmeter);
	top_cpuset.mems_generation = cpuset_mems_generation++;

	init_task.cpuset = &top_cpuset;

	err = register_filesystem(&cpuset_fs_type);
	if (err < 0)
		goto out;
	cpuset_mount = kern_mount(&cpuset_fs_type);
	if (IS_ERR(cpuset_mount)) {
		printk(KERN_ERR "cpuset: could not mount!\n");
		err = PTR_ERR(cpuset_mount);
		cpuset_mount = NULL;
		goto out;
	}
	root = cpuset_mount->mnt_sb->s_root;
	root->d_fsdata = &top_cpuset;
	root->d_inode->i_nlink++;
	top_cpuset.dentry = root;
	root->d_inode->i_op = &cpuset_dir_inode_operations;
	number_of_cpusets = 1;
	err = cpuset_populate_dir(root);
	/* memory_pressure_enabled is in root cpuset only */
	if (err == 0)
		err = cpuset_add_file(root, &cft_memory_pressure_enabled);
out:
	return err;
}

/**
 * cpuset_init_smp - initialize cpus_allowed
 *
 * Description: Finish top cpuset after cpu, node maps are initialized
 **/

void __init cpuset_init_smp(void)
{
	top_cpuset.cpus_allowed = cpu_online_map;
	top_cpuset.mems_allowed = node_online_map;
}

/**
 * cpuset_fork - attach newly forked task to its parents cpuset.
 * @tsk: pointer to task_struct of forking parent process.
 *
 * Description: A task inherits its parent's cpuset at fork().
 *
 * A pointer to the shared cpuset was automatically copied in fork.c
 * by dup_task_struct().  However, we ignore that copy, since it was
 * not made under the protection of task_lock(), so might no longer be
 * a valid cpuset pointer.  attach_task() might have already changed
 * current->cpuset, allowing the previously referenced cpuset to
 * be removed and freed.  Instead, we task_lock(current) and copy
 * its present value of current->cpuset for our freshly forked child.
 *
 * At the point that cpuset_fork() is called, 'current' is the parent
 * task, and the passed argument 'child' points to the child task.
 **/

void cpuset_fork(struct task_struct *child)
{
	task_lock(current);
	child->cpuset = current->cpuset;
	atomic_inc(&child->cpuset->count);
	task_unlock(current);
}

/**
 * cpuset_exit - detach cpuset from exiting task
 * @tsk: pointer to task_struct of exiting process
 *
 * Description: Detach cpuset from @tsk and release it.
 *
 * Note that cpusets marked notify_on_release force every task in
 * them to take the global manage_mutex mutex when exiting.
 * This could impact scaling on very large systems.  Be reluctant to
 * use notify_on_release cpusets where very high task exit scaling
 * is required on large systems.
 *
 * Don't even think about derefencing 'cs' after the cpuset use count
 * goes to zero, except inside a critical section guarded by manage_mutex
 * or callback_mutex.   Otherwise a zero cpuset use count is a license to
 * any other task to nuke the cpuset immediately, via cpuset_rmdir().
 *
 * This routine has to take manage_mutex, not callback_mutex, because
 * it is holding that mutex while calling check_for_release(),
 * which calls kmalloc(), so can't be called holding callback_mutex().
 *
 * We don't need to task_lock() this reference to tsk->cpuset,
 * because tsk is already marked PF_EXITING, so attach_task() won't
 * mess with it, or task is a failed fork, never visible to attach_task.
 *
 * the_top_cpuset_hack:
 *
 *    Set the exiting tasks cpuset to the root cpuset (top_cpuset).
 *
 *    Don't leave a task unable to allocate memory, as that is an
 *    accident waiting to happen should someone add a callout in
 *    do_exit() after the cpuset_exit() call that might allocate.
 *    If a task tries to allocate memory with an invalid cpuset,
 *    it will oops in cpuset_update_task_memory_state().
 *
 *    We call cpuset_exit() while the task is still competent to
 *    handle notify_on_release(), then leave the task attached to
 *    the root cpuset (top_cpuset) for the remainder of its exit.
 *
 *    To do this properly, we would increment the reference count on
 *    top_cpuset, and near the very end of the kernel/exit.c do_exit()
 *    code we would add a second cpuset function call, to drop that
 *    reference.  This would just create an unnecessary hot spot on
 *    the top_cpuset reference count, to no avail.
 *
 *    Normally, holding a reference to a cpuset without bumping its
 *    count is unsafe.   The cpuset could go away, or someone could
 *    attach us to a different cpuset, decrementing the count on
 *    the first cpuset that we never incremented.  But in this case,
 *    top_cpuset isn't going away, and either task has PF_EXITING set,
 *    which wards off any attach_task() attempts, or task is a failed
 *    fork, never visible to attach_task.
 *
 *    Another way to do this would be to set the cpuset pointer
 *    to NULL here, and check in cpuset_update_task_memory_state()
 *    for a NULL pointer.  This hack avoids that NULL check, for no
 *    cost (other than this way too long comment ;).
 **/

void cpuset_exit(struct task_struct *tsk)
{
	struct cpuset *cs;

	cs = tsk->cpuset;
	tsk->cpuset = &top_cpuset;	/* the_top_cpuset_hack - see above */

	if (notify_on_release(cs)) {
		char *pathbuf = NULL;

		mutex_lock(&manage_mutex);
		if (atomic_dec_and_test(&cs->count))
			check_for_release(cs, &pathbuf);
		mutex_unlock(&manage_mutex);
		cpuset_release_agent(pathbuf);
	} else {
		atomic_dec(&cs->count);
	}
}

/**
 * cpuset_cpus_allowed - return cpus_allowed mask from a tasks cpuset.
 * @tsk: pointer to task_struct from which to obtain cpuset->cpus_allowed.
 *
 * Description: Returns the cpumask_t cpus_allowed of the cpuset
 * attached to the specified @tsk.  Guaranteed to return some non-empty
 * subset of cpu_online_map, even if this means going outside the
 * tasks cpuset.
 **/

cpumask_t cpuset_cpus_allowed(struct task_struct *tsk)
{
	cpumask_t mask;

	mutex_lock(&callback_mutex);
	task_lock(tsk);
	guarantee_online_cpus(tsk->cpuset, &mask);
	task_unlock(tsk);
	mutex_unlock(&callback_mutex);

	return mask;
}

void cpuset_init_current_mems_allowed(void)
{
	current->mems_allowed = NODE_MASK_ALL;
}

/**
 * cpuset_mems_allowed - return mems_allowed mask from a tasks cpuset.
 * @tsk: pointer to task_struct from which to obtain cpuset->mems_allowed.
 *
 * Description: Returns the nodemask_t mems_allowed of the cpuset
 * attached to the specified @tsk.  Guaranteed to return some non-empty
 * subset of node_online_map, even if this means going outside the
 * tasks cpuset.
 **/

nodemask_t cpuset_mems_allowed(struct task_struct *tsk)
{
	nodemask_t mask;

	mutex_lock(&callback_mutex);
	task_lock(tsk);
	guarantee_online_mems(tsk->cpuset, &mask);
	task_unlock(tsk);
	mutex_unlock(&callback_mutex);

	return mask;
}

/**
 * cpuset_zonelist_valid_mems_allowed - check zonelist vs. curremt mems_allowed
 * @zl: the zonelist to be checked
 *
 * Are any of the nodes on zonelist zl allowed in current->mems_allowed?
 */
int cpuset_zonelist_valid_mems_allowed(struct zonelist *zl)
{
	int i;

	for (i = 0; zl->zones[i]; i++) {
		int nid = zl->zones[i]->zone_pgdat->node_id;

		if (node_isset(nid, current->mems_allowed))
			return 1;
	}
	return 0;
}

/*
 * nearest_exclusive_ancestor() - Returns the nearest mem_exclusive
 * ancestor to the specified cpuset.  Call holding callback_mutex.
 * If no ancestor is mem_exclusive (an unusual configuration), then
 * returns the root cpuset.
 */
static const struct cpuset *nearest_exclusive_ancestor(const struct cpuset *cs)
{
	while (!is_mem_exclusive(cs) && cs->parent)
		cs = cs->parent;
	return cs;
}

/**
 * cpuset_zone_allowed - Can we allocate memory on zone z's memory node?
 * @z: is this zone on an allowed node?
 * @gfp_mask: memory allocation flags (we use __GFP_HARDWALL)
 *
 * If we're in interrupt, yes, we can always allocate.  If zone
 * z's node is in our tasks mems_allowed, yes.  If it's not a
 * __GFP_HARDWALL request and this zone's nodes is in the nearest
 * mem_exclusive cpuset ancestor to this tasks cpuset, yes.
 * Otherwise, no.
 *
 * GFP_USER allocations are marked with the __GFP_HARDWALL bit,
 * and do not allow allocations outside the current tasks cpuset.
 * GFP_KERNEL allocations are not so marked, so can escape to the
 * nearest mem_exclusive ancestor cpuset.
 *
 * Scanning up parent cpusets requires callback_mutex.  The __alloc_pages()
 * routine only calls here with __GFP_HARDWALL bit _not_ set if
 * it's a GFP_KERNEL allocation, and all nodes in the current tasks
 * mems_allowed came up empty on the first pass over the zonelist.
 * So only GFP_KERNEL allocations, if all nodes in the cpuset are
 * short of memory, might require taking the callback_mutex mutex.
 *
 * The first loop over the zonelist in mm/page_alloc.c:__alloc_pages()
 * calls here with __GFP_HARDWALL always set in gfp_mask, enforcing
 * hardwall cpusets - no allocation on a node outside the cpuset is
 * allowed (unless in interrupt, of course).
 *
 * The second loop doesn't even call here for GFP_ATOMIC requests
 * (if the __alloc_pages() local variable 'wait' is set).  That check
 * and the checks below have the combined affect in the second loop of
 * the __alloc_pages() routine that:
 *	in_interrupt - any node ok (current task context irrelevant)
 *	GFP_ATOMIC   - any node ok
 *	GFP_KERNEL   - any node in enclosing mem_exclusive cpuset ok
 *	GFP_USER     - only nodes in current tasks mems allowed ok.
 **/

int __cpuset_zone_allowed(struct zone *z, gfp_t gfp_mask)
{
	int node;			/* node that zone z is on */
	const struct cpuset *cs;	/* current cpuset ancestors */
	int allowed;			/* is allocation in zone z allowed? */

	if (in_interrupt())
		return 1;
	node = z->zone_pgdat->node_id;
	if (node_isset(node, current->mems_allowed))
		return 1;
	if (gfp_mask & __GFP_HARDWALL)	/* If hardwall request, stop here */
		return 0;

	if (current->flags & PF_EXITING) /* Let dying task have memory */
		return 1;

	/* Not hardwall and node outside mems_allowed: scan up cpusets */
	mutex_lock(&callback_mutex);

	task_lock(current);
	cs = nearest_exclusive_ancestor(current->cpuset);
	task_unlock(current);

	allowed = node_isset(node, cs->mems_allowed);
	mutex_unlock(&callback_mutex);
	return allowed;
}

/**
 * cpuset_lock - lock out any changes to cpuset structures
 *
 * The out of memory (oom) code needs to mutex_lock cpusets
 * from being changed while it scans the tasklist looking for a
 * task in an overlapping cpuset.  Expose callback_mutex via this
 * cpuset_lock() routine, so the oom code can lock it, before
 * locking the task list.  The tasklist_lock is a spinlock, so
 * must be taken inside callback_mutex.
 */

void cpuset_lock(void)
{
	mutex_lock(&callback_mutex);
}

/**
 * cpuset_unlock - release lock on cpuset changes
 *
 * Undo the lock taken in a previous cpuset_lock() call.
 */

void cpuset_unlock(void)
{
	mutex_unlock(&callback_mutex);
}

/**
 * cpuset_mem_spread_node() - On which node to begin search for a page
 *
 * If a task is marked PF_SPREAD_PAGE or PF_SPREAD_SLAB (as for
 * tasks in a cpuset with is_spread_page or is_spread_slab set),
 * and if the memory allocation used cpuset_mem_spread_node()
 * to determine on which node to start looking, as it will for
 * certain page cache or slab cache pages such as used for file
 * system buffers and inode caches, then instead of starting on the
 * local node to look for a free page, rather spread the starting
 * node around the tasks mems_allowed nodes.
 *
 * We don't have to worry about the returned node being offline
 * because "it can't happen", and even if it did, it would be ok.
 *
 * The routines calling guarantee_online_mems() are careful to
 * only set nodes in task->mems_allowed that are online.  So it
 * should not be possible for the following code to return an
 * offline node.  But if it did, that would be ok, as this routine
 * is not returning the node where the allocation must be, only
 * the node where the search should start.  The zonelist passed to
 * __alloc_pages() will include all nodes.  If the slab allocator
 * is passed an offline node, it will fall back to the local node.
 * See kmem_cache_alloc_node().
 */

int cpuset_mem_spread_node(void)
{
	int node;

	node = next_node(current->cpuset_mem_spread_rotor, current->mems_allowed);
	if (node == MAX_NUMNODES)
		node = first_node(current->mems_allowed);
	current->cpuset_mem_spread_rotor = node;
	return node;
}
EXPORT_SYMBOL_GPL(cpuset_mem_spread_node);

/**
 * cpuset_excl_nodes_overlap - Do we overlap @p's mem_exclusive ancestors?
 * @p: pointer to task_struct of some other task.
 *
 * Description: Return true if the nearest mem_exclusive ancestor
 * cpusets of tasks @p and current overlap.  Used by oom killer to
 * determine if task @p's memory usage might impact the memory
 * available to the current task.
 *
 * Call while holding callback_mutex.
 **/

int cpuset_excl_nodes_overlap(const struct task_struct *p)
{
	const struct cpuset *cs1, *cs2;	/* my and p's cpuset ancestors */
	int overlap = 0;		/* do cpusets overlap? */

	task_lock(current);
	if (current->flags & PF_EXITING) {
		task_unlock(current);
		goto done;
	}
	cs1 = nearest_exclusive_ancestor(current->cpuset);
	task_unlock(current);

	task_lock((struct task_struct *)p);
	if (p->flags & PF_EXITING) {
		task_unlock((struct task_struct *)p);
		goto done;
	}
	cs2 = nearest_exclusive_ancestor(p->cpuset);
	task_unlock((struct task_struct *)p);

	overlap = nodes_intersects(cs1->mems_allowed, cs2->mems_allowed);
done:
	return overlap;
}

/*
 * Collection of memory_pressure is suppressed unless
 * this flag is enabled by writing "1" to the special
 * cpuset file 'memory_pressure_enabled' in the root cpuset.
 */

int cpuset_memory_pressure_enabled __read_mostly;

/**
 * cpuset_memory_pressure_bump - keep stats of per-cpuset reclaims.
 *
 * Keep a running average of the rate of synchronous (direct)
 * page reclaim efforts initiated by tasks in each cpuset.
 *
 * This represents the rate at which some task in the cpuset
 * ran low on memory on all nodes it was allowed to use, and
 * had to enter the kernels page reclaim code in an effort to
 * create more free memory by tossing clean pages or swapping
 * or writing dirty pages.
 *
 * Display to user space in the per-cpuset read-only file
 * "memory_pressure".  Value displayed is an integer
 * representing the recent rate of entry into the synchronous
 * (direct) page reclaim by any task attached to the cpuset.
 **/

void __cpuset_memory_pressure_bump(void)
{
	struct cpuset *cs;

	task_lock(current);
	cs = current->cpuset;
	fmeter_markevent(&cs->fmeter);
	task_unlock(current);
}

/*
 * proc_cpuset_show()
 *  - Print tasks cpuset path into seq_file.
 *  - Used for /proc/<pid>/cpuset.
 *  - No need to task_lock(tsk) on this tsk->cpuset reference, as it
 *    doesn't really matter if tsk->cpuset changes after we read it,
 *    and we take manage_mutex, keeping attach_task() from changing it
 *    anyway.  No need to check that tsk->cpuset != NULL, thanks to
 *    the_top_cpuset_hack in cpuset_exit(), which sets an exiting tasks
 *    cpuset to top_cpuset.
 */
static int proc_cpuset_show(struct seq_file *m, void *v)
{
	struct task_struct *tsk;
	char *buf;
	int retval = 0;

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	tsk = m->private;
	mutex_lock(&manage_mutex);
	retval = cpuset_path(tsk->cpuset, buf, PAGE_SIZE);
	if (retval < 0)
		goto out;
	seq_puts(m, buf);
	seq_putc(m, '\n');
out:
	mutex_unlock(&manage_mutex);
	kfree(buf);
	return retval;
}

static int cpuset_open(struct inode *inode, struct file *file)
{
	struct task_struct *tsk = PROC_I(inode)->task;
	return single_open(file, proc_cpuset_show, tsk);
}

struct file_operations proc_cpuset_operations = {
	.open		= cpuset_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/* Display task cpus_allowed, mems_allowed in /proc/<pid>/status file. */
char *cpuset_task_status_allowed(struct task_struct *task, char *buffer)
{
	buffer += sprintf(buffer, "Cpus_allowed:\t");
	buffer += cpumask_scnprintf(buffer, PAGE_SIZE, task->cpus_allowed);
	buffer += sprintf(buffer, "\n");
	buffer += sprintf(buffer, "Mems_allowed:\t");
	buffer += nodemask_scnprintf(buffer, PAGE_SIZE, task->mems_allowed);
	buffer += sprintf(buffer, "\n");
	return buffer;
}
