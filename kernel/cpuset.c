/*
 *  kernel/cpuset.c
 *
 *  Processor and Memory placement constraints for sets of tasks.
 *
 *  Copyright (C) 2003 BULL SA.
 *  Copyright (C) 2004 Silicon Graphics, Inc.
 *
 *  Portions derived from Patrick Mochel's sysfs code.
 *  sysfs is Copyright (c) 2001-3 Patrick Mochel
 *  Portions Copyright (c) 2004 Silicon Graphics, Inc.
 *
 *  2003-10-10 Written by Simon Derr <simon.derr@bull.net>
 *  2003-10-22 Updates by Stephen Hemminger.
 *  2004 May-July Rework by Paul Jackson <pj@sgi.com>
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
#include <asm/semaphore.h>

#define CPUSET_SUPER_MAGIC 		0x27e0eb

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
};

/* bits in struct cpuset flags field */
typedef enum {
	CS_CPU_EXCLUSIVE,
	CS_MEM_EXCLUSIVE,
	CS_REMOVED,
	CS_NOTIFY_ON_RELEASE
} cpuset_flagbits_t;

/* convenient tests for these bits */
static inline int is_cpu_exclusive(const struct cpuset *cs)
{
	return !!test_bit(CS_CPU_EXCLUSIVE, &cs->flags);
}

static inline int is_mem_exclusive(const struct cpuset *cs)
{
	return !!test_bit(CS_MEM_EXCLUSIVE, &cs->flags);
}

static inline int is_removed(const struct cpuset *cs)
{
	return !!test_bit(CS_REMOVED, &cs->flags);
}

static inline int notify_on_release(const struct cpuset *cs)
{
	return !!test_bit(CS_NOTIFY_ON_RELEASE, &cs->flags);
}

/*
 * Increment this atomic integer everytime any cpuset changes its
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
 */
static atomic_t cpuset_mems_generation = ATOMIC_INIT(1);

static struct cpuset top_cpuset = {
	.flags = ((1 << CS_CPU_EXCLUSIVE) | (1 << CS_MEM_EXCLUSIVE)),
	.cpus_allowed = CPU_MASK_ALL,
	.mems_allowed = NODE_MASK_ALL,
	.count = ATOMIC_INIT(0),
	.sibling = LIST_HEAD_INIT(top_cpuset.sibling),
	.children = LIST_HEAD_INIT(top_cpuset.children),
	.parent = NULL,
	.dentry = NULL,
	.mems_generation = 0,
};

static struct vfsmount *cpuset_mount;
static struct super_block *cpuset_sb = NULL;

/*
 * We have two global cpuset semaphores below.  They can nest.
 * It is ok to first take manage_sem, then nest callback_sem.  We also
 * require taking task_lock() when dereferencing a tasks cpuset pointer.
 * See "The task_lock() exception", at the end of this comment.
 *
 * A task must hold both semaphores to modify cpusets.  If a task
 * holds manage_sem, then it blocks others wanting that semaphore,
 * ensuring that it is the only task able to also acquire callback_sem
 * and be able to modify cpusets.  It can perform various checks on
 * the cpuset structure first, knowing nothing will change.  It can
 * also allocate memory while just holding manage_sem.  While it is
 * performing these checks, various callback routines can briefly
 * acquire callback_sem to query cpusets.  Once it is ready to make
 * the changes, it takes callback_sem, blocking everyone else.
 *
 * Calls to the kernel memory allocator can not be made while holding
 * callback_sem, as that would risk double tripping on callback_sem
 * from one of the callbacks into the cpuset code from within
 * __alloc_pages().
 *
 * If a task is only holding callback_sem, then it has read-only
 * access to cpusets.
 *
 * The task_struct fields mems_allowed and mems_generation may only
 * be accessed in the context of that task, so require no locks.
 *
 * Any task can increment and decrement the count field without lock.
 * So in general, code holding manage_sem or callback_sem can't rely
 * on the count field not changing.  However, if the count goes to
 * zero, then only attach_task(), which holds both semaphores, can
 * increment it again.  Because a count of zero means that no tasks
 * are currently attached, therefore there is no way a task attached
 * to that cpuset can fork (the other way to increment the count).
 * So code holding manage_sem or callback_sem can safely assume that
 * if the count is zero, it will stay zero.  Similarly, if a task
 * holds manage_sem or callback_sem on a cpuset with zero count, it
 * knows that the cpuset won't be removed, as cpuset_rmdir() needs
 * both of those semaphores.
 *
 * A possible optimization to improve parallelism would be to make
 * callback_sem a R/W semaphore (rwsem), allowing the callback routines
 * to proceed in parallel, with read access, until the holder of
 * manage_sem needed to take this rwsem for exclusive write access
 * and modify some cpusets.
 *
 * The cpuset_common_file_write handler for operations that modify
 * the cpuset hierarchy holds manage_sem across the entire operation,
 * single threading all such cpuset modifications across the system.
 *
 * The cpuset_common_file_read() handlers only hold callback_sem across
 * small pieces of code, such as when reading out possibly multi-word
 * cpumasks and nodemasks.
 *
 * The fork and exit callbacks cpuset_fork() and cpuset_exit(), don't
 * (usually) take either semaphore.  These are the two most performance
 * critical pieces of code here.  The exception occurs on cpuset_exit(),
 * when a task in a notify_on_release cpuset exits.  Then manage_sem
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
 * so using both semaphores, however there are several performance
 * critical places that need to reference task->cpuset without the
 * expense of grabbing a system global semaphore.  Therefore except as
 * noted below, when dereferencing or, as in attach_task(), modifying
 * a tasks cpuset pointer we use task_lock(), which acts on a spinlock
 * (task->alloc_lock) already in the task_struct routinely used for
 * such matters.
 */

