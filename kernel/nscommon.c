// SPDX-License-Identifier: GPL-2.0-only

#include <linux/ns_common.h>

int ns_common_init(struct ns_common *ns, const struct proc_ns_operations *ops,
		   bool alloc_inum)
{
	if (alloc_inum && !ns->inum) {
		int ret;
		ret = proc_alloc_inum(&ns->inum);
		if (ret)
			return ret;
	}
	refcount_set(&ns->count, 1);
	ns->stashed = NULL;
	ns->ops = ops;
	ns->ns_id = 0;
	RB_CLEAR_NODE(&ns->ns_tree_node);
	INIT_LIST_HEAD(&ns->ns_list_node);
	return 0;
}
