// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2025 Christian Brauner <brauner@kernel.org> */

#include <linux/nstree.h>
#include <linux/proc_ns.h>
#include <linux/rculist.h>
#include <linux/vfsdebug.h>
#include <linux/syscalls.h>
#include <linux/user_namespace.h>

static __cacheline_aligned_in_smp DEFINE_SEQLOCK(ns_tree_lock);

DEFINE_LOCK_GUARD_0(ns_tree_writer,
		    write_seqlock(&ns_tree_lock),
		    write_sequnlock(&ns_tree_lock))

DEFINE_LOCK_GUARD_0(ns_tree_locked_reader,
		    read_seqlock_excl(&ns_tree_lock),
		    read_sequnlock_excl(&ns_tree_lock))

static struct ns_tree_root ns_unified_root = { /* protected by ns_tree_lock */
	.ns_rb = RB_ROOT,
	.ns_list_head = LIST_HEAD_INIT(ns_unified_root.ns_list_head),
};

struct ns_tree_root mnt_ns_tree = {
	.ns_rb = RB_ROOT,
	.ns_list_head = LIST_HEAD_INIT(mnt_ns_tree.ns_list_head),
};

struct ns_tree_root net_ns_tree = {
	.ns_rb = RB_ROOT,
	.ns_list_head = LIST_HEAD_INIT(net_ns_tree.ns_list_head),
};
EXPORT_SYMBOL_GPL(net_ns_tree);

struct ns_tree_root uts_ns_tree = {
	.ns_rb = RB_ROOT,
	.ns_list_head = LIST_HEAD_INIT(uts_ns_tree.ns_list_head),
};

struct ns_tree_root user_ns_tree = {
	.ns_rb = RB_ROOT,
	.ns_list_head = LIST_HEAD_INIT(user_ns_tree.ns_list_head),
};

struct ns_tree_root ipc_ns_tree = {
	.ns_rb = RB_ROOT,
	.ns_list_head = LIST_HEAD_INIT(ipc_ns_tree.ns_list_head),
};

struct ns_tree_root pid_ns_tree = {
	.ns_rb = RB_ROOT,
	.ns_list_head = LIST_HEAD_INIT(pid_ns_tree.ns_list_head),
};

struct ns_tree_root cgroup_ns_tree = {
	.ns_rb = RB_ROOT,
	.ns_list_head = LIST_HEAD_INIT(cgroup_ns_tree.ns_list_head),
};

struct ns_tree_root time_ns_tree = {
	.ns_rb = RB_ROOT,
	.ns_list_head = LIST_HEAD_INIT(time_ns_tree.ns_list_head),
};

/**
 * ns_tree_node_init - Initialize a namespace tree node
 * @node: The node to initialize
 *
 * Initializes both the rbtree node and list entry.
 */
void ns_tree_node_init(struct ns_tree_node *node)
{
	RB_CLEAR_NODE(&node->ns_node);
	INIT_LIST_HEAD(&node->ns_list_entry);
}

/**
 * ns_tree_root_init - Initialize a namespace tree root
 * @root: The root to initialize
 *
 * Initializes both the rbtree root and list head.
 */
void ns_tree_root_init(struct ns_tree_root *root)
{
	root->ns_rb = RB_ROOT;
	INIT_LIST_HEAD(&root->ns_list_head);
}

/**
 * ns_tree_node_empty - Check if a namespace tree node is empty
 * @node: The node to check
 *
 * Returns true if the node is not in any tree.
 */
bool ns_tree_node_empty(const struct ns_tree_node *node)
{
	return RB_EMPTY_NODE(&node->ns_node);
}

/**
 * ns_tree_node_add - Add a node to a namespace tree
 * @node: The node to add
 * @root: The tree root to add to
 * @cmp: Comparison function for rbtree insertion
 *
 * Adds the node to both the rbtree and the list, maintaining sorted order.
 * The list is maintained in the same order as the rbtree to enable efficient
 * iteration.
 *
 * Returns: NULL if insertion succeeded, existing node if duplicate found
 */
