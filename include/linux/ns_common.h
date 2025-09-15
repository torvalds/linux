/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_NS_COMMON_H
#define _LINUX_NS_COMMON_H

#include <linux/refcount.h>
#include <linux/rbtree.h>

struct proc_ns_operations;

struct cgroup_namespace;
struct ipc_namespace;
struct mnt_namespace;
struct net;
struct pid_namespace;
struct time_namespace;
struct user_namespace;
struct uts_namespace;

struct ns_common {
	struct dentry *stashed;
	const struct proc_ns_operations *ops;
	unsigned int inum;
	refcount_t count;
	union {
		struct {
			u64 ns_id;
			struct rb_node ns_tree_node;
			struct list_head ns_list_node;
		};
		struct rcu_head ns_rcu;
	};
};

#define to_ns_common(__ns)                              \
	_Generic((__ns),                                \
		struct cgroup_namespace *: &(__ns)->ns, \
		struct ipc_namespace *:    &(__ns)->ns, \
		struct mnt_namespace *:    &(__ns)->ns, \
		struct net *:              &(__ns)->ns, \
		struct pid_namespace *:    &(__ns)->ns, \
		struct time_namespace *:   &(__ns)->ns, \
		struct user_namespace *:   &(__ns)->ns, \
		struct uts_namespace *:    &(__ns)->ns)

#endif
