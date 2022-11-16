/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FS_CEPH_MDS_CLIENT_H
#define _FS_CEPH_MDS_CLIENT_H

#include <linux/completion.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/spinlock.h>
#include <linux/refcount.h>
#include <linux/utsname.h>
#include <linux/ktime.h>

#include <linux/ceph/types.h>
#include <linux/ceph/messenger.h>
#include <linux/ceph/mdsmap.h>
#include <linux/ceph/auth.h>

#include "metric.h"
#include "super.h"

/* The first 8 bits are reserved for old ceph releases */
enum ceph_feature_type {
	CEPHFS_FEATURE_MIMIC = 8,
	CEPHFS_FEATURE_REPLY_ENCODING,
	CEPHFS_FEATURE_RECLAIM_CLIENT,
	CEPHFS_FEATURE_LAZY_CAP_WANTED,
	CEPHFS_FEATURE_MULTI_RECONNECT,
	CEPHFS_FEATURE_DELEG_INO,
	CEPHFS_FEATURE_METRIC_COLLECT,
	CEPHFS_FEATURE_ALTERNATE_NAME,
	CEPHFS_FEATURE_NOTIFY_SESSION_STATE,
	CEPHFS_FEATURE_OP_GETVXATTR,

	CEPHFS_FEATURE_MAX = CEPHFS_FEATURE_OP_GETVXATTR,
};

#define CEPHFS_FEATURES_CLIENT_SUPPORTED {	\
	0, 1, 2, 3, 4, 5, 6, 7,			\
	CEPHFS_FEATURE_MIMIC,			\
	CEPHFS_FEATURE_REPLY_ENCODING,		\
	CEPHFS_FEATURE_LAZY_CAP_WANTED,		\
	CEPHFS_FEATURE_MULTI_RECONNECT,		\
	CEPHFS_FEATURE_DELEG_INO,		\
	CEPHFS_FEATURE_METRIC_COLLECT,		\
	CEPHFS_FEATURE_NOTIFY_SESSION_STATE,	\
	CEPHFS_FEATURE_OP_GETVXATTR,		\
}

/*
 * Some lock dependencies:
 *
 * session->s_mutex
 *         mdsc->mutex
 *
 *         mdsc->snap_rwsem
 *
 *         ci->i_ceph_lock
 *                 mdsc->snap_flush_lock
 *                 mdsc->cap_delay_lock
 *
 */

struct ceph_fs_client;
struct ceph_cap;

/*
 * parsed info about a single inode.  pointers are into the encoded
 * on-wire structures within the mds reply message payload.
 */
struct ceph_mds_reply_info_in {
	struct ceph_mds_reply_inode *in;
	struct ceph_dir_layout dir_layout;
	u32 symlink_len;
	char *symlink;
	u32 xattr_len;
	char *xattr_data;
	u64 inline_version;
	u32 inline_len;
	char *inline_data;
	u32 pool_ns_len;
	char *pool_ns_data;
	u64 max_bytes;
	u64 max_files;
	s32 dir_pin;
	struct ceph_timespec btime;
	struct ceph_timespec snap_btime;
	u64 rsnaps;
	u64 change_attr;
};

struct ceph_mds_reply_dir_entry {
	char                          *name;
	u32                           name_len;
	struct ceph_mds_reply_lease   *lease;
	struct ceph_mds_reply_info_in inode;
	loff_t			      offset;
};

struct ceph_mds_reply_xattr {
	char *xattr_value;
	size_t xattr_value_len;
};

/*
 * parsed info about an mds reply, including information about
 * either: 1) the target inode and/or its parent directory and dentry,
 * and directory contents (for readdir results), or
 * 2) the file range lock info (for fcntl F_GETLK results).
 */
struct ceph_mds_reply_info_parsed {
	struct ceph_mds_reply_head    *head;

	/* trace */
	struct ceph_mds_reply_info_in diri, targeti;
	struct ceph_mds_reply_dirfrag *dirfrag;
	char                          *dname;
	u32                           dname_len;
	struct ceph_mds_reply_lease   *dlease;
	struct ceph_mds_reply_xattr   xattr_info;

	/* extra */
	union {
		/* for fcntl F_GETLK results */
		struct ceph_filelock *filelock_reply;

