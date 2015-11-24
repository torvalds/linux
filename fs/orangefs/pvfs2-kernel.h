/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/*
 *  The ORANGEFS Linux kernel support allows ORANGEFS volumes to be mounted and
 *  accessed through the Linux VFS (i.e. using standard I/O system calls).
 *  This support is only needed on clients that wish to mount the file system.
 *
 */

/*
 *  Declarations and macros for the ORANGEFS Linux kernel support.
 */

#ifndef __ORANGEFSKERNEL_H
#define __ORANGEFSKERNEL_H

#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/statfs.h>
#include <linux/backing-dev.h>
#include <linux/device.h>
#include <linux/mpage.h>
#include <linux/namei.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>

#include <linux/aio.h>
#include <linux/posix_acl.h>
#include <linux/posix_acl_xattr.h>
#include <linux/compat.h>
#include <linux/mount.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/uio.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/wait.h>
#include <linux/dcache.h>
#include <linux/pagemap.h>
#include <linux/poll.h>
#include <linux/rwsem.h>
#include <linux/xattr.h>
#include <linux/exportfs.h>

#include <asm/unaligned.h>

#include "pvfs2-dev-proto.h"

#ifdef ORANGEFS_KERNEL_DEBUG
#define ORANGEFS_DEFAULT_OP_TIMEOUT_SECS       10
#else
#define ORANGEFS_DEFAULT_OP_TIMEOUT_SECS       20
#endif

#define ORANGEFS_BUFMAP_WAIT_TIMEOUT_SECS   30

#define ORANGEFS_DEFAULT_SLOT_TIMEOUT_SECS     900	/* 15 minutes */

#define ORANGEFS_REQDEVICE_NAME          "pvfs2-req"

#define ORANGEFS_DEVREQ_MAGIC             0x20030529
#define ORANGEFS_LINK_MAX                 0x000000FF
#define ORANGEFS_PURGE_RETRY_COUNT     0x00000005
#define ORANGEFS_SEEK_END              0x00000002
#define ORANGEFS_MAX_NUM_OPTIONS          0x00000004
#define ORANGEFS_MAX_MOUNT_OPT_LEN        0x00000080
#define ORANGEFS_MAX_FSKEY_LEN            64

#define MAX_DEV_REQ_UPSIZE (2*sizeof(__s32) +   \
sizeof(__u64) + sizeof(struct orangefs_upcall_s))
#define MAX_DEV_REQ_DOWNSIZE (2*sizeof(__s32) + \
sizeof(__u64) + sizeof(struct orangefs_downcall_s))

#define BITS_PER_LONG_DIV_8 (BITS_PER_LONG >> 3)

/* borrowed from irda.h */
#ifndef MSECS_TO_JIFFIES
#define MSECS_TO_JIFFIES(ms) (((ms)*HZ+999)/1000)
#endif

#define MAX_ALIGNED_DEV_REQ_UPSIZE				\
		(MAX_DEV_REQ_UPSIZE +				\
			((((MAX_DEV_REQ_UPSIZE /		\
				(BITS_PER_LONG_DIV_8)) *	\
				(BITS_PER_LONG_DIV_8)) +	\
			    (BITS_PER_LONG_DIV_8)) -		\
			MAX_DEV_REQ_UPSIZE))

#define MAX_ALIGNED_DEV_REQ_DOWNSIZE				\
		(MAX_DEV_REQ_DOWNSIZE +				\
			((((MAX_DEV_REQ_DOWNSIZE /		\
				(BITS_PER_LONG_DIV_8)) *	\
				(BITS_PER_LONG_DIV_8)) +	\
			    (BITS_PER_LONG_DIV_8)) -		\
			MAX_DEV_REQ_DOWNSIZE))

/*
 * valid orangefs kernel operation states
 *
 * unknown  - op was just initialized
 * waiting  - op is on request_list (upward bound)
 * inprogr  - op is in progress (waiting for downcall)
 * serviced - op has matching downcall; ok
 * purged   - op has to start a timer since client-core
 *            exited uncleanly before servicing op
 */
enum orangefs_vfs_op_states {
	OP_VFS_STATE_UNKNOWN = 0,
	OP_VFS_STATE_WAITING = 1,
	OP_VFS_STATE_INPROGR = 2,
	OP_VFS_STATE_SERVICED = 4,
	OP_VFS_STATE_PURGED = 8,
};

