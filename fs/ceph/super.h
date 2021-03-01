/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FS_CEPH_SUPER_H
#define _FS_CEPH_SUPER_H

#include <linux/ceph/ceph_debug.h>

#include <asm/unaligned.h>
#include <linux/backing-dev.h>
#include <linux/completion.h>
#include <linux/exportfs.h>
#include <linux/fs.h>
#include <linux/mempool.h>
#include <linux/pagemap.h>
#include <linux/wait.h>
#include <linux/writeback.h>
#include <linux/slab.h>
#include <linux/posix_acl.h>
#include <linux/refcount.h>
#include <linux/security.h>

#include <linux/ceph/libceph.h>

#ifdef CONFIG_CEPH_FSCACHE
#include <linux/fscache.h>
#endif

/* f_type in struct statfs */
#define CEPH_SUPER_MAGIC 0x00c36400

/* large granularity for statfs utilization stats to facilitate
 * large volume sizes on 32-bit machines. */
#define CEPH_BLOCK_SHIFT   22  /* 4 MB */
#define CEPH_BLOCK         (1 << CEPH_BLOCK_SHIFT)

#define CEPH_MOUNT_OPT_CLEANRECOVER    (1<<1) /* auto reonnect (clean mode) after blocklisted */
#define CEPH_MOUNT_OPT_DIRSTAT         (1<<4) /* `cat dirname` for stats */
#define CEPH_MOUNT_OPT_RBYTES          (1<<5) /* dir st_bytes = rbytes */
#define CEPH_MOUNT_OPT_NOASYNCREADDIR  (1<<7) /* no dcache readdir */
#define CEPH_MOUNT_OPT_INO32           (1<<8) /* 32 bit inos */
#define CEPH_MOUNT_OPT_DCACHE          (1<<9) /* use dcache for readdir etc */
#define CEPH_MOUNT_OPT_FSCACHE         (1<<10) /* use fscache */
#define CEPH_MOUNT_OPT_NOPOOLPERM      (1<<11) /* no pool permission check */
#define CEPH_MOUNT_OPT_MOUNTWAIT       (1<<12) /* mount waits if no mds is up */
#define CEPH_MOUNT_OPT_NOQUOTADF       (1<<13) /* no root dir quota in statfs */
#define CEPH_MOUNT_OPT_NOCOPYFROM      (1<<14) /* don't use RADOS 'copy-from' op */
#define CEPH_MOUNT_OPT_ASYNC_DIROPS    (1<<15) /* allow async directory ops */

#define CEPH_MOUNT_OPT_DEFAULT			\
	(CEPH_MOUNT_OPT_DCACHE |		\
	 CEPH_MOUNT_OPT_NOCOPYFROM)

#define ceph_set_mount_opt(fsc, opt) \
	(fsc)->mount_options->flags |= CEPH_MOUNT_OPT_##opt
#define ceph_clear_mount_opt(fsc, opt) \
	(fsc)->mount_options->flags &= ~CEPH_MOUNT_OPT_##opt
