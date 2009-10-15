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
#include <linux/lockdep.h>
#ifndef CONFIG_OCFS2_COMPAT_JBD
# include <linux/jbd2.h>
#else
# include <linux/jbd.h>
# include "ocfs2_jbd_compat.h"
#endif

/* For union ocfs2_dlm_lksb */
#include "stackglue.h"

#include "ocfs2_fs.h"
#include "ocfs2_lockid.h"

/* For struct ocfs2_blockcheck_stats */
#include "blockcheck.h"


/* Caching of metadata buffers */

/* Most user visible OCFS2 inodes will have very few pieces of
 * metadata, but larger files (including bitmaps, etc) must be taken
 * into account when designing an access scheme. We allow a small
 * amount of inlined blocks to be stored on an array and grow the
 * structure into a rb tree when necessary. */
#define OCFS2_CACHE_INFO_MAX_ARRAY 2

/* Flags for ocfs2_caching_info */

enum ocfs2_caching_info_flags {
	/* Indicates that the metadata cache is using the inline array */
	OCFS2_CACHE_FL_INLINE	= 1<<1,
};

struct ocfs2_caching_operations;
struct ocfs2_caching_info {
	/*
	 * The parent structure provides the locks, but because the
	 * parent structure can differ, it provides locking operations
	 * to struct ocfs2_caching_info.
	 */
	const struct ocfs2_caching_operations *ci_ops;

	/* next two are protected by trans_inc_lock */
	/* which transaction were we created on? Zero if none. */
	unsigned long		ci_created_trans;
	/* last transaction we were a part of. */
	unsigned long		ci_last_trans;

	/* Cache structures */
	unsigned int		ci_flags;
	unsigned int		ci_num_cached;
	union {
	sector_t	ci_array[OCFS2_CACHE_INFO_MAX_ARRAY];
		struct rb_root	ci_tree;
	} ci_cache;
};
/*
 * Need this prototype here instead of in uptodate.h because journal.h
 * uses it.
 */
struct super_block *ocfs2_metadata_cache_get_super(struct ocfs2_caching_info *ci);

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
#define OCFS2_LOCK_ATTACHED      (0x00000001) /* we have initialized
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
#define OCFS2_LOCK_NOCACHE       (0x00000200) /* don't use a holder count */
#define OCFS2_LOCK_PENDING       (0x00000400) /* This lockres is pending a
						 call to dlm_lock.  Only
						 exists with BUSY set. */

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
	union ocfs2_dlm_lksb     l_lksb;

	/* used from AST/BAST funcs. */
	enum ocfs2_ast_action    l_action;
	enum ocfs2_unlock_action l_unlock_action;
	int                      l_requested;
	int                      l_blocking;
	unsigned int             l_pending_gen;

	wait_queue_head_t        l_event;

	struct list_head         l_debug_list;

#ifdef CONFIG_OCFS2_FS_STATS
	unsigned long long	 l_lock_num_prmode; 	   /* PR acquires */
	unsigned long long 	 l_lock_num_exmode; 	   /* EX acquires */
	unsigned int		 l_lock_num_prmode_failed; /* Failed PR gets */
	unsigned int		 l_lock_num_exmode_failed; /* Failed EX gets */
	unsigned long long	 l_lock_total_prmode; 	   /* Tot wait for PR */
	unsigned long long	 l_lock_total_exmode; 	   /* Tot wait for EX */
	unsigned int		 l_lock_max_prmode; 	   /* Max wait for PR */
	unsigned int		 l_lock_max_exmode; 	   /* Max wait for EX */
	unsigned int		 l_lock_refresh;	   /* Disk refreshes */
#endif
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map	 l_lockdep_map;
#endif
};

enum ocfs2_orphan_scan_state {
	ORPHAN_SCAN_ACTIVE,
	ORPHAN_SCAN_INACTIVE
};

