/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SNAPSHOT_TYPES_H
#define _BCACHEFS_SNAPSHOT_TYPES_H

#include "bbpos_types.h"
#include "darray.h"
#include "subvolume_types.h"

typedef DARRAY(u32) snapshot_id_list;

#define IS_ANCESTOR_BITMAP	128

struct snapshot_t {
	enum snapshot_id_state {
		SNAPSHOT_ID_empty,
		SNAPSHOT_ID_live,
		SNAPSHOT_ID_deleted,
	}			state;
	u32			parent;
	u32			skip[3];
	u32			depth;
	u32			children[2];
	u32			subvol; /* Nonzero only if a subvolume points to this node: */
	u32			tree;
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

struct snapshot_interior_delete {
	u32	id;
	u32	live_child;
};
typedef DARRAY(struct snapshot_interior_delete) interior_delete_list;

struct snapshot_delete {
	struct mutex		lock;
	struct work_struct	work;

	struct mutex		progress_lock;
	snapshot_id_list	deleting_from_trees;
	snapshot_id_list	delete_leaves;
	interior_delete_list	delete_interior;

	bool			running;
	struct bbpos		pos;
};

#endif /* _BCACHEFS_SNAPSHOT_TYPES_H */
