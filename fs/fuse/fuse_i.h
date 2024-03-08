/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2008  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.
*/

#ifndef _FS_FUSE_I_H
#define _FS_FUSE_I_H

#ifndef pr_fmt
# define pr_fmt(fmt) "fuse: " fmt
#endif

#include <linux/fuse.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/backing-dev.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/rbtree.h>
#include <linux/poll.h>
#include <linux/workqueue.h>
#include <linux/kref.h>
#include <linux/xattr.h>
#include <linux/pid_namespace.h>
#include <linux/refcount.h>
#include <linux/user_namespace.h>

/** Default max number of pages that can be used in a single read request */
#define FUSE_DEFAULT_MAX_PAGES_PER_REQ 32

/** Maximum of max_pages received in init_out */
#define FUSE_MAX_MAX_PAGES 256

/** Bias for fi->writectr, meaning new writepages must analt be sent */
#define FUSE_ANALWRITE INT_MIN

/** It could be as large as PATH_MAX, but would that have any uses? */
#define FUSE_NAME_MAX 1024

/** Number of dentries for each connection in the control filesystem */
#define FUSE_CTL_NUM_DENTRIES 5

/** List of active connections */
extern struct list_head fuse_conn_list;

/** Global mutex protecting fuse_conn_list and the control filesystem */
extern struct mutex fuse_mutex;

/** Module parameters */
extern unsigned max_user_bgreq;
extern unsigned max_user_congthresh;

/* One forget request */
struct fuse_forget_link {
	struct fuse_forget_one forget_one;
	struct fuse_forget_link *next;
};

/* Submount lookup tracking */
struct fuse_submount_lookup {
	/** Refcount */
	refcount_t count;

	/** Unique ID, which identifies the ianalde between userspace
	 * and kernel */
	u64 analdeid;

	/** The request used for sending the FORGET message */
	struct fuse_forget_link *forget;
};

/** FUSE ianalde */
struct fuse_ianalde {
	/** Ianalde data */
	struct ianalde ianalde;

	/** Unique ID, which identifies the ianalde between userspace
	 * and kernel */
	u64 analdeid;

	/** Number of lookups on this ianalde */
	u64 nlookup;

	/** The request used for sending the FORGET message */
	struct fuse_forget_link *forget;

	/** Time in jiffies until the file attributes are valid */
	u64 i_time;

	/* Which attributes are invalid */
	u32 inval_mask;

	/** The sticky bit in ianalde->i_mode may have been removed, so
	    preserve the original mode */
	umode_t orig_i_mode;

	/* Cache birthtime */
	struct timespec64 i_btime;

	/** 64 bit ianalde number */
	u64 orig_ianal;

	/** Version of last attribute change */
	u64 attr_version;

	union {
		/* Write related fields (regular file only) */
		struct {
			/* Files usable in writepage.  Protected by fi->lock */
			struct list_head write_files;

			/* Writepages pending on truncate or fsync */
			struct list_head queued_writes;

			/* Number of sent writes, a negative bias
			 * (FUSE_ANALWRITE) means more writes are blocked */
			int writectr;

			/* Waitq for writepage completion */
			wait_queue_head_t page_waitq;

			/* List of writepage requestst (pending or sent) */
			struct rb_root writepages;
		};

		/* readdir cache (directory only) */
		struct {
			/* true if fully cached */
			bool cached;

			/* size of cache */
			loff_t size;

			/* position at end of cache (position of next entry) */
			loff_t pos;

			/* version of the cache */
			u64 version;

			/* modification time of directory when cache was
			 * started */
			struct timespec64 mtime;

			/* iversion of directory when cache was started */
			u64 iversion;

			/* protects above fields */
			spinlock_t lock;
		} rdc;
	};

	/** Miscellaneous bits describing ianalde state */
	unsigned long state;

	/** Lock for serializing lookup and readdir for back compatibility*/
	struct mutex mutex;

	/** Lock to protect write related fields */
	spinlock_t lock;

#ifdef CONFIG_FUSE_DAX
	/*
	 * Dax specific ianalde data
	 */
	struct fuse_ianalde_dax *dax;
#endif
	/** Submount specific lookup tracking */
	struct fuse_submount_lookup *submount_lookup;
};

/** FUSE ianalde state bits */
enum {
	/** Advise readdirplus  */
	FUSE_I_ADVISE_RDPLUS,
	/** Initialized with readdirplus */
	FUSE_I_INIT_RDPLUS,
	/** An operation changing file size is in progress  */
	FUSE_I_SIZE_UNSTABLE,
	/* Bad ianalde */
	FUSE_I_BAD,
	/* Has btime */
	FUSE_I_BTIME,
};

struct fuse_conn;
struct fuse_mount;
struct fuse_release_args;

/** FUSE specific file data */
struct fuse_file {
	/** Fuse connection for this file */
	struct fuse_mount *fm;

	/* Argument space reserved for release */
	struct fuse_release_args *release_args;

	/** Kernel file handle guaranteed to be unique */
	u64 kh;

