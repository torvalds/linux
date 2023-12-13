/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PERF_ANNOTATE_DATA_H
#define _PERF_ANNOTATE_DATA_H

#include <errno.h>
#include <linux/compiler.h>
#include <linux/rbtree.h>
#include <linux/types.h>

struct map_symbol;

/**
 * struct annotated_member - Type of member field
 * @node: List entry in the parent list
 * @children: List head for child nodes
 * @type_name: Name of the member type
 * @var_name: Name of the member variable
 * @offset: Offset from the outer data type
 * @size: Size of the member field
 *
 * This represents a member type in a data type.
 */
struct annotated_member {
	struct list_head node;
	struct list_head children;
	char *type_name;
	char *var_name;
	int offset;
	int size;
};

/**
 * struct annotated_data_type - Data type to profile
 * @node: RB-tree node for dso->type_tree
 * @self: Actual type information
 *
 * This represents a data type accessed by samples in the profile data.
 */
struct annotated_data_type {
	struct rb_node node;
	struct annotated_member self;
};

extern struct annotated_data_type unknown_type;

#ifdef HAVE_DWARF_SUPPORT

/* Returns data type at the location (ip, reg, offset) */
struct annotated_data_type *find_data_type(struct map_symbol *ms, u64 ip,
					   int reg, int offset);

/* Release all data type information in the tree */
void annotated_data_type__tree_delete(struct rb_root *root);

#else /* HAVE_DWARF_SUPPORT */

static inline struct annotated_data_type *
find_data_type(struct map_symbol *ms __maybe_unused, u64 ip __maybe_unused,
	       int reg __maybe_unused, int offset __maybe_unused)
{
	return NULL;
}

static inline void annotated_data_type__tree_delete(struct rb_root *root __maybe_unused)
{
}

#endif /* HAVE_DWARF_SUPPORT */

#endif /* _PERF_ANNOTATE_DATA_H */
