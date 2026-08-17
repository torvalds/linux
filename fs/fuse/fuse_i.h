/* SPDX-License-Identifier: GPL-2.0 */
/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2008  Miklos Szeredi <miklos@szeredi.hu>
*/

#ifndef _FS_FUSE_I_H
#define _FS_FUSE_I_H

#ifndef pr_fmt
# define pr_fmt(fmt) "fuse: " fmt
#endif

#include "args.h"
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

/** Bias for fi->writectr, meaning new writepages must not be sent */
#define FUSE_NOWRITE INT_MIN

/** Maximum length of a filename, not including terminating null */

/* maximum, small enough for FUSE_MIN_READ_BUFFER*/
#define FUSE_NAME_LOW_MAX 1024
/* maximum, but needs a request buffer > FUSE_MIN_READ_BUFFER */
#define FUSE_NAME_MAX (PATH_MAX - 1)

/** Number of dentries for each connection in the control filesystem */
#define FUSE_CTL_NUM_DENTRIES 5

/*
 * Dentries invalidation workqueue period, in seconds.  The value of this
 * parameter shall be >= FUSE_DENTRY_INVAL_FREQ_MIN seconds, or 0 (zero), in
 * which case no workqueue will be created.
 */
extern unsigned inval_wq __read_mostly;

/** Maximum of max_pages received in init_out */
extern unsigned int fuse_max_pages_limit;

/** List of active connections */
extern struct list_head fuse_conn_list;

/** Global mutex protecting fuse_conn_list and the control filesystem */
extern struct mutex fuse_mutex;

/** Module parameters */
extern unsigned int max_user_bgreq;
extern unsigned int max_user_congthresh;

struct fuse_forget_link;

/**
 * struct fuse_submount_lookup - Submount lookup tracking
 */
struct fuse_submount_lookup {
	/** @count: Refcount */
	refcount_t count;

	/**
	 * @nodeid: Unique ID, which identifies the inode between userspace
	 * and kernel
	 */
	u64 nodeid;

	/** @forget: The request used for sending the FORGET message */
	struct fuse_forget_link *forget;
};

/* Container for data related to mapping to backing file */
struct fuse_backing {
	struct file *file;
	const struct cred *cred;

	/* refcount */
	refcount_t count;
	struct rcu_head rcu;
};

/**
 * struct fuse_inode - FUSE inode
 */
struct fuse_inode {
	/** @inode: Inode data */
	struct inode inode;

	/**
	 * @nodeid: Unique ID, which identifies the inode between userspace
	 * and kernel
	 */
	u64 nodeid;

	/** @nlookup: Number of lookups on this inode */
	u64 nlookup;

	/** @forget: The request used for sending the FORGET message */
	struct fuse_forget_link *forget;

	/** @i_time: Time in jiffies until the file attributes are valid */
	u64 i_time;

	/** @inval_mask: Which attributes are invalid */
	u32 inval_mask;

	/**
	 * @orig_i_mode: The sticky bit in inode->i_mode may have been removed,
	 * so preserve the original mode
	 */
	umode_t orig_i_mode;

	/** @i_btime: Cache birthtime */
	struct timespec64 i_btime;

	/** @orig_ino: 64-bit inode number */
	u64 orig_ino;

	/** @attr_version: Version of last attribute change */
	u64 attr_version;

	union {
		/* read/write io cache (regular file only) */
		struct {
			/**
			 * @write_files: Files usable in writepage.
			 * Protected by fi->lock
			 */
			struct list_head write_files;

			/**
			 * @queued_writes: Writepages pending on truncate or
			 * fsync
			 */
			struct list_head queued_writes;

			/**
			 * @writectr: Number of sent writes, a negative bias
			 * (FUSE_NOWRITE) means more writes are blocked
			 */
			int writectr;

			/** @iocachectr: Number of files/maps using page cache */
			int iocachectr;

			/** @page_waitq: Waitq for writepage completion */
			wait_queue_head_t page_waitq;

			/** @direct_io_waitq: waitq for direct-io completion */
			wait_queue_head_t direct_io_waitq;
		};

		/** @rdc: readdir cache (directory only) */
		struct {
			/** @cached: true if fully cached */
			bool cached;

			/** @size: size of cache */
			loff_t size;

			/**
			 * @pos: position at end of cache (position of next
			 * entry)
			 */
			loff_t pos;

			/** @version: version of the cache */
			u64 version;

			/**
			 * @mtime: modification time of directory when cache was
			 * started
			 */
			struct timespec64 mtime;

			/**
			 * @epoch: epoch of fc when cache was started
			 */
			int epoch;

			/**
			 * @iversion: iversion of directory when cache was
			 * started
			 */
			u64 iversion;

			/** @lock: protects above fields */
			spinlock_t lock;
		} rdc;
	};

