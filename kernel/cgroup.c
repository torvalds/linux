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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cgroup.h>
#include <linux/cred.h>
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/init_task.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/magic.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/proc_fs.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/rwsem.h>
#include <linux/string.h>
#include <linux/sort.h>
#include <linux/kmod.h>
#include <linux/delayacct.h>
#include <linux/cgroupstats.h>
#include <linux/hashtable.h>
#include <linux/pid_namespace.h>
#include <linux/idr.h>
#include <linux/vmalloc.h> /* TODO: replace with more sophisticated array */
#include <linux/kthread.h>
#include <linux/delay.h>

#include <linux/atomic.h>

/*
 * pidlists linger the following amount before being destroyed.  The goal
 * is avoiding frequent destruction in the middle of consecutive read calls
 * Expiring in the middle is a performance problem not a correctness one.
 * 1 sec should be enough.
 */
#define CGROUP_PIDLIST_DESTROY_DELAY	HZ

#define CGROUP_FILE_NAME_MAX		(MAX_CGROUP_TYPE_NAMELEN +	\
					 MAX_CFTYPE_NAME + 2)

/*
 * cgroup_mutex is the master lock.  Any modification to cgroup or its
 * hierarchy must be performed while holding it.
 *
 * css_set_rwsem protects task->cgroups pointer, the list of css_set
 * objects, and the chain of tasks off each css_set.
 *
 * These locks are exported if CONFIG_PROVE_RCU so that accessors in
 * cgroup.h can use them for lockdep annotations.
 */
#ifdef CONFIG_PROVE_RCU
DEFINE_MUTEX(cgroup_mutex);
DECLARE_RWSEM(css_set_rwsem);
EXPORT_SYMBOL_GPL(cgroup_mutex);
EXPORT_SYMBOL_GPL(css_set_rwsem);
#else
static DEFINE_MUTEX(cgroup_mutex);
static DECLARE_RWSEM(css_set_rwsem);
#endif

/*
 * Protects cgroup_idr and css_idr so that IDs can be released without
 * grabbing cgroup_mutex.
 */
static DEFINE_SPINLOCK(cgroup_idr_lock);

/*
 * Protects cgroup_subsys->release_agent_path.  Modifying it also requires
 * cgroup_mutex.  Reading requires either cgroup_mutex or this spinlock.
 */
static DEFINE_SPINLOCK(release_agent_path_lock);

#define cgroup_assert_mutex_or_rcu_locked()				\
	rcu_lockdep_assert(rcu_read_lock_held() ||			\
			   lockdep_is_held(&cgroup_mutex),		\
			   "cgroup_mutex or RCU read lock required");

/*
 * cgroup destruction makes heavy use of work items and there can be a lot
 * of concurrent destructions.  Use a separate workqueue so that cgroup
 * destruction work items don't end up filling up max_active of system_wq
 * which may lead to deadlock.
 */
static struct workqueue_struct *cgroup_destroy_wq;

/*
 * pidlist destructions need to be flushed on cgroup destruction.  Use a
 * separate workqueue as flush domain.
 */
static struct workqueue_struct *cgroup_pidlist_destroy_wq;

/* generate an array of cgroup subsystem pointers */
#define SUBSYS(_x) [_x ## _cgrp_id] = &_x ## _cgrp_subsys,
static struct cgroup_subsys *cgroup_subsys[] = {
#include <linux/cgroup_subsys.h>
};
#undef SUBSYS

/* array of cgroup subsystem names */
#define SUBSYS(_x) [_x ## _cgrp_id] = #_x,
static const char *cgroup_subsys_name[] = {
#include <linux/cgroup_subsys.h>
};
#undef SUBSYS

/*
 * The default hierarchy, reserved for the subsystems that are otherwise
 * unattached - it never has more than a single cgroup, and all tasks are
 * part of that cgroup.
 */
struct cgroup_root cgrp_dfl_root;

/*
 * The default hierarchy always exists but is hidden until mounted for the
 * first time.  This is for backward compatibility.
 */
static bool cgrp_dfl_root_visible;

/* some controllers are not supported in the default hierarchy */
static const unsigned int cgrp_dfl_root_inhibit_ss_mask = 0
#ifdef CONFIG_CGROUP_DEBUG
	| (1 << debug_cgrp_id)
#endif
	;

/* The list of hierarchy roots */

static LIST_HEAD(cgroup_roots);
static int cgroup_root_count;

/* hierarchy ID allocation and mapping, protected by cgroup_mutex */
static DEFINE_IDR(cgroup_hierarchy_idr);

/*
 * Assign a monotonically increasing serial number to csses.  It guarantees
 * cgroups with bigger numbers are newer than those with smaller numbers.
 * Also, as csses are always appended to the parent's ->children list, it
 * guarantees that sibling csses are always sorted in the ascending serial
 * number order on the list.  Protected by cgroup_mutex.
 */
static u64 css_serial_nr_next = 1;

/* This flag indicates whether tasks in the fork and exit paths should
 * check for fork/exit handlers to call. This avoids us having to do
 * extra work in the fork/exit path if none of the subsystems need to
 * be called.
 */
static int need_forkexit_callback __read_mostly;

static struct cftype cgroup_base_files[];

static void cgroup_put(struct cgroup *cgrp);
static int rebind_subsystems(struct cgroup_root *dst_root,
			     unsigned int ss_mask);
static int cgroup_destroy_locked(struct cgroup *cgrp);
static int create_css(struct cgroup *cgrp, struct cgroup_subsys *ss,
		      bool visible);
static void css_release(struct percpu_ref *ref);
static void kill_css(struct cgroup_subsys_state *css);
static int cgroup_addrm_files(struct cgroup *cgrp, struct cftype cfts[],
			      bool is_add);
static void cgroup_pidlist_destroy_all(struct cgroup *cgrp);

/* IDR wrappers which synchronize using cgroup_idr_lock */
static int cgroup_idr_alloc(struct idr *idr, void *ptr, int start, int end,
			    gfp_t gfp_mask)
{
	int ret;

	idr_preload(gfp_mask);
	spin_lock_bh(&cgroup_idr_lock);
	ret = idr_alloc(idr, ptr, start, end, gfp_mask);
	spin_unlock_bh(&cgroup_idr_lock);
	idr_preload_end();
	return ret;
}

static void *cgroup_idr_replace(struct idr *idr, void *ptr, int id)
{
	void *ret;

	spin_lock_bh(&cgroup_idr_lock);
	ret = idr_replace(idr, ptr, id);
	spin_unlock_bh(&cgroup_idr_lock);
	return ret;
}

static void cgroup_idr_remove(struct idr *idr, int id)
{
	spin_lock_bh(&cgroup_idr_lock);
	idr_remove(idr, id);
	spin_unlock_bh(&cgroup_idr_lock);
}

static struct cgroup *cgroup_parent(struct cgroup *cgrp)
{
	struct cgroup_subsys_state *parent_css = cgrp->self.parent;

	if (parent_css)
		return container_of(parent_css, struct cgroup, self);
	return NULL;
}

/**
 * cgroup_css - obtain a cgroup's css for the specified subsystem
 * @cgrp: the cgroup of interest
 * @ss: the subsystem of interest (%NULL returns @cgrp->self)
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
		return rcu_dereference_check(cgrp->subsys[ss->id],
					lockdep_is_held(&cgroup_mutex));
	else
		return &cgrp->self;
}

/**
 * cgroup_e_css - obtain a cgroup's effective css for the specified subsystem
 * @cgrp: the cgroup of interest
 * @ss: the subsystem of interest (%NULL returns @cgrp->self)
 *
 * Similar to cgroup_css() but returns the effctive css, which is defined
 * as the matching css of the nearest ancestor including self which has @ss
 * enabled.  If @ss is associated with the hierarchy @cgrp is on, this
 * function is guaranteed to return non-NULL css.
 */
static struct cgroup_subsys_state *cgroup_e_css(struct cgroup *cgrp,
						struct cgroup_subsys *ss)
{
	lockdep_assert_held(&cgroup_mutex);

	if (!ss)
		return &cgrp->self;

	if (!(cgrp->root->subsys_mask & (1 << ss->id)))
		return NULL;

	while (cgroup_parent(cgrp) &&
	       !(cgroup_parent(cgrp)->child_subsys_mask & (1 << ss->id)))
		cgrp = cgroup_parent(cgrp);

	return cgroup_css(cgrp, ss);
}

/* convenient tests for these bits */
static inline bool cgroup_is_dead(const struct cgroup *cgrp)
{
	return !(cgrp->self.flags & CSS_ONLINE);
}

struct cgroup_subsys_state *of_css(struct kernfs_open_file *of)
{
	struct cgroup *cgrp = of->kn->parent->priv;
	struct cftype *cft = of_cft(of);

	/*
	 * This is open and unprotected implementation of cgroup_css().
	 * seq_css() is only called from a kernfs file operation which has
	 * an active reference on the file.  Because all the subsystem
	 * files are drained before a css is disassociated with a cgroup,
	 * the matching css from the cgroup's subsys table is guaranteed to
	 * be and stay valid until the enclosing operation is complete.
	 */
	if (cft->ss)
		return rcu_dereference_raw(cgrp->subsys[cft->ss->id]);
	else
		return &cgrp->self;
}
EXPORT_SYMBOL_GPL(of_css);

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
		cgrp = cgroup_parent(cgrp);
	}
	return false;
}

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
 * for_each_css - iterate all css's of a cgroup
 * @css: the iteration cursor
 * @ssid: the index of the subsystem, CGROUP_SUBSYS_COUNT after reaching the end
 * @cgrp: the target cgroup to iterate css's of
 *
 * Should be called under cgroup_[tree_]mutex.
 */
#define for_each_css(css, ssid, cgrp)					\
	for ((ssid) = 0; (ssid) < CGROUP_SUBSYS_COUNT; (ssid)++)	\
		if (!((css) = rcu_dereference_check(			\
				(cgrp)->subsys[(ssid)],			\
				lockdep_is_held(&cgroup_mutex)))) { }	\
		else

/**
 * for_each_e_css - iterate all effective css's of a cgroup
 * @css: the iteration cursor
 * @ssid: the index of the subsystem, CGROUP_SUBSYS_COUNT after reaching the end
 * @cgrp: the target cgroup to iterate css's of
 *
 * Should be called under cgroup_[tree_]mutex.
 */
#define for_each_e_css(css, ssid, cgrp)					\
	for ((ssid) = 0; (ssid) < CGROUP_SUBSYS_COUNT; (ssid)++)	\
		if (!((css) = cgroup_e_css(cgrp, cgroup_subsys[(ssid)]))) \
			;						\
		else

/**
 * for_each_subsys - iterate all enabled cgroup subsystems
 * @ss: the iteration cursor
 * @ssid: the index of @ss, CGROUP_SUBSYS_COUNT after reaching the end
 */
#define for_each_subsys(ss, ssid)					\
	for ((ssid) = 0; (ssid) < CGROUP_SUBSYS_COUNT &&		\
	     (((ss) = cgroup_subsys[ssid]) || true); (ssid)++)

/* iterate across the hierarchies */
#define for_each_root(root)						\
	list_for_each_entry((root), &cgroup_roots, root_list)

/* iterate over child cgrps, lock should be held throughout iteration */
#define cgroup_for_each_live_child(child, cgrp)				\
	list_for_each_entry((child), &(cgrp)->self.children, self.sibling) \
		if (({ lockdep_assert_held(&cgroup_mutex);		\
		       cgroup_is_dead(child); }))			\
			;						\
		else

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

/*
 * The default css_set - used by init and its children prior to any
 * hierarchies being mounted. It contains a pointer to the root state
 * for each subsystem. Also used to anchor the list of css_sets. Not
 * reference-counted, to improve performance when child cgroups
 * haven't been created.
 */
struct css_set init_css_set = {
	.refcount		= ATOMIC_INIT(1),
	.cgrp_links		= LIST_HEAD_INIT(init_css_set.cgrp_links),
	.tasks			= LIST_HEAD_INIT(init_css_set.tasks),
	.mg_tasks		= LIST_HEAD_INIT(init_css_set.mg_tasks),
	.mg_preload_node	= LIST_HEAD_INIT(init_css_set.mg_preload_node),
	.mg_node		= LIST_HEAD_INIT(init_css_set.mg_node),
};

static int css_set_count	= 1;	/* 1 for init_css_set */

/**
 * cgroup_update_populated - updated populated count of a cgroup
 * @cgrp: the target cgroup
 * @populated: inc or dec populated count
 *
 * @cgrp is either getting the first task (css_set) or losing the last.
 * Update @cgrp->populated_cnt accordingly.  The count is propagated
 * towards root so that a given cgroup's populated_cnt is zero iff the
 * cgroup and all its descendants are empty.
 *
 * @cgrp's interface file "cgroup.populated" is zero if
 * @cgrp->populated_cnt is zero and 1 otherwise.  When @cgrp->populated_cnt
 * changes from or to zero, userland is notified that the content of the
 * interface file has changed.  This can be used to detect when @cgrp and
 * its descendants become populated or empty.
 */
static void cgroup_update_populated(struct cgroup *cgrp, bool populated)
{
	lockdep_assert_held(&css_set_rwsem);

	do {
		bool trigger;

		if (populated)
			trigger = !cgrp->populated_cnt++;
		else
			trigger = !--cgrp->populated_cnt;

		if (!trigger)
			break;

		if (cgrp->populated_kn)
			kernfs_notify(cgrp->populated_kn);
		cgrp = cgroup_parent(cgrp);
	} while (cgrp);
}

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

static void put_css_set_locked(struct css_set *cset, bool taskexit)
{
	struct cgrp_cset_link *link, *tmp_link;
	struct cgroup_subsys *ss;
	int ssid;

	lockdep_assert_held(&css_set_rwsem);

	if (!atomic_dec_and_test(&cset->refcount))
		return;

	/* This css_set is dead. unlink it and release cgroup refcounts */
	for_each_subsys(ss, ssid)
		list_del(&cset->e_cset_node[ssid]);
	hash_del(&cset->hlist);
	css_set_count--;

	list_for_each_entry_safe(link, tmp_link, &cset->cgrp_links, cgrp_link) {
		struct cgroup *cgrp = link->cgrp;

		list_del(&link->cset_link);
		list_del(&link->cgrp_link);

		/* @cgrp can't go away while we're holding css_set_rwsem */
		if (list_empty(&cgrp->cset_links)) {
			cgroup_update_populated(cgrp, false);
			if (notify_on_release(cgrp)) {
				if (taskexit)
					set_bit(CGRP_RELEASABLE, &cgrp->flags);
				check_for_release(cgrp);
			}
		}

		kfree(link);
	}

	kfree_rcu(cset, rcu_head);
}

static void put_css_set(struct css_set *cset, bool taskexit)
{
	/*
	 * Ensure that the refcount doesn't hit zero while any readers
	 * can see it. Similar to atomic_dec_and_lock(), but for an
	 * rwlock
	 */
	if (atomic_add_unless(&cset->refcount, -1, 1))
		return;

	down_write(&css_set_rwsem);
	put_css_set_locked(cset, taskexit);
	up_write(&css_set_rwsem);
}

/*
 * refcounted get/put for css_set objects
 */
static inline void get_css_set(struct css_set *cset)
{
	atomic_inc(&cset->refcount);
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

	/*
	 * On the default hierarchy, there can be csets which are
	 * associated with the same set of cgroups but different csses.
	 * Let's first ensure that csses match.
	 */
	if (memcmp(template, cset->subsys, sizeof(cset->subsys)))
		return false;

	/*
	 * Compare cgroup pointers in order to distinguish between
	 * different cgroups in hierarchies.  As different cgroups may
	 * share the same effective css, this comparison is always
	 * necessary.
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
	struct cgroup_root *root = cgrp->root;
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
			/*
			 * @ss is in this hierarchy, so we want the
			 * effective css from @cgrp.
			 */
			template[i] = cgroup_e_css(cgrp, ss);
		} else {
			/*
			 * @ss is not in this hierarchy, so we don't want
			 * to change the css.
			 */
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

	if (cgroup_on_dfl(cgrp))
		cset->dfl_cgrp = cgrp;

	link = list_first_entry(tmp_links, struct cgrp_cset_link, cset_link);
	link->cset = cset;
	link->cgrp = cgrp;

	if (list_empty(&cgrp->cset_links))
		cgroup_update_populated(cgrp, true);
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
	struct cgroup_subsys *ss;
	unsigned long key;
	int ssid;

	lockdep_assert_held(&cgroup_mutex);

	/* First see if we already have a cgroup group that matches
	 * the desired set */
	down_read(&css_set_rwsem);
	cset = find_existing_css_set(old_cset, cgrp, template);
	if (cset)
		get_css_set(cset);
	up_read(&css_set_rwsem);

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
	INIT_LIST_HEAD(&cset->mg_tasks);
	INIT_LIST_HEAD(&cset->mg_preload_node);
	INIT_LIST_HEAD(&cset->mg_node);
	INIT_HLIST_NODE(&cset->hlist);

	/* Copy the set of subsystem state objects generated in
	 * find_existing_css_set() */
	memcpy(cset->subsys, template, sizeof(cset->subsys));

	down_write(&css_set_rwsem);
	/* Add reference counts and links from the new css_set. */
	list_for_each_entry(link, &old_cset->cgrp_links, cgrp_link) {
		struct cgroup *c = link->cgrp;

		if (c->root == cgrp->root)
			c = cgrp;
		link_css_set(&tmp_links, cset, c);
	}

	BUG_ON(!list_empty(&tmp_links));

	css_set_count++;

	/* Add @cset to the hash table */
	key = css_set_hash(cset->subsys);
	hash_add(css_set_table, &cset->hlist, key);

	for_each_subsys(ss, ssid)
		list_add_tail(&cset->e_cset_node[ssid],
			      &cset->subsys[ssid]->cgroup->e_csets[ssid]);

	up_write(&css_set_rwsem);

	return cset;
}

