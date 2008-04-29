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
#include <linux/kref.h>
#include <linux/cpumask.h>
#include <linux/nodemask.h>
#include <linux/rcupdate.h>
#include <linux/cgroupstats.h>
#include <linux/prio_heap.h>

#ifdef CONFIG_CGROUPS

struct cgroupfs_root;
struct cgroup_subsys;
struct inode;

extern int cgroup_init_early(void);
extern int cgroup_init(void);
extern void cgroup_init_smp(void);
extern void cgroup_lock(void);
extern void cgroup_unlock(void);
extern void cgroup_fork(struct task_struct *p);
extern void cgroup_fork_callbacks(struct task_struct *p);
extern void cgroup_post_fork(struct task_struct *p);
extern void cgroup_exit(struct task_struct *p, int run_callbacks);
extern int cgroupstats_build(struct cgroupstats *stats,
				struct dentry *dentry);

extern struct file_operations proc_cgroup_operations;

/* Define the enumeration of all cgroup subsystems */
#define SUBSYS(_x) _x ## _subsys_id,
enum cgroup_subsys_id {
#include <linux/cgroup_subsys.h>
	CGROUP_SUBSYS_COUNT
};
#undef SUBSYS

/* Per-subsystem/per-cgroup state maintained by the system. */
struct cgroup_subsys_state {
	/* The cgroup that this subsystem is attached to. Useful
	 * for subsystems that want to know about the cgroup
	 * hierarchy structure */
	struct cgroup *cgroup;

	/* State maintained by the cgroup system to allow
	 * subsystems to be "busy". Should be accessed via css_get()
	 * and css_put() */

	atomic_t refcnt;

	unsigned long flags;
};

/* bits in struct cgroup_subsys_state flags field */
enum {
	CSS_ROOT, /* This CSS is the root of the subsystem */
};

/*
 * Call css_get() to hold a reference on the cgroup;
 *
 */

static inline void css_get(struct cgroup_subsys_state *css)
{
	/* We don't need to reference count the root state */
	if (!test_bit(CSS_ROOT, &css->flags))
		atomic_inc(&css->refcnt);
}
/*
 * css_put() should be called to release a reference taken by
 * css_get()
 */

extern void __css_put(struct cgroup_subsys_state *css);
static inline void css_put(struct cgroup_subsys_state *css)
{
	if (!test_bit(CSS_ROOT, &css->flags))
		__css_put(css);
}

/* bits in struct cgroup flags field */
enum {
	/* Control Group is dead */
	CGRP_REMOVED,
	/* Control Group has previously had a child cgroup or a task,
	 * but no longer (only if CGRP_NOTIFY_ON_RELEASE is set) */
	CGRP_RELEASABLE,
	/* Control Group requires release notifications to userspace */
	CGRP_NOTIFY_ON_RELEASE,
};

struct cgroup {
	unsigned long flags;		/* "unsigned long" so bitops work */

	/* count users of this cgroup. >0 means busy, but doesn't
	 * necessarily indicate the number of tasks in the
	 * cgroup */
	atomic_t count;

	/*
	 * We link our 'sibling' struct into our parent's 'children'.
	 * Our children link their 'sibling' into our 'children'.
	 */
	struct list_head sibling;	/* my parent's children */
	struct list_head children;	/* my children */

	struct cgroup *parent;	/* my parent */
	struct dentry *dentry;	  	/* cgroup fs entry */

	/* Private pointers for each registered subsystem */
	struct cgroup_subsys_state *subsys[CGROUP_SUBSYS_COUNT];

	struct cgroupfs_root *root;
	struct cgroup *top_cgroup;

	/*
	 * List of cg_cgroup_links pointing at css_sets with
	 * tasks in this cgroup. Protected by css_set_lock
	 */
	struct list_head css_sets;

	/*
	 * Linked list running through all cgroups that can
	 * potentially be reaped by the release agent. Protected by
	 * release_list_lock
	 */
	struct list_head release_list;
};

/* A css_set is a structure holding pointers to a set of
 * cgroup_subsys_state objects. This saves space in the task struct
 * object and speeds up fork()/exit(), since a single inc/dec and a
 * list_add()/del() can bump the reference count on the entire
 * cgroup set for a task.
 */

struct css_set {

	/* Reference count */
	struct kref ref;

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
	 * during subsystem registration (at boot time).
	 */
	struct cgroup_subsys_state *subsys[CGROUP_SUBSYS_COUNT];
};