	/** @state: Miscellaneous bits describing inode state */
	unsigned long state;

	/**
	 * @mutex: Lock for serializing lookup and readdir for back
	 * compatibility
	 */
	struct mutex mutex;

	/** @lock: Lock to protect write-related fields */
	spinlock_t lock;

#ifdef CONFIG_FUSE_DAX
	/**
	 * @dax: Dax specific inode data
	 */
	struct fuse_inode_dax *dax;
#endif
	/** @submount_lookup: Submount specific lookup tracking */
	struct fuse_submount_lookup *submount_lookup;
#ifdef CONFIG_FUSE_PASSTHROUGH
	/** @fb: Reference to backing file in passthrough mode */
	struct fuse_backing *fb;
#endif

	/**
	 * @cached_i_blkbits: The underlying inode->i_blkbits value will not
	 * be modified, so preserve the blocksize specified by the server.
	 */
	u8 cached_i_blkbits;
};

/** FUSE inode state bits */
enum {
	/** Advise readdirplus  */
	FUSE_I_ADVISE_RDPLUS,
	/** Initialized with readdirplus */
	FUSE_I_INIT_RDPLUS,
	/** An operation changing file size is in progress  */
	FUSE_I_SIZE_UNSTABLE,
	/* Bad inode */
	FUSE_I_BAD,
	/* Has btime */
	FUSE_I_BTIME,
	/* Wants or already has page cache IO */
	FUSE_I_CACHE_IO_MODE,
	/*
	 * Client has exclusive access to the inode, either because fs is local
	 * or the fuse server has an exclusive "lease" on distributed fs
	 */
	FUSE_I_EXCLUSIVE,
};

struct fuse_conn;
struct fuse_mount;
union fuse_file_args;

/**
 * struct fuse_file - FUSE-specific file data
 */
struct fuse_file {
	/** @fm: Fuse connection for this file */
	struct fuse_mount *fm;

	/** @args: Argument space reserved for open/release */
	union fuse_file_args *args;

	/** @kh: Kernel file handle guaranteed to be unique */
	u64 kh;

	/** @fh: File handle used by userspace */
	u64 fh;

	/** @nodeid: Node id of this file */
	u64 nodeid;

	/** @count: Refcount */
	refcount_t count;

	/** @open_flags: FOPEN_* flags returned by open */
	u32 open_flags;

	/** @write_entry: Entry on inode's write_files list */
	struct list_head write_entry;

	/** @readdir: Readdir-related */
	struct {
		/** @pos: Dir stream position */
		loff_t pos;

		/** @cache_off: Offset in cache */
		loff_t cache_off;

		/** @version: Version of cache we are reading */
		u64 version;

	} readdir;

	/** @polled_node: RB node to be linked on fuse_conn->polled_files */
	struct rb_node polled_node;

	/** @poll_wait: Wait queue head for poll */
	wait_queue_head_t poll_wait;

	/** @iomode: Does file hold a fi->iocachectr refcount? */
	enum { IOM_NONE, IOM_CACHED, IOM_UNCACHED } iomode;

#ifdef CONFIG_FUSE_PASSTHROUGH
	/** @passthrough: Reference to backing file in passthrough mode */
	struct file *passthrough;
	/** @cred: passthrough file credentials */
	const struct cred *cred;
#endif

	/** @flock: Has flock been performed on this file? */
	bool flock:1;
};

struct fuse_release_args {
	struct fuse_args args;
	struct fuse_release_in inarg;
	struct inode *inode;
};

union fuse_file_args {
	/* Used during open() */
	struct fuse_open_out open_outarg;
	/* Used during release() */
	struct fuse_release_args release_args;
};

#define FUSE_ARGS(args) struct fuse_args args = {}

/** The request IO state (for asynchronous processing) */
struct fuse_io_priv {
	struct kref refcnt;
	struct work_struct work;
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

enum fuse_dax_mode {
	FUSE_DAX_INODE_DEFAULT,	/* default */
	FUSE_DAX_ALWAYS,	/* "-o dax=always" */
	FUSE_DAX_NEVER,		/* "-o dax=never" */
	FUSE_DAX_INODE_USER,	/* "-o dax=inode" */
};

static inline bool fuse_is_inode_dax_mode(enum fuse_dax_mode mode)
{
	return mode == FUSE_DAX_INODE_DEFAULT || mode == FUSE_DAX_INODE_USER;
}

struct fuse_fs_context {
	struct fuse_dev *fud;
	unsigned int rootmode;
	kuid_t user_id;
	kgid_t group_id;
	bool is_bdev:1;
	bool rootmode_present:1;
	bool user_id_present:1;
	bool group_id_present:1;
	bool default_permissions:1;
	bool allow_other:1;
	bool destroy:1;
	bool no_control:1;
	bool no_force_umount:1;
	bool legacy_opts_show:1;
	enum fuse_dax_mode dax_mode;
	unsigned int max_read;
	unsigned int blksize;
	const char *subtype;

