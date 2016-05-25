/*
 * ceph_fs.h - Ceph constants and data types to share between kernel and
 * user space.
 *
 * Most types in this file are defined as little-endian, and are
 * primarily intended to describe data structures that pass over the
 * wire or that are stored on disk.
 *
 * LGPL2
 */

#ifndef CEPH_FS_H
#define CEPH_FS_H

#include <linux/ceph/msgr.h>
#include <linux/ceph/rados.h>

/*
 * subprotocol versions.  when specific messages types or high-level
 * protocols change, bump the affected components.  we keep rev
 * internal cluster protocols separately from the public,
 * client-facing protocol.
 */
#define CEPH_OSDC_PROTOCOL   24 /* server/client */
#define CEPH_MDSC_PROTOCOL   32 /* server/client */
#define CEPH_MONC_PROTOCOL   15 /* server/client */


#define CEPH_INO_ROOT   1
#define CEPH_INO_CEPH   2       /* hidden .ceph dir */
#define CEPH_INO_DOTDOT 3	/* used by ceph fuse for parent (..) */

/* arbitrary limit on max # of monitors (cluster of 3 is typical) */
#define CEPH_MAX_MON   31

/*
 * ceph_file_layout - describe data layout for a file/inode
 */
struct ceph_file_layout {
	/* file -> object mapping */
	__le32 fl_stripe_unit;     /* stripe unit, in bytes.  must be multiple
				      of page size. */
	__le32 fl_stripe_count;    /* over this many objects */
	__le32 fl_object_size;     /* until objects are this big, then move to
				      new objects */
	__le32 fl_cas_hash;        /* UNUSED.  0 = none; 1 = sha256 */

	/* pg -> disk layout */
	__le32 fl_object_stripe_unit;  /* UNUSED.  for per-object parity, if any */

	/* object -> pg layout */
	__le32 fl_unused;       /* unused; used to be preferred primary for pg (-1 for none) */
	__le32 fl_pg_pool;      /* namespace, crush ruleset, rep level */
} __attribute__ ((packed));

#define ceph_file_layout_su(l) ((__s32)le32_to_cpu((l).fl_stripe_unit))
#define ceph_file_layout_stripe_count(l) \
	((__s32)le32_to_cpu((l).fl_stripe_count))
#define ceph_file_layout_object_size(l) ((__s32)le32_to_cpu((l).fl_object_size))
#define ceph_file_layout_cas_hash(l) ((__s32)le32_to_cpu((l).fl_cas_hash))
#define ceph_file_layout_object_su(l) \
	((__s32)le32_to_cpu((l).fl_object_stripe_unit))
#define ceph_file_layout_pg_pool(l) \
	((__s32)le32_to_cpu((l).fl_pg_pool))

static inline unsigned ceph_file_layout_stripe_width(struct ceph_file_layout *l)
{
	return le32_to_cpu(l->fl_stripe_unit) *
		le32_to_cpu(l->fl_stripe_count);
}

/* "period" == bytes before i start on a new set of objects */
static inline unsigned ceph_file_layout_period(struct ceph_file_layout *l)
{
	return le32_to_cpu(l->fl_object_size) *
		le32_to_cpu(l->fl_stripe_count);
}

#define CEPH_MIN_STRIPE_UNIT 65536

int ceph_file_layout_is_valid(const struct ceph_file_layout *layout);

struct ceph_dir_layout {
	__u8   dl_dir_hash;   /* see ceph_hash.h for ids */
	__u8   dl_unused1;
	__u16  dl_unused2;
	__u32  dl_unused3;
} __attribute__ ((packed));

/* crypto algorithms */
#define CEPH_CRYPTO_NONE 0x0
#define CEPH_CRYPTO_AES  0x1

#define CEPH_AES_IV "cephsageyudagreg"

/* security/authentication protocols */
#define CEPH_AUTH_UNKNOWN	0x0
#define CEPH_AUTH_NONE	 	0x1
#define CEPH_AUTH_CEPHX	 	0x2

#define CEPH_AUTH_UID_DEFAULT ((__u64) -1)


/*********************************************
 * message layer
 */

/*
 * message types
 */

/* misc */
#define CEPH_MSG_SHUTDOWN               1
#define CEPH_MSG_PING                   2

