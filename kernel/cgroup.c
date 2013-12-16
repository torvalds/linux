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
#include <linux/cred.h>
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/init_task.h>
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
#include <linux/hashtable.h>
#include <linux/namei.h>
#include <linux/pid_namespace.h>
#include <linux/idr.h>
#include <linux/vmalloc.h> /* TODO: replace with more sophisticated array */
#include <linux/eventfd.h>
#include <linux/poll.h>
#include <linux/flex_array.h> /* used in cgroup_attach_task */
#include <linux/kthread.h>
#include <linux/file.h>

#include <linux/atomic.h>

/*
 * cgroup_mutex is the master lock.  Any modification to cgroup or its
 * hierarchy must be performed while holding it.
 *
 * cgroup_root_mutex nests inside cgroup_mutex and should be held to modify
 * cgroupfs_root of any cgroup hierarchy - subsys list, flags,
 * release_agent_path and so on.  Modifying requires both cgroup_mutex and
 * cgroup_root_mutex.  Readers can acquire either of the two.  This is to
 * break the following locking order cycle.
 *
 *  A. cgroup_mutex -> cred_guard_mutex -> s_type->i_mutex_key -> namespace_sem
 *  B. namespace_sem -> cgroup_mutex
 *
 * B happens only through cgroup_show_options() and using cgroup_root_mutex
 * breaks it.
 */
#ifdef CONFIG_PROVE_RCU
DEFINE_MUTEX(cgroup_mutex);
EXPORT_SYMBOL_GPL(cgroup_mutex);	/* only for lockdep */
#else
static DEFINE_MUTEX(cgroup_mutex);
#endif

static DEFINE_MUTEX(cgroup_root_mutex);

/*
 * Generate an array of cgroup subsystem pointers. At boot time, this is
 * populated with the built in subsystems, and modular subsystems are
 * registered after that. The mutable section of this array is protected by
 * cgroup_mutex.
 */
#define SUBSYS(_x) [_x ## _subsys_id] = &_x ## _subsys,
#define IS_SUBSYS_ENABLED(option) IS_BUILTIN(option)
static struct cgroup_subsys *cgroup_subsys[CGROUP_SUBSYS_COUNT] = {
#include <linux/cgroup_subsys.h>
};

/*
 * The dummy hierarchy, reserved for the subsystems that are otherwise
 * unattached - it never has more than a single cgroup, and all tasks are
 * part of that cgroup.
 */
static struct cgroupfs_root cgroup_dummy_root;

/* dummy_top is a shorthand for the dummy hierarchy's top cgroup */
static struct cgroup * const cgroup_dummy_top = &cgroup_dummy_root.top_cgroup;

/*
 * cgroupfs file entry, pointed to from leaf dentry->d_fsdata.
 */
struct cfent {
	struct list_head		node;
	struct dentry			*dentry;
	struct cftype			*type;
	struct cgroup_subsys_state	*css;

	/* file xattrs */
	struct simple_xattrs		xattrs;
};

/*
 * cgroup_event represents events which userspace want to receive.
 */
struct cgroup_event {
	/*
	 * css which the event belongs to.
	 */
	struct cgroup_subsys_state *css;
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

static LIST_HEAD(cgroup_roots);
static int cgroup_root_count;

/*
 * Hierarchy ID allocation and mapping.  It follows the same exclusion
 * rules as other root ops - both cgroup_mutex and cgroup_root_mutex for
 * writes, either for reads.
 */
static DEFINE_IDR(cgroup_hierarchy_idr);

static struct cgroup_name root_cgroup_name = { .name = "/" };

/*
 * Assign a monotonically increasing serial number to cgroups.  It
 * guarantees cgroups with bigger numbers are newer than those with smaller
 * numbers.  Also, as cgroups are always appended to the parent's
 * ->children list, it guarantees that sibling cgroups are always sorted in
 * the ascending serial number order on the list.  Protected by
 * cgroup_mutex.
 */
static u64 cgroup_serial_nr_next = 1;

/* This flag indicates whether tasks in the fork and exit paths should
 * check for fork/exit handlers to call. This avoids us having to do
 * extra work in the fork/exit path if none of the subsystems need to
 * be called.
 */
static int need_forkexit_callback __read_mostly;

static struct cftype cgroup_base_files[];

static void cgroup_destroy_css_killed(struct cgroup *cgrp);
static int cgroup_destroy_locked(struct cgroup *cgrp);
static int cgroup_addrm_files(struct cgroup *cgrp, struct cftype cfts[],
			      bool is_add);

/**
 * cgroup_css - obtain a cgroup's css for the specified subsystem
 * @cgrp: the cgroup of interest
 * @ss: the subsystem of interest (%NULL returns the dummy_css)
 *
 * Return @cgrp's css (cgroup_subsys_state) associated with @ss.  This
 * function must be called either under cgroup_mutex or rcu_read_lock() and
 * the caller is responsible for pinning the returned css if it wants to
 * keep accessing it outside the said locks.  This function may return
 * %NULL if @cgrp doesn't have @subsys_id enabled.
 */
static struct cgroup_subsys_state *cgroup_css(struct cgroup *cgrp,
					      struct cgroup_subsys *ss)
{
	if (ss)
		return rcu_dereference_check(cgrp->subsys[ss->subsys_id],
					     lockdep_is_held(&cgroup_mutex));
	else
		return &cgrp->dummy_css;
}

/* convenient tests for these bits */
static inline bool cgroup_is_dead(const struct cgroup *cgrp)
{
	return test_bit(CGRP_DEAD, &cgrp->flags);
}

/**
 * cgroup_is_descendant - test ancestry
 * @cgrp: the cgroup to be tested
 * @ancestor: possible ancestor of @cgrp
 *
 * Test whether @cgrp is a descendant of @ancestor.  It also returns %true
 * if @cgrp == @ancestor.  This function is safe to call as long as @cgrp
 * and @ancestor are accessible.
 */
bool cgroup_is_descendant(struct cgroup *cgrp, struct cgroup *ancestor)
{
	while (cgrp) {
		if (cgrp == ancestor)
			return true;
		cgrp = cgrp->parent;
	}
	return false;
}
EXPORT_SYMBOL_GPL(cgroup_is_descendant);

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

/**
 * for_each_subsys - iterate all loaded cgroup subsystems
 * @ss: the iteration cursor
 * @i: the index of @ss, CGROUP_SUBSYS_COUNT after reaching the end
 *
 * Should be called under cgroup_mutex.
 */
#define for_each_subsys(ss, i)						\
	for ((i) = 0; (i) < CGROUP_SUBSYS_COUNT; (i)++)			\
		if (({ lockdep_assert_held(&cgroup_mutex);		\
		       !((ss) = cgroup_subsys[i]); })) { }		\
		else

/**
 * for_each_builtin_subsys - iterate all built-in cgroup subsystems
 * @ss: the iteration cursor
 * @i: the index of @ss, CGROUP_BUILTIN_SUBSYS_COUNT after reaching the end
 *
 * Bulit-in subsystems are always present and iteration itself doesn't
 * require any synchronization.
 */
#define for_each_builtin_subsys(ss, i)					\
	for ((i) = 0; (i) < CGROUP_BUILTIN_SUBSYS_COUNT &&		\
	     (((ss) = cgroup_subsys[i]) || true); (i)++)

/* iterate each subsystem attached to a hierarchy */
#define for_each_root_subsys(root, ss)					\
	list_for_each_entry((ss), &(root)->subsys_list, sibling)

/* iterate across the active hierarchies */
#define for_each_active_root(root)					\
	list_for_each_entry((root), &cgroup_roots, root_list)

static inline struct cgroup *__d_cgrp(struct dentry *dentry)
{
	return dentry->d_fsdata;
}

static inline struct cfent *__d_cfe(struct dentry *dentry)
{
	return dentry->d_fsdata;
}

static inline struct cftype *__d_cft(struct dentry *dentry)
{
	return __d_cfe(dentry)->type;
}

/**
 * cgroup_lock_live_group - take cgroup_mutex and check that cgrp is alive.
 * @cgrp: the cgroup to be checked for liveness
 *
 * On success, returns true; the mutex should be later unlocked.  On
 * failure returns false with no lock held.
 */
static bool cgroup_lock_live_group(struct cgroup *cgrp)
{
	mutex_lock(&cgroup_mutex);
	if (cgroup_is_dead(cgrp)) {
		mutex_unlock(&cgroup_mutex);
		return false;
	}
	return true;
}

/* the list of cgroups eligible for automatic release. Protected by
 * release_list_lock */
static LIST_HEAD(release_list);
static DEFINE_RAW_SPINLOCK(release_list_lock);
static void cgroup_release_agent(struct work_struct *work);
static DECLARE_WORK(release_agent_work, cgroup_release_agent);
static void check_for_release(struct cgroup *cgrp);

/*
 * A cgroup can be associated with multiple css_sets as different tasks may
 * belong to different cgroups on different hierarchies.  In the other
 * direction, a css_set is naturally associated with multiple cgroups.
 * This M:N relationship is represented by the following link structure
 * which exists for each association and allows traversing the associations
 * from both sides.
 */
struct cgrp_cset_link {
	/* the cgroup and css_set this link associates */
	struct cgroup		*cgrp;
	struct css_set		*cset;

	/* list of cgrp_cset_links anchored at cgrp->cset_links */
	struct list_head	cset_link;

	/* list of cgrp_cset_links anchored at css_set->cgrp_links */
	struct list_head	cgrp_link;
};

/* The default css_set - used by init and its children prior to any
 * hierarchies being mounted. It contains a pointer to the root state
 * for each subsystem. Also used to anchor the list of css_sets. Not
 * reference-counted, to improve performance when child cgroups
 * haven't been created.
 */

static struct css_set init_css_set;
static struct cgrp_cset_link init_cgrp_cset_link;

/*
 * css_set_lock protects the list of css_set objects, and the chain of
 * tasks off each css_set.  Nests outside task->alloc_lock due to
 * css_task_iter_start().
 */
static DEFINE_RWLOCK(css_set_lock);
static int css_set_count;

/*
 * hash table for cgroup groups. This improves the performance to find
 * an existing css_set. This hash doesn't (currently) take into
 * account cgroups in empty hierarchies.
 */
#define CSS_SET_HASH_BITS	7
static DEFINE_HASHTABLE(css_set_table, CSS_SET_HASH_BITS);

static unsigned long css_set_hash(struct cgroup_subsys_state *css[])
{
	unsigned long key = 0UL;
	struct cgroup_subsys *ss;
	int i;

	for_each_subsys(ss, i)
		key += (unsigned long)css[i];
	key = (key >> 16) ^ key;

	return key;
}

/*
 * We don't maintain the lists running through each css_set to its task
 * until after the first call to css_task_iter_start().  This reduces the
 * fork()/exit() overhead for people who have cgroups compiled into their
 * kernel but not actually in use.
 */
static int use_task_css_set_links __read_mostly;

static void __put_css_set(struct css_set *cset, int taskexit)
{
	struct cgrp_cset_link *link, *tmp_link;

	/*
	 * Ensure that the refcount doesn't hit zero while any readers
	 * can see it. Similar to atomic_dec_and_lock(), but for an
	 * rwlock
	 */
	if (atomic_add_unless(&cset->refcount, -1, 1))
		return;
	write_lock(&css_set_lock);
	if (!atomic_dec_and_test(&cset->refcount)) {
		write_unlock(&css_set_lock);
		return;
	}

	/* This css_set is dead. unlink it and release cgroup refcounts */
	hash_del(&cset->hlist);
	css_set_count--;

	list_for_each_entry_safe(link, tmp_link, &cset->cgrp_links, cgrp_link) {
		struct cgroup *cgrp = link->cgrp;

		list_del(&link->cset_link);
		list_del(&link->cgrp_link);

		/* @cgrp can't go away while we're holding css_set_lock */
		if (list_empty(&cgrp->cset_links) && notify_on_release(cgrp)) {
			if (taskexit)
				set_bit(CGRP_RELEASABLE, &cgrp->flags);
			check_for_release(cgrp);
		}

		kfree(link);
	}

	write_unlock(&css_set_lock);
	kfree_rcu(cset, rcu_head);
}

/*
 * refcounted get/put for css_set objects
 */
static inline void get_css_set(struct css_set *cset)
{
	atomic_inc(&cset->refcount);
}

static inline void put_css_set(struct css_set *cset)
{
	__put_css_set(cset, 0);
}

static inline void put_css_set_taskexit(struct css_set *cset)
{
	__put_css_set(cset, 1);
}

/**
 * compare_css_sets - helper function for find_existing_css_set().
 * @cset: candidate css_set being tested
 * @old_cset: existing css_set for a task
 * @new_cgrp: cgroup that's being entered by the task
 * @template: desired set of css pointers in css_set (pre-calculated)
 *
 * Returns true if "cset" matches "old_cset" except for the hierarchy
 * which "new_cgrp" belongs to, for which it should match "new_cgrp".
 */
static bool compare_css_sets(struct css_set *cset,
			     struct css_set *old_cset,
			     struct cgroup *new_cgrp,
			     struct cgroup_subsys_state *template[])
{
	struct list_head *l1, *l2;

	if (memcmp(template, cset->subsys, sizeof(cset->subsys))) {
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

	l1 = &cset->cgrp_links;
	l2 = &old_cset->cgrp_links;
	while (1) {
		struct cgrp_cset_link *link1, *link2;
		struct cgroup *cgrp1, *cgrp2;

		l1 = l1->next;
		l2 = l2->next;
		/* See if we reached the end - both lists are equal length. */
		if (l1 == &cset->cgrp_links) {
			BUG_ON(l2 != &old_cset->cgrp_links);
			break;
		} else {
			BUG_ON(l2 == &old_cset->cgrp_links);
		}
		/* Locate the cgroups associated with these links. */
		link1 = list_entry(l1, struct cgrp_cset_link, cgrp_link);
		link2 = list_entry(l2, struct cgrp_cset_link, cgrp_link);
		cgrp1 = link1->cgrp;
		cgrp2 = link2->cgrp;
		/* Hierarchies should be linked in the same order. */
		BUG_ON(cgrp1->root != cgrp2->root);

		/*
		 * If this hierarchy is the hierarchy of the cgroup
		 * that's changing, then we need to check that this
		 * css_set points to the new cgroup; if it's any other
		 * hierarchy, then this css_set should point to the
		 * same cgroup as the old css_set.
		 */
		if (cgrp1->root == new_cgrp->root) {
			if (cgrp1 != new_cgrp)
				return false;
		} else {
			if (cgrp1 != cgrp2)
				return false;
		}
	}
	return true;
}

/**
 * find_existing_css_set - init css array and find the matching css_set
 * @old_cset: the css_set that we're using before the cgroup transition
 * @cgrp: the cgroup that we're moving into
 * @template: out param for the new set of csses, should be clear on entry
 */
static struct css_set *find_existing_css_set(struct css_set *old_cset,
					struct cgroup *cgrp,
					struct cgroup_subsys_state *template[])
{
	struct cgroupfs_root *root = cgrp->root;
	struct cgroup_subsys *ss;
	struct css_set *cset;
	unsigned long key;
	int i;

	/*
	 * Build the set of subsystem state objects that we want to see in the
	 * new css_set. while subsystems can change globally, the entries here
	 * won't change, so no need for locking.
	 */
	for_each_subsys(ss, i) {
		if (root->subsys_mask & (1UL << i)) {
			/* Subsystem is in this hierarchy. So we want
			 * the subsystem state from the new
			 * cgroup */
			template[i] = cgroup_css(cgrp, ss);
		} else {
			/* Subsystem is not in this hierarchy, so we
			 * don't want to change the subsystem state */
			template[i] = old_cset->subsys[i];
		}
	}

	key = css_set_hash(template);
	hash_for_each_possible(css_set_table, cset, hlist, key) {
		if (!compare_css_sets(cset, old_cset, cgrp, template))
			continue;

		/* This css_set matches what we need */
		return cset;
	}

	/* No existing cgroup group matched */
	return NULL;
}

static void free_cgrp_cset_links(struct list_head *links_to_free)
{
	struct cgrp_cset_link *link, *tmp_link;

	list_for_each_entry_safe(link, tmp_link, links_to_free, cset_link) {
		list_del(&link->cset_link);
		kfree(link);
	}
}

/**
 * allocate_cgrp_cset_links - allocate cgrp_cset_links
 * @count: the number of links to allocate
 * @tmp_links: list_head the allocated links are put on
 *
 * Allocate @count cgrp_cset_link structures and chain them on @tmp_links
 * through ->cset_link.  Returns 0 on success or -errno.
 */
static int allocate_cgrp_cset_links(int count, struct list_head *tmp_links)
{
	struct cgrp_cset_link *link;
	int i;

	INIT_LIST_HEAD(tmp_links);

	for (i = 0; i < count; i++) {
		link = kzalloc(sizeof(*link), GFP_KERNEL);
		if (!link) {
			free_cgrp_cset_links(tmp_links);
			return -ENOMEM;
		}
		list_add(&link->cset_link, tmp_links);
	}
	return 0;
}

/**
 * link_css_set - a helper function to link a css_set to a cgroup
 * @tmp_links: cgrp_cset_link objects allocated by allocate_cgrp_cset_links()
 * @cset: the css_set to be linked
 * @cgrp: the destination cgroup
 */
static void link_css_set(struct list_head *tmp_links, struct css_set *cset,
			 struct cgroup *cgrp)
{
	struct cgrp_cset_link *link;

	BUG_ON(list_empty(tmp_links));
	link = list_first_entry(tmp_links, struct cgrp_cset_link, cset_link);
	link->cset = cset;
	link->cgrp = cgrp;
	list_move(&link->cset_link, &cgrp->cset_links);
	/*
	 * Always add links to the tail of the list so that the list
	 * is sorted by order of hierarchy creation
	 */
	list_add_tail(&link->cgrp_link, &cset->cgrp_links);
}

/**
 * find_css_set - return a new css_set with one cgroup updated
 * @old_cset: the baseline css_set
 * @cgrp: the cgroup to be updated
 *
 * Return a new css_set that's equivalent to @old_cset, but with @cgrp
 * substituted into the appropriate hierarchy.
 */
static struct css_set *find_css_set(struct css_set *old_cset,
				    struct cgroup *cgrp)
{
	struct cgroup_subsys_state *template[CGROUP_SUBSYS_COUNT] = { };
	struct css_set *cset;
	struct list_head tmp_links;
	struct cgrp_cset_link *link;
	unsigned long key;