	/* DAX device, may be NULL */
	struct dax_device *dax_dev;
};

struct fuse_sync_bucket {
	/* count is a possible scalability bottleneck */
	atomic_t count;
	wait_queue_head_t waitq;
	struct rcu_head rcu;
};

/**
 * struct fuse_conn - A Fuse connection.
 *
 * This structure is created, when the root filesystem is mounted, and
 * is destroyed, when the client device is closed and the last
 * fuse_mount is destroyed.
 */
struct fuse_conn {
	/**
	 * @lock: Lock protecting:
	 * - polled_files
	 * - backing_files_map
	 * - curr_bucket
	 */
	spinlock_t lock;

	/** @count: Refcount */
	refcount_t count;

	/** @epoch: Current epoch for up-to-date dentries */
	atomic_t epoch;

	/** @epoch_work: Used to invalidate dentries from old epochs */
	struct work_struct epoch_work;

	/** @rcu: Used to delay freeing fuse_conn, making it safe */
	struct rcu_head rcu;

	/** @user_id: The user id for this mount */
	kuid_t user_id;

	/** @group_id: The group id for this mount */
	kgid_t group_id;

	/** @pid_ns: The pid namespace for this mount */
	struct pid_namespace *pid_ns;

	/** @user_ns: The user namespace for this mount */
	struct user_namespace *user_ns;

	/** @max_read: Maximum read size */
	unsigned max_read;

	/** @max_write: Maximum write size */
	unsigned max_write;

	/**
	 * @max_pages: Maximum number of pages that can be used in a
	 * single request
	 */
	unsigned int max_pages;

	/**
	 * @max_pages_limit: Constrain ->max_pages to this value during
	 * feature negotiation
	 */
	unsigned int max_pages_limit;

	/** @chan: transport layer object */
	struct fuse_chan *chan;

	/** @khctr: The next unique kernel file handle */
	atomic64_t khctr;

	/**
	 * @polled_files: rbtree of fuse_files waiting for poll events
	 * indexed by ph
	 */
	struct rb_root polled_files;

	/**
	 * @congestion_threshold: Number of background requests at which
	 * congestion starts
	 */
	unsigned congestion_threshold;

	/**
	 * @conn_error: Connection failed (version mismatch).  Cannot race with
	 * setting other bitfields since it is only set once in INIT
	 * reply, before any other request, and never cleared
	 */
	unsigned conn_error:1;

	/** @conn_init: Connection successful.  Only set in INIT */
	unsigned conn_init:1;

	/** @async_read: Do readahead asynchronously?  Only set in INIT */
	unsigned async_read:1;

	/**
	 * @abort_err: Return an unique read error after abort.
	 * Only set in INIT
	 */
	unsigned abort_err:1;

	/**
	 * @atomic_o_trunc: Do not send separate SETATTR request before
	 * open(O_TRUNC)
	 */
	unsigned atomic_o_trunc:1;

	/**
	 * @export_support: Filesystem supports NFS exporting.
	 * Only set in INIT
	 */
	unsigned export_support:1;

	/** @writeback_cache: write-back cache policy (default is write-through) */
	unsigned writeback_cache:1;

	/**
	 * @parallel_dirops: allow parallel lookups and readdir (default is
	 * serialized)
	 */
	unsigned parallel_dirops:1;

	/**
	 * @handle_killpriv: handle fs handles killing suid/sgid/cap on
	 * write/chown/trunc
	 */
	unsigned handle_killpriv:1;

	/** @cache_symlinks: cache READLINK responses in page cache */
	unsigned cache_symlinks:1;

	/** @legacy_opts_show: show legacy mount options */
	unsigned int legacy_opts_show:1;

	/**
	 * @handle_killpriv_v2:
	 * fs kills suid/sgid/cap on write/chown/trunc. suid is killed on
	 * write/trunc only if caller did not have CAP_FSETID.  sgid is killed
	 * on write/truncate only if caller did not have CAP_FSETID as well as
	 * file has group execute permission.
	 */
	unsigned handle_killpriv_v2:1;

	/*
	 * The following bitfields are only for optimization purposes
	 * and hence races in setting them will not cause malfunction
	 */

	/** @no_open: Is open/release not implemented by fs? */
	unsigned no_open:1;

	/** @no_opendir: Is opendir/releasedir not implemented by fs? */
	unsigned no_opendir:1;

	/** @no_fsync: Is fsync not implemented by fs? */
	unsigned no_fsync:1;

	/** @no_fsyncdir: Is fsyncdir not implemented by fs? */
	unsigned no_fsyncdir:1;

	/** @no_flush: Is flush not implemented by fs? */
	unsigned no_flush:1;

