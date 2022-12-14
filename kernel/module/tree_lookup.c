// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Modules tree lookup
 *
 * Copyright (C) 2015 Peter Zijlstra
 * Copyright (C) 2015 Rusty Russell
 */

#include <linux/module.h>
#include <linux/rbtree_latch.h>
#include "internal.h"

/*
 * Use a latched RB-tree for __module_address(); this allows us to use
 * RCU-sched lookups of the address from any context.
 *
 * This is conditional on PERF_EVENTS || TRACING because those can really hit
 * __module_address() hard by doing a lot of stack unwinding; potentially from
 * NMI context.
 */

static __always_inline unsigned long __mod_tree_val(struct latch_tree_node *n)
{
	struct module_layout *layout = container_of(n, struct module_layout, mtn.node);

	return (unsigned long)layout->base;
}

static __always_inline unsigned long __mod_tree_size(struct latch_tree_node *n)
{
	struct module_layout *layout = container_of(n, struct module_layout, mtn.node);

	return (unsigned long)layout->size;
}

static __always_inline bool
mod_tree_less(struct latch_tree_node *a, struct latch_tree_node *b)
{
	return __mod_tree_val(a) < __mod_tree_val(b);
}

static __always_inline int
mod_tree_comp(void *key, struct latch_tree_node *n)
{
	unsigned long val = (unsigned long)key;
	unsigned long start, end;

	start = __mod_tree_val(n);
	if (val < start)
		return -1;

	end = start + __mod_tree_size(n);
	if (val >= end)
		return 1;

	return 0;
}

static const struct latch_tree_ops mod_tree_ops = {
	.less = mod_tree_less,
	.comp = mod_tree_comp,
};

static noinline void __mod_tree_insert(struct mod_tree_node *node, struct mod_tree_root *tree)
{
	latch_tree_insert(&node->node, &tree->root, &mod_tree_ops);
}

static void __mod_tree_remove(struct mod_tree_node *node, struct mod_tree_root *tree)
{
	latch_tree_erase(&node->node, &tree->root, &mod_tree_ops);
}

/*
 * These modifications: insert, remove_init and remove; are serialized by the
 * module_mutex.
 */
void mod_tree_insert(struct module *mod)
{
	mod->core_layout.mtn.mod = mod;
	mod->init_layout.mtn.mod = mod;

	__mod_tree_insert(&mod->core_layout.mtn, &mod_tree);
	if (mod->init_layout.size)
		__mod_tree_insert(&mod->init_layout.mtn, &mod_tree);

#ifdef CONFIG_ARCH_WANTS_MODULES_DATA_IN_VMALLOC
	mod->data_layout.mtn.mod = mod;
	__mod_tree_insert(&mod->data_layout.mtn, &mod_data_tree);
#endif
}

void mod_tree_remove_init(struct module *mod)
{
	if (mod->init_layout.size)
		__mod_tree_remove(&mod->init_layout.mtn, &mod_tree);
}

void mod_tree_remove(struct module *mod)
{
	__mod_tree_remove(&mod->core_layout.mtn, &mod_tree);
	mod_tree_remove_init(mod);
#ifdef CONFIG_ARCH_WANTS_MODULES_DATA_IN_VMALLOC
	__mod_tree_remove(&mod->data_layout.mtn, &mod_data_tree);
#endif
}

struct module *mod_find(unsigned long addr, struct mod_tree_root *tree)
{
	struct latch_tree_node *ltn;

	ltn = latch_tree_find((void *)addr, &tree->root, &mod_tree_ops);
	if (!ltn)
		return NULL;

	return container_of(ltn, struct mod_tree_node, node)->mod;
}
