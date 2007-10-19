/*
 *  kernel/cgroup.c
 *
 *  Generic process-grouping system.
 *
 *  Based originally on the cpuset system, extracted by Paul Menage
 *  Copyright (C) 2006 Google, Inc
 *
 *  Copyright notices from the original cpuset code:
 *  --------------------------------------------------
 *  Copyright (C) 2003 BULL SA.
 *  Copyright (C) 2004-2006 Silicon Graphics, Inc.
 *
 *  Portions derived from Patrick Mochel's sysfs code.
 *  sysfs is Copyright (c) 2001-3 Patrick Mochel
 *
 *  2003-10-10 Written by Simon Derr.
 *  2003-10-22 Updates by Stephen Hemminger.
 *  2004 May-July Rework by Paul Jackson.
 *  ---------------------------------------------------
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of the Linux
 *  distribution for more details.
 */

#include <linux/cgroup.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/magic.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/sort.h>
#include <asm/atomic.h>

/* Generate an array of cgroup subsystem pointers */
#define SUBSYS(_x) &_x ## _subsys,

static struct cgroup_subsys *subsys[] = {
#include <linux/cgroup_subsys.h>
};

/*
 * A cgroupfs_root represents the root of a cgroup hierarchy,
 * and may be associated with a superblock to form an active
 * hierarchy
 */
struct cgroupfs_root {
	struct super_block *sb;

	/*
	 * The bitmask of subsystems intended to be attached to this
	 * hierarchy
	 */
	unsigned long subsys_bits;

	/* The bitmask of subsystems currently attached to this hierarchy */
	unsigned long actual_subsys_bits;

	/* A list running through the attached subsystems */
	struct list_head subsys_list;

	/* The root cgroup for this hierarchy */
	struct cgroup top_cgroup;

	/* Tracks how many cgroups are currently defined in hierarchy.*/
	int number_of_cgroups;

	/* A list running through the mounted hierarchies */
	struct list_head root_list;

	/* Hierarchy-specific flags */
	unsigned long flags;
};


/*
 * The "rootnode" hierarchy is the "dummy hierarchy", reserved for the
 * subsystems that are otherwise unattached - it never has more than a
 * single cgroup, and all tasks are part of that cgroup.
 */
static struct cgroupfs_root rootnode;

/* The list of hierarchy roots */

static LIST_HEAD(roots);

/* dummytop is a shorthand for the dummy hierarchy's top cgroup */
#define dummytop (&rootnode.top_cgroup)

/* This flag indicates whether tasks in the fork and exit paths should
 * take callback_mutex and check for fork/exit handlers to call. This
 * avoids us having to do extra work in the fork/exit path if none of the
 * subsystems need to be called.
 */
static int need_forkexit_callback;

/* bits in struct cgroup flags field */
enum {
	CONT_REMOVED,
};

/* convenient tests for these bits */
inline int cgroup_is_removed(const struct cgroup *cont)
{
	return test_bit(CONT_REMOVED, &cont->flags);
}

/* bits in struct cgroupfs_root flags field */
enum {
	ROOT_NOPREFIX, /* mounted subsystems have no named prefix */
};

/*
 * for_each_subsys() allows you to iterate on each subsystem attached to
 * an active hierarchy
 */
#define for_each_subsys(_root, _ss) \
list_for_each_entry(_ss, &_root->subsys_list, sibling)

/* for_each_root() allows you to iterate across the active hierarchies */
#define for_each_root(_root) \
list_for_each_entry(_root, &roots, root_list)

/*
 * There is one global cgroup mutex. We also require taking
 * task_lock() when dereferencing a task's cgroup subsys pointers.
 * See "The task_lock() exception", at the end of this comment.
 *
 * A task must hold cgroup_mutex to modify cgroups.
 *
 * Any task can increment and decrement the count field without lock.
 * So in general, code holding cgroup_mutex can't rely on the count
 * field not changing.  However, if the count goes to zero, then only
 * attach_task() can increment it again.  Because a count of zero
 * means that no tasks are currently attached, therefore there is no
 * way a task attached to that cgroup can fork (the other way to
 * increment the count).  So code holding cgroup_mutex can safely
 * assume that if the count is zero, it will stay zero. Similarly, if
 * a task holds cgroup_mutex on a cgroup with zero count, it
 * knows that the cgroup won't be removed, as cgroup_rmdir()
 * needs that mutex.
 *
 * The cgroup_common_file_write handler for operations that modify
 * the cgroup hierarchy holds cgroup_mutex across the entire operation,
 * single threading all such cgroup modifications across the system.
 *
 * The fork and exit callbacks cgroup_fork() and cgroup_exit(), don't
 * (usually) take cgroup_mutex.  These are the two most performance
 * critical pieces of code here.  The exception occurs on cgroup_exit(),
 * when a task in a notify_on_release cgroup exits.  Then cgroup_mutex
 * is taken, and if the cgroup count is zero, a usermode call made
 * to /sbin/cgroup_release_agent with the name of the cgroup (path
 * relative to the root of cgroup file system) as the argument.
 *
 * A cgroup can only be deleted if both its 'count' of using tasks
 * is zero, and its list of 'children' cgroups is empty.  Since all
 * tasks in the system use _some_ cgroup, and since there is always at
 * least one task in the system (init, pid == 1), therefore, top_cgroup
 * always has either children cgroups and/or using tasks.  So we don't
 * need a special hack to ensure that top_cgroup cannot be deleted.
 *
 *	The task_lock() exception
 *
 * The need for this exception arises from the action of
 * attach_task(), which overwrites one tasks cgroup pointer with
 * another.  It does so using cgroup_mutexe, however there are
 * several performance critical places that need to reference
 * task->cgroup without the expense of grabbing a system global
 * mutex.  Therefore except as noted below, when dereferencing or, as
 * in attach_task(), modifying a task'ss cgroup pointer we use
 * task_lock(), which acts on a spinlock (task->alloc_lock) already in
 * the task_struct routinely used for such matters.
 *
 * P.S.  One more locking exception.  RCU is used to guard the
 * update of a tasks cgroup pointer by attach_task()
 */