/* client <-> monitor */
#define CEPH_MSG_MON_MAP                4
#define CEPH_MSG_MON_GET_MAP            5
#define CEPH_MSG_STATFS                 13
#define CEPH_MSG_STATFS_REPLY           14
#define CEPH_MSG_MON_SUBSCRIBE          15
#define CEPH_MSG_MON_SUBSCRIBE_ACK      16
#define CEPH_MSG_AUTH			17
#define CEPH_MSG_AUTH_REPLY		18
#define CEPH_MSG_MON_GET_VERSION        19
#define CEPH_MSG_MON_GET_VERSION_REPLY  20

/* client <-> mds */
#define CEPH_MSG_MDS_MAP                21

#define CEPH_MSG_CLIENT_SESSION         22
#define CEPH_MSG_CLIENT_RECONNECT       23

#define CEPH_MSG_CLIENT_REQUEST         24
#define CEPH_MSG_CLIENT_REQUEST_FORWARD 25
#define CEPH_MSG_CLIENT_REPLY           26
#define CEPH_MSG_CLIENT_CAPS            0x310
#define CEPH_MSG_CLIENT_LEASE           0x311
#define CEPH_MSG_CLIENT_SNAP            0x312
#define CEPH_MSG_CLIENT_CAPRELEASE      0x313

/* pool ops */
#define CEPH_MSG_POOLOP_REPLY           48
#define CEPH_MSG_POOLOP                 49


/* osd */
#define CEPH_MSG_OSD_MAP                41
#define CEPH_MSG_OSD_OP                 42
#define CEPH_MSG_OSD_OPREPLY            43
#define CEPH_MSG_WATCH_NOTIFY           44


/* watch-notify operations */
enum {
	CEPH_WATCH_EVENT_NOTIFY		  = 1, /* notifying watcher */
	CEPH_WATCH_EVENT_NOTIFY_COMPLETE  = 2, /* notifier notified when done */
	CEPH_WATCH_EVENT_DISCONNECT       = 3, /* we were disconnected */
};


struct ceph_mon_request_header {
	__le64 have_version;
	__le16 session_mon;
	__le64 session_mon_tid;
} __attribute__ ((packed));

struct ceph_mon_statfs {
	struct ceph_mon_request_header monhdr;
	struct ceph_fsid fsid;
} __attribute__ ((packed));

struct ceph_statfs {
	__le64 kb, kb_used, kb_avail;
	__le64 num_objects;
} __attribute__ ((packed));

struct ceph_mon_statfs_reply {
	struct ceph_fsid fsid;
	__le64 version;
	struct ceph_statfs st;
} __attribute__ ((packed));

struct ceph_osd_getmap {
	struct ceph_mon_request_header monhdr;
	struct ceph_fsid fsid;
	__le32 start;
} __attribute__ ((packed));

struct ceph_mds_getmap {
	struct ceph_mon_request_header monhdr;
	struct ceph_fsid fsid;
} __attribute__ ((packed));

struct ceph_client_mount {
	struct ceph_mon_request_header monhdr;
} __attribute__ ((packed));

#define CEPH_SUBSCRIBE_ONETIME    1  /* i want only 1 update after have */

struct ceph_mon_subscribe_item {
	__le64 start;
	__u8 flags;
} __attribute__ ((packed));

struct ceph_mon_subscribe_ack {
	__le32 duration;         /* seconds */
	struct ceph_fsid fsid;
} __attribute__ ((packed));

#define CEPH_FS_CLUSTER_ID_NONE  -1

/*
 * mdsmap flags
 */
#define CEPH_MDSMAP_DOWN    (1<<0)  /* cluster deliberately down */

/*
 * mds states
 *   > 0 -> in
 *  <= 0 -> out
 */
#define CEPH_MDS_STATE_DNE          0  /* down, does not exist. */
#define CEPH_MDS_STATE_STOPPED     -1  /* down, once existed, but no subtrees.
					  empty log. */