#define set_op_state_waiting(op)     ((op)->op_state = OP_VFS_STATE_WAITING)
#define set_op_state_inprogress(op)  ((op)->op_state = OP_VFS_STATE_INPROGR)
#define set_op_state_serviced(op)    ((op)->op_state = OP_VFS_STATE_SERVICED)
#define set_op_state_purged(op)      ((op)->op_state |= OP_VFS_STATE_PURGED)

#define op_state_waiting(op)     ((op)->op_state & OP_VFS_STATE_WAITING)
#define op_state_in_progress(op) ((op)->op_state & OP_VFS_STATE_INPROGR)
#define op_state_serviced(op)    ((op)->op_state & OP_VFS_STATE_SERVICED)
#define op_state_purged(op)      ((op)->op_state & OP_VFS_STATE_PURGED)

#define get_op(op)					\
	do {						\
		atomic_inc(&(op)->aio_ref_count);	\
		gossip_debug(GOSSIP_DEV_DEBUG,	\
			"(get) Alloced OP (%p:%llu)\n",	\
			op,				\
			llu((op)->tag));		\
	} while (0)

#define put_op(op)							\
	do {								\
		if (atomic_sub_and_test(1, &(op)->aio_ref_count) == 1) {  \
			gossip_debug(GOSSIP_DEV_DEBUG,		\
				"(put) Releasing OP (%p:%llu)\n",	\
				op,					\
				llu((op)->tag));			\
			op_release(op);					\
			}						\
	} while (0)

#define op_wait(op) (atomic_read(&(op)->aio_ref_count) <= 2 ? 0 : 1)

/*
 * Defines for controlling whether I/O upcalls are for async or sync operations
 */
enum ORANGEFS_async_io_type {
	ORANGEFS_VFS_SYNC_IO = 0,
	ORANGEFS_VFS_ASYNC_IO = 1,
};

/*
 * An array of client_debug_mask will be built to hold debug keyword/mask
 * values fetched from userspace.
 */
struct client_debug_mask {
	char *keyword;
	__u64 mask1;
	__u64 mask2;
};

/*
 * orangefs kernel memory related flags
 */

#if ((defined ORANGEFS_KERNEL_DEBUG) && (defined CONFIG_DEBUG_SLAB))
#define ORANGEFS_CACHE_CREATE_FLAGS SLAB_RED_ZONE
#else
#define ORANGEFS_CACHE_CREATE_FLAGS 0
#endif /* ((defined ORANGEFS_KERNEL_DEBUG) && (defined CONFIG_DEBUG_SLAB)) */

#define ORANGEFS_CACHE_ALLOC_FLAGS (GFP_KERNEL)
#define ORANGEFS_GFP_FLAGS (GFP_KERNEL)
#define ORANGEFS_BUFMAP_GFP_FLAGS (GFP_KERNEL)

/* orangefs xattr and acl related defines */
#define ORANGEFS_XATTR_INDEX_POSIX_ACL_ACCESS  1
#define ORANGEFS_XATTR_INDEX_POSIX_ACL_DEFAULT 2
#define ORANGEFS_XATTR_INDEX_TRUSTED           3
#define ORANGEFS_XATTR_INDEX_DEFAULT           4

#if 0
#ifndef POSIX_ACL_XATTR_ACCESS
#define POSIX_ACL_XATTR_ACCESS	"system.posix_acl_access"
#endif
#ifndef POSIX_ACL_XATTR_DEFAULT
#define POSIX_ACL_XATTR_DEFAULT	"system.posix_acl_default"
#endif
#endif

#define ORANGEFS_XATTR_NAME_ACL_ACCESS  POSIX_ACL_XATTR_ACCESS
#define ORANGEFS_XATTR_NAME_ACL_DEFAULT POSIX_ACL_XATTR_DEFAULT
#define ORANGEFS_XATTR_NAME_TRUSTED_PREFIX "trusted."
#define ORANGEFS_XATTR_NAME_DEFAULT_PREFIX ""

/* these functions are defined in orangefs-utils.c */
int orangefs_prepare_cdm_array(char *debug_array_string);
int orangefs_prepare_debugfs_help_string(int);

/* defined in orangefs-debugfs.c */
int orangefs_client_debug_init(void);