struct ocfs2_orphan_scan {
	struct mutex 		os_lock;
	struct ocfs2_super 	*os_osb;
	struct ocfs2_lock_res 	os_lockres;     /* lock to synchronize scans */
	struct delayed_work 	os_orphan_scan_work;
	struct timespec		os_scantime;  /* time this node ran the scan */
	u32			os_count;      /* tracks node specific scans */
	u32  			os_seqno;       /* tracks cluster wide scans */
	atomic_t		os_state;              /* ACTIVE or INACTIVE */
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
	VOLUME_MOUNTED_QUOTAS,
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
	OCFS2_LA_UNUSED = 0,	/* Local alloc will never be used for
				 * this mountpoint. */
	OCFS2_LA_ENABLED,	/* Local alloc is in use. */
	OCFS2_LA_THROTTLED,	/* Local alloc is in use, but number
				 * of bits has been reduced. */
	OCFS2_LA_DISABLED	/* Local alloc has temporarily been
				 * disabled. */
};

enum ocfs2_mount_options
{
	OCFS2_MOUNT_HB_LOCAL   = 1 << 0, /* Heartbeat started in local mode */
	OCFS2_MOUNT_BARRIER = 1 << 1,	/* Use block barriers */
	OCFS2_MOUNT_NOINTR  = 1 << 2,   /* Don't catch signals */
	OCFS2_MOUNT_ERRORS_PANIC = 1 << 3, /* Panic on errors */
	OCFS2_MOUNT_DATA_WRITEBACK = 1 << 4, /* No data ordering */
	OCFS2_MOUNT_LOCALFLOCKS = 1 << 5, /* No cluster aware user file locks */
	OCFS2_MOUNT_NOUSERXATTR = 1 << 6, /* No user xattr */
	OCFS2_MOUNT_INODE64 = 1 << 7,	/* Allow inode numbers > 2^32 */
	OCFS2_MOUNT_POSIX_ACL = 1 << 8,	/* Force POSIX access control lists */
	OCFS2_MOUNT_NO_POSIX_ACL = 1 << 9,	/* Disable POSIX access
						   control lists */
	OCFS2_MOUNT_USRQUOTA = 1 << 10, /* We support user quotas */
	OCFS2_MOUNT_GRPQUOTA = 1 << 11, /* We support group quotas */
};

#define OCFS2_OSB_SOFT_RO			0x0001
#define OCFS2_OSB_HARD_RO			0x0002
#define OCFS2_OSB_ERROR_FS			0x0004
#define OCFS2_OSB_DROP_DENTRY_LOCK_IMMED	0x0008

#define OCFS2_DEFAULT_ATIME_QUANTUM		60

struct ocfs2_journal;
struct ocfs2_slot_info;
struct ocfs2_recovery_map;
struct ocfs2_replay_map;
struct ocfs2_quota_recovery;
struct ocfs2_dentry_lock;
struct ocfs2_super
{
	struct task_struct *commit_task;
	struct super_block *sb;
	struct inode *root_inode;
	struct inode *sys_root_inode;
	struct inode *system_inodes[NUM_SYSTEM_INODES];

	struct ocfs2_slot_info *slot_info;

	u32 *slot_recovery_generations;

	spinlock_t node_map_lock;

	u64 root_blkno;
	u64 system_dir_blkno;
	u64 bitmap_blkno;
	u32 bitmap_cpg;
	u8 *uuid;
	char *uuid_str;
	u32 uuid_hash;
	u8 *vol_label;
	u64 first_cluster_group_blkno;
	u32 fs_generation;

	u32 s_feature_compat;
	u32 s_feature_incompat;
	u32 s_feature_ro_compat;

	/* Protects s_next_generation, osb_flags and s_inode_steal_slot.
	 * Could protect more on osb as it's very short lived.
	 */
	spinlock_t osb_lock;
	u32 s_next_generation;
	unsigned long osb_flags;
	s16 s_inode_steal_slot;
	atomic_t s_num_inodes_stolen;

	unsigned long s_mount_opt;
	unsigned int s_atime_quantum;

	unsigned int max_slots;
	unsigned int node_num;
	int slot_num;
	int preferred_slot;
	int s_sectsize_bits;
	int s_clustersize;
	int s_clustersize_bits;
	unsigned int s_xattr_inline_size;

	atomic_t vol_state;
	struct mutex recovery_lock;
	struct ocfs2_recovery_map *recovery_map;
	struct ocfs2_replay_map *replay_map;
	struct task_struct *recovery_thread_task;
	int disable_recovery;
	wait_queue_head_t checkpoint_event;
	atomic_t needs_checkpoint;
	struct ocfs2_journal *journal;
	unsigned long osb_commit_interval;