static DEFINE_MUTEX(cgroup_mutex);

/**
 * cgroup_lock - lock out any changes to cgroup structures
 *
 */

void cgroup_lock(void)
{
	mutex_lock(&cgroup_mutex);
}

/**
 * cgroup_unlock - release lock on cgroup changes
 *
 * Undo the lock taken in a previous cgroup_lock() call.
 */

void cgroup_unlock(void)
{
	mutex_unlock(&cgroup_mutex);
}

/*
 * A couple of forward declarations required, due to cyclic reference loop:
 * cgroup_mkdir -> cgroup_create -> cgroup_populate_dir ->
 * cgroup_add_file -> cgroup_create_file -> cgroup_dir_inode_operations
 * -> cgroup_mkdir.
 */

static int cgroup_mkdir(struct inode *dir, struct dentry *dentry, int mode);
static int cgroup_rmdir(struct inode *unused_dir, struct dentry *dentry);
static int cgroup_populate_dir(struct cgroup *cont);
static struct inode_operations cgroup_dir_inode_operations;

static struct inode *cgroup_new_inode(mode_t mode, struct super_block *sb)
{
	struct inode *inode = new_inode(sb);
	static struct backing_dev_info cgroup_backing_dev_info = {
		.capabilities	= BDI_CAP_NO_ACCT_DIRTY | BDI_CAP_NO_WRITEBACK,
	};

	if (inode) {
		inode->i_mode = mode;
		inode->i_uid = current->fsuid;
		inode->i_gid = current->fsgid;
		inode->i_blocks = 0;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		inode->i_mapping->backing_dev_info = &cgroup_backing_dev_info;
	}
	return inode;
}

static void cgroup_diput(struct dentry *dentry, struct inode *inode)
{
	/* is dentry a directory ? if so, kfree() associated cgroup */
	if (S_ISDIR(inode->i_mode)) {
		struct cgroup *cont = dentry->d_fsdata;
		BUG_ON(!(cgroup_is_removed(cont)));
		kfree(cont);
	}
	iput(inode);
}

static void remove_dir(struct dentry *d)
{
	struct dentry *parent = dget(d->d_parent);

	d_delete(d);
	simple_rmdir(parent->d_inode, d);
	dput(parent);
}

static void cgroup_clear_directory(struct dentry *dentry)
{
	struct list_head *node;

	BUG_ON(!mutex_is_locked(&dentry->d_inode->i_mutex));
	spin_lock(&dcache_lock);
	node = dentry->d_subdirs.next;
	while (node != &dentry->d_subdirs) {
		struct dentry *d = list_entry(node, struct dentry, d_u.d_child);
		list_del_init(node);
		if (d->d_inode) {
			/* This should never be called on a cgroup
			 * directory with child cgroups */
			BUG_ON(d->d_inode->i_mode & S_IFDIR);
			d = dget_locked(d);
			spin_unlock(&dcache_lock);
			d_delete(d);
			simple_unlink(dentry->d_inode, d);
			dput(d);
			spin_lock(&dcache_lock);
		}
		node = dentry->d_subdirs.next;
	}
	spin_unlock(&dcache_lock);
}

/*
 * NOTE : the dentry must have been dget()'ed
 */
static void cgroup_d_remove_dir(struct dentry *dentry)
{
	cgroup_clear_directory(dentry);

	spin_lock(&dcache_lock);
	list_del_init(&dentry->d_u.d_child);
	spin_unlock(&dcache_lock);
	remove_dir(dentry);
}

static int rebind_subsystems(struct cgroupfs_root *root,
			      unsigned long final_bits)
{
	unsigned long added_bits, removed_bits;
	struct cgroup *cont = &root->top_cgroup;
	int i;

	removed_bits = root->actual_subsys_bits & ~final_bits;
	added_bits = final_bits & ~root->actual_subsys_bits;
	/* Check that any added subsystems are currently free */
	for (i = 0; i < CGROUP_SUBSYS_COUNT; i++) {
		unsigned long long bit = 1ull << i;
		struct cgroup_subsys *ss = subsys[i];
		if (!(bit & added_bits))
			continue;
		if (ss->root != &rootnode) {
			/* Subsystem isn't free */
			return -EBUSY;
		}
	}

	/* Currently we don't handle adding/removing subsystems when
	 * any child cgroups exist. This is theoretically supportable
	 * but involves complex error handling, so it's being left until
	 * later */
	if (!list_empty(&cont->children))
		return -EBUSY;

	/* Process each subsystem */
	for (i = 0; i < CGROUP_SUBSYS_COUNT; i++) {
		struct cgroup_subsys *ss = subsys[i];
		unsigned long bit = 1UL << i;
		if (bit & added_bits) {
			/* We're binding this subsystem to this hierarchy */
			BUG_ON(cont->subsys[i]);
			BUG_ON(!dummytop->subsys[i]);
			BUG_ON(dummytop->subsys[i]->cgroup != dummytop);
			cont->subsys[i] = dummytop->subsys[i];
			cont->subsys[i]->cgroup = cont;
			list_add(&ss->sibling, &root->subsys_list);
			rcu_assign_pointer(ss->root, root);
			if (ss->bind)
				ss->bind(ss, cont);

		} else if (bit & removed_bits) {
			/* We're removing this subsystem */
			BUG_ON(cont->subsys[i] != dummytop->subsys[i]);
			BUG_ON(cont->subsys[i]->cgroup != cont);
			if (ss->bind)
				ss->bind(ss, dummytop);
			dummytop->subsys[i]->cgroup = dummytop;
			cont->subsys[i] = NULL;
			rcu_assign_pointer(subsys[i]->root, &rootnode);
			list_del(&ss->sibling);
		} else if (bit & final_bits) {
			/* Subsystem state should already exist */
			BUG_ON(!cont->subsys[i]);
		} else {
			/* Subsystem state shouldn't exist */
			BUG_ON(cont->subsys[i]);
		}
	}
	root->subsys_bits = root->actual_subsys_bits = final_bits;
	synchronize_rcu();

	return 0;
}