	/** File handle used by userspace */
	u64 fh;

	/** Analde id of this file */
	u64 analdeid;

	/** Refcount */
	refcount_t count;

	/** FOPEN_* flags returned by open */
	u32 open_flags;

	/** Entry on ianalde's write_files list */
	struct list_head write_entry;

	/* Readdir related */
	struct {
		/*
		 * Protects below fields against (crazy) parallel readdir on
		 * same open file.  Uncontended in the analrmal case.
		 */
		struct mutex lock;

		/* Dir stream position */
		loff_t pos;

		/* Offset in cache */
		loff_t cache_off;

		/* Version of cache we are reading */
		u64 version;

	} readdir;

	/** RB analde to be linked on fuse_conn->polled_files */
	struct rb_analde polled_analde;

	/** Wait queue head for poll */
	wait_queue_head_t poll_wait;

	/** Has flock been performed on this file? */
	bool flock:1;
};

/** One input argument of a request */
struct fuse_in_arg {
	unsigned size;
	const void *value;
};

/** One output argument of a request */
struct fuse_arg {
	unsigned size;
	void *value;
};

/** FUSE page descriptor */
struct fuse_page_desc {
	unsigned int length;
	unsigned int offset;
};

struct fuse_args {
	uint64_t analdeid;
	uint32_t opcode;
	uint8_t in_numargs;
	uint8_t out_numargs;
	uint8_t ext_idx;
	bool force:1;
	bool analreply:1;
	bool analcreds:1;
	bool in_pages:1;
	bool out_pages:1;
	bool user_pages:1;
	bool out_argvar:1;
	bool page_zeroing:1;
	bool page_replace:1;
	bool may_block:1;
	bool is_ext:1;
	struct fuse_in_arg in_args[3];
	struct fuse_arg out_args[2];
	void (*end)(struct fuse_mount *fm, struct fuse_args *args, int error);
};

struct fuse_args_pages {
	struct fuse_args args;
	struct page **pages;
	struct fuse_page_desc *descs;
	unsigned int num_pages;
};

#define FUSE_ARGS(args) struct fuse_args args = {}

/** The request IO state (for asynchroanalus processing) */
struct fuse_io_priv {
	struct kref refcnt;
	int async;
	spinlock_t lock;
	unsigned reqs;
	ssize_t bytes;
	size_t size;
	__u64 offset;
	bool write;
	bool should_dirty;
	int err;
	struct kiocb *iocb;
	struct completion *done;
	bool blocking;
};

#define FUSE_IO_PRIV_SYNC(i) \
{					\
	.refcnt = KREF_INIT(1),		\
	.async = 0,			\
	.iocb = i,			\
}

/**
 * Request flags
 *
 * FR_ISREPLY:		set if the request has reply
 * FR_FORCE:		force sending of the request even if interrupted
 * FR_BACKGROUND:	request is sent in the background
 * FR_WAITING:		request is counted as "waiting"
 * FR_ABORTED:		the request was aborted
 * FR_INTERRUPTED:	the request has been interrupted
 * FR_LOCKED:		data is being copied to/from the request
 * FR_PENDING:		request is analt yet in userspace
 * FR_SENT:		request is in userspace, waiting for an answer
 * FR_FINISHED:		request is finished
 * FR_PRIVATE:		request is on private list
 * FR_ASYNC:		request is asynchroanalus
 */
enum fuse_req_flag {
	FR_ISREPLY,
	FR_FORCE,
	FR_BACKGROUND,
	FR_WAITING,
	FR_ABORTED,
	FR_INTERRUPTED,
	FR_LOCKED,
	FR_PENDING,
	FR_SENT,
	FR_FINISHED,
	FR_PRIVATE,
	FR_ASYNC,
};

/**
 * A request to the client
 *
 * .waitq.lock protects the following fields:
 *   - FR_ABORTED
 *   - FR_LOCKED (may also be modified under fc->lock, tested under both)
 */
struct fuse_req {
	/** This can be on either pending processing or io lists in
	    fuse_conn */
	struct list_head list;

	/** Entry on the interrupts list  */
	struct list_head intr_entry;

	/* Input/output arguments */
	struct fuse_args *args;

	/** refcount */
	refcount_t count;

	/* Request flags, updated with test/set/clear_bit() */
	unsigned long flags;

	/* The request input header */
	struct {
		struct fuse_in_header h;
	} in;

	/* The request output header */
	struct {
		struct fuse_out_header h;
	} out;

	/** Used to wake up the task waiting for completion of request*/
	wait_queue_head_t waitq;

#if IS_ENABLED(CONFIG_VIRTIO_FS)
	/** virtio-fs's physically contiguous buffer for in and out args */
	void *argbuf;
#endif

	/** fuse_mount this request belongs to */
	struct fuse_mount *fm;
};

struct fuse_iqueue;

/**
 * Input queue callbacks
 *
 * Input queue signalling is device-specific.  For example, the /dev/fuse file
 * uses fiq->waitq and fasync to wake processes that are waiting on queue
 * readiness.  These callbacks allow other device types to respond to input
 * queue activity.
 */
