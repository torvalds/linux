/*
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
#include <linux/proc_fs.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/backing-dev.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/magic.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/sort.h>
#include <linux/kmod.h>
#include <linux/delayacct.h>
#include <linux/cgroupstats.h>
#include <linux/hash.h>

#include <asm/atomic.h>

static DEFINE_MUTEX(cgroup_mutex);

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

	/* The path to use for release notifications. No locking
	 * between setting and use - so if userspace updates this
	 * while child cgroups exist, you could miss a
	 * notification. We ensure that it's always a valid
	 * NUL-terminated string */
	char release_agent_path[PATH_MAX];
};


/*
 * The "rootnode" hierarchy is the "dummy hierarchy", reserved for the
 * subsystems that are otherwise unattached - it never has more than a
 * single cgroup, and all tasks are part of that cgroup.
 */
static struct cgroupfs_root rootnode;

/* The list of hierarchy roots */

static LIST_HEAD(roots);
static int root_count;

/* dummytop is a shorthand for the dummy hierarchy's top cgroup */
#define dummytop (&rootnode.top_cgroup)

/* This flag indicates whether tasks in the fork and exit paths should
 * check for fork/exit handlers to call. This avoids us having to do
 * extra work in the fork/exit path if none of the subsystems need to
 * be called.
 */
static int need_forkexit_callback;

/* convenient tests for these bits */
inline int cgroup_is_removed(const struct cgroup *cgrp)
{
	return test_bit(CGRP_REMOVED, &cgrp->flags);
}

/* bits in struct cgroupfs_root flags field */
enum {
	ROOT_NOPREFIX, /* mounted subsystems have no named prefix */
};

static int cgroup_is_releasable(const struct cgroup *cgrp)
{
	const int bits =
		(1 << CGRP_RELEASABLE) |
		(1 << CGRP_NOTIFY_ON_RELEASE);
	return (cgrp->flags & bits) == bits;
}

static int notify_on_release(const struct cgroup *cgrp)
{
	return test_bit(CGRP_NOTIFY_ON_RELEASE, &cgrp->flags);
}

/*
 * for_each_subsys() allows you to iterate on each subsystem attached to
 * an active hierarchy
 */
#define for_each_subsys(_root, _ss) \
list_for_each_entry(_ss, &_root->subsys_list, sibling)

/* for_each_root() allows you to iterate across the active hierarchies */
#define for_each_root(_root) \
list_for_each_entry(_root, &roots, root_list)

/* the list of cgroups eligible for automatic release. Protected by
 * release_list_lock */
static LIST_HEAD(release_list);
static DEFINE_SPINLOCK(release_list_lock);
static void cgroup_release_agent(struct work_struct *work);
static DECLARE_WORK(release_agent_work, cgroup_release_agent);
static void check_for_release(struct cgroup *cgrp);

/* Link structure for associating css_set objects with cgroups */
struct cg_cgroup_link {
	/*
	 * List running through cg_cgroup_links associated with a
	 * cgroup, anchored on cgroup->css_sets
	 */
	struct list_head cgrp_link_list;
	/*
	 * List running through cg_cgroup_links pointing at a
	 * single css_set object, anchored on css_set->cg_links
	 */
	struct list_head cg_link_list;
	struct css_set *cg;
};

/* The default css_set - used by init and its children prior to any
 * hierarchies being mounted. It contains a pointer to the root state
 * for each subsystem. Also used to anchor the list of css_sets. Not
 * reference-counted, to improve performance when child cgroups
 * haven't been created.
 */

static struct css_set init_css_set;
static struct cg_cgroup_link init_css_set_link;

/* css_set_lock protects the list of css_set objects, and the
 * chain of tasks off each css_set.  Nests outside task->alloc_lock
 * due to cgroup_iter_start() */
static DEFINE_RWLOCK(css_set_lock);
static int css_set_count;

/* hash table for cgroup groups. This improves the performance to
 * find an existing css_set */
#define CSS_SET_HASH_BITS	7
#define CSS_SET_TABLE_SIZE	(1 << CSS_SET_HASH_BITS)
static struct hlist_head css_set_table[CSS_SET_TABLE_SIZE];

static struct hlist_head *css_set_hash(struct cgroup_subsys_state *css[])
{
	int i;
	int index;
	unsigned long tmp = 0UL;

	for (i = 0; i < CGROUP_SUBSYS_COUNT; i++)
		tmp += (unsigned long)css[i];
	tmp = (tmp >> 16) ^ tmp;

	index = hash_long(tmp, CSS_SET_HASH_BITS);

	return &css_set_table[index];
}

/* We don't maintain the lists running through each css_set to its
 * task until after the first call to cgroup_iter_start(). This
 * reduces the fork()/exit() overhead for people who have cgroups
 * compiled into their kernel but not actually in use */
static int use_task_css_set_links;

/* When we create or destroy a css_set, the operation simply
 * takes/releases a reference count on all the cgroups referenced
 * by subsystems in this css_set. This can end up multiple-counting
 * some cgroups, but that's OK - the ref-count is just a
 * busy/not-busy indicator; ensuring that we only count each cgroup
 * once would require taking a global lock to ensure that no
 * subsystems moved between hierarchies while we were doing so.
 *
 * Possible TODO: decide at boot time based on the number of
 * registered subsystems and the number of CPUs or NUMA nodes whether
 * it's better for performance to ref-count every subsystem, or to
 * take a global lock and only add one ref count to each hierarchy.
 */

/*
 * unlink a css_set from the list and free it
 */
static void unlink_css_set(struct css_set *cg)
{
	write_lock(&css_set_lock);
	hlist_del(&cg->hlist);
	css_set_count--;
	while (!list_empty(&cg->cg_links)) {
		struct cg_cgroup_link *link;
		link = list_entry(cg->cg_links.next,
				  struct cg_cgroup_link, cg_link_list);
		list_del(&link->cg_link_list);
		list_del(&link->cgrp_link_list);
		kfree(link);
	}
	write_unlock(&css_set_lock);
}

static void __release_css_set(struct kref *k, int taskexit)
{
	int i;
	struct css_set *cg = container_of(k, struct css_set, ref);

	unlink_css_set(cg);

	rcu_read_lock();
	for (i = 0; i < CGROUP_SUBSYS_COUNT; i++) {
		struct cgroup *cgrp = cg->subsys[i]->cgroup;
		if (atomic_dec_and_test(&cgrp->count) &&
		    notify_on_release(cgrp)) {
			if (taskexit)
				set_bit(CGRP_RELEASABLE, &cgrp->flags);
			check_for_release(cgrp);
		}
	}
	rcu_read_unlock();
	kfree(cg);
}

static void release_css_set(struct kref *k)
{
	__release_css_set(k, 0);
}

static void release_css_set_taskexit(struct kref *k)
{
	__release_css_set(k, 1);
}

/*
 * refcounted get/put for css_set objects
 */
static inline void get_css_set(struct css_set *cg)
{
	kref_get(&cg->ref);
}

static inline void put_css_set(struct css_set *cg)
{
	kref_put(&cg->ref, release_css_set);
}

static inline void put_css_set_taskexit(struct css_set *cg)
{
	kref_put(&cg->ref, release_css_set_taskexit);
}

/*
 * find_existing_css_set() is a helper for
 * find_css_set(), and checks to see whether an existing
 * css_set is suitable.
 *
 * oldcg: the cgroup group that we're using before the cgroup
 * transition
 *
 * cgrp: the cgroup that we're moving into
 *
 * template: location in which to build the desired set of subsystem
 * state objects for the new cgroup group
 */
static struct css_set *find_existing_css_set(
	struct css_set *oldcg,
	struct cgroup *cgrp,
	struct cgroup_subsys_state *template[])
{
	int i;
	struct cgroupfs_root *root = cgrp->root;
	struct hlist_head *hhead;
	struct hlist_node *node;
	struct css_set *cg;

	/* Built the set of subsystem state objects that we want to
	 * see in the new css_set */
	for (i = 0; i < CGROUP_SUBSYS_COUNT; i++) {
		if (root->subsys_bits & (1UL << i)) {
			/* Subsystem is in this hierarchy. So we want
			 * the subsystem state from the new
			 * cgroup */
			template[i] = cgrp->subsys[i];
		} else {
			/* Subsystem is not in this hierarchy, so we
			 * don't want to change the subsystem state */
			template[i] = oldcg->subsys[i];
		}
	}

	hhead = css_set_hash(template);
	hlist_for_each_entry(cg, node, hhead, hlist) {
		if (!memcmp(template, cg->subsys, sizeof(cg->subsys))) {
			/* All subsystems matched */
			return cg;
		}
	}

	/* No existing cgroup group matched */
	return NULL;
}

/*
 * allocate_cg_links() allocates "count" cg_cgroup_link structures
 * and chains them on tmp through their cgrp_link_list fields. Returns 0 on
 * success or a negative error
 */
static int allocate_cg_links(int count, struct list_head *tmp)
{
	struct cg_cgroup_link *link;
	int i;
	INIT_LIST_HEAD(tmp);
	for (i = 0; i < count; i++) {
		link = kmalloc(sizeof(*link), GFP_KERNEL);
		if (!link) {
			while (!list_empty(tmp)) {
				link = list_entry(tmp->next,
						  struct cg_cgroup_link,
						  cgrp_link_list);
				list_del(&link->cgrp_link_list);
				kfree(link);
			}
			return -ENOMEM;
		}
		list_add(&link->cgrp_link_list, tmp);
	}
	return 0;
}

static void free_cg_links(struct list_head *tmp)
{
	while (!list_empty(tmp)) {
		struct cg_cgroup_link *link;
		link = list_entry(tmp->next,
				  struct cg_cgroup_link,
				  cgrp_link_list);
		list_del(&link->cgrp_link_list);
		kfree(link);
	}
}

/*
 * find_css_set() takes an existing cgroup group and a
 * cgroup object, and returns a css_set object that's
 * equivalent to the old group, but with the given cgroup
 * substituted into the appropriate hierarchy. Must be called with
 * cgroup_mutex held
 */