	struct delayed_work		la_enable_wq;

	/*
	 * Must hold local alloc i_mutex and osb->osb_lock to change
	 * local_alloc_bits. Reads can be done under either lock.
	 */
	unsigned int local_alloc_bits;
	unsigned int local_alloc_default_bits;

	enum ocfs2_local_alloc_state local_alloc_state; /* protected
							 * by osb_lock */

	struct buffer_head *local_alloc_bh;

	u64 la_last_gd;

	/* Next three fields are for local node slot recovery during
	 * mount. */
	int dirty;
	struct ocfs2_dinode *local_alloc_copy;
	struct ocfs2_quota_recovery *quota_rec;

	struct ocfs2_blockcheck_stats osb_ecc_stats;
	struct ocfs2_alloc_stats alloc_stats;
	char dev_str[20];		/* "major,minor" of the device */

	char osb_cluster_stack[OCFS2_STACK_LABEL_LEN + 1];
	struct ocfs2_cluster_connection *cconn;
	struct ocfs2_lock_res osb_super_lockres;
	struct ocfs2_lock_res osb_rename_lockres;
	struct ocfs2_lock_res osb_nfs_sync_lockres;
	struct ocfs2_dlm_debug *osb_dlm_debug;

	struct dentry *osb_debug_root;
	struct dentry *osb_ctxt;

	wait_queue_head_t recovery_event;

	spinlock_t dc_task_lock;
	struct task_struct *dc_task;
	wait_queue_head_t dc_event;
	unsigned long dc_wake_sequence;
	unsigned long dc_work_sequence;

	/*
	 * Any thread can add locks to the list, but the downconvert
	 * thread is the only one allowed to remove locks. Any change
	 * to this rule requires updating
	 * ocfs2_downconvert_thread_do_work().
	 */
	struct list_head blocked_lock_list;
	unsigned long blocked_lock_count;

	/* List of dentry locks to release. Anyone can add locks to
	 * the list, ocfs2_wq processes the list  */
	struct ocfs2_dentry_lock *dentry_lock_list;
	struct work_struct dentry_lock_work;

	wait_queue_head_t		osb_mount_event;

	/* Truncate log info */
	struct inode			*osb_tl_inode;
	struct buffer_head		*osb_tl_bh;
	struct delayed_work		osb_truncate_log_wq;

	struct ocfs2_node_map		osb_recovering_orphan_dirs;
	unsigned int			*osb_orphan_wipes;
	wait_queue_head_t		osb_wipe_event;

	struct ocfs2_orphan_scan	osb_orphan_scan;

	/* used to protect metaecc calculation check of xattr. */
	spinlock_t osb_xattr_lock;

	unsigned int			osb_dx_mask;
	u32				osb_dx_seed[4];

	/* the group we used to allocate inodes. */
	u64				osb_inode_alloc_group;

	/* rb tree root for refcount lock. */
	struct rb_root	osb_rf_lock_tree;
	struct ocfs2_refcount_tree *osb_ref_tree_lru;
};

#define OCFS2_SB(sb)	    ((struct ocfs2_super *)(sb)->s_fs_info)

/* Useful typedef for passing around journal access functions */
typedef int (*ocfs2_journal_access_func)(handle_t *handle,
					 struct ocfs2_caching_info *ci,
					 struct buffer_head *bh, int type);

static inline int ocfs2_should_order_data(struct inode *inode)
{
	if (!S_ISREG(inode->i_mode))
		return 0;
	if (OCFS2_SB(inode->i_sb)->s_mount_opt & OCFS2_MOUNT_DATA_WRITEBACK)
		return 0;
	return 1;
}

static inline int ocfs2_sparse_alloc(struct ocfs2_super *osb)
{
	if (osb->s_feature_incompat & OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC)
		return 1;
	return 0;
}

static inline int ocfs2_writes_unwritten_extents(struct ocfs2_super *osb)
{
	/*
	 * Support for sparse files is a pre-requisite
	 */
	if (!ocfs2_sparse_alloc(osb))
		return 0;

	if (osb->s_feature_ro_compat & OCFS2_FEATURE_RO_COMPAT_UNWRITTEN)
		return 1;
	return 0;
}