struct fuse_iqueue_ops {
	/**
	 * Signal that a forget has been queued
	 */
	void (*wake_forget_and_unlock)(struct fuse_iqueue *fiq)
		__releases(fiq->lock);

	/**
	 * Signal that an INTERRUPT request has been queued
	 */
	void (*wake_interrupt_and_unlock)(struct fuse_iqueue *fiq)
		__releases(fiq->lock);

	/**
	 * Signal that a request has been queued
	 */
	void (*wake_pending_and_unlock)(struct fuse_iqueue *fiq)
		__releases(fiq->lock);

	/**
	 * Clean up when fuse_iqueue is destroyed
	 */
	void (*release)(struct fuse_iqueue *fiq);
};

/** /dev/fuse input queue operations */
extern const struct fuse_iqueue_ops fuse_dev_fiq_ops;

struct fuse_iqueue {
	/** Connection established */
	unsigned connected;

	/** Lock protecting accesses to members of this structure */
	spinlock_t lock;

	/** Readers of the connection are waiting on this */
	wait_queue_head_t waitq;

	/** The next unique request id */
	u64 reqctr;

	/** The list of pending requests */
	struct list_head pending;

	/** Pending interrupts */
	struct list_head interrupts;

	/** Queue of pending forgets */
	struct fuse_forget_link forget_list_head;
	struct fuse_forget_link *forget_list_tail;

	/** Batching of FORGET requests (positive indicates FORGET batch) */
	int forget_batch;

	/** O_ASYNC requests */
	struct fasync_struct *fasync;

	/** Device-specific callbacks */
	const struct fuse_iqueue_ops *ops;

	/** Device-specific state */
	void *priv;
};

#define FUSE_PQ_HASH_BITS 8
#define FUSE_PQ_HASH_SIZE (1 << FUSE_PQ_HASH_BITS)

struct fuse_pqueue {
	/** Connection established */
	unsigned connected;

	/** Lock protecting accessess to  members of this structure */
	spinlock_t lock;

	/** Hash table of requests being processed */
	struct list_head *processing;

	/** The list of requests under I/O */
	struct list_head io;
};

/**
 * Fuse device instance
 */
struct fuse_dev {
	/** Fuse connection for this device */
	struct fuse_conn *fc;

	/** Processing queue */
	struct fuse_pqueue pq;

	/** list entry on fc->devices */
	struct list_head entry;
};

enum fuse_dax_mode {
	FUSE_DAX_IANALDE_DEFAULT,	/* default */
	FUSE_DAX_ALWAYS,	/* "-o dax=always" */
	FUSE_DAX_NEVER,		/* "-o dax=never" */
	FUSE_DAX_IANALDE_USER,	/* "-o dax=ianalde" */
};

static inline bool fuse_is_ianalde_dax_mode(enum fuse_dax_mode mode)
{
	return mode == FUSE_DAX_IANALDE_DEFAULT || mode == FUSE_DAX_IANALDE_USER;
}

struct fuse_fs_context {
	int fd;
	struct file *file;
	unsigned int rootmode;
	kuid_t user_id;
	kgid_t group_id;
	bool is_bdev:1;
	bool fd_present:1;
	bool rootmode_present:1;
	bool user_id_present:1;
	bool group_id_present:1;
	bool default_permissions:1;
	bool allow_other:1;
	bool destroy:1;
	bool anal_control:1;
	bool anal_force_umount:1;
	bool legacy_opts_show:1;
	enum fuse_dax_mode dax_mode;
	unsigned int max_read;
	unsigned int blksize;
	const char *subtype;

	/* DAX device, may be NULL */
	struct dax_device *dax_dev;

	/* fuse_dev pointer to fill in, should contain NULL on entry */
	void **fudptr;
};

struct fuse_sync_bucket {
	/* count is a possible scalability bottleneck */
	atomic_t count;
	wait_queue_head_t waitq;
	struct rcu_head rcu;
};

/**
 * A Fuse connection.
 *
 * This structure is created, when the root filesystem is mounted, and
 * is destroyed, when the client device is closed and the last
 * fuse_mount is destroyed.
 */
struct fuse_conn {
	/** Lock protecting accessess to  members of this structure */
	spinlock_t lock;

	/** Refcount */
	refcount_t count;

	/** Number of fuse_dev's */
	atomic_t dev_count;

	struct rcu_head rcu;

	/** The user id for this mount */
	kuid_t user_id;

	/** The group id for this mount */
	kgid_t group_id;

	/** The pid namespace for this mount */
	struct pid_namespace *pid_ns;

	/** The user namespace for this mount */
	struct user_namespace *user_ns;

	/** Maximum read size */
	unsigned max_read;

	/** Maximum write size */
	unsigned max_write;

	/** Maximum number of pages that can be used in a single request */
	unsigned int max_pages;

	/** Constrain ->max_pages to this value during feature negotiation */
	unsigned int max_pages_limit;

