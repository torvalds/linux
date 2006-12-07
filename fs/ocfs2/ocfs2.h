/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * ocfs2.h
 *
 * Defines macros and structures used in OCFS2
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#ifndef OCFS2_H
#define OCFS2_H

#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/workqueue.h>
#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/jbd.h>

#include "cluster/nodemanager.h"
#include "cluster/heartbeat.h"
#include "cluster/tcp.h"

#include "dlm/dlmapi.h"

#include "ocfs2_fs.h"
#include "endian.h"
#include "ocfs2_lockid.h"

struct ocfs2_extent_map {
	u32		em_clusters;
	struct rb_root	em_extents;
};

/* Most user visible OCFS2 inodes will have very few pieces of
 * metadata, but larger files (including bitmaps, etc) must be taken
 * into account when designing an access scheme. We allow a small
 * amount of inlined blocks to be stored on an array and grow the
 * structure into a rb tree when necessary. */
#define OCFS2_INODE_MAX_CACHE_ARRAY 2

struct ocfs2_caching_info {
	unsigned int		ci_num_cached;
	union {
		sector_t	ci_array[OCFS2_INODE_MAX_CACHE_ARRAY];
		struct rb_root	ci_tree;
	} ci_cache;
};

/* this limits us to 256 nodes
 * if we need more, we can do a kmalloc for the map */
#define OCFS2_NODE_MAP_MAX_NODES    256
struct ocfs2_node_map {
	u16 num_nodes;
	unsigned long map[BITS_TO_LONGS(OCFS2_NODE_MAP_MAX_NODES)];
};

enum ocfs2_ast_action {
	OCFS2_AST_INVALID = 0,
	OCFS2_AST_ATTACH,
	OCFS2_AST_CONVERT,
	OCFS2_AST_DOWNCONVERT,
};

/* actions for an unlockast function to take. */
enum ocfs2_unlock_action {
	OCFS2_UNLOCK_INVALID = 0,
	OCFS2_UNLOCK_CANCEL_CONVERT,
	OCFS2_UNLOCK_DROP_LOCK,
};

/* ocfs2_lock_res->l_flags flags. */
#define OCFS2_LOCK_ATTACHED      (0x00000001) /* have we initialized
					       * the lvb */
#define OCFS2_LOCK_BUSY          (0x00000002) /* we are currently in
					       * dlm_lock */
#define OCFS2_LOCK_BLOCKED       (0x00000004) /* blocked waiting to
					       * downconvert*/
#define OCFS2_LOCK_LOCAL         (0x00000008) /* newly created inode */
#define OCFS2_LOCK_NEEDS_REFRESH (0x00000010)
#define OCFS2_LOCK_REFRESHING    (0x00000020)
#define OCFS2_LOCK_INITIALIZED   (0x00000040) /* track initialization
					       * for shutdown paths */
#define OCFS2_LOCK_FREEING       (0x00000080) /* help dlmglue track
					       * when to skip queueing
					       * a lock because it's
					       * about to be
					       * dropped. */
#define OCFS2_LOCK_QUEUED        (0x00000100) /* queued for downconvert */

struct ocfs2_lock_res_ops;

typedef void (*ocfs2_lock_callback)(int status, unsigned long data);

struct ocfs2_lock_res {
	void                    *l_priv;
	struct ocfs2_lock_res_ops *l_ops;
	spinlock_t               l_lock;

	struct list_head         l_blocked_list;
	struct list_head         l_mask_waiters;

	enum ocfs2_lock_type     l_type;
	unsigned long		 l_flags;
	char                     l_name[OCFS2_LOCK_ID_MAX_LEN];
	int                      l_level;
	unsigned int             l_ro_holders;
	unsigned int             l_ex_holders;
	struct dlm_lockstatus    l_lksb;

	/* used from AST/BAST funcs. */
	enum ocfs2_ast_action    l_action;
	enum ocfs2_unlock_action l_unlock_action;
	int                      l_requested;
	int                      l_blocking;

	wait_queue_head_t        l_event;

	struct list_head         l_debug_list;
};

struct ocfs2_dlm_debug {
	struct kref d_refcnt;
	struct dentry *d_locking_state;
	struct list_head d_lockres_tracking;
};

enum ocfs2_vol_state
{
	VOLUME_INIT = 0,
	VOLUME_MOUNTED,
	VOLUME_DISMOUNTED,
	VOLUME_DISABLED
};

struct ocfs2_alloc_stats
{
	atomic_t moves;
	atomic_t local_data;
	atomic_t bitmap_data;
	atomic_t bg_allocs;
	atomic_t bg_extends;
};

enum ocfs2_local_alloc_state
{
	OCFS2_LA_UNUSED = 0,
	OCFS2_LA_ENABLED,
	OCFS2_LA_DISABLED
};

enum ocfs2_mount_options
{
	OCFS2_MOUNT_HB_LOCAL   = 1 << 0, /* Heartbeat started in local mode */
	OCFS2_MOUNT_BARRIER = 1 << 1,	/* Use block barriers */
	OCFS2_MOUNT_NOINTR  = 1 << 2,   /* Don't catch signals */
	OCFS2_MOUNT_ERRORS_PANIC = 1 << 3, /* Panic on errors */
	OCFS2_MOUNT_DATA_WRITEBACK = 1 << 4, /* No data ordering */
};