static int cgroup_show_options(struct seq_file *seq, struct vfsmount *vfs)
{
	struct cgroupfs_root *root = vfs->mnt_sb->s_fs_info;
	struct cgroup_subsys *ss;

	mutex_lock(&cgroup_mutex);
	for_each_subsys(root, ss)
		seq_printf(seq, ",%s", ss->name);
	if (test_bit(ROOT_NOPREFIX, &root->flags))
		seq_puts(seq, ",noprefix");
	mutex_unlock(&cgroup_mutex);
	return 0;
}

struct cgroup_sb_opts {
	unsigned long subsys_bits;
	unsigned long flags;
};

/* Convert a hierarchy specifier into a bitmask of subsystems and
 * flags. */
static int parse_cgroupfs_options(char *data,
				     struct cgroup_sb_opts *opts)
{
	char *token, *o = data ?: "all";

	opts->subsys_bits = 0;
	opts->flags = 0;

	while ((token = strsep(&o, ",")) != NULL) {
		if (!*token)
			return -EINVAL;
		if (!strcmp(token, "all")) {
			opts->subsys_bits = (1 << CGROUP_SUBSYS_COUNT) - 1;
		} else if (!strcmp(token, "noprefix")) {
			set_bit(ROOT_NOPREFIX, &opts->flags);
		} else {
			struct cgroup_subsys *ss;
			int i;
			for (i = 0; i < CGROUP_SUBSYS_COUNT; i++) {
				ss = subsys[i];
				if (!strcmp(token, ss->name)) {
					set_bit(i, &opts->subsys_bits);
					break;
				}
			}
			if (i == CGROUP_SUBSYS_COUNT)
				return -ENOENT;
		}
	}

	/* We can't have an empty hierarchy */
	if (!opts->subsys_bits)
		return -EINVAL;

	return 0;
}

static int cgroup_remount(struct super_block *sb, int *flags, char *data)
{
	int ret = 0;
	struct cgroupfs_root *root = sb->s_fs_info;
	struct cgroup *cont = &root->top_cgroup;
	struct cgroup_sb_opts opts;

	mutex_lock(&cont->dentry->d_inode->i_mutex);
	mutex_lock(&cgroup_mutex);

	/* See what subsystems are wanted */
	ret = parse_cgroupfs_options(data, &opts);
	if (ret)
		goto out_unlock;

	/* Don't allow flags to change at remount */
	if (opts.flags != root->flags) {
		ret = -EINVAL;
		goto out_unlock;
	}

	ret = rebind_subsystems(root, opts.subsys_bits);

	/* (re)populate subsystem files */
	if (!ret)
		cgroup_populate_dir(cont);

 out_unlock:
	mutex_unlock(&cgroup_mutex);
	mutex_unlock(&cont->dentry->d_inode->i_mutex);
	return ret;
}

static struct super_operations cgroup_ops = {
	.statfs = simple_statfs,
	.drop_inode = generic_delete_inode,
	.show_options = cgroup_show_options,
	.remount_fs = cgroup_remount,
};

static void init_cgroup_root(struct cgroupfs_root *root)
{
	struct cgroup *cont = &root->top_cgroup;
	INIT_LIST_HEAD(&root->subsys_list);
	INIT_LIST_HEAD(&root->root_list);
	root->number_of_cgroups = 1;
	cont->root = root;
	cont->top_cgroup = cont;
	INIT_LIST_HEAD(&cont->sibling);
	INIT_LIST_HEAD(&cont->children);
}

static int cgroup_test_super(struct super_block *sb, void *data)
{
	struct cgroupfs_root *new = data;
	struct cgroupfs_root *root = sb->s_fs_info;

	/* First check subsystems */
	if (new->subsys_bits != root->subsys_bits)
	    return 0;

	/* Next check flags */
	if (new->flags != root->flags)
		return 0;

	return 1;
}

static int cgroup_set_super(struct super_block *sb, void *data)
{
	int ret;
	struct cgroupfs_root *root = data;

	ret = set_anon_super(sb, NULL);
	if (ret)
		return ret;

	sb->s_fs_info = root;
	root->sb = sb;

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = CGROUP_SUPER_MAGIC;
	sb->s_op = &cgroup_ops;

	return 0;
}

static int cgroup_get_rootdir(struct super_block *sb)
{
	struct inode *inode =
		cgroup_new_inode(S_IFDIR | S_IRUGO | S_IXUGO | S_IWUSR, sb);
	struct dentry *dentry;

	if (!inode)
		return -ENOMEM;

	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;
	inode->i_op = &cgroup_dir_inode_operations;
	/* directories start off with i_nlink == 2 (for "." entry) */
	inc_nlink(inode);
	dentry = d_alloc_root(inode);
	if (!dentry) {
		iput(inode);
		return -ENOMEM;
	}
	sb->s_root = dentry;
	return 0;
}

