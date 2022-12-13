// SPDX-License-Identifier: GPL-2.0+
/*
 * maple_tree.c: Userspace shim for maple tree test-suite
 * Copyright (c) 2018 Liam R. Howlett <Liam.Howlett@Oracle.com>
 */

#define CONFIG_DEBUG_MAPLE_TREE
#define CONFIG_MAPLE_SEARCH
#include "test.h"

#define module_init(x)
#define module_exit(x)
#define MODULE_AUTHOR(x)
#define MODULE_LICENSE(x)
#define dump_stack()	assert(0)

#include "../../../lib/maple_tree.c"
#undef CONFIG_DEBUG_MAPLE_TREE
#include "../../../lib/test_maple_tree.c"

void farmer_tests(void)
{
	struct maple_node *node;
	DEFINE_MTREE(tree);

	mt_dump(&tree);

	tree.ma_root = xa_mk_value(0);
	mt_dump(&tree);

	node = mt_alloc_one(GFP_KERNEL);
	node->parent = (void *)((unsigned long)(&tree) | 1);
	node->slot[0] = xa_mk_value(0);
	node->slot[1] = xa_mk_value(1);
	node->mr64.pivot[0] = 0;
	node->mr64.pivot[1] = 1;
	node->mr64.pivot[2] = 0;
	tree.ma_root = mt_mk_node(node, maple_leaf_64);
	mt_dump(&tree);

	ma_free_rcu(node);
}

void maple_tree_tests(void)
{
	farmer_tests();
	maple_tree_seed();
	maple_tree_harvest();
}

int __weak main(void)
{
	maple_tree_init();
	maple_tree_tests();
	rcu_barrier();
	if (nr_allocated)
		printf("nr_allocated = %d\n", nr_allocated);
	return 0;
}