#define OCFS2_OSB_SOFT_RO	0x0001
#define OCFS2_OSB_HARD_RO	0x0002
#define OCFS2_OSB_ERROR_FS	0x0004
#define OCFS2_DEFAULT_ATIME_QUANTUM	60

struct ocfs2_journal;
struct ocfs2_super
{
	struct task_struct *commit_task;
	struct super_block *sb;
	struct inode *root_inode;
	struct inode *sys_root_inode;
	struct inode *system_inodes[NUM_SYSTEM_INODES];

	struct ocfs2_slot_info *slot_info;

	spinlock_t node_map_lock;
	struct ocfs2_node_map mounted_map;
	struct ocfs2_node_map recovery_map;
	struct ocfs2_node_map umount_map;

	u64 root_blkno;
	u64 system_dir_blkno;
	u64 bitmap_blkno;
	u32 bitmap_cpg;
	u8 *uuid;
	char *uuid_str;
	u8 *vol_label;
	u64 first_cluster_group_blkno;
	u32 fs_generation;

	u32 s_feature_compat;
	u32 s_feature_incompat;
	u32 s_feature_ro_compat;

	/* Protects s_next_generaion, osb_flags. Could protect more on
	 * osb as it's very short lived. */
	spinlock_t osb_lock;
	u32 s_next_generation;
	unsigned long osb_flags;

	unsigned long s_mount_opt;
	unsigned int s_atime_quantum;

	u16 max_slots;
	s16 node_num;
	s16 slot_num;
	int s_sectsize_bits;
	int s_clustersize;
	int s_clustersize_bits;

	atomic_t vol_state;
	struct mutex recovery_lock;
	struct task_struct *recovery_thread_task;
	int disable_recovery;
	wait_queue_head_t checkpoint_event;
	atomic_t needs_checkpoint;
	struct ocfs2_journal *journal;

	enum ocfs2_local_alloc_state local_alloc_state;
	struct buffer_head *local_alloc_bh;
	u64 la_last_gd;

	/* Next two fields are for local node slot recovery during
	 * mount. */
	int dirty;
	struct ocfs2_dinode *local_alloc_copy;

	struct ocfs2_alloc_stats alloc_stats;
	char dev_str[20];		/* "major,minor" of the device */

	struct dlm_ctxt *dlm;
	struct ocfs2_lock_res osb_super_lockres;
	struct ocfs2_lock_res osb_rename_lockres;
	struct dlm_eviction_cb osb_eviction_cb;
	struct ocfs2_dlm_debug *osb_dlm_debug;

	struct dentry *osb_debug_root;

	wait_queue_head_t recovery_event;

	spinlock_t vote_task_lock;
	struct task_struct *vote_task;
	wait_queue_head_t vote_event;
	unsigned long vote_wake_sequence;
	unsigned long vote_work_sequence;

	struct list_head blocked_lock_list;
	unsigned long blocked_lock_count;

	struct list_head vote_list;
	int vote_count;

	u32 net_key;
	spinlock_t net_response_lock;
	unsigned int net_response_ids;
	struct list_head net_response_list;

	struct o2hb_callback_func osb_hb_up;
	struct o2hb_callback_func osb_hb_down;

	struct list_head	osb_net_handlers;

	wait_queue_head_t		osb_mount_event;

	/* Truncate log info */
	struct inode			*osb_tl_inode;
	struct buffer_head		*osb_tl_bh;
	struct delayed_work		osb_truncate_log_wq;

	struct ocfs2_node_map		osb_recovering_orphan_dirs;
	unsigned int			*osb_orphan_wipes;
	wait_queue_head_t		osb_wipe_event;
};

#define OCFS2_SB(sb)	    ((struct ocfs2_super *)(sb)->s_fs_info)

static inline int ocfs2_should_order_data(struct inode *inode)
{
	if (!S_ISREG(inode->i_mode))
		return 0;
	if (OCFS2_SB(inode->i_sb)->s_mount_opt & OCFS2_MOUNT_DATA_WRITEBACK)
		return 0;
	return 1;
}

/* set / clear functions because cluster events can make these happen
 * in parallel so we want the transitions to be atomic. this also
 * means that any future flags osb_flags must be protected by spinlock
 * too! */
static inline void ocfs2_set_osb_flag(struct ocfs2_super *osb,
				      unsigned long flag)
{
	spin_lock(&osb->osb_lock);
	osb->osb_flags |= flag;
	spin_unlock(&osb->osb_lock);
}

static inline void ocfs2_set_ro_flag(struct ocfs2_super *osb,
				     int hard)
{
	spin_lock(&osb->osb_lock);
	osb->osb_flags &= ~(OCFS2_OSB_SOFT_RO|OCFS2_OSB_HARD_RO);
	if (hard)
		osb->osb_flags |= OCFS2_OSB_HARD_RO;
	else
		osb->osb_flags |= OCFS2_OSB_SOFT_RO;
	spin_unlock(&osb->osb_lock);
}

