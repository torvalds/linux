/*
 *  Generic process-grouping system.
 *
 *  Based originally on the cpuset system, extracted by Paul Menage
 *  Copyright (C) 2006 Google, Inc
 *
 *  Notifications support
 *  Copyright (C) 2009 Nokia Corporation
 *  Author: Kirill A. Shutemov
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
#include <linux/ctype.h>
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
#include <linux/module.h>
#include <linux/delayacct.h>
#include <linux/cgroupstats.h>
#include <linux/hash.h>
#include <linux/namei.h>
#include <linux/smp_lock.h>
#include <linux/pid_namespace.h>
#include <linux/idr.h>
#include <linux/vmalloc.h> /* TODO: replace with more sophisticated array */
#include <linux/eventfd.h>
#include <linux/poll.h>
#include <linux/capability.h>

#include <asm/atomic.h>

static DEFINE_MUTEX(cgroup_mutex);

/*
 * Generate an array of cgroup subsystem pointers. At boot time, this is
 * populated up to CGROUP_BUILTIN_SUBSYS_COUNT, and modular subsystems are
 * registered after that. The mutable section of this array is protected by
 * cgroup_mutex.
 */
#define SUBSYS(_x) &_x ## _subsys,
static struct cgroup_subsys *subsys[CGROUP_SUBSYS_COUNT] = {
#include <linux/cgroup_subsys.h>
};

#define MAX_CGROUP_ROOT_NAMELEN 64

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

	/* Unique id for this hierarchy. */
	int hierarchy_id;

	/* The bitmask of subsystems currently attached to this hierarchy */
	unsigned long actual_subsys_bits;

	/* A list running through the attached subsystems */
	struct list_head subsys_list;

	/* The root cgroup for this hierarchy */
	struct cgroup top_cgroup;

	/* Tracks how many cgroups are currently defined in hierarchy.*/
	int number_of_cgroups;

	/* A list running through the active hierarchies */
	struct list_head root_list;

	/* Hierarchy-specific flags */
	unsigned long flags;

	/* The path to use for release notifications. */
	char release_agent_path[PATH_MAX];

	/* The name for this hierarchy - may be empty */
	char name[MAX_CGROUP_ROOT_NAMELEN];
};

/*
 * The "rootnode" hierarchy is the "dummy hierarchy", reserved for the
 * subsystems that are otherwise unattached - it never has more than a
 * single cgroup, and all tasks are part of that cgroup.
 */
static struct cgroupfs_root rootnode;

/*
 * CSS ID -- ID per subsys's Cgroup Subsys State(CSS). used only when
 * cgroup_subsys->use_id != 0.
 */
#define CSS_ID_MAX	(65535)
struct css_id {
	/*
	 * The css to which this ID points. This pointer is set to valid value
	 * after cgroup is populated. If cgroup is removed, this will be NULL.
	 * This pointer is expected to be RCU-safe because destroy()
	 * is called after synchronize_rcu(). But for safe use, css_is_removed()
	 * css_tryget() should be used for avoiding race.
	 */
	struct cgroup_subsys_state *css;
	/*
	 * ID of this css.
	 */
	unsigned short id;
	/*
	 * Depth in hierarchy which this ID belongs to.
	 */
	unsigned short depth;
	/*
	 * ID is freed by RCU. (and lookup routine is RCU safe.)
	 */
	struct rcu_head rcu_head;
	/*
	 * Hierarchy of CSS ID belongs to.
	 */
	unsigned short stack[0]; /* Array of Length (depth+1) */
};

/*
 * cgroup_event represents events which userspace want to recieve.
 */
struct cgroup_event {
	/*
	 * Cgroup which the event belongs to.
	 */
	struct cgroup *cgrp;
	/*
	 * Control file which the event associated.
	 */
	struct cftype *cft;
	/*
	 * eventfd to signal userspace about the event.
	 */
	struct eventfd_ctx *eventfd;
	/*
	 * Each of these stored in a list by the cgroup.
	 */
	struct list_head list;
	/*
	 * All fields below needed to unregister event when
	 * userspace closes eventfd.
	 */
	poll_table pt;
	wait_queue_head_t *wqh;
	wait_queue_t wait;
	struct work_struct remove;
};

/* The list of hierarchy roots */

static LIST_HEAD(roots);
static int root_count;

static DEFINE_IDA(hierarchy_ida);
static int next_hierarchy_id;
static DEFINE_SPINLOCK(hierarchy_id_lock);

/* dummytop is a shorthand for the dummy hierarchy's top cgroup */
#define dummytop (&rootnode.top_cgroup)

/* This flag indicates whether tasks in the fork and exit paths should
 * check for fork/exit handlers to call. This avoids us having to do
 * extra work in the fork/exit path if none of the subsystems need to
 * be called.
 */
static int need_forkexit_callback __read_mostly;

#ifdef CONFIG_PROVE_LOCKING
int cgroup_lock_is_held(void)
{
	return lockdep_is_held(&cgroup_mutex);
}
#else /* #ifdef CONFIG_PROVE_LOCKING */
int cgroup_lock_is_held(void)
{
	return mutex_is_locked(&cgroup_mutex);
}
#endif /* #else #ifdef CONFIG_PROVE_LOCKING */

EXPORT_SYMBOL_GPL(cgroup_lock_is_held);

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

/* for_each_active_root() allows you to iterate across the active hierarchies */
#define for_each_active_root(_root) \
list_for_each_entry(_root, &roots, root_list)

/* the list of cgroups eligible for automatic release. Protected by
 * release_list_lock */
static LIST_HEAD(release_list);
static DEFINE_SPINLOCK(release_list_lock);
static void cgroup_release_agent(struct work_struct *work);
static DECLARE_WORK(release_agent_work, cgroup_release_agent);
static void check_for_release(struct cgroup *cgrp);

/*
 * A queue for waiters to do rmdir() cgroup. A tasks will sleep when
 * cgroup->count == 0 && list_empty(&cgroup->children) && subsys has some
 * reference to css->refcnt. In general, this refcnt is expected to goes down
 * to zero, soon.
 *
 * CGRP_WAIT_ON_RMDIR flag is set under cgroup's inode->i_mutex;
 */
DECLARE_WAIT_QUEUE_HEAD(cgroup_rmdir_waitq);

static void cgroup_wakeup_rmdir_waiter(struct cgroup *cgrp)
{
	if (unlikely(test_and_clear_bit(CGRP_WAIT_ON_RMDIR, &cgrp->flags)))
		wake_up_all(&cgroup_rmdir_waitq);
}

void cgroup_exclude_rmdir(struct cgroup_subsys_state *css)
{
	css_get(css);
}

void cgroup_release_and_wakeup_rmdir(struct cgroup_subsys_state *css)
{
	cgroup_wakeup_rmdir_waiter(css->cgroup);
	css_put(css);
}

/* Link structure for associating css_set objects with cgroups */
struct cg_cgroup_link {
	/*
	 * List running through cg_cgroup_links associated with a
	 * cgroup, anchored on cgroup->css_sets
	 */
	struct list_head cgrp_link_list;
	struct cgroup *cgrp;
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

static int cgroup_init_idr(struct cgroup_subsys *ss,
			   struct cgroup_subsys_state *css);

/* css_set_lock protects the list of css_set objects, and the
 * chain of tasks off each css_set.  Nests outside task->alloc_lock
 * due to cgroup_iter_start() */
static DEFINE_RWLOCK(css_set_lock);
static int css_set_count;

/*
 * hash table for cgroup groups. This improves the performance to find
 * an existing css_set. This hash doesn't (currently) take into
 * account cgroups in empty hierarchies.
 */
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

static void free_css_set_work(struct work_struct *work)
{
	struct css_set *cg = container_of(work, struct css_set, work);
	struct cg_cgroup_link *link;
	struct cg_cgroup_link *saved_link;

	write_lock(&css_set_lock);
	list_for_each_entry_safe(link, saved_link, &cg->cg_links,
				 cg_link_list) {
		struct cgroup *cgrp = link->cgrp;
		list_del(&link->cg_link_list);
		list_del(&link->cgrp_link_list);
		if (atomic_dec_and_test(&cgrp->count)) {
			check_for_release(cgrp);
			cgroup_wakeup_rmdir_waiter(cgrp);
		}
		kfree(link);
	}
	write_unlock(&css_set_lock);

	kfree(cg);
}

static void free_css_set_rcu(struct rcu_head *obj)
{
	struct css_set *cg = container_of(obj, struct css_set, rcu_head);

	INIT_WORK(&cg->work, free_css_set_work);
	schedule_work(&cg->work);
}

/* We don't maintain the lists running through each css_set to its
 * task until after the first call to cgroup_iter_start(). This
 * reduces the fork()/exit() overhead for people who have cgroups
 * compiled into their kernel but not actually in use */
static int use_task_css_set_links __read_mostly;

/*
 * refcounted get/put for css_set objects
 */
static inline void get_css_set(struct css_set *cg)
{
	atomic_inc(&cg->refcount);
}

static void put_css_set(struct css_set *cg)
{
	/*
	 * Ensure that the refcount doesn't hit zero while any readers
	 * can see it. Similar to atomic_dec_and_lock(), but for an
	 * rwlock
	 */
	if (atomic_add_unless(&cg->refcount, -1, 1))
		return;
	write_lock(&css_set_lock);
	if (!atomic_dec_and_test(&cg->refcount)) {
		write_unlock(&css_set_lock);
		return;
	}

	hlist_del(&cg->hlist);
	css_set_count--;

	write_unlock(&css_set_lock);
	call_rcu(&cg->rcu_head, free_css_set_rcu);
}

/*
 * compare_css_sets - helper function for find_existing_css_set().
 * @cg: candidate css_set being tested
 * @old_cg: existing css_set for a task
 * @new_cgrp: cgroup that's being entered by the task
 * @template: desired set of css pointers in css_set (pre-calculated)
 *
 * Returns true if "cg" matches "old_cg" except for the hierarchy
 * which "new_cgrp" belongs to, for which it should match "new_cgrp".
 */
static bool compare_css_sets(struct css_set *cg,
			     struct css_set *old_cg,
			     struct cgroup *new_cgrp,
			     struct cgroup_subsys_state *template[])
{
	struct list_head *l1, *l2;

	if (memcmp(template, cg->subsys, sizeof(cg->subsys))) {
		/* Not all subsystems matched */
		return false;
	}

	/*
	 * Compare cgroup pointers in order to distinguish between
	 * different cgroups in heirarchies with no subsystems. We
	 * could get by with just this check alone (and skip the
	 * memcmp above) but on most setups the memcmp check will
	 * avoid the need for this more expensive check on almost all
	 * candidates.
	 */

	l1 = &cg->cg_links;
	l2 = &old_cg->cg_links;
	while (1) {
		struct cg_cgroup_link *cgl1, *cgl2;
		struct cgroup *cg1, *cg2;

		l1 = l1->next;
		l2 = l2->next;
		/* See if we reached the end - both lists are equal length. */
		if (l1 == &cg->cg_links) {
			BUG_ON(l2 != &old_cg->cg_links);
			break;
		} else {
			BUG_ON(l2 == &old_cg->cg_links);
		}
		/* Locate the cgroups associated with these links. */
		cgl1 = list_entry(l1, struct cg_cgroup_link, cg_link_list);
		cgl2 = list_entry(l2, struct cg_cgroup_link, cg_link_list);
		cg1 = cgl1->cgrp;
		cg2 = cgl2->cgrp;
		/* Hierarchies should be linked in the same order. */
		BUG_ON(cg1->root != cg2->root);

		/*
		 * If this hierarchy is the hierarchy of the cgroup
		 * that's changing, then we need to check that this
		 * css_set points to the new cgroup; if it's any other
		 * hierarchy, then this css_set should point to the
		 * same cgroup as the old css_set.
		 */
		if (cg1->root == new_cgrp->root) {
			if (cg1 != new_cgrp)
				return false;
		} else {
			if (cg1 != cg2)
				return false;
		}
	}
	return true;
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

	/*
	 * Build the set of subsystem state objects that we want to see in the
	 * new css_set. while subsystems can change globally, the entries here
	 * won't change, so no need for locking.
	 */
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
		if (!compare_css_sets(cg, oldcg, cgrp, template))
			continue;

		/* This css_set matches what we need */
		return cg;
	}

	/* No existing cgroup group matched */
	return NULL;
}

static void free_cg_links(struct list_head *tmp)
{
	struct cg_cgroup_link *link;
	struct cg_cgroup_link *saved_link;

	list_for_each_entry_safe(link, saved_link, tmp, cgrp_link_list) {
		list_del(&link->cgrp_link_list);
		kfree(link);
	}
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
			free_cg_links(tmp);
			return -ENOMEM;
		}
		list_add(&link->cgrp_link_list, tmp);
	}
	return 0;
}

/**
 * link_css_set - a helper function to link a css_set to a cgroup
 * @tmp_cg_links: cg_cgroup_link objects allocated by allocate_cg_links()
 * @cg: the css_set to be linked
 * @cgrp: the destination cgroup
 */
static void link_css_set(struct list_head *tmp_cg_links,
			 struct css_set *cg, struct cgroup *cgrp)
{
	struct cg_cgroup_link *link;

	BUG_ON(list_empty(tmp_cg_links));
	link = list_first_entry(tmp_cg_links, struct cg_cgroup_link,
				cgrp_link_list);
	link->cg = cg;
	link->cgrp = cgrp;
	atomic_inc(&cgrp->count);
	list_move(&link->cgrp_link_list, &cgrp->css_sets);
	/*
	 * Always add links to the tail of the list so that the list
	 * is sorted by order of hierarchy creation
	 */
	list_add_tail(&link->cg_link_list, &cg->cg_links);
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

