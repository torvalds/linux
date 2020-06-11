/* SPDX-License-Identifier: GPL-2.0-or-later */
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * ocfs2.h
 *
 * Defines macros and structures used in OCFS2
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 */

#ifndef OCFS2_H
#define OCFS2_H

#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/llist.h>
#include <linux/rbtree.h>
#include <linux/workqueue.h>
#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/lockdep.h>
#include <linux/jbd2.h>

/* For union ocfs2_dlm_lksb */
#include "stackglue.h"

#include "ocfs2_fs.h"
#include "ocfs2_lockid.h"
#include "ocfs2_ioctl.h"

/* For struct ocfs2_blockcheck_stats */
#include "blockcheck.h"

#include "reservations.h"

#include "filecheck.h"

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
#define OCFS2_LOCK_UPCONVERT_FINISHING (0x00000800) /* blocks the dc thread
						     * from downconverting
						     * before the upconvert
						     * has completed */

#define OCFS2_LOCK_NONBLOCK_FINISHED (0x00001000) /* NONBLOCK cluster
						   * lock has already
						   * returned, do not block
						   * dc thread from
						   * downconverting */

struct ocfs2_lock_res_ops;

typedef void (*ocfs2_lock_callback)(int status, unsigned long data);

#ifdef CONFIG_OCFS2_FS_STATS
struct ocfs2_lock_stats {
	u64		ls_total;	/* Total wait in NSEC */
	u32		ls_gets;	/* Num acquires */
	u32		ls_fail;	/* Num failed acquires */

	/* Storing max wait in usecs saves 24 bytes per inode */
	u32		ls_max;		/* Max wait in USEC */
	u64		ls_last;	/* Last unlock time in USEC */
};
#endif

struct ocfs2_lock_res {
	void                    *l_priv;
	struct ocfs2_lock_res_ops *l_ops;


	struct list_head         l_blocked_list;
	struct list_head         l_mask_waiters;
	struct list_head	 l_holders;

	unsigned long		 l_flags;
	char                     l_name[OCFS2_LOCK_ID_MAX_LEN];
	unsigned int             l_ro_holders;
	unsigned int             l_ex_holders;
	signed char		 l_level;
	signed char		 l_requested;
	signed char		 l_blocking;

	/* Data packed - type enum ocfs2_lock_type */
	unsigned char            l_type;

	/* used from AST/BAST funcs. */
	/* Data packed - enum type ocfs2_ast_action */
	unsigned char            l_action;
	/* Data packed - enum type ocfs2_unlock_action */
	unsigned char            l_unlock_action;
	unsigned int             l_pending_gen;

	spinlock_t               l_lock;

	struct ocfs2_dlm_lksb    l_lksb;

	wait_queue_head_t        l_event;

	struct list_head         l_debug_list;

#ifdef CONFIG_OCFS2_FS_STATS
	struct ocfs2_lock_stats  l_lock_prmode;		/* PR mode stats */
	u32                      l_lock_refresh;	/* Disk refreshes */
	u64                      l_lock_wait;	/* First lock wait time */
	struct ocfs2_lock_stats  l_lock_exmode;		/* EX mode stats */
#endif
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map	 l_lockdep_map;
#endif
};

enum ocfs2_orphan_reco_type {
	ORPHAN_NO_NEED_TRUNCATE = 0,
	ORPHAN_NEED_TRUNCATE,
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
	time64_t		os_scantime;  /* time this node ran the scan */
	u32			os_count;      /* tracks node specific scans */
	u32  			os_seqno;       /* tracks cluster wide scans */
	atomic_t		os_state;              /* ACTIVE or INACTIVE */
};

struct ocfs2_dlm_debug {
	struct kref d_refcnt;
	u32 d_filter_secs;
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
	OCFS2_MOUNT_HB_LOCAL = 1 << 0, /* Local heartbeat */
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
	OCFS2_MOUNT_COHERENCY_BUFFERED = 1 << 12, /* Allow concurrent O_DIRECT
						     writes */
	OCFS2_MOUNT_HB_NONE = 1 << 13, /* No heartbeat */
	OCFS2_MOUNT_HB_GLOBAL = 1 << 14, /* Global heartbeat */

	OCFS2_MOUNT_JOURNAL_ASYNC_COMMIT = 1 << 15,  /* Journal Async Commit */
	OCFS2_MOUNT_ERRORS_CONT = 1 << 16, /* Return EIO to the calling process on error */
	OCFS2_MOUNT_ERRORS_ROFS = 1 << 17, /* Change filesystem to read-only on error */
	OCFS2_MOUNT_NOCLUSTER = 1 << 18, /* No cluster aware filesystem mount */
};

#define OCFS2_OSB_SOFT_RO	0x0001
#define OCFS2_OSB_HARD_RO	0x0002
#define OCFS2_OSB_ERROR_FS	0x0004
#define OCFS2_DEFAULT_ATIME_QUANTUM	60

