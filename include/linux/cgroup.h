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
#include <linux/rcupdate.h>
#include <linux/rculist.h>
#include <linux/cgroupstats.h>
#include <linux/prio_heap.h>
#include <linux/rwsem.h>
#include <linux/idr.h>
#include <linux/workqueue.h>
#include <linux/xattr.h>
#include <linux/fs.h>

#ifdef CONFIG_CGROUPS

struct cgroupfs_root;
struct cgroup_subsys;
struct inode;
struct cgroup;
struct css_id;
struct eventfd_ctx;

extern int cgroup_init_early(void);
extern int cgroup_init(void);
extern void cgroup_fork(struct task_struct *p);
extern void cgroup_post_fork(struct task_struct *p);
extern void cgroup_exit(struct task_struct *p, int run_callbacks);
extern int cgroupstats_build(struct cgroupstats *stats,
				struct dentry *dentry);
extern int cgroup_load_subsys(struct cgroup_subsys *ss);
extern void cgroup_unload_subsys(struct cgroup_subsys *ss);

extern int proc_cgroup_show(struct seq_file *, void *);

/*
 * Define the enumeration of all cgroup subsystems.
 *
 * We define ids for builtin subsystems and then modular ones.
 */
#define SUBSYS(_x) _x ## _subsys_id,
enum cgroup_subsys_id {
#define IS_SUBSYS_ENABLED(option) IS_BUILTIN(option)
#include <linux/cgroup_subsys.h>
#undef IS_SUBSYS_ENABLED
	CGROUP_BUILTIN_SUBSYS_COUNT,

	__CGROUP_SUBSYS_TEMP_PLACEHOLDER = CGROUP_BUILTIN_SUBSYS_COUNT - 1,

#define IS_SUBSYS_ENABLED(option) IS_MODULE(option)
#include <linux/cgroup_subsys.h>
#undef IS_SUBSYS_ENABLED
	CGROUP_SUBSYS_COUNT,
};
#undef SUBSYS

/* Per-subsystem/per-cgroup state maintained by the system. */
struct cgroup_subsys_state {
	/*
	 * The cgroup that this subsystem is attached to. Useful
	 * for subsystems that want to know about the cgroup
	 * hierarchy structure
	 */
	struct cgroup *cgroup;

	/*
	 * State maintained by the cgroup system to allow subsystems
	 * to be "busy". Should be accessed via css_get(),
	 * css_tryget() and css_put().
	 */

	atomic_t refcnt;

	unsigned long flags;
	/* ID for this css, if possible */
	struct css_id __rcu *id;

	/* Used to put @cgroup->dentry on the last css_put() */
	struct work_struct dput_work;
};

/* bits in struct cgroup_subsys_state flags field */
enum {
	CSS_ROOT	= (1 << 0), /* this CSS is the root of the subsystem */
	CSS_ONLINE	= (1 << 1), /* between ->css_online() and ->css_offline() */
};

/* Caller must verify that the css is not for root cgroup */
static inline void __css_get(struct cgroup_subsys_state *css, int count)
{
	atomic_add(count, &css->refcnt);
}

/*
 * Call css_get() to hold a reference on the css; it can be used
 * for a reference obtained via:
 * - an existing ref-counted reference to the css
 * - task->cgroups for a locked task
 */

static inline void css_get(struct cgroup_subsys_state *css)
{
	/* We don't need to reference count the root state */
	if (!(css->flags & CSS_ROOT))
		__css_get(css, 1);
}

/*
 * Call css_tryget() to take a reference on a css if your existing
 * (known-valid) reference isn't already ref-counted. Returns false if
 * the css has been destroyed.
 */

extern bool __css_tryget(struct cgroup_subsys_state *css);
static inline bool css_tryget(struct cgroup_subsys_state *css)
{
	if (css->flags & CSS_ROOT)
		return true;
	return __css_tryget(css);
}

/*
 * css_put() should be called to release a reference taken by
 * css_get() or css_tryget()
 */

extern void __css_put(struct cgroup_subsys_state *css);
static inline void css_put(struct cgroup_subsys_state *css)
{
	if (!(css->flags & CSS_ROOT))
		__css_put(css);
}