static int cgroup_get_sb(struct file_system_type *fs_type,
			 int flags, const char *unused_dev_name,
			 void *data, struct vfsmount *mnt)
{
	struct cgroup_sb_opts opts;
	int ret = 0;
	struct super_block *sb;
	struct cgroupfs_root *root;

	/* First find the desired set of subsystems */
	ret = parse_cgroupfs_options(data, &opts);
	if (ret)
		return ret;

	root = kzalloc(sizeof(*root), GFP_KERNEL);
	if (!root)
		return -ENOMEM;

	init_cgroup_root(root);
	root->subsys_bits = opts.subsys_bits;
	root->flags = opts.flags;

	sb = sget(fs_type, cgroup_test_super, cgroup_set_super, root);

	if (IS_ERR(sb)) {
		kfree(root);
		return PTR_ERR(sb);
	}

	if (sb->s_fs_info != root) {
		/* Reusing an existing superblock */
		BUG_ON(sb->s_root == NULL);
		kfree(root);
		root = NULL;
	} else {
		/* New superblock */
		struct cgroup *cont = &root->top_cgroup;

		BUG_ON(sb->s_root != NULL);

		ret = cgroup_get_rootdir(sb);
		if (ret)
			goto drop_new_super;

		mutex_lock(&cgroup_mutex);

		ret = rebind_subsystems(root, root->subsys_bits);
		if (ret == -EBUSY) {
			mutex_unlock(&cgroup_mutex);
			goto drop_new_super;
		}

		/* EBUSY should be the only error here */
		BUG_ON(ret);

		list_add(&root->root_list, &roots);

		sb->s_root->d_fsdata = &root->top_cgroup;
		root->top_cgroup.dentry = sb->s_root;

		BUG_ON(!list_empty(&cont->sibling));
		BUG_ON(!list_empty(&cont->children));
		BUG_ON(root->number_of_cgroups != 1);

		/*
		 * I believe that it's safe to nest i_mutex inside
		 * cgroup_mutex in this case, since no-one else can
		 * be accessing this directory yet. But we still need
		 * to teach lockdep that this is the case - currently
		 * a cgroupfs remount triggers a lockdep warning
		 */
		mutex_lock(&cont->dentry->d_inode->i_mutex);
		cgroup_populate_dir(cont);
		mutex_unlock(&cont->dentry->d_inode->i_mutex);
		mutex_unlock(&cgroup_mutex);
	}

	return simple_set_mnt(mnt, sb);

 drop_new_super:
	up_write(&sb->s_umount);
	deactivate_super(sb);
	return ret;
}

static void cgroup_kill_sb(struct super_block *sb) {
	struct cgroupfs_root *root = sb->s_fs_info;
	struct cgroup *cont = &root->top_cgroup;
	int ret;

	BUG_ON(!root);

	BUG_ON(root->number_of_cgroups != 1);
	BUG_ON(!list_empty(&cont->children));
	BUG_ON(!list_empty(&cont->sibling));

	mutex_lock(&cgroup_mutex);

	/* Rebind all subsystems back to the default hierarchy */
	ret = rebind_subsystems(root, 0);
	/* Shouldn't be able to fail ... */
	BUG_ON(ret);

	if (!list_empty(&root->root_list))
		list_del(&root->root_list);
	mutex_unlock(&cgroup_mutex);

	kfree(root);
	kill_litter_super(sb);
}

static struct file_system_type cgroup_fs_type = {
	.name = "cgroup",
	.get_sb = cgroup_get_sb,
	.kill_sb = cgroup_kill_sb,
};

static inline struct cgroup *__d_cont(struct dentry *dentry)
{
	return dentry->d_fsdata;
}

static inline struct cftype *__d_cft(struct dentry *dentry)
{
	return dentry->d_fsdata;
}

/*
 * Called with cgroup_mutex held.  Writes path of cgroup into buf.
 * Returns 0 on success, -errno on error.
 */
int cgroup_path(const struct cgroup *cont, char *buf, int buflen)
{
	char *start;

	if (cont == dummytop) {
		/*
		 * Inactive subsystems have no dentry for their root
		 * cgroup
		 */
		strcpy(buf, "/");
		return 0;
	}

	start = buf + buflen;

	*--start = '\0';
	for (;;) {
		int len = cont->dentry->d_name.len;
		if ((start -= len) < buf)
			return -ENAMETOOLONG;
		memcpy(start, cont->dentry->d_name.name, len);
		cont = cont->parent;
		if (!cont)
			break;
		if (!cont->parent)
			continue;
		if (--start < buf)
			return -ENAMETOOLONG;
		*start = '/';
	}
	memmove(buf, start, buf + buflen - start);
	return 0;
}

/*
 * Return the first subsystem attached to a cgroup's hierarchy, and
 * its subsystem id.
 */

static void get_first_subsys(const struct cgroup *cont,
			struct cgroup_subsys_state **css, int *subsys_id)
{
	const struct cgroupfs_root *root = cont->root;
	const struct cgroup_subsys *test_ss;
	BUG_ON(list_empty(&root->subsys_list));
	test_ss = list_entry(root->subsys_list.next,
			     struct cgroup_subsys, sibling);
	if (css) {
		*css = cont->subsys[test_ss->subsys_id];
		BUG_ON(!*css);
	}
	if (subsys_id)
		*subsys_id = test_ss->subsys_id;
}

/*
 * Attach task 'tsk' to cgroup 'cont'
 *
 * Call holding cgroup_mutex.  May take task_lock of
 * the task 'pid' during call.
 */
