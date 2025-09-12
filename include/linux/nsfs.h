/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025 Christian Brauner <brauner@kernel.org> */

#ifndef _LINUX_NSFS_H
#define _LINUX_NSFS_H

#include <linux/ns_common.h>
#include <linux/cred.h>
#include <linux/pid_namespace.h>

struct path;
struct task_struct;
struct proc_ns_operations;

int ns_get_path(struct path *path, struct task_struct *task,
		const struct proc_ns_operations *ns_ops);
typedef struct ns_common *ns_get_path_helper_t(void *);
int ns_get_path_cb(struct path *path, ns_get_path_helper_t ns_get_cb,
		   void *private_data);

bool ns_match(const struct ns_common *ns, dev_t dev, ino_t ino);

int ns_get_name(char *buf, size_t size, struct task_struct *task,
			const struct proc_ns_operations *ns_ops);
void nsfs_init(void);

#define __current_namespace_from_type(__ns)				\
	_Generic((__ns),						\
		struct cgroup_namespace *: current->nsproxy->cgroup_ns,	\
		struct ipc_namespace *:    current->nsproxy->ipc_ns,	\
		struct net *:              current->nsproxy->net_ns,	\
		struct pid_namespace *:    task_active_pid_ns(current),	\
		struct mnt_namespace *:    current->nsproxy->mnt_ns,	\
		struct time_namespace *:   current->nsproxy->time_ns,	\
		struct user_namespace *:   current_user_ns(),		\
		struct uts_namespace *:    current->nsproxy->uts_ns)

#define current_in_namespace(__ns) (__current_namespace_from_type(__ns) == __ns)

#endif /* _LINUX_NSFS_H */