void debug_string_to_mask(char *, void *, int);
void do_c_mask(int, char *, struct client_debug_mask **);
void do_k_mask(int, char *, __u64 **);

void debug_mask_to_string(void *, int);
void do_k_string(void *, int);
void do_c_string(void *, int);
int check_amalgam_keyword(void *, int);
int keyword_is_amalgam(char *);

/*these variables are defined in orangefs-mod.c */
extern char kernel_debug_string[ORANGEFS_MAX_DEBUG_STRING_LEN];
extern char client_debug_string[ORANGEFS_MAX_DEBUG_STRING_LEN];
extern char client_debug_array_string[ORANGEFS_MAX_DEBUG_STRING_LEN];
extern unsigned int kernel_mask_set_mod_init;

extern int orangefs_init_acl(struct inode *inode, struct inode *dir);
extern const struct xattr_handler *orangefs_xattr_handlers[];

extern struct posix_acl *orangefs_get_acl(struct inode *inode, int type);
extern int orangefs_set_acl(struct inode *inode, struct posix_acl *acl, int type);

/*
 * Redefine xtvec structure so that we could move helper functions out of
 * the define
 */
struct xtvec {
	__kernel_off_t xtv_off;		/* must be off_t */
	__kernel_size_t xtv_len;	/* must be size_t */
};

/*
 * orangefs data structures
 */
struct orangefs_kernel_op_s {
	enum orangefs_vfs_op_states op_state;
	__u64 tag;

	/*
	 * Set uses_shared_memory to 1 if this operation uses shared memory.
	 * If true, then a retry on the op must also get a new shared memory
	 * buffer and re-populate it.
	 */
	int uses_shared_memory;

	struct orangefs_upcall_s upcall;
	struct orangefs_downcall_s downcall;

	wait_queue_head_t waitq;
	spinlock_t lock;

	int io_completed;
	wait_queue_head_t io_completion_waitq;

	/* VFS aio fields */

	/* used by the async I/O code to stash the orangefs_kiocb_s structure */
	void *priv;

	/* used again for the async I/O code for deallocation */
	atomic_t aio_ref_count;

	int attempts;

	struct list_head list;
};

/* per inode private orangefs info */
struct orangefs_inode_s {
	struct orangefs_object_kref refn;
	char link_target[ORANGEFS_NAME_MAX];
	__s64 blksize;
	/*
	 * Reading/Writing Extended attributes need to acquire the appropriate
	 * reader/writer semaphore on the orangefs_inode_s structure.
	 */
	struct rw_semaphore xattr_sem;

	struct inode vfs_inode;
	sector_t last_failed_block_index_read;

	/*
	 * State of in-memory attributes not yet flushed to disk associated
	 * with this object
	 */
	unsigned long pinode_flags;

	/* All allocated orangefs_inode_s objects are chained to a list */
	struct list_head list;
};

#define P_ATIME_FLAG 0
#define P_MTIME_FLAG 1
#define P_CTIME_FLAG 2
#define P_MODE_FLAG  3

#define ClearAtimeFlag(pinode) clear_bit(P_ATIME_FLAG, &(pinode)->pinode_flags)
#define SetAtimeFlag(pinode)   set_bit(P_ATIME_FLAG, &(pinode)->pinode_flags)
#define AtimeFlag(pinode)      test_bit(P_ATIME_FLAG, &(pinode)->pinode_flags)

#define ClearMtimeFlag(pinode) clear_bit(P_MTIME_FLAG, &(pinode)->pinode_flags)
#define SetMtimeFlag(pinode)   set_bit(P_MTIME_FLAG, &(pinode)->pinode_flags)
#define MtimeFlag(pinode)      test_bit(P_MTIME_FLAG, &(pinode)->pinode_flags)

#define ClearCtimeFlag(pinode) clear_bit(P_CTIME_FLAG, &(pinode)->pinode_flags)
#define SetCtimeFlag(pinode)   set_bit(P_CTIME_FLAG, &(pinode)->pinode_flags)
#define CtimeFlag(pinode)      test_bit(P_CTIME_FLAG, &(pinode)->pinode_flags)

#define ClearModeFlag(pinode) clear_bit(P_MODE_FLAG, &(pinode)->pinode_flags)
#define SetModeFlag(pinode)   set_bit(P_MODE_FLAG, &(pinode)->pinode_flags)
#define ModeFlag(pinode)      test_bit(P_MODE_FLAG, &(pinode)->pinode_flags)