struct ocfs2_journal;
struct ocfs2_slot_info;
struct ocfs2_recovery_map;
struct ocfs2_replay_map;
struct ocfs2_quota_recovery;
struct ocfs2_super
{
	struct task_struct *commit_task;
	struct super_block *sb;
	struct inode *root_inode;
	struct inode *sys_root_inode;
	struct inode *global_system_inodes[NUM_GLOBAL_SYSTEM_INODES];
	struct inode **local_system_inodes;

	struct ocfs2_slot_info *slot_info;

	u32 *slot_recovery_generations;

	spinlock_t node_map_lock;

	u64 root_blkno;
	u64 system_dir_blkno;
	u64 bitmap_blkno;
	u32 bitmap_cpg;
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
	s16 s_meta_steal_slot;
	atomic_t s_num_inodes_stolen;
	atomic_t s_num_meta_stolen;

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
	struct ocfs2_journal *journal;
	unsigned long osb_commit_interval;

	struct delayed_work		la_enable_wq;

	/*
	 * Must hold local alloc i_mutex and osb->osb_lock to change
	 * local_alloc_bits. Reads can be done under either lock.
	 */
	unsigned int local_alloc_bits;
	unsigned int local_alloc_default_bits;
	/* osb_clusters_at_boot can become stale! Do not trust it to
	 * be up to date. */
	unsigned int osb_clusters_at_boot;

	enum ocfs2_local_alloc_state local_alloc_state; /* protected
							 * by osb_lock */

	struct buffer_head *local_alloc_bh;

	u64 la_last_gd;

	struct ocfs2_reservation_map	osb_la_resmap;

	unsigned int	osb_resv_level;
	unsigned int	osb_dir_resv_level;

	/* Next two fields are for local node slot recovery during
	 * mount. */
	struct ocfs2_dinode *local_alloc_copy;
	struct ocfs2_quota_recovery *quota_rec;

	struct ocfs2_blockcheck_stats osb_ecc_stats;
	struct ocfs2_alloc_stats alloc_stats;
	char dev_str[20];		/* "major,minor" of the device */

	u8 osb_stackflags;

	char osb_cluster_stack[OCFS2_STACK_LABEL_LEN + 1];
	char osb_cluster_name[OCFS2_CLUSTER_NAME_LEN + 1];
	struct ocfs2_cluster_connection *cconn;
	struct ocfs2_lock_res osb_super_lockres;
	struct ocfs2_lock_res osb_rename_lockres;
	struct ocfs2_lock_res osb_nfs_sync_lockres;
	struct ocfs2_lock_res osb_trim_fs_lockres;
	struct mutex obs_trim_fs_mutex;
	struct ocfs2_dlm_debug *osb_dlm_debug;

	struct dentry *osb_debug_root;

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

	/* List of dquot structures to drop last reference to */
	struct llist_head dquot_drop_list;
	struct work_struct dquot_drop_work;

	wait_queue_head_t		osb_mount_event;

	/* Truncate log info */
	struct inode			*osb_tl_inode;
	struct buffer_head		*osb_tl_bh;
	struct delayed_work		osb_truncate_log_wq;
	atomic_t			osb_tl_disable;
	/*
	 * How many clusters in our truncate log.
	 * It must be protected by osb_tl_inode->i_mutex.
	 */
	unsigned int truncated_clusters;

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

	struct mutex system_file_mutex;

	/*
	 * OCFS2 needs to schedule several different types of work which
	 * require cluster locking, disk I/O, recovery waits, etc. Since these
	 * types of work tend to be heavy we avoid using the kernel events
	 * workqueue and schedule on our own.
	 */
	struct workqueue_struct *ocfs2_wq;

	/* sysfs directory per partition */
	struct kset *osb_dev_kset;

