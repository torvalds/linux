// SPDX-License-Identifier: GPL-2.0-only

#include <linux/nstree.h>
#include <linux/proc_ns.h>
#include <linux/vfsdebug.h>

/**
 * struct ns_tree - Namespace tree
 * @ns_tree: Rbtree of namespaces of a particular type
 * @ns_list: Sequentially walkable list of all namespaces of this type
 * @ns_tree_lock: Seqlock to protect the tree and list
 * @type: type of namespaces in this tree
 */
struct ns_tree {
       struct rb_root ns_tree;
       struct list_head ns_list;
       seqlock_t ns_tree_lock;
       int type;
};

struct ns_tree mnt_ns_tree = {
	.ns_tree = RB_ROOT,
	.ns_list = LIST_HEAD_INIT(mnt_ns_tree.ns_list),
	.ns_tree_lock = __SEQLOCK_UNLOCKED(mnt_ns_tree.ns_tree_lock),
	.type = CLONE_NEWNS,
};

struct ns_tree net_ns_tree = {
	.ns_tree = RB_ROOT,
	.ns_list = LIST_HEAD_INIT(net_ns_tree.ns_list),
	.ns_tree_lock = __SEQLOCK_UNLOCKED(net_ns_tree.ns_tree_lock),
	.type = CLONE_NEWNET,
};
EXPORT_SYMBOL_GPL(net_ns_tree);

struct ns_tree uts_ns_tree = {
	.ns_tree = RB_ROOT,
	.ns_list = LIST_HEAD_INIT(uts_ns_tree.ns_list),
	.ns_tree_lock = __SEQLOCK_UNLOCKED(uts_ns_tree.ns_tree_lock),
	.type = CLONE_NEWUTS,
};

struct ns_tree user_ns_tree = {
	.ns_tree = RB_ROOT,
	.ns_list = LIST_HEAD_INIT(user_ns_tree.ns_list),
	.ns_tree_lock = __SEQLOCK_UNLOCKED(user_ns_tree.ns_tree_lock),
	.type = CLONE_NEWUSER,
};

struct ns_tree ipc_ns_tree = {
	.ns_tree = RB_ROOT,
	.ns_list = LIST_HEAD_INIT(ipc_ns_tree.ns_list),
	.ns_tree_lock = __SEQLOCK_UNLOCKED(ipc_ns_tree.ns_tree_lock),
	.type = CLONE_NEWIPC,
};

struct ns_tree pid_ns_tree = {
	.ns_tree = RB_ROOT,
	.ns_list = LIST_HEAD_INIT(pid_ns_tree.ns_list),
	.ns_tree_lock = __SEQLOCK_UNLOCKED(pid_ns_tree.ns_tree_lock),
	.type = CLONE_NEWPID,
};

struct ns_tree cgroup_ns_tree = {
	.ns_tree = RB_ROOT,
	.ns_list = LIST_HEAD_INIT(cgroup_ns_tree.ns_list),
	.ns_tree_lock = __SEQLOCK_UNLOCKED(cgroup_ns_tree.ns_tree_lock),
	.type = CLONE_NEWCGROUP,
};

struct ns_tree time_ns_tree = {
	.ns_tree = RB_ROOT,
	.ns_list = LIST_HEAD_INIT(time_ns_tree.ns_list),
	.ns_tree_lock = __SEQLOCK_UNLOCKED(time_ns_tree.ns_tree_lock),
	.type = CLONE_NEWTIME,
};

DEFINE_COOKIE(namespace_cookie);

static inline struct ns_common *node_to_ns(const struct rb_node *node)
{
	if (!node)
		return NULL;
	return rb_entry(node, struct ns_common, ns_tree_node);
}

static inline int ns_cmp(struct rb_node *a, const struct rb_node *b)
{
	struct ns_common *ns_a = node_to_ns(a);
	struct ns_common *ns_b = node_to_ns(b);
	u64 ns_id_a = ns_a->ns_id;
	u64 ns_id_b = ns_b->ns_id;

	if (ns_id_a < ns_id_b)
		return -1;
	if (ns_id_a > ns_id_b)
		return 1;
	return 0;
}

void __ns_tree_add_raw(struct ns_common *ns, struct ns_tree *ns_tree)
{
	struct rb_node *node, *prev;

	VFS_WARN_ON_ONCE(!ns->ns_id);

	write_seqlock(&ns_tree->ns_tree_lock);

	VFS_WARN_ON_ONCE(ns->ns_type != ns_tree->type);

	node = rb_find_add_rcu(&ns->ns_tree_node, &ns_tree->ns_tree, ns_cmp);
	/*
	 * If there's no previous entry simply add it after the
	 * head and if there is add it after the previous entry.
	 */
	prev = rb_prev(&ns->ns_tree_node);
	if (!prev)
		list_add_rcu(&ns->ns_list_node, &ns_tree->ns_list);
	else
		list_add_rcu(&ns->ns_list_node, &node_to_ns(prev)->ns_list_node);

	write_sequnlock(&ns_tree->ns_tree_lock);

	VFS_WARN_ON_ONCE(node);
}