static inline int ocfs2_is_hard_readonly(struct ocfs2_super *osb)
{
	int ret;

	spin_lock(&osb->osb_lock);
	ret = osb->osb_flags & OCFS2_OSB_HARD_RO;
	spin_unlock(&osb->osb_lock);

	return ret;
}

static inline int ocfs2_is_soft_readonly(struct ocfs2_super *osb)
{
	int ret;

	spin_lock(&osb->osb_lock);
	ret = osb->osb_flags & OCFS2_OSB_SOFT_RO;
	spin_unlock(&osb->osb_lock);

	return ret;
}

#define OCFS2_IS_VALID_DINODE(ptr)					\
	(!strcmp((ptr)->i_signature, OCFS2_INODE_SIGNATURE))

#define OCFS2_RO_ON_INVALID_DINODE(__sb, __di)	do {			\
	typeof(__di) ____di = (__di);					\
	ocfs2_error((__sb), 						\
		"Dinode # %llu has bad signature %.*s",			\
		(unsigned long long)(____di)->i_blkno, 7,		\
		(____di)->i_signature);					\
} while (0);

#define OCFS2_IS_VALID_EXTENT_BLOCK(ptr)				\
	(!strcmp((ptr)->h_signature, OCFS2_EXTENT_BLOCK_SIGNATURE))

#define OCFS2_RO_ON_INVALID_EXTENT_BLOCK(__sb, __eb)	do {		\
	typeof(__eb) ____eb = (__eb);					\
	ocfs2_error((__sb), 						\
		"Extent Block # %llu has bad signature %.*s",		\
		(unsigned long long)(____eb)->h_blkno, 7,		\
		(____eb)->h_signature);					\
} while (0);

#define OCFS2_IS_VALID_GROUP_DESC(ptr)					\
	(!strcmp((ptr)->bg_signature, OCFS2_GROUP_DESC_SIGNATURE))

#define OCFS2_RO_ON_INVALID_GROUP_DESC(__sb, __gd)	do {		\
	typeof(__gd) ____gd = (__gd);					\
		ocfs2_error((__sb),					\
		"Group Descriptor # %llu has bad signature %.*s",	\
		(unsigned long long)(____gd)->bg_blkno, 7,		\
		(____gd)->bg_signature);				\
} while (0);

static inline unsigned long ino_from_blkno(struct super_block *sb,
					   u64 blkno)
{
	return (unsigned long)(blkno & (u64)ULONG_MAX);
}

static inline u64 ocfs2_clusters_to_blocks(struct super_block *sb,
					   u32 clusters)
{
	int c_to_b_bits = OCFS2_SB(sb)->s_clustersize_bits -
		sb->s_blocksize_bits;

	return (u64)clusters << c_to_b_bits;
}

static inline u32 ocfs2_blocks_to_clusters(struct super_block *sb,
					   u64 blocks)
{
	int b_to_c_bits = OCFS2_SB(sb)->s_clustersize_bits -
		sb->s_blocksize_bits;

	return (u32)(blocks >> b_to_c_bits);
}

static inline unsigned int ocfs2_clusters_for_bytes(struct super_block *sb,
						    u64 bytes)
{
	int cl_bits = OCFS2_SB(sb)->s_clustersize_bits;
	unsigned int clusters;

	bytes += OCFS2_SB(sb)->s_clustersize - 1;
	/* OCFS2 just cannot have enough clusters to overflow this */
	clusters = (unsigned int)(bytes >> cl_bits);

	return clusters;
}

static inline u64 ocfs2_blocks_for_bytes(struct super_block *sb,
					 u64 bytes)
{
	bytes += sb->s_blocksize - 1;
	return bytes >> sb->s_blocksize_bits;
}

static inline u64 ocfs2_clusters_to_bytes(struct super_block *sb,
					  u32 clusters)
{
	return (u64)clusters << OCFS2_SB(sb)->s_clustersize_bits;
}

static inline u64 ocfs2_align_bytes_to_clusters(struct super_block *sb,
						u64 bytes)
{
	int cl_bits = OCFS2_SB(sb)->s_clustersize_bits;
	unsigned int clusters;

	clusters = ocfs2_clusters_for_bytes(sb, bytes);
	return (u64)clusters << cl_bits;
}

static inline u64 ocfs2_align_bytes_to_blocks(struct super_block *sb,
					      u64 bytes)
{
	u64 blocks;

        blocks = ocfs2_blocks_for_bytes(sb, bytes);
	return blocks << sb->s_blocksize_bits;
}

static inline unsigned long ocfs2_align_bytes_to_sectors(u64 bytes)
{
	return (unsigned long)((bytes + 511) >> 9);
}

#define ocfs2_set_bit ext2_set_bit
#define ocfs2_clear_bit ext2_clear_bit
#define ocfs2_test_bit ext2_test_bit
#define ocfs2_find_next_zero_bit ext2_find_next_zero_bit
#endif  /* OCFS2_H */

