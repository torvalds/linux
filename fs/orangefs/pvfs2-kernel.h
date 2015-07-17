/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/*
 *  The PVFS2 Linux kernel support allows PVFS2 volumes to be mounted and
 *  accessed through the Linux VFS (i.e. using standard I/O system calls).
 *  This support is only needed on clients that wish to mount the file system.
 *
 */

/*
 *  Declarations and macros for the PVFS2 Linux kernel support.
 */

#ifndef __PVFS2KERNEL_H
#define __PVFS2KERNEL_H

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

#ifdef PVFS2_KERNEL_DEBUG
#define PVFS2_DEFAULT_OP_TIMEOUT_SECS       10
#else
#define PVFS2_DEFAULT_OP_TIMEOUT_SECS       20
#endif

#define PVFS2_BUFMAP_WAIT_TIMEOUT_SECS      30

#define PVFS2_DEFAULT_SLOT_TIMEOUT_SECS     900	/* 15 minutes */

#define PVFS2_REQDEVICE_NAME          "pvfs2-req"

#define PVFS2_DEVREQ_MAGIC             0x20030529
#define PVFS2_LINK_MAX                 0x000000FF
#define PVFS2_PURGE_RETRY_COUNT        0x00000005
#define PVFS2_SEEK_END                 0x00000002
#define PVFS2_MAX_NUM_OPTIONS          0x00000004
#define PVFS2_MAX_MOUNT_OPT_LEN        0x00000080
#define PVFS2_MAX_FSKEY_LEN            64

#define MAX_DEV_REQ_UPSIZE (2*sizeof(__s32) +   \
sizeof(__u64) + sizeof(struct pvfs2_upcall_s))
#define MAX_DEV_REQ_DOWNSIZE (2*sizeof(__s32) + \
sizeof(__u64) + sizeof(struct pvfs2_downcall_s))

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
 * valid pvfs2 kernel operation states
 *
 * unknown  - op was just initialized
 * waiting  - op is on request_list (upward bound)
 * inprogr  - op is in progress (waiting for downcall)
 * serviced - op has matching downcall; ok
 * purged   - op has to start a timer since client-core
 *            exited uncleanly before servicing op
 */
enum pvfs2_vfs_op_states {
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
enum PVFS_async_io_type {
	PVFS_VFS_SYNC_IO = 0,
	PVFS_VFS_ASYNC_IO = 1,
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
 * pvfs2 kernel memory related flags
 */

#if ((defined PVFS2_KERNEL_DEBUG) && (defined CONFIG_DEBUG_SLAB))
#define PVFS2_CACHE_CREATE_FLAGS SLAB_RED_ZONE
#else
#define PVFS2_CACHE_CREATE_FLAGS 0
#endif /* ((defined PVFS2_KERNEL_DEBUG) && (defined CONFIG_DEBUG_SLAB)) */

#define PVFS2_CACHE_ALLOC_FLAGS (GFP_KERNEL)
#define PVFS2_GFP_FLAGS (GFP_KERNEL)
#define PVFS2_BUFMAP_GFP_FLAGS (GFP_KERNEL)

#define pvfs2_kmap(page) kmap(page)
#define pvfs2_kunmap(page) kunmap(page)

/* pvfs2 xattr and acl related defines */
#define PVFS2_XATTR_INDEX_POSIX_ACL_ACCESS  1
#define PVFS2_XATTR_INDEX_POSIX_ACL_DEFAULT 2
#define PVFS2_XATTR_INDEX_TRUSTED           3
#define PVFS2_XATTR_INDEX_DEFAULT           4

#if 0
#ifndef POSIX_ACL_XATTR_ACCESS
#define POSIX_ACL_XATTR_ACCESS	"system.posix_acl_access"
#endif
#ifndef POSIX_ACL_XATTR_DEFAULT
#define POSIX_ACL_XATTR_DEFAULT	"system.posix_acl_default"
#endif
#endif

#define PVFS2_XATTR_NAME_ACL_ACCESS  POSIX_ACL_XATTR_ACCESS
#define PVFS2_XATTR_NAME_ACL_DEFAULT POSIX_ACL_XATTR_DEFAULT
#define PVFS2_XATTR_NAME_TRUSTED_PREFIX "trusted."
#define PVFS2_XATTR_NAME_DEFAULT_PREFIX ""

/* these functions are defined in pvfs2-utils.c */
int orangefs_prepare_cdm_array(char *debug_array_string);
int orangefs_prepare_debugfs_help_string(int);

/* defined in pvfs2-debugfs.c */
int pvfs2_client_debug_init(void);

void debug_string_to_mask(char *, void *, int);
void do_c_mask(int, char *, struct client_debug_mask **);
void do_k_mask(int, char *, __u64 **);

void debug_mask_to_string(void *, int);
void do_k_string(void *, int);
void do_c_string(void *, int);
int check_amalgam_keyword(void *, int);
int keyword_is_amalgam(char *);

/*these variables are defined in pvfs2-mod.c */
extern char kernel_debug_string[PVFS2_MAX_DEBUG_STRING_LEN];
extern char client_debug_string[PVFS2_MAX_DEBUG_STRING_LEN];
extern char client_debug_array_string[PVFS2_MAX_DEBUG_STRING_LEN];
/* HELLO
extern struct client_debug_mask current_client_mask;
*/
extern unsigned int kernel_mask_set_mod_init;

extern int pvfs2_init_acl(struct inode *inode, struct inode *dir);
extern const struct xattr_handler *pvfs2_xattr_handlers[];

extern struct posix_acl *pvfs2_get_acl(struct inode *inode, int type);
extern int pvfs2_set_acl(struct inode *inode, struct posix_acl *acl, int type);

int pvfs2_xattr_set_default(struct dentry *dentry,
			    const char *name,
			    const void *buffer,
			    size_t size,
			    int flags,
			    int handler_flags);

int pvfs2_xattr_get_default(struct dentry *dentry,
			    const char *name,
			    void *buffer,
			    size_t size,
			    int handler_flags);

/*
 * Redefine xtvec structure so that we could move helper functions out of
 * the define
 */
struct xtvec {
	__kernel_off_t xtv_off;		/* must be off_t */
	__kernel_size_t xtv_len;	/* must be size_t */
};

/*
 * pvfs2 data structures
 */
struct pvfs2_kernel_op_s {
	enum pvfs2_vfs_op_states op_state;
	__u64 tag;

