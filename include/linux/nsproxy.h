#ifndef _LINUX_NSPROXY_H
#define _LINUX_NSPROXY_H

#include <linux/spinlock.h>
#include <linux/sched.h>

struct mnt_namespace;
struct uts_namespace;
struct ipc_namespace;
struct pid_namespace;

/*
 * A structure to contain pointers to all per-process
 * namespaces - fs (mount), uts, network, sysvipc, etc.
 *
 * 'count' is the number of tasks holding a reference.
 * The count for each namespace, then, will be the number
 * of nsproxies pointing to it, not the number of tasks.
 *
 * The nsproxy is shared by tasks which share all namespaces.
 * As soon as a single namespace is cloned or unshared, the
 * nsproxy is copied.
 */
struct nsproxy {
	atomic_t count;
	struct uts_namespace *uts_ns;
	struct ipc_namespace *ipc_ns;
	struct mnt_namespace *mnt_ns;
	struct pid_namespace *pid_ns;
	struct user_namespace *user_ns;
	struct net 	     *net_ns;
};
extern struct nsproxy init_nsproxy;

int copy_namespaces(unsigned long flags, struct task_struct *tsk);
void get_task_namespaces(struct task_struct *tsk);
void free_nsproxy(struct nsproxy *ns);
int unshare_nsproxy_namespaces(unsigned long, struct nsproxy **,
	struct fs_struct *);

static inline void put_nsproxy(struct nsproxy *ns)
{
	if (atomic_dec_and_test(&ns->count)) {
		free_nsproxy(ns);
	}
}

static inline void exit_task_namespaces(struct task_struct *p)
{
	struct nsproxy *ns = p->nsproxy;
	if (ns) {
		task_lock(p);
		p->nsproxy = NULL;
		task_unlock(p);
		put_nsproxy(ns);
	}
}

#ifdef CONFIG_CGROUP_NS
int ns_cgroup_clone(struct task_struct *tsk);
#else
static inline int ns_cgroup_clone(struct task_struct *tsk) { return 0; }
#endif

#endif