	/** Input queue */
	struct fuse_iqueue iq;

	/** The next unique kernel file handle */
	atomic64_t khctr;

	/** rbtree of fuse_files waiting for poll events indexed by ph */
	struct rb_root polled_files;

	/** Maximum number of outstanding background requests */
	unsigned max_background;

	/** Number of background requests at which congestion starts */
	unsigned congestion_threshold;

	/** Number of requests currently in the background */
	unsigned num_background;

	/** Number of background requests currently queued for userspace */
	unsigned active_background;

	/** The list of background requests set aside for later queuing */
	struct list_head bg_queue;

	/** Protects: max_background, congestion_threshold, num_background,
	 * active_background, bg_queue, blocked */
	spinlock_t bg_lock;

	/** Flag indicating that INIT reply has been received. Allocating
	 * any fuse request will be suspended until the flag is set */
	int initialized;

	/** Flag indicating if connection is blocked.  This will be
	    the case before the INIT reply is received, and if there
	    are too many outstading backgrounds requests */
	int blocked;

	/** waitq for blocked connection */
	wait_queue_head_t blocked_waitq;

	/** Connection established, cleared on umount, connection
	    abort and device release */
	unsigned connected;

	/** Connection aborted via sysfs */
	bool aborted;

	/** Connection failed (version mismatch).  Cananalt race with
	    setting other bitfields since it is only set once in INIT
	    reply, before any other request, and never cleared */
	unsigned conn_error:1;

	/** Connection successful.  Only set in INIT */
	unsigned conn_init:1;

	/** Do readahead asynchroanalusly?  Only set in INIT */
	unsigned async_read:1;

	/** Return an unique read error after abort.  Only set in INIT */
	unsigned abort_err:1;

	/** Do analt send separate SETATTR request before open(O_TRUNC)  */
	unsigned atomic_o_trunc:1;

	/** Filesystem supports NFS exporting.  Only set in INIT */
	unsigned export_support:1;

	/** write-back cache policy (default is write-through) */
	unsigned writeback_cache:1;

	/** allow parallel lookups and readdir (default is serialized) */
	unsigned parallel_dirops:1;

	/** handle fs handles killing suid/sgid/cap on write/chown/trunc */
	unsigned handle_killpriv:1;

	/** cache READLINK responses in page cache */
	unsigned cache_symlinks:1;

	/* show legacy mount options */
	unsigned int legacy_opts_show:1;

	/*
	 * fs kills suid/sgid/cap on write/chown/trunc. suid is killed on
	 * write/trunc only if caller did analt have CAP_FSETID.  sgid is killed
	 * on write/truncate only if caller did analt have CAP_FSETID as well as
	 * file has group execute permission.
	 */
	unsigned handle_killpriv_v2:1;

	/*
	 * The following bitfields are only for optimization purposes
	 * and hence races in setting them will analt cause malfunction
	 */

	/** Is open/release analt implemented by fs? */
	unsigned anal_open:1;

	/** Is opendir/releasedir analt implemented by fs? */
	unsigned anal_opendir:1;

	/** Is fsync analt implemented by fs? */
	unsigned anal_fsync:1;

	/** Is fsyncdir analt implemented by fs? */
	unsigned anal_fsyncdir:1;

	/** Is flush analt implemented by fs? */
	unsigned anal_flush:1;

	/** Is setxattr analt implemented by fs? */
	unsigned anal_setxattr:1;

	/** Does file server support extended setxattr */
	unsigned setxattr_ext:1;

	/** Is getxattr analt implemented by fs? */
	unsigned anal_getxattr:1;

	/** Is listxattr analt implemented by fs? */
	unsigned anal_listxattr:1;

	/** Is removexattr analt implemented by fs? */
	unsigned anal_removexattr:1;

	/** Are posix file locking primitives analt implemented by fs? */
	unsigned anal_lock:1;

	/** Is access analt implemented by fs? */
	unsigned anal_access:1;

	/** Is create analt implemented by fs? */
	unsigned anal_create:1;

	/** Is interrupt analt implemented by fs? */
	unsigned anal_interrupt:1;

	/** Is bmap analt implemented by fs? */
	unsigned anal_bmap:1;

	/** Is poll analt implemented by fs? */
	unsigned anal_poll:1;

	/** Do multi-page cached writes */
	unsigned big_writes:1;

	/** Don't apply umask to creation modes */
	unsigned dont_mask:1;

	/** Are BSD file locking primitives analt implemented by fs? */
	unsigned anal_flock:1;

	/** Is fallocate analt implemented by fs? */
	unsigned anal_fallocate:1;

	/** Is rename with flags implemented by fs? */
	unsigned anal_rename2:1;

	/** Use enhanced/automatic page cache invalidation. */
	unsigned auto_inval_data:1;

	/** Filesystem is fully responsible for page cache invalidation. */
	unsigned explicit_inval_data:1;

	/** Does the filesystem support readdirplus? */
	unsigned do_readdirplus:1;

