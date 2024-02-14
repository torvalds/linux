/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SNAPSHOT_FORMAT_H
#define _BCACHEFS_SNAPSHOT_FORMAT_H

struct bch_snapshot {
	struct bch_val		v;
	__le32			flags;
	__le32			parent;
	__le32			children[2];
	__le32			subvol;
	/* corresponds to a bch_snapshot_tree in BTREE_ID_snapshot_trees */
	__le32			tree;
	__le32			depth;
	__le32			skip[3];
	bch_le128		btime;
};

LE32_BITMASK(BCH_SNAPSHOT_DELETED,	struct bch_snapshot, flags,  0,  1)

/* True if a subvolume points to this snapshot node: */
LE32_BITMASK(BCH_SNAPSHOT_SUBVOL,	struct bch_snapshot, flags,  1,  2)

/*
 * Snapshot trees:
 *
 * The snapshot_trees btree gives us persistent indentifier for each tree of
 * bch_snapshot nodes, and allow us to record and easily find the root/master
 * subvolume that other snapshots were created from:
 */
struct bch_snapshot_tree {
	struct bch_val		v;
	__le32			master_subvol;
	__le32			root_snapshot;
};

#endif /* _BCACHEFS_SNAPSHOT_FORMAT_H */