#define CEPH_MDS_STATE_BOOT        -4  /* up, boot announcement. */
#define CEPH_MDS_STATE_STANDBY     -5  /* up, idle.  waiting for assignment. */
#define CEPH_MDS_STATE_CREATING    -6  /* up, creating MDS instance. */
#define CEPH_MDS_STATE_STARTING    -7  /* up, starting previously stopped mds */
#define CEPH_MDS_STATE_STANDBY_REPLAY -8 /* up, tailing active node's journal */
#define CEPH_MDS_STATE_REPLAYONCE   -9 /* up, replaying an active node's journal */

#define CEPH_MDS_STATE_REPLAY       8  /* up, replaying journal. */
#define CEPH_MDS_STATE_RESOLVE      9  /* up, disambiguating distributed
					  operations (import, rename, etc.) */
#define CEPH_MDS_STATE_RECONNECT    10 /* up, reconnect to clients */
#define CEPH_MDS_STATE_REJOIN       11 /* up, rejoining distributed cache */
#define CEPH_MDS_STATE_CLIENTREPLAY 12 /* up, replaying client operations */
#define CEPH_MDS_STATE_ACTIVE       13 /* up, active */
#define CEPH_MDS_STATE_STOPPING     14 /* up, but exporting metadata */

extern const char *ceph_mds_state_name(int s);


/*
 * metadata lock types.
 *  - these are bitmasks.. we can compose them
 *  - they also define the lock ordering by the MDS
 *  - a few of these are internal to the mds
 */
#define CEPH_LOCK_DVERSION    1
#define CEPH_LOCK_DN          2
#define CEPH_LOCK_ISNAP       16
#define CEPH_LOCK_IVERSION    32    /* mds internal */
#define CEPH_LOCK_IFILE       64
#define CEPH_LOCK_IAUTH       128
#define CEPH_LOCK_ILINK       256
#define CEPH_LOCK_IDFT        512   /* dir frag tree */
#define CEPH_LOCK_INEST       1024  /* mds internal */
#define CEPH_LOCK_IXATTR      2048
#define CEPH_LOCK_IFLOCK      4096  /* advisory file locks */
#define CEPH_LOCK_INO         8192  /* immutable inode bits; not a lock */
#define CEPH_LOCK_IPOLICY     16384 /* policy lock on dirs. MDS internal */

/* client_session ops */
enum {
	CEPH_SESSION_REQUEST_OPEN,
	CEPH_SESSION_OPEN,
	CEPH_SESSION_REQUEST_CLOSE,
	CEPH_SESSION_CLOSE,
	CEPH_SESSION_REQUEST_RENEWCAPS,
	CEPH_SESSION_RENEWCAPS,
	CEPH_SESSION_STALE,
	CEPH_SESSION_RECALL_STATE,
	CEPH_SESSION_FLUSHMSG,
	CEPH_SESSION_FLUSHMSG_ACK,
	CEPH_SESSION_FORCE_RO,
};

extern const char *ceph_session_op_name(int op);

struct ceph_mds_session_head {
	__le32 op;
	__le64 seq;
	struct ceph_timespec stamp;
	__le32 max_caps, max_leases;
} __attribute__ ((packed));

/* client_request */
/*
 * metadata ops.
 *  & 0x001000 -> write op
 *  & 0x010000 -> follow symlink (e.g. stat(), not lstat()).
 &  & 0x100000 -> use weird ino/path trace
 */
#define CEPH_MDS_OP_WRITE        0x001000
enum {
	CEPH_MDS_OP_LOOKUP     = 0x00100,
	CEPH_MDS_OP_GETATTR    = 0x00101,
	CEPH_MDS_OP_LOOKUPHASH = 0x00102,
	CEPH_MDS_OP_LOOKUPPARENT = 0x00103,
	CEPH_MDS_OP_LOOKUPINO  = 0x00104,
	CEPH_MDS_OP_LOOKUPNAME = 0x00105,

	CEPH_MDS_OP_SETXATTR   = 0x01105,
	CEPH_MDS_OP_RMXATTR    = 0x01106,
	CEPH_MDS_OP_SETLAYOUT  = 0x01107,
	CEPH_MDS_OP_SETATTR    = 0x01108,
	CEPH_MDS_OP_SETFILELOCK= 0x01109,
	CEPH_MDS_OP_GETFILELOCK= 0x00110,
	CEPH_MDS_OP_SETDIRLAYOUT=0x0110a,

