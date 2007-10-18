/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2006  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.
*/

#include <linux/fuse.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/backing-dev.h>
#include <linux/mutex.h>

/** Max number of pages that can be used in a single read request */
#define FUSE_MAX_PAGES_PER_REQ 32

/** Maximum number of outstanding background requests */
#define FUSE_MAX_BACKGROUND 12

/** Congestion starts at 75% of maximum */
#define FUSE_CONGESTION_THRESHOLD (FUSE_MAX_BACKGROUND * 75 / 100)

/** It could be as large as PATH_MAX, but would that have any uses? */
#define FUSE_NAME_MAX 1024

/** Number of dentries for each connection in the control filesystem */
#define FUSE_CTL_NUM_DENTRIES 3

/** If the FUSE_DEFAULT_PERMISSIONS flag is given, the filesystem
    module will check permissions based on the file mode.  Otherwise no
    permission checking is done in the kernel */
#define FUSE_DEFAULT_PERMISSIONS (1 << 0)

/** If the FUSE_ALLOW_OTHER flag is given, then not only the user
    doing the mount will be allowed to access the filesystem */
#define FUSE_ALLOW_OTHER         (1 << 1)

/** List of active connections */
extern struct list_head fuse_conn_list;

/** Global mutex protecting fuse_conn_list and the control filesystem */
extern struct mutex fuse_mutex;

/** FUSE inode */
struct fuse_inode {
	/** Inode data */
	struct inode inode;

	/** Unique ID, which identifies the inode between userspace
	 * and kernel */
	u64 nodeid;

	/** Number of lookups on this inode */
	u64 nlookup;

	/** The request used for sending the FORGET message */
	struct fuse_req *forget_req;

	/** Time in jiffies until the file attributes are valid */
	u64 i_time;

	/** The sticky bit in inode->i_mode may have been removed, so
	    preserve the original mode */
	mode_t orig_i_mode;

	/** Version of last attribute change */
	u64 attr_version;
};

/** FUSE specific file data */
struct fuse_file {
	/** Request reserved for flush and release */
	struct fuse_req *reserved_req;

	/** File handle used by userspace */
	u64 fh;

	/** Refcount */
	atomic_t count;
};

/** One input argument of a request */
struct fuse_in_arg {
	unsigned size;
	const void *value;
};

/** The request input */
struct fuse_in {
	/** The request header */
	struct fuse_in_header h;

	/** True if the data for the last argument is in req->pages */
	unsigned argpages:1;

	/** Number of arguments */
	unsigned numargs;

	/** Array of arguments */
	struct fuse_in_arg args[3];
};

/** One output argument of a request */
struct fuse_arg {
	unsigned size;
	void *value;
};

/** The request output */
struct fuse_out {
	/** Header returned from userspace */
	struct fuse_out_header h;

	/*
	 * The following bitfields are not changed during the request
	 * processing
	 */

	/** Last argument is variable length (can be shorter than
	    arg->size) */
	unsigned argvar:1;

	/** Last argument is a list of pages to copy data to */
	unsigned argpages:1;

	/** Zero partially or not copied pages */
	unsigned page_zeroing:1;

	/** Number or arguments */
	unsigned numargs;

	/** Array of arguments */
	struct fuse_arg args[3];
};

/** The request state */
enum fuse_req_state {
	FUSE_REQ_INIT = 0,
	FUSE_REQ_PENDING,
	FUSE_REQ_READING,
	FUSE_REQ_SENT,
	FUSE_REQ_WRITING,
	FUSE_REQ_FINISHED
};

struct fuse_conn;

/**
 * A request to the client
 */
struct fuse_req {
	/** This can be on either pending processing or io lists in
	    fuse_conn */
	struct list_head list;

	/** Entry on the interrupts list  */
	struct list_head intr_entry;

	/** refcount */
	atomic_t count;

	/** Unique ID for the interrupt request */
	u64 intr_unique;

	/*
	 * The following bitfields are either set once before the
	 * request is queued or setting/clearing them is protected by
	 * fuse_conn->lock
	 */

	/** True if the request has reply */
	unsigned isreply:1;

	/** Force sending of the request even if interrupted */
	unsigned force:1;

	/** The request was aborted */
	unsigned aborted:1;

	/** Request is sent in the background */
	unsigned background:1;

	/** The request has been interrupted */
	unsigned interrupted:1;

	/** Data is being copied to/from the request */
	unsigned locked:1;

	/** Request is counted as "waiting" */
	unsigned waiting:1;

	/** State of the request */
	enum fuse_req_state state;

	/** The request input */
	struct fuse_in in;