static struct css_set *find_css_set(
	struct css_set *oldcg, struct cgroup *cgrp)
{
	struct css_set *res;
	struct cgroup_subsys_state *template[CGROUP_SUBSYS_COUNT];
	int i;

	struct list_head tmp_cg_links;
	struct cg_cgroup_link *link;

	struct hlist_head *hhead;

	/* First see if we already have a cgroup group that matches
	 * the desired set */
	write_lock(&css_set_lock);
	res = find_existing_css_set(oldcg, cgrp, template);
	if (res)
		get_css_set(res);
	write_unlock(&css_set_lock);

	if (res)
		return res;

	res = kmalloc(sizeof(*res), GFP_KERNEL);
	if (!res)
		return NULL;

	/* Allocate all the cg_cgroup_link objects that we'll need */
	if (allocate_cg_links(root_count, &tmp_cg_links) < 0) {
		kfree(res);
		return NULL;
	}

	kref_init(&res->ref);
	INIT_LIST_HEAD(&res->cg_links);
	INIT_LIST_HEAD(&res->tasks);
	INIT_HLIST_NODE(&res->hlist);

	/* Copy the set of subsystem state objects generated in
	 * find_existing_css_set() */
	memcpy(res->subsys, template, sizeof(res->subsys));

	write_lock(&css_set_lock);
	/* Add reference counts and links from the new css_set. */
	for (i = 0; i < CGROUP_SUBSYS_COUNT; i++) {
		struct cgroup *cgrp = res->subsys[i]->cgroup;
		struct cgroup_subsys *ss = subsys[i];
		atomic_inc(&cgrp->count);
		/*
		 * We want to add a link once per cgroup, so we
		 * only do it for the first subsystem in each
		 * hierarchy
		 */
		if (ss->root->subsys_list.next == &ss->sibling) {
			BUG_ON(list_empty(&tmp_cg_links));
			link = list_entry(tmp_cg_links.next,
					  struct cg_cgroup_link,
					  cgrp_link_list);
			list_del(&link->cgrp_link_list);
			list_add(&link->cgrp_link_list, &cgrp->css_sets);
			link->cg = res;
			list_add(&link->cg_link_list, &res->cg_links);
		}
	}
	if (list_empty(&rootnode.subsys_list)) {
		link = list_entry(tmp_cg_links.next,
				  struct cg_cgroup_link,
				  cgrp_link_list);
		list_del(&link->cgrp_link_list);
		list_add(&link->cgrp_link_list, &dummytop->css_sets);
		link->cg = res;
		list_add(&link->cg_link_list, &res->cg_links);
	}

	BUG_ON(!list_empty(&tmp_cg_links));

	css_set_count++;

	/* Add this cgroup group to the hash table */
	hhead = css_set_hash(res->subsys);
	hlist_add_head(&res->hlist, hhead);

	write_unlock(&css_set_lock);

	return res;
}

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
 * cgroup_attach_task() can increment it again.  Because a count of zero
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
 * to the release agent with the name of the cgroup (path relative to
 * the root of cgroup file system) as the argument.
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
 * cgroup_attach_task(), which overwrites one tasks cgroup pointer with
 * another.  It does so using cgroup_mutex, however there are
 * several performance critical places that need to reference
 * task->cgroup without the expense of grabbing a system global
 * mutex.  Therefore except as noted below, when dereferencing or, as
 * in cgroup_attach_task(), modifying a task'ss cgroup pointer we use
 * task_lock(), which acts on a spinlock (task->alloc_lock) already in
 * the task_struct routinely used for such matters.
 *
 * P.S.  One more locking exception.  RCU is used to guard the
 * update of a tasks cgroup pointer by cgroup_attach_task()
 */

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
static int cgroup_populate_dir(struct cgroup *cgrp);
static struct inode_operations cgroup_dir_inode_operations;
static struct file_operations proc_cgroupstats_operations;

static struct backing_dev_info cgroup_backing_dev_info = {
	.capabilities	= BDI_CAP_NO_ACCT_DIRTY | BDI_CAP_NO_WRITEBACK,
};