	lockdep_assert_held(&cgroup_mutex);

	/* First see if we already have a cgroup group that matches
	 * the desired set */
	read_lock(&css_set_lock);
	cset = find_existing_css_set(old_cset, cgrp, template);
	if (cset)
		get_css_set(cset);
	read_unlock(&css_set_lock);

	if (cset)
		return cset;

	cset = kzalloc(sizeof(*cset), GFP_KERNEL);
	if (!cset)
		return NULL;

	/* Allocate all the cgrp_cset_link objects that we'll need */
	if (allocate_cgrp_cset_links(cgroup_root_count, &tmp_links) < 0) {
		kfree(cset);
		return NULL;
	}

	atomic_set(&cset->refcount, 1);
	INIT_LIST_HEAD(&cset->cgrp_links);
	INIT_LIST_HEAD(&cset->tasks);
	INIT_HLIST_NODE(&cset->hlist);

	/* Copy the set of subsystem state objects generated in
	 * find_existing_css_set() */
	memcpy(cset->subsys, template, sizeof(cset->subsys));

	write_lock(&css_set_lock);
	/* Add reference counts and links from the new css_set. */
	list_for_each_entry(link, &old_cset->cgrp_links, cgrp_link) {
		struct cgroup *c = link->cgrp;

		if (c->root == cgrp->root)
			c = cgrp;
		link_css_set(&tmp_links, cset, c);
	}

	BUG_ON(!list_empty(&tmp_links));

	css_set_count++;

	/* Add this cgroup group to the hash table */
	key = css_set_hash(cset->subsys);
	hash_add(css_set_table, &cset->hlist, key);

	write_unlock(&css_set_lock);

	return cset;
}

/*
 * Return the cgroup for "task" from the given hierarchy. Must be
 * called with cgroup_mutex held.
 */
static struct cgroup *task_cgroup_from_root(struct task_struct *task,
					    struct cgroupfs_root *root)
{
	struct css_set *cset;
	struct cgroup *res = NULL;

