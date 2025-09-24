// SPDX-License-Identifier: GPL-2.0-only

#include <linux/ns_common.h>
#include <linux/proc_ns.h>
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
	refcount_set(&ns->__ns_ref, 1);
	ns->stashed = NULL;
	ns->ops = ops;
	ns->ns_id = 0;
	ns->ns_type = ns_type;
	RB_CLEAR_NODE(&ns->ns_tree_node);
	INIT_LIST_HEAD(&ns->ns_list_node);

#ifdef CONFIG_DEBUG_VFS
	ns_debug(ns, ops);
#endif

	if (inum) {
		ns->inum = inum;
		return 0;
	}
	return proc_alloc_inum(&ns->inum);
}

void __ns_common_free(struct ns_common *ns)
{
	proc_free_inum(ns->inum);
}