	/** @no_setxattr: Is setxattr not implemented by fs? */
	unsigned no_setxattr:1;

	/** @setxattr_ext: Does file server support extended setxattr */
	unsigned setxattr_ext:1;

	/** @no_getxattr: Is getxattr not implemented by fs? */
	unsigned no_getxattr:1;

	/** @no_listxattr: Is listxattr not implemented by fs? */
	unsigned no_listxattr:1;

	/** @no_removexattr: Is removexattr not implemented by fs? */
	unsigned no_removexattr:1;

	/** @no_lock: Are posix file locking primitives not implemented by fs? */
	unsigned no_lock:1;

	/** @no_access: Is access not implemented by fs? */
	unsigned no_access:1;

	/** @no_create: Is create not implemented by fs? */
	unsigned no_create:1;

	/** @no_bmap: Is bmap not implemented by fs? */
	unsigned no_bmap:1;

	/** @no_poll: Is poll not implemented by fs? */
	unsigned no_poll:1;

	/** @big_writes: Do multi-page cached writes */
	unsigned big_writes:1;

	/** @dont_mask: Don't apply umask to creation modes */
	unsigned dont_mask:1;

	/** @no_flock: Are BSD file locking primitives not implemented by fs? */
	unsigned no_flock:1;

	/** @no_fallocate: Is fallocate not implemented by fs? */
	unsigned no_fallocate:1;

	/** @no_rename2: Is rename with flags implemented by fs? */
	unsigned no_rename2:1;

	/** @auto_inval_data: Use enhanced/automatic page cache invalidation. */
	unsigned auto_inval_data:1;

	/**
	 * @explicit_inval_data: Filesystem is fully responsible for page cache
	 * invalidation.
	 */
	unsigned explicit_inval_data:1;

	/** @do_readdirplus: Does the filesystem support readdirplus? */
	unsigned do_readdirplus:1;

	/** @readdirplus_auto: Does the filesystem want adaptive readdirplus? */
	unsigned readdirplus_auto:1;

	/**
	 * @async_dio: Does the filesystem support asynchronous direct-IO
	 * submission?
	 */
	unsigned async_dio:1;

	/** @no_lseek: Is lseek not implemented by fs? */
	unsigned no_lseek:1;

	/** @posix_acl: Does the filesystem support posix acls? */
	unsigned posix_acl:1;

	/**
	 * @default_permissions: Check permissions based on the file mode
	 * or not?
	 */
	unsigned default_permissions:1;

	/**
	 * @allow_other: Allow other than the mounter user to access the
	 * filesystem ?
	 */
	unsigned allow_other:1;

	/** @no_copy_file_range: Does the filesystem support copy_file_range? */
	unsigned no_copy_file_range:1;

	/**
	 * @no_copy_file_range_64: Does the filesystem support
	 * copy_file_range_64?
	 */
	unsigned no_copy_file_range_64:1;

	/** @destroy: Send DESTROY request */
	unsigned int destroy:1;

	/** @delete_stale: Delete dentries that have gone stale */
	unsigned int delete_stale:1;

	/** @no_control: Do not create entry in fusectl fs */
	unsigned int no_control:1;

	/** @no_force_umount: Do not allow MNT_FORCE umount */
	unsigned int no_force_umount:1;

	/** @auto_submounts: Auto-mount submounts announced by the server */
	unsigned int auto_submounts:1;

	/** @sync_fs: Propagate syncfs() to server */
	unsigned int sync_fs:1;

	/** @init_security: Initialize security xattrs when creating a new inode */
	unsigned int init_security:1;

	/**
	 * @create_supp_group: Add supplementary group info when creating
	 * a new inode
	 */
	unsigned int create_supp_group:1;

	/** @inode_dax: Does the filesystem support per inode DAX? */
	unsigned int inode_dax:1;

	/** @no_tmpfile: Is tmpfile not implemented by fs? */
	unsigned int no_tmpfile:1;

	/**
	 * @direct_io_allow_mmap: Relax restrictions to allow shared mmap
	 * in FOPEN_DIRECT_IO mode
	 */
	unsigned int direct_io_allow_mmap:1;

	/** @no_statx: Is statx not implemented by fs? */
	unsigned int no_statx:1;

	/** @passthrough: Passthrough support for read/write IO */
	unsigned int passthrough:1;

	/** @use_pages_for_kvec_io: Use pages instead of pointer for kernel I/O */
	unsigned int use_pages_for_kvec_io:1;

	/** @no_link: Is link not implemented by fs? */
	unsigned int no_link:1;

	/** @sync_init: Is synchronous FUSE_INIT allowed? */
	unsigned int sync_init:1;

	/** @max_stack_depth: Maximum stack depth for passthrough backing files */
	int max_stack_depth;

	/** @minor: Negotiated minor version */
	unsigned minor;

