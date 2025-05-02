/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SNAPSHOT_TYPES_H
#define _BCACHEFS_SNAPSHOT_TYPES_H

#include "bbpos_types.h"
#include "subvolume_types.h"

struct snapshot_interior_delete {
	u32	id;
	u32	live_child;
};
typedef DARRAY(struct snapshot_interior_delete) interior_delete_list;

struct snapshot_delete {
	struct work_struct	work;

	struct mutex		progress_lock;
	snapshot_id_list	deleting_from_trees;
	snapshot_id_list	delete_leaves;
	interior_delete_list	delete_interior;

	bool			running;
	struct bbpos		pos;
};

#endif /* _BCACHEFS_SNAPSHOT_TYPES_H */