static DECLARE_MUTEX(manage_sem);
static DECLARE_MUTEX(callback_sem);

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
		struct dentry *d = list_entry(node, struct dentry, d_child);
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
	list_del_init(&dentry->d_child);
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
 * Call with manage_sem held.  Writes path of cpuset into buf.
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
 * When we had only one cpuset semaphore, we had to call this
 * without holding it, to avoid deadlock when call_usermodehelper()
 * allocated memory.  With two locks, we could now call this while
 * holding manage_sem, but we still don't, so as to minimize
 * the time manage_sem is held.
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
 * cpuset_release_agent() with it later on, once manage_sem is dropped.
 * Call here with manage_sem held.
 *
 * This check_for_release() routine is responsible for kmalloc'ing
 * pathbuf.  The above cpuset_release_agent() is responsible for
 * kfree'ing pathbuf.  The caller of these routines is responsible
 * for providing a pathbuf pointer, initialized to NULL, then
 * calling check_for_release() with manage_sem held and the address
 * of the pathbuf pointer, then dropping manage_sem, then calling
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
 * Call with callback_sem held.
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
 * Call with callback_sem held.
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

/*
 * Refresh current tasks mems_allowed and mems_generation from current
 * tasks cpuset.
 *
 * Call without callback_sem or task_lock() held.  May be called with
 * or without manage_sem held.  Will acquire task_lock() and might
 * acquire callback_sem during call.
 *
 * The task_lock() is required to dereference current->cpuset safely.
 * Without it, we could pick up the pointer value of current->cpuset
 * in one instruction, and then attach_task could give us a different
 * cpuset, and then the cpuset we had could be removed and freed,
 * and then on our next instruction, we could dereference a no longer
 * valid cpuset pointer to get its mems_generation field.
 *
 * This routine is needed to update the per-task mems_allowed data,
 * within the tasks context, when it is trying to allocate memory
 * (in various mm/mempolicy.c routines) and notices that some other
 * task has been modifying its cpuset.
 */

static void refresh_mems(void)
{
	int my_cpusets_mem_gen;

	task_lock(current);
	my_cpusets_mem_gen = current->cpuset->mems_generation;
	task_unlock(current);

	if (current->cpuset_mems_generation != my_cpusets_mem_gen) {
		struct cpuset *cs;
		nodemask_t oldmem = current->mems_allowed;

		down(&callback_sem);
		task_lock(current);
		cs = current->cpuset;
		guarantee_online_mems(cs, &current->mems_allowed);
		current->cpuset_mems_generation = cs->mems_generation;
		task_unlock(current);
		up(&callback_sem);
		if (!nodes_equal(oldmem, current->mems_allowed))
			numa_policy_rebind(&oldmem, &current->mems_allowed);
	}
}