	CEPH_MDS_OP_MKNOD      = 0x01201,
	CEPH_MDS_OP_LINK       = 0x01202,
	CEPH_MDS_OP_UNLINK     = 0x01203,
	CEPH_MDS_OP_RENAME     = 0x01204,
	CEPH_MDS_OP_MKDIR      = 0x01220,
	CEPH_MDS_OP_RMDIR      = 0x01221,
	CEPH_MDS_OP_SYMLINK    = 0x01222,

	CEPH_MDS_OP_CREATE     = 0x01301,
	CEPH_MDS_OP_OPEN       = 0x00302,
	CEPH_MDS_OP_READDIR    = 0x00305,

	CEPH_MDS_OP_LOOKUPSNAP = 0x00400,
	CEPH_MDS_OP_MKSNAP     = 0x01400,
	CEPH_MDS_OP_RMSNAP     = 0x01401,
	CEPH_MDS_OP_LSSNAP     = 0x00402,
	CEPH_MDS_OP_RENAMESNAP = 0x01403,
};

extern const char *ceph_mds_op_name(int op);


#define CEPH_SETATTR_MODE   1
#define CEPH_SETATTR_UID    2
#define CEPH_SETATTR_GID    4
#define CEPH_SETATTR_MTIME  8
#define CEPH_SETATTR_ATIME 16
#define CEPH_SETATTR_SIZE  32
#define CEPH_SETATTR_CTIME 64

/*
 * Ceph setxattr request flags.
 */
#define CEPH_XATTR_CREATE  (1 << 0)
#define CEPH_XATTR_REPLACE (1 << 1)
#define CEPH_XATTR_REMOVE  (1 << 31)

union ceph_mds_request_args {
	struct {
		__le32 mask;                 /* CEPH_CAP_* */
	} __attribute__ ((packed)) getattr;
	struct {
		__le32 mode;
		__le32 uid;
		__le32 gid;
		struct ceph_timespec mtime;
		struct ceph_timespec atime;
		__le64 size, old_size;       /* old_size needed by truncate */
		__le32 mask;                 /* CEPH_SETATTR_* */
	} __attribute__ ((packed)) setattr;
	struct {
		__le32 frag;                 /* which dir fragment */
		__le32 max_entries;          /* how many dentries to grab */
		__le32 max_bytes;
	} __attribute__ ((packed)) readdir;
	struct {
		__le32 mode;
		__le32 rdev;
	} __attribute__ ((packed)) mknod;
	struct {
		__le32 mode;
	} __attribute__ ((packed)) mkdir;
	struct {
		__le32 flags;
		__le32 mode;
		__le32 stripe_unit;          /* layout for newly created file */
		__le32 stripe_count;         /* ... */
		__le32 object_size;
		__le32 file_replication;
               __le32 mask;                 /* CEPH_CAP_* */
               __le32 old_size;
	} __attribute__ ((packed)) open;
	struct {
		__le32 flags;
	} __attribute__ ((packed)) setxattr;
	struct {
		struct ceph_file_layout layout;
	} __attribute__ ((packed)) setlayout;
	struct {
		__u8 rule; /* currently fcntl or flock */
		__u8 type; /* shared, exclusive, remove*/
		__le64 owner; /* owner of the lock */
		__le64 pid; /* process id requesting the lock */
		__le64 start; /* initial location to lock */
		__le64 length; /* num bytes to lock from start */
		__u8 wait; /* will caller wait for lock to become available? */
	} __attribute__ ((packed)) filelock_change;
} __attribute__ ((packed));

#define CEPH_MDS_FLAG_REPLAY        1  /* this is a replayed op */
#define CEPH_MDS_FLAG_WANT_DENTRY   2  /* want dentry in reply */

struct ceph_mds_request_head {
	__le64 oldest_client_tid;
	__le32 mdsmap_epoch;           /* on client */
	__le32 flags;                  /* CEPH_MDS_FLAG_* */
	__u8 num_retry, num_fwd;       /* count retry, fwd attempts */
	__le16 num_releases;           /* # include cap/lease release records */
	__le32 op;                     /* mds op code */
	__le32 caller_uid, caller_gid;
	__le64 ino;                    /* use this ino for openc, mkdir, mknod,
					  etc. (if replaying) */
	union ceph_mds_request_args args;
} __attribute__ ((packed));

