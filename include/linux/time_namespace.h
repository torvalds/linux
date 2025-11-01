/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TIMENS_H
#define _LINUX_TIMENS_H


#include <linux/sched.h>
#include <linux/nsproxy.h>
#include <linux/ns_common.h>
#include <linux/err.h>
#include <linux/time64.h>

struct user_namespace;
extern struct user_namespace init_user_ns;

struct seq_file;
struct vm_area_struct;

struct timens_offsets {
	struct timespec64 monotonic;
	struct timespec64 boottime;
};

struct time_namespace {
	struct user_namespace	*user_ns;
	struct ucounts		*ucounts;
	struct ns_common	ns;
	struct timens_offsets	offsets;
	struct page		*vvar_page;
	/* If set prevents changing offsets after any task joined namespace. */
	bool			frozen_offsets;
} __randomize_layout;

extern struct time_namespace init_time_ns;

#ifdef CONFIG_TIME_NS
static inline struct time_namespace *to_time_ns(struct ns_common *ns)
{
	return container_of(ns, struct time_namespace, ns);
}
void __init time_ns_init(void);
extern int vdso_join_timens(struct task_struct *task,
			    struct time_namespace *ns);
extern void timens_commit(struct task_struct *tsk, struct time_namespace *ns);

static inline struct time_namespace *get_time_ns(struct time_namespace *ns)
{
	ns_ref_inc(ns);
	return ns;
}

struct time_namespace *copy_time_ns(u64 flags,
				    struct user_namespace *user_ns,
				    struct time_namespace *old_ns);
void free_time_ns(struct time_namespace *ns);
void timens_on_fork(struct nsproxy *nsproxy, struct task_struct *tsk);
struct page *find_timens_vvar_page(struct vm_area_struct *vma);

static inline void put_time_ns(struct time_namespace *ns)
{
	if (ns_ref_put(ns))
		free_time_ns(ns);
}

void proc_timens_show_offsets(struct task_struct *p, struct seq_file *m);

struct proc_timens_offset {
	int			clockid;
	struct timespec64	val;
};

int proc_timens_set_offset(struct file *file, struct task_struct *p,
			   struct proc_timens_offset *offsets, int n);

static inline void timens_add_monotonic(struct timespec64 *ts)
{
	struct timens_offsets *ns_offsets = &current->nsproxy->time_ns->offsets;

	*ts = timespec64_add(*ts, ns_offsets->monotonic);
}

static inline void timens_add_boottime(struct timespec64 *ts)
{
	struct timens_offsets *ns_offsets = &current->nsproxy->time_ns->offsets;

	*ts = timespec64_add(*ts, ns_offsets->boottime);
}

static inline u64 timens_add_boottime_ns(u64 nsec)
{
	struct timens_offsets *ns_offsets = &current->nsproxy->time_ns->offsets;

	return nsec + timespec64_to_ns(&ns_offsets->boottime);
}

static inline void timens_sub_boottime(struct timespec64 *ts)
{
	struct timens_offsets *ns_offsets = &current->nsproxy->time_ns->offsets;

	*ts = timespec64_sub(*ts, ns_offsets->boottime);
}

ktime_t do_timens_ktime_to_host(clockid_t clockid, ktime_t tim,
				struct timens_offsets *offsets);

static inline ktime_t timens_ktime_to_host(clockid_t clockid, ktime_t tim)
{
	struct time_namespace *ns = current->nsproxy->time_ns;

	if (likely(ns == &init_time_ns))
		return tim;

	return do_timens_ktime_to_host(clockid, tim, &ns->offsets);
}

#else
static inline void __init time_ns_init(void)
{
}

static inline int vdso_join_timens(struct task_struct *task,
				   struct time_namespace *ns)
{
	return 0;
}

static inline void timens_commit(struct task_struct *tsk,
				 struct time_namespace *ns)
{
}

static inline struct time_namespace *get_time_ns(struct time_namespace *ns)
{
	return NULL;
}

static inline void put_time_ns(struct time_namespace *ns)
{
}

static inline
struct time_namespace *copy_time_ns(u64 flags,
				    struct user_namespace *user_ns,
				    struct time_namespace *old_ns)
{
	if (flags & CLONE_NEWTIME)
		return ERR_PTR(-EINVAL);

	return old_ns;
}

static inline void timens_on_fork(struct nsproxy *nsproxy,
				 struct task_struct *tsk)
{
	return;
}

static inline struct page *find_timens_vvar_page(struct vm_area_struct *vma)
{
	return NULL;
}

static inline void timens_add_monotonic(struct timespec64 *ts) { }
static inline void timens_add_boottime(struct timespec64 *ts) { }

static inline u64 timens_add_boottime_ns(u64 nsec)
{
	return nsec;
}

static inline void timens_sub_boottime(struct timespec64 *ts) { }

static inline ktime_t timens_ktime_to_host(clockid_t clockid, ktime_t tim)
{
	return tim;
}
#endif

#endif /* _LINUX_TIMENS_H */