/* bits in struct cgroup flags field */
enum {
	/* Control Group is dead */
	CGRP_REMOVED,
	/*
	 * Control Group has previously had a child cgroup or a task,
	 * but no longer (only if CGRP_NOTIFY_ON_RELEASE is set)
	 */
	CGRP_RELEASABLE,
	/* Control Group requires release notifications to userspace */
	CGRP_NOTIFY_ON_RELEASE,
	/*
	 * Clone the parent's configuration when creating a new child
	 * cpuset cgroup.  For historical reasons, this option can be
	 * specified at mount time and thus is implemented here.
	 */
	CGRP_CPUSET_CLONE_CHILDREN,
	/* see the comment above CGRP_ROOT_SANE_BEHAVIOR for details */
	CGRP_SANE_BEHAVIOR,
};

struct cgroup_name {
	struct rcu_head rcu_head;
	char name[];
};

struct cgroup {
	unsigned long flags;		/* "unsigned long" so bitops work */

	/*
	 * count users of this cgroup. >0 means busy, but doesn't
	 * necessarily indicate the number of tasks in the cgroup
	 */
	atomic_t count;

	int id;				/* ida allocated in-hierarchy ID */

	/*
	 * We link our 'sibling' struct into our parent's 'children'.
	 * Our children link their 'sibling' into our 'children'.
	 */
	struct list_head sibling;	/* my parent's children */
	struct list_head children;	/* my children */
	struct list_head files;		/* my files */

	struct cgroup *parent;		/* my parent */
	struct dentry *dentry;		/* cgroup fs entry, RCU protected */

	/*
	 * This is a copy of dentry->d_name, and it's needed because
	 * we can't use dentry->d_name in cgroup_path().
	 *
	 * You must acquire rcu_read_lock() to access cgrp->name, and
	 * the only place that can change it is rename(), which is
	 * protected by parent dir's i_mutex.
	 *
	 * Normally you should use cgroup_name() wrapper rather than
	 * access it directly.
	 */
	struct cgroup_name __rcu *name;

	/* Private pointers for each registered subsystem */
	struct cgroup_subsys_state *subsys[CGROUP_SUBSYS_COUNT];

	struct cgroupfs_root *root;

	/*
	 * List of cg_cgroup_links pointing at css_sets with
	 * tasks in this cgroup. Protected by css_set_lock
	 */
	struct list_head css_sets;

	struct list_head allcg_node;	/* cgroupfs_root->allcg_list */
	struct list_head cft_q_node;	/* used during cftype add/rm */

	/*
	 * Linked list running through all cgroups that can
	 * potentially be reaped by the release agent. Protected by
	 * release_list_lock
	 */
	struct list_head release_list;

	/*
	 * list of pidlists, up to two for each namespace (one for procs, one
	 * for tasks); created on demand.
	 */
	struct list_head pidlists;
	struct mutex pidlist_mutex;

	/* For RCU-protected deletion */
	struct rcu_head rcu_head;
	struct work_struct free_work;

	/* List of events which userspace want to receive */
	struct list_head event_list;
	spinlock_t event_list_lock;

	/* directory xattrs */
	struct simple_xattrs xattrs;
};

#define MAX_CGROUP_ROOT_NAMELEN 64

/* cgroupfs_root->flags */
enum {
	/*
	 * Unfortunately, cgroup core and various controllers are riddled
	 * with idiosyncrasies and pointless options.  The following flag,
	 * when set, will force sane behavior - some options are forced on,
	 * others are disallowed, and some controllers will change their
	 * hierarchical or other behaviors.
	 *
	 * The set of behaviors affected by this flag are still being
	 * determined and developed and the mount option for this flag is
	 * prefixed with __DEVEL__.  The prefix will be dropped once we
	 * reach the point where all behaviors are compatible with the
	 * planned unified hierarchy, which will automatically turn on this
	 * flag.
	 *
	 * The followings are the behaviors currently affected this flag.
	 *
	 * - Mount options "noprefix" and "clone_children" are disallowed.
	 *   Also, cgroupfs file cgroup.clone_children is not created.
	 *
	 * - When mounting an existing superblock, mount options should
	 *   match.
	 *
	 * - Remount is disallowed.
	 *
	 * - memcg: use_hierarchy is on by default and the cgroup file for
	 *   the flag is not created.
	 *
	 * The followings are planned changes.
	 *
	 * - release_agent will be disallowed once replacement notification
	 *   mechanism is implemented.
	 */
	CGRP_ROOT_SANE_BEHAVIOR	= (1 << 0),