static int attach_task(struct cgroup *cont, struct task_struct *tsk)
{
	int retval = 0;
	struct cgroup_subsys *ss;
	struct cgroup *oldcont;
	struct css_set *cg = &tsk->cgroups;
	struct cgroupfs_root *root = cont->root;
	int i;
	int subsys_id;

	get_first_subsys(cont, NULL, &subsys_id);

	/* Nothing to do if the task is already in that cgroup */
	oldcont = task_cgroup(tsk, subsys_id);
	if (cont == oldcont)
		return 0;

	for_each_subsys(root, ss) {
		if (ss->can_attach) {
			retval = ss->can_attach(ss, cont, tsk);
			if (retval) {
				return retval;
			}
		}
	}

	task_lock(tsk);
	if (tsk->flags & PF_EXITING) {
		task_unlock(tsk);
		return -ESRCH;
	}
	/* Update the css_set pointers for the subsystems in this
	 * hierarchy */
	for (i = 0; i < CGROUP_SUBSYS_COUNT; i++) {
		if (root->subsys_bits & (1ull << i)) {
			/* Subsystem is in this hierarchy. So we want
			 * the subsystem state from the new
			 * cgroup. Transfer the refcount from the
			 * old to the new */
			atomic_inc(&cont->count);
			atomic_dec(&cg->subsys[i]->cgroup->count);
			rcu_assign_pointer(cg->subsys[i], cont->subsys[i]);
		}
	}
	task_unlock(tsk);

	for_each_subsys(root, ss) {
		if (ss->attach) {
			ss->attach(ss, cont, oldcont, tsk);
		}
	}

	synchronize_rcu();
	return 0;
}

/*
 * Attach task with pid 'pid' to cgroup 'cont'. Call with
 * cgroup_mutex, may take task_lock of task
 */
static int attach_task_by_pid(struct cgroup *cont, char *pidbuf)
{
	pid_t pid;
	struct task_struct *tsk;
	int ret;

	if (sscanf(pidbuf, "%d", &pid) != 1)
		return -EIO;

	if (pid) {
		rcu_read_lock();
		tsk = find_task_by_pid(pid);
		if (!tsk || tsk->flags & PF_EXITING) {
			rcu_read_unlock();
			return -ESRCH;
		}
		get_task_struct(tsk);
		rcu_read_unlock();

		if ((current->euid) && (current->euid != tsk->uid)
		    && (current->euid != tsk->suid)) {
			put_task_struct(tsk);
			return -EACCES;
		}
	} else {
		tsk = current;
		get_task_struct(tsk);
	}

	ret = attach_task(cont, tsk);
	put_task_struct(tsk);
	return ret;
}

/* The various types of files and directories in a cgroup file system */

enum cgroup_filetype {
	FILE_ROOT,
	FILE_DIR,
	FILE_TASKLIST,
};

static ssize_t cgroup_write_uint(struct cgroup *cont, struct cftype *cft,
				 struct file *file,
				 const char __user *userbuf,
				 size_t nbytes, loff_t *unused_ppos)
{
	char buffer[64];
	int retval = 0;
	u64 val;
	char *end;

	if (!nbytes)
		return -EINVAL;
	if (nbytes >= sizeof(buffer))
		return -E2BIG;
	if (copy_from_user(buffer, userbuf, nbytes))
		return -EFAULT;

	buffer[nbytes] = 0;     /* nul-terminate */

	/* strip newline if necessary */
	if (nbytes && (buffer[nbytes-1] == '\n'))
		buffer[nbytes-1] = 0;
	val = simple_strtoull(buffer, &end, 0);
	if (*end)
		return -EINVAL;

	/* Pass to subsystem */
	retval = cft->write_uint(cont, cft, val);
	if (!retval)
		retval = nbytes;
	return retval;
}

static ssize_t cgroup_common_file_write(struct cgroup *cont,
					   struct cftype *cft,
					   struct file *file,
					   const char __user *userbuf,
					   size_t nbytes, loff_t *unused_ppos)
{
	enum cgroup_filetype type = cft->private;
	char *buffer;
	int retval = 0;

	if (nbytes >= PATH_MAX)
		return -E2BIG;

	/* +1 for nul-terminator */
	buffer = kmalloc(nbytes + 1, GFP_KERNEL);
	if (buffer == NULL)
		return -ENOMEM;

	if (copy_from_user(buffer, userbuf, nbytes)) {
		retval = -EFAULT;
		goto out1;
	}
	buffer[nbytes] = 0;	/* nul-terminate */

	mutex_lock(&cgroup_mutex);

	if (cgroup_is_removed(cont)) {
		retval = -ENODEV;
		goto out2;
	}

	switch (type) {
	case FILE_TASKLIST:
		retval = attach_task_by_pid(cont, buffer);
		break;
	default:
		retval = -EINVAL;
		goto out2;
	}

	if (retval == 0)
		retval = nbytes;
out2:
	mutex_unlock(&cgroup_mutex);
out1:
	kfree(buffer);
	return retval;
}

static ssize_t cgroup_file_write(struct file *file, const char __user *buf,
						size_t nbytes, loff_t *ppos)
{
	struct cftype *cft = __d_cft(file->f_dentry);
	struct cgroup *cont = __d_cont(file->f_dentry->d_parent);

	if (!cft)
		return -ENODEV;
	if (cft->write)
		return cft->write(cont, cft, file, buf, nbytes, ppos);
	if (cft->write_uint)
		return cgroup_write_uint(cont, cft, file, buf, nbytes, ppos);
	return -EINVAL;
}

static ssize_t cgroup_read_uint(struct cgroup *cont, struct cftype *cft,
				   struct file *file,
				   char __user *buf, size_t nbytes,
				   loff_t *ppos)
{
	char tmp[64];
	u64 val = cft->read_uint(cont, cft);
	int len = sprintf(tmp, "%llu\n", (unsigned long long) val);

	return simple_read_from_buffer(buf, nbytes, ppos, tmp, len);
}