static struct cgroup_root *cgroup_root_from_kf(struct kernfs_root *kf_root)
{
	struct cgroup *root_cgrp = kf_root->kn->priv;

	return root_cgrp->root;
}

static int cgroup_init_root_id(struct cgroup_root *root)
{
	int id;

	lockdep_assert_held(&cgroup_mutex);

	id = idr_alloc_cyclic(&cgroup_hierarchy_idr, root, 0, 0, GFP_KERNEL);
	if (id < 0)
		return id;

	root->hierarchy_id = id;
	return 0;
}

static void cgroup_exit_root_id(struct cgroup_root *root)
{
	lockdep_assert_held(&cgroup_mutex);

	if (root->hierarchy_id) {
		idr_remove(&cgroup_hierarchy_idr, root->hierarchy_id);
		root->hierarchy_id = 0;
	}
}

static void cgroup_free_root(struct cgroup_root *root)
{
	if (root) {
		/* hierarhcy ID shoulid already have been released */
		WARN_ON_ONCE(root->hierarchy_id);

		idr_destroy(&root->cgroup_idr);
		kfree(root);
	}
}

static void cgroup_destroy_root(struct cgroup_root *root)
{
	struct cgroup *cgrp = &root->cgrp;
	struct cgrp_cset_link *link, *tmp_link;

	mutex_lock(&cgroup_mutex);

	BUG_ON(atomic_read(&root->nr_cgrps));
	BUG_ON(!list_empty(&cgrp->self.children));

	/* Rebind all subsystems back to the default hierarchy */
	rebind_subsystems(&cgrp_dfl_root, root->subsys_mask);

	/*
	 * Release all the links from cset_links to this hierarchy's
	 * root cgroup
	 */
	down_write(&css_set_rwsem);

	list_for_each_entry_safe(link, tmp_link, &cgrp->cset_links, cset_link) {
		list_del(&link->cset_link);
		list_del(&link->cgrp_link);
		kfree(link);
	}
	up_write(&css_set_rwsem);

	if (!list_empty(&root->root_list)) {
		list_del(&root->root_list);
		cgroup_root_count--;
	}

	cgroup_exit_root_id(root);

	mutex_unlock(&cgroup_mutex);

	kernfs_destroy_root(root->kf_root);
	cgroup_free_root(root);
}

/* look up cgroup associated with given css_set on the specified hierarchy */
static struct cgroup *cset_cgroup_from_root(struct css_set *cset,
					    struct cgroup_root *root)
{
	struct cgroup *res = NULL;

	lockdep_assert_held(&cgroup_mutex);
	lockdep_assert_held(&css_set_rwsem);

	if (cset == &init_css_set) {
		res = &root->cgrp;
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

	BUG_ON(!res);
	return res;
}

/*
 * Return the cgroup for "task" from the given hierarchy. Must be
 * called with cgroup_mutex and css_set_rwsem held.
 */
static struct cgroup *task_cgroup_from_root(struct task_struct *task,
					    struct cgroup_root *root)
{
	/*
	 * No need to lock the task - since we hold cgroup_mutex the
	 * task can't change groups, so the only thing that can happen
	 * is that it exits and its css is set back to init_css_set.
	 */
	return cset_cgroup_from_root(task_css_set(task), root);
}

/*
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
 * least one task in the system (init, pid == 1), therefore, root cgroup
 * always has either children cgroups and/or using tasks.  So we don't
 * need a special hack to ensure that root cgroup cannot be deleted.
 *
 * P.S.  One more locking exception.  RCU is used to guard the
 * update of a tasks cgroup pointer by cgroup_attach_task()
 */

static int cgroup_populate_dir(struct cgroup *cgrp, unsigned int subsys_mask);
static struct kernfs_syscall_ops cgroup_kf_syscall_ops;
static const struct file_operations proc_cgroupstats_operations;

static char *cgroup_file_name(struct cgroup *cgrp, const struct cftype *cft,
			      char *buf)
{
	if (cft->ss && !(cft->flags & CFTYPE_NO_PREFIX) &&
	    !(cgrp->root->flags & CGRP_ROOT_NOPREFIX))
		snprintf(buf, CGROUP_FILE_NAME_MAX, "%s.%s",
			 cft->ss->name, cft->name);
	else
		strncpy(buf, cft->name, CGROUP_FILE_NAME_MAX);
	return buf;
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

	if (cft->read_u64 || cft->read_s64 || cft->seq_show)
		mode |= S_IRUGO;

	if (cft->write_u64 || cft->write_s64 || cft->write)
		mode |= S_IWUSR;

	return mode;
}

static void cgroup_get(struct cgroup *cgrp)
{
	WARN_ON_ONCE(cgroup_is_dead(cgrp));
	css_get(&cgrp->self);
}

static void cgroup_put(struct cgroup *cgrp)
{
	css_put(&cgrp->self);
}

/**
 * cgroup_refresh_child_subsys_mask - update child_subsys_mask
 * @cgrp: the target cgroup
 *
 * On the default hierarchy, a subsystem may request other subsystems to be
 * enabled together through its ->depends_on mask.  In such cases, more
 * subsystems than specified in "cgroup.subtree_control" may be enabled.
 *
 * This function determines which subsystems need to be enabled given the
 * current @cgrp->subtree_control and records it in
 * @cgrp->child_subsys_mask.  The resulting mask is always a superset of
 * @cgrp->subtree_control and follows the usual hierarchy rules.
 */
static void cgroup_refresh_child_subsys_mask(struct cgroup *cgrp)
{
	struct cgroup *parent = cgroup_parent(cgrp);
	unsigned int cur_ss_mask = cgrp->subtree_control;
	struct cgroup_subsys *ss;
	int ssid;

	lockdep_assert_held(&cgroup_mutex);

	if (!cgroup_on_dfl(cgrp)) {
		cgrp->child_subsys_mask = cur_ss_mask;
		return;
	}

	while (true) {
		unsigned int new_ss_mask = cur_ss_mask;

		for_each_subsys(ss, ssid)
			if (cur_ss_mask & (1 << ssid))
				new_ss_mask |= ss->depends_on;

		/*
		 * Mask out subsystems which aren't available.  This can
		 * happen only if some depended-upon subsystems were bound
		 * to non-default hierarchies.
		 */
		if (parent)
			new_ss_mask &= parent->child_subsys_mask;
		else
			new_ss_mask &= cgrp->root->subsys_mask;

		if (new_ss_mask == cur_ss_mask)
			break;
		cur_ss_mask = new_ss_mask;
	}

	cgrp->child_subsys_mask = cur_ss_mask;
}

/**
 * cgroup_kn_unlock - unlocking helper for cgroup kernfs methods
 * @kn: the kernfs_node being serviced
 *
 * This helper undoes cgroup_kn_lock_live() and should be invoked before
 * the method finishes if locking succeeded.  Note that once this function
 * returns the cgroup returned by cgroup_kn_lock_live() may become
 * inaccessible any time.  If the caller intends to continue to access the
 * cgroup, it should pin it before invoking this function.
 */
static void cgroup_kn_unlock(struct kernfs_node *kn)
{
	struct cgroup *cgrp;

	if (kernfs_type(kn) == KERNFS_DIR)
		cgrp = kn->priv;
	else
		cgrp = kn->parent->priv;

	mutex_unlock(&cgroup_mutex);

	kernfs_unbreak_active_protection(kn);
	cgroup_put(cgrp);
}

/**
 * cgroup_kn_lock_live - locking helper for cgroup kernfs methods
 * @kn: the kernfs_node being serviced
 *
 * This helper is to be used by a cgroup kernfs method currently servicing
 * @kn.  It breaks the active protection, performs cgroup locking and
 * verifies that the associated cgroup is alive.  Returns the cgroup if
 * alive; otherwise, %NULL.  A successful return should be undone by a
 * matching cgroup_kn_unlock() invocation.
 *
 * Any cgroup kernfs method implementation which requires locking the
 * associated cgroup should use this helper.  It avoids nesting cgroup
 * locking under kernfs active protection and allows all kernfs operations
 * including self-removal.
 */
static struct cgroup *cgroup_kn_lock_live(struct kernfs_node *kn)
{
	struct cgroup *cgrp;

	if (kernfs_type(kn) == KERNFS_DIR)
		cgrp = kn->priv;
	else
		cgrp = kn->parent->priv;

	/*
	 * We're gonna grab cgroup_mutex which nests outside kernfs
	 * active_ref.  cgroup liveliness check alone provides enough
	 * protection against removal.  Ensure @cgrp stays accessible and
	 * break the active_ref protection.
	 */
	cgroup_get(cgrp);
	kernfs_break_active_protection(kn);

	mutex_lock(&cgroup_mutex);

	if (!cgroup_is_dead(cgrp))
		return cgrp;

	cgroup_kn_unlock(kn);
	return NULL;
}

static void cgroup_rm_file(struct cgroup *cgrp, const struct cftype *cft)
{
	char name[CGROUP_FILE_NAME_MAX];

	lockdep_assert_held(&cgroup_mutex);
	kernfs_remove_by_name(cgrp->kn, cgroup_file_name(cgrp, cft, name));
}

/**
 * cgroup_clear_dir - remove subsys files in a cgroup directory
 * @cgrp: target cgroup
 * @subsys_mask: mask of the subsystem ids whose files should be removed
 */
static void cgroup_clear_dir(struct cgroup *cgrp, unsigned int subsys_mask)
{
	struct cgroup_subsys *ss;
	int i;

	for_each_subsys(ss, i) {
		struct cftype *cfts;

		if (!(subsys_mask & (1 << i)))
			continue;
		list_for_each_entry(cfts, &ss->cfts, node)
			cgroup_addrm_files(cgrp, cfts, false);
	}
}

static int rebind_subsystems(struct cgroup_root *dst_root, unsigned int ss_mask)
{
	struct cgroup_subsys *ss;
	unsigned int tmp_ss_mask;
	int ssid, i, ret;

	lockdep_assert_held(&cgroup_mutex);

	for_each_subsys(ss, ssid) {
		if (!(ss_mask & (1 << ssid)))
			continue;

		/* if @ss has non-root csses attached to it, can't move */
		if (css_next_child(NULL, cgroup_css(&ss->root->cgrp, ss)))
			return -EBUSY;

		/* can't move between two non-dummy roots either */
		if (ss->root != &cgrp_dfl_root && dst_root != &cgrp_dfl_root)
			return -EBUSY;
	}

	/* skip creating root files on dfl_root for inhibited subsystems */
	tmp_ss_mask = ss_mask;
	if (dst_root == &cgrp_dfl_root)
		tmp_ss_mask &= ~cgrp_dfl_root_inhibit_ss_mask;

	ret = cgroup_populate_dir(&dst_root->cgrp, tmp_ss_mask);
	if (ret) {
		if (dst_root != &cgrp_dfl_root)
			return ret;

		/*
		 * Rebinding back to the default root is not allowed to
		 * fail.  Using both default and non-default roots should
		 * be rare.  Moving subsystems back and forth even more so.
		 * Just warn about it and continue.
		 */
		if (cgrp_dfl_root_visible) {
			pr_warn("failed to create files (%d) while rebinding 0x%x to default root\n",
				ret, ss_mask);
			pr_warn("you may retry by moving them to a different hierarchy and unbinding\n");
		}
	}

	/*
	 * Nothing can fail from this point on.  Remove files for the
	 * removed subsystems and rebind each subsystem.
	 */
	for_each_subsys(ss, ssid)
		if (ss_mask & (1 << ssid))
			cgroup_clear_dir(&ss->root->cgrp, 1 << ssid);

	for_each_subsys(ss, ssid) {
		struct cgroup_root *src_root;
		struct cgroup_subsys_state *css;
		struct css_set *cset;

		if (!(ss_mask & (1 << ssid)))
			continue;

		src_root = ss->root;
		css = cgroup_css(&src_root->cgrp, ss);

		WARN_ON(!css || cgroup_css(&dst_root->cgrp, ss));

		RCU_INIT_POINTER(src_root->cgrp.subsys[ssid], NULL);
		rcu_assign_pointer(dst_root->cgrp.subsys[ssid], css);
		ss->root = dst_root;
		css->cgroup = &dst_root->cgrp;

		down_write(&css_set_rwsem);
		hash_for_each(css_set_table, i, cset, hlist)
			list_move_tail(&cset->e_cset_node[ss->id],
				       &dst_root->cgrp.e_csets[ss->id]);
		up_write(&css_set_rwsem);

		src_root->subsys_mask &= ~(1 << ssid);
		src_root->cgrp.subtree_control &= ~(1 << ssid);
		cgroup_refresh_child_subsys_mask(&src_root->cgrp);

		/* default hierarchy doesn't enable controllers by default */
		dst_root->subsys_mask |= 1 << ssid;
		if (dst_root != &cgrp_dfl_root) {
			dst_root->cgrp.subtree_control |= 1 << ssid;
			cgroup_refresh_child_subsys_mask(&dst_root->cgrp);
		}

		if (ss->bind)
			ss->bind(css);
	}

	kernfs_activate(dst_root->cgrp.kn);
	return 0;
}

static int cgroup_show_options(struct seq_file *seq,
			       struct kernfs_root *kf_root)
{
	struct cgroup_root *root = cgroup_root_from_kf(kf_root);
	struct cgroup_subsys *ss;
	int ssid;

	for_each_subsys(ss, ssid)
		if (root->subsys_mask & (1 << ssid))
			seq_printf(seq, ",%s", ss->name);
	if (root->flags & CGRP_ROOT_SANE_BEHAVIOR)
		seq_puts(seq, ",sane_behavior");
	if (root->flags & CGRP_ROOT_NOPREFIX)
		seq_puts(seq, ",noprefix");
	if (root->flags & CGRP_ROOT_XATTR)
		seq_puts(seq, ",xattr");

	spin_lock(&release_agent_path_lock);
	if (strlen(root->release_agent_path))
		seq_printf(seq, ",release_agent=%s", root->release_agent_path);
	spin_unlock(&release_agent_path_lock);

	if (test_bit(CGRP_CPUSET_CLONE_CHILDREN, &root->cgrp.flags))
		seq_puts(seq, ",clone_children");
	if (strlen(root->name))
		seq_printf(seq, ",name=%s", root->name);
	return 0;
}

struct cgroup_sb_opts {
	unsigned int subsys_mask;
	unsigned int flags;
	char *release_agent;
	bool cpuset_clone_children;
	char *name;
	/* User explicitly requested empty subsystem */
	bool none;
};

static int parse_cgroupfs_options(char *data, struct cgroup_sb_opts *opts)
{
	char *token, *o = data;
	bool all_ss = false, one_ss = false;
	unsigned int mask = -1U;
	struct cgroup_subsys *ss;
	int i;

#ifdef CONFIG_CPUSETS
	mask = ~(1U << cpuset_cgrp_id);
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
			opts->subsys_mask |= (1 << i);
			one_ss = true;

			break;
		}
		if (i == CGROUP_SUBSYS_COUNT)
			return -ENOENT;
	}

	/* Consistency checks */

	if (opts->flags & CGRP_ROOT_SANE_BEHAVIOR) {
		pr_warn("sane_behavior: this is still under development and its behaviors will change, proceed at your own risk\n");

		if ((opts->flags & (CGRP_ROOT_NOPREFIX | CGRP_ROOT_XATTR)) ||
		    opts->cpuset_clone_children || opts->release_agent ||
		    opts->name) {
			pr_err("sane_behavior: noprefix, xattr, clone_children, release_agent and name are not allowed\n");
			return -EINVAL;
		}
	} else {
		/*
		 * If the 'all' option was specified select all the
		 * subsystems, otherwise if 'none', 'name=' and a subsystem
		 * name options were not specified, let's default to 'all'
		 */
		if (all_ss || (!one_ss && !opts->none && !opts->name))
			for_each_subsys(ss, i)
				if (!ss->disabled)
					opts->subsys_mask |= (1 << i);

		/*
		 * We either have to specify by name or by subsystems. (So
		 * all empty hierarchies must have a name).
		 */
		if (!opts->subsys_mask && !opts->name)
			return -EINVAL;
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

	return 0;
}

