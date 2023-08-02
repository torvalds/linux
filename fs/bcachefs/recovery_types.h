/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_RECOVERY_TYPES_H
#define _BCACHEFS_RECOVERY_TYPES_H

#define PASS_SILENT		BIT(0)
#define PASS_FSCK		BIT(1)
#define PASS_UNCLEAN		BIT(2)
#define PASS_ALWAYS		BIT(3)

#define BCH_RECOVERY_PASSES()									\
	x(alloc_read,			PASS_ALWAYS)						\
	x(stripes_read,			PASS_ALWAYS)						\
	x(initialize_subvolumes,	0)							\
	x(snapshots_read,		PASS_ALWAYS)						\
	x(check_topology,		0)							\
	x(check_allocations,		PASS_FSCK)						\
	x(set_may_go_rw,		PASS_ALWAYS|PASS_SILENT)				\
	x(journal_replay,		PASS_ALWAYS)						\
	x(check_alloc_info,		PASS_FSCK)						\
	x(check_lrus,			PASS_FSCK)						\
	x(check_btree_backpointers,	PASS_FSCK)						\
	x(check_backpointers_to_extents,PASS_FSCK)						\
	x(check_extents_to_backpointers,PASS_FSCK)						\
	x(check_alloc_to_lru_refs,	PASS_FSCK)						\
	x(fs_freespace_init,		PASS_ALWAYS|PASS_SILENT)				\
	x(bucket_gens_init,		0)							\
	x(check_snapshot_trees,		PASS_FSCK)						\
	x(check_snapshots,		PASS_FSCK)						\
	x(check_subvols,		PASS_FSCK)						\
	x(delete_dead_snapshots,	PASS_FSCK|PASS_UNCLEAN)					\
	x(fs_upgrade_for_subvolumes,	0)							\
	x(check_inodes,			PASS_FSCK|PASS_UNCLEAN)					\
	x(check_extents,		PASS_FSCK)						\
	x(check_dirents,		PASS_FSCK)						\
	x(check_xattrs,			PASS_FSCK)						\
	x(check_root,			PASS_FSCK)						\
	x(check_directory_structure,	PASS_FSCK)						\
	x(check_nlinks,			PASS_FSCK)						\
	x(fix_reflink_p,		0)							\

enum bch_recovery_pass {
#define x(n, when)	BCH_RECOVERY_PASS_##n,
	BCH_RECOVERY_PASSES()
#undef x
};

#endif /* _BCACHEFS_RECOVERY_TYPES_H */