	/** @entry: Entry on the fuse_conn_list */
	struct list_head entry;

	/** @dev: Device ID from the root super block */
	dev_t dev;

	/** @scramble_key: Key for lock owner ID scrambling */
	u32 scramble_key[4];

	/** @attr_version: Version counter for attribute changes */
	atomic64_t attr_version;

	/** @evict_ctr: Version counter for evict inode */
	atomic64_t evict_ctr;

	/** @name_max: maximum file name length */
	u32 name_max;

	/** @release: Called on final put */
	void (*release)(struct fuse_conn *);

	/**
	 * @killsb: Read/write semaphore to hold when accessing the sb of any
	 * fuse_mount belonging to this connection
	 */
	struct rw_semaphore killsb;

#ifdef CONFIG_FUSE_DAX
	/** @dax_mode: Dax mode */
	enum fuse_dax_mode dax_mode;

	/** @dax: Dax specific conn data, non-NULL if DAX is enabled */
	struct fuse_conn_dax *dax;
#endif

	/** @mounts: List of filesystems using this connection */
	struct list_head mounts;

	/** @curr_bucket: New writepages go into this bucket */
	struct fuse_sync_bucket __rcu *curr_bucket;

#ifdef CONFIG_FUSE_PASSTHROUGH
	/** @backing_files_map: IDR for backing files ids */
	struct idr backing_files_map;
#endif
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

/*
 * Empty header for FUSE opcodes without specific header needs.
 * Used as a placeholder in args->in_args[0] for consistency
 * across all FUSE operations, simplifying request handling.
 */
struct fuse_zero_header {};

static inline void fuse_set_zero_arg0(struct fuse_args *args)
{
	args->in_args[0].size = sizeof(struct fuse_zero_header);
	args->in_args[0].value = NULL;
}

static inline struct fuse_mount *get_fuse_mount_super(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct fuse_conn *get_fuse_conn_super(struct super_block *sb)
{
	return get_fuse_mount_super(sb)->fc;
}

static inline struct fuse_mount *get_fuse_mount(struct inode *inode)
{
	return get_fuse_mount_super(inode->i_sb);
}

static inline struct fuse_conn *get_fuse_conn(struct inode *inode)
{
	return get_fuse_mount_super(inode->i_sb)->fc;
}

static inline struct fuse_inode *get_fuse_inode(const struct inode *inode)
{
	return container_of(inode, struct fuse_inode, inode);
}

static inline u64 get_node_id(struct inode *inode)
{
	return get_fuse_inode(inode)->nodeid;
}

static inline int invalid_nodeid(u64 nodeid)
{
	return !nodeid || nodeid == FUSE_ROOT_ID;
}

static inline u64 fuse_get_attr_version(struct fuse_conn *fc)
{
	return atomic64_read(&fc->attr_version);
}

static inline u64 fuse_get_evict_ctr(struct fuse_conn *fc)
{
	return atomic64_read(&fc->evict_ctr);
}

static inline bool fuse_stale_inode(const struct inode *inode, int generation,
				    struct fuse_attr *attr)
{
	return inode->i_generation != generation ||
		inode_wrong_type(inode, attr->mode);
}

static inline void fuse_make_bad(struct inode *inode)
{
	set_bit(FUSE_I_BAD, &get_fuse_inode(inode)->state);
}

static inline bool fuse_is_bad(struct inode *inode)
{
	return unlikely(test_bit(FUSE_I_BAD, &get_fuse_inode(inode)->state));
}

static inline bool fuse_inode_is_exclusive(const struct inode *inode)
{
	const struct fuse_inode *fi = get_fuse_inode(inode);

	return test_bit(FUSE_I_EXCLUSIVE, &fi->state);
}

static inline struct folio **fuse_folios_alloc(unsigned int nfolios, gfp_t flags,
					       struct fuse_folio_desc **desc)
{
	struct folio **folios;

	folios = kzalloc(nfolios * (sizeof(struct folio *) +
				    sizeof(struct fuse_folio_desc)), flags);
	*desc = (void *) (folios + nfolios);

	return folios;
}

static inline void fuse_folio_descs_length_init(struct fuse_folio_desc *descs,
						unsigned int index,
						unsigned int nr_folios)
{
	int i;

	for (i = index; i < index + nr_folios; i++)
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

/*
 * Get a filled in inode
 */
struct inode *fuse_iget(struct super_block *sb, u64 nodeid,
			int generation, struct fuse_attr *attr,
			u64 attr_valid, u64 attr_version,
			u64 evict_ctr);

int fuse_lookup_name(struct super_block *sb, u64 nodeid, const struct qstr *name,
		     struct fuse_entry_out *outarg, struct inode **inode);

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
			bool folio_locked;
		} write;
	};
	struct fuse_args_pages ap;
	struct fuse_io_priv *io;
	struct fuse_file *ff;
};

void fuse_read_args_fill(struct fuse_io_args *ia, struct file *file, loff_t pos,
			 size_t count, int opcode);


struct fuse_file *fuse_file_alloc(struct fuse_mount *fm, bool release);
void fuse_file_free(struct fuse_file *ff);
int fuse_finish_open(struct inode *inode, struct file *file);

void fuse_sync_release(struct fuse_inode *fi, struct fuse_file *ff,
		       unsigned int flags);

/*
 * Send RELEASE or RELEASEDIR request
 */
void fuse_release_common(struct file *file, bool isdir);

/*
 * Send FSYNC or FSYNCDIR request
 */
int fuse_fsync_common(struct file *file, loff_t start, loff_t end,
		      int datasync, int opcode);

/*
 * Notify poll wakeup
 */
int fuse_notify_poll_wakeup(struct fuse_conn *fc,
			    struct fuse_notify_poll_wakeup_out *outarg);

/*
 * Initialize file operations on a regular file
 */
void fuse_init_file_inode(struct inode *inode, unsigned int flags);

/*
 * Initialize inode operations on regular files and special files
 */
void fuse_init_common(struct inode *inode);

/*
 * Initialize inode and file operations on a directory
 */
void fuse_init_dir(struct inode *inode);

/*
 * Initialize inode operations on a symlink
 */
void fuse_init_symlink(struct inode *inode);

/*
 * Change attributes of an inode
 */
void fuse_change_attributes(struct inode *inode, struct fuse_attr *attr,
			    struct fuse_statx *sx,
			    u64 attr_valid, u64 attr_version);

void fuse_change_attributes_common(struct inode *inode, struct fuse_attr *attr,
				   struct fuse_statx *sx,
				   u64 attr_valid, u32 cache_mask,
				   u64 evict_ctr);

u32 fuse_get_cache_mask(struct inode *inode);

int fuse_ctl_init(void);
void __exit fuse_ctl_cleanup(void);

/*
 * Simple request sending that does request allocation and freeing
 */
ssize_t __fuse_simple_request(struct mnt_idmap *idmap,
			      struct fuse_mount *fm,
			      struct fuse_args *args);

static inline ssize_t fuse_simple_request(struct fuse_mount *fm, struct fuse_args *args)
{
	return __fuse_simple_request(&invalid_mnt_idmap, fm, args);
}

static inline ssize_t fuse_simple_idmap_request(struct mnt_idmap *idmap,
						struct fuse_mount *fm,
						struct fuse_args *args)
{
	return __fuse_simple_request(idmap, fm, args);
}

int fuse_simple_background(struct fuse_mount *fm, struct fuse_args *args,
			   gfp_t gfp_flags);
int fuse_simple_notify_reply(struct fuse_mount *fm, struct fuse_args *args, u64 unique);

void fuse_dentry_tree_init(void);
void fuse_dentry_tree_cleanup(void);

void fuse_epoch_work(struct work_struct *work);

/*
 * Invalidate inode attributes
 */

/* Attributes possibly changed on data modification */
#define FUSE_STATX_MODIFY	(STATX_MTIME | STATX_CTIME | STATX_BLOCKS)

/* Attributes possibly changed on data and/or size modification */
#define FUSE_STATX_MODSIZE	(FUSE_STATX_MODIFY | STATX_SIZE)

/* Attributes possibly changed on directory modification */
#define FUSE_STATX_MODDIR	(FUSE_STATX_MODSIZE | STATX_NLINK)

void fuse_invalidate_attr(struct inode *inode);
void fuse_invalidate_attr_mask(struct inode *inode, u32 mask);

void fuse_invalidate_entry_cache(struct dentry *entry);

void fuse_invalidate_atime(struct inode *inode);

u64 fuse_time_to_jiffies(u64 sec, u32 nsec);
#define ATTR_TIMEOUT(o) \
	fuse_time_to_jiffies((o)->attr_valid, (o)->attr_valid_nsec)

void fuse_change_entry_timeout(struct dentry *entry, struct fuse_entry_out *o);

/*
 * Initialize fuse_conn
 */
void fuse_conn_init(struct fuse_conn *fc, struct fuse_mount *fm,
		    struct user_namespace *user_ns, struct fuse_chan *fch);

int fuse_send_init(struct fuse_mount *fm);

/**
 * fuse_fill_super_common - Fill in superblock and initialize fuse connection
 * @sb: partially-initialized superblock to fill in
 * @ctx: mount context
 */
int fuse_fill_super_common(struct super_block *sb, struct fuse_fs_context *ctx);

/**
 * fuse_mount_remove - Remove the mount from the connection
 * @fm: fuse_mount to remove
 *
 * Returns: whether this was the last mount
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
 * fuse_ctl_add_conn - Add connection to control filesystem
 * @fc: Fuse connection to add
 */
int fuse_ctl_add_conn(struct fuse_conn *fc);

/**
 * fuse_ctl_remove_conn - Remove connection from control filesystem
 * @fc: Fuse connection to remove
 */
void fuse_ctl_remove_conn(struct fuse_conn *fc);

/*
 * Is file type valid?
 */
int fuse_valid_type(int m);

bool fuse_invalid_attr(struct fuse_attr *attr);

/*
 * Is current process allowed to perform filesystem operation?
 */
bool fuse_allow_current_process(struct fuse_conn *fc);

u64 fuse_lock_owner_id(struct fuse_conn *fc, fl_owner_t id);

void fuse_flush_time_update(struct inode *inode);
void fuse_update_ctime(struct inode *inode);

int fuse_update_attributes(struct inode *inode, struct file *file, u32 mask);

void fuse_flush_writepages(struct inode *inode);

void fuse_set_nowrite(struct inode *inode);
void fuse_release_nowrite(struct inode *inode);

/*
 * Scan all fuse_mounts belonging to fc to find the first where
 * ilookup5() returns a result.  Return that result and the
 * respective fuse_mount in *fm (unless fm is NULL).
 *
 * The caller must hold fc->killsb.
 */
struct inode *fuse_ilookup(struct fuse_conn *fc, u64 nodeid,
			   struct fuse_mount **fm);

/*
 * File-system tells the kernel to invalidate cache for the given node id.
 */
int fuse_reverse_inval_inode(struct fuse_conn *fc, u64 nodeid,
			     loff_t offset, loff_t len);

/*
 * File-system tells the kernel to invalidate parent attributes and
 * the dentry matching parent/name.
 *
 * If the child_nodeid is non-zero and:
 *    - matches the inode number for the dentry matching parent/name,
 *    - is not a mount point
 *    - is a file or oan empty directory
 * then the dentry is unhashed (d_delete()).
 */
int fuse_reverse_inval_entry(struct fuse_conn *fc, u64 parent_nodeid,
			     u64 child_nodeid, struct qstr *name, u32 flags);

/*
 * Try to prune this inode.  If neither the inode itself nor dentries associated
 * with this inode have any external reference, then the inode can be freed.
 */
void fuse_try_prune_one_inode(struct fuse_conn *fc, u64 nodeid);

int fuse_do_open(struct fuse_mount *fm, u64 nodeid, struct file *file,
		 bool isdir);

/*
 * fuse_direct_io() flags
 */

/** If set, it is WRITE; otherwise - READ */
#define FUSE_DIO_WRITE (1 << 0)

/** CUSE pass fuse_direct_io() a file which f_mapping->host is not from FUSE */
#define FUSE_DIO_CUSE  (1 << 1)

ssize_t fuse_direct_io(struct fuse_io_priv *io, struct iov_iter *iter,
		       loff_t *ppos, int flags);
long fuse_do_ioctl(struct file *file, unsigned int cmd, unsigned long arg,
		   unsigned int flags);
long fuse_ioctl_common(struct file *file, unsigned int cmd,
		       unsigned long arg, unsigned int flags);
__poll_t fuse_file_poll(struct file *file, poll_table *wait);

bool fuse_write_update_attr(struct inode *inode, loff_t pos, ssize_t written);

int fuse_flush_times(struct inode *inode, struct fuse_file *ff);
int fuse_write_inode(struct inode *inode, struct writeback_control *wbc);

int fuse_do_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		    struct iattr *attr, struct file *file);