static ssize_t cgroup_file_read(struct file *file, char __user *buf,
				   size_t nbytes, loff_t *ppos)
{
	struct cftype *cft = __d_cft(file->f_dentry);
	struct cgroup *cont = __d_cont(file->f_dentry->d_parent);

	if (!cft)
		return -ENODEV;

	if (cft->read)
		return cft->read(cont, cft, file, buf, nbytes, ppos);
	if (cft->read_uint)
		return cgroup_read_uint(cont, cft, file, buf, nbytes, ppos);
	return -EINVAL;
}

static int cgroup_file_open(struct inode *inode, struct file *file)
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

static int cgroup_file_release(struct inode *inode, struct file *file)
{
	struct cftype *cft = __d_cft(file->f_dentry);
	if (cft->release)
		return cft->release(inode, file);
	return 0;
}

/*
 * cgroup_rename - Only allow simple rename of directories in place.
 */
static int cgroup_rename(struct inode *old_dir, struct dentry *old_dentry,
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

static struct file_operations cgroup_file_operations = {
	.read = cgroup_file_read,
	.write = cgroup_file_write,
	.llseek = generic_file_llseek,
	.open = cgroup_file_open,
	.release = cgroup_file_release,
};

static struct inode_operations cgroup_dir_inode_operations = {
	.lookup = simple_lookup,
	.mkdir = cgroup_mkdir,
	.rmdir = cgroup_rmdir,
	.rename = cgroup_rename,
};

static int cgroup_create_file(struct dentry *dentry, int mode,
				struct super_block *sb)
{
	static struct dentry_operations cgroup_dops = {
		.d_iput = cgroup_diput,
	};

	struct inode *inode;

	if (!dentry)
		return -ENOENT;
	if (dentry->d_inode)
		return -EEXIST;

	inode = cgroup_new_inode(mode, sb);
	if (!inode)
		return -ENOMEM;

	if (S_ISDIR(mode)) {
		inode->i_op = &cgroup_dir_inode_operations;
		inode->i_fop = &simple_dir_operations;

		/* start off with i_nlink == 2 (for "." entry) */
		inc_nlink(inode);

		/* start with the directory inode held, so that we can
		 * populate it without racing with another mkdir */
		mutex_lock(&inode->i_mutex);
	} else if (S_ISREG(mode)) {
		inode->i_size = 0;
		inode->i_fop = &cgroup_file_operations;
	}
	dentry->d_op = &cgroup_dops;
	d_instantiate(dentry, inode);
	dget(dentry);	/* Extra count - pin the dentry in core */
	return 0;
}

/*
 *	cgroup_create_dir - create a directory for an object.
 *	cont:	the cgroup we create the directory for.
 *		It must have a valid ->parent field
 *		And we are going to fill its ->dentry field.
 *	dentry: dentry of the new container
 *	mode:	mode to set on new directory.
 */
static int cgroup_create_dir(struct cgroup *cont, struct dentry *dentry,
				int mode)
{
	struct dentry *parent;
	int error = 0;

	parent = cont->parent->dentry;
	error = cgroup_create_file(dentry, S_IFDIR | mode, cont->root->sb);
	if (!error) {
		dentry->d_fsdata = cont;
		inc_nlink(parent->d_inode);
		cont->dentry = dentry;
		dget(dentry);
	}
	dput(dentry);

	return error;
}

int cgroup_add_file(struct cgroup *cont,
		       struct cgroup_subsys *subsys,
		       const struct cftype *cft)
{
	struct dentry *dir = cont->dentry;
	struct dentry *dentry;
	int error;

	char name[MAX_CGROUP_TYPE_NAMELEN + MAX_CFTYPE_NAME + 2] = { 0 };
	if (subsys && !test_bit(ROOT_NOPREFIX, &cont->root->flags)) {
		strcpy(name, subsys->name);
		strcat(name, ".");
	}
	strcat(name, cft->name);
	BUG_ON(!mutex_is_locked(&dir->d_inode->i_mutex));
	dentry = lookup_one_len(name, dir, strlen(name));
	if (!IS_ERR(dentry)) {
		error = cgroup_create_file(dentry, 0644 | S_IFREG,
						cont->root->sb);
		if (!error)
			dentry->d_fsdata = (void *)cft;
		dput(dentry);
	} else
		error = PTR_ERR(dentry);
	return error;
}

int cgroup_add_files(struct cgroup *cont,
			struct cgroup_subsys *subsys,
			const struct cftype cft[],
			int count)
{
	int i, err;
	for (i = 0; i < count; i++) {
		err = cgroup_add_file(cont, subsys, &cft[i]);
		if (err)
			return err;
	}
	return 0;
}

/* Count the number of tasks in a cgroup. Could be made more
 * time-efficient but less space-efficient with more linked lists
 * running through each cgroup and the css_set structures that
 * referenced it. Must be called with tasklist_lock held for read or
 * write or in an rcu critical section.
 */
int __cgroup_task_count(const struct cgroup *cont)
{
	int count = 0;
	struct task_struct *g, *p;
	struct cgroup_subsys_state *css;
	int subsys_id;

	get_first_subsys(cont, &css, &subsys_id);
	do_each_thread(g, p) {
		if (task_subsys_state(p, subsys_id) == css)
			count ++;
	} while_each_thread(g, p);
	return count;
}

/*
 * Stuff for reading the 'tasks' file.
 *
 * Reading this file can return large amounts of data if a cgroup has
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
struct ctr_struct {
	char *buf;
	int bufsz;
};

/*
 * Load into 'pidarray' up to 'npids' of the tasks using cgroup
 * 'cont'.  Return actual number of pids loaded.  No need to
 * task_lock(p) when reading out p->cgroup, since we're in an RCU
 * read section, so the css_set can't go away, and is
 * immutable after creation.
 */
static int pid_array_load(pid_t *pidarray, int npids, struct cgroup *cont)
{
	int n = 0;
	struct task_struct *g, *p;
	struct cgroup_subsys_state *css;
	int subsys_id;

	get_first_subsys(cont, &css, &subsys_id);
	rcu_read_lock();
	do_each_thread(g, p) {
		if (task_subsys_state(p, subsys_id) == css) {
			pidarray[n++] = pid_nr(task_pid(p));
			if (unlikely(n == npids))
				goto array_full;
		}
	} while_each_thread(g, p);

array_full:
	rcu_read_unlock();
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
 * process id's of tasks currently attached to the cgroup being opened.
 *
 * Does not require any specific cgroup mutexes, and does not take any.
 */
static int cgroup_tasks_open(struct inode *unused, struct file *file)
{
	struct cgroup *cont = __d_cont(file->f_dentry->d_parent);
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
	 * If cgroup gets more users after we read count, we won't have
	 * enough space - tough.  This race is indistinguishable to the
	 * caller from the case that the additional cgroup users didn't
	 * show up until sometime later on.
	 */
	npids = cgroup_task_count(cont);
	if (npids) {
		pidarray = kmalloc(npids * sizeof(pid_t), GFP_KERNEL);
		if (!pidarray)
			goto err1;

		npids = pid_array_load(pidarray, npids, cont);
		sort(pidarray, npids, sizeof(pid_t), cmppid, NULL);

		/* Call pid_array_to_buf() twice, first just to get bufsz */
		ctr->bufsz = pid_array_to_buf(&c, sizeof(c), pidarray, npids) + 1;
		ctr->buf = kmalloc(ctr->bufsz, GFP_KERNEL);
		if (!ctr->buf)
			goto err2;
		ctr->bufsz = pid_array_to_buf(ctr->buf, ctr->bufsz, pidarray, npids);

		kfree(pidarray);
	} else {
		ctr->buf = 0;
		ctr->bufsz = 0;
	}
	file->private_data = ctr;
	return 0;

err2:
	kfree(pidarray);
err1:
	kfree(ctr);
err0:
	return -ENOMEM;
}

static ssize_t cgroup_tasks_read(struct cgroup *cont,
				    struct cftype *cft,
				    struct file *file, char __user *buf,
				    size_t nbytes, loff_t *ppos)
{
	struct ctr_struct *ctr = file->private_data;

	return simple_read_from_buffer(buf, nbytes, ppos, ctr->buf, ctr->bufsz);
}

static int cgroup_tasks_release(struct inode *unused_inode,
					struct file *file)
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
	.open = cgroup_tasks_open,
	.read = cgroup_tasks_read,
	.write = cgroup_common_file_write,
	.release = cgroup_tasks_release,
	.private = FILE_TASKLIST,
};

static int cgroup_populate_dir(struct cgroup *cont)
{
	int err;
	struct cgroup_subsys *ss;

	/* First clear out any existing files */
	cgroup_clear_directory(cont->dentry);

	err = cgroup_add_file(cont, NULL, &cft_tasks);
	if (err < 0)
		return err;

	for_each_subsys(cont->root, ss) {
		if (ss->populate && (err = ss->populate(ss, cont)) < 0)
			return err;
	}

	return 0;
}

static void init_cgroup_css(struct cgroup_subsys_state *css,
			       struct cgroup_subsys *ss,
			       struct cgroup *cont)
{
	css->cgroup = cont;
	atomic_set(&css->refcnt, 0);
	css->flags = 0;
	if (cont == dummytop)
		set_bit(CSS_ROOT, &css->flags);
	BUG_ON(cont->subsys[ss->subsys_id]);
	cont->subsys[ss->subsys_id] = css;
}

/*
 *	cgroup_create - create a cgroup
 *	parent:	cgroup that will be parent of the new cgroup.
 *	name:		name of the new cgroup. Will be strcpy'ed.
 *	mode:		mode to set on new inode
 *
 *	Must be called with the mutex on the parent inode held
 */

static long cgroup_create(struct cgroup *parent, struct dentry *dentry,
			     int mode)
{
	struct cgroup *cont;
	struct cgroupfs_root *root = parent->root;
	int err = 0;
	struct cgroup_subsys *ss;
	struct super_block *sb = root->sb;

	cont = kzalloc(sizeof(*cont), GFP_KERNEL);
	if (!cont)
		return -ENOMEM;

	/* Grab a reference on the superblock so the hierarchy doesn't
	 * get deleted on unmount if there are child cgroups.  This
	 * can be done outside cgroup_mutex, since the sb can't
	 * disappear while someone has an open control file on the
	 * fs */
	atomic_inc(&sb->s_active);

	mutex_lock(&cgroup_mutex);

	cont->flags = 0;
	INIT_LIST_HEAD(&cont->sibling);
	INIT_LIST_HEAD(&cont->children);

	cont->parent = parent;
	cont->root = parent->root;
	cont->top_cgroup = parent->top_cgroup;

	for_each_subsys(root, ss) {
		struct cgroup_subsys_state *css = ss->create(ss, cont);
		if (IS_ERR(css)) {
			err = PTR_ERR(css);
			goto err_destroy;
		}
		init_cgroup_css(css, ss, cont);
	}

	list_add(&cont->sibling, &cont->parent->children);
	root->number_of_cgroups++;

	err = cgroup_create_dir(cont, dentry, mode);
	if (err < 0)
		goto err_remove;

	/* The cgroup directory was pre-locked for us */
	BUG_ON(!mutex_is_locked(&cont->dentry->d_inode->i_mutex));

	err = cgroup_populate_dir(cont);
	/* If err < 0, we have a half-filled directory - oh well ;) */

	mutex_unlock(&cgroup_mutex);
	mutex_unlock(&cont->dentry->d_inode->i_mutex);

	return 0;

 err_remove:

	list_del(&cont->sibling);
	root->number_of_cgroups--;

 err_destroy:

	for_each_subsys(root, ss) {
		if (cont->subsys[ss->subsys_id])
			ss->destroy(ss, cont);
	}

	mutex_unlock(&cgroup_mutex);

	/* Release the reference count that we took on the superblock */
	deactivate_super(sb);

	kfree(cont);
	return err;
}

static int cgroup_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	struct cgroup *c_parent = dentry->d_parent->d_fsdata;

	/* the vfs holds inode->i_mutex already */
	return cgroup_create(c_parent, dentry, mode | S_IFDIR);
}