	struct list_head tmp_cg_links;

	struct hlist_head *hhead;
	struct cg_cgroup_link *link;

	/* First see if we already have a cgroup group that matches
	 * the desired set */
	read_lock(&css_set_lock);
	res = find_existing_css_set(oldcg, cgrp, template);
	if (res)
		get_css_set(res);
	read_unlock(&css_set_lock);

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

	atomic_set(&res->refcount, 1);
	INIT_LIST_HEAD(&res->cg_links);
	INIT_LIST_HEAD(&res->tasks);
	INIT_HLIST_NODE(&res->hlist);

	/* Copy the set of subsystem state objects generated in
	 * find_existing_css_set() */
	memcpy(res->subsys, template, sizeof(res->subsys));

	write_lock(&css_set_lock);
	/* Add reference counts and links from the new css_set. */
	list_for_each_entry(link, &oldcg->cg_links, cg_link_list) {
		struct cgroup *c = link->cgrp;
		if (c->root == cgrp->root)
			c = cgrp;
		link_css_set(&tmp_cg_links, res, c);
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
 * Return the cgroup for "task" from the given hierarchy. Must be
 * called with cgroup_mutex held.
 */
static struct cgroup *task_cgroup_from_root(struct task_struct *task,
					    struct cgroupfs_root *root)
{
	struct css_set *css;
	struct cgroup *res = NULL;

	BUG_ON(!mutex_is_locked(&cgroup_mutex));
	read_lock(&css_set_lock);
	/*
	 * No need to lock the task - since we hold cgroup_mutex the
	 * task can't change groups, so the only thing that can happen
	 * is that it exits and its css is set back to init_css_set.
	 */
	css = task->cgroups;
	if (css == &init_css_set) {
		res = &root->top_cgroup;
	} else {
		struct cg_cgroup_link *link;
		list_for_each_entry(link, &css->cg_links, cg_link_list) {
			struct cgroup *c = link->cgrp;
			if (c->root == root) {
				res = c;
				break;
			}
		}
	}
	read_unlock(&css_set_lock);
	BUG_ON(!res);
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
 * task->cgroups without the expense of grabbing a system global
 * mutex.  Therefore except as noted below, when dereferencing or, as
 * in cgroup_attach_task(), modifying a task's cgroups pointer we use
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
EXPORT_SYMBOL_GPL(cgroup_lock);

/**
 * cgroup_unlock - release lock on cgroup changes
 *
 * Undo the lock taken in a previous cgroup_lock() call.
 */
void cgroup_unlock(void)
{
	mutex_unlock(&cgroup_mutex);
}
EXPORT_SYMBOL_GPL(cgroup_unlock);

/*
 * A couple of forward declarations required, due to cyclic reference loop:
 * cgroup_mkdir -> cgroup_create -> cgroup_populate_dir ->
 * cgroup_add_file -> cgroup_create_file -> cgroup_dir_inode_operations
 * -> cgroup_mkdir.
 */

static int cgroup_mkdir(struct inode *dir, struct dentry *dentry, int mode);
static int cgroup_rmdir(struct inode *unused_dir, struct dentry *dentry);
static int cgroup_populate_dir(struct cgroup *cgrp);
static const struct inode_operations cgroup_dir_inode_operations;
static const struct file_operations proc_cgroupstats_operations;

static struct backing_dev_info cgroup_backing_dev_info = {
	.name		= "cgroup",
	.capabilities	= BDI_CAP_NO_ACCT_AND_WRITEBACK,
};

static int alloc_css_id(struct cgroup_subsys *ss,
			struct cgroup *parent, struct cgroup *child);

static struct inode *cgroup_new_inode(mode_t mode, struct super_block *sb)
{
	struct inode *inode = new_inode(sb);

	if (inode) {
		inode->i_mode = mode;
		inode->i_uid = current_fsuid();
		inode->i_gid = current_fsgid();
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		inode->i_mapping->backing_dev_info = &cgroup_backing_dev_info;
	}
	return inode;
}

/*
 * Call subsys's pre_destroy handler.
 * This is called before css refcnt check.
 */
static int cgroup_call_pre_destroy(struct cgroup *cgrp)
{
	struct cgroup_subsys *ss;
	int ret = 0;

	for_each_subsys(cgrp->root, ss)
		if (ss->pre_destroy) {
			ret = ss->pre_destroy(ss, cgrp);
			if (ret)
				break;
		}

	return ret;
}

static void free_cgroup_rcu(struct rcu_head *obj)
{
	struct cgroup *cgrp = container_of(obj, struct cgroup, rcu_head);

	kfree(cgrp);
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
		for_each_subsys(cgrp->root, ss)
			ss->destroy(ss, cgrp);

		cgrp->root->number_of_cgroups--;
		mutex_unlock(&cgroup_mutex);

		/*
		 * Drop the active superblock reference that we took when we
		 * created the cgroup
		 */
		deactivate_super(cgrp->root->sb);

		/*
		 * if we're getting rid of the cgroup, refcount should ensure
		 * that there are no pidlists left.
		 */
		BUG_ON(!list_empty(&cgrp->pidlists));

		call_rcu(&cgrp->rcu_head, free_cgroup_rcu);
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

/*
 * Call with cgroup_mutex held. Drops reference counts on modules, including
 * any duplicate ones that parse_cgroupfs_options took. If this function
 * returns an error, no reference counts are touched.
 */
static int rebind_subsystems(struct cgroupfs_root *root,
			      unsigned long final_bits)
{
	unsigned long added_bits, removed_bits;
	struct cgroup *cgrp = &root->top_cgroup;
	int i;

	BUG_ON(!mutex_is_locked(&cgroup_mutex));

	removed_bits = root->actual_subsys_bits & ~final_bits;
	added_bits = final_bits & ~root->actual_subsys_bits;
	/* Check that any added subsystems are currently free */
	for (i = 0; i < CGROUP_SUBSYS_COUNT; i++) {
		unsigned long bit = 1UL << i;
		struct cgroup_subsys *ss = subsys[i];
		if (!(bit & added_bits))
			continue;
		/*
		 * Nobody should tell us to do a subsys that doesn't exist:
		 * parse_cgroupfs_options should catch that case and refcounts
		 * ensure that subsystems won't disappear once selected.
		 */
		BUG_ON(ss == NULL);
		if (ss->root != &rootnode) {
			/* Subsystem isn't free */
			return -EBUSY;
		}
	}

	/* Currently we don't handle adding/removing subsystems when
	 * any child cgroups exist. This is theoretically supportable
	 * but involves complex error handling, so it's being left until
	 * later */
	if (root->number_of_cgroups > 1)
		return -EBUSY;

	/* Process each subsystem */
	for (i = 0; i < CGROUP_SUBSYS_COUNT; i++) {
		struct cgroup_subsys *ss = subsys[i];
		unsigned long bit = 1UL << i;
		if (bit & added_bits) {
			/* We're binding this subsystem to this hierarchy */
			BUG_ON(ss == NULL);
			BUG_ON(cgrp->subsys[i]);
			BUG_ON(!dummytop->subsys[i]);
			BUG_ON(dummytop->subsys[i]->cgroup != dummytop);
			mutex_lock(&ss->hierarchy_mutex);
			cgrp->subsys[i] = dummytop->subsys[i];
			cgrp->subsys[i]->cgroup = cgrp;
			list_move(&ss->sibling, &root->subsys_list);
			ss->root = root;
			if (ss->bind)
				ss->bind(ss, cgrp);
			mutex_unlock(&ss->hierarchy_mutex);
			/* refcount was already taken, and we're keeping it */
		} else if (bit & removed_bits) {
			/* We're removing this subsystem */
			BUG_ON(ss == NULL);
			BUG_ON(cgrp->subsys[i] != dummytop->subsys[i]);
			BUG_ON(cgrp->subsys[i]->cgroup != cgrp);
			mutex_lock(&ss->hierarchy_mutex);
			if (ss->bind)
				ss->bind(ss, dummytop);
			dummytop->subsys[i]->cgroup = dummytop;
			cgrp->subsys[i] = NULL;
			subsys[i]->root = &rootnode;
			list_move(&ss->sibling, &rootnode.subsys_list);
			mutex_unlock(&ss->hierarchy_mutex);
			/* subsystem is now free - drop reference on module */
			module_put(ss->module);
		} else if (bit & final_bits) {
			/* Subsystem state should already exist */
			BUG_ON(ss == NULL);
			BUG_ON(!cgrp->subsys[i]);
			/*
			 * a refcount was taken, but we already had one, so
			 * drop the extra reference.
			 */
			module_put(ss->module);
#ifdef CONFIG_MODULE_UNLOAD
			BUG_ON(ss->module && !module_refcount(ss->module));
#endif
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
	if (strlen(root->name))
		seq_printf(seq, ",name=%s", root->name);
	mutex_unlock(&cgroup_mutex);
	return 0;
}

struct cgroup_sb_opts {
	unsigned long subsys_bits;
	unsigned long flags;
	char *release_agent;
	char *name;
	/* User explicitly requested empty subsystem */
	bool none;

	struct cgroupfs_root *new_root;

};

/*
 * Convert a hierarchy specifier into a bitmask of subsystems and flags. Call
 * with cgroup_mutex held to protect the subsys[] array. This function takes
 * refcounts on subsystems to be used, unless it returns error, in which case
 * no refcounts are taken.
 */
static int parse_cgroupfs_options(char *data, struct cgroup_sb_opts *opts)
{
	char *token, *o = data ?: "all";
	unsigned long mask = (unsigned long)-1;
	int i;
	bool module_pin_failed = false;

	BUG_ON(!mutex_is_locked(&cgroup_mutex));

#ifdef CONFIG_CPUSETS
	mask = ~(1UL << cpuset_subsys_id);
#endif

	memset(opts, 0, sizeof(*opts));

	while ((token = strsep(&o, ",")) != NULL) {
		if (!*token)
			return -EINVAL;
		if (!strcmp(token, "all")) {
			/* Add all non-disabled subsystems */
			opts->subsys_bits = 0;
			for (i = 0; i < CGROUP_SUBSYS_COUNT; i++) {
				struct cgroup_subsys *ss = subsys[i];
				if (ss == NULL)
					continue;
				if (!ss->disabled)
					opts->subsys_bits |= 1ul << i;
			}
		} else if (!strcmp(token, "none")) {
			/* Explicitly have no subsystems */
			opts->none = true;
		} else if (!strcmp(token, "noprefix")) {
			set_bit(ROOT_NOPREFIX, &opts->flags);
		} else if (!strncmp(token, "release_agent=", 14)) {
			/* Specifying two release agents is forbidden */
			if (opts->release_agent)
				return -EINVAL;
			opts->release_agent =
				kstrndup(token + 14, PATH_MAX - 1, GFP_KERNEL);
			if (!opts->release_agent)
				return -ENOMEM;
		} else if (!strncmp(token, "name=", 5)) {
			const char *name = token + 5;
			/* Can't specify an empty name */
			if (!strlen(name))
				return -EINVAL;
			/* Must match [\w.-]+ */
			for (i = 0; i < strlen(name); i++) {
				char c = name[i];
				if (isalnum(c))
					continue;
				if ((c == '.') || (c == '-') || (c == '_'))
					continue;
				return -EINVAL;
			}
			/* Specifying two names is forbidden */
			if (opts->name)
				return -EINVAL;
			opts->name = kstrndup(name,
					      MAX_CGROUP_ROOT_NAMELEN - 1,
					      GFP_KERNEL);
			if (!opts->name)
				return -ENOMEM;
		} else {
			struct cgroup_subsys *ss;
			for (i = 0; i < CGROUP_SUBSYS_COUNT; i++) {
				ss = subsys[i];
				if (ss == NULL)
					continue;
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

	/* Consistency checks */

	/*
	 * Option noprefix was introduced just for backward compatibility
	 * with the old cpuset, so we allow noprefix only if mounting just
	 * the cpuset subsystem.
	 */
	if (test_bit(ROOT_NOPREFIX, &opts->flags) &&
	    (opts->subsys_bits & mask))
		return -EINVAL;


	/* Can't specify "none" and some subsystems */
	if (opts->subsys_bits && opts->none)
		return -EINVAL;

	/*
	 * We either have to specify by name or by subsystems. (So all
	 * empty hierarchies must have a name).
	 */
	if (!opts->subsys_bits && !opts->name)
		return -EINVAL;

	/*
	 * Grab references on all the modules we'll need, so the subsystems
	 * don't dance around before rebind_subsystems attaches them. This may
	 * take duplicate reference counts on a subsystem that's already used,
	 * but rebind_subsystems handles this case.
	 */
	for (i = CGROUP_BUILTIN_SUBSYS_COUNT; i < CGROUP_SUBSYS_COUNT; i++) {
		unsigned long bit = 1UL << i;

		if (!(bit & opts->subsys_bits))
			continue;
		if (!try_module_get(subsys[i]->module)) {
			module_pin_failed = true;
			break;
		}
	}
	if (module_pin_failed) {
		/*
		 * oops, one of the modules was going away. this means that we
		 * raced with a module_delete call, and to the user this is
		 * essentially a "subsystem doesn't exist" case.
		 */
		for (i--; i >= CGROUP_BUILTIN_SUBSYS_COUNT; i--) {
			/* drop refcounts only on the ones we took */
			unsigned long bit = 1UL << i;

			if (!(bit & opts->subsys_bits))
				continue;
			module_put(subsys[i]->module);
		}
		return -ENOENT;
	}

	return 0;
}

static void drop_parsed_module_refcounts(unsigned long subsys_bits)
{
	int i;
	for (i = CGROUP_BUILTIN_SUBSYS_COUNT; i < CGROUP_SUBSYS_COUNT; i++) {
		unsigned long bit = 1UL << i;

		if (!(bit & subsys_bits))
			continue;
		module_put(subsys[i]->module);
	}
}

static int cgroup_remount(struct super_block *sb, int *flags, char *data)
{
	int ret = 0;
	struct cgroupfs_root *root = sb->s_fs_info;
	struct cgroup *cgrp = &root->top_cgroup;
	struct cgroup_sb_opts opts;

	lock_kernel();
	mutex_lock(&cgrp->dentry->d_inode->i_mutex);
	mutex_lock(&cgroup_mutex);

	/* See what subsystems are wanted */
	ret = parse_cgroupfs_options(data, &opts);
	if (ret)
		goto out_unlock;

	/* Don't allow flags or name to change at remount */
	if (opts.flags != root->flags ||
	    (opts.name && strcmp(opts.name, root->name))) {
		ret = -EINVAL;
		drop_parsed_module_refcounts(opts.subsys_bits);
		goto out_unlock;
	}

	ret = rebind_subsystems(root, opts.subsys_bits);
	if (ret) {
		drop_parsed_module_refcounts(opts.subsys_bits);
		goto out_unlock;
	}

	/* (re)populate subsystem files */
	cgroup_populate_dir(cgrp);

	if (opts.release_agent)
		strcpy(root->release_agent_path, opts.release_agent);
 out_unlock:
	kfree(opts.release_agent);
	kfree(opts.name);
	mutex_unlock(&cgroup_mutex);
	mutex_unlock(&cgrp->dentry->d_inode->i_mutex);
	unlock_kernel();
	return ret;
}

static const struct super_operations cgroup_ops = {
	.statfs = simple_statfs,
	.drop_inode = generic_delete_inode,
	.show_options = cgroup_show_options,
	.remount_fs = cgroup_remount,
};

static void init_cgroup_housekeeping(struct cgroup *cgrp)
{
	INIT_LIST_HEAD(&cgrp->sibling);
	INIT_LIST_HEAD(&cgrp->children);
	INIT_LIST_HEAD(&cgrp->css_sets);
	INIT_LIST_HEAD(&cgrp->release_list);
	INIT_LIST_HEAD(&cgrp->pidlists);
	mutex_init(&cgrp->pidlist_mutex);
	INIT_LIST_HEAD(&cgrp->event_list);
	spin_lock_init(&cgrp->event_list_lock);
}

static void init_cgroup_root(struct cgroupfs_root *root)
{
	struct cgroup *cgrp = &root->top_cgroup;
	INIT_LIST_HEAD(&root->subsys_list);
	INIT_LIST_HEAD(&root->root_list);
	root->number_of_cgroups = 1;
	cgrp->root = root;
	cgrp->top_cgroup = cgrp;
	init_cgroup_housekeeping(cgrp);
}

static bool init_root_id(struct cgroupfs_root *root)
{
	int ret = 0;

	do {
		if (!ida_pre_get(&hierarchy_ida, GFP_KERNEL))
			return false;
		spin_lock(&hierarchy_id_lock);
		/* Try to allocate the next unused ID */
		ret = ida_get_new_above(&hierarchy_ida, next_hierarchy_id,
					&root->hierarchy_id);
		if (ret == -ENOSPC)
			/* Try again starting from 0 */
			ret = ida_get_new(&hierarchy_ida, &root->hierarchy_id);
		if (!ret) {
			next_hierarchy_id = root->hierarchy_id + 1;
		} else if (ret != -EAGAIN) {
			/* Can only get here if the 31-bit IDR is full ... */
			BUG_ON(ret);
		}
		spin_unlock(&hierarchy_id_lock);
	} while (ret);
	return true;
}

static int cgroup_test_super(struct super_block *sb, void *data)
{
	struct cgroup_sb_opts *opts = data;
	struct cgroupfs_root *root = sb->s_fs_info;

	/* If we asked for a name then it must match */
	if (opts->name && strcmp(opts->name, root->name))
		return 0;

	/*
	 * If we asked for subsystems (or explicitly for no
	 * subsystems) then they must match
	 */
	if ((opts->subsys_bits || opts->none)
	    && (opts->subsys_bits != root->subsys_bits))
		return 0;

	return 1;
}

static struct cgroupfs_root *cgroup_root_from_opts(struct cgroup_sb_opts *opts)
{
	struct cgroupfs_root *root;

	if (!opts->subsys_bits && !opts->none)
		return NULL;

	root = kzalloc(sizeof(*root), GFP_KERNEL);
	if (!root)
		return ERR_PTR(-ENOMEM);

	if (!init_root_id(root)) {
		kfree(root);
		return ERR_PTR(-ENOMEM);
	}
	init_cgroup_root(root);

	root->subsys_bits = opts->subsys_bits;
	root->flags = opts->flags;
	if (opts->release_agent)
		strcpy(root->release_agent_path, opts->release_agent);
	if (opts->name)
		strcpy(root->name, opts->name);
	return root;
}

static void cgroup_drop_root(struct cgroupfs_root *root)
{
	if (!root)
		return;

	BUG_ON(!root->hierarchy_id);
	spin_lock(&hierarchy_id_lock);
	ida_remove(&hierarchy_ida, root->hierarchy_id);
	spin_unlock(&hierarchy_id_lock);
	kfree(root);
}

static int cgroup_set_super(struct super_block *sb, void *data)
{
	int ret;
	struct cgroup_sb_opts *opts = data;

	/* If we don't have a new root, we can't set up a new sb */
	if (!opts->new_root)
		return -EINVAL;

	BUG_ON(!opts->subsys_bits && !opts->none);

	ret = set_anon_super(sb, NULL);
	if (ret)
		return ret;

	sb->s_fs_info = opts->new_root;
	opts->new_root->sb = sb;

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
	struct cgroupfs_root *root;
	int ret = 0;
	struct super_block *sb;
	struct cgroupfs_root *new_root;

	/* First find the desired set of subsystems */
	mutex_lock(&cgroup_mutex);
	ret = parse_cgroupfs_options(data, &opts);
	mutex_unlock(&cgroup_mutex);
	if (ret)
		goto out_err;

	/*
	 * Allocate a new cgroup root. We may not need it if we're
	 * reusing an existing hierarchy.
	 */
	new_root = cgroup_root_from_opts(&opts);
	if (IS_ERR(new_root)) {
		ret = PTR_ERR(new_root);
		goto drop_modules;
	}
	opts.new_root = new_root;

	/* Locate an existing or new sb for this hierarchy */
	sb = sget(fs_type, cgroup_test_super, cgroup_set_super, &opts);
	if (IS_ERR(sb)) {
		ret = PTR_ERR(sb);
		cgroup_drop_root(opts.new_root);
		goto drop_modules;
	}

	root = sb->s_fs_info;
	BUG_ON(!root);
	if (root == opts.new_root) {
		/* We used the new root structure, so this is a new hierarchy */
		struct list_head tmp_cg_links;
		struct cgroup *root_cgrp = &root->top_cgroup;
		struct inode *inode;
		struct cgroupfs_root *existing_root;
		int i;

		BUG_ON(sb->s_root != NULL);

		ret = cgroup_get_rootdir(sb);
		if (ret)
			goto drop_new_super;
		inode = sb->s_root->d_inode;

		mutex_lock(&inode->i_mutex);
		mutex_lock(&cgroup_mutex);

		if (strlen(root->name)) {
			/* Check for name clashes with existing mounts */
			for_each_active_root(existing_root) {
				if (!strcmp(existing_root->name, root->name)) {
					ret = -EBUSY;
					mutex_unlock(&cgroup_mutex);
					mutex_unlock(&inode->i_mutex);
					goto drop_new_super;
				}
			}
		}

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
			free_cg_links(&tmp_cg_links);
			goto drop_new_super;
		}
		/*
		 * There must be no failure case after here, since rebinding
		 * takes care of subsystems' refcounts, which are explicitly
		 * dropped in the failure exit path.
		 */

		/* EBUSY should be the only error here */
		BUG_ON(ret);

		list_add(&root->root_list, &roots);
		root_count++;

		sb->s_root->d_fsdata = root_cgrp;
		root->top_cgroup.dentry = sb->s_root;

		/* Link the top cgroup in this hierarchy into all
		 * the css_set objects */
		write_lock(&css_set_lock);
		for (i = 0; i < CSS_SET_TABLE_SIZE; i++) {
			struct hlist_head *hhead = &css_set_table[i];
			struct hlist_node *node;
			struct css_set *cg;

			hlist_for_each_entry(cg, node, hhead, hlist)
				link_css_set(&tmp_cg_links, cg, root_cgrp);
		}
		write_unlock(&css_set_lock);

		free_cg_links(&tmp_cg_links);

		BUG_ON(!list_empty(&root_cgrp->sibling));
		BUG_ON(!list_empty(&root_cgrp->children));
		BUG_ON(root->number_of_cgroups != 1);

		cgroup_populate_dir(root_cgrp);
		mutex_unlock(&cgroup_mutex);
		mutex_unlock(&inode->i_mutex);
	} else {
		/*
		 * We re-used an existing hierarchy - the new root (if
		 * any) is not needed
		 */
		cgroup_drop_root(opts.new_root);
		/* no subsys rebinding, so refcounts don't change */
		drop_parsed_module_refcounts(opts.subsys_bits);
	}

	simple_set_mnt(mnt, sb);
	kfree(opts.release_agent);
	kfree(opts.name);
	return 0;

 drop_new_super:
	deactivate_locked_super(sb);
 drop_modules:
	drop_parsed_module_refcounts(opts.subsys_bits);
 out_err:
	kfree(opts.release_agent);
	kfree(opts.name);

	return ret;
}

static void cgroup_kill_sb(struct super_block *sb) {
	struct cgroupfs_root *root = sb->s_fs_info;
	struct cgroup *cgrp = &root->top_cgroup;
	int ret;
	struct cg_cgroup_link *link;
	struct cg_cgroup_link *saved_link;

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

	list_for_each_entry_safe(link, saved_link, &cgrp->css_sets,
				 cgrp_link_list) {
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

	kill_litter_super(sb);
	cgroup_drop_root(root);
}

static struct file_system_type cgroup_fs_type = {
	.name = "cgroup",
	.get_sb = cgroup_get_sb,
	.kill_sb = cgroup_kill_sb,
};

static struct kobject *cgroup_kobj;

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
 * Called with cgroup_mutex held or else with an RCU-protected cgroup
 * reference.  Writes path of cgroup into buf.  Returns 0 on success,
 * -errno on error.
 */
int cgroup_path(const struct cgroup *cgrp, char *buf, int buflen)
{
	char *start;
	struct dentry *dentry = rcu_dereference_check(cgrp->dentry,
						      rcu_read_lock_held() ||
						      cgroup_lock_is_held());

	if (!dentry || cgrp == dummytop) {
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
		int len = dentry->d_name.len;

		if ((start -= len) < buf)
			return -ENAMETOOLONG;
		memcpy(start, dentry->d_name.name, len);
		cgrp = cgrp->parent;
		if (!cgrp)
			break;

		dentry = rcu_dereference_check(cgrp->dentry,
					       rcu_read_lock_held() ||
					       cgroup_lock_is_held());
		if (!cgrp->parent)
			continue;
		if (--start < buf)
			return -ENAMETOOLONG;
		*start = '/';
	}
	memmove(buf, start, buf + buflen - start);
	return 0;
}
EXPORT_SYMBOL_GPL(cgroup_path);

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
	struct cgroup_subsys *ss, *failed_ss = NULL;
	struct cgroup *oldcgrp;
	struct css_set *cg;
	struct css_set *newcg;
	struct cgroupfs_root *root = cgrp->root;

	/* Nothing to do if the task is already in that cgroup */
	oldcgrp = task_cgroup_from_root(tsk, root);
	if (cgrp == oldcgrp)
		return 0;

	for_each_subsys(root, ss) {
		if (ss->can_attach) {
			retval = ss->can_attach(ss, cgrp, tsk, false);
			if (retval) {
				/*
				 * Remember on which subsystem the can_attach()
				 * failed, so that we only call cancel_attach()
				 * against the subsystems whose can_attach()
				 * succeeded. (See below)
				 */
				failed_ss = ss;
				goto out;
			}
		} else if (!capable(CAP_SYS_ADMIN)) {
			const struct cred *cred = current_cred(), *tcred;

			/* No can_attach() - check perms generically */
			tcred = __task_cred(tsk);
			if (cred->euid != tcred->uid &&
			    cred->euid != tcred->suid) {
				return -EACCES;
			}
		}
	}

	task_lock(tsk);
	cg = tsk->cgroups;
	get_css_set(cg);
	task_unlock(tsk);
	/*
	 * Locate or allocate a new css_set for this task,
	 * based on its final set of cgroups
	 */
	newcg = find_css_set(cg, cgrp);
	put_css_set(cg);
	if (!newcg) {
		retval = -ENOMEM;
		goto out;
	}

	task_lock(tsk);
	if (tsk->flags & PF_EXITING) {
		task_unlock(tsk);
		put_css_set(newcg);
		retval = -ESRCH;
		goto out;
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
			ss->attach(ss, cgrp, oldcgrp, tsk, false);
	}
	set_bit(CGRP_RELEASABLE, &cgrp->flags);
	/* put_css_set will not destroy cg until after an RCU grace period */
	put_css_set(cg);

	/*
	 * wake up rmdir() waiter. the rmdir should fail since the cgroup
	 * is no longer empty.
	 */
	cgroup_wakeup_rmdir_waiter(cgrp);
out:
	if (retval) {
		for_each_subsys(root, ss) {
			if (ss == failed_ss)
				/*
				 * This subsystem was the one that failed the
				 * can_attach() check earlier, so we don't need
				 * to call cancel_attach() against it or any
				 * remaining subsystems.
				 */
				break;
			if (ss->cancel_attach)
				ss->cancel_attach(ss, cgrp, tsk, false);
		}
	}
	return retval;
}

/**
 * cgroup_attach_task_all - attach task 'tsk' to all cgroups of task 'from'
 * @from: attach to all cgroups of a given task
 * @tsk: the task to be attached
 */
int cgroup_attach_task_all(struct task_struct *from, struct task_struct *tsk)
{
	struct cgroupfs_root *root;
	int retval = 0;

	cgroup_lock();
	for_each_active_root(root) {
		struct cgroup *from_cg = task_cgroup_from_root(from, root);

		retval = cgroup_attach_task(from_cg, tsk);
		if (retval)
			break;
	}
	cgroup_unlock();

	return retval;
}
EXPORT_SYMBOL_GPL(cgroup_attach_task_all);

/*
 * Attach task with pid 'pid' to cgroup 'cgrp'. Call with cgroup_mutex
 * held. May take task_lock of task
 */
static int attach_task_by_pid(struct cgroup *cgrp, u64 pid)
{
	struct task_struct *tsk;
	int ret;

	if (pid) {
		rcu_read_lock();
		tsk = find_task_by_vpid(pid);
		if (!tsk || tsk->flags & PF_EXITING) {
			rcu_read_unlock();
			return -ESRCH;
		}
		get_task_struct(tsk);
		rcu_read_unlock();
	} else {
		tsk = current;
		get_task_struct(tsk);
	}

	ret = cgroup_attach_task(cgrp, tsk);
	put_task_struct(tsk);
	return ret;
}

static int cgroup_tasks_write(struct cgroup *cgrp, struct cftype *cft, u64 pid)
{
	int ret;
	if (!cgroup_lock_live_group(cgrp))
		return -ENODEV;
	ret = attach_task_by_pid(cgrp, pid);
	cgroup_unlock();
	return ret;
}

/**
 * cgroup_lock_live_group - take cgroup_mutex and check that cgrp is alive.
 * @cgrp: the cgroup to be checked for liveness
 *
 * On success, returns true; the lock should be later released with
 * cgroup_unlock(). On failure returns false with no lock held.
 */
bool cgroup_lock_live_group(struct cgroup *cgrp)
{
	mutex_lock(&cgroup_mutex);
	if (cgroup_is_removed(cgrp)) {
		mutex_unlock(&cgroup_mutex);
		return false;
	}
	return true;
}
EXPORT_SYMBOL_GPL(cgroup_lock_live_group);

static int cgroup_release_agent_write(struct cgroup *cgrp, struct cftype *cft,
				      const char *buffer)
{
	BUILD_BUG_ON(sizeof(cgrp->root->release_agent_path) < PATH_MAX);
	if (!cgroup_lock_live_group(cgrp))
		return -ENODEV;
	strcpy(cgrp->root->release_agent_path, buffer);
	cgroup_unlock();
	return 0;
}

static int cgroup_release_agent_show(struct cgroup *cgrp, struct cftype *cft,
				     struct seq_file *seq)
{
	if (!cgroup_lock_live_group(cgrp))
		return -ENODEV;
	seq_puts(seq, cgrp->root->release_agent_path);
	seq_putc(seq, '\n');
	cgroup_unlock();
	return 0;
}

/* A buffer size big enough for numbers or short strings */
#define CGROUP_LOCAL_BUFFER_SIZE 64

static ssize_t cgroup_write_X64(struct cgroup *cgrp, struct cftype *cft,
				struct file *file,
				const char __user *userbuf,
				size_t nbytes, loff_t *unused_ppos)
{
	char buffer[CGROUP_LOCAL_BUFFER_SIZE];
	int retval = 0;
	char *end;

	if (!nbytes)
		return -EINVAL;
	if (nbytes >= sizeof(buffer))
		return -E2BIG;
	if (copy_from_user(buffer, userbuf, nbytes))
		return -EFAULT;

	buffer[nbytes] = 0;     /* nul-terminate */
	if (cft->write_u64) {
		u64 val = simple_strtoull(strstrip(buffer), &end, 0);
		if (*end)
			return -EINVAL;
		retval = cft->write_u64(cgrp, cft, val);
	} else {
		s64 val = simple_strtoll(strstrip(buffer), &end, 0);
		if (*end)
			return -EINVAL;
		retval = cft->write_s64(cgrp, cft, val);
	}
	if (!retval)
		retval = nbytes;
	return retval;
}

static ssize_t cgroup_write_string(struct cgroup *cgrp, struct cftype *cft,
				   struct file *file,
				   const char __user *userbuf,
				   size_t nbytes, loff_t *unused_ppos)
{
	char local_buffer[CGROUP_LOCAL_BUFFER_SIZE];
	int retval = 0;
	size_t max_bytes = cft->max_write_len;
	char *buffer = local_buffer;

	if (!max_bytes)
		max_bytes = sizeof(local_buffer) - 1;
	if (nbytes >= max_bytes)
		return -E2BIG;
	/* Allocate a dynamic buffer if we need one */
	if (nbytes >= sizeof(local_buffer)) {
		buffer = kmalloc(nbytes + 1, GFP_KERNEL);
		if (buffer == NULL)
			return -ENOMEM;
	}
	if (nbytes && copy_from_user(buffer, userbuf, nbytes)) {
		retval = -EFAULT;
		goto out;
	}

	buffer[nbytes] = 0;     /* nul-terminate */
	retval = cft->write_string(cgrp, cft, strstrip(buffer));
	if (!retval)
		retval = nbytes;
out:
	if (buffer != local_buffer)
		kfree(buffer);
	return retval;
}

static ssize_t cgroup_file_write(struct file *file, const char __user *buf,
						size_t nbytes, loff_t *ppos)
{
	struct cftype *cft = __d_cft(file->f_dentry);
	struct cgroup *cgrp = __d_cgrp(file->f_dentry->d_parent);

	if (cgroup_is_removed(cgrp))
		return -ENODEV;
	if (cft->write)
		return cft->write(cgrp, cft, file, buf, nbytes, ppos);
	if (cft->write_u64 || cft->write_s64)
		return cgroup_write_X64(cgrp, cft, file, buf, nbytes, ppos);
	if (cft->write_string)
		return cgroup_write_string(cgrp, cft, file, buf, nbytes, ppos);
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
	char tmp[CGROUP_LOCAL_BUFFER_SIZE];
	u64 val = cft->read_u64(cgrp, cft);
	int len = sprintf(tmp, "%llu\n", (unsigned long long) val);

	return simple_read_from_buffer(buf, nbytes, ppos, tmp, len);
}

static ssize_t cgroup_read_s64(struct cgroup *cgrp, struct cftype *cft,
			       struct file *file,
			       char __user *buf, size_t nbytes,
			       loff_t *ppos)
{
	char tmp[CGROUP_LOCAL_BUFFER_SIZE];
	s64 val = cft->read_s64(cgrp, cft);
	int len = sprintf(tmp, "%lld\n", (long long) val);

	return simple_read_from_buffer(buf, nbytes, ppos, tmp, len);
}

static ssize_t cgroup_file_read(struct file *file, char __user *buf,
				   size_t nbytes, loff_t *ppos)
{
	struct cftype *cft = __d_cft(file->f_dentry);
	struct cgroup *cgrp = __d_cgrp(file->f_dentry->d_parent);

	if (cgroup_is_removed(cgrp))
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

static int cgroup_seqfile_release(struct inode *inode, struct file *file)
{
	struct seq_file *seq = file->private_data;
	kfree(seq->private);
	return single_release(inode, file);
}

static const struct file_operations cgroup_seqfile_operations = {
	.read = seq_read,
	.write = cgroup_file_write,
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

static const struct file_operations cgroup_file_operations = {
	.read = cgroup_file_read,
	.write = cgroup_file_write,
	.llseek = generic_file_llseek,
	.open = cgroup_file_open,
	.release = cgroup_file_release,
};

static const struct inode_operations cgroup_dir_inode_operations = {
	.lookup = simple_lookup,
	.mkdir = cgroup_mkdir,
	.rmdir = cgroup_rmdir,
	.rename = cgroup_rename,
};

/*
 * Check if a file is a control file
 */
static inline struct cftype *__file_cft(struct file *file)
{
	if (file->f_dentry->d_inode->i_fop != &cgroup_file_operations)
		return ERR_PTR(-EINVAL);
	return __d_cft(file->f_dentry);
}

static int cgroup_create_file(struct dentry *dentry, mode_t mode,
				struct super_block *sb)
{
	static const struct dentry_operations cgroup_dops = {
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
				mode_t mode)
{
	struct dentry *parent;
	int error = 0;

	parent = cgrp->parent->dentry;
	error = cgroup_create_file(dentry, S_IFDIR | mode, cgrp->root->sb);
	if (!error) {
		dentry->d_fsdata = cgrp;
		inc_nlink(parent->d_inode);
		rcu_assign_pointer(cgrp->dentry, dentry);
		dget(dentry);
	}
	dput(dentry);

	return error;
}

/**
 * cgroup_file_mode - deduce file mode of a control file
 * @cft: the control file in question
 *
 * returns cft->mode if ->mode is not 0
 * returns S_IRUGO|S_IWUSR if it has both a read and a write handler
 * returns S_IRUGO if it has only a read handler
 * returns S_IWUSR if it has only a write hander
 */
static mode_t cgroup_file_mode(const struct cftype *cft)
{
	mode_t mode = 0;

	if (cft->mode)
		return cft->mode;

	if (cft->read || cft->read_u64 || cft->read_s64 ||
	    cft->read_map || cft->read_seq_string)
		mode |= S_IRUGO;

	if (cft->write || cft->write_u64 || cft->write_s64 ||
	    cft->write_string || cft->trigger)
		mode |= S_IWUSR;

	return mode;
}

int cgroup_add_file(struct cgroup *cgrp,
		       struct cgroup_subsys *subsys,
		       const struct cftype *cft)
{
	struct dentry *dir = cgrp->dentry;
	struct dentry *dentry;
	int error;
	mode_t mode;

	char name[MAX_CGROUP_TYPE_NAMELEN + MAX_CFTYPE_NAME + 2] = { 0 };
	if (subsys && !test_bit(ROOT_NOPREFIX, &cgrp->root->flags)) {
		strcpy(name, subsys->name);
		strcat(name, ".");
	}
	strcat(name, cft->name);
	BUG_ON(!mutex_is_locked(&dir->d_inode->i_mutex));
	dentry = lookup_one_len(name, dir, strlen(name));
	if (!IS_ERR(dentry)) {
		mode = cgroup_file_mode(cft);
		error = cgroup_create_file(dentry, mode | S_IFREG,
						cgrp->root->sb);
		if (!error)
			dentry->d_fsdata = (void *)cft;
		dput(dentry);
	} else
		error = PTR_ERR(dentry);
	return error;
}
EXPORT_SYMBOL_GPL(cgroup_add_file);

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
EXPORT_SYMBOL_GPL(cgroup_add_files);

/**
 * cgroup_task_count - count the number of tasks in a cgroup.
 * @cgrp: the cgroup in question
 *
 * Return the number of tasks in the cgroup.
 */
int cgroup_task_count(const struct cgroup *cgrp)
{
	int count = 0;
	struct cg_cgroup_link *link;

	read_lock(&css_set_lock);
	list_for_each_entry(link, &cgrp->css_sets, cgrp_link_list) {
		count += atomic_read(&link->cg->refcount);
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
	struct cg_cgroup_link *link;

	/* If the iterator cg is NULL, we have no tasks */
	if (!it->cg_link)
		return NULL;
	res = list_entry(l, struct task_struct, cg_list);
	/* Advance iterator to find next entry */
	l = l->next;
	link = list_entry(it->cg_link, struct cg_cgroup_link, cgrp_link_list);
	if (l == &link->cg->tasks) {
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
 * Stuff for reading the 'tasks'/'procs' files.
 *
 * Reading this file can return large amounts of data if a cgroup has
 * *lots* of attached tasks. So it may need several calls to read(),
 * but we cannot guarantee that the information we produce is correct
 * unless we produce it entirely atomically.
 *
 */

/*
 * The following two functions "fix" the issue where there are more pids
 * than kmalloc will give memory for; in such cases, we use vmalloc/vfree.
 * TODO: replace with a kernel-wide solution to this problem
 */
#define PIDLIST_TOO_LARGE(c) ((c) * sizeof(pid_t) > (PAGE_SIZE * 2))
static void *pidlist_allocate(int count)
{
	if (PIDLIST_TOO_LARGE(count))
		return vmalloc(count * sizeof(pid_t));
	else
		return kmalloc(count * sizeof(pid_t), GFP_KERNEL);
}
static void pidlist_free(void *p)
{
	if (is_vmalloc_addr(p))
		vfree(p);
	else
		kfree(p);
}
static void *pidlist_resize(void *p, int newcount)
{
	void *newlist;
	/* note: if new alloc fails, old p will still be valid either way */
	if (is_vmalloc_addr(p)) {
		newlist = vmalloc(newcount * sizeof(pid_t));
		if (!newlist)
			return NULL;
		memcpy(newlist, p, newcount * sizeof(pid_t));
		vfree(p);
	} else {
		newlist = krealloc(p, newcount * sizeof(pid_t), GFP_KERNEL);
	}
	return newlist;
}

/*
 * pidlist_uniq - given a kmalloc()ed list, strip out all duplicate entries
 * If the new stripped list is sufficiently smaller and there's enough memory
 * to allocate a new buffer, will let go of the unneeded memory. Returns the
 * number of unique elements.
 */
/* is the size difference enough that we should re-allocate the array? */
#define PIDLIST_REALLOC_DIFFERENCE(old, new) ((old) - PAGE_SIZE >= (new))
static int pidlist_uniq(pid_t **p, int length)
{
	int src, dest = 1;
	pid_t *list = *p;
	pid_t *newlist;

	/*
	 * we presume the 0th element is unique, so i starts at 1. trivial
	 * edge cases first; no work needs to be done for either
	 */
	if (length == 0 || length == 1)
		return length;
	/* src and dest walk down the list; dest counts unique elements */
	for (src = 1; src < length; src++) {
		/* find next unique element */
		while (list[src] == list[src-1]) {
			src++;
			if (src == length)
				goto after;
		}
		/* dest always points to where the next unique element goes */
		list[dest] = list[src];
		dest++;
	}
after:
	/*
	 * if the length difference is large enough, we want to allocate a
	 * smaller buffer to save memory. if this fails due to out of memory,
	 * we'll just stay with what we've got.
	 */
	if (PIDLIST_REALLOC_DIFFERENCE(length, dest)) {
		newlist = pidlist_resize(list, dest);
		if (newlist)
			*p = newlist;
	}
	return dest;
}

static int cmppid(const void *a, const void *b)
{
	return *(pid_t *)a - *(pid_t *)b;
}

/*
 * find the appropriate pidlist for our purpose (given procs vs tasks)
 * returns with the lock on that pidlist already held, and takes care
 * of the use count, or returns NULL with no locks held if we're out of
 * memory.
 */
static struct cgroup_pidlist *cgroup_pidlist_find(struct cgroup *cgrp,
						  enum cgroup_filetype type)
{
	struct cgroup_pidlist *l;
	/* don't need task_nsproxy() if we're looking at ourself */
	struct pid_namespace *ns = current->nsproxy->pid_ns;

	/*
	 * We can't drop the pidlist_mutex before taking the l->mutex in case
	 * the last ref-holder is trying to remove l from the list at the same
	 * time. Holding the pidlist_mutex precludes somebody taking whichever
	 * list we find out from under us - compare release_pid_array().
	 */
	mutex_lock(&cgrp->pidlist_mutex);
	list_for_each_entry(l, &cgrp->pidlists, links) {
		if (l->key.type == type && l->key.ns == ns) {
			/* make sure l doesn't vanish out from under us */
			down_write(&l->mutex);
			mutex_unlock(&cgrp->pidlist_mutex);
			return l;
		}
	}
	/* entry not found; create a new one */
	l = kmalloc(sizeof(struct cgroup_pidlist), GFP_KERNEL);
	if (!l) {
		mutex_unlock(&cgrp->pidlist_mutex);
		return l;
	}
	init_rwsem(&l->mutex);
	down_write(&l->mutex);
	l->key.type = type;
	l->key.ns = get_pid_ns(ns);
	l->use_count = 0; /* don't increment here */
	l->list = NULL;
	l->owner = cgrp;
	list_add(&l->links, &cgrp->pidlists);
	mutex_unlock(&cgrp->pidlist_mutex);
	return l;
}

/*
 * Load a cgroup's pidarray with either procs' tgids or tasks' pids
 */
static int pidlist_array_load(struct cgroup *cgrp, enum cgroup_filetype type,
			      struct cgroup_pidlist **lp)
{
	pid_t *array;
	int length;
	int pid, n = 0; /* used for populating the array */
	struct cgroup_iter it;
	struct task_struct *tsk;
	struct cgroup_pidlist *l;

	/*
	 * If cgroup gets more users after we read count, we won't have
	 * enough space - tough.  This race is indistinguishable to the
	 * caller from the case that the additional cgroup users didn't
	 * show up until sometime later on.
	 */
	length = cgroup_task_count(cgrp);
	array = pidlist_allocate(length);
	if (!array)
		return -ENOMEM;
	/* now, populate the array */
	cgroup_iter_start(cgrp, &it);
	while ((tsk = cgroup_iter_next(cgrp, &it))) {
		if (unlikely(n == length))
			break;
		/* get tgid or pid for procs or tasks file respectively */
		if (type == CGROUP_FILE_PROCS)
			pid = task_tgid_vnr(tsk);
		else
			pid = task_pid_vnr(tsk);
		if (pid > 0) /* make sure to only use valid results */
			array[n++] = pid;
	}
	cgroup_iter_end(cgrp, &it);
	length = n;
	/* now sort & (if procs) strip out duplicates */
	sort(array, length, sizeof(pid_t), cmppid, NULL);
	if (type == CGROUP_FILE_PROCS)
		length = pidlist_uniq(&array, length);
	l = cgroup_pidlist_find(cgrp, type);
	if (!l) {
		pidlist_free(array);
		return -ENOMEM;
	}
	/* store array, freeing old if necessary - lock already held */
	pidlist_free(l->list);
	l->list = array;
	l->length = length;
	l->use_count++;
	up_write(&l->mutex);
	*lp = l;
	return 0;
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
	 * Validate dentry by checking the superblock operations,
	 * and make sure it's a directory.
	 */
	if (dentry->d_sb->s_op != &cgroup_ops ||
	    !S_ISDIR(dentry->d_inode->i_mode))
		 goto err;

	ret = 0;
	cgrp = dentry->d_fsdata;

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

err:
	return ret;
}


/*
 * seq_file methods for the tasks/procs files. The seq_file position is the
 * next pid to display; the seq_file iterator is a pointer to the pid
 * in the cgroup->l->list array.
 */

static void *cgroup_pidlist_start(struct seq_file *s, loff_t *pos)
{
	/*
	 * Initially we receive a position value that corresponds to
	 * one more than the last pid shown (or 0 on the first call or
	 * after a seek to the start). Use a binary-search to find the
	 * next pid to display, if any
	 */
	struct cgroup_pidlist *l = s->private;
	int index = 0, pid = *pos;
	int *iter;

	down_read(&l->mutex);
	if (pid) {
		int end = l->length;

		while (index < end) {
			int mid = (index + end) / 2;
			if (l->list[mid] == pid) {
				index = mid;
				break;
			} else if (l->list[mid] <= pid)
				index = mid + 1;
			else
				end = mid;
		}
	}
	/* If we're off the end of the array, we're done */
	if (index >= l->length)
		return NULL;
	/* Update the abstract position to be the actual pid that we found */
	iter = l->list + index;
	*pos = *iter;
	return iter;
}

static void cgroup_pidlist_stop(struct seq_file *s, void *v)
{
	struct cgroup_pidlist *l = s->private;
	up_read(&l->mutex);
}

static void *cgroup_pidlist_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct cgroup_pidlist *l = s->private;
	pid_t *p = v;
	pid_t *end = l->list + l->length;
	/*
	 * Advance to the next pid in the array. If this goes off the
	 * end, we're done
	 */
	p++;
	if (p >= end) {
		return NULL;
	} else {
		*pos = *p;
		return p;
	}
}

static int cgroup_pidlist_show(struct seq_file *s, void *v)
{
	return seq_printf(s, "%d\n", *(int *)v);
}

/*
 * seq_operations functions for iterating on pidlists through seq_file -
 * independent of whether it's tasks or procs
 */
static const struct seq_operations cgroup_pidlist_seq_operations = {
	.start = cgroup_pidlist_start,
	.stop = cgroup_pidlist_stop,
	.next = cgroup_pidlist_next,
	.show = cgroup_pidlist_show,
};

static void cgroup_release_pid_array(struct cgroup_pidlist *l)
{
	/*
	 * the case where we're the last user of this particular pidlist will
	 * have us remove it from the cgroup's list, which entails taking the
	 * mutex. since in pidlist_find the pidlist->lock depends on cgroup->
	 * pidlist_mutex, we have to take pidlist_mutex first.
	 */
	mutex_lock(&l->owner->pidlist_mutex);
	down_write(&l->mutex);
	BUG_ON(!l->use_count);
	if (!--l->use_count) {
		/* we're the last user if refcount is 0; remove and free */
		list_del(&l->links);
		mutex_unlock(&l->owner->pidlist_mutex);
		pidlist_free(l->list);
		put_pid_ns(l->key.ns);
		up_write(&l->mutex);
		kfree(l);
		return;
	}
	mutex_unlock(&l->owner->pidlist_mutex);
	up_write(&l->mutex);
}

static int cgroup_pidlist_release(struct inode *inode, struct file *file)
{
	struct cgroup_pidlist *l;
	if (!(file->f_mode & FMODE_READ))
		return 0;
	/*
	 * the seq_file will only be initialized if the file was opened for
	 * reading; hence we check if it's not null only in that case.
	 */
	l = ((struct seq_file *)file->private_data)->private;
	cgroup_release_pid_array(l);
	return seq_release(inode, file);
}

static const struct file_operations cgroup_pidlist_operations = {
	.read = seq_read,
	.llseek = seq_lseek,
	.write = cgroup_file_write,
	.release = cgroup_pidlist_release,
};

/*
 * The following functions handle opens on a file that displays a pidlist
 * (tasks or procs). Prepare an array of the process/thread IDs of whoever's
 * in the cgroup.
 */
/* helper function for the two below it */
static int cgroup_pidlist_open(struct file *file, enum cgroup_filetype type)
{
	struct cgroup *cgrp = __d_cgrp(file->f_dentry->d_parent);
	struct cgroup_pidlist *l;
	int retval;

	/* Nothing to do for write-only files */
	if (!(file->f_mode & FMODE_READ))
		return 0;

	/* have the array populated */
	retval = pidlist_array_load(cgrp, type, &l);
	if (retval)
		return retval;
	/* configure file information */
	file->f_op = &cgroup_pidlist_operations;

	retval = seq_open(file, &cgroup_pidlist_seq_operations);
	if (retval) {
		cgroup_release_pid_array(l);
		return retval;
	}
	((struct seq_file *)file->private_data)->private = l;
	return 0;
}
static int cgroup_tasks_open(struct inode *unused, struct file *file)
{
	return cgroup_pidlist_open(file, CGROUP_FILE_TASKS);
}
static int cgroup_procs_open(struct inode *unused, struct file *file)
{
	return cgroup_pidlist_open(file, CGROUP_FILE_PROCS);
}

static u64 cgroup_read_notify_on_release(struct cgroup *cgrp,
					    struct cftype *cft)
{
	return notify_on_release(cgrp);
}

static int cgroup_write_notify_on_release(struct cgroup *cgrp,
					  struct cftype *cft,
					  u64 val)
{
	clear_bit(CGRP_RELEASABLE, &cgrp->flags);
	if (val)
		set_bit(CGRP_NOTIFY_ON_RELEASE, &cgrp->flags);
	else
		clear_bit(CGRP_NOTIFY_ON_RELEASE, &cgrp->flags);
	return 0;
}

/*
 * Unregister event and free resources.
 *
 * Gets called from workqueue.
 */
static void cgroup_event_remove(struct work_struct *work)
{
	struct cgroup_event *event = container_of(work, struct cgroup_event,
			remove);
	struct cgroup *cgrp = event->cgrp;

	event->cft->unregister_event(cgrp, event->cft, event->eventfd);

	eventfd_ctx_put(event->eventfd);
	kfree(event);
	dput(cgrp->dentry);
}

/*
 * Gets called on POLLHUP on eventfd when user closes it.
 *
 * Called with wqh->lock held and interrupts disabled.
 */
static int cgroup_event_wake(wait_queue_t *wait, unsigned mode,
		int sync, void *key)
{
	struct cgroup_event *event = container_of(wait,
			struct cgroup_event, wait);
	struct cgroup *cgrp = event->cgrp;
	unsigned long flags = (unsigned long)key;

	if (flags & POLLHUP) {
		__remove_wait_queue(event->wqh, &event->wait);
		spin_lock(&cgrp->event_list_lock);
		list_del(&event->list);
		spin_unlock(&cgrp->event_list_lock);
		/*
		 * We are in atomic context, but cgroup_event_remove() may
		 * sleep, so we have to call it in workqueue.
		 */
		schedule_work(&event->remove);
	}

	return 0;
}

static void cgroup_event_ptable_queue_proc(struct file *file,
		wait_queue_head_t *wqh, poll_table *pt)
{
	struct cgroup_event *event = container_of(pt,
			struct cgroup_event, pt);

	event->wqh = wqh;
	add_wait_queue(wqh, &event->wait);
}

/*
 * Parse input and register new cgroup event handler.
 *
 * Input must be in format '<event_fd> <control_fd> <args>'.
 * Interpretation of args is defined by control file implementation.
 */
static int cgroup_write_event_control(struct cgroup *cgrp, struct cftype *cft,
				      const char *buffer)
{
	struct cgroup_event *event = NULL;
	unsigned int efd, cfd;
	struct file *efile = NULL;
	struct file *cfile = NULL;
	char *endp;
	int ret;

	efd = simple_strtoul(buffer, &endp, 10);
	if (*endp != ' ')
		return -EINVAL;
	buffer = endp + 1;

	cfd = simple_strtoul(buffer, &endp, 10);
	if ((*endp != ' ') && (*endp != '\0'))
		return -EINVAL;
	buffer = endp + 1;

	event = kzalloc(sizeof(*event), GFP_KERNEL);
	if (!event)
		return -ENOMEM;
	event->cgrp = cgrp;
	INIT_LIST_HEAD(&event->list);
	init_poll_funcptr(&event->pt, cgroup_event_ptable_queue_proc);
	init_waitqueue_func_entry(&event->wait, cgroup_event_wake);
	INIT_WORK(&event->remove, cgroup_event_remove);

	efile = eventfd_fget(efd);
	if (IS_ERR(efile)) {
		ret = PTR_ERR(efile);
		goto fail;
	}

	event->eventfd = eventfd_ctx_fileget(efile);
	if (IS_ERR(event->eventfd)) {
		ret = PTR_ERR(event->eventfd);
		goto fail;
	}

	cfile = fget(cfd);
	if (!cfile) {
		ret = -EBADF;
		goto fail;
	}

	/* the process need read permission on control file */
	ret = file_permission(cfile, MAY_READ);
	if (ret < 0)
		goto fail;

	event->cft = __file_cft(cfile);
	if (IS_ERR(event->cft)) {
		ret = PTR_ERR(event->cft);
		goto fail;
	}

	if (!event->cft->register_event || !event->cft->unregister_event) {
		ret = -EINVAL;
		goto fail;
	}

	ret = event->cft->register_event(cgrp, event->cft,
			event->eventfd, buffer);
	if (ret)
		goto fail;

	if (efile->f_op->poll(efile, &event->pt) & POLLHUP) {
		event->cft->unregister_event(cgrp, event->cft, event->eventfd);
		ret = 0;
		goto fail;
	}

	/*
	 * Events should be removed after rmdir of cgroup directory, but before
	 * destroying subsystem state objects. Let's take reference to cgroup
	 * directory dentry to do that.
	 */
	dget(cgrp->dentry);

	spin_lock(&cgrp->event_list_lock);
	list_add(&event->list, &cgrp->event_list);
	spin_unlock(&cgrp->event_list_lock);

	fput(cfile);
	fput(efile);

	return 0;

fail:
	if (cfile)
		fput(cfile);

	if (event && event->eventfd && !IS_ERR(event->eventfd))
		eventfd_ctx_put(event->eventfd);

	if (!IS_ERR_OR_NULL(efile))
		fput(efile);

	kfree(event);

	return ret;
}

/*
 * for the common functions, 'private' gives the type of file
 */
/* for hysterical raisins, we can't put this on the older files */
#define CGROUP_FILE_GENERIC_PREFIX "cgroup."
static struct cftype files[] = {
	{
		.name = "tasks",
		.open = cgroup_tasks_open,
		.write_u64 = cgroup_tasks_write,
		.release = cgroup_pidlist_release,
		.mode = S_IRUGO | S_IWUSR,
	},
	{
		.name = CGROUP_FILE_GENERIC_PREFIX "procs",
		.open = cgroup_procs_open,
		/* .write_u64 = cgroup_procs_write, TODO */
		.release = cgroup_pidlist_release,
		.mode = S_IRUGO,
	},
	{
		.name = "notify_on_release",
		.read_u64 = cgroup_read_notify_on_release,
		.write_u64 = cgroup_write_notify_on_release,
	},
	{
		.name = CGROUP_FILE_GENERIC_PREFIX "event_control",
		.write_string = cgroup_write_event_control,
		.mode = S_IWUGO,
	},
};

static struct cftype cft_release_agent = {
	.name = "release_agent",
	.read_seq_string = cgroup_release_agent_show,
	.write_string = cgroup_release_agent_write,
	.max_write_len = PATH_MAX,
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
	/* This cgroup is ready now */
	for_each_subsys(cgrp->root, ss) {
		struct cgroup_subsys_state *css = cgrp->subsys[ss->subsys_id];
		/*
		 * Update id->css pointer and make this css visible from
		 * CSS ID functions. This pointer will be dereferened
		 * from RCU-read-side without locks.
		 */
		if (css->id)
			rcu_assign_pointer(css->id->css, css);
	}

	return 0;
}

static void init_cgroup_css(struct cgroup_subsys_state *css,
			       struct cgroup_subsys *ss,
			       struct cgroup *cgrp)
{
	css->cgroup = cgrp;
	atomic_set(&css->refcnt, 1);
	css->flags = 0;
	css->id = NULL;
	if (cgrp == dummytop)
		set_bit(CSS_ROOT, &css->flags);
	BUG_ON(cgrp->subsys[ss->subsys_id]);
	cgrp->subsys[ss->subsys_id] = css;
}

static void cgroup_lock_hierarchy(struct cgroupfs_root *root)
{
	/* We need to take each hierarchy_mutex in a consistent order */
	int i;

	/*
	 * No worry about a race with rebind_subsystems that might mess up the
	 * locking order, since both parties are under cgroup_mutex.
	 */
	for (i = 0; i < CGROUP_SUBSYS_COUNT; i++) {
		struct cgroup_subsys *ss = subsys[i];
		if (ss == NULL)
			continue;
		if (ss->root == root)
			mutex_lock(&ss->hierarchy_mutex);
	}
}

static void cgroup_unlock_hierarchy(struct cgroupfs_root *root)
{
	int i;

	for (i = 0; i < CGROUP_SUBSYS_COUNT; i++) {
		struct cgroup_subsys *ss = subsys[i];
		if (ss == NULL)
			continue;
		if (ss->root == root)
			mutex_unlock(&ss->hierarchy_mutex);
	}
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
			     mode_t mode)
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

	init_cgroup_housekeeping(cgrp);

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
		if (ss->use_id) {
			err = alloc_css_id(ss, parent, cgrp);
			if (err)
				goto err_destroy;
		}
		/* At error, ->destroy() callback has to free assigned ID. */
	}

	cgroup_lock_hierarchy(root);
	list_add(&cgrp->sibling, &cgrp->parent->children);
	cgroup_unlock_hierarchy(root);
	root->number_of_cgroups++;

	err = cgroup_create_dir(cgrp, dentry, mode);
	if (err < 0)
		goto err_remove;

	set_bit(CGRP_RELEASABLE, &parent->flags);

	/* The cgroup directory was pre-locked for us */
	BUG_ON(!mutex_is_locked(&cgrp->dentry->d_inode->i_mutex));

	err = cgroup_populate_dir(cgrp);
	/* If err < 0, we have a half-filled directory - oh well ;) */

	mutex_unlock(&cgroup_mutex);
	mutex_unlock(&cgrp->dentry->d_inode->i_mutex);

	return 0;

 err_remove:

	cgroup_lock_hierarchy(root);
	list_del(&cgrp->sibling);
	cgroup_unlock_hierarchy(root);
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

static int cgroup_has_css_refs(struct cgroup *cgrp)
{
	/* Check the reference count on each subsystem. Since we
	 * already established that there are no tasks in the
	 * cgroup, if the css refcount is also 1, then there should
	 * be no outstanding references, so the subsystem is safe to
	 * destroy. We scan across all subsystems rather than using
	 * the per-hierarchy linked list of mounted subsystems since
	 * we can be called via check_for_release() with no
	 * synchronization other than RCU, and the subsystem linked
	 * list isn't RCU-safe */
	int i;
	/*
	 * We won't need to lock the subsys array, because the subsystems
	 * we're concerned about aren't going anywhere since our cgroup root
	 * has a reference on them.
	 */
	for (i = 0; i < CGROUP_SUBSYS_COUNT; i++) {
		struct cgroup_subsys *ss = subsys[i];
		struct cgroup_subsys_state *css;
		/* Skip subsystems not present or not in this hierarchy */
		if (ss == NULL || ss->root != cgrp->root)
			continue;
		css = cgrp->subsys[ss->subsys_id];
		/* When called from check_for_release() it's possible
		 * that by this point the cgroup has been removed
		 * and the css deleted. But a false-positive doesn't
		 * matter, since it can only happen if the cgroup
		 * has been deleted and hence no longer needs the
		 * release agent to be called anyway. */
		if (css && (atomic_read(&css->refcnt) > 1))
			return 1;
	}
	return 0;
}

/*
 * Atomically mark all (or else none) of the cgroup's CSS objects as
 * CSS_REMOVED. Return true on success, or false if the cgroup has
 * busy subsystems. Call with cgroup_mutex held
 */

static int cgroup_clear_css_refs(struct cgroup *cgrp)
{
	struct cgroup_subsys *ss;
	unsigned long flags;
	bool failed = false;
	local_irq_save(flags);
	for_each_subsys(cgrp->root, ss) {
		struct cgroup_subsys_state *css = cgrp->subsys[ss->subsys_id];
		int refcnt;
		while (1) {
			/* We can only remove a CSS with a refcnt==1 */
			refcnt = atomic_read(&css->refcnt);
			if (refcnt > 1) {
				failed = true;
				goto done;
			}
			BUG_ON(!refcnt);
			/*
			 * Drop the refcnt to 0 while we check other
			 * subsystems. This will cause any racing
			 * css_tryget() to spin until we set the
			 * CSS_REMOVED bits or abort
			 */
			if (atomic_cmpxchg(&css->refcnt, refcnt, 0) == refcnt)
				break;
			cpu_relax();
		}
	}
 done:
	for_each_subsys(cgrp->root, ss) {
		struct cgroup_subsys_state *css = cgrp->subsys[ss->subsys_id];
		if (failed) {
			/*
			 * Restore old refcnt if we previously managed
			 * to clear it from 1 to 0
			 */
			if (!atomic_read(&css->refcnt))
				atomic_set(&css->refcnt, 1);
		} else {
			/* Commit the fact that the CSS is removed */
			set_bit(CSS_REMOVED, &css->flags);
		}
	}
	local_irq_restore(flags);
	return !failed;
}

/* checks if all of the css_sets attached to a cgroup have a refcount of 0.
 * Must be called with css_set_lock held */
static int cgroup_css_sets_empty(struct cgroup *cgrp)
{
	struct cg_cgroup_link *link;

	list_for_each_entry(link, &cgrp->css_sets, cgrp_link_list) {
		struct css_set *cg = link->cg;
		if (atomic_read(&cg->refcount) > 0)
			return 0;
	}

	return 1;
}

static int cgroup_rmdir(struct inode *unused_dir, struct dentry *dentry)
{
	struct cgroup *cgrp = dentry->d_fsdata;
	struct dentry *d;
	struct cgroup *parent;
	DEFINE_WAIT(wait);
	struct cgroup_event *event, *tmp;
	int ret;

	/* the vfs holds both inode->i_mutex already */
again:
	mutex_lock(&cgroup_mutex);
	if (!cgroup_css_sets_empty(cgrp)) {
		mutex_unlock(&cgroup_mutex);
		return -EBUSY;
	}
	if (!list_empty(&cgrp->children)) {
		mutex_unlock(&cgroup_mutex);
		return -EBUSY;
	}
	mutex_unlock(&cgroup_mutex);

	/*
	 * In general, subsystem has no css->refcnt after pre_destroy(). But
	 * in racy cases, subsystem may have to get css->refcnt after
	 * pre_destroy() and it makes rmdir return with -EBUSY. This sometimes
	 * make rmdir return -EBUSY too often. To avoid that, we use waitqueue
	 * for cgroup's rmdir. CGRP_WAIT_ON_RMDIR is for synchronizing rmdir
	 * and subsystem's reference count handling. Please see css_get/put
	 * and css_tryget() and cgroup_wakeup_rmdir_waiter() implementation.
	 */
	set_bit(CGRP_WAIT_ON_RMDIR, &cgrp->flags);

	/*
	 * Call pre_destroy handlers of subsys. Notify subsystems
	 * that rmdir() request comes.
	 */
	ret = cgroup_call_pre_destroy(cgrp);
	if (ret) {
		clear_bit(CGRP_WAIT_ON_RMDIR, &cgrp->flags);
		return ret;
	}

	mutex_lock(&cgroup_mutex);
	parent = cgrp->parent;
	if (!cgroup_css_sets_empty(cgrp) || !list_empty(&cgrp->children)) {
		clear_bit(CGRP_WAIT_ON_RMDIR, &cgrp->flags);
		mutex_unlock(&cgroup_mutex);
		return -EBUSY;
	}
	prepare_to_wait(&cgroup_rmdir_waitq, &wait, TASK_INTERRUPTIBLE);
	if (!cgroup_clear_css_refs(cgrp)) {
		mutex_unlock(&cgroup_mutex);
		/*
		 * Because someone may call cgroup_wakeup_rmdir_waiter() before
		 * prepare_to_wait(), we need to check this flag.
		 */
		if (test_bit(CGRP_WAIT_ON_RMDIR, &cgrp->flags))
			schedule();
		finish_wait(&cgroup_rmdir_waitq, &wait);
		clear_bit(CGRP_WAIT_ON_RMDIR, &cgrp->flags);
		if (signal_pending(current))
			return -EINTR;
		goto again;
	}
	/* NO css_tryget() can success after here. */
	finish_wait(&cgroup_rmdir_waitq, &wait);
	clear_bit(CGRP_WAIT_ON_RMDIR, &cgrp->flags);

	spin_lock(&release_list_lock);
	set_bit(CGRP_REMOVED, &cgrp->flags);
	if (!list_empty(&cgrp->release_list))
		list_del(&cgrp->release_list);
	spin_unlock(&release_list_lock);

	cgroup_lock_hierarchy(cgrp->root);
	/* delete this cgroup from parent->children */
	list_del(&cgrp->sibling);
	cgroup_unlock_hierarchy(cgrp->root);

	spin_lock(&cgrp->dentry->d_lock);
	d = dget(cgrp->dentry);
	spin_unlock(&d->d_lock);

	cgroup_d_remove_dir(d);
	dput(d);

	check_for_release(parent);

	/*
	 * Unregister events and notify userspace.
	 * Notify userspace about cgroup removing only after rmdir of cgroup
	 * directory to avoid race between userspace and kernelspace
	 */
	spin_lock(&cgrp->event_list_lock);
	list_for_each_entry_safe(event, tmp, &cgrp->event_list, list) {
		list_del(&event->list);
		remove_wait_queue(event->wqh, &event->wait);
		eventfd_signal(event->eventfd, 1);
		schedule_work(&event->remove);
	}
	spin_unlock(&cgrp->event_list_lock);

	mutex_unlock(&cgroup_mutex);
	return 0;
}

static void __init cgroup_init_subsys(struct cgroup_subsys *ss)
{
	struct cgroup_subsys_state *css;

	printk(KERN_INFO "Initializing cgroup subsys %s\n", ss->name);

	/* Create the top cgroup state for this subsystem */
	list_add(&ss->sibling, &rootnode.subsys_list);
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

	mutex_init(&ss->hierarchy_mutex);
	lockdep_set_class(&ss->hierarchy_mutex, &ss->subsys_key);
	ss->active = 1;

	/* this function shouldn't be used with modular subsystems, since they
	 * need to register a subsys_id, among other things */
	BUG_ON(ss->module);
}

/**
 * cgroup_load_subsys: load and register a modular subsystem at runtime
 * @ss: the subsystem to load
 *
 * This function should be called in a modular subsystem's initcall. If the
 * subsystem is built as a module, it will be assigned a new subsys_id and set
 * up for use. If the subsystem is built-in anyway, work is delegated to the
 * simpler cgroup_init_subsys.
 */
int __init_or_module cgroup_load_subsys(struct cgroup_subsys *ss)
{
	int i;
	struct cgroup_subsys_state *css;

	/* check name and function validity */
	if (ss->name == NULL || strlen(ss->name) > MAX_CGROUP_TYPE_NAMELEN ||
	    ss->create == NULL || ss->destroy == NULL)
		return -EINVAL;

	/*
	 * we don't support callbacks in modular subsystems. this check is
	 * before the ss->module check for consistency; a subsystem that could
	 * be a module should still have no callbacks even if the user isn't
	 * compiling it as one.
	 */
	if (ss->fork || ss->exit)
		return -EINVAL;

	/*
	 * an optionally modular subsystem is built-in: we want to do nothing,
	 * since cgroup_init_subsys will have already taken care of it.
	 */
	if (ss->module == NULL) {
		/* a few sanity checks */
		BUG_ON(ss->subsys_id >= CGROUP_BUILTIN_SUBSYS_COUNT);
		BUG_ON(subsys[ss->subsys_id] != ss);
		return 0;
	}

	/*
	 * need to register a subsys id before anything else - for example,
	 * init_cgroup_css needs it.
	 */
	mutex_lock(&cgroup_mutex);
	/* find the first empty slot in the array */
	for (i = CGROUP_BUILTIN_SUBSYS_COUNT; i < CGROUP_SUBSYS_COUNT; i++) {
		if (subsys[i] == NULL)
			break;
	}
	if (i == CGROUP_SUBSYS_COUNT) {
		/* maximum number of subsystems already registered! */
		mutex_unlock(&cgroup_mutex);
		return -EBUSY;
	}
	/* assign ourselves the subsys_id */
	ss->subsys_id = i;
	subsys[i] = ss;

	/*
	 * no ss->create seems to need anything important in the ss struct, so
	 * this can happen first (i.e. before the rootnode attachment).
	 */
	css = ss->create(ss, dummytop);
	if (IS_ERR(css)) {
		/* failure case - need to deassign the subsys[] slot. */
		subsys[i] = NULL;
		mutex_unlock(&cgroup_mutex);
		return PTR_ERR(css);
	}

	list_add(&ss->sibling, &rootnode.subsys_list);
	ss->root = &rootnode;

	/* our new subsystem will be attached to the dummy hierarchy. */
	init_cgroup_css(css, ss, dummytop);
	/* init_idr must be after init_cgroup_css because it sets css->id. */
	if (ss->use_id) {
		int ret = cgroup_init_idr(ss, css);
		if (ret) {
			dummytop->subsys[ss->subsys_id] = NULL;
			ss->destroy(ss, dummytop);
			subsys[i] = NULL;
			mutex_unlock(&cgroup_mutex);
			return ret;
		}
	}

	/*
	 * Now we need to entangle the css into the existing css_sets. unlike
	 * in cgroup_init_subsys, there are now multiple css_sets, so each one
	 * will need a new pointer to it; done by iterating the css_set_table.
	 * furthermore, modifying the existing css_sets will corrupt the hash
	 * table state, so each changed css_set will need its hash recomputed.
	 * this is all done under the css_set_lock.
	 */
	write_lock(&css_set_lock);
	for (i = 0; i < CSS_SET_TABLE_SIZE; i++) {
		struct css_set *cg;
		struct hlist_node *node, *tmp;
		struct hlist_head *bucket = &css_set_table[i], *new_bucket;

		hlist_for_each_entry_safe(cg, node, tmp, bucket, hlist) {
			/* skip entries that we already rehashed */
			if (cg->subsys[ss->subsys_id])
				continue;
			/* remove existing entry */
			hlist_del(&cg->hlist);
			/* set new value */
			cg->subsys[ss->subsys_id] = css;
			/* recompute hash and restore entry */
			new_bucket = css_set_hash(cg->subsys);
			hlist_add_head(&cg->hlist, new_bucket);
		}
	}
	write_unlock(&css_set_lock);

	mutex_init(&ss->hierarchy_mutex);
	lockdep_set_class(&ss->hierarchy_mutex, &ss->subsys_key);
	ss->active = 1;

	/* success! */
	mutex_unlock(&cgroup_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(cgroup_load_subsys);

/**
 * cgroup_unload_subsys: unload a modular subsystem
 * @ss: the subsystem to unload
 *
 * This function should be called in a modular subsystem's exitcall. When this
 * function is invoked, the refcount on the subsystem's module will be 0, so
 * the subsystem will not be attached to any hierarchy.
 */
void cgroup_unload_subsys(struct cgroup_subsys *ss)
{
	struct cg_cgroup_link *link;
	struct hlist_head *hhead;

	BUG_ON(ss->module == NULL);

	/*
	 * we shouldn't be called if the subsystem is in use, and the use of
	 * try_module_get in parse_cgroupfs_options should ensure that it
	 * doesn't start being used while we're killing it off.
	 */
	BUG_ON(ss->root != &rootnode);

	mutex_lock(&cgroup_mutex);
	/* deassign the subsys_id */
	BUG_ON(ss->subsys_id < CGROUP_BUILTIN_SUBSYS_COUNT);
	subsys[ss->subsys_id] = NULL;

	/* remove subsystem from rootnode's list of subsystems */
	list_del(&ss->sibling);

	/*
	 * disentangle the css from all css_sets attached to the dummytop. as
	 * in loading, we need to pay our respects to the hashtable gods.
	 */
	write_lock(&css_set_lock);
	list_for_each_entry(link, &dummytop->css_sets, cgrp_link_list) {
		struct css_set *cg = link->cg;

		hlist_del(&cg->hlist);
		BUG_ON(!cg->subsys[ss->subsys_id]);
		cg->subsys[ss->subsys_id] = NULL;
		hhead = css_set_hash(cg->subsys);
		hlist_add_head(&cg->hlist, hhead);
	}
	write_unlock(&css_set_lock);

	/*
	 * remove subsystem's css from the dummytop and free it - need to free
	 * before marking as null because ss->destroy needs the cgrp->subsys
	 * pointer to find their state. note that this also takes care of
	 * freeing the css_id.
	 */
	ss->destroy(ss, dummytop);
	dummytop->subsys[ss->subsys_id] = NULL;

	mutex_unlock(&cgroup_mutex);
}
EXPORT_SYMBOL_GPL(cgroup_unload_subsys);

/**
 * cgroup_init_early - cgroup initialization at system boot
 *
 * Initialize cgroups at system boot, and initialize any
 * subsystems that request early init.
 */
int __init cgroup_init_early(void)
{
	int i;
	atomic_set(&init_css_set.refcount, 1);
	INIT_LIST_HEAD(&init_css_set.cg_links);
	INIT_LIST_HEAD(&init_css_set.tasks);
	INIT_HLIST_NODE(&init_css_set.hlist);
	css_set_count = 1;
	init_cgroup_root(&rootnode);
	root_count = 1;
	init_task.cgroups = &init_css_set;

	init_css_set_link.cg = &init_css_set;
	init_css_set_link.cgrp = dummytop;
	list_add(&init_css_set_link.cgrp_link_list,
		 &rootnode.top_cgroup.css_sets);
	list_add(&init_css_set_link.cg_link_list,
		 &init_css_set.cg_links);

	for (i = 0; i < CSS_SET_TABLE_SIZE; i++)
		INIT_HLIST_HEAD(&css_set_table[i]);

	/* at bootup time, we don't worry about modular subsystems */
	for (i = 0; i < CGROUP_BUILTIN_SUBSYS_COUNT; i++) {
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

	/* at bootup time, we don't worry about modular subsystems */
	for (i = 0; i < CGROUP_BUILTIN_SUBSYS_COUNT; i++) {
		struct cgroup_subsys *ss = subsys[i];
		if (!ss->early_init)
			cgroup_init_subsys(ss);
		if (ss->use_id)
			cgroup_init_idr(ss, init_css_set.subsys[ss->subsys_id]);
	}

	/* Add init_css_set to the hash table */
	hhead = css_set_hash(init_css_set.subsys);
	hlist_add_head(&init_css_set.hlist, hhead);
	BUG_ON(!init_root_id(&rootnode));

	cgroup_kobj = kobject_create_and_add("cgroup", fs_kobj);
	if (!cgroup_kobj) {
		err = -ENOMEM;
		goto out;
	}

	err = register_filesystem(&cgroup_fs_type);
	if (err < 0) {
		kobject_put(cgroup_kobj);
		goto out;
	}

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

	for_each_active_root(root) {
		struct cgroup_subsys *ss;
		struct cgroup *cgrp;
		int count = 0;

		seq_printf(m, "%d:", root->hierarchy_id);
		for_each_subsys(root, ss)
			seq_printf(m, "%s%s", count++ ? "," : "", ss->name);
		if (strlen(root->name))
			seq_printf(m, "%sname=%s", count ? "," : "",
				   root->name);
		seq_putc(m, ':');
		cgrp = task_cgroup_from_root(tsk, root);
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

const struct file_operations proc_cgroup_operations = {
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
	/*
	 * ideally we don't want subsystems moving around while we do this.
	 * cgroup_mutex is also necessary to guarantee an atomic snapshot of
	 * subsys/hierarchy state.
	 */
	mutex_lock(&cgroup_mutex);
	for (i = 0; i < CGROUP_SUBSYS_COUNT; i++) {
		struct cgroup_subsys *ss = subsys[i];
		if (ss == NULL)
			continue;
		seq_printf(m, "%s\t%d\t%d\t%d\n",
			   ss->name, ss->root->hierarchy_id,
			   ss->root->number_of_cgroups, !ss->disabled);
	}
	mutex_unlock(&cgroup_mutex);
	return 0;
}

static int cgroupstats_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_cgroupstats_show, NULL);
}

static const struct file_operations proc_cgroupstats_operations = {
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
		/*
		 * forkexit callbacks are only supported for builtin
		 * subsystems, and the builtin section of the subsys array is
		 * immutable, so we don't need to lock the subsys array here.
		 */
		for (i = 0; i < CGROUP_BUILTIN_SUBSYS_COUNT; i++) {
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
		task_lock(child);
		if (list_empty(&child->cg_list))
			list_add(&child->cg_list, &child->cgroups->tasks);
		task_unlock(child);
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
		/*
		 * modular subsystems can't use callbacks, so no need to lock
		 * the subsys array
		 */
		for (i = 0; i < CGROUP_BUILTIN_SUBSYS_COUNT; i++) {
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
			list_del_init(&tsk->cg_list);
		write_unlock(&css_set_lock);
	}

	/* Reassign the task to the init_css_set. */
	task_lock(tsk);
	cg = tsk->cgroups;
	tsk->cgroups = &init_css_set;
	task_unlock(tsk);
	if (cg)
		put_css_set(cg);
}

/**
 * cgroup_clone - clone the cgroup the given subsystem is attached to
 * @tsk: the task to be moved
 * @subsys: the given subsystem
 * @nodename: the name for the new cgroup
 *
 * Duplicate the current cgroup in the hierarchy that the given
 * subsystem is attached to, and move this task into the new
 * child.
 */
int cgroup_clone(struct task_struct *tsk, struct cgroup_subsys *subsys,
							char *nodename)
{
	struct dentry *dentry;
	int ret = 0;
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
		mutex_unlock(&cgroup_mutex);
		return 0;
	}

	/* Pin the hierarchy */
	if (!atomic_inc_not_zero(&root->sb->s_active)) {
		/* We race with the final deactivate_super() */
		mutex_unlock(&cgroup_mutex);
		return 0;
	}

	/* Keep the cgroup alive */
	task_lock(tsk);
	parent = task_cgroup(tsk, subsys->subsys_id);
	cg = tsk->cgroups;
	get_css_set(cg);
	task_unlock(tsk);

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
	ret = vfs_mkdir(inode, dentry, 0755);
	child = __d_cgrp(dentry);
	dput(dentry);
	if (ret) {
		printk(KERN_INFO
		       "Failed to create cgroup %s: %d\n", nodename,
		       ret);
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

		deactivate_super(root->sb);
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
	deactivate_super(root->sb);
	return ret;
}

/**
 * cgroup_is_descendant - see if @cgrp is a descendant of @task's cgrp
 * @cgrp: the cgroup in question
 * @task: the task in question
 *
 * See if @cgrp is a descendant of @task's cgroup in the appropriate
 * hierarchy.
 *
 * If we are sending in dummytop, then presumably we are creating
 * the top cgroup in the subsystem.
 *
 * Called only by the ns (nsproxy) cgroup.
 */
int cgroup_is_descendant(const struct cgroup *cgrp, struct task_struct *task)
{
	int ret;
	struct cgroup *target;

	if (cgrp == dummytop)
		return 1;

	target = task_cgroup_from_root(task, cgrp->root);
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

/* Caller must verify that the css is not for root cgroup */
void __css_get(struct cgroup_subsys_state *css, int count)
{
	atomic_add(count, &css->refcnt);
	set_bit(CGRP_RELEASABLE, &css->cgroup->flags);
}
EXPORT_SYMBOL_GPL(__css_get);

/* Caller must verify that the css is not for root cgroup */
void __css_put(struct cgroup_subsys_state *css, int count)
{
	struct cgroup *cgrp = css->cgroup;
	int val;
	rcu_read_lock();
	val = atomic_sub_return(count, &css->refcnt);
	if (val == 1) {
		check_for_release(cgrp);
		cgroup_wakeup_rmdir_waiter(cgrp);
	}
	rcu_read_unlock();
	WARN_ON_ONCE(val < 1);
}
EXPORT_SYMBOL_GPL(__css_put);

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
		char *pathbuf = NULL, *agentbuf = NULL;
		struct cgroup *cgrp = list_entry(release_list.next,
						    struct cgroup,
						    release_list);
		list_del_init(&cgrp->release_list);
		spin_unlock(&release_list_lock);
		pathbuf = kmalloc(PAGE_SIZE, GFP_KERNEL);
		if (!pathbuf)
			goto continue_free;
		if (cgroup_path(cgrp, pathbuf, PAGE_SIZE) < 0)
			goto continue_free;
		agentbuf = kstrdup(cgrp->root->release_agent_path, GFP_KERNEL);
		if (!agentbuf)
			goto continue_free;

		i = 0;
		argv[i++] = agentbuf;
		argv[i++] = pathbuf;
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
		mutex_lock(&cgroup_mutex);
 continue_free:
		kfree(pathbuf);
		kfree(agentbuf);
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
		/*
		 * cgroup_disable, being at boot time, can't know about module
		 * subsystems, so we don't worry about them.
		 */
		for (i = 0; i < CGROUP_BUILTIN_SUBSYS_COUNT; i++) {
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

/*
 * Functons for CSS ID.
 */

/*
 *To get ID other than 0, this should be called when !cgroup_is_removed().
 */
unsigned short css_id(struct cgroup_subsys_state *css)
{
	struct css_id *cssid;

	/*
	 * This css_id() can return correct value when somone has refcnt
	 * on this or this is under rcu_read_lock(). Once css->id is allocated,
	 * it's unchanged until freed.
	 */
	cssid = rcu_dereference_check(css->id,
			rcu_read_lock_held() || atomic_read(&css->refcnt));

	if (cssid)
		return cssid->id;
	return 0;
}
EXPORT_SYMBOL_GPL(css_id);

unsigned short css_depth(struct cgroup_subsys_state *css)
{
	struct css_id *cssid;

	cssid = rcu_dereference_check(css->id,
			rcu_read_lock_held() || atomic_read(&css->refcnt));

	if (cssid)
		return cssid->depth;
	return 0;
}
EXPORT_SYMBOL_GPL(css_depth);

/**
 *  css_is_ancestor - test "root" css is an ancestor of "child"
 * @child: the css to be tested.
 * @root: the css supporsed to be an ancestor of the child.
 *
 * Returns true if "root" is an ancestor of "child" in its hierarchy. Because
 * this function reads css->id, this use rcu_dereference() and rcu_read_lock().
 * But, considering usual usage, the csses should be valid objects after test.
 * Assuming that the caller will do some action to the child if this returns
 * returns true, the caller must take "child";s reference count.
 * If "child" is valid object and this returns true, "root" is valid, too.
 */

bool css_is_ancestor(struct cgroup_subsys_state *child,
		    const struct cgroup_subsys_state *root)
{
	struct css_id *child_id;
	struct css_id *root_id;
	bool ret = true;

	rcu_read_lock();
	child_id  = rcu_dereference(child->id);
	root_id = rcu_dereference(root->id);
	if (!child_id
	    || !root_id
	    || (child_id->depth < root_id->depth)
	    || (child_id->stack[root_id->depth] != root_id->id))
		ret = false;
	rcu_read_unlock();
	return ret;
}

static void __free_css_id_cb(struct rcu_head *head)
{
	struct css_id *id;

	id = container_of(head, struct css_id, rcu_head);
	kfree(id);
}

void free_css_id(struct cgroup_subsys *ss, struct cgroup_subsys_state *css)
{
	struct css_id *id = css->id;
	/* When this is called before css_id initialization, id can be NULL */
	if (!id)
		return;

	BUG_ON(!ss->use_id);

	rcu_assign_pointer(id->css, NULL);
	rcu_assign_pointer(css->id, NULL);
	spin_lock(&ss->id_lock);
	idr_remove(&ss->idr, id->id);
	spin_unlock(&ss->id_lock);
	call_rcu(&id->rcu_head, __free_css_id_cb);
}
EXPORT_SYMBOL_GPL(free_css_id);

/*
 * This is called by init or create(). Then, calls to this function are
 * always serialized (By cgroup_mutex() at create()).
 */

static struct css_id *get_new_cssid(struct cgroup_subsys *ss, int depth)
{
	struct css_id *newid;
	int myid, error, size;

	BUG_ON(!ss->use_id);

	size = sizeof(*newid) + sizeof(unsigned short) * (depth + 1);
	newid = kzalloc(size, GFP_KERNEL);
	if (!newid)
		return ERR_PTR(-ENOMEM);
	/* get id */
	if (unlikely(!idr_pre_get(&ss->idr, GFP_KERNEL))) {
		error = -ENOMEM;
		goto err_out;
	}
	spin_lock(&ss->id_lock);
	/* Don't use 0. allocates an ID of 1-65535 */
	error = idr_get_new_above(&ss->idr, newid, 1, &myid);
	spin_unlock(&ss->id_lock);

	/* Returns error when there are no free spaces for new ID.*/
	if (error) {
		error = -ENOSPC;
		goto err_out;
	}
	if (myid > CSS_ID_MAX)
		goto remove_idr;

	newid->id = myid;
	newid->depth = depth;
	return newid;
remove_idr:
	error = -ENOSPC;
	spin_lock(&ss->id_lock);
	idr_remove(&ss->idr, myid);
	spin_unlock(&ss->id_lock);
err_out:
	kfree(newid);
	return ERR_PTR(error);

}

static int __init_or_module cgroup_init_idr(struct cgroup_subsys *ss,
					    struct cgroup_subsys_state *rootcss)
{
	struct css_id *newid;

	spin_lock_init(&ss->id_lock);
	idr_init(&ss->idr);

	newid = get_new_cssid(ss, 0);
	if (IS_ERR(newid))
		return PTR_ERR(newid);

	newid->stack[0] = newid->id;
	newid->css = rootcss;
	rootcss->id = newid;
	return 0;
}

static int alloc_css_id(struct cgroup_subsys *ss, struct cgroup *parent,
			struct cgroup *child)
{
	int subsys_id, i, depth = 0;
	struct cgroup_subsys_state *parent_css, *child_css;
	struct css_id *child_id, *parent_id;

	subsys_id = ss->subsys_id;
	parent_css = parent->subsys[subsys_id];
	child_css = child->subsys[subsys_id];
	parent_id = parent_css->id;
	depth = parent_id->depth + 1;

	child_id = get_new_cssid(ss, depth);
	if (IS_ERR(child_id))
		return PTR_ERR(child_id);

	for (i = 0; i < depth; i++)
		child_id->stack[i] = parent_id->stack[i];
	child_id->stack[depth] = child_id->id;
	/*
	 * child_id->css pointer will be set after this cgroup is available
	 * see cgroup_populate_dir()
	 */
	rcu_assign_pointer(child_css->id, child_id);

	return 0;
}

/**
 * css_lookup - lookup css by id
 * @ss: cgroup subsys to be looked into.
 * @id: the id
 *
 * Returns pointer to cgroup_subsys_state if there is valid one with id.
 * NULL if not. Should be called under rcu_read_lock()
 */
struct cgroup_subsys_state *css_lookup(struct cgroup_subsys *ss, int id)
{
	struct css_id *cssid = NULL;

	BUG_ON(!ss->use_id);
	cssid = idr_find(&ss->idr, id);

	if (unlikely(!cssid))
		return NULL;

	return rcu_dereference(cssid->css);
}
EXPORT_SYMBOL_GPL(css_lookup);

/**
 * css_get_next - lookup next cgroup under specified hierarchy.
 * @ss: pointer to subsystem
 * @id: current position of iteration.
 * @root: pointer to css. search tree under this.
 * @foundid: position of found object.
 *
 * Search next css under the specified hierarchy of rootid. Calling under
 * rcu_read_lock() is necessary. Returns NULL if it reaches the end.
 */
struct cgroup_subsys_state *
css_get_next(struct cgroup_subsys *ss, int id,
	     struct cgroup_subsys_state *root, int *foundid)
{
	struct cgroup_subsys_state *ret = NULL;
	struct css_id *tmp;
	int tmpid;
	int rootid = css_id(root);
	int depth = css_depth(root);

	if (!rootid)
		return NULL;

	BUG_ON(!ss->use_id);
	/* fill start point for scan */
	tmpid = id;
	while (1) {
		/*
		 * scan next entry from bitmap(tree), tmpid is updated after
		 * idr_get_next().
		 */
		spin_lock(&ss->id_lock);
		tmp = idr_get_next(&ss->idr, &tmpid);
		spin_unlock(&ss->id_lock);

		if (!tmp)
			break;
		if (tmp->depth >= depth && tmp->stack[depth] == rootid) {
			ret = rcu_dereference(tmp->css);
			if (ret) {
				*foundid = tmpid;
				break;
			}
		}
		/* continue to scan from next id */
		tmpid = tmpid + 1;
	}
	return ret;
}

#ifdef CONFIG_CGROUP_DEBUG
static struct cgroup_subsys_state *debug_create(struct cgroup_subsys *ss,
						   struct cgroup *cont)
{
	struct cgroup_subsys_state *css = kzalloc(sizeof(*css), GFP_KERNEL);

	if (!css)
		return ERR_PTR(-ENOMEM);

	return css;
}

static void debug_destroy(struct cgroup_subsys *ss, struct cgroup *cont)
{
	kfree(cont->subsys[debug_subsys_id]);
}

static u64 cgroup_refcount_read(struct cgroup *cont, struct cftype *cft)
{
	return atomic_read(&cont->count);
}

static u64 debug_taskcount_read(struct cgroup *cont, struct cftype *cft)
{
	return cgroup_task_count(cont);
}

static u64 current_css_set_read(struct cgroup *cont, struct cftype *cft)
{
	return (u64)(unsigned long)current->cgroups;
}

static u64 current_css_set_refcount_read(struct cgroup *cont,
					   struct cftype *cft)
{
	u64 count;

	rcu_read_lock();
	count = atomic_read(&current->cgroups->refcount);
	rcu_read_unlock();
	return count;
}

static int current_css_set_cg_links_read(struct cgroup *cont,
					 struct cftype *cft,
					 struct seq_file *seq)
{
	struct cg_cgroup_link *link;
	struct css_set *cg;

	read_lock(&css_set_lock);
	rcu_read_lock();
	cg = rcu_dereference(current->cgroups);
	list_for_each_entry(link, &cg->cg_links, cg_link_list) {
		struct cgroup *c = link->cgrp;
		const char *name;

		if (c->dentry)
			name = c->dentry->d_name.name;
		else
			name = "?";
		seq_printf(seq, "Root %d group %s\n",
			   c->root->hierarchy_id, name);
	}
	rcu_read_unlock();
	read_unlock(&css_set_lock);
	return 0;
}

#define MAX_TASKS_SHOWN_PER_CSS 25
static int cgroup_css_links_read(struct cgroup *cont,
				 struct cftype *cft,
				 struct seq_file *seq)
{
	struct cg_cgroup_link *link;

	read_lock(&css_set_lock);
	list_for_each_entry(link, &cont->css_sets, cgrp_link_list) {
		struct css_set *cg = link->cg;
		struct task_struct *task;
		int count = 0;
		seq_printf(seq, "css_set %p\n", cg);
		list_for_each_entry(task, &cg->tasks, cg_list) {
			if (count++ > MAX_TASKS_SHOWN_PER_CSS) {
				seq_puts(seq, "  ...\n");
				break;
			} else {
				seq_printf(seq, "  task %d\n",
					   task_pid_vnr(task));
			}
		}
	}
	read_unlock(&css_set_lock);
	return 0;
}

static u64 releasable_read(struct cgroup *cgrp, struct cftype *cft)
{
	return test_bit(CGRP_RELEASABLE, &cgrp->flags);
}

static struct cftype debug_files[] =  {
	{
		.name = "cgroup_refcount",
		.read_u64 = cgroup_refcount_read,
	},
	{
		.name = "taskcount",
		.read_u64 = debug_taskcount_read,
	},

	{
		.name = "current_css_set",
		.read_u64 = current_css_set_read,
	},

	{
		.name = "current_css_set_refcount",
		.read_u64 = current_css_set_refcount_read,
	},

	{
		.name = "current_css_set_cg_links",
		.read_seq_string = current_css_set_cg_links_read,
	},

	{
		.name = "cgroup_css_links",
		.read_seq_string = cgroup_css_links_read,
	},

	{
		.name = "releasable",
		.read_u64 = releasable_read,
	},
};

static int debug_populate(struct cgroup_subsys *ss, struct cgroup *cont)
{
	return cgroup_add_files(cont, ss, debug_files,
				ARRAY_SIZE(debug_files));
}

struct cgroup_subsys debug_subsys = {
	.name = "debug",
	.create = debug_create,
	.destroy = debug_destroy,
	.populate = debug_populate,
	.subsys_id = debug_subsys_id,
};
#endif /* CONFIG_CGROUP_DEBUG */