static int cgroup_remount(struct kernfs_root *kf_root, int *flags, char *data)
{
	int ret = 0;
	struct cgroup_root *root = cgroup_root_from_kf(kf_root);
	struct cgroup_sb_opts opts;
	unsigned int added_mask, removed_mask;

	if (root->flags & CGRP_ROOT_SANE_BEHAVIOR) {
		pr_err("sane_behavior: remount is not allowed\n");
		return -EINVAL;
	}

	mutex_lock(&cgroup_mutex);

	/* See what subsystems are wanted */
	ret = parse_cgroupfs_options(data, &opts);
	if (ret)
		goto out_unlock;

	if (opts.subsys_mask != root->subsys_mask || opts.release_agent)
		pr_warn("option changes via remount are deprecated (pid=%d comm=%s)\n",
			task_tgid_nr(current), current->comm);

	added_mask = opts.subsys_mask & ~root->subsys_mask;
	removed_mask = root->subsys_mask & ~opts.subsys_mask;

	/* Don't allow flags or name to change at remount */
	if (((opts.flags ^ root->flags) & CGRP_ROOT_OPTION_MASK) ||
	    (opts.name && strcmp(opts.name, root->name))) {
		pr_err("option or name mismatch, new: 0x%x \"%s\", old: 0x%x \"%s\"\n",
		       opts.flags & CGRP_ROOT_OPTION_MASK, opts.name ?: "",
		       root->flags & CGRP_ROOT_OPTION_MASK, root->name);
		ret = -EINVAL;
		goto out_unlock;
	}

	/* remounting is not allowed for populated hierarchies */
	if (!list_empty(&root->cgrp.self.children)) {
		ret = -EBUSY;
		goto out_unlock;
	}

	ret = rebind_subsystems(root, added_mask);
	if (ret)
		goto out_unlock;

	rebind_subsystems(&cgrp_dfl_root, removed_mask);

	if (opts.release_agent) {
		spin_lock(&release_agent_path_lock);
		strcpy(root->release_agent_path, opts.release_agent);
		spin_unlock(&release_agent_path_lock);
	}
 out_unlock:
	kfree(opts.release_agent);
	kfree(opts.name);
	mutex_unlock(&cgroup_mutex);
	return ret;
}

/*
 * To reduce the fork() overhead for systems that are not actually using
 * their cgroups capability, we don't maintain the lists running through
 * each css_set to its tasks until we see the list actually used - in other
 * words after the first mount.
 */
static bool use_task_css_set_links __read_mostly;

static void cgroup_enable_task_cg_lists(void)
{
	struct task_struct *p, *g;

	down_write(&css_set_rwsem);

	if (use_task_css_set_links)
		goto out_unlock;

	use_task_css_set_links = true;

	/*
	 * We need tasklist_lock because RCU is not safe against
	 * while_each_thread(). Besides, a forking task that has passed
	 * cgroup_post_fork() without seeing use_task_css_set_links = 1
	 * is not guaranteed to have its child immediately visible in the
	 * tasklist if we walk through it with RCU.
	 */
	read_lock(&tasklist_lock);
	do_each_thread(g, p) {
		WARN_ON_ONCE(!list_empty(&p->cg_list) ||
			     task_css_set(p) != &init_css_set);

		/*
		 * We should check if the process is exiting, otherwise
		 * it will race with cgroup_exit() in that the list
		 * entry won't be deleted though the process has exited.
		 * Do it while holding siglock so that we don't end up
		 * racing against cgroup_exit().
		 */
		spin_lock_irq(&p->sighand->siglock);
		if (!(p->flags & PF_EXITING)) {
			struct css_set *cset = task_css_set(p);

			list_add(&p->cg_list, &cset->tasks);
			get_css_set(cset);
		}
		spin_unlock_irq(&p->sighand->siglock);
	} while_each_thread(g, p);
	read_unlock(&tasklist_lock);
out_unlock:
	up_write(&css_set_rwsem);
}

static void init_cgroup_housekeeping(struct cgroup *cgrp)
{
	struct cgroup_subsys *ss;
	int ssid;

	INIT_LIST_HEAD(&cgrp->self.sibling);
	INIT_LIST_HEAD(&cgrp->self.children);
	INIT_LIST_HEAD(&cgrp->cset_links);
	INIT_LIST_HEAD(&cgrp->release_list);
	INIT_LIST_HEAD(&cgrp->pidlists);
	mutex_init(&cgrp->pidlist_mutex);
	cgrp->self.cgroup = cgrp;
	cgrp->self.flags |= CSS_ONLINE;

	for_each_subsys(ss, ssid)
		INIT_LIST_HEAD(&cgrp->e_csets[ssid]);

	init_waitqueue_head(&cgrp->offline_waitq);
}

static void init_cgroup_root(struct cgroup_root *root,
			     struct cgroup_sb_opts *opts)
{
	struct cgroup *cgrp = &root->cgrp;

	INIT_LIST_HEAD(&root->root_list);
	atomic_set(&root->nr_cgrps, 1);
	cgrp->root = root;
	init_cgroup_housekeeping(cgrp);
	idr_init(&root->cgroup_idr);

	root->flags = opts->flags;
	if (opts->release_agent)
		strcpy(root->release_agent_path, opts->release_agent);
	if (opts->name)
		strcpy(root->name, opts->name);
	if (opts->cpuset_clone_children)
		set_bit(CGRP_CPUSET_CLONE_CHILDREN, &root->cgrp.flags);
}

static int cgroup_setup_root(struct cgroup_root *root, unsigned int ss_mask)
{
	LIST_HEAD(tmp_links);
	struct cgroup *root_cgrp = &root->cgrp;
	struct css_set *cset;
	int i, ret;

	lockdep_assert_held(&cgroup_mutex);

	ret = cgroup_idr_alloc(&root->cgroup_idr, root_cgrp, 1, 2, GFP_NOWAIT);
	if (ret < 0)
		goto out;
	root_cgrp->id = ret;

	ret = percpu_ref_init(&root_cgrp->self.refcnt, css_release);
	if (ret)
		goto out;

	/*
	 * We're accessing css_set_count without locking css_set_rwsem here,
	 * but that's OK - it can only be increased by someone holding
	 * cgroup_lock, and that's us. The worst that can happen is that we
	 * have some link structures left over
	 */
	ret = allocate_cgrp_cset_links(css_set_count, &tmp_links);
	if (ret)
		goto cancel_ref;

	ret = cgroup_init_root_id(root);
	if (ret)
		goto cancel_ref;

	root->kf_root = kernfs_create_root(&cgroup_kf_syscall_ops,
					   KERNFS_ROOT_CREATE_DEACTIVATED,
					   root_cgrp);
	if (IS_ERR(root->kf_root)) {
		ret = PTR_ERR(root->kf_root);
		goto exit_root_id;
	}
	root_cgrp->kn = root->kf_root->kn;

	ret = cgroup_addrm_files(root_cgrp, cgroup_base_files, true);
	if (ret)
		goto destroy_root;

	ret = rebind_subsystems(root, ss_mask);
	if (ret)
		goto destroy_root;

	/*
	 * There must be no failure case after here, since rebinding takes
	 * care of subsystems' refcounts, which are explicitly dropped in
	 * the failure exit path.
	 */
	list_add(&root->root_list, &cgroup_roots);
	cgroup_root_count++;

	/*
	 * Link the root cgroup in this hierarchy into all the css_set
	 * objects.
	 */
	down_write(&css_set_rwsem);
	hash_for_each(css_set_table, i, cset, hlist)
		link_css_set(&tmp_links, cset, root_cgrp);
	up_write(&css_set_rwsem);

	BUG_ON(!list_empty(&root_cgrp->self.children));
	BUG_ON(atomic_read(&root->nr_cgrps) != 1);

	kernfs_activate(root_cgrp->kn);
	ret = 0;
	goto out;

destroy_root:
	kernfs_destroy_root(root->kf_root);
	root->kf_root = NULL;
exit_root_id:
	cgroup_exit_root_id(root);
cancel_ref:
	percpu_ref_cancel_init(&root_cgrp->self.refcnt);
out:
	free_cgrp_cset_links(&tmp_links);
	return ret;
}

static struct dentry *cgroup_mount(struct file_system_type *fs_type,
			 int flags, const char *unused_dev_name,
			 void *data)
{
	struct cgroup_root *root;
	struct cgroup_sb_opts opts;
	struct dentry *dentry;
	int ret;
	bool new_sb;

	/*
	 * The first time anyone tries to mount a cgroup, enable the list
	 * linking each css_set to its tasks and fix up all existing tasks.
	 */
	if (!use_task_css_set_links)
		cgroup_enable_task_cg_lists();

	mutex_lock(&cgroup_mutex);

	/* First find the desired set of subsystems */
	ret = parse_cgroupfs_options(data, &opts);
	if (ret)
		goto out_unlock;

	/* look for a matching existing root */
	if (!opts.subsys_mask && !opts.none && !opts.name) {
		cgrp_dfl_root_visible = true;
		root = &cgrp_dfl_root;
		cgroup_get(&root->cgrp);
		ret = 0;
		goto out_unlock;
	}

	for_each_root(root) {
		bool name_match = false;

		if (root == &cgrp_dfl_root)
			continue;

		/*
		 * If we asked for a name then it must match.  Also, if
		 * name matches but sybsys_mask doesn't, we should fail.
		 * Remember whether name matched.
		 */
		if (opts.name) {
			if (strcmp(opts.name, root->name))
				continue;
			name_match = true;
		}

		/*
		 * If we asked for subsystems (or explicitly for no
		 * subsystems) then they must match.
		 */
		if ((opts.subsys_mask || opts.none) &&
		    (opts.subsys_mask != root->subsys_mask)) {
			if (!name_match)
				continue;
			ret = -EBUSY;
			goto out_unlock;
		}

		if ((root->flags ^ opts.flags) & CGRP_ROOT_OPTION_MASK) {
			if ((root->flags | opts.flags) & CGRP_ROOT_SANE_BEHAVIOR) {
				pr_err("sane_behavior: new mount options should match the existing superblock\n");
				ret = -EINVAL;
				goto out_unlock;
			} else {
				pr_warn("new mount options do not match the existing superblock, will be ignored\n");
			}
		}

		/*
		 * A root's lifetime is governed by its root cgroup.
		 * tryget_live failure indicate that the root is being
		 * destroyed.  Wait for destruction to complete so that the
		 * subsystems are free.  We can use wait_queue for the wait
		 * but this path is super cold.  Let's just sleep for a bit
		 * and retry.
		 */
		if (!percpu_ref_tryget_live(&root->cgrp.self.refcnt)) {
			mutex_unlock(&cgroup_mutex);
			msleep(10);
			ret = restart_syscall();
			goto out_free;
		}

		ret = 0;
		goto out_unlock;
	}

	/*
	 * No such thing, create a new one.  name= matching without subsys
	 * specification is allowed for already existing hierarchies but we
	 * can't create new one without subsys specification.
	 */
	if (!opts.subsys_mask && !opts.none) {
		ret = -EINVAL;
		goto out_unlock;
	}

	root = kzalloc(sizeof(*root), GFP_KERNEL);
	if (!root) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	init_cgroup_root(root, &opts);

	ret = cgroup_setup_root(root, opts.subsys_mask);
	if (ret)
		cgroup_free_root(root);

out_unlock:
	mutex_unlock(&cgroup_mutex);
out_free:
	kfree(opts.release_agent);
	kfree(opts.name);

	if (ret)
		return ERR_PTR(ret);

	dentry = kernfs_mount(fs_type, flags, root->kf_root,
				CGROUP_SUPER_MAGIC, &new_sb);
	if (IS_ERR(dentry) || !new_sb)
		cgroup_put(&root->cgrp);
	return dentry;
}

static void cgroup_kill_sb(struct super_block *sb)
{
	struct kernfs_root *kf_root = kernfs_root_from_sb(sb);
	struct cgroup_root *root = cgroup_root_from_kf(kf_root);

	/*
	 * If @root doesn't have any mounts or children, start killing it.
	 * This prevents new mounts by disabling percpu_ref_tryget_live().
	 * cgroup_mount() may wait for @root's release.
	 *
	 * And don't kill the default root.
	 */
	if (css_has_online_children(&root->cgrp.self) ||
	    root == &cgrp_dfl_root)
		cgroup_put(&root->cgrp);
	else
		percpu_ref_kill(&root->cgrp.self.refcnt);

	kernfs_kill_sb(sb);
}

static struct file_system_type cgroup_fs_type = {
	.name = "cgroup",
	.mount = cgroup_mount,
	.kill_sb = cgroup_kill_sb,
};

static struct kobject *cgroup_kobj;

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
 * Return value is the same as kernfs_path().
 */
char *task_cgroup_path(struct task_struct *task, char *buf, size_t buflen)
{
	struct cgroup_root *root;
	struct cgroup *cgrp;
	int hierarchy_id = 1;
	char *path = NULL;

	mutex_lock(&cgroup_mutex);
	down_read(&css_set_rwsem);

	root = idr_get_next(&cgroup_hierarchy_idr, &hierarchy_id);

	if (root) {
		cgrp = task_cgroup_from_root(task, root);
		path = cgroup_path(cgrp, buf, buflen);
	} else {
		/* if no hierarchy exists, everyone is in "/" */
		if (strlcpy(buf, "/", buflen) < buflen)
			path = buf;
	}

	up_read(&css_set_rwsem);
	mutex_unlock(&cgroup_mutex);
	return path;
}
EXPORT_SYMBOL_GPL(task_cgroup_path);

/* used to track tasks and other necessary states during migration */
struct cgroup_taskset {
	/* the src and dst cset list running through cset->mg_node */
	struct list_head	src_csets;
	struct list_head	dst_csets;

	/*
	 * Fields for cgroup_taskset_*() iteration.
	 *
	 * Before migration is committed, the target migration tasks are on
	 * ->mg_tasks of the csets on ->src_csets.  After, on ->mg_tasks of
	 * the csets on ->dst_csets.  ->csets point to either ->src_csets
	 * or ->dst_csets depending on whether migration is committed.
	 *
	 * ->cur_csets and ->cur_task point to the current task position
	 * during iteration.
	 */
	struct list_head	*csets;
	struct css_set		*cur_cset;
	struct task_struct	*cur_task;
};

/**
 * cgroup_taskset_first - reset taskset and return the first task
 * @tset: taskset of interest
 *
 * @tset iteration is initialized and the first task is returned.
 */
struct task_struct *cgroup_taskset_first(struct cgroup_taskset *tset)
{
	tset->cur_cset = list_first_entry(tset->csets, struct css_set, mg_node);
	tset->cur_task = NULL;

	return cgroup_taskset_next(tset);
}

/**
 * cgroup_taskset_next - iterate to the next task in taskset
 * @tset: taskset of interest
 *
 * Return the next task in @tset.  Iteration must have been initialized
 * with cgroup_taskset_first().
 */
struct task_struct *cgroup_taskset_next(struct cgroup_taskset *tset)
{
	struct css_set *cset = tset->cur_cset;
	struct task_struct *task = tset->cur_task;

	while (&cset->mg_node != tset->csets) {
		if (!task)
			task = list_first_entry(&cset->mg_tasks,
						struct task_struct, cg_list);
		else
			task = list_next_entry(task, cg_list);

		if (&task->cg_list != &cset->mg_tasks) {
			tset->cur_cset = cset;
			tset->cur_task = task;
			return task;
		}

		cset = list_next_entry(cset, mg_node);
		task = NULL;
	}

	return NULL;
}

/**
 * cgroup_task_migrate - move a task from one cgroup to another.
 * @old_cgrp: the cgroup @tsk is being migrated from
 * @tsk: the task being migrated
 * @new_cset: the new css_set @tsk is being attached to
 *
 * Must be called with cgroup_mutex, threadgroup and css_set_rwsem locked.
 */
static void cgroup_task_migrate(struct cgroup *old_cgrp,
				struct task_struct *tsk,
				struct css_set *new_cset)
{
	struct css_set *old_cset;

	lockdep_assert_held(&cgroup_mutex);
	lockdep_assert_held(&css_set_rwsem);

	/*
	 * We are synchronized through threadgroup_lock() against PF_EXITING
	 * setting such that we can't race against cgroup_exit() changing the
	 * css_set to init_css_set and dropping the old one.
	 */
	WARN_ON_ONCE(tsk->flags & PF_EXITING);
	old_cset = task_css_set(tsk);

	get_css_set(new_cset);
	rcu_assign_pointer(tsk->cgroups, new_cset);

	/*
	 * Use move_tail so that cgroup_taskset_first() still returns the
	 * leader after migration.  This works because cgroup_migrate()
	 * ensures that the dst_cset of the leader is the first on the
	 * tset's dst_csets list.
	 */
	list_move_tail(&tsk->cg_list, &new_cset->mg_tasks);

	/*
	 * We just gained a reference on old_cset by taking it from the
	 * task. As trading it for new_cset is protected by cgroup_mutex,
	 * we're safe to drop it here; it will be freed under RCU.
	 */
	set_bit(CGRP_RELEASABLE, &old_cgrp->flags);
	put_css_set_locked(old_cset, false);
}

/**
 * cgroup_migrate_finish - cleanup after attach
 * @preloaded_csets: list of preloaded css_sets
 *
 * Undo cgroup_migrate_add_src() and cgroup_migrate_prepare_dst().  See
 * those functions for details.
 */
static void cgroup_migrate_finish(struct list_head *preloaded_csets)
{
	struct css_set *cset, *tmp_cset;

	lockdep_assert_held(&cgroup_mutex);

	down_write(&css_set_rwsem);
	list_for_each_entry_safe(cset, tmp_cset, preloaded_csets, mg_preload_node) {
		cset->mg_src_cgrp = NULL;
		cset->mg_dst_cset = NULL;
		list_del_init(&cset->mg_preload_node);
		put_css_set_locked(cset, false);
	}
	up_write(&css_set_rwsem);
}

/**
 * cgroup_migrate_add_src - add a migration source css_set
 * @src_cset: the source css_set to add
 * @dst_cgrp: the destination cgroup
 * @preloaded_csets: list of preloaded css_sets
 *
 * Tasks belonging to @src_cset are about to be migrated to @dst_cgrp.  Pin
 * @src_cset and add it to @preloaded_csets, which should later be cleaned
 * up by cgroup_migrate_finish().
 *
 * This function may be called without holding threadgroup_lock even if the
 * target is a process.  Threads may be created and destroyed but as long
 * as cgroup_mutex is not dropped, no new css_set can be put into play and
 * the preloaded css_sets are guaranteed to cover all migrations.
 */