static struct inode *cgroup_new_inode(mode_t mode, struct super_block *sb)
{
	struct inode *inode = new_inode(sb);

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

/*
 * Call subsys's pre_destroy handler.
 * This is called before css refcnt check.
 */
static void cgroup_call_pre_destroy(struct cgroup *cgrp)
{
	struct cgroup_subsys *ss;
	for_each_subsys(cgrp->root, ss)
		if (ss->pre_destroy && cgrp->subsys[ss->subsys_id])
			ss->pre_destroy(ss, cgrp);
	return;
}

static void cgroup_diput(struct dentry *dentry, struct inode *inode)
{
	/* is dentry a directory ? if so, kfree() associated cgroup */
	if (S_ISDIR(inode->i_mode)) {
		struct cgroup *cgrp = dentry->d_fsdata;
		struct cgroup_subsys *ss;
		BUG_ON(!(cgroup_is_removed(cgrp)));
		/* It's possible for external users to be holding css
		 * reference counts on a cgroup; css_put() needs to
		 * be able to access the cgroup after decrementing
		 * the reference count in order to know if it needs to
		 * queue the cgroup to be handled by the release
		 * agent */
		synchronize_rcu();

		mutex_lock(&cgroup_mutex);
		/*
		 * Release the subsystem state objects.
		 */
		for_each_subsys(cgrp->root, ss) {
			if (cgrp->subsys[ss->subsys_id])
				ss->destroy(ss, cgrp);
		}

		cgrp->root->number_of_cgroups--;
		mutex_unlock(&cgroup_mutex);

		/* Drop the active superblock reference that we took when we
		 * created the cgroup */
		deactivate_super(cgrp->root->sb);

		kfree(cgrp);
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
	struct cgroup *cgrp = &root->top_cgroup;
	int i;

	removed_bits = root->actual_subsys_bits & ~final_bits;
	added_bits = final_bits & ~root->actual_subsys_bits;
	/* Check that any added subsystems are currently free */
	for (i = 0; i < CGROUP_SUBSYS_COUNT; i++) {
		unsigned long bit = 1UL << i;
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
	if (!list_empty(&cgrp->children))
		return -EBUSY;

	/* Process each subsystem */
	for (i = 0; i < CGROUP_SUBSYS_COUNT; i++) {
		struct cgroup_subsys *ss = subsys[i];
		unsigned long bit = 1UL << i;
		if (bit & added_bits) {
			/* We're binding this subsystem to this hierarchy */
			BUG_ON(cgrp->subsys[i]);
			BUG_ON(!dummytop->subsys[i]);
			BUG_ON(dummytop->subsys[i]->cgroup != dummytop);
			cgrp->subsys[i] = dummytop->subsys[i];
			cgrp->subsys[i]->cgroup = cgrp;
			list_add(&ss->sibling, &root->subsys_list);
			rcu_assign_pointer(ss->root, root);
			if (ss->bind)
				ss->bind(ss, cgrp);

		} else if (bit & removed_bits) {
			/* We're removing this subsystem */
			BUG_ON(cgrp->subsys[i] != dummytop->subsys[i]);
			BUG_ON(cgrp->subsys[i]->cgroup != cgrp);
			if (ss->bind)
				ss->bind(ss, dummytop);
			dummytop->subsys[i]->cgroup = dummytop;
			cgrp->subsys[i] = NULL;
			rcu_assign_pointer(subsys[i]->root, &rootnode);
			list_del(&ss->sibling);
		} else if (bit & final_bits) {
			/* Subsystem state should already exist */
			BUG_ON(!cgrp->subsys[i]);
		} else {
			/* Subsystem state shouldn't exist */
			BUG_ON(cgrp->subsys[i]);
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
	if (strlen(root->release_agent_path))
		seq_printf(seq, ",release_agent=%s", root->release_agent_path);
	mutex_unlock(&cgroup_mutex);
	return 0;
}

struct cgroup_sb_opts {
	unsigned long subsys_bits;
	unsigned long flags;
	char *release_agent;
};

/* Convert a hierarchy specifier into a bitmask of subsystems and
 * flags. */
static int parse_cgroupfs_options(char *data,
				     struct cgroup_sb_opts *opts)
{
	char *token, *o = data ?: "all";

	opts->subsys_bits = 0;
	opts->flags = 0;
	opts->release_agent = NULL;

	while ((token = strsep(&o, ",")) != NULL) {
		if (!*token)
			return -EINVAL;
		if (!strcmp(token, "all")) {
			/* Add all non-disabled subsystems */
			int i;
			opts->subsys_bits = 0;
			for (i = 0; i < CGROUP_SUBSYS_COUNT; i++) {
				struct cgroup_subsys *ss = subsys[i];
				if (!ss->disabled)
					opts->subsys_bits |= 1ul << i;
			}
		} else if (!strcmp(token, "noprefix")) {
			set_bit(ROOT_NOPREFIX, &opts->flags);
		} else if (!strncmp(token, "release_agent=", 14)) {
			/* Specifying two release agents is forbidden */
			if (opts->release_agent)
				return -EINVAL;
			opts->release_agent = kzalloc(PATH_MAX, GFP_KERNEL);
			if (!opts->release_agent)
				return -ENOMEM;
			strncpy(opts->release_agent, token + 14, PATH_MAX - 1);
			opts->release_agent[PATH_MAX - 1] = 0;
		} else {
			struct cgroup_subsys *ss;
			int i;
			for (i = 0; i < CGROUP_SUBSYS_COUNT; i++) {
				ss = subsys[i];
				if (!strcmp(token, ss->name)) {
					if (!ss->disabled)
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
	struct cgroup *cgrp = &root->top_cgroup;
	struct cgroup_sb_opts opts;

	mutex_lock(&cgrp->dentry->d_inode->i_mutex);
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
		cgroup_populate_dir(cgrp);

	if (opts.release_agent)
		strcpy(root->release_agent_path, opts.release_agent);
 out_unlock:
	if (opts.release_agent)
		kfree(opts.release_agent);
	mutex_unlock(&cgroup_mutex);
	mutex_unlock(&cgrp->dentry->d_inode->i_mutex);
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
	struct cgroup *cgrp = &root->top_cgroup;
	INIT_LIST_HEAD(&root->subsys_list);
	INIT_LIST_HEAD(&root->root_list);
	root->number_of_cgroups = 1;
	cgrp->root = root;
	cgrp->top_cgroup = cgrp;
	INIT_LIST_HEAD(&cgrp->sibling);
	INIT_LIST_HEAD(&cgrp->children);
	INIT_LIST_HEAD(&cgrp->css_sets);
	INIT_LIST_HEAD(&cgrp->release_list);
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
	struct list_head tmp_cg_links;
	INIT_LIST_HEAD(&tmp_cg_links);

	/* First find the desired set of subsystems */
	ret = parse_cgroupfs_options(data, &opts);
	if (ret) {
		if (opts.release_agent)
			kfree(opts.release_agent);
		return ret;
	}

	root = kzalloc(sizeof(*root), GFP_KERNEL);
	if (!root) {
		if (opts.release_agent)
			kfree(opts.release_agent);
		return -ENOMEM;
	}

	init_cgroup_root(root);
	root->subsys_bits = opts.subsys_bits;
	root->flags = opts.flags;
	if (opts.release_agent) {
		strcpy(root->release_agent_path, opts.release_agent);
		kfree(opts.release_agent);
	}

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
		struct cgroup *cgrp = &root->top_cgroup;
		struct inode *inode;
		int i;

		BUG_ON(sb->s_root != NULL);

		ret = cgroup_get_rootdir(sb);
		if (ret)
			goto drop_new_super;
		inode = sb->s_root->d_inode;

		mutex_lock(&inode->i_mutex);
		mutex_lock(&cgroup_mutex);

		/*
		 * We're accessing css_set_count without locking
		 * css_set_lock here, but that's OK - it can only be
		 * increased by someone holding cgroup_lock, and
		 * that's us. The worst that can happen is that we
		 * have some link structures left over
		 */
		ret = allocate_cg_links(css_set_count, &tmp_cg_links);
		if (ret) {
			mutex_unlock(&cgroup_mutex);
			mutex_unlock(&inode->i_mutex);
			goto drop_new_super;
		}

		ret = rebind_subsystems(root, root->subsys_bits);
		if (ret == -EBUSY) {
			mutex_unlock(&cgroup_mutex);
			mutex_unlock(&inode->i_mutex);
			goto drop_new_super;
		}

		/* EBUSY should be the only error here */
		BUG_ON(ret);

		list_add(&root->root_list, &roots);
		root_count++;

		sb->s_root->d_fsdata = &root->top_cgroup;
		root->top_cgroup.dentry = sb->s_root;

		/* Link the top cgroup in this hierarchy into all
		 * the css_set objects */
		write_lock(&css_set_lock);
		for (i = 0; i < CSS_SET_TABLE_SIZE; i++) {
			struct hlist_head *hhead = &css_set_table[i];
			struct hlist_node *node;
			struct css_set *cg;

			hlist_for_each_entry(cg, node, hhead, hlist) {
				struct cg_cgroup_link *link;

				BUG_ON(list_empty(&tmp_cg_links));
				link = list_entry(tmp_cg_links.next,
						  struct cg_cgroup_link,
						  cgrp_link_list);
				list_del(&link->cgrp_link_list);
				link->cg = cg;
				list_add(&link->cgrp_link_list,
					 &root->top_cgroup.css_sets);
				list_add(&link->cg_link_list, &cg->cg_links);
			}
		}
		write_unlock(&css_set_lock);

		free_cg_links(&tmp_cg_links);

		BUG_ON(!list_empty(&cgrp->sibling));
		BUG_ON(!list_empty(&cgrp->children));
		BUG_ON(root->number_of_cgroups != 1);

		cgroup_populate_dir(cgrp);
		mutex_unlock(&inode->i_mutex);
		mutex_unlock(&cgroup_mutex);
	}

	return simple_set_mnt(mnt, sb);

 drop_new_super:
	up_write(&sb->s_umount);
	deactivate_super(sb);
	free_cg_links(&tmp_cg_links);
	return ret;
}

static void cgroup_kill_sb(struct super_block *sb) {
	struct cgroupfs_root *root = sb->s_fs_info;
	struct cgroup *cgrp = &root->top_cgroup;
	int ret;

	BUG_ON(!root);

	BUG_ON(root->number_of_cgroups != 1);
	BUG_ON(!list_empty(&cgrp->children));
	BUG_ON(!list_empty(&cgrp->sibling));

	mutex_lock(&cgroup_mutex);

	/* Rebind all subsystems back to the default hierarchy */
	ret = rebind_subsystems(root, 0);
	/* Shouldn't be able to fail ... */
	BUG_ON(ret);

	/*
	 * Release all the links from css_sets to this hierarchy's
	 * root cgroup
	 */
	write_lock(&css_set_lock);
	while (!list_empty(&cgrp->css_sets)) {
		struct cg_cgroup_link *link;
		link = list_entry(cgrp->css_sets.next,
				  struct cg_cgroup_link, cgrp_link_list);
		list_del(&link->cg_link_list);
		list_del(&link->cgrp_link_list);
		kfree(link);
	}
	write_unlock(&css_set_lock);

	if (!list_empty(&root->root_list)) {
		list_del(&root->root_list);
		root_count--;
	}
	mutex_unlock(&cgroup_mutex);

	kfree(root);
	kill_litter_super(sb);
}

static struct file_system_type cgroup_fs_type = {
	.name = "cgroup",
	.get_sb = cgroup_get_sb,
	.kill_sb = cgroup_kill_sb,
};

static inline struct cgroup *__d_cgrp(struct dentry *dentry)
{
	return dentry->d_fsdata;
}

static inline struct cftype *__d_cft(struct dentry *dentry)
{
	return dentry->d_fsdata;
}

/**
 * cgroup_path - generate the path of a cgroup
 * @cgrp: the cgroup in question
 * @buf: the buffer to write the path into
 * @buflen: the length of the buffer
 *
 * Called with cgroup_mutex held. Writes path of cgroup into buf.
 * Returns 0 on success, -errno on error.
 */
int cgroup_path(const struct cgroup *cgrp, char *buf, int buflen)
{
	char *start;

	if (cgrp == dummytop) {
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
		int len = cgrp->dentry->d_name.len;
		if ((start -= len) < buf)
			return -ENAMETOOLONG;
		memcpy(start, cgrp->dentry->d_name.name, len);
		cgrp = cgrp->parent;
		if (!cgrp)
			break;
		if (!cgrp->parent)
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

static void get_first_subsys(const struct cgroup *cgrp,
			struct cgroup_subsys_state **css, int *subsys_id)
{
	const struct cgroupfs_root *root = cgrp->root;
	const struct cgroup_subsys *test_ss;
	BUG_ON(list_empty(&root->subsys_list));
	test_ss = list_entry(root->subsys_list.next,
			     struct cgroup_subsys, sibling);
	if (css) {
		*css = cgrp->subsys[test_ss->subsys_id];
		BUG_ON(!*css);
	}
	if (subsys_id)
		*subsys_id = test_ss->subsys_id;
}

/**
 * cgroup_attach_task - attach task 'tsk' to cgroup 'cgrp'
 * @cgrp: the cgroup the task is attaching to
 * @tsk: the task to be attached
 *
 * Call holding cgroup_mutex. May take task_lock of
 * the task 'tsk' during call.
 */
int cgroup_attach_task(struct cgroup *cgrp, struct task_struct *tsk)
{
	int retval = 0;
	struct cgroup_subsys *ss;
	struct cgroup *oldcgrp;
	struct css_set *cg = tsk->cgroups;
	struct css_set *newcg;
	struct cgroupfs_root *root = cgrp->root;
	int subsys_id;

	get_first_subsys(cgrp, NULL, &subsys_id);

	/* Nothing to do if the task is already in that cgroup */
	oldcgrp = task_cgroup(tsk, subsys_id);
	if (cgrp == oldcgrp)
		return 0;

	for_each_subsys(root, ss) {
		if (ss->can_attach) {
			retval = ss->can_attach(ss, cgrp, tsk);
			if (retval)
				return retval;
		}
	}

	/*
	 * Locate or allocate a new css_set for this task,
	 * based on its final set of cgroups
	 */
	newcg = find_css_set(cg, cgrp);
	if (!newcg)
		return -ENOMEM;

	task_lock(tsk);
	if (tsk->flags & PF_EXITING) {
		task_unlock(tsk);
		put_css_set(newcg);
		return -ESRCH;
	}
	rcu_assign_pointer(tsk->cgroups, newcg);
	task_unlock(tsk);

	/* Update the css_set linked lists if we're using them */
	write_lock(&css_set_lock);
	if (!list_empty(&tsk->cg_list)) {
		list_del(&tsk->cg_list);
		list_add(&tsk->cg_list, &newcg->tasks);
	}
	write_unlock(&css_set_lock);

	for_each_subsys(root, ss) {
		if (ss->attach)
			ss->attach(ss, cgrp, oldcgrp, tsk);
	}
	set_bit(CGRP_RELEASABLE, &oldcgrp->flags);
	synchronize_rcu();
	put_css_set(cg);
	return 0;
}

/*
 * Attach task with pid 'pid' to cgroup 'cgrp'. Call with
 * cgroup_mutex, may take task_lock of task
 */
static int attach_task_by_pid(struct cgroup *cgrp, char *pidbuf)
{
	pid_t pid;
	struct task_struct *tsk;
	int ret;

	if (sscanf(pidbuf, "%d", &pid) != 1)
		return -EIO;

	if (pid) {
		rcu_read_lock();
		tsk = find_task_by_vpid(pid);
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

	ret = cgroup_attach_task(cgrp, tsk);
	put_task_struct(tsk);
	return ret;
}

/* The various types of files and directories in a cgroup file system */
enum cgroup_filetype {
	FILE_ROOT,
	FILE_DIR,
	FILE_TASKLIST,
	FILE_NOTIFY_ON_RELEASE,
	FILE_RELEASE_AGENT,
};

static ssize_t cgroup_write_X64(struct cgroup *cgrp, struct cftype *cft,
				struct file *file,
				const char __user *userbuf,
				size_t nbytes, loff_t *unused_ppos)
{
	char buffer[64];
	int retval = 0;
	char *end;

	if (!nbytes)
		return -EINVAL;
	if (nbytes >= sizeof(buffer))
		return -E2BIG;
	if (copy_from_user(buffer, userbuf, nbytes))
		return -EFAULT;

	buffer[nbytes] = 0;     /* nul-terminate */
	strstrip(buffer);
	if (cft->write_u64) {
		u64 val = simple_strtoull(buffer, &end, 0);
		if (*end)
			return -EINVAL;
		retval = cft->write_u64(cgrp, cft, val);
	} else {
		s64 val = simple_strtoll(buffer, &end, 0);
		if (*end)
			return -EINVAL;
		retval = cft->write_s64(cgrp, cft, val);
	}
	if (!retval)
		retval = nbytes;
	return retval;
}

static ssize_t cgroup_common_file_write(struct cgroup *cgrp,
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
	strstrip(buffer);	/* strip -just- trailing whitespace */

	mutex_lock(&cgroup_mutex);

	/*
	 * This was already checked for in cgroup_file_write(), but
	 * check again now we're holding cgroup_mutex.
	 */
	if (cgroup_is_removed(cgrp)) {
		retval = -ENODEV;
		goto out2;
	}

	switch (type) {
	case FILE_TASKLIST:
		retval = attach_task_by_pid(cgrp, buffer);
		break;
	case FILE_NOTIFY_ON_RELEASE:
		clear_bit(CGRP_RELEASABLE, &cgrp->flags);
		if (simple_strtoul(buffer, NULL, 10) != 0)
			set_bit(CGRP_NOTIFY_ON_RELEASE, &cgrp->flags);
		else
			clear_bit(CGRP_NOTIFY_ON_RELEASE, &cgrp->flags);
		break;
	case FILE_RELEASE_AGENT:
		BUILD_BUG_ON(sizeof(cgrp->root->release_agent_path) < PATH_MAX);
		strcpy(cgrp->root->release_agent_path, buffer);
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
	struct cgroup *cgrp = __d_cgrp(file->f_dentry->d_parent);

	if (!cft || cgroup_is_removed(cgrp))
		return -ENODEV;
	if (cft->write)
		return cft->write(cgrp, cft, file, buf, nbytes, ppos);
	if (cft->write_u64 || cft->write_s64)
		return cgroup_write_X64(cgrp, cft, file, buf, nbytes, ppos);
	if (cft->trigger) {
		int ret = cft->trigger(cgrp, (unsigned int)cft->private);
		return ret ? ret : nbytes;
	}
	return -EINVAL;
}

static ssize_t cgroup_read_u64(struct cgroup *cgrp, struct cftype *cft,
			       struct file *file,
			       char __user *buf, size_t nbytes,
			       loff_t *ppos)
{
	char tmp[64];
	u64 val = cft->read_u64(cgrp, cft);
	int len = sprintf(tmp, "%llu\n", (unsigned long long) val);

	return simple_read_from_buffer(buf, nbytes, ppos, tmp, len);
}

static ssize_t cgroup_read_s64(struct cgroup *cgrp, struct cftype *cft,
			       struct file *file,
			       char __user *buf, size_t nbytes,
			       loff_t *ppos)
{
	char tmp[64];
	s64 val = cft->read_s64(cgrp, cft);
	int len = sprintf(tmp, "%lld\n", (long long) val);

	return simple_read_from_buffer(buf, nbytes, ppos, tmp, len);
}

static ssize_t cgroup_common_file_read(struct cgroup *cgrp,
					  struct cftype *cft,
					  struct file *file,
					  char __user *buf,
					  size_t nbytes, loff_t *ppos)
{
	enum cgroup_filetype type = cft->private;
	char *page;
	ssize_t retval = 0;
	char *s;

	if (!(page = (char *)__get_free_page(GFP_KERNEL)))
		return -ENOMEM;

	s = page;

	switch (type) {
	case FILE_RELEASE_AGENT:
	{
		struct cgroupfs_root *root;
		size_t n;
		mutex_lock(&cgroup_mutex);
		root = cgrp->root;
		n = strnlen(root->release_agent_path,
			    sizeof(root->release_agent_path));
		n = min(n, (size_t) PAGE_SIZE);
		strncpy(s, root->release_agent_path, n);
		mutex_unlock(&cgroup_mutex);
		s += n;
		break;
	}
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

static ssize_t cgroup_file_read(struct file *file, char __user *buf,
				   size_t nbytes, loff_t *ppos)
{
	struct cftype *cft = __d_cft(file->f_dentry);
	struct cgroup *cgrp = __d_cgrp(file->f_dentry->d_parent);

	if (!cft || cgroup_is_removed(cgrp))
		return -ENODEV;

	if (cft->read)
		return cft->read(cgrp, cft, file, buf, nbytes, ppos);
	if (cft->read_u64)
		return cgroup_read_u64(cgrp, cft, file, buf, nbytes, ppos);
	if (cft->read_s64)
		return cgroup_read_s64(cgrp, cft, file, buf, nbytes, ppos);
	return -EINVAL;
}

/*
 * seqfile ops/methods for returning structured data. Currently just
 * supports string->u64 maps, but can be extended in future.
 */

struct cgroup_seqfile_state {
	struct cftype *cft;
	struct cgroup *cgroup;
};

static int cgroup_map_add(struct cgroup_map_cb *cb, const char *key, u64 value)
{
	struct seq_file *sf = cb->state;
	return seq_printf(sf, "%s %llu\n", key, (unsigned long long)value);
}

static int cgroup_seqfile_show(struct seq_file *m, void *arg)
{
	struct cgroup_seqfile_state *state = m->private;
	struct cftype *cft = state->cft;
	if (cft->read_map) {
		struct cgroup_map_cb cb = {
			.fill = cgroup_map_add,
			.state = m,
		};
		return cft->read_map(state->cgroup, cft, &cb);
	}
	return cft->read_seq_string(state->cgroup, cft, m);
}

int cgroup_seqfile_release(struct inode *inode, struct file *file)
{
	struct seq_file *seq = file->private_data;
	kfree(seq->private);
	return single_release(inode, file);
}

static struct file_operations cgroup_seqfile_operations = {
	.read = seq_read,
	.llseek = seq_lseek,
	.release = cgroup_seqfile_release,
};

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
	if (cft->read_map || cft->read_seq_string) {
		struct cgroup_seqfile_state *state =
			kzalloc(sizeof(*state), GFP_USER);
		if (!state)
			return -ENOMEM;
		state->cft = cft;
		state->cgroup = __d_cgrp(file->f_dentry->d_parent);
		file->f_op = &cgroup_seqfile_operations;
		err = single_open(file, cgroup_seqfile_show, state);
		if (err < 0)
			kfree(state);
	} else if (cft->open)
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
		mutex_lock_nested(&inode->i_mutex, I_MUTEX_CHILD);
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
 * cgroup_create_dir - create a directory for an object.
 * @cgrp: the cgroup we create the directory for. It must have a valid
 *        ->parent field. And we are going to fill its ->dentry field.
 * @dentry: dentry of the new cgroup
 * @mode: mode to set on new directory.
 */
static int cgroup_create_dir(struct cgroup *cgrp, struct dentry *dentry,
				int mode)
{
	struct dentry *parent;
	int error = 0;

	parent = cgrp->parent->dentry;
	error = cgroup_create_file(dentry, S_IFDIR | mode, cgrp->root->sb);
	if (!error) {
		dentry->d_fsdata = cgrp;
		inc_nlink(parent->d_inode);
		cgrp->dentry = dentry;
		dget(dentry);
	}
	dput(dentry);

	return error;
}

int cgroup_add_file(struct cgroup *cgrp,
		       struct cgroup_subsys *subsys,
		       const struct cftype *cft)
{
	struct dentry *dir = cgrp->dentry;
	struct dentry *dentry;
	int error;

	char name[MAX_CGROUP_TYPE_NAMELEN + MAX_CFTYPE_NAME + 2] = { 0 };
	if (subsys && !test_bit(ROOT_NOPREFIX, &cgrp->root->flags)) {
		strcpy(name, subsys->name);
		strcat(name, ".");
	}
	strcat(name, cft->name);
	BUG_ON(!mutex_is_locked(&dir->d_inode->i_mutex));
	dentry = lookup_one_len(name, dir, strlen(name));
	if (!IS_ERR(dentry)) {
		error = cgroup_create_file(dentry, 0644 | S_IFREG,
						cgrp->root->sb);
		if (!error)
			dentry->d_fsdata = (void *)cft;
		dput(dentry);
	} else
		error = PTR_ERR(dentry);
	return error;
}

int cgroup_add_files(struct cgroup *cgrp,
			struct cgroup_subsys *subsys,
			const struct cftype cft[],
			int count)
{
	int i, err;
	for (i = 0; i < count; i++) {
		err = cgroup_add_file(cgrp, subsys, &cft[i]);
		if (err)
			return err;
	}
	return 0;
}

/**
 * cgroup_task_count - count the number of tasks in a cgroup.
 * @cgrp: the cgroup in question
 *
 * Return the number of tasks in the cgroup.
 */
int cgroup_task_count(const struct cgroup *cgrp)
{
	int count = 0;
	struct list_head *l;

	read_lock(&css_set_lock);
	l = cgrp->css_sets.next;
	while (l != &cgrp->css_sets) {
		struct cg_cgroup_link *link =
			list_entry(l, struct cg_cgroup_link, cgrp_link_list);
		count += atomic_read(&link->cg->ref.refcount);
		l = l->next;
	}
	read_unlock(&css_set_lock);
	return count;
}

/*
 * Advance a list_head iterator.  The iterator should be positioned at
 * the start of a css_set
 */
static void cgroup_advance_iter(struct cgroup *cgrp,
					  struct cgroup_iter *it)
{
	struct list_head *l = it->cg_link;
	struct cg_cgroup_link *link;
	struct css_set *cg;

	/* Advance to the next non-empty css_set */
	do {
		l = l->next;
		if (l == &cgrp->css_sets) {
			it->cg_link = NULL;
			return;
		}
		link = list_entry(l, struct cg_cgroup_link, cgrp_link_list);
		cg = link->cg;
	} while (list_empty(&cg->tasks));
	it->cg_link = l;
	it->task = cg->tasks.next;
}

/*
 * To reduce the fork() overhead for systems that are not actually
 * using their cgroups capability, we don't maintain the lists running
 * through each css_set to its tasks until we see the list actually
 * used - in other words after the first call to cgroup_iter_start().
 *
 * The tasklist_lock is not held here, as do_each_thread() and
 * while_each_thread() are protected by RCU.
 */
static void cgroup_enable_task_cg_lists(void)
{
	struct task_struct *p, *g;
	write_lock(&css_set_lock);
	use_task_css_set_links = 1;
	do_each_thread(g, p) {
		task_lock(p);
		/*
		 * We should check if the process is exiting, otherwise
		 * it will race with cgroup_exit() in that the list
		 * entry won't be deleted though the process has exited.
		 */
		if (!(p->flags & PF_EXITING) && list_empty(&p->cg_list))
			list_add(&p->cg_list, &p->cgroups->tasks);
		task_unlock(p);
	} while_each_thread(g, p);
	write_unlock(&css_set_lock);
}

void cgroup_iter_start(struct cgroup *cgrp, struct cgroup_iter *it)
{
	/*
	 * The first time anyone tries to iterate across a cgroup,
	 * we need to enable the list linking each css_set to its
	 * tasks, and fix up all existing tasks.
	 */
	if (!use_task_css_set_links)
		cgroup_enable_task_cg_lists();

	read_lock(&css_set_lock);
	it->cg_link = &cgrp->css_sets;
	cgroup_advance_iter(cgrp, it);
}

struct task_struct *cgroup_iter_next(struct cgroup *cgrp,
					struct cgroup_iter *it)
{
	struct task_struct *res;
	struct list_head *l = it->task;

	/* If the iterator cg is NULL, we have no tasks */
	if (!it->cg_link)
		return NULL;
	res = list_entry(l, struct task_struct, cg_list);
	/* Advance iterator to find next entry */
	l = l->next;
	if (l == &res->cgroups->tasks) {
		/* We reached the end of this task list - move on to
		 * the next cg_cgroup_link */
		cgroup_advance_iter(cgrp, it);
	} else {
		it->task = l;
	}
	return res;
}

void cgroup_iter_end(struct cgroup *cgrp, struct cgroup_iter *it)
{
	read_unlock(&css_set_lock);
}

static inline int started_after_time(struct task_struct *t1,
				     struct timespec *time,
				     struct task_struct *t2)
{
	int start_diff = timespec_compare(&t1->start_time, time);
	if (start_diff > 0) {
		return 1;
	} else if (start_diff < 0) {
		return 0;
	} else {
		/*
		 * Arbitrarily, if two processes started at the same
		 * time, we'll say that the lower pointer value
		 * started first. Note that t2 may have exited by now
		 * so this may not be a valid pointer any longer, but
		 * that's fine - it still serves to distinguish
		 * between two tasks started (effectively) simultaneously.
		 */
		return t1 > t2;
	}
}

/*
 * This function is a callback from heap_insert() and is used to order
 * the heap.
 * In this case we order the heap in descending task start time.
 */
static inline int started_after(void *p1, void *p2)
{
	struct task_struct *t1 = p1;
	struct task_struct *t2 = p2;
	return started_after_time(t1, &t2->start_time, t2);
}

/**
 * cgroup_scan_tasks - iterate though all the tasks in a cgroup
 * @scan: struct cgroup_scanner containing arguments for the scan
 *
 * Arguments include pointers to callback functions test_task() and
 * process_task().
 * Iterate through all the tasks in a cgroup, calling test_task() for each,
 * and if it returns true, call process_task() for it also.
 * The test_task pointer may be NULL, meaning always true (select all tasks).
 * Effectively duplicates cgroup_iter_{start,next,end}()
 * but does not lock css_set_lock for the call to process_task().
 * The struct cgroup_scanner may be embedded in any structure of the caller's
 * creation.
 * It is guaranteed that process_task() will act on every task that
 * is a member of the cgroup for the duration of this call. This
 * function may or may not call process_task() for tasks that exit
 * or move to a different cgroup during the call, or are forked or
 * move into the cgroup during the call.
 *
 * Note that test_task() may be called with locks held, and may in some
 * situations be called multiple times for the same task, so it should
 * be cheap.
 * If the heap pointer in the struct cgroup_scanner is non-NULL, a heap has been
 * pre-allocated and will be used for heap operations (and its "gt" member will
 * be overwritten), else a temporary heap will be used (allocation of which
 * may cause this function to fail).
 */
int cgroup_scan_tasks(struct cgroup_scanner *scan)
{
	int retval, i;
	struct cgroup_iter it;
	struct task_struct *p, *dropped;
	/* Never dereference latest_task, since it's not refcounted */
	struct task_struct *latest_task = NULL;
	struct ptr_heap tmp_heap;
	struct ptr_heap *heap;
	struct timespec latest_time = { 0, 0 };

	if (scan->heap) {
		/* The caller supplied our heap and pre-allocated its memory */
		heap = scan->heap;
		heap->gt = &started_after;
	} else {
		/* We need to allocate our own heap memory */
		heap = &tmp_heap;
		retval = heap_init(heap, PAGE_SIZE, GFP_KERNEL, &started_after);
		if (retval)
			/* cannot allocate the heap */
			return retval;
	}

 again:
	/*
	 * Scan tasks in the cgroup, using the scanner's "test_task" callback
	 * to determine which are of interest, and using the scanner's
	 * "process_task" callback to process any of them that need an update.
	 * Since we don't want to hold any locks during the task updates,
	 * gather tasks to be processed in a heap structure.
	 * The heap is sorted by descending task start time.
	 * If the statically-sized heap fills up, we overflow tasks that
	 * started later, and in future iterations only consider tasks that
	 * started after the latest task in the previous pass. This
	 * guarantees forward progress and that we don't miss any tasks.
	 */
	heap->size = 0;
	cgroup_iter_start(scan->cg, &it);
	while ((p = cgroup_iter_next(scan->cg, &it))) {
		/*
		 * Only affect tasks that qualify per the caller's callback,
		 * if he provided one
		 */
		if (scan->test_task && !scan->test_task(p, scan))
			continue;
		/*
		 * Only process tasks that started after the last task
		 * we processed
		 */
		if (!started_after_time(p, &latest_time, latest_task))
			continue;
		dropped = heap_insert(heap, p);
		if (dropped == NULL) {
			/*
			 * The new task was inserted; the heap wasn't
			 * previously full
			 */
			get_task_struct(p);
		} else if (dropped != p) {
			/*
			 * The new task was inserted, and pushed out a
			 * different task
			 */
			get_task_struct(p);
			put_task_struct(dropped);
		}
		/*
		 * Else the new task was newer than anything already in
		 * the heap and wasn't inserted
		 */
	}
	cgroup_iter_end(scan->cg, &it);

	if (heap->size) {
		for (i = 0; i < heap->size; i++) {
			struct task_struct *q = heap->ptrs[i];
			if (i == 0) {
				latest_time = q->start_time;
				latest_task = q;
			}
			/* Process the task per the caller's callback */
			scan->process_task(q, scan);
			put_task_struct(q);
		}
		/*
		 * If we had to process any tasks at all, scan again
		 * in case some of them were in the middle of forking
		 * children that didn't get processed.
		 * Not the most efficient way to do it, but it avoids
		 * having to take callback_mutex in the fork path
		 */
		goto again;
	}
	if (heap == &tmp_heap)
		heap_free(&tmp_heap);
	return 0;
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
 * 'cgrp'.  Return actual number of pids loaded.  No need to
 * task_lock(p) when reading out p->cgroup, since we're in an RCU
 * read section, so the css_set can't go away, and is
 * immutable after creation.
 */
static int pid_array_load(pid_t *pidarray, int npids, struct cgroup *cgrp)
{
	int n = 0;
	struct cgroup_iter it;
	struct task_struct *tsk;
	cgroup_iter_start(cgrp, &it);
	while ((tsk = cgroup_iter_next(cgrp, &it))) {
		if (unlikely(n == npids))
			break;
		pidarray[n++] = task_pid_vnr(tsk);
	}
	cgroup_iter_end(cgrp, &it);
	return n;
}

/**
 * cgroupstats_build - build and fill cgroupstats
 * @stats: cgroupstats to fill information into
 * @dentry: A dentry entry belonging to the cgroup for which stats have
 * been requested.
 *
 * Build and fill cgroupstats so that taskstats can export it to user
 * space.
 */
int cgroupstats_build(struct cgroupstats *stats, struct dentry *dentry)
{
	int ret = -EINVAL;
	struct cgroup *cgrp;
	struct cgroup_iter it;
	struct task_struct *tsk;
	/*
	 * Validate dentry by checking the superblock operations
	 */
	if (dentry->d_sb->s_op != &cgroup_ops)
		 goto err;

	ret = 0;
	cgrp = dentry->d_fsdata;
	rcu_read_lock();

	cgroup_iter_start(cgrp, &it);
	while ((tsk = cgroup_iter_next(cgrp, &it))) {
		switch (tsk->state) {
		case TASK_RUNNING:
			stats->nr_running++;
			break;
		case TASK_INTERRUPTIBLE:
			stats->nr_sleeping++;
			break;
		case TASK_UNINTERRUPTIBLE:
			stats->nr_uninterruptible++;
			break;
		case TASK_STOPPED:
			stats->nr_stopped++;
			break;
		default:
			if (delayacct_is_task_waiting_on_io(tsk))
				stats->nr_io_wait++;
			break;
		}
	}
	cgroup_iter_end(cgrp, &it);

	rcu_read_unlock();
err:
	return ret;
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
	struct cgroup *cgrp = __d_cgrp(file->f_dentry->d_parent);
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
	npids = cgroup_task_count(cgrp);
	if (npids) {
		pidarray = kmalloc(npids * sizeof(pid_t), GFP_KERNEL);
		if (!pidarray)
			goto err1;

		npids = pid_array_load(pidarray, npids, cgrp);
		sort(pidarray, npids, sizeof(pid_t), cmppid, NULL);

		/* Call pid_array_to_buf() twice, first just to get bufsz */
		ctr->bufsz = pid_array_to_buf(&c, sizeof(c), pidarray, npids) + 1;
		ctr->buf = kmalloc(ctr->bufsz, GFP_KERNEL);
		if (!ctr->buf)
			goto err2;
		ctr->bufsz = pid_array_to_buf(ctr->buf, ctr->bufsz, pidarray, npids);

		kfree(pidarray);
	} else {
		ctr->buf = NULL;
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

static ssize_t cgroup_tasks_read(struct cgroup *cgrp,
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

static u64 cgroup_read_notify_on_release(struct cgroup *cgrp,
					    struct cftype *cft)
{
	return notify_on_release(cgrp);
}

/*
 * for the common functions, 'private' gives the type of file
 */
static struct cftype files[] = {
	{
		.name = "tasks",
		.open = cgroup_tasks_open,
		.read = cgroup_tasks_read,
		.write = cgroup_common_file_write,
		.release = cgroup_tasks_release,
		.private = FILE_TASKLIST,
	},

	{
		.name = "notify_on_release",
		.read_u64 = cgroup_read_notify_on_release,
		.write = cgroup_common_file_write,
		.private = FILE_NOTIFY_ON_RELEASE,
	},
};

static struct cftype cft_release_agent = {
	.name = "release_agent",
	.read = cgroup_common_file_read,
	.write = cgroup_common_file_write,
	.private = FILE_RELEASE_AGENT,
};

static int cgroup_populate_dir(struct cgroup *cgrp)
{
	int err;
	struct cgroup_subsys *ss;

	/* First clear out any existing files */
	cgroup_clear_directory(cgrp->dentry);

	err = cgroup_add_files(cgrp, NULL, files, ARRAY_SIZE(files));
	if (err < 0)
		return err;

	if (cgrp == cgrp->top_cgroup) {
		if ((err = cgroup_add_file(cgrp, NULL, &cft_release_agent)) < 0)
			return err;
	}

	for_each_subsys(cgrp->root, ss) {
		if (ss->populate && (err = ss->populate(ss, cgrp)) < 0)
			return err;
	}

	return 0;
}

static void init_cgroup_css(struct cgroup_subsys_state *css,
			       struct cgroup_subsys *ss,
			       struct cgroup *cgrp)
{
	css->cgroup = cgrp;
	atomic_set(&css->refcnt, 0);
	css->flags = 0;
	if (cgrp == dummytop)
		set_bit(CSS_ROOT, &css->flags);
	BUG_ON(cgrp->subsys[ss->subsys_id]);
	cgrp->subsys[ss->subsys_id] = css;
}

/*
 * cgroup_create - create a cgroup
 * @parent: cgroup that will be parent of the new cgroup
 * @dentry: dentry of the new cgroup
 * @mode: mode to set on new inode
 *
 * Must be called with the mutex on the parent inode held
 */
static long cgroup_create(struct cgroup *parent, struct dentry *dentry,
			     int mode)
{
	struct cgroup *cgrp;
	struct cgroupfs_root *root = parent->root;
	int err = 0;
	struct cgroup_subsys *ss;
	struct super_block *sb = root->sb;

	cgrp = kzalloc(sizeof(*cgrp), GFP_KERNEL);
	if (!cgrp)
		return -ENOMEM;

	/* Grab a reference on the superblock so the hierarchy doesn't
	 * get deleted on unmount if there are child cgroups.  This
	 * can be done outside cgroup_mutex, since the sb can't
	 * disappear while someone has an open control file on the
	 * fs */
	atomic_inc(&sb->s_active);

	mutex_lock(&cgroup_mutex);

	INIT_LIST_HEAD(&cgrp->sibling);
	INIT_LIST_HEAD(&cgrp->children);
	INIT_LIST_HEAD(&cgrp->css_sets);
	INIT_LIST_HEAD(&cgrp->release_list);

	cgrp->parent = parent;
	cgrp->root = parent->root;
	cgrp->top_cgroup = parent->top_cgroup;

	if (notify_on_release(parent))
		set_bit(CGRP_NOTIFY_ON_RELEASE, &cgrp->flags);

	for_each_subsys(root, ss) {
		struct cgroup_subsys_state *css = ss->create(ss, cgrp);
		if (IS_ERR(css)) {
			err = PTR_ERR(css);
			goto err_destroy;
		}
		init_cgroup_css(css, ss, cgrp);
	}

	list_add(&cgrp->sibling, &cgrp->parent->children);
	root->number_of_cgroups++;

	err = cgroup_create_dir(cgrp, dentry, mode);
	if (err < 0)
		goto err_remove;

	/* The cgroup directory was pre-locked for us */
	BUG_ON(!mutex_is_locked(&cgrp->dentry->d_inode->i_mutex));

	err = cgroup_populate_dir(cgrp);
	/* If err < 0, we have a half-filled directory - oh well ;) */

	mutex_unlock(&cgroup_mutex);
	mutex_unlock(&cgrp->dentry->d_inode->i_mutex);

	return 0;

 err_remove:

	list_del(&cgrp->sibling);
	root->number_of_cgroups--;

 err_destroy:

	for_each_subsys(root, ss) {
		if (cgrp->subsys[ss->subsys_id])
			ss->destroy(ss, cgrp);
	}

	mutex_unlock(&cgroup_mutex);

	/* Release the reference count that we took on the superblock */
	deactivate_super(sb);

	kfree(cgrp);
	return err;
}

static int cgroup_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	struct cgroup *c_parent = dentry->d_parent->d_fsdata;

	/* the vfs holds inode->i_mutex already */
	return cgroup_create(c_parent, dentry, mode | S_IFDIR);
}

static inline int cgroup_has_css_refs(struct cgroup *cgrp)
{
	/* Check the reference count on each subsystem. Since we
	 * already established that there are no tasks in the
	 * cgroup, if the css refcount is also 0, then there should
	 * be no outstanding references, so the subsystem is safe to
	 * destroy. We scan across all subsystems rather than using
	 * the per-hierarchy linked list of mounted subsystems since
	 * we can be called via check_for_release() with no
	 * synchronization other than RCU, and the subsystem linked
	 * list isn't RCU-safe */
	int i;
	for (i = 0; i < CGROUP_SUBSYS_COUNT; i++) {
		struct cgroup_subsys *ss = subsys[i];
		struct cgroup_subsys_state *css;
		/* Skip subsystems not in this hierarchy */
		if (ss->root != cgrp->root)
			continue;
		css = cgrp->subsys[ss->subsys_id];
		/* When called from check_for_release() it's possible
		 * that by this point the cgroup has been removed
		 * and the css deleted. But a false-positive doesn't
		 * matter, since it can only happen if the cgroup
		 * has been deleted and hence no longer needs the
		 * release agent to be called anyway. */
		if (css && atomic_read(&css->refcnt))
			return 1;
	}
	return 0;
}

static int cgroup_rmdir(struct inode *unused_dir, struct dentry *dentry)
{
	struct cgroup *cgrp = dentry->d_fsdata;
	struct dentry *d;
	struct cgroup *parent;
	struct super_block *sb;
	struct cgroupfs_root *root;

	/* the vfs holds both inode->i_mutex already */

	mutex_lock(&cgroup_mutex);
	if (atomic_read(&cgrp->count) != 0) {
		mutex_unlock(&cgroup_mutex);
		return -EBUSY;
	}
	if (!list_empty(&cgrp->children)) {
		mutex_unlock(&cgroup_mutex);
		return -EBUSY;
	}

	parent = cgrp->parent;
	root = cgrp->root;
	sb = root->sb;

	/*
	 * Call pre_destroy handlers of subsys. Notify subsystems
	 * that rmdir() request comes.
	 */
	cgroup_call_pre_destroy(cgrp);

	if (cgroup_has_css_refs(cgrp)) {
		mutex_unlock(&cgroup_mutex);
		return -EBUSY;
	}

	spin_lock(&release_list_lock);
	set_bit(CGRP_REMOVED, &cgrp->flags);
	if (!list_empty(&cgrp->release_list))
		list_del(&cgrp->release_list);
	spin_unlock(&release_list_lock);
	/* delete my sibling from parent->children */
	list_del(&cgrp->sibling);
	spin_lock(&cgrp->dentry->d_lock);
	d = dget(cgrp->dentry);
	cgrp->dentry = NULL;
	spin_unlock(&d->d_lock);

	cgroup_d_remove_dir(d);
	dput(d);

	set_bit(CGRP_RELEASABLE, &parent->flags);
	check_for_release(parent);

	mutex_unlock(&cgroup_mutex);
	return 0;
}

static void __init cgroup_init_subsys(struct cgroup_subsys *ss)
{
	struct cgroup_subsys_state *css;

	printk(KERN_INFO "Initializing cgroup subsys %s\n", ss->name);

	/* Create the top cgroup state for this subsystem */
	ss->root = &rootnode;
	css = ss->create(ss, dummytop);
	/* We don't handle early failures gracefully */
	BUG_ON(IS_ERR(css));
	init_cgroup_css(css, ss, dummytop);

	/* Update the init_css_set to contain a subsys
	 * pointer to this state - since the subsystem is
	 * newly registered, all tasks and hence the
	 * init_css_set is in the subsystem's top cgroup. */
	init_css_set.subsys[ss->subsys_id] = dummytop->subsys[ss->subsys_id];

	need_forkexit_callback |= ss->fork || ss->exit;

	/* At system boot, before all subsystems have been
	 * registered, no tasks have been forked, so we don't
	 * need to invoke fork callbacks here. */
	BUG_ON(!list_empty(&init_task.tasks));

	ss->active = 1;
}

/**
 * cgroup_init_early - cgroup initialization at system boot
 *
 * Initialize cgroups at system boot, and initialize any
 * subsystems that request early init.
 */
int __init cgroup_init_early(void)
{
	int i;
	kref_init(&init_css_set.ref);
	kref_get(&init_css_set.ref);
	INIT_LIST_HEAD(&init_css_set.cg_links);
	INIT_LIST_HEAD(&init_css_set.tasks);
	INIT_HLIST_NODE(&init_css_set.hlist);
	css_set_count = 1;
	init_cgroup_root(&rootnode);
	list_add(&rootnode.root_list, &roots);
	root_count = 1;
	init_task.cgroups = &init_css_set;

	init_css_set_link.cg = &init_css_set;
	list_add(&init_css_set_link.cgrp_link_list,
		 &rootnode.top_cgroup.css_sets);
	list_add(&init_css_set_link.cg_link_list,
		 &init_css_set.cg_links);

	for (i = 0; i < CSS_SET_TABLE_SIZE; i++)
		INIT_HLIST_HEAD(&css_set_table[i]);

	for (i = 0; i < CGROUP_SUBSYS_COUNT; i++) {
		struct cgroup_subsys *ss = subsys[i];

		BUG_ON(!ss->name);
		BUG_ON(strlen(ss->name) > MAX_CGROUP_TYPE_NAMELEN);
		BUG_ON(!ss->create);
		BUG_ON(!ss->destroy);
		if (ss->subsys_id != i) {
			printk(KERN_ERR "cgroup: Subsys %s id == %d\n",
			       ss->name, ss->subsys_id);
			BUG();
		}

		if (ss->early_init)
			cgroup_init_subsys(ss);
	}
	return 0;
}

/**
 * cgroup_init - cgroup initialization
 *
 * Register cgroup filesystem and /proc file, and initialize
 * any subsystems that didn't request early init.
 */
int __init cgroup_init(void)
{
	int err;
	int i;
	struct hlist_head *hhead;

	err = bdi_init(&cgroup_backing_dev_info);
	if (err)
		return err;

	for (i = 0; i < CGROUP_SUBSYS_COUNT; i++) {
		struct cgroup_subsys *ss = subsys[i];
		if (!ss->early_init)
			cgroup_init_subsys(ss);
	}

	/* Add init_css_set to the hash table */
	hhead = css_set_hash(init_css_set.subsys);
	hlist_add_head(&init_css_set.hlist, hhead);

	err = register_filesystem(&cgroup_fs_type);
	if (err < 0)
		goto out;

	proc_create("cgroups", 0, NULL, &proc_cgroupstats_operations);

out:
	if (err)
		bdi_destroy(&cgroup_backing_dev_info);

	return err;
}

/*
 * proc_cgroup_show()
 *  - Print task's cgroup paths into seq_file, one line for each hierarchy
 *  - Used for /proc/<pid>/cgroup.
 *  - No need to task_lock(tsk) on this tsk->cgroup reference, as it
 *    doesn't really matter if tsk->cgroup changes after we read it,
 *    and we take cgroup_mutex, keeping cgroup_attach_task() from changing it
 *    anyway.  No need to check that tsk->cgroup != NULL, thanks to
 *    the_top_cgroup_hack in cgroup_exit(), which sets an exiting tasks
 *    cgroup to top_cgroup.
 */

/* TODO: Use a proper seq_file iterator */
static int proc_cgroup_show(struct seq_file *m, void *v)
{
	struct pid *pid;
	struct task_struct *tsk;
	char *buf;
	int retval;
	struct cgroupfs_root *root;

	retval = -ENOMEM;
	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		goto out;

	retval = -ESRCH;
	pid = m->private;
	tsk = get_pid_task(pid, PIDTYPE_PID);
	if (!tsk)
		goto out_free;

	retval = 0;

	mutex_lock(&cgroup_mutex);

	for_each_root(root) {
		struct cgroup_subsys *ss;
		struct cgroup *cgrp;
		int subsys_id;
		int count = 0;

		/* Skip this hierarchy if it has no active subsystems */
		if (!root->actual_subsys_bits)
			continue;
		seq_printf(m, "%lu:", root->subsys_bits);
		for_each_subsys(root, ss)
			seq_printf(m, "%s%s", count++ ? "," : "", ss->name);
		seq_putc(m, ':');
		get_first_subsys(&root->top_cgroup, NULL, &subsys_id);
		cgrp = task_cgroup(tsk, subsys_id);
		retval = cgroup_path(cgrp, buf, PAGE_SIZE);
		if (retval < 0)
			goto out_unlock;
		seq_puts(m, buf);
		seq_putc(m, '\n');
	}

out_unlock:
	mutex_unlock(&cgroup_mutex);
	put_task_struct(tsk);
out_free:
	kfree(buf);
out:
	return retval;
}

static int cgroup_open(struct inode *inode, struct file *file)
{
	struct pid *pid = PROC_I(inode)->pid;
	return single_open(file, proc_cgroup_show, pid);
}

struct file_operations proc_cgroup_operations = {
	.open		= cgroup_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/* Display information about each subsystem and each hierarchy */
static int proc_cgroupstats_show(struct seq_file *m, void *v)
{
	int i;

	seq_puts(m, "#subsys_name\thierarchy\tnum_cgroups\tenabled\n");
	mutex_lock(&cgroup_mutex);
	for (i = 0; i < CGROUP_SUBSYS_COUNT; i++) {
		struct cgroup_subsys *ss = subsys[i];
		seq_printf(m, "%s\t%lu\t%d\t%d\n",
			   ss->name, ss->root->subsys_bits,
			   ss->root->number_of_cgroups, !ss->disabled);
	}
	mutex_unlock(&cgroup_mutex);
	return 0;
}

static int cgroupstats_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_cgroupstats_show, NULL);
}

static struct file_operations proc_cgroupstats_operations = {
	.open = cgroupstats_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/**
 * cgroup_fork - attach newly forked task to its parents cgroup.
 * @child: pointer to task_struct of forking parent process.
 *
 * Description: A task inherits its parent's cgroup at fork().
 *
 * A pointer to the shared css_set was automatically copied in
 * fork.c by dup_task_struct().  However, we ignore that copy, since
 * it was not made under the protection of RCU or cgroup_mutex, so
 * might no longer be a valid cgroup pointer.  cgroup_attach_task() might
 * have already changed current->cgroups, allowing the previously
 * referenced cgroup group to be removed and freed.
 *
 * At the point that cgroup_fork() is called, 'current' is the parent
 * task, and the passed argument 'child' points to the child task.
 */
void cgroup_fork(struct task_struct *child)
{
	task_lock(current);
	child->cgroups = current->cgroups;
	get_css_set(child->cgroups);
	task_unlock(current);
	INIT_LIST_HEAD(&child->cg_list);
}

/**
 * cgroup_fork_callbacks - run fork callbacks
 * @child: the new task
 *
 * Called on a new task very soon before adding it to the
 * tasklist. No need to take any locks since no-one can
 * be operating on this task.
 */
void cgroup_fork_callbacks(struct task_struct *child)
{
	if (need_forkexit_callback) {
		int i;
		for (i = 0; i < CGROUP_SUBSYS_COUNT; i++) {
			struct cgroup_subsys *ss = subsys[i];
			if (ss->fork)
				ss->fork(ss, child);
		}
	}
}

/**
 * cgroup_post_fork - called on a new task after adding it to the task list
 * @child: the task in question
 *
 * Adds the task to the list running through its css_set if necessary.
 * Has to be after the task is visible on the task list in case we race
 * with the first call to cgroup_iter_start() - to guarantee that the
 * new task ends up on its list.
 */
void cgroup_post_fork(struct task_struct *child)
{
	if (use_task_css_set_links) {
		write_lock(&css_set_lock);
		if (list_empty(&child->cg_list))
			list_add(&child->cg_list, &child->cgroups->tasks);
		write_unlock(&css_set_lock);
	}
}
/**
 * cgroup_exit - detach cgroup from exiting task
 * @tsk: pointer to task_struct of exiting process
 * @run_callback: run exit callbacks?
 *
 * Description: Detach cgroup from @tsk and release it.
 *
 * Note that cgroups marked notify_on_release force every task in
 * them to take the global cgroup_mutex mutex when exiting.
 * This could impact scaling on very large systems.  Be reluctant to
 * use notify_on_release cgroups where very high task exit scaling
 * is required on large systems.
 *
 * the_top_cgroup_hack:
 *
 *    Set the exiting tasks cgroup to the root cgroup (top_cgroup).
 *
 *    We call cgroup_exit() while the task is still competent to
 *    handle notify_on_release(), then leave the task attached to the
 *    root cgroup in each hierarchy for the remainder of its exit.
 *
 *    To do this properly, we would increment the reference count on
 *    top_cgroup, and near the very end of the kernel/exit.c do_exit()
 *    code we would add a second cgroup function call, to drop that
 *    reference.  This would just create an unnecessary hot spot on
 *    the top_cgroup reference count, to no avail.
 *
 *    Normally, holding a reference to a cgroup without bumping its
 *    count is unsafe.   The cgroup could go away, or someone could
 *    attach us to a different cgroup, decrementing the count on
 *    the first cgroup that we never incremented.  But in this case,
 *    top_cgroup isn't going away, and either task has PF_EXITING set,
 *    which wards off any cgroup_attach_task() attempts, or task is a failed
 *    fork, never visible to cgroup_attach_task.
 */
void cgroup_exit(struct task_struct *tsk, int run_callbacks)
{
	int i;
	struct css_set *cg;

	if (run_callbacks && need_forkexit_callback) {
		for (i = 0; i < CGROUP_SUBSYS_COUNT; i++) {
			struct cgroup_subsys *ss = subsys[i];
			if (ss->exit)
				ss->exit(ss, tsk);
		}
	}

	/*
	 * Unlink from the css_set task list if necessary.
	 * Optimistically check cg_list before taking
	 * css_set_lock
	 */
	if (!list_empty(&tsk->cg_list)) {
		write_lock(&css_set_lock);
		if (!list_empty(&tsk->cg_list))
			list_del(&tsk->cg_list);
		write_unlock(&css_set_lock);
	}

	/* Reassign the task to the init_css_set. */
	task_lock(tsk);
	cg = tsk->cgroups;
	tsk->cgroups = &init_css_set;
	task_unlock(tsk);
	if (cg)
		put_css_set_taskexit(cg);
}

/**
 * cgroup_clone - clone the cgroup the given subsystem is attached to
 * @tsk: the task to be moved
 * @subsys: the given subsystem
 *
 * Duplicate the current cgroup in the hierarchy that the given
 * subsystem is attached to, and move this task into the new
 * child.
 */
int cgroup_clone(struct task_struct *tsk, struct cgroup_subsys *subsys)
{
	struct dentry *dentry;
	int ret = 0;
	char nodename[MAX_CGROUP_TYPE_NAMELEN];
	struct cgroup *parent, *child;
	struct inode *inode;
	struct css_set *cg;
	struct cgroupfs_root *root;
	struct cgroup_subsys *ss;

	/* We shouldn't be called by an unregistered subsystem */
	BUG_ON(!subsys->active);

	/* First figure out what hierarchy and cgroup we're dealing
	 * with, and pin them so we can drop cgroup_mutex */
	mutex_lock(&cgroup_mutex);
 again:
	root = subsys->root;
	if (root == &rootnode) {
		printk(KERN_INFO
		       "Not cloning cgroup for unused subsystem %s\n",
		       subsys->name);
		mutex_unlock(&cgroup_mutex);
		return 0;
	}
	cg = tsk->cgroups;
	parent = task_cgroup(tsk, subsys->subsys_id);

	snprintf(nodename, MAX_CGROUP_TYPE_NAMELEN, "node_%d", tsk->pid);

	/* Pin the hierarchy */
	atomic_inc(&parent->root->sb->s_active);

	/* Keep the cgroup alive */
	get_css_set(cg);
	mutex_unlock(&cgroup_mutex);

	/* Now do the VFS work to create a cgroup */
	inode = parent->dentry->d_inode;

	/* Hold the parent directory mutex across this operation to
	 * stop anyone else deleting the new cgroup */
	mutex_lock(&inode->i_mutex);
	dentry = lookup_one_len(nodename, parent->dentry, strlen(nodename));
	if (IS_ERR(dentry)) {
		printk(KERN_INFO
		       "cgroup: Couldn't allocate dentry for %s: %ld\n", nodename,
		       PTR_ERR(dentry));
		ret = PTR_ERR(dentry);
		goto out_release;
	}

	/* Create the cgroup directory, which also creates the cgroup */
	ret = vfs_mkdir(inode, dentry, S_IFDIR | 0755);
	child = __d_cgrp(dentry);
	dput(dentry);
	if (ret) {
		printk(KERN_INFO
		       "Failed to create cgroup %s: %d\n", nodename,
		       ret);
		goto out_release;
	}

	if (!child) {
		printk(KERN_INFO
		       "Couldn't find new cgroup %s\n", nodename);
		ret = -ENOMEM;
		goto out_release;
	}

	/* The cgroup now exists. Retake cgroup_mutex and check
	 * that we're still in the same state that we thought we
	 * were. */
	mutex_lock(&cgroup_mutex);
	if ((root != subsys->root) ||
	    (parent != task_cgroup(tsk, subsys->subsys_id))) {
		/* Aargh, we raced ... */
		mutex_unlock(&inode->i_mutex);
		put_css_set(cg);

		deactivate_super(parent->root->sb);
		/* The cgroup is still accessible in the VFS, but
		 * we're not going to try to rmdir() it at this
		 * point. */
		printk(KERN_INFO
		       "Race in cgroup_clone() - leaking cgroup %s\n",
		       nodename);
		goto again;
	}

	/* do any required auto-setup */
	for_each_subsys(root, ss) {
		if (ss->post_clone)
			ss->post_clone(ss, child);
	}

	/* All seems fine. Finish by moving the task into the new cgroup */
	ret = cgroup_attach_task(child, tsk);
	mutex_unlock(&cgroup_mutex);

 out_release:
	mutex_unlock(&inode->i_mutex);

	mutex_lock(&cgroup_mutex);
	put_css_set(cg);
	mutex_unlock(&cgroup_mutex);
	deactivate_super(parent->root->sb);
	return ret;
}

/**
 * cgroup_is_descendant - see if @cgrp is a descendant of current task's cgrp
 * @cgrp: the cgroup in question
 *
 * See if @cgrp is a descendant of the current task's cgroup in
 * the appropriate hierarchy.
 *
 * If we are sending in dummytop, then presumably we are creating
 * the top cgroup in the subsystem.
 *
 * Called only by the ns (nsproxy) cgroup.
 */
int cgroup_is_descendant(const struct cgroup *cgrp)
{
	int ret;
	struct cgroup *target;
	int subsys_id;

	if (cgrp == dummytop)
		return 1;

	get_first_subsys(cgrp, NULL, &subsys_id);
	target = task_cgroup(current, subsys_id);
	while (cgrp != target && cgrp!= cgrp->top_cgroup)
		cgrp = cgrp->parent;
	ret = (cgrp == target);
	return ret;
}

static void check_for_release(struct cgroup *cgrp)
{
	/* All of these checks rely on RCU to keep the cgroup
	 * structure alive */
	if (cgroup_is_releasable(cgrp) && !atomic_read(&cgrp->count)
	    && list_empty(&cgrp->children) && !cgroup_has_css_refs(cgrp)) {
		/* Control Group is currently removeable. If it's not
		 * already queued for a userspace notification, queue
		 * it now */
		int need_schedule_work = 0;
		spin_lock(&release_list_lock);
		if (!cgroup_is_removed(cgrp) &&
		    list_empty(&cgrp->release_list)) {
			list_add(&cgrp->release_list, &release_list);
			need_schedule_work = 1;
		}
		spin_unlock(&release_list_lock);
		if (need_schedule_work)
			schedule_work(&release_agent_work);
	}
}

void __css_put(struct cgroup_subsys_state *css)
{
	struct cgroup *cgrp = css->cgroup;
	rcu_read_lock();
	if (atomic_dec_and_test(&css->refcnt) && notify_on_release(cgrp)) {
		set_bit(CGRP_RELEASABLE, &cgrp->flags);
		check_for_release(cgrp);
	}
	rcu_read_unlock();
}

/*
 * Notify userspace when a cgroup is released, by running the
 * configured release agent with the name of the cgroup (path
 * relative to the root of cgroup file system) as the argument.
 *
 * Most likely, this user command will try to rmdir this cgroup.
 *
 * This races with the possibility that some other task will be
 * attached to this cgroup before it is removed, or that some other
 * user task will 'mkdir' a child cgroup of this cgroup.  That's ok.
 * The presumed 'rmdir' will fail quietly if this cgroup is no longer
 * unused, and this cgroup will be reprieved from its death sentence,
 * to continue to serve a useful existence.  Next time it's released,
 * we will get notified again, if it still has 'notify_on_release' set.
 *
 * The final arg to call_usermodehelper() is UMH_WAIT_EXEC, which
 * means only wait until the task is successfully execve()'d.  The
 * separate release agent task is forked by call_usermodehelper(),
 * then control in this thread returns here, without waiting for the
 * release agent task.  We don't bother to wait because the caller of
 * this routine has no use for the exit status of the release agent
 * task, so no sense holding our caller up for that.
 */
static void cgroup_release_agent(struct work_struct *work)
{
	BUG_ON(work != &release_agent_work);
	mutex_lock(&cgroup_mutex);
	spin_lock(&release_list_lock);
	while (!list_empty(&release_list)) {
		char *argv[3], *envp[3];
		int i;
		char *pathbuf;
		struct cgroup *cgrp = list_entry(release_list.next,
						    struct cgroup,
						    release_list);
		list_del_init(&cgrp->release_list);
		spin_unlock(&release_list_lock);
		pathbuf = kmalloc(PAGE_SIZE, GFP_KERNEL);
		if (!pathbuf) {
			spin_lock(&release_list_lock);
			continue;
		}

		if (cgroup_path(cgrp, pathbuf, PAGE_SIZE) < 0) {
			kfree(pathbuf);
			spin_lock(&release_list_lock);
			continue;
		}

		i = 0;
		argv[i++] = cgrp->root->release_agent_path;
		argv[i++] = (char *)pathbuf;
		argv[i] = NULL;

		i = 0;
		/* minimal command environment */
		envp[i++] = "HOME=/";
		envp[i++] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";
		envp[i] = NULL;

		/* Drop the lock while we invoke the usermode helper,
		 * since the exec could involve hitting disk and hence
		 * be a slow process */
		mutex_unlock(&cgroup_mutex);
		call_usermodehelper(argv[0], argv, envp, UMH_WAIT_EXEC);
		kfree(pathbuf);
		mutex_lock(&cgroup_mutex);
		spin_lock(&release_list_lock);
	}
	spin_unlock(&release_list_lock);
	mutex_unlock(&cgroup_mutex);
}

static int __init cgroup_disable(char *str)
{
	int i;
	char *token;

	while ((token = strsep(&str, ",")) != NULL) {
		if (!*token)
			continue;

		for (i = 0; i < CGROUP_SUBSYS_COUNT; i++) {
			struct cgroup_subsys *ss = subsys[i];

			if (!strcmp(token, ss->name)) {
				ss->disabled = 1;
				printk(KERN_INFO "Disabling %s control group"
					" subsystem\n", ss->name);
				break;
			}
		}
	}
	return 1;
}
__setup("cgroup_disable=", cgroup_disable);