/* cap/lease release record */
struct ceph_mds_request_release {
	__le64 ino, cap_id;            /* ino and unique cap id */
	__le32 caps, wanted;           /* new issued, wanted */
	__le32 seq, issue_seq, mseq;
	__le32 dname_seq;              /* if releasing a dentry lease, a */
	__le32 dname_len;              /* string follows. */
} __attribute__ ((packed));

/* client reply */
struct ceph_mds_reply_head {
	__le32 op;
	__le32 result;
	__le32 mdsmap_epoch;
	__u8 safe;                     /* true if committed to disk */
	__u8 is_dentry, is_target;     /* true if dentry, target inode records
					  are included with reply */
} __attribute__ ((packed));

/* one for each node split */
struct ceph_frag_tree_split {
	__le32 frag;                   /* this frag splits... */
	__le32 by;                     /* ...by this many bits */
} __attribute__ ((packed));

struct ceph_frag_tree_head {
	__le32 nsplits;                /* num ceph_frag_tree_split records */
	struct ceph_frag_tree_split splits[];
} __attribute__ ((packed));

/* capability issue, for bundling with mds reply */
struct ceph_mds_reply_cap {
	__le32 caps, wanted;           /* caps issued, wanted */
	__le64 cap_id;
	__le32 seq, mseq;
	__le64 realm;                  /* snap realm */
	__u8 flags;                    /* CEPH_CAP_FLAG_* */
} __attribute__ ((packed));

#define CEPH_CAP_FLAG_AUTH	(1 << 0)  /* cap is issued by auth mds */
#define CEPH_CAP_FLAG_RELEASE	(1 << 1)  /* release the cap */

/* inode record, for bundling with mds reply */
struct ceph_mds_reply_inode {
	__le64 ino;
	__le64 snapid;
	__le32 rdev;
	__le64 version;                /* inode version */
	__le64 xattr_version;          /* version for xattr blob */
	struct ceph_mds_reply_cap cap; /* caps issued for this inode */
	struct ceph_file_layout layout;
	struct ceph_timespec ctime, mtime, atime;
	__le32 time_warp_seq;
	__le64 size, max_size, truncate_size;
	__le32 truncate_seq;
	__le32 mode, uid, gid;
	__le32 nlink;
	__le64 files, subdirs, rbytes, rfiles, rsubdirs;  /* dir stats */
	struct ceph_timespec rctime;
	struct ceph_frag_tree_head fragtree;  /* (must be at end of struct) */
} __attribute__ ((packed));
/* followed by frag array, symlink string, dir layout, xattr blob */

/* reply_lease follows dname, and reply_inode */
struct ceph_mds_reply_lease {
	__le16 mask;            /* lease type(s) */
	__le32 duration_ms;     /* lease duration */
	__le32 seq;
} __attribute__ ((packed));

struct ceph_mds_reply_dirfrag {
	__le32 frag;            /* fragment */
	__le32 auth;            /* auth mds, if this is a delegation point */
	__le32 ndist;           /* number of mds' this is replicated on */
	__le32 dist[];
} __attribute__ ((packed));

#define CEPH_LOCK_FCNTL		1
#define CEPH_LOCK_FLOCK		2
#define CEPH_LOCK_FCNTL_INTR    3
#define CEPH_LOCK_FLOCK_INTR    4


#define CEPH_LOCK_SHARED   1
#define CEPH_LOCK_EXCL     2
#define CEPH_LOCK_UNLOCK   4

struct ceph_filelock {
	__le64 start;/* file offset to start lock at */
	__le64 length; /* num bytes to lock; 0 for all following start */
	__le64 client; /* which client holds the lock */
	__le64 owner; /* owner the lock */
	__le64 pid; /* process id holding the lock on the client */
	__u8 type; /* shared lock, exclusive lock, or unlock */
} __attribute__ ((packed));


/* file access modes */
#define CEPH_FILE_MODE_PIN        0
#define CEPH_FILE_MODE_RD         1
#define CEPH_FILE_MODE_WR         2
#define CEPH_FILE_MODE_RDWR       3  /* RD | WR */
#define CEPH_FILE_MODE_LAZY       4  /* lazy io */
#define CEPH_FILE_MODE_NUM        8  /* bc these are bit fields.. mostly */

