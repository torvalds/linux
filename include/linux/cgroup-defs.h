/*
 * linux/cgroup-defs.h - basic definitions for cgroup
 *
 * This file provides basic type and interface.  Include this file directly
 * only if necessary to avoid cyclic dependencies.
 */
#ifndef _LINUX_CGROUP_DEFS_H
#define _LINUX_CGROUP_DEFS_H

#include <linux/limits.h>
#include <linux/list.h>
#include <linux/idr.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/percpu-refcount.h>
#include <linux/percpu-rwsem.h>
#include <linux/workqueue.h>
#include <linux/bpf-cgroup.h>

#ifdef CONFIG_CGROUPS

struct cgroup;
struct cgroup_root;
struct cgroup_subsys;
struct cgroup_taskset;
struct kernfs_node;
struct kernfs_ops;
struct kernfs_open_file;
struct seq_file;

#define MAX_CGROUP_TYPE_NAMELEN 32
#define MAX_CGROUP_ROOT_NAMELEN 64
#define MAX_CFTYPE_NAME		64

/* define the enumeration of all cgroup subsystems */
#define SUBSYS(_x) _x ## _cgrp_id,
enum cgroup_subsys_id {
#include <linux/cgroup_subsys.h>
	CGROUP_SUBSYS_COUNT,
};
#undef SUBSYS

/* bits in struct cgroup_subsys_state flags field */
enum {
	CSS_NO_REF	= (1 << 0), /* no reference counting for this css */
	CSS_ONLINE	= (1 << 1), /* between ->css_online() and ->css_offline() */
	CSS_RELEASED	= (1 << 2), /* refcnt reached zero, released */
	CSS_VISIBLE	= (1 << 3), /* css is visible to userland */
};

/* bits in struct cgroup flags field */
enum {
	/* Control Group requires release notifications to userspace */
	CGRP_NOTIFY_ON_RELEASE,
	/*
	 * Clone the parent's configuration when creating a new child
	 * cpuset cgroup.  For historical reasons, this option can be
	 * specified at mount time and thus is implemented here.
	 */
	CGRP_CPUSET_CLONE_CHILDREN,
};

/* cgroup_root->flags */
enum {
	CGRP_ROOT_NOPREFIX	= (1 << 1), /* mounted subsystems have no named prefix */
	CGRP_ROOT_XATTR		= (1 << 2), /* supports extended attributes */
};

/* cftype->flags */
enum {
	CFTYPE_ONLY_ON_ROOT	= (1 << 0),	/* only create on root cgrp */
	CFTYPE_NOT_ON_ROOT	= (1 << 1),	/* don't create on root cgrp */
	CFTYPE_NO_PREFIX	= (1 << 3),	/* (DON'T USE FOR NEW FILES) no subsys prefix */
	CFTYPE_WORLD_WRITABLE	= (1 << 4),	/* (DON'T USE FOR NEW FILES) S_IWUGO */

	/* internal flags, do not use outside cgroup core proper */
	__CFTYPE_ONLY_ON_DFL	= (1 << 16),	/* only on default hierarchy */
	__CFTYPE_NOT_ON_DFL	= (1 << 17),	/* not on default hierarchy */
};

/*
 * cgroup_file is the handle for a file instance created in a cgroup which
 * is used, for example, to generate file changed notifications.  This can
 * be obtained by setting cftype->file_offset.
 */
struct cgroup_file {
	/* do not access any fields from outside cgroup core */
	struct kernfs_node *kn;
};

/*
 * Per-subsystem/per-cgroup state maintained by the system.  This is the
 * fundamental structural building block that controllers deal with.
 *
 * Fields marked with "PI:" are public and immutable and may be accessed
 * directly without synchronization.
 */
struct cgroup_subsys_state {
	/* PI: the cgroup that this css is attached to */
	struct cgroup *cgroup;

	/* PI: the cgroup subsystem that this css is attached to */
	struct cgroup_subsys *ss;

	/* reference count - access via css_[try]get() and css_put() */
	struct percpu_ref refcnt;

	/* PI: the parent css */
	struct cgroup_subsys_state *parent;

	/* siblings list anchored at the parent's ->children */
	struct list_head sibling;
	struct list_head children;

	/*
	 * PI: Subsys-unique ID.  0 is unused and root is always 1.  The
	 * matching css can be looked up using css_from_id().
	 */
	int id;

	unsigned int flags;

	/*
	 * Monotonically increasing unique serial number which defines a
	 * uniform order among all csses.  It's guaranteed that all
	 * ->children lists are in the ascending order of ->serial_nr and
	 * used to allow interrupting and resuming iterations.
	 */
	u64 serial_nr;