void __ns_tree_remove(struct ns_common *ns, struct ns_tree *ns_tree)
{
	VFS_WARN_ON_ONCE(RB_EMPTY_NODE(&ns->ns_tree_node));
	VFS_WARN_ON_ONCE(list_empty(&ns->ns_list_node));
	VFS_WARN_ON_ONCE(ns->ns_type != ns_tree->type);

	write_seqlock(&ns_tree->ns_tree_lock);
	rb_erase(&ns->ns_tree_node, &ns_tree->ns_tree);
	list_bidir_del_rcu(&ns->ns_list_node);
	RB_CLEAR_NODE(&ns->ns_tree_node);
	write_sequnlock(&ns_tree->ns_tree_lock);
}
EXPORT_SYMBOL_GPL(__ns_tree_remove);

static int ns_find(const void *key, const struct rb_node *node)
{
	const u64 ns_id = *(u64 *)key;
	const struct ns_common *ns = node_to_ns(node);

	if (ns_id < ns->ns_id)
		return -1;
	if (ns_id > ns->ns_id)
		return 1;
	return 0;
}


static struct ns_tree *ns_tree_from_type(int ns_type)
{
	switch (ns_type) {
	case CLONE_NEWCGROUP:
		return &cgroup_ns_tree;
	case CLONE_NEWIPC:
		return &ipc_ns_tree;
	case CLONE_NEWNS:
		return &mnt_ns_tree;
	case CLONE_NEWNET:
		return &net_ns_tree;
	case CLONE_NEWPID:
		return &pid_ns_tree;
	case CLONE_NEWUSER:
		return &user_ns_tree;
	case CLONE_NEWUTS:
		return &uts_ns_tree;
	case CLONE_NEWTIME:
		return &time_ns_tree;
	}

	return NULL;
}

struct ns_common *ns_tree_lookup_rcu(u64 ns_id, int ns_type)
{
	struct ns_tree *ns_tree;
	struct rb_node *node;
	unsigned int seq;

	RCU_LOCKDEP_WARN(!rcu_read_lock_held(), "suspicious ns_tree_lookup_rcu() usage");

	ns_tree = ns_tree_from_type(ns_type);
	if (!ns_tree)
		return NULL;

	do {
		seq = read_seqbegin(&ns_tree->ns_tree_lock);
		node = rb_find_rcu(&ns_id, &ns_tree->ns_tree, ns_find);
		if (node)
			break;
	} while (read_seqretry(&ns_tree->ns_tree_lock, seq));

	if (!node)
		return NULL;

	VFS_WARN_ON_ONCE(node_to_ns(node)->ns_type != ns_type);

	return node_to_ns(node);
}

/**
 * ns_tree_adjoined_rcu - find the next/previous namespace in the same
 * tree
 * @ns: namespace to start from
 * @previous: if true find the previous namespace, otherwise the next
 *
 * Find the next or previous namespace in the same tree as @ns. If
 * there is no next/previous namespace, -ENOENT is returned.
 */
struct ns_common *__ns_tree_adjoined_rcu(struct ns_common *ns,
					 struct ns_tree *ns_tree, bool previous)
{
	struct list_head *list;

	RCU_LOCKDEP_WARN(!rcu_read_lock_held(), "suspicious ns_tree_adjoined_rcu() usage");

	if (previous)
		list = rcu_dereference(list_bidir_prev_rcu(&ns->ns_list_node));
	else
		list = rcu_dereference(list_next_rcu(&ns->ns_list_node));
	if (list_is_head(list, &ns_tree->ns_list))
		return ERR_PTR(-ENOENT);

	VFS_WARN_ON_ONCE(list_entry_rcu(list, struct ns_common, ns_list_node)->ns_type != ns_tree->type);

	return list_entry_rcu(list, struct ns_common, ns_list_node);
}

/**
 * ns_tree_gen_id - generate a new namespace id
 * @ns: namespace to generate id for
 *
 * Generates a new namespace id and assigns it to the namespace. All
 * namespaces types share the same id space and thus can be compared
 * directly. IOW, when two ids of two namespace are equal, they are
 * identical.
 */
u64 ns_tree_gen_id(struct ns_common *ns)
{
	guard(preempt)();
	ns->ns_id = gen_cookie_next(&namespace_cookie);
	return ns->ns_id;
}
