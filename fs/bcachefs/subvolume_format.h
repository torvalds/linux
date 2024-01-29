/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SUBVOLUME_FORMAT_H
#define _BCACHEFS_SUBVOLUME_FORMAT_H

#define SUBVOL_POS_MIN		POS(0, 1)
#define SUBVOL_POS_MAX		POS(0, S32_MAX)
#define BCACHEFS_ROOT_SUBVOL	1

struct bch_subvolume {
	struct bch_val		v;
	__le32			flags;
	__le32			snapshot;
	__le64			inode;
	/*
	 * Snapshot subvolumes form a tree, separate from the snapshot nodes
	 * tree - if this subvolume is a snapshot, this is the ID of the
	 * subvolume it was created from:
	 *
	 * This is _not_ necessarily the subvolume of the directory containing
	 * this subvolume:
	 */
	__le32			parent;
	__le32			pad;
	bch_le128		otime;
};

LE32_BITMASK(BCH_SUBVOLUME_RO,		struct bch_subvolume, flags,  0,  1)
/*
 * We need to know whether a subvolume is a snapshot so we can know whether we
 * can delete it (or whether it should just be rm -rf'd)
 */
LE32_BITMASK(BCH_SUBVOLUME_SNAP,	struct bch_subvolume, flags,  1,  2)
LE32_BITMASK(BCH_SUBVOLUME_UNLINKED,	struct bch_subvolume, flags,  2,  3)

#endif /* _BCACHEFS_SUBVOLUME_FORMAT_H */
