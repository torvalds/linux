/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_CGROUP_H
#define _LINUX_CGROUP_H
/*
 *  cgroup interface
 *
 *  Copyright (C) 2003 BULL SA
 *  Copyright (C) 2004-2006 Silicon Graphics, Inc.
 *
 */

#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/nodemask.h>
#include <linux/rculist.h>
#include <linux/cgroupstats.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/kernfs.h>
#include <linux/jump_label.h>
#include <linux/types.h>
#include <linux/ns_common.h>
#include <linux/nsproxy.h>
#include <linux/user_namespace.h>
#include <linux/refcount.h>
#include <linux/kernel_stat.h>

#include <linux/cgroup-defs.h>

#ifdef CONFIG_CGROUPS

/*
 * All weight knobs on the default hierarhcy should use the following min,
 * default and max values.  The default value is the logarithmic center of
 * MIN and MAX and allows 100x to be expressed in both directions.
 */
#define CGROUP_WEIGHT_MIN		1
#define CGROUP_WEIGHT_DFL		100
#define CGROUP_WEIGHT_MAX		10000

/* walk only threadgroup leaders */
#define CSS_TASK_ITER_PROCS		(1U << 0)
/* walk all threaded css_sets in the domain */
#define CSS_TASK_ITER_THREADED		(1U << 1)

/* internal flags */
#define CSS_TASK_ITER_SKIPPED		(1U << 16)

/* a css_task_iter should be treated as an opaque object */
struct css_task_iter {
	struct cgroup_subsys		*ss;
	unsigned int			flags;

	struct list_head		*cset_pos;
	struct list_head		*cset_head;

	struct list_head		*tcset_pos;
	struct list_head		*tcset_head;

	struct list_head		*task_pos;
	struct list_head		*tasks_head;
	struct list_head		*mg_tasks_head;
	struct list_head		*dying_tasks_head;

	struct css_set			*cur_cset;
	struct css_set			*cur_dcset;
	struct task_struct		*cur_task;
	struct list_head		iters_node;	/* css_set->task_iters */
};

extern struct cgroup_root cgrp_dfl_root;
extern struct css_set init_css_set;

#define SUBSYS(_x) extern struct cgroup_subsys _x ## _cgrp_subsys;
#include <linux/cgroup_subsys.h>
#undef SUBSYS

#define SUBSYS(_x)								\
	extern struct static_key_true _x ## _cgrp_subsys_enabled_key;		\
	extern struct static_key_true _x ## _cgrp_subsys_on_dfl_key;
#include <linux/cgroup_subsys.h>
#undef SUBSYS

/**
 * cgroup_subsys_enabled - fast test on whether a subsys is enabled
 * @ss: subsystem in question
 */