/*
 * is_cpuset_subset(p, q) - Is cpuset p a subset of cpuset q?
 *
 * One cpuset is a subset of another if all its allowed CPUs and
 * Memory Nodes are a subset of the other, and its exclusive flags
 * are only set if the other's are set.  Call holding manage_sem.
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
 * manage_sem held.
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
 * Call with manage_sem held.  May nest a call to the
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
 * Call with manage_sem held.  May take callback_sem during call.
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
	down(&callback_sem);
	cs->cpus_allowed = trialcs.cpus_allowed;
	up(&callback_sem);
	if (is_cpu_exclusive(cs) && !cpus_unchanged)
		update_cpu_domains(cs);
	return 0;
}

/*
 * Call with manage_sem held.  May take callback_sem during call.
 */

static int update_nodemask(struct cpuset *cs, char *buf)
{
	struct cpuset trialcs;
	int retval;

	trialcs = *cs;
	retval = nodelist_parse(buf, trialcs.mems_allowed);
	if (retval < 0)
		return retval;
	nodes_and(trialcs.mems_allowed, trialcs.mems_allowed, node_online_map);
	if (nodes_empty(trialcs.mems_allowed))
		return -ENOSPC;
	retval = validate_change(cs, &trialcs);
	if (retval == 0) {
		down(&callback_sem);
		cs->mems_allowed = trialcs.mems_allowed;
		atomic_inc(&cpuset_mems_generation);
		cs->mems_generation = atomic_read(&cpuset_mems_generation);
		up(&callback_sem);
	}
	return retval;
}

/*
 * update_flag - read a 0 or a 1 in a file and update associated flag
 * bit:	the bit to update (CS_CPU_EXCLUSIVE, CS_MEM_EXCLUSIVE,
 *						CS_NOTIFY_ON_RELEASE)
 * cs:	the cpuset to update
 * buf:	the buffer where we read the 0 or 1
 *
 * Call with manage_sem held.
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
	down(&callback_sem);
	if (turning_on)
		set_bit(bit, &cs->flags);
	else
		clear_bit(bit, &cs->flags);
	up(&callback_sem);

	if (cpu_exclusive_changed)
                update_cpu_domains(cs);
	return 0;
}

/*
 * Attack task specified by pid in 'pidbuf' to cpuset 'cs', possibly
 * writing the path of the old cpuset in 'ppathbuf' if it needs to be
 * notified on release.
 *
 * Call holding manage_sem.  May take callback_sem and task_lock of
 * the task 'pid' during call.
 */

static int attach_task(struct cpuset *cs, char *pidbuf, char **ppathbuf)
{
	pid_t pid;
	struct task_struct *tsk;
	struct cpuset *oldcs;
	cpumask_t cpus;

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

	down(&callback_sem);

	task_lock(tsk);
	oldcs = tsk->cpuset;
	if (!oldcs) {
		task_unlock(tsk);
		up(&callback_sem);
		put_task_struct(tsk);
		return -ESRCH;
	}
	atomic_inc(&cs->count);
	tsk->cpuset = cs;
	task_unlock(tsk);

	guarantee_online_cpus(cs, &cpus);
	set_cpus_allowed(tsk, cpus);

	up(&callback_sem);
	put_task_struct(tsk);
	if (atomic_dec_and_test(&oldcs->count))
		check_for_release(oldcs, ppathbuf);
	return 0;
}

/* The various types of files and directories in a cpuset file system */

typedef enum {
	FILE_ROOT,
	FILE_DIR,
	FILE_CPULIST,
	FILE_MEMLIST,
	FILE_CPU_EXCLUSIVE,
	FILE_MEM_EXCLUSIVE,
	FILE_NOTIFY_ON_RELEASE,
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

	down(&manage_sem);

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
	up(&manage_sem);
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

	down(&callback_sem);
	mask = cs->cpus_allowed;
	up(&callback_sem);

	return cpulist_scnprintf(page, PAGE_SIZE, mask);
}