	/*
	 * Set uses_shared_memory to 1 if this operation uses shared memory.
	 * If true, then a retry on the op must also get a new shared memory
	 * buffer and re-populate it.
	 */
	int uses_shared_memory;

	struct pvfs2_upcall_s upcall;
	struct pvfs2_downcall_s downcall;

	wait_queue_head_t waitq;
	spinlock_t lock;

	int io_completed;
	wait_queue_head_t io_completion_waitq;

	/*
	 * upcalls requiring variable length trailers require that this struct
	 * be in the request list even after client-core does a read() on the
	 * device to dequeue the upcall.
	 * if op_linger field goes to 0, we dequeue this op off the list.
	 * else we let it stay. What gets passed to the read() is
	 * a) if op_linger field is = 1, pvfs2_kernel_op_s itself
	 * b) else if = 0, we pass ->upcall.trailer_buf
	 * We expect to have only a single upcall trailer buffer,
	 * so we expect callers with trailers
	 * to set this field to 2 and others to set it to 1.
	 */
	__s32 op_linger, op_linger_tmp;
	/* VFS aio fields */

	/* used by the async I/O code to stash the pvfs2_kiocb_s structure */
	void *priv;

	/* used again for the async I/O code for deallocation */
	atomic_t aio_ref_count;

	int attempts;

	struct list_head list;
};

/* per inode private pvfs2 info */
struct pvfs2_inode_s {
	struct pvfs2_object_kref refn;
	char link_target[PVFS_NAME_MAX];
	__s64 blksize;
	/*
	 * Reading/Writing Extended attributes need to acquire the appropriate
	 * reader/writer semaphore on the pvfs2_inode_s structure.
	 */
	struct rw_semaphore xattr_sem;

	struct inode vfs_inode;
	sector_t last_failed_block_index_read;

	/*
	 * State of in-memory attributes not yet flushed to disk associated
	 * with this object
	 */
	unsigned long pinode_flags;

	/* All allocated pvfs2_inode_s objects are chained to a list */
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

/* per superblock private pvfs2 info */
struct pvfs2_sb_info_s {
	struct pvfs2_khandle root_khandle;
	__s32 fs_id;
	int id;
	int flags;
#define PVFS2_OPT_INTR		0x01
#define PVFS2_OPT_LOCAL_LOCK	0x02
	char devname[PVFS_MAX_SERVER_ADDR_LEN];
	struct super_block *sb;
	int mount_pending;
	struct list_head list;
};

/*
 * a temporary structure used only for sb mount time that groups the
 * mount time data provided along with a private superblock structure
 * that is allocated before a 'kernel' superblock is allocated.
*/
struct pvfs2_mount_sb_info_s {
	void *data;
	struct pvfs2_khandle root_khandle;
	__s32 fs_id;
	int id;
};

/*
 * structure that holds the state of any async I/O operation issued
 * through the VFS. Needed especially to handle cancellation requests
 * or even completion notification so that the VFS client-side daemon
 * can free up its vfs_request slots.
 */
struct pvfs2_kiocb_s {
	/* the pointer to the task that initiated the AIO */
	struct task_struct *tsk;

