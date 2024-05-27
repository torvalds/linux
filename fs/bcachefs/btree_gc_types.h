/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BTREE_GC_TYPES_H
#define _BCACHEFS_BTREE_GC_TYPES_H

#include <linux/generic-radix-tree.h>

enum gc_phase {
	GC_PHASE_not_running,
	GC_PHASE_start,
	GC_PHASE_sb,
	GC_PHASE_btree,
};

struct gc_pos {
	enum gc_phase		phase:8;
	enum btree_id		btree:8;
	u16			level;
	struct bpos		pos;
};

struct reflink_gc {
	u64		offset;
	u32		size;
	u32		refcount;
};

typedef GENRADIX(struct reflink_gc) reflink_gc_table;

#endif /* _BCACHEFS_BTREE_GC_TYPES_H */