int ceph_flags_to_mode(int flags);

#define CEPH_INLINE_NONE	((__u64)-1)

/* capability bits */
#define CEPH_CAP_PIN         1  /* no specific capabilities beyond the pin */

/* generic cap bits */
#define CEPH_CAP_GSHARED     1  /* client can reads */
#define CEPH_CAP_GEXCL       2  /* client can read and update */
#define CEPH_CAP_GCACHE      4  /* (file) client can cache reads */
#define CEPH_CAP_GRD         8  /* (file) client can read */
#define CEPH_CAP_GWR        16  /* (file) client can write */
#define CEPH_CAP_GBUFFER    32  /* (file) client can buffer writes */
#define CEPH_CAP_GWREXTEND  64  /* (file) client can extend EOF */
#define CEPH_CAP_GLAZYIO   128  /* (file) client can perform lazy io */

#define CEPH_CAP_SIMPLE_BITS  2
#define CEPH_CAP_FILE_BITS    8

/* per-lock shift */
#define CEPH_CAP_SAUTH      2
#define CEPH_CAP_SLINK      4
#define CEPH_CAP_SXATTR     6
#define CEPH_CAP_SFILE      8
#define CEPH_CAP_SFLOCK    20

#define CEPH_CAP_BITS      22

/* composed values */
#define CEPH_CAP_AUTH_SHARED  (CEPH_CAP_GSHARED  << CEPH_CAP_SAUTH)
#define CEPH_CAP_AUTH_EXCL     (CEPH_CAP_GEXCL     << CEPH_CAP_SAUTH)
#define CEPH_CAP_LINK_SHARED  (CEPH_CAP_GSHARED  << CEPH_CAP_SLINK)
#define CEPH_CAP_LINK_EXCL     (CEPH_CAP_GEXCL     << CEPH_CAP_SLINK)
#define CEPH_CAP_XATTR_SHARED (CEPH_CAP_GSHARED  << CEPH_CAP_SXATTR)
#define CEPH_CAP_XATTR_EXCL    (CEPH_CAP_GEXCL     << CEPH_CAP_SXATTR)
#define CEPH_CAP_FILE(x)    (x << CEPH_CAP_SFILE)
#define CEPH_CAP_FILE_SHARED   (CEPH_CAP_GSHARED   << CEPH_CAP_SFILE)
#define CEPH_CAP_FILE_EXCL     (CEPH_CAP_GEXCL     << CEPH_CAP_SFILE)
#define CEPH_CAP_FILE_CACHE    (CEPH_CAP_GCACHE    << CEPH_CAP_SFILE)
#define CEPH_CAP_FILE_RD       (CEPH_CAP_GRD       << CEPH_CAP_SFILE)
#define CEPH_CAP_FILE_WR       (CEPH_CAP_GWR       << CEPH_CAP_SFILE)
#define CEPH_CAP_FILE_BUFFER   (CEPH_CAP_GBUFFER   << CEPH_CAP_SFILE)
#define CEPH_CAP_FILE_WREXTEND (CEPH_CAP_GWREXTEND << CEPH_CAP_SFILE)
#define CEPH_CAP_FILE_LAZYIO   (CEPH_CAP_GLAZYIO   << CEPH_CAP_SFILE)
#define CEPH_CAP_FLOCK_SHARED  (CEPH_CAP_GSHARED   << CEPH_CAP_SFLOCK)
#define CEPH_CAP_FLOCK_EXCL    (CEPH_CAP_GEXCL     << CEPH_CAP_SFLOCK)