/* per superblock private orangefs info */
struct orangefs_sb_info_s {
	struct orangefs_khandle root_khandle;
	__s32 fs_id;
	int id;
	int flags;
#define ORANGEFS_OPT_INTR	0x01
#define ORANGEFS_OPT_LOCAL_LOCK	0x02
	char devname[ORANGEFS_MAX_SERVER_ADDR_LEN];
	struct super_block *sb;
	int mount_pending;
	struct list_head list;
};

/*
 * structure that holds the state of any async I/O operation issued
 * through the VFS. Needed especially to handle cancellation requests
 * or even completion notification so that the VFS client-side daemon
 * can free up its vfs_request slots.
 */
struct orangefs_kiocb_s {
	/* the pointer to the task that initiated the AIO */
	struct task_struct *tsk;

	/* pointer to the kiocb that kicked this operation */
	struct kiocb *kiocb;

	/* buffer index that was used for the I/O */
	struct orangefs_bufmap *bufmap;
	int buffer_index;

	/* orangefs kernel operation type */
	struct orangefs_kernel_op_s *op;

	/* The user space buffers from/to which I/O is being staged */
	struct iovec *iov;

	/* number of elements in the iovector */
	unsigned long nr_segs;

	/* set to indicate the type of the operation */
	int rw;

	/* file offset */
	loff_t offset;

	/* and the count in bytes */
	size_t bytes_to_be_copied;

	ssize_t bytes_copied;
	int needs_cleanup;
};

struct orangefs_stats {
	unsigned long cache_hits;
	unsigned long cache_misses;
	unsigned long reads;
	unsigned long writes;
};

extern struct orangefs_stats g_orangefs_stats;

/*
 * NOTE: See Documentation/filesystems/porting for information
 * on implementing FOO_I and properly accessing fs private data
 */
static inline struct orangefs_inode_s *ORANGEFS_I(struct inode *inode)
{
	return container_of(inode, struct orangefs_inode_s, vfs_inode);
}

static inline struct orangefs_sb_info_s *ORANGEFS_SB(struct super_block *sb)
{
	return (struct orangefs_sb_info_s *) sb->s_fs_info;
}

/* ino_t descends from "unsigned long", 8 bytes, 64 bits. */
static inline ino_t orangefs_khandle_to_ino(struct orangefs_khandle *khandle)
{
	union {
		unsigned char u[8];
		__u64 ino;
	} ihandle;

	ihandle.u[0] = khandle->u[0] ^ khandle->u[4];
	ihandle.u[1] = khandle->u[1] ^ khandle->u[5];
	ihandle.u[2] = khandle->u[2] ^ khandle->u[6];
	ihandle.u[3] = khandle->u[3] ^ khandle->u[7];
	ihandle.u[4] = khandle->u[12] ^ khandle->u[8];
	ihandle.u[5] = khandle->u[13] ^ khandle->u[9];
	ihandle.u[6] = khandle->u[14] ^ khandle->u[10];
	ihandle.u[7] = khandle->u[15] ^ khandle->u[11];

	return ihandle.ino;
}

static inline struct orangefs_khandle *get_khandle_from_ino(struct inode *inode)
{
	return &(ORANGEFS_I(inode)->refn.khandle);
}

static inline __s32 get_fsid_from_ino(struct inode *inode)
{
	return ORANGEFS_I(inode)->refn.fs_id;
}

static inline ino_t get_ino_from_khandle(struct inode *inode)
{
	struct orangefs_khandle *khandle;
	ino_t ino;

	khandle = get_khandle_from_ino(inode);
	ino = orangefs_khandle_to_ino(khandle);
	return ino;
}

static inline ino_t get_parent_ino_from_dentry(struct dentry *dentry)
{
	return get_ino_from_khandle(dentry->d_parent->d_inode);
}

static inline int is_root_handle(struct inode *inode)
{
	gossip_debug(GOSSIP_DCACHE_DEBUG,
		     "%s: root handle: %pU, this handle: %pU:\n",
		     __func__,
		     &ORANGEFS_SB(inode->i_sb)->root_khandle,
		     get_khandle_from_ino(inode));

	if (ORANGEFS_khandle_cmp(&(ORANGEFS_SB(inode->i_sb)->root_khandle),
			     get_khandle_from_ino(inode)))
		return 0;
	else
		return 1;
}

