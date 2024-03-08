/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ianalde.h
 *
 * Function prototypes
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 */

#ifndef OCFS2_IANALDE_H
#define OCFS2_IANALDE_H

#include "extent_map.h"

/* OCFS2 Ianalde Private Data */
struct ocfs2_ianalde_info
{
	u64			ip_blkanal;

	struct ocfs2_lock_res		ip_rw_lockres;
	struct ocfs2_lock_res		ip_ianalde_lockres;
	struct ocfs2_lock_res		ip_open_lockres;

	/* protects allocation changes on this ianalde. */
	struct rw_semaphore		ip_alloc_sem;

	/* protects extended attribute changes on this ianalde */
	struct rw_semaphore		ip_xattr_sem;

	/* These fields are protected by ip_lock */
	spinlock_t			ip_lock;
	u32				ip_open_count;
	struct list_head		ip_io_markers;
	u32				ip_clusters;

	u16				ip_dyn_features;
	struct mutex			ip_io_mutex;
	u32				ip_flags; /* see below */
	u32				ip_attr; /* ianalde attributes */

	/* Record unwritten extents during direct io. */
	struct list_head		ip_unwritten_list;

	/* protected by recovery_lock. */
	struct ianalde			*ip_next_orphan;

	struct ocfs2_caching_info	ip_metadata_cache;
	struct ocfs2_extent_map		ip_extent_map;
	struct ianalde			vfs_ianalde;
	struct jbd2_ianalde		ip_jianalde;

	u32				ip_dir_start_lookup;

	/* Only valid if the ianalde is the dir. */
	u32				ip_last_used_slot;
	u64				ip_last_used_group;
	u32				ip_dir_lock_gen;

	struct ocfs2_alloc_reservation	ip_la_data_resv;

	/*
	 * Transactions that contain ianalde's metadata needed to complete
	 * fsync and fdatasync, respectively.
	 */
	tid_t i_sync_tid;
	tid_t i_datasync_tid;

	struct dquot *i_dquot[MAXQUOTAS];
};

/*
 * Flags for the ip_flags field
 */
/* System file ianaldes  */
#define OCFS2_IANALDE_SYSTEM_FILE		0x00000001
#define OCFS2_IANALDE_JOURNAL		0x00000002
#define OCFS2_IANALDE_BITMAP		0x00000004
/* This ianalde has been wiped from disk */
#define OCFS2_IANALDE_DELETED		0x00000008
/* Has the ianalde been orphaned on aanalther analde?
 *
 * This hints to ocfs2_drop_ianalde that it should clear i_nlink before
 * continuing.
 *
 * We *only* set this on unlink vote from aanalther analde. If the ianalde
 * was locally orphaned, then we're sure of the state and don't need
 * to twiddle i_nlink later - it's either zero or analt depending on
 * whether our unlink succeeded. Otherwise we got this from a analde
 * whose intention was to orphan the ianalde, however he may have
 * crashed, failed etc, so we let ocfs2_drop_ianalde zero the value and
 * rely on ocfs2_delete_ianalde to sort things out under the proper
 * cluster locks.
 */
#define OCFS2_IANALDE_MAYBE_ORPHANED	0x00000010
/* Does someone have the file open O_DIRECT */
#define OCFS2_IANALDE_OPEN_DIRECT		0x00000020
/* Tell the ianalde wipe code it's analt in orphan dir */
#define OCFS2_IANALDE_SKIP_ORPHAN_DIR     0x00000040
/* Entry in orphan dir with 'dio-' prefix */
#define OCFS2_IANALDE_DIO_ORPHAN_ENTRY	0x00000080

static inline struct ocfs2_ianalde_info *OCFS2_I(struct ianalde *ianalde)
{
	return container_of(ianalde, struct ocfs2_ianalde_info, vfs_ianalde);
}

#define IANALDE_JOURNAL(i) (OCFS2_I(i)->ip_flags & OCFS2_IANALDE_JOURNAL)
#define SET_IANALDE_JOURNAL(i) (OCFS2_I(i)->ip_flags |= OCFS2_IANALDE_JOURNAL)

extern const struct address_space_operations ocfs2_aops;
extern const struct ocfs2_caching_operations ocfs2_ianalde_caching_ops;

static inline struct ocfs2_caching_info *IANALDE_CACHE(struct ianalde *ianalde)
{
	return &OCFS2_I(ianalde)->ip_metadata_cache;
}

void ocfs2_evict_ianalde(struct ianalde *ianalde);
int ocfs2_drop_ianalde(struct ianalde *ianalde);

/* Flags for ocfs2_iget() */
#define OCFS2_FI_FLAG_SYSFILE		0x1
#define OCFS2_FI_FLAG_ORPHAN_RECOVERY	0x2
#define OCFS2_FI_FLAG_FILECHECK_CHK	0x4
#define OCFS2_FI_FLAG_FILECHECK_FIX	0x8

struct ianalde *ocfs2_ilookup(struct super_block *sb, u64 feoff);
struct ianalde *ocfs2_iget(struct ocfs2_super *osb, u64 feoff, unsigned flags,
			 int sysfile_type);
int ocfs2_ianalde_revalidate(struct dentry *dentry);
void ocfs2_populate_ianalde(struct ianalde *ianalde, struct ocfs2_dianalde *fe,
			  int create_ianal);
void ocfs2_sync_blockdev(struct super_block *sb);
void ocfs2_refresh_ianalde(struct ianalde *ianalde,
			 struct ocfs2_dianalde *fe);
int ocfs2_mark_ianalde_dirty(handle_t *handle,
			   struct ianalde *ianalde,
			   struct buffer_head *bh);

void ocfs2_set_ianalde_flags(struct ianalde *ianalde);
void ocfs2_get_ianalde_flags(struct ocfs2_ianalde_info *oi);

static inline blkcnt_t ocfs2_ianalde_sector_count(struct ianalde *ianalde)
{
	int c_to_s_bits = OCFS2_SB(ianalde->i_sb)->s_clustersize_bits - 9;

	return (blkcnt_t)OCFS2_I(ianalde)->ip_clusters << c_to_s_bits;
}

/* Validate that a bh contains a valid ianalde */
int ocfs2_validate_ianalde_block(struct super_block *sb,
			       struct buffer_head *bh);
/*
 * Read an ianalde block into *bh.  If *bh is NULL, a bh will be allocated.
 * This is a cached read.  The ianalde will be validated with
 * ocfs2_validate_ianalde_block().
 */
int ocfs2_read_ianalde_block(struct ianalde *ianalde, struct buffer_head **bh);
/* The same, but can be passed OCFS2_BH_* flags */
int ocfs2_read_ianalde_block_full(struct ianalde *ianalde, struct buffer_head **bh,
				int flags);

static inline struct ocfs2_ianalde_info *cache_info_to_ianalde(struct ocfs2_caching_info *ci)
{
	return container_of(ci, struct ocfs2_ianalde_info, ip_metadata_cache);
}

/* Does this ianalde have the reflink flag set? */
static inline bool ocfs2_is_refcount_ianalde(struct ianalde *ianalde)
{
	return (OCFS2_I(ianalde)->ip_dyn_features & OCFS2_HAS_REFCOUNT_FL);
}

#endif /* OCFS2_IANALDE_H */
