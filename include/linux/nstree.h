/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_NSTREE_H
#define _LINUX_NSTREE_H

#include <linux/ns_common.h>
#include <linux/nsproxy.h>
#include <linux/rbtree.h>
#include <linux/seqlock.h>
#include <linux/rculist.h>
#include <linux/cookie.h>

extern struct ns_tree cgroup_ns_tree;
extern struct ns_tree ipc_ns_tree;
extern struct ns_tree mnt_ns_tree;
extern struct ns_tree net_ns_tree;
extern struct ns_tree pid_ns_tree;
extern struct ns_tree time_ns_tree;
extern struct ns_tree user_ns_tree;
extern struct ns_tree uts_ns_tree;

#define to_ns_tree(__ns)					\
	_Generic((__ns),					\
		struct cgroup_namespace *: &(cgroup_ns_tree),	\
		struct ipc_namespace *:    &(ipc_ns_tree),	\
		struct net *:              &(net_ns_tree),	\
		struct pid_namespace *:    &(pid_ns_tree),	\
		struct mnt_namespace *:    &(mnt_ns_tree),	\
		struct time_namespace *:   &(time_ns_tree),	\
		struct user_namespace *:   &(user_ns_tree),	\
		struct uts_namespace *:    &(uts_ns_tree))

u64 ns_tree_gen_id(struct ns_common *ns);
void __ns_tree_add_raw(struct ns_common *ns, struct ns_tree *ns_tree);
void __ns_tree_remove(struct ns_common *ns, struct ns_tree *ns_tree);
struct ns_common *ns_tree_lookup_rcu(u64 ns_id, int ns_type);
struct ns_common *__ns_tree_adjoined_rcu(struct ns_common *ns,
					 struct ns_tree *ns_tree,
					 bool previous);

static inline void __ns_tree_add(struct ns_common *ns, struct ns_tree *ns_tree)
{
	ns_tree_gen_id(ns);
	__ns_tree_add_raw(ns, ns_tree);
}

/**
 * ns_tree_add_raw - Add a namespace to a namespace
 * @ns: Namespace to add
 *
 * This function adds a namespace to the appropriate namespace tree
 * without assigning a id.
 */
#define ns_tree_add_raw(__ns) __ns_tree_add_raw(to_ns_common(__ns), to_ns_tree(__ns))

/**
 * ns_tree_add - Add a namespace to a namespace tree
 * @ns: Namespace to add
 *
 * This function assigns a new id to the namespace and adds it to the
 * appropriate namespace tree and list.
 */
#define ns_tree_add(__ns) __ns_tree_add(to_ns_common(__ns), to_ns_tree(__ns))

/**
 * ns_tree_remove - Remove a namespace from a namespace tree
 * @ns: Namespace to remove
 *
 * This function removes a namespace from the appropriate namespace
 * tree and list.
 */
#define ns_tree_remove(__ns)  __ns_tree_remove(to_ns_common(__ns), to_ns_tree(__ns))

#define ns_tree_adjoined_rcu(__ns, __previous) \
	__ns_tree_adjoined_rcu(to_ns_common(__ns), to_ns_tree(__ns), __previous)

#define ns_tree_active(__ns) (!RB_EMPTY_NODE(&to_ns_common(__ns)->ns_tree_node))

#endif /* _LINUX_NSTREE_H */
