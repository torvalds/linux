/* SPDX-License-Identifier: GPL-2.0-or-later */
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: yesexpandtab sw=8 ts=8 sts=0:
 *
 * iyesde.h
 *
 * Function prototypes
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 */

#ifndef OCFS2_INODE_H
#define OCFS2_INODE_H

#include "extent_map.h"

/* OCFS2 Iyesde Private Data */
struct ocfs2_iyesde_info
{
	u64			ip_blkyes;

	struct ocfs2_lock_res		ip_rw_lockres;
	struct ocfs2_lock_res		ip_iyesde_lockres;
	struct ocfs2_lock_res		ip_open_lockres;

	/* protects allocation changes on this iyesde. */
	struct rw_semaphore		ip_alloc_sem;

	/* protects extended attribute changes on this iyesde */
	struct rw_semaphore		ip_xattr_sem;

	/* These fields are protected by ip_lock */
	spinlock_t			ip_lock;
	u32				ip_open_count;
	struct list_head		ip_io_markers;
	u32				ip_clusters;

	u16				ip_dyn_features;
	struct mutex			ip_io_mutex;
	u32				ip_flags; /* see below */
	u32				ip_attr; /* iyesde attributes */

	/* Record unwritten extents during direct io. */
	struct list_head		ip_unwritten_list;

	/* protected by recovery_lock. */
	struct iyesde			*ip_next_orphan;

	struct ocfs2_caching_info	ip_metadata_cache;
	struct ocfs2_extent_map		ip_extent_map;
	struct iyesde			vfs_iyesde;
	struct jbd2_iyesde		ip_jiyesde;

	u32				ip_dir_start_lookup;

	/* Only valid if the iyesde is the dir. */
	u32				ip_last_used_slot;
	u64				ip_last_used_group;
	u32				ip_dir_lock_gen;

	struct ocfs2_alloc_reservation	ip_la_data_resv;

	/*
	 * Transactions that contain iyesde's metadata needed to complete
	 * fsync and fdatasync, respectively.
	 */
	tid_t i_sync_tid;
	tid_t i_datasync_tid;

	struct dquot *i_dquot[MAXQUOTAS];
};

/*
 * Flags for the ip_flags field
 */
/* System file iyesdes  */
#define OCFS2_INODE_SYSTEM_FILE		0x00000001
#define OCFS2_INODE_JOURNAL		0x00000002
#define OCFS2_INODE_BITMAP		0x00000004
/* This iyesde has been wiped from disk */
#define OCFS2_INODE_DELETED		0x00000008
/* Has the iyesde been orphaned on ayesther yesde?
 *
 * This hints to ocfs2_drop_iyesde that it should clear i_nlink before
 * continuing.
 *
 * We *only* set this on unlink vote from ayesther yesde. If the iyesde
 * was locally orphaned, then we're sure of the state and don't need
 * to twiddle i_nlink later - it's either zero or yest depending on
 * whether our unlink succeeded. Otherwise we got this from a yesde
 * whose intention was to orphan the iyesde, however he may have
 * crashed, failed etc, so we let ocfs2_drop_iyesde zero the value and
 * rely on ocfs2_delete_iyesde to sort things out under the proper
 * cluster locks.
 */
#define OCFS2_INODE_MAYBE_ORPHANED	0x00000010
/* Does someone have the file open O_DIRECT */
#define OCFS2_INODE_OPEN_DIRECT		0x00000020
/* Tell the iyesde wipe code it's yest in orphan dir */
#define OCFS2_INODE_SKIP_ORPHAN_DIR     0x00000040
/* Entry in orphan dir with 'dio-' prefix */
#define OCFS2_INODE_DIO_ORPHAN_ENTRY	0x00000080

static inline struct ocfs2_iyesde_info *OCFS2_I(struct iyesde *iyesde)
{
	return container_of(iyesde, struct ocfs2_iyesde_info, vfs_iyesde);
}

#define INODE_JOURNAL(i) (OCFS2_I(i)->ip_flags & OCFS2_INODE_JOURNAL)
#define SET_INODE_JOURNAL(i) (OCFS2_I(i)->ip_flags |= OCFS2_INODE_JOURNAL)

extern const struct address_space_operations ocfs2_aops;
extern const struct ocfs2_caching_operations ocfs2_iyesde_caching_ops;

static inline struct ocfs2_caching_info *INODE_CACHE(struct iyesde *iyesde)
{
	return &OCFS2_I(iyesde)->ip_metadata_cache;
}

void ocfs2_evict_iyesde(struct iyesde *iyesde);
int ocfs2_drop_iyesde(struct iyesde *iyesde);

/* Flags for ocfs2_iget() */
#define OCFS2_FI_FLAG_SYSFILE		0x1
#define OCFS2_FI_FLAG_ORPHAN_RECOVERY	0x2
#define OCFS2_FI_FLAG_FILECHECK_CHK	0x4
#define OCFS2_FI_FLAG_FILECHECK_FIX	0x8

struct iyesde *ocfs2_ilookup(struct super_block *sb, u64 feoff);
struct iyesde *ocfs2_iget(struct ocfs2_super *osb, u64 feoff, unsigned flags,
			 int sysfile_type);
int ocfs2_iyesde_revalidate(struct dentry *dentry);
void ocfs2_populate_iyesde(struct iyesde *iyesde, struct ocfs2_diyesde *fe,
			  int create_iyes);
void ocfs2_sync_blockdev(struct super_block *sb);
void ocfs2_refresh_iyesde(struct iyesde *iyesde,
			 struct ocfs2_diyesde *fe);
int ocfs2_mark_iyesde_dirty(handle_t *handle,
			   struct iyesde *iyesde,
			   struct buffer_head *bh);

void ocfs2_set_iyesde_flags(struct iyesde *iyesde);
void ocfs2_get_iyesde_flags(struct ocfs2_iyesde_info *oi);

static inline blkcnt_t ocfs2_iyesde_sector_count(struct iyesde *iyesde)
{
	int c_to_s_bits = OCFS2_SB(iyesde->i_sb)->s_clustersize_bits - 9;

	return (blkcnt_t)OCFS2_I(iyesde)->ip_clusters << c_to_s_bits;
}

/* Validate that a bh contains a valid iyesde */
int ocfs2_validate_iyesde_block(struct super_block *sb,
			       struct buffer_head *bh);
/*
 * Read an iyesde block into *bh.  If *bh is NULL, a bh will be allocated.
 * This is a cached read.  The iyesde will be validated with
 * ocfs2_validate_iyesde_block().
 */
int ocfs2_read_iyesde_block(struct iyesde *iyesde, struct buffer_head **bh);
/* The same, but can be passed OCFS2_BH_* flags */
int ocfs2_read_iyesde_block_full(struct iyesde *iyesde, struct buffer_head **bh,
				int flags);

static inline struct ocfs2_iyesde_info *cache_info_to_iyesde(struct ocfs2_caching_info *ci)
{
	return container_of(ci, struct ocfs2_iyesde_info, ip_metadata_cache);
}

/* Does this iyesde have the reflink flag set? */
static inline bool ocfs2_is_refcount_iyesde(struct iyesde *iyesde)
{
	return (OCFS2_I(iyesde)->ip_dyn_features & OCFS2_HAS_REFCOUNT_FL);
}

#endif /* OCFS2_INODE_H */