static inline int ocfs2_supports_inline_data(struct ocfs2_super *osb)
{
	if (osb->s_feature_incompat & OCFS2_FEATURE_INCOMPAT_INLINE_DATA)
		return 1;
	return 0;
}

static inline int ocfs2_supports_xattr(struct ocfs2_super *osb)
{
	if (osb->s_feature_incompat & OCFS2_FEATURE_INCOMPAT_XATTR)
		return 1;
	return 0;
}

static inline int ocfs2_meta_ecc(struct ocfs2_super *osb)
{
	if (osb->s_feature_incompat & OCFS2_FEATURE_INCOMPAT_META_ECC)
		return 1;
	return 0;
}

static inline int ocfs2_supports_indexed_dirs(struct ocfs2_super *osb)
{
	if (osb->s_feature_incompat & OCFS2_FEATURE_INCOMPAT_INDEXED_DIRS)
		return 1;
	return 0;
}

static inline unsigned int ocfs2_link_max(struct ocfs2_super *osb)
{
	if (ocfs2_supports_indexed_dirs(osb))
		return OCFS2_DX_LINK_MAX;
	return OCFS2_LINK_MAX;
}

static inline unsigned int ocfs2_read_links_count(struct ocfs2_dinode *di)
{
	u32 nlink = le16_to_cpu(di->i_links_count);
	u32 hi = le16_to_cpu(di->i_links_count_hi);

	if (di->i_dyn_features & cpu_to_le16(OCFS2_INDEXED_DIR_FL))
		nlink |= (hi << OCFS2_LINKS_HI_SHIFT);

	return nlink;
}

static inline void ocfs2_set_links_count(struct ocfs2_dinode *di, u32 nlink)
{
	u16 lo, hi;

	lo = nlink;
	hi = nlink >> OCFS2_LINKS_HI_SHIFT;

	di->i_links_count = cpu_to_le16(lo);
	di->i_links_count_hi = cpu_to_le16(hi);
}

static inline void ocfs2_add_links_count(struct ocfs2_dinode *di, int n)
{
	u32 links = ocfs2_read_links_count(di);

	links += n;

	ocfs2_set_links_count(di, links);
}