	/** The request output */
	struct fuse_out out;

	/** Used to wake up the task waiting for completion of request*/
	wait_queue_head_t waitq;

	/** Data for asynchronous requests */
	union {
		struct fuse_forget_in forget_in;
		struct fuse_release_in release_in;
		struct fuse_init_in init_in;
		struct fuse_init_out init_out;
		struct fuse_read_in read_in;
		struct fuse_lk_in lk_in;
	} misc;

	/** page vector */
	struct page *pages[FUSE_MAX_PAGES_PER_REQ];

	/** number of pages in vector */
	unsigned num_pages;

	/** offset of data on first page */
	unsigned page_offset;

	/** File used in the request (or NULL) */
	struct fuse_file *ff;

	/** vfsmount used in release */
	struct vfsmount *vfsmount;

	/** dentry used in release */
	struct dentry *dentry;

	/** Request completion callback */
	void (*end)(struct fuse_conn *, struct fuse_req *);

	/** Request is stolen from fuse_file->reserved_req */
	struct file *stolen_file;
};

/**
 * A Fuse connection.
 *
 * This structure is created, when the filesystem is mounted, and is
 * destroyed, when the client device is closed and the filesystem is
 * unmounted.
 */
struct fuse_conn {
	/** Lock protecting accessess to  members of this structure */
	spinlock_t lock;

	/** Mutex protecting against directory alias creation */
	struct mutex inst_mutex;

	/** Refcount */
	atomic_t count;

	/** The user id for this mount */
	uid_t user_id;

	/** The group id for this mount */
	gid_t group_id;

	/** The fuse mount flags for this mount */
	unsigned flags;

	/** Maximum read size */
	unsigned max_read;

	/** Maximum write size */
	unsigned max_write;

	/** Readers of the connection are waiting on this */
	wait_queue_head_t waitq;

	/** The list of pending requests */
	struct list_head pending;

	/** The list of requests being processed */
	struct list_head processing;

	/** The list of requests under I/O */
	struct list_head io;

	/** Number of requests currently in the background */
	unsigned num_background;

	/** Pending interrupts */
	struct list_head interrupts;

	/** Flag indicating if connection is blocked.  This will be
	    the case before the INIT reply is received, and if there
	    are too many outstading backgrounds requests */
	int blocked;

	/** waitq for blocked connection */
	wait_queue_head_t blocked_waitq;

	/** waitq for reserved requests */
	wait_queue_head_t reserved_req_waitq;

	/** The next unique request id */
	u64 reqctr;

	/** Connection established, cleared on umount, connection
	    abort and device release */
	unsigned connected;

	/** Connection failed (version mismatch).  Cannot race with
	    setting other bitfields since it is only set once in INIT
	    reply, before any other request, and never cleared */
	unsigned conn_error : 1;

	/** Connection successful.  Only set in INIT */
	unsigned conn_init : 1;

	/** Do readpages asynchronously?  Only set in INIT */
	unsigned async_read : 1;

	/*
	 * The following bitfields are only for optimization purposes
	 * and hence races in setting them will not cause malfunction
	 */

	/** Is fsync not implemented by fs? */
	unsigned no_fsync : 1;

	/** Is fsyncdir not implemented by fs? */
	unsigned no_fsyncdir : 1;

	/** Is flush not implemented by fs? */
	unsigned no_flush : 1;

	/** Is setxattr not implemented by fs? */
	unsigned no_setxattr : 1;

	/** Is getxattr not implemented by fs? */
	unsigned no_getxattr : 1;

	/** Is listxattr not implemented by fs? */
	unsigned no_listxattr : 1;

	/** Is removexattr not implemented by fs? */
	unsigned no_removexattr : 1;

	/** Are file locking primitives not implemented by fs? */
	unsigned no_lock : 1;

	/** Is access not implemented by fs? */
	unsigned no_access : 1;

	/** Is create not implemented by fs? */
	unsigned no_create : 1;

	/** Is interrupt not implemented by fs? */
	unsigned no_interrupt : 1;

	/** Is bmap not implemented by fs? */
	unsigned no_bmap : 1;

	/** The number of requests waiting for completion */
	atomic_t num_waiting;

	/** Negotiated minor version */
	unsigned minor;

	/** Backing dev info */
	struct backing_dev_info bdi;

	/** Entry on the fuse_conn_list */
	struct list_head entry;

	/** Unique ID */
	u64 id;

	/** Dentries in the control filesystem */
	struct dentry *ctl_dentry[FUSE_CTL_NUM_DENTRIES];

	/** number of dentries used in the above array */
	int ctl_ndents;

	/** O_ASYNC requests */
	struct fasync_struct *fasync;