struct rb_node *ns_tree_node_add(struct ns_tree_node *node,
				  struct ns_tree_root *root,
				  int (*cmp)(struct rb_node *, const struct rb_node *))
{
	struct rb_node *ret, *prev;

	/* Add to rbtree */
	ret = rb_find_add_rcu(&node->ns_node, &root->ns_rb, cmp);

	/* Add to list in sorted order */
	prev = rb_prev(&node->ns_node);
	if (!prev) {
		/* No previous node, add at head */
		list_add_rcu(&node->ns_list_entry, &root->ns_list_head);
	} else {
		/* Add after previous node */
		struct ns_tree_node *prev_node;
		prev_node = rb_entry(prev, struct ns_tree_node, ns_node);
		list_add_rcu(&node->ns_list_entry, &prev_node->ns_list_entry);
	}

	return ret;
}

/**
 * ns_tree_node_del - Remove a node from a namespace tree
 * @node: The node to remove
 * @root: The tree root to remove from
 *
 * Removes the node from both the rbtree and the list atomically.
 */
void ns_tree_node_del(struct ns_tree_node *node, struct ns_tree_root *root)
{
	rb_erase(&node->ns_node, &root->ns_rb);
	RB_CLEAR_NODE(&node->ns_node);
	list_bidir_del_rcu(&node->ns_list_entry);
}

static inline struct ns_common *node_to_ns(const struct rb_node *node)
{
	if (!node)
		return NULL;
	return rb_entry(node, struct ns_common, ns_tree_node.ns_node);
}

static inline struct ns_common *node_to_ns_unified(const struct rb_node *node)
{
	if (!node)
		return NULL;
	return rb_entry(node, struct ns_common, ns_unified_node.ns_node);
}

static inline struct ns_common *node_to_ns_owner(const struct rb_node *node)
{
	if (!node)
		return NULL;
	return rb_entry(node, struct ns_common, ns_owner_node.ns_node);
}

static int ns_id_cmp(u64 id_a, u64 id_b)
{
	if (id_a < id_b)
		return -1;
	if (id_a > id_b)
		return 1;
	return 0;
}

static int ns_cmp(struct rb_node *a, const struct rb_node *b)
{
	return ns_id_cmp(node_to_ns(a)->ns_id, node_to_ns(b)->ns_id);
}

static int ns_cmp_unified(struct rb_node *a, const struct rb_node *b)
{
	return ns_id_cmp(node_to_ns_unified(a)->ns_id, node_to_ns_unified(b)->ns_id);
}

static int ns_cmp_owner(struct rb_node *a, const struct rb_node *b)
{
	return ns_id_cmp(node_to_ns_owner(a)->ns_id, node_to_ns_owner(b)->ns_id);
}

void __ns_tree_add_raw(struct ns_common *ns, struct ns_tree_root *ns_tree)
{
	struct rb_node *node;
	const struct proc_ns_operations *ops = ns->ops;

	VFS_WARN_ON_ONCE(!ns->ns_id);

	guard(ns_tree_writer)();

	/* Add to per-type tree and list */
	node = ns_tree_node_add(&ns->ns_tree_node, ns_tree, ns_cmp);

	/* Add to unified tree and list */
	ns_tree_node_add(&ns->ns_unified_node, &ns_unified_root, ns_cmp_unified);

	/* Add to owner's tree if applicable */
	if (ops) {
		struct user_namespace *user_ns;

		VFS_WARN_ON_ONCE(!ops->owner);
		user_ns = ops->owner(ns);
		if (user_ns) {
			struct ns_common *owner = &user_ns->ns;
			VFS_WARN_ON_ONCE(owner->ns_type != CLONE_NEWUSER);

			/* Insert into owner's tree and list */
			ns_tree_node_add(&ns->ns_owner_node, &owner->ns_owner_root, ns_cmp_owner);
		} else {
			/* Only the initial user namespace doesn't have an owner. */
			VFS_WARN_ON_ONCE(ns != to_ns_common(&init_user_ns));
		}
	}

	VFS_WARN_ON_ONCE(node);
}