/* cap masks (for getattr) */
#define CEPH_STAT_CAP_INODE    CEPH_CAP_PIN
#define CEPH_STAT_CAP_TYPE     CEPH_CAP_PIN  /* mode >> 12 */
#define CEPH_STAT_CAP_SYMLINK  CEPH_CAP_PIN
#define CEPH_STAT_CAP_UID      CEPH_CAP_AUTH_SHARED
#define CEPH_STAT_CAP_GID      CEPH_CAP_AUTH_SHARED
#define CEPH_STAT_CAP_MODE     CEPH_CAP_AUTH_SHARED
#define CEPH_STAT_CAP_NLINK    CEPH_CAP_LINK_SHARED
#define CEPH_STAT_CAP_LAYOUT   CEPH_CAP_FILE_SHARED
#define CEPH_STAT_CAP_MTIME    CEPH_CAP_FILE_SHARED
#define CEPH_STAT_CAP_SIZE     CEPH_CAP_FILE_SHARED
#define CEPH_STAT_CAP_ATIME    CEPH_CAP_FILE_SHARED  /* fixme */
#define CEPH_STAT_CAP_XATTR    CEPH_CAP_XATTR_SHARED
#define CEPH_STAT_CAP_INODE_ALL (CEPH_CAP_PIN |			\
				 CEPH_CAP_AUTH_SHARED |	\
				 CEPH_CAP_LINK_SHARED |	\
				 CEPH_CAP_FILE_SHARED |	\
				 CEPH_CAP_XATTR_SHARED)
#define CEPH_STAT_CAP_INLINE_DATA (CEPH_CAP_FILE_SHARED | \
				   CEPH_CAP_FILE_RD)

#define CEPH_CAP_ANY_SHARED (CEPH_CAP_AUTH_SHARED |			\
			      CEPH_CAP_LINK_SHARED |			\
			      CEPH_CAP_XATTR_SHARED |			\
			      CEPH_CAP_FILE_SHARED)
#define CEPH_CAP_ANY_RD   (CEPH_CAP_ANY_SHARED | CEPH_CAP_FILE_RD |	\
			   CEPH_CAP_FILE_CACHE)

#define CEPH_CAP_ANY_EXCL (CEPH_CAP_AUTH_EXCL |		\
			   CEPH_CAP_LINK_EXCL |		\
			   CEPH_CAP_XATTR_EXCL |	\
			   CEPH_CAP_FILE_EXCL)
#define CEPH_CAP_ANY_FILE_RD (CEPH_CAP_FILE_RD | CEPH_CAP_FILE_CACHE | \
			      CEPH_CAP_FILE_SHARED)
#define CEPH_CAP_ANY_FILE_WR (CEPH_CAP_FILE_WR | CEPH_CAP_FILE_BUFFER |	\
			      CEPH_CAP_FILE_EXCL)
#define CEPH_CAP_ANY_WR   (CEPH_CAP_ANY_EXCL | CEPH_CAP_ANY_FILE_WR)
#define CEPH_CAP_ANY      (CEPH_CAP_ANY_RD | CEPH_CAP_ANY_EXCL | \
			   CEPH_CAP_ANY_FILE_WR | CEPH_CAP_FILE_LAZYIO | \
			   CEPH_CAP_PIN)

#define CEPH_CAP_LOCKS (CEPH_LOCK_IFILE | CEPH_LOCK_IAUTH | CEPH_LOCK_ILINK | \
			CEPH_LOCK_IXATTR)

int ceph_caps_for_mode(int mode);

enum {
	CEPH_CAP_OP_GRANT,         /* mds->client grant */
	CEPH_CAP_OP_REVOKE,        /* mds->client revoke */
	CEPH_CAP_OP_TRUNC,         /* mds->client trunc notify */
	CEPH_CAP_OP_EXPORT,        /* mds has exported the cap */
	CEPH_CAP_OP_IMPORT,        /* mds has imported the cap */
	CEPH_CAP_OP_UPDATE,        /* client->mds update */
	CEPH_CAP_OP_DROP,          /* client->mds drop cap bits */
	CEPH_CAP_OP_FLUSH,         /* client->mds cap writeback */
	CEPH_CAP_OP_FLUSH_ACK,     /* mds->client flushed */
	CEPH_CAP_OP_FLUSHSNAP,     /* client->mds flush snapped metadata */
	CEPH_CAP_OP_FLUSHSNAP_ACK, /* mds->client flushed snapped metadata */
	CEPH_CAP_OP_RELEASE,       /* client->mds release (clean) cap */
	CEPH_CAP_OP_RENEW,         /* client->mds renewal request */
};

extern const char *ceph_cap_op_name(int op);

/*
 * caps message, used for capability callbacks, acks, requests, etc.
 */
