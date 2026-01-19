/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MEMORY_FAILURE_H
#define _LINUX_MEMORY_FAILURE_H

#include <linux/interval_tree.h>

struct pfn_address_space;

struct pfn_address_space {
	struct interval_tree_node node;
	struct address_space *mapping;
	int (*pfn_to_vma_pgoff)(struct vm_area_struct *vma,
				unsigned long pfn, pgoff_t *pgoff);
};

int register_pfn_address_space(struct pfn_address_space *pfn_space);
void unregister_pfn_address_space(struct pfn_address_space *pfn_space);

#endif /* _LINUX_MEMORY_FAILURE_H */