void __ns_tree_remove(struct ns_common *ns, struct ns_tree_root *ns_tree)
{
	const struct proc_ns_operations *ops = ns->ops;
	struct user_namespace *user_ns;

	VFS_WARN_ON_ONCE(ns_tree_node_empty(&ns->ns_tree_node));
	VFS_WARN_ON_ONCE(list_empty(&ns->ns_tree_node.ns_list_entry));

	write_seqlock(&ns_tree_lock);

	/* Remove from per-type tree and list */
	ns_tree_node_del(&ns->ns_tree_node, ns_tree);

	/* Remove from unified tree and list */
	ns_tree_node_del(&ns->ns_unified_node, &ns_unified_root);

	/* Remove from owner's tree if applicable */
	if (ops) {
		user_ns = ops->owner(ns);
		if (user_ns) {
			struct ns_common *owner = &user_ns->ns;
			ns_tree_node_del(&ns->ns_owner_node, &owner->ns_owner_root);
		}
	}

	write_sequnlock(&ns_tree_lock);
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

static int ns_find_unified(const void *key, const struct rb_node *node)
{
	const u64 ns_id = *(u64 *)key;
	const struct ns_common *ns = node_to_ns_unified(node);

	if (ns_id < ns->ns_id)
		return -1;
	if (ns_id > ns->ns_id)
		return 1;
	return 0;
}

static struct ns_tree_root *ns_tree_from_type(int ns_type)
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

static struct ns_common *__ns_unified_tree_lookup_rcu(u64 ns_id)
{
	struct rb_node *node;
	unsigned int seq;

	do {
		seq = read_seqbegin(&ns_tree_lock);
		node = rb_find_rcu(&ns_id, &ns_unified_root.ns_rb, ns_find_unified);
		if (node)
			break;
	} while (read_seqretry(&ns_tree_lock, seq));

	return node_to_ns_unified(node);
}

static struct ns_common *__ns_tree_lookup_rcu(u64 ns_id, int ns_type)
{
	struct ns_tree_root *ns_tree;
	struct rb_node *node;
	unsigned int seq;

	ns_tree = ns_tree_from_type(ns_type);
	if (!ns_tree)
		return NULL;

	do {
		seq = read_seqbegin(&ns_tree_lock);
		node = rb_find_rcu(&ns_id, &ns_tree->ns_rb, ns_find);
		if (node)
			break;
	} while (read_seqretry(&ns_tree_lock, seq));

	return node_to_ns(node);
}

struct ns_common *ns_tree_lookup_rcu(u64 ns_id, int ns_type)
{
	RCU_LOCKDEP_WARN(!rcu_read_lock_held(), "suspicious ns_tree_lookup_rcu() usage");

	if (ns_type)
		return __ns_tree_lookup_rcu(ns_id, ns_type);

	return __ns_unified_tree_lookup_rcu(ns_id);
}

/**
 * __ns_tree_adjoined_rcu - find the next/previous namespace in the same
 * tree
 * @ns: namespace to start from
 * @ns_tree: namespace tree to search in
 * @previous: if true find the previous namespace, otherwise the next
 *
 * Find the next or previous namespace in the same tree as @ns. If
 * there is no next/previous namespace, -ENOENT is returned.
 */
struct ns_common *__ns_tree_adjoined_rcu(struct ns_common *ns,
					 struct ns_tree_root *ns_tree, bool previous)
{
	struct list_head *list;

	RCU_LOCKDEP_WARN(!rcu_read_lock_held(), "suspicious ns_tree_adjoined_rcu() usage");

	if (previous)
		list = rcu_dereference(list_bidir_prev_rcu(&ns->ns_tree_node.ns_list_entry));
	else
		list = rcu_dereference(list_next_rcu(&ns->ns_tree_node.ns_list_entry));
	if (list_is_head(list, &ns_tree->ns_list_head))
		return ERR_PTR(-ENOENT);

	return list_entry_rcu(list, struct ns_common, ns_tree_node.ns_list_entry);
}

/**
 * __ns_tree_gen_id - generate a new namespace id
 * @ns: namespace to generate id for
 * @id: if non-zero, this is the initial namespace and this is a fixed id
 *
 * Generates a new namespace id and assigns it to the namespace. All
 * namespaces types share the same id space and thus can be compared
 * directly. IOW, when two ids of two namespace are equal, they are
 * identical.
 */
u64 __ns_tree_gen_id(struct ns_common *ns, u64 id)
{
	static atomic64_t namespace_cookie = ATOMIC64_INIT(NS_LAST_INIT_ID + 1);

	if (id)
		ns->ns_id = id;
	else
		ns->ns_id = atomic64_inc_return(&namespace_cookie);
	return ns->ns_id;
}

struct klistns {
	u64 __user *uns_ids;
	u32 nr_ns_ids;
	u64 last_ns_id;
	u64 user_ns_id;
	u32 ns_type;
	struct user_namespace *user_ns;
	bool userns_capable;
	struct ns_common *first_ns;
};

static void __free_klistns_free(const struct klistns *kls)
{
	if (kls->user_ns_id != LISTNS_CURRENT_USER)
		put_user_ns(kls->user_ns);
	if (kls->first_ns && kls->first_ns->ops)
		kls->first_ns->ops->put(kls->first_ns);
}

#define NS_ALL (PID_NS | USER_NS | MNT_NS | UTS_NS | IPC_NS | NET_NS | CGROUP_NS | TIME_NS)

static int copy_ns_id_req(const struct ns_id_req __user *req,
			  struct ns_id_req *kreq)
{
	int ret;
	size_t usize;

	BUILD_BUG_ON(sizeof(struct ns_id_req) != NS_ID_REQ_SIZE_VER0);

	ret = get_user(usize, &req->size);
	if (ret)
		return -EFAULT;
	if (unlikely(usize > PAGE_SIZE))
		return -E2BIG;
	if (unlikely(usize < NS_ID_REQ_SIZE_VER0))
		return -EINVAL;
	memset(kreq, 0, sizeof(*kreq));
	ret = copy_struct_from_user(kreq, sizeof(*kreq), req, usize);
	if (ret)
		return ret;
	if (kreq->spare != 0)
		return -EINVAL;
	if (kreq->ns_type & ~NS_ALL)
		return -EOPNOTSUPP;
	return 0;
}

static inline int prepare_klistns(struct klistns *kls, struct ns_id_req *kreq,
				  u64 __user *ns_ids, size_t nr_ns_ids)
{
	kls->last_ns_id = kreq->ns_id;
	kls->user_ns_id = kreq->user_ns_id;
	kls->nr_ns_ids	= nr_ns_ids;
	kls->ns_type	= kreq->ns_type;
	kls->uns_ids	= ns_ids;
	return 0;
}

/*
 * Lookup a namespace owned by owner with id >= ns_id.
 * Returns the namespace with the smallest id that is >= ns_id.
 */
static struct ns_common *lookup_ns_owner_at(u64 ns_id, struct ns_common *owner)
{
	struct ns_common *ret = NULL;
	struct rb_node *node;

	VFS_WARN_ON_ONCE(owner->ns_type != CLONE_NEWUSER);

	guard(ns_tree_locked_reader)();

	node = owner->ns_owner_root.ns_rb.rb_node;
	while (node) {
		struct ns_common *ns;

		ns = node_to_ns_owner(node);
		if (ns_id <= ns->ns_id) {
			ret = ns;
			if (ns_id == ns->ns_id)
				break;
			node = node->rb_left;
		} else {
			node = node->rb_right;
		}
	}

	if (ret)
		ret = ns_get_unless_inactive(ret);
	return ret;
}

static struct ns_common *lookup_ns_id(u64 mnt_ns_id, int ns_type)
{
	struct ns_common *ns;

	guard(rcu)();
	ns = ns_tree_lookup_rcu(mnt_ns_id, ns_type);
	if (!ns)
		return NULL;

	if (!ns_get_unless_inactive(ns))
		return NULL;

	return ns;
}

static inline bool __must_check ns_requested(const struct klistns *kls,
					     const struct ns_common *ns)
{
	return !kls->ns_type || (kls->ns_type & ns->ns_type);
}

static inline bool __must_check may_list_ns(const struct klistns *kls,
					    struct ns_common *ns)
{
	if (kls->user_ns) {
		if (kls->userns_capable)
			return true;
	} else {
		struct ns_common *owner;
		struct user_namespace *user_ns;

		owner = ns_owner(ns);
		if (owner)
			user_ns = to_user_ns(owner);
		else
			user_ns = &init_user_ns;
		if (ns_capable_noaudit(user_ns, CAP_SYS_ADMIN))
			return true;
	}

	if (is_current_namespace(ns))
		return true;

	if (ns->ns_type != CLONE_NEWUSER)
		return false;

	if (ns_capable_noaudit(to_user_ns(ns), CAP_SYS_ADMIN))
		return true;

	return false;
}

static inline void ns_put(struct ns_common *ns)
{
	if (ns && ns->ops)
		ns->ops->put(ns);
}

DEFINE_FREE(ns_put, struct ns_common *, if (!IS_ERR_OR_NULL(_T)) ns_put(_T))

static inline struct ns_common *__must_check legitimize_ns(const struct klistns *kls,
							   struct ns_common *candidate)
{
	struct ns_common *ns __free(ns_put) = NULL;

	if (!ns_requested(kls, candidate))
		return NULL;

	ns = ns_get_unless_inactive(candidate);
	if (!ns)
		return NULL;

	if (!may_list_ns(kls, ns))
		return NULL;

	return no_free_ptr(ns);
}

static ssize_t do_listns_userns(struct klistns *kls)
{
	u64 __user *ns_ids = kls->uns_ids;
	size_t nr_ns_ids = kls->nr_ns_ids;
	struct ns_common *ns = NULL, *first_ns = NULL, *prev = NULL;
	const struct list_head *head;
	ssize_t ret;

	VFS_WARN_ON_ONCE(!kls->user_ns_id);

	if (kls->user_ns_id == LISTNS_CURRENT_USER)
		ns = to_ns_common(current_user_ns());
	else if (kls->user_ns_id)
		ns = lookup_ns_id(kls->user_ns_id, CLONE_NEWUSER);
	if (!ns)
		return -EINVAL;
	kls->user_ns = to_user_ns(ns);

	/*
	 * Use the rbtree to find the first namespace we care about and
	 * then use it's list entry to iterate from there.
	 */
	if (kls->last_ns_id) {
		kls->first_ns = lookup_ns_owner_at(kls->last_ns_id + 1, ns);
		if (!kls->first_ns)
			return -ENOENT;
		first_ns = kls->first_ns;
	}

	ret = 0;
	head = &to_ns_common(kls->user_ns)->ns_owner_root.ns_list_head;
	kls->userns_capable = ns_capable_noaudit(kls->user_ns, CAP_SYS_ADMIN);

	rcu_read_lock();

	if (!first_ns)
		first_ns = list_entry_rcu(head->next, typeof(*first_ns), ns_owner_node.ns_list_entry);

	ns = first_ns;
	list_for_each_entry_from_rcu(ns, head, ns_owner_node.ns_list_entry) {
		struct ns_common *valid;

		if (!nr_ns_ids)
			break;

		valid = legitimize_ns(kls, ns);
		if (!valid)
			continue;

		rcu_read_unlock();

		ns_put(prev);
		prev = valid;

		if (put_user(valid->ns_id, ns_ids + ret)) {
			ns_put(prev);
			return -EFAULT;
		}

		nr_ns_ids--;
		ret++;

		rcu_read_lock();
	}

	rcu_read_unlock();
	ns_put(prev);
	return ret;
}

/*
 * Lookup a namespace with id >= ns_id in either the unified tree or a type-specific tree.
 * Returns the namespace with the smallest id that is >= ns_id.
 */
static struct ns_common *lookup_ns_id_at(u64 ns_id, int ns_type)
{
	struct ns_common *ret = NULL;
	struct ns_tree_root *ns_tree = NULL;
	struct rb_node *node;

	if (ns_type) {
		ns_tree = ns_tree_from_type(ns_type);
		if (!ns_tree)
			return NULL;
	}

	guard(ns_tree_locked_reader)();

	if (ns_tree)
		node = ns_tree->ns_rb.rb_node;
	else
		node = ns_unified_root.ns_rb.rb_node;

	while (node) {
		struct ns_common *ns;

		if (ns_type)
			ns = node_to_ns(node);
		else
			ns = node_to_ns_unified(node);

		if (ns_id <= ns->ns_id) {
			if (ns_type)
				ret = node_to_ns(node);
			else
				ret = node_to_ns_unified(node);
			if (ns_id == ns->ns_id)
				break;
			node = node->rb_left;
		} else {
			node = node->rb_right;
		}
	}

	if (ret)
		ret = ns_get_unless_inactive(ret);
	return ret;
}

static inline struct ns_common *first_ns_common(const struct list_head *head,
						struct ns_tree_root *ns_tree)
{
	if (ns_tree)
		return list_entry_rcu(head->next, struct ns_common, ns_tree_node.ns_list_entry);
	return list_entry_rcu(head->next, struct ns_common, ns_unified_node.ns_list_entry);
}

static inline struct ns_common *next_ns_common(struct ns_common *ns,
					       struct ns_tree_root *ns_tree)
{
	if (ns_tree)
		return list_entry_rcu(ns->ns_tree_node.ns_list_entry.next, struct ns_common, ns_tree_node.ns_list_entry);
	return list_entry_rcu(ns->ns_unified_node.ns_list_entry.next, struct ns_common, ns_unified_node.ns_list_entry);
}

static inline bool ns_common_is_head(struct ns_common *ns,
				     const struct list_head *head,
				     struct ns_tree_root *ns_tree)
{
	if (ns_tree)
		return &ns->ns_tree_node.ns_list_entry == head;
	return &ns->ns_unified_node.ns_list_entry == head;
}

static ssize_t do_listns(struct klistns *kls)
{
	u64 __user *ns_ids = kls->uns_ids;
	size_t nr_ns_ids = kls->nr_ns_ids;
	struct ns_common *ns, *first_ns = NULL, *prev = NULL;
	struct ns_tree_root *ns_tree = NULL;
	const struct list_head *head;
	u32 ns_type;
	ssize_t ret;

	if (hweight32(kls->ns_type) == 1)
		ns_type = kls->ns_type;
	else
		ns_type = 0;

	if (ns_type) {
		ns_tree = ns_tree_from_type(ns_type);
		if (!ns_tree)
			return -EINVAL;
	}

	if (kls->last_ns_id) {
		kls->first_ns = lookup_ns_id_at(kls->last_ns_id + 1, ns_type);
		if (!kls->first_ns)
			return -ENOENT;
		first_ns = kls->first_ns;
	}

	ret = 0;
	if (ns_tree)
		head = &ns_tree->ns_list_head;
	else
		head = &ns_unified_root.ns_list_head;

	rcu_read_lock();

	if (!first_ns)
		first_ns = first_ns_common(head, ns_tree);

	for (ns = first_ns; !ns_common_is_head(ns, head, ns_tree) && nr_ns_ids;
	     ns = next_ns_common(ns, ns_tree)) {
		struct ns_common *valid;

		valid = legitimize_ns(kls, ns);
		if (!valid)
			continue;

		rcu_read_unlock();

		ns_put(prev);
		prev = valid;

		if (put_user(valid->ns_id, ns_ids + ret)) {
			ns_put(prev);
			return -EFAULT;
		}

		nr_ns_ids--;
		ret++;

		rcu_read_lock();
	}

	rcu_read_unlock();
	ns_put(prev);
	return ret;
}

SYSCALL_DEFINE4(listns, const struct ns_id_req __user *, req,
		u64 __user *, ns_ids, size_t, nr_ns_ids, unsigned int, flags)
{
	struct klistns klns __free(klistns_free) = {};
	const size_t maxcount = 1000000;
	struct ns_id_req kreq;
	ssize_t ret;

	if (flags)
		return -EINVAL;

	if (unlikely(nr_ns_ids > maxcount))
		return -EOVERFLOW;

	if (!access_ok(ns_ids, nr_ns_ids * sizeof(*ns_ids)))
		return -EFAULT;

	ret = copy_ns_id_req(req, &kreq);
	if (ret)
		return ret;

	ret = prepare_klistns(&klns, &kreq, ns_ids, nr_ns_ids);
	if (ret)
		return ret;

	if (kreq.user_ns_id)
		return do_listns_userns(&klns);

	return do_listns(&klns);
}