		/* for readdir results */
		struct {
			struct ceph_mds_reply_dirfrag *dir_dir;
			size_t			      dir_buf_size;
			int                           dir_nr;
			bool			      dir_end;
			bool			      dir_complete;
			bool			      hash_order;
			bool			      offset_hash;
			struct ceph_mds_reply_dir_entry  *dir_entries;
		};

		/* for create results */
		struct {
			bool has_create_ino;
			u64 ino;
		};
	};

	/* encoded blob describing snapshot contexts for certain
	   operations (e.g., open) */
	void *snapblob;
	int snapblob_len;
};


/*
 * cap releases are batched and sent to the MDS en masse.
 *
 * Account for per-message overhead of mds_cap_release header
 * and __le32 for osd epoch barrier trailing field.
 */
#define CEPH_CAPS_PER_RELEASE ((PAGE_SIZE - sizeof(u32) -		\
				sizeof(struct ceph_mds_cap_release)) /	\
			        sizeof(struct ceph_mds_cap_item))


/*
 * state associated with each MDS<->client session
 */
enum {
	CEPH_MDS_SESSION_NEW = 1,
	CEPH_MDS_SESSION_OPENING = 2,
	CEPH_MDS_SESSION_OPEN = 3,
	CEPH_MDS_SESSION_HUNG = 4,
	CEPH_MDS_SESSION_RESTARTING = 5,
	CEPH_MDS_SESSION_RECONNECTING = 6,
	CEPH_MDS_SESSION_CLOSING = 7,
	CEPH_MDS_SESSION_CLOSED = 8,
	CEPH_MDS_SESSION_REJECTED = 9,
};

struct ceph_mds_session {
	struct ceph_mds_client *s_mdsc;
	int               s_mds;
	int               s_state;
	unsigned long     s_ttl;      /* time until mds kills us */
	unsigned long	  s_features;
	u64               s_seq;      /* incoming msg seq # */
	struct mutex      s_mutex;    /* serialize session messages */

	struct ceph_connection s_con;

	struct ceph_auth_handshake s_auth;

	atomic_t          s_cap_gen;  /* inc each time we get mds stale msg */
	unsigned long     s_cap_ttl;  /* when session caps expire. protected by s_mutex */

	/* protected by s_cap_lock */
	spinlock_t        s_cap_lock;
	refcount_t        s_ref;
	struct list_head  s_caps;     /* all caps issued by this session */
	struct ceph_cap  *s_cap_iterator;
	int               s_nr_caps;
	int               s_num_cap_releases;
	int		  s_cap_reconnect;
	int		  s_readonly;
	struct list_head  s_cap_releases; /* waiting cap_release messages */
	struct work_struct s_cap_release_work;

	/* See ceph_inode_info->i_dirty_item. */
	struct list_head  s_cap_dirty;	      /* inodes w/ dirty caps */

	/* See ceph_inode_info->i_flushing_item. */
	struct list_head  s_cap_flushing;     /* inodes w/ flushing caps */

	unsigned long     s_renew_requested; /* last time we sent a renew req */
	u64               s_renew_seq;

	struct list_head  s_waiting;  /* waiting requests */
	struct list_head  s_unsafe;   /* unsafe requests */
	struct xarray	  s_delegated_inos;
};

/*
 * modes of choosing which MDS to send a request to
 */
enum {
	USE_ANY_MDS,
	USE_RANDOM_MDS,
	USE_AUTH_MDS,   /* prefer authoritative mds for this metadata item */
};

struct ceph_mds_request;
struct ceph_mds_client;

/*
 * request completion callback
 */
typedef void (*ceph_mds_request_callback_t) (struct ceph_mds_client *mdsc,
					     struct ceph_mds_request *req);
/*
 * wait for request completion callback
 */
typedef int (*ceph_mds_request_wait_callback_t) (struct ceph_mds_client *mdsc,
						 struct ceph_mds_request *req);

/*
 * an in-flight mds request
 */
struct ceph_mds_request {
	u64 r_tid;                   /* transaction id */
	struct rb_node r_node;
	struct ceph_mds_client *r_mdsc;

	struct kref       r_kref;
	int r_op;                    /* mds op code */

	/* operation on what? */
	struct inode *r_inode;              /* arg1 */
	struct dentry *r_dentry;            /* arg1 */
	struct dentry *r_old_dentry;        /* arg2: rename from or link from */
	struct inode *r_old_dentry_dir;     /* arg2: old dentry's parent dir */
	char *r_path1, *r_path2;
	struct ceph_vino r_ino1, r_ino2;