/*
 * cgroup_map_cb is an abstract callback API for reporting map-valued
 * control files
 */

struct cgroup_map_cb {
	int (*fill)(struct cgroup_map_cb *cb, const char *key, u64 value);
	void *state;
};

/* struct cftype:
 *
 * The files in the cgroup filesystem mostly have a very simple read/write
 * handling, some common function will take care of it. Nevertheless some cases
 * (read tasks) are special and therefore I define this structure for every
 * kind of file.
 *
 *
 * When reading/writing to a file:
 *	- the cgroup to use is file->f_dentry->d_parent->d_fsdata
 *	- the 'cftype' of the file is file->f_dentry->d_fsdata
 */

#define MAX_CFTYPE_NAME 64
struct cftype {
	/* By convention, the name should begin with the name of the
	 * subsystem, followed by a period */
	char name[MAX_CFTYPE_NAME];
	int private;
	int (*open) (struct inode *inode, struct file *file);
	ssize_t (*read) (struct cgroup *cgrp, struct cftype *cft,
			 struct file *file,
			 char __user *buf, size_t nbytes, loff_t *ppos);
	/*
	 * read_u64() is a shortcut for the common case of returning a
	 * single integer. Use it in place of read()
	 */
	u64 (*read_u64) (struct cgroup *cgrp, struct cftype *cft);
	/*
	 * read_s64() is a signed version of read_u64()
	 */
	s64 (*read_s64) (struct cgroup *cgrp, struct cftype *cft);
	/*
	 * read_map() is used for defining a map of key/value
	 * pairs. It should call cb->fill(cb, key, value) for each
	 * entry. The key/value pairs (and their ordering) should not
	 * change between reboots.
	 */
	int (*read_map) (struct cgroup *cont, struct cftype *cft,
			 struct cgroup_map_cb *cb);
	/*
	 * read_seq_string() is used for outputting a simple sequence
	 * using seqfile.
	 */
	int (*read_seq_string) (struct cgroup *cont, struct cftype *cft,
			 struct seq_file *m);

	ssize_t (*write) (struct cgroup *cgrp, struct cftype *cft,
			  struct file *file,
			  const char __user *buf, size_t nbytes, loff_t *ppos);

	/*
	 * write_u64() is a shortcut for the common case of accepting
	 * a single integer (as parsed by simple_strtoull) from
	 * userspace. Use in place of write(); return 0 or error.
	 */
	int (*write_u64) (struct cgroup *cgrp, struct cftype *cft, u64 val);
	/*
	 * write_s64() is a signed version of write_u64()
	 */
	int (*write_s64) (struct cgroup *cgrp, struct cftype *cft, s64 val);

	/*
	 * trigger() callback can be used to get some kick from the
	 * userspace, when the actual string written is not important
	 * at all. The private field can be used to determine the
	 * kick type for multiplexing.
	 */
	int (*trigger)(struct cgroup *cgrp, unsigned int event);

	int (*release) (struct inode *inode, struct file *file);
};

struct cgroup_scanner {
	struct cgroup *cg;
	int (*test_task)(struct task_struct *p, struct cgroup_scanner *scan);
	void (*process_task)(struct task_struct *p,
			struct cgroup_scanner *scan);
	struct ptr_heap *heap;
};

/* Add a new file to the given cgroup directory. Should only be
 * called by subsystems from within a populate() method */
int cgroup_add_file(struct cgroup *cgrp, struct cgroup_subsys *subsys,
		       const struct cftype *cft);

/* Add a set of new files to the given cgroup directory. Should
 * only be called by subsystems from within a populate() method */
int cgroup_add_files(struct cgroup *cgrp,
			struct cgroup_subsys *subsys,
			const struct cftype cft[],
			int count);

int cgroup_is_removed(const struct cgroup *cgrp);

int cgroup_path(const struct cgroup *cgrp, char *buf, int buflen);

int cgroup_task_count(const struct cgroup *cgrp);

/* Return true if the cgroup is a descendant of the current cgroup */
int cgroup_is_descendant(const struct cgroup *cgrp);

/* Control Group subsystem type. See Documentation/cgroups.txt for details */