	/* pointer to the kiocb that kicked this operation */
	struct kiocb *kiocb;

	/* buffer index that was used for the I/O */
	struct pvfs2_bufmap *bufmap;
	int buffer_index;

	/* pvfs2 kernel operation type */
	struct pvfs2_kernel_op_s *op;

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

struct pvfs2_stats {
	unsigned long cache_hits;
	unsigned long cache_misses;
	unsigned long reads;
	unsigned long writes;
};

extern struct pvfs2_stats g_pvfs2_stats;

/*
  NOTE: See Documentation/filesystems/porting for information
  on implementing FOO_I and properly accessing fs private data
*/
static inline struct pvfs2_inode_s *PVFS2_I(struct inode *inode)
{
	return container_of(inode, struct pvfs2_inode_s, vfs_inode);
}

static inline struct pvfs2_sb_info_s *PVFS2_SB(struct super_block *sb)
{
	return (struct pvfs2_sb_info_s *) sb->s_fs_info;
}

/* ino_t descends from "unsigned long", 8 bytes, 64 bits. */
static inline ino_t pvfs2_khandle_to_ino(struct pvfs2_khandle *khandle)
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

static inline struct pvfs2_khandle *get_khandle_from_ino(struct inode *inode)
{
	return &(PVFS2_I(inode)->refn.khandle);
}

static inline __s32 get_fsid_from_ino(struct inode *inode)
{
	return PVFS2_I(inode)->refn.fs_id;
}

static inline ino_t get_ino_from_khandle(struct inode *inode)
{
	struct pvfs2_khandle *khandle;
	ino_t ino;

	khandle = get_khandle_from_ino(inode);
	ino = pvfs2_khandle_to_ino(khandle);
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
		     &PVFS2_SB(inode->i_sb)->root_khandle,
		     get_khandle_from_ino(inode));

	if (PVFS_khandle_cmp(&(PVFS2_SB(inode->i_sb)->root_khandle),
			     get_khandle_from_ino(inode)))
		return 0;
	else
		return 1;
}

static inline int match_handle(struct pvfs2_khandle resp_handle,
			       struct inode *inode)
{
	gossip_debug(GOSSIP_DCACHE_DEBUG,
		     "%s: one handle: %pU, another handle:%pU:\n",
		     __func__,
		     &resp_handle,
		     get_khandle_from_ino(inode));