	CGRP_ROOT_NOPREFIX	= (1 << 1), /* mounted subsystems have no named prefix */
	CGRP_ROOT_XATTR		= (1 << 2), /* supports extended attributes */
};

/*
 * A cgroupfs_root represents the root of a cgroup hierarchy, and may be
 * associated with a superblock to form an active hierarchy.  This is
 * internal to cgroup core.  Don't access directly from controllers.
 */
struct cgroupfs_root {
	struct super_block *sb;

	/*
	 * The bitmask of subsystems intended to be attached to this
	 * hierarchy
	 */
	unsigned long subsys_mask;

	/* Unique id for this hierarchy. */
	int hierarchy_id;

	/* The bitmask of subsystems currently attached to this hierarchy */
	unsigned long actual_subsys_mask;

	/* A list running through the attached subsystems */
	struct list_head subsys_list;

	/* The root cgroup for this hierarchy */
	struct cgroup top_cgroup;

	/* Tracks how many cgroups are currently defined in hierarchy.*/
	int number_of_cgroups;

	/* A list running through the active hierarchies */
	struct list_head root_list;

	/* All cgroups on this root, cgroup_mutex protected */
	struct list_head allcg_list;

	/* Hierarchy-specific flags */
	unsigned long flags;

	/* IDs for cgroups in this hierarchy */
	struct ida cgroup_ida;

	/* The path to use for release notifications. */
	char release_agent_path[PATH_MAX];

	/* The name for this hierarchy - may be empty */
	char name[MAX_CGROUP_ROOT_NAMELEN];
};

/*
 * A css_set is a structure holding pointers to a set of
 * cgroup_subsys_state objects. This saves space in the task struct
 * object and speeds up fork()/exit(), since a single inc/dec and a
 * list_add()/del() can bump the reference count on the entire cgroup
 * set for a task.
 */

struct css_set {

	/* Reference count */
	atomic_t refcount;

	/*
	 * List running through all cgroup groups in the same hash
	 * slot. Protected by css_set_lock
	 */
	struct hlist_node hlist;

	/*
	 * List running through all tasks using this cgroup
	 * group. Protected by css_set_lock
	 */
	struct list_head tasks;

	/*
	 * List of cg_cgroup_link objects on link chains from
	 * cgroups referenced from this css_set. Protected by
	 * css_set_lock
	 */
	struct list_head cg_links;

	/*
	 * Set of subsystem states, one for each subsystem. This array
	 * is immutable after creation apart from the init_css_set
	 * during subsystem registration (at boot time) and modular subsystem
	 * loading/unloading.
	 */
	struct cgroup_subsys_state *subsys[CGROUP_SUBSYS_COUNT];

	/* For RCU-protected deletion */
	struct rcu_head rcu_head;
};

/*
 * cgroup_map_cb is an abstract callback API for reporting map-valued
 * control files
 */

struct cgroup_map_cb {
	int (*fill)(struct cgroup_map_cb *cb, const char *key, u64 value);
	void *state;
};

/*
 * struct cftype: handler definitions for cgroup control files
 *
 * When reading/writing to a file:
 *	- the cgroup to use is file->f_dentry->d_parent->d_fsdata
 *	- the 'cftype' of the file is file->f_dentry->d_fsdata
 */

/* cftype->flags */
#define CFTYPE_ONLY_ON_ROOT	(1U << 0)	/* only create on root cg */
#define CFTYPE_NOT_ON_ROOT	(1U << 1)	/* don't create on root cg */
#define CFTYPE_INSANE		(1U << 2)	/* don't create if sane_behavior */

#define MAX_CFTYPE_NAME		64

struct cftype {
	/*
	 * By convention, the name should begin with the name of the
	 * subsystem, followed by a period.  Zero length string indicates
	 * end of cftype array.
	 */
	char name[MAX_CFTYPE_NAME];
	int private;
	/*
	 * If not 0, file mode is set to this value, otherwise it will
	 * be figured out automatically
	 */
	umode_t mode;

	/*
	 * If non-zero, defines the maximum length of string that can
	 * be passed to write_string; defaults to 64
	 */
	size_t max_write_len;