struct cgroup_subsys {
	struct cgroup_subsys_state *(*create)(struct cgroup_subsys *ss,
						  struct cgroup *cgrp);
	void (*pre_destroy)(struct cgroup_subsys *ss, struct cgroup *cgrp);
	void (*destroy)(struct cgroup_subsys *ss, struct cgroup *cgrp);
	int (*can_attach)(struct cgroup_subsys *ss,
			  struct cgroup *cgrp, struct task_struct *tsk);
	void (*attach)(struct cgroup_subsys *ss, struct cgroup *cgrp,
			struct cgroup *old_cgrp, struct task_struct *tsk);
	void (*fork)(struct cgroup_subsys *ss, struct task_struct *task);
	void (*exit)(struct cgroup_subsys *ss, struct task_struct *task);
	int (*populate)(struct cgroup_subsys *ss,
			struct cgroup *cgrp);
	void (*post_clone)(struct cgroup_subsys *ss, struct cgroup *cgrp);
	void (*bind)(struct cgroup_subsys *ss, struct cgroup *root);
	/*
	 * This routine is called with the task_lock of mm->owner held
	 */
	void (*mm_owner_changed)(struct cgroup_subsys *ss,
					struct cgroup *old,
					struct cgroup *new);
	int subsys_id;
	int active;
	int disabled;
	int early_init;
#define MAX_CGROUP_TYPE_NAMELEN 32
	const char *name;

	/* Protected by RCU */
	struct cgroupfs_root *root;

	struct list_head sibling;

	void *private;
};

#define SUBSYS(_x) extern struct cgroup_subsys _x ## _subsys;
#include <linux/cgroup_subsys.h>
#undef SUBSYS

static inline struct cgroup_subsys_state *cgroup_subsys_state(
	struct cgroup *cgrp, int subsys_id)
{
	return cgrp->subsys[subsys_id];
}

static inline struct cgroup_subsys_state *task_subsys_state(
	struct task_struct *task, int subsys_id)
{
	return rcu_dereference(task->cgroups->subsys[subsys_id]);
}

static inline struct cgroup* task_cgroup(struct task_struct *task,
					       int subsys_id)
{
	return task_subsys_state(task, subsys_id)->cgroup;
}

int cgroup_clone(struct task_struct *tsk, struct cgroup_subsys *ss);

/* A cgroup_iter should be treated as an opaque object */
struct cgroup_iter {
	struct list_head *cg_link;
	struct list_head *task;
};

/* To iterate across the tasks in a cgroup:
 *
 * 1) call cgroup_iter_start to intialize an iterator
 *
 * 2) call cgroup_iter_next() to retrieve member tasks until it
 *    returns NULL or until you want to end the iteration
 *
 * 3) call cgroup_iter_end() to destroy the iterator.
 *
 * Or, call cgroup_scan_tasks() to iterate through every task in a cpuset.
 *    - cgroup_scan_tasks() holds the css_set_lock when calling the test_task()
 *      callback, but not while calling the process_task() callback.
 */
void cgroup_iter_start(struct cgroup *cgrp, struct cgroup_iter *it);
struct task_struct *cgroup_iter_next(struct cgroup *cgrp,
					struct cgroup_iter *it);
void cgroup_iter_end(struct cgroup *cgrp, struct cgroup_iter *it);
int cgroup_scan_tasks(struct cgroup_scanner *scan);
int cgroup_attach_task(struct cgroup *, struct task_struct *);

#else /* !CONFIG_CGROUPS */

static inline int cgroup_init_early(void) { return 0; }
static inline int cgroup_init(void) { return 0; }
static inline void cgroup_init_smp(void) {}
static inline void cgroup_fork(struct task_struct *p) {}
static inline void cgroup_fork_callbacks(struct task_struct *p) {}
static inline void cgroup_post_fork(struct task_struct *p) {}
static inline void cgroup_exit(struct task_struct *p, int callbacks) {}

static inline void cgroup_lock(void) {}
static inline void cgroup_unlock(void) {}
static inline int cgroupstats_build(struct cgroupstats *stats,
					struct dentry *dentry)
{
	return -EINVAL;
}

#endif /* !CONFIG_CGROUPS */

#ifdef CONFIG_MM_OWNER
extern void
cgroup_mm_owner_callbacks(struct task_struct *old, struct task_struct *new);
#else /* !CONFIG_MM_OWNER */
static inline void
cgroup_mm_owner_callbacks(struct task_struct *old, struct task_struct *new)
{
}
#endif /* CONFIG_MM_OWNER */
#endif /* _LINUX_CGROUP_H */