void fuse_unlock_inode(struct inode *inode, bool locked);
bool fuse_lock_inode(struct inode *inode);

int fuse_setxattr(struct inode *inode, const char *name, const void *value,
		  size_t size, int flags, unsigned int extra_flags);
ssize_t fuse_getxattr(struct inode *inode, const char *name, void *value,
		      size_t size);
ssize_t fuse_listxattr(struct dentry *entry, char *list, size_t size);
int fuse_removexattr(struct inode *inode, const char *name);
extern const struct xattr_handler * const fuse_xattr_handlers[];

struct posix_acl;
struct posix_acl *fuse_get_inode_acl(struct inode *inode, int type, bool rcu);
struct posix_acl *fuse_get_acl(struct mnt_idmap *idmap,
			       struct dentry *dentry, int type);
int fuse_set_acl(struct mnt_idmap *, struct dentry *dentry,
		 struct posix_acl *acl, int type);

/* readdir.c */
int fuse_readdir(struct file *file, struct dir_context *ctx);

void fuse_free_conn(struct fuse_conn *fc);

/* dax.c */

#define FUSE_IS_DAX(inode) (IS_ENABLED(CONFIG_FUSE_DAX) && IS_DAX(inode))

ssize_t fuse_dax_read_iter(struct kiocb *iocb, struct iov_iter *to);
ssize_t fuse_dax_write_iter(struct kiocb *iocb, struct iov_iter *from);
int fuse_dax_mmap(struct file *file, struct vm_area_struct *vma);
int fuse_dax_break_layouts(struct inode *inode, u64 dmap_start, u64 dmap_end);
int fuse_dax_conn_alloc(struct fuse_conn *fc, enum fuse_dax_mode mode,
			struct dax_device *dax_dev);
