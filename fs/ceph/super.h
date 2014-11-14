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

#define CEPH_MOUNT_OPT_DIRSTAT         (1<<4) /* `cat dirname` for stats */
#define CEPH_MOUNT_OPT_RBYTES          (1<<5) /* dir st_bytes = rbytes */
#define CEPH_MOUNT_OPT_NOASYNCREADDIR  (1<<7) /* no dcache readdir */
#define CEPH_MOUNT_OPT_INO32           (1<<8) /* 32 bit inos */
#define CEPH_MOUNT_OPT_DCACHE          (1<<9) /* use dcache for readdir etc */
#define CEPH_MOUNT_OPT_FSCACHE         (1<<10) /* use fscache */

#define CEPH_MOUNT_OPT_DEFAULT    (CEPH_MOUNT_OPT_RBYTES)

#define ceph_set_mount_opt(fsc, opt) \
	(fsc)->mount_options->flags |= CEPH_MOUNT_OPT_##opt;
#define ceph_test_mount_opt(fsc, opt) \
	(!!((fsc)->mount_options->flags & CEPH_MOUNT_OPT_##opt))

#define CEPH_RSIZE_DEFAULT             0           /* max read size */
#define CEPH_RASIZE_DEFAULT            (8192*1024) /* readahead */
#define CEPH_MAX_READDIR_DEFAULT        1024
#define CEPH_MAX_READDIR_BYTES_DEFAULT  (512*1024)
#define CEPH_SNAPDIRNAME_DEFAULT        ".snap"

struct ceph_mount_options {
	int flags;
	int sb_flags;

	int wsize;            /* max write size */
	int rsize;            /* max read size */
	int rasize;           /* max readahead */
	int congestion_kb;    /* max writeback in flight */
	int caps_wanted_delay_min, caps_wanted_delay_max;
	int cap_release_safety;
	int max_readdir;       /* max readdir result (entires) */
	int max_readdir_bytes; /* max readdir result (bytes) */

	/*
	 * everything above this point can be memcmp'd; everything below
	 * is handled in compare_mount_options()
	 */

	char *snapdir_name;   /* default ".snap" */
};

struct ceph_fs_client {
	struct super_block *sb;

	struct ceph_mount_options *mount_options;
	struct ceph_client *client;

	unsigned long mount_state;
	int min_caps;                  /* min caps i added */

	struct ceph_mds_client *mdsc;

	/* writeback */
	mempool_t *wb_pagevec_pool;
	struct workqueue_struct *wb_wq;
	struct workqueue_struct *pg_inv_wq;
	struct workqueue_struct *trunc_wq;
	atomic_long_t writeback_count;

	struct backing_dev_info backing_dev_info;

#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs_dentry_lru, *debugfs_caps;
	struct dentry *debugfs_congestion_kb;
	struct dentry *debugfs_bdi;
	struct dentry *debugfs_mdsc, *debugfs_mdsmap;
	struct dentry *debugfs_mds_sessions;
#endif

#ifdef CONFIG_CEPH_FSCACHE
	struct fscache_cookie *fscache;
	struct workqueue_struct *revalidate_wq;
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
	int mds;
	u64 cap_id;       /* unique cap id (mds provided) */
	int issued;       /* latest, from the mds */
	int implemented;  /* implemented superset of issued (for revocation) */
	int mds_wanted;
	u32 seq, issue_seq, mseq;
	u32 cap_gen;      /* active/stale cycle */
	unsigned long last_used;
	struct list_head caps_item;
};

#define CHECK_CAPS_NODELAY    1  /* do not delay any further */
#define CHECK_CAPS_AUTHONLY   2  /* only check auth cap */
#define CHECK_CAPS_FLUSH      4  /* flush any dirty caps */

/*
 * Snapped cap state that is pending flush to mds.  When a snapshot occurs,
 * we first complete any in-process sync writes and writeback any dirty
 * data before flushing the snapped state (tracked here) back to the MDS.
 */
struct ceph_cap_snap {
	atomic_t nref;
	struct ceph_inode_info *ci;
	struct list_head ci_item, flushing_item;

	u64 follows, flush_tid;
	int issued, dirty;
	struct ceph_snap_context *context;

	umode_t mode;
	kuid_t uid;
	kgid_t gid;

	struct ceph_buffer *xattr_blob;
	u64 xattr_version;

	u64 size;
	struct timespec mtime, atime, ctime;
	u64 time_warp_seq;
	int writing;   /* a sync write is still in progress */
	int dirty_pages;     /* dirty pages awaiting writeback */
};

static inline void ceph_put_cap_snap(struct ceph_cap_snap *capsnap)
{
	if (atomic_dec_and_test(&capsnap->nref)) {
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
	struct ceph_mds_session *lease_session;
	u32 lease_gen, lease_shared_gen;
	u32 lease_seq;
	unsigned long lease_renew_after, lease_renew_from;
	struct list_head lru;
	struct dentry *dentry;
	u64 time;
	u64 offset;
};

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

	unsigned i_ceph_flags;
	int i_ordered_count;
	atomic_t i_release_count;
	atomic_t i_complete_count;

	struct ceph_dir_layout i_dir_layout;
	struct ceph_file_layout i_layout;
	char *i_symlink;

	/* for dirs */
	struct timespec i_rctime;
	u64 i_rbytes, i_rfiles, i_rsubdirs;
	u64 i_files, i_subdirs;

	struct rb_root i_fragtree;
	struct mutex i_fragtree_mutex;

	struct ceph_inode_xattrs_info i_xattrs;

	/* capabilities.  protected _both_ by i_ceph_lock and cap->session's
	 * s_mutex. */
	struct rb_root i_caps;           /* cap list */
	struct ceph_cap *i_auth_cap;     /* authoritative cap, if any */
	unsigned i_dirty_caps, i_flushing_caps;     /* mask of dirtied fields */
	struct list_head i_dirty_item, i_flushing_item;
	u64 i_cap_flush_seq;
	/* we need to track cap writeback on a per-cap-bit basis, to allow
	 * overlapping, pipelined cap flushes to the mds.  we can probably
	 * reduce the tid to 8 bits if we're concerned about inode size. */
	u16 i_cap_flush_last_tid, i_cap_flush_tid[CEPH_CAP_BITS];
	wait_queue_head_t i_cap_wq;      /* threads waiting on a capability */
	unsigned long i_hold_caps_min; /* jiffies */
	unsigned long i_hold_caps_max; /* jiffies */
	struct list_head i_cap_delay_list;  /* for delayed cap release to mds */
	struct ceph_cap_reservation i_cap_migration_resv;
	struct list_head i_cap_snaps;   /* snapped state pending flush to mds */
	struct ceph_snap_context *i_head_snapc;  /* set if wr_buffer_head > 0 or
						    dirty|flushing caps */
	unsigned i_snap_caps;           /* cap bits for snapped files */

	int i_nr_by_mode[CEPH_FILE_MODE_NUM];  /* open file counts */

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
	int i_rd_ref, i_rdcache_ref, i_wr_ref, i_wb_ref;
	int i_wrbuffer_ref, i_wrbuffer_ref_head;
	u32 i_shared_gen;       /* increment each time we get FILE_SHARED */
	u32 i_rdcache_gen;      /* incremented each time we get FILE_CACHE. */
	u32 i_rdcache_revoking; /* RDCACHE gen to async invalidate, if any */

	struct list_head i_unsafe_writes; /* uncommitted sync writes */
	struct list_head i_unsafe_dirops; /* uncommitted mds dir ops */
	spinlock_t i_unsafe_lock;

	struct ceph_snap_realm *i_snap_realm; /* snap realm (if caps) */
	int i_snap_realm_counter; /* snap realm (if caps) */
	struct list_head i_snap_realm_item;
	struct list_head i_snap_flush_item;

	struct work_struct i_wb_work;  /* writeback work */
	struct work_struct i_pg_inv_work;  /* page invalidation work */

	struct work_struct i_vmtruncate_work;

#ifdef CONFIG_CEPH_FSCACHE
	struct fscache_cookie *fscache;
	u32 i_fscache_gen; /* sequence, for delayed fscache validate */
	struct work_struct i_revalidate_work;
#endif
	struct inode vfs_inode; /* at end */
};

static inline struct ceph_inode_info *ceph_inode(struct inode *inode)
{
	return container_of(inode, struct ceph_inode_info, vfs_inode);
}

static inline struct ceph_fs_client *ceph_inode_to_client(struct inode *inode)
{
	return (struct ceph_fs_client *)inode->i_sb->s_fs_info;
}

static inline struct ceph_fs_client *ceph_sb_to_client(struct super_block *sb)
{
	return (struct ceph_fs_client *)sb->s_fs_info;
}

static inline struct ceph_vino ceph_vino(struct inode *inode)
{
	return ceph_inode(inode)->i_vino;
}

/*
 * ino_t is <64 bits on many architectures, blech.
 *
 *               i_ino (kernel inode)   st_ino (userspace)
 * i386          32                     32
 * x86_64+ino32  64                     32
 * x86_64        64                     64
 */
static inline u32 ceph_ino_to_ino32(__u64 vino)
{
	u32 ino = vino & 0xffffffff;
	ino ^= vino >> 32;
	if (!ino)
		ino = 2;
	return ino;
}

/*
 * kernel i_ino value
 */
static inline ino_t ceph_vino_to_ino(struct ceph_vino vino)
{
#if BITS_PER_LONG == 32
	return ceph_ino_to_ino32(vino.ino);
#else
	return (ino_t)vino.ino;
#endif
}

/*
 * user-visible ino (stat, filldir)
 */
#if BITS_PER_LONG == 32
static inline ino_t ceph_translate_ino(struct super_block *sb, ino_t ino)
{
	return ino;
}
#else
static inline ino_t ceph_translate_ino(struct super_block *sb, ino_t ino)
{
	if (ceph_test_mount_opt(ceph_sb_to_client(sb), INO32))
		ino = ceph_ino_to_ino32(ino);
	return ino;
}
#endif


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
	ino_t t = ceph_vino_to_ino(vino);
	return ilookup5(sb, t, ceph_ino_compare, &vino);
}


/*
 * Ceph inode.
 */
#define CEPH_I_DIR_ORDERED	1  /* dentries in dir are ordered */
#define CEPH_I_NODELAY		4  /* do not delay cap release */
#define CEPH_I_FLUSH		8  /* do not delay flush of dirty metadata */
#define CEPH_I_NOFLUSH		16 /* do not flush dirty caps */

static inline void __ceph_dir_set_complete(struct ceph_inode_info *ci,
					   int release_count, int ordered_count)
{
	atomic_set(&ci->i_complete_count, release_count);
	if (ci->i_ordered_count == ordered_count)
		ci->i_ceph_flags |= CEPH_I_DIR_ORDERED;
	else
		ci->i_ceph_flags &= ~CEPH_I_DIR_ORDERED;
}

static inline void __ceph_dir_clear_complete(struct ceph_inode_info *ci)
{
	atomic_inc(&ci->i_release_count);
}

static inline bool __ceph_dir_is_complete(struct ceph_inode_info *ci)
{
	return atomic_read(&ci->i_complete_count) ==
		atomic_read(&ci->i_release_count);
}

static inline bool __ceph_dir_is_complete_ordered(struct ceph_inode_info *ci)
{
	return __ceph_dir_is_complete(ci) &&
		(ci->i_ceph_flags & CEPH_I_DIR_ORDERED);
}

static inline void ceph_dir_clear_complete(struct inode *inode)
{
	__ceph_dir_clear_complete(ceph_inode(inode));
}

static inline void ceph_dir_clear_ordered(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	spin_lock(&ci->i_ceph_lock);
	ci->i_ordered_count++;
	ci->i_ceph_flags &= ~CEPH_I_DIR_ORDERED;
	spin_unlock(&ci->i_ceph_lock);
}

static inline bool ceph_dir_is_complete_ordered(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	bool ret;
	spin_lock(&ci->i_ceph_lock);
	ret = __ceph_dir_is_complete_ordered(ci);
	spin_unlock(&ci->i_ceph_lock);
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

static inline struct ceph_dentry_info *ceph_dentry(struct dentry *dentry)
{
	return (struct ceph_dentry_info *)dentry->d_fsdata;
}

static inline loff_t ceph_make_fpos(unsigned frag, unsigned off)
{
	return ((loff_t)frag << 32) | (loff_t)off;
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

static inline int ceph_caps_issued_mask(struct ceph_inode_info *ci, int mask,
					int touch)
{
	int r;
	spin_lock(&ci->i_ceph_lock);
	r = __ceph_caps_issued_mask(ci, mask, touch);
	spin_unlock(&ci->i_ceph_lock);
	return r;
}

static inline int __ceph_caps_dirty(struct ceph_inode_info *ci)
{
	return ci->i_dirty_caps | ci->i_flushing_caps;
}
extern int __ceph_mark_dirty_caps(struct ceph_inode_info *ci, int mask);

extern int __ceph_caps_revoking_other(struct ceph_inode_info *ci,
				      struct ceph_cap *ocap, int mask);
extern int ceph_caps_revoking(struct ceph_inode_info *ci, int mask);
extern int __ceph_caps_used(struct ceph_inode_info *ci);

extern int __ceph_caps_file_wanted(struct ceph_inode_info *ci);

/*
 * wanted, by virtue of open file modes AND cap refs (buffered/cached data)
 */
static inline int __ceph_caps_wanted(struct ceph_inode_info *ci)
{
	int w = __ceph_caps_file_wanted(ci) | __ceph_caps_used(ci);
	if (w & CEPH_CAP_FILE_BUFFER)
		w |= CEPH_CAP_FILE_EXCL;  /* we want EXCL if dirty data */
	return w;
}

/* what the mds thinks we want */
extern int __ceph_caps_mds_wanted(struct ceph_inode_info *ci);

extern void ceph_caps_init(struct ceph_mds_client *mdsc);
extern void ceph_caps_finalize(struct ceph_mds_client *mdsc);
extern void ceph_adjust_min_caps(struct ceph_mds_client *mdsc, int delta);
extern void ceph_reserve_caps(struct ceph_mds_client *mdsc,
			     struct ceph_cap_reservation *ctx, int need);
extern int ceph_unreserve_caps(struct ceph_mds_client *mdsc,
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

	/* readdir: position within the dir */
	u32 frag;
	struct ceph_mds_request *last_readdir;

	/* readdir: position within a frag */
	unsigned offset;       /* offset of last chunk, adjusted for . and .. */
	unsigned next_offset;  /* offset of next chunk (last_name's + 1) */
	char *last_name;       /* last entry in previous chunk */
	struct dentry *dentry; /* next dentry (for dcache readdir) */
	int dir_release_count;
	int dir_ordered_count;

	/* used for -o dirstat read() on directory thing */
	char *dir_info;
	int dir_info_len;
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
	congestion_kb = (16*int_sqrt(totalram_pages)) << (PAGE_SHIFT-10);
	if (congestion_kb > 256*1024)
		congestion_kb = 256*1024;

	return congestion_kb;
}



/* snap.c */
struct ceph_snap_realm *ceph_lookup_snap_realm(struct ceph_mds_client *mdsc,
					       u64 ino);
extern void ceph_get_snap_realm(struct ceph_mds_client *mdsc,
				struct ceph_snap_realm *realm);
extern void ceph_put_snap_realm(struct ceph_mds_client *mdsc,
				struct ceph_snap_realm *realm);
extern int ceph_update_snap_trace(struct ceph_mds_client *m,
				  void *p, void *e, bool deletion);
extern void ceph_handle_snap(struct ceph_mds_client *mdsc,
			     struct ceph_mds_session *session,
			     struct ceph_msg *msg);
extern void ceph_queue_cap_snap(struct ceph_inode_info *ci);
extern int __ceph_finish_cap_snap(struct ceph_inode_info *ci,
				  struct ceph_cap_snap *capsnap);
extern void ceph_cleanup_empty_realms(struct ceph_mds_client *mdsc);
extern int ceph_snap_init(void);
extern void ceph_snap_exit(void);

/*
 * a cap_snap is "pending" if it is still awaiting an in-progress
 * sync write (that may/may not still update size, mtime, etc.).
 */
static inline bool __ceph_have_pending_cap_snap(struct ceph_inode_info *ci)
{
	return !list_empty(&ci->i_cap_snaps) &&
		list_entry(ci->i_cap_snaps.prev, struct ceph_cap_snap,
			   ci_item)->writing;
}

/* inode.c */
extern const struct inode_operations ceph_file_iops;

extern struct inode *ceph_alloc_inode(struct super_block *sb);
extern void ceph_destroy_inode(struct inode *inode);
extern int ceph_drop_inode(struct inode *inode);

extern struct inode *ceph_get_inode(struct super_block *sb,
				    struct ceph_vino vino);
extern struct inode *ceph_get_snapdir(struct inode *parent);
extern int ceph_fill_file_size(struct inode *inode, int issued,
			       u32 truncate_seq, u64 truncate_size, u64 size);
extern void ceph_fill_file_time(struct inode *inode, int issued,
				u64 time_warp_seq, struct timespec *ctime,
				struct timespec *mtime, struct timespec *atime);
extern int ceph_fill_trace(struct super_block *sb,
			   struct ceph_mds_request *req,
			   struct ceph_mds_session *session);
extern int ceph_readdir_prepopulate(struct ceph_mds_request *req,
				    struct ceph_mds_session *session);

extern int ceph_inode_holds_cap(struct inode *inode, int mask);

extern int ceph_inode_set_size(struct inode *inode, loff_t size);
extern void __ceph_do_pending_vmtruncate(struct inode *inode);
extern void ceph_queue_vmtruncate(struct inode *inode);

extern void ceph_queue_invalidate(struct inode *inode);
extern void ceph_queue_writeback(struct inode *inode);

extern int __ceph_do_getattr(struct inode *inode, struct page *locked_page,
			     int mask, bool force);
static inline int ceph_do_getattr(struct inode *inode, int mask, bool force)
{
	return __ceph_do_getattr(inode, NULL, mask, force);
}
extern int ceph_permission(struct inode *inode, int mask);
extern int ceph_setattr(struct dentry *dentry, struct iattr *attr);
extern int ceph_getattr(struct vfsmount *mnt, struct dentry *dentry,
			struct kstat *stat);

/* xattr.c */
extern int ceph_setxattr(struct dentry *, const char *, const void *,
			 size_t, int);
int __ceph_setxattr(struct dentry *, const char *, const void *, size_t, int);
ssize_t __ceph_getxattr(struct inode *, const char *, void *, size_t);
int __ceph_removexattr(struct dentry *, const char *);
extern ssize_t ceph_getxattr(struct dentry *, const char *, void *, size_t);
extern ssize_t ceph_listxattr(struct dentry *, char *, size_t);
extern int ceph_removexattr(struct dentry *, const char *);
extern void __ceph_build_xattrs_blob(struct ceph_inode_info *ci);
extern void __ceph_destroy_xattrs(struct ceph_inode_info *ci);
extern void __init ceph_xattr_init(void);
extern void ceph_xattr_exit(void);
extern const struct xattr_handler *ceph_xattr_handlers[];

/* acl.c */
struct ceph_acls_info {
	void *default_acl;
	void *acl;
	struct ceph_pagelist *pagelist;
};

#ifdef CONFIG_CEPH_FS_POSIX_ACL

struct posix_acl *ceph_get_acl(struct inode *, int);
int ceph_set_acl(struct inode *inode, struct posix_acl *acl, int type);
int ceph_pre_init_acls(struct inode *dir, umode_t *mode,
		       struct ceph_acls_info *info);
void ceph_init_inode_acls(struct inode *inode, struct ceph_acls_info *info);
void ceph_release_acls_info(struct ceph_acls_info *info);

static inline void ceph_forget_all_cached_acls(struct inode *inode)
{
       forget_all_cached_acls(inode);
}

#else

#define ceph_get_acl NULL
#define ceph_set_acl NULL

static inline int ceph_pre_init_acls(struct inode *dir, umode_t *mode,
				     struct ceph_acls_info *info)
{
	return 0;
}
static inline void ceph_init_inode_acls(struct inode *inode,
					struct ceph_acls_info *info)
{
}
static inline void ceph_release_acls_info(struct ceph_acls_info *info)
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
			 int fmode, unsigned issued, unsigned wanted,
			 unsigned cap, unsigned seq, u64 realmino, int flags,
			 struct ceph_cap **new_cap);
extern void __ceph_remove_cap(struct ceph_cap *cap, bool queue_release);
extern void ceph_put_cap(struct ceph_mds_client *mdsc,
			 struct ceph_cap *cap);
extern int ceph_is_any_caps(struct inode *inode);

extern void __queue_cap_release(struct ceph_mds_session *session, u64 ino,
				u64 cap_id, u32 migrate_seq, u32 issue_seq);
extern void ceph_queue_caps_release(struct inode *inode);
extern int ceph_write_inode(struct inode *inode, struct writeback_control *wbc);
extern int ceph_fsync(struct file *file, loff_t start, loff_t end,
		      int datasync);
extern void ceph_kick_flushing_caps(struct ceph_mds_client *mdsc,
				    struct ceph_mds_session *session);
extern struct ceph_cap *ceph_get_cap_for_mds(struct ceph_inode_info *ci,
					     int mds);
extern int ceph_get_cap_mds(struct inode *inode);
extern void ceph_get_cap_refs(struct ceph_inode_info *ci, int caps);
extern void ceph_put_cap_refs(struct ceph_inode_info *ci, int had);
extern void ceph_put_wrbuffer_cap_refs(struct ceph_inode_info *ci, int nr,
				       struct ceph_snap_context *snapc);
extern void __ceph_flush_snaps(struct ceph_inode_info *ci,
			       struct ceph_mds_session **psession,
			       int again);
extern void ceph_check_caps(struct ceph_inode_info *ci, int flags,
			    struct ceph_mds_session *session);
extern void ceph_check_delayed_caps(struct ceph_mds_client *mdsc);
extern void ceph_flush_dirty_caps(struct ceph_mds_client *mdsc);

extern int ceph_encode_inode_release(void **p, struct inode *inode,
				     int mds, int drop, int unless, int force);
extern int ceph_encode_dentry_release(void **p, struct dentry *dn,
				      int mds, int drop, int unless);

extern int ceph_get_caps(struct ceph_inode_info *ci, int need, int want,
			 loff_t endoff, int *got, struct page **pinned_page);

/* for counting open files by mode */
static inline void __ceph_get_fmode(struct ceph_inode_info *ci, int mode)
{
	ci->i_nr_by_mode[mode]++;
}
extern void ceph_put_fmode(struct ceph_inode_info *ci, int mode);

/* addr.c */
extern const struct address_space_operations ceph_aops;
extern int ceph_mmap(struct file *file, struct vm_area_struct *vma);

/* file.c */
extern const struct file_operations ceph_file_fops;
extern const struct address_space_operations ceph_aops;

extern int ceph_open(struct inode *inode, struct file *file);
extern int ceph_atomic_open(struct inode *dir, struct dentry *dentry,
			    struct file *file, unsigned flags, umode_t mode,
			    int *opened);
extern int ceph_release(struct inode *inode, struct file *filp);
extern void ceph_fill_inline_data(struct inode *inode, struct page *locked_page,
				  char *data, size_t len);
int ceph_uninline_data(struct file *filp, struct page *locked_page);
/* dir.c */
extern const struct file_operations ceph_dir_fops;
extern const struct inode_operations ceph_dir_iops;
extern const struct dentry_operations ceph_dentry_ops, ceph_snap_dentry_ops,
	ceph_snapdir_dentry_ops;

extern int ceph_handle_notrace_create(struct inode *dir, struct dentry *dentry);
extern int ceph_handle_snapdir(struct ceph_mds_request *req,
			       struct dentry *dentry, int err);
extern struct dentry *ceph_finish_lookup(struct ceph_mds_request *req,
					 struct dentry *dentry, int err);

extern void ceph_dentry_lru_add(struct dentry *dn);
extern void ceph_dentry_lru_touch(struct dentry *dn);
extern void ceph_dentry_lru_del(struct dentry *dn);
extern void ceph_invalidate_dentry_lease(struct dentry *dentry);
extern unsigned ceph_dentry_hash(struct inode *dir, struct dentry *dn);
extern struct inode *ceph_get_dentry_parent_inode(struct dentry *dentry);

/*
 * our d_ops vary depending on whether the inode is live,
 * snapshotted (read-only), or a virtual ".snap" directory.
 */
int ceph_init_dentry(struct dentry *dentry);


/* ioctl.c */
extern long ceph_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

/* export.c */
extern const struct export_operations ceph_export_ops;

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
extern int lock_to_ceph_filelock(struct file_lock *fl, struct ceph_filelock *c);

/* debugfs.c */
extern int ceph_fs_debugfs_init(struct ceph_fs_client *client);
extern void ceph_fs_debugfs_cleanup(struct ceph_fs_client *client);

#endif /* _FS_CEPH_SUPER_H */