	struct inode *r_parent;		    /* parent dir inode */
	struct inode *r_target_inode;       /* resulting inode */

#define CEPH_MDS_R_DIRECT_IS_HASH	(1) /* r_direct_hash is valid */
#define CEPH_MDS_R_ABORTED		(2) /* call was aborted */
#define CEPH_MDS_R_GOT_UNSAFE		(3) /* got an unsafe reply */
#define CEPH_MDS_R_GOT_SAFE		(4) /* got a safe reply */
#define CEPH_MDS_R_GOT_RESULT		(5) /* got a result */
#define CEPH_MDS_R_DID_PREPOPULATE	(6) /* prepopulated readdir */
#define CEPH_MDS_R_PARENT_LOCKED	(7) /* is r_parent->i_rwsem wlocked? */
#define CEPH_MDS_R_ASYNC		(8) /* async request */
	unsigned long	r_req_flags;

	struct mutex r_fill_mutex;

	union ceph_mds_request_args r_args;
	int r_fmode;        /* file mode, if expecting cap */
	int r_request_release_offset;
	const struct cred *r_cred;
	struct timespec64 r_stamp;

	/* for choosing which mds to send this request to */
	int r_direct_mode;
	u32 r_direct_hash;      /* choose dir frag based on this dentry hash */

	/* data payload is used for xattr ops */
	struct ceph_pagelist *r_pagelist;

	/* what caps shall we drop? */
	int r_inode_drop, r_inode_unless;
	int r_dentry_drop, r_dentry_unless;
	int r_old_dentry_drop, r_old_dentry_unless;
	struct inode *r_old_inode;
	int r_old_inode_drop, r_old_inode_unless;

	struct ceph_msg  *r_request;  /* original request */
	struct ceph_msg  *r_reply;
	struct ceph_mds_reply_info_parsed r_reply_info;
	int r_err;
	u32               r_readdir_offset;

	struct page *r_locked_page;
	int r_dir_caps;
	int r_num_caps;

	unsigned long r_timeout;  /* optional.  jiffies, 0 is "wait forever" */
	unsigned long r_started;  /* start time to measure timeout against */
	unsigned long r_start_latency;  /* start time to measure latency */
	unsigned long r_end_latency;    /* finish time to measure latency */
	unsigned long r_request_started; /* start time for mds request only,
					    used to measure lease durations */

	/* link unsafe requests to parent directory, for fsync */
	struct inode	*r_unsafe_dir;
	struct list_head r_unsafe_dir_item;

	/* unsafe requests that modify the target inode */
	struct list_head r_unsafe_target_item;

	struct ceph_mds_session *r_session;

	int               r_attempts;   /* resend attempts */
	int               r_num_fwd;    /* number of forward attempts */
	int               r_resend_mds; /* mds to resend to next, if any*/
	u32               r_sent_on_mseq; /* cap mseq request was sent at*/
	u64		  r_deleg_ino;

	struct list_head  r_wait;
	struct completion r_completion;
	struct completion r_safe_completion;
	ceph_mds_request_callback_t r_callback;
	struct list_head  r_unsafe_item;  /* per-session unsafe list item */

	long long	  r_dir_release_cnt;
	long long	  r_dir_ordered_cnt;
	int		  r_readdir_cache_idx;

	int		  r_feature_needed;

	struct ceph_cap_reservation r_caps_reservation;
};

struct ceph_pool_perm {
	struct rb_node node;
	int perm;
	s64 pool;
	size_t pool_ns_len;
	char pool_ns[];
};

struct ceph_snapid_map {
	struct rb_node node;
	struct list_head lru;
	atomic_t ref;
	u64 snap;
	dev_t dev;
	unsigned long last_used;
};

/*
 * node for list of quotarealm inodes that are not visible from the filesystem
 * mountpoint, but required to handle, e.g. quotas.
 */
struct ceph_quotarealm_inode {
	struct rb_node node;
	u64 ino;
	unsigned long timeout; /* last time a lookup failed for this inode */
	struct mutex mutex;
	struct inode *inode;
};

struct cap_wait {
	struct list_head	list;
	u64			ino;
	pid_t			tgid;
	int			need;
	int			want;
};

/*
 * mds client state
 */
struct ceph_mds_client {
	struct ceph_fs_client  *fsc;
	struct mutex            mutex;         /* all nested structures */