static int cpuset_sprintf_memlist(char *page, struct cpuset *cs)
{
	nodemask_t mask;

	down(&callback_sem);
	mask = cs->mems_allowed;
	up(&callback_sem);

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
 *	cs: 	the cpuset we create the directory for.
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

	down(&dir->d_inode->i_sem);
	dentry = cpuset_get_dentry(dir, cft->name);
	if (!IS_ERR(dentry)) {
		error = cpuset_create_file(dentry, 0644 | S_IFREG);
		if (!error)
			dentry->d_fsdata = (void *)cft;
		dput(dentry);
	} else
		error = PTR_ERR(dentry);
	up(&dir->d_inode->i_sem);
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
static inline int pid_array_load(pid_t *pidarray, int npids, struct cpuset *cs)
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
 * Does not require any specific cpuset semaphores, and does not take any.
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
 *	Must be called with the semaphore on the parent inode held
 */

static long cpuset_create(struct cpuset *parent, const char *name, int mode)
{
	struct cpuset *cs;
	int err;

	cs = kmalloc(sizeof(*cs), GFP_KERNEL);
	if (!cs)
		return -ENOMEM;

	down(&manage_sem);
	refresh_mems();
	cs->flags = 0;
	if (notify_on_release(parent))
		set_bit(CS_NOTIFY_ON_RELEASE, &cs->flags);
	cs->cpus_allowed = CPU_MASK_NONE;
	cs->mems_allowed = NODE_MASK_NONE;
	atomic_set(&cs->count, 0);
	INIT_LIST_HEAD(&cs->sibling);
	INIT_LIST_HEAD(&cs->children);
	atomic_inc(&cpuset_mems_generation);
	cs->mems_generation = atomic_read(&cpuset_mems_generation);

	cs->parent = parent;

	down(&callback_sem);
	list_add(&cs->sibling, &cs->parent->children);
	up(&callback_sem);

	err = cpuset_create_dir(cs, name, mode);
	if (err < 0)
		goto err;

	/*
	 * Release manage_sem before cpuset_populate_dir() because it
	 * will down() this new directory's i_sem and if we race with
	 * another mkdir, we might deadlock.
	 */
	up(&manage_sem);

	err = cpuset_populate_dir(cs->dentry);
	/* If err < 0, we have a half-filled directory - oh well ;) */
	return 0;
err:
	list_del(&cs->sibling);
	up(&manage_sem);
	kfree(cs);
	return err;
}

static int cpuset_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	struct cpuset *c_parent = dentry->d_parent->d_fsdata;

	/* the vfs holds inode->i_sem already */
	return cpuset_create(c_parent, dentry->d_name.name, mode | S_IFDIR);
}