static inline int ocfs2_refcount_tree(struct ocfs2_super *osb)
{
	if (osb->s_feature_incompat & OCFS2_FEATURE_INCOMPAT_REFCOUNT_TREE)
		return 1;
	return 0;
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


static inline unsigned long  ocfs2_test_osb_flag(struct ocfs2_super *osb,
						 unsigned long flag)
{
	unsigned long ret;

	spin_lock(&osb->osb_lock);
	ret = osb->osb_flags & flag;
	spin_unlock(&osb->osb_lock);
	return ret;
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

static inline int ocfs2_userspace_stack(struct ocfs2_super *osb)
{
	return (osb->s_feature_incompat &
		OCFS2_FEATURE_INCOMPAT_USERSPACE_STACK);
}

static inline int ocfs2_mount_local(struct ocfs2_super *osb)
{
	return (osb->s_feature_incompat & OCFS2_FEATURE_INCOMPAT_LOCAL_MOUNT);
}

static inline int ocfs2_uses_extended_slot_map(struct ocfs2_super *osb)
{
	return (osb->s_feature_incompat &
		OCFS2_FEATURE_INCOMPAT_EXTENDED_SLOT_MAP);
}


#define OCFS2_IS_VALID_DINODE(ptr)					\
	(!strcmp((ptr)->i_signature, OCFS2_INODE_SIGNATURE))

#define OCFS2_IS_VALID_EXTENT_BLOCK(ptr)				\
	(!strcmp((ptr)->h_signature, OCFS2_EXTENT_BLOCK_SIGNATURE))

#define OCFS2_IS_VALID_GROUP_DESC(ptr)					\
	(!strcmp((ptr)->bg_signature, OCFS2_GROUP_DESC_SIGNATURE))


#define OCFS2_IS_VALID_XATTR_BLOCK(ptr)					\
	(!strcmp((ptr)->xb_signature, OCFS2_XATTR_BLOCK_SIGNATURE))

#define OCFS2_IS_VALID_DIR_TRAILER(ptr)					\
	(!strcmp((ptr)->db_signature, OCFS2_DIR_TRAILER_SIGNATURE))

#define OCFS2_IS_VALID_DX_ROOT(ptr)					\
	(!strcmp((ptr)->dr_signature, OCFS2_DX_ROOT_SIGNATURE))

#define OCFS2_IS_VALID_DX_LEAF(ptr)					\
	(!strcmp((ptr)->dl_signature, OCFS2_DX_LEAF_SIGNATURE))

#define OCFS2_IS_VALID_REFCOUNT_BLOCK(ptr)				\
	(!strcmp((ptr)->rf_signature, OCFS2_REFCOUNT_BLOCK_SIGNATURE))

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

static inline u64 ocfs2_block_to_cluster_start(struct super_block *sb,
					       u64 blocks)
{
	int bits = OCFS2_SB(sb)->s_clustersize_bits - sb->s_blocksize_bits;
	unsigned int clusters;

	clusters = ocfs2_blocks_to_clusters(sb, blocks);
	return (u64)clusters << bits;
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

static inline unsigned int ocfs2_page_index_to_clusters(struct super_block *sb,
							unsigned long pg_index)
{
	u32 clusters = pg_index;
	unsigned int cbits = OCFS2_SB(sb)->s_clustersize_bits;

	if (unlikely(PAGE_CACHE_SHIFT > cbits))
		clusters = pg_index << (PAGE_CACHE_SHIFT - cbits);
	else if (PAGE_CACHE_SHIFT < cbits)
		clusters = pg_index >> (cbits - PAGE_CACHE_SHIFT);

	return clusters;
}

/*
 * Find the 1st page index which covers the given clusters.
 */
static inline pgoff_t ocfs2_align_clusters_to_page_index(struct super_block *sb,
							u32 clusters)
{
	unsigned int cbits = OCFS2_SB(sb)->s_clustersize_bits;
        pgoff_t index = clusters;

	if (PAGE_CACHE_SHIFT > cbits) {
		index = (pgoff_t)clusters >> (PAGE_CACHE_SHIFT - cbits);
	} else if (PAGE_CACHE_SHIFT < cbits) {
		index = (pgoff_t)clusters << (cbits - PAGE_CACHE_SHIFT);
	}

	return index;
}

static inline unsigned int ocfs2_pages_per_cluster(struct super_block *sb)
{
	unsigned int cbits = OCFS2_SB(sb)->s_clustersize_bits;
	unsigned int pages_per_cluster = 1;

	if (PAGE_CACHE_SHIFT < cbits)
		pages_per_cluster = 1 << (cbits - PAGE_CACHE_SHIFT);

	return pages_per_cluster;
}

static inline unsigned int ocfs2_megabytes_to_clusters(struct super_block *sb,
						       unsigned int megs)
{
	BUILD_BUG_ON(OCFS2_MAX_CLUSTERSIZE > 1048576);

	return megs << (20 - OCFS2_SB(sb)->s_clustersize_bits);
}

static inline void ocfs2_init_inode_steal_slot(struct ocfs2_super *osb)
{
	spin_lock(&osb->osb_lock);
	osb->s_inode_steal_slot = OCFS2_INVALID_SLOT;
	spin_unlock(&osb->osb_lock);
	atomic_set(&osb->s_num_inodes_stolen, 0);
}

static inline void ocfs2_set_inode_steal_slot(struct ocfs2_super *osb,
					      s16 slot)
{
	spin_lock(&osb->osb_lock);
	osb->s_inode_steal_slot = slot;
	spin_unlock(&osb->osb_lock);
}

static inline s16 ocfs2_get_inode_steal_slot(struct ocfs2_super *osb)
{
	s16 slot;

	spin_lock(&osb->osb_lock);
	slot = osb->s_inode_steal_slot;
	spin_unlock(&osb->osb_lock);

	return slot;
}

#define ocfs2_set_bit ext2_set_bit
#define ocfs2_clear_bit ext2_clear_bit
#define ocfs2_test_bit ext2_test_bit
#define ocfs2_find_next_zero_bit ext2_find_next_zero_bit
#define ocfs2_find_next_bit ext2_find_next_bit
#endif  /* OCFS2_H */

