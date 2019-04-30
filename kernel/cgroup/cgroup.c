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

#include "cgroup-internal.h"

#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/init_task.h>
#include <linux/kernel.h>
#include <linux/magic.h>
#include <linux/mutex.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/proc_fs.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/percpu-rwsem.h>
#include <linux/string.h>
#include <linux/hashtable.h>
#include <linux/idr.h>
#include <linux/kthread.h>
#include <linux/atomic.h>
#include <linux/cpuset.h>
#include <linux/proc_ns.h>
#include <linux/nsproxy.h>
#include <linux/file.h>
#include <linux/sched/cputime.h>
#include <linux/psi.h>
#include <net/sock.h>

#define CREATE_TRACE_POINTS
#include <trace/events/cgroup.h>

#define CGROUP_FILE_NAME_MAX		(MAX_CGROUP_TYPE_NAMELEN +	\
					 MAX_CFTYPE_NAME + 2)
/* let's not notify more than 100 times per second */
#define CGROUP_FILE_NOTIFY_MIN_INTV	DIV_ROUND_UP(HZ, 100)

/*
 * cgroup_mutex is the master lock.  Any modification to cgroup or its
 * hierarchy must be performed while holding it.
 *
 * css_set_lock protects task->cgroups pointer, the list of css_set
 * objects, and the chain of tasks off each css_set.
 *
 * These locks are exported if CONFIG_PROVE_RCU so that accessors in
 * cgroup.h can use them for lockdep annotations.
 */
DEFINE_MUTEX(cgroup_mutex);
DEFINE_SPINLOCK(css_set_lock);

#ifdef CONFIG_PROVE_RCU
EXPORT_SYMBOL_GPL(cgroup_mutex);
EXPORT_SYMBOL_GPL(css_set_lock);
#endif

DEFINE_SPINLOCK(trace_cgroup_path_lock);
char trace_cgroup_path[TRACE_CGROUP_PATH_LEN];

/*
 * Protects cgroup_idr and css_idr so that IDs can be released without
 * grabbing cgroup_mutex.
 */
static DEFINE_SPINLOCK(cgroup_idr_lock);

/*
 * Protects cgroup_file->kn for !self csses.  It synchronizes notifications
 * against file removal/re-creation across css hiding.
 */
static DEFINE_SPINLOCK(cgroup_file_kn_lock);

struct percpu_rw_semaphore cgroup_threadgroup_rwsem;

#define cgroup_assert_mutex_or_rcu_locked()				\
	RCU_LOCKDEP_WARN(!rcu_read_lock_held() &&			\
			   !lockdep_is_held(&cgroup_mutex),		\
			   "cgroup_mutex or RCU read lock required");

/*
 * cgroup destruction makes heavy use of work items and there can be a lot
 * of concurrent destructions.  Use a separate workqueue so that cgroup
 * destruction work items don't end up filling up max_active of system_wq
 * which may lead to deadlock.
 */
static struct workqueue_struct *cgroup_destroy_wq;

/* generate an array of cgroup subsystem pointers */
#define SUBSYS(_x) [_x ## _cgrp_id] = &_x ## _cgrp_subsys,
struct cgroup_subsys *cgroup_subsys[] = {
#include <linux/cgroup_subsys.h>
};
#undef SUBSYS

/* array of cgroup subsystem names */
#define SUBSYS(_x) [_x ## _cgrp_id] = #_x,
static const char *cgroup_subsys_name[] = {
#include <linux/cgroup_subsys.h>
};
#undef SUBSYS