#define ceph_test_mount_opt(fsc, opt) \
	(!!((fsc)->mount_options->flags & CEPH_MOUNT_OPT_##opt))

/* max size of osd read request, limited by libceph */
#define CEPH_MAX_READ_SIZE              CEPH_MSG_MAX_DATA_LEN
/* osd has a configurable limitaion of max write size.
 * CEPH_MSG_MAX_DATA_LEN should be small enough. */
#define CEPH_MAX_WRITE_SIZE		CEPH_MSG_MAX_DATA_LEN
#define CEPH_RASIZE_DEFAULT             (8192*1024)    /* max readahead */
#define CEPH_MAX_READDIR_DEFAULT        1024
#define CEPH_MAX_READDIR_BYTES_DEFAULT  (512*1024)
#define CEPH_SNAPDIRNAME_DEFAULT        ".snap"

/*
 * Delay telling the MDS we no longer want caps, in case we reopen
 * the file.  Delay a minimum amount of time, even if we send a cap
 * message for some other reason.  Otherwise, take the oppotunity to
 * update the mds to avoid sending another message later.
 */
#define CEPH_CAPS_WANTED_DELAY_MIN_DEFAULT      5  /* cap release delay */
#define CEPH_CAPS_WANTED_DELAY_MAX_DEFAULT     60  /* cap release delay */

struct ceph_mount_options {
	unsigned int flags;

	unsigned int wsize;            /* max write size */
	unsigned int rsize;            /* max read size */
	unsigned int rasize;           /* max readahead */
	unsigned int congestion_kb;    /* max writeback in flight */
	unsigned int caps_wanted_delay_min, caps_wanted_delay_max;
	int caps_max;
	unsigned int max_readdir;       /* max readdir result (entries) */
	unsigned int max_readdir_bytes; /* max readdir result (bytes) */

	/*
	 * everything above this point can be memcmp'd; everything below
	 * is handled in compare_mount_options()
	 */

	char *snapdir_name;   /* default ".snap" */
	char *mds_namespace;  /* default NULL */
	char *server_path;    /* default NULL (means "/") */
	char *fscache_uniq;   /* default NULL */
};

struct ceph_fs_client {
	struct super_block *sb;

	struct list_head metric_wakeup;

	struct ceph_mount_options *mount_options;
	struct ceph_client *client;

	int mount_state;

	bool blocklisted;

	bool have_copy_from2;

	u32 filp_gen;
	loff_t max_file_size;

	struct ceph_mds_client *mdsc;

	atomic_long_t writeback_count;

	struct workqueue_struct *inode_wq;
	struct workqueue_struct *cap_wq;

#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs_dentry_lru, *debugfs_caps;
	struct dentry *debugfs_congestion_kb;
	struct dentry *debugfs_bdi;
	struct dentry *debugfs_mdsc, *debugfs_mdsmap;
	struct dentry *debugfs_metric;
	struct dentry *debugfs_status;
	struct dentry *debugfs_mds_sessions;
#endif

#ifdef CONFIG_CEPH_FSCACHE
	struct fscache_cookie *fscache;
#endif
};


/*
 * File i/o capability.  This tracks shared state with the metadata
 * server that allows us to cache or writeback attributes or to read
 * and write data.  For any given inode, we should have one or more
 * capabilities, one issued by each metadata server, and our
 * cumulative access is the OR of all issued capabilities.
 *
 * Each cap is referenced by the inode's i_caps rbtree and by per-mds
 * session capability lists.
 */
struct ceph_cap {
	struct ceph_inode_info *ci;
	struct rb_node ci_node;          /* per-ci cap tree */
	struct ceph_mds_session *session;
	struct list_head session_caps;   /* per-session caplist */
	u64 cap_id;       /* unique cap id (mds provided) */
	union {
		/* in-use caps */
		struct {
			int issued;       /* latest, from the mds */
			int implemented;  /* implemented superset of
					     issued (for revocation) */
			int mds;	  /* mds index for this cap */
			int mds_wanted;   /* caps wanted from this mds */
		};
		/* caps to release */
		struct {
			u64 cap_ino;
			int queue_release;
		};
	};
	u32 seq, issue_seq, mseq;
	u32 cap_gen;      /* active/stale cycle */
	unsigned long last_used;
	struct list_head caps_item;
};

#define CHECK_CAPS_AUTHONLY   1  /* only check auth cap */
#define CHECK_CAPS_FLUSH      2  /* flush any dirty caps */
#define CHECK_CAPS_NOINVAL    4  /* don't invalidate pagecache */

struct ceph_cap_flush {
	u64 tid;
	int caps; /* 0 means capsnap */
	bool wake; /* wake up flush waiters when finish ? */
	struct list_head g_list; // global
	struct list_head i_list; // per inode
};

/*
 * Snapped cap state that is pending flush to mds.  When a snapshot occurs,
 * we first complete any in-process sync writes and writeback any dirty
 * data before flushing the snapped state (tracked here) back to the MDS.
 */
struct ceph_cap_snap {
	refcount_t nref;
	struct list_head ci_item;

	struct ceph_cap_flush cap_flush;

	u64 follows;
	int issued, dirty;
	struct ceph_snap_context *context;

	umode_t mode;
	kuid_t uid;
	kgid_t gid;

	struct ceph_buffer *xattr_blob;
	u64 xattr_version;

	u64 size;
	u64 change_attr;
	struct timespec64 mtime, atime, ctime, btime;
	u64 time_warp_seq;
	u64 truncate_size;
	u32 truncate_seq;
	int writing;   /* a sync write is still in progress */
	int dirty_pages;     /* dirty pages awaiting writeback */
	bool inline_data;
	bool need_flush;
};

static inline void ceph_put_cap_snap(struct ceph_cap_snap *capsnap)
{
	if (refcount_dec_and_test(&capsnap->nref)) {
		if (capsnap->xattr_blob)
			ceph_buffer_put(capsnap->xattr_blob);
		kfree(capsnap);
	}
}

/*
 * The frag tree describes how a directory is fragmented, potentially across
 * multiple metadata servers.  It is also used to indicate points where
 * metadata authority is delegated, and whether/where metadata is replicated.
 *
 * A _leaf_ frag will be present in the i_fragtree IFF there is
 * delegation info.  That is, if mds >= 0 || ndist > 0.
 */
#define CEPH_MAX_DIRFRAG_REP 4

struct ceph_inode_frag {
	struct rb_node node;

	/* fragtree state */
	u32 frag;
	int split_by;         /* i.e. 2^(split_by) children */

	/* delegation and replication info */
	int mds;              /* -1 if same authority as parent */
	int ndist;            /* >0 if replicated */
	int dist[CEPH_MAX_DIRFRAG_REP];
};

/*
 * We cache inode xattrs as an encoded blob until they are first used,
 * at which point we parse them into an rbtree.
 */
struct ceph_inode_xattr {
	struct rb_node node;

	const char *name;
	int name_len;
	const char *val;
	int val_len;
	int dirty;

	int should_free_name;
	int should_free_val;
};

/*
 * Ceph dentry state
 */
struct ceph_dentry_info {
	struct dentry *dentry;
	struct ceph_mds_session *lease_session;
	struct list_head lease_list;
	unsigned flags;
	int lease_shared_gen;
	u32 lease_gen;
	u32 lease_seq;
	unsigned long lease_renew_after, lease_renew_from;
	unsigned long time;
	u64 offset;
};

#define CEPH_DENTRY_REFERENCED		1
#define CEPH_DENTRY_LEASE_LIST		2
#define CEPH_DENTRY_SHRINK_LIST		4
#define CEPH_DENTRY_PRIMARY_LINK	8

struct ceph_inode_xattrs_info {
	/*
	 * (still encoded) xattr blob. we avoid the overhead of parsing
	 * this until someone actually calls getxattr, etc.
	 *
	 * blob->vec.iov_len == 4 implies there are no xattrs; blob ==
	 * NULL means we don't know.
	*/
	struct ceph_buffer *blob, *prealloc_blob;

	struct rb_root index;
	bool dirty;
	int count;
	int names_size;
	int vals_size;
	u64 version, index_version;
};

/*
 * Ceph inode.
 */
struct ceph_inode_info {
	struct ceph_vino i_vino;   /* ceph ino + snap */

	spinlock_t i_ceph_lock;

	u64 i_version;
	u64 i_inline_version;
	u32 i_time_warp_seq;

	unsigned long i_ceph_flags;
	atomic64_t i_release_count;
	atomic64_t i_ordered_count;
	atomic64_t i_complete_seq[2];

	struct ceph_dir_layout i_dir_layout;
	struct ceph_file_layout i_layout;
	struct ceph_file_layout i_cached_layout;	// for async creates
	char *i_symlink;

	/* for dirs */
	struct timespec64 i_rctime;
	u64 i_rbytes, i_rfiles, i_rsubdirs;
	u64 i_files, i_subdirs;

	/* quotas */
	u64 i_max_bytes, i_max_files;

	s32 i_dir_pin;

	struct rb_root i_fragtree;
	int i_fragtree_nsplits;
	struct mutex i_fragtree_mutex;

	struct ceph_inode_xattrs_info i_xattrs;

	/* capabilities.  protected _both_ by i_ceph_lock and cap->session's
	 * s_mutex. */
	struct rb_root i_caps;           /* cap list */
	struct ceph_cap *i_auth_cap;     /* authoritative cap, if any */
	unsigned i_dirty_caps, i_flushing_caps;     /* mask of dirtied fields */

	/*
	 * Link to the auth cap's session's s_cap_dirty list. s_cap_dirty
	 * is protected by the mdsc->cap_dirty_lock, but each individual item
	 * is also protected by the inode's i_ceph_lock. Walking s_cap_dirty
	 * requires the mdsc->cap_dirty_lock. List presence for an item can
	 * be tested under the i_ceph_lock. Changing anything requires both.
	 */
	struct list_head i_dirty_item;

	/*
	 * Link to session's s_cap_flushing list. Protected in a similar
	 * fashion to i_dirty_item, but also by the s_mutex for changes. The
	 * s_cap_flushing list can be walked while holding either the s_mutex
	 * or msdc->cap_dirty_lock. List presence can also be checked while
	 * holding the i_ceph_lock for this inode.
	 */
	struct list_head i_flushing_item;

	/* we need to track cap writeback on a per-cap-bit basis, to allow
	 * overlapping, pipelined cap flushes to the mds.  we can probably
	 * reduce the tid to 8 bits if we're concerned about inode size. */
	struct ceph_cap_flush *i_prealloc_cap_flush;
	struct list_head i_cap_flush_list;
	wait_queue_head_t i_cap_wq;      /* threads waiting on a capability */
	unsigned long i_hold_caps_max; /* jiffies */
	struct list_head i_cap_delay_list;  /* for delayed cap release to mds */
	struct ceph_cap_reservation i_cap_migration_resv;
	struct list_head i_cap_snaps;   /* snapped state pending flush to mds */
	struct ceph_snap_context *i_head_snapc;  /* set if wr_buffer_head > 0 or
						    dirty|flushing caps */
	unsigned i_snap_caps;           /* cap bits for snapped files */

	unsigned long i_last_rd;
	unsigned long i_last_wr;
	int i_nr_by_mode[CEPH_FILE_MODE_BITS];  /* open file counts */

	struct mutex i_truncate_mutex;
	u32 i_truncate_seq;        /* last truncate to smaller size */
	u64 i_truncate_size;       /*  and the size we last truncated down to */
	int i_truncate_pending;    /*  still need to call vmtruncate */

	u64 i_max_size;            /* max file size authorized by mds */
	u64 i_reported_size; /* (max_)size reported to or requested of mds */
	u64 i_wanted_max_size;     /* offset we'd like to write too */
	u64 i_requested_max_size;  /* max_size we've requested */

	/* held references to caps */
	int i_pin_ref;
	int i_rd_ref, i_rdcache_ref, i_wr_ref, i_wb_ref, i_fx_ref;
	int i_wrbuffer_ref, i_wrbuffer_ref_head;
	atomic_t i_filelock_ref;
	atomic_t i_shared_gen;       /* increment each time we get FILE_SHARED */
	u32 i_rdcache_gen;      /* incremented each time we get FILE_CACHE. */
	u32 i_rdcache_revoking; /* RDCACHE gen to async invalidate, if any */

	struct list_head i_unsafe_dirops; /* uncommitted mds dir ops */
	struct list_head i_unsafe_iops;   /* uncommitted mds inode ops */
	spinlock_t i_unsafe_lock;

	union {
		struct ceph_snap_realm *i_snap_realm; /* snap realm (if caps) */
		struct ceph_snapid_map *i_snapid_map; /* snapid -> dev_t */
	};
	int i_snap_realm_counter; /* snap realm (if caps) */
	struct list_head i_snap_realm_item;
	struct list_head i_snap_flush_item;
	struct timespec64 i_btime;
	struct timespec64 i_snap_btime;

	struct work_struct i_work;
	unsigned long  i_work_mask;

#ifdef CONFIG_CEPH_FSCACHE
	struct fscache_cookie *fscache;
	u32 i_fscache_gen;
#endif
	errseq_t i_meta_err;

	struct inode vfs_inode; /* at end */
};

static inline struct ceph_inode_info *
ceph_inode(const struct inode *inode)
{
	return container_of(inode, struct ceph_inode_info, vfs_inode);
}

static inline struct ceph_fs_client *
ceph_inode_to_client(const struct inode *inode)
{
	return (struct ceph_fs_client *)inode->i_sb->s_fs_info;
}

static inline struct ceph_fs_client *
ceph_sb_to_client(const struct super_block *sb)
{
	return (struct ceph_fs_client *)sb->s_fs_info;
}

static inline struct ceph_mds_client *
ceph_sb_to_mdsc(const struct super_block *sb)
{
	return (struct ceph_mds_client *)ceph_sb_to_client(sb)->mdsc;
}

static inline struct ceph_vino
ceph_vino(const struct inode *inode)
{
	return ceph_inode(inode)->i_vino;
}

static inline u32 ceph_ino_to_ino32(u64 vino)
{
	u32 ino = vino & 0xffffffff;
	ino ^= vino >> 32;
	if (!ino)
		ino = 2;
	return ino;
}

/*
 * Inode numbers in cephfs are 64 bits, but inode->i_ino is 32-bits on
 * some arches. We generally do not use this value inside the ceph driver, but
 * we do want to set it to something, so that generic vfs code has an
 * appropriate value for tracepoints and the like.
 */
static inline ino_t ceph_vino_to_ino_t(struct ceph_vino vino)
{
	if (sizeof(ino_t) == sizeof(u32))
		return ceph_ino_to_ino32(vino.ino);
	return (ino_t)vino.ino;
}

/* for printf-style formatting */
#define ceph_vinop(i) ceph_inode(i)->i_vino.ino, ceph_inode(i)->i_vino.snap

static inline u64 ceph_ino(struct inode *inode)
{
	return ceph_inode(inode)->i_vino.ino;
}

static inline u64 ceph_snap(struct inode *inode)
{
	return ceph_inode(inode)->i_vino.snap;
}

/**
 * ceph_present_ino - format an inode number for presentation to userland
 * @sb: superblock where the inode lives
 * @ino: inode number to (possibly) convert
 *
 * If the user mounted with the ino32 option, then the 64-bit value needs
 * to be converted to something that can fit inside 32 bits. Note that
 * internal kernel code never uses this value, so this is entirely for
 * userland consumption.
 */
static inline u64 ceph_present_ino(struct super_block *sb, u64 ino)
{
	if (unlikely(ceph_test_mount_opt(ceph_sb_to_client(sb), INO32)))
		return ceph_ino_to_ino32(ino);
	return ino;
}

static inline u64 ceph_present_inode(struct inode *inode)
{
	return ceph_present_ino(inode->i_sb, ceph_ino(inode));
}

static inline int ceph_ino_compare(struct inode *inode, void *data)
{
	struct ceph_vino *pvino = (struct ceph_vino *)data;
	struct ceph_inode_info *ci = ceph_inode(inode);
	return ci->i_vino.ino == pvino->ino &&
		ci->i_vino.snap == pvino->snap;
}


static inline struct inode *ceph_find_inode(struct super_block *sb,
					    struct ceph_vino vino)
{
	/*
	 * NB: The hashval will be run through the fs/inode.c hash function
	 * anyway, so there is no need to squash the inode number down to
	 * 32-bits first. Just use low-order bits on arches with 32-bit long.
	 */
	return ilookup5(sb, (unsigned long)vino.ino, ceph_ino_compare, &vino);
}


/*
 * Ceph inode.
 */
#define CEPH_I_DIR_ORDERED	(1 << 0)  /* dentries in dir are ordered */
#define CEPH_I_FLUSH		(1 << 2)  /* do not delay flush of dirty metadata */
#define CEPH_I_POOL_PERM	(1 << 3)  /* pool rd/wr bits are valid */
#define CEPH_I_POOL_RD		(1 << 4)  /* can read from pool */
#define CEPH_I_POOL_WR		(1 << 5)  /* can write to pool */
#define CEPH_I_SEC_INITED	(1 << 6)  /* security initialized */
#define CEPH_I_KICK_FLUSH	(1 << 7)  /* kick flushing caps */
#define CEPH_I_FLUSH_SNAPS	(1 << 8)  /* need flush snapss */
#define CEPH_I_ERROR_WRITE	(1 << 9) /* have seen write errors */
#define CEPH_I_ERROR_FILELOCK	(1 << 10) /* have seen file lock errors */
#define CEPH_I_ODIRECT		(1 << 11) /* inode in direct I/O mode */
#define CEPH_ASYNC_CREATE_BIT	(12)	  /* async create in flight for this */
#define CEPH_I_ASYNC_CREATE	(1 << CEPH_ASYNC_CREATE_BIT)

/*
 * Masks of ceph inode work.
 */
#define CEPH_I_WORK_WRITEBACK		0
#define CEPH_I_WORK_INVALIDATE_PAGES	1
#define CEPH_I_WORK_VMTRUNCATE		2
#define CEPH_I_WORK_CHECK_CAPS		3
#define CEPH_I_WORK_FLUSH_SNAPS		4

/*
 * We set the ERROR_WRITE bit when we start seeing write errors on an inode
 * and then clear it when they start succeeding. Note that we do a lockless
 * check first, and only take the lock if it looks like it needs to be changed.
 * The write submission code just takes this as a hint, so we're not too
 * worried if a few slip through in either direction.
 */
static inline void ceph_set_error_write(struct ceph_inode_info *ci)
{
	if (!(READ_ONCE(ci->i_ceph_flags) & CEPH_I_ERROR_WRITE)) {
		spin_lock(&ci->i_ceph_lock);
		ci->i_ceph_flags |= CEPH_I_ERROR_WRITE;
		spin_unlock(&ci->i_ceph_lock);
	}
}

static inline void ceph_clear_error_write(struct ceph_inode_info *ci)
{
	if (READ_ONCE(ci->i_ceph_flags) & CEPH_I_ERROR_WRITE) {
		spin_lock(&ci->i_ceph_lock);
		ci->i_ceph_flags &= ~CEPH_I_ERROR_WRITE;
		spin_unlock(&ci->i_ceph_lock);
	}
}

static inline void __ceph_dir_set_complete(struct ceph_inode_info *ci,
					   long long release_count,
					   long long ordered_count)
{
	/*
	 * Makes sure operations that setup readdir cache (update page
	 * cache and i_size) are strongly ordered w.r.t. the following
	 * atomic64_set() operations.
	 */
	smp_mb();
	atomic64_set(&ci->i_complete_seq[0], release_count);
	atomic64_set(&ci->i_complete_seq[1], ordered_count);
}

static inline void __ceph_dir_clear_complete(struct ceph_inode_info *ci)
{
	atomic64_inc(&ci->i_release_count);
}

static inline void __ceph_dir_clear_ordered(struct ceph_inode_info *ci)
{
	atomic64_inc(&ci->i_ordered_count);
}

static inline bool __ceph_dir_is_complete(struct ceph_inode_info *ci)
{
	return atomic64_read(&ci->i_complete_seq[0]) ==
		atomic64_read(&ci->i_release_count);
}

static inline bool __ceph_dir_is_complete_ordered(struct ceph_inode_info *ci)
{
	return  atomic64_read(&ci->i_complete_seq[0]) ==
		atomic64_read(&ci->i_release_count) &&
		atomic64_read(&ci->i_complete_seq[1]) ==
		atomic64_read(&ci->i_ordered_count);
}

static inline void ceph_dir_clear_complete(struct inode *inode)
{
	__ceph_dir_clear_complete(ceph_inode(inode));
}

static inline void ceph_dir_clear_ordered(struct inode *inode)
{
	__ceph_dir_clear_ordered(ceph_inode(inode));
}

static inline bool ceph_dir_is_complete_ordered(struct inode *inode)
{
	bool ret = __ceph_dir_is_complete_ordered(ceph_inode(inode));
	smp_rmb();
	return ret;
}

/* find a specific frag @f */
extern struct ceph_inode_frag *__ceph_find_frag(struct ceph_inode_info *ci,
						u32 f);

/*
 * choose fragment for value @v.  copy frag content to pfrag, if leaf
 * exists
 */
extern u32 ceph_choose_frag(struct ceph_inode_info *ci, u32 v,
			    struct ceph_inode_frag *pfrag,
			    int *found);

static inline struct ceph_dentry_info *ceph_dentry(const struct dentry *dentry)
{
	return (struct ceph_dentry_info *)dentry->d_fsdata;
}

/*
 * caps helpers
 */
static inline bool __ceph_is_any_real_caps(struct ceph_inode_info *ci)
{
	return !RB_EMPTY_ROOT(&ci->i_caps);
}

extern int __ceph_caps_issued(struct ceph_inode_info *ci, int *implemented);
extern int __ceph_caps_issued_mask(struct ceph_inode_info *ci, int mask, int t);
extern int __ceph_caps_issued_mask_metric(struct ceph_inode_info *ci, int mask,
					  int t);
extern int __ceph_caps_issued_other(struct ceph_inode_info *ci,
				    struct ceph_cap *cap);

static inline int ceph_caps_issued(struct ceph_inode_info *ci)
{
	int issued;
	spin_lock(&ci->i_ceph_lock);
	issued = __ceph_caps_issued(ci, NULL);
	spin_unlock(&ci->i_ceph_lock);
	return issued;
}

static inline int ceph_caps_issued_mask_metric(struct ceph_inode_info *ci,
					       int mask, int touch)
{
	int r;
	spin_lock(&ci->i_ceph_lock);
	r = __ceph_caps_issued_mask_metric(ci, mask, touch);
	spin_unlock(&ci->i_ceph_lock);
	return r;
}

static inline int __ceph_caps_dirty(struct ceph_inode_info *ci)
{
	return ci->i_dirty_caps | ci->i_flushing_caps;
}
extern struct ceph_cap_flush *ceph_alloc_cap_flush(void);
extern void ceph_free_cap_flush(struct ceph_cap_flush *cf);
extern int __ceph_mark_dirty_caps(struct ceph_inode_info *ci, int mask,
				  struct ceph_cap_flush **pcf);

extern int __ceph_caps_revoking_other(struct ceph_inode_info *ci,
				      struct ceph_cap *ocap, int mask);
extern int ceph_caps_revoking(struct ceph_inode_info *ci, int mask);
extern int __ceph_caps_used(struct ceph_inode_info *ci);

static inline bool __ceph_is_file_opened(struct ceph_inode_info *ci)
{
	return ci->i_nr_by_mode[0];
}
extern int __ceph_caps_file_wanted(struct ceph_inode_info *ci);
extern int __ceph_caps_wanted(struct ceph_inode_info *ci);

/* what the mds thinks we want */
extern int __ceph_caps_mds_wanted(struct ceph_inode_info *ci, bool check);

extern void ceph_caps_init(struct ceph_mds_client *mdsc);
extern void ceph_caps_finalize(struct ceph_mds_client *mdsc);
extern void ceph_adjust_caps_max_min(struct ceph_mds_client *mdsc,
				     struct ceph_mount_options *fsopt);
extern int ceph_reserve_caps(struct ceph_mds_client *mdsc,
			     struct ceph_cap_reservation *ctx, int need);
extern void ceph_unreserve_caps(struct ceph_mds_client *mdsc,
			       struct ceph_cap_reservation *ctx);
extern void ceph_reservation_status(struct ceph_fs_client *client,
				    int *total, int *avail, int *used,
				    int *reserved, int *min);



/*
 * we keep buffered readdir results attached to file->private_data
 */
#define CEPH_F_SYNC     1
#define CEPH_F_ATEND    2

struct ceph_file_info {
	short fmode;     /* initialized on open */
	short flags;     /* CEPH_F_* */

	spinlock_t rw_contexts_lock;
	struct list_head rw_contexts;

	errseq_t meta_err;
	u32 filp_gen;
	atomic_t num_locks;
};

struct ceph_dir_file_info {
	struct ceph_file_info file_info;

	/* readdir: position within the dir */
	u32 frag;
	struct ceph_mds_request *last_readdir;

	/* readdir: position within a frag */
	unsigned next_offset;  /* offset of next chunk (last_name's + 1) */
	char *last_name;       /* last entry in previous chunk */
	long long dir_release_count;
	long long dir_ordered_count;
	int readdir_cache_idx;

	/* used for -o dirstat read() on directory thing */
	char *dir_info;
	int dir_info_len;
};

struct ceph_rw_context {
	struct list_head list;
	struct task_struct *thread;
	int caps;
};

#define CEPH_DEFINE_RW_CONTEXT(_name, _caps)	\
	struct ceph_rw_context _name = {	\
		.thread = current,		\
		.caps = _caps,			\
	}

static inline void ceph_add_rw_context(struct ceph_file_info *cf,
				       struct ceph_rw_context *ctx)
{
	spin_lock(&cf->rw_contexts_lock);
	list_add(&ctx->list, &cf->rw_contexts);
	spin_unlock(&cf->rw_contexts_lock);
}

static inline void ceph_del_rw_context(struct ceph_file_info *cf,
				       struct ceph_rw_context *ctx)
{
	spin_lock(&cf->rw_contexts_lock);
	list_del(&ctx->list);
	spin_unlock(&cf->rw_contexts_lock);
}

static inline struct ceph_rw_context*
ceph_find_rw_context(struct ceph_file_info *cf)
{
	struct ceph_rw_context *ctx, *found = NULL;
	spin_lock(&cf->rw_contexts_lock);
	list_for_each_entry(ctx, &cf->rw_contexts, list) {
		if (ctx->thread == current) {
			found = ctx;
			break;
		}
	}
	spin_unlock(&cf->rw_contexts_lock);
	return found;
}

struct ceph_readdir_cache_control {
	struct page  *page;
	struct dentry **dentries;
	int index;
};

/*
 * A "snap realm" describes a subset of the file hierarchy sharing
 * the same set of snapshots that apply to it.  The realms themselves
 * are organized into a hierarchy, such that children inherit (some of)
 * the snapshots of their parents.
 *
 * All inodes within the realm that have capabilities are linked into a
 * per-realm list.
 */
struct ceph_snap_realm {
	u64 ino;
	struct inode *inode;
	atomic_t nref;
	struct rb_node node;

	u64 created, seq;
	u64 parent_ino;
	u64 parent_since;   /* snapid when our current parent became so */

	u64 *prior_parent_snaps;      /* snaps inherited from any parents we */
	u32 num_prior_parent_snaps;   /*  had prior to parent_since */
	u64 *snaps;                   /* snaps specific to this realm */
	u32 num_snaps;

	struct ceph_snap_realm *parent;
	struct list_head children;       /* list of child realms */
	struct list_head child_item;

	struct list_head empty_item;     /* if i have ref==0 */

	struct list_head dirty_item;     /* if realm needs new context */

	/* the current set of snaps for this realm */
	struct ceph_snap_context *cached_context;

	struct list_head inodes_with_caps;
	spinlock_t inodes_with_caps_lock;
};

static inline int default_congestion_kb(void)
{
	int congestion_kb;

	/*
	 * Copied from NFS
	 *
	 * congestion size, scale with available memory.
	 *
	 *  64MB:    8192k
	 * 128MB:   11585k
	 * 256MB:   16384k
	 * 512MB:   23170k
	 *   1GB:   32768k
	 *   2GB:   46340k
	 *   4GB:   65536k
	 *   8GB:   92681k
	 *  16GB:  131072k
	 *
	 * This allows larger machines to have larger/more transfers.
	 * Limit the default to 256M
	 */
	congestion_kb = (16*int_sqrt(totalram_pages())) << (PAGE_SHIFT-10);
	if (congestion_kb > 256*1024)
		congestion_kb = 256*1024;

	return congestion_kb;
}


/* super.c */
extern int ceph_force_reconnect(struct super_block *sb);
/* snap.c */
struct ceph_snap_realm *ceph_lookup_snap_realm(struct ceph_mds_client *mdsc,
					       u64 ino);
extern void ceph_get_snap_realm(struct ceph_mds_client *mdsc,
				struct ceph_snap_realm *realm);
extern void ceph_put_snap_realm(struct ceph_mds_client *mdsc,
				struct ceph_snap_realm *realm);
extern int ceph_update_snap_trace(struct ceph_mds_client *m,
				  void *p, void *e, bool deletion,
				  struct ceph_snap_realm **realm_ret);
extern void ceph_handle_snap(struct ceph_mds_client *mdsc,
			     struct ceph_mds_session *session,
			     struct ceph_msg *msg);
extern void ceph_queue_cap_snap(struct ceph_inode_info *ci);
extern int __ceph_finish_cap_snap(struct ceph_inode_info *ci,
				  struct ceph_cap_snap *capsnap);
extern void ceph_cleanup_empty_realms(struct ceph_mds_client *mdsc);

extern struct ceph_snapid_map *ceph_get_snapid_map(struct ceph_mds_client *mdsc,
						   u64 snap);
extern void ceph_put_snapid_map(struct ceph_mds_client* mdsc,
				struct ceph_snapid_map *sm);
extern void ceph_trim_snapid_map(struct ceph_mds_client *mdsc);
extern void ceph_cleanup_snapid_map(struct ceph_mds_client *mdsc);


/*
 * a cap_snap is "pending" if it is still awaiting an in-progress
 * sync write (that may/may not still update size, mtime, etc.).
 */
static inline bool __ceph_have_pending_cap_snap(struct ceph_inode_info *ci)
{
	return !list_empty(&ci->i_cap_snaps) &&
	       list_last_entry(&ci->i_cap_snaps, struct ceph_cap_snap,
			       ci_item)->writing;
}

/* inode.c */
struct ceph_mds_reply_info_in;
struct ceph_mds_reply_dirfrag;

extern const struct inode_operations ceph_file_iops;

extern struct inode *ceph_alloc_inode(struct super_block *sb);
extern void ceph_evict_inode(struct inode *inode);
extern void ceph_free_inode(struct inode *inode);

extern struct inode *ceph_get_inode(struct super_block *sb,
				    struct ceph_vino vino);
extern struct inode *ceph_get_snapdir(struct inode *parent);
extern int ceph_fill_file_size(struct inode *inode, int issued,
			       u32 truncate_seq, u64 truncate_size, u64 size);
extern void ceph_fill_file_time(struct inode *inode, int issued,
				u64 time_warp_seq, struct timespec64 *ctime,
				struct timespec64 *mtime,
				struct timespec64 *atime);
extern int ceph_fill_inode(struct inode *inode, struct page *locked_page,
		    struct ceph_mds_reply_info_in *iinfo,
		    struct ceph_mds_reply_dirfrag *dirinfo,
		    struct ceph_mds_session *session, int cap_fmode,
		    struct ceph_cap_reservation *caps_reservation);
extern int ceph_fill_trace(struct super_block *sb,
			   struct ceph_mds_request *req);
extern int ceph_readdir_prepopulate(struct ceph_mds_request *req,
				    struct ceph_mds_session *session);

extern int ceph_inode_holds_cap(struct inode *inode, int mask);

extern bool ceph_inode_set_size(struct inode *inode, loff_t size);
extern void __ceph_do_pending_vmtruncate(struct inode *inode);

extern void ceph_async_iput(struct inode *inode);

void ceph_queue_inode_work(struct inode *inode, int work_bit);

static inline void ceph_queue_vmtruncate(struct inode *inode)
{
	ceph_queue_inode_work(inode, CEPH_I_WORK_VMTRUNCATE);
}

static inline void ceph_queue_invalidate(struct inode *inode)
{
	ceph_queue_inode_work(inode, CEPH_I_WORK_INVALIDATE_PAGES);
}

static inline void ceph_queue_writeback(struct inode *inode)
{
	ceph_queue_inode_work(inode, CEPH_I_WORK_WRITEBACK);
}

static inline void ceph_queue_check_caps(struct inode *inode)
{
	ceph_queue_inode_work(inode, CEPH_I_WORK_CHECK_CAPS);
}

static inline void ceph_queue_flush_snaps(struct inode *inode)
{
	ceph_queue_inode_work(inode, CEPH_I_WORK_FLUSH_SNAPS);
}

extern int __ceph_do_getattr(struct inode *inode, struct page *locked_page,
			     int mask, bool force);
static inline int ceph_do_getattr(struct inode *inode, int mask, bool force)
{
	return __ceph_do_getattr(inode, NULL, mask, force);
}
extern int ceph_permission(struct user_namespace *mnt_userns,
			   struct inode *inode, int mask);
extern int __ceph_setattr(struct inode *inode, struct iattr *attr);
extern int ceph_setattr(struct user_namespace *mnt_userns,
			struct dentry *dentry, struct iattr *attr);
extern int ceph_getattr(struct user_namespace *mnt_userns,
			const struct path *path, struct kstat *stat,
			u32 request_mask, unsigned int flags);

/* xattr.c */
int __ceph_setxattr(struct inode *, const char *, const void *, size_t, int);
ssize_t __ceph_getxattr(struct inode *, const char *, void *, size_t);
extern ssize_t ceph_listxattr(struct dentry *, char *, size_t);
extern struct ceph_buffer *__ceph_build_xattrs_blob(struct ceph_inode_info *ci);
extern void __ceph_destroy_xattrs(struct ceph_inode_info *ci);
extern const struct xattr_handler *ceph_xattr_handlers[];

struct ceph_acl_sec_ctx {
#ifdef CONFIG_CEPH_FS_POSIX_ACL
	void *default_acl;
	void *acl;
#endif
#ifdef CONFIG_CEPH_FS_SECURITY_LABEL
	void *sec_ctx;
	u32 sec_ctxlen;
#endif
	struct ceph_pagelist *pagelist;
};

#ifdef CONFIG_SECURITY
extern bool ceph_security_xattr_deadlock(struct inode *in);
extern bool ceph_security_xattr_wanted(struct inode *in);
#else
static inline bool ceph_security_xattr_deadlock(struct inode *in)
{
	return false;
}
static inline bool ceph_security_xattr_wanted(struct inode *in)
{
	return false;
}
#endif

#ifdef CONFIG_CEPH_FS_SECURITY_LABEL
extern int ceph_security_init_secctx(struct dentry *dentry, umode_t mode,
				     struct ceph_acl_sec_ctx *ctx);
static inline void ceph_security_invalidate_secctx(struct inode *inode)
{
	security_inode_invalidate_secctx(inode);
}
#else
static inline int ceph_security_init_secctx(struct dentry *dentry, umode_t mode,
					    struct ceph_acl_sec_ctx *ctx)
{
	return 0;
}
static inline void ceph_security_invalidate_secctx(struct inode *inode)
{
}
#endif

void ceph_release_acl_sec_ctx(struct ceph_acl_sec_ctx *as_ctx);

/* acl.c */
#ifdef CONFIG_CEPH_FS_POSIX_ACL

struct posix_acl *ceph_get_acl(struct inode *, int);
int ceph_set_acl(struct user_namespace *mnt_userns,
		 struct inode *inode, struct posix_acl *acl, int type);
int ceph_pre_init_acls(struct inode *dir, umode_t *mode,
		       struct ceph_acl_sec_ctx *as_ctx);
void ceph_init_inode_acls(struct inode *inode,
			  struct ceph_acl_sec_ctx *as_ctx);

static inline void ceph_forget_all_cached_acls(struct inode *inode)
{
       forget_all_cached_acls(inode);
}

#else

#define ceph_get_acl NULL
#define ceph_set_acl NULL

static inline int ceph_pre_init_acls(struct inode *dir, umode_t *mode,
				     struct ceph_acl_sec_ctx *as_ctx)
{
	return 0;
}
static inline void ceph_init_inode_acls(struct inode *inode,
					struct ceph_acl_sec_ctx *as_ctx)
{
}
static inline int ceph_acl_chmod(struct dentry *dentry, struct inode *inode)
{
	return 0;
}

static inline void ceph_forget_all_cached_acls(struct inode *inode)
{
}

#endif

/* caps.c */
extern const char *ceph_cap_string(int c);
extern void ceph_handle_caps(struct ceph_mds_session *session,
			     struct ceph_msg *msg);
extern struct ceph_cap *ceph_get_cap(struct ceph_mds_client *mdsc,
				     struct ceph_cap_reservation *ctx);
extern void ceph_add_cap(struct inode *inode,
			 struct ceph_mds_session *session, u64 cap_id,
			 unsigned issued, unsigned wanted,
			 unsigned cap, unsigned seq, u64 realmino, int flags,
			 struct ceph_cap **new_cap);
extern void __ceph_remove_cap(struct ceph_cap *cap, bool queue_release);
extern void __ceph_remove_caps(struct ceph_inode_info *ci);
extern void ceph_put_cap(struct ceph_mds_client *mdsc,
			 struct ceph_cap *cap);
extern int ceph_is_any_caps(struct inode *inode);

extern int ceph_write_inode(struct inode *inode, struct writeback_control *wbc);
extern int ceph_fsync(struct file *file, loff_t start, loff_t end,
		      int datasync);
extern void ceph_early_kick_flushing_caps(struct ceph_mds_client *mdsc,
					  struct ceph_mds_session *session);
extern void ceph_kick_flushing_caps(struct ceph_mds_client *mdsc,
				    struct ceph_mds_session *session);
void ceph_kick_flushing_inode_caps(struct ceph_mds_session *session,
				   struct ceph_inode_info *ci);
extern struct ceph_cap *ceph_get_cap_for_mds(struct ceph_inode_info *ci,
					     int mds);
extern void ceph_take_cap_refs(struct ceph_inode_info *ci, int caps,
				bool snap_rwsem_locked);
extern void ceph_get_cap_refs(struct ceph_inode_info *ci, int caps);
extern void ceph_put_cap_refs(struct ceph_inode_info *ci, int had);
extern void ceph_put_cap_refs_async(struct ceph_inode_info *ci, int had);
extern void ceph_put_cap_refs_no_check_caps(struct ceph_inode_info *ci,
					    int had);
extern void ceph_put_wrbuffer_cap_refs(struct ceph_inode_info *ci, int nr,
				       struct ceph_snap_context *snapc);
extern void ceph_flush_snaps(struct ceph_inode_info *ci,
			     struct ceph_mds_session **psession);
extern bool __ceph_should_report_size(struct ceph_inode_info *ci);
extern void ceph_check_caps(struct ceph_inode_info *ci, int flags,
			    struct ceph_mds_session *session);
extern void ceph_check_delayed_caps(struct ceph_mds_client *mdsc);
extern void ceph_flush_dirty_caps(struct ceph_mds_client *mdsc);
extern int  ceph_drop_caps_for_unlink(struct inode *inode);
extern int ceph_encode_inode_release(void **p, struct inode *inode,
				     int mds, int drop, int unless, int force);
extern int ceph_encode_dentry_release(void **p, struct dentry *dn,
				      struct inode *dir,
				      int mds, int drop, int unless);

extern int ceph_get_caps(struct file *filp, int need, int want,
			 loff_t endoff, int *got, struct page **pinned_page);
extern int ceph_try_get_caps(struct inode *inode,
			     int need, int want, bool nonblock, int *got);

/* for counting open files by mode */
extern void ceph_get_fmode(struct ceph_inode_info *ci, int mode, int count);
extern void ceph_put_fmode(struct ceph_inode_info *ci, int mode, int count);
extern void __ceph_touch_fmode(struct ceph_inode_info *ci,
			       struct ceph_mds_client *mdsc, int fmode);

/* addr.c */
extern const struct address_space_operations ceph_aops;
extern int ceph_mmap(struct file *file, struct vm_area_struct *vma);
extern int ceph_uninline_data(struct file *filp, struct page *locked_page);
extern int ceph_pool_perm_check(struct inode *inode, int need);
extern void ceph_pool_perm_destroy(struct ceph_mds_client* mdsc);

/* file.c */
extern const struct file_operations ceph_file_fops;

extern int ceph_renew_caps(struct inode *inode, int fmode);
extern int ceph_open(struct inode *inode, struct file *file);
extern int ceph_atomic_open(struct inode *dir, struct dentry *dentry,
			    struct file *file, unsigned flags, umode_t mode);
extern int ceph_release(struct inode *inode, struct file *filp);
extern void ceph_fill_inline_data(struct inode *inode, struct page *locked_page,
				  char *data, size_t len);

/* dir.c */
extern const struct file_operations ceph_dir_fops;
extern const struct file_operations ceph_snapdir_fops;
extern const struct inode_operations ceph_dir_iops;
extern const struct inode_operations ceph_snapdir_iops;
extern const struct dentry_operations ceph_dentry_ops;

extern loff_t ceph_make_fpos(unsigned high, unsigned off, bool hash_order);
extern int ceph_handle_notrace_create(struct inode *dir, struct dentry *dentry);
extern int ceph_handle_snapdir(struct ceph_mds_request *req,
			       struct dentry *dentry, int err);
extern struct dentry *ceph_finish_lookup(struct ceph_mds_request *req,
					 struct dentry *dentry, int err);

extern void __ceph_dentry_lease_touch(struct ceph_dentry_info *di);
extern void __ceph_dentry_dir_lease_touch(struct ceph_dentry_info *di);
extern void ceph_invalidate_dentry_lease(struct dentry *dentry);
extern int ceph_trim_dentries(struct ceph_mds_client *mdsc);
extern unsigned ceph_dentry_hash(struct inode *dir, struct dentry *dn);
extern void ceph_readdir_cache_release(struct ceph_readdir_cache_control *ctl);

/* ioctl.c */
extern long ceph_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

/* export.c */
extern const struct export_operations ceph_export_ops;
struct inode *ceph_lookup_inode(struct super_block *sb, u64 ino);

/* locks.c */
extern __init void ceph_flock_init(void);
extern int ceph_lock(struct file *file, int cmd, struct file_lock *fl);
extern int ceph_flock(struct file *file, int cmd, struct file_lock *fl);
extern void ceph_count_locks(struct inode *inode, int *p_num, int *f_num);
extern int ceph_encode_locks_to_buffer(struct inode *inode,
				       struct ceph_filelock *flocks,
				       int num_fcntl_locks,
				       int num_flock_locks);
extern int ceph_locks_to_pagelist(struct ceph_filelock *flocks,
				  struct ceph_pagelist *pagelist,
				  int num_fcntl_locks, int num_flock_locks);

/* debugfs.c */
extern void ceph_fs_debugfs_init(struct ceph_fs_client *client);
extern void ceph_fs_debugfs_cleanup(struct ceph_fs_client *client);

/* quota.c */
static inline bool __ceph_has_any_quota(struct ceph_inode_info *ci)
{
	return ci->i_max_files || ci->i_max_bytes;
}

extern void ceph_adjust_quota_realms_count(struct inode *inode, bool inc);

static inline void __ceph_update_quota(struct ceph_inode_info *ci,
				       u64 max_bytes, u64 max_files)
{
	bool had_quota, has_quota;
	had_quota = __ceph_has_any_quota(ci);
	ci->i_max_bytes = max_bytes;
	ci->i_max_files = max_files;
	has_quota = __ceph_has_any_quota(ci);

	if (had_quota != has_quota)
		ceph_adjust_quota_realms_count(&ci->vfs_inode, has_quota);
}

extern void ceph_handle_quota(struct ceph_mds_client *mdsc,
			      struct ceph_mds_session *session,
			      struct ceph_msg *msg);
extern bool ceph_quota_is_max_files_exceeded(struct inode *inode);
extern bool ceph_quota_is_same_realm(struct inode *old, struct inode *new);
extern bool ceph_quota_is_max_bytes_exceeded(struct inode *inode,
					     loff_t newlen);
extern bool ceph_quota_is_max_bytes_approaching(struct inode *inode,
						loff_t newlen);
extern bool ceph_quota_update_statfs(struct ceph_fs_client *fsc,
				     struct kstatfs *buf);
extern void ceph_cleanup_quotarealms_inodes(struct ceph_mds_client *mdsc);

#endif /* _FS_CEPH_SUPER_H */
