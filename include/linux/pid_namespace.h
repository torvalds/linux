#ifndef _LINUX_PID_NS_H
#define _LINUX_PID_NS_H

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/threads.h>
#include <linux/pid.h>
#include <linux/nsproxy.h>
#include <linux/kref.h>

struct pidmap {
       atomic_t nr_free;
       void *page;
};

#define PIDMAP_ENTRIES         ((PID_MAX_LIMIT + 8*PAGE_SIZE - 1)/PAGE_SIZE/8)

struct pid_namespace {
	struct kref kref;
	struct pidmap pidmap[PIDMAP_ENTRIES];
	int last_pid;
	struct task_struct *child_reaper;
};

extern struct pid_namespace init_pid_ns;

static inline struct pid_namespace *get_pid_ns(struct pid_namespace *ns)
{
	kref_get(&ns->kref);
	return ns;
}

extern struct pid_namespace *copy_pid_ns(unsigned long flags, struct pid_namespace *ns);
extern void free_pid_ns(struct kref *kref);

static inline void put_pid_ns(struct pid_namespace *ns)
{
	kref_put(&ns->kref, free_pid_ns);
}

static inline struct task_struct *child_reaper(struct task_struct *tsk)
{
	return init_pid_ns.child_reaper;
}

#endif /* _LINUX_PID_NS_H */
