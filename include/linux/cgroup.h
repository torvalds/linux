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
extern void cgroup_exit(struct task_struct *p, int run_callbacks);

extern struct file_operations proc_cgroup_operations;

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

static inline void css_put(struct cgroup_subsys_state *css)
{
	if (!test_bit(CSS_ROOT, &css->flags))
		atomic_dec(&css->refcnt);
}

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
 *	- the cgroup to use in file->f_dentry->d_parent->d_fsdata
 *	- the 'cftype' of the file is file->f_dentry->d_fsdata
 */

#define MAX_CFTYPE_NAME 64
struct cftype {
	/* By convention, the name should begin with the name of the
	 * subsystem, followed by a period */
	char name[MAX_CFTYPE_NAME];
	int private;
	int (*open) (struct inode *inode, struct file *file);
	ssize_t (*read) (struct cgroup *cont, struct cftype *cft,
			 struct file *file,
			 char __user *buf, size_t nbytes, loff_t *ppos);
	/*
	 * read_uint() is a shortcut for the common case of returning a
	 * single integer. Use it in place of read()
	 */
	u64 (*read_uint) (struct cgroup *cont, struct cftype *cft);
	ssize_t (*write) (struct cgroup *cont, struct cftype *cft,
			  struct file *file,
			  const char __user *buf, size_t nbytes, loff_t *ppos);

	/*
	 * write_uint() is a shortcut for the common case of accepting
	 * a single integer (as parsed by simple_strtoull) from
	 * userspace. Use in place of write(); return 0 or error.
	 */
	int (*write_uint) (struct cgroup *cont, struct cftype *cft, u64 val);

	int (*release) (struct inode *inode, struct file *file);
};

/* Add a new file to the given cgroup directory. Should only be
 * called by subsystems from within a populate() method */
int cgroup_add_file(struct cgroup *cont, struct cgroup_subsys *subsys,
		       const struct cftype *cft);

/* Add a set of new files to the given cgroup directory. Should
 * only be called by subsystems from within a populate() method */
int cgroup_add_files(struct cgroup *cont,
			struct cgroup_subsys *subsys,
			const struct cftype cft[],
			int count);

int cgroup_is_removed(const struct cgroup *cont);

int cgroup_path(const struct cgroup *cont, char *buf, int buflen);

int __cgroup_task_count(const struct cgroup *cont);
static inline int cgroup_task_count(const struct cgroup *cont)
{
	int task_count;
	rcu_read_lock();
	task_count = __cgroup_task_count(cont);
	rcu_read_unlock();
	return task_count;
}

/* Return true if the cgroup is a descendant of the current cgroup */
int cgroup_is_descendant(const struct cgroup *cont);

/* Control Group subsystem type. See Documentation/cgroups.txt for details */

struct cgroup_subsys {
	struct cgroup_subsys_state *(*create)(struct cgroup_subsys *ss,
						  struct cgroup *cont);
	void (*destroy)(struct cgroup_subsys *ss, struct cgroup *cont);
	int (*can_attach)(struct cgroup_subsys *ss,
			  struct cgroup *cont, struct task_struct *tsk);
	void (*attach)(struct cgroup_subsys *ss, struct cgroup *cont,
			struct cgroup *old_cont, struct task_struct *tsk);
	void (*fork)(struct cgroup_subsys *ss, struct task_struct *task);
	void (*exit)(struct cgroup_subsys *ss, struct task_struct *task);
	int (*populate)(struct cgroup_subsys *ss,
			struct cgroup *cont);
	void (*post_clone)(struct cgroup_subsys *ss, struct cgroup *cont);
	void (*bind)(struct cgroup_subsys *ss, struct cgroup *root);
	int subsys_id;
	int active;
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
	struct cgroup *cont, int subsys_id)
{
	return cont->subsys[subsys_id];
}

static inline struct cgroup_subsys_state *task_subsys_state(
	struct task_struct *task, int subsys_id)
{
	return rcu_dereference(task->cgroups.subsys[subsys_id]);
}

static inline struct cgroup* task_cgroup(struct task_struct *task,
					       int subsys_id)
{
	return task_subsys_state(task, subsys_id)->cgroup;
}

int cgroup_path(const struct cgroup *cont, char *buf, int buflen);

int cgroup_clone(struct task_struct *tsk, struct cgroup_subsys *ss);

#else /* !CONFIG_CGROUPS */

static inline int cgroup_init_early(void) { return 0; }
static inline int cgroup_init(void) { return 0; }
static inline void cgroup_init_smp(void) {}
static inline void cgroup_fork(struct task_struct *p) {}
static inline void cgroup_fork_callbacks(struct task_struct *p) {}
static inline void cgroup_exit(struct task_struct *p, int callbacks) {}

static inline void cgroup_lock(void) {}
static inline void cgroup_unlock(void) {}

#endif /* !CONFIG_CGROUPS */

#endif /* _LINUX_CGROUP_H */
