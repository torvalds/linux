// SPDX-License-Identifier: GPL-2.0-only

#include <linux/ns_common.h>
#include <linux/proc_ns.h>

int __ns_common_init(struct ns_common *ns, const struct proc_ns_operations *ops, int inum)
{
	refcount_set(&ns->__ns_ref, 1);
	ns->stashed = NULL;
	ns->ops = ops;
	ns->ns_id = 0;
	RB_CLEAR_NODE(&ns->ns_tree_node);
	INIT_LIST_HEAD(&ns->ns_list_node);

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
