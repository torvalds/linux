/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SUBVOLUME_TYPES_H
#define _BCACHEFS_SUBVOLUME_TYPES_H

#include "darray.h"

typedef DARRAY(u32) snapshot_id_list;

#define IS_ANCESTOR_BITMAP	128

struct snapshot_t {
	u32			parent;
	u32			skip[3];
	u32			depth;
	u32			children[2];
	u32			subvol; /* Nonzero only if a subvolume points to this node: */
	u32			tree;
	u32			equiv;
	unsigned long		is_ancestor[BITS_TO_LONGS(IS_ANCESTOR_BITMAP)];
};

struct snapshot_table {
	struct rcu_head		rcu;
	size_t			nr;
#ifndef RUST_BINDGEN
	DECLARE_FLEX_ARRAY(struct snapshot_t, s);
#else
	struct snapshot_t	s[0];
#endif
};

typedef struct {
	u32		subvol;
	u64		inum;
} subvol_inum;

#endif /* _BCACHEFS_SUBVOLUME_TYPES_H */