	/*
	 * Incremented by online self and children.  Used to guarantee that
	 * parents are not offlined before their children.
	 */
	atomic_t online_cnt;

	/* percpu_ref killing and RCU release */
	struct rcu_head rcu_head;
	struct work_struct destroy_work;
};

/*
 * A css_set is a structure holding pointers to a set of
 * cgroup_subsys_state objects. This saves space in the task struct
 * object and speeds up fork()/exit(), since a single inc/dec and a
 * list_add()/del() can bump the reference count on the entire cgroup
 * set for a task.
 */
struct css_set {
	/*
	 * Set of subsystem states, one for each subsystem. This array is
	 * immutable after creation apart from the init_css_set during
	 * subsystem registration (at boot time).
	 */
	struct cgroup_subsys_state *subsys[CGROUP_SUBSYS_COUNT];

	/* reference count */
	atomic_t refcount;

	/* the default cgroup associated with this css_set */
	struct cgroup *dfl_cgrp;

	/*
	 * Lists running through all tasks using this cgroup group.
	 * mg_tasks lists tasks which belong to this cset but are in the
	 * process of being migrated out or in.  Protected by
	 * css_set_rwsem, but, during migration, once tasks are moved to
	 * mg_tasks, it can be read safely while holding cgroup_mutex.
	 */
	struct list_head tasks;
	struct list_head mg_tasks;

	/* all css_task_iters currently walking this cset */
	struct list_head task_iters;

	/*
	 * On the default hierarhcy, ->subsys[ssid] may point to a css
	 * attached to an ancestor instead of the cgroup this css_set is
	 * associated with.  The following node is anchored at
	 * ->subsys[ssid]->cgroup->e_csets[ssid] and provides a way to
	 * iterate through all css's attached to a given cgroup.
	 */
	struct list_head e_cset_node[CGROUP_SUBSYS_COUNT];

	/*
	 * List running through all cgroup groups in the same hash
	 * slot. Protected by css_set_lock
	 */
	struct hlist_node hlist;

	/*
	 * List of cgrp_cset_links pointing at cgroups referenced from this
	 * css_set.  Protected by css_set_lock.
	 */
	struct list_head cgrp_links;

	/*
	 * List of csets participating in the on-going migration either as
	 * source or destination.  Protected by cgroup_mutex.
	 */
	struct list_head mg_preload_node;
	struct list_head mg_node;

	/*
	 * If this cset is acting as the source of migration the following
	 * two fields are set.  mg_src_cgrp and mg_dst_cgrp are
	 * respectively the source and destination cgroups of the on-going
	 * migration.  mg_dst_cset is the destination cset the target tasks
	 * on this cset should be migrated to.  Protected by cgroup_mutex.
	 */
	struct cgroup *mg_src_cgrp;
	struct cgroup *mg_dst_cgrp;
	struct css_set *mg_dst_cset;

	/* dead and being drained, ignore for migration */
	bool dead;

	/* For RCU-protected deletion */
	struct rcu_head rcu_head;
};

struct cgroup {
	/* self css with NULL ->ss, points back to this cgroup */
	struct cgroup_subsys_state self;

	unsigned long flags;		/* "unsigned long" so bitops work */

	/*
	 * idr allocated in-hierarchy ID.
	 *
	 * ID 0 is not used, the ID of the root cgroup is always 1, and a
	 * new cgroup will be assigned with a smallest available ID.
	 *
	 * Allocating/Removing ID must be protected by cgroup_mutex.
	 */
	int id;

	/*
	 * The depth this cgroup is at.  The root is at depth zero and each
	 * step down the hierarchy increments the level.  This along with
	 * ancestor_ids[] can determine whether a given cgroup is a
	 * descendant of another without traversing the hierarchy.
	 */
	int level;

	/*
	 * Each non-empty css_set associated with this cgroup contributes
	 * one to populated_cnt.  All children with non-zero popuplated_cnt
	 * of their own contribute one.  The count is zero iff there's no
	 * task in this cgroup or its subtree.
	 */
	int populated_cnt;

	struct kernfs_node *kn;		/* cgroup kernfs entry */
	struct cgroup_file procs_file;	/* handle for "cgroup.procs" */
	struct cgroup_file events_file;	/* handle for "cgroup.events" */