	/* CFTYPE_* flags */
	unsigned int flags;

	int (*open)(struct inode *inode, struct file *file);
	ssize_t (*read)(struct cgroup *cgrp, struct cftype *cft,
			struct file *file,
			char __user *buf, size_t nbytes, loff_t *ppos);
	/*
	 * read_u64() is a shortcut for the common case of returning a
	 * single integer. Use it in place of read()
	 */
	u64 (*read_u64)(struct cgroup *cgrp, struct cftype *cft);
	/*
	 * read_s64() is a signed version of read_u64()
	 */
	s64 (*read_s64)(struct cgroup *cgrp, struct cftype *cft);
	/*
	 * read_map() is used for defining a map of key/value
	 * pairs. It should call cb->fill(cb, key, value) for each
	 * entry. The key/value pairs (and their ordering) should not
	 * change between reboots.
	 */
	int (*read_map)(struct cgroup *cont, struct cftype *cft,
			struct cgroup_map_cb *cb);
	/*
	 * read_seq_string() is used for outputting a simple sequence
	 * using seqfile.
	 */
	int (*read_seq_string)(struct cgroup *cont, struct cftype *cft,
			       struct seq_file *m);

	ssize_t (*write)(struct cgroup *cgrp, struct cftype *cft,
			 struct file *file,
			 const char __user *buf, size_t nbytes, loff_t *ppos);

	/*
	 * write_u64() is a shortcut for the common case of accepting
	 * a single integer (as parsed by simple_strtoull) from
	 * userspace. Use in place of write(); return 0 or error.
	 */
	int (*write_u64)(struct cgroup *cgrp, struct cftype *cft, u64 val);
	/*
	 * write_s64() is a signed version of write_u64()
	 */
	int (*write_s64)(struct cgroup *cgrp, struct cftype *cft, s64 val);

	/*
	 * write_string() is passed a nul-terminated kernelspace
	 * buffer of maximum length determined by max_write_len.
	 * Returns 0 or -ve error code.
	 */
	int (*write_string)(struct cgroup *cgrp, struct cftype *cft,
			    const char *buffer);
	/*
	 * trigger() callback can be used to get some kick from the
	 * userspace, when the actual string written is not important
	 * at all. The private field can be used to determine the
	 * kick type for multiplexing.
	 */
	int (*trigger)(struct cgroup *cgrp, unsigned int event);

	int (*release)(struct inode *inode, struct file *file);

	/*
	 * register_event() callback will be used to add new userspace
	 * waiter for changes related to the cftype. Implement it if
	 * you want to provide this functionality. Use eventfd_signal()
	 * on eventfd to send notification to userspace.
	 */
	int (*register_event)(struct cgroup *cgrp, struct cftype *cft,
			struct eventfd_ctx *eventfd, const char *args);
	/*
	 * unregister_event() callback will be called when userspace
	 * closes the eventfd or on cgroup removing.
	 * This callback must be implemented, if you want provide
	 * notification functionality.
	 */
	void (*unregister_event)(struct cgroup *cgrp, struct cftype *cft,
			struct eventfd_ctx *eventfd);
};

/*
 * cftype_sets describe cftypes belonging to a subsystem and are chained at
 * cgroup_subsys->cftsets.  Each cftset points to an array of cftypes
 * terminated by zero length name.
 */
struct cftype_set {
	struct list_head		node;	/* chained at subsys->cftsets */
	struct cftype			*cfts;
};

struct cgroup_scanner {
	struct cgroup *cg;
	int (*test_task)(struct task_struct *p, struct cgroup_scanner *scan);
	void (*process_task)(struct task_struct *p,
			struct cgroup_scanner *scan);
	struct ptr_heap *heap;
	void *data;
};

/*
 * See the comment above CGRP_ROOT_SANE_BEHAVIOR for details.  This
 * function can be called as long as @cgrp is accessible.
 */
static inline bool cgroup_sane_behavior(const struct cgroup *cgrp)
{
	return cgrp->root->flags & CGRP_ROOT_SANE_BEHAVIOR;
}

/* Caller should hold rcu_read_lock() */
static inline const char *cgroup_name(const struct cgroup *cgrp)
{
	return rcu_dereference(cgrp->name)->name;
}