struct ceph_mds_caps {
	__le32 op;                  /* CEPH_CAP_OP_* */
	__le64 ino, realm;
	__le64 cap_id;
	__le32 seq, issue_seq;
	__le32 caps, wanted, dirty; /* latest issued/wanted/dirty */
	__le32 migrate_seq;
	__le64 snap_follows;
	__le32 snap_trace_len;

	/* authlock */
	__le32 uid, gid, mode;

	/* linklock */
	__le32 nlink;

	/* xattrlock */
	__le32 xattr_len;
	__le64 xattr_version;

	/* filelock */
	__le64 size, max_size, truncate_size;
	__le32 truncate_seq;
	struct ceph_timespec mtime, atime, ctime;
	struct ceph_file_layout layout;
	__le32 time_warp_seq;
} __attribute__ ((packed));

struct ceph_mds_cap_peer {
	__le64 cap_id;
	__le32 seq;
	__le32 mseq;
	__le32 mds;
	__u8   flags;
} __attribute__ ((packed));

/* cap release msg head */
struct ceph_mds_cap_release {
	__le32 num;                /* number of cap_items that follow */
} __attribute__ ((packed));

struct ceph_mds_cap_item {
	__le64 ino;
	__le64 cap_id;
	__le32 migrate_seq, seq;
} __attribute__ ((packed));

#define CEPH_MDS_LEASE_REVOKE           1  /*    mds  -> client */
#define CEPH_MDS_LEASE_RELEASE          2  /* client  -> mds    */
#define CEPH_MDS_LEASE_RENEW            3  /* client <-> mds    */
#define CEPH_MDS_LEASE_REVOKE_ACK       4  /* client  -> mds    */

extern const char *ceph_lease_op_name(int o);

/* lease msg header */
struct ceph_mds_lease {
	__u8 action;            /* CEPH_MDS_LEASE_* */
	__le16 mask;            /* which lease */
	__le64 ino;
	__le64 first, last;     /* snap range */
	__le32 seq;
	__le32 duration_ms;     /* duration of renewal */
} __attribute__ ((packed));
/* followed by a __le32+string for dname */

/* client reconnect */
struct ceph_mds_cap_reconnect {
	__le64 cap_id;
	__le32 wanted;
	__le32 issued;
	__le64 snaprealm;
	__le64 pathbase;        /* base ino for our path to this ino */
	__le32 flock_len;       /* size of flock state blob, if any */
} __attribute__ ((packed));
/* followed by flock blob */

struct ceph_mds_cap_reconnect_v1 {
	__le64 cap_id;
	__le32 wanted;
	__le32 issued;
	__le64 size;
	struct ceph_timespec mtime, atime;
	__le64 snaprealm;
	__le64 pathbase;        /* base ino for our path to this ino */
} __attribute__ ((packed));

struct ceph_mds_snaprealm_reconnect {
	__le64 ino;     /* snap realm base */
	__le64 seq;     /* snap seq for this snap realm */
	__le64 parent;  /* parent realm */
} __attribute__ ((packed));

/*
 * snaps
 */
enum {
	CEPH_SNAP_OP_UPDATE,  /* CREATE or DESTROY */
	CEPH_SNAP_OP_CREATE,
	CEPH_SNAP_OP_DESTROY,
	CEPH_SNAP_OP_SPLIT,
};

extern const char *ceph_snap_op_name(int o);

/* snap msg header */
struct ceph_mds_snap_head {
	__le32 op;                /* CEPH_SNAP_OP_* */
	__le64 split;             /* ino to split off, if any */
	__le32 num_split_inos;    /* # inos belonging to new child realm */
	__le32 num_split_realms;  /* # child realms udner new child realm */
	__le32 trace_len;         /* size of snap trace blob */
} __attribute__ ((packed));
/* followed by split ino list, then split realms, then the trace blob */

/*
 * encode info about a snaprealm, as viewed by a client
 */
struct ceph_mds_snap_realm {
	__le64 ino;           /* ino */
	__le64 created;       /* snap: when created */
	__le64 parent;        /* ino: parent realm */
	__le64 parent_since;  /* snap: same parent since */
	__le64 seq;           /* snap: version */
	__le32 num_snaps;
	__le32 num_prior_parent_snaps;
} __attribute__ ((packed));
/* followed by my snap list, then prior parent snap list */

#endif