	/* file check related stuff */
	struct ocfs2_filecheck_sysfs_entry osb_fc_ent;
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

static inline int ocfs2_supports_append_dio(struct ocfs2_super *osb)
{
	if (osb->s_feature_incompat & OCFS2_FEATURE_INCOMPAT_APPEND_DIO)
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

static inline int ocfs2_supports_discontig_bg(struct ocfs2_super *osb)
{
	if (osb->s_feature_incompat & OCFS2_FEATURE_INCOMPAT_DISCONTIG_BG)
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

static inline int ocfs2_clusterinfo_valid(struct ocfs2_super *osb)
{
	return (osb->s_feature_incompat &
		(OCFS2_FEATURE_INCOMPAT_USERSPACE_STACK |
		 OCFS2_FEATURE_INCOMPAT_CLUSTERINFO));
}

static inline int ocfs2_userspace_stack(struct ocfs2_super *osb)
{
	if (ocfs2_clusterinfo_valid(osb) &&
	    memcmp(osb->osb_cluster_stack, OCFS2_CLASSIC_CLUSTER_STACK,
		   OCFS2_STACK_LABEL_LEN))
		return 1;
	return 0;
}

static inline int ocfs2_o2cb_stack(struct ocfs2_super *osb)
{
	if (ocfs2_clusterinfo_valid(osb) &&
	    !memcmp(osb->osb_cluster_stack, OCFS2_CLASSIC_CLUSTER_STACK,
		   OCFS2_STACK_LABEL_LEN))
		return 1;
	return 0;
}

static inline int ocfs2_cluster_o2cb_global_heartbeat(struct ocfs2_super *osb)
{
	return ocfs2_o2cb_stack(osb) &&
		(osb->osb_stackflags & OCFS2_CLUSTER_O2CB_GLOBAL_HEARTBEAT);
}

static inline int ocfs2_mount_local(struct ocfs2_super *osb)
{
	return ((osb->s_feature_incompat & OCFS2_FEATURE_INCOMPAT_LOCAL_MOUNT)
		|| (osb->s_mount_opt & OCFS2_MOUNT_NOCLUSTER));
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

static inline u32 ocfs2_clusters_for_blocks(struct super_block *sb,
		u64 blocks)
{
	int b_to_c_bits = OCFS2_SB(sb)->s_clustersize_bits -
			sb->s_blocksize_bits;

	blocks += (1 << b_to_c_bits) - 1;
	return (u32)(blocks >> b_to_c_bits);
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

static inline unsigned int ocfs2_bytes_to_clusters(struct super_block *sb,
		u64 bytes)
{
	int cl_bits = OCFS2_SB(sb)->s_clustersize_bits;
	unsigned int clusters;

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

	if (unlikely(PAGE_SHIFT > cbits))
		clusters = pg_index << (PAGE_SHIFT - cbits);
	else if (PAGE_SHIFT < cbits)
		clusters = pg_index >> (cbits - PAGE_SHIFT);

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

	if (PAGE_SHIFT > cbits) {
		index = (pgoff_t)clusters >> (PAGE_SHIFT - cbits);
	} else if (PAGE_SHIFT < cbits) {
		index = (pgoff_t)clusters << (cbits - PAGE_SHIFT);
	}

	return index;
}

static inline unsigned int ocfs2_pages_per_cluster(struct super_block *sb)
{
	unsigned int cbits = OCFS2_SB(sb)->s_clustersize_bits;
	unsigned int pages_per_cluster = 1;

	if (PAGE_SHIFT < cbits)
		pages_per_cluster = 1 << (cbits - PAGE_SHIFT);

	return pages_per_cluster;
}

static inline unsigned int ocfs2_megabytes_to_clusters(struct super_block *sb,
						       unsigned int megs)
{
	BUILD_BUG_ON(OCFS2_MAX_CLUSTERSIZE > 1048576);

	return megs << (20 - OCFS2_SB(sb)->s_clustersize_bits);
}

static inline unsigned int ocfs2_clusters_to_megabytes(struct super_block *sb,
						       unsigned int clusters)
{
	return clusters >> (20 - OCFS2_SB(sb)->s_clustersize_bits);
}

static inline void _ocfs2_set_bit(unsigned int bit, unsigned long *bitmap)
{
	__set_bit_le(bit, bitmap);
}
#define ocfs2_set_bit(bit, addr) _ocfs2_set_bit((bit), (unsigned long *)(addr))

static inline void _ocfs2_clear_bit(unsigned int bit, unsigned long *bitmap)
{
	__clear_bit_le(bit, bitmap);
}
#define ocfs2_clear_bit(bit, addr) _ocfs2_clear_bit((bit), (unsigned long *)(addr))

#define ocfs2_test_bit test_bit_le
#define ocfs2_find_next_zero_bit find_next_zero_bit_le
#define ocfs2_find_next_bit find_next_bit_le

static inline void *correct_addr_and_bit_unaligned(int *bit, void *addr)
{
#if BITS_PER_LONG == 64
	*bit += ((unsigned long) addr & 7UL) << 3;
	addr = (void *) ((unsigned long) addr & ~7UL);
#elif BITS_PER_LONG == 32
	*bit += ((unsigned long) addr & 3UL) << 3;
	addr = (void *) ((unsigned long) addr & ~3UL);
#else
#error "how many bits you are?!"
#endif
	return addr;
}

static inline void ocfs2_set_bit_unaligned(int bit, void *bitmap)
{
	bitmap = correct_addr_and_bit_unaligned(&bit, bitmap);
	ocfs2_set_bit(bit, bitmap);
}

static inline void ocfs2_clear_bit_unaligned(int bit, void *bitmap)
{
	bitmap = correct_addr_and_bit_unaligned(&bit, bitmap);
	ocfs2_clear_bit(bit, bitmap);
}

static inline int ocfs2_test_bit_unaligned(int bit, void *bitmap)
{
	bitmap = correct_addr_and_bit_unaligned(&bit, bitmap);
	return ocfs2_test_bit(bit, bitmap);
}

static inline int ocfs2_find_next_zero_bit_unaligned(void *bitmap, int max,
							int start)
{
	int fix = 0, ret, tmpmax;
	bitmap = correct_addr_and_bit_unaligned(&fix, bitmap);
	tmpmax = max + fix;
	start += fix;

	ret = ocfs2_find_next_zero_bit(bitmap, tmpmax, start) - fix;
	if (ret > max)
		return max;
	return ret;
}

#endif  /* OCFS2_H */