	/** Key for lock owner ID scrambling */
	u32 scramble_key[4];

	/** Reserved request for the DESTROY message */
	struct fuse_req *destroy_req;

	/** Version counter for attribute changes */
	u64 attr_version;
};

static inline struct fuse_conn *get_fuse_conn_super(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct fuse_conn *get_fuse_conn(struct inode *inode)
{
	return get_fuse_conn_super(inode->i_sb);
}

static inline struct fuse_inode *get_fuse_inode(struct inode *inode)
{
	return container_of(inode, struct fuse_inode, inode);
}

static inline u64 get_node_id(struct inode *inode)
{
	return get_fuse_inode(inode)->nodeid;
}

/** Device operations */
extern const struct file_operations fuse_dev_operations;

/**
 * Get a filled in inode
 */
struct inode *fuse_iget(struct super_block *sb, unsigned long nodeid,
			int generation, struct fuse_attr *attr,
			u64 attr_valid, u64 attr_version);

/**
 * Send FORGET command
 */
void fuse_send_forget(struct fuse_conn *fc, struct fuse_req *req,
		      unsigned long nodeid, u64 nlookup);

/**
 * Initialize READ or READDIR request
 */
void fuse_read_fill(struct fuse_req *req, struct fuse_file *ff,
		    struct inode *inode, loff_t pos, size_t count, int opcode);

/**
 * Send OPEN or OPENDIR request
 */
int fuse_open_common(struct inode *inode, struct file *file, int isdir);

struct fuse_file *fuse_file_alloc(void);
void fuse_file_free(struct fuse_file *ff);
void fuse_finish_open(struct inode *inode, struct file *file,
		      struct fuse_file *ff, struct fuse_open_out *outarg);

/** Fill in ff->reserved_req with a RELEASE request */
void fuse_release_fill(struct fuse_file *ff, u64 nodeid, int flags, int opcode);

/**
 * Send RELEASE or RELEASEDIR request
 */
int fuse_release_common(struct inode *inode, struct file *file, int isdir);

/**
 * Send FSYNC or FSYNCDIR request
 */
int fuse_fsync_common(struct file *file, struct dentry *de, int datasync,
		      int isdir);

/**
 * Initialize file operations on a regular file
 */
void fuse_init_file_inode(struct inode *inode);

/**
 * Initialize inode operations on regular files and special files
 */
void fuse_init_common(struct inode *inode);

/**
 * Initialize inode and file operations on a directory
 */
void fuse_init_dir(struct inode *inode);

/**
 * Initialize inode operations on a symlink
 */
void fuse_init_symlink(struct inode *inode);

/**
 * Change attributes of an inode
 */
void fuse_change_attributes(struct inode *inode, struct fuse_attr *attr,
			    u64 attr_valid, u64 attr_version);

/**
 * Initialize the client device
 */
int fuse_dev_init(void);

/**
 * Cleanup the client device
 */
void fuse_dev_cleanup(void);

int fuse_ctl_init(void);
void fuse_ctl_cleanup(void);

/**
 * Allocate a request
 */
struct fuse_req *fuse_request_alloc(void);

/**
 * Free a request
 */
void fuse_request_free(struct fuse_req *req);

/**
 * Get a request, may fail with -ENOMEM
 */
struct fuse_req *fuse_get_req(struct fuse_conn *fc);

/**
 * Gets a requests for a file operation, always succeeds
 */
struct fuse_req *fuse_get_req_nofail(struct fuse_conn *fc, struct file *file);

/**
 * Decrement reference count of a request.  If count goes to zero free
 * the request.
 */
void fuse_put_request(struct fuse_conn *fc, struct fuse_req *req);

/**
 * Send a request (synchronous)
 */
void request_send(struct fuse_conn *fc, struct fuse_req *req);

/**
 * Send a request with no reply
 */
void request_send_noreply(struct fuse_conn *fc, struct fuse_req *req);

/**
 * Send a request in the background
 */
void request_send_background(struct fuse_conn *fc, struct fuse_req *req);

/* Abort all requests */
void fuse_abort_conn(struct fuse_conn *fc);

/**
 * Invalidate inode attributes
 */
void fuse_invalidate_attr(struct inode *inode);

/**
 * Acquire reference to fuse_conn
 */
struct fuse_conn *fuse_conn_get(struct fuse_conn *fc);

/**
 * Release reference to fuse_conn
 */
void fuse_conn_put(struct fuse_conn *fc);

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

/**
 * Is task allowed to perform filesystem operation?
 */
int fuse_allow_task(struct fuse_conn *fc, struct task_struct *task);