static void cgroup_migrate_add_src(struct css_set *src_cset,
				   struct cgroup *dst_cgrp,
				   struct list_head *preloaded_csets)
{
	struct cgroup *src_cgrp;

	lockdep_assert_held(&cgroup_mutex);
	lockdep_assert_held(&css_set_rwsem);

	src_cgrp = cset_cgroup_from_root(src_cset, dst_cgrp->root);

	if (!list_empty(&src_cset->mg_preload_node))
		return;

	WARN_ON(src_cset->mg_src_cgrp);
	WARN_ON(!list_empty(&src_cset->mg_tasks));
	WARN_ON(!list_empty(&src_cset->mg_node));

	src_cset->mg_src_cgrp = src_cgrp;
	get_css_set(src_cset);
	list_add(&src_cset->mg_preload_node, preloaded_csets);
}

/**
 * cgroup_migrate_prepare_dst - prepare destination css_sets for migration
 * @dst_cgrp: the destination cgroup (may be %NULL)
 * @preloaded_csets: list of preloaded source css_sets
 *
 * Tasks are about to be moved to @dst_cgrp and all the source css_sets
 * have been preloaded to @preloaded_csets.  This function looks up and
 * pins all destination css_sets, links each to its source, and append them
 * to @preloaded_csets.  If @dst_cgrp is %NULL, the destination of each
 * source css_set is assumed to be its cgroup on the default hierarchy.
 *
 * This function must be called after cgroup_migrate_add_src() has been
 * called on each migration source css_set.  After migration is performed
 * using cgroup_migrate(), cgroup_migrate_finish() must be called on
 * @preloaded_csets.
 */
static int cgroup_migrate_prepare_dst(struct cgroup *dst_cgrp,
				      struct list_head *preloaded_csets)
{
	LIST_HEAD(csets);
	struct css_set *src_cset, *tmp_cset;

	lockdep_assert_held(&cgroup_mutex);

	/*
	 * Except for the root, child_subsys_mask must be zero for a cgroup
	 * with tasks so that child cgroups don't compete against tasks.
	 */
	if (dst_cgrp && cgroup_on_dfl(dst_cgrp) && cgroup_parent(dst_cgrp) &&
	    dst_cgrp->child_subsys_mask)
		return -EBUSY;

	/* look up the dst cset for each src cset and link it to src */
	list_for_each_entry_safe(src_cset, tmp_cset, preloaded_csets, mg_preload_node) {
		struct css_set *dst_cset;

		dst_cset = find_css_set(src_cset,
					dst_cgrp ?: src_cset->dfl_cgrp);
		if (!dst_cset)
			goto err;

		WARN_ON_ONCE(src_cset->mg_dst_cset || dst_cset->mg_dst_cset);

		/*
		 * If src cset equals dst, it's noop.  Drop the src.
		 * cgroup_migrate() will skip the cset too.  Note that we
		 * can't handle src == dst as some nodes are used by both.
		 */
		if (src_cset == dst_cset) {
			src_cset->mg_src_cgrp = NULL;
			list_del_init(&src_cset->mg_preload_node);
			put_css_set(src_cset, false);
			put_css_set(dst_cset, false);
			continue;
		}

		src_cset->mg_dst_cset = dst_cset;

		if (list_empty(&dst_cset->mg_preload_node))
			list_add(&dst_cset->mg_preload_node, &csets);
		else
			put_css_set(dst_cset, false);
	}

	list_splice_tail(&csets, preloaded_csets);
	return 0;
err:
	cgroup_migrate_finish(&csets);
	return -ENOMEM;
}

/**
 * cgroup_migrate - migrate a process or task to a cgroup
 * @cgrp: the destination cgroup
 * @leader: the leader of the process or the task to migrate
 * @threadgroup: whether @leader points to the whole process or a single task
 *
 * Migrate a process or task denoted by @leader to @cgrp.  If migrating a
 * process, the caller must be holding threadgroup_lock of @leader.  The
 * caller is also responsible for invoking cgroup_migrate_add_src() and
 * cgroup_migrate_prepare_dst() on the targets before invoking this
 * function and following up with cgroup_migrate_finish().
 *
 * As long as a controller's ->can_attach() doesn't fail, this function is
 * guaranteed to succeed.  This means that, excluding ->can_attach()
 * failure, when migrating multiple targets, the success or failure can be
 * decided for all targets by invoking group_migrate_prepare_dst() before
 * actually starting migrating.
 */
static int cgroup_migrate(struct cgroup *cgrp, struct task_struct *leader,
			  bool threadgroup)
{
	struct cgroup_taskset tset = {
		.src_csets	= LIST_HEAD_INIT(tset.src_csets),
		.dst_csets	= LIST_HEAD_INIT(tset.dst_csets),
		.csets		= &tset.src_csets,
	};
	struct cgroup_subsys_state *css, *failed_css = NULL;
	struct css_set *cset, *tmp_cset;
	struct task_struct *task, *tmp_task;
	int i, ret;

	/*
	 * Prevent freeing of tasks while we take a snapshot. Tasks that are
	 * already PF_EXITING could be freed from underneath us unless we
	 * take an rcu_read_lock.
	 */
	down_write(&css_set_rwsem);
	rcu_read_lock();
	task = leader;
	do {
		/* @task either already exited or can't exit until the end */
		if (task->flags & PF_EXITING)
			goto next;

		/* leave @task alone if post_fork() hasn't linked it yet */
		if (list_empty(&task->cg_list))
			goto next;

		cset = task_css_set(task);
		if (!cset->mg_src_cgrp)
			goto next;

		/*
		 * cgroup_taskset_first() must always return the leader.
		 * Take care to avoid disturbing the ordering.
		 */
		list_move_tail(&task->cg_list, &cset->mg_tasks);
		if (list_empty(&cset->mg_node))
			list_add_tail(&cset->mg_node, &tset.src_csets);
		if (list_empty(&cset->mg_dst_cset->mg_node))
			list_move_tail(&cset->mg_dst_cset->mg_node,
				       &tset.dst_csets);
	next:
		if (!threadgroup)
			break;
	} while_each_thread(leader, task);
	rcu_read_unlock();
	up_write(&css_set_rwsem);

	/* methods shouldn't be called if no task is actually migrating */
	if (list_empty(&tset.src_csets))
		return 0;

	/* check that we can legitimately attach to the cgroup */
	for_each_e_css(css, i, cgrp) {
		if (css->ss->can_attach) {
			ret = css->ss->can_attach(css, &tset);
			if (ret) {
				failed_css = css;
				goto out_cancel_attach;
			}
		}
	}

	/*
	 * Now that we're guaranteed success, proceed to move all tasks to
	 * the new cgroup.  There are no failure cases after here, so this
	 * is the commit point.
	 */
	down_write(&css_set_rwsem);
	list_for_each_entry(cset, &tset.src_csets, mg_node) {
		list_for_each_entry_safe(task, tmp_task, &cset->mg_tasks, cg_list)
			cgroup_task_migrate(cset->mg_src_cgrp, task,
					    cset->mg_dst_cset);
	}
	up_write(&css_set_rwsem);

	/*
	 * Migration is committed, all target tasks are now on dst_csets.
	 * Nothing is sensitive to fork() after this point.  Notify
	 * controllers that migration is complete.
	 */
	tset.csets = &tset.dst_csets;

	for_each_e_css(css, i, cgrp)
		if (css->ss->attach)
			css->ss->attach(css, &tset);

	ret = 0;
	goto out_release_tset;

out_cancel_attach:
	for_each_e_css(css, i, cgrp) {
		if (css == failed_css)
			break;
		if (css->ss->cancel_attach)
			css->ss->cancel_attach(css, &tset);
	}
out_release_tset:
	down_write(&css_set_rwsem);
	list_splice_init(&tset.dst_csets, &tset.src_csets);
	list_for_each_entry_safe(cset, tmp_cset, &tset.src_csets, mg_node) {
		list_splice_tail_init(&cset->mg_tasks, &cset->tasks);
		list_del_init(&cset->mg_node);
	}
	up_write(&css_set_rwsem);
	return ret;
}

/**
 * cgroup_attach_task - attach a task or a whole threadgroup to a cgroup
 * @dst_cgrp: the cgroup to attach to
 * @leader: the task or the leader of the threadgroup to be attached
 * @threadgroup: attach the whole threadgroup?
 *
 * Call holding cgroup_mutex and threadgroup_lock of @leader.
 */
static int cgroup_attach_task(struct cgroup *dst_cgrp,
			      struct task_struct *leader, bool threadgroup)
{
	LIST_HEAD(preloaded_csets);
	struct task_struct *task;
	int ret;

	/* look up all src csets */
	down_read(&css_set_rwsem);
	rcu_read_lock();
	task = leader;
	do {
		cgroup_migrate_add_src(task_css_set(task), dst_cgrp,
				       &preloaded_csets);
		if (!threadgroup)
			break;
	} while_each_thread(leader, task);
	rcu_read_unlock();
	up_read(&css_set_rwsem);

	/* prepare dst csets and commit */
	ret = cgroup_migrate_prepare_dst(dst_cgrp, &preloaded_csets);
	if (!ret)
		ret = cgroup_migrate(dst_cgrp, leader, threadgroup);

	cgroup_migrate_finish(&preloaded_csets);
	return ret;
}

/*
 * Find the task_struct of the task to attach by vpid and pass it along to the
 * function to attach either it or all tasks in its threadgroup. Will lock
 * cgroup_mutex and threadgroup.
 */
