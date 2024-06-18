/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_RECOVERY_PASSES_TYPES_H
#define _BCACHEFS_RECOVERY_PASSES_TYPES_H

#define PASS_SILENT		BIT(0)
#define PASS_FSCK		BIT(1)
#define PASS_UNCLEAN		BIT(2)
#define PASS_ALWAYS		BIT(3)
#define PASS_ONLINE		BIT(4)

/*
 * Passes may be reordered, but the second field is a persistent identifier and
 * must never change:
 */
#define BCH_RECOVERY_PASSES()							\
	x(scan_for_btree_nodes,			37, 0)				\
	x(check_topology,			 4, 0)				\
	x(alloc_read,				 0, PASS_ALWAYS)		\
	x(stripes_read,				 1, PASS_ALWAYS)		\
	x(initialize_subvolumes,		 2, 0)				\
	x(snapshots_read,			 3, PASS_ALWAYS)		\
	x(check_allocations,			 5, PASS_FSCK)			\
	x(trans_mark_dev_sbs,			 6, PASS_ALWAYS|PASS_SILENT)	\
	x(fs_journal_alloc,			 7, PASS_ALWAYS|PASS_SILENT)	\
	x(set_may_go_rw,			 8, PASS_ALWAYS|PASS_SILENT)	\
	x(journal_replay,			 9, PASS_ALWAYS)		\
	x(check_alloc_info,			10, PASS_ONLINE|PASS_FSCK)	\
	x(check_lrus,				11, PASS_ONLINE|PASS_FSCK)	\
	x(check_btree_backpointers,		12, PASS_ONLINE|PASS_FSCK)	\
	x(check_backpointers_to_extents,	13, PASS_ONLINE|PASS_FSCK)	\
	x(check_extents_to_backpointers,	14, PASS_ONLINE|PASS_FSCK)	\
	x(check_alloc_to_lru_refs,		15, PASS_ONLINE|PASS_FSCK)	\
	x(fs_freespace_init,			16, PASS_ALWAYS|PASS_SILENT)	\
	x(bucket_gens_init,			17, 0)				\
	x(reconstruct_snapshots,		38, 0)				\
	x(check_snapshot_trees,			18, PASS_ONLINE|PASS_FSCK)	\
	x(check_snapshots,			19, PASS_ONLINE|PASS_FSCK)	\
	x(check_subvols,			20, PASS_ONLINE|PASS_FSCK)	\
	x(check_subvol_children,		35, PASS_ONLINE|PASS_FSCK)	\
	x(delete_dead_snapshots,		21, PASS_ONLINE|PASS_FSCK)	\
	x(fs_upgrade_for_subvolumes,		22, 0)				\
	x(check_inodes,				24, PASS_FSCK)			\
	x(check_extents,			25, PASS_FSCK)			\
	x(check_indirect_extents,		26, PASS_FSCK)			\
	x(check_dirents,			27, PASS_FSCK)			\
	x(check_xattrs,				28, PASS_FSCK)			\
	x(check_root,				29, PASS_ONLINE|PASS_FSCK)	\
	x(check_subvolume_structure,		36, PASS_ONLINE|PASS_FSCK)	\
	x(check_directory_structure,		30, PASS_ONLINE|PASS_FSCK)	\
	x(check_nlinks,				31, PASS_FSCK)			\
	x(resume_logged_ops,			23, PASS_ALWAYS)		\
	x(delete_dead_inodes,			32, PASS_FSCK|PASS_UNCLEAN)	\
	x(fix_reflink_p,			33, 0)				\
	x(set_fs_needs_rebalance,		34, 0)				\

/* We normally enumerate recovery passes in the order we run them: */
enum bch_recovery_pass {
#define x(n, id, when)	BCH_RECOVERY_PASS_##n,
	BCH_RECOVERY_PASSES()
#undef x
	BCH_RECOVERY_PASS_NR
};

/* But we also need stable identifiers that can be used in the superblock */
enum bch_recovery_pass_stable {
#define x(n, id, when)	BCH_RECOVERY_PASS_STABLE_##n = id,
	BCH_RECOVERY_PASSES()
#undef x
};

#endif /* _BCACHEFS_RECOVERY_PASSES_TYPES_H */