static int cgroup_rmdir(struct inode *unused_dir, struct dentry *dentry)
{
	struct cgroup *cont = dentry->d_fsdata;
	struct dentry *d;
	struct cgroup *parent;
	struct cgroup_subsys *ss;
	struct super_block *sb;
	struct cgroupfs_root *root;
	int css_busy = 0;

	/* the vfs holds both inode->i_mutex already */

	mutex_lock(&cgroup_mutex);
	if (atomic_read(&cont->count) != 0) {
		mutex_unlock(&cgroup_mutex);
		return -EBUSY;
	}
	if (!list_empty(&cont->children)) {
		mutex_unlock(&cgroup_mutex);
		return -EBUSY;
	}

	parent = cont->parent;
	root = cont->root;
	sb = root->sb;

	/* Check the reference count on each subsystem. Since we
	 * already established that there are no tasks in the
	 * cgroup, if the css refcount is also 0, then there should
	 * be no outstanding references, so the subsystem is safe to
	 * destroy */
	for_each_subsys(root, ss) {
		struct cgroup_subsys_state *css;
		css = cont->subsys[ss->subsys_id];
		if (atomic_read(&css->refcnt)) {
			css_busy = 1;
			break;
		}
	}
	if (css_busy) {
		mutex_unlock(&cgroup_mutex);
		return -EBUSY;
	}

	for_each_subsys(root, ss) {
		if (cont->subsys[ss->subsys_id])
			ss->destroy(ss, cont);
	}

	set_bit(CONT_REMOVED, &cont->flags);
	/* delete my sibling from parent->children */
	list_del(&cont->sibling);
	spin_lock(&cont->dentry->d_lock);
	d = dget(cont->dentry);
	cont->dentry = NULL;
	spin_unlock(&d->d_lock);

	cgroup_d_remove_dir(d);
	dput(d);
	root->number_of_cgroups--;

	mutex_unlock(&cgroup_mutex);
	/* Drop the active superblock reference that we took when we
	 * created the cgroup */
	deactivate_super(sb);
	return 0;
}