static inline int match_handle(struct orangefs_khandle resp_handle,
			       struct inode *inode)
{
	gossip_debug(GOSSIP_DCACHE_DEBUG,
		     "%s: one handle: %pU, another handle:%pU:\n",
		     __func__,
		     &resp_handle,
		     get_khandle_from_ino(inode));

	if (ORANGEFS_khandle_cmp(&resp_handle, get_khandle_from_ino(inode)))
		return 0;
	else
		return 1;
}

/*
 * defined in orangefs-cache.c
 */
int op_cache_initialize(void);
int op_cache_finalize(void);
struct orangefs_kernel_op_s *op_alloc(__s32 type);
char *get_opname_string(struct orangefs_kernel_op_s *new_op);
void op_release(struct orangefs_kernel_op_s *op);

int dev_req_cache_initialize(void);
int dev_req_cache_finalize(void);
void *dev_req_alloc(void);
void dev_req_release(void *);

int orangefs_inode_cache_initialize(void);
int orangefs_inode_cache_finalize(void);

int kiocb_cache_initialize(void);
int kiocb_cache_finalize(void);
struct orangefs_kiocb_s *kiocb_alloc(void);
void kiocb_release(struct orangefs_kiocb_s *ptr);

/*
 * defined in orangefs-mod.c
 */
void purge_inprogress_ops(void);

/*
 * defined in waitqueue.c
 */
int wait_for_matching_downcall(struct orangefs_kernel_op_s *op);
int wait_for_cancellation_downcall(struct orangefs_kernel_op_s *op);
void orangefs_clean_up_interrupted_operation(struct orangefs_kernel_op_s *op);
void purge_waiting_ops(void);

/*
 * defined in super.c
 */
struct dentry *orangefs_mount(struct file_system_type *fst,
			   int flags,
			   const char *devname,
			   void *data);

void orangefs_kill_sb(struct super_block *sb);
int orangefs_remount(struct super_block *sb);

int fsid_key_table_initialize(void);
void fsid_key_table_finalize(void);

/*
 * defined in inode.c
 */
__u32 convert_to_orangefs_mask(unsigned long lite_mask);
struct inode *orangefs_new_inode(struct super_block *sb,
			      struct inode *dir,
			      int mode,
			      dev_t dev,
			      struct orangefs_object_kref *ref);

int orangefs_setattr(struct dentry *dentry, struct iattr *iattr);

int orangefs_getattr(struct vfsmount *mnt,
		  struct dentry *dentry,
		  struct kstat *kstat);

/*
 * defined in xattr.c
 */
int orangefs_setxattr(struct dentry *dentry,
		   const char *name,
		   const void *value,
		   size_t size,
		   int flags);

ssize_t orangefs_getxattr(struct dentry *dentry,
		       const char *name,
		       void *buffer,
		       size_t size);

ssize_t orangefs_listxattr(struct dentry *dentry, char *buffer, size_t size);

/*
 * defined in namei.c
 */
struct inode *orangefs_iget(struct super_block *sb,
			 struct orangefs_object_kref *ref);

ssize_t orangefs_inode_read(struct inode *inode,
			    struct iov_iter *iter,
			    loff_t *offset,
			    loff_t readahead_size);

/*
 * defined in devorangefs-req.c
 */
int orangefs_dev_init(void);
void orangefs_dev_cleanup(void);
int is_daemon_in_service(void);
int fs_mount_pending(__s32 fsid);

/*
 * defined in orangefs-utils.c
 */
__s32 fsid_of_op(struct orangefs_kernel_op_s *op);

int orangefs_flush_inode(struct inode *inode);

ssize_t orangefs_inode_getxattr(struct inode *inode,
			     const char *prefix,
			     const char *name,
			     void *buffer,
			     size_t size);

int orangefs_inode_setxattr(struct inode *inode,
			 const char *prefix,
			 const char *name,
			 const void *value,
			 size_t size,
			 int flags);

int orangefs_inode_getattr(struct inode *inode, __u32 mask);

int orangefs_inode_setattr(struct inode *inode, struct iattr *iattr);

void orangefs_op_initialize(struct orangefs_kernel_op_s *op);

void orangefs_make_bad_inode(struct inode *inode);

void block_signals(sigset_t *);

