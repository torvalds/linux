/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PERF_ANNOTATE_DATA_H
#define _PERF_ANNOTATE_DATA_H

#include <errno.h>
#include <linux/compiler.h>
#include <linux/rbtree.h>
#include <linux/types.h>

struct map_symbol;

/**
 * struct annotated_data_type - Data type to profile
 * @type_name: Name of the data type
 * @type_size: Size of the data type
 *
 * This represents a data type accessed by samples in the profile data.
 */
struct annotated_data_type {
	struct rb_node node;
	char *type_name;
	int type_size;
};

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