int cgroup_add_cftypes(struct cgroup_subsys *ss, struct cftype *cfts);
int cgroup_rm_cftypes(struct cgroup_subsys *ss, struct cftype *cfts);

int cgroup_is_removed(const struct cgroup *cgrp);
bool cgroup_is_descendant(struct cgroup *cgrp, struct cgroup *ancestor);

int cgroup_path(const struct cgroup *cgrp, char *buf, int buflen);

int cgroup_task_count(const struct cgroup *cgrp);

/*
 * Control Group taskset, used to pass around set of tasks to cgroup_subsys
 * methods.
 */
struct cgroup_taskset;
struct task_struct *cgroup_taskset_first(struct cgroup_taskset *tset);
struct task_struct *cgroup_taskset_next(struct cgroup_taskset *tset);
struct cgroup *cgroup_taskset_cur_cgroup(struct cgroup_taskset *tset);
int cgroup_taskset_size(struct cgroup_taskset *tset);

/**
 * cgroup_taskset_for_each - iterate cgroup_taskset
 * @task: the loop cursor
 * @skip_cgrp: skip if task's cgroup matches this, %NULL to iterate through all
 * @tset: taskset to iterate
 */
#define cgroup_taskset_for_each(task, skip_cgrp, tset)			\
	for ((task) = cgroup_taskset_first((tset)); (task);		\
	     (task) = cgroup_taskset_next((tset)))			\
		if (!(skip_cgrp) ||					\
		    cgroup_taskset_cur_cgroup((tset)) != (skip_cgrp))

/*
 * Control Group subsystem type.
 * See Documentation/cgroups/cgroups.txt for details
 */

struct cgroup_subsys {
	struct cgroup_subsys_state *(*css_alloc)(struct cgroup *cgrp);
	int (*css_online)(struct cgroup *cgrp);
	void (*css_offline)(struct cgroup *cgrp);
	void (*css_free)(struct cgroup *cgrp);

	int (*can_attach)(struct cgroup *cgrp, struct cgroup_taskset *tset);
	void (*cancel_attach)(struct cgroup *cgrp, struct cgroup_taskset *tset);
	void (*attach)(struct cgroup *cgrp, struct cgroup_taskset *tset);
	void (*fork)(struct task_struct *task);
	void (*exit)(struct cgroup *cgrp, struct cgroup *old_cgrp,
		     struct task_struct *task);
	void (*bind)(struct cgroup *root);

	int subsys_id;
	int disabled;
	int early_init;
	/*
	 * True if this subsys uses ID. ID is not available before cgroup_init()
	 * (not available in early_init time.)
	 */
	bool use_id;

	/*
	 * If %false, this subsystem is properly hierarchical -
	 * configuration, resource accounting and restriction on a parent
	 * cgroup cover those of its children.  If %true, hierarchy support
	 * is broken in some ways - some subsystems ignore hierarchy
	 * completely while others are only implemented half-way.
	 *
	 * It's now disallowed to create nested cgroups if the subsystem is
	 * broken and cgroup core will emit a warning message on such
	 * cases.  Eventually, all subsystems will be made properly
	 * hierarchical and this will go away.
	 */
	bool broken_hierarchy;
	bool warned_broken_hierarchy;

#define MAX_CGROUP_TYPE_NAMELEN 32
	const char *name;

	/*
	 * Link to parent, and list entry in parent's children.
	 * Protected by cgroup_lock()
	 */
	struct cgroupfs_root *root;
	struct list_head sibling;
	/* used when use_id == true */
	struct idr idr;
	spinlock_t id_lock;

	/* list of cftype_sets */
	struct list_head cftsets;

	/* base cftypes, automatically [de]registered with subsys itself */
	struct cftype *base_cftypes;
	struct cftype_set base_cftset;

	/* should be defined only by modular subsystems */
	struct module *module;
};

#define SUBSYS(_x) extern struct cgroup_subsys _x ## _subsys;
#define IS_SUBSYS_ENABLED(option) IS_BUILTIN(option)
#include <linux/cgroup_subsys.h>
#undef IS_SUBSYS_ENABLED
#undef SUBSYS