	if (PVFS_khandle_cmp(&resp_handle, get_khandle_from_ino(inode)))
		return 0;
	else
		return 1;
}

/*
 * defined in pvfs2-cache.c
 */
int op_cache_initialize(void);
int op_cache_finalize(void);
struct pvfs2_kernel_op_s *op_alloc(__s32 type);
struct pvfs2_kernel_op_s *op_alloc_trailer(__s32 type);
char *get_opname_string(struct pvfs2_kernel_op_s *new_op);
void op_release(struct pvfs2_kernel_op_s *op);

int dev_req_cache_initialize(void);
int dev_req_cache_finalize(void);
void *dev_req_alloc(void);
void dev_req_release(void *);

int pvfs2_inode_cache_initialize(void);
int pvfs2_inode_cache_finalize(void);

int kiocb_cache_initialize(void);
int kiocb_cache_finalize(void);
struct pvfs2_kiocb_s *kiocb_alloc(void);
void kiocb_release(struct pvfs2_kiocb_s *ptr);

/*
 * defined in pvfs2-mod.c
 */
void purge_inprogress_ops(void);

/*
 * defined in waitqueue.c
 */
int wait_for_matching_downcall(struct pvfs2_kernel_op_s *op);
int wait_for_cancellation_downcall(struct pvfs2_kernel_op_s *op);
void pvfs2_clean_up_interrupted_operation(struct pvfs2_kernel_op_s *op);
void purge_waiting_ops(void);

/*
 * defined in super.c
 */
struct dentry *pvfs2_mount(struct file_system_type *fst,
			   int flags,
			   const char *devname,
			   void *data);

void pvfs2_kill_sb(struct super_block *sb);
int pvfs2_remount(struct super_block *sb);

int fsid_key_table_initialize(void);
void fsid_key_table_finalize(void);

/*
 * defined in inode.c
 */
__u32 convert_to_pvfs2_mask(unsigned long lite_mask);
struct inode *pvfs2_new_inode(struct super_block *sb,
			      struct inode *dir,
			      int mode,
			      dev_t dev,
			      struct pvfs2_object_kref *ref);

int pvfs2_setattr(struct dentry *dentry, struct iattr *iattr);

int pvfs2_getattr(struct vfsmount *mnt,
		  struct dentry *dentry,
		  struct kstat *kstat);

/*
 * defined in xattr.c
 */
int pvfs2_setxattr(struct dentry *dentry,
		   const char *name,
		   const void *value,
		   size_t size,
		   int flags);

ssize_t pvfs2_getxattr(struct dentry *dentry,
		       const char *name,
		       void *buffer,
		       size_t size);

ssize_t pvfs2_listxattr(struct dentry *dentry, char *buffer, size_t size);

/*
 * defined in namei.c
 */
struct inode *pvfs2_iget(struct super_block *sb,
			 struct pvfs2_object_kref *ref);

ssize_t pvfs2_inode_read(struct inode *inode,
			 char *buf,
			 size_t count,
			 loff_t *offset,
			 loff_t readahead_size);

/*
 * defined in devpvfs2-req.c
 */
int pvfs2_dev_init(void);
void pvfs2_dev_cleanup(void);
int is_daemon_in_service(void);
int fs_mount_pending(__s32 fsid);

/*
 * defined in pvfs2-utils.c
 */
__s32 fsid_of_op(struct pvfs2_kernel_op_s *op);

int pvfs2_flush_inode(struct inode *inode);

ssize_t pvfs2_inode_getxattr(struct inode *inode,
			     const char *prefix,
			     const char *name,
			     void *buffer,
			     size_t size);

int pvfs2_inode_setxattr(struct inode *inode,
			 const char *prefix,
			 const char *name,
			 const void *value,
			 size_t size,
			 int flags);

int pvfs2_inode_getattr(struct inode *inode, __u32 mask);

int pvfs2_inode_setattr(struct inode *inode, struct iattr *iattr);

void pvfs2_op_initialize(struct pvfs2_kernel_op_s *op);

void pvfs2_make_bad_inode(struct inode *inode);

void mask_blocked_signals(sigset_t *orig_sigset);

void unmask_blocked_signals(sigset_t *orig_sigset);

int pvfs2_unmount_sb(struct super_block *sb);

int pvfs2_cancel_op_in_progress(__u64 tag);

__u64 pvfs2_convert_time_field(void *time_ptr);

int pvfs2_normalize_to_errno(__s32 error_code);

extern struct mutex devreq_mutex;
extern struct mutex request_mutex;
extern int debug;
extern int op_timeout_secs;
extern int slot_timeout_secs;
extern struct list_head pvfs2_superblocks;
extern spinlock_t pvfs2_superblocks_lock;
extern struct list_head pvfs2_request_list;
extern spinlock_t pvfs2_request_list_lock;
extern wait_queue_head_t pvfs2_request_list_waitq;
extern struct list_head *htable_ops_in_progress;
extern spinlock_t htable_ops_in_progress_lock;
extern int hash_table_size;

extern const struct address_space_operations pvfs2_address_operations;
extern struct backing_dev_info pvfs2_backing_dev_info;
extern struct inode_operations pvfs2_file_inode_operations;
extern const struct file_operations pvfs2_file_operations;
extern struct inode_operations pvfs2_symlink_inode_operations;
extern struct inode_operations pvfs2_dir_inode_operations;
extern const struct file_operations pvfs2_dir_operations;
extern const struct dentry_operations pvfs2_dentry_operations;
extern const struct file_operations pvfs2_devreq_file_operations;

extern wait_queue_head_t pvfs2_bufmap_init_waitq;

/*
 * misc convenience macros
 */
#define add_op_to_request_list(op)				\
do {								\
	spin_lock(&pvfs2_request_list_lock);			\
	spin_lock(&op->lock);					\
	set_op_state_waiting(op);				\
	list_add_tail(&op->list, &pvfs2_request_list);		\
	spin_unlock(&pvfs2_request_list_lock);			\
	spin_unlock(&op->lock);					\
	wake_up_interruptible(&pvfs2_request_list_waitq);	\
} while (0)

#define add_priority_op_to_request_list(op)				\
	do {								\
		spin_lock(&pvfs2_request_list_lock);			\
		spin_lock(&op->lock);					\
		set_op_state_waiting(op);				\
									\
		list_add(&op->list, &pvfs2_request_list);		\
		spin_unlock(&pvfs2_request_list_lock);			\
		spin_unlock(&op->lock);					\
		wake_up_interruptible(&pvfs2_request_list_waitq);	\
} while (0)

#define remove_op_from_request_list(op)					\
	do {								\
		struct list_head *tmp = NULL;				\
		struct list_head *tmp_safe = NULL;			\
		struct pvfs2_kernel_op_s *tmp_op = NULL;		\
									\
		spin_lock(&pvfs2_request_list_lock);			\
		list_for_each_safe(tmp, tmp_safe, &pvfs2_request_list) { \
			tmp_op = list_entry(tmp,			\
					    struct pvfs2_kernel_op_s,	\
					    list);			\
			if (tmp_op && (tmp_op == op)) {			\
				list_del(&tmp_op->list);		\
				break;					\
			}						\
		}							\
		spin_unlock(&pvfs2_request_list_lock);			\
	} while (0)

#define PVFS2_OP_INTERRUPTIBLE 1   /* service_operation() is interruptible */
#define PVFS2_OP_PRIORITY      2   /* service_operation() is high priority */
#define PVFS2_OP_CANCELLATION  4   /* this is a cancellation */
#define PVFS2_OP_NO_SEMAPHORE  8   /* don't acquire semaphore */
#define PVFS2_OP_ASYNC         16  /* Queue it, but don't wait */

int service_operation(struct pvfs2_kernel_op_s *op,
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
 * if a pvfs2 sysint level error occured and i/o has been completed,
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
		pvfs2_cancel_op_in_progress(new_op->tag);	\
		op_release(new_op);				\
	} else {						\
		wake_up_daemon_for_return(new_op);		\
	}							\
	new_op = NULL;						\
	pvfs_bufmap_put(bufmap, buffer_index);				\
	buffer_index = -1;					\
} while (0)

#define get_interruptible_flag(inode) \
	((PVFS2_SB(inode->i_sb)->flags & PVFS2_OPT_INTR) ? \
		PVFS2_OP_INTERRUPTIBLE : 0)

#define add_pvfs2_sb(sb)						\
do {									\
	gossip_debug(GOSSIP_SUPER_DEBUG,				\
		     "Adding SB %p to pvfs2 superblocks\n",		\
		     PVFS2_SB(sb));					\
	spin_lock(&pvfs2_superblocks_lock);				\
	list_add_tail(&PVFS2_SB(sb)->list, &pvfs2_superblocks);		\
	spin_unlock(&pvfs2_superblocks_lock); \
} while (0)

#define remove_pvfs2_sb(sb)						\
do {									\
	struct list_head *tmp = NULL;					\
	struct list_head *tmp_safe = NULL;				\
	struct pvfs2_sb_info_s *pvfs2_sb = NULL;			\
									\
	spin_lock(&pvfs2_superblocks_lock);				\
	list_for_each_safe(tmp, tmp_safe, &pvfs2_superblocks) {		\
		pvfs2_sb = list_entry(tmp,				\
				      struct pvfs2_sb_info_s,		\
				      list);				\
		if (pvfs2_sb && (pvfs2_sb->sb == sb)) {			\
			gossip_debug(GOSSIP_SUPER_DEBUG,		\
			    "Removing SB %p from pvfs2 superblocks\n",	\
			pvfs2_sb);					\
			list_del(&pvfs2_sb->list);			\
			break;						\
		}							\
	}								\
	spin_unlock(&pvfs2_superblocks_lock);				\
} while (0)

#define pvfs2_lock_inode(inode) spin_lock(&inode->i_lock)
#define pvfs2_unlock_inode(inode) spin_unlock(&inode->i_lock)
#define pvfs2_current_signal_lock current->sighand->siglock
#define pvfs2_current_sigaction current->sighand->action

#define fill_default_sys_attrs(sys_attr, type, mode)			\
do {									\
	sys_attr.owner = from_kuid(current_user_ns(), current_fsuid()); \
	sys_attr.group = from_kgid(current_user_ns(), current_fsgid()); \
	sys_attr.size = 0;						\
	sys_attr.perms = PVFS_util_translate_mode(mode);		\
	sys_attr.objtype = type;					\
	sys_attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;			\
} while (0)

#define pvfs2_inode_lock(__i)  mutex_lock(&(__i)->i_mutex)

#define pvfs2_inode_unlock(__i) mutex_unlock(&(__i)->i_mutex)

static inline void pvfs2_i_size_write(struct inode *inode, loff_t i_size)
{
#if BITS_PER_LONG == 32 && defined(CONFIG_SMP)
	pvfs2_inode_lock(inode);
#endif
	i_size_write(inode, i_size);
#if BITS_PER_LONG == 32 && defined(CONFIG_SMP)
	pvfs2_inode_unlock(inode);
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

#endif /* __PVFS2KERNEL_H */