/* array of static_keys for cgroup_subsys_enabled() and cgroup_subsys_on_dfl() */
#define SUBSYS(_x)								\
	DEFINE_STATIC_KEY_TRUE(_x ## _cgrp_subsys_enabled_key);			\
	DEFINE_STATIC_KEY_TRUE(_x ## _cgrp_subsys_on_dfl_key);			\
	EXPORT_SYMBOL_GPL(_x ## _cgrp_subsys_enabled_key);			\
	EXPORT_SYMBOL_GPL(_x ## _cgrp_subsys_on_dfl_key);
#include <linux/cgroup_subsys.h>
#undef SUBSYS

#define SUBSYS(_x) [_x ## _cgrp_id] = &_x ## _cgrp_subsys_enabled_key,
static struct static_key_true *cgroup_subsys_enabled_key[] = {
#include <linux/cgroup_subsys.h>
};
#undef SUBSYS

#define SUBSYS(_x) [_x ## _cgrp_id] = &_x ## _cgrp_subsys_on_dfl_key,
static struct static_key_true *cgroup_subsys_on_dfl_key[] = {
#include <linux/cgroup_subsys.h>
};
#undef SUBSYS

static DEFINE_PER_CPU(struct cgroup_rstat_cpu, cgrp_dfl_root_rstat_cpu);

/*
 * The default hierarchy, reserved for the subsystems that are otherwise
 * unattached - it never has more than a single cgroup, and all tasks are
 * part of that cgroup.
 */
struct cgroup_root cgrp_dfl_root = { .cgrp.rstat_cpu = &cgrp_dfl_root_rstat_cpu };
EXPORT_SYMBOL_GPL(cgrp_dfl_root);

/*
 * The default hierarchy always exists but is hidden until mounted for the
 * first time.  This is for backward compatibility.
 */
static bool cgrp_dfl_visible;

/* some controllers are not supported in the default hierarchy */
static u16 cgrp_dfl_inhibit_ss_mask;

/* some controllers are implicitly enabled on the default hierarchy */
static u16 cgrp_dfl_implicit_ss_mask;

/* some controllers can be threaded on the default hierarchy */
static u16 cgrp_dfl_threaded_ss_mask;

/* The list of hierarchy roots */
LIST_HEAD(cgroup_roots);
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

/*
 * These bitmasks identify subsystems with specific features to avoid
 * having to do iterative checks repeatedly.
 */
static u16 have_fork_callback __read_mostly;
static u16 have_exit_callback __read_mostly;
static u16 have_release_callback __read_mostly;
static u16 have_canfork_callback __read_mostly;

/* cgroup namespace for init task */
struct cgroup_namespace init_cgroup_ns = {
	.count		= REFCOUNT_INIT(2),
	.user_ns	= &init_user_ns,
	.ns.ops		= &cgroupns_operations,
	.ns.inum	= PROC_CGROUP_INIT_INO,
	.root_cset	= &init_css_set,
};

static struct file_system_type cgroup2_fs_type;
static struct cftype cgroup_base_files[];

static int cgroup_apply_control(struct cgroup *cgrp);
static void cgroup_finalize_control(struct cgroup *cgrp, int ret);
static void css_task_iter_skip(struct css_task_iter *it,
			       struct task_struct *task);
static int cgroup_destroy_locked(struct cgroup *cgrp);
static struct cgroup_subsys_state *css_create(struct cgroup *cgrp,
					      struct cgroup_subsys *ss);
static void css_release(struct percpu_ref *ref);
static void kill_css(struct cgroup_subsys_state *css);
static int cgroup_addrm_files(struct cgroup_subsys_state *css,
			      struct cgroup *cgrp, struct cftype cfts[],
			      bool is_add);

/**
 * cgroup_ssid_enabled - cgroup subsys enabled test by subsys ID
 * @ssid: subsys ID of interest
 *
 * cgroup_subsys_enabled() can only be used with literal subsys names which
 * is fine for individual subsystems but unsuitable for cgroup core.  This
 * is slower static_key_enabled() based test indexed by @ssid.
 */
bool cgroup_ssid_enabled(int ssid)
{
	if (CGROUP_SUBSYS_COUNT == 0)
		return false;

	return static_key_enabled(cgroup_subsys_enabled_key[ssid]);
}

/**
 * cgroup_on_dfl - test whether a cgroup is on the default hierarchy
 * @cgrp: the cgroup of interest
 *
 * The default hierarchy is the v2 interface of cgroup and this function
 * can be used to test whether a cgroup is on the default hierarchy for
 * cases where a subsystem should behave differnetly depending on the
 * interface version.
 *
 * The set of behaviors which change on the default hierarchy are still
 * being determined and the mount option is prefixed with __DEVEL__.
 *
 * List of changed behaviors:
 *
 * - Mount options "noprefix", "xattr", "clone_children", "release_agent"
 *   and "name" are disallowed.
 *
 * - When mounting an existing superblock, mount options should match.
 *
 * - Remount is disallowed.
 *
 * - rename(2) is disallowed.
 *
 * - "tasks" is removed.  Everything should be at process granularity.  Use
 *   "cgroup.procs" instead.
 *
 * - "cgroup.procs" is not sorted.  pids will be unique unless they got
 *   recycled inbetween reads.
 *
 * - "release_agent" and "notify_on_release" are removed.  Replacement
 *   notification mechanism will be implemented.
 *
 * - "cgroup.clone_children" is removed.
 *
 * - "cgroup.subtree_populated" is available.  Its value is 0 if the cgroup
 *   and its descendants contain no task; otherwise, 1.  The file also
 *   generates kernfs notification which can be monitored through poll and
 *   [di]notify when the value of the file changes.
 *
 * - cpuset: tasks will be kept in empty cpusets when hotplug happens and
 *   take masks of ancestors with non-empty cpus/mems, instead of being
 *   moved to an ancestor.
 *
 * - cpuset: a task can be moved into an empty cpuset, and again it takes
 *   masks of ancestors.
 *
 * - memcg: use_hierarchy is on by default and the cgroup file for the flag
 *   is not created.
 *
 * - blkcg: blk-throttle becomes properly hierarchical.
 *
 * - debug: disallowed on the default hierarchy.
 */
bool cgroup_on_dfl(const struct cgroup *cgrp)
{
	return cgrp->root == &cgrp_dfl_root;
}

/* IDR wrappers which synchronize using cgroup_idr_lock */
static int cgroup_idr_alloc(struct idr *idr, void *ptr, int start, int end,
			    gfp_t gfp_mask)
{
	int ret;

	idr_preload(gfp_mask);
	spin_lock_bh(&cgroup_idr_lock);
	ret = idr_alloc(idr, ptr, start, end, gfp_mask & ~__GFP_DIRECT_RECLAIM);
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

static bool cgroup_has_tasks(struct cgroup *cgrp)
{
	return cgrp->nr_populated_csets;
}

bool cgroup_is_threaded(struct cgroup *cgrp)
{
	return cgrp->dom_cgrp != cgrp;
}

/* can @cgrp host both domain and threaded children? */
static bool cgroup_is_mixable(struct cgroup *cgrp)
{
	/*
	 * Root isn't under domain level resource control exempting it from
	 * the no-internal-process constraint, so it can serve as a thread
	 * root and a parent of resource domains at the same time.
	 */
	return !cgroup_parent(cgrp);
}

/* can @cgrp become a thread root? should always be true for a thread root */
static bool cgroup_can_be_thread_root(struct cgroup *cgrp)
{
	/* mixables don't care */
	if (cgroup_is_mixable(cgrp))
		return true;

	/* domain roots can't be nested under threaded */
	if (cgroup_is_threaded(cgrp))
		return false;

	/* can only have either domain or threaded children */
	if (cgrp->nr_populated_domain_children)
		return false;

	/* and no domain controllers can be enabled */
	if (cgrp->subtree_control & ~cgrp_dfl_threaded_ss_mask)
		return false;

	return true;
}

/* is @cgrp root of a threaded subtree? */
bool cgroup_is_thread_root(struct cgroup *cgrp)
{
	/* thread root should be a domain */
	if (cgroup_is_threaded(cgrp))
		return false;

	/* a domain w/ threaded children is a thread root */
	if (cgrp->nr_threaded_children)
		return true;

	/*
	 * A domain which has tasks and explicit threaded controllers
	 * enabled is a thread root.
	 */
	if (cgroup_has_tasks(cgrp) &&
	    (cgrp->subtree_control & cgrp_dfl_threaded_ss_mask))
		return true;

	return false;
}

/* a domain which isn't connected to the root w/o brekage can't be used */
static bool cgroup_is_valid_domain(struct cgroup *cgrp)
{
	/* the cgroup itself can be a thread root */
	if (cgroup_is_threaded(cgrp))
		return false;

	/* but the ancestors can't be unless mixable */
	while ((cgrp = cgroup_parent(cgrp))) {
		if (!cgroup_is_mixable(cgrp) && cgroup_is_thread_root(cgrp))
			return false;
		if (cgroup_is_threaded(cgrp))
			return false;
	}

	return true;
}

/* subsystems visibly enabled on a cgroup */
static u16 cgroup_control(struct cgroup *cgrp)
{
	struct cgroup *parent = cgroup_parent(cgrp);
	u16 root_ss_mask = cgrp->root->subsys_mask;

	if (parent) {
		u16 ss_mask = parent->subtree_control;

		/* threaded cgroups can only have threaded controllers */
		if (cgroup_is_threaded(cgrp))
			ss_mask &= cgrp_dfl_threaded_ss_mask;
		return ss_mask;
	}

	if (cgroup_on_dfl(cgrp))
		root_ss_mask &= ~(cgrp_dfl_inhibit_ss_mask |
				  cgrp_dfl_implicit_ss_mask);
	return root_ss_mask;
}

/* subsystems enabled on a cgroup */
static u16 cgroup_ss_mask(struct cgroup *cgrp)
{
	struct cgroup *parent = cgroup_parent(cgrp);

	if (parent) {
		u16 ss_mask = parent->subtree_ss_mask;

		/* threaded cgroups can only have threaded controllers */
		if (cgroup_is_threaded(cgrp))
			ss_mask &= cgrp_dfl_threaded_ss_mask;
		return ss_mask;
	}

	return cgrp->root->subsys_mask;
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
 * cgroup_tryget_css - try to get a cgroup's css for the specified subsystem
 * @cgrp: the cgroup of interest
 * @ss: the subsystem of interest
 *
 * Find and get @cgrp's css assocaited with @ss.  If the css doesn't exist
 * or is offline, %NULL is returned.
 */
static struct cgroup_subsys_state *cgroup_tryget_css(struct cgroup *cgrp,
						     struct cgroup_subsys *ss)
{
	struct cgroup_subsys_state *css;

	rcu_read_lock();
	css = cgroup_css(cgrp, ss);
	if (!css || !css_tryget_online(css))
		css = NULL;
	rcu_read_unlock();

	return css;
}

/**
 * cgroup_e_css - obtain a cgroup's effective css for the specified subsystem
 * @cgrp: the cgroup of interest
 * @ss: the subsystem of interest (%NULL returns @cgrp->self)
 *
 * Similar to cgroup_css() but returns the effective css, which is defined
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

	/*
	 * This function is used while updating css associations and thus
	 * can't test the csses directly.  Test ss_mask.
	 */
	while (!(cgroup_ss_mask(cgrp) & (1 << ss->id))) {
		cgrp = cgroup_parent(cgrp);
		if (!cgrp)
			return NULL;
	}

	return cgroup_css(cgrp, ss);
}

/**
 * cgroup_get_e_css - get a cgroup's effective css for the specified subsystem
 * @cgrp: the cgroup of interest
 * @ss: the subsystem of interest
 *
 * Find and get the effective css of @cgrp for @ss.  The effective css is
 * defined as the matching css of the nearest ancestor including self which
 * has @ss enabled.  If @ss is not mounted on the hierarchy @cgrp is on,
 * the root css is returned, so this function always returns a valid css.
 * The returned css must be put using css_put().
 */
struct cgroup_subsys_state *cgroup_get_e_css(struct cgroup *cgrp,
					     struct cgroup_subsys *ss)
{
	struct cgroup_subsys_state *css;

	rcu_read_lock();

	do {
		css = cgroup_css(cgrp, ss);

		if (css && css_tryget_online(css))
			goto out_unlock;
		cgrp = cgroup_parent(cgrp);
	} while (cgrp);

	css = init_css_set.subsys[ss->id];
	css_get(css);
out_unlock:
	rcu_read_unlock();
	return css;
}

static void cgroup_get_live(struct cgroup *cgrp)
{
	WARN_ON_ONCE(cgroup_is_dead(cgrp));
	css_get(&cgrp->self);
}

/**
 * __cgroup_task_count - count the number of tasks in a cgroup. The caller
 * is responsible for taking the css_set_lock.
 * @cgrp: the cgroup in question
 */
int __cgroup_task_count(const struct cgroup *cgrp)
{
	int count = 0;
	struct cgrp_cset_link *link;

	lockdep_assert_held(&css_set_lock);

	list_for_each_entry(link, &cgrp->cset_links, cset_link)
		count += link->cset->nr_tasks;

	return count;
}

/**
 * cgroup_task_count - count the number of tasks in a cgroup.
 * @cgrp: the cgroup in question
 */
int cgroup_task_count(const struct cgroup *cgrp)
{
	int count;

	spin_lock_irq(&css_set_lock);
	count = __cgroup_task_count(cgrp);
	spin_unlock_irq(&css_set_lock);

	return count;
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
 * do_each_subsys_mask - filter for_each_subsys with a bitmask
 * @ss: the iteration cursor
 * @ssid: the index of @ss, CGROUP_SUBSYS_COUNT after reaching the end
 * @ss_mask: the bitmask
 *
 * The block will only run for cases where the ssid-th bit (1 << ssid) of
 * @ss_mask is set.
 */
#define do_each_subsys_mask(ss, ssid, ss_mask) do {			\
	unsigned long __ss_mask = (ss_mask);				\
	if (!CGROUP_SUBSYS_COUNT) { /* to avoid spurious gcc warning */	\
		(ssid) = 0;						\
		break;							\
	}								\
	for_each_set_bit(ssid, &__ss_mask, CGROUP_SUBSYS_COUNT) {	\
		(ss) = cgroup_subsys[ssid];				\
		{

#define while_each_subsys_mask()					\
		}							\
	}								\
} while (false)

/* iterate over child cgrps, lock should be held throughout iteration */
#define cgroup_for_each_live_child(child, cgrp)				\
	list_for_each_entry((child), &(cgrp)->self.children, self.sibling) \
		if (({ lockdep_assert_held(&cgroup_mutex);		\
		       cgroup_is_dead(child); }))			\
			;						\
		else

/* walk live descendants in preorder */
#define cgroup_for_each_live_descendant_pre(dsct, d_css, cgrp)		\
	css_for_each_descendant_pre((d_css), cgroup_css((cgrp), NULL))	\
		if (({ lockdep_assert_held(&cgroup_mutex);		\
		       (dsct) = (d_css)->cgroup;			\
		       cgroup_is_dead(dsct); }))			\
			;						\
		else

/* walk live descendants in postorder */
#define cgroup_for_each_live_descendant_post(dsct, d_css, cgrp)		\
	css_for_each_descendant_post((d_css), cgroup_css((cgrp), NULL))	\
		if (({ lockdep_assert_held(&cgroup_mutex);		\
		       (dsct) = (d_css)->cgroup;			\
		       cgroup_is_dead(dsct); }))			\
			;						\
		else

/*
 * The default css_set - used by init and its children prior to any
 * hierarchies being mounted. It contains a pointer to the root state
 * for each subsystem. Also used to anchor the list of css_sets. Not
 * reference-counted, to improve performance when child cgroups
 * haven't been created.
 */
struct css_set init_css_set = {
	.refcount		= REFCOUNT_INIT(1),
	.dom_cset		= &init_css_set,
	.tasks			= LIST_HEAD_INIT(init_css_set.tasks),
	.mg_tasks		= LIST_HEAD_INIT(init_css_set.mg_tasks),
	.dying_tasks		= LIST_HEAD_INIT(init_css_set.dying_tasks),
	.task_iters		= LIST_HEAD_INIT(init_css_set.task_iters),
	.threaded_csets		= LIST_HEAD_INIT(init_css_set.threaded_csets),
	.cgrp_links		= LIST_HEAD_INIT(init_css_set.cgrp_links),
	.mg_preload_node	= LIST_HEAD_INIT(init_css_set.mg_preload_node),
	.mg_node		= LIST_HEAD_INIT(init_css_set.mg_node),

	/*
	 * The following field is re-initialized when this cset gets linked
	 * in cgroup_init().  However, let's initialize the field
	 * statically too so that the default cgroup can be accessed safely
	 * early during boot.
	 */
	.dfl_cgrp		= &cgrp_dfl_root.cgrp,
};

static int css_set_count	= 1;	/* 1 for init_css_set */

static bool css_set_threaded(struct css_set *cset)
{
	return cset->dom_cset != cset;
}

/**
 * css_set_populated - does a css_set contain any tasks?
 * @cset: target css_set
 *
 * css_set_populated() should be the same as !!cset->nr_tasks at steady
 * state. However, css_set_populated() can be called while a task is being
 * added to or removed from the linked list before the nr_tasks is
 * properly updated. Hence, we can't just look at ->nr_tasks here.
 */
static bool css_set_populated(struct css_set *cset)
{
	lockdep_assert_held(&css_set_lock);

	return !list_empty(&cset->tasks) || !list_empty(&cset->mg_tasks);
}

/**
 * cgroup_update_populated - update the populated count of a cgroup
 * @cgrp: the target cgroup
 * @populated: inc or dec populated count
 *
 * One of the css_sets associated with @cgrp is either getting its first
 * task or losing the last.  Update @cgrp->nr_populated_* accordingly.  The
 * count is propagated towards root so that a given cgroup's
 * nr_populated_children is zero iff none of its descendants contain any
 * tasks.
 *
 * @cgrp's interface file "cgroup.populated" is zero if both
 * @cgrp->nr_populated_csets and @cgrp->nr_populated_children are zero and
 * 1 otherwise.  When the sum changes from or to zero, userland is notified
 * that the content of the interface file has changed.  This can be used to
 * detect when @cgrp and its descendants become populated or empty.
 */
static void cgroup_update_populated(struct cgroup *cgrp, bool populated)
{
	struct cgroup *child = NULL;
	int adj = populated ? 1 : -1;

	lockdep_assert_held(&css_set_lock);

	do {
		bool was_populated = cgroup_is_populated(cgrp);

		if (!child) {
			cgrp->nr_populated_csets += adj;
		} else {
			if (cgroup_is_threaded(child))
				cgrp->nr_populated_threaded_children += adj;
			else
				cgrp->nr_populated_domain_children += adj;
		}

		if (was_populated == cgroup_is_populated(cgrp))
			break;

		cgroup1_check_for_release(cgrp);
		cgroup_file_notify(&cgrp->events_file);

		child = cgrp;
		cgrp = cgroup_parent(cgrp);
	} while (cgrp);
}

/**
 * css_set_update_populated - update populated state of a css_set
 * @cset: target css_set
 * @populated: whether @cset is populated or depopulated
 *
 * @cset is either getting the first task or losing the last.  Update the
 * populated counters of all associated cgroups accordingly.
 */
static void css_set_update_populated(struct css_set *cset, bool populated)
{
	struct cgrp_cset_link *link;

	lockdep_assert_held(&css_set_lock);

	list_for_each_entry(link, &cset->cgrp_links, cgrp_link)
		cgroup_update_populated(link->cgrp, populated);
}

/*
 * @task is leaving, advance task iterators which are pointing to it so
 * that they can resume at the next position.  Advancing an iterator might
 * remove it from the list, use safe walk.  See css_task_iter_skip() for
 * details.
 */
static void css_set_skip_task_iters(struct css_set *cset,
				    struct task_struct *task)
{
	struct css_task_iter *it, *pos;

	list_for_each_entry_safe(it, pos, &cset->task_iters, iters_node)
		css_task_iter_skip(it, task);
}

/**
 * css_set_move_task - move a task from one css_set to another
 * @task: task being moved
 * @from_cset: css_set @task currently belongs to (may be NULL)
 * @to_cset: new css_set @task is being moved to (may be NULL)
 * @use_mg_tasks: move to @to_cset->mg_tasks instead of ->tasks
 *
 * Move @task from @from_cset to @to_cset.  If @task didn't belong to any
 * css_set, @from_cset can be NULL.  If @task is being disassociated
 * instead of moved, @to_cset can be NULL.
 *
 * This function automatically handles populated counter updates and
 * css_task_iter adjustments but the caller is responsible for managing
 * @from_cset and @to_cset's reference counts.
 */
static void css_set_move_task(struct task_struct *task,
			      struct css_set *from_cset, struct css_set *to_cset,
			      bool use_mg_tasks)
{
	lockdep_assert_held(&css_set_lock);

	if (to_cset && !css_set_populated(to_cset))
		css_set_update_populated(to_cset, true);

	if (from_cset) {
		WARN_ON_ONCE(list_empty(&task->cg_list));

		css_set_skip_task_iters(from_cset, task);
		list_del_init(&task->cg_list);
		if (!css_set_populated(from_cset))
			css_set_update_populated(from_cset, false);
	} else {
		WARN_ON_ONCE(!list_empty(&task->cg_list));
	}

	if (to_cset) {
		/*
		 * We are synchronized through cgroup_threadgroup_rwsem
		 * against PF_EXITING setting such that we can't race
		 * against cgroup_exit() changing the css_set to
		 * init_css_set and dropping the old one.
		 */
		WARN_ON_ONCE(task->flags & PF_EXITING);

		cgroup_move_task(task, to_cset);
		list_add_tail(&task->cg_list, use_mg_tasks ? &to_cset->mg_tasks :
							     &to_cset->tasks);
	}
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

void put_css_set_locked(struct css_set *cset)
{
	struct cgrp_cset_link *link, *tmp_link;
	struct cgroup_subsys *ss;
	int ssid;

	lockdep_assert_held(&css_set_lock);

	if (!refcount_dec_and_test(&cset->refcount))
		return;

	WARN_ON_ONCE(!list_empty(&cset->threaded_csets));

	/* This css_set is dead. unlink it and release cgroup and css refs */
	for_each_subsys(ss, ssid) {
		list_del(&cset->e_cset_node[ssid]);
		css_put(cset->subsys[ssid]);
	}
	hash_del(&cset->hlist);
	css_set_count--;

	list_for_each_entry_safe(link, tmp_link, &cset->cgrp_links, cgrp_link) {
		list_del(&link->cset_link);
		list_del(&link->cgrp_link);
		if (cgroup_parent(link->cgrp))
			cgroup_put(link->cgrp);
		kfree(link);
	}

	if (css_set_threaded(cset)) {
		list_del(&cset->threaded_csets_node);
		put_css_set_locked(cset->dom_cset);
	}

	kfree_rcu(cset, rcu_head);
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
	struct cgroup *new_dfl_cgrp;
	struct list_head *l1, *l2;

	/*
	 * On the default hierarchy, there can be csets which are
	 * associated with the same set of cgroups but different csses.
	 * Let's first ensure that csses match.
	 */
	if (memcmp(template, cset->subsys, sizeof(cset->subsys)))
		return false;


	/* @cset's domain should match the default cgroup's */
	if (cgroup_on_dfl(new_cgrp))
		new_dfl_cgrp = new_cgrp;
	else
		new_dfl_cgrp = old_cset->dfl_cgrp;

	if (new_dfl_cgrp->dom_cgrp != cset->dom_cset->dfl_cgrp)
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

	/*
	 * Always add links to the tail of the lists so that the lists are
	 * in choronological order.
	 */
	list_move_tail(&link->cset_link, &cgrp->cset_links);
	list_add_tail(&link->cgrp_link, &cset->cgrp_links);

	if (cgroup_parent(cgrp))
		cgroup_get_live(cgrp);
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
	spin_lock_irq(&css_set_lock);
	cset = find_existing_css_set(old_cset, cgrp, template);
	if (cset)
		get_css_set(cset);
	spin_unlock_irq(&css_set_lock);

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

	refcount_set(&cset->refcount, 1);
	cset->dom_cset = cset;
	INIT_LIST_HEAD(&cset->tasks);
	INIT_LIST_HEAD(&cset->mg_tasks);
	INIT_LIST_HEAD(&cset->dying_tasks);
	INIT_LIST_HEAD(&cset->task_iters);
	INIT_LIST_HEAD(&cset->threaded_csets);
	INIT_HLIST_NODE(&cset->hlist);
	INIT_LIST_HEAD(&cset->cgrp_links);
	INIT_LIST_HEAD(&cset->mg_preload_node);
	INIT_LIST_HEAD(&cset->mg_node);

	/* Copy the set of subsystem state objects generated in
	 * find_existing_css_set() */
	memcpy(cset->subsys, template, sizeof(cset->subsys));

	spin_lock_irq(&css_set_lock);
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

	for_each_subsys(ss, ssid) {
		struct cgroup_subsys_state *css = cset->subsys[ssid];

		list_add_tail(&cset->e_cset_node[ssid],
			      &css->cgroup->e_csets[ssid]);
		css_get(css);
	}

	spin_unlock_irq(&css_set_lock);

	/*
	 * If @cset should be threaded, look up the matching dom_cset and
	 * link them up.  We first fully initialize @cset then look for the
	 * dom_cset.  It's simpler this way and safe as @cset is guaranteed
	 * to stay empty until we return.
	 */
	if (cgroup_is_threaded(cset->dfl_cgrp)) {
		struct css_set *dcset;

		dcset = find_css_set(cset, cset->dfl_cgrp->dom_cgrp);
		if (!dcset) {
			put_css_set(cset);
			return NULL;
		}

		spin_lock_irq(&css_set_lock);
		cset->dom_cset = dcset;
		list_add_tail(&cset->threaded_csets_node,
			      &dcset->threaded_csets);
		spin_unlock_irq(&css_set_lock);
	}

	return cset;
}

struct cgroup_root *cgroup_root_from_kf(struct kernfs_root *kf_root)
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

	idr_remove(&cgroup_hierarchy_idr, root->hierarchy_id);
}

void cgroup_free_root(struct cgroup_root *root)
{
	if (root) {
		idr_destroy(&root->cgroup_idr);
		kfree(root);
	}
}

static void cgroup_destroy_root(struct cgroup_root *root)
{
	struct cgroup *cgrp = &root->cgrp;
	struct cgrp_cset_link *link, *tmp_link;

	trace_cgroup_destroy_root(root);

	cgroup_lock_and_drain_offline(&cgrp_dfl_root.cgrp);

	BUG_ON(atomic_read(&root->nr_cgrps));
	BUG_ON(!list_empty(&cgrp->self.children));

	/* Rebind all subsystems back to the default hierarchy */
	WARN_ON(rebind_subsystems(&cgrp_dfl_root, root->subsys_mask));

	/*
	 * Release all the links from cset_links to this hierarchy's
	 * root cgroup
	 */
	spin_lock_irq(&css_set_lock);

	list_for_each_entry_safe(link, tmp_link, &cgrp->cset_links, cset_link) {
		list_del(&link->cset_link);
		list_del(&link->cgrp_link);
		kfree(link);
	}

	spin_unlock_irq(&css_set_lock);

	if (!list_empty(&root->root_list)) {
		list_del(&root->root_list);
		cgroup_root_count--;
	}

	cgroup_exit_root_id(root);

	mutex_unlock(&cgroup_mutex);

	kernfs_destroy_root(root->kf_root);
	cgroup_free_root(root);
}

/*
 * look up cgroup associated with current task's cgroup namespace on the
 * specified hierarchy
 */
static struct cgroup *
current_cgns_cgroup_from_root(struct cgroup_root *root)
{
	struct cgroup *res = NULL;
	struct css_set *cset;

	lockdep_assert_held(&css_set_lock);

	rcu_read_lock();

	cset = current->nsproxy->cgroup_ns->root_cset;
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
	rcu_read_unlock();

	BUG_ON(!res);
	return res;
}

/* look up cgroup associated with given css_set on the specified hierarchy */
static struct cgroup *cset_cgroup_from_root(struct css_set *cset,
					    struct cgroup_root *root)
{
	struct cgroup *res = NULL;

	lockdep_assert_held(&cgroup_mutex);
	lockdep_assert_held(&css_set_lock);

	if (cset == &init_css_set) {
		res = &root->cgrp;
	} else if (root == &cgrp_dfl_root) {
		res = cset->dfl_cgrp;
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
 * called with cgroup_mutex and css_set_lock held.
 */
struct cgroup *task_cgroup_from_root(struct task_struct *task,
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

static struct kernfs_syscall_ops cgroup_kf_syscall_ops;

static char *cgroup_file_name(struct cgroup *cgrp, const struct cftype *cft,
			      char *buf)
{
	struct cgroup_subsys *ss = cft->ss;

	if (cft->ss && !(cft->flags & CFTYPE_NO_PREFIX) &&
	    !(cgrp->root->flags & CGRP_ROOT_NOPREFIX))
		snprintf(buf, CGROUP_FILE_NAME_MAX, "%s.%s",
			 cgroup_on_dfl(cgrp) ? ss->name : ss->legacy_name,
			 cft->name);
	else
		strscpy(buf, cft->name, CGROUP_FILE_NAME_MAX);
	return buf;
}

/**
 * cgroup_file_mode - deduce file mode of a control file
 * @cft: the control file in question
 *
 * S_IRUGO for read, S_IWUSR for write.
 */
static umode_t cgroup_file_mode(const struct cftype *cft)
{
	umode_t mode = 0;

	if (cft->read_u64 || cft->read_s64 || cft->seq_show)
		mode |= S_IRUGO;

	if (cft->write_u64 || cft->write_s64 || cft->write) {
		if (cft->flags & CFTYPE_WORLD_WRITABLE)
			mode |= S_IWUGO;
		else
			mode |= S_IWUSR;
	}

	return mode;
}

/**
 * cgroup_calc_subtree_ss_mask - calculate subtree_ss_mask
 * @subtree_control: the new subtree_control mask to consider
 * @this_ss_mask: available subsystems
 *
 * On the default hierarchy, a subsystem may request other subsystems to be
 * enabled together through its ->depends_on mask.  In such cases, more
 * subsystems than specified in "cgroup.subtree_control" may be enabled.
 *
 * This function calculates which subsystems need to be enabled if
 * @subtree_control is to be applied while restricted to @this_ss_mask.
 */
static u16 cgroup_calc_subtree_ss_mask(u16 subtree_control, u16 this_ss_mask)
{
	u16 cur_ss_mask = subtree_control;
	struct cgroup_subsys *ss;
	int ssid;

	lockdep_assert_held(&cgroup_mutex);

	cur_ss_mask |= cgrp_dfl_implicit_ss_mask;

	while (true) {
		u16 new_ss_mask = cur_ss_mask;

		do_each_subsys_mask(ss, ssid, cur_ss_mask) {
			new_ss_mask |= ss->depends_on;
		} while_each_subsys_mask();

		/*
		 * Mask out subsystems which aren't available.  This can
		 * happen only if some depended-upon subsystems were bound
		 * to non-default hierarchies.
		 */
		new_ss_mask &= this_ss_mask;

		if (new_ss_mask == cur_ss_mask)
			break;
		cur_ss_mask = new_ss_mask;
	}

	return cur_ss_mask;
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
void cgroup_kn_unlock(struct kernfs_node *kn)
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
 * @drain_offline: perform offline draining on the cgroup
 *
 * This helper is to be used by a cgroup kernfs method currently servicing
 * @kn.  It breaks the active protection, performs cgroup locking and
 * verifies that the associated cgroup is alive.  Returns the cgroup if
 * alive; otherwise, %NULL.  A successful return should be undone by a
 * matching cgroup_kn_unlock() invocation.  If @drain_offline is %true, the
 * cgroup is drained of offlining csses before return.
 *
 * Any cgroup kernfs method implementation which requires locking the
 * associated cgroup should use this helper.  It avoids nesting cgroup
 * locking under kernfs active protection and allows all kernfs operations
 * including self-removal.
 */
struct cgroup *cgroup_kn_lock_live(struct kernfs_node *kn, bool drain_offline)
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
	if (!cgroup_tryget(cgrp))
		return NULL;
	kernfs_break_active_protection(kn);

	if (drain_offline)
		cgroup_lock_and_drain_offline(cgrp);
	else
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

	if (cft->file_offset) {
		struct cgroup_subsys_state *css = cgroup_css(cgrp, cft->ss);
		struct cgroup_file *cfile = (void *)css + cft->file_offset;

		spin_lock_irq(&cgroup_file_kn_lock);
		cfile->kn = NULL;
		spin_unlock_irq(&cgroup_file_kn_lock);

		del_timer_sync(&cfile->notify_timer);
	}

	kernfs_remove_by_name(cgrp->kn, cgroup_file_name(cgrp, cft, name));
}

/**
 * css_clear_dir - remove subsys files in a cgroup directory
 * @css: taget css
 */
static void css_clear_dir(struct cgroup_subsys_state *css)
{
	struct cgroup *cgrp = css->cgroup;
	struct cftype *cfts;

	if (!(css->flags & CSS_VISIBLE))
		return;

	css->flags &= ~CSS_VISIBLE;

	if (!css->ss) {
		if (cgroup_on_dfl(cgrp))
			cfts = cgroup_base_files;
		else
			cfts = cgroup1_base_files;

		cgroup_addrm_files(css, cgrp, cfts, false);
	} else {
		list_for_each_entry(cfts, &css->ss->cfts, node)
			cgroup_addrm_files(css, cgrp, cfts, false);
	}
}

/**
 * css_populate_dir - create subsys files in a cgroup directory
 * @css: target css
 *
 * On failure, no file is added.
 */
static int css_populate_dir(struct cgroup_subsys_state *css)
{
	struct cgroup *cgrp = css->cgroup;
	struct cftype *cfts, *failed_cfts;
	int ret;

	if ((css->flags & CSS_VISIBLE) || !cgrp->kn)
		return 0;

	if (!css->ss) {
		if (cgroup_on_dfl(cgrp))
			cfts = cgroup_base_files;
		else
			cfts = cgroup1_base_files;

		ret = cgroup_addrm_files(&cgrp->self, cgrp, cfts, true);
		if (ret < 0)
			return ret;
	} else {
		list_for_each_entry(cfts, &css->ss->cfts, node) {
			ret = cgroup_addrm_files(css, cgrp, cfts, true);
			if (ret < 0) {
				failed_cfts = cfts;
				goto err;
			}
		}
	}

	css->flags |= CSS_VISIBLE;

	return 0;
err:
	list_for_each_entry(cfts, &css->ss->cfts, node) {
		if (cfts == failed_cfts)
			break;
		cgroup_addrm_files(css, cgrp, cfts, false);
	}
	return ret;
}

int rebind_subsystems(struct cgroup_root *dst_root, u16 ss_mask)
{
	struct cgroup *dcgrp = &dst_root->cgrp;
	struct cgroup_subsys *ss;
	int ssid, i, ret;

	lockdep_assert_held(&cgroup_mutex);

	do_each_subsys_mask(ss, ssid, ss_mask) {
		/*
		 * If @ss has non-root csses attached to it, can't move.
		 * If @ss is an implicit controller, it is exempt from this
		 * rule and can be stolen.
		 */
		if (css_next_child(NULL, cgroup_css(&ss->root->cgrp, ss)) &&
		    !ss->implicit_on_dfl)
			return -EBUSY;

		/* can't move between two non-dummy roots either */
		if (ss->root != &cgrp_dfl_root && dst_root != &cgrp_dfl_root)
			return -EBUSY;
	} while_each_subsys_mask();

	do_each_subsys_mask(ss, ssid, ss_mask) {
		struct cgroup_root *src_root = ss->root;
		struct cgroup *scgrp = &src_root->cgrp;
		struct cgroup_subsys_state *css = cgroup_css(scgrp, ss);
		struct css_set *cset;

		WARN_ON(!css || cgroup_css(dcgrp, ss));

		/* disable from the source */
		src_root->subsys_mask &= ~(1 << ssid);
		WARN_ON(cgroup_apply_control(scgrp));
		cgroup_finalize_control(scgrp, 0);

		/* rebind */
		RCU_INIT_POINTER(scgrp->subsys[ssid], NULL);
		rcu_assign_pointer(dcgrp->subsys[ssid], css);
		ss->root = dst_root;
		css->cgroup = dcgrp;

		spin_lock_irq(&css_set_lock);
		hash_for_each(css_set_table, i, cset, hlist)
			list_move_tail(&cset->e_cset_node[ss->id],
				       &dcgrp->e_csets[ss->id]);
		spin_unlock_irq(&css_set_lock);

		/* default hierarchy doesn't enable controllers by default */
		dst_root->subsys_mask |= 1 << ssid;
		if (dst_root == &cgrp_dfl_root) {
			static_branch_enable(cgroup_subsys_on_dfl_key[ssid]);
		} else {
			dcgrp->subtree_control |= 1 << ssid;
			static_branch_disable(cgroup_subsys_on_dfl_key[ssid]);
		}

		ret = cgroup_apply_control(dcgrp);
		if (ret)
			pr_warn("partial failure to rebind %s controller (err=%d)\n",
				ss->name, ret);

		if (ss->bind)
			ss->bind(css);
	} while_each_subsys_mask();

	kernfs_activate(dcgrp->kn);
	return 0;
}

int cgroup_show_path(struct seq_file *sf, struct kernfs_node *kf_node,
		     struct kernfs_root *kf_root)
{
	int len = 0;
	char *buf = NULL;
	struct cgroup_root *kf_cgroot = cgroup_root_from_kf(kf_root);
	struct cgroup *ns_cgroup;

	buf = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	spin_lock_irq(&css_set_lock);
	ns_cgroup = current_cgns_cgroup_from_root(kf_cgroot);
	len = kernfs_path_from_node(kf_node, ns_cgroup->kn, buf, PATH_MAX);
	spin_unlock_irq(&css_set_lock);

	if (len >= PATH_MAX)
		len = -ERANGE;
	else if (len > 0) {
		seq_escape(sf, buf, " \t\n\\");
		len = 0;
	}
	kfree(buf);
	return len;
}

static int parse_cgroup_root_flags(char *data, unsigned int *root_flags)
{
	char *token;

	*root_flags = 0;

	if (!data || *data == '\0')
		return 0;

	while ((token = strsep(&data, ",")) != NULL) {
		if (!strcmp(token, "nsdelegate")) {
			*root_flags |= CGRP_ROOT_NS_DELEGATE;
			continue;
		}

		pr_err("cgroup2: unknown option \"%s\"\n", token);
		return -EINVAL;
	}

	return 0;
}

static void apply_cgroup_root_flags(unsigned int root_flags)
{
	if (current->nsproxy->cgroup_ns == &init_cgroup_ns) {
		if (root_flags & CGRP_ROOT_NS_DELEGATE)
			cgrp_dfl_root.flags |= CGRP_ROOT_NS_DELEGATE;
		else
			cgrp_dfl_root.flags &= ~CGRP_ROOT_NS_DELEGATE;
	}
}

static int cgroup_show_options(struct seq_file *seq, struct kernfs_root *kf_root)
{
	if (cgrp_dfl_root.flags & CGRP_ROOT_NS_DELEGATE)
		seq_puts(seq, ",nsdelegate");
	return 0;
}

static int cgroup_remount(struct kernfs_root *kf_root, int *flags, char *data)
{
	unsigned int root_flags;
	int ret;

	ret = parse_cgroup_root_flags(data, &root_flags);
	if (ret)
		return ret;

	apply_cgroup_root_flags(root_flags);
	return 0;
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

	/*
	 * We need tasklist_lock because RCU is not safe against
	 * while_each_thread(). Besides, a forking task that has passed
	 * cgroup_post_fork() without seeing use_task_css_set_links = 1
	 * is not guaranteed to have its child immediately visible in the
	 * tasklist if we walk through it with RCU.
	 */
	read_lock(&tasklist_lock);
	spin_lock_irq(&css_set_lock);

	if (use_task_css_set_links)
		goto out_unlock;

	use_task_css_set_links = true;

	do_each_thread(g, p) {
		WARN_ON_ONCE(!list_empty(&p->cg_list) ||
			     task_css_set(p) != &init_css_set);

		/*
		 * We should check if the process is exiting, otherwise
		 * it will race with cgroup_exit() in that the list
		 * entry won't be deleted though the process has exited.
		 * Do it while holding siglock so that we don't end up
		 * racing against cgroup_exit().
		 *
		 * Interrupts were already disabled while acquiring
		 * the css_set_lock, so we do not need to disable it
		 * again when acquiring the sighand->siglock here.
		 */
		spin_lock(&p->sighand->siglock);
		if (!(p->flags & PF_EXITING)) {
			struct css_set *cset = task_css_set(p);

			if (!css_set_populated(cset))
				css_set_update_populated(cset, true);
			list_add_tail(&p->cg_list, &cset->tasks);
			get_css_set(cset);
			cset->nr_tasks++;
		}
		spin_unlock(&p->sighand->siglock);
	} while_each_thread(g, p);
out_unlock:
	spin_unlock_irq(&css_set_lock);
	read_unlock(&tasklist_lock);
}

static void init_cgroup_housekeeping(struct cgroup *cgrp)
{
	struct cgroup_subsys *ss;
	int ssid;

	INIT_LIST_HEAD(&cgrp->self.sibling);
	INIT_LIST_HEAD(&cgrp->self.children);
	INIT_LIST_HEAD(&cgrp->cset_links);
	INIT_LIST_HEAD(&cgrp->pidlists);
	mutex_init(&cgrp->pidlist_mutex);
	cgrp->self.cgroup = cgrp;
	cgrp->self.flags |= CSS_ONLINE;
	cgrp->dom_cgrp = cgrp;
	cgrp->max_descendants = INT_MAX;
	cgrp->max_depth = INT_MAX;
	INIT_LIST_HEAD(&cgrp->rstat_css_list);
	prev_cputime_init(&cgrp->prev_cputime);

	for_each_subsys(ss, ssid)
		INIT_LIST_HEAD(&cgrp->e_csets[ssid]);

	init_waitqueue_head(&cgrp->offline_waitq);
	INIT_WORK(&cgrp->release_agent_work, cgroup1_release_agent);
}

void init_cgroup_root(struct cgroup_root *root, struct cgroup_sb_opts *opts)
{
	struct cgroup *cgrp = &root->cgrp;

	INIT_LIST_HEAD(&root->root_list);
	atomic_set(&root->nr_cgrps, 1);
	cgrp->root = root;
	init_cgroup_housekeeping(cgrp);
	idr_init(&root->cgroup_idr);

	root->flags = opts->flags;
	if (opts->release_agent)
		strscpy(root->release_agent_path, opts->release_agent, PATH_MAX);
	if (opts->name)
		strscpy(root->name, opts->name, MAX_CGROUP_ROOT_NAMELEN);
	if (opts->cpuset_clone_children)
		set_bit(CGRP_CPUSET_CLONE_CHILDREN, &root->cgrp.flags);
}

int cgroup_setup_root(struct cgroup_root *root, u16 ss_mask)
{
	LIST_HEAD(tmp_links);
	struct cgroup *root_cgrp = &root->cgrp;
	struct kernfs_syscall_ops *kf_sops;
	struct css_set *cset;
	int i, ret;

	lockdep_assert_held(&cgroup_mutex);

	ret = cgroup_idr_alloc(&root->cgroup_idr, root_cgrp, 1, 2, GFP_KERNEL);
	if (ret < 0)
		goto out;
	root_cgrp->id = ret;
	root_cgrp->ancestor_ids[0] = ret;

	ret = percpu_ref_init(&root_cgrp->self.refcnt, css_release,
			      0, GFP_KERNEL);
	if (ret)
		goto out;

	/*
	 * We're accessing css_set_count without locking css_set_lock here,
	 * but that's OK - it can only be increased by someone holding
	 * cgroup_lock, and that's us.  Later rebinding may disable
	 * controllers on the default hierarchy and thus create new csets,
	 * which can't be more than the existing ones.  Allocate 2x.
	 */
	ret = allocate_cgrp_cset_links(2 * css_set_count, &tmp_links);
	if (ret)
		goto cancel_ref;

	ret = cgroup_init_root_id(root);
	if (ret)
		goto cancel_ref;

	kf_sops = root == &cgrp_dfl_root ?
		&cgroup_kf_syscall_ops : &cgroup1_kf_syscall_ops;

	root->kf_root = kernfs_create_root(kf_sops,
					   KERNFS_ROOT_CREATE_DEACTIVATED |
					   KERNFS_ROOT_SUPPORT_EXPORTOP,
					   root_cgrp);
	if (IS_ERR(root->kf_root)) {
		ret = PTR_ERR(root->kf_root);
		goto exit_root_id;
	}
	root_cgrp->kn = root->kf_root->kn;

	ret = css_populate_dir(&root_cgrp->self);
	if (ret)
		goto destroy_root;

	ret = rebind_subsystems(root, ss_mask);
	if (ret)
		goto destroy_root;

	ret = cgroup_bpf_inherit(root_cgrp);
	WARN_ON_ONCE(ret);

	trace_cgroup_setup_root(root);

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
	spin_lock_irq(&css_set_lock);
	hash_for_each(css_set_table, i, cset, hlist) {
		link_css_set(&tmp_links, cset, root_cgrp);
		if (css_set_populated(cset))
			cgroup_update_populated(root_cgrp, true);
	}
	spin_unlock_irq(&css_set_lock);

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
	percpu_ref_exit(&root_cgrp->self.refcnt);
out:
	free_cgrp_cset_links(&tmp_links);
	return ret;
}

struct dentry *cgroup_do_mount(struct file_system_type *fs_type, int flags,
			       struct cgroup_root *root, unsigned long magic,
			       struct cgroup_namespace *ns)
{
	struct dentry *dentry;
	bool new_sb = false;

	dentry = kernfs_mount(fs_type, flags, root->kf_root, magic, &new_sb);

	/*
	 * In non-init cgroup namespace, instead of root cgroup's dentry,
	 * we return the dentry corresponding to the cgroupns->root_cgrp.
	 */
	if (!IS_ERR(dentry) && ns != &init_cgroup_ns) {
		struct dentry *nsdentry;
		struct super_block *sb = dentry->d_sb;
		struct cgroup *cgrp;

		mutex_lock(&cgroup_mutex);
		spin_lock_irq(&css_set_lock);

		cgrp = cset_cgroup_from_root(ns->root_cset, root);

		spin_unlock_irq(&css_set_lock);
		mutex_unlock(&cgroup_mutex);

		nsdentry = kernfs_node_dentry(cgrp->kn, sb);
		dput(dentry);
		if (IS_ERR(nsdentry))
			deactivate_locked_super(sb);
		dentry = nsdentry;
	}

	if (!new_sb)
		cgroup_put(&root->cgrp);

	return dentry;
}

static struct dentry *cgroup_mount(struct file_system_type *fs_type,
			 int flags, const char *unused_dev_name,
			 void *data)
{
	struct cgroup_namespace *ns = current->nsproxy->cgroup_ns;
	struct dentry *dentry;
	int ret;

	get_cgroup_ns(ns);

	/* Check if the caller has permission to mount. */
	if (!ns_capable(ns->user_ns, CAP_SYS_ADMIN)) {
		put_cgroup_ns(ns);
		return ERR_PTR(-EPERM);
	}

	/*
	 * The first time anyone tries to mount a cgroup, enable the list
	 * linking each css_set to its tasks and fix up all existing tasks.
	 */
	if (!use_task_css_set_links)
		cgroup_enable_task_cg_lists();

	if (fs_type == &cgroup2_fs_type) {
		unsigned int root_flags;

		ret = parse_cgroup_root_flags(data, &root_flags);
		if (ret) {
			put_cgroup_ns(ns);
			return ERR_PTR(ret);
		}

		cgrp_dfl_visible = true;
		cgroup_get_live(&cgrp_dfl_root.cgrp);

		dentry = cgroup_do_mount(&cgroup2_fs_type, flags, &cgrp_dfl_root,
					 CGROUP2_SUPER_MAGIC, ns);
		if (!IS_ERR(dentry))
			apply_cgroup_root_flags(root_flags);
	} else {
		dentry = cgroup1_mount(&cgroup_fs_type, flags, data,
				       CGROUP_SUPER_MAGIC, ns);
	}

	put_cgroup_ns(ns);
	return dentry;
}

static void cgroup_kill_sb(struct super_block *sb)
{
	struct kernfs_root *kf_root = kernfs_root_from_sb(sb);
	struct cgroup_root *root = cgroup_root_from_kf(kf_root);

	/*
	 * If @root doesn't have any children, start killing it.
	 * This prevents new mounts by disabling percpu_ref_tryget_live().
	 * cgroup_mount() may wait for @root's release.
	 *
	 * And don't kill the default root.
	 */
	if (list_empty(&root->cgrp.self.children) && root != &cgrp_dfl_root &&
	    !percpu_ref_is_dying(&root->cgrp.self.refcnt))
		percpu_ref_kill(&root->cgrp.self.refcnt);
	cgroup_put(&root->cgrp);
	kernfs_kill_sb(sb);
}

struct file_system_type cgroup_fs_type = {
	.name = "cgroup",
	.mount = cgroup_mount,
	.kill_sb = cgroup_kill_sb,
	.fs_flags = FS_USERNS_MOUNT,
};

static struct file_system_type cgroup2_fs_type = {
	.name = "cgroup2",
	.mount = cgroup_mount,
	.kill_sb = cgroup_kill_sb,
	.fs_flags = FS_USERNS_MOUNT,
};

int cgroup_path_ns_locked(struct cgroup *cgrp, char *buf, size_t buflen,
			  struct cgroup_namespace *ns)
{
	struct cgroup *root = cset_cgroup_from_root(ns->root_cset, cgrp->root);

	return kernfs_path_from_node(cgrp->kn, root->kn, buf, buflen);
}

int cgroup_path_ns(struct cgroup *cgrp, char *buf, size_t buflen,
		   struct cgroup_namespace *ns)
{
	int ret;

	mutex_lock(&cgroup_mutex);
	spin_lock_irq(&css_set_lock);

	ret = cgroup_path_ns_locked(cgrp, buf, buflen, ns);

	spin_unlock_irq(&css_set_lock);
	mutex_unlock(&cgroup_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(cgroup_path_ns);

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
int task_cgroup_path(struct task_struct *task, char *buf, size_t buflen)
{
	struct cgroup_root *root;
	struct cgroup *cgrp;
	int hierarchy_id = 1;
	int ret;

	mutex_lock(&cgroup_mutex);
	spin_lock_irq(&css_set_lock);

	root = idr_get_next(&cgroup_hierarchy_idr, &hierarchy_id);

	if (root) {
		cgrp = task_cgroup_from_root(task, root);
		ret = cgroup_path_ns_locked(cgrp, buf, buflen, &init_cgroup_ns);
	} else {
		/* if no hierarchy exists, everyone is in "/" */
		ret = strlcpy(buf, "/", buflen);
	}

	spin_unlock_irq(&css_set_lock);
	mutex_unlock(&cgroup_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(task_cgroup_path);

/**
 * cgroup_migrate_add_task - add a migration target task to a migration context
 * @task: target task
 * @mgctx: target migration context
 *
 * Add @task, which is a migration target, to @mgctx->tset.  This function
 * becomes noop if @task doesn't need to be migrated.  @task's css_set
 * should have been added as a migration source and @task->cg_list will be
 * moved from the css_set's tasks list to mg_tasks one.
 */
static void cgroup_migrate_add_task(struct task_struct *task,
				    struct cgroup_mgctx *mgctx)
{
	struct css_set *cset;

	lockdep_assert_held(&css_set_lock);

	/* @task either already exited or can't exit until the end */
	if (task->flags & PF_EXITING)
		return;

	/* leave @task alone if post_fork() hasn't linked it yet */
	if (list_empty(&task->cg_list))
		return;

	cset = task_css_set(task);
	if (!cset->mg_src_cgrp)
		return;

	mgctx->tset.nr_tasks++;

	list_move_tail(&task->cg_list, &cset->mg_tasks);
	if (list_empty(&cset->mg_node))
		list_add_tail(&cset->mg_node,
			      &mgctx->tset.src_csets);
	if (list_empty(&cset->mg_dst_cset->mg_node))
		list_add_tail(&cset->mg_dst_cset->mg_node,
			      &mgctx->tset.dst_csets);
}

/**
 * cgroup_taskset_first - reset taskset and return the first task
 * @tset: taskset of interest
 * @dst_cssp: output variable for the destination css
 *
 * @tset iteration is initialized and the first task is returned.
 */
struct task_struct *cgroup_taskset_first(struct cgroup_taskset *tset,
					 struct cgroup_subsys_state **dst_cssp)
{
	tset->cur_cset = list_first_entry(tset->csets, struct css_set, mg_node);
	tset->cur_task = NULL;

	return cgroup_taskset_next(tset, dst_cssp);
}

/**
 * cgroup_taskset_next - iterate to the next task in taskset
 * @tset: taskset of interest
 * @dst_cssp: output variable for the destination css
 *
 * Return the next task in @tset.  Iteration must have been initialized
 * with cgroup_taskset_first().
 */
struct task_struct *cgroup_taskset_next(struct cgroup_taskset *tset,
					struct cgroup_subsys_state **dst_cssp)
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

			/*
			 * This function may be called both before and
			 * after cgroup_taskset_migrate().  The two cases
			 * can be distinguished by looking at whether @cset
			 * has its ->mg_dst_cset set.
			 */
			if (cset->mg_dst_cset)
				*dst_cssp = cset->mg_dst_cset->subsys[tset->ssid];
			else
				*dst_cssp = cset->subsys[tset->ssid];

			return task;
		}

		cset = list_next_entry(cset, mg_node);
		task = NULL;
	}

	return NULL;
}

/**
 * cgroup_taskset_migrate - migrate a taskset
 * @mgctx: migration context
 *
 * Migrate tasks in @mgctx as setup by migration preparation functions.
 * This function fails iff one of the ->can_attach callbacks fails and
 * guarantees that either all or none of the tasks in @mgctx are migrated.
 * @mgctx is consumed regardless of success.
 */
static int cgroup_migrate_execute(struct cgroup_mgctx *mgctx)
{
	struct cgroup_taskset *tset = &mgctx->tset;
	struct cgroup_subsys *ss;
	struct task_struct *task, *tmp_task;
	struct css_set *cset, *tmp_cset;
	int ssid, failed_ssid, ret;

	/* check that we can legitimately attach to the cgroup */
	if (tset->nr_tasks) {
		do_each_subsys_mask(ss, ssid, mgctx->ss_mask) {
			if (ss->can_attach) {
				tset->ssid = ssid;
				ret = ss->can_attach(tset);
				if (ret) {
					failed_ssid = ssid;
					goto out_cancel_attach;
				}
			}
		} while_each_subsys_mask();
	}

	/*
	 * Now that we're guaranteed success, proceed to move all tasks to
	 * the new cgroup.  There are no failure cases after here, so this
	 * is the commit point.
	 */
	spin_lock_irq(&css_set_lock);
	list_for_each_entry(cset, &tset->src_csets, mg_node) {
		list_for_each_entry_safe(task, tmp_task, &cset->mg_tasks, cg_list) {
			struct css_set *from_cset = task_css_set(task);
			struct css_set *to_cset = cset->mg_dst_cset;

			get_css_set(to_cset);
			to_cset->nr_tasks++;
			css_set_move_task(task, from_cset, to_cset, true);
			from_cset->nr_tasks--;
			/*
			 * If the source or destination cgroup is frozen,
			 * the task might require to change its state.
			 */
			cgroup_freezer_migrate_task(task, from_cset->dfl_cgrp,
						    to_cset->dfl_cgrp);
			put_css_set_locked(from_cset);

		}
	}
	spin_unlock_irq(&css_set_lock);

	/*
	 * Migration is committed, all target tasks are now on dst_csets.
	 * Nothing is sensitive to fork() after this point.  Notify
	 * controllers that migration is complete.
	 */
	tset->csets = &tset->dst_csets;

	if (tset->nr_tasks) {
		do_each_subsys_mask(ss, ssid, mgctx->ss_mask) {
			if (ss->attach) {
				tset->ssid = ssid;
				ss->attach(tset);
			}
		} while_each_subsys_mask();
	}

	ret = 0;
	goto out_release_tset;

out_cancel_attach:
	if (tset->nr_tasks) {
		do_each_subsys_mask(ss, ssid, mgctx->ss_mask) {
			if (ssid == failed_ssid)
				break;
			if (ss->cancel_attach) {
				tset->ssid = ssid;
				ss->cancel_attach(tset);
			}
		} while_each_subsys_mask();
	}
out_release_tset:
	spin_lock_irq(&css_set_lock);
	list_splice_init(&tset->dst_csets, &tset->src_csets);
	list_for_each_entry_safe(cset, tmp_cset, &tset->src_csets, mg_node) {
		list_splice_tail_init(&cset->mg_tasks, &cset->tasks);
		list_del_init(&cset->mg_node);
	}
	spin_unlock_irq(&css_set_lock);

	/*
	 * Re-initialize the cgroup_taskset structure in case it is reused
	 * again in another cgroup_migrate_add_task()/cgroup_migrate_execute()
	 * iteration.
	 */
	tset->nr_tasks = 0;
	tset->csets    = &tset->src_csets;
	return ret;
}

/**
 * cgroup_migrate_vet_dst - verify whether a cgroup can be migration destination
 * @dst_cgrp: destination cgroup to test
 *
 * On the default hierarchy, except for the mixable, (possible) thread root
 * and threaded cgroups, subtree_control must be zero for migration
 * destination cgroups with tasks so that child cgroups don't compete
 * against tasks.
 */
int cgroup_migrate_vet_dst(struct cgroup *dst_cgrp)
{
	/* v1 doesn't have any restriction */
	if (!cgroup_on_dfl(dst_cgrp))
		return 0;

	/* verify @dst_cgrp can host resources */
	if (!cgroup_is_valid_domain(dst_cgrp->dom_cgrp))
		return -EOPNOTSUPP;

	/* mixables don't care */
	if (cgroup_is_mixable(dst_cgrp))
		return 0;

	/*
	 * If @dst_cgrp is already or can become a thread root or is
	 * threaded, it doesn't matter.
	 */
	if (cgroup_can_be_thread_root(dst_cgrp) || cgroup_is_threaded(dst_cgrp))
		return 0;

	/* apply no-internal-process constraint */
	if (dst_cgrp->subtree_control)
		return -EBUSY;

	return 0;
}

/**
 * cgroup_migrate_finish - cleanup after attach
 * @mgctx: migration context
 *
 * Undo cgroup_migrate_add_src() and cgroup_migrate_prepare_dst().  See
 * those functions for details.
 */
void cgroup_migrate_finish(struct cgroup_mgctx *mgctx)
{
	LIST_HEAD(preloaded);
	struct css_set *cset, *tmp_cset;

	lockdep_assert_held(&cgroup_mutex);

	spin_lock_irq(&css_set_lock);

	list_splice_tail_init(&mgctx->preloaded_src_csets, &preloaded);
	list_splice_tail_init(&mgctx->preloaded_dst_csets, &preloaded);

	list_for_each_entry_safe(cset, tmp_cset, &preloaded, mg_preload_node) {
		cset->mg_src_cgrp = NULL;
		cset->mg_dst_cgrp = NULL;
		cset->mg_dst_cset = NULL;
		list_del_init(&cset->mg_preload_node);
		put_css_set_locked(cset);
	}

	spin_unlock_irq(&css_set_lock);
}

/**
 * cgroup_migrate_add_src - add a migration source css_set
 * @src_cset: the source css_set to add
 * @dst_cgrp: the destination cgroup
 * @mgctx: migration context
 *
 * Tasks belonging to @src_cset are about to be migrated to @dst_cgrp.  Pin
 * @src_cset and add it to @mgctx->src_csets, which should later be cleaned
 * up by cgroup_migrate_finish().
 *
 * This function may be called without holding cgroup_threadgroup_rwsem
 * even if the target is a process.  Threads may be created and destroyed
 * but as long as cgroup_mutex is not dropped, no new css_set can be put
 * into play and the preloaded css_sets are guaranteed to cover all
 * migrations.
 */
void cgroup_migrate_add_src(struct css_set *src_cset,
			    struct cgroup *dst_cgrp,
			    struct cgroup_mgctx *mgctx)
{
	struct cgroup *src_cgrp;

	lockdep_assert_held(&cgroup_mutex);
	lockdep_assert_held(&css_set_lock);

	/*
	 * If ->dead, @src_set is associated with one or more dead cgroups
	 * and doesn't contain any migratable tasks.  Ignore it early so
	 * that the rest of migration path doesn't get confused by it.
	 */
	if (src_cset->dead)
		return;

	src_cgrp = cset_cgroup_from_root(src_cset, dst_cgrp->root);

	if (!list_empty(&src_cset->mg_preload_node))
		return;

	WARN_ON(src_cset->mg_src_cgrp);
	WARN_ON(src_cset->mg_dst_cgrp);
	WARN_ON(!list_empty(&src_cset->mg_tasks));
	WARN_ON(!list_empty(&src_cset->mg_node));

	src_cset->mg_src_cgrp = src_cgrp;
	src_cset->mg_dst_cgrp = dst_cgrp;
	get_css_set(src_cset);
	list_add_tail(&src_cset->mg_preload_node, &mgctx->preloaded_src_csets);
}

/**
 * cgroup_migrate_prepare_dst - prepare destination css_sets for migration
 * @mgctx: migration context
 *
 * Tasks are about to be moved and all the source css_sets have been
 * preloaded to @mgctx->preloaded_src_csets.  This function looks up and
 * pins all destination css_sets, links each to its source, and append them
 * to @mgctx->preloaded_dst_csets.
 *
 * This function must be called after cgroup_migrate_add_src() has been
 * called on each migration source css_set.  After migration is performed
 * using cgroup_migrate(), cgroup_migrate_finish() must be called on
 * @mgctx.
 */
int cgroup_migrate_prepare_dst(struct cgroup_mgctx *mgctx)
{
	struct css_set *src_cset, *tmp_cset;

	lockdep_assert_held(&cgroup_mutex);

	/* look up the dst cset for each src cset and link it to src */
	list_for_each_entry_safe(src_cset, tmp_cset, &mgctx->preloaded_src_csets,
				 mg_preload_node) {
		struct css_set *dst_cset;
		struct cgroup_subsys *ss;
		int ssid;

		dst_cset = find_css_set(src_cset, src_cset->mg_dst_cgrp);
		if (!dst_cset)
			return -ENOMEM;

		WARN_ON_ONCE(src_cset->mg_dst_cset || dst_cset->mg_dst_cset);

		/*
		 * If src cset equals dst, it's noop.  Drop the src.
		 * cgroup_migrate() will skip the cset too.  Note that we
		 * can't handle src == dst as some nodes are used by both.
		 */
		if (src_cset == dst_cset) {
			src_cset->mg_src_cgrp = NULL;
			src_cset->mg_dst_cgrp = NULL;
			list_del_init(&src_cset->mg_preload_node);
			put_css_set(src_cset);
			put_css_set(dst_cset);
			continue;
		}

		src_cset->mg_dst_cset = dst_cset;

		if (list_empty(&dst_cset->mg_preload_node))
			list_add_tail(&dst_cset->mg_preload_node,
				      &mgctx->preloaded_dst_csets);
		else
			put_css_set(dst_cset);

		for_each_subsys(ss, ssid)
			if (src_cset->subsys[ssid] != dst_cset->subsys[ssid])
				mgctx->ss_mask |= 1 << ssid;
	}

	return 0;
}

/**
 * cgroup_migrate - migrate a process or task to a cgroup
 * @leader: the leader of the process or the task to migrate
 * @threadgroup: whether @leader points to the whole process or a single task
 * @mgctx: migration context
 *
 * Migrate a process or task denoted by @leader.  If migrating a process,
 * the caller must be holding cgroup_threadgroup_rwsem.  The caller is also
 * responsible for invoking cgroup_migrate_add_src() and
 * cgroup_migrate_prepare_dst() on the targets before invoking this
 * function and following up with cgroup_migrate_finish().
 *
 * As long as a controller's ->can_attach() doesn't fail, this function is
 * guaranteed to succeed.  This means that, excluding ->can_attach()
 * failure, when migrating multiple targets, the success or failure can be
 * decided for all targets by invoking group_migrate_prepare_dst() before
 * actually starting migrating.
 */
int cgroup_migrate(struct task_struct *leader, bool threadgroup,
		   struct cgroup_mgctx *mgctx)
{
	struct task_struct *task;

	/*
	 * Prevent freeing of tasks while we take a snapshot. Tasks that are
	 * already PF_EXITING could be freed from underneath us unless we
	 * take an rcu_read_lock.
	 */
	spin_lock_irq(&css_set_lock);
	rcu_read_lock();
	task = leader;
	do {
		cgroup_migrate_add_task(task, mgctx);
		if (!threadgroup)
			break;
	} while_each_thread(leader, task);
	rcu_read_unlock();
	spin_unlock_irq(&css_set_lock);

	return cgroup_migrate_execute(mgctx);
}

/**
 * cgroup_attach_task - attach a task or a whole threadgroup to a cgroup
 * @dst_cgrp: the cgroup to attach to
 * @leader: the task or the leader of the threadgroup to be attached
 * @threadgroup: attach the whole threadgroup?
 *
 * Call holding cgroup_mutex and cgroup_threadgroup_rwsem.
 */
int cgroup_attach_task(struct cgroup *dst_cgrp, struct task_struct *leader,
		       bool threadgroup)
{
	DEFINE_CGROUP_MGCTX(mgctx);
	struct task_struct *task;
	int ret;

	ret = cgroup_migrate_vet_dst(dst_cgrp);
	if (ret)
		return ret;

	/* look up all src csets */
	spin_lock_irq(&css_set_lock);
	rcu_read_lock();
	task = leader;
	do {
		cgroup_migrate_add_src(task_css_set(task), dst_cgrp, &mgctx);
		if (!threadgroup)
			break;
	} while_each_thread(leader, task);
	rcu_read_unlock();
	spin_unlock_irq(&css_set_lock);

	/* prepare dst csets and commit */
	ret = cgroup_migrate_prepare_dst(&mgctx);
	if (!ret)
		ret = cgroup_migrate(leader, threadgroup, &mgctx);

	cgroup_migrate_finish(&mgctx);

	if (!ret)
		TRACE_CGROUP_PATH(attach_task, dst_cgrp, leader, threadgroup);

	return ret;
}

struct task_struct *cgroup_procs_write_start(char *buf, bool threadgroup)
	__acquires(&cgroup_threadgroup_rwsem)
{
	struct task_struct *tsk;
	pid_t pid;

	if (kstrtoint(strstrip(buf), 0, &pid) || pid < 0)
		return ERR_PTR(-EINVAL);

	percpu_down_write(&cgroup_threadgroup_rwsem);

	rcu_read_lock();
	if (pid) {
		tsk = find_task_by_vpid(pid);
		if (!tsk) {
			tsk = ERR_PTR(-ESRCH);
			goto out_unlock_threadgroup;
		}
	} else {
		tsk = current;
	}

	if (threadgroup)
		tsk = tsk->group_leader;

	/*
	 * kthreads may acquire PF_NO_SETAFFINITY during initialization.
	 * If userland migrates such a kthread to a non-root cgroup, it can
	 * become trapped in a cpuset, or RT kthread may be born in a
	 * cgroup with no rt_runtime allocated.  Just say no.
	 */
	if (tsk->no_cgroup_migration || (tsk->flags & PF_NO_SETAFFINITY)) {
		tsk = ERR_PTR(-EINVAL);
		goto out_unlock_threadgroup;
	}

	get_task_struct(tsk);
	goto out_unlock_rcu;

out_unlock_threadgroup:
	percpu_up_write(&cgroup_threadgroup_rwsem);
out_unlock_rcu:
	rcu_read_unlock();
	return tsk;
}

void cgroup_procs_write_finish(struct task_struct *task)
	__releases(&cgroup_threadgroup_rwsem)
{
	struct cgroup_subsys *ss;
	int ssid;

	/* release reference from cgroup_procs_write_start() */
	put_task_struct(task);

	percpu_up_write(&cgroup_threadgroup_rwsem);
	for_each_subsys(ss, ssid)
		if (ss->post_attach)
			ss->post_attach();
}

static void cgroup_print_ss_mask(struct seq_file *seq, u16 ss_mask)
{
	struct cgroup_subsys *ss;
	bool printed = false;
	int ssid;

	do_each_subsys_mask(ss, ssid, ss_mask) {
		if (printed)
			seq_putc(seq, ' ');
		seq_printf(seq, "%s", ss->name);
		printed = true;
	} while_each_subsys_mask();
	if (printed)
		seq_putc(seq, '\n');
}

/* show controllers which are enabled from the parent */
static int cgroup_controllers_show(struct seq_file *seq, void *v)
{
	struct cgroup *cgrp = seq_css(seq)->cgroup;

	cgroup_print_ss_mask(seq, cgroup_control(cgrp));
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
 * @cgrp's control masks have changed and its subtree's css associations
 * need to be updated accordingly.  This function looks up all css_sets
 * which are attached to the subtree, creates the matching updated css_sets
 * and migrates the tasks to the new ones.
 */
static int cgroup_update_dfl_csses(struct cgroup *cgrp)
{
	DEFINE_CGROUP_MGCTX(mgctx);
	struct cgroup_subsys_state *d_css;
	struct cgroup *dsct;
	struct css_set *src_cset;
	int ret;

	lockdep_assert_held(&cgroup_mutex);

	percpu_down_write(&cgroup_threadgroup_rwsem);

	/* look up all csses currently attached to @cgrp's subtree */
	spin_lock_irq(&css_set_lock);
	cgroup_for_each_live_descendant_pre(dsct, d_css, cgrp) {
		struct cgrp_cset_link *link;

		list_for_each_entry(link, &dsct->cset_links, cset_link)
			cgroup_migrate_add_src(link->cset, dsct, &mgctx);
	}
	spin_unlock_irq(&css_set_lock);

	/* NULL dst indicates self on default hierarchy */
	ret = cgroup_migrate_prepare_dst(&mgctx);
	if (ret)
		goto out_finish;

	spin_lock_irq(&css_set_lock);
	list_for_each_entry(src_cset, &mgctx.preloaded_src_csets, mg_preload_node) {
		struct task_struct *task, *ntask;

		/* all tasks in src_csets need to be migrated */
		list_for_each_entry_safe(task, ntask, &src_cset->tasks, cg_list)
			cgroup_migrate_add_task(task, &mgctx);
	}
	spin_unlock_irq(&css_set_lock);

	ret = cgroup_migrate_execute(&mgctx);
out_finish:
	cgroup_migrate_finish(&mgctx);
	percpu_up_write(&cgroup_threadgroup_rwsem);
	return ret;
}

/**
 * cgroup_lock_and_drain_offline - lock cgroup_mutex and drain offlined csses
 * @cgrp: root of the target subtree
 *
 * Because css offlining is asynchronous, userland may try to re-enable a
 * controller while the previous css is still around.  This function grabs
 * cgroup_mutex and drains the previous css instances of @cgrp's subtree.
 */
void cgroup_lock_and_drain_offline(struct cgroup *cgrp)
	__acquires(&cgroup_mutex)
{
	struct cgroup *dsct;
	struct cgroup_subsys_state *d_css;
	struct cgroup_subsys *ss;
	int ssid;

restart:
	mutex_lock(&cgroup_mutex);

	cgroup_for_each_live_descendant_post(dsct, d_css, cgrp) {
		for_each_subsys(ss, ssid) {
			struct cgroup_subsys_state *css = cgroup_css(dsct, ss);
			DEFINE_WAIT(wait);

			if (!css || !percpu_ref_is_dying(&css->refcnt))
				continue;

			cgroup_get_live(dsct);
			prepare_to_wait(&dsct->offline_waitq, &wait,
					TASK_UNINTERRUPTIBLE);

			mutex_unlock(&cgroup_mutex);
			schedule();
			finish_wait(&dsct->offline_waitq, &wait);

			cgroup_put(dsct);
			goto restart;
		}
	}
}

/**
 * cgroup_save_control - save control masks and dom_cgrp of a subtree
 * @cgrp: root of the target subtree
 *
 * Save ->subtree_control, ->subtree_ss_mask and ->dom_cgrp to the
 * respective old_ prefixed fields for @cgrp's subtree including @cgrp
 * itself.
 */
static void cgroup_save_control(struct cgroup *cgrp)
{
	struct cgroup *dsct;
	struct cgroup_subsys_state *d_css;

	cgroup_for_each_live_descendant_pre(dsct, d_css, cgrp) {
		dsct->old_subtree_control = dsct->subtree_control;
		dsct->old_subtree_ss_mask = dsct->subtree_ss_mask;
		dsct->old_dom_cgrp = dsct->dom_cgrp;
	}
}

/**
 * cgroup_propagate_control - refresh control masks of a subtree
 * @cgrp: root of the target subtree
 *
 * For @cgrp and its subtree, ensure ->subtree_ss_mask matches
 * ->subtree_control and propagate controller availability through the
 * subtree so that descendants don't have unavailable controllers enabled.
 */
static void cgroup_propagate_control(struct cgroup *cgrp)
{
	struct cgroup *dsct;
	struct cgroup_subsys_state *d_css;

	cgroup_for_each_live_descendant_pre(dsct, d_css, cgrp) {
		dsct->subtree_control &= cgroup_control(dsct);
		dsct->subtree_ss_mask =
			cgroup_calc_subtree_ss_mask(dsct->subtree_control,
						    cgroup_ss_mask(dsct));
	}
}

/**
 * cgroup_restore_control - restore control masks and dom_cgrp of a subtree
 * @cgrp: root of the target subtree
 *
 * Restore ->subtree_control, ->subtree_ss_mask and ->dom_cgrp from the
 * respective old_ prefixed fields for @cgrp's subtree including @cgrp
 * itself.
 */
static void cgroup_restore_control(struct cgroup *cgrp)
{
	struct cgroup *dsct;
	struct cgroup_subsys_state *d_css;

	cgroup_for_each_live_descendant_post(dsct, d_css, cgrp) {
		dsct->subtree_control = dsct->old_subtree_control;
		dsct->subtree_ss_mask = dsct->old_subtree_ss_mask;
		dsct->dom_cgrp = dsct->old_dom_cgrp;
	}
}

static bool css_visible(struct cgroup_subsys_state *css)
{
	struct cgroup_subsys *ss = css->ss;
	struct cgroup *cgrp = css->cgroup;

	if (cgroup_control(cgrp) & (1 << ss->id))
		return true;
	if (!(cgroup_ss_mask(cgrp) & (1 << ss->id)))
		return false;
	return cgroup_on_dfl(cgrp) && ss->implicit_on_dfl;
}

/**
 * cgroup_apply_control_enable - enable or show csses according to control
 * @cgrp: root of the target subtree
 *
 * Walk @cgrp's subtree and create new csses or make the existing ones
 * visible.  A css is created invisible if it's being implicitly enabled
 * through dependency.  An invisible css is made visible when the userland
 * explicitly enables it.
 *
 * Returns 0 on success, -errno on failure.  On failure, csses which have
 * been processed already aren't cleaned up.  The caller is responsible for
 * cleaning up with cgroup_apply_control_disable().
 */
static int cgroup_apply_control_enable(struct cgroup *cgrp)
{
	struct cgroup *dsct;
	struct cgroup_subsys_state *d_css;
	struct cgroup_subsys *ss;
	int ssid, ret;

	cgroup_for_each_live_descendant_pre(dsct, d_css, cgrp) {
		for_each_subsys(ss, ssid) {
			struct cgroup_subsys_state *css = cgroup_css(dsct, ss);

			if (!(cgroup_ss_mask(dsct) & (1 << ss->id)))
				continue;

			if (!css) {
				css = css_create(dsct, ss);
				if (IS_ERR(css))
					return PTR_ERR(css);
			}

			WARN_ON_ONCE(percpu_ref_is_dying(&css->refcnt));

			if (css_visible(css)) {
				ret = css_populate_dir(css);
				if (ret)
					return ret;
			}
		}
	}

	return 0;
}

/**
 * cgroup_apply_control_disable - kill or hide csses according to control
 * @cgrp: root of the target subtree
 *
 * Walk @cgrp's subtree and kill and hide csses so that they match
 * cgroup_ss_mask() and cgroup_visible_mask().
 *
 * A css is hidden when the userland requests it to be disabled while other
 * subsystems are still depending on it.  The css must not actively control
 * resources and be in the vanilla state if it's made visible again later.
 * Controllers which may be depended upon should provide ->css_reset() for
 * this purpose.
 */
static void cgroup_apply_control_disable(struct cgroup *cgrp)
{
	struct cgroup *dsct;
	struct cgroup_subsys_state *d_css;
	struct cgroup_subsys *ss;
	int ssid;

	cgroup_for_each_live_descendant_post(dsct, d_css, cgrp) {
		for_each_subsys(ss, ssid) {
			struct cgroup_subsys_state *css = cgroup_css(dsct, ss);

			if (!css)
				continue;

			WARN_ON_ONCE(percpu_ref_is_dying(&css->refcnt));

			if (css->parent &&
			    !(cgroup_ss_mask(dsct) & (1 << ss->id))) {
				kill_css(css);
			} else if (!css_visible(css)) {
				css_clear_dir(css);
				if (ss->css_reset)
					ss->css_reset(css);
			}
		}
	}
}

/**
 * cgroup_apply_control - apply control mask updates to the subtree
 * @cgrp: root of the target subtree
 *
 * subsystems can be enabled and disabled in a subtree using the following
 * steps.
 *
 * 1. Call cgroup_save_control() to stash the current state.
 * 2. Update ->subtree_control masks in the subtree as desired.
 * 3. Call cgroup_apply_control() to apply the changes.
 * 4. Optionally perform other related operations.
 * 5. Call cgroup_finalize_control() to finish up.
 *
 * This function implements step 3 and propagates the mask changes
 * throughout @cgrp's subtree, updates csses accordingly and perform
 * process migrations.
 */
static int cgroup_apply_control(struct cgroup *cgrp)
{
	int ret;

	cgroup_propagate_control(cgrp);

	ret = cgroup_apply_control_enable(cgrp);
	if (ret)
		return ret;

	/*
	 * At this point, cgroup_e_css() results reflect the new csses
	 * making the following cgroup_update_dfl_csses() properly update
	 * css associations of all tasks in the subtree.
	 */
	ret = cgroup_update_dfl_csses(cgrp);
	if (ret)
		return ret;

	return 0;
}

/**
 * cgroup_finalize_control - finalize control mask update
 * @cgrp: root of the target subtree
 * @ret: the result of the update
 *
 * Finalize control mask update.  See cgroup_apply_control() for more info.
 */
static void cgroup_finalize_control(struct cgroup *cgrp, int ret)
{
	if (ret) {
		cgroup_restore_control(cgrp);
		cgroup_propagate_control(cgrp);
	}

	cgroup_apply_control_disable(cgrp);
}

static int cgroup_vet_subtree_control_enable(struct cgroup *cgrp, u16 enable)
{
	u16 domain_enable = enable & ~cgrp_dfl_threaded_ss_mask;

	/* if nothing is getting enabled, nothing to worry about */
	if (!enable)
		return 0;

	/* can @cgrp host any resources? */
	if (!cgroup_is_valid_domain(cgrp->dom_cgrp))
		return -EOPNOTSUPP;

	/* mixables don't care */
	if (cgroup_is_mixable(cgrp))
		return 0;

	if (domain_enable) {
		/* can't enable domain controllers inside a thread subtree */
		if (cgroup_is_thread_root(cgrp) || cgroup_is_threaded(cgrp))
			return -EOPNOTSUPP;
	} else {
		/*
		 * Threaded controllers can handle internal competitions
		 * and are always allowed inside a (prospective) thread
		 * subtree.
		 */
		if (cgroup_can_be_thread_root(cgrp) || cgroup_is_threaded(cgrp))
			return 0;
	}

	/*
	 * Controllers can't be enabled for a cgroup with tasks to avoid
	 * child cgroups competing against tasks.
	 */
	if (cgroup_has_tasks(cgrp))
		return -EBUSY;

	return 0;
}

/* change the enabled child controllers for a cgroup in the default hierarchy */
static ssize_t cgroup_subtree_control_write(struct kernfs_open_file *of,
					    char *buf, size_t nbytes,
					    loff_t off)
{
	u16 enable = 0, disable = 0;
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
		do_each_subsys_mask(ss, ssid, ~cgrp_dfl_inhibit_ss_mask) {
			if (!cgroup_ssid_enabled(ssid) ||
			    strcmp(tok + 1, ss->name))
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
		} while_each_subsys_mask();
		if (ssid == CGROUP_SUBSYS_COUNT)
			return -EINVAL;
	}

	cgrp = cgroup_kn_lock_live(of->kn, true);
	if (!cgrp)
		return -ENODEV;

	for_each_subsys(ss, ssid) {
		if (enable & (1 << ssid)) {
			if (cgrp->subtree_control & (1 << ssid)) {
				enable &= ~(1 << ssid);
				continue;
			}

			if (!(cgroup_control(cgrp) & (1 << ssid))) {
				ret = -ENOENT;
				goto out_unlock;
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

	ret = cgroup_vet_subtree_control_enable(cgrp, enable);
	if (ret)
		goto out_unlock;

	/* save and update control masks and prepare csses */
	cgroup_save_control(cgrp);

	cgrp->subtree_control |= enable;
	cgrp->subtree_control &= ~disable;

	ret = cgroup_apply_control(cgrp);
	cgroup_finalize_control(cgrp, ret);
	if (ret)
		goto out_unlock;

	kernfs_activate(cgrp->kn);
out_unlock:
	cgroup_kn_unlock(of->kn);
	return ret ?: nbytes;
}

/**
 * cgroup_enable_threaded - make @cgrp threaded
 * @cgrp: the target cgroup
 *
 * Called when "threaded" is written to the cgroup.type interface file and
 * tries to make @cgrp threaded and join the parent's resource domain.
 * This function is never called on the root cgroup as cgroup.type doesn't
 * exist on it.
 */
static int cgroup_enable_threaded(struct cgroup *cgrp)
{
	struct cgroup *parent = cgroup_parent(cgrp);
	struct cgroup *dom_cgrp = parent->dom_cgrp;
	struct cgroup *dsct;
	struct cgroup_subsys_state *d_css;
	int ret;

	lockdep_assert_held(&cgroup_mutex);

	/* noop if already threaded */
	if (cgroup_is_threaded(cgrp))
		return 0;

	/*
	 * If @cgroup is populated or has domain controllers enabled, it
	 * can't be switched.  While the below cgroup_can_be_thread_root()
	 * test can catch the same conditions, that's only when @parent is
	 * not mixable, so let's check it explicitly.
	 */
	if (cgroup_is_populated(cgrp) ||
	    cgrp->subtree_control & ~cgrp_dfl_threaded_ss_mask)
		return -EOPNOTSUPP;

	/* we're joining the parent's domain, ensure its validity */
	if (!cgroup_is_valid_domain(dom_cgrp) ||
	    !cgroup_can_be_thread_root(dom_cgrp))
		return -EOPNOTSUPP;

	/*
	 * The following shouldn't cause actual migrations and should
	 * always succeed.
	 */
	cgroup_save_control(cgrp);

	cgroup_for_each_live_descendant_pre(dsct, d_css, cgrp)
		if (dsct == cgrp || cgroup_is_threaded(dsct))
			dsct->dom_cgrp = dom_cgrp;

	ret = cgroup_apply_control(cgrp);
	if (!ret)
		parent->nr_threaded_children++;

	cgroup_finalize_control(cgrp, ret);
	return ret;
}

static int cgroup_type_show(struct seq_file *seq, void *v)
{
	struct cgroup *cgrp = seq_css(seq)->cgroup;

	if (cgroup_is_threaded(cgrp))
		seq_puts(seq, "threaded\n");
	else if (!cgroup_is_valid_domain(cgrp))
		seq_puts(seq, "domain invalid\n");
	else if (cgroup_is_thread_root(cgrp))
		seq_puts(seq, "domain threaded\n");
	else
		seq_puts(seq, "domain\n");

	return 0;
}

static ssize_t cgroup_type_write(struct kernfs_open_file *of, char *buf,
				 size_t nbytes, loff_t off)
{
	struct cgroup *cgrp;
	int ret;

	/* only switching to threaded mode is supported */
	if (strcmp(strstrip(buf), "threaded"))
		return -EINVAL;

	/* drain dying csses before we re-apply (threaded) subtree control */
	cgrp = cgroup_kn_lock_live(of->kn, true);
	if (!cgrp)
		return -ENOENT;

	/* threaded can only be enabled */
	ret = cgroup_enable_threaded(cgrp);

	cgroup_kn_unlock(of->kn);
	return ret ?: nbytes;
}

static int cgroup_max_descendants_show(struct seq_file *seq, void *v)
{
	struct cgroup *cgrp = seq_css(seq)->cgroup;
	int descendants = READ_ONCE(cgrp->max_descendants);

	if (descendants == INT_MAX)
		seq_puts(seq, "max\n");
	else
		seq_printf(seq, "%d\n", descendants);

	return 0;
}

static ssize_t cgroup_max_descendants_write(struct kernfs_open_file *of,
					   char *buf, size_t nbytes, loff_t off)
{
	struct cgroup *cgrp;
	int descendants;
	ssize_t ret;

	buf = strstrip(buf);
	if (!strcmp(buf, "max")) {
		descendants = INT_MAX;
	} else {
		ret = kstrtoint(buf, 0, &descendants);
		if (ret)
			return ret;
	}

	if (descendants < 0)
		return -ERANGE;

	cgrp = cgroup_kn_lock_live(of->kn, false);
	if (!cgrp)
		return -ENOENT;

	cgrp->max_descendants = descendants;

	cgroup_kn_unlock(of->kn);

	return nbytes;
}

static int cgroup_max_depth_show(struct seq_file *seq, void *v)
{
	struct cgroup *cgrp = seq_css(seq)->cgroup;
	int depth = READ_ONCE(cgrp->max_depth);

	if (depth == INT_MAX)
		seq_puts(seq, "max\n");
	else
		seq_printf(seq, "%d\n", depth);

	return 0;
}

static ssize_t cgroup_max_depth_write(struct kernfs_open_file *of,
				      char *buf, size_t nbytes, loff_t off)
{
	struct cgroup *cgrp;
	ssize_t ret;
	int depth;

	buf = strstrip(buf);
	if (!strcmp(buf, "max")) {
		depth = INT_MAX;
	} else {
		ret = kstrtoint(buf, 0, &depth);
		if (ret)
			return ret;
	}

	if (depth < 0)
		return -ERANGE;

	cgrp = cgroup_kn_lock_live(of->kn, false);
	if (!cgrp)
		return -ENOENT;

	cgrp->max_depth = depth;

	cgroup_kn_unlock(of->kn);

	return nbytes;
}

static int cgroup_events_show(struct seq_file *seq, void *v)
{
	struct cgroup *cgrp = seq_css(seq)->cgroup;

	seq_printf(seq, "populated %d\n", cgroup_is_populated(cgrp));
	seq_printf(seq, "frozen %d\n", test_bit(CGRP_FROZEN, &cgrp->flags));

	return 0;
}

static int cgroup_stat_show(struct seq_file *seq, void *v)
{
	struct cgroup *cgroup = seq_css(seq)->cgroup;

	seq_printf(seq, "nr_descendants %d\n",
		   cgroup->nr_descendants);
	seq_printf(seq, "nr_dying_descendants %d\n",
		   cgroup->nr_dying_descendants);

	return 0;
}

static int __maybe_unused cgroup_extra_stat_show(struct seq_file *seq,
						 struct cgroup *cgrp, int ssid)
{
	struct cgroup_subsys *ss = cgroup_subsys[ssid];
	struct cgroup_subsys_state *css;
	int ret;

	if (!ss->css_extra_stat_show)
		return 0;

	css = cgroup_tryget_css(cgrp, ss);
	if (!css)
		return 0;

	ret = ss->css_extra_stat_show(seq, css);
	css_put(css);
	return ret;
}

static int cpu_stat_show(struct seq_file *seq, void *v)
{
	struct cgroup __maybe_unused *cgrp = seq_css(seq)->cgroup;
	int ret = 0;

	cgroup_base_stat_cputime_show(seq);
#ifdef CONFIG_CGROUP_SCHED
	ret = cgroup_extra_stat_show(seq, cgrp, cpu_cgrp_id);
#endif
	return ret;
}

#ifdef CONFIG_PSI
static int cgroup_io_pressure_show(struct seq_file *seq, void *v)
{
	return psi_show(seq, &seq_css(seq)->cgroup->psi, PSI_IO);
}
static int cgroup_memory_pressure_show(struct seq_file *seq, void *v)
{
	return psi_show(seq, &seq_css(seq)->cgroup->psi, PSI_MEM);
}
static int cgroup_cpu_pressure_show(struct seq_file *seq, void *v)
{
	return psi_show(seq, &seq_css(seq)->cgroup->psi, PSI_CPU);
}

static ssize_t cgroup_pressure_write(struct kernfs_open_file *of, char *buf,
					  size_t nbytes, enum psi_res res)
{
	struct psi_trigger *new;
	struct cgroup *cgrp;

	cgrp = cgroup_kn_lock_live(of->kn, false);
	if (!cgrp)
		return -ENODEV;

	cgroup_get(cgrp);
	cgroup_kn_unlock(of->kn);

	new = psi_trigger_create(&cgrp->psi, buf, nbytes, res);
	if (IS_ERR(new)) {
		cgroup_put(cgrp);
		return PTR_ERR(new);
	}

	psi_trigger_replace(&of->priv, new);

	cgroup_put(cgrp);

	return nbytes;
}

static ssize_t cgroup_io_pressure_write(struct kernfs_open_file *of,
					  char *buf, size_t nbytes,
					  loff_t off)
{
	return cgroup_pressure_write(of, buf, nbytes, PSI_IO);
}

static ssize_t cgroup_memory_pressure_write(struct kernfs_open_file *of,
					  char *buf, size_t nbytes,
					  loff_t off)
{
	return cgroup_pressure_write(of, buf, nbytes, PSI_MEM);
}

static ssize_t cgroup_cpu_pressure_write(struct kernfs_open_file *of,
					  char *buf, size_t nbytes,
					  loff_t off)
{
	return cgroup_pressure_write(of, buf, nbytes, PSI_CPU);
}

static __poll_t cgroup_pressure_poll(struct kernfs_open_file *of,
					  poll_table *pt)
{
	return psi_trigger_poll(&of->priv, of->file, pt);
}

static void cgroup_pressure_release(struct kernfs_open_file *of)
{
	psi_trigger_replace(&of->priv, NULL);
}
#endif /* CONFIG_PSI */

static int cgroup_freeze_show(struct seq_file *seq, void *v)
{
	struct cgroup *cgrp = seq_css(seq)->cgroup;

	seq_printf(seq, "%d\n", cgrp->freezer.freeze);

	return 0;
}

static ssize_t cgroup_freeze_write(struct kernfs_open_file *of,
				   char *buf, size_t nbytes, loff_t off)
{
	struct cgroup *cgrp;
	ssize_t ret;
	int freeze;

	ret = kstrtoint(strstrip(buf), 0, &freeze);
	if (ret)
		return ret;

	if (freeze < 0 || freeze > 1)
		return -ERANGE;

	cgrp = cgroup_kn_lock_live(of->kn, false);
	if (!cgrp)
		return -ENOENT;

	cgroup_freeze(cgrp, freeze);

	cgroup_kn_unlock(of->kn);

	return nbytes;
}

static int cgroup_file_open(struct kernfs_open_file *of)
{
	struct cftype *cft = of->kn->priv;

	if (cft->open)
		return cft->open(of);
	return 0;
}

static void cgroup_file_release(struct kernfs_open_file *of)
{
	struct cftype *cft = of->kn->priv;

	if (cft->release)
		cft->release(of);
}

static ssize_t cgroup_file_write(struct kernfs_open_file *of, char *buf,
				 size_t nbytes, loff_t off)
{
	struct cgroup_namespace *ns = current->nsproxy->cgroup_ns;
	struct cgroup *cgrp = of->kn->parent->priv;
	struct cftype *cft = of->kn->priv;
	struct cgroup_subsys_state *css;
	int ret;

	/*
	 * If namespaces are delegation boundaries, disallow writes to
	 * files in an non-init namespace root from inside the namespace
	 * except for the files explicitly marked delegatable -
	 * cgroup.procs and cgroup.subtree_control.
	 */
	if ((cgrp->root->flags & CGRP_ROOT_NS_DELEGATE) &&
	    !(cft->flags & CFTYPE_NS_DELEGATABLE) &&
	    ns != &init_cgroup_ns && ns->root_cset->dfl_cgrp == cgrp)
		return -EPERM;

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

static __poll_t cgroup_file_poll(struct kernfs_open_file *of, poll_table *pt)
{
	struct cftype *cft = of->kn->priv;

	if (cft->poll)
		return cft->poll(of, pt);

	return kernfs_generic_poll(of, pt);
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
	if (seq_cft(seq)->seq_stop)
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
	.open			= cgroup_file_open,
	.release		= cgroup_file_release,
	.write			= cgroup_file_write,
	.poll			= cgroup_file_poll,
	.seq_show		= cgroup_seqfile_show,
};

static struct kernfs_ops cgroup_kf_ops = {
	.atomic_write_len	= PAGE_SIZE,
	.open			= cgroup_file_open,
	.release		= cgroup_file_release,
	.write			= cgroup_file_write,
	.poll			= cgroup_file_poll,
	.seq_start		= cgroup_seqfile_start,
	.seq_next		= cgroup_seqfile_next,
	.seq_stop		= cgroup_seqfile_stop,
	.seq_show		= cgroup_seqfile_show,
};

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

static void cgroup_file_notify_timer(struct timer_list *timer)
{
	cgroup_file_notify(container_of(timer, struct cgroup_file,
					notify_timer));
}

static int cgroup_add_file(struct cgroup_subsys_state *css, struct cgroup *cgrp,
			   struct cftype *cft)
{
	char name[CGROUP_FILE_NAME_MAX];
	struct kernfs_node *kn;
	struct lock_class_key *key = NULL;
	int ret;

#ifdef CONFIG_DEBUG_LOCK_ALLOC
	key = &cft->lockdep_key;
#endif
	kn = __kernfs_create_file(cgrp->kn, cgroup_file_name(cgrp, cft, name),
				  cgroup_file_mode(cft),
				  GLOBAL_ROOT_UID, GLOBAL_ROOT_GID,
				  0, cft->kf_ops, cft,
				  NULL, key);
	if (IS_ERR(kn))
		return PTR_ERR(kn);

	ret = cgroup_kn_set_ugid(kn);
	if (ret) {
		kernfs_remove(kn);
		return ret;
	}

	if (cft->file_offset) {
		struct cgroup_file *cfile = (void *)css + cft->file_offset;

		timer_setup(&cfile->notify_timer, cgroup_file_notify_timer, 0);

		spin_lock_irq(&cgroup_file_kn_lock);
		cfile->kn = kn;
		spin_unlock_irq(&cgroup_file_kn_lock);
	}

	return 0;
}

/**
 * cgroup_addrm_files - add or remove files to a cgroup directory
 * @css: the target css
 * @cgrp: the target cgroup (usually css->cgroup)
 * @cfts: array of cftypes to be added
 * @is_add: whether to add or remove
 *
 * Depending on @is_add, add or remove files defined by @cfts on @cgrp.
 * For removals, this function never fails.
 */
static int cgroup_addrm_files(struct cgroup_subsys_state *css,
			      struct cgroup *cgrp, struct cftype cfts[],
			      bool is_add)
{
	struct cftype *cft, *cft_end = NULL;
	int ret = 0;

	lockdep_assert_held(&cgroup_mutex);

restart:
	for (cft = cfts; cft != cft_end && cft->name[0] != '\0'; cft++) {
		/* does cft->flags tell us to skip this file on @cgrp? */
		if ((cft->flags & __CFTYPE_ONLY_ON_DFL) && !cgroup_on_dfl(cgrp))
			continue;
		if ((cft->flags & __CFTYPE_NOT_ON_DFL) && cgroup_on_dfl(cgrp))
			continue;
		if ((cft->flags & CFTYPE_NOT_ON_ROOT) && !cgroup_parent(cgrp))
			continue;
		if ((cft->flags & CFTYPE_ONLY_ON_ROOT) && cgroup_parent(cgrp))
			continue;

		if (is_add) {
			ret = cgroup_add_file(css, cgrp, cft);
			if (ret) {
				pr_warn("%s: failed to add %s, err=%d\n",
					__func__, cft->name, ret);
				cft_end = cft;
				is_add = false;
				goto restart;
			}
		} else {
			cgroup_rm_file(cgrp, cft);
		}
	}
	return ret;
}

static int cgroup_apply_cftypes(struct cftype *cfts, bool is_add)
{
	struct cgroup_subsys *ss = cfts[0].ss;
	struct cgroup *root = &ss->root->cgrp;
	struct cgroup_subsys_state *css;
	int ret = 0;

	lockdep_assert_held(&cgroup_mutex);

	/* add/rm files for all cgroups created before */
	css_for_each_descendant_pre(css, cgroup_css(root, ss)) {
		struct cgroup *cgrp = css->cgroup;

		if (!(css->flags & CSS_VISIBLE))
			continue;

		ret = cgroup_addrm_files(css, cgrp, cfts, is_add);
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

		/* revert flags set by cgroup core while adding @cfts */
		cft->flags &= ~(__CFTYPE_ONLY_ON_DFL | __CFTYPE_NOT_ON_DFL);
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
static int cgroup_add_cftypes(struct cgroup_subsys *ss, struct cftype *cfts)
{
	int ret;

	if (!cgroup_ssid_enabled(ss->id))
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
 * cgroup_add_dfl_cftypes - add an array of cftypes for default hierarchy
 * @ss: target cgroup subsystem
 * @cfts: zero-length name terminated array of cftypes
 *
 * Similar to cgroup_add_cftypes() but the added files are only used for
 * the default hierarchy.
 */
int cgroup_add_dfl_cftypes(struct cgroup_subsys *ss, struct cftype *cfts)
{
	struct cftype *cft;

	for (cft = cfts; cft && cft->name[0] != '\0'; cft++)
		cft->flags |= __CFTYPE_ONLY_ON_DFL;
	return cgroup_add_cftypes(ss, cfts);
}

/**
 * cgroup_add_legacy_cftypes - add an array of cftypes for legacy hierarchies
 * @ss: target cgroup subsystem
 * @cfts: zero-length name terminated array of cftypes
 *
 * Similar to cgroup_add_cftypes() but the added files are only used for
 * the legacy hierarchies.
 */
int cgroup_add_legacy_cftypes(struct cgroup_subsys *ss, struct cftype *cfts)
{
	struct cftype *cft;

	for (cft = cfts; cft && cft->name[0] != '\0'; cft++)
		cft->flags |= __CFTYPE_NOT_ON_DFL;
	return cgroup_add_cftypes(ss, cfts);
}

/**
 * cgroup_file_notify - generate a file modified event for a cgroup_file
 * @cfile: target cgroup_file
 *
 * @cfile must have been obtained by setting cftype->file_offset.
 */
void cgroup_file_notify(struct cgroup_file *cfile)
{
	unsigned long flags;

	spin_lock_irqsave(&cgroup_file_kn_lock, flags);
	if (cfile->kn) {
		unsigned long last = cfile->notified_at;
		unsigned long next = last + CGROUP_FILE_NOTIFY_MIN_INTV;

		if (time_in_range(jiffies, last, next)) {
			timer_reduce(&cfile->notify_timer, next);
		} else {
			kernfs_notify(cfile->kn);
			cfile->notified_at = jiffies;
		}
	}
	spin_unlock_irqrestore(&cgroup_file_kn_lock, flags);
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
		if (child->flags & CSS_ONLINE) {
			ret = true;
			break;
		}
	}
	rcu_read_unlock();
	return ret;
}

static struct css_set *css_task_iter_next_css_set(struct css_task_iter *it)
{
	struct list_head *l;
	struct cgrp_cset_link *link;
	struct css_set *cset;

	lockdep_assert_held(&css_set_lock);

	/* find the next threaded cset */
	if (it->tcset_pos) {
		l = it->tcset_pos->next;

		if (l != it->tcset_head) {
			it->tcset_pos = l;
			return container_of(l, struct css_set,
					    threaded_csets_node);
		}

		it->tcset_pos = NULL;
	}

	/* find the next cset */
	l = it->cset_pos;
	l = l->next;
	if (l == it->cset_head) {
		it->cset_pos = NULL;
		return NULL;
	}

	if (it->ss) {
		cset = container_of(l, struct css_set, e_cset_node[it->ss->id]);
	} else {
		link = list_entry(l, struct cgrp_cset_link, cset_link);
		cset = link->cset;
	}

	it->cset_pos = l;

	/* initialize threaded css_set walking */
	if (it->flags & CSS_TASK_ITER_THREADED) {
		if (it->cur_dcset)
			put_css_set_locked(it->cur_dcset);
		it->cur_dcset = cset;
		get_css_set(cset);

		it->tcset_head = &cset->threaded_csets;
		it->tcset_pos = &cset->threaded_csets;
	}

	return cset;
}

/**
 * css_task_iter_advance_css_set - advance a task itererator to the next css_set
 * @it: the iterator to advance
 *
 * Advance @it to the next css_set to walk.
 */
static void css_task_iter_advance_css_set(struct css_task_iter *it)
{
	struct css_set *cset;

	lockdep_assert_held(&css_set_lock);

	/* Advance to the next non-empty css_set */
	do {
		cset = css_task_iter_next_css_set(it);
		if (!cset) {
			it->task_pos = NULL;
			return;
		}
	} while (!css_set_populated(cset) && list_empty(&cset->dying_tasks));

	if (!list_empty(&cset->tasks)) {
		it->task_pos = cset->tasks.next;
		it->cur_tasks_head = &cset->tasks;
	} else if (!list_empty(&cset->mg_tasks)) {
		it->task_pos = cset->mg_tasks.next;
		it->cur_tasks_head = &cset->mg_tasks;
	} else {
		it->task_pos = cset->dying_tasks.next;
		it->cur_tasks_head = &cset->dying_tasks;
	}

	it->tasks_head = &cset->tasks;
	it->mg_tasks_head = &cset->mg_tasks;
	it->dying_tasks_head = &cset->dying_tasks;

	/*
	 * We don't keep css_sets locked across iteration steps and thus
	 * need to take steps to ensure that iteration can be resumed after
	 * the lock is re-acquired.  Iteration is performed at two levels -
	 * css_sets and tasks in them.
	 *
	 * Once created, a css_set never leaves its cgroup lists, so a
	 * pinned css_set is guaranteed to stay put and we can resume
	 * iteration afterwards.
	 *
	 * Tasks may leave @cset across iteration steps.  This is resolved
	 * by registering each iterator with the css_set currently being
	 * walked and making css_set_move_task() advance iterators whose
	 * next task is leaving.
	 */
	if (it->cur_cset) {
		list_del(&it->iters_node);
		put_css_set_locked(it->cur_cset);
	}
	get_css_set(cset);
	it->cur_cset = cset;
	list_add(&it->iters_node, &cset->task_iters);
}

static void css_task_iter_skip(struct css_task_iter *it,
			       struct task_struct *task)
{
	lockdep_assert_held(&css_set_lock);

	if (it->task_pos == &task->cg_list) {
		it->task_pos = it->task_pos->next;
		it->flags |= CSS_TASK_ITER_SKIPPED;
	}
}

static void css_task_iter_advance(struct css_task_iter *it)
{
	struct task_struct *task;

	lockdep_assert_held(&css_set_lock);
repeat:
	if (it->task_pos) {
		/*
		 * Advance iterator to find next entry.  cset->tasks is
		 * consumed first and then ->mg_tasks.  After ->mg_tasks,
		 * we move onto the next cset.
		 */
		if (it->flags & CSS_TASK_ITER_SKIPPED)
			it->flags &= ~CSS_TASK_ITER_SKIPPED;
		else
			it->task_pos = it->task_pos->next;

		if (it->task_pos == it->tasks_head) {
			it->task_pos = it->mg_tasks_head->next;
			it->cur_tasks_head = it->mg_tasks_head;
		}
		if (it->task_pos == it->mg_tasks_head) {
			it->task_pos = it->dying_tasks_head->next;
			it->cur_tasks_head = it->dying_tasks_head;
		}
		if (it->task_pos == it->dying_tasks_head)
			css_task_iter_advance_css_set(it);
	} else {
		/* called from start, proceed to the first cset */
		css_task_iter_advance_css_set(it);
	}

	if (!it->task_pos)
		return;

	task = list_entry(it->task_pos, struct task_struct, cg_list);

	if (it->flags & CSS_TASK_ITER_PROCS) {
		/* if PROCS, skip over tasks which aren't group leaders */
		if (!thread_group_leader(task))
			goto repeat;

		/* and dying leaders w/o live member threads */
		if (it->cur_tasks_head == it->dying_tasks_head &&
		    !atomic_read(&task->signal->live))
			goto repeat;
	} else {
		/* skip all dying ones */
		if (it->cur_tasks_head == it->dying_tasks_head)
			goto repeat;
	}
}

/**
 * css_task_iter_start - initiate task iteration
 * @css: the css to walk tasks of
 * @flags: CSS_TASK_ITER_* flags
 * @it: the task iterator to use
 *
 * Initiate iteration through the tasks of @css.  The caller can call
 * css_task_iter_next() to walk through the tasks until the function
 * returns NULL.  On completion of iteration, css_task_iter_end() must be
 * called.
 */
void css_task_iter_start(struct cgroup_subsys_state *css, unsigned int flags,
			 struct css_task_iter *it)
{
	/* no one should try to iterate before mounting cgroups */
	WARN_ON_ONCE(!use_task_css_set_links);

	memset(it, 0, sizeof(*it));

	spin_lock_irq(&css_set_lock);

	it->ss = css->ss;
	it->flags = flags;

	if (it->ss)
		it->cset_pos = &css->cgroup->e_csets[css->ss->id];
	else
		it->cset_pos = &css->cgroup->cset_links;

	it->cset_head = it->cset_pos;

	css_task_iter_advance(it);

	spin_unlock_irq(&css_set_lock);
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
	if (it->cur_task) {
		put_task_struct(it->cur_task);
		it->cur_task = NULL;
	}

	spin_lock_irq(&css_set_lock);

	/* @it may be half-advanced by skips, finish advancing */
	if (it->flags & CSS_TASK_ITER_SKIPPED)
		css_task_iter_advance(it);

	if (it->task_pos) {
		it->cur_task = list_entry(it->task_pos, struct task_struct,
					  cg_list);
		get_task_struct(it->cur_task);
		css_task_iter_advance(it);
	}

	spin_unlock_irq(&css_set_lock);

	return it->cur_task;
}

/**
 * css_task_iter_end - finish task iteration
 * @it: the task iterator to finish
 *
 * Finish task iteration started by css_task_iter_start().
 */
void css_task_iter_end(struct css_task_iter *it)
{
	if (it->cur_cset) {
		spin_lock_irq(&css_set_lock);
		list_del(&it->iters_node);
		put_css_set_locked(it->cur_cset);
		spin_unlock_irq(&css_set_lock);
	}

	if (it->cur_dcset)
		put_css_set(it->cur_dcset);

	if (it->cur_task)
		put_task_struct(it->cur_task);
}

static void cgroup_procs_release(struct kernfs_open_file *of)
{
	if (of->priv) {
		css_task_iter_end(of->priv);
		kfree(of->priv);
	}
}

static void *cgroup_procs_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct kernfs_open_file *of = s->private;
	struct css_task_iter *it = of->priv;

	if (pos)
		(*pos)++;

	return css_task_iter_next(it);
}

static void *__cgroup_procs_start(struct seq_file *s, loff_t *pos,
				  unsigned int iter_flags)
{
	struct kernfs_open_file *of = s->private;
	struct cgroup *cgrp = seq_css(s)->cgroup;
	struct css_task_iter *it = of->priv;

	/*
	 * When a seq_file is seeked, it's always traversed sequentially
	 * from position 0, so we can simply keep iterating on !0 *pos.
	 */
	if (!it) {
		if (WARN_ON_ONCE((*pos)))
			return ERR_PTR(-EINVAL);

		it = kzalloc(sizeof(*it), GFP_KERNEL);
		if (!it)
			return ERR_PTR(-ENOMEM);
		of->priv = it;
		css_task_iter_start(&cgrp->self, iter_flags, it);
	} else if (!(*pos)) {
		css_task_iter_end(it);
		css_task_iter_start(&cgrp->self, iter_flags, it);
	} else
		return it->cur_task;

	return cgroup_procs_next(s, NULL, NULL);
}

static void *cgroup_procs_start(struct seq_file *s, loff_t *pos)
{
	struct cgroup *cgrp = seq_css(s)->cgroup;

	/*
	 * All processes of a threaded subtree belong to the domain cgroup
	 * of the subtree.  Only threads can be distributed across the
	 * subtree.  Reject reads on cgroup.procs in the subtree proper.
	 * They're always empty anyway.
	 */
	if (cgroup_is_threaded(cgrp))
		return ERR_PTR(-EOPNOTSUPP);

	return __cgroup_procs_start(s, pos, CSS_TASK_ITER_PROCS |
					    CSS_TASK_ITER_THREADED);
}

static int cgroup_procs_show(struct seq_file *s, void *v)
{
	seq_printf(s, "%d\n", task_pid_vnr(v));
	return 0;
}

static int cgroup_procs_write_permission(struct cgroup *src_cgrp,
					 struct cgroup *dst_cgrp,
					 struct super_block *sb)
{
	struct cgroup_namespace *ns = current->nsproxy->cgroup_ns;
	struct cgroup *com_cgrp = src_cgrp;
	struct inode *inode;
	int ret;

	lockdep_assert_held(&cgroup_mutex);

	/* find the common ancestor */
	while (!cgroup_is_descendant(dst_cgrp, com_cgrp))
		com_cgrp = cgroup_parent(com_cgrp);

	/* %current should be authorized to migrate to the common ancestor */
	inode = kernfs_get_inode(sb, com_cgrp->procs_file.kn);
	if (!inode)
		return -ENOMEM;

	ret = inode_permission(inode, MAY_WRITE);
	iput(inode);
	if (ret)
		return ret;

	/*
	 * If namespaces are delegation boundaries, %current must be able
	 * to see both source and destination cgroups from its namespace.
	 */
	if ((cgrp_dfl_root.flags & CGRP_ROOT_NS_DELEGATE) &&
	    (!cgroup_is_descendant(src_cgrp, ns->root_cset->dfl_cgrp) ||
	     !cgroup_is_descendant(dst_cgrp, ns->root_cset->dfl_cgrp)))
		return -ENOENT;

	return 0;
}

static ssize_t cgroup_procs_write(struct kernfs_open_file *of,
				  char *buf, size_t nbytes, loff_t off)
{
	struct cgroup *src_cgrp, *dst_cgrp;
	struct task_struct *task;
	ssize_t ret;

	dst_cgrp = cgroup_kn_lock_live(of->kn, false);
	if (!dst_cgrp)
		return -ENODEV;

	task = cgroup_procs_write_start(buf, true);
	ret = PTR_ERR_OR_ZERO(task);
	if (ret)
		goto out_unlock;

	/* find the source cgroup */
	spin_lock_irq(&css_set_lock);
	src_cgrp = task_cgroup_from_root(task, &cgrp_dfl_root);
	spin_unlock_irq(&css_set_lock);

	ret = cgroup_procs_write_permission(src_cgrp, dst_cgrp,
					    of->file->f_path.dentry->d_sb);
	if (ret)
		goto out_finish;

	ret = cgroup_attach_task(dst_cgrp, task, true);

out_finish:
	cgroup_procs_write_finish(task);
out_unlock:
	cgroup_kn_unlock(of->kn);

	return ret ?: nbytes;
}

static void *cgroup_threads_start(struct seq_file *s, loff_t *pos)
{
	return __cgroup_procs_start(s, pos, 0);
}

static ssize_t cgroup_threads_write(struct kernfs_open_file *of,
				    char *buf, size_t nbytes, loff_t off)
{
	struct cgroup *src_cgrp, *dst_cgrp;
	struct task_struct *task;
	ssize_t ret;

	buf = strstrip(buf);

	dst_cgrp = cgroup_kn_lock_live(of->kn, false);
	if (!dst_cgrp)
		return -ENODEV;

	task = cgroup_procs_write_start(buf, false);
	ret = PTR_ERR_OR_ZERO(task);
	if (ret)
		goto out_unlock;

	/* find the source cgroup */
	spin_lock_irq(&css_set_lock);
	src_cgrp = task_cgroup_from_root(task, &cgrp_dfl_root);
	spin_unlock_irq(&css_set_lock);

	/* thread migrations follow the cgroup.procs delegation rule */
	ret = cgroup_procs_write_permission(src_cgrp, dst_cgrp,
					    of->file->f_path.dentry->d_sb);
	if (ret)
		goto out_finish;

	/* and must be contained in the same domain */
	ret = -EOPNOTSUPP;
	if (src_cgrp->dom_cgrp != dst_cgrp->dom_cgrp)
		goto out_finish;

	ret = cgroup_attach_task(dst_cgrp, task, false);

out_finish:
	cgroup_procs_write_finish(task);
out_unlock:
	cgroup_kn_unlock(of->kn);

	return ret ?: nbytes;
}

/* cgroup core interface files for the default hierarchy */
static struct cftype cgroup_base_files[] = {
	{
		.name = "cgroup.type",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = cgroup_type_show,
		.write = cgroup_type_write,
	},
	{
		.name = "cgroup.procs",
		.flags = CFTYPE_NS_DELEGATABLE,
		.file_offset = offsetof(struct cgroup, procs_file),
		.release = cgroup_procs_release,
		.seq_start = cgroup_procs_start,
		.seq_next = cgroup_procs_next,
		.seq_show = cgroup_procs_show,
		.write = cgroup_procs_write,
	},
	{
		.name = "cgroup.threads",
		.flags = CFTYPE_NS_DELEGATABLE,
		.release = cgroup_procs_release,
		.seq_start = cgroup_threads_start,
		.seq_next = cgroup_procs_next,
		.seq_show = cgroup_procs_show,
		.write = cgroup_threads_write,
	},
	{
		.name = "cgroup.controllers",
		.seq_show = cgroup_controllers_show,
	},
	{
		.name = "cgroup.subtree_control",
		.flags = CFTYPE_NS_DELEGATABLE,
		.seq_show = cgroup_subtree_control_show,
		.write = cgroup_subtree_control_write,
	},
	{
		.name = "cgroup.events",
		.flags = CFTYPE_NOT_ON_ROOT,
		.file_offset = offsetof(struct cgroup, events_file),
		.seq_show = cgroup_events_show,
	},
	{
		.name = "cgroup.max.descendants",
		.seq_show = cgroup_max_descendants_show,
		.write = cgroup_max_descendants_write,
	},
	{
		.name = "cgroup.max.depth",
		.seq_show = cgroup_max_depth_show,
		.write = cgroup_max_depth_write,
	},
	{
		.name = "cgroup.stat",
		.seq_show = cgroup_stat_show,
	},
	{
		.name = "cgroup.freeze",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = cgroup_freeze_show,
		.write = cgroup_freeze_write,
	},
	{
		.name = "cpu.stat",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = cpu_stat_show,
	},
#ifdef CONFIG_PSI
	{
		.name = "io.pressure",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = cgroup_io_pressure_show,
		.write = cgroup_io_pressure_write,
		.poll = cgroup_pressure_poll,
		.release = cgroup_pressure_release,
	},
	{
		.name = "memory.pressure",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = cgroup_memory_pressure_show,
		.write = cgroup_memory_pressure_write,
		.poll = cgroup_pressure_poll,
		.release = cgroup_pressure_release,
	},
	{
		.name = "cpu.pressure",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = cgroup_cpu_pressure_show,
		.write = cgroup_cpu_pressure_write,
		.poll = cgroup_pressure_poll,
		.release = cgroup_pressure_release,
	},
#endif /* CONFIG_PSI */
	{ }	/* terminate */
};

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
static void css_free_rwork_fn(struct work_struct *work)
{
	struct cgroup_subsys_state *css = container_of(to_rcu_work(work),
				struct cgroup_subsys_state, destroy_rwork);
	struct cgroup_subsys *ss = css->ss;
	struct cgroup *cgrp = css->cgroup;

	percpu_ref_exit(&css->refcnt);

	if (ss) {
		/* css free path */
		struct cgroup_subsys_state *parent = css->parent;
		int id = css->id;

		ss->css_free(css);
		cgroup_idr_remove(&ss->css_idr, id);
		cgroup_put(cgrp);

		if (parent)
			css_put(parent);
	} else {
		/* cgroup free path */
		atomic_dec(&cgrp->root->nr_cgrps);
		cgroup1_pidlist_destroy_all(cgrp);
		cancel_work_sync(&cgrp->release_agent_work);

		if (cgroup_parent(cgrp)) {
			/*
			 * We get a ref to the parent, and put the ref when
			 * this cgroup is being freed, so it's guaranteed
			 * that the parent won't be destroyed before its
			 * children.
			 */
			cgroup_put(cgroup_parent(cgrp));
			kernfs_put(cgrp->kn);
			psi_cgroup_free(cgrp);
			if (cgroup_on_dfl(cgrp))
				cgroup_rstat_exit(cgrp);
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
		if (!list_empty(&css->rstat_css_node)) {
			cgroup_rstat_flush(cgrp);
			list_del_rcu(&css->rstat_css_node);
		}

		cgroup_idr_replace(&ss->css_idr, NULL, css->id);
		if (ss->css_released)
			ss->css_released(css);
	} else {
		struct cgroup *tcgrp;

		/* cgroup release path */
		TRACE_CGROUP_PATH(release, cgrp);

		if (cgroup_on_dfl(cgrp))
			cgroup_rstat_flush(cgrp);

		spin_lock_irq(&css_set_lock);
		for (tcgrp = cgroup_parent(cgrp); tcgrp;
		     tcgrp = cgroup_parent(tcgrp))
			tcgrp->nr_dying_descendants--;
		spin_unlock_irq(&css_set_lock);

		cgroup_idr_remove(&cgrp->root->cgroup_idr, cgrp->id);
		cgrp->id = -1;

		/*
		 * There are two control paths which try to determine
		 * cgroup from dentry without going through kernfs -
		 * cgroupstats_build() and css_tryget_online_from_dir().
		 * Those are supported by RCU protecting clearing of
		 * cgrp->kn->priv backpointer.
		 */
		if (cgrp->kn)
			RCU_INIT_POINTER(*(void __rcu __force **)&cgrp->kn->priv,
					 NULL);

		cgroup_bpf_put(cgrp);
	}

	mutex_unlock(&cgroup_mutex);

	INIT_RCU_WORK(&css->destroy_rwork, css_free_rwork_fn);
	queue_rcu_work(cgroup_destroy_wq, &css->destroy_rwork);
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

	cgroup_get_live(cgrp);

	memset(css, 0, sizeof(*css));
	css->cgroup = cgrp;
	css->ss = ss;
	css->id = -1;
	INIT_LIST_HEAD(&css->sibling);
	INIT_LIST_HEAD(&css->children);
	INIT_LIST_HEAD(&css->rstat_css_node);
	css->serial_nr = css_serial_nr_next++;
	atomic_set(&css->online_cnt, 0);

	if (cgroup_parent(cgrp)) {
		css->parent = cgroup_css(cgroup_parent(cgrp), ss);
		css_get(css->parent);
	}

	if (cgroup_on_dfl(cgrp) && ss->css_rstat_flush)
		list_add_rcu(&css->rstat_css_node, &cgrp->rstat_css_list);

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

		atomic_inc(&css->online_cnt);
		if (css->parent)
			atomic_inc(&css->parent->online_cnt);
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
 * css_create - create a cgroup_subsys_state
 * @cgrp: the cgroup new css will be associated with
 * @ss: the subsys of new css
 *
 * Create a new css associated with @cgrp - @ss pair.  On success, the new
 * css is online and installed in @cgrp.  This function doesn't create the
 * interface files.  Returns 0 on success, -errno on failure.
 */
static struct cgroup_subsys_state *css_create(struct cgroup *cgrp,
					      struct cgroup_subsys *ss)
{
	struct cgroup *parent = cgroup_parent(cgrp);
	struct cgroup_subsys_state *parent_css = cgroup_css(parent, ss);
	struct cgroup_subsys_state *css;
	int err;

	lockdep_assert_held(&cgroup_mutex);

	css = ss->css_alloc(parent_css);
	if (!css)
		css = ERR_PTR(-ENOMEM);
	if (IS_ERR(css))
		return css;

	init_and_link_css(css, ss, cgrp);

	err = percpu_ref_init(&css->refcnt, css_release, 0, GFP_KERNEL);
	if (err)
		goto err_free_css;

	err = cgroup_idr_alloc(&ss->css_idr, NULL, 2, 0, GFP_KERNEL);
	if (err < 0)
		goto err_free_css;
	css->id = err;

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

	return css;

err_list_del:
	list_del_rcu(&css->sibling);
err_free_css:
	list_del_rcu(&css->rstat_css_node);
	INIT_RCU_WORK(&css->destroy_rwork, css_free_rwork_fn);
	queue_rcu_work(cgroup_destroy_wq, &css->destroy_rwork);
	return ERR_PTR(err);
}

/*
 * The returned cgroup is fully initialized including its control mask, but
 * it isn't associated with its kernfs_node and doesn't have the control
 * mask applied.
 */
static struct cgroup *cgroup_create(struct cgroup *parent)
{
	struct cgroup_root *root = parent->root;
	struct cgroup *cgrp, *tcgrp;
	int level = parent->level + 1;
	int ret;

	/* allocate the cgroup and its ID, 0 is reserved for the root */
	cgrp = kzalloc(struct_size(cgrp, ancestor_ids, (level + 1)),
		       GFP_KERNEL);
	if (!cgrp)
		return ERR_PTR(-ENOMEM);

	ret = percpu_ref_init(&cgrp->self.refcnt, css_release, 0, GFP_KERNEL);
	if (ret)
		goto out_free_cgrp;

	if (cgroup_on_dfl(parent)) {
		ret = cgroup_rstat_init(cgrp);
		if (ret)
			goto out_cancel_ref;
	}

	/*
	 * Temporarily set the pointer to NULL, so idr_find() won't return
	 * a half-baked cgroup.
	 */
	cgrp->id = cgroup_idr_alloc(&root->cgroup_idr, NULL, 2, 0, GFP_KERNEL);
	if (cgrp->id < 0) {
		ret = -ENOMEM;
		goto out_stat_exit;
	}

	init_cgroup_housekeeping(cgrp);

	cgrp->self.parent = &parent->self;
	cgrp->root = root;
	cgrp->level = level;

	ret = psi_cgroup_alloc(cgrp);
	if (ret)
		goto out_idr_free;

	ret = cgroup_bpf_inherit(cgrp);
	if (ret)
		goto out_psi_free;

	/*
	 * New cgroup inherits effective freeze counter, and
	 * if the parent has to be frozen, the child has too.
	 */
	cgrp->freezer.e_freeze = parent->freezer.e_freeze;
	if (cgrp->freezer.e_freeze) {
		/*
		 * Set the CGRP_FREEZE flag, so when a process will be
		 * attached to the child cgroup, it will become frozen.
		 * At this point the new cgroup is unpopulated, so we can
		 * consider it frozen immediately.
		 */
		set_bit(CGRP_FREEZE, &cgrp->flags);
		set_bit(CGRP_FROZEN, &cgrp->flags);
	}

	spin_lock_irq(&css_set_lock);
	for (tcgrp = cgrp; tcgrp; tcgrp = cgroup_parent(tcgrp)) {
		cgrp->ancestor_ids[tcgrp->level] = tcgrp->id;

		if (tcgrp != cgrp) {
			tcgrp->nr_descendants++;

			/*
			 * If the new cgroup is frozen, all ancestor cgroups
			 * get a new frozen descendant, but their state can't
			 * change because of this.
			 */
			if (cgrp->freezer.e_freeze)
				tcgrp->freezer.nr_frozen_descendants++;
		}
	}
	spin_unlock_irq(&css_set_lock);

	if (notify_on_release(parent))
		set_bit(CGRP_NOTIFY_ON_RELEASE, &cgrp->flags);

	if (test_bit(CGRP_CPUSET_CLONE_CHILDREN, &parent->flags))
		set_bit(CGRP_CPUSET_CLONE_CHILDREN, &cgrp->flags);

	cgrp->self.serial_nr = css_serial_nr_next++;

	/* allocation complete, commit to creation */
	list_add_tail_rcu(&cgrp->self.sibling, &cgroup_parent(cgrp)->self.children);
	atomic_inc(&root->nr_cgrps);
	cgroup_get_live(parent);

	/*
	 * @cgrp is now fully operational.  If something fails after this
	 * point, it'll be released via the normal destruction path.
	 */
	cgroup_idr_replace(&root->cgroup_idr, cgrp, cgrp->id);

	/*
	 * On the default hierarchy, a child doesn't automatically inherit
	 * subtree_control from the parent.  Each is configured manually.
	 */
	if (!cgroup_on_dfl(cgrp))
		cgrp->subtree_control = cgroup_control(cgrp);

	cgroup_propagate_control(cgrp);

	return cgrp;

out_psi_free:
	psi_cgroup_free(cgrp);
out_idr_free:
	cgroup_idr_remove(&root->cgroup_idr, cgrp->id);
out_stat_exit:
	if (cgroup_on_dfl(parent))
		cgroup_rstat_exit(cgrp);
out_cancel_ref:
	percpu_ref_exit(&cgrp->self.refcnt);
out_free_cgrp:
	kfree(cgrp);
	return ERR_PTR(ret);
}

static bool cgroup_check_hierarchy_limits(struct cgroup *parent)
{
	struct cgroup *cgroup;
	int ret = false;
	int level = 1;

	lockdep_assert_held(&cgroup_mutex);

	for (cgroup = parent; cgroup; cgroup = cgroup_parent(cgroup)) {
		if (cgroup->nr_descendants >= cgroup->max_descendants)
			goto fail;

		if (level > cgroup->max_depth)
			goto fail;

		level++;
	}

	ret = true;
fail:
	return ret;
}

int cgroup_mkdir(struct kernfs_node *parent_kn, const char *name, umode_t mode)
{
	struct cgroup *parent, *cgrp;
	struct kernfs_node *kn;
	int ret;

	/* do not accept '\n' to prevent making /proc/<pid>/cgroup unparsable */
	if (strchr(name, '\n'))
		return -EINVAL;

	parent = cgroup_kn_lock_live(parent_kn, false);
	if (!parent)
		return -ENODEV;

	if (!cgroup_check_hierarchy_limits(parent)) {
		ret = -EAGAIN;
		goto out_unlock;
	}

	cgrp = cgroup_create(parent);
	if (IS_ERR(cgrp)) {
		ret = PTR_ERR(cgrp);
		goto out_unlock;
	}

	/* create the directory */
	kn = kernfs_create_dir(parent->kn, name, mode, cgrp);
	if (IS_ERR(kn)) {
		ret = PTR_ERR(kn);
		goto out_destroy;
	}
	cgrp->kn = kn;

	/*
	 * This extra ref will be put in cgroup_free_fn() and guarantees
	 * that @cgrp->kn is always accessible.
	 */
	kernfs_get(kn);

	ret = cgroup_kn_set_ugid(kn);
	if (ret)
		goto out_destroy;

	ret = css_populate_dir(&cgrp->self);
	if (ret)
		goto out_destroy;

	ret = cgroup_apply_control_enable(cgrp);
	if (ret)
		goto out_destroy;

	TRACE_CGROUP_PATH(mkdir, cgrp);

	/* let's create and online css's */
	kernfs_activate(kn);

	ret = 0;
	goto out_unlock;

out_destroy:
	cgroup_destroy_locked(cgrp);
out_unlock:
	cgroup_kn_unlock(parent_kn);
	return ret;
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

	do {
		offline_css(css);
		css_put(css);
		/* @css can't go away while we're holding cgroup_mutex */
		css = css->parent;
	} while (css && atomic_dec_and_test(&css->online_cnt));

	mutex_unlock(&cgroup_mutex);
}

/* css kill confirmation processing requires process context, bounce */
static void css_killed_ref_fn(struct percpu_ref *ref)
{
	struct cgroup_subsys_state *css =
		container_of(ref, struct cgroup_subsys_state, refcnt);

	if (atomic_dec_and_test(&css->online_cnt)) {
		INIT_WORK(&css->destroy_work, css_killed_work_fn);
		queue_work(cgroup_destroy_wq, &css->destroy_work);
	}
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

	if (css->flags & CSS_DYING)
		return;

	css->flags |= CSS_DYING;

	/*
	 * This must happen before css is disassociated with its cgroup.
	 * See seq_css() for details.
	 */
	css_clear_dir(css);

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
	struct cgroup *tcgrp, *parent = cgroup_parent(cgrp);
	struct cgroup_subsys_state *css;
	struct cgrp_cset_link *link;
	int ssid;

	lockdep_assert_held(&cgroup_mutex);

	/*
	 * Only migration can raise populated from zero and we're already
	 * holding cgroup_mutex.
	 */
	if (cgroup_is_populated(cgrp))
		return -EBUSY;

	/*
	 * Make sure there's no live children.  We can't test emptiness of
	 * ->self.children as dead children linger on it while being
	 * drained; otherwise, "rmdir parent/child parent" may fail.
	 */
	if (css_has_online_children(&cgrp->self))
		return -EBUSY;

	/*
	 * Mark @cgrp and the associated csets dead.  The former prevents
	 * further task migration and child creation by disabling
	 * cgroup_lock_live_group().  The latter makes the csets ignored by
	 * the migration path.
	 */
	cgrp->self.flags &= ~CSS_ONLINE;

	spin_lock_irq(&css_set_lock);
	list_for_each_entry(link, &cgrp->cset_links, cset_link)
		link->cset->dead = true;
	spin_unlock_irq(&css_set_lock);

	/* initiate massacre of all css's */
	for_each_css(css, ssid, cgrp)
		kill_css(css);

	/* clear and remove @cgrp dir, @cgrp has an extra ref on its kn */
	css_clear_dir(&cgrp->self);
	kernfs_remove(cgrp->kn);

	if (parent && cgroup_is_threaded(cgrp))
		parent->nr_threaded_children--;

	spin_lock_irq(&css_set_lock);
	for (tcgrp = cgroup_parent(cgrp); tcgrp; tcgrp = cgroup_parent(tcgrp)) {
		tcgrp->nr_descendants--;
		tcgrp->nr_dying_descendants++;
		/*
		 * If the dying cgroup is frozen, decrease frozen descendants
		 * counters of ancestor cgroups.
		 */
		if (test_bit(CGRP_FROZEN, &cgrp->flags))
			tcgrp->freezer.nr_frozen_descendants--;
	}
	spin_unlock_irq(&css_set_lock);

	cgroup1_check_for_release(parent);

	/* put the base reference */
	percpu_ref_kill(&cgrp->self.refcnt);

	return 0;
};

int cgroup_rmdir(struct kernfs_node *kn)
{
	struct cgroup *cgrp;
	int ret = 0;

	cgrp = cgroup_kn_lock_live(kn, false);
	if (!cgrp)
		return 0;

	ret = cgroup_destroy_locked(cgrp);
	if (!ret)
		TRACE_CGROUP_PATH(rmdir, cgrp);

	cgroup_kn_unlock(kn);
	return ret;
}

static struct kernfs_syscall_ops cgroup_kf_syscall_ops = {
	.show_options		= cgroup_show_options,
	.remount_fs		= cgroup_remount,
	.mkdir			= cgroup_mkdir,
	.rmdir			= cgroup_rmdir,
	.show_path		= cgroup_show_path,
};

static void __init cgroup_init_subsys(struct cgroup_subsys *ss, bool early)
{
	struct cgroup_subsys_state *css;

	pr_debug("Initializing cgroup subsys %s\n", ss->name);

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

	have_fork_callback |= (bool)ss->fork << ss->id;
	have_exit_callback |= (bool)ss->exit << ss->id;
	have_release_callback |= (bool)ss->release << ss->id;
	have_canfork_callback |= (bool)ss->can_fork << ss->id;

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
	static struct cgroup_sb_opts __initdata opts;
	struct cgroup_subsys *ss;
	int i;

	init_cgroup_root(&cgrp_dfl_root, &opts);
	cgrp_dfl_root.cgrp.self.flags |= CSS_NO_REF;

	RCU_INIT_POINTER(init_task.cgroups, &init_css_set);

	for_each_subsys(ss, i) {
		WARN(!ss->css_alloc || !ss->css_free || ss->name || ss->id,
		     "invalid cgroup_subsys %d:%s css_alloc=%p css_free=%p id:name=%d:%s\n",
		     i, cgroup_subsys_name[i], ss->css_alloc, ss->css_free,
		     ss->id, ss->name);
		WARN(strlen(cgroup_subsys_name[i]) > MAX_CGROUP_TYPE_NAMELEN,
		     "cgroup_subsys_name %s too long\n", cgroup_subsys_name[i]);

		ss->id = i;
		ss->name = cgroup_subsys_name[i];
		if (!ss->legacy_name)
			ss->legacy_name = cgroup_subsys_name[i];

		if (ss->early_init)
			cgroup_init_subsys(ss, true);
	}
	return 0;
}

static u16 cgroup_disable_mask __initdata;

/**
 * cgroup_init - cgroup initialization
 *
 * Register cgroup filesystem and /proc file, and initialize
 * any subsystems that didn't request early init.
 */
int __init cgroup_init(void)
{
	struct cgroup_subsys *ss;
	int ssid;

	BUILD_BUG_ON(CGROUP_SUBSYS_COUNT > 16);
	BUG_ON(percpu_init_rwsem(&cgroup_threadgroup_rwsem));
	BUG_ON(cgroup_init_cftypes(NULL, cgroup_base_files));
	BUG_ON(cgroup_init_cftypes(NULL, cgroup1_base_files));

	cgroup_rstat_boot();

	/*
	 * The latency of the synchronize_sched() is too high for cgroups,
	 * avoid it at the cost of forcing all readers into the slow path.
	 */
	rcu_sync_enter_start(&cgroup_threadgroup_rwsem.rss);

	get_user_ns(init_cgroup_ns.user_ns);

	mutex_lock(&cgroup_mutex);

	/*
	 * Add init_css_set to the hash table so that dfl_root can link to
	 * it during init.
	 */
	hash_add(css_set_table, &init_css_set.hlist,
		 css_set_hash(init_css_set.subsys));

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
		if (cgroup_disable_mask & (1 << ssid)) {
			static_branch_disable(cgroup_subsys_enabled_key[ssid]);
			printk(KERN_INFO "Disabling %s control group subsystem\n",
			       ss->name);
			continue;
		}

		if (cgroup1_ssid_disabled(ssid))
			printk(KERN_INFO "Disabling %s control group subsystem in v1 mounts\n",
			       ss->name);

		cgrp_dfl_root.subsys_mask |= 1 << ss->id;

		/* implicit controllers must be threaded too */
		WARN_ON(ss->implicit_on_dfl && !ss->threaded);

		if (ss->implicit_on_dfl)
			cgrp_dfl_implicit_ss_mask |= 1 << ss->id;
		else if (!ss->dfl_cftypes)
			cgrp_dfl_inhibit_ss_mask |= 1 << ss->id;

		if (ss->threaded)
			cgrp_dfl_threaded_ss_mask |= 1 << ss->id;

		if (ss->dfl_cftypes == ss->legacy_cftypes) {
			WARN_ON(cgroup_add_cftypes(ss, ss->dfl_cftypes));
		} else {
			WARN_ON(cgroup_add_dfl_cftypes(ss, ss->dfl_cftypes));
			WARN_ON(cgroup_add_legacy_cftypes(ss, ss->legacy_cftypes));
		}

		if (ss->bind)
			ss->bind(init_css_set.subsys[ssid]);

		mutex_lock(&cgroup_mutex);
		css_populate_dir(init_css_set.subsys[ssid]);
		mutex_unlock(&cgroup_mutex);
	}

	/* init_css_set.subsys[] has been updated, re-hash */
	hash_del(&init_css_set.hlist);
	hash_add(css_set_table, &init_css_set.hlist,
		 css_set_hash(init_css_set.subsys));

	WARN_ON(sysfs_create_mount_point(fs_kobj, "cgroup"));
	WARN_ON(register_filesystem(&cgroup_fs_type));
	WARN_ON(register_filesystem(&cgroup2_fs_type));
	WARN_ON(!proc_create_single("cgroups", 0, NULL, proc_cgroupstats_show));

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
	return 0;
}
core_initcall(cgroup_wq_init);

void cgroup_path_from_kernfs_id(const union kernfs_node_id *id,
					char *buf, size_t buflen)
{
	struct kernfs_node *kn;

	kn = kernfs_get_node_by_id(cgrp_dfl_root.kf_root, id);
	if (!kn)
		return;
	kernfs_path(kn, buf, buflen);
	kernfs_put(kn);
}

/*
 * proc_cgroup_show()
 *  - Print task's cgroup paths into seq_file, one line for each hierarchy
 *  - Used for /proc/<pid>/cgroup.
 */
int proc_cgroup_show(struct seq_file *m, struct pid_namespace *ns,
		     struct pid *pid, struct task_struct *tsk)
{
	char *buf;
	int retval;
	struct cgroup_root *root;

	retval = -ENOMEM;
	buf = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!buf)
		goto out;

	mutex_lock(&cgroup_mutex);
	spin_lock_irq(&css_set_lock);

	for_each_root(root) {
		struct cgroup_subsys *ss;
		struct cgroup *cgrp;
		int ssid, count = 0;

		if (root == &cgrp_dfl_root && !cgrp_dfl_visible)
			continue;

		seq_printf(m, "%d:", root->hierarchy_id);
		if (root != &cgrp_dfl_root)
			for_each_subsys(ss, ssid)
				if (root->subsys_mask & (1 << ssid))
					seq_printf(m, "%s%s", count++ ? "," : "",
						   ss->legacy_name);
		if (strlen(root->name))
			seq_printf(m, "%sname=%s", count ? "," : "",
				   root->name);
		seq_putc(m, ':');

		cgrp = task_cgroup_from_root(tsk, root);

		/*
		 * On traditional hierarchies, all zombie tasks show up as
		 * belonging to the root cgroup.  On the default hierarchy,
		 * while a zombie doesn't show up in "cgroup.procs" and
		 * thus can't be migrated, its /proc/PID/cgroup keeps
		 * reporting the cgroup it belonged to before exiting.  If
		 * the cgroup is removed before the zombie is reaped,
		 * " (deleted)" is appended to the cgroup path.
		 */
		if (cgroup_on_dfl(cgrp) || !(tsk->flags & PF_EXITING)) {
			retval = cgroup_path_ns_locked(cgrp, buf, PATH_MAX,
						current->nsproxy->cgroup_ns);
			if (retval >= PATH_MAX)
				retval = -ENAMETOOLONG;
			if (retval < 0)
				goto out_unlock;

			seq_puts(m, buf);
		} else {
			seq_puts(m, "/");
		}

		if (cgroup_on_dfl(cgrp) && cgroup_is_dead(cgrp))
			seq_puts(m, " (deleted)\n");
		else
			seq_putc(m, '\n');
	}

	retval = 0;
out_unlock:
	spin_unlock_irq(&css_set_lock);
	mutex_unlock(&cgroup_mutex);
	kfree(buf);
out:
	return retval;
}

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
 * cgroup_can_fork - called on a new task before the process is exposed
 * @child: the task in question.
 *
 * This calls the subsystem can_fork() callbacks. If the can_fork() callback
 * returns an error, the fork aborts with that error code. This allows for
 * a cgroup subsystem to conditionally allow or deny new forks.
 */
int cgroup_can_fork(struct task_struct *child)
{
	struct cgroup_subsys *ss;
	int i, j, ret;

	do_each_subsys_mask(ss, i, have_canfork_callback) {
		ret = ss->can_fork(child);
		if (ret)
			goto out_revert;
	} while_each_subsys_mask();

	return 0;

out_revert:
	for_each_subsys(ss, j) {
		if (j >= i)
			break;
		if (ss->cancel_fork)
			ss->cancel_fork(child);
	}

	return ret;
}

/**
 * cgroup_cancel_fork - called if a fork failed after cgroup_can_fork()
 * @child: the task in question
 *
 * This calls the cancel_fork() callbacks if a fork failed *after*
 * cgroup_can_fork() succeded.
 */
void cgroup_cancel_fork(struct task_struct *child)
{
	struct cgroup_subsys *ss;
	int i;

	for_each_subsys(ss, i)
		if (ss->cancel_fork)
			ss->cancel_fork(child);
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
	 * This may race against cgroup_enable_task_cg_lists().  As that
	 * function sets use_task_css_set_links before grabbing
	 * tasklist_lock and we just went through tasklist_lock to add
	 * @child, it's guaranteed that either we see the set
	 * use_task_css_set_links or cgroup_enable_task_cg_lists() sees
	 * @child during its iteration.
	 *
	 * If we won the race, @child is associated with %current's
	 * css_set.  Grabbing css_set_lock guarantees both that the
	 * association is stable, and, on completion of the parent's
	 * migration, @child is visible in the source of migration or
	 * already in the destination cgroup.  This guarantee is necessary
	 * when implementing operations which need to migrate all tasks of
	 * a cgroup to another.
	 *
	 * Note that if we lose to cgroup_enable_task_cg_lists(), @child
	 * will remain in init_css_set.  This is safe because all tasks are
	 * in the init_css_set before cg_links is enabled and there's no
	 * operation which transfers all tasks out of init_css_set.
	 */
	if (use_task_css_set_links) {
		struct css_set *cset;

		spin_lock_irq(&css_set_lock);
		cset = task_css_set(current);
		if (list_empty(&child->cg_list)) {
			get_css_set(cset);
			cset->nr_tasks++;
			css_set_move_task(child, NULL, cset, false);
		}

		/*
		 * If the cgroup has to be frozen, the new task has too.
		 * Let's set the JOBCTL_TRAP_FREEZE jobctl bit to get
		 * the task into the frozen state.
		 */
		if (unlikely(cgroup_task_freeze(child))) {
			spin_lock(&child->sighand->siglock);
			WARN_ON_ONCE(child->frozen);
			child->jobctl |= JOBCTL_TRAP_FREEZE;
			spin_unlock(&child->sighand->siglock);

			/*
			 * Calling cgroup_update_frozen() isn't required here,
			 * because it will be called anyway a bit later
			 * from do_freezer_trap(). So we avoid cgroup's
			 * transient switch from the frozen state and back.
			 */
		}

		spin_unlock_irq(&css_set_lock);
	}

	/*
	 * Call ss->fork().  This must happen after @child is linked on
	 * css_set; otherwise, @child might change state between ->fork()
	 * and addition to css_set.
	 */
	do_each_subsys_mask(ss, i, have_fork_callback) {
		ss->fork(child);
	} while_each_subsys_mask();
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
	int i;

	/*
	 * Unlink from @tsk from its css_set.  As migration path can't race
	 * with us, we can check css_set and cg_list without synchronization.
	 */
	cset = task_css_set(tsk);

	if (!list_empty(&tsk->cg_list)) {
		spin_lock_irq(&css_set_lock);
		css_set_move_task(tsk, cset, NULL, false);
		list_add_tail(&tsk->cg_list, &cset->dying_tasks);
		cset->nr_tasks--;

		if (unlikely(cgroup_task_frozen(tsk)))
			cgroup_freezer_frozen_exit(tsk);
		else if (unlikely(cgroup_task_freeze(tsk)))
			cgroup_update_frozen(task_dfl_cgroup(tsk));

		spin_unlock_irq(&css_set_lock);
	} else {
		get_css_set(cset);
	}

	/* see cgroup_post_fork() for details */
	do_each_subsys_mask(ss, i, have_exit_callback) {
		ss->exit(tsk);
	} while_each_subsys_mask();
}

void cgroup_release(struct task_struct *task)
{
	struct cgroup_subsys *ss;
	int ssid;

	do_each_subsys_mask(ss, ssid, have_release_callback) {
		ss->release(task);
	} while_each_subsys_mask();

	if (use_task_css_set_links) {
		spin_lock_irq(&css_set_lock);
		css_set_skip_task_iters(task_css_set(task), task);
		list_del_init(&task->cg_list);
		spin_unlock_irq(&css_set_lock);
	}
}

void cgroup_free(struct task_struct *task)
{
	struct css_set *cset = task_css_set(task);
	put_css_set(cset);
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
			if (strcmp(token, ss->name) &&
			    strcmp(token, ss->legacy_name))
				continue;
			cgroup_disable_mask |= 1 << i;
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
	struct file_system_type *s_type = dentry->d_sb->s_type;
	struct cgroup_subsys_state *css = NULL;
	struct cgroup *cgrp;

	/* is @dentry a cgroup dir? */
	if ((s_type != &cgroup_fs_type && s_type != &cgroup2_fs_type) ||
	    !kn || kernfs_type(kn) != KERNFS_DIR)
		return ERR_PTR(-EBADF);

	rcu_read_lock();

	/*
	 * This path doesn't originate from kernfs and @kn could already
	 * have been or be removed at any point.  @kn->priv is RCU
	 * protected for this access.  See css_release_work_fn() for details.
	 */
	cgrp = rcu_dereference(*(void __rcu __force **)&kn->priv);
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

/**
 * cgroup_get_from_path - lookup and get a cgroup from its default hierarchy path
 * @path: path on the default hierarchy
 *
 * Find the cgroup at @path on the default hierarchy, increment its
 * reference count and return it.  Returns pointer to the found cgroup on
 * success, ERR_PTR(-ENOENT) if @path doens't exist and ERR_PTR(-ENOTDIR)
 * if @path points to a non-directory.
 */
struct cgroup *cgroup_get_from_path(const char *path)
{
	struct kernfs_node *kn;
	struct cgroup *cgrp;

	mutex_lock(&cgroup_mutex);

	kn = kernfs_walk_and_get(cgrp_dfl_root.cgrp.kn, path);
	if (kn) {
		if (kernfs_type(kn) == KERNFS_DIR) {
			cgrp = kn->priv;
			cgroup_get_live(cgrp);
		} else {
			cgrp = ERR_PTR(-ENOTDIR);
		}
		kernfs_put(kn);
	} else {
		cgrp = ERR_PTR(-ENOENT);
	}

	mutex_unlock(&cgroup_mutex);
	return cgrp;
}
EXPORT_SYMBOL_GPL(cgroup_get_from_path);

/**
 * cgroup_get_from_fd - get a cgroup pointer from a fd
 * @fd: fd obtained by open(cgroup2_dir)
 *
 * Find the cgroup from a fd which should be obtained
 * by opening a cgroup directory.  Returns a pointer to the
 * cgroup on success. ERR_PTR is returned if the cgroup
 * cannot be found.
 */
struct cgroup *cgroup_get_from_fd(int fd)
{
	struct cgroup_subsys_state *css;
	struct cgroup *cgrp;
	struct file *f;

	f = fget_raw(fd);
	if (!f)
		return ERR_PTR(-EBADF);

	css = css_tryget_online_from_dir(f->f_path.dentry, NULL);
	fput(f);
	if (IS_ERR(css))
		return ERR_CAST(css);

	cgrp = css->cgroup;
	if (!cgroup_on_dfl(cgrp)) {
		cgroup_put(cgrp);
		return ERR_PTR(-EBADF);
	}

	return cgrp;
}
EXPORT_SYMBOL_GPL(cgroup_get_from_fd);

/*
 * sock->sk_cgrp_data handling.  For more info, see sock_cgroup_data
 * definition in cgroup-defs.h.
 */
#ifdef CONFIG_SOCK_CGROUP_DATA

#if defined(CONFIG_CGROUP_NET_PRIO) || defined(CONFIG_CGROUP_NET_CLASSID)

DEFINE_SPINLOCK(cgroup_sk_update_lock);
static bool cgroup_sk_alloc_disabled __read_mostly;

void cgroup_sk_alloc_disable(void)
{
	if (cgroup_sk_alloc_disabled)
		return;
	pr_info("cgroup: disabling cgroup2 socket matching due to net_prio or net_cls activation\n");
	cgroup_sk_alloc_disabled = true;
}

#else

#define cgroup_sk_alloc_disabled	false

#endif

void cgroup_sk_alloc(struct sock_cgroup_data *skcd)
{
	if (cgroup_sk_alloc_disabled) {
		skcd->no_refcnt = 1;
		return;
	}

	/* Don't associate the sock with unrelated interrupted task's cgroup. */
	if (in_interrupt())
		return;

	rcu_read_lock();

	while (true) {
		struct css_set *cset;

		cset = task_css_set(current);
		if (likely(cgroup_tryget(cset->dfl_cgrp))) {
			skcd->val = (unsigned long)cset->dfl_cgrp;
			break;
		}
		cpu_relax();
	}

	rcu_read_unlock();
}

void cgroup_sk_clone(struct sock_cgroup_data *skcd)
{
	/* Socket clone path */
	if (skcd->val) {
		if (skcd->no_refcnt)
			return;
		/*
		 * We might be cloning a socket which is left in an empty
		 * cgroup and the cgroup might have already been rmdir'd.
		 * Don't use cgroup_get_live().
		 */
		cgroup_get(sock_cgroup_ptr(skcd));
	}
}

void cgroup_sk_free(struct sock_cgroup_data *skcd)
{
	if (skcd->no_refcnt)
		return;

	cgroup_put(sock_cgroup_ptr(skcd));
}

#endif	/* CONFIG_SOCK_CGROUP_DATA */

#ifdef CONFIG_CGROUP_BPF
int cgroup_bpf_attach(struct cgroup *cgrp, struct bpf_prog *prog,
		      enum bpf_attach_type type, u32 flags)
{
	int ret;

	mutex_lock(&cgroup_mutex);
	ret = __cgroup_bpf_attach(cgrp, prog, type, flags);
	mutex_unlock(&cgroup_mutex);
	return ret;
}
int cgroup_bpf_detach(struct cgroup *cgrp, struct bpf_prog *prog,
		      enum bpf_attach_type type, u32 flags)
{
	int ret;

	mutex_lock(&cgroup_mutex);
	ret = __cgroup_bpf_detach(cgrp, prog, type, flags);
	mutex_unlock(&cgroup_mutex);
	return ret;
}
int cgroup_bpf_query(struct cgroup *cgrp, const union bpf_attr *attr,
		     union bpf_attr __user *uattr)
{
	int ret;

	mutex_lock(&cgroup_mutex);
	ret = __cgroup_bpf_query(cgrp, attr, uattr);
	mutex_unlock(&cgroup_mutex);
	return ret;
}
#endif /* CONFIG_CGROUP_BPF */

#ifdef CONFIG_SYSFS
static ssize_t show_delegatable_files(struct cftype *files, char *buf,
				      ssize_t size, const char *prefix)
{
	struct cftype *cft;
	ssize_t ret = 0;

	for (cft = files; cft && cft->name[0] != '\0'; cft++) {
		if (!(cft->flags & CFTYPE_NS_DELEGATABLE))
			continue;

		if (prefix)
			ret += snprintf(buf + ret, size - ret, "%s.", prefix);

		ret += snprintf(buf + ret, size - ret, "%s\n", cft->name);

		if (WARN_ON(ret >= size))
			break;
	}

	return ret;
}

static ssize_t delegate_show(struct kobject *kobj, struct kobj_attribute *attr,
			      char *buf)
{
	struct cgroup_subsys *ss;
	int ssid;
	ssize_t ret = 0;

	ret = show_delegatable_files(cgroup_base_files, buf, PAGE_SIZE - ret,
				     NULL);

	for_each_subsys(ss, ssid)
		ret += show_delegatable_files(ss->dfl_cftypes, buf + ret,
					      PAGE_SIZE - ret,
					      cgroup_subsys_name[ssid]);

	return ret;
}
static struct kobj_attribute cgroup_delegate_attr = __ATTR_RO(delegate);

static ssize_t features_show(struct kobject *kobj, struct kobj_attribute *attr,
			     char *buf)
{
	return snprintf(buf, PAGE_SIZE, "nsdelegate\n");
}
static struct kobj_attribute cgroup_features_attr = __ATTR_RO(features);

static struct attribute *cgroup_sysfs_attrs[] = {
	&cgroup_delegate_attr.attr,
	&cgroup_features_attr.attr,
	NULL,
};

static const struct attribute_group cgroup_sysfs_attr_group = {
	.attrs = cgroup_sysfs_attrs,
	.name = "cgroup",
};

static int __init cgroup_sysfs_init(void)
{
	return sysfs_create_group(kernel_kobj, &cgroup_sysfs_attr_group);
}
subsys_initcall(cgroup_sysfs_init);

static u64 power_of_ten(int power)
{
	u64 v = 1;
	while (power--)
		v *= 10;
	return v;
}

/**
 * cgroup_parse_float - parse a floating number
 * @input: input string
 * @dec_shift: number of decimal digits to shift
 * @v: output
 *
 * Parse a decimal floating point number in @input and store the result in
 * @v with decimal point right shifted @dec_shift times.  For example, if
 * @input is "12.3456" and @dec_shift is 3, *@v will be set to 12345.
 * Returns 0 on success, -errno otherwise.
 *
 * There's nothing cgroup specific about this function except that it's
 * currently the only user.
 */
int cgroup_parse_float(const char *input, unsigned dec_shift, s64 *v)
{
	s64 whole, frac = 0;
	int fstart = 0, fend = 0, flen;

	if (!sscanf(input, "%lld.%n%lld%n", &whole, &fstart, &frac, &fend))
		return -EINVAL;
	if (frac < 0)
		return -EINVAL;

	flen = fend > fstart ? fend - fstart : 0;
	if (flen < dec_shift)
		frac *= power_of_ten(dec_shift - flen);
	else
		frac = DIV_ROUND_CLOSEST_ULL(frac, power_of_ten(flen - dec_shift));

	*v = whole * power_of_ten(dec_shift) + frac;
	return 0;
}

#endif /* CONFIG_SYSFS */