static inline struct cgroup_subsys_state *cgroup_subsys_state(
	struct cgroup *cgrp, int subsys_id)
{
	return cgrp->subsys[subsys_id];
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
#define task_css_set_check(task, __c)					\
	rcu_dereference_check((task)->cgroups,				\
		lockdep_is_held(&(task)->alloc_lock) ||			\
		lockdep_is_held(&cgroup_mutex) || (__c))
#else
#define task_css_set_check(task, __c)					\
	rcu_dereference((task)->cgroups)
#endif

/**
 * task_subsys_state_check - obtain css for (task, subsys) w/ extra access conds
 * @task: the target task
 * @subsys_id: the target subsystem ID
 * @__c: extra condition expression to be passed to rcu_dereference_check()
 *
 * Return the cgroup_subsys_state for the (@task, @subsys_id) pair.  The
 * synchronization rules are the same as task_css_set_check().
 */
#define task_subsys_state_check(task, subsys_id, __c)			\
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
 * task_subsys_state - obtain css for (task, subsys)
 * @task: the target task
 * @subsys_id: the target subsystem ID
 *
 * See task_subsys_state_check().
 */
static inline struct cgroup_subsys_state *
task_subsys_state(struct task_struct *task, int subsys_id)
{
	return task_subsys_state_check(task, subsys_id, false);
}

static inline struct cgroup* task_cgroup(struct task_struct *task,
					       int subsys_id)
{
	return task_subsys_state(task, subsys_id)->cgroup;
}

/**
 * cgroup_for_each_child - iterate through children of a cgroup
 * @pos: the cgroup * to use as the loop cursor
 * @cgroup: cgroup whose children to walk
 *
 * Walk @cgroup's children.  Must be called under rcu_read_lock().  A child
 * cgroup which hasn't finished ->css_online() or already has finished
 * ->css_offline() may show up during traversal and it's each subsystem's
 * responsibility to verify that each @pos is alive.
 *
 * If a subsystem synchronizes against the parent in its ->css_online() and
 * before starting iterating, a cgroup which finished ->css_online() is
 * guaranteed to be visible in the future iterations.
 */
#define cgroup_for_each_child(pos, cgroup)				\
	list_for_each_entry_rcu(pos, &(cgroup)->children, sibling)

struct cgroup *cgroup_next_descendant_pre(struct cgroup *pos,
					  struct cgroup *cgroup);
struct cgroup *cgroup_rightmost_descendant(struct cgroup *pos);

/**
 * cgroup_for_each_descendant_pre - pre-order walk of a cgroup's descendants
 * @pos: the cgroup * to use as the loop cursor
 * @cgroup: cgroup whose descendants to walk
 *
 * Walk @cgroup's descendants.  Must be called under rcu_read_lock().  A
 * descendant cgroup which hasn't finished ->css_online() or already has
 * finished ->css_offline() may show up during traversal and it's each
 * subsystem's responsibility to verify that each @pos is alive.
 *
 * If a subsystem synchronizes against the parent in its ->css_online() and
 * before starting iterating, and synchronizes against @pos on each
 * iteration, any descendant cgroup which finished ->css_online() is
 * guaranteed to be visible in the future iterations.
 *
 * In other words, the following guarantees that a descendant can't escape
 * state updates of its ancestors.
 *
 * my_online(@cgrp)
 * {
 *	Lock @cgrp->parent and @cgrp;
 *	Inherit state from @cgrp->parent;
 *	Unlock both.
 * }
 *
 * my_update_state(@cgrp)
 * {
 *	Lock @cgrp;
 *	Update @cgrp's state;
 *	Unlock @cgrp;
 *
 *	cgroup_for_each_descendant_pre(@pos, @cgrp) {
 *		Lock @pos;
 *		Verify @pos is alive and inherit state from @pos->parent;
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
 * inheritance happens for any cgroup after the latest update to its
 * parent.
 *
 * If checking parent's state requires locking the parent, each inheriting
 * iteration should lock and unlock both @pos->parent and @pos.
 *
 * Alternatively, a subsystem may choose to use a single global lock to
 * synchronize ->css_online() and ->css_offline() against tree-walking
 * operations.
 */
#define cgroup_for_each_descendant_pre(pos, cgroup)			\
	for (pos = cgroup_next_descendant_pre(NULL, (cgroup)); (pos);	\
	     pos = cgroup_next_descendant_pre((pos), (cgroup)))

struct cgroup *cgroup_next_descendant_post(struct cgroup *pos,
					   struct cgroup *cgroup);

/**
 * cgroup_for_each_descendant_post - post-order walk of a cgroup's descendants
 * @pos: the cgroup * to use as the loop cursor
 * @cgroup: cgroup whose descendants to walk
 *
 * Similar to cgroup_for_each_descendant_pre() but performs post-order
 * traversal instead.  Note that the walk visibility guarantee described in
 * pre-order walk doesn't apply the same to post-order walks.
 */
#define cgroup_for_each_descendant_post(pos, cgroup)			\
	for (pos = cgroup_next_descendant_post(NULL, (cgroup)); (pos);	\
	     pos = cgroup_next_descendant_post((pos), (cgroup)))

/* A cgroup_iter should be treated as an opaque object */
struct cgroup_iter {
	struct list_head *cg_link;
	struct list_head *task;
};

/*
 * To iterate across the tasks in a cgroup:
 *
 * 1) call cgroup_iter_start to initialize an iterator
 *
 * 2) call cgroup_iter_next() to retrieve member tasks until it
 *    returns NULL or until you want to end the iteration
 *
 * 3) call cgroup_iter_end() to destroy the iterator.
 *
 * Or, call cgroup_scan_tasks() to iterate through every task in a
 * cgroup - cgroup_scan_tasks() holds the css_set_lock when calling
 * the test_task() callback, but not while calling the process_task()
 * callback.
 */
void cgroup_iter_start(struct cgroup *cgrp, struct cgroup_iter *it);
struct task_struct *cgroup_iter_next(struct cgroup *cgrp,
					struct cgroup_iter *it);
void cgroup_iter_end(struct cgroup *cgrp, struct cgroup_iter *it);
int cgroup_scan_tasks(struct cgroup_scanner *scan);
int cgroup_attach_task_all(struct task_struct *from, struct task_struct *);
int cgroup_transfer_tasks(struct cgroup *to, struct cgroup *from);

/*
 * CSS ID is ID for cgroup_subsys_state structs under subsys. This only works
 * if cgroup_subsys.use_id == true. It can be used for looking up and scanning.
 * CSS ID is assigned at cgroup allocation (create) automatically
 * and removed when subsys calls free_css_id() function. This is because
 * the lifetime of cgroup_subsys_state is subsys's matter.
 *
 * Looking up and scanning function should be called under rcu_read_lock().
 * Taking cgroup_mutex is not necessary for following calls.
 * But the css returned by this routine can be "not populated yet" or "being
 * destroyed". The caller should check css and cgroup's status.
 */

/*
 * Typically Called at ->destroy(), or somewhere the subsys frees
 * cgroup_subsys_state.
 */
void free_css_id(struct cgroup_subsys *ss, struct cgroup_subsys_state *css);

/* Find a cgroup_subsys_state which has given ID */

struct cgroup_subsys_state *css_lookup(struct cgroup_subsys *ss, int id);

/* Returns true if root is ancestor of cg */
bool css_is_ancestor(struct cgroup_subsys_state *cg,
		     const struct cgroup_subsys_state *root);

/* Get id and depth of css */
unsigned short css_id(struct cgroup_subsys_state *css);
unsigned short css_depth(struct cgroup_subsys_state *css);
struct cgroup_subsys_state *cgroup_css_from_dir(struct file *f, int id);

#else /* !CONFIG_CGROUPS */

static inline int cgroup_init_early(void) { return 0; }
static inline int cgroup_init(void) { return 0; }
static inline void cgroup_fork(struct task_struct *p) {}
static inline void cgroup_post_fork(struct task_struct *p) {}
static inline void cgroup_exit(struct task_struct *p, int callbacks) {}

static inline void cgroup_lock(void) {}
static inline void cgroup_unlock(void) {}
static inline int cgroupstats_build(struct cgroupstats *stats,
					struct dentry *dentry)
{
	return -EINVAL;
}

/* No cgroups - nothing to do */
static inline int cgroup_attach_task_all(struct task_struct *from,
					 struct task_struct *t)
{
	return 0;
}

#endif /* !CONFIG_CGROUPS */

#endif /* _LINUX_CGROUP_H */