	/*
	 * The bitmask of subsystems enabled on the child cgroups.
	 * ->subtree_control is the one configured through
	 * "cgroup.subtree_control" while ->child_ss_mask is the effective
	 * one which may have more subsystems enabled.  Controller knobs
	 * are made available iff it's enabled in ->subtree_control.
	 */
	u16 subtree_control;
	u16 subtree_ss_mask;
	u16 old_subtree_control;
	u16 old_subtree_ss_mask;

	/* Private pointers for each registered subsystem */
	struct cgroup_subsys_state __rcu *subsys[CGROUP_SUBSYS_COUNT];

	struct cgroup_root *root;

	/*
	 * List of cgrp_cset_links pointing at css_sets with tasks in this
	 * cgroup.  Protected by css_set_lock.
	 */
	struct list_head cset_links;

	/*
	 * On the default hierarchy, a css_set for a cgroup with some
	 * susbsys disabled will point to css's which are associated with
	 * the closest ancestor which has the subsys enabled.  The
	 * following lists all css_sets which point to this cgroup's css
	 * for the given subsystem.
	 */
	struct list_head e_csets[CGROUP_SUBSYS_COUNT];

	/*
	 * list of pidlists, up to two for each namespace (one for procs, one
	 * for tasks); created on demand.
	 */
	struct list_head pidlists;
	struct mutex pidlist_mutex;

	/* used to wait for offlining of csses */
	wait_queue_head_t offline_waitq;

	/* used to schedule release agent */
	struct work_struct release_agent_work;

	/* used to store eBPF programs */
	struct cgroup_bpf bpf;

	/* ids of the ancestors at each level including self */
	int ancestor_ids[];
};

/*
 * A cgroup_root represents the root of a cgroup hierarchy, and may be
 * associated with a kernfs_root to form an active hierarchy.  This is
 * internal to cgroup core.  Don't access directly from controllers.
 */
struct cgroup_root {
	struct kernfs_root *kf_root;

	/* The bitmask of subsystems attached to this hierarchy */
	unsigned int subsys_mask;

	/* Unique id for this hierarchy. */
	int hierarchy_id;

	/* The root cgroup.  Root is destroyed on its release. */
	struct cgroup cgrp;

	/* for cgrp->ancestor_ids[0] */
	int cgrp_ancestor_id_storage;

	/* Number of cgroups in the hierarchy, used only for /proc/cgroups */
	atomic_t nr_cgrps;

	/* A list running through the active hierarchies */
	struct list_head root_list;

	/* Hierarchy-specific flags */
	unsigned int flags;

	/* IDs for cgroups in this hierarchy */
	struct idr cgroup_idr;

	/* The path to use for release notifications. */
	char release_agent_path[PATH_MAX];

	/* The name for this hierarchy - may be empty */
	char name[MAX_CGROUP_ROOT_NAMELEN];
};

/*
 * struct cftype: handler definitions for cgroup control files
 *
 * When reading/writing to a file:
 *	- the cgroup to use is file->f_path.dentry->d_parent->d_fsdata
 *	- the 'cftype' of the file is file->f_path.dentry->d_fsdata
 */
struct cftype {
	/*
	 * By convention, the name should begin with the name of the
	 * subsystem, followed by a period.  Zero length string indicates
	 * end of cftype array.
	 */
	char name[MAX_CFTYPE_NAME];
	unsigned long private;

	/*
	 * The maximum length of string, excluding trailing nul, that can
	 * be passed to write.  If < PAGE_SIZE-1, PAGE_SIZE-1 is assumed.
	 */
	size_t max_write_len;

	/* CFTYPE_* flags */
	unsigned int flags;

	/*
	 * If non-zero, should contain the offset from the start of css to
	 * a struct cgroup_file field.  cgroup will record the handle of
	 * the created file into it.  The recorded handle can be used as
	 * long as the containing css remains accessible.
	 */
	unsigned int file_offset;

	/*
	 * Fields used for internal bookkeeping.  Initialized automatically
	 * during registration.
	 */
	struct cgroup_subsys *ss;	/* NULL for cgroup core files */
	struct list_head node;		/* anchored at ss->cfts */
	struct kernfs_ops *kf_ops;

	int (*open)(struct kernfs_open_file *of);
	void (*release)(struct kernfs_open_file *of);

	/*
	 * read_u64() is a shortcut for the common case of returning a
	 * single integer. Use it in place of read()
	 */
	u64 (*read_u64)(struct cgroup_subsys_state *css, struct cftype *cft);
	/*
	 * read_s64() is a signed version of read_u64()
	 */
	s64 (*read_s64)(struct cgroup_subsys_state *css, struct cftype *cft);

	/* generic seq_file read interface */
	int (*seq_show)(struct seq_file *sf, void *v);

