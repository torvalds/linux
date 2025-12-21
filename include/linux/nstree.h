/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025 Christian Brauner <brauner@kernel.org> */
#ifndef _LINUX_NSTREE_H
#define _LINUX_NSTREE_H

#include <linux/ns/nstree_types.h>
#include <linux/nsproxy.h>
#include <linux/rbtree.h>
#include <linux/seqlock.h>
#include <linux/rculist.h>
#include <linux/cookie.h>
#include <uapi/linux/nsfs.h>

struct ns_common;

extern struct ns_tree_root cgroup_ns_tree;
extern struct ns_tree_root ipc_ns_tree;
extern struct ns_tree_root mnt_ns_tree;
extern struct ns_tree_root net_ns_tree;
extern struct ns_tree_root pid_ns_tree;
extern struct ns_tree_root time_ns_tree;
extern struct ns_tree_root user_ns_tree;
extern struct ns_tree_root uts_ns_tree;

void ns_tree_node_init(struct ns_tree_node *node);
void ns_tree_root_init(struct ns_tree_root *root);
bool ns_tree_node_empty(const struct ns_tree_node *node);
struct rb_node *ns_tree_node_add(struct ns_tree_node *node,
				  struct ns_tree_root *root,
				  int (*cmp)(struct rb_node *, const struct rb_node *));
void ns_tree_node_del(struct ns_tree_node *node, struct ns_tree_root *root);

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

#define ns_tree_gen_id(__ns)                 \
	__ns_tree_gen_id(to_ns_common(__ns), \
			 (((__ns) == ns_init_ns(__ns)) ? ns_init_id(__ns) : 0))

u64 __ns_tree_gen_id(struct ns_common *ns, u64 id);
void __ns_tree_add_raw(struct ns_common *ns, struct ns_tree_root *ns_tree);
void __ns_tree_remove(struct ns_common *ns, struct ns_tree_root *ns_tree);
struct ns_common *ns_tree_lookup_rcu(u64 ns_id, int ns_type);
struct ns_common *__ns_tree_adjoined_rcu(struct ns_common *ns,
					 struct ns_tree_root *ns_tree,
					 bool previous);

static inline void __ns_tree_add(struct ns_common *ns, struct ns_tree_root *ns_tree, u64 id)
{
	__ns_tree_gen_id(ns, id);
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
#define ns_tree_add(__ns)                                   \
	__ns_tree_add(to_ns_common(__ns), to_ns_tree(__ns), \
		      (((__ns) == ns_init_ns(__ns)) ? ns_init_id(__ns) : 0))

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

#define ns_tree_active(__ns) (!RB_EMPTY_NODE(&to_ns_common(__ns)->ns_tree_node.ns_node))

#endif /* _LINUX_NSTREE_H */
