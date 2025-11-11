// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2025 Christian Brauner <brauner@kernel.org> */

#include <linux/ns_common.h>
#include <linux/nstree.h>
#include <linux/proc_ns.h>
#include <linux/user_namespace.h>
#include <linux/vfsdebug.h>

#ifdef CONFIG_DEBUG_VFS
static void ns_debug(struct ns_common *ns, const struct proc_ns_operations *ops)
{
	switch (ns->ns_type) {
#ifdef CONFIG_CGROUPS
	case CLONE_NEWCGROUP:
		VFS_WARN_ON_ONCE(ops != &cgroupns_operations);
		break;
#endif
#ifdef CONFIG_IPC_NS
	case CLONE_NEWIPC:
		VFS_WARN_ON_ONCE(ops != &ipcns_operations);
		break;
#endif
	case CLONE_NEWNS:
		VFS_WARN_ON_ONCE(ops != &mntns_operations);
		break;
#ifdef CONFIG_NET_NS
	case CLONE_NEWNET:
		VFS_WARN_ON_ONCE(ops != &netns_operations);
		break;
#endif
#ifdef CONFIG_PID_NS
	case CLONE_NEWPID:
		VFS_WARN_ON_ONCE(ops != &pidns_operations);
		break;
#endif
#ifdef CONFIG_TIME_NS
	case CLONE_NEWTIME:
		VFS_WARN_ON_ONCE(ops != &timens_operations);
		break;
#endif
#ifdef CONFIG_USER_NS
	case CLONE_NEWUSER:
		VFS_WARN_ON_ONCE(ops != &userns_operations);
		break;
#endif
#ifdef CONFIG_UTS_NS
	case CLONE_NEWUTS:
		VFS_WARN_ON_ONCE(ops != &utsns_operations);
		break;
#endif
	}
}
#endif

int __ns_common_init(struct ns_common *ns, u32 ns_type, const struct proc_ns_operations *ops, int inum)
{
	int ret = 0;

	refcount_set(&ns->__ns_ref, 1);
	ns->stashed = NULL;
	ns->ops = ops;
	ns->ns_id = 0;
	ns->ns_type = ns_type;
	ns_tree_node_init(&ns->ns_tree_node);
	ns_tree_node_init(&ns->ns_unified_node);
	ns_tree_node_init(&ns->ns_owner_node);
	ns_tree_root_init(&ns->ns_owner_root);

#ifdef CONFIG_DEBUG_VFS
	ns_debug(ns, ops);
#endif

	if (inum)
		ns->inum = inum;
	else
		ret = proc_alloc_inum(&ns->inum);
	if (ret)
		return ret;
	/*
	 * Tree ref starts at 0. It's incremented when namespace enters
	 * active use (installed in nsproxy) and decremented when all
	 * active uses are gone. Initial namespaces are always active.
	 */
	if (is_ns_init_inum(ns))
		atomic_set(&ns->__ns_ref_active, 1);
	else
		atomic_set(&ns->__ns_ref_active, 0);
	return 0;
}

void __ns_common_free(struct ns_common *ns)
{
	proc_free_inum(ns->inum);
}

struct ns_common *__must_check ns_owner(struct ns_common *ns)
{
	struct user_namespace *owner;

	if (unlikely(!ns->ops))
		return NULL;
	VFS_WARN_ON_ONCE(!ns->ops->owner);
	owner = ns->ops->owner(ns);
	VFS_WARN_ON_ONCE(!owner && ns != to_ns_common(&init_user_ns));
	if (!owner)
		return NULL;
	/* Skip init_user_ns as it's always active */
	if (owner == &init_user_ns)
		return NULL;
	return to_ns_common(owner);
}

/*
 * The active reference count works by having each namespace that gets
 * created take a single active reference on its owning user namespace.
 * That single reference is only released once the child namespace's
 * active count itself goes down.
 *
 * A regular namespace tree might look as follow:
 * Legend:
 * + : adding active reference
 * - : dropping active reference
 * x : always active (initial namespace)
 *
 *
 *                 net_ns          pid_ns
 *                       \        /
 *                        +      +
 *                        user_ns1 (2)
 *                            |
 *                 ipc_ns     |     uts_ns
 *                       \    |    /
 *                        +   +   +
 *                        user_ns2 (3)
 *                            |
 *            cgroup_ns       |       mnt_ns
 *                     \      |      /
 *                      x     x     x
 *                      init_user_ns (1)
 *
 * If both net_ns and pid_ns put their last active reference on
 * themselves it will cascade to user_ns1 dropping its own active
 * reference and dropping one active reference on user_ns2:
 *
 *                 net_ns          pid_ns
 *                       \        /
 *                        -      -
 *                        user_ns1 (0)
 *                            |
 *                 ipc_ns     |     uts_ns
 *                       \    |    /
 *                        +   -   +
 *                        user_ns2 (2)
 *                            |
 *            cgroup_ns       |       mnt_ns
 *                     \      |      /
 *                      x     x     x
 *                      init_user_ns (1)
 *
 * The iteration stops once we reach a namespace that still has active
 * references.
 */