void fuse_dax_conn_free(struct fuse_conn *fc);
bool fuse_dax_inode_alloc(struct super_block *sb, struct fuse_inode *fi);
void fuse_dax_inode_init(struct inode *inode, unsigned int flags);
void fuse_dax_inode_cleanup(struct inode *inode);
void fuse_dax_dontcache(struct inode *inode, unsigned int flags);
bool fuse_dax_check_alignment(struct fuse_conn *fc, unsigned int map_alignment);
void fuse_dax_cancel_work(struct fuse_conn *fc);

/* ioctl.c */
long fuse_file_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
long fuse_file_compat_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg);
int fuse_fileattr_get(struct dentry *dentry, struct file_kattr *fa);
int fuse_fileattr_set(struct mnt_idmap *idmap,
		      struct dentry *dentry, struct file_kattr *fa);

/* iomode.c */
int fuse_file_cached_io_open(struct inode *inode, struct fuse_file *ff);
int fuse_inode_uncached_io_start(struct fuse_inode *fi,
				 struct fuse_backing *fb);
void fuse_inode_uncached_io_end(struct fuse_inode *fi);

int fuse_file_io_open(struct file *file, struct inode *inode);
void fuse_file_io_release(struct fuse_file *ff, struct inode *inode);

/* file.c */
struct fuse_file *fuse_file_open(struct fuse_mount *fm, u64 nodeid,
				 unsigned int open_flags, bool isdir);