void set_signals(sigset_t *);

int orangefs_unmount_sb(struct super_block *sb);

int orangefs_cancel_op_in_progress(__u64 tag);

static inline __u64 orangefs_convert_time_field(const struct timespec *ts)
{
	return (__u64)ts->tv_sec;
}

int orangefs_normalize_to_errno(__s32 error_code);

extern struct mutex devreq_mutex;
extern struct mutex request_mutex;
extern int debug;
extern int op_timeout_secs;
extern int slot_timeout_secs;
extern struct list_head orangefs_superblocks;
extern spinlock_t orangefs_superblocks_lock;
extern struct list_head orangefs_request_list;
extern spinlock_t orangefs_request_list_lock;
extern wait_queue_head_t orangefs_request_list_waitq;
extern struct list_head *htable_ops_in_progress;
extern spinlock_t htable_ops_in_progress_lock;
extern int hash_table_size;

extern const struct address_space_operations orangefs_address_operations;
extern struct backing_dev_info orangefs_backing_dev_info;
extern struct inode_operations orangefs_file_inode_operations;
extern const struct file_operations orangefs_file_operations;
extern struct inode_operations orangefs_symlink_inode_operations;
extern struct inode_operations orangefs_dir_inode_operations;
extern const struct file_operations orangefs_dir_operations;
extern const struct dentry_operations orangefs_dentry_operations;
extern const struct file_operations orangefs_devreq_file_operations;

extern wait_queue_head_t orangefs_bufmap_init_waitq;

/*
 * misc convenience macros
 */
#define add_op_to_request_list(op)				\
do {								\
	spin_lock(&orangefs_request_list_lock);			\
	spin_lock(&op->lock);					\
	set_op_state_waiting(op);				\
	list_add_tail(&op->list, &orangefs_request_list);		\
	spin_unlock(&orangefs_request_list_lock);			\
	spin_unlock(&op->lock);					\
	wake_up_interruptible(&orangefs_request_list_waitq);	\
} while (0)

#define add_priority_op_to_request_list(op)				\
	do {								\
		spin_lock(&orangefs_request_list_lock);			\
		spin_lock(&op->lock);					\
		set_op_state_waiting(op);				\
									\
		list_add(&op->list, &orangefs_request_list);		\
		spin_unlock(&orangefs_request_list_lock);			\
		spin_unlock(&op->lock);					\
		wake_up_interruptible(&orangefs_request_list_waitq);	\
} while (0)

#define remove_op_from_request_list(op)					\
	do {								\
		struct list_head *tmp = NULL;				\
		struct list_head *tmp_safe = NULL;			\
		struct orangefs_kernel_op_s *tmp_op = NULL;		\
									\
		spin_lock(&orangefs_request_list_lock);			\
		list_for_each_safe(tmp, tmp_safe, &orangefs_request_list) { \
			tmp_op = list_entry(tmp,			\
					    struct orangefs_kernel_op_s,	\
					    list);			\
			if (tmp_op && (tmp_op == op)) {			\
				list_del(&tmp_op->list);		\
				break;					\
			}						\
		}							\
		spin_unlock(&orangefs_request_list_lock);			\
	} while (0)

#define ORANGEFS_OP_INTERRUPTIBLE 1   /* service_operation() is interruptible */
#define ORANGEFS_OP_PRIORITY      2   /* service_operation() is high priority */
#define ORANGEFS_OP_CANCELLATION  4   /* this is a cancellation */
#define ORANGEFS_OP_NO_SEMAPHORE  8   /* don't acquire semaphore */
#define ORANGEFS_OP_ASYNC         16  /* Queue it, but don't wait */

int service_operation(struct orangefs_kernel_op_s *op,
		      const char *op_name,
		      int flags);

/*
 * handles two possible error cases, depending on context.
 *
 * by design, our vfs i/o errors need to be handled in one of two ways,
 * depending on where the error occured.
 *
 * if the error happens in the waitqueue code because we either timed
 * out or a signal was raised while waiting, we need to cancel the
 * userspace i/o operation and free the op manually.  this is done to
 * avoid having the device start writing application data to our shared
 * bufmap pages without us expecting it.
 *
 * FIXME: POSSIBLE OPTIMIZATION:
 * However, if we timed out or if we got a signal AND our upcall was never
 * picked off the queue (i.e. we were in OP_VFS_STATE_WAITING), then we don't
 * need to send a cancellation upcall. The way we can handle this is
 * set error_exit to 2 in such cases and 1 whenever cancellation has to be
 * sent and have handle_error
 * take care of this situation as well..
 *
 * if a orangefs sysint level error occured and i/o has been completed,
 * there is no need to cancel the operation, as the user has finished
 * using the bufmap page and so there is no danger in this case.  in
 * this case, we wake up the device normally so that it may free the
 * op, as normal.
 *
 * note the only reason this is a macro is because both read and write
 * cases need the exact same handling code.
 */