static ssize_t __cgroup_procs_write(struct kernfs_open_file *of, char *buf,
				    size_t nbytes, loff_t off, bool threadgroup)
{
	struct task_struct *tsk;
	const struct cred *cred = current_cred(), *tcred;
	struct cgroup *cgrp;
	pid_t pid;
	int ret;

	if (kstrtoint(strstrip(buf), 0, &pid) || pid < 0)
		return -EINVAL;

	cgrp = cgroup_kn_lock_live(of->kn);
	if (!cgrp)
		return -ENODEV;

retry_find_task:
	rcu_read_lock();
	if (pid) {
		tsk = find_task_by_vpid(pid);
		if (!tsk) {
			rcu_read_unlock();
			ret = -ESRCH;
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
	cgroup_kn_unlock(of->kn);
	return ret ?: nbytes;
}

/**
 * cgroup_attach_task_all - attach task 'tsk' to all cgroups of task 'from'
 * @from: attach to all cgroups of a given task
 * @tsk: the task to be attached
 */
int cgroup_attach_task_all(struct task_struct *from, struct task_struct *tsk)
{
	struct cgroup_root *root;
	int retval = 0;

	mutex_lock(&cgroup_mutex);
	for_each_root(root) {
		struct cgroup *from_cgrp;

		if (root == &cgrp_dfl_root)
			continue;

		down_read(&css_set_rwsem);
		from_cgrp = task_cgroup_from_root(from, root);
		up_read(&css_set_rwsem);

		retval = cgroup_attach_task(from_cgrp, tsk, false);
		if (retval)
			break;
	}
	mutex_unlock(&cgroup_mutex);

	return retval;
}
EXPORT_SYMBOL_GPL(cgroup_attach_task_all);

static ssize_t cgroup_tasks_write(struct kernfs_open_file *of,
				  char *buf, size_t nbytes, loff_t off)
{
	return __cgroup_procs_write(of, buf, nbytes, off, false);
}

static ssize_t cgroup_procs_write(struct kernfs_open_file *of,
				  char *buf, size_t nbytes, loff_t off)
{
	return __cgroup_procs_write(of, buf, nbytes, off, true);
}

static ssize_t cgroup_release_agent_write(struct kernfs_open_file *of,
					  char *buf, size_t nbytes, loff_t off)
{
	struct cgroup *cgrp;

	BUILD_BUG_ON(sizeof(cgrp->root->release_agent_path) < PATH_MAX);

	cgrp = cgroup_kn_lock_live(of->kn);
	if (!cgrp)
		return -ENODEV;
	spin_lock(&release_agent_path_lock);
	strlcpy(cgrp->root->release_agent_path, strstrip(buf),
		sizeof(cgrp->root->release_agent_path));
	spin_unlock(&release_agent_path_lock);
	cgroup_kn_unlock(of->kn);
	return nbytes;
}

static int cgroup_release_agent_show(struct seq_file *seq, void *v)
{
	struct cgroup *cgrp = seq_css(seq)->cgroup;

	spin_lock(&release_agent_path_lock);
	seq_puts(seq, cgrp->root->release_agent_path);
	spin_unlock(&release_agent_path_lock);
	seq_putc(seq, '\n');
	return 0;
}

static int cgroup_sane_behavior_show(struct seq_file *seq, void *v)
{
	struct cgroup *cgrp = seq_css(seq)->cgroup;

	seq_printf(seq, "%d\n", cgroup_sane_behavior(cgrp));
	return 0;
}

static void cgroup_print_ss_mask(struct seq_file *seq, unsigned int ss_mask)
{
	struct cgroup_subsys *ss;
	bool printed = false;
	int ssid;

	for_each_subsys(ss, ssid) {
		if (ss_mask & (1 << ssid)) {
			if (printed)
				seq_putc(seq, ' ');
			seq_printf(seq, "%s", ss->name);
			printed = true;
		}
	}
	if (printed)
		seq_putc(seq, '\n');
}

/* show controllers which are currently attached to the default hierarchy */
static int cgroup_root_controllers_show(struct seq_file *seq, void *v)
{
	struct cgroup *cgrp = seq_css(seq)->cgroup;

	cgroup_print_ss_mask(seq, cgrp->root->subsys_mask &
			     ~cgrp_dfl_root_inhibit_ss_mask);
	return 0;
}

/* show controllers which are enabled from the parent */
static int cgroup_controllers_show(struct seq_file *seq, void *v)
{
	struct cgroup *cgrp = seq_css(seq)->cgroup;

	cgroup_print_ss_mask(seq, cgroup_parent(cgrp)->subtree_control);
	return 0;
}

/* show controllers which are enabled for a given cgroup's children */
static int cgroup_subtree_control_show(struct seq_file *seq, void *v)
{
	struct cgroup *cgrp = seq_css(seq)->cgroup;

	cgroup_print_ss_mask(seq, cgrp->subtree_control);
	return 0;
}

/**
 * cgroup_update_dfl_csses - update css assoc of a subtree in default hierarchy
 * @cgrp: root of the subtree to update csses for
 *
 * @cgrp's child_subsys_mask has changed and its subtree's (self excluded)
 * css associations need to be updated accordingly.  This function looks up
 * all css_sets which are attached to the subtree, creates the matching
 * updated css_sets and migrates the tasks to the new ones.
 */
static int cgroup_update_dfl_csses(struct cgroup *cgrp)
{
	LIST_HEAD(preloaded_csets);
	struct cgroup_subsys_state *css;
	struct css_set *src_cset;
	int ret;

	lockdep_assert_held(&cgroup_mutex);

	/* look up all csses currently attached to @cgrp's subtree */
	down_read(&css_set_rwsem);
	css_for_each_descendant_pre(css, cgroup_css(cgrp, NULL)) {
		struct cgrp_cset_link *link;

		/* self is not affected by child_subsys_mask change */
		if (css->cgroup == cgrp)
			continue;

		list_for_each_entry(link, &css->cgroup->cset_links, cset_link)
			cgroup_migrate_add_src(link->cset, cgrp,
					       &preloaded_csets);
	}
	up_read(&css_set_rwsem);

	/* NULL dst indicates self on default hierarchy */
	ret = cgroup_migrate_prepare_dst(NULL, &preloaded_csets);
	if (ret)
		goto out_finish;

	list_for_each_entry(src_cset, &preloaded_csets, mg_preload_node) {
		struct task_struct *last_task = NULL, *task;

		/* src_csets precede dst_csets, break on the first dst_cset */
		if (!src_cset->mg_src_cgrp)
			break;

		/*
		 * All tasks in src_cset need to be migrated to the
		 * matching dst_cset.  Empty it process by process.  We
		 * walk tasks but migrate processes.  The leader might even
		 * belong to a different cset but such src_cset would also
		 * be among the target src_csets because the default
		 * hierarchy enforces per-process membership.
		 */
		while (true) {
			down_read(&css_set_rwsem);
			task = list_first_entry_or_null(&src_cset->tasks,
						struct task_struct, cg_list);
			if (task) {
				task = task->group_leader;
				WARN_ON_ONCE(!task_css_set(task)->mg_src_cgrp);
				get_task_struct(task);
			}
			up_read(&css_set_rwsem);

			if (!task)
				break;

			/* guard against possible infinite loop */
			if (WARN(last_task == task,
				 "cgroup: update_dfl_csses failed to make progress, aborting in inconsistent state\n"))
				goto out_finish;
			last_task = task;

			threadgroup_lock(task);
			/* raced against de_thread() from another thread? */
			if (!thread_group_leader(task)) {
				threadgroup_unlock(task);
				put_task_struct(task);
				continue;
			}

			ret = cgroup_migrate(src_cset->dfl_cgrp, task, true);

			threadgroup_unlock(task);
			put_task_struct(task);

			if (WARN(ret, "cgroup: failed to update controllers for the default hierarchy (%d), further operations may crash or hang\n", ret))
				goto out_finish;
		}
	}

out_finish:
	cgroup_migrate_finish(&preloaded_csets);
	return ret;
}

/* change the enabled child controllers for a cgroup in the default hierarchy */
static ssize_t cgroup_subtree_control_write(struct kernfs_open_file *of,
					    char *buf, size_t nbytes,
					    loff_t off)
{
	unsigned int enable = 0, disable = 0;
	unsigned int css_enable, css_disable, old_ctrl, new_ctrl;
	struct cgroup *cgrp, *child;
	struct cgroup_subsys *ss;
	char *tok;
	int ssid, ret;

	/*
	 * Parse input - space separated list of subsystem names prefixed
	 * with either + or -.
	 */
	buf = strstrip(buf);
	while ((tok = strsep(&buf, " "))) {
		if (tok[0] == '\0')
			continue;
		for_each_subsys(ss, ssid) {
			if (ss->disabled || strcmp(tok + 1, ss->name) ||
			    ((1 << ss->id) & cgrp_dfl_root_inhibit_ss_mask))
				continue;

			if (*tok == '+') {
				enable |= 1 << ssid;
				disable &= ~(1 << ssid);
			} else if (*tok == '-') {
				disable |= 1 << ssid;
				enable &= ~(1 << ssid);
			} else {
				return -EINVAL;
			}
			break;
		}
		if (ssid == CGROUP_SUBSYS_COUNT)
			return -EINVAL;
	}

	cgrp = cgroup_kn_lock_live(of->kn);
	if (!cgrp)
		return -ENODEV;

	for_each_subsys(ss, ssid) {
		if (enable & (1 << ssid)) {
			if (cgrp->subtree_control & (1 << ssid)) {
				enable &= ~(1 << ssid);
				continue;
			}

			/* unavailable or not enabled on the parent? */
			if (!(cgrp_dfl_root.subsys_mask & (1 << ssid)) ||
			    (cgroup_parent(cgrp) &&
			     !(cgroup_parent(cgrp)->subtree_control & (1 << ssid)))) {
				ret = -ENOENT;
				goto out_unlock;
			}

			/*
			 * @ss is already enabled through dependency and
			 * we'll just make it visible.  Skip draining.
			 */
			if (cgrp->child_subsys_mask & (1 << ssid))
				continue;

			/*
			 * Because css offlining is asynchronous, userland
			 * might try to re-enable the same controller while
			 * the previous instance is still around.  In such
			 * cases, wait till it's gone using offline_waitq.
			 */
			cgroup_for_each_live_child(child, cgrp) {
				DEFINE_WAIT(wait);

				if (!cgroup_css(child, ss))
					continue;

				cgroup_get(child);
				prepare_to_wait(&child->offline_waitq, &wait,
						TASK_UNINTERRUPTIBLE);
				cgroup_kn_unlock(of->kn);
				schedule();
				finish_wait(&child->offline_waitq, &wait);
				cgroup_put(child);

				return restart_syscall();
			}
		} else if (disable & (1 << ssid)) {
			if (!(cgrp->subtree_control & (1 << ssid))) {
				disable &= ~(1 << ssid);
				continue;
			}

			/* a child has it enabled? */
			cgroup_for_each_live_child(child, cgrp) {
				if (child->subtree_control & (1 << ssid)) {
					ret = -EBUSY;
					goto out_unlock;
				}
			}
		}
	}

	if (!enable && !disable) {
		ret = 0;
		goto out_unlock;
	}

	/*
	 * Except for the root, subtree_control must be zero for a cgroup
	 * with tasks so that child cgroups don't compete against tasks.
	 */
	if (enable && cgroup_parent(cgrp) && !list_empty(&cgrp->cset_links)) {
		ret = -EBUSY;
		goto out_unlock;
	}

	/*
	 * Update subsys masks and calculate what needs to be done.  More
	 * subsystems than specified may need to be enabled or disabled
	 * depending on subsystem dependencies.
	 */
	cgrp->subtree_control |= enable;
	cgrp->subtree_control &= ~disable;

	old_ctrl = cgrp->child_subsys_mask;
	cgroup_refresh_child_subsys_mask(cgrp);
	new_ctrl = cgrp->child_subsys_mask;

	css_enable = ~old_ctrl & new_ctrl;
	css_disable = old_ctrl & ~new_ctrl;
	enable |= css_enable;
	disable |= css_disable;

	/*
	 * Create new csses or make the existing ones visible.  A css is
	 * created invisible if it's being implicitly enabled through
	 * dependency.  An invisible css is made visible when the userland
	 * explicitly enables it.
	 */
	for_each_subsys(ss, ssid) {
		if (!(enable & (1 << ssid)))
			continue;

		cgroup_for_each_live_child(child, cgrp) {
			if (css_enable & (1 << ssid))
				ret = create_css(child, ss,
					cgrp->subtree_control & (1 << ssid));
			else
				ret = cgroup_populate_dir(child, 1 << ssid);
			if (ret)
				goto err_undo_css;
		}
	}

	/*
	 * At this point, cgroup_e_css() results reflect the new csses
	 * making the following cgroup_update_dfl_csses() properly update
	 * css associations of all tasks in the subtree.
	 */
	ret = cgroup_update_dfl_csses(cgrp);
	if (ret)
		goto err_undo_css;

	/*
	 * All tasks are migrated out of disabled csses.  Kill or hide
	 * them.  A css is hidden when the userland requests it to be
	 * disabled while other subsystems are still depending on it.  The
	 * css must not actively control resources and be in the vanilla
	 * state if it's made visible again later.  Controllers which may
	 * be depended upon should provide ->css_reset() for this purpose.
	 */
	for_each_subsys(ss, ssid) {
		if (!(disable & (1 << ssid)))
			continue;

		cgroup_for_each_live_child(child, cgrp) {
			struct cgroup_subsys_state *css = cgroup_css(child, ss);

			if (css_disable & (1 << ssid)) {
				kill_css(css);
			} else {
				cgroup_clear_dir(child, 1 << ssid);
				if (ss->css_reset)
					ss->css_reset(css);
			}
		}
	}

	kernfs_activate(cgrp->kn);
	ret = 0;
out_unlock:
	cgroup_kn_unlock(of->kn);
	return ret ?: nbytes;

err_undo_css:
	cgrp->subtree_control &= ~enable;
	cgrp->subtree_control |= disable;
	cgroup_refresh_child_subsys_mask(cgrp);

	for_each_subsys(ss, ssid) {
		if (!(enable & (1 << ssid)))
			continue;

		cgroup_for_each_live_child(child, cgrp) {
			struct cgroup_subsys_state *css = cgroup_css(child, ss);

			if (!css)
				continue;

			if (css_enable & (1 << ssid))
				kill_css(css);
			else
				cgroup_clear_dir(child, 1 << ssid);
		}
	}
	goto out_unlock;
}

static int cgroup_populated_show(struct seq_file *seq, void *v)
{
	seq_printf(seq, "%d\n", (bool)seq_css(seq)->cgroup->populated_cnt);
	return 0;
}

static ssize_t cgroup_file_write(struct kernfs_open_file *of, char *buf,
				 size_t nbytes, loff_t off)
{
	struct cgroup *cgrp = of->kn->parent->priv;
	struct cftype *cft = of->kn->priv;
	struct cgroup_subsys_state *css;
	int ret;

	if (cft->write)
		return cft->write(of, buf, nbytes, off);

	/*
	 * kernfs guarantees that a file isn't deleted with operations in
	 * flight, which means that the matching css is and stays alive and
	 * doesn't need to be pinned.  The RCU locking is not necessary
	 * either.  It's just for the convenience of using cgroup_css().
	 */
	rcu_read_lock();
	css = cgroup_css(cgrp, cft->ss);
	rcu_read_unlock();

	if (cft->write_u64) {
		unsigned long long v;
		ret = kstrtoull(buf, 0, &v);
		if (!ret)
			ret = cft->write_u64(css, cft, v);
	} else if (cft->write_s64) {
		long long v;
		ret = kstrtoll(buf, 0, &v);
		if (!ret)
			ret = cft->write_s64(css, cft, v);
	} else {
		ret = -EINVAL;
	}

	return ret ?: nbytes;
}

static void *cgroup_seqfile_start(struct seq_file *seq, loff_t *ppos)
{
	return seq_cft(seq)->seq_start(seq, ppos);
}

static void *cgroup_seqfile_next(struct seq_file *seq, void *v, loff_t *ppos)
{
	return seq_cft(seq)->seq_next(seq, v, ppos);
}

static void cgroup_seqfile_stop(struct seq_file *seq, void *v)
{
	seq_cft(seq)->seq_stop(seq, v);
}

static int cgroup_seqfile_show(struct seq_file *m, void *arg)
{
	struct cftype *cft = seq_cft(m);
	struct cgroup_subsys_state *css = seq_css(m);

	if (cft->seq_show)
		return cft->seq_show(m, arg);

	if (cft->read_u64)
		seq_printf(m, "%llu\n", cft->read_u64(css, cft));
	else if (cft->read_s64)
		seq_printf(m, "%lld\n", cft->read_s64(css, cft));
	else
		return -EINVAL;
	return 0;
}

static struct kernfs_ops cgroup_kf_single_ops = {
	.atomic_write_len	= PAGE_SIZE,
	.write			= cgroup_file_write,
	.seq_show		= cgroup_seqfile_show,
};

static struct kernfs_ops cgroup_kf_ops = {
	.atomic_write_len	= PAGE_SIZE,
	.write			= cgroup_file_write,
	.seq_start		= cgroup_seqfile_start,
	.seq_next		= cgroup_seqfile_next,
	.seq_stop		= cgroup_seqfile_stop,
	.seq_show		= cgroup_seqfile_show,
};

/*
 * cgroup_rename - Only allow simple rename of directories in place.
 */
static int cgroup_rename(struct kernfs_node *kn, struct kernfs_node *new_parent,
			 const char *new_name_str)
{
	struct cgroup *cgrp = kn->priv;
	int ret;

	if (kernfs_type(kn) != KERNFS_DIR)
		return -ENOTDIR;
	if (kn->parent != new_parent)
		return -EIO;

	/*
	 * This isn't a proper migration and its usefulness is very
	 * limited.  Disallow if sane_behavior.
	 */
	if (cgroup_sane_behavior(cgrp))
		return -EPERM;

	/*
	 * We're gonna grab cgroup_mutex which nests outside kernfs
	 * active_ref.  kernfs_rename() doesn't require active_ref
	 * protection.  Break them before grabbing cgroup_mutex.
	 */
	kernfs_break_active_protection(new_parent);
	kernfs_break_active_protection(kn);

	mutex_lock(&cgroup_mutex);

	ret = kernfs_rename(kn, new_parent, new_name_str);

	mutex_unlock(&cgroup_mutex);

	kernfs_unbreak_active_protection(kn);
	kernfs_unbreak_active_protection(new_parent);
	return ret;
}

/* set uid and gid of cgroup dirs and files to that of the creator */
static int cgroup_kn_set_ugid(struct kernfs_node *kn)
{
	struct iattr iattr = { .ia_valid = ATTR_UID | ATTR_GID,
			       .ia_uid = current_fsuid(),
			       .ia_gid = current_fsgid(), };

	if (uid_eq(iattr.ia_uid, GLOBAL_ROOT_UID) &&
	    gid_eq(iattr.ia_gid, GLOBAL_ROOT_GID))
		return 0;

	return kernfs_setattr(kn, &iattr);
}

static int cgroup_add_file(struct cgroup *cgrp, struct cftype *cft)
{
	char name[CGROUP_FILE_NAME_MAX];
	struct kernfs_node *kn;
	struct lock_class_key *key = NULL;
	int ret;

#ifdef CONFIG_DEBUG_LOCK_ALLOC
	key = &cft->lockdep_key;
#endif
	kn = __kernfs_create_file(cgrp->kn, cgroup_file_name(cgrp, cft, name),
				  cgroup_file_mode(cft), 0, cft->kf_ops, cft,
				  NULL, false, key);
	if (IS_ERR(kn))
		return PTR_ERR(kn);

	ret = cgroup_kn_set_ugid(kn);
	if (ret) {
		kernfs_remove(kn);
		return ret;
	}

	if (cft->seq_show == cgroup_populated_show)
		cgrp->populated_kn = kn;
	return 0;
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

	lockdep_assert_held(&cgroup_mutex);

	for (cft = cfts; cft->name[0] != '\0'; cft++) {
		/* does cft->flags tell us to skip this file on @cgrp? */
		if ((cft->flags & CFTYPE_ONLY_ON_DFL) && !cgroup_on_dfl(cgrp))
			continue;
		if ((cft->flags & CFTYPE_INSANE) && cgroup_sane_behavior(cgrp))
			continue;
		if ((cft->flags & CFTYPE_NOT_ON_ROOT) && !cgroup_parent(cgrp))
			continue;
		if ((cft->flags & CFTYPE_ONLY_ON_ROOT) && cgroup_parent(cgrp))
			continue;

		if (is_add) {
			ret = cgroup_add_file(cgrp, cft);
			if (ret) {
				pr_warn("%s: failed to add %s, err=%d\n",
					__func__, cft->name, ret);
				return ret;
			}
		} else {
			cgroup_rm_file(cgrp, cft);
		}
	}
	return 0;
}

static int cgroup_apply_cftypes(struct cftype *cfts, bool is_add)
{
	LIST_HEAD(pending);
	struct cgroup_subsys *ss = cfts[0].ss;
	struct cgroup *root = &ss->root->cgrp;
	struct cgroup_subsys_state *css;
	int ret = 0;

	lockdep_assert_held(&cgroup_mutex);

	/* add/rm files for all cgroups created before */
	css_for_each_descendant_pre(css, cgroup_css(root, ss)) {
		struct cgroup *cgrp = css->cgroup;

		if (cgroup_is_dead(cgrp))
			continue;

		ret = cgroup_addrm_files(cgrp, cfts, is_add);
		if (ret)
			break;
	}

	if (is_add && !ret)
		kernfs_activate(root->kn);
	return ret;
}

static void cgroup_exit_cftypes(struct cftype *cfts)
{
	struct cftype *cft;

	for (cft = cfts; cft->name[0] != '\0'; cft++) {
		/* free copy for custom atomic_write_len, see init_cftypes() */
		if (cft->max_write_len && cft->max_write_len != PAGE_SIZE)
			kfree(cft->kf_ops);
		cft->kf_ops = NULL;
		cft->ss = NULL;
	}
}

static int cgroup_init_cftypes(struct cgroup_subsys *ss, struct cftype *cfts)
{
	struct cftype *cft;

	for (cft = cfts; cft->name[0] != '\0'; cft++) {
		struct kernfs_ops *kf_ops;

		WARN_ON(cft->ss || cft->kf_ops);

		if (cft->seq_start)
			kf_ops = &cgroup_kf_ops;
		else
			kf_ops = &cgroup_kf_single_ops;

		/*
		 * Ugh... if @cft wants a custom max_write_len, we need to
		 * make a copy of kf_ops to set its atomic_write_len.
		 */
		if (cft->max_write_len && cft->max_write_len != PAGE_SIZE) {
			kf_ops = kmemdup(kf_ops, sizeof(*kf_ops), GFP_KERNEL);
			if (!kf_ops) {
				cgroup_exit_cftypes(cfts);
				return -ENOMEM;
			}
			kf_ops->atomic_write_len = cft->max_write_len;
		}

		cft->kf_ops = kf_ops;
		cft->ss = ss;
	}

	return 0;
}

static int cgroup_rm_cftypes_locked(struct cftype *cfts)
{
	lockdep_assert_held(&cgroup_mutex);

	if (!cfts || !cfts[0].ss)
		return -ENOENT;

	list_del(&cfts->node);
	cgroup_apply_cftypes(cfts, false);
	cgroup_exit_cftypes(cfts);
	return 0;
}

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
	int ret;

	mutex_lock(&cgroup_mutex);
	ret = cgroup_rm_cftypes_locked(cfts);
	mutex_unlock(&cgroup_mutex);
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
	int ret;

	if (ss->disabled)
		return 0;

	if (!cfts || cfts[0].name[0] == '\0')
		return 0;

	ret = cgroup_init_cftypes(ss, cfts);
	if (ret)
		return ret;

	mutex_lock(&cgroup_mutex);

	list_add_tail(&cfts->node, &ss->cfts);
	ret = cgroup_apply_cftypes(cfts, true);
	if (ret)
		cgroup_rm_cftypes_locked(cfts);

	mutex_unlock(&cgroup_mutex);
	return ret;
}

/**
 * cgroup_task_count - count the number of tasks in a cgroup.
 * @cgrp: the cgroup in question
 *
 * Return the number of tasks in the cgroup.
 */
static int cgroup_task_count(const struct cgroup *cgrp)
{
	int count = 0;
	struct cgrp_cset_link *link;

	down_read(&css_set_rwsem);
	list_for_each_entry(link, &cgrp->cset_links, cset_link)
		count += atomic_read(&link->cset->refcount);
	up_read(&css_set_rwsem);
	return count;
}

/**
 * css_next_child - find the next child of a given css
 * @pos: the current position (%NULL to initiate traversal)
 * @parent: css whose children to walk
 *
 * This function returns the next child of @parent and should be called
 * under either cgroup_mutex or RCU read lock.  The only requirement is
 * that @parent and @pos are accessible.  The next sibling is guaranteed to
 * be returned regardless of their states.
 *
 * If a subsystem synchronizes ->css_online() and the start of iteration, a
 * css which finished ->css_online() is guaranteed to be visible in the
 * future iterations and will stay visible until the last reference is put.
 * A css which hasn't finished ->css_online() or already finished
 * ->css_offline() may show up during traversal.  It's each subsystem's
 * responsibility to synchronize against on/offlining.
 */
struct cgroup_subsys_state *css_next_child(struct cgroup_subsys_state *pos,
					   struct cgroup_subsys_state *parent)
{
	struct cgroup_subsys_state *next;

	cgroup_assert_mutex_or_rcu_locked();

	/*
	 * @pos could already have been unlinked from the sibling list.
	 * Once a cgroup is removed, its ->sibling.next is no longer
	 * updated when its next sibling changes.  CSS_RELEASED is set when
	 * @pos is taken off list, at which time its next pointer is valid,
	 * and, as releases are serialized, the one pointed to by the next
	 * pointer is guaranteed to not have started release yet.  This
	 * implies that if we observe !CSS_RELEASED on @pos in this RCU
	 * critical section, the one pointed to by its next pointer is
	 * guaranteed to not have finished its RCU grace period even if we
	 * have dropped rcu_read_lock() inbetween iterations.
	 *
	 * If @pos has CSS_RELEASED set, its next pointer can't be
	 * dereferenced; however, as each css is given a monotonically
	 * increasing unique serial number and always appended to the
	 * sibling list, the next one can be found by walking the parent's
	 * children until the first css with higher serial number than
	 * @pos's.  While this path can be slower, it happens iff iteration
	 * races against release and the race window is very small.
	 */
	if (!pos) {
		next = list_entry_rcu(parent->children.next, struct cgroup_subsys_state, sibling);
	} else if (likely(!(pos->flags & CSS_RELEASED))) {
		next = list_entry_rcu(pos->sibling.next, struct cgroup_subsys_state, sibling);
	} else {
		list_for_each_entry_rcu(next, &parent->children, sibling)
			if (next->serial_nr > pos->serial_nr)
				break;
	}

	/*
	 * @next, if not pointing to the head, can be dereferenced and is
	 * the next sibling.
	 */
	if (&next->sibling != &parent->children)
		return next;
	return NULL;
}

/**
 * css_next_descendant_pre - find the next descendant for pre-order walk
 * @pos: the current position (%NULL to initiate traversal)
 * @root: css whose descendants to walk
 *
 * To be used by css_for_each_descendant_pre().  Find the next descendant
 * to visit for pre-order traversal of @root's descendants.  @root is
 * included in the iteration and the first node to be visited.
 *
 * While this function requires cgroup_mutex or RCU read locking, it
 * doesn't require the whole traversal to be contained in a single critical
 * section.  This function will return the correct next descendant as long
 * as both @pos and @root are accessible and @pos is a descendant of @root.
 *
 * If a subsystem synchronizes ->css_online() and the start of iteration, a
 * css which finished ->css_online() is guaranteed to be visible in the
 * future iterations and will stay visible until the last reference is put.
 * A css which hasn't finished ->css_online() or already finished
 * ->css_offline() may show up during traversal.  It's each subsystem's
 * responsibility to synchronize against on/offlining.
 */
struct cgroup_subsys_state *
css_next_descendant_pre(struct cgroup_subsys_state *pos,
			struct cgroup_subsys_state *root)
{
	struct cgroup_subsys_state *next;

	cgroup_assert_mutex_or_rcu_locked();

	/* if first iteration, visit @root */
	if (!pos)
		return root;

	/* visit the first child if exists */
	next = css_next_child(NULL, pos);
	if (next)
		return next;

	/* no child, visit my or the closest ancestor's next sibling */
	while (pos != root) {
		next = css_next_child(pos, pos->parent);
		if (next)
			return next;
		pos = pos->parent;
	}

	return NULL;
}

/**
 * css_rightmost_descendant - return the rightmost descendant of a css
 * @pos: css of interest
 *
 * Return the rightmost descendant of @pos.  If there's no descendant, @pos
 * is returned.  This can be used during pre-order traversal to skip
 * subtree of @pos.
 *
 * While this function requires cgroup_mutex or RCU read locking, it
 * doesn't require the whole traversal to be contained in a single critical
 * section.  This function will return the correct rightmost descendant as
 * long as @pos is accessible.
 */
struct cgroup_subsys_state *
css_rightmost_descendant(struct cgroup_subsys_state *pos)
{
	struct cgroup_subsys_state *last, *tmp;

	cgroup_assert_mutex_or_rcu_locked();

	do {
		last = pos;
		/* ->prev isn't RCU safe, walk ->next till the end */
		pos = NULL;
		css_for_each_child(tmp, last)
			pos = tmp;
	} while (pos);

	return last;
}

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
 * While this function requires cgroup_mutex or RCU read locking, it
 * doesn't require the whole traversal to be contained in a single critical
 * section.  This function will return the correct next descendant as long
 * as both @pos and @cgroup are accessible and @pos is a descendant of
 * @cgroup.
 *
 * If a subsystem synchronizes ->css_online() and the start of iteration, a
 * css which finished ->css_online() is guaranteed to be visible in the
 * future iterations and will stay visible until the last reference is put.
 * A css which hasn't finished ->css_online() or already finished
 * ->css_offline() may show up during traversal.  It's each subsystem's
 * responsibility to synchronize against on/offlining.
 */
struct cgroup_subsys_state *
css_next_descendant_post(struct cgroup_subsys_state *pos,
			 struct cgroup_subsys_state *root)
{
	struct cgroup_subsys_state *next;

	cgroup_assert_mutex_or_rcu_locked();

	/* if first iteration, visit leftmost descendant which may be @root */
	if (!pos)
		return css_leftmost_descendant(root);

	/* if we visited @root, we're done */
	if (pos == root)
		return NULL;

	/* if there's an unvisited sibling, visit its leftmost descendant */
	next = css_next_child(pos, pos->parent);
	if (next)
		return css_leftmost_descendant(next);

	/* no sibling left, visit parent */
	return pos->parent;
}

/**
 * css_has_online_children - does a css have online children
 * @css: the target css
 *
 * Returns %true if @css has any online children; otherwise, %false.  This
 * function can be called from any context but the caller is responsible
 * for synchronizing against on/offlining as necessary.
 */
bool css_has_online_children(struct cgroup_subsys_state *css)
{
	struct cgroup_subsys_state *child;
	bool ret = false;

	rcu_read_lock();
	css_for_each_child(child, css) {
		if (css->flags & CSS_ONLINE) {
			ret = true;
			break;
		}
	}
	rcu_read_unlock();
	return ret;
}

/**
 * css_advance_task_iter - advance a task itererator to the next css_set
 * @it: the iterator to advance
 *
 * Advance @it to the next css_set to walk.
 */
static void css_advance_task_iter(struct css_task_iter *it)
{
	struct list_head *l = it->cset_pos;
	struct cgrp_cset_link *link;
	struct css_set *cset;

	/* Advance to the next non-empty css_set */
	do {
		l = l->next;
		if (l == it->cset_head) {
			it->cset_pos = NULL;
			return;
		}

		if (it->ss) {
			cset = container_of(l, struct css_set,
					    e_cset_node[it->ss->id]);
		} else {
			link = list_entry(l, struct cgrp_cset_link, cset_link);
			cset = link->cset;
		}
	} while (list_empty(&cset->tasks) && list_empty(&cset->mg_tasks));

	it->cset_pos = l;

	if (!list_empty(&cset->tasks))
		it->task_pos = cset->tasks.next;
	else
		it->task_pos = cset->mg_tasks.next;

	it->tasks_head = &cset->tasks;
	it->mg_tasks_head = &cset->mg_tasks;
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
	__acquires(css_set_rwsem)
{
	/* no one should try to iterate before mounting cgroups */
	WARN_ON_ONCE(!use_task_css_set_links);

	down_read(&css_set_rwsem);

	it->ss = css->ss;

	if (it->ss)
		it->cset_pos = &css->cgroup->e_csets[css->ss->id];
	else
		it->cset_pos = &css->cgroup->cset_links;

	it->cset_head = it->cset_pos;

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
	struct list_head *l = it->task_pos;

	/* If the iterator cg is NULL, we have no tasks */
	if (!it->cset_pos)
		return NULL;
	res = list_entry(l, struct task_struct, cg_list);

	/*
	 * Advance iterator to find next entry.  cset->tasks is consumed
	 * first and then ->mg_tasks.  After ->mg_tasks, we move onto the
	 * next cset.
	 */
	l = l->next;

	if (l == it->tasks_head)
		l = it->mg_tasks_head->next;

	if (l == it->mg_tasks_head)
		css_advance_task_iter(it);
	else
		it->task_pos = l;

	return res;
}

/**
 * css_task_iter_end - finish task iteration
 * @it: the task iterator to finish
 *
 * Finish task iteration started by css_task_iter_start().
 */
void css_task_iter_end(struct css_task_iter *it)
	__releases(css_set_rwsem)
{
	up_read(&css_set_rwsem);
}

/**
 * cgroup_trasnsfer_tasks - move tasks from one cgroup to another
 * @to: cgroup to which the tasks will be moved
 * @from: cgroup in which the tasks currently reside
 *
 * Locking rules between cgroup_post_fork() and the migration path
 * guarantee that, if a task is forking while being migrated, the new child
 * is guaranteed to be either visible in the source cgroup after the
 * parent's migration is complete or put into the target cgroup.  No task
 * can slip out of migration through forking.
 */
int cgroup_transfer_tasks(struct cgroup *to, struct cgroup *from)
{
	LIST_HEAD(preloaded_csets);
	struct cgrp_cset_link *link;
	struct css_task_iter it;
	struct task_struct *task;
	int ret;

	mutex_lock(&cgroup_mutex);

	/* all tasks in @from are being moved, all csets are source */
	down_read(&css_set_rwsem);
	list_for_each_entry(link, &from->cset_links, cset_link)
		cgroup_migrate_add_src(link->cset, to, &preloaded_csets);
	up_read(&css_set_rwsem);

	ret = cgroup_migrate_prepare_dst(to, &preloaded_csets);
	if (ret)
		goto out_err;

	/*
	 * Migrate tasks one-by-one until @form is empty.  This fails iff
	 * ->can_attach() fails.
	 */
	do {
		css_task_iter_start(&from->self, &it);
		task = css_task_iter_next(&it);
		if (task)
			get_task_struct(task);
		css_task_iter_end(&it);

		if (task) {
			ret = cgroup_migrate(to, task, false);
			put_task_struct(task);
		}
	} while (task && !ret);
out_err:
	cgroup_migrate_finish(&preloaded_csets);
	mutex_unlock(&cgroup_mutex);
	return ret;
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
	/* each of these stored in a list by its cgroup */
	struct list_head links;
	/* pointer to the cgroup we belong to, for list removal purposes */
	struct cgroup *owner;
	/* for delayed destruction */
	struct delayed_work destroy_dwork;
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
 * Used to destroy all pidlists lingering waiting for destroy timer.  None
 * should be left afterwards.
 */
static void cgroup_pidlist_destroy_all(struct cgroup *cgrp)
{
	struct cgroup_pidlist *l, *tmp_l;

	mutex_lock(&cgrp->pidlist_mutex);
	list_for_each_entry_safe(l, tmp_l, &cgrp->pidlists, links)
		mod_delayed_work(cgroup_pidlist_destroy_wq, &l->destroy_dwork, 0);
	mutex_unlock(&cgrp->pidlist_mutex);

	flush_workqueue(cgroup_pidlist_destroy_wq);
	BUG_ON(!list_empty(&cgrp->pidlists));
}

static void cgroup_pidlist_destroy_work_fn(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct cgroup_pidlist *l = container_of(dwork, struct cgroup_pidlist,
						destroy_dwork);
	struct cgroup_pidlist *tofree = NULL;

	mutex_lock(&l->owner->pidlist_mutex);

	/*
	 * Destroy iff we didn't get queued again.  The state won't change
	 * as destroy_dwork can only be queued while locked.
	 */
	if (!delayed_work_pending(dwork)) {
		list_del(&l->links);
		pidlist_free(l->list);
		put_pid_ns(l->key.ns);
		tofree = l;
	}

	mutex_unlock(&l->owner->pidlist_mutex);
	kfree(tofree);
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

/*
 * The two pid files - task and cgroup.procs - guaranteed that the result
 * is sorted, which forced this whole pidlist fiasco.  As pid order is
 * different per namespace, each namespace needs differently sorted list,
 * making it impossible to use, for example, single rbtree of member tasks
 * sorted by task pointer.  As pidlists can be fairly large, allocating one
 * per open file is dangerous, so cgroup had to implement shared pool of
 * pidlists keyed by cgroup and namespace.
 *
 * All this extra complexity was caused by the original implementation
 * committing to an entirely unnecessary property.  In the long term, we
 * want to do away with it.  Explicitly scramble sort order if
 * sane_behavior so that no such expectation exists in the new interface.
 *
 * Scrambling is done by swapping every two consecutive bits, which is
 * non-identity one-to-one mapping which disturbs sort order sufficiently.
 */
static pid_t pid_fry(pid_t pid)
{
	unsigned a = pid & 0x55555555;
	unsigned b = pid & 0xAAAAAAAA;

	return (a << 1) | (b >> 1);
}

static pid_t cgroup_pid_fry(struct cgroup *cgrp, pid_t pid)
{
	if (cgroup_sane_behavior(cgrp))
		return pid_fry(pid);
	else
		return pid;
}

static int cmppid(const void *a, const void *b)
{
	return *(pid_t *)a - *(pid_t *)b;
}

static int fried_cmppid(const void *a, const void *b)
{
	return pid_fry(*(pid_t *)a) - pid_fry(*(pid_t *)b);
}

static struct cgroup_pidlist *cgroup_pidlist_find(struct cgroup *cgrp,
						  enum cgroup_filetype type)
{
	struct cgroup_pidlist *l;
	/* don't need task_nsproxy() if we're looking at ourself */
	struct pid_namespace *ns = task_active_pid_ns(current);

	lockdep_assert_held(&cgrp->pidlist_mutex);

	list_for_each_entry(l, &cgrp->pidlists, links)
		if (l->key.type == type && l->key.ns == ns)
			return l;
	return NULL;
}

/*
 * find the appropriate pidlist for our purpose (given procs vs tasks)
 * returns with the lock on that pidlist already held, and takes care
 * of the use count, or returns NULL with no locks held if we're out of
 * memory.
 */
static struct cgroup_pidlist *cgroup_pidlist_find_create(struct cgroup *cgrp,
						enum cgroup_filetype type)
{
	struct cgroup_pidlist *l;

	lockdep_assert_held(&cgrp->pidlist_mutex);

	l = cgroup_pidlist_find(cgrp, type);
	if (l)
		return l;

	/* entry not found; create a new one */
	l = kzalloc(sizeof(struct cgroup_pidlist), GFP_KERNEL);
	if (!l)
		return l;

	INIT_DELAYED_WORK(&l->destroy_dwork, cgroup_pidlist_destroy_work_fn);
	l->key.type = type;
	/* don't need task_nsproxy() if we're looking at ourself */
	l->key.ns = get_pid_ns(task_active_pid_ns(current));
	l->owner = cgrp;
	list_add(&l->links, &cgrp->pidlists);
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

	lockdep_assert_held(&cgrp->pidlist_mutex);

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
	css_task_iter_start(&cgrp->self, &it);
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
	if (cgroup_sane_behavior(cgrp))
		sort(array, length, sizeof(pid_t), fried_cmppid, NULL);
	else
		sort(array, length, sizeof(pid_t), cmppid, NULL);
	if (type == CGROUP_FILE_PROCS)
		length = pidlist_uniq(array, length);

	l = cgroup_pidlist_find_create(cgrp, type);
	if (!l) {
		mutex_unlock(&cgrp->pidlist_mutex);
		pidlist_free(array);
		return -ENOMEM;
	}

	/* store array, freeing old if necessary */
	pidlist_free(l->list);
	l->list = array;
	l->length = length;
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
	struct kernfs_node *kn = kernfs_node_from_dentry(dentry);
	struct cgroup *cgrp;
	struct css_task_iter it;
	struct task_struct *tsk;

	/* it should be kernfs_node belonging to cgroupfs and is a directory */
	if (dentry->d_sb->s_type != &cgroup_fs_type || !kn ||
	    kernfs_type(kn) != KERNFS_DIR)
		return -EINVAL;

	mutex_lock(&cgroup_mutex);

	/*
	 * We aren't being called from kernfs and there's no guarantee on
	 * @kn->priv's validity.  For this and css_tryget_online_from_dir(),
	 * @kn->priv is RCU safe.  Let's do the RCU dancing.
	 */
	rcu_read_lock();
	cgrp = rcu_dereference(kn->priv);
	if (!cgrp || cgroup_is_dead(cgrp)) {
		rcu_read_unlock();
		mutex_unlock(&cgroup_mutex);
		return -ENOENT;
	}
	rcu_read_unlock();

	css_task_iter_start(&cgrp->self, &it);
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

	mutex_unlock(&cgroup_mutex);
	return 0;
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
	struct kernfs_open_file *of = s->private;
	struct cgroup *cgrp = seq_css(s)->cgroup;
	struct cgroup_pidlist *l;
	enum cgroup_filetype type = seq_cft(s)->private;
	int index = 0, pid = *pos;
	int *iter, ret;

	mutex_lock(&cgrp->pidlist_mutex);

	/*
	 * !NULL @of->priv indicates that this isn't the first start()
	 * after open.  If the matching pidlist is around, we can use that.
	 * Look for it.  Note that @of->priv can't be used directly.  It
	 * could already have been destroyed.
	 */
	if (of->priv)
		of->priv = cgroup_pidlist_find(cgrp, type);

	/*
	 * Either this is the first start() after open or the matching
	 * pidlist has been destroyed inbetween.  Create a new one.
	 */
	if (!of->priv) {
		ret = pidlist_array_load(cgrp, type,
					 (struct cgroup_pidlist **)&of->priv);
		if (ret)
			return ERR_PTR(ret);
	}
	l = of->priv;

	if (pid) {
		int end = l->length;

		while (index < end) {
			int mid = (index + end) / 2;
			if (cgroup_pid_fry(cgrp, l->list[mid]) == pid) {
				index = mid;
				break;
			} else if (cgroup_pid_fry(cgrp, l->list[mid]) <= pid)
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
	*pos = cgroup_pid_fry(cgrp, *iter);
	return iter;
}

static void cgroup_pidlist_stop(struct seq_file *s, void *v)
{
	struct kernfs_open_file *of = s->private;
	struct cgroup_pidlist *l = of->priv;

	if (l)
		mod_delayed_work(cgroup_pidlist_destroy_wq, &l->destroy_dwork,
				 CGROUP_PIDLIST_DESTROY_DELAY);
	mutex_unlock(&seq_css(s)->cgroup->pidlist_mutex);
}

static void *cgroup_pidlist_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct kernfs_open_file *of = s->private;
	struct cgroup_pidlist *l = of->priv;
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
		*pos = cgroup_pid_fry(seq_css(s)->cgroup, *p);
		return p;
	}
}

static int cgroup_pidlist_show(struct seq_file *s, void *v)
{
	return seq_printf(s, "%d\n", *(int *)v);
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
		.seq_start = cgroup_pidlist_start,
		.seq_next = cgroup_pidlist_next,
		.seq_stop = cgroup_pidlist_stop,
		.seq_show = cgroup_pidlist_show,
		.private = CGROUP_FILE_PROCS,
		.write = cgroup_procs_write,
		.mode = S_IRUGO | S_IWUSR,
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
		.seq_show = cgroup_sane_behavior_show,
	},
	{
		.name = "cgroup.controllers",
		.flags = CFTYPE_ONLY_ON_DFL | CFTYPE_ONLY_ON_ROOT,
		.seq_show = cgroup_root_controllers_show,
	},
	{
		.name = "cgroup.controllers",
		.flags = CFTYPE_ONLY_ON_DFL | CFTYPE_NOT_ON_ROOT,
		.seq_show = cgroup_controllers_show,
	},
	{
		.name = "cgroup.subtree_control",
		.flags = CFTYPE_ONLY_ON_DFL,
		.seq_show = cgroup_subtree_control_show,
		.write = cgroup_subtree_control_write,
	},
	{
		.name = "cgroup.populated",
		.flags = CFTYPE_ONLY_ON_DFL | CFTYPE_NOT_ON_ROOT,
		.seq_show = cgroup_populated_show,
	},

	/*
	 * Historical crazy stuff.  These don't have "cgroup."  prefix and
	 * don't exist if sane_behavior.  If you're depending on these, be
	 * prepared to be burned.
	 */
	{
		.name = "tasks",
		.flags = CFTYPE_INSANE,		/* use "procs" instead */
		.seq_start = cgroup_pidlist_start,
		.seq_next = cgroup_pidlist_next,
		.seq_stop = cgroup_pidlist_stop,
		.seq_show = cgroup_pidlist_show,
		.private = CGROUP_FILE_TASKS,
		.write = cgroup_tasks_write,
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
		.seq_show = cgroup_release_agent_show,
		.write = cgroup_release_agent_write,
		.max_write_len = PATH_MAX - 1,
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
static int cgroup_populate_dir(struct cgroup *cgrp, unsigned int subsys_mask)
{
	struct cgroup_subsys *ss;
	int i, ret = 0;

	/* process cftsets of each subsystem */
	for_each_subsys(ss, i) {
		struct cftype *cfts;

		if (!(subsys_mask & (1 << i)))
			continue;

		list_for_each_entry(cfts, &ss->cfts, node) {
			ret = cgroup_addrm_files(cgrp, cfts, true);
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
 *    and thus css_tryget_online() is guaranteed to fail, the css can be
 *    offlined by invoking offline_css().  After offlining, the base ref is
 *    put.  Implemented in css_killed_work_fn().
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

	if (css->ss) {
		/* css free path */
		if (css->parent)
			css_put(css->parent);

		css->ss->css_free(css);
		cgroup_put(cgrp);
	} else {
		/* cgroup free path */
		atomic_dec(&cgrp->root->nr_cgrps);
		cgroup_pidlist_destroy_all(cgrp);

		if (cgroup_parent(cgrp)) {
			/*
			 * We get a ref to the parent, and put the ref when
			 * this cgroup is being freed, so it's guaranteed
			 * that the parent won't be destroyed before its
			 * children.
			 */
			cgroup_put(cgroup_parent(cgrp));
			kernfs_put(cgrp->kn);
			kfree(cgrp);
		} else {
			/*
			 * This is root cgroup's refcnt reaching zero,
			 * which indicates that the root should be
			 * released.
			 */
			cgroup_destroy_root(cgrp->root);
		}
	}
}

static void css_free_rcu_fn(struct rcu_head *rcu_head)
{
	struct cgroup_subsys_state *css =
		container_of(rcu_head, struct cgroup_subsys_state, rcu_head);

	INIT_WORK(&css->destroy_work, css_free_work_fn);
	queue_work(cgroup_destroy_wq, &css->destroy_work);
}

static void css_release_work_fn(struct work_struct *work)
{
	struct cgroup_subsys_state *css =
		container_of(work, struct cgroup_subsys_state, destroy_work);
	struct cgroup_subsys *ss = css->ss;
	struct cgroup *cgrp = css->cgroup;

	mutex_lock(&cgroup_mutex);

	css->flags |= CSS_RELEASED;
	list_del_rcu(&css->sibling);

	if (ss) {
		/* css release path */
		cgroup_idr_remove(&ss->css_idr, css->id);
	} else {
		/* cgroup release path */
		cgroup_idr_remove(&cgrp->root->cgroup_idr, cgrp->id);
		cgrp->id = -1;
	}

	mutex_unlock(&cgroup_mutex);

	call_rcu(&css->rcu_head, css_free_rcu_fn);
}

static void css_release(struct percpu_ref *ref)
{
	struct cgroup_subsys_state *css =
		container_of(ref, struct cgroup_subsys_state, refcnt);

	INIT_WORK(&css->destroy_work, css_release_work_fn);
	queue_work(cgroup_destroy_wq, &css->destroy_work);
}

static void init_and_link_css(struct cgroup_subsys_state *css,
			      struct cgroup_subsys *ss, struct cgroup *cgrp)
{
	lockdep_assert_held(&cgroup_mutex);

	cgroup_get(cgrp);

	memset(css, 0, sizeof(*css));
	css->cgroup = cgrp;
	css->ss = ss;
	INIT_LIST_HEAD(&css->sibling);
	INIT_LIST_HEAD(&css->children);
	css->serial_nr = css_serial_nr_next++;

	if (cgroup_parent(cgrp)) {
		css->parent = cgroup_css(cgroup_parent(cgrp), ss);
		css_get(css->parent);
	}

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
		rcu_assign_pointer(css->cgroup->subsys[ss->id], css);
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
	RCU_INIT_POINTER(css->cgroup->subsys[ss->id], NULL);

	wake_up_all(&css->cgroup->offline_waitq);
}

/**
 * create_css - create a cgroup_subsys_state
 * @cgrp: the cgroup new css will be associated with
 * @ss: the subsys of new css
 * @visible: whether to create control knobs for the new css or not
 *
 * Create a new css associated with @cgrp - @ss pair.  On success, the new
 * css is online and installed in @cgrp with all interface files created if
 * @visible.  Returns 0 on success, -errno on failure.
 */
static int create_css(struct cgroup *cgrp, struct cgroup_subsys *ss,
		      bool visible)
{
	struct cgroup *parent = cgroup_parent(cgrp);
	struct cgroup_subsys_state *parent_css = cgroup_css(parent, ss);
	struct cgroup_subsys_state *css;
	int err;

	lockdep_assert_held(&cgroup_mutex);

	css = ss->css_alloc(parent_css);
	if (IS_ERR(css))
		return PTR_ERR(css);

	init_and_link_css(css, ss, cgrp);

	err = percpu_ref_init(&css->refcnt, css_release);
	if (err)
		goto err_free_css;

	err = cgroup_idr_alloc(&ss->css_idr, NULL, 2, 0, GFP_NOWAIT);
	if (err < 0)
		goto err_free_percpu_ref;
	css->id = err;

	if (visible) {
		err = cgroup_populate_dir(cgrp, 1 << ss->id);
		if (err)
			goto err_free_id;
	}

	/* @css is ready to be brought online now, make it visible */
	list_add_tail_rcu(&css->sibling, &parent_css->children);
	cgroup_idr_replace(&ss->css_idr, css, css->id);

	err = online_css(css);
	if (err)
		goto err_list_del;

	if (ss->broken_hierarchy && !ss->warned_broken_hierarchy &&
	    cgroup_parent(parent)) {
		pr_warn("%s (%d) created nested cgroup for controller \"%s\" which has incomplete hierarchy support. Nested cgroups may change behavior in the future.\n",
			current->comm, current->pid, ss->name);
		if (!strcmp(ss->name, "memory"))
			pr_warn("\"memory\" requires setting use_hierarchy to 1 on the root\n");
		ss->warned_broken_hierarchy = true;
	}

	return 0;

err_list_del:
	list_del_rcu(&css->sibling);
	cgroup_clear_dir(css->cgroup, 1 << css->ss->id);
err_free_id:
	cgroup_idr_remove(&ss->css_idr, css->id);
err_free_percpu_ref:
	percpu_ref_cancel_init(&css->refcnt);
err_free_css:
	call_rcu(&css->rcu_head, css_free_rcu_fn);
	return err;
}

static int cgroup_mkdir(struct kernfs_node *parent_kn, const char *name,
			umode_t mode)
{
	struct cgroup *parent, *cgrp;
	struct cgroup_root *root;
	struct cgroup_subsys *ss;
	struct kernfs_node *kn;
	int ssid, ret;

	parent = cgroup_kn_lock_live(parent_kn);
	if (!parent)
		return -ENODEV;
	root = parent->root;

	/* allocate the cgroup and its ID, 0 is reserved for the root */
	cgrp = kzalloc(sizeof(*cgrp), GFP_KERNEL);
	if (!cgrp) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	ret = percpu_ref_init(&cgrp->self.refcnt, css_release);
	if (ret)
		goto out_free_cgrp;

	/*
	 * Temporarily set the pointer to NULL, so idr_find() won't return
	 * a half-baked cgroup.
	 */
	cgrp->id = cgroup_idr_alloc(&root->cgroup_idr, NULL, 2, 0, GFP_NOWAIT);
	if (cgrp->id < 0) {
		ret = -ENOMEM;
		goto out_cancel_ref;
	}

	init_cgroup_housekeeping(cgrp);

	cgrp->self.parent = &parent->self;
	cgrp->root = root;

	if (notify_on_release(parent))
		set_bit(CGRP_NOTIFY_ON_RELEASE, &cgrp->flags);

	if (test_bit(CGRP_CPUSET_CLONE_CHILDREN, &parent->flags))
		set_bit(CGRP_CPUSET_CLONE_CHILDREN, &cgrp->flags);

	/* create the directory */
	kn = kernfs_create_dir(parent->kn, name, mode, cgrp);
	if (IS_ERR(kn)) {
		ret = PTR_ERR(kn);
		goto out_free_id;
	}
	cgrp->kn = kn;

	/*
	 * This extra ref will be put in cgroup_free_fn() and guarantees
	 * that @cgrp->kn is always accessible.
	 */
	kernfs_get(kn);

	cgrp->self.serial_nr = css_serial_nr_next++;

	/* allocation complete, commit to creation */
	list_add_tail_rcu(&cgrp->self.sibling, &cgroup_parent(cgrp)->self.children);
	atomic_inc(&root->nr_cgrps);
	cgroup_get(parent);

	/*
	 * @cgrp is now fully operational.  If something fails after this
	 * point, it'll be released via the normal destruction path.
	 */
	cgroup_idr_replace(&root->cgroup_idr, cgrp, cgrp->id);

	ret = cgroup_kn_set_ugid(kn);
	if (ret)
		goto out_destroy;

	ret = cgroup_addrm_files(cgrp, cgroup_base_files, true);
	if (ret)
		goto out_destroy;

	/* let's create and online css's */
	for_each_subsys(ss, ssid) {
		if (parent->child_subsys_mask & (1 << ssid)) {
			ret = create_css(cgrp, ss,
					 parent->subtree_control & (1 << ssid));
			if (ret)
				goto out_destroy;
		}
	}

	/*
	 * On the default hierarchy, a child doesn't automatically inherit
	 * subtree_control from the parent.  Each is configured manually.
	 */
	if (!cgroup_on_dfl(cgrp)) {
		cgrp->subtree_control = parent->subtree_control;
		cgroup_refresh_child_subsys_mask(cgrp);
	}

	kernfs_activate(kn);

	ret = 0;
	goto out_unlock;

out_free_id:
	cgroup_idr_remove(&root->cgroup_idr, cgrp->id);
out_cancel_ref:
	percpu_ref_cancel_init(&cgrp->self.refcnt);
out_free_cgrp:
	kfree(cgrp);
out_unlock:
	cgroup_kn_unlock(parent_kn);
	return ret;

out_destroy:
	cgroup_destroy_locked(cgrp);
	goto out_unlock;
}

/*
 * This is called when the refcnt of a css is confirmed to be killed.
 * css_tryget_online() is now guaranteed to fail.  Tell the subsystem to
 * initate destruction and put the css ref from kill_css().
 */
static void css_killed_work_fn(struct work_struct *work)
{
	struct cgroup_subsys_state *css =
		container_of(work, struct cgroup_subsys_state, destroy_work);

	mutex_lock(&cgroup_mutex);
	offline_css(css);
	mutex_unlock(&cgroup_mutex);

	css_put(css);
}

/* css kill confirmation processing requires process context, bounce */
static void css_killed_ref_fn(struct percpu_ref *ref)
{
	struct cgroup_subsys_state *css =
		container_of(ref, struct cgroup_subsys_state, refcnt);

	INIT_WORK(&css->destroy_work, css_killed_work_fn);
	queue_work(cgroup_destroy_wq, &css->destroy_work);
}

/**
 * kill_css - destroy a css
 * @css: css to destroy
 *
 * This function initiates destruction of @css by removing cgroup interface
 * files and putting its base reference.  ->css_offline() will be invoked
 * asynchronously once css_tryget_online() is guaranteed to fail and when
 * the reference count reaches zero, @css will be released.
 */
static void kill_css(struct cgroup_subsys_state *css)
{
	lockdep_assert_held(&cgroup_mutex);

	/*
	 * This must happen before css is disassociated with its cgroup.
	 * See seq_css() for details.
	 */
	cgroup_clear_dir(css->cgroup, 1 << css->ss->id);

	/*
	 * Killing would put the base ref, but we need to keep it alive
	 * until after ->css_offline().
	 */
	css_get(css);

	/*
	 * cgroup core guarantees that, by the time ->css_offline() is
	 * invoked, no new css reference will be given out via
	 * css_tryget_online().  We can't simply call percpu_ref_kill() and
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
 * guarantee that css_tryget_online() won't succeed by the time
 * ->css_offline() is invoked.  To satisfy all the requirements,
 * destruction is implemented in the following two steps.
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
	struct cgroup_subsys_state *css;
	bool empty;
	int ssid;

	lockdep_assert_held(&cgroup_mutex);

	/*
	 * css_set_rwsem synchronizes access to ->cset_links and prevents
	 * @cgrp from being removed while put_css_set() is in progress.
	 */
	down_read(&css_set_rwsem);
	empty = list_empty(&cgrp->cset_links);
	up_read(&css_set_rwsem);
	if (!empty)
		return -EBUSY;

	/*
	 * Make sure there's no live children.  We can't test emptiness of
	 * ->self.children as dead children linger on it while being
	 * drained; otherwise, "rmdir parent/child parent" may fail.
	 */
	if (css_has_online_children(&cgrp->self))
		return -EBUSY;

	/*
	 * Mark @cgrp dead.  This prevents further task migration and child
	 * creation by disabling cgroup_lock_live_group().
	 */
	cgrp->self.flags &= ~CSS_ONLINE;

	/* initiate massacre of all css's */
	for_each_css(css, ssid, cgrp)
		kill_css(css);

	/* CSS_ONLINE is clear, remove from ->release_list for the last time */
	raw_spin_lock(&release_list_lock);
	if (!list_empty(&cgrp->release_list))
		list_del_init(&cgrp->release_list);
	raw_spin_unlock(&release_list_lock);

	/*
	 * Remove @cgrp directory along with the base files.  @cgrp has an
	 * extra ref on its kn.
	 */
	kernfs_remove(cgrp->kn);

	set_bit(CGRP_RELEASABLE, &cgroup_parent(cgrp)->flags);
	check_for_release(cgroup_parent(cgrp));

	/* put the base reference */
	percpu_ref_kill(&cgrp->self.refcnt);

	return 0;
};

static int cgroup_rmdir(struct kernfs_node *kn)
{
	struct cgroup *cgrp;
	int ret = 0;

	cgrp = cgroup_kn_lock_live(kn);
	if (!cgrp)
		return 0;
	cgroup_get(cgrp);	/* for @kn->priv clearing */

	ret = cgroup_destroy_locked(cgrp);

	cgroup_kn_unlock(kn);

	/*
	 * There are two control paths which try to determine cgroup from
	 * dentry without going through kernfs - cgroupstats_build() and
	 * css_tryget_online_from_dir().  Those are supported by RCU
	 * protecting clearing of cgrp->kn->priv backpointer, which should
	 * happen after all files under it have been removed.
	 */
	if (!ret)
		RCU_INIT_POINTER(*(void __rcu __force **)&kn->priv, NULL);

	cgroup_put(cgrp);
	return ret;
}

static struct kernfs_syscall_ops cgroup_kf_syscall_ops = {
	.remount_fs		= cgroup_remount,
	.show_options		= cgroup_show_options,
	.mkdir			= cgroup_mkdir,
	.rmdir			= cgroup_rmdir,
	.rename			= cgroup_rename,
};

static void __init cgroup_init_subsys(struct cgroup_subsys *ss, bool early)
{
	struct cgroup_subsys_state *css;

	printk(KERN_INFO "Initializing cgroup subsys %s\n", ss->name);

	mutex_lock(&cgroup_mutex);

	idr_init(&ss->css_idr);
	INIT_LIST_HEAD(&ss->cfts);

	/* Create the root cgroup state for this subsystem */
	ss->root = &cgrp_dfl_root;
	css = ss->css_alloc(cgroup_css(&cgrp_dfl_root.cgrp, ss));
	/* We don't handle early failures gracefully */
	BUG_ON(IS_ERR(css));
	init_and_link_css(css, ss, &cgrp_dfl_root.cgrp);

	/*
	 * Root csses are never destroyed and we can't initialize
	 * percpu_ref during early init.  Disable refcnting.
	 */
	css->flags |= CSS_NO_REF;

	if (early) {
		/* allocation can't be done safely during early init */
		css->id = 1;
	} else {
		css->id = cgroup_idr_alloc(&ss->css_idr, css, 1, 2, GFP_KERNEL);
		BUG_ON(css->id < 0);
	}

	/* Update the init_css_set to contain a subsys
	 * pointer to this state - since the subsystem is
	 * newly registered, all tasks and hence the
	 * init_css_set is in the subsystem's root cgroup. */
	init_css_set.subsys[ss->id] = css;

	need_forkexit_callback |= ss->fork || ss->exit;

	/* At system boot, before all subsystems have been
	 * registered, no tasks have been forked, so we don't
	 * need to invoke fork callbacks here. */
	BUG_ON(!list_empty(&init_task.tasks));

	BUG_ON(online_css(css));

	mutex_unlock(&cgroup_mutex);
}

/**
 * cgroup_init_early - cgroup initialization at system boot
 *
 * Initialize cgroups at system boot, and initialize any
 * subsystems that request early init.
 */
int __init cgroup_init_early(void)
{
	static struct cgroup_sb_opts __initdata opts =
		{ .flags = CGRP_ROOT_SANE_BEHAVIOR };
	struct cgroup_subsys *ss;
	int i;

	init_cgroup_root(&cgrp_dfl_root, &opts);
	cgrp_dfl_root.cgrp.self.flags |= CSS_NO_REF;

	RCU_INIT_POINTER(init_task.cgroups, &init_css_set);

	for_each_subsys(ss, i) {
		WARN(!ss->css_alloc || !ss->css_free || ss->name || ss->id,
		     "invalid cgroup_subsys %d:%s css_alloc=%p css_free=%p name:id=%d:%s\n",
		     i, cgroup_subsys_name[i], ss->css_alloc, ss->css_free,
		     ss->id, ss->name);
		WARN(strlen(cgroup_subsys_name[i]) > MAX_CGROUP_TYPE_NAMELEN,
		     "cgroup_subsys_name %s too long\n", cgroup_subsys_name[i]);

		ss->id = i;
		ss->name = cgroup_subsys_name[i];

		if (ss->early_init)
			cgroup_init_subsys(ss, true);
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
	int ssid, err;

	BUG_ON(cgroup_init_cftypes(NULL, cgroup_base_files));

	mutex_lock(&cgroup_mutex);

	/* Add init_css_set to the hash table */
	key = css_set_hash(init_css_set.subsys);
	hash_add(css_set_table, &init_css_set.hlist, key);

	BUG_ON(cgroup_setup_root(&cgrp_dfl_root, 0));

	mutex_unlock(&cgroup_mutex);

	for_each_subsys(ss, ssid) {
		if (ss->early_init) {
			struct cgroup_subsys_state *css =
				init_css_set.subsys[ss->id];

			css->id = cgroup_idr_alloc(&ss->css_idr, css, 1, 2,
						   GFP_KERNEL);
			BUG_ON(css->id < 0);
		} else {
			cgroup_init_subsys(ss, false);
		}

		list_add_tail(&init_css_set.e_cset_node[ssid],
			      &cgrp_dfl_root.cgrp.e_csets[ssid]);

		/*
		 * Setting dfl_root subsys_mask needs to consider the
		 * disabled flag and cftype registration needs kmalloc,
		 * both of which aren't available during early_init.
		 */
		if (!ss->disabled) {
			cgrp_dfl_root.subsys_mask |= 1 << ss->id;
			WARN_ON(cgroup_add_cftypes(ss, ss->base_cftypes));
		}
	}

	cgroup_kobj = kobject_create_and_add("cgroup", fs_kobj);
	if (!cgroup_kobj)
		return -ENOMEM;

	err = register_filesystem(&cgroup_fs_type);
	if (err < 0) {
		kobject_put(cgroup_kobj);
		return err;
	}

	proc_create("cgroups", 0, NULL, &proc_cgroupstats_operations);
	return 0;
}

static int __init cgroup_wq_init(void)
{
	/*
	 * There isn't much point in executing destruction path in
	 * parallel.  Good chunk is serialized with cgroup_mutex anyway.
	 * Use 1 for @max_active.
	 *
	 * We would prefer to do this in cgroup_init() above, but that
	 * is called before init_workqueues(): so leave this until after.
	 */
	cgroup_destroy_wq = alloc_workqueue("cgroup_destroy", 0, 1);
	BUG_ON(!cgroup_destroy_wq);

	/*
	 * Used to destroy pidlists and separate to serve as flush domain.
	 * Cap @max_active to 1 too.
	 */
	cgroup_pidlist_destroy_wq = alloc_workqueue("cgroup_pidlist_destroy",
						    0, 1);
	BUG_ON(!cgroup_pidlist_destroy_wq);

	return 0;
}
core_initcall(cgroup_wq_init);

/*
 * proc_cgroup_show()
 *  - Print task's cgroup paths into seq_file, one line for each hierarchy
 *  - Used for /proc/<pid>/cgroup.
 */

/* TODO: Use a proper seq_file iterator */
int proc_cgroup_show(struct seq_file *m, void *v)
{
	struct pid *pid;
	struct task_struct *tsk;
	char *buf, *path;
	int retval;
	struct cgroup_root *root;

	retval = -ENOMEM;
	buf = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!buf)
		goto out;

	retval = -ESRCH;
	pid = m->private;
	tsk = get_pid_task(pid, PIDTYPE_PID);
	if (!tsk)
		goto out_free;

	retval = 0;

	mutex_lock(&cgroup_mutex);
	down_read(&css_set_rwsem);

	for_each_root(root) {
		struct cgroup_subsys *ss;
		struct cgroup *cgrp;
		int ssid, count = 0;

		if (root == &cgrp_dfl_root && !cgrp_dfl_root_visible)
			continue;

		seq_printf(m, "%d:", root->hierarchy_id);
		for_each_subsys(ss, ssid)
			if (root->subsys_mask & (1 << ssid))
				seq_printf(m, "%s%s", count++ ? "," : "", ss->name);
		if (strlen(root->name))
			seq_printf(m, "%sname=%s", count ? "," : "",
				   root->name);
		seq_putc(m, ':');
		cgrp = task_cgroup_from_root(tsk, root);
		path = cgroup_path(cgrp, buf, PATH_MAX);
		if (!path) {
			retval = -ENAMETOOLONG;
			goto out_unlock;
		}
		seq_puts(m, path);
		seq_putc(m, '\n');
	}

out_unlock:
	up_read(&css_set_rwsem);
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
			   atomic_read(&ss->root->nr_cgrps), !ss->disabled);

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
 * cgroup_fork - initialize cgroup related fields during copy_process()
 * @child: pointer to task_struct of forking parent process.
 *
 * A task is associated with the init_css_set until cgroup_post_fork()
 * attaches it to the parent's css_set.  Empty cg_list indicates that
 * @child isn't holding reference to its css_set.
 */
void cgroup_fork(struct task_struct *child)
{
	RCU_INIT_POINTER(child->cgroups, &init_css_set);
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
	 * This may race against cgroup_enable_task_cg_links().  As that
	 * function sets use_task_css_set_links before grabbing
	 * tasklist_lock and we just went through tasklist_lock to add
	 * @child, it's guaranteed that either we see the set
	 * use_task_css_set_links or cgroup_enable_task_cg_lists() sees
	 * @child during its iteration.
	 *
	 * If we won the race, @child is associated with %current's
	 * css_set.  Grabbing css_set_rwsem guarantees both that the
	 * association is stable, and, on completion of the parent's
	 * migration, @child is visible in the source of migration or
	 * already in the destination cgroup.  This guarantee is necessary
	 * when implementing operations which need to migrate all tasks of
	 * a cgroup to another.
	 *
	 * Note that if we lose to cgroup_enable_task_cg_links(), @child
	 * will remain in init_css_set.  This is safe because all tasks are
	 * in the init_css_set before cg_links is enabled and there's no
	 * operation which transfers all tasks out of init_css_set.
	 */
	if (use_task_css_set_links) {
		struct css_set *cset;

		down_write(&css_set_rwsem);
		cset = task_css_set(current);
		if (list_empty(&child->cg_list)) {
			rcu_assign_pointer(child->cgroups, cset);
			list_add(&child->cg_list, &cset->tasks);
			get_css_set(cset);
		}
		up_write(&css_set_rwsem);
	}

	/*
	 * Call ss->fork().  This must happen after @child is linked on
	 * css_set; otherwise, @child might change state between ->fork()
	 * and addition to css_set.
	 */
	if (need_forkexit_callback) {
		for_each_subsys(ss, i)
			if (ss->fork)
				ss->fork(child);
	}
}

/**
 * cgroup_exit - detach cgroup from exiting task
 * @tsk: pointer to task_struct of exiting process
 *
 * Description: Detach cgroup from @tsk and release it.
 *
 * Note that cgroups marked notify_on_release force every task in
 * them to take the global cgroup_mutex mutex when exiting.
 * This could impact scaling on very large systems.  Be reluctant to
 * use notify_on_release cgroups where very high task exit scaling
 * is required on large systems.
 *
 * We set the exiting tasks cgroup to the root cgroup (top_cgroup).  We
 * call cgroup_exit() while the task is still competent to handle
 * notify_on_release(), then leave the task attached to the root cgroup in
 * each hierarchy for the remainder of its exit.  No need to bother with
 * init_css_set refcnting.  init_css_set never goes away and we can't race
 * with migration path - PF_EXITING is visible to migration path.
 */
void cgroup_exit(struct task_struct *tsk)
{
	struct cgroup_subsys *ss;
	struct css_set *cset;
	bool put_cset = false;
	int i;

	/*
	 * Unlink from @tsk from its css_set.  As migration path can't race
	 * with us, we can check cg_list without grabbing css_set_rwsem.
	 */
	if (!list_empty(&tsk->cg_list)) {
		down_write(&css_set_rwsem);
		list_del_init(&tsk->cg_list);
		up_write(&css_set_rwsem);
		put_cset = true;
	}

	/* Reassign the task to the init_css_set. */
	cset = task_css_set(tsk);
	RCU_INIT_POINTER(tsk->cgroups, &init_css_set);

	if (need_forkexit_callback) {
		/* see cgroup_post_fork() for details */
		for_each_subsys(ss, i) {
			if (ss->exit) {
				struct cgroup_subsys_state *old_css = cset->subsys[i];
				struct cgroup_subsys_state *css = task_css(tsk, i);

				ss->exit(css, old_css, tsk);
			}
		}
	}

	if (put_cset)
		put_css_set(cset, true);
}

static void check_for_release(struct cgroup *cgrp)
{
	if (cgroup_is_releasable(cgrp) && list_empty(&cgrp->cset_links) &&
	    !css_has_online_children(&cgrp->self)) {
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
		char *pathbuf = NULL, *agentbuf = NULL, *path;
		struct cgroup *cgrp = list_entry(release_list.next,
						    struct cgroup,
						    release_list);
		list_del_init(&cgrp->release_list);
		raw_spin_unlock(&release_list_lock);
		pathbuf = kmalloc(PATH_MAX, GFP_KERNEL);
		if (!pathbuf)
			goto continue_free;
		path = cgroup_path(cgrp, pathbuf, PATH_MAX);
		if (!path)
			goto continue_free;
		agentbuf = kstrdup(cgrp->root->release_agent_path, GFP_KERNEL);
		if (!agentbuf)
			goto continue_free;

		i = 0;
		argv[i++] = agentbuf;
		argv[i++] = path;
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

		for_each_subsys(ss, i) {
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
 * css_tryget_online_from_dir - get corresponding css from a cgroup dentry
 * @dentry: directory dentry of interest
 * @ss: subsystem of interest
 *
 * If @dentry is a directory for a cgroup which has @ss enabled on it, try
 * to get the corresponding css and return it.  If such css doesn't exist
 * or can't be pinned, an ERR_PTR value is returned.
 */
struct cgroup_subsys_state *css_tryget_online_from_dir(struct dentry *dentry,
						       struct cgroup_subsys *ss)
{
	struct kernfs_node *kn = kernfs_node_from_dentry(dentry);
	struct cgroup_subsys_state *css = NULL;
	struct cgroup *cgrp;

	/* is @dentry a cgroup dir? */
	if (dentry->d_sb->s_type != &cgroup_fs_type || !kn ||
	    kernfs_type(kn) != KERNFS_DIR)
		return ERR_PTR(-EBADF);

	rcu_read_lock();

	/*
	 * This path doesn't originate from kernfs and @kn could already
	 * have been or be removed at any point.  @kn->priv is RCU
	 * protected for this access.  See cgroup_rmdir() for details.
	 */
	cgrp = rcu_dereference(kn->priv);
	if (cgrp)
		css = cgroup_css(cgrp, ss);

	if (!css || !css_tryget_online(css))
		css = ERR_PTR(-ENOENT);

	rcu_read_unlock();
	return css;
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
	WARN_ON_ONCE(!rcu_read_lock_held());
	return idr_find(&ss->css_idr, id);
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

static int current_css_set_cg_links_read(struct seq_file *seq, void *v)
{
	struct cgrp_cset_link *link;
	struct css_set *cset;
	char *name_buf;

	name_buf = kmalloc(NAME_MAX + 1, GFP_KERNEL);
	if (!name_buf)
		return -ENOMEM;

	down_read(&css_set_rwsem);
	rcu_read_lock();
	cset = rcu_dereference(current->cgroups);
	list_for_each_entry(link, &cset->cgrp_links, cgrp_link) {
		struct cgroup *c = link->cgrp;

		cgroup_name(c, name_buf, NAME_MAX + 1);
		seq_printf(seq, "Root %d group %s\n",
			   c->root->hierarchy_id, name_buf);
	}
	rcu_read_unlock();
	up_read(&css_set_rwsem);
	kfree(name_buf);
	return 0;
}

#define MAX_TASKS_SHOWN_PER_CSS 25
static int cgroup_css_links_read(struct seq_file *seq, void *v)
{
	struct cgroup_subsys_state *css = seq_css(seq);
	struct cgrp_cset_link *link;

	down_read(&css_set_rwsem);
	list_for_each_entry(link, &css->cgroup->cset_links, cset_link) {
		struct css_set *cset = link->cset;
		struct task_struct *task;
		int count = 0;

		seq_printf(seq, "css_set %p\n", cset);

		list_for_each_entry(task, &cset->tasks, cg_list) {
			if (count++ > MAX_TASKS_SHOWN_PER_CSS)
				goto overflow;
			seq_printf(seq, "  task %d\n", task_pid_vnr(task));
		}

		list_for_each_entry(task, &cset->mg_tasks, cg_list) {
			if (count++ > MAX_TASKS_SHOWN_PER_CSS)
				goto overflow;
			seq_printf(seq, "  task %d\n", task_pid_vnr(task));
		}
		continue;
	overflow:
		seq_puts(seq, "  ...\n");
	}
	up_read(&css_set_rwsem);
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
		.seq_show = current_css_set_cg_links_read,
	},

	{
		.name = "cgroup_css_links",
		.seq_show = cgroup_css_links_read,
	},

	{
		.name = "releasable",
		.read_u64 = releasable_read,
	},

	{ }	/* terminate */
};

struct cgroup_subsys debug_cgrp_subsys = {
	.css_alloc = debug_css_alloc,
	.css_free = debug_css_free,
	.base_cftypes = debug_files,
};
#endif /* CONFIG_CGROUP_DEBUG */