	BUG_ON(!mutex_is_locked(&cgroup_mutex));
	read_lock(&css_set_lock);
	/*
	 * No need to lock the task - since we hold cgroup_mutex the
	 * task can't change groups, so the only thing that can happen
	 * is that it exits and its css is set back to init_css_set.
	 */
	cset = task_css_set(task);
	if (cset == &init_css_set) {
		res = &root->top_cgroup;
	} else {
		struct cgrp_cset_link *link;

		list_for_each_entry(link, &cset->cgrp_links, cgrp_link) {
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
 * cgroup_attach_task(), which overwrites one task's cgroup pointer with
 * another.  It does so using cgroup_mutex, however there are
 * several performance critical places that need to reference
 * task->cgroup without the expense of grabbing a system global
 * mutex.  Therefore except as noted below, when dereferencing or, as
 * in cgroup_attach_task(), modifying a task's cgroup pointer we use
 * task_lock(), which acts on a spinlock (task->alloc_lock) already in
 * the task_struct routinely used for such matters.
 *
 * P.S.  One more locking exception.  RCU is used to guard the
 * update of a tasks cgroup pointer by cgroup_attach_task()
 */

/*
 * A couple of forward declarations required, due to cyclic reference loop:
 * cgroup_mkdir -> cgroup_create -> cgroup_populate_dir ->
 * cgroup_add_file -> cgroup_create_file -> cgroup_dir_inode_operations
 * -> cgroup_mkdir.
 */

static int cgroup_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode);
static int cgroup_rmdir(struct inode *unused_dir, struct dentry *dentry);
static int cgroup_populate_dir(struct cgroup *cgrp, unsigned long subsys_mask);
static const struct inode_operations cgroup_dir_inode_operations;
static const struct file_operations proc_cgroupstats_operations;

static struct backing_dev_info cgroup_backing_dev_info = {
	.name		= "cgroup",
	.capabilities	= BDI_CAP_NO_ACCT_AND_WRITEBACK,
};

static struct inode *cgroup_new_inode(umode_t mode, struct super_block *sb)
{
	struct inode *inode = new_inode(sb);

	if (inode) {
		inode->i_ino = get_next_ino();
		inode->i_mode = mode;
		inode->i_uid = current_fsuid();
		inode->i_gid = current_fsgid();
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		inode->i_mapping->backing_dev_info = &cgroup_backing_dev_info;
	}
	return inode;
}

static struct cgroup_name *cgroup_alloc_name(struct dentry *dentry)
{
	struct cgroup_name *name;

	name = kmalloc(sizeof(*name) + dentry->d_name.len + 1, GFP_KERNEL);
	if (!name)
		return NULL;
	strcpy(name->name, dentry->d_name.name);
	return name;
}

static void cgroup_free_fn(struct work_struct *work)
{
	struct cgroup *cgrp = container_of(work, struct cgroup, destroy_work);

	mutex_lock(&cgroup_mutex);
	cgrp->root->number_of_cgroups--;
	mutex_unlock(&cgroup_mutex);

	/*
	 * We get a ref to the parent's dentry, and put the ref when
	 * this cgroup is being freed, so it's guaranteed that the
	 * parent won't be destroyed before its children.
	 */
	dput(cgrp->parent->dentry);

	/*
	 * Drop the active superblock reference that we took when we
	 * created the cgroup. This will free cgrp->root, if we are
	 * holding the last reference to @sb.
	 */
	deactivate_super(cgrp->root->sb);

	/*
	 * if we're getting rid of the cgroup, refcount should ensure
	 * that there are no pidlists left.
	 */
	BUG_ON(!list_empty(&cgrp->pidlists));

	simple_xattrs_free(&cgrp->xattrs);

	kfree(rcu_dereference_raw(cgrp->name));
	kfree(cgrp);
}

static void cgroup_free_rcu(struct rcu_head *head)
{
	struct cgroup *cgrp = container_of(head, struct cgroup, rcu_head);

	INIT_WORK(&cgrp->destroy_work, cgroup_free_fn);
	schedule_work(&cgrp->destroy_work);
}

static void cgroup_diput(struct dentry *dentry, struct inode *inode)
{
	/* is dentry a directory ? if so, kfree() associated cgroup */
	if (S_ISDIR(inode->i_mode)) {
		struct cgroup *cgrp = dentry->d_fsdata;

		BUG_ON(!(cgroup_is_dead(cgrp)));
		call_rcu(&cgrp->rcu_head, cgroup_free_rcu);
	} else {
		struct cfent *cfe = __d_cfe(dentry);
		struct cgroup *cgrp = dentry->d_parent->d_fsdata;

		WARN_ONCE(!list_empty(&cfe->node) &&
			  cgrp != &cgrp->root->top_cgroup,
			  "cfe still linked for %s\n", cfe->type->name);
		simple_xattrs_free(&cfe->xattrs);
		kfree(cfe);
	}
	iput(inode);
}

static int cgroup_delete(const struct dentry *d)
{
	return 1;
}

static void remove_dir(struct dentry *d)
{
	struct dentry *parent = dget(d->d_parent);

	d_delete(d);
	simple_rmdir(parent->d_inode, d);
	dput(parent);
}

static void cgroup_rm_file(struct cgroup *cgrp, const struct cftype *cft)
{
	struct cfent *cfe;

	lockdep_assert_held(&cgrp->dentry->d_inode->i_mutex);
	lockdep_assert_held(&cgroup_mutex);

	/*
	 * If we're doing cleanup due to failure of cgroup_create(),
	 * the corresponding @cfe may not exist.
	 */
	list_for_each_entry(cfe, &cgrp->files, node) {
		struct dentry *d = cfe->dentry;

		if (cft && cfe->type != cft)
			continue;

		dget(d);
		d_delete(d);
		simple_unlink(cgrp->dentry->d_inode, d);
		list_del_init(&cfe->node);
		dput(d);

		break;
	}
}

/**
 * cgroup_clear_dir - remove subsys files in a cgroup directory
 * @cgrp: target cgroup
 * @subsys_mask: mask of the subsystem ids whose files should be removed
 */
static void cgroup_clear_dir(struct cgroup *cgrp, unsigned long subsys_mask)
{
	struct cgroup_subsys *ss;
	int i;

	for_each_subsys(ss, i) {
		struct cftype_set *set;

		if (!test_bit(i, &subsys_mask))
			continue;
		list_for_each_entry(set, &ss->cftsets, node)
			cgroup_addrm_files(cgrp, set->cfts, false);
	}
}

/*
 * NOTE : the dentry must have been dget()'ed
 */
static void cgroup_d_remove_dir(struct dentry *dentry)
{
	struct dentry *parent;

	parent = dentry->d_parent;
	spin_lock(&parent->d_lock);
	spin_lock_nested(&dentry->d_lock, DENTRY_D_LOCK_NESTED);
	list_del_init(&dentry->d_u.d_child);
	spin_unlock(&dentry->d_lock);
	spin_unlock(&parent->d_lock);
	remove_dir(dentry);
}

/*
 * Call with cgroup_mutex held. Drops reference counts on modules, including
 * any duplicate ones that parse_cgroupfs_options took. If this function
 * returns an error, no reference counts are touched.
 */
static int rebind_subsystems(struct cgroupfs_root *root,
			     unsigned long added_mask, unsigned removed_mask)
{
	struct cgroup *cgrp = &root->top_cgroup;
	struct cgroup_subsys *ss;
	unsigned long pinned = 0;
	int i, ret;

	BUG_ON(!mutex_is_locked(&cgroup_mutex));
	BUG_ON(!mutex_is_locked(&cgroup_root_mutex));

	/* Check that any added subsystems are currently free */
	for_each_subsys(ss, i) {
		if (!(added_mask & (1 << i)))
			continue;

		/* is the subsystem mounted elsewhere? */
		if (ss->root != &cgroup_dummy_root) {
			ret = -EBUSY;
			goto out_put;
		}

		/* pin the module */
		if (!try_module_get(ss->module)) {
			ret = -ENOENT;
			goto out_put;
		}
		pinned |= 1 << i;
	}

	/* subsys could be missing if unloaded between parsing and here */
	if (added_mask != pinned) {
		ret = -ENOENT;
		goto out_put;
	}

	ret = cgroup_populate_dir(cgrp, added_mask);
	if (ret)
		goto out_put;

	/*
	 * Nothing can fail from this point on.  Remove files for the
	 * removed subsystems and rebind each subsystem.
	 */
	cgroup_clear_dir(cgrp, removed_mask);

	for_each_subsys(ss, i) {
		unsigned long bit = 1UL << i;

		if (bit & added_mask) {
			/* We're binding this subsystem to this hierarchy */
			BUG_ON(cgroup_css(cgrp, ss));
			BUG_ON(!cgroup_css(cgroup_dummy_top, ss));
			BUG_ON(cgroup_css(cgroup_dummy_top, ss)->cgroup != cgroup_dummy_top);

			rcu_assign_pointer(cgrp->subsys[i],
					   cgroup_css(cgroup_dummy_top, ss));
			cgroup_css(cgrp, ss)->cgroup = cgrp;

			list_move(&ss->sibling, &root->subsys_list);
			ss->root = root;
			if (ss->bind)
				ss->bind(cgroup_css(cgrp, ss));

			/* refcount was already taken, and we're keeping it */
			root->subsys_mask |= bit;
		} else if (bit & removed_mask) {
			/* We're removing this subsystem */
			BUG_ON(cgroup_css(cgrp, ss) != cgroup_css(cgroup_dummy_top, ss));
			BUG_ON(cgroup_css(cgrp, ss)->cgroup != cgrp);

			if (ss->bind)
				ss->bind(cgroup_css(cgroup_dummy_top, ss));

			cgroup_css(cgroup_dummy_top, ss)->cgroup = cgroup_dummy_top;
			RCU_INIT_POINTER(cgrp->subsys[i], NULL);

			cgroup_subsys[i]->root = &cgroup_dummy_root;
			list_move(&ss->sibling, &cgroup_dummy_root.subsys_list);

			/* subsystem is now free - drop reference on module */
			module_put(ss->module);
			root->subsys_mask &= ~bit;
		}
	}

	/*
	 * Mark @root has finished binding subsystems.  @root->subsys_mask
	 * now matches the bound subsystems.
	 */
	root->flags |= CGRP_ROOT_SUBSYS_BOUND;

	return 0;

out_put:
	for_each_subsys(ss, i)
		if (pinned & (1 << i))
			module_put(ss->module);
	return ret;
}

static int cgroup_show_options(struct seq_file *seq, struct dentry *dentry)
{
	struct cgroupfs_root *root = dentry->d_sb->s_fs_info;
	struct cgroup_subsys *ss;

	mutex_lock(&cgroup_root_mutex);
	for_each_root_subsys(root, ss)
		seq_printf(seq, ",%s", ss->name);
	if (root->flags & CGRP_ROOT_SANE_BEHAVIOR)
		seq_puts(seq, ",sane_behavior");
	if (root->flags & CGRP_ROOT_NOPREFIX)
		seq_puts(seq, ",noprefix");
	if (root->flags & CGRP_ROOT_XATTR)
		seq_puts(seq, ",xattr");
	if (strlen(root->release_agent_path))
		seq_printf(seq, ",release_agent=%s", root->release_agent_path);
	if (test_bit(CGRP_CPUSET_CLONE_CHILDREN, &root->top_cgroup.flags))
		seq_puts(seq, ",clone_children");
	if (strlen(root->name))
		seq_printf(seq, ",name=%s", root->name);
	mutex_unlock(&cgroup_root_mutex);
	return 0;
}

struct cgroup_sb_opts {
	unsigned long subsys_mask;
	unsigned long flags;
	char *release_agent;
	bool cpuset_clone_children;
	char *name;
	/* User explicitly requested empty subsystem */
	bool none;

	struct cgroupfs_root *new_root;

};

/*
 * Convert a hierarchy specifier into a bitmask of subsystems and
 * flags. Call with cgroup_mutex held to protect the cgroup_subsys[]
 * array. This function takes refcounts on subsystems to be used, unless it
 * returns error, in which case no refcounts are taken.
 */
static int parse_cgroupfs_options(char *data, struct cgroup_sb_opts *opts)
{
	char *token, *o = data;
	bool all_ss = false, one_ss = false;
	unsigned long mask = (unsigned long)-1;
	struct cgroup_subsys *ss;
	int i;

	BUG_ON(!mutex_is_locked(&cgroup_mutex));

#ifdef CONFIG_CPUSETS
	mask = ~(1UL << cpuset_subsys_id);
#endif

	memset(opts, 0, sizeof(*opts));

	while ((token = strsep(&o, ",")) != NULL) {
		if (!*token)
			return -EINVAL;
		if (!strcmp(token, "none")) {
			/* Explicitly have no subsystems */
			opts->none = true;
			continue;
		}
		if (!strcmp(token, "all")) {
			/* Mutually exclusive option 'all' + subsystem name */
			if (one_ss)
				return -EINVAL;
			all_ss = true;
			continue;
		}
		if (!strcmp(token, "__DEVEL__sane_behavior")) {
			opts->flags |= CGRP_ROOT_SANE_BEHAVIOR;
			continue;
		}
		if (!strcmp(token, "noprefix")) {
			opts->flags |= CGRP_ROOT_NOPREFIX;
			continue;
		}
		if (!strcmp(token, "clone_children")) {
			opts->cpuset_clone_children = true;
			continue;
		}
		if (!strcmp(token, "xattr")) {
			opts->flags |= CGRP_ROOT_XATTR;
			continue;
		}
		if (!strncmp(token, "release_agent=", 14)) {
			/* Specifying two release agents is forbidden */
			if (opts->release_agent)
				return -EINVAL;
			opts->release_agent =
				kstrndup(token + 14, PATH_MAX - 1, GFP_KERNEL);
			if (!opts->release_agent)
				return -ENOMEM;
			continue;
		}
		if (!strncmp(token, "name=", 5)) {
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

			continue;
		}

		for_each_subsys(ss, i) {
			if (strcmp(token, ss->name))
				continue;
			if (ss->disabled)
				continue;

			/* Mutually exclusive option 'all' + subsystem name */
			if (all_ss)
				return -EINVAL;
			set_bit(i, &opts->subsys_mask);
			one_ss = true;

			break;
		}
		if (i == CGROUP_SUBSYS_COUNT)
			return -ENOENT;
	}

	/*
	 * If the 'all' option was specified select all the subsystems,
	 * otherwise if 'none', 'name=' and a subsystem name options
	 * were not specified, let's default to 'all'
	 */
	if (all_ss || (!one_ss && !opts->none && !opts->name))
		for_each_subsys(ss, i)
			if (!ss->disabled)
				set_bit(i, &opts->subsys_mask);

	/* Consistency checks */

	if (opts->flags & CGRP_ROOT_SANE_BEHAVIOR) {
		pr_warning("cgroup: sane_behavior: this is still under development and its behaviors will change, proceed at your own risk\n");

		if (opts->flags & CGRP_ROOT_NOPREFIX) {
			pr_err("cgroup: sane_behavior: noprefix is not allowed\n");
			return -EINVAL;
		}

		if (opts->cpuset_clone_children) {
			pr_err("cgroup: sane_behavior: clone_children is not allowed\n");
			return -EINVAL;
		}
	}

	/*
	 * Option noprefix was introduced just for backward compatibility
	 * with the old cpuset, so we allow noprefix only if mounting just
	 * the cpuset subsystem.
	 */
	if ((opts->flags & CGRP_ROOT_NOPREFIX) && (opts->subsys_mask & mask))
		return -EINVAL;


	/* Can't specify "none" and some subsystems */
	if (opts->subsys_mask && opts->none)
		return -EINVAL;

	/*
	 * We either have to specify by name or by subsystems. (So all
	 * empty hierarchies must have a name).
	 */
	if (!opts->subsys_mask && !opts->name)
		return -EINVAL;

	return 0;
}

static int cgroup_remount(struct super_block *sb, int *flags, char *data)
{
	int ret = 0;
	struct cgroupfs_root *root = sb->s_fs_info;
	struct cgroup *cgrp = &root->top_cgroup;
	struct cgroup_sb_opts opts;
	unsigned long added_mask, removed_mask;

	if (root->flags & CGRP_ROOT_SANE_BEHAVIOR) {
		pr_err("cgroup: sane_behavior: remount is not allowed\n");
		return -EINVAL;
	}

	mutex_lock(&cgrp->dentry->d_inode->i_mutex);
	mutex_lock(&cgroup_mutex);
	mutex_lock(&cgroup_root_mutex);

	/* See what subsystems are wanted */
	ret = parse_cgroupfs_options(data, &opts);
	if (ret)
		goto out_unlock;

	if (opts.subsys_mask != root->subsys_mask || opts.release_agent)
		pr_warning("cgroup: option changes via remount are deprecated (pid=%d comm=%s)\n",
			   task_tgid_nr(current), current->comm);

	added_mask = opts.subsys_mask & ~root->subsys_mask;
	removed_mask = root->subsys_mask & ~opts.subsys_mask;

	/* Don't allow flags or name to change at remount */
	if (((opts.flags ^ root->flags) & CGRP_ROOT_OPTION_MASK) ||
	    (opts.name && strcmp(opts.name, root->name))) {
		pr_err("cgroup: option or name mismatch, new: 0x%lx \"%s\", old: 0x%lx \"%s\"\n",
		       opts.flags & CGRP_ROOT_OPTION_MASK, opts.name ?: "",
		       root->flags & CGRP_ROOT_OPTION_MASK, root->name);
		ret = -EINVAL;
		goto out_unlock;
	}

	/* remounting is not allowed for populated hierarchies */
	if (root->number_of_cgroups > 1) {
		ret = -EBUSY;
		goto out_unlock;
	}

	ret = rebind_subsystems(root, added_mask, removed_mask);
	if (ret)
		goto out_unlock;

	if (opts.release_agent)
		strcpy(root->release_agent_path, opts.release_agent);
 out_unlock:
	kfree(opts.release_agent);
	kfree(opts.name);
	mutex_unlock(&cgroup_root_mutex);
	mutex_unlock(&cgroup_mutex);
	mutex_unlock(&cgrp->dentry->d_inode->i_mutex);
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
	INIT_LIST_HEAD(&cgrp->files);
	INIT_LIST_HEAD(&cgrp->cset_links);
	INIT_LIST_HEAD(&cgrp->release_list);
	INIT_LIST_HEAD(&cgrp->pidlists);
	mutex_init(&cgrp->pidlist_mutex);
	cgrp->dummy_css.cgroup = cgrp;
	INIT_LIST_HEAD(&cgrp->event_list);
	spin_lock_init(&cgrp->event_list_lock);
	simple_xattrs_init(&cgrp->xattrs);
}

static void init_cgroup_root(struct cgroupfs_root *root)
{
	struct cgroup *cgrp = &root->top_cgroup;

	INIT_LIST_HEAD(&root->subsys_list);
	INIT_LIST_HEAD(&root->root_list);
	root->number_of_cgroups = 1;
	cgrp->root = root;
	RCU_INIT_POINTER(cgrp->name, &root_cgroup_name);
	init_cgroup_housekeeping(cgrp);
	idr_init(&root->cgroup_idr);
}

static int cgroup_init_root_id(struct cgroupfs_root *root, int start, int end)
{
	int id;

	lockdep_assert_held(&cgroup_mutex);
	lockdep_assert_held(&cgroup_root_mutex);

	id = idr_alloc_cyclic(&cgroup_hierarchy_idr, root, start, end,
			      GFP_KERNEL);
	if (id < 0)
		return id;

	root->hierarchy_id = id;
	return 0;
}

static void cgroup_exit_root_id(struct cgroupfs_root *root)
{
	lockdep_assert_held(&cgroup_mutex);
	lockdep_assert_held(&cgroup_root_mutex);

	if (root->hierarchy_id) {
		idr_remove(&cgroup_hierarchy_idr, root->hierarchy_id);
		root->hierarchy_id = 0;
	}
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
	if ((opts->subsys_mask || opts->none)
	    && (opts->subsys_mask != root->subsys_mask))
		return 0;

	return 1;
}

static struct cgroupfs_root *cgroup_root_from_opts(struct cgroup_sb_opts *opts)
{
	struct cgroupfs_root *root;

	if (!opts->subsys_mask && !opts->none)
		return NULL;

	root = kzalloc(sizeof(*root), GFP_KERNEL);
	if (!root)
		return ERR_PTR(-ENOMEM);

	init_cgroup_root(root);

	/*
	 * We need to set @root->subsys_mask now so that @root can be
	 * matched by cgroup_test_super() before it finishes
	 * initialization; otherwise, competing mounts with the same
	 * options may try to bind the same subsystems instead of waiting
	 * for the first one leading to unexpected mount errors.
	 * SUBSYS_BOUND will be set once actual binding is complete.
	 */
	root->subsys_mask = opts->subsys_mask;
	root->flags = opts->flags;
	if (opts->release_agent)
		strcpy(root->release_agent_path, opts->release_agent);
	if (opts->name)
		strcpy(root->name, opts->name);
	if (opts->cpuset_clone_children)
		set_bit(CGRP_CPUSET_CLONE_CHILDREN, &root->top_cgroup.flags);
	return root;
}

static void cgroup_free_root(struct cgroupfs_root *root)
{
	if (root) {
		/* hierarhcy ID shoulid already have been released */
		WARN_ON_ONCE(root->hierarchy_id);

		idr_destroy(&root->cgroup_idr);
		kfree(root);
	}
}

static int cgroup_set_super(struct super_block *sb, void *data)
{
	int ret;
	struct cgroup_sb_opts *opts = data;

	/* If we don't have a new root, we can't set up a new sb */
	if (!opts->new_root)
		return -EINVAL;

	BUG_ON(!opts->subsys_mask && !opts->none);

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
	static const struct dentry_operations cgroup_dops = {
		.d_iput = cgroup_diput,
		.d_delete = cgroup_delete,
	};

	struct inode *inode =
		cgroup_new_inode(S_IFDIR | S_IRUGO | S_IXUGO | S_IWUSR, sb);

	if (!inode)
		return -ENOMEM;

	inode->i_fop = &simple_dir_operations;
	inode->i_op = &cgroup_dir_inode_operations;
	/* directories start off with i_nlink == 2 (for "." entry) */
	inc_nlink(inode);
	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
		return -ENOMEM;
	/* for everything else we want ->d_op set */
	sb->s_d_op = &cgroup_dops;
	return 0;
}

static struct dentry *cgroup_mount(struct file_system_type *fs_type,
			 int flags, const char *unused_dev_name,
			 void *data)
{
	struct cgroup_sb_opts opts;
	struct cgroupfs_root *root;
	int ret = 0;
	struct super_block *sb;
	struct cgroupfs_root *new_root;
	struct list_head tmp_links;
	struct inode *inode;
	const struct cred *cred;

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
		goto out_err;
	}
	opts.new_root = new_root;

	/* Locate an existing or new sb for this hierarchy */
	sb = sget(fs_type, cgroup_test_super, cgroup_set_super, 0, &opts);
	if (IS_ERR(sb)) {
		ret = PTR_ERR(sb);
		cgroup_free_root(opts.new_root);
		goto out_err;
	}

	root = sb->s_fs_info;
	BUG_ON(!root);
	if (root == opts.new_root) {
		/* We used the new root structure, so this is a new hierarchy */
		struct cgroup *root_cgrp = &root->top_cgroup;
		struct cgroupfs_root *existing_root;
		int i;
		struct css_set *cset;

		BUG_ON(sb->s_root != NULL);

		ret = cgroup_get_rootdir(sb);
		if (ret)
			goto drop_new_super;
		inode = sb->s_root->d_inode;

		mutex_lock(&inode->i_mutex);
		mutex_lock(&cgroup_mutex);
		mutex_lock(&cgroup_root_mutex);

		root_cgrp->id = idr_alloc(&root->cgroup_idr, root_cgrp,
					   0, 1, GFP_KERNEL);
		if (root_cgrp->id < 0)
			goto unlock_drop;

		/* Check for name clashes with existing mounts */
		ret = -EBUSY;
		if (strlen(root->name))
			for_each_active_root(existing_root)
				if (!strcmp(existing_root->name, root->name))
					goto unlock_drop;

		/*
		 * We're accessing css_set_count without locking
		 * css_set_lock here, but that's OK - it can only be
		 * increased by someone holding cgroup_lock, and
		 * that's us. The worst that can happen is that we
		 * have some link structures left over
		 */
		ret = allocate_cgrp_cset_links(css_set_count, &tmp_links);
		if (ret)
			goto unlock_drop;

		/* ID 0 is reserved for dummy root, 1 for unified hierarchy */
		ret = cgroup_init_root_id(root, 2, 0);
		if (ret)
			goto unlock_drop;

		sb->s_root->d_fsdata = root_cgrp;
		root_cgrp->dentry = sb->s_root;

		/*
		 * We're inside get_sb() and will call lookup_one_len() to
		 * create the root files, which doesn't work if SELinux is
		 * in use.  The following cred dancing somehow works around
		 * it.  See 2ce9738ba ("cgroupfs: use init_cred when
		 * populating new cgroupfs mount") for more details.
		 */
		cred = override_creds(&init_cred);

		ret = cgroup_addrm_files(root_cgrp, cgroup_base_files, true);
		if (ret)
			goto rm_base_files;

		ret = rebind_subsystems(root, root->subsys_mask, 0);
		if (ret)
			goto rm_base_files;

		revert_creds(cred);

		/*
		 * There must be no failure case after here, since rebinding
		 * takes care of subsystems' refcounts, which are explicitly
		 * dropped in the failure exit path.
		 */

		list_add(&root->root_list, &cgroup_roots);
		cgroup_root_count++;

		/* Link the top cgroup in this hierarchy into all
		 * the css_set objects */
		write_lock(&css_set_lock);
		hash_for_each(css_set_table, i, cset, hlist)
			link_css_set(&tmp_links, cset, root_cgrp);
		write_unlock(&css_set_lock);

		free_cgrp_cset_links(&tmp_links);

		BUG_ON(!list_empty(&root_cgrp->children));
		BUG_ON(root->number_of_cgroups != 1);

		mutex_unlock(&cgroup_root_mutex);
		mutex_unlock(&cgroup_mutex);
		mutex_unlock(&inode->i_mutex);
	} else {
		/*
		 * We re-used an existing hierarchy - the new root (if
		 * any) is not needed
		 */
		cgroup_free_root(opts.new_root);

		if ((root->flags ^ opts.flags) & CGRP_ROOT_OPTION_MASK) {
			if ((root->flags | opts.flags) & CGRP_ROOT_SANE_BEHAVIOR) {
				pr_err("cgroup: sane_behavior: new mount options should match the existing superblock\n");
				ret = -EINVAL;
				goto drop_new_super;
			} else {
				pr_warning("cgroup: new mount options do not match the existing superblock, will be ignored\n");
			}
		}
	}

	kfree(opts.release_agent);
	kfree(opts.name);
	return dget(sb->s_root);

 rm_base_files:
	free_cgrp_cset_links(&tmp_links);
	cgroup_addrm_files(&root->top_cgroup, cgroup_base_files, false);
	revert_creds(cred);
 unlock_drop:
	cgroup_exit_root_id(root);
	mutex_unlock(&cgroup_root_mutex);
	mutex_unlock(&cgroup_mutex);
	mutex_unlock(&inode->i_mutex);
 drop_new_super:
	deactivate_locked_super(sb);
 out_err:
	kfree(opts.release_agent);
	kfree(opts.name);
	return ERR_PTR(ret);
}

static void cgroup_kill_sb(struct super_block *sb) {
	struct cgroupfs_root *root = sb->s_fs_info;
	struct cgroup *cgrp = &root->top_cgroup;
	struct cgrp_cset_link *link, *tmp_link;
	int ret;

	BUG_ON(!root);

	BUG_ON(root->number_of_cgroups != 1);
	BUG_ON(!list_empty(&cgrp->children));

	mutex_lock(&cgrp->dentry->d_inode->i_mutex);
	mutex_lock(&cgroup_mutex);
	mutex_lock(&cgroup_root_mutex);

	/* Rebind all subsystems back to the default hierarchy */
	if (root->flags & CGRP_ROOT_SUBSYS_BOUND) {
		ret = rebind_subsystems(root, 0, root->subsys_mask);
		/* Shouldn't be able to fail ... */
		BUG_ON(ret);
	}

	/*
	 * Release all the links from cset_links to this hierarchy's
	 * root cgroup
	 */
	write_lock(&css_set_lock);

	list_for_each_entry_safe(link, tmp_link, &cgrp->cset_links, cset_link) {
		list_del(&link->cset_link);
		list_del(&link->cgrp_link);
		kfree(link);
	}
	write_unlock(&css_set_lock);

	if (!list_empty(&root->root_list)) {
		list_del(&root->root_list);
		cgroup_root_count--;
	}

	cgroup_exit_root_id(root);

	mutex_unlock(&cgroup_root_mutex);
	mutex_unlock(&cgroup_mutex);
	mutex_unlock(&cgrp->dentry->d_inode->i_mutex);

	simple_xattrs_free(&cgrp->xattrs);

	kill_litter_super(sb);
	cgroup_free_root(root);
}

static struct file_system_type cgroup_fs_type = {
	.name = "cgroup",
	.mount = cgroup_mount,
	.kill_sb = cgroup_kill_sb,
};

static struct kobject *cgroup_kobj;

/**
 * cgroup_path - generate the path of a cgroup
 * @cgrp: the cgroup in question
 * @buf: the buffer to write the path into
 * @buflen: the length of the buffer
 *
 * Writes path of cgroup into buf.  Returns 0 on success, -errno on error.
 *
 * We can't generate cgroup path using dentry->d_name, as accessing
 * dentry->name must be protected by irq-unsafe dentry->d_lock or parent
 * inode's i_mutex, while on the other hand cgroup_path() can be called
 * with some irq-safe spinlocks held.
 */
int cgroup_path(const struct cgroup *cgrp, char *buf, int buflen)
{
	int ret = -ENAMETOOLONG;
	char *start;

	if (!cgrp->parent) {
		if (strlcpy(buf, "/", buflen) >= buflen)
			return -ENAMETOOLONG;
		return 0;
	}

	start = buf + buflen - 1;
	*start = '\0';

	rcu_read_lock();
	do {
		const char *name = cgroup_name(cgrp);
		int len;

		len = strlen(name);
		if ((start -= len) < buf)
			goto out;
		memcpy(start, name, len);

		if (--start < buf)
			goto out;
		*start = '/';

		cgrp = cgrp->parent;
	} while (cgrp->parent);
	ret = 0;
	memmove(buf, start, buf + buflen - start);
out:
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL_GPL(cgroup_path);

/**
 * task_cgroup_path - cgroup path of a task in the first cgroup hierarchy
 * @task: target task
 * @buf: the buffer to write the path into
 * @buflen: the length of the buffer
 *
 * Determine @task's cgroup on the first (the one with the lowest non-zero
 * hierarchy_id) cgroup hierarchy and copy its path into @buf.  This
 * function grabs cgroup_mutex and shouldn't be used inside locks used by
 * cgroup controller callbacks.
 *
 * Returns 0 on success, fails with -%ENAMETOOLONG if @buflen is too short.
 */
int task_cgroup_path(struct task_struct *task, char *buf, size_t buflen)
{
	struct cgroupfs_root *root;
	struct cgroup *cgrp;
	int hierarchy_id = 1, ret = 0;

	if (buflen < 2)
		return -ENAMETOOLONG;

	mutex_lock(&cgroup_mutex);

	root = idr_get_next(&cgroup_hierarchy_idr, &hierarchy_id);

	if (root) {
		cgrp = task_cgroup_from_root(task, root);
		ret = cgroup_path(cgrp, buf, buflen);
	} else {
		/* if no hierarchy exists, everyone is in "/" */
		memcpy(buf, "/", 2);
	}

	mutex_unlock(&cgroup_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(task_cgroup_path);

/*
 * Control Group taskset
 */
struct task_and_cgroup {
	struct task_struct	*task;
	struct cgroup		*cgrp;
	struct css_set		*cset;
};

struct cgroup_taskset {
	struct task_and_cgroup	single;
	struct flex_array	*tc_array;
	int			tc_array_len;
	int			idx;
	struct cgroup		*cur_cgrp;
};

/**
 * cgroup_taskset_first - reset taskset and return the first task
 * @tset: taskset of interest
 *
 * @tset iteration is initialized and the first task is returned.
 */
struct task_struct *cgroup_taskset_first(struct cgroup_taskset *tset)
{
	if (tset->tc_array) {
		tset->idx = 0;
		return cgroup_taskset_next(tset);
	} else {
		tset->cur_cgrp = tset->single.cgrp;
		return tset->single.task;
	}
}
EXPORT_SYMBOL_GPL(cgroup_taskset_first);

/**
 * cgroup_taskset_next - iterate to the next task in taskset
 * @tset: taskset of interest
 *
 * Return the next task in @tset.  Iteration must have been initialized
 * with cgroup_taskset_first().
 */
struct task_struct *cgroup_taskset_next(struct cgroup_taskset *tset)
{
	struct task_and_cgroup *tc;

	if (!tset->tc_array || tset->idx >= tset->tc_array_len)
		return NULL;

	tc = flex_array_get(tset->tc_array, tset->idx++);
	tset->cur_cgrp = tc->cgrp;
	return tc->task;
}
EXPORT_SYMBOL_GPL(cgroup_taskset_next);

/**
 * cgroup_taskset_cur_css - return the matching css for the current task
 * @tset: taskset of interest
 * @subsys_id: the ID of the target subsystem
 *
 * Return the css for the current (last returned) task of @tset for
 * subsystem specified by @subsys_id.  This function must be preceded by
 * either cgroup_taskset_first() or cgroup_taskset_next().
 */
struct cgroup_subsys_state *cgroup_taskset_cur_css(struct cgroup_taskset *tset,
						   int subsys_id)
{
	return cgroup_css(tset->cur_cgrp, cgroup_subsys[subsys_id]);
}
EXPORT_SYMBOL_GPL(cgroup_taskset_cur_css);

/**
 * cgroup_taskset_size - return the number of tasks in taskset
 * @tset: taskset of interest
 */
int cgroup_taskset_size(struct cgroup_taskset *tset)
{
	return tset->tc_array ? tset->tc_array_len : 1;
}
EXPORT_SYMBOL_GPL(cgroup_taskset_size);


/*
 * cgroup_task_migrate - move a task from one cgroup to another.
 *
 * Must be called with cgroup_mutex and threadgroup locked.
 */
static void cgroup_task_migrate(struct cgroup *old_cgrp,
				struct task_struct *tsk,
				struct css_set *new_cset)
{
	struct css_set *old_cset;

	/*
	 * We are synchronized through threadgroup_lock() against PF_EXITING
	 * setting such that we can't race against cgroup_exit() changing the
	 * css_set to init_css_set and dropping the old one.
	 */
	WARN_ON_ONCE(tsk->flags & PF_EXITING);
	old_cset = task_css_set(tsk);

	task_lock(tsk);
	rcu_assign_pointer(tsk->cgroups, new_cset);
	task_unlock(tsk);

	/* Update the css_set linked lists if we're using them */
	write_lock(&css_set_lock);
	if (!list_empty(&tsk->cg_list))
		list_move(&tsk->cg_list, &new_cset->tasks);
	write_unlock(&css_set_lock);

	/*
	 * We just gained a reference on old_cset by taking it from the
	 * task. As trading it for new_cset is protected by cgroup_mutex,
	 * we're safe to drop it here; it will be freed under RCU.
	 */
	set_bit(CGRP_RELEASABLE, &old_cgrp->flags);
	put_css_set(old_cset);
}

/**
 * cgroup_attach_task - attach a task or a whole threadgroup to a cgroup
 * @cgrp: the cgroup to attach to
 * @tsk: the task or the leader of the threadgroup to be attached
 * @threadgroup: attach the whole threadgroup?
 *
 * Call holding cgroup_mutex and the group_rwsem of the leader. Will take
 * task_lock of @tsk or each thread in the threadgroup individually in turn.
 */
static int cgroup_attach_task(struct cgroup *cgrp, struct task_struct *tsk,
			      bool threadgroup)
{
	int retval, i, group_size;
	struct cgroup_subsys *ss, *failed_ss = NULL;
	struct cgroupfs_root *root = cgrp->root;
	/* threadgroup list cursor and array */
	struct task_struct *leader = tsk;
	struct task_and_cgroup *tc;
	struct flex_array *group;
	struct cgroup_taskset tset = { };

	/*
	 * step 0: in order to do expensive, possibly blocking operations for
	 * every thread, we cannot iterate the thread group list, since it needs
	 * rcu or tasklist locked. instead, build an array of all threads in the
	 * group - group_rwsem prevents new threads from appearing, and if
	 * threads exit, this will just be an over-estimate.
	 */
	if (threadgroup)
		group_size = get_nr_threads(tsk);
	else
		group_size = 1;
	/* flex_array supports very large thread-groups better than kmalloc. */
	group = flex_array_alloc(sizeof(*tc), group_size, GFP_KERNEL);
	if (!group)
		return -ENOMEM;
	/* pre-allocate to guarantee space while iterating in rcu read-side. */
	retval = flex_array_prealloc(group, 0, group_size, GFP_KERNEL);
	if (retval)
		goto out_free_group_list;

	i = 0;
	/*
	 * Prevent freeing of tasks while we take a snapshot. Tasks that are
	 * already PF_EXITING could be freed from underneath us unless we
	 * take an rcu_read_lock.
	 */
	rcu_read_lock();
	do {
		struct task_and_cgroup ent;

		/* @tsk either already exited or can't exit until the end */
		if (tsk->flags & PF_EXITING)
			goto next;

		/* as per above, nr_threads may decrease, but not increase. */
		BUG_ON(i >= group_size);
		ent.task = tsk;
		ent.cgrp = task_cgroup_from_root(tsk, root);
		/* nothing to do if this task is already in the cgroup */
		if (ent.cgrp == cgrp)
			goto next;
		/*
		 * saying GFP_ATOMIC has no effect here because we did prealloc
		 * earlier, but it's good form to communicate our expectations.
		 */
		retval = flex_array_put(group, i, &ent, GFP_ATOMIC);
		BUG_ON(retval != 0);
		i++;
	next:
		if (!threadgroup)
			break;
	} while_each_thread(leader, tsk);
	rcu_read_unlock();
	/* remember the number of threads in the array for later. */
	group_size = i;
	tset.tc_array = group;
	tset.tc_array_len = group_size;

	/* methods shouldn't be called if no task is actually migrating */
	retval = 0;
	if (!group_size)
		goto out_free_group_list;

	/*
	 * step 1: check that we can legitimately attach to the cgroup.
	 */
	for_each_root_subsys(root, ss) {
		struct cgroup_subsys_state *css = cgroup_css(cgrp, ss);

		if (ss->can_attach) {
			retval = ss->can_attach(css, &tset);
			if (retval) {
				failed_ss = ss;
				goto out_cancel_attach;
			}
		}
	}

	/*
	 * step 2: make sure css_sets exist for all threads to be migrated.
	 * we use find_css_set, which allocates a new one if necessary.
	 */
	for (i = 0; i < group_size; i++) {
		struct css_set *old_cset;

		tc = flex_array_get(group, i);
		old_cset = task_css_set(tc->task);
		tc->cset = find_css_set(old_cset, cgrp);
		if (!tc->cset) {
			retval = -ENOMEM;
			goto out_put_css_set_refs;
		}
	}

	/*
	 * step 3: now that we're guaranteed success wrt the css_sets,
	 * proceed to move all tasks to the new cgroup.  There are no
	 * failure cases after here, so this is the commit point.
	 */
	for (i = 0; i < group_size; i++) {
		tc = flex_array_get(group, i);
		cgroup_task_migrate(tc->cgrp, tc->task, tc->cset);
	}
	/* nothing is sensitive to fork() after this point. */

	/*
	 * step 4: do subsystem attach callbacks.
	 */
	for_each_root_subsys(root, ss) {
		struct cgroup_subsys_state *css = cgroup_css(cgrp, ss);

		if (ss->attach)
			ss->attach(css, &tset);
	}

	/*
	 * step 5: success! and cleanup
	 */
	retval = 0;
out_put_css_set_refs:
	if (retval) {
		for (i = 0; i < group_size; i++) {
			tc = flex_array_get(group, i);
			if (!tc->cset)
				break;
			put_css_set(tc->cset);
		}
	}
out_cancel_attach:
	if (retval) {
		for_each_root_subsys(root, ss) {
			struct cgroup_subsys_state *css = cgroup_css(cgrp, ss);

			if (ss == failed_ss)
				break;
			if (ss->cancel_attach)
				ss->cancel_attach(css, &tset);
		}
	}
out_free_group_list:
	flex_array_free(group);
	return retval;
}

/*
 * Find the task_struct of the task to attach by vpid and pass it along to the
 * function to attach either it or all tasks in its threadgroup. Will lock
 * cgroup_mutex and threadgroup; may take task_lock of task.
 */
static int attach_task_by_pid(struct cgroup *cgrp, u64 pid, bool threadgroup)
{
	struct task_struct *tsk;
	const struct cred *cred = current_cred(), *tcred;
	int ret;

	if (!cgroup_lock_live_group(cgrp))
		return -ENODEV;

retry_find_task:
	rcu_read_lock();
	if (pid) {
		tsk = find_task_by_vpid(pid);
		if (!tsk) {
			rcu_read_unlock();
			ret= -ESRCH;
			goto out_unlock_cgroup;
		}
		/*
		 * even if we're attaching all tasks in the thread group, we
		 * only need to check permissions on one of them.
		 */
		tcred = __task_cred(tsk);
		if (!uid_eq(cred->euid, GLOBAL_ROOT_UID) &&
		    !uid_eq(cred->euid, tcred->uid) &&
		    !uid_eq(cred->euid, tcred->suid)) {
			rcu_read_unlock();
			ret = -EACCES;
			goto out_unlock_cgroup;
		}
	} else
		tsk = current;

	if (threadgroup)
		tsk = tsk->group_leader;

	/*
	 * Workqueue threads may acquire PF_NO_SETAFFINITY and become
	 * trapped in a cpuset, or RT worker may be born in a cgroup
	 * with no rt_runtime allocated.  Just say no.
	 */
	if (tsk == kthreadd_task || (tsk->flags & PF_NO_SETAFFINITY)) {
		ret = -EINVAL;
		rcu_read_unlock();
		goto out_unlock_cgroup;
	}

	get_task_struct(tsk);
	rcu_read_unlock();

	threadgroup_lock(tsk);
	if (threadgroup) {
		if (!thread_group_leader(tsk)) {
			/*
			 * a race with de_thread from another thread's exec()
			 * may strip us of our leadership, if this happens,
			 * there is no choice but to throw this task away and
			 * try again; this is
			 * "double-double-toil-and-trouble-check locking".
			 */
			threadgroup_unlock(tsk);
			put_task_struct(tsk);
			goto retry_find_task;
		}
	}

	ret = cgroup_attach_task(cgrp, tsk, threadgroup);

	threadgroup_unlock(tsk);

	put_task_struct(tsk);
out_unlock_cgroup:
	mutex_unlock(&cgroup_mutex);
	return ret;
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

	mutex_lock(&cgroup_mutex);
	for_each_active_root(root) {
		struct cgroup *from_cgrp = task_cgroup_from_root(from, root);

		retval = cgroup_attach_task(from_cgrp, tsk, false);
		if (retval)
			break;
	}
	mutex_unlock(&cgroup_mutex);

	return retval;
}
EXPORT_SYMBOL_GPL(cgroup_attach_task_all);

static int cgroup_tasks_write(struct cgroup_subsys_state *css,
			      struct cftype *cft, u64 pid)
{
	return attach_task_by_pid(css->cgroup, pid, false);
}

static int cgroup_procs_write(struct cgroup_subsys_state *css,
			      struct cftype *cft, u64 tgid)
{
	return attach_task_by_pid(css->cgroup, tgid, true);
}

static int cgroup_release_agent_write(struct cgroup_subsys_state *css,
				      struct cftype *cft, const char *buffer)
{
	BUILD_BUG_ON(sizeof(css->cgroup->root->release_agent_path) < PATH_MAX);
	if (strlen(buffer) >= PATH_MAX)
		return -EINVAL;
	if (!cgroup_lock_live_group(css->cgroup))
		return -ENODEV;
	mutex_lock(&cgroup_root_mutex);
	strcpy(css->cgroup->root->release_agent_path, buffer);
	mutex_unlock(&cgroup_root_mutex);
	mutex_unlock(&cgroup_mutex);
	return 0;
}

static int cgroup_release_agent_show(struct cgroup_subsys_state *css,
				     struct cftype *cft, struct seq_file *seq)
{
	struct cgroup *cgrp = css->cgroup;

	if (!cgroup_lock_live_group(cgrp))
		return -ENODEV;
	seq_puts(seq, cgrp->root->release_agent_path);
	seq_putc(seq, '\n');
	mutex_unlock(&cgroup_mutex);
	return 0;
}

static int cgroup_sane_behavior_show(struct cgroup_subsys_state *css,
				     struct cftype *cft, struct seq_file *seq)
{
	seq_printf(seq, "%d\n", cgroup_sane_behavior(css->cgroup));
	return 0;
}

/* A buffer size big enough for numbers or short strings */
#define CGROUP_LOCAL_BUFFER_SIZE 64

static ssize_t cgroup_write_X64(struct cgroup_subsys_state *css,
				struct cftype *cft, struct file *file,
				const char __user *userbuf, size_t nbytes,
				loff_t *unused_ppos)
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
		retval = cft->write_u64(css, cft, val);
	} else {
		s64 val = simple_strtoll(strstrip(buffer), &end, 0);
		if (*end)
			return -EINVAL;
		retval = cft->write_s64(css, cft, val);
	}
	if (!retval)
		retval = nbytes;
	return retval;
}

static ssize_t cgroup_write_string(struct cgroup_subsys_state *css,
				   struct cftype *cft, struct file *file,
				   const char __user *userbuf, size_t nbytes,
				   loff_t *unused_ppos)
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
	retval = cft->write_string(css, cft, strstrip(buffer));
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
	struct cfent *cfe = __d_cfe(file->f_dentry);
	struct cftype *cft = __d_cft(file->f_dentry);
	struct cgroup_subsys_state *css = cfe->css;

	if (cft->write)
		return cft->write(css, cft, file, buf, nbytes, ppos);
	if (cft->write_u64 || cft->write_s64)
		return cgroup_write_X64(css, cft, file, buf, nbytes, ppos);
	if (cft->write_string)
		return cgroup_write_string(css, cft, file, buf, nbytes, ppos);
	if (cft->trigger) {
		int ret = cft->trigger(css, (unsigned int)cft->private);
		return ret ? ret : nbytes;
	}
	return -EINVAL;
}

static ssize_t cgroup_read_u64(struct cgroup_subsys_state *css,
			       struct cftype *cft, struct file *file,
			       char __user *buf, size_t nbytes, loff_t *ppos)
{
	char tmp[CGROUP_LOCAL_BUFFER_SIZE];
	u64 val = cft->read_u64(css, cft);
	int len = sprintf(tmp, "%llu\n", (unsigned long long) val);

	return simple_read_from_buffer(buf, nbytes, ppos, tmp, len);
}

static ssize_t cgroup_read_s64(struct cgroup_subsys_state *css,
			       struct cftype *cft, struct file *file,
			       char __user *buf, size_t nbytes, loff_t *ppos)
{
	char tmp[CGROUP_LOCAL_BUFFER_SIZE];
	s64 val = cft->read_s64(css, cft);
	int len = sprintf(tmp, "%lld\n", (long long) val);

	return simple_read_from_buffer(buf, nbytes, ppos, tmp, len);
}

static ssize_t cgroup_file_read(struct file *file, char __user *buf,
				size_t nbytes, loff_t *ppos)
{
	struct cfent *cfe = __d_cfe(file->f_dentry);
	struct cftype *cft = __d_cft(file->f_dentry);
	struct cgroup_subsys_state *css = cfe->css;

	if (cft->read)
		return cft->read(css, cft, file, buf, nbytes, ppos);
	if (cft->read_u64)
		return cgroup_read_u64(css, cft, file, buf, nbytes, ppos);
	if (cft->read_s64)
		return cgroup_read_s64(css, cft, file, buf, nbytes, ppos);
	return -EINVAL;
}

/*
 * seqfile ops/methods for returning structured data. Currently just
 * supports string->u64 maps, but can be extended in future.
 */

static int cgroup_map_add(struct cgroup_map_cb *cb, const char *key, u64 value)
{
	struct seq_file *sf = cb->state;
	return seq_printf(sf, "%s %llu\n", key, (unsigned long long)value);
}

static int cgroup_seqfile_show(struct seq_file *m, void *arg)
{
	struct cfent *cfe = m->private;
	struct cftype *cft = cfe->type;
	struct cgroup_subsys_state *css = cfe->css;

	if (cft->read_map) {
		struct cgroup_map_cb cb = {
			.fill = cgroup_map_add,
			.state = m,
		};
		return cft->read_map(css, cft, &cb);
	}
	return cft->read_seq_string(css, cft, m);
}

static const struct file_operations cgroup_seqfile_operations = {
	.read = seq_read,
	.write = cgroup_file_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int cgroup_file_open(struct inode *inode, struct file *file)
{
	struct cfent *cfe = __d_cfe(file->f_dentry);
	struct cftype *cft = __d_cft(file->f_dentry);
	struct cgroup *cgrp = __d_cgrp(cfe->dentry->d_parent);
	struct cgroup_subsys_state *css;
	int err;

	err = generic_file_open(inode, file);
	if (err)
		return err;

	/*
	 * If the file belongs to a subsystem, pin the css.  Will be
	 * unpinned either on open failure or release.  This ensures that
	 * @css stays alive for all file operations.
	 */
	rcu_read_lock();
	css = cgroup_css(cgrp, cft->ss);
	if (cft->ss && !css_tryget(css))
		css = NULL;
	rcu_read_unlock();

	if (!css)
		return -ENODEV;

	/*
	 * @cfe->css is used by read/write/close to determine the
	 * associated css.  @file->private_data would be a better place but
	 * that's already used by seqfile.  Multiple accessors may use it
	 * simultaneously which is okay as the association never changes.
	 */
	WARN_ON_ONCE(cfe->css && cfe->css != css);
	cfe->css = css;

	if (cft->read_map || cft->read_seq_string) {
		file->f_op = &cgroup_seqfile_operations;
		err = single_open(file, cgroup_seqfile_show, cfe);
	} else if (cft->open) {
		err = cft->open(inode, file);
	}

	if (css->ss && err)
		css_put(css);
	return err;
}

static int cgroup_file_release(struct inode *inode, struct file *file)
{
	struct cfent *cfe = __d_cfe(file->f_dentry);
	struct cftype *cft = __d_cft(file->f_dentry);
	struct cgroup_subsys_state *css = cfe->css;
	int ret = 0;

	if (cft->release)
		ret = cft->release(inode, file);
	if (css->ss)
		css_put(css);
	return ret;
}

/*
 * cgroup_rename - Only allow simple rename of directories in place.
 */
static int cgroup_rename(struct inode *old_dir, struct dentry *old_dentry,
			    struct inode *new_dir, struct dentry *new_dentry)
{
	int ret;
	struct cgroup_name *name, *old_name;
	struct cgroup *cgrp;

	/*
	 * It's convinient to use parent dir's i_mutex to protected
	 * cgrp->name.
	 */
	lockdep_assert_held(&old_dir->i_mutex);

	if (!S_ISDIR(old_dentry->d_inode->i_mode))
		return -ENOTDIR;
	if (new_dentry->d_inode)
		return -EEXIST;
	if (old_dir != new_dir)
		return -EIO;

	cgrp = __d_cgrp(old_dentry);

	/*
	 * This isn't a proper migration and its usefulness is very
	 * limited.  Disallow if sane_behavior.
	 */
	if (cgroup_sane_behavior(cgrp))
		return -EPERM;

	name = cgroup_alloc_name(new_dentry);
	if (!name)
		return -ENOMEM;

	ret = simple_rename(old_dir, old_dentry, new_dir, new_dentry);
	if (ret) {
		kfree(name);
		return ret;
	}

	old_name = rcu_dereference_protected(cgrp->name, true);
	rcu_assign_pointer(cgrp->name, name);

	kfree_rcu(old_name, rcu_head);
	return 0;
}

static struct simple_xattrs *__d_xattrs(struct dentry *dentry)
{
	if (S_ISDIR(dentry->d_inode->i_mode))
		return &__d_cgrp(dentry)->xattrs;
	else
		return &__d_cfe(dentry)->xattrs;
}

static inline int xattr_enabled(struct dentry *dentry)
{
	struct cgroupfs_root *root = dentry->d_sb->s_fs_info;
	return root->flags & CGRP_ROOT_XATTR;
}

static bool is_valid_xattr(const char *name)
{
	if (!strncmp(name, XATTR_TRUSTED_PREFIX, XATTR_TRUSTED_PREFIX_LEN) ||
	    !strncmp(name, XATTR_SECURITY_PREFIX, XATTR_SECURITY_PREFIX_LEN))
		return true;
	return false;
}

static int cgroup_setxattr(struct dentry *dentry, const char *name,
			   const void *val, size_t size, int flags)
{
	if (!xattr_enabled(dentry))
		return -EOPNOTSUPP;
	if (!is_valid_xattr(name))
		return -EINVAL;
	return simple_xattr_set(__d_xattrs(dentry), name, val, size, flags);
}

static int cgroup_removexattr(struct dentry *dentry, const char *name)
{
	if (!xattr_enabled(dentry))
		return -EOPNOTSUPP;
	if (!is_valid_xattr(name))
		return -EINVAL;
	return simple_xattr_remove(__d_xattrs(dentry), name);
}

static ssize_t cgroup_getxattr(struct dentry *dentry, const char *name,
			       void *buf, size_t size)
{
	if (!xattr_enabled(dentry))
		return -EOPNOTSUPP;
	if (!is_valid_xattr(name))
		return -EINVAL;
	return simple_xattr_get(__d_xattrs(dentry), name, buf, size);
}

static ssize_t cgroup_listxattr(struct dentry *dentry, char *buf, size_t size)
{
	if (!xattr_enabled(dentry))
		return -EOPNOTSUPP;
	return simple_xattr_list(__d_xattrs(dentry), buf, size);
}

static const struct file_operations cgroup_file_operations = {
	.read = cgroup_file_read,
	.write = cgroup_file_write,
	.llseek = generic_file_llseek,
	.open = cgroup_file_open,
	.release = cgroup_file_release,
};

static const struct inode_operations cgroup_file_inode_operations = {
	.setxattr = cgroup_setxattr,
	.getxattr = cgroup_getxattr,
	.listxattr = cgroup_listxattr,
	.removexattr = cgroup_removexattr,
};

static const struct inode_operations cgroup_dir_inode_operations = {
	.lookup = simple_lookup,
	.mkdir = cgroup_mkdir,
	.rmdir = cgroup_rmdir,
	.rename = cgroup_rename,
	.setxattr = cgroup_setxattr,
	.getxattr = cgroup_getxattr,
	.listxattr = cgroup_listxattr,
	.removexattr = cgroup_removexattr,
};

/*
 * Check if a file is a control file
 */
static inline struct cftype *__file_cft(struct file *file)
{
	if (file_inode(file)->i_fop != &cgroup_file_operations)
		return ERR_PTR(-EINVAL);
	return __d_cft(file->f_dentry);
}

static int cgroup_create_file(struct dentry *dentry, umode_t mode,
				struct super_block *sb)
{
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
		inc_nlink(dentry->d_parent->d_inode);

		/*
		 * Control reaches here with cgroup_mutex held.
		 * @inode->i_mutex should nest outside cgroup_mutex but we
		 * want to populate it immediately without releasing
		 * cgroup_mutex.  As @inode isn't visible to anyone else
		 * yet, trylock will always succeed without affecting
		 * lockdep checks.
		 */
		WARN_ON_ONCE(!mutex_trylock(&inode->i_mutex));
	} else if (S_ISREG(mode)) {
		inode->i_size = 0;
		inode->i_fop = &cgroup_file_operations;
		inode->i_op = &cgroup_file_inode_operations;
	}
	d_instantiate(dentry, inode);
	dget(dentry);	/* Extra count - pin the dentry in core */
	return 0;
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
static umode_t cgroup_file_mode(const struct cftype *cft)
{
	umode_t mode = 0;

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

static int cgroup_add_file(struct cgroup *cgrp, struct cftype *cft)
{
	struct dentry *dir = cgrp->dentry;
	struct cgroup *parent = __d_cgrp(dir);
	struct dentry *dentry;
	struct cfent *cfe;
	int error;
	umode_t mode;
	char name[MAX_CGROUP_TYPE_NAMELEN + MAX_CFTYPE_NAME + 2] = { 0 };

	if (cft->ss && !(cft->flags & CFTYPE_NO_PREFIX) &&
	    !(cgrp->root->flags & CGRP_ROOT_NOPREFIX)) {
		strcpy(name, cft->ss->name);
		strcat(name, ".");
	}
	strcat(name, cft->name);

	BUG_ON(!mutex_is_locked(&dir->d_inode->i_mutex));

	cfe = kzalloc(sizeof(*cfe), GFP_KERNEL);
	if (!cfe)
		return -ENOMEM;

	dentry = lookup_one_len(name, dir, strlen(name));
	if (IS_ERR(dentry)) {
		error = PTR_ERR(dentry);
		goto out;
	}

	cfe->type = (void *)cft;
	cfe->dentry = dentry;
	dentry->d_fsdata = cfe;
	simple_xattrs_init(&cfe->xattrs);

	mode = cgroup_file_mode(cft);
	error = cgroup_create_file(dentry, mode | S_IFREG, cgrp->root->sb);
	if (!error) {
		list_add_tail(&cfe->node, &parent->files);
		cfe = NULL;
	}
	dput(dentry);
out:
	kfree(cfe);
	return error;
}

/**
 * cgroup_addrm_files - add or remove files to a cgroup directory
 * @cgrp: the target cgroup
 * @cfts: array of cftypes to be added
 * @is_add: whether to add or remove
 *
 * Depending on @is_add, add or remove files defined by @cfts on @cgrp.
 * For removals, this function never fails.  If addition fails, this
 * function doesn't remove files already added.  The caller is responsible
 * for cleaning up.
 */
static int cgroup_addrm_files(struct cgroup *cgrp, struct cftype cfts[],
			      bool is_add)
{
	struct cftype *cft;
	int ret;

	lockdep_assert_held(&cgrp->dentry->d_inode->i_mutex);
	lockdep_assert_held(&cgroup_mutex);

	for (cft = cfts; cft->name[0] != '\0'; cft++) {
		/* does cft->flags tell us to skip this file on @cgrp? */
		if ((cft->flags & CFTYPE_INSANE) && cgroup_sane_behavior(cgrp))
			continue;
		if ((cft->flags & CFTYPE_NOT_ON_ROOT) && !cgrp->parent)
			continue;
		if ((cft->flags & CFTYPE_ONLY_ON_ROOT) && cgrp->parent)
			continue;

		if (is_add) {
			ret = cgroup_add_file(cgrp, cft);
			if (ret) {
				pr_warn("cgroup_addrm_files: failed to add %s, err=%d\n",
					cft->name, ret);
				return ret;
			}
		} else {
			cgroup_rm_file(cgrp, cft);
		}
	}
	return 0;
}

static void cgroup_cfts_prepare(void)
	__acquires(&cgroup_mutex)
{
	/*
	 * Thanks to the entanglement with vfs inode locking, we can't walk
	 * the existing cgroups under cgroup_mutex and create files.
	 * Instead, we use css_for_each_descendant_pre() and drop RCU read
	 * lock before calling cgroup_addrm_files().
	 */
	mutex_lock(&cgroup_mutex);
}

static int cgroup_cfts_commit(struct cftype *cfts, bool is_add)
	__releases(&cgroup_mutex)
{
	LIST_HEAD(pending);
	struct cgroup_subsys *ss = cfts[0].ss;
	struct cgroup *root = &ss->root->top_cgroup;
	struct super_block *sb = ss->root->sb;
	struct dentry *prev = NULL;
	struct inode *inode;
	struct cgroup_subsys_state *css;
	u64 update_before;
	int ret = 0;

	/* %NULL @cfts indicates abort and don't bother if @ss isn't attached */
	if (!cfts || ss->root == &cgroup_dummy_root ||
	    !atomic_inc_not_zero(&sb->s_active)) {
		mutex_unlock(&cgroup_mutex);
		return 0;
	}

	/*
	 * All cgroups which are created after we drop cgroup_mutex will
	 * have the updated set of files, so we only need to update the
	 * cgroups created before the current @cgroup_serial_nr_next.
	 */
	update_before = cgroup_serial_nr_next;

	mutex_unlock(&cgroup_mutex);

	/* add/rm files for all cgroups created before */
	rcu_read_lock();
	css_for_each_descendant_pre(css, cgroup_css(root, ss)) {
		struct cgroup *cgrp = css->cgroup;

		if (cgroup_is_dead(cgrp))
			continue;

		inode = cgrp->dentry->d_inode;
		dget(cgrp->dentry);
		rcu_read_unlock();

		dput(prev);
		prev = cgrp->dentry;

		mutex_lock(&inode->i_mutex);
		mutex_lock(&cgroup_mutex);
		if (cgrp->serial_nr < update_before && !cgroup_is_dead(cgrp))
			ret = cgroup_addrm_files(cgrp, cfts, is_add);
		mutex_unlock(&cgroup_mutex);
		mutex_unlock(&inode->i_mutex);

		rcu_read_lock();
		if (ret)
			break;
	}
	rcu_read_unlock();
	dput(prev);
	deactivate_super(sb);
	return ret;
}

/**
 * cgroup_add_cftypes - add an array of cftypes to a subsystem
 * @ss: target cgroup subsystem
 * @cfts: zero-length name terminated array of cftypes
 *
 * Register @cfts to @ss.  Files described by @cfts are created for all
 * existing cgroups to which @ss is attached and all future cgroups will
 * have them too.  This function can be called anytime whether @ss is
 * attached or not.
 *
 * Returns 0 on successful registration, -errno on failure.  Note that this
 * function currently returns 0 as long as @cfts registration is successful
 * even if some file creation attempts on existing cgroups fail.
 */
int cgroup_add_cftypes(struct cgroup_subsys *ss, struct cftype *cfts)
{
	struct cftype_set *set;
	struct cftype *cft;
	int ret;

	set = kzalloc(sizeof(*set), GFP_KERNEL);
	if (!set)
		return -ENOMEM;

	for (cft = cfts; cft->name[0] != '\0'; cft++)
		cft->ss = ss;

	cgroup_cfts_prepare();
	set->cfts = cfts;
	list_add_tail(&set->node, &ss->cftsets);
	ret = cgroup_cfts_commit(cfts, true);
	if (ret)
		cgroup_rm_cftypes(cfts);
	return ret;
}
EXPORT_SYMBOL_GPL(cgroup_add_cftypes);

/**
 * cgroup_rm_cftypes - remove an array of cftypes from a subsystem
 * @cfts: zero-length name terminated array of cftypes
 *
 * Unregister @cfts.  Files described by @cfts are removed from all
 * existing cgroups and all future cgroups won't have them either.  This
 * function can be called anytime whether @cfts' subsys is attached or not.
 *
 * Returns 0 on successful unregistration, -ENOENT if @cfts is not
 * registered.
 */
int cgroup_rm_cftypes(struct cftype *cfts)
{
	struct cftype_set *set;

	if (!cfts || !cfts[0].ss)
		return -ENOENT;

	cgroup_cfts_prepare();

	list_for_each_entry(set, &cfts[0].ss->cftsets, node) {
		if (set->cfts == cfts) {
			list_del(&set->node);
			kfree(set);
			cgroup_cfts_commit(cfts, false);
			return 0;
		}
	}

	cgroup_cfts_commit(NULL, false);
	return -ENOENT;
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
	struct cgrp_cset_link *link;

	read_lock(&css_set_lock);
	list_for_each_entry(link, &cgrp->cset_links, cset_link)
		count += atomic_read(&link->cset->refcount);
	read_unlock(&css_set_lock);
	return count;
}

/*
 * To reduce the fork() overhead for systems that are not actually using
 * their cgroups capability, we don't maintain the lists running through
 * each css_set to its tasks until we see the list actually used - in other
 * words after the first call to css_task_iter_start().
 */
static void cgroup_enable_task_cg_lists(void)
{
	struct task_struct *p, *g;
	write_lock(&css_set_lock);
	use_task_css_set_links = 1;
	/*
	 * We need tasklist_lock because RCU is not safe against
	 * while_each_thread(). Besides, a forking task that has passed
	 * cgroup_post_fork() without seeing use_task_css_set_links = 1
	 * is not guaranteed to have its child immediately visible in the
	 * tasklist if we walk through it with RCU.
	 */
	read_lock(&tasklist_lock);
	do_each_thread(g, p) {
		task_lock(p);
		/*
		 * We should check if the process is exiting, otherwise
		 * it will race with cgroup_exit() in that the list
		 * entry won't be deleted though the process has exited.
		 */
		if (!(p->flags & PF_EXITING) && list_empty(&p->cg_list))
			list_add(&p->cg_list, &task_css_set(p)->tasks);
		task_unlock(p);
	} while_each_thread(g, p);
	read_unlock(&tasklist_lock);
	write_unlock(&css_set_lock);
}

/**
 * css_next_child - find the next child of a given css
 * @pos_css: the current position (%NULL to initiate traversal)
 * @parent_css: css whose children to walk
 *
 * This function returns the next child of @parent_css and should be called
 * under RCU read lock.  The only requirement is that @parent_css and
 * @pos_css are accessible.  The next sibling is guaranteed to be returned
 * regardless of their states.
 */
struct cgroup_subsys_state *
css_next_child(struct cgroup_subsys_state *pos_css,
	       struct cgroup_subsys_state *parent_css)
{
	struct cgroup *pos = pos_css ? pos_css->cgroup : NULL;
	struct cgroup *cgrp = parent_css->cgroup;
	struct cgroup *next;

	WARN_ON_ONCE(!rcu_read_lock_held());

	/*
	 * @pos could already have been removed.  Once a cgroup is removed,
	 * its ->sibling.next is no longer updated when its next sibling
	 * changes.  As CGRP_DEAD assertion is serialized and happens
	 * before the cgroup is taken off the ->sibling list, if we see it
	 * unasserted, it's guaranteed that the next sibling hasn't
	 * finished its grace period even if it's already removed, and thus
	 * safe to dereference from this RCU critical section.  If
	 * ->sibling.next is inaccessible, cgroup_is_dead() is guaranteed
	 * to be visible as %true here.
	 *
	 * If @pos is dead, its next pointer can't be dereferenced;
	 * however, as each cgroup is given a monotonically increasing
	 * unique serial number and always appended to the sibling list,
	 * the next one can be found by walking the parent's children until
	 * we see a cgroup with higher serial number than @pos's.  While
	 * this path can be slower, it's taken only when either the current
	 * cgroup is removed or iteration and removal race.
	 */
	if (!pos) {
		next = list_entry_rcu(cgrp->children.next, struct cgroup, sibling);
	} else if (likely(!cgroup_is_dead(pos))) {
		next = list_entry_rcu(pos->sibling.next, struct cgroup, sibling);
	} else {
		list_for_each_entry_rcu(next, &cgrp->children, sibling)
			if (next->serial_nr > pos->serial_nr)
				break;
	}

	if (&next->sibling == &cgrp->children)
		return NULL;

	return cgroup_css(next, parent_css->ss);
}
EXPORT_SYMBOL_GPL(css_next_child);

/**
 * css_next_descendant_pre - find the next descendant for pre-order walk
 * @pos: the current position (%NULL to initiate traversal)
 * @root: css whose descendants to walk
 *
 * To be used by css_for_each_descendant_pre().  Find the next descendant
 * to visit for pre-order traversal of @root's descendants.  @root is
 * included in the iteration and the first node to be visited.
 *
 * While this function requires RCU read locking, it doesn't require the
 * whole traversal to be contained in a single RCU critical section.  This
 * function will return the correct next descendant as long as both @pos
 * and @root are accessible and @pos is a descendant of @root.
 */
struct cgroup_subsys_state *
css_next_descendant_pre(struct cgroup_subsys_state *pos,
			struct cgroup_subsys_state *root)
{
	struct cgroup_subsys_state *next;

	WARN_ON_ONCE(!rcu_read_lock_held());

	/* if first iteration, visit @root */
	if (!pos)
		return root;

	/* visit the first child if exists */
	next = css_next_child(NULL, pos);
	if (next)
		return next;

	/* no child, visit my or the closest ancestor's next sibling */
	while (pos != root) {
		next = css_next_child(pos, css_parent(pos));
		if (next)
			return next;
		pos = css_parent(pos);
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(css_next_descendant_pre);

/**
 * css_rightmost_descendant - return the rightmost descendant of a css
 * @pos: css of interest
 *
 * Return the rightmost descendant of @pos.  If there's no descendant, @pos
 * is returned.  This can be used during pre-order traversal to skip
 * subtree of @pos.
 *
 * While this function requires RCU read locking, it doesn't require the
 * whole traversal to be contained in a single RCU critical section.  This
 * function will return the correct rightmost descendant as long as @pos is
 * accessible.
 */
struct cgroup_subsys_state *
css_rightmost_descendant(struct cgroup_subsys_state *pos)
{
	struct cgroup_subsys_state *last, *tmp;

	WARN_ON_ONCE(!rcu_read_lock_held());

	do {
		last = pos;
		/* ->prev isn't RCU safe, walk ->next till the end */
		pos = NULL;
		css_for_each_child(tmp, last)
			pos = tmp;
	} while (pos);

	return last;
}
EXPORT_SYMBOL_GPL(css_rightmost_descendant);

static struct cgroup_subsys_state *
css_leftmost_descendant(struct cgroup_subsys_state *pos)
{
	struct cgroup_subsys_state *last;

	do {
		last = pos;
		pos = css_next_child(NULL, pos);
	} while (pos);

	return last;
}

/**
 * css_next_descendant_post - find the next descendant for post-order walk
 * @pos: the current position (%NULL to initiate traversal)
 * @root: css whose descendants to walk
 *
 * To be used by css_for_each_descendant_post().  Find the next descendant
 * to visit for post-order traversal of @root's descendants.  @root is
 * included in the iteration and the last node to be visited.
 *
 * While this function requires RCU read locking, it doesn't require the
 * whole traversal to be contained in a single RCU critical section.  This
 * function will return the correct next descendant as long as both @pos
 * and @cgroup are accessible and @pos is a descendant of @cgroup.
 */
struct cgroup_subsys_state *
css_next_descendant_post(struct cgroup_subsys_state *pos,
			 struct cgroup_subsys_state *root)
{
	struct cgroup_subsys_state *next;

	WARN_ON_ONCE(!rcu_read_lock_held());

	/* if first iteration, visit leftmost descendant which may be @root */
	if (!pos)
		return css_leftmost_descendant(root);

	/* if we visited @root, we're done */
	if (pos == root)
		return NULL;

	/* if there's an unvisited sibling, visit its leftmost descendant */
	next = css_next_child(pos, css_parent(pos));
	if (next)
		return css_leftmost_descendant(next);

	/* no sibling left, visit parent */
	return css_parent(pos);
}
EXPORT_SYMBOL_GPL(css_next_descendant_post);

/**
 * css_advance_task_iter - advance a task itererator to the next css_set
 * @it: the iterator to advance
 *
 * Advance @it to the next css_set to walk.
 */
static void css_advance_task_iter(struct css_task_iter *it)
{
	struct list_head *l = it->cset_link;
	struct cgrp_cset_link *link;
	struct css_set *cset;

	/* Advance to the next non-empty css_set */
	do {
		l = l->next;
		if (l == &it->origin_css->cgroup->cset_links) {
			it->cset_link = NULL;
			return;
		}
		link = list_entry(l, struct cgrp_cset_link, cset_link);
		cset = link->cset;
	} while (list_empty(&cset->tasks));
	it->cset_link = l;
	it->task = cset->tasks.next;
}

/**
 * css_task_iter_start - initiate task iteration
 * @css: the css to walk tasks of
 * @it: the task iterator to use
 *
 * Initiate iteration through the tasks of @css.  The caller can call
 * css_task_iter_next() to walk through the tasks until the function
 * returns NULL.  On completion of iteration, css_task_iter_end() must be
 * called.
 *
 * Note that this function acquires a lock which is released when the
 * iteration finishes.  The caller can't sleep while iteration is in
 * progress.
 */
void css_task_iter_start(struct cgroup_subsys_state *css,
			 struct css_task_iter *it)
	__acquires(css_set_lock)
{
	/*
	 * The first time anyone tries to iterate across a css, we need to
	 * enable the list linking each css_set to its tasks, and fix up
	 * all existing tasks.
	 */
	if (!use_task_css_set_links)
		cgroup_enable_task_cg_lists();

	read_lock(&css_set_lock);

	it->origin_css = css;
	it->cset_link = &css->cgroup->cset_links;

	css_advance_task_iter(it);
}

/**
 * css_task_iter_next - return the next task for the iterator
 * @it: the task iterator being iterated
 *
 * The "next" function for task iteration.  @it should have been
 * initialized via css_task_iter_start().  Returns NULL when the iteration
 * reaches the end.
 */
struct task_struct *css_task_iter_next(struct css_task_iter *it)
{
	struct task_struct *res;
	struct list_head *l = it->task;
	struct cgrp_cset_link *link;

	/* If the iterator cg is NULL, we have no tasks */
	if (!it->cset_link)
		return NULL;
	res = list_entry(l, struct task_struct, cg_list);
	/* Advance iterator to find next entry */
	l = l->next;
	link = list_entry(it->cset_link, struct cgrp_cset_link, cset_link);
	if (l == &link->cset->tasks) {
		/*
		 * We reached the end of this task list - move on to the
		 * next cgrp_cset_link.
		 */
		css_advance_task_iter(it);
	} else {
		it->task = l;
	}
	return res;
}

/**
 * css_task_iter_end - finish task iteration
 * @it: the task iterator to finish
 *
 * Finish task iteration started by css_task_iter_start().
 */
void css_task_iter_end(struct css_task_iter *it)
	__releases(css_set_lock)
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
 * css_scan_tasks - iterate though all the tasks in a css
 * @css: the css to iterate tasks of
 * @test: optional test callback
 * @process: process callback
 * @data: data passed to @test and @process
 * @heap: optional pre-allocated heap used for task iteration
 *
 * Iterate through all the tasks in @css, calling @test for each, and if it
 * returns %true, call @process for it also.
 *
 * @test may be NULL, meaning always true (select all tasks), which
 * effectively duplicates css_task_iter_{start,next,end}() but does not
 * lock css_set_lock for the call to @process.
 *
 * It is guaranteed that @process will act on every task that is a member
 * of @css for the duration of this call.  This function may or may not
 * call @process for tasks that exit or move to a different css during the
 * call, or are forked or move into the css during the call.
 *
 * Note that @test may be called with locks held, and may in some
 * situations be called multiple times for the same task, so it should be
 * cheap.
 *
 * If @heap is non-NULL, a heap has been pre-allocated and will be used for
 * heap operations (and its "gt" member will be overwritten), else a
 * temporary heap will be used (allocation of which may cause this function
 * to fail).
 */
int css_scan_tasks(struct cgroup_subsys_state *css,
		   bool (*test)(struct task_struct *, void *),
		   void (*process)(struct task_struct *, void *),
		   void *data, struct ptr_heap *heap)
{
	int retval, i;
	struct css_task_iter it;
	struct task_struct *p, *dropped;
	/* Never dereference latest_task, since it's not refcounted */
	struct task_struct *latest_task = NULL;
	struct ptr_heap tmp_heap;
	struct timespec latest_time = { 0, 0 };

	if (heap) {
		/* The caller supplied our heap and pre-allocated its memory */
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
	 * Scan tasks in the css, using the @test callback to determine
	 * which are of interest, and invoking @process callback on the
	 * ones which need an update.  Since we don't want to hold any
	 * locks during the task updates, gather tasks to be processed in a
	 * heap structure.  The heap is sorted by descending task start
	 * time.  If the statically-sized heap fills up, we overflow tasks
	 * that started later, and in future iterations only consider tasks
	 * that started after the latest task in the previous pass. This
	 * guarantees forward progress and that we don't miss any tasks.
	 */
	heap->size = 0;
	css_task_iter_start(css, &it);
	while ((p = css_task_iter_next(&it))) {
		/*
		 * Only affect tasks that qualify per the caller's callback,
		 * if he provided one
		 */
		if (test && !test(p, data))
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
	css_task_iter_end(&it);

	if (heap->size) {
		for (i = 0; i < heap->size; i++) {
			struct task_struct *q = heap->ptrs[i];
			if (i == 0) {
				latest_time = q->start_time;
				latest_task = q;
			}
			/* Process the task per the caller's callback */
			process(q, data);
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

static void cgroup_transfer_one_task(struct task_struct *task, void *data)
{
	struct cgroup *new_cgroup = data;

	mutex_lock(&cgroup_mutex);
	cgroup_attach_task(new_cgroup, task, false);
	mutex_unlock(&cgroup_mutex);
}

/**
 * cgroup_trasnsfer_tasks - move tasks from one cgroup to another
 * @to: cgroup to which the tasks will be moved
 * @from: cgroup in which the tasks currently reside
 */
int cgroup_transfer_tasks(struct cgroup *to, struct cgroup *from)
{
	return css_scan_tasks(&from->dummy_css, NULL, cgroup_transfer_one_task,
			      to, NULL);
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

/* which pidlist file are we talking about? */
enum cgroup_filetype {
	CGROUP_FILE_PROCS,
	CGROUP_FILE_TASKS,
};

/*
 * A pidlist is a list of pids that virtually represents the contents of one
 * of the cgroup files ("procs" or "tasks"). We keep a list of such pidlists,
 * a pair (one each for procs, tasks) for each pid namespace that's relevant
 * to the cgroup.
 */
struct cgroup_pidlist {
	/*
	 * used to find which pidlist is wanted. doesn't change as long as
	 * this particular list stays in the list.
	*/
	struct { enum cgroup_filetype type; struct pid_namespace *ns; } key;
	/* array of xids */
	pid_t *list;
	/* how many elements the above list has */
	int length;
	/* how many files are using the current array */
	int use_count;
	/* each of these stored in a list by its cgroup */
	struct list_head links;
	/* pointer to the cgroup we belong to, for list removal purposes */
	struct cgroup *owner;
	/* protects the other fields */
	struct rw_semaphore rwsem;
};

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

/*
 * pidlist_uniq - given a kmalloc()ed list, strip out all duplicate entries
 * Returns the number of unique elements.
 */
static int pidlist_uniq(pid_t *list, int length)
{
	int src, dest = 1;

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
	struct pid_namespace *ns = task_active_pid_ns(current);

	/*
	 * We can't drop the pidlist_mutex before taking the l->rwsem in case
	 * the last ref-holder is trying to remove l from the list at the same
	 * time. Holding the pidlist_mutex precludes somebody taking whichever
	 * list we find out from under us - compare release_pid_array().
	 */
	mutex_lock(&cgrp->pidlist_mutex);
	list_for_each_entry(l, &cgrp->pidlists, links) {
		if (l->key.type == type && l->key.ns == ns) {
			/* make sure l doesn't vanish out from under us */
			down_write(&l->rwsem);
			mutex_unlock(&cgrp->pidlist_mutex);
			return l;
		}
	}
	/* entry not found; create a new one */
	l = kzalloc(sizeof(struct cgroup_pidlist), GFP_KERNEL);
	if (!l) {
		mutex_unlock(&cgrp->pidlist_mutex);
		return l;
	}
	init_rwsem(&l->rwsem);
	down_write(&l->rwsem);
	l->key.type = type;
	l->key.ns = get_pid_ns(ns);
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
	struct css_task_iter it;
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
	css_task_iter_start(&cgrp->dummy_css, &it);
	while ((tsk = css_task_iter_next(&it))) {
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
	css_task_iter_end(&it);
	length = n;
	/* now sort & (if procs) strip out duplicates */
	sort(array, length, sizeof(pid_t), cmppid, NULL);
	if (type == CGROUP_FILE_PROCS)
		length = pidlist_uniq(array, length);
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
	up_write(&l->rwsem);
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
	struct css_task_iter it;
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

	css_task_iter_start(&cgrp->dummy_css, &it);
	while ((tsk = css_task_iter_next(&it))) {
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
	css_task_iter_end(&it);

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

	down_read(&l->rwsem);
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
	up_read(&l->rwsem);
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
	down_write(&l->rwsem);
	BUG_ON(!l->use_count);
	if (!--l->use_count) {
		/* we're the last user if refcount is 0; remove and free */
		list_del(&l->links);
		mutex_unlock(&l->owner->pidlist_mutex);
		pidlist_free(l->list);
		put_pid_ns(l->key.ns);
		up_write(&l->rwsem);
		kfree(l);
		return;
	}
	mutex_unlock(&l->owner->pidlist_mutex);
	up_write(&l->rwsem);
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

static u64 cgroup_read_notify_on_release(struct cgroup_subsys_state *css,
					 struct cftype *cft)
{
	return notify_on_release(css->cgroup);
}

static int cgroup_write_notify_on_release(struct cgroup_subsys_state *css,
					  struct cftype *cft, u64 val)
{
	clear_bit(CGRP_RELEASABLE, &css->cgroup->flags);
	if (val)
		set_bit(CGRP_NOTIFY_ON_RELEASE, &css->cgroup->flags);
	else
		clear_bit(CGRP_NOTIFY_ON_RELEASE, &css->cgroup->flags);
	return 0;
}

/*
 * When dput() is called asynchronously, if umount has been done and
 * then deactivate_super() in cgroup_free_fn() kills the superblock,
 * there's a small window that vfs will see the root dentry with non-zero
 * refcnt and trigger BUG().
 *
 * That's why we hold a reference before dput() and drop it right after.
 */
static void cgroup_dput(struct cgroup *cgrp)
{
	struct super_block *sb = cgrp->root->sb;

	atomic_inc(&sb->s_active);
	dput(cgrp->dentry);
	deactivate_super(sb);
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
	struct cgroup_subsys_state *css = event->css;

	remove_wait_queue(event->wqh, &event->wait);

	event->cft->unregister_event(css, event->cft, event->eventfd);

	/* Notify userspace the event is going away. */
	eventfd_signal(event->eventfd, 1);

	eventfd_ctx_put(event->eventfd);
	kfree(event);
	css_put(css);
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
	struct cgroup *cgrp = event->css->cgroup;
	unsigned long flags = (unsigned long)key;

	if (flags & POLLHUP) {
		/*
		 * If the event has been detached at cgroup removal, we
		 * can simply return knowing the other side will cleanup
		 * for us.
		 *
		 * We can't race against event freeing since the other
		 * side will require wqh->lock via remove_wait_queue(),
		 * which we hold.
		 */
		spin_lock(&cgrp->event_list_lock);
		if (!list_empty(&event->list)) {
			list_del_init(&event->list);
			/*
			 * We are in atomic context, but cgroup_event_remove()
			 * may sleep, so we have to call it in workqueue.
			 */
			schedule_work(&event->remove);
		}
		spin_unlock(&cgrp->event_list_lock);
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
static int cgroup_write_event_control(struct cgroup_subsys_state *dummy_css,
				      struct cftype *cft, const char *buffer)
{
	struct cgroup *cgrp = dummy_css->cgroup;
	struct cgroup_event *event;
	struct cgroup_subsys_state *cfile_css;
	unsigned int efd, cfd;
	struct fd efile;
	struct fd cfile;
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

	INIT_LIST_HEAD(&event->list);
	init_poll_funcptr(&event->pt, cgroup_event_ptable_queue_proc);
	init_waitqueue_func_entry(&event->wait, cgroup_event_wake);
	INIT_WORK(&event->remove, cgroup_event_remove);

	efile = fdget(efd);
	if (!efile.file) {
		ret = -EBADF;
		goto out_kfree;
	}

	event->eventfd = eventfd_ctx_fileget(efile.file);
	if (IS_ERR(event->eventfd)) {
		ret = PTR_ERR(event->eventfd);
		goto out_put_efile;
	}

	cfile = fdget(cfd);
	if (!cfile.file) {
		ret = -EBADF;
		goto out_put_eventfd;
	}

	/* the process need read permission on control file */
	/* AV: shouldn't we check that it's been opened for read instead? */
	ret = inode_permission(file_inode(cfile.file), MAY_READ);
	if (ret < 0)
		goto out_put_cfile;

	event->cft = __file_cft(cfile.file);
	if (IS_ERR(event->cft)) {
		ret = PTR_ERR(event->cft);
		goto out_put_cfile;
	}

	if (!event->cft->ss) {
		ret = -EBADF;
		goto out_put_cfile;
	}

	/*
	 * Determine the css of @cfile, verify it belongs to the same
	 * cgroup as cgroup.event_control, and associate @event with it.
	 * Remaining events are automatically removed on cgroup destruction
	 * but the removal is asynchronous, so take an extra ref.
	 */
	rcu_read_lock();

	ret = -EINVAL;
	event->css = cgroup_css(cgrp, event->cft->ss);
	cfile_css = css_from_dir(cfile.file->f_dentry->d_parent, event->cft->ss);
	if (event->css && event->css == cfile_css && css_tryget(event->css))
		ret = 0;

	rcu_read_unlock();
	if (ret)
		goto out_put_cfile;

	if (!event->cft->register_event || !event->cft->unregister_event) {
		ret = -EINVAL;
		goto out_put_css;
	}

	ret = event->cft->register_event(event->css, event->cft,
			event->eventfd, buffer);
	if (ret)
		goto out_put_css;

	efile.file->f_op->poll(efile.file, &event->pt);

	spin_lock(&cgrp->event_list_lock);
	list_add(&event->list, &cgrp->event_list);
	spin_unlock(&cgrp->event_list_lock);

	fdput(cfile);
	fdput(efile);

	return 0;

out_put_css:
	css_put(event->css);
out_put_cfile:
	fdput(cfile);
out_put_eventfd:
	eventfd_ctx_put(event->eventfd);
out_put_efile:
	fdput(efile);
out_kfree:
	kfree(event);

	return ret;
}

static u64 cgroup_clone_children_read(struct cgroup_subsys_state *css,
				      struct cftype *cft)
{
	return test_bit(CGRP_CPUSET_CLONE_CHILDREN, &css->cgroup->flags);
}

static int cgroup_clone_children_write(struct cgroup_subsys_state *css,
				       struct cftype *cft, u64 val)
{
	if (val)
		set_bit(CGRP_CPUSET_CLONE_CHILDREN, &css->cgroup->flags);
	else
		clear_bit(CGRP_CPUSET_CLONE_CHILDREN, &css->cgroup->flags);
	return 0;
}

static struct cftype cgroup_base_files[] = {
	{
		.name = "cgroup.procs",
		.open = cgroup_procs_open,
		.write_u64 = cgroup_procs_write,
		.release = cgroup_pidlist_release,
		.mode = S_IRUGO | S_IWUSR,
	},
	{
		.name = "cgroup.event_control",
		.write_string = cgroup_write_event_control,
		.mode = S_IWUGO,
	},
	{
		.name = "cgroup.clone_children",
		.flags = CFTYPE_INSANE,
		.read_u64 = cgroup_clone_children_read,
		.write_u64 = cgroup_clone_children_write,
	},
	{
		.name = "cgroup.sane_behavior",
		.flags = CFTYPE_ONLY_ON_ROOT,
		.read_seq_string = cgroup_sane_behavior_show,
	},

	/*
	 * Historical crazy stuff.  These don't have "cgroup."  prefix and
	 * don't exist if sane_behavior.  If you're depending on these, be
	 * prepared to be burned.
	 */
	{
		.name = "tasks",
		.flags = CFTYPE_INSANE,		/* use "procs" instead */
		.open = cgroup_tasks_open,
		.write_u64 = cgroup_tasks_write,
		.release = cgroup_pidlist_release,
		.mode = S_IRUGO | S_IWUSR,
	},
	{
		.name = "notify_on_release",
		.flags = CFTYPE_INSANE,
		.read_u64 = cgroup_read_notify_on_release,
		.write_u64 = cgroup_write_notify_on_release,
	},
	{
		.name = "release_agent",
		.flags = CFTYPE_INSANE | CFTYPE_ONLY_ON_ROOT,
		.read_seq_string = cgroup_release_agent_show,
		.write_string = cgroup_release_agent_write,
		.max_write_len = PATH_MAX,
	},
	{ }	/* terminate */
};

/**
 * cgroup_populate_dir - create subsys files in a cgroup directory
 * @cgrp: target cgroup
 * @subsys_mask: mask of the subsystem ids whose files should be added
 *
 * On failure, no file is added.
 */
static int cgroup_populate_dir(struct cgroup *cgrp, unsigned long subsys_mask)
{
	struct cgroup_subsys *ss;
	int i, ret = 0;

	/* process cftsets of each subsystem */
	for_each_subsys(ss, i) {
		struct cftype_set *set;

		if (!test_bit(i, &subsys_mask))
			continue;

		list_for_each_entry(set, &ss->cftsets, node) {
			ret = cgroup_addrm_files(cgrp, set->cfts, true);
			if (ret < 0)
				goto err;
		}
	}
	return 0;
err:
	cgroup_clear_dir(cgrp, subsys_mask);
	return ret;
}

/*
 * css destruction is four-stage process.
 *
 * 1. Destruction starts.  Killing of the percpu_ref is initiated.
 *    Implemented in kill_css().
 *
 * 2. When the percpu_ref is confirmed to be visible as killed on all CPUs
 *    and thus css_tryget() is guaranteed to fail, the css can be offlined
 *    by invoking offline_css().  After offlining, the base ref is put.
 *    Implemented in css_killed_work_fn().
 *
 * 3. When the percpu_ref reaches zero, the only possible remaining
 *    accessors are inside RCU read sections.  css_release() schedules the
 *    RCU callback.
 *
 * 4. After the grace period, the css can be freed.  Implemented in
 *    css_free_work_fn().
 *
 * It is actually hairier because both step 2 and 4 require process context
 * and thus involve punting to css->destroy_work adding two additional
 * steps to the already complex sequence.
 */
static void css_free_work_fn(struct work_struct *work)
{
	struct cgroup_subsys_state *css =
		container_of(work, struct cgroup_subsys_state, destroy_work);
	struct cgroup *cgrp = css->cgroup;

	if (css->parent)
		css_put(css->parent);

	css->ss->css_free(css);
	cgroup_dput(cgrp);
}

static void css_free_rcu_fn(struct rcu_head *rcu_head)
{
	struct cgroup_subsys_state *css =
		container_of(rcu_head, struct cgroup_subsys_state, rcu_head);

	/*
	 * css holds an extra ref to @cgrp->dentry which is put on the last
	 * css_put().  dput() requires process context which we don't have.
	 */
	INIT_WORK(&css->destroy_work, css_free_work_fn);
	schedule_work(&css->destroy_work);
}

static void css_release(struct percpu_ref *ref)
{
	struct cgroup_subsys_state *css =
		container_of(ref, struct cgroup_subsys_state, refcnt);

	call_rcu(&css->rcu_head, css_free_rcu_fn);
}

static void init_css(struct cgroup_subsys_state *css, struct cgroup_subsys *ss,
		     struct cgroup *cgrp)
{
	css->cgroup = cgrp;
	css->ss = ss;
	css->flags = 0;

	if (cgrp->parent)
		css->parent = cgroup_css(cgrp->parent, ss);
	else
		css->flags |= CSS_ROOT;

	BUG_ON(cgroup_css(cgrp, ss));
}

/* invoke ->css_online() on a new CSS and mark it online if successful */
static int online_css(struct cgroup_subsys_state *css)
{
	struct cgroup_subsys *ss = css->ss;
	int ret = 0;

	lockdep_assert_held(&cgroup_mutex);

	if (ss->css_online)
		ret = ss->css_online(css);
	if (!ret) {
		css->flags |= CSS_ONLINE;
		css->cgroup->nr_css++;
		rcu_assign_pointer(css->cgroup->subsys[ss->subsys_id], css);
	}
	return ret;
}

/* if the CSS is online, invoke ->css_offline() on it and mark it offline */
static void offline_css(struct cgroup_subsys_state *css)
{
	struct cgroup_subsys *ss = css->ss;

	lockdep_assert_held(&cgroup_mutex);

	if (!(css->flags & CSS_ONLINE))
		return;

	if (ss->css_offline)
		ss->css_offline(css);

	css->flags &= ~CSS_ONLINE;
	css->cgroup->nr_css--;
	RCU_INIT_POINTER(css->cgroup->subsys[ss->subsys_id], css);
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
			     umode_t mode)
{
	struct cgroup_subsys_state *css_ar[CGROUP_SUBSYS_COUNT] = { };
	struct cgroup *cgrp;
	struct cgroup_name *name;
	struct cgroupfs_root *root = parent->root;
	int err = 0;
	struct cgroup_subsys *ss;
	struct super_block *sb = root->sb;

	/* allocate the cgroup and its ID, 0 is reserved for the root */
	cgrp = kzalloc(sizeof(*cgrp), GFP_KERNEL);
	if (!cgrp)
		return -ENOMEM;

	name = cgroup_alloc_name(dentry);
	if (!name)
		goto err_free_cgrp;
	rcu_assign_pointer(cgrp->name, name);

	/*
	 * Temporarily set the pointer to NULL, so idr_find() won't return
	 * a half-baked cgroup.
	 */
	cgrp->id = idr_alloc(&root->cgroup_idr, NULL, 1, 0, GFP_KERNEL);
	if (cgrp->id < 0)
		goto err_free_name;

	/*
	 * Only live parents can have children.  Note that the liveliness
	 * check isn't strictly necessary because cgroup_mkdir() and
	 * cgroup_rmdir() are fully synchronized by i_mutex; however, do it
	 * anyway so that locking is contained inside cgroup proper and we
	 * don't get nasty surprises if we ever grow another caller.
	 */
	if (!cgroup_lock_live_group(parent)) {
		err = -ENODEV;
		goto err_free_id;
	}

	/* Grab a reference on the superblock so the hierarchy doesn't
	 * get deleted on unmount if there are child cgroups.  This
	 * can be done outside cgroup_mutex, since the sb can't
	 * disappear while someone has an open control file on the
	 * fs */
	atomic_inc(&sb->s_active);

	init_cgroup_housekeeping(cgrp);

	dentry->d_fsdata = cgrp;
	cgrp->dentry = dentry;

	cgrp->parent = parent;
	cgrp->dummy_css.parent = &parent->dummy_css;
	cgrp->root = parent->root;

	if (notify_on_release(parent))
		set_bit(CGRP_NOTIFY_ON_RELEASE, &cgrp->flags);

	if (test_bit(CGRP_CPUSET_CLONE_CHILDREN, &parent->flags))
		set_bit(CGRP_CPUSET_CLONE_CHILDREN, &cgrp->flags);

	for_each_root_subsys(root, ss) {
		struct cgroup_subsys_state *css;

		css = ss->css_alloc(cgroup_css(parent, ss));
		if (IS_ERR(css)) {
			err = PTR_ERR(css);
			goto err_free_all;
		}
		css_ar[ss->subsys_id] = css;

		err = percpu_ref_init(&css->refcnt, css_release);
		if (err)
			goto err_free_all;

		init_css(css, ss, cgrp);
	}

	/*
	 * Create directory.  cgroup_create_file() returns with the new
	 * directory locked on success so that it can be populated without
	 * dropping cgroup_mutex.
	 */
	err = cgroup_create_file(dentry, S_IFDIR | mode, sb);
	if (err < 0)
		goto err_free_all;
	lockdep_assert_held(&dentry->d_inode->i_mutex);

	cgrp->serial_nr = cgroup_serial_nr_next++;

	/* allocation complete, commit to creation */
	list_add_tail_rcu(&cgrp->sibling, &cgrp->parent->children);
	root->number_of_cgroups++;

	/* each css holds a ref to the cgroup's dentry and the parent css */
	for_each_root_subsys(root, ss) {
		struct cgroup_subsys_state *css = css_ar[ss->subsys_id];

		dget(dentry);
		css_get(css->parent);
	}

	/* hold a ref to the parent's dentry */
	dget(parent->dentry);

	/* creation succeeded, notify subsystems */
	for_each_root_subsys(root, ss) {
		struct cgroup_subsys_state *css = css_ar[ss->subsys_id];

		err = online_css(css);
		if (err)
			goto err_destroy;

		if (ss->broken_hierarchy && !ss->warned_broken_hierarchy &&
		    parent->parent) {
			pr_warning("cgroup: %s (%d) created nested cgroup for controller \"%s\" which has incomplete hierarchy support. Nested cgroups may change behavior in the future.\n",
				   current->comm, current->pid, ss->name);
			if (!strcmp(ss->name, "memory"))
				pr_warning("cgroup: \"memory\" requires setting use_hierarchy to 1 on the root.\n");
			ss->warned_broken_hierarchy = true;
		}
	}

	idr_replace(&root->cgroup_idr, cgrp, cgrp->id);

	err = cgroup_addrm_files(cgrp, cgroup_base_files, true);
	if (err)
		goto err_destroy;

	err = cgroup_populate_dir(cgrp, root->subsys_mask);
	if (err)
		goto err_destroy;

	mutex_unlock(&cgroup_mutex);
	mutex_unlock(&cgrp->dentry->d_inode->i_mutex);

	return 0;

err_free_all:
	for_each_root_subsys(root, ss) {
		struct cgroup_subsys_state *css = css_ar[ss->subsys_id];

		if (css) {
			percpu_ref_cancel_init(&css->refcnt);
			ss->css_free(css);
		}
	}
	mutex_unlock(&cgroup_mutex);
	/* Release the reference count that we took on the superblock */
	deactivate_super(sb);
err_free_id:
	idr_remove(&root->cgroup_idr, cgrp->id);
err_free_name:
	kfree(rcu_dereference_raw(cgrp->name));
err_free_cgrp:
	kfree(cgrp);
	return err;

err_destroy:
	cgroup_destroy_locked(cgrp);
	mutex_unlock(&cgroup_mutex);
	mutex_unlock(&dentry->d_inode->i_mutex);
	return err;
}

static int cgroup_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct cgroup *c_parent = dentry->d_parent->d_fsdata;

	/* the vfs holds inode->i_mutex already */
	return cgroup_create(c_parent, dentry, mode | S_IFDIR);
}

/*
 * This is called when the refcnt of a css is confirmed to be killed.
 * css_tryget() is now guaranteed to fail.
 */
static void css_killed_work_fn(struct work_struct *work)
{
	struct cgroup_subsys_state *css =
		container_of(work, struct cgroup_subsys_state, destroy_work);
	struct cgroup *cgrp = css->cgroup;

	mutex_lock(&cgroup_mutex);

	/*
	 * css_tryget() is guaranteed to fail now.  Tell subsystems to
	 * initate destruction.
	 */
	offline_css(css);

	/*
	 * If @cgrp is marked dead, it's waiting for refs of all css's to
	 * be disabled before proceeding to the second phase of cgroup
	 * destruction.  If we are the last one, kick it off.
	 */
	if (!cgrp->nr_css && cgroup_is_dead(cgrp))
		cgroup_destroy_css_killed(cgrp);

	mutex_unlock(&cgroup_mutex);

	/*
	 * Put the css refs from kill_css().  Each css holds an extra
	 * reference to the cgroup's dentry and cgroup removal proceeds
	 * regardless of css refs.  On the last put of each css, whenever
	 * that may be, the extra dentry ref is put so that dentry
	 * destruction happens only after all css's are released.
	 */
	css_put(css);
}

/* css kill confirmation processing requires process context, bounce */
static void css_killed_ref_fn(struct percpu_ref *ref)
{
	struct cgroup_subsys_state *css =
		container_of(ref, struct cgroup_subsys_state, refcnt);

	INIT_WORK(&css->destroy_work, css_killed_work_fn);
	schedule_work(&css->destroy_work);
}

/**
 * kill_css - destroy a css
 * @css: css to destroy
 *
 * This function initiates destruction of @css by removing cgroup interface
 * files and putting its base reference.  ->css_offline() will be invoked
 * asynchronously once css_tryget() is guaranteed to fail and when the
 * reference count reaches zero, @css will be released.
 */
static void kill_css(struct cgroup_subsys_state *css)
{
	cgroup_clear_dir(css->cgroup, 1 << css->ss->subsys_id);

	/*
	 * Killing would put the base ref, but we need to keep it alive
	 * until after ->css_offline().
	 */
	css_get(css);

	/*
	 * cgroup core guarantees that, by the time ->css_offline() is
	 * invoked, no new css reference will be given out via
	 * css_tryget().  We can't simply call percpu_ref_kill() and
	 * proceed to offlining css's because percpu_ref_kill() doesn't
	 * guarantee that the ref is seen as killed on all CPUs on return.
	 *
	 * Use percpu_ref_kill_and_confirm() to get notifications as each
	 * css is confirmed to be seen as killed on all CPUs.
	 */
	percpu_ref_kill_and_confirm(&css->refcnt, css_killed_ref_fn);
}

/**
 * cgroup_destroy_locked - the first stage of cgroup destruction
 * @cgrp: cgroup to be destroyed
 *
 * css's make use of percpu refcnts whose killing latency shouldn't be
 * exposed to userland and are RCU protected.  Also, cgroup core needs to
 * guarantee that css_tryget() won't succeed by the time ->css_offline() is
 * invoked.  To satisfy all the requirements, destruction is implemented in
 * the following two steps.
 *
 * s1. Verify @cgrp can be destroyed and mark it dying.  Remove all
 *     userland visible parts and start killing the percpu refcnts of
 *     css's.  Set up so that the next stage will be kicked off once all
 *     the percpu refcnts are confirmed to be killed.
 *
 * s2. Invoke ->css_offline(), mark the cgroup dead and proceed with the
 *     rest of destruction.  Once all cgroup references are gone, the
 *     cgroup is RCU-freed.
 *
 * This function implements s1.  After this step, @cgrp is gone as far as
 * the userland is concerned and a new cgroup with the same name may be
 * created.  As cgroup doesn't care about the names internally, this
 * doesn't cause any problem.
 */
static int cgroup_destroy_locked(struct cgroup *cgrp)
	__releases(&cgroup_mutex) __acquires(&cgroup_mutex)
{
	struct dentry *d = cgrp->dentry;
	struct cgroup_event *event, *tmp;
	struct cgroup_subsys *ss;
	struct cgroup *child;
	bool empty;

	lockdep_assert_held(&d->d_inode->i_mutex);
	lockdep_assert_held(&cgroup_mutex);

	/*
	 * css_set_lock synchronizes access to ->cset_links and prevents
	 * @cgrp from being removed while __put_css_set() is in progress.
	 */
	read_lock(&css_set_lock);
	empty = list_empty(&cgrp->cset_links);
	read_unlock(&css_set_lock);
	if (!empty)
		return -EBUSY;

	/*
	 * Make sure there's no live children.  We can't test ->children
	 * emptiness as dead children linger on it while being destroyed;
	 * otherwise, "rmdir parent/child parent" may fail with -EBUSY.
	 */
	empty = true;
	rcu_read_lock();
	list_for_each_entry_rcu(child, &cgrp->children, sibling) {
		empty = cgroup_is_dead(child);
		if (!empty)
			break;
	}
	rcu_read_unlock();
	if (!empty)
		return -EBUSY;

	/*
	 * Initiate massacre of all css's.  cgroup_destroy_css_killed()
	 * will be invoked to perform the rest of destruction once the
	 * percpu refs of all css's are confirmed to be killed.
	 */
	for_each_root_subsys(cgrp->root, ss)
		kill_css(cgroup_css(cgrp, ss));

	/*
	 * Mark @cgrp dead.  This prevents further task migration and child
	 * creation by disabling cgroup_lock_live_group().  Note that
	 * CGRP_DEAD assertion is depended upon by css_next_child() to
	 * resume iteration after dropping RCU read lock.  See
	 * css_next_child() for details.
	 */
	set_bit(CGRP_DEAD, &cgrp->flags);

	/* CGRP_DEAD is set, remove from ->release_list for the last time */
	raw_spin_lock(&release_list_lock);
	if (!list_empty(&cgrp->release_list))
		list_del_init(&cgrp->release_list);
	raw_spin_unlock(&release_list_lock);

	/*
	 * If @cgrp has css's attached, the second stage of cgroup
	 * destruction is kicked off from css_killed_work_fn() after the
	 * refs of all attached css's are killed.  If @cgrp doesn't have
	 * any css, we kick it off here.
	 */
	if (!cgrp->nr_css)
		cgroup_destroy_css_killed(cgrp);

	/*
	 * Clear the base files and remove @cgrp directory.  The removal
	 * puts the base ref but we aren't quite done with @cgrp yet, so
	 * hold onto it.
	 */
	cgroup_addrm_files(cgrp, cgroup_base_files, false);
	dget(d);
	cgroup_d_remove_dir(d);

	/*
	 * Unregister events and notify userspace.
	 * Notify userspace about cgroup removing only after rmdir of cgroup
	 * directory to avoid race between userspace and kernelspace.
	 */
	spin_lock(&cgrp->event_list_lock);
	list_for_each_entry_safe(event, tmp, &cgrp->event_list, list) {
		list_del_init(&event->list);
		schedule_work(&event->remove);
	}
	spin_unlock(&cgrp->event_list_lock);

	return 0;
};

/**
 * cgroup_destroy_css_killed - the second step of cgroup destruction
 * @work: cgroup->destroy_free_work
 *
 * This function is invoked from a work item for a cgroup which is being
 * destroyed after all css's are offlined and performs the rest of
 * destruction.  This is the second step of destruction described in the
 * comment above cgroup_destroy_locked().
 */
static void cgroup_destroy_css_killed(struct cgroup *cgrp)
{
	struct cgroup *parent = cgrp->parent;
	struct dentry *d = cgrp->dentry;

	lockdep_assert_held(&cgroup_mutex);

	/* delete this cgroup from parent->children */
	list_del_rcu(&cgrp->sibling);

	/*
	 * We should remove the cgroup object from idr before its grace
	 * period starts, so we won't be looking up a cgroup while the
	 * cgroup is being freed.
	 */
	idr_remove(&cgrp->root->cgroup_idr, cgrp->id);
	cgrp->id = -1;

	dput(d);

	set_bit(CGRP_RELEASABLE, &parent->flags);
	check_for_release(parent);
}

static int cgroup_rmdir(struct inode *unused_dir, struct dentry *dentry)
{
	int ret;

	mutex_lock(&cgroup_mutex);
	ret = cgroup_destroy_locked(dentry->d_fsdata);
	mutex_unlock(&cgroup_mutex);

	return ret;
}

static void __init_or_module cgroup_init_cftsets(struct cgroup_subsys *ss)
{
	INIT_LIST_HEAD(&ss->cftsets);

	/*
	 * base_cftset is embedded in subsys itself, no need to worry about
	 * deregistration.
	 */
	if (ss->base_cftypes) {
		struct cftype *cft;

		for (cft = ss->base_cftypes; cft->name[0] != '\0'; cft++)
			cft->ss = ss;

		ss->base_cftset.cfts = ss->base_cftypes;
		list_add_tail(&ss->base_cftset.node, &ss->cftsets);
	}
}

static void __init cgroup_init_subsys(struct cgroup_subsys *ss)
{
	struct cgroup_subsys_state *css;

	printk(KERN_INFO "Initializing cgroup subsys %s\n", ss->name);

	mutex_lock(&cgroup_mutex);

	/* init base cftset */
	cgroup_init_cftsets(ss);

	/* Create the top cgroup state for this subsystem */
	list_add(&ss->sibling, &cgroup_dummy_root.subsys_list);
	ss->root = &cgroup_dummy_root;
	css = ss->css_alloc(cgroup_css(cgroup_dummy_top, ss));
	/* We don't handle early failures gracefully */
	BUG_ON(IS_ERR(css));
	init_css(css, ss, cgroup_dummy_top);

	/* Update the init_css_set to contain a subsys
	 * pointer to this state - since the subsystem is
	 * newly registered, all tasks and hence the
	 * init_css_set is in the subsystem's top cgroup. */
	init_css_set.subsys[ss->subsys_id] = css;

	need_forkexit_callback |= ss->fork || ss->exit;

	/* At system boot, before all subsystems have been
	 * registered, no tasks have been forked, so we don't
	 * need to invoke fork callbacks here. */
	BUG_ON(!list_empty(&init_task.tasks));

	BUG_ON(online_css(css));

	mutex_unlock(&cgroup_mutex);

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
	struct cgroup_subsys_state *css;
	int i, ret;
	struct hlist_node *tmp;
	struct css_set *cset;
	unsigned long key;

	/* check name and function validity */
	if (ss->name == NULL || strlen(ss->name) > MAX_CGROUP_TYPE_NAMELEN ||
	    ss->css_alloc == NULL || ss->css_free == NULL)
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
		/* a sanity check */
		BUG_ON(cgroup_subsys[ss->subsys_id] != ss);
		return 0;
	}

	/* init base cftset */
	cgroup_init_cftsets(ss);

	mutex_lock(&cgroup_mutex);
	cgroup_subsys[ss->subsys_id] = ss;

	/*
	 * no ss->css_alloc seems to need anything important in the ss
	 * struct, so this can happen first (i.e. before the dummy root
	 * attachment).
	 */
	css = ss->css_alloc(cgroup_css(cgroup_dummy_top, ss));
	if (IS_ERR(css)) {
		/* failure case - need to deassign the cgroup_subsys[] slot. */
		cgroup_subsys[ss->subsys_id] = NULL;
		mutex_unlock(&cgroup_mutex);
		return PTR_ERR(css);
	}

	list_add(&ss->sibling, &cgroup_dummy_root.subsys_list);
	ss->root = &cgroup_dummy_root;

	/* our new subsystem will be attached to the dummy hierarchy. */
	init_css(css, ss, cgroup_dummy_top);

	/*
	 * Now we need to entangle the css into the existing css_sets. unlike
	 * in cgroup_init_subsys, there are now multiple css_sets, so each one
	 * will need a new pointer to it; done by iterating the css_set_table.
	 * furthermore, modifying the existing css_sets will corrupt the hash
	 * table state, so each changed css_set will need its hash recomputed.
	 * this is all done under the css_set_lock.
	 */
	write_lock(&css_set_lock);
	hash_for_each_safe(css_set_table, i, tmp, cset, hlist) {
		/* skip entries that we already rehashed */
		if (cset->subsys[ss->subsys_id])
			continue;
		/* remove existing entry */
		hash_del(&cset->hlist);
		/* set new value */
		cset->subsys[ss->subsys_id] = css;
		/* recompute hash and restore entry */
		key = css_set_hash(cset->subsys);
		hash_add(css_set_table, &cset->hlist, key);
	}
	write_unlock(&css_set_lock);

	ret = online_css(css);
	if (ret)
		goto err_unload;

	/* success! */
	mutex_unlock(&cgroup_mutex);
	return 0;

err_unload:
	mutex_unlock(&cgroup_mutex);
	/* @ss can't be mounted here as try_module_get() would fail */
	cgroup_unload_subsys(ss);
	return ret;
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
	struct cgrp_cset_link *link;

	BUG_ON(ss->module == NULL);

	/*
	 * we shouldn't be called if the subsystem is in use, and the use of
	 * try_module_get() in rebind_subsystems() should ensure that it
	 * doesn't start being used while we're killing it off.
	 */
	BUG_ON(ss->root != &cgroup_dummy_root);

	mutex_lock(&cgroup_mutex);

	offline_css(cgroup_css(cgroup_dummy_top, ss));

	/* deassign the subsys_id */
	cgroup_subsys[ss->subsys_id] = NULL;

	/* remove subsystem from the dummy root's list of subsystems */
	list_del_init(&ss->sibling);

	/*
	 * disentangle the css from all css_sets attached to the dummy
	 * top. as in loading, we need to pay our respects to the hashtable
	 * gods.
	 */
	write_lock(&css_set_lock);
	list_for_each_entry(link, &cgroup_dummy_top->cset_links, cset_link) {
		struct css_set *cset = link->cset;
		unsigned long key;

		hash_del(&cset->hlist);
		cset->subsys[ss->subsys_id] = NULL;
		key = css_set_hash(cset->subsys);
		hash_add(css_set_table, &cset->hlist, key);
	}
	write_unlock(&css_set_lock);

	/*
	 * remove subsystem's css from the cgroup_dummy_top and free it -
	 * need to free before marking as null because ss->css_free needs
	 * the cgrp->subsys pointer to find their state.
	 */
	ss->css_free(cgroup_css(cgroup_dummy_top, ss));
	RCU_INIT_POINTER(cgroup_dummy_top->subsys[ss->subsys_id], NULL);

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
	struct cgroup_subsys *ss;
	int i;

	atomic_set(&init_css_set.refcount, 1);
	INIT_LIST_HEAD(&init_css_set.cgrp_links);
	INIT_LIST_HEAD(&init_css_set.tasks);
	INIT_HLIST_NODE(&init_css_set.hlist);
	css_set_count = 1;
	init_cgroup_root(&cgroup_dummy_root);
	cgroup_root_count = 1;
	RCU_INIT_POINTER(init_task.cgroups, &init_css_set);

	init_cgrp_cset_link.cset = &init_css_set;
	init_cgrp_cset_link.cgrp = cgroup_dummy_top;
	list_add(&init_cgrp_cset_link.cset_link, &cgroup_dummy_top->cset_links);
	list_add(&init_cgrp_cset_link.cgrp_link, &init_css_set.cgrp_links);

	/* at bootup time, we don't worry about modular subsystems */
	for_each_builtin_subsys(ss, i) {
		BUG_ON(!ss->name);
		BUG_ON(strlen(ss->name) > MAX_CGROUP_TYPE_NAMELEN);
		BUG_ON(!ss->css_alloc);
		BUG_ON(!ss->css_free);
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
	struct cgroup_subsys *ss;
	unsigned long key;
	int i, err;

	err = bdi_init(&cgroup_backing_dev_info);
	if (err)
		return err;

	for_each_builtin_subsys(ss, i) {
		if (!ss->early_init)
			cgroup_init_subsys(ss);
	}

	/* allocate id for the dummy hierarchy */
	mutex_lock(&cgroup_mutex);
	mutex_lock(&cgroup_root_mutex);

	/* Add init_css_set to the hash table */
	key = css_set_hash(init_css_set.subsys);
	hash_add(css_set_table, &init_css_set.hlist, key);

	BUG_ON(cgroup_init_root_id(&cgroup_dummy_root, 0, 1));

	err = idr_alloc(&cgroup_dummy_root.cgroup_idr, cgroup_dummy_top,
			0, 1, GFP_KERNEL);
	BUG_ON(err < 0);

	mutex_unlock(&cgroup_root_mutex);
	mutex_unlock(&cgroup_mutex);

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
int proc_cgroup_show(struct seq_file *m, void *v)
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
		for_each_root_subsys(root, ss)
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

/* Display information about each subsystem and each hierarchy */
static int proc_cgroupstats_show(struct seq_file *m, void *v)
{
	struct cgroup_subsys *ss;
	int i;

	seq_puts(m, "#subsys_name\thierarchy\tnum_cgroups\tenabled\n");
	/*
	 * ideally we don't want subsystems moving around while we do this.
	 * cgroup_mutex is also necessary to guarantee an atomic snapshot of
	 * subsys/hierarchy state.
	 */
	mutex_lock(&cgroup_mutex);

	for_each_subsys(ss, i)
		seq_printf(m, "%s\t%d\t%d\t%d\n",
			   ss->name, ss->root->hierarchy_id,
			   ss->root->number_of_cgroups, !ss->disabled);

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
	get_css_set(task_css_set(current));
	child->cgroups = current->cgroups;
	task_unlock(current);
	INIT_LIST_HEAD(&child->cg_list);
}

/**
 * cgroup_post_fork - called on a new task after adding it to the task list
 * @child: the task in question
 *
 * Adds the task to the list running through its css_set if necessary and
 * call the subsystem fork() callbacks.  Has to be after the task is
 * visible on the task list in case we race with the first call to
 * cgroup_task_iter_start() - to guarantee that the new task ends up on its
 * list.
 */
void cgroup_post_fork(struct task_struct *child)
{
	struct cgroup_subsys *ss;
	int i;

	/*
	 * use_task_css_set_links is set to 1 before we walk the tasklist
	 * under the tasklist_lock and we read it here after we added the child
	 * to the tasklist under the tasklist_lock as well. If the child wasn't
	 * yet in the tasklist when we walked through it from
	 * cgroup_enable_task_cg_lists(), then use_task_css_set_links value
	 * should be visible now due to the paired locking and barriers implied
	 * by LOCK/UNLOCK: it is written before the tasklist_lock unlock
	 * in cgroup_enable_task_cg_lists() and read here after the tasklist_lock
	 * lock on fork.
	 */
	if (use_task_css_set_links) {
		write_lock(&css_set_lock);
		task_lock(child);
		if (list_empty(&child->cg_list))
			list_add(&child->cg_list, &task_css_set(child)->tasks);
		task_unlock(child);
		write_unlock(&css_set_lock);
	}

	/*
	 * Call ss->fork().  This must happen after @child is linked on
	 * css_set; otherwise, @child might change state between ->fork()
	 * and addition to css_set.
	 */
	if (need_forkexit_callback) {
		/*
		 * fork/exit callbacks are supported only for builtin
		 * subsystems, and the builtin section of the subsys
		 * array is immutable, so we don't need to lock the
		 * subsys array here. On the other hand, modular section
		 * of the array can be freed at module unload, so we
		 * can't touch that.
		 */
		for_each_builtin_subsys(ss, i)
			if (ss->fork)
				ss->fork(child);
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
	struct cgroup_subsys *ss;
	struct css_set *cset;
	int i;

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
	cset = task_css_set(tsk);
	RCU_INIT_POINTER(tsk->cgroups, &init_css_set);

	if (run_callbacks && need_forkexit_callback) {
		/*
		 * fork/exit callbacks are supported only for builtin
		 * subsystems, see cgroup_post_fork() for details.
		 */
		for_each_builtin_subsys(ss, i) {
			if (ss->exit) {
				struct cgroup_subsys_state *old_css = cset->subsys[i];
				struct cgroup_subsys_state *css = task_css(tsk, i);

				ss->exit(css, old_css, tsk);
			}
		}
	}
	task_unlock(tsk);

	put_css_set_taskexit(cset);
}

static void check_for_release(struct cgroup *cgrp)
{
	if (cgroup_is_releasable(cgrp) &&
	    list_empty(&cgrp->cset_links) && list_empty(&cgrp->children)) {
		/*
		 * Control Group is currently removeable. If it's not
		 * already queued for a userspace notification, queue
		 * it now
		 */
		int need_schedule_work = 0;

		raw_spin_lock(&release_list_lock);
		if (!cgroup_is_dead(cgrp) &&
		    list_empty(&cgrp->release_list)) {
			list_add(&cgrp->release_list, &release_list);
			need_schedule_work = 1;
		}
		raw_spin_unlock(&release_list_lock);
		if (need_schedule_work)
			schedule_work(&release_agent_work);
	}
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
	raw_spin_lock(&release_list_lock);
	while (!list_empty(&release_list)) {
		char *argv[3], *envp[3];
		int i;
		char *pathbuf = NULL, *agentbuf = NULL;
		struct cgroup *cgrp = list_entry(release_list.next,
						    struct cgroup,
						    release_list);
		list_del_init(&cgrp->release_list);
		raw_spin_unlock(&release_list_lock);
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
		raw_spin_lock(&release_list_lock);
	}
	raw_spin_unlock(&release_list_lock);
	mutex_unlock(&cgroup_mutex);
}

static int __init cgroup_disable(char *str)
{
	struct cgroup_subsys *ss;
	char *token;
	int i;

	while ((token = strsep(&str, ",")) != NULL) {
		if (!*token)
			continue;

		/*
		 * cgroup_disable, being at boot time, can't know about
		 * module subsystems, so we don't worry about them.
		 */
		for_each_builtin_subsys(ss, i) {
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

/**
 * css_from_dir - get corresponding css from the dentry of a cgroup dir
 * @dentry: directory dentry of interest
 * @ss: subsystem of interest
 *
 * Must be called under RCU read lock.  The caller is responsible for
 * pinning the returned css if it needs to be accessed outside the RCU
 * critical section.
 */
struct cgroup_subsys_state *css_from_dir(struct dentry *dentry,
					 struct cgroup_subsys *ss)
{
	struct cgroup *cgrp;

	WARN_ON_ONCE(!rcu_read_lock_held());

	/* is @dentry a cgroup dir? */
	if (!dentry->d_inode ||
	    dentry->d_inode->i_op != &cgroup_dir_inode_operations)
		return ERR_PTR(-EBADF);

	cgrp = __d_cgrp(dentry);
	return cgroup_css(cgrp, ss) ?: ERR_PTR(-ENOENT);
}

/**
 * css_from_id - lookup css by id
 * @id: the cgroup id
 * @ss: cgroup subsys to be looked into
 *
 * Returns the css if there's valid one with @id, otherwise returns NULL.
 * Should be called under rcu_read_lock().
 */
struct cgroup_subsys_state *css_from_id(int id, struct cgroup_subsys *ss)
{
	struct cgroup *cgrp;

	rcu_lockdep_assert(rcu_read_lock_held() ||
			   lockdep_is_held(&cgroup_mutex),
			   "css_from_id() needs proper protection");

	cgrp = idr_find(&ss->root->cgroup_idr, id);
	if (cgrp)
		return cgroup_css(cgrp, ss);
	return NULL;
}

#ifdef CONFIG_CGROUP_DEBUG
static struct cgroup_subsys_state *
debug_css_alloc(struct cgroup_subsys_state *parent_css)
{
	struct cgroup_subsys_state *css = kzalloc(sizeof(*css), GFP_KERNEL);

	if (!css)
		return ERR_PTR(-ENOMEM);

	return css;
}

static void debug_css_free(struct cgroup_subsys_state *css)
{
	kfree(css);
}

static u64 debug_taskcount_read(struct cgroup_subsys_state *css,
				struct cftype *cft)
{
	return cgroup_task_count(css->cgroup);
}

static u64 current_css_set_read(struct cgroup_subsys_state *css,
				struct cftype *cft)
{
	return (u64)(unsigned long)current->cgroups;
}

static u64 current_css_set_refcount_read(struct cgroup_subsys_state *css,
					 struct cftype *cft)
{
	u64 count;

	rcu_read_lock();
	count = atomic_read(&task_css_set(current)->refcount);
	rcu_read_unlock();
	return count;
}

static int current_css_set_cg_links_read(struct cgroup_subsys_state *css,
					 struct cftype *cft,
					 struct seq_file *seq)
{
	struct cgrp_cset_link *link;
	struct css_set *cset;

	read_lock(&css_set_lock);
	rcu_read_lock();
	cset = rcu_dereference(current->cgroups);
	list_for_each_entry(link, &cset->cgrp_links, cgrp_link) {
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
static int cgroup_css_links_read(struct cgroup_subsys_state *css,
				 struct cftype *cft, struct seq_file *seq)
{
	struct cgrp_cset_link *link;

	read_lock(&css_set_lock);
	list_for_each_entry(link, &css->cgroup->cset_links, cset_link) {
		struct css_set *cset = link->cset;
		struct task_struct *task;
		int count = 0;
		seq_printf(seq, "css_set %p\n", cset);
		list_for_each_entry(task, &cset->tasks, cg_list) {
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

static u64 releasable_read(struct cgroup_subsys_state *css, struct cftype *cft)
{
	return test_bit(CGRP_RELEASABLE, &css->cgroup->flags);
}

static struct cftype debug_files[] =  {
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

	{ }	/* terminate */
};

struct cgroup_subsys debug_subsys = {
	.name = "debug",
	.css_alloc = debug_css_alloc,
	.css_free = debug_css_free,
	.subsys_id = debug_subsys_id,
	.base_cftypes = debug_files,
};
#endif /* CONFIG_CGROUP_DEBUG */