void __ns_ref_active_put(struct ns_common *ns)
{
	/* Initial namespaces are always active. */
	if (is_ns_init_id(ns))
		return;

	if (!atomic_dec_and_test(&ns->__ns_ref_active)) {
		VFS_WARN_ON_ONCE(__ns_ref_active_read(ns) < 0);
		return;
	}

	VFS_WARN_ON_ONCE(is_ns_init_id(ns));
	VFS_WARN_ON_ONCE(!__ns_ref_read(ns));

	for (;;) {
		ns = ns_owner(ns);
		if (!ns)
			return;
		VFS_WARN_ON_ONCE(is_ns_init_id(ns));
		if (!atomic_dec_and_test(&ns->__ns_ref_active)) {
			VFS_WARN_ON_ONCE(__ns_ref_active_read(ns) < 0);
			return;
		}
	}
}

/*
 * The active reference count works by having each namespace that gets
 * created take a single active reference on its owning user namespace.
 * That single reference is only released once the child namespace's
 * active count itself goes down. This makes it possible to efficiently
 * resurrect a namespace tree:
 *
 * A regular namespace tree might look as follow:
 * Legend:
 * + : adding active reference
 * - : dropping active reference
 * x : always active (initial namespace)
 *
 *
 *                 net_ns          pid_ns
 *                       \        /
 *                        +      +
 *                        user_ns1 (2)
 *                            |
 *                 ipc_ns     |     uts_ns
 *                       \    |    /
 *                        +   +   +
 *                        user_ns2 (3)
 *                            |
 *            cgroup_ns       |       mnt_ns
 *                     \      |      /
 *                      x     x     x
 *                      init_user_ns (1)
 *
 * If both net_ns and pid_ns put their last active reference on
 * themselves it will cascade to user_ns1 dropping its own active
 * reference and dropping one active reference on user_ns2:
 *
 *                 net_ns          pid_ns
 *                       \        /
 *                        -      -
 *                        user_ns1 (0)
 *                            |
 *                 ipc_ns     |     uts_ns
 *                       \    |    /
 *                        +   -   +
 *                        user_ns2 (2)
 *                            |
 *            cgroup_ns       |       mnt_ns
 *                     \      |      /
 *                      x     x     x
 *                      init_user_ns (1)
 *
 * Assume the whole tree is dead but all namespaces are still active:
 *
 *                 net_ns          pid_ns
 *                       \        /
 *                        -      -
 *                        user_ns1 (0)
 *                            |
 *                 ipc_ns     |     uts_ns
 *                       \    |    /
 *                        -   -   -
 *                        user_ns2 (0)
 *                            |
 *            cgroup_ns       |       mnt_ns
 *                     \      |      /
 *                      x     x     x
 *                      init_user_ns (1)
 *
 * Now assume the net_ns gets resurrected (.e.g., via the SIOCGSKNS ioctl()):
 *
 *                 net_ns          pid_ns
 *                       \        /
 *                        +      -
 *                        user_ns1 (0)
 *                            |
 *                 ipc_ns     |     uts_ns
 *                       \    |    /
 *                        -   +   -
 *                        user_ns2 (0)
 *                            |
 *            cgroup_ns       |       mnt_ns
 *                     \      |      /
 *                      x     x     x
 *                      init_user_ns (1)
 *
 * If net_ns had a zero reference count and we bumped it we also need to
 * take another reference on its owning user namespace. Similarly, if
 * pid_ns had a zero reference count it also needs to take another
 * reference on its owning user namespace. So both net_ns and pid_ns
 * will each have their own reference on the owning user namespace.
 *
 * If the owning user namespace user_ns1 had a zero reference count then
 * it also needs to take another reference on its owning user namespace
 * and so on.
 */
void __ns_ref_active_get(struct ns_common *ns)
{
	int prev;

	/* Initial namespaces are always active. */
	if (is_ns_init_id(ns))
		return;

	/* If we didn't resurrect the namespace we're done. */
	prev = atomic_fetch_add(1, &ns->__ns_ref_active);
	VFS_WARN_ON_ONCE(prev < 0);
	if (likely(prev))
		return;

	/*
	 * We did resurrect it. Walk the ownership hierarchy upwards
	 * until we found an owning user namespace that is active.
	 */
	for (;;) {
		ns = ns_owner(ns);
		if (!ns)
			return;

		VFS_WARN_ON_ONCE(is_ns_init_id(ns));
		prev = atomic_fetch_add(1, &ns->__ns_ref_active);
		VFS_WARN_ON_ONCE(prev < 0);
		if (likely(prev))
			return;
	}
}