	/* optional ops, implement all or none */
	void *(*seq_start)(struct seq_file *sf, loff_t *ppos);
	void *(*seq_next)(struct seq_file *sf, void *v, loff_t *ppos);
	void (*seq_stop)(struct seq_file *sf, void *v);

	/*
	 * write_u64() is a shortcut for the common case of accepting
	 * a single integer (as parsed by simple_strtoull) from
	 * userspace. Use in place of write(); return 0 or error.
	 */
	int (*write_u64)(struct cgroup_subsys_state *css, struct cftype *cft,
			 u64 val);
	/*
	 * write_s64() is a signed version of write_u64()
	 */
	int (*write_s64)(struct cgroup_subsys_state *css, struct cftype *cft,
			 s64 val);

	/*
	 * write() is the generic write callback which maps directly to
	 * kernfs write operation and overrides all other operations.
	 * Maximum write size is determined by ->max_write_len.  Use
	 * of_css/cft() to access the associated css and cft.
	 */
	ssize_t (*write)(struct kernfs_open_file *of,
			 char *buf, size_t nbytes, loff_t off);

#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lock_class_key	lockdep_key;
#endif
};

/*
 * Control Group subsystem type.
 * See Documentation/cgroups/cgroups.txt for details
 */
struct cgroup_subsys {
	struct cgroup_subsys_state *(*css_alloc)(struct cgroup_subsys_state *parent_css);
	int (*css_online)(struct cgroup_subsys_state *css);
	void (*css_offline)(struct cgroup_subsys_state *css);
	void (*css_released)(struct cgroup_subsys_state *css);
	void (*css_free)(struct cgroup_subsys_state *css);
	void (*css_reset)(struct cgroup_subsys_state *css);

	int (*can_attach)(struct cgroup_taskset *tset);
	void (*cancel_attach)(struct cgroup_taskset *tset);
	void (*attach)(struct cgroup_taskset *tset);
	void (*post_attach)(void);
	int (*can_fork)(struct task_struct *task);
	void (*cancel_fork)(struct task_struct *task);
	void (*fork)(struct task_struct *task);
	void (*exit)(struct task_struct *task);
	void (*free)(struct task_struct *task);
	void (*bind)(struct cgroup_subsys_state *root_css);

	bool early_init:1;

	/*
	 * If %true, the controller, on the default hierarchy, doesn't show
	 * up in "cgroup.controllers" or "cgroup.subtree_control", is
	 * implicitly enabled on all cgroups on the default hierarchy, and
	 * bypasses the "no internal process" constraint.  This is for
	 * utility type controllers which is transparent to userland.
	 *
	 * An implicit controller can be stolen from the default hierarchy
	 * anytime and thus must be okay with offline csses from previous
	 * hierarchies coexisting with csses for the current one.
	 */
	bool implicit_on_dfl:1;

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
	bool broken_hierarchy:1;
	bool warned_broken_hierarchy:1;

	/* the following two fields are initialized automtically during boot */
	int id;
	const char *name;

	/* optional, initialized automatically during boot if not set */
	const char *legacy_name;

	/* link to parent, protected by cgroup_lock() */
	struct cgroup_root *root;

	/* idr for css->id */
	struct idr css_idr;

	/*
	 * List of cftypes.  Each entry is the first entry of an array
	 * terminated by zero length name.
	 */
	struct list_head cfts;

	/*
	 * Base cftypes which are automatically registered.  The two can
	 * point to the same array.
	 */
	struct cftype *dfl_cftypes;	/* for the default hierarchy */
	struct cftype *legacy_cftypes;	/* for the legacy hierarchies */

	/*
	 * A subsystem may depend on other subsystems.  When such subsystem
	 * is enabled on a cgroup, the depended-upon subsystems are enabled
	 * together if available.  Subsystems enabled due to dependency are
	 * not visible to userland until explicitly enabled.  The following
	 * specifies the mask of subsystems that this one depends on.
	 */
	unsigned int depends_on;
};

extern struct percpu_rw_semaphore cgroup_threadgroup_rwsem;

/**
 * cgroup_threadgroup_change_begin - threadgroup exclusion for cgroups
 * @tsk: target task
 *
 * Allows cgroup operations to synchronize against threadgroup changes
 * using a percpu_rw_semaphore.
 */
static inline void cgroup_threadgroup_change_begin(struct task_struct *tsk)
{
	percpu_down_read(&cgroup_threadgroup_rwsem);
}

/**
 * cgroup_threadgroup_change_end - threadgroup exclusion for cgroups
 * @tsk: target task
 *
 * Counterpart of cgroup_threadcgroup_change_begin().
 */