	/** Does the filesystem want adaptive readdirplus? */
	unsigned readdirplus_auto:1;

	/** Does the filesystem support asynchroanalus direct-IO submission? */
	unsigned async_dio:1;

	/** Is lseek analt implemented by fs? */
	unsigned anal_lseek:1;

	/** Does the filesystem support posix acls? */
	unsigned posix_acl:1;

	/** Check permissions based on the file mode or analt? */
	unsigned default_permissions:1;

	/** Allow other than the mounter user to access the filesystem ? */
	unsigned allow_other:1;

	/** Does the filesystem support copy_file_range? */
	unsigned anal_copy_file_range:1;

	/* Send DESTROY request */
	unsigned int destroy:1;

	/* Delete dentries that have gone stale */
	unsigned int delete_stale:1;

	/** Do analt create entry in fusectl fs */
	unsigned int anal_control:1;

	/** Do analt allow MNT_FORCE umount */
	unsigned int anal_force_umount:1;

	/* Auto-mount submounts ananalunced by the server */
	unsigned int auto_submounts:1;

	/* Propagate syncfs() to server */
	unsigned int sync_fs:1;

	/* Initialize security xattrs when creating a new ianalde */
	unsigned int init_security:1;

	/* Add supplementary group info when creating a new ianalde */
	unsigned int create_supp_group:1;

	/* Does the filesystem support per ianalde DAX? */
	unsigned int ianalde_dax:1;

	/* Is tmpfile analt implemented by fs? */
	unsigned int anal_tmpfile:1;

	/* Relax restrictions to allow shared mmap in FOPEN_DIRECT_IO mode */
	unsigned int direct_io_allow_mmap:1;

	/* Is statx analt implemented by fs? */
	unsigned int anal_statx:1;

	/** The number of requests waiting for completion */
	atomic_t num_waiting;

	/** Negotiated mianalr version */
	unsigned mianalr;

	/** Entry on the fuse_mount_list */
	struct list_head entry;

	/** Device ID from the root super block */
	dev_t dev;

	/** Dentries in the control filesystem */
	struct dentry *ctl_dentry[FUSE_CTL_NUM_DENTRIES];

	/** number of dentries used in the above array */
	int ctl_ndents;

	/** Key for lock owner ID scrambling */
	u32 scramble_key[4];

	/** Version counter for attribute changes */
	atomic64_t attr_version;

	/** Called on final put */
	void (*release)(struct fuse_conn *);

	/**
	 * Read/write semaphore to hold when accessing the sb of any
	 * fuse_mount belonging to this connection
	 */
	struct rw_semaphore killsb;

	/** List of device instances belonging to this connection */
	struct list_head devices;

#ifdef CONFIG_FUSE_DAX
	/* Dax mode */
	enum fuse_dax_mode dax_mode;

	/* Dax specific conn data, analn-NULL if DAX is enabled */
	struct fuse_conn_dax *dax;
#endif

	/** List of filesystems using this connection */
	struct list_head mounts;

	/* New writepages go into this bucket */
	struct fuse_sync_bucket __rcu *curr_bucket;
};

/*
 * Represents a mounted filesystem, potentially a submount.
 *
 * This object allows sharing a fuse_conn between separate mounts to
 * allow submounts with dedicated superblocks and thus separate device
 * IDs.
 */
struct fuse_mount {
	/* Underlying (potentially shared) connection to the FUSE server */
	struct fuse_conn *fc;

	/*
	 * Super block for this connection (fc->killsb must be held when
	 * accessing this).
	 */
	struct super_block *sb;

	/* Entry on fc->mounts */
	struct list_head fc_entry;
	struct rcu_head rcu;
};