static void cgroup_init_subsys(struct cgroup_subsys *ss)
{
	struct task_struct *g, *p;
	struct cgroup_subsys_state *css;
	printk(KERN_ERR "Initializing cgroup subsys %s\n", ss->name);

	/* Create the top cgroup state for this subsystem */
	ss->root = &rootnode;
	css = ss->create(ss, dummytop);
	/* We don't handle early failures gracefully */
	BUG_ON(IS_ERR(css));
	init_cgroup_css(css, ss, dummytop);

	/* Update all tasks to contain a subsys pointer to this state
	 * - since the subsystem is newly registered, all tasks are in
	 * the subsystem's top cgroup. */

 	/* If this subsystem requested that it be notified with fork
 	 * events, we should send it one now for every process in the
 	 * system */

	read_lock(&tasklist_lock);
	init_task.cgroups.subsys[ss->subsys_id] = css;
	if (ss->fork)
		ss->fork(ss, &init_task);

	do_each_thread(g, p) {
		printk(KERN_INFO "Setting task %p css to %p (%d)\n", css, p, p->pid);
		p->cgroups.subsys[ss->subsys_id] = css;
		if (ss->fork)
			ss->fork(ss, p);
	} while_each_thread(g, p);
	read_unlock(&tasklist_lock);

	need_forkexit_callback |= ss->fork || ss->exit;

	ss->active = 1;
}

/**
 * cgroup_init_early - initialize cgroups at system boot, and
 * initialize any subsystems that request early init.
 */
int __init cgroup_init_early(void)
{
	int i;
	init_cgroup_root(&rootnode);
	list_add(&rootnode.root_list, &roots);

	for (i = 0; i < CGROUP_SUBSYS_COUNT; i++) {
		struct cgroup_subsys *ss = subsys[i];

		BUG_ON(!ss->name);
		BUG_ON(strlen(ss->name) > MAX_CGROUP_TYPE_NAMELEN);
		BUG_ON(!ss->create);
		BUG_ON(!ss->destroy);
		if (ss->subsys_id != i) {
			printk(KERN_ERR "Subsys %s id == %d\n",
			       ss->name, ss->subsys_id);
			BUG();
		}

		if (ss->early_init)
			cgroup_init_subsys(ss);
	}
	return 0;
}

/**
 * cgroup_init - register cgroup filesystem and /proc file, and
 * initialize any subsystems that didn't request early init.
 */
int __init cgroup_init(void)
{
	int err;
	int i;

	for (i = 0; i < CGROUP_SUBSYS_COUNT; i++) {
		struct cgroup_subsys *ss = subsys[i];
		if (!ss->early_init)
			cgroup_init_subsys(ss);
	}

	err = register_filesystem(&cgroup_fs_type);
	if (err < 0)
		goto out;

out:
	return err;
}