static inline void cgroup_threadgroup_change_end(struct task_struct *tsk)
{
	percpu_up_read(&cgroup_threadgroup_rwsem);
}

#else	/* CONFIG_CGROUPS */

#define CGROUP_SUBSYS_COUNT 0

static inline void cgroup_threadgroup_change_begin(struct task_struct *tsk)
{
	might_sleep();
}

static inline void cgroup_threadgroup_change_end(struct task_struct *tsk) {}

#endif	/* CONFIG_CGROUPS */

#ifdef CONFIG_SOCK_CGROUP_DATA

/*
 * sock_cgroup_data is embedded at sock->sk_cgrp_data and contains
 * per-socket cgroup information except for memcg association.
 *
 * On legacy hierarchies, net_prio and net_cls controllers directly set
 * attributes on each sock which can then be tested by the network layer.
 * On the default hierarchy, each sock is associated with the cgroup it was
 * created in and the networking layer can match the cgroup directly.
 *
 * To avoid carrying all three cgroup related fields separately in sock,
 * sock_cgroup_data overloads (prioidx, classid) and the cgroup pointer.
 * On boot, sock_cgroup_data records the cgroup that the sock was created
 * in so that cgroup2 matches can be made; however, once either net_prio or
 * net_cls starts being used, the area is overriden to carry prioidx and/or
 * classid.  The two modes are distinguished by whether the lowest bit is
 * set.  Clear bit indicates cgroup pointer while set bit prioidx and
 * classid.
 *
 * While userland may start using net_prio or net_cls at any time, once
 * either is used, cgroup2 matching no longer works.  There is no reason to
 * mix the two and this is in line with how legacy and v2 compatibility is
 * handled.  On mode switch, cgroup references which are already being
 * pointed to by socks may be leaked.  While this can be remedied by adding
 * synchronization around sock_cgroup_data, given that the number of leaked
 * cgroups is bound and highly unlikely to be high, this seems to be the
 * better trade-off.
 */
struct sock_cgroup_data {
	union {
#ifdef __LITTLE_ENDIAN
		struct {
			u8	is_data;
			u8	padding;
			u16	prioidx;
			u32	classid;
		} __packed;
#else
		struct {
			u32	classid;
			u16	prioidx;
			u8	padding;
			u8	is_data;
		} __packed;
#endif
		u64		val;
	};
};

/*
 * There's a theoretical window where the following accessors race with
 * updaters and return part of the previous pointer as the prioidx or
 * classid.  Such races are short-lived and the result isn't critical.
 */
static inline u16 sock_cgroup_prioidx(struct sock_cgroup_data *skcd)
{
	/* fallback to 1 which is always the ID of the root cgroup */
	return (skcd->is_data & 1) ? skcd->prioidx : 1;
}

static inline u32 sock_cgroup_classid(struct sock_cgroup_data *skcd)
{
	/* fallback to 0 which is the unconfigured default classid */
	return (skcd->is_data & 1) ? skcd->classid : 0;
}

/*
 * If invoked concurrently, the updaters may clobber each other.  The
 * caller is responsible for synchronization.
 */
static inline void sock_cgroup_set_prioidx(struct sock_cgroup_data *skcd,
					   u16 prioidx)
{
	struct sock_cgroup_data skcd_buf = {{ .val = READ_ONCE(skcd->val) }};

	if (sock_cgroup_prioidx(&skcd_buf) == prioidx)
		return;

	if (!(skcd_buf.is_data & 1)) {
		skcd_buf.val = 0;
		skcd_buf.is_data = 1;
	}

	skcd_buf.prioidx = prioidx;
	WRITE_ONCE(skcd->val, skcd_buf.val);	/* see sock_cgroup_ptr() */
}

static inline void sock_cgroup_set_classid(struct sock_cgroup_data *skcd,
					   u32 classid)
{
	struct sock_cgroup_data skcd_buf = {{ .val = READ_ONCE(skcd->val) }};

	if (sock_cgroup_classid(&skcd_buf) == classid)
		return;

	if (!(skcd_buf.is_data & 1)) {
		skcd_buf.val = 0;
		skcd_buf.is_data = 1;
	}

	skcd_buf.classid = classid;
	WRITE_ONCE(skcd->val, skcd_buf.val);	/* see sock_cgroup_ptr() */
}

#else	/* CONFIG_SOCK_CGROUP_DATA */

struct sock_cgroup_data {
};

#endif	/* CONFIG_SOCK_CGROUP_DATA */

#endif	/* _LINUX_CGROUP_DEFS_H */