void fuse_file_release(struct inode *inode, struct fuse_file *ff,
		       unsigned int open_flags, fl_owner_t id, bool isdir);

/* backing.c */
#ifdef CONFIG_FUSE_PASSTHROUGH
struct fuse_backing *fuse_backing_get(struct fuse_backing *fb);
void fuse_backing_put(struct fuse_backing *fb);
struct fuse_backing *fuse_backing_lookup(struct fuse_conn *fc, int backing_id);
#else

static inline struct fuse_backing *fuse_backing_get(struct fuse_backing *fb)
{
	return NULL;
}

static inline void fuse_backing_put(struct fuse_backing *fb)
{
}
static inline struct fuse_backing *fuse_backing_lookup(struct fuse_conn *fc,
						       int backing_id)
{
	return NULL;
}
#endif

void fuse_backing_files_init(struct fuse_conn *fc);
void fuse_backing_files_free(struct fuse_conn *fc);

/* passthrough.c */
static inline struct fuse_backing *fuse_inode_backing(struct fuse_inode *fi)
{
#ifdef CONFIG_FUSE_PASSTHROUGH
	return READ_ONCE(fi->fb);
#else
	return NULL;
#endif
}

static inline struct fuse_backing *fuse_inode_backing_set(struct fuse_inode *fi,
							  struct fuse_backing *fb)
{
#ifdef CONFIG_FUSE_PASSTHROUGH
	return xchg(&fi->fb, fb);
#else
	return NULL;
#endif
}

struct fuse_backing *fuse_passthrough_open(struct file *file, int backing_id);
void fuse_passthrough_release(struct fuse_file *ff, struct fuse_backing *fb);

static inline struct file *fuse_file_passthrough(struct fuse_file *ff)
{
#ifdef CONFIG_FUSE_PASSTHROUGH
	return ff->passthrough;
#else
	return NULL;
#endif
}

ssize_t fuse_passthrough_read_iter(struct kiocb *iocb, struct iov_iter *iter);
ssize_t fuse_passthrough_write_iter(struct kiocb *iocb, struct iov_iter *iter);
ssize_t fuse_passthrough_splice_read(struct file *in, loff_t *ppos,
				     struct pipe_inode_info *pipe,
				     size_t len, unsigned int flags);
ssize_t fuse_passthrough_splice_write(struct pipe_inode_info *pipe,
				      struct file *out, loff_t *ppos,
				      size_t len, unsigned int flags);
ssize_t fuse_passthrough_mmap(struct file *file, struct vm_area_struct *vma);

#ifdef CONFIG_SYSCTL
extern int fuse_sysctl_register(void);
extern void fuse_sysctl_unregister(void);
#else
#define fuse_sysctl_register()		(0)
#define fuse_sysctl_unregister()	do { } while (0)
#endif /* CONFIG_SYSCTL */

#endif /* _FS_FUSE_I_H */