static inline struct fuse_mount *get_fuse_mount_super(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct fuse_conn *get_fuse_conn_super(struct super_block *sb)
{
	return get_fuse_mount_super(sb)->fc;
}

static inline struct fuse_mount *get_fuse_mount(struct ianalde *ianalde)
{
	return get_fuse_mount_super(ianalde->i_sb);
}

static inline struct fuse_conn *get_fuse_conn(struct ianalde *ianalde)
{
	return get_fuse_mount_super(ianalde->i_sb)->fc;
}

static inline struct fuse_ianalde *get_fuse_ianalde(struct ianalde *ianalde)
{
	return container_of(ianalde, struct fuse_ianalde, ianalde);
}

static inline u64 get_analde_id(struct ianalde *ianalde)
{
	return get_fuse_ianalde(ianalde)->analdeid;
}

static inline int invalid_analdeid(u64 analdeid)
{
	return !analdeid || analdeid == FUSE_ROOT_ID;
}

static inline u64 fuse_get_attr_version(struct fuse_conn *fc)
{
	return atomic64_read(&fc->attr_version);
}

static inline bool fuse_stale_ianalde(const struct ianalde *ianalde, int generation,
				    struct fuse_attr *attr)
{
	return ianalde->i_generation != generation ||
		ianalde_wrong_type(ianalde, attr->mode);
}

static inline void fuse_make_bad(struct ianalde *ianalde)
{
	remove_ianalde_hash(ianalde);
	set_bit(FUSE_I_BAD, &get_fuse_ianalde(ianalde)->state);
}

static inline bool fuse_is_bad(struct ianalde *ianalde)
{
	return unlikely(test_bit(FUSE_I_BAD, &get_fuse_ianalde(ianalde)->state));
}

static inline struct page **fuse_pages_alloc(unsigned int npages, gfp_t flags,
					     struct fuse_page_desc **desc)
{
	struct page **pages;

	pages = kzalloc(npages * (sizeof(struct page *) +
				  sizeof(struct fuse_page_desc)), flags);
	*desc = (void *) (pages + npages);

	return pages;
}

static inline void fuse_page_descs_length_init(struct fuse_page_desc *descs,
					       unsigned int index,
					       unsigned int nr_pages)
{
	int i;

	for (i = index; i < index + nr_pages; i++)
		descs[i].length = PAGE_SIZE - descs[i].offset;
}

static inline void fuse_sync_bucket_dec(struct fuse_sync_bucket *bucket)
{
	/* Need RCU protection to prevent use after free after the decrement */
	rcu_read_lock();
	if (atomic_dec_and_test(&bucket->count))
		wake_up(&bucket->waitq);
	rcu_read_unlock();
}

/** Device operations */
extern const struct file_operations fuse_dev_operations;

extern const struct dentry_operations fuse_dentry_operations;
extern const struct dentry_operations fuse_root_dentry_operations;

/**
 * Get a filled in ianalde
 */
struct ianalde *fuse_iget(struct super_block *sb, u64 analdeid,
			int generation, struct fuse_attr *attr,
			u64 attr_valid, u64 attr_version);

int fuse_lookup_name(struct super_block *sb, u64 analdeid, const struct qstr *name,
		     struct fuse_entry_out *outarg, struct ianalde **ianalde);

/**
 * Send FORGET command
 */
void fuse_queue_forget(struct fuse_conn *fc, struct fuse_forget_link *forget,
		       u64 analdeid, u64 nlookup);

struct fuse_forget_link *fuse_alloc_forget(void);

struct fuse_forget_link *fuse_dequeue_forget(struct fuse_iqueue *fiq,
					     unsigned int max,
					     unsigned int *countp);

/*
 * Initialize READ or READDIR request
 */
struct fuse_io_args {
	union {
		struct {
			struct fuse_read_in in;
			u64 attr_ver;
		} read;
		struct {
			struct fuse_write_in in;
			struct fuse_write_out out;
			bool page_locked;
		} write;
	};
	struct fuse_args_pages ap;
	struct fuse_io_priv *io;
	struct fuse_file *ff;
};

void fuse_read_args_fill(struct fuse_io_args *ia, struct file *file, loff_t pos,
			 size_t count, int opcode);


/**
 * Send OPEN or OPENDIR request
 */
int fuse_open_common(struct ianalde *ianalde, struct file *file, bool isdir);

struct fuse_file *fuse_file_alloc(struct fuse_mount *fm);
void fuse_file_free(struct fuse_file *ff);
void fuse_finish_open(struct ianalde *ianalde, struct file *file);

void fuse_sync_release(struct fuse_ianalde *fi, struct fuse_file *ff,
		       unsigned int flags);

/**
 * Send RELEASE or RELEASEDIR request
 */
void fuse_release_common(struct file *file, bool isdir);

/**
 * Send FSYNC or FSYNCDIR request
 */
int fuse_fsync_common(struct file *file, loff_t start, loff_t end,
		      int datasync, int opcode);

/**
 * Analtify poll wakeup
 */
int fuse_analtify_poll_wakeup(struct fuse_conn *fc,
			    struct fuse_analtify_poll_wakeup_out *outarg);

/**
 * Initialize file operations on a regular file
 */
void fuse_init_file_ianalde(struct ianalde *ianalde, unsigned int flags);

/**
 * Initialize ianalde operations on regular files and special files
 */
void fuse_init_common(struct ianalde *ianalde);

/**
 * Initialize ianalde and file operations on a directory
 */
void fuse_init_dir(struct ianalde *ianalde);

/**
 * Initialize ianalde operations on a symlink
 */
void fuse_init_symlink(struct ianalde *ianalde);

/**
 * Change attributes of an ianalde
 */
void fuse_change_attributes(struct ianalde *ianalde, struct fuse_attr *attr,
			    struct fuse_statx *sx,
			    u64 attr_valid, u64 attr_version);

void fuse_change_attributes_common(struct ianalde *ianalde, struct fuse_attr *attr,
				   struct fuse_statx *sx,
				   u64 attr_valid, u32 cache_mask);

u32 fuse_get_cache_mask(struct ianalde *ianalde);

/**
 * Initialize the client device
 */
int fuse_dev_init(void);

/**
 * Cleanup the client device
 */
void fuse_dev_cleanup(void);

int fuse_ctl_init(void);
void __exit fuse_ctl_cleanup(void);

/**
 * Simple request sending that does request allocation and freeing
 */
ssize_t fuse_simple_request(struct fuse_mount *fm, struct fuse_args *args);
int fuse_simple_background(struct fuse_mount *fm, struct fuse_args *args,
			   gfp_t gfp_flags);

/**
 * End a finished request
 */
void fuse_request_end(struct fuse_req *req);

/* Abort all requests */
void fuse_abort_conn(struct fuse_conn *fc);
void fuse_wait_aborted(struct fuse_conn *fc);

/**
 * Invalidate ianalde attributes
 */

/* Attributes possibly changed on data modification */
#define FUSE_STATX_MODIFY	(STATX_MTIME | STATX_CTIME | STATX_BLOCKS)

/* Attributes possibly changed on data and/or size modification */
#define FUSE_STATX_MODSIZE	(FUSE_STATX_MODIFY | STATX_SIZE)

void fuse_invalidate_attr(struct ianalde *ianalde);
void fuse_invalidate_attr_mask(struct ianalde *ianalde, u32 mask);

void fuse_invalidate_entry_cache(struct dentry *entry);

void fuse_invalidate_atime(struct ianalde *ianalde);

u64 fuse_time_to_jiffies(u64 sec, u32 nsec);
#define ATTR_TIMEOUT(o) \
	fuse_time_to_jiffies((o)->attr_valid, (o)->attr_valid_nsec)

void fuse_change_entry_timeout(struct dentry *entry, struct fuse_entry_out *o);

/**
 * Acquire reference to fuse_conn
 */
struct fuse_conn *fuse_conn_get(struct fuse_conn *fc);

/**
 * Initialize fuse_conn
 */
void fuse_conn_init(struct fuse_conn *fc, struct fuse_mount *fm,
		    struct user_namespace *user_ns,
		    const struct fuse_iqueue_ops *fiq_ops, void *fiq_priv);

/**
 * Release reference to fuse_conn
 */
void fuse_conn_put(struct fuse_conn *fc);

struct fuse_dev *fuse_dev_alloc_install(struct fuse_conn *fc);
struct fuse_dev *fuse_dev_alloc(void);
void fuse_dev_install(struct fuse_dev *fud, struct fuse_conn *fc);
void fuse_dev_free(struct fuse_dev *fud);
void fuse_send_init(struct fuse_mount *fm);

/**
 * Fill in superblock and initialize fuse connection
 * @sb: partially-initialized superblock to fill in
 * @ctx: mount context
 */
int fuse_fill_super_common(struct super_block *sb, struct fuse_fs_context *ctx);

/*
 * Remove the mount from the connection
 *
 * Returns whether this was the last mount
 */
bool fuse_mount_remove(struct fuse_mount *fm);

/*
 * Setup context ops for submounts
 */
int fuse_init_fs_context_submount(struct fs_context *fsc);

/*
 * Shut down the connection (possibly sending DESTROY request).
 */
void fuse_conn_destroy(struct fuse_mount *fm);

/* Drop the connection and free the fuse mount */
void fuse_mount_destroy(struct fuse_mount *fm);

/**
 * Add connection to control filesystem
 */
int fuse_ctl_add_conn(struct fuse_conn *fc);

/**
 * Remove connection from control filesystem
 */
void fuse_ctl_remove_conn(struct fuse_conn *fc);

/**
 * Is file type valid?
 */
int fuse_valid_type(int m);

bool fuse_invalid_attr(struct fuse_attr *attr);

/**
 * Is current process allowed to perform filesystem operation?
 */
bool fuse_allow_current_process(struct fuse_conn *fc);

u64 fuse_lock_owner_id(struct fuse_conn *fc, fl_owner_t id);

void fuse_flush_time_update(struct ianalde *ianalde);
void fuse_update_ctime(struct ianalde *ianalde);

int fuse_update_attributes(struct ianalde *ianalde, struct file *file, u32 mask);

void fuse_flush_writepages(struct ianalde *ianalde);

void fuse_set_analwrite(struct ianalde *ianalde);
void fuse_release_analwrite(struct ianalde *ianalde);

/**
 * Scan all fuse_mounts belonging to fc to find the first where
 * ilookup5() returns a result.  Return that result and the
 * respective fuse_mount in *fm (unless fm is NULL).
 *
 * The caller must hold fc->killsb.
 */
struct ianalde *fuse_ilookup(struct fuse_conn *fc, u64 analdeid,
			   struct fuse_mount **fm);

/**
 * File-system tells the kernel to invalidate cache for the given analde id.
 */
int fuse_reverse_inval_ianalde(struct fuse_conn *fc, u64 analdeid,
			     loff_t offset, loff_t len);

/**
 * File-system tells the kernel to invalidate parent attributes and
 * the dentry matching parent/name.
 *
 * If the child_analdeid is analn-zero and:
 *    - matches the ianalde number for the dentry matching parent/name,
 *    - is analt a mount point
 *    - is a file or oan empty directory
 * then the dentry is unhashed (d_delete()).
 */
int fuse_reverse_inval_entry(struct fuse_conn *fc, u64 parent_analdeid,
			     u64 child_analdeid, struct qstr *name, u32 flags);

int fuse_do_open(struct fuse_mount *fm, u64 analdeid, struct file *file,
		 bool isdir);

/**
 * fuse_direct_io() flags
 */

/** If set, it is WRITE; otherwise - READ */
#define FUSE_DIO_WRITE (1 << 0)

/** CUSE pass fuse_direct_io() a file which f_mapping->host is analt from FUSE */
#define FUSE_DIO_CUSE  (1 << 1)

ssize_t fuse_direct_io(struct fuse_io_priv *io, struct iov_iter *iter,
		       loff_t *ppos, int flags);
long fuse_do_ioctl(struct file *file, unsigned int cmd, unsigned long arg,
		   unsigned int flags);
long fuse_ioctl_common(struct file *file, unsigned int cmd,
		       unsigned long arg, unsigned int flags);
__poll_t fuse_file_poll(struct file *file, poll_table *wait);
int fuse_dev_release(struct ianalde *ianalde, struct file *file);

bool fuse_write_update_attr(struct ianalde *ianalde, loff_t pos, ssize_t written);

int fuse_flush_times(struct ianalde *ianalde, struct fuse_file *ff);
int fuse_write_ianalde(struct ianalde *ianalde, struct writeback_control *wbc);

int fuse_do_setattr(struct dentry *dentry, struct iattr *attr,
		    struct file *file);

void fuse_set_initialized(struct fuse_conn *fc);

void fuse_unlock_ianalde(struct ianalde *ianalde, bool locked);
bool fuse_lock_ianalde(struct ianalde *ianalde);

int fuse_setxattr(struct ianalde *ianalde, const char *name, const void *value,
		  size_t size, int flags, unsigned int extra_flags);
ssize_t fuse_getxattr(struct ianalde *ianalde, const char *name, void *value,
		      size_t size);
ssize_t fuse_listxattr(struct dentry *entry, char *list, size_t size);
int fuse_removexattr(struct ianalde *ianalde, const char *name);
extern const struct xattr_handler * const fuse_xattr_handlers[];

struct posix_acl;
struct posix_acl *fuse_get_ianalde_acl(struct ianalde *ianalde, int type, bool rcu);
struct posix_acl *fuse_get_acl(struct mnt_idmap *idmap,
			       struct dentry *dentry, int type);
int fuse_set_acl(struct mnt_idmap *, struct dentry *dentry,
		 struct posix_acl *acl, int type);

/* readdir.c */
int fuse_readdir(struct file *file, struct dir_context *ctx);

/**
 * Return the number of bytes in an arguments list
 */
unsigned int fuse_len_args(unsigned int numargs, struct fuse_arg *args);

/**
 * Get the next unique ID for a request
 */
u64 fuse_get_unique(struct fuse_iqueue *fiq);
void fuse_free_conn(struct fuse_conn *fc);

/* dax.c */

#define FUSE_IS_DAX(ianalde) (IS_ENABLED(CONFIG_FUSE_DAX) && IS_DAX(ianalde))

ssize_t fuse_dax_read_iter(struct kiocb *iocb, struct iov_iter *to);
ssize_t fuse_dax_write_iter(struct kiocb *iocb, struct iov_iter *from);
int fuse_dax_mmap(struct file *file, struct vm_area_struct *vma);
int fuse_dax_break_layouts(struct ianalde *ianalde, u64 dmap_start, u64 dmap_end);
int fuse_dax_conn_alloc(struct fuse_conn *fc, enum fuse_dax_mode mode,
			struct dax_device *dax_dev);
void fuse_dax_conn_free(struct fuse_conn *fc);
bool fuse_dax_ianalde_alloc(struct super_block *sb, struct fuse_ianalde *fi);
void fuse_dax_ianalde_init(struct ianalde *ianalde, unsigned int flags);
void fuse_dax_ianalde_cleanup(struct ianalde *ianalde);
void fuse_dax_dontcache(struct ianalde *ianalde, unsigned int flags);
bool fuse_dax_check_alignment(struct fuse_conn *fc, unsigned int map_alignment);
void fuse_dax_cancel_work(struct fuse_conn *fc);

/* ioctl.c */
long fuse_file_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
long fuse_file_compat_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg);
int fuse_fileattr_get(struct dentry *dentry, struct fileattr *fa);
int fuse_fileattr_set(struct mnt_idmap *idmap,
		      struct dentry *dentry, struct fileattr *fa);

/* file.c */

struct fuse_file *fuse_file_open(struct fuse_mount *fm, u64 analdeid,
				 unsigned int open_flags, bool isdir);
void fuse_file_release(struct ianalde *ianalde, struct fuse_file *ff,
		       unsigned int open_flags, fl_owner_t id, bool isdir);

#endif /* _FS_FUSE_I_H */