	struct ceph_mdsmap      *mdsmap;
	struct completion       safe_umount_waiters;
	wait_queue_head_t       session_close_wq;
	struct list_head        waiting_for_map;
	int 			mdsmap_err;

	struct ceph_mds_session **sessions;    /* NULL for mds if no session */
	atomic_t		num_sessions;
	int                     max_sessions;  /* len of sessions array */
	int                     stopping;      /* true if shutting down */

	atomic64_t		quotarealms_count; /* # realms with quota */
	/*
	 * We keep a list of inodes we don't see in the mountpoint but that we
	 * need to track quota realms.
	 */
	struct rb_root		quotarealms_inodes;
	struct mutex		quotarealms_inodes_mutex;

	/*
	 * snap_rwsem will cover cap linkage into snaprealms, and
	 * realm snap contexts.  (later, we can do per-realm snap
	 * contexts locks..)  the empty list contains realms with no
	 * references (implying they contain no inodes with caps) that
	 * should be destroyed.
	 */
	u64			last_snap_seq;
	struct rw_semaphore     snap_rwsem;
	struct rb_root          snap_realms;
	struct list_head        snap_empty;
	int			num_snap_realms;
	spinlock_t              snap_empty_lock;  /* protect snap_empty */

	u64                    last_tid;      /* most recent mds request */
	u64                    oldest_tid;    /* oldest incomplete mds request,
						 excluding setfilelock requests */
	struct rb_root         request_tree;  /* pending mds requests */
	struct delayed_work    delayed_work;  /* delayed work */
	unsigned long    last_renew_caps;  /* last time we renewed our caps */
	struct list_head cap_delay_list;   /* caps with delayed release */
	spinlock_t       cap_delay_lock;   /* protects cap_delay_list */
	struct list_head snap_flush_list;  /* cap_snaps ready to flush */
	spinlock_t       snap_flush_lock;

	u64               last_cap_flush_tid;
	struct list_head  cap_flush_list;
	struct list_head  cap_dirty_migrating; /* ...that are migration... */
	int               num_cap_flushing; /* # caps we are flushing */
	spinlock_t        cap_dirty_lock;   /* protects above items */
	wait_queue_head_t cap_flushing_wq;

	struct work_struct cap_reclaim_work;
	atomic_t	   cap_reclaim_pending;

	/*
	 * Cap reservations
	 *
	 * Maintain a global pool of preallocated struct ceph_caps, referenced
	 * by struct ceph_caps_reservations.  This ensures that we preallocate
	 * memory needed to successfully process an MDS response.  (If an MDS
	 * sends us cap information and we fail to process it, we will have
	 * problems due to the client and MDS being out of sync.)
	 *
	 * Reservations are 'owned' by a ceph_cap_reservation context.
	 */
	spinlock_t	caps_list_lock;
	struct		list_head caps_list; /* unused (reserved or
						unreserved) */
	struct		list_head cap_wait_list;
	int		caps_total_count;    /* total caps allocated */
	int		caps_use_count;      /* in use */
	int		caps_use_max;	     /* max used caps */
	int		caps_reserve_count;  /* unused, reserved */
	int		caps_avail_count;    /* unused, unreserved */
	int		caps_min_count;      /* keep at least this many
						(unreserved) */
	spinlock_t	  dentry_list_lock;
	struct list_head  dentry_leases;     /* fifo list */
	struct list_head  dentry_dir_leases; /* lru list */

	struct ceph_client_metric metric;

	spinlock_t		snapid_map_lock;
	struct rb_root		snapid_map_tree;
	struct list_head	snapid_map_lru;

	struct rw_semaphore     pool_perm_rwsem;
	struct rb_root		pool_perm_tree;

	char nodename[__NEW_UTS_LEN + 1];
};

extern const char *ceph_mds_op_name(int op);

extern bool check_session_state(struct ceph_mds_session *s);
void inc_session_sequence(struct ceph_mds_session *s);

extern struct ceph_mds_session *
__ceph_lookup_mds_session(struct ceph_mds_client *, int mds);

extern const char *ceph_session_state_name(int s);

extern struct ceph_mds_session *
ceph_get_mds_session(struct ceph_mds_session *s);
extern void ceph_put_mds_session(struct ceph_mds_session *s);

extern int ceph_send_msg_mds(struct ceph_mds_client *mdsc,
			     struct ceph_msg *msg, int mds);