#define handle_io_error()					\
do {								\
	if (!op_state_serviced(new_op)) {			\
		orangefs_cancel_op_in_progress(new_op->tag);	\
		op_release(new_op);				\
	} else {						\
		wake_up_daemon_for_return(new_op);		\
	}							\
	new_op = NULL;						\
	orangefs_bufmap_put(bufmap, buffer_index);				\
	buffer_index = -1;					\
} while (0)

#define get_interruptible_flag(inode) \
	((ORANGEFS_SB(inode->i_sb)->flags & ORANGEFS_OPT_INTR) ? \
		ORANGEFS_OP_INTERRUPTIBLE : 0)

#define add_orangefs_sb(sb)						\
do {									\
	gossip_debug(GOSSIP_SUPER_DEBUG,				\
		     "Adding SB %p to orangefs superblocks\n",		\
		     ORANGEFS_SB(sb));					\
	spin_lock(&orangefs_superblocks_lock);				\
	list_add_tail(&ORANGEFS_SB(sb)->list, &orangefs_superblocks);		\
	spin_unlock(&orangefs_superblocks_lock); \
} while (0)

#define remove_orangefs_sb(sb)						\
do {									\
	struct list_head *tmp = NULL;					\
	struct list_head *tmp_safe = NULL;				\
	struct orangefs_sb_info_s *orangefs_sb = NULL;			\
									\
	spin_lock(&orangefs_superblocks_lock);				\
	list_for_each_safe(tmp, tmp_safe, &orangefs_superblocks) {		\
		orangefs_sb = list_entry(tmp,				\
				      struct orangefs_sb_info_s,		\
				      list);				\
		if (orangefs_sb && (orangefs_sb->sb == sb)) {			\
			gossip_debug(GOSSIP_SUPER_DEBUG,		\
			    "Removing SB %p from orangefs superblocks\n",	\
			orangefs_sb);					\
			list_del(&orangefs_sb->list);			\
			break;						\
		}							\
	}								\
	spin_unlock(&orangefs_superblocks_lock);				\
} while (0)

#define orangefs_lock_inode(inode) spin_lock(&inode->i_lock)
#define orangefs_unlock_inode(inode) spin_unlock(&inode->i_lock)

#define fill_default_sys_attrs(sys_attr, type, mode)			\
do {									\
	sys_attr.owner = from_kuid(current_user_ns(), current_fsuid()); \
	sys_attr.group = from_kgid(current_user_ns(), current_fsgid()); \
	sys_attr.size = 0;						\
	sys_attr.perms = ORANGEFS_util_translate_mode(mode);		\
	sys_attr.objtype = type;					\
	sys_attr.mask = ORANGEFS_ATTR_SYS_ALL_SETABLE;			\
} while (0)

#define orangefs_inode_lock(__i)  mutex_lock(&(__i)->i_mutex)

#define orangefs_inode_unlock(__i) mutex_unlock(&(__i)->i_mutex)

static inline void orangefs_i_size_write(struct inode *inode, loff_t i_size)
{
#if BITS_PER_LONG == 32 && defined(CONFIG_SMP)
	ornagefs_inode_lock(inode);
#endif
	i_size_write(inode, i_size);
#if BITS_PER_LONG == 32 && defined(CONFIG_SMP)
	orangefs_inode_unlock(inode);
#endif
}

static inline unsigned int diff(struct timeval *end, struct timeval *begin)
{
	if (end->tv_usec < begin->tv_usec) {
		end->tv_usec += 1000000;
		end->tv_sec--;
	}
	end->tv_sec -= begin->tv_sec;
	end->tv_usec -= begin->tv_usec;
	return (end->tv_sec * 1000000) + end->tv_usec;
}

#endif /* __ORANGEFSKERNEL_H */