static int cpuset_rmdir(struct inode *unused_dir, struct dentry *dentry)
{
	struct cpuset *cs = dentry->d_fsdata;
	struct dentry *d;
	struct cpuset *parent;
	char *pathbuf = NULL;

	/* the vfs holds both inode->i_sem already */

	down(&manage_sem);
	refresh_mems();
	if (atomic_read(&cs->count) > 0) {
		up(&manage_sem);
		return -EBUSY;
	}
	if (!list_empty(&cs->children)) {
		up(&manage_sem);
		return -EBUSY;
	}
	parent = cs->parent;
	down(&callback_sem);
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
	up(&callback_sem);
	if (list_empty(&parent->children))
		check_for_release(parent, &pathbuf);
	up(&manage_sem);
	cpuset_release_agent(pathbuf);
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

	atomic_inc(&cpuset_mems_generation);
	top_cpuset.mems_generation = atomic_read(&cpuset_mems_generation);

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
	err = cpuset_populate_dir(root);
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
 * them to take the global manage_sem semaphore when exiting.
 * This could impact scaling on very large systems.  Be reluctant to
 * use notify_on_release cpusets where very high task exit scaling
 * is required on large systems.
 *
 * Don't even think about derefencing 'cs' after the cpuset use count
 * goes to zero, except inside a critical section guarded by manage_sem
 * or callback_sem.   Otherwise a zero cpuset use count is a license to
 * any other task to nuke the cpuset immediately, via cpuset_rmdir().
 *
 * This routine has to take manage_sem, not callback_sem, because
 * it is holding that semaphore while calling check_for_release(),
 * which calls kmalloc(), so can't be called holding callback__sem().
 *
 * We don't need to task_lock() this reference to tsk->cpuset,
 * because tsk is already marked PF_EXITING, so attach_task() won't
 * mess with it.
 **/

void cpuset_exit(struct task_struct *tsk)
{
	struct cpuset *cs;

	BUG_ON(!(tsk->flags & PF_EXITING));

	cs = tsk->cpuset;
	tsk->cpuset = NULL;

	if (notify_on_release(cs)) {
		char *pathbuf = NULL;

		down(&manage_sem);
		if (atomic_dec_and_test(&cs->count))
			check_for_release(cs, &pathbuf);
		up(&manage_sem);
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

cpumask_t cpuset_cpus_allowed(const struct task_struct *tsk)
{
	cpumask_t mask;

	down(&callback_sem);
	task_lock((struct task_struct *)tsk);
	guarantee_online_cpus(tsk->cpuset, &mask);
	task_unlock((struct task_struct *)tsk);
	up(&callback_sem);

	return mask;
}

void cpuset_init_current_mems_allowed(void)
{
	current->mems_allowed = NODE_MASK_ALL;
}

/**
 * cpuset_update_current_mems_allowed - update mems parameters to new values
 *
 * If the current tasks cpusets mems_allowed changed behind our backs,
 * update current->mems_allowed and mems_generation to the new value.
 * Do not call this routine if in_interrupt().
 *
 * Call without callback_sem or task_lock() held.  May be called
 * with or without manage_sem held.  Unless exiting, it will acquire
 * task_lock().  Also might acquire callback_sem during call to
 * refresh_mems().
 */

void cpuset_update_current_mems_allowed(void)
{
	struct cpuset *cs;
	int need_to_refresh = 0;

	task_lock(current);
	cs = current->cpuset;
	if (!cs)
		goto done;
	if (current->cpuset_mems_generation != cs->mems_generation)
		need_to_refresh = 1;
done:
	task_unlock(current);
	if (need_to_refresh)
		refresh_mems();
}

/**
 * cpuset_restrict_to_mems_allowed - limit nodes to current mems_allowed
 * @nodes: pointer to a node bitmap that is and-ed with mems_allowed
 */
void cpuset_restrict_to_mems_allowed(unsigned long *nodes)
{
	bitmap_and(nodes, nodes, nodes_addr(current->mems_allowed),
							MAX_NUMNODES);
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
 * ancestor to the specified cpuset.  Call holding callback_sem.
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
 * Scanning up parent cpusets requires callback_sem.  The __alloc_pages()
 * routine only calls here with __GFP_HARDWALL bit _not_ set if
 * it's a GFP_KERNEL allocation, and all nodes in the current tasks
 * mems_allowed came up empty on the first pass over the zonelist.
 * So only GFP_KERNEL allocations, if all nodes in the cpuset are
 * short of memory, might require taking the callback_sem semaphore.
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

int cpuset_zone_allowed(struct zone *z, gfp_t gfp_mask)
{
	int node;			/* node that zone z is on */
	const struct cpuset *cs;	/* current cpuset ancestors */
	int allowed = 1;		/* is allocation in zone z allowed? */

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
	down(&callback_sem);

	task_lock(current);
	cs = nearest_exclusive_ancestor(current->cpuset);
	task_unlock(current);

	allowed = node_isset(node, cs->mems_allowed);
	up(&callback_sem);
	return allowed;
}

/**
 * cpuset_excl_nodes_overlap - Do we overlap @p's mem_exclusive ancestors?
 * @p: pointer to task_struct of some other task.
 *
 * Description: Return true if the nearest mem_exclusive ancestor
 * cpusets of tasks @p and current overlap.  Used by oom killer to
 * determine if task @p's memory usage might impact the memory
 * available to the current task.
 *
 * Acquires callback_sem - not suitable for calling from a fast path.
 **/

int cpuset_excl_nodes_overlap(const struct task_struct *p)
{
	const struct cpuset *cs1, *cs2;	/* my and p's cpuset ancestors */
	int overlap = 0;		/* do cpusets overlap? */

	down(&callback_sem);

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
	up(&callback_sem);

	return overlap;
}

/*
 * proc_cpuset_show()
 *  - Print tasks cpuset path into seq_file.
 *  - Used for /proc/<pid>/cpuset.
 *  - No need to task_lock(tsk) on this tsk->cpuset reference, as it
 *    doesn't really matter if tsk->cpuset changes after we read it,
 *    and we take manage_sem, keeping attach_task() from changing it
 *    anyway.
 */

static int proc_cpuset_show(struct seq_file *m, void *v)
{
	struct cpuset *cs;
	struct task_struct *tsk;
	char *buf;
	int retval = 0;

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	tsk = m->private;
	down(&manage_sem);
	cs = tsk->cpuset;
	if (!cs) {
		retval = -EINVAL;
		goto out;
	}

	retval = cpuset_path(cs, buf, PAGE_SIZE);
	if (retval < 0)
		goto out;
	seq_puts(m, buf);
	seq_putc(m, '\n');
out:
	up(&manage_sem);
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