extern int ceph_mdsc_init(struct ceph_fs_client *fsc);
extern void ceph_mdsc_close_sessions(struct ceph_mds_client *mdsc);
extern void ceph_mdsc_force_umount(struct ceph_mds_client *mdsc);
extern void ceph_mdsc_destroy(struct ceph_fs_client *fsc);

extern void ceph_mdsc_sync(struct ceph_mds_client *mdsc);

extern void ceph_invalidate_dir_request(struct ceph_mds_request *req);
extern int ceph_alloc_readdir_reply_buffer(struct ceph_mds_request *req,
					   struct inode *dir);
extern struct ceph_mds_request *
ceph_mdsc_create_request(struct ceph_mds_client *mdsc, int op, int mode);
extern int ceph_mdsc_submit_request(struct ceph_mds_client *mdsc,
				    struct inode *dir,
				    struct ceph_mds_request *req);
int ceph_mdsc_wait_request(struct ceph_mds_client *mdsc,
			struct ceph_mds_request *req,
			ceph_mds_request_wait_callback_t wait_func);
extern int ceph_mdsc_do_request(struct ceph_mds_client *mdsc,
				struct inode *dir,
				struct ceph_mds_request *req);
extern void ceph_mdsc_release_dir_caps(struct ceph_mds_request *req);
extern void ceph_mdsc_release_dir_caps_no_check(struct ceph_mds_request *req);
static inline void ceph_mdsc_get_request(struct ceph_mds_request *req)
{
	kref_get(&req->r_kref);
}
extern void ceph_mdsc_release_request(struct kref *kref);
static inline void ceph_mdsc_put_request(struct ceph_mds_request *req)
{
	kref_put(&req->r_kref, ceph_mdsc_release_request);
}

extern void send_flush_mdlog(struct ceph_mds_session *s);
extern void ceph_mdsc_iterate_sessions(struct ceph_mds_client *mdsc,
				       void (*cb)(struct ceph_mds_session *),
				       bool check_state);
extern struct ceph_msg *ceph_create_session_msg(u32 op, u64 seq);
extern void __ceph_queue_cap_release(struct ceph_mds_session *session,
				    struct ceph_cap *cap);
extern void ceph_flush_cap_releases(struct ceph_mds_client *mdsc,
				    struct ceph_mds_session *session);
extern void ceph_queue_cap_reclaim_work(struct ceph_mds_client *mdsc);
extern void ceph_reclaim_caps_nr(struct ceph_mds_client *mdsc, int nr);
extern int ceph_iterate_session_caps(struct ceph_mds_session *session,
				     int (*cb)(struct inode *,
					       struct ceph_cap *, void *),
				     void *arg);
extern void ceph_mdsc_pre_umount(struct ceph_mds_client *mdsc);

static inline void ceph_mdsc_free_path(char *path, int len)
{
	if (!IS_ERR_OR_NULL(path))
		__putname(path - (PATH_MAX - 1 - len));
}

extern char *ceph_mdsc_build_path(struct dentry *dentry, int *plen, u64 *base,
				  int stop_on_nosnap);

extern void __ceph_mdsc_drop_dentry_lease(struct dentry *dentry);
extern void ceph_mdsc_lease_send_msg(struct ceph_mds_session *session,
				     struct dentry *dentry, char action,
				     u32 seq);

extern void ceph_mdsc_handle_mdsmap(struct ceph_mds_client *mdsc,
				    struct ceph_msg *msg);
extern void ceph_mdsc_handle_fsmap(struct ceph_mds_client *mdsc,
				   struct ceph_msg *msg);

extern struct ceph_mds_session *
ceph_mdsc_open_export_target_session(struct ceph_mds_client *mdsc, int target);
extern void ceph_mdsc_open_export_target_sessions(struct ceph_mds_client *mdsc,
					  struct ceph_mds_session *session);

extern int ceph_trim_caps(struct ceph_mds_client *mdsc,
			  struct ceph_mds_session *session,
			  int max_caps);

static inline int ceph_wait_on_async_create(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);

	return wait_on_bit(&ci->i_ceph_flags, CEPH_ASYNC_CREATE_BIT,
			   TASK_KILLABLE);
}

extern int ceph_wait_on_conflict_unlink(struct dentry *dentry);
extern u64 ceph_get_deleg_ino(struct ceph_mds_session *session);
extern int ceph_restore_deleg_ino(struct ceph_mds_session *session, u64 ino);
#endif