#define cgroup_subsys_enabled(ss)						\
	static_branch_likely(&ss ## _enabled_key)

/**
 * cgroup_subsys_on_dfl - fast test on whether a subsys is on default hierarchy
 * @ss: subsystem in question
 */
#define cgroup_subsys_on_dfl(ss)						\
	static_branch_likely(&ss ## _on_dfl_key)

bool css_has_online_children(struct cgroup_subsys_state *css);
struct cgroup_subsys_state *css_from_id(int id, struct cgroup_subsys *ss);
struct cgroup_subsys_state *cgroup_e_css(struct cgroup *cgroup,
					 struct cgroup_subsys *ss);
struct cgroup_subsys_state *cgroup_get_e_css(struct cgroup *cgroup,
					     struct cgroup_subsys *ss);
struct cgroup_subsys_state *css_tryget_online_from_dir(struct dentry *dentry,
						       struct cgroup_subsys *ss);

struct cgroup *cgroup_get_from_path(const char *path);
struct cgroup *cgroup_get_from_fd(int fd);

int cgroup_attach_task_all(struct task_struct *from, struct task_struct *);
int cgroup_transfer_tasks(struct cgroup *to, struct cgroup *from);

int cgroup_add_dfl_cftypes(struct cgroup_subsys *ss, struct cftype *cfts);
int cgroup_add_legacy_cftypes(struct cgroup_subsys *ss, struct cftype *cfts);
int cgroup_rm_cftypes(struct cftype *cfts);
void cgroup_file_notify(struct cgroup_file *cfile);

int task_cgroup_path(struct task_struct *task, char *buf, size_t buflen);
int cgroupstats_build(struct cgroupstats *stats, struct dentry *dentry);
int proc_cgroup_show(struct seq_file *m, struct pid_namespace *ns,
		     struct pid *pid, struct task_struct *tsk);

void cgroup_fork(struct task_struct *p);
extern int cgroup_can_fork(struct task_struct *p);
extern void cgroup_cancel_fork(struct task_struct *p);
extern void cgroup_post_fork(struct task_struct *p);
void cgroup_exit(struct task_struct *p);
void cgroup_release(struct task_struct *p);
void cgroup_free(struct task_struct *p);

int cgroup_init_early(void);
int cgroup_init(void);

int cgroup_parse_float(const char *input, unsigned dec_shift, s64 *v);

/*
 * Iteration helpers and macros.
 */

struct cgroup_subsys_state *css_next_child(struct cgroup_subsys_state *pos,
					   struct cgroup_subsys_state *parent);
struct cgroup_subsys_state *css_next_descendant_pre(struct cgroup_subsys_state *pos,
						    struct cgroup_subsys_state *css);
struct cgroup_subsys_state *css_rightmost_descendant(struct cgroup_subsys_state *pos);
struct cgroup_subsys_state *css_next_descendant_post(struct cgroup_subsys_state *pos,
						     struct cgroup_subsys_state *css);

struct task_struct *cgroup_taskset_first(struct cgroup_taskset *tset,
					 struct cgroup_subsys_state **dst_cssp);
struct task_struct *cgroup_taskset_next(struct cgroup_taskset *tset,
					struct cgroup_subsys_state **dst_cssp);

void css_task_iter_start(struct cgroup_subsys_state *css, unsigned int flags,
			 struct css_task_iter *it);
struct task_struct *css_task_iter_next(struct css_task_iter *it);
void css_task_iter_end(struct css_task_iter *it);

/**
 * css_for_each_child - iterate through children of a css
 * @pos: the css * to use as the loop cursor
 * @parent: css whose children to walk
 *
 * Walk @parent's children.  Must be called under rcu_read_lock().
 *
 * If a subsystem synchronizes ->css_online() and the start of iteration, a
 * css which finished ->css_online() is guaranteed to be visible in the
 * future iterations and will stay visible until the last reference is put.
 * A css which hasn't finished ->css_online() or already finished
 * ->css_offline() may show up during traversal.  It's each subsystem's
 * responsibility to synchronize against on/offlining.
 *
 * It is allowed to temporarily drop RCU read lock during iteration.  The
 * caller is responsible for ensuring that @pos remains accessible until
 * the start of the next iteration by, for example, bumping the css refcnt.
 */
#define css_for_each_child(pos, parent)					\
	for ((pos) = css_next_child(NULL, (parent)); (pos);		\
	     (pos) = css_next_child((pos), (parent)))

/**
 * css_for_each_descendant_pre - pre-order walk of a css's descendants
 * @pos: the css * to use as the loop cursor
 * @root: css whose descendants to walk
 *
 * Walk @root's descendants.  @root is included in the iteration and the
 * first node to be visited.  Must be called under rcu_read_lock().
 *
 * If a subsystem synchronizes ->css_online() and the start of iteration, a
 * css which finished ->css_online() is guaranteed to be visible in the
 * future iterations and will stay visible until the last reference is put.
 * A css which hasn't finished ->css_online() or already finished
 * ->css_offline() may show up during traversal.  It's each subsystem's
 * responsibility to synchronize against on/offlining.
 *
 * For example, the following guarantees that a descendant can't escape
 * state updates of its ancestors.
 *
 * my_online(@css)
 * {
 *	Lock @css's parent and @css;
 *	Inherit state from the parent;
 *	Unlock both.
 * }
 *
 * my_update_state(@css)
 * {
 *	css_for_each_descendant_pre(@pos, @css) {
 *		Lock @pos;
 *		if (@pos == @css)
 *			Update @css's state;
 *		else
 *			Verify @pos is alive and inherit state from its parent;
 *		Unlock @pos;
 *	}
 * }
 *
 * As long as the inheriting step, including checking the parent state, is
 * enclosed inside @pos locking, double-locking the parent isn't necessary
 * while inheriting.  The state update to the parent is guaranteed to be
 * visible by walking order and, as long as inheriting operations to the
 * same @pos are atomic to each other, multiple updates racing each other
 * still result in the correct state.  It's guaranateed that at least one
 * inheritance happens for any css after the latest update to its parent.
 *
 * If checking parent's state requires locking the parent, each inheriting
 * iteration should lock and unlock both @pos->parent and @pos.
 *
 * Alternatively, a subsystem may choose to use a single global lock to
 * synchronize ->css_online() and ->css_offline() against tree-walking
 * operations.
 *
 * It is allowed to temporarily drop RCU read lock during iteration.  The
 * caller is responsible for ensuring that @pos remains accessible until
 * the start of the next iteration by, for example, bumping the css refcnt.
 */
#define css_for_each_descendant_pre(pos, css)				\
	for ((pos) = css_next_descendant_pre(NULL, (css)); (pos);	\
	     (pos) = css_next_descendant_pre((pos), (css)))

/**
 * css_for_each_descendant_post - post-order walk of a css's descendants
 * @pos: the css * to use as the loop cursor
 * @css: css whose descendants to walk
 *
 * Similar to css_for_each_descendant_pre() but performs post-order
 * traversal instead.  @root is included in the iteration and the last
 * node to be visited.
 *
 * If a subsystem synchronizes ->css_online() and the start of iteration, a
 * css which finished ->css_online() is guaranteed to be visible in the
 * future iterations and will stay visible until the last reference is put.
 * A css which hasn't finished ->css_online() or already finished
 * ->css_offline() may show up during traversal.  It's each subsystem's
 * responsibility to synchronize against on/offlining.
 *
 * Note that the walk visibility guarantee example described in pre-order
 * walk doesn't apply the same to post-order walks.
 */
#define css_for_each_descendant_post(pos, css)				\
	for ((pos) = css_next_descendant_post(NULL, (css)); (pos);	\
	     (pos) = css_next_descendant_post((pos), (css)))

/**
 * cgroup_taskset_for_each - iterate cgroup_taskset
 * @task: the loop cursor
 * @dst_css: the destination css
 * @tset: taskset to iterate
 *
 * @tset may contain multiple tasks and they may belong to multiple
 * processes.
 *
 * On the v2 hierarchy, there may be tasks from multiple processes and they
 * may not share the source or destination csses.
 *
 * On traditional hierarchies, when there are multiple tasks in @tset, if a
 * task of a process is in @tset, all tasks of the process are in @tset.
 * Also, all are guaranteed to share the same source and destination csses.
 *
 * Iteration is not in any specific order.
 */
#define cgroup_taskset_for_each(task, dst_css, tset)			\
	for ((task) = cgroup_taskset_first((tset), &(dst_css));		\
	     (task);							\
	     (task) = cgroup_taskset_next((tset), &(dst_css)))

/**
 * cgroup_taskset_for_each_leader - iterate group leaders in a cgroup_taskset
 * @leader: the loop cursor
 * @dst_css: the destination css
 * @tset: taskset to iterate
 *
 * Iterate threadgroup leaders of @tset.  For single-task migrations, @tset
 * may not contain any.
 */
#define cgroup_taskset_for_each_leader(leader, dst_css, tset)		\
	for ((leader) = cgroup_taskset_first((tset), &(dst_css));	\
	     (leader);							\
	     (leader) = cgroup_taskset_next((tset), &(dst_css)))	\
		if ((leader) != (leader)->group_leader)			\
			;						\
		else

/*
 * Inline functions.
 */

/**
 * css_get - obtain a reference on the specified css
 * @css: target css
 *
 * The caller must already have a reference.
 */
static inline void css_get(struct cgroup_subsys_state *css)
{
	if (!(css->flags & CSS_NO_REF))
		percpu_ref_get(&css->refcnt);
}

/**
 * css_get_many - obtain references on the specified css
 * @css: target css
 * @n: number of references to get
 *
 * The caller must already have a reference.
 */
static inline void css_get_many(struct cgroup_subsys_state *css, unsigned int n)
{
	if (!(css->flags & CSS_NO_REF))
		percpu_ref_get_many(&css->refcnt, n);
}

/**
 * css_tryget - try to obtain a reference on the specified css
 * @css: target css
 *
 * Obtain a reference on @css unless it already has reached zero and is
 * being released.  This function doesn't care whether @css is on or
 * offline.  The caller naturally needs to ensure that @css is accessible
 * but doesn't have to be holding a reference on it - IOW, RCU protected
 * access is good enough for this function.  Returns %true if a reference
 * count was successfully obtained; %false otherwise.
 */
static inline bool css_tryget(struct cgroup_subsys_state *css)
{
	if (!(css->flags & CSS_NO_REF))
		return percpu_ref_tryget(&css->refcnt);
	return true;
}

/**
 * css_tryget_online - try to obtain a reference on the specified css if online
 * @css: target css
 *
 * Obtain a reference on @css if it's online.  The caller naturally needs
 * to ensure that @css is accessible but doesn't have to be holding a
 * reference on it - IOW, RCU protected access is good enough for this
 * function.  Returns %true if a reference count was successfully obtained;
 * %false otherwise.
 */
static inline bool css_tryget_online(struct cgroup_subsys_state *css)
{
	if (!(css->flags & CSS_NO_REF))
		return percpu_ref_tryget_live(&css->refcnt);
	return true;
}

/**
 * css_is_dying - test whether the specified css is dying
 * @css: target css
 *
 * Test whether @css is in the process of offlining or already offline.  In
 * most cases, ->css_online() and ->css_offline() callbacks should be
 * enough; however, the actual offline operations are RCU delayed and this
 * test returns %true also when @css is scheduled to be offlined.
 *
 * This is useful, for example, when the use case requires synchronous
 * behavior with respect to cgroup removal.  cgroup removal schedules css
 * offlining but the css can seem alive while the operation is being
 * delayed.  If the delay affects user visible semantics, this test can be
 * used to resolve the situation.
 */
static inline bool css_is_dying(struct cgroup_subsys_state *css)
{
	return !(css->flags & CSS_NO_REF) && percpu_ref_is_dying(&css->refcnt);
}

/**
 * css_put - put a css reference
 * @css: target css
 *
 * Put a reference obtained via css_get() and css_tryget_online().
 */
static inline void css_put(struct cgroup_subsys_state *css)
{
	if (!(css->flags & CSS_NO_REF))
		percpu_ref_put(&css->refcnt);
}

/**
 * css_put_many - put css references
 * @css: target css
 * @n: number of references to put
 *
 * Put references obtained via css_get() and css_tryget_online().
 */
static inline void css_put_many(struct cgroup_subsys_state *css, unsigned int n)
{
	if (!(css->flags & CSS_NO_REF))
		percpu_ref_put_many(&css->refcnt, n);
}

static inline void cgroup_get(struct cgroup *cgrp)
{
	css_get(&cgrp->self);
}

static inline bool cgroup_tryget(struct cgroup *cgrp)
{
	return css_tryget(&cgrp->self);
}

static inline void cgroup_put(struct cgroup *cgrp)
{
	css_put(&cgrp->self);
}

/**
 * task_css_set_check - obtain a task's css_set with extra access conditions
 * @task: the task to obtain css_set for
 * @__c: extra condition expression to be passed to rcu_dereference_check()
 *
 * A task's css_set is RCU protected, initialized and exited while holding
 * task_lock(), and can only be modified while holding both cgroup_mutex
 * and task_lock() while the task is alive.  This macro verifies that the
 * caller is inside proper critical section and returns @task's css_set.
 *
 * The caller can also specify additional allowed conditions via @__c, such
 * as locks used during the cgroup_subsys::attach() methods.
 */
#ifdef CONFIG_PROVE_RCU
extern struct mutex cgroup_mutex;
extern spinlock_t css_set_lock;
#define task_css_set_check(task, __c)					\
	rcu_dereference_check((task)->cgroups,				\
		lockdep_is_held(&cgroup_mutex) ||			\
		lockdep_is_held(&css_set_lock) ||			\
		((task)->flags & PF_EXITING) || (__c))
#else
#define task_css_set_check(task, __c)					\
	rcu_dereference((task)->cgroups)
#endif

/**
 * task_css_check - obtain css for (task, subsys) w/ extra access conds
 * @task: the target task
 * @subsys_id: the target subsystem ID
 * @__c: extra condition expression to be passed to rcu_dereference_check()
 *
 * Return the cgroup_subsys_state for the (@task, @subsys_id) pair.  The
 * synchronization rules are the same as task_css_set_check().
 */
#define task_css_check(task, subsys_id, __c)				\
	task_css_set_check((task), (__c))->subsys[(subsys_id)]

/**
 * task_css_set - obtain a task's css_set
 * @task: the task to obtain css_set for
 *
 * See task_css_set_check().
 */
static inline struct css_set *task_css_set(struct task_struct *task)
{
	return task_css_set_check(task, false);
}

/**
 * task_css - obtain css for (task, subsys)
 * @task: the target task
 * @subsys_id: the target subsystem ID
 *
 * See task_css_check().
 */
static inline struct cgroup_subsys_state *task_css(struct task_struct *task,
						   int subsys_id)
{
	return task_css_check(task, subsys_id, false);
}

/**
 * task_get_css - find and get the css for (task, subsys)
 * @task: the target task
 * @subsys_id: the target subsystem ID
 *
 * Find the css for the (@task, @subsys_id) combination, increment a
 * reference on and return it.  This function is guaranteed to return a
 * valid css.  The returned css may already have been offlined.
 */
static inline struct cgroup_subsys_state *
task_get_css(struct task_struct *task, int subsys_id)
{
	struct cgroup_subsys_state *css;

	rcu_read_lock();
	while (true) {
		css = task_css(task, subsys_id);
		/*
		 * Can't use css_tryget_online() here.  A task which has
		 * PF_EXITING set may stay associated with an offline css.
		 * If such task calls this function, css_tryget_online()
		 * will keep failing.
		 */
		if (likely(css_tryget(css)))
			break;
		cpu_relax();
	}
	rcu_read_unlock();
	return css;
}

/**
 * task_css_is_root - test whether a task belongs to the root css
 * @task: the target task
 * @subsys_id: the target subsystem ID
 *
 * Test whether @task belongs to the root css on the specified subsystem.
 * May be invoked in any context.
 */
static inline bool task_css_is_root(struct task_struct *task, int subsys_id)
{
	return task_css_check(task, subsys_id, true) ==
		init_css_set.subsys[subsys_id];
}

static inline struct cgroup *task_cgroup(struct task_struct *task,
					 int subsys_id)
{
	return task_css(task, subsys_id)->cgroup;
}

static inline struct cgroup *task_dfl_cgroup(struct task_struct *task)
{
	return task_css_set(task)->dfl_cgrp;
}

static inline struct cgroup *cgroup_parent(struct cgroup *cgrp)
{
	struct cgroup_subsys_state *parent_css = cgrp->self.parent;

	if (parent_css)
		return container_of(parent_css, struct cgroup, self);
	return NULL;
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
static inline bool cgroup_is_descendant(struct cgroup *cgrp,
					struct cgroup *ancestor)
{
	if (cgrp->root != ancestor->root || cgrp->level < ancestor->level)
		return false;
	return cgrp->ancestor_ids[ancestor->level] == ancestor->id;
}

/**
 * cgroup_ancestor - find ancestor of cgroup
 * @cgrp: cgroup to find ancestor of
 * @ancestor_level: level of ancestor to find starting from root
 *
 * Find ancestor of cgroup at specified level starting from root if it exists
 * and return pointer to it. Return NULL if @cgrp doesn't have ancestor at
 * @ancestor_level.
 *
 * This function is safe to call as long as @cgrp is accessible.
 */
static inline struct cgroup *cgroup_ancestor(struct cgroup *cgrp,
					     int ancestor_level)
{
	if (cgrp->level < ancestor_level)
		return NULL;
	while (cgrp && cgrp->level > ancestor_level)
		cgrp = cgroup_parent(cgrp);
	return cgrp;
}

/**
 * task_under_cgroup_hierarchy - test task's membership of cgroup ancestry
 * @task: the task to be tested
 * @ancestor: possible ancestor of @task's cgroup
 *
 * Tests whether @task's default cgroup hierarchy is a descendant of @ancestor.
 * It follows all the same rules as cgroup_is_descendant, and only applies
 * to the default hierarchy.
 */
static inline bool task_under_cgroup_hierarchy(struct task_struct *task,
					       struct cgroup *ancestor)
{
	struct css_set *cset = task_css_set(task);

	return cgroup_is_descendant(cset->dfl_cgrp, ancestor);
}

/* no synchronization, the result can only be used as a hint */
static inline bool cgroup_is_populated(struct cgroup *cgrp)
{
	return cgrp->nr_populated_csets + cgrp->nr_populated_domain_children +
		cgrp->nr_populated_threaded_children;
}

/* returns ino associated with a cgroup */
static inline ino_t cgroup_ino(struct cgroup *cgrp)
{
	return cgrp->kn->id.ino;
}

/* cft/css accessors for cftype->write() operation */
static inline struct cftype *of_cft(struct kernfs_open_file *of)
{
	return of->kn->priv;
}

struct cgroup_subsys_state *of_css(struct kernfs_open_file *of);

/* cft/css accessors for cftype->seq_*() operations */
static inline struct cftype *seq_cft(struct seq_file *seq)
{
	return of_cft(seq->private);
}

static inline struct cgroup_subsys_state *seq_css(struct seq_file *seq)
{
	return of_css(seq->private);
}

/*
 * Name / path handling functions.  All are thin wrappers around the kernfs
 * counterparts and can be called under any context.
 */

static inline int cgroup_name(struct cgroup *cgrp, char *buf, size_t buflen)
{
	return kernfs_name(cgrp->kn, buf, buflen);
}

static inline int cgroup_path(struct cgroup *cgrp, char *buf, size_t buflen)
{
	return kernfs_path(cgrp->kn, buf, buflen);
}

static inline void pr_cont_cgroup_name(struct cgroup *cgrp)
{
	pr_cont_kernfs_name(cgrp->kn);
}

static inline void pr_cont_cgroup_path(struct cgroup *cgrp)
{
	pr_cont_kernfs_path(cgrp->kn);
}

static inline struct psi_group *cgroup_psi(struct cgroup *cgrp)
{
	return &cgrp->psi;
}

static inline void cgroup_init_kthreadd(void)
{
	/*
	 * kthreadd is inherited by all kthreads, keep it in the root so
	 * that the new kthreads are guaranteed to stay in the root until
	 * initialization is finished.
	 */
	current->no_cgroup_migration = 1;
}

static inline void cgroup_kthread_ready(void)
{
	/*
	 * This kthread finished initialization.  The creator should have
	 * set PF_NO_SETAFFINITY if this kthread should stay in the root.
	 */
	current->no_cgroup_migration = 0;
}

static inline union kernfs_node_id *cgroup_get_kernfs_id(struct cgroup *cgrp)
{
	return &cgrp->kn->id;
}

void cgroup_path_from_kernfs_id(const union kernfs_node_id *id,
					char *buf, size_t buflen);
#else /* !CONFIG_CGROUPS */

struct cgroup_subsys_state;
struct cgroup;

static inline void css_put(struct cgroup_subsys_state *css) {}
static inline int cgroup_attach_task_all(struct task_struct *from,
					 struct task_struct *t) { return 0; }
static inline int cgroupstats_build(struct cgroupstats *stats,
				    struct dentry *dentry) { return -EINVAL; }

static inline void cgroup_fork(struct task_struct *p) {}
static inline int cgroup_can_fork(struct task_struct *p) { return 0; }
static inline void cgroup_cancel_fork(struct task_struct *p) {}
static inline void cgroup_post_fork(struct task_struct *p) {}
static inline void cgroup_exit(struct task_struct *p) {}
static inline void cgroup_release(struct task_struct *p) {}
static inline void cgroup_free(struct task_struct *p) {}

static inline int cgroup_init_early(void) { return 0; }
static inline int cgroup_init(void) { return 0; }
static inline void cgroup_init_kthreadd(void) {}
static inline void cgroup_kthread_ready(void) {}
static inline union kernfs_node_id *cgroup_get_kernfs_id(struct cgroup *cgrp)
{
	return NULL;
}

static inline struct cgroup *cgroup_parent(struct cgroup *cgrp)
{
	return NULL;
}

static inline struct psi_group *cgroup_psi(struct cgroup *cgrp)
{
	return NULL;
}

static inline bool task_under_cgroup_hierarchy(struct task_struct *task,
					       struct cgroup *ancestor)
{
	return true;
}

static inline void cgroup_path_from_kernfs_id(const union kernfs_node_id *id,
	char *buf, size_t buflen) {}
#endif /* !CONFIG_CGROUPS */

#ifdef CONFIG_CGROUPS
/*
 * cgroup scalable recursive statistics.
 */
void cgroup_rstat_updated(struct cgroup *cgrp, int cpu);
void cgroup_rstat_flush(struct cgroup *cgrp);
void cgroup_rstat_flush_irqsafe(struct cgroup *cgrp);
void cgroup_rstat_flush_hold(struct cgroup *cgrp);
void cgroup_rstat_flush_release(void);

/*
 * Basic resource stats.
 */
#ifdef CONFIG_CGROUP_CPUACCT
void cpuacct_charge(struct task_struct *tsk, u64 cputime);
void cpuacct_account_field(struct task_struct *tsk, int index, u64 val);
#else
static inline void cpuacct_charge(struct task_struct *tsk, u64 cputime) {}
static inline void cpuacct_account_field(struct task_struct *tsk, int index,
					 u64 val) {}
#endif

void __cgroup_account_cputime(struct cgroup *cgrp, u64 delta_exec);
void __cgroup_account_cputime_field(struct cgroup *cgrp,
				    enum cpu_usage_stat index, u64 delta_exec);

static inline void cgroup_account_cputime(struct task_struct *task,
					  u64 delta_exec)
{
	struct cgroup *cgrp;

	cpuacct_charge(task, delta_exec);

	rcu_read_lock();
	cgrp = task_dfl_cgroup(task);
	if (cgroup_parent(cgrp))
		__cgroup_account_cputime(cgrp, delta_exec);
	rcu_read_unlock();
}

static inline void cgroup_account_cputime_field(struct task_struct *task,
						enum cpu_usage_stat index,
						u64 delta_exec)
{
	struct cgroup *cgrp;

	cpuacct_account_field(task, index, delta_exec);

	rcu_read_lock();
	cgrp = task_dfl_cgroup(task);
	if (cgroup_parent(cgrp))
		__cgroup_account_cputime_field(cgrp, index, delta_exec);
	rcu_read_unlock();
}

#else	/* CONFIG_CGROUPS */

static inline void cgroup_account_cputime(struct task_struct *task,
					  u64 delta_exec) {}
static inline void cgroup_account_cputime_field(struct task_struct *task,
						enum cpu_usage_stat index,
						u64 delta_exec) {}

#endif	/* CONFIG_CGROUPS */

/*
 * sock->sk_cgrp_data handling.  For more info, see sock_cgroup_data
 * definition in cgroup-defs.h.
 */
#ifdef CONFIG_SOCK_CGROUP_DATA

#if defined(CONFIG_CGROUP_NET_PRIO) || defined(CONFIG_CGROUP_NET_CLASSID)
extern spinlock_t cgroup_sk_update_lock;
#endif

void cgroup_sk_alloc_disable(void);
void cgroup_sk_alloc(struct sock_cgroup_data *skcd);
void cgroup_sk_free(struct sock_cgroup_data *skcd);

static inline struct cgroup *sock_cgroup_ptr(struct sock_cgroup_data *skcd)
{
#if defined(CONFIG_CGROUP_NET_PRIO) || defined(CONFIG_CGROUP_NET_CLASSID)
	unsigned long v;

	/*
	 * @skcd->val is 64bit but the following is safe on 32bit too as we
	 * just need the lower ulong to be written and read atomically.
	 */
	v = READ_ONCE(skcd->val);

	if (v & 1)
		return &cgrp_dfl_root.cgrp;

	return (struct cgroup *)(unsigned long)v ?: &cgrp_dfl_root.cgrp;
#else
	return (struct cgroup *)(unsigned long)skcd->val;
#endif
}

#else	/* CONFIG_CGROUP_DATA */

static inline void cgroup_sk_alloc(struct sock_cgroup_data *skcd) {}
static inline void cgroup_sk_free(struct sock_cgroup_data *skcd) {}

#endif	/* CONFIG_CGROUP_DATA */

struct cgroup_namespace {
	refcount_t		count;
	struct ns_common	ns;
	struct user_namespace	*user_ns;
	struct ucounts		*ucounts;
	struct css_set          *root_cset;
};

extern struct cgroup_namespace init_cgroup_ns;

#ifdef CONFIG_CGROUPS

void free_cgroup_ns(struct cgroup_namespace *ns);

struct cgroup_namespace *copy_cgroup_ns(unsigned long flags,
					struct user_namespace *user_ns,
					struct cgroup_namespace *old_ns);

int cgroup_path_ns(struct cgroup *cgrp, char *buf, size_t buflen,
		   struct cgroup_namespace *ns);

#else /* !CONFIG_CGROUPS */

static inline void free_cgroup_ns(struct cgroup_namespace *ns) { }
static inline struct cgroup_namespace *
copy_cgroup_ns(unsigned long flags, struct user_namespace *user_ns,
	       struct cgroup_namespace *old_ns)
{
	return old_ns;
}

#endif /* !CONFIG_CGROUPS */

static inline void get_cgroup_ns(struct cgroup_namespace *ns)
{
	if (ns)
		refcount_inc(&ns->count);
}

static inline void put_cgroup_ns(struct cgroup_namespace *ns)
{
	if (ns && refcount_dec_and_test(&ns->count))
		free_cgroup_ns(ns);
}

#ifdef CONFIG_CGROUPS

void cgroup_enter_frozen(void);
void cgroup_leave_frozen(bool always_leave);
void cgroup_update_frozen(struct cgroup *cgrp);
void cgroup_freeze(struct cgroup *cgrp, bool freeze);
void cgroup_freezer_migrate_task(struct task_struct *task, struct cgroup *src,
				 struct cgroup *dst);

static inline bool cgroup_task_freeze(struct task_struct *task)
{
	bool ret;

	if (task->flags & PF_KTHREAD)
		return false;

	rcu_read_lock();
	ret = test_bit(CGRP_FREEZE, &task_dfl_cgroup(task)->flags);
	rcu_read_unlock();

	return ret;
}

static inline bool cgroup_task_frozen(struct task_struct *task)
{
	return task->frozen;
}

#else /* !CONFIG_CGROUPS */

static inline void cgroup_enter_frozen(void) { }
static inline void cgroup_leave_frozen(bool always_leave) { }
static inline bool cgroup_task_freeze(struct task_struct *task)
{
	return false;
}
static inline bool cgroup_task_frozen(struct task_struct *task)
{
	return false;
}

#endif /* !CONFIG_CGROUPS */

#endif /* _LINUX_CGROUP_H */
